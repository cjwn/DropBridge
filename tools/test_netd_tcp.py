"""Real lwIP TCP over loopback IPC; no radio, phone or external network.

The tiny IPv6/TCP peer deliberately uses fixed test addresses and a fixed
initial sequence. It checks transport/proxy boundaries, not AirDrop/TLS.
"""
import argparse
import hashlib
import ipaddress
import os
import queue
import re
import socket
import struct
import subprocess
import threading
import time
from pathlib import Path

REMOTE = ipaddress.IPv6Address('fe80::2').packed
LOCAL = ipaddress.IPv6Address('fe80::200:ff:fe00:1').packed


def checksum(data):
    if len(data) % 2:
        data += b'\0'
    total = sum(struct.unpack(f'!{len(data) // 2}H', data))
    while total >> 16:
        total = (total & 65535) + (total >> 16)
    return (~total) & 65535


class Peer:
    def __init__(self, sock):
        self.sock = sock
        self.sock.settimeout(0.25)
        self.lock = threading.Lock()
        self.cv = threading.Condition()
        self.seq, self.acked, self.next_rx = 1001, 1000, 0
        self.window, self.scale = 0, 0
        self.response = bytearray()
        self.syn = self.fin = self.rst = self.closed = False
        self.error = None
        self.thread = threading.Thread(target=self.read, daemon=True)
        self.thread.start()
        self.send(2, seq=1000, options=b'\x02\x04\x05\x50\x03\x03\x00\x00')
        self.wait(lambda: self.syn and self.acked == 1001, 'SYN/ACK')

    def wait(self, predicate, what, timeout=10):
        with self.cv:
            ok = self.cv.wait_for(lambda: predicate() or self.error or self.rst or self.closed, timeout)
            if self.closed:
                raise ConnectionError('test peer closed')
            if self.error:
                raise self.error
            if self.rst:
                raise ConnectionError('lwIP reset the test connection')
            assert ok and predicate(), f'timed out waiting for {what}'

    def send(self, flags, payload=b'', seq=None, options=b''):
        with self.lock:
            tcp = struct.pack('!HHIIBBHHH', 50000, 8771, self.seq if seq is None else seq,
                              self.next_rx, (5 + len(options) // 4) << 4, flags, 65535, 0, 0)
            tcp += options + payload
            pseudo = REMOTE + LOCAL + struct.pack('!I3xB', len(tcp), 6)
            tcp = tcp[:16] + struct.pack('!H', checksum(pseudo + tcp)) + tcp[18:]
            ip = struct.pack('!IHBB', 6 << 28, len(tcp), 6, 64) + REMOTE + LOCAL + tcp
            self.sock.sendall(struct.pack('<H', len(ip)) + ip)

    def exact(self, n):
        result = bytearray()
        while len(result) < n and not self.closed:
            try:
                data = self.sock.recv(n - len(result))
            except socket.timeout:
                continue
            if not data:
                raise EOFError('netd IPC closed')
            result.extend(data)
        return bytes(result)

    def read(self):
        try:
            while not self.closed:
                header = self.exact(2)
                if self.closed:
                    return
                packet = self.exact(struct.unpack('<H', header)[0])
                if len(packet) < 60 or packet[6] != 6:
                    continue
                end = 40 + struct.unpack_from('!H', packet, 4)[0]
                tcp = packet[40:end]
                if struct.unpack_from('!HH', tcp) != (8771, 50000):
                    continue
                pseudo = packet[8:40] + struct.pack('!I3xB', len(tcp), 6)
                assert checksum(pseudo + tcp) == 0, 'bad real lwIP TCP checksum'
                seq, ack, offset, flags, window = struct.unpack_from('!IIBBH', tcp, 4)
                payload = tcp[(offset >> 4) * 4:]
                with self.cv:
                    if flags & 4:
                        self.rst = True
                    if flags & 2:
                        self.syn = True
                        self.next_rx = seq + 1
                        options = tcp[20:(offset >> 4) * 4]
                        i = 0
                        while i < len(options) and options[i]:
                            if options[i] == 1:
                                i += 1
                                continue
                            size = options[i + 1]
                            if options[i] == 3:
                                self.scale = min(options[i + 2], 14)
                            i += size
                    if flags & 16:
                        self.acked = max(self.acked, ack)
                        self.window = window if flags & 2 else window << self.scale
                    if payload and seq == self.next_rx:
                        self.response.extend(payload)
                        self.next_rx += len(payload)
                    if flags & 1 and seq + len(payload) == self.next_rx:
                        self.next_rx += 1
                        self.fin = True
                    self.cv.notify_all()
                if flags & 3 or payload:
                    self.send(16)
        except Exception as error:
            if not self.closed:
                with self.cv:
                    self.error = error
                    self.cv.notify_all()

    def upload(self, data, stop=None):
        offset = 0
        while offset < len(data):
            if stop and stop.is_set():
                return
            # Bound the in-flight burst so this test client does not itself
            # overflow lwIP's small input mailbox. It is not a throughput tool.
            def available():
                return min(self.acked + self.window - self.seq, 4096 - (self.seq - self.acked))
            self.wait(lambda: available() > 0 or (stop and stop.is_set()), 'receive window')
            if stop and stop.is_set():
                return
            payload = data[offset:offset + min(1024, available())]
            seq = self.seq
            self.seq += len(payload)
            offset += len(payload)
            self.send(24, payload, seq=seq)
        self.wait(lambda: self.acked >= self.seq, 'upload ACK')

    def finish(self):
        seq = self.seq
        self.seq += 1
        self.send(17, seq=seq)

    def reset(self):
        self.send(20, seq=self.acked)

    def close(self):
        with self.cv:
            self.closed = True
            self.cv.notify_all()
        self.sock.close()
        self.thread.join(timeout=2)


class Run:
    def __init__(self, exe, name):
        self.work = Path('build/diagnostics/tcp-diag') / f'{name}-{time.time_ns()}'
        self.work.mkdir(parents=True)
        self.lines, self.events = [], queue.Queue()
        self.server = socket.socket()
        self.server.setsockopt(socket.SOL_SOCKET, socket.SO_EXCLUSIVEADDRUSE, 1)
        self.server.setsockopt(socket.SOL_SOCKET, socket.SO_RCVBUF, 1024)
        self.server.bind(('127.0.0.1', 8771))  # Fail before starting anything if occupied.
        self.server.listen(1)
        self.server.settimeout(5)
        env = os.environ.copy()
        for key in ('AWDL_NETD_MDNS_PROXY', 'AWDL_NETD_NATIVE_MDNS'):
            env.pop(key, None)
        env['AWDL_NETD_ADDR_FILE'] = str((self.work / 'mock.addr').resolve())
        self.process = subprocess.Popen([str(exe)], cwd=self.work, env=env,
                                        stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                                        text=True, encoding='utf-8', errors='replace')
        self.reader = threading.Thread(target=self.output, daemon=True)
        self.reader.start()
        self.peer = None
        try:
            self.wait_line(lambda line: 'waiting for awdld on 127.0.0.1:49877' in line)
            sock = socket.create_connection(('127.0.0.1', 49877), timeout=2)
            sock.sendall(b'AWDL' + LOCAL)
            self.wait_line(lambda line: 'TCP 8771 -> 127.0.0.1:8771 ready' in line)
            self.peer = Peer(sock)
            self.local, _ = self.server.accept()
            self.local.settimeout(12)
        except BaseException:
            self.close()
            raise

    def output(self):
        for line in self.process.stdout:
            self.lines.append(line)
            self.events.put(line)

    def wait_line(self, predicate, timeout=12):
        for line in self.lines:
            if predicate(line):
                return line
        deadline = time.monotonic() + timeout
        while True:
            assert self.process.poll() is None, ''.join(self.lines)
            line = self.events.get(timeout=max(0.01, deadline - time.monotonic()))
            if predicate(line):
                return line
            assert time.monotonic() < deadline, 'diagnostic event timeout'

    def close(self):
        if self.peer:
            self.peer.close()
        if hasattr(self, 'local'):
            self.local.close()
        self.server.close()
        if self.process.poll() is None:
            try:
                self.process.wait(timeout=2)
            except subprocess.TimeoutExpired:
                self.process.kill()  # Only our test child.
        self.process.wait(timeout=3)
        self.reader.join(timeout=1)
        output = ''.join(self.lines)
        (self.work / 'netd.log').write_text(output, encoding='utf-8')
        print(output.rstrip())


def roundtrip(exe):
    run = Run(exe, 'roundtrip')
    errors, received = [], bytearray()
    data = bytes(range(256)) * 256
    response = b'loopback-response' * 200

    def receiver():
        try:
            time.sleep(2.2)  # Cross two receive polls without treating healthy idle as failure.
            while True:
                chunk = run.local.recv(4096)
                if not chunk:
                    break
                received.extend(chunk)
            run.local.sendall(response)
            run.local.shutdown(socket.SHUT_WR)
        except Exception as error:
            errors.append(error)

    worker = threading.Thread(target=receiver, daemon=True)
    worker.start()
    try:
        run.peer.upload(data)
        run.peer.finish()  # Half-close must still allow a response.
        run.peer.wait(lambda: run.peer.fin, 'response and FIN')
        worker.join(timeout=3)
        assert not worker.is_alive() and not errors, errors
        assert received == data and run.peer.response == response, 'proxy byte mismatch'
        run.wait_line(lambda s: 'proxy flow=' in s and 'active=0' in s)
        output = ''.join(run.lines)
        assert f'up_read={len(data)} up_written={len(data)} down_read={len(response)} down_written={len(response)}' in output
        assert not re.search(r'(?:alloc_fail|input_fail|tx_fail)=[1-9]', output)
        print(f'PASS: real lwIP handshake, slow reader, {len(data)} upload bytes, {len(response)} response bytes, half-close cleanup; SHA256={hashlib.sha256(received).hexdigest()}')
    finally:
        run.close()
        worker.join(timeout=2)


def pressure(exe, expect_stuck=False):
    run = Run(exe, 'nonreading-reset')
    errors, stop = [], threading.Event()

    def upload():
        try:
            run.peer.upload(bytes(8 * 1024 * 1024), stop)
        except (AssertionError, ConnectionError, EOFError, OSError) as error:
            errors.append(str(error))

    sender = threading.Thread(target=upload, daemon=True)
    sender.start()
    try:
        # Socket backpressure need not produce a zero TCP window: lwIP can
        # refuse delivery to a full receive mailbox before its byte window fills.
        run.peer.wait(lambda: run.peer.acked > 65536, 'initial upload progress')
        last_ack, since = run.peer.acked, time.monotonic()
        deadline = time.monotonic() + 8
        while time.monotonic() - since < 0.5:
            assert time.monotonic() < deadline, 'receiver did not exert backpressure'
            time.sleep(0.02)
            if run.peer.acked != last_ack:
                last_ack, since = run.peer.acked, time.monotonic()
        assert run.peer.seq > run.peer.acked, 'no outstanding TCP data at stall'
        stop.set()
        with run.peer.cv:
            run.peer.cv.notify_all()
        run.peer.reset()
        if expect_stuck:
            began = time.monotonic()
            # Consume new snapshots until one actually observes the stuck state
            # >6 s after reset; a stale pre-reset line cannot establish this.
            while True:
                line = run.events.get(timeout=12)
                assert 'proxy flow=' not in line or 'active=0' not in line, 'negative control unexpectedly cleaned up'
                if ('up_phase=local_send' in line and 'active=1' in line
                        and time.monotonic() - began > 6):
                    break
            assert any('dir=rx' in s and 'rst=1' in s for s in run.lines), 'no peer reset observed'
            print(f'EXPECTED DEFECT: non-reading local receiver + peer reset left proxy active after {time.monotonic() - began:.3f} s')
        else:
            run.wait_line(lambda s: 'proxy flow=' in s and 'active=0' in s, timeout=12)
            print('PASS: real local-send backpressure observed; non-reading receiver + peer reset cleaned up')
    finally:
        stop.set()
        run.close()
        sender.join(timeout=2)
        assert not sender.is_alive(), 'test sender leaked'


def reset_case(exe, local_reset):
    name = 'receiver-reset' if local_reset else 'idle-peer-reset'
    run = Run(exe, name)
    try:
        if local_reset:
            run.local.setsockopt(socket.SOL_SOCKET, socket.SO_LINGER, struct.pack('HH', 1, 0))
            run.local.close()
        else:
            run.peer.reset()
        run.wait_line(lambda s: 'proxy flow=' in s and 'active=0' in s)
        assert run.process.poll() is None, 'a per-connection reset killed netd'
        print(f'PASS: {name}: both proxy workers released, netd remains alive')
    finally:
        run.close()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('exe', type=Path)
    parser.add_argument('--expect-stuck', action='store_true', help='record the pre-fix cleanup defect')
    parser.add_argument('--case', choices=('all', 'roundtrip', 'pressure', 'faults'), default='roundtrip')
    args = parser.parse_args()
    exe = args.exe.resolve(strict=True)
    if not args.expect_stuck and args.case in ('all', 'roundtrip'):
        roundtrip(exe)
    if args.case in ('all', 'pressure', 'faults'):
        pressure(exe, args.expect_stuck)
    if args.case in ('all', 'faults'):
        reset_case(exe, local_reset=False)
        reset_case(exe, local_reset=True)


if __name__ == '__main__':
    main()
