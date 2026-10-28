"""Real lwIP TCP over loopback IPC; no radio, phone or external network.

The tiny IPv6/TCP peer deliberately uses fixed test addresses and a fixed
initial sequence. It checks transport/proxy boundaries, not AirDrop/TLS.

The `soak` case is the only throughput-oriented case: it streams a
deterministic byte pattern through the real proxy over repeated connections,
asserts byte-exact delivery, no input-queue drops, no ACK-progress stall and
no per-connection handle/private-byte growth. it measures the proxy and lwIP,
not the air interface, so its MB/s is an upper bound for the real link.
"""
import argparse
import ctypes
import ctypes.wintypes as wt
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
MSS = 1360  # == lwIP TCP_MSS / netif MTU 1450 minus IPv6+TCP headers


def checksum(data):
    if len(data) % 2:
        data += b'\0'
    total = sum(struct.unpack(f'!{len(data) // 2}H', data))
    while total >> 16:
        total = (total & 65535) + (total >> 16)
    return (~total) & 65535


class Peer:
    def __init__(self, sock, peer_port=50000, seq0=1000):
        self.sock = sock
        self.sock.settimeout(0.25)
        self.lock = threading.Lock()
        self.cv = threading.Condition()
        self.peer_port = peer_port
        self.seq0 = seq0
        self.seq, self.acked, self.next_rx = seq0 + 1, seq0, 0
        self.window, self.scale = 0, 0
        self.response = bytearray()
        self.response_first = self.response_last = None
        self.syn = self.fin = self.rst = self.closed = False
        self.error = None
        self.thread = threading.Thread(target=self.read, daemon=True)
        self.thread.start()
        self.begin(peer_port, seq0)

    def begin(self, peer_port=None, seq0=None):
        """Start a new connection on the shared IPC socket.

        The data plane accepts one awdld IPC connection for the process, so
        sequential test connections reuse the socket and are separated by
        source port. Packets still in flight for an older port are ignored by
        the read filter.
        """
        with self.cv:
            if peer_port is not None:
                self.peer_port = peer_port
            if seq0 is not None:
                self.seq0 = seq0
            self.seq, self.acked, self.next_rx = self.seq0 + 1, self.seq0, 0
            self.window, self.scale = 0, 0
            self.response = bytearray()
            self.response_first = self.response_last = None
            self.syn = self.fin = self.rst = False
            self.error = None
            self.cv.notify_all()
        self.send(2, seq=self.seq0, options=b'\x02\x04\x05\x50\x03\x03\x00\x00')
        self.wait(lambda: self.syn and self.acked == self.seq0 + 1, 'SYN/ACK')

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
            tcp = struct.pack('!HHIIBBHHH', self.peer_port, 8771, self.seq if seq is None else seq,
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
                if struct.unpack_from('!HH', tcp) != (8771, self.peer_port):
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
                        if not self.response:
                            self.response_first = time.monotonic()
                        self.response_last = time.monotonic()
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

    def stream_upload(self, data, inflight=16 * 1024, batch=8, rate=0.0, rto=3.0,
                      stall_limit=8.0):
        """ACK-clocked upload with go-back-N recovery, for throughput probing.

        `upload()` caps in-flight data at 4 KB on purpose and only measures
        round-trip latency. This one is bounded by `inflight` and paces itself
        from ACKs; `rate` (segments/s) adds an optional ceiling for over-drive
        tests. lwIP queues at most MEMP_NUM_TCPIP_MSG_INPKT=8 inputs before it
        rejects a frame, so the default keeps fewer segments than that in
        flight and a rejection is a genuine finding, not routine.

        A rejected frame is never silently accepted: the sender rewinds to the
        first unacknowledged byte (go-back-N) after `rto`, and a real stall
        raises instead of hanging.
        """
        base = self.seq0 + 1
        offset = 0
        retransmits = 0
        last_acked, last_progress = self.acked, time.monotonic()
        schedule = time.monotonic()

        def resend_unacked():
            """Go-back-N over the unacknowledged window (bounded by inflight)."""
            start = self.acked - base
            end = min(len(data), start + inflight)
            seq = self.acked
            while start < end:
                size = min(MSS, end - start)
                self.send(24, data[start:start + size], seq=seq)
                seq += size
                start += size
            self.seq = seq

        # Keep going while bytes are still to send OR the tail is unacknowledged:
        # a lost final segment must be retransmitted too, as real TCP would.
        while offset < len(data) or self.acked < self.seq:
            def available():
                return min(self.acked + self.window - self.seq,
                           inflight - (self.seq - self.acked))

            with self.cv:
                ok = self.cv.wait_for(
                    lambda: available() > 0 or self.error or self.rst or self.closed, rto)
                if self.closed:
                    raise ConnectionError('test peer closed')
                if self.error:
                    raise self.error
                if self.rst:
                    raise ConnectionError('lwIP reset the test connection')

            now = time.monotonic()
            if self.acked != last_acked:
                last_acked, last_progress = self.acked, now
            if not ok:
                # The stall check must not depend on outstanding data: a zero
                # window with nothing unacknowledged otherwise waits forever.
                if now - last_progress > stall_limit:
                    raise AssertionError(
                        f'no ACK progress for {now - last_progress:.1f}s at '
                        f'{offset}/{len(data)} bytes (unacked={self.seq - self.acked} '
                        f'window={self.window} retransmits={retransmits})')
                if self.seq > self.acked:
                    resend_unacked()
                    offset = max(offset, self.seq - base)
                    retransmits += 1
                elif offset < len(data):
                    # Zero-window probe, as TCP does: one byte past the window
                    # makes the peer re-announce its window.
                    self.send(24, data[offset:offset + 1], seq=self.seq)
                    self.seq += 1
                    offset += 1
                continue

            sent = 0
            while sent < batch and offset < len(data):
                room = available()
                if room <= 0:
                    break
                size = min(MSS, room, len(data) - offset)
                payload = data[offset:offset + size]
                seq = self.seq
                self.seq += size
                offset += size
                sent += 1
                self.send(24, payload, seq=seq)
            if sent:
                time.sleep(0)  # let the IPC reader drain lwIP's small input pool
                if rate:
                    schedule = max(schedule, time.monotonic() - 0.2) + sent / rate
                    delay = schedule - time.monotonic()
                    if delay > 0:
                        time.sleep(delay)
        return offset, retransmits

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
    def __init__(self, exe, name, rcvbuf=1024, mdns=False):
        self.work = Path('build/diagnostics/tcp-diag') / f'{name}-{time.time_ns()}'
        self.work.mkdir(parents=True)
        self.lines, self.events = [], queue.Queue()
        self.server = socket.socket()
        self.server.setsockopt(socket.SOL_SOCKET, socket.SO_EXCLUSIVEADDRUSE, 1)
        if rcvbuf is not None:
            # Small receive buffer is what provokes backpressure; a throughput
            # case must use the default socket sizing instead.
            self.server.setsockopt(socket.SOL_SOCKET, socket.SO_RCVBUF, rcvbuf)
        self.server.bind(('127.0.0.1', 8771))  # Fail before starting anything if occupied.
        self.server.listen(4)
        self.server.settimeout(5)
        env = os.environ.copy()
        for key in ('AWDL_NETD_MDNS_PROXY', 'AWDL_NETD_NATIVE_MDNS'):
            env.pop(key, None)
        if mdns:
            # Production sets this in the supervisor; the UDP sockets it adds
            # also consume lwIP netconns, so a TCP-only test is not equivalent.
            env['AWDL_NETD_MDNS_PROXY'] = '1'
        env['AWDL_NETD_ADDR_FILE'] = str((self.work / 'mock.addr').resolve())
        self.process = subprocess.Popen([str(exe)], cwd=self.work, env=env,
                                        stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                                        text=True, encoding='utf-8', errors='replace')
        self.reader = threading.Thread(target=self.output, daemon=True)
        self.reader.start()
        self.pid = self.process.pid
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

    def next_connection(self, peer_port, seq0):
        """Open the next sequential connection on the already-running netd."""
        self.local.close()
        self.peer.begin(peer_port, seq0)
        self.local, _ = self.server.accept()
        self.local.settimeout(15)

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


def last_stats(run):
    for line in reversed(run.lines):
        if 'ipc stats' in line:
            return line
    return ''


def snapshot_count(run):
    return sum(1 for line in run.lines if 'ipc stats' in line)


def wait_fresh_snapshot(run, timeout=8):
    """Return the lines of the first 5 s stats snapshot printed after now.

    The 5 s cadence means a just-finished connection cannot be judged from an
    older snapshot; only lines after the newest `ipc stats` header belong to
    the same batch.
    """
    before = snapshot_count(run)
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        if snapshot_count(run) > before:
            break
        time.sleep(0.1)
    else:
        raise AssertionError(f'no netd stats snapshot within {timeout} s')
    lines = list(run.lines)
    start = max(index for index, line in enumerate(lines) if 'ipc stats' in line)
    return lines[start:]


class _ProcessMemoryCountersEx(ctypes.Structure):
    _fields_ = [
        ('cb', wt.DWORD),
        ('PageFaultCount', wt.DWORD),
        ('PeakWorkingSetSize', ctypes.c_size_t),
        ('WorkingSetSize', ctypes.c_size_t),
        ('QuotaPeakPagedPoolUsage', ctypes.c_size_t),
        ('QuotaPagedPoolUsage', ctypes.c_size_t),
        ('QuotaPeakNonPagedPoolUsage', ctypes.c_size_t),
        ('QuotaNonPagedPoolUsage', ctypes.c_size_t),
        ('PagefileUsage', ctypes.c_size_t),
        ('PeakPagefileUsage', ctypes.c_size_t),
        ('PrivateUsage', ctypes.c_size_t),
    ]


def process_usage(pid):
    """Handle count, working set and private bytes of the netd child."""
    kernel32 = ctypes.WinDLL('kernel32', use_last_error=True)
    psapi = ctypes.WinDLL('psapi', use_last_error=True)
    kernel32.OpenProcess.restype = ctypes.c_void_p
    kernel32.OpenProcess.argtypes = [wt.DWORD, wt.BOOL, wt.DWORD]
    kernel32.CloseHandle.argtypes = [ctypes.c_void_p]
    kernel32.GetProcessHandleCount.argtypes = [ctypes.c_void_p, ctypes.POINTER(wt.DWORD)]
    psapi.GetProcessMemoryInfo.argtypes = [ctypes.c_void_p, ctypes.c_void_p, wt.DWORD]
    psapi.GetProcessMemoryInfo.restype = wt.BOOL
    handle = kernel32.OpenProcess(0x0400 | 0x1000, False, pid)  # QUERY_INFORMATION|LIMITED
    assert handle, f'OpenProcess({pid}) failed: {ctypes.get_last_error()}'
    try:
        handles = wt.DWORD()
        assert kernel32.GetProcessHandleCount(handle, ctypes.byref(handles)), 'handle count failed'
        counters = _ProcessMemoryCountersEx()
        counters.cb = ctypes.sizeof(counters)
        assert psapi.GetProcessMemoryInfo(handle, ctypes.byref(counters), counters.cb), \
            'GetProcessMemoryInfo failed'
        return handles.value, counters.WorkingSetSize, counters.PrivateUsage
    finally:
        kernel32.CloseHandle(handle)


def soak(exe, megabytes=64.0, cycles=1, inflight=16 * 1024, batch=8, rate=0.0,
         stall=1.2, response_kb=256, handle_limit=16, private_limit_mb=32.0,
         allow_drops=False, mdns=False):
    """Stream fixed-size connections through the real proxy and check health.

    Asserted here: byte-exact delivery both directions, ACK progress never
    stalls, the proxy is idle again after each close, handles/private bytes do
    not grow across sequential connections, and (unless --allow-drops) that
    netd never rejected a frame. NOT asserted: any air-interface rate; this is
    the proxy + lwIP ceiling on this machine.
    """
    size = int(megabytes * 1024 * 1024)
    response = (b'AirBridge32-soak-response' * (response_kb * 1024 // 26 + 1))[:response_kb * 1024]
    run = Run(exe, 'soak', rcvbuf=None, mdns=mdns)
    if mdns:
        run.wait_line(lambda line: 'mDNS proxy' in line)
    baseline = None
    cycles_done = 0
    try:
        for cycle in range(cycles):
            if cycle:
                run.next_connection(50000 + cycle, 1000 + 1000 * cycle)
            peer = run.peer
            source = (bytes(range(256)) * (size // 256 + 1))[:size]
            source_hash = hashlib.sha256(source).digest()
            received_hash = hashlib.sha256()
            received = [0]
            errors = []
            stall_at = size // 4 if stall else 0

            def receiver():
                stalled = False
                try:
                    while True:
                        try:
                            chunk = run.local.recv(65536)
                        except socket.timeout:
                            continue
                        if not chunk:
                            break
                        received_hash.update(chunk)
                        received[0] += len(chunk)
                        if stall_at and received[0] >= stall_at and not stalled:
                            # Healthy idle under load: well below the proxy's 5 s
                            # send-no-progress limit, so cleanup must not fire.
                            stalled = True
                            time.sleep(stall)
                    run.local.sendall(response)
                    run.local.shutdown(socket.SHUT_WR)
                except Exception as error:
                    errors.append(error)

            worker = threading.Thread(target=receiver, daemon=True)
            started = time.monotonic()
            worker.start()
            _, retransmits = peer.stream_upload(source, inflight=inflight, batch=batch,
                                                rate=rate)
            upload_seconds = time.monotonic() - started
            assert not peer.fin, f'cycle {cycle}: FIN arrived before the upload finished'
            peer.finish()
            worker.join(timeout=30)
            assert not worker.is_alive(), f'cycle {cycle}: receiver thread stuck'
            assert not errors, f'cycle {cycle}: receiver error {errors}'
            peer.wait(lambda: peer.fin, 'response FIN', timeout=20)
            total_seconds = time.monotonic() - started
            # Peer-observed arrival window of the response, so the number does
            # not depend on when the local side happened to flush it.
            down_seconds = 0.0
            if peer.response_first:
                down_seconds = peer.response_last - peer.response_first
            down_rate = (f'{len(response) / 1e6 / down_seconds:.2f} MB/s'
                         if down_seconds > 0 else 'coarser than the clock')

            assert received[0] == size, f'cycle {cycle}: received {received[0]} of {size} bytes'
            assert received_hash.digest() == source_hash, f'cycle {cycle}: upload bytes differ'
            assert bytes(peer.response) == response, f'cycle {cycle}: down-path bytes differ'
            cycles_done += 1
            handles, working, private = process_usage(run.pid)
            if baseline is None:
                baseline = (handles, private)
            print(f'cycle {cycle}: {size / 1e6:g} MB up in {upload_seconds:.3f} s '
                  f'= {size / 1e6 / upload_seconds:.2f} MB/s (incl. {stall:g} s receiver idle); '
                  f'{len(response) / 1e6:g} MB down in {down_seconds * 1000:.1f} ms '
                  f'= {down_rate} (total {total_seconds:.3f} s); '
                  f'retransmits={retransmits}; '
                  f'netd handles={handles} working={working / 1e6:.1f} MB '
                  f'private={private / 1e6:.1f} MB')

        lines = wait_fresh_snapshot(run)
        stats = last_stats(run)
        match = re.search(r'alloc_fail=(\d+) copy_fail=(\d+) input_fail=(\d+)', stats)
        assert match, f'cannot parse counters: {stats.strip()}'
        drops = [int(value) for value in match.groups()]
        tx_match = re.search(r'tx_fail=(\d+)', stats)
        drops.append(int(tx_match.group(1)) if tx_match else 0)
        if not allow_drops:
            assert not any(drops), (f'netd allocation/input/output failure during soak {drops}; '
                                    f'lower --inflight-kb/--batch or use --allow-drops when '
                                    f'deliberately over-driving: {stats.strip()}')
        active = [line for line in lines if 'proxy flow=' in line and 'active=1' in line]
        assert not active, 'proxy connection still active after soak: ' + ''.join(active)
        handles, _, private = process_usage(run.pid)
        handle_growth = handles - baseline[0]
        private_growth = (private - baseline[1]) / 1e6
        assert handle_growth <= handle_limit, (f'handle growth {handle_growth} over {cycles} connections '
                                               f'exceeds {handle_limit}')
        assert private_growth <= private_limit_mb, (f'private bytes grew {private_growth:.1f} MB over '
                                                    f'{cycles} connections (limit {private_limit_mb} MB)')
        print(f'PASS: {cycles_done} sequential connection(s), {megabytes:g} MB byte-exact each way, '
              f'drops={drops}, proxy idle after cleanup, handle growth {handle_growth}, '
              f'private growth {private_growth:.1f} MB; netd {stats.strip()}')
    finally:
        run.close()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('exe', type=Path)
    parser.add_argument('--expect-stuck', action='store_true', help='record the pre-fix cleanup defect')
    parser.add_argument('--case', choices=('all', 'roundtrip', 'pressure', 'faults', 'soak'),
                        default='roundtrip')
    parser.add_argument('--mb', type=float, default=64.0, help='soak: payload size per connection')
    parser.add_argument('--cycles', type=int, default=1, help='soak: sequential connections')
    parser.add_argument('--inflight-kb', type=float, default=16.0,
                        help='soak: bytes in flight per connection (16 KB = 12 segments, '
                             'above the legacy 8-slot lwIP input queue)')
    parser.add_argument('--batch', type=int, default=8, help='soak: segments per send burst')
    parser.add_argument('--rate', type=float, default=0.0,
                        help='soak: optional segment/s ceiling (0 = pure ACK clocking)')
    parser.add_argument('--stall', type=float, default=1.2,
                        help='soak: seconds the local receiver idles mid-transfer')
    parser.add_argument('--allow-drops', action='store_true',
                        help='soak: over-drive on purpose; report netd drops instead of failing')
    parser.add_argument('--mdns', action='store_true',
                        help='soak: start netd with AWDL_NETD_MDNS_PROXY=1, as production does')
    args = parser.parse_args()
    exe = args.exe.resolve(strict=True)
    if not args.expect_stuck and args.case in ('all', 'roundtrip'):
        roundtrip(exe)
    if args.case in ('all', 'pressure', 'faults'):
        pressure(exe, args.expect_stuck)
    if args.case in ('all', 'faults'):
        reset_case(exe, local_reset=False)
        reset_case(exe, local_reset=True)
    if args.case == 'soak':
        soak(exe, megabytes=args.mb, cycles=args.cycles,
             inflight=int(args.inflight_kb * 1024), batch=args.batch,
             rate=args.rate, stall=args.stall, allow_drops=args.allow_drops,
             mdns=args.mdns)


if __name__ == '__main__':
    main()
