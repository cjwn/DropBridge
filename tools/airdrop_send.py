#!/usr/bin/env python3
"""Send files from this PC to an iPhone over AirDrop, through awdl_netd.

Why this exists: netd only accepted *inbound* connections (phone -> PC). The
outbound proxy (opt-in, `AWDL_NETD_OUTBOUND`) lets a local process name the far
end and then speaks the normal AirDrop protocol over it. The protocol itself is
reused from upstream `airdrop/sender.py` (MIT); this file only replaces its two
transport assumptions:

  * discovery: the interface browser is replaced by queries through netd's mDNS
    proxy (127.0.0.1:15353), which already carries AWDL multicast;
  * connection: the Windows link-local 4-tuple connect is replaced by a local
    connection to netd's outbound proxy plus a `CONNECT <ipv6> <port>` line.

The receiver still decides whether to take the file: an iPhone set to
"Receiving Off" or "Contacts Only" answers with a certificate alert or not at
all. "Everyone for 10 Minutes" is the configuration this tool is tested with.
"""
from __future__ import annotations

import argparse
import http.client
import logging
import socket
import sys
import time
from pathlib import Path

REPO = Path(__file__).resolve().parents[1]
AIRDROP = REPO / 'upstream' / 'winawdl' / 'airdrop'
sys.path.insert(0, str(AIRDROP))

import identity  # noqa: E402  (upstream modules live on the path above)
import mdns  # noqa: E402
import sender  # noqa: E402

SERVICE_TYPE = '_airdrop._tcp.local'
DEFAULT_PROXY_PORT = 8772
DEFAULT_MDNS_PORT = 15353
CONNECT_TIMEOUT = 10


class ProxyBrowser:
    """Receiver discovery through netd's mDNS proxy instead of an interface."""

    def __init__(self, port=DEFAULT_MDNS_PORT, service_type=SERVICE_TYPE):
        self.port = port
        self.instances = mdns.Instances(service_type)
        self.heard = 0
        self.sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        self.sock.bind(('127.0.0.1', 0))
        self.sock.settimeout(0.3)
        self._next_ask = 0.0

    def poll(self):
        now = time.monotonic()
        if now >= self._next_ask:
            self.sock.sendto(mdns.query(self.instances.questions()), ('127.0.0.1', self.port))
            self._next_ask = now + 1.0
        deadline = now + 0.5
        while time.monotonic() < deadline:
            try:
                data, _ = self.sock.recvfrom(9000)
            except socket.timeout:
                break
            self.instances.feed(data, None)
            self.heard += 1
        return self.instances.devices(within=30)

    def close(self):
        self.sock.close()


class ProxyConnection(http.client.HTTPConnection):
    """HTTPS to a link-local receiver, tunnelled through netd's outbound proxy."""

    proxy_port = DEFAULT_PROXY_PORT

    def __init__(self, address, port, scope, context, timeout):
        super().__init__(address, port, timeout=timeout)
        self.context = context

    def connect(self):
        sock = socket.create_connection(('127.0.0.1', self.proxy_port), timeout=self.timeout)
        try:
            sock.settimeout(self.timeout)
            sock.sendall(f'CONNECT {self.host} {self.port}\n'.encode('ascii'))
            self.sock = self.context.wrap_socket(sock)
        except OSError:
            sock.close()
            raise


def parse_args(argv=None):
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('files', nargs='+', help='files to send')
    parser.add_argument('--to', help='part of the receiver name, when several answer')
    parser.add_argument('--address',
                        help='send straight to this IPv6 (skips discovery; the port defaults '
                             'to 8771). Use when the phone answers nothing but its mDNS queries '
                             'show its link-local address in the netd log.')
    parser.add_argument('--search', type=int, default=20,
                        help='seconds to look for receivers (default 20)')
    parser.add_argument('--name', default=socket.gethostname().split('.')[0],
                        help='name the phone shows in its prompt')
    parser.add_argument('--keys', default=str(REPO / 'dist' / 'data' / 'keys'),
                        help='directory with certificate.self-signed.pem and the key')
    parser.add_argument('--proxy-port', type=int, default=DEFAULT_PROXY_PORT)
    parser.add_argument('--mdns-port', type=int, default=DEFAULT_MDNS_PORT)
    parser.add_argument('--trace', action='store_true')
    parser.add_argument('--list', action='store_true',
                        help='only list what answers, send nothing')
    return parser.parse_args(argv)


def main(argv=None):
    settings = parse_args(argv)
    logging.basicConfig(level=logging.DEBUG if settings.trace else logging.INFO,
                        stream=sys.stdout,
                        format='%(asctime)s %(levelname)s %(message)s')
    ProxyConnection.proxy_port = settings.proxy_port
    sender.Connection = ProxyConnection      # every sender helper now tunnels
    try:
        context = sender.client_context(Path(settings.keys))
    except identity.IdentityError as error:
        raise SystemExit(str(error)) from None
    files = sender.outgoing(settings.files)

    if settings.address:
        # Direct path: discovery is skipped because the caller already knows the
        # link-local address (for example from netd's mDNS source log).
        address = settings.address.split('%')[0]
        logging.info('sending straight to %s; accept on the phone', address)
        try:
            ok = sender.send(address, 8771, None, context, settings.name, files)
        except TimeoutError:
            logging.warning('%s did not answer in time', address)
            return 1
        except sender.Unwilling:
            logging.warning('%s refused this computer\'s certificate: the iPhone is probably '
                            'set to contacts only', address)
            return 1
        except (OSError, http.client.HTTPException) as error:
            logging.warning('the connection to %s broke: %s', address,
                            error or type(error).__name__)
            return 1
        if ok:
            logging.info('sent %d file(s) to %s', len(files), address)
            return 0
        logging.warning('the phone did not take the files')
        return 1

    browser = ProxyBrowser(settings.mdns_port)
    try:
        named = sender.find_receivers(browser, None, context, settings.to, settings.search)
        logging.info('%d mDNS packets came back through the proxy', browser.heard)
    finally:
        browser.close()
    if not named:
        logging.warning('no receiver answered. On the iPhone open AirDrop and choose '
                        '"Everyone for 10 Minutes", then keep the share sheet open.')
        return 2
    for name, (address, port) in sorted(named.items()):
        logging.info('candidate: %s at %s port %d', name, address, port)
    if settings.list:
        return 0

    name, (address, port) = sender.choose(named, settings.to)
    logging.info('asking %s at %s; accept on the phone', name, address)
    try:
        ok = sender.send(address, port, None, context, settings.name, files)
    except TimeoutError:
        logging.warning('%s did not answer in time', name)
        return 1
    except sender.Unwilling:
        logging.warning('%s refused this computer\'s certificate: the iPhone is probably '
                        'set to contacts only', name)
        return 1
    except (OSError, http.client.HTTPException) as error:
        logging.warning('the connection to %s broke: %s', name, error or type(error).__name__)
        return 1
    if ok:
        logging.info('sent %d file(s) to %s', len(files), name)
        return 0
    logging.warning('the phone did not take the files')
    return 1


if __name__ == '__main__':
    sys.exit(main())
