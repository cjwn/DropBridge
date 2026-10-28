"""Loopback-only IPC faults against the real netd executable. No radio needed."""
import argparse
import ipaddress
import os
import queue
import socket
import subprocess
import threading
import time
from pathlib import Path


def check(exe, name, pieces, expected, pending=None, heartbeat=False, connect_delay=0):
    env = os.environ.copy()
    for key in ('AWDL_NETD_MDNS_PROXY', 'AWDL_NETD_NATIVE_MDNS', 'AWDL_NETD_ADDR_FILE'):
        env.pop(key, None)
    # Keep even the address file under the task's ignored build directory.
    work = Path('build/diagnostics/netd-offline').resolve()
    work.mkdir(parents=True, exist_ok=True)
    env['AWDL_NETD_ADDR_FILE'] = str(work / 'mock.addr')
    process = subprocess.Popen([str(exe)], cwd=work, env=env,
                               stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                               text=True, encoding='utf-8', errors='replace')
    lines = []
    events = queue.Queue()

    def read_output():
        for line in process.stdout:
            lines.append(line)
            events.put(line)

    reader = threading.Thread(target=read_output, daemon=True)
    reader.start()
    try:
        deadline = time.monotonic() + 5
        # Connect only after THIS process confirms it owns the listener. If
        # another chain owns 49877, abort instead of touching that chain.
        while True:
            remaining = deadline - time.monotonic()
            assert remaining > 0, 'netd listener not ready'
            line = events.get(timeout=remaining)
            if 'waiting for awdld on 127.0.0.1:49877' in line:
                break
            assert process.poll() is None, 'netd failed before binding its listener'
        if connect_delay:
            time.sleep(connect_delay)
            assert process.poll() is None, 'waiting for the first peer looked like link failure'
        with socket.create_connection(('127.0.0.1', 49877), timeout=2) as peer:
            peer.sendall(b'AWDL' + ipaddress.IPv6Address('fe80::200:ff:fe00:1').packed)
            if heartbeat:
                deadline = time.monotonic() + 8
                while not any('ipc stats' in line for line in lines) and time.monotonic() < deadline:
                    assert process.poll() is None, 'healthy idle unexpectedly terminated'
                    time.sleep(0.02)
                assert any('ipc stats' in line for line in lines), 'no healthy heartbeat'
            else:
                time.sleep(0.15)
            assert process.poll() is None, 'healthy handshake unexpectedly terminated'
            for piece in pieces:
                peer.sendall(piece)
                time.sleep(0.01)
            if not pieces or expected == 'op=recv':
                time.sleep(0.15)
                assert process.poll() is None, 'healthy idle/fragmented input looked like a broken link'
            began = time.monotonic()
            peer.shutdown(socket.SHUT_WR)
            try:
                code = process.wait(timeout=3)
            except subprocess.TimeoutExpired as error:
                raise AssertionError('data connection ended but process stayed alive for 3 s') from error
        reader.join(timeout=1)
        output = ''.join(lines)
        assert code == 2, f'exit {code}, expected 2'
        assert expected in output, output
        if pending is not None:
            assert f'pending_bytes={pending}' in output, output
        assert 'data plane failed; exiting 2' in output, output
        print(f'PASS: {name}: exit=2 after EOF in {time.monotonic() - began:.3f} s')
        print(output.rstrip())
    finally:
        if process.poll() is None:
            process.kill()  # Only the child this test created.
        process.wait(timeout=3)
        reader.join(timeout=1)
        if process.returncode != 2:
            print(''.join(lines).rstrip())


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('exe', type=Path)
    parser.add_argument('--only-eof', action='store_true', help='also useful to expose old process false-liveness')
    args = parser.parse_args()
    exe = args.exe.resolve(strict=True)
    check(exe, 'healthy idle then EOF', [], 'op=recv', pending=0, heartbeat=not args.only_eof)
    if not args.only_eof:
        check(exe, 'delayed initial connection then EOF', [], 'op=recv', pending=0, connect_delay=2.1)
        check(exe, 'truncated packet then EOF', [b'\x40', b'\x00', b'\x60' + bytes(7)], 'op=recv', pending=10)
        check(exe, 'invalid IPv6 frame length', [b'\x01\x00'], 'op=invalid_frame_length')
        # Valid IPv6 with no next header: observe queueing, not TCP delivery.
        packet = bytearray(40)
        packet[0], packet[6], packet[7] = 0x60, 59, 64
        packet[8:24] = ipaddress.IPv6Address('fe80::2').packed
        packet[24:40] = ipaddress.IPv6Address('fe80::200:ff:fe00:1').packed
        check(exe, 'split complete IPv6 frame then EOF', [b'\x28', b'\x00' + packet[:13], packet[13:]], 'op=recv', pending=0)


if __name__ == '__main__':
    main()
