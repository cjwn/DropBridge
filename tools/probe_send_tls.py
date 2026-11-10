#!/usr/bin/env python3
"""Diagnostic-only TLS probe for DropBridge outbound proxy.

Does not send /Discover, /Ask or files. Never logs private keys, certificate
PEM, or connection payloads. Run with an iPhone you own and AirDrop enabled.
"""
import argparse
import hashlib
import socket
import ssl
from pathlib import Path


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--address", required=True, help="iPhone AWDL link-local IPv6 address from mDNS")
    parser.add_argument("--port", type=int, required=True, help="iPhone AirDrop SRV port from mDNS (not assumed 8771)")
    parser.add_argument("--keys", type=Path, required=True, help="DropBridge data/keys directory")
    parser.add_argument("--proxy-port", type=int, default=8772)
    parser.add_argument("--max-tls", choices=["default", "1.2"], default="default",
                        help="Use 1.2 as an A/B compatibility probe, not a production recommendation")
    parser.add_argument("--omit-client-cert", action="store_true",
                        help="A/B test only: see whether server requires a client certificate")
    args = parser.parse_args()
    address = args.address.strip().split("%")[0]
    try:
        socket.inet_pton(socket.AF_INET6, address)
    except OSError:
        parser.error("--address must be an IPv6 literal")
    if not 1 <= args.port <= 65535:
        parser.error("--port must be between 1 and 65535")

    ctx = ssl.SSLContext(ssl.PROTOCOL_TLS_CLIENT)
    ctx.check_hostname = False
    ctx.verify_mode = ssl.CERT_NONE  # Research only: do not assume the server is trusted.
    if args.max_tls == "1.2":
        ctx.maximum_version = ssl.TLSVersion.TLSv1_2

    if not args.omit_client_cert:
        cert = args.keys / "certificate.self-signed.pem"
        key = args.keys / "key.self-signed.pem"
        if not cert.is_file() or not key.is_file():
            parser.error("certificate/key missing in --keys")
        ctx.load_cert_chain(str(cert), str(key))
        pem = cert.read_text(encoding="ascii")
        der = ssl.PEM_cert_to_DER_cert(pem)
        print("Local client certificate SHA-256:", hashlib.sha256(der).hexdigest())
        # Fingerprint is diagnostic only; never print private key material.
    print("TLS OpenSSL:", ssl.OPENSSL_VERSION)
    print("TLS maximum:", ctx.maximum_version.name)
    print("Client cert sent if requested:", not args.omit_client_cert)
    print("Target:", address, "port", args.port)

    try:
        with socket.create_connection(("127.0.0.1", args.proxy_port), timeout=10) as raw:
            raw.settimeout(12)
            raw.sendall(f"CONNECT {address} {args.port}\n".encode("ascii"))
            with ctx.wrap_socket(raw, server_hostname=None) as secure:
                print("RESULT: HANDSHAKE_SUCCEEDED")
                print("Negotiated version:", secure.version())
                print("Cipher:", secure.cipher())
                peer = secure.getpeercert(binary_form=True)
                if peer:
                    print("Remote certificate SHA-256:", hashlib.sha256(peer).hexdigest())
                print("No files or HTTP requests were sent.")
                return 0
    except ssl.SSLError as exc:
        print("RESULT: TLS_ERROR")
        print("Python SSL reason:", getattr(exc, "reason", None))
        print("Python SSL library:", getattr(exc, "library", None))
        print("Details:", str(exc))
        return 2
    except (OSError, TimeoutError) as exc:
        print("RESULT: NETWORK_OR_PROXY_ERROR")
        print("Details:", type(exc).__name__, str(exc))
        return 3


if __name__ == "__main__":
    raise SystemExit(main())
