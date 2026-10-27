"""Minimal radio_link v3 client for testing airbridge_radio.

Usage:
  python tools/radio_link_client.py rx 5
  python tools/radio_link_client.py tx 10
  python tools/radio_link_client.py rx 3 --tx 5

The TX frame is the canonical devourer probe request, parsed from
upstream/devourer/examples/tx/main.cpp, with the sequence number patched per
frame. It is sent over the named pipe, not through libusb directly.
"""
import os
import queue
import re
import struct
import sys
import threading
import time
from pathlib import Path

PIPE = r"\\.\pipe\winawdl-radio"
MAX_PAYLOAD = 16384


def read_exact(f, n):
    data = b""
    while len(data) < n:
        chunk = f.read(n - len(data))
        if not chunk:
            raise EOFError("pipe closed")
        data += chunk
    return data


def connect(timeout_s=90):
    deadline = time.time() + timeout_s
    last = None
    while time.time() < deadline:
        try:
            return open(PIPE, "r+b", buffering=0)
        except OSError as exc:
            last = exc
            time.sleep(0.5)
    raise SystemExit(f"cannot connect to {PIPE}: {last}")


def send(f, msg_type, payload):
    header = struct.pack("<HHI", msg_type, 0, len(payload))
    f.write(header + payload)


def reader(f, q):
    try:
        while True:
            header = read_exact(f, 8)
            msg_type, _, length = struct.unpack("<HHI", header)
            if length > MAX_PAYLOAD:
                q.put(("error", length))
                return
            payload = read_exact(f, length) if length else b""
            q.put((msg_type, payload))
    except Exception:
        return


def canonical_frame():
    src = Path(__file__).resolve().parents[1] / "upstream/devourer/examples/tx/main.cpp"
    text = src.read_text(encoding="utf-8", errors="replace")
    match = re.search(r"uint8_t beacon_frame\[\]\s*=\s*\{(.*?)\};", text, re.S)
    if not match:
        raise SystemExit("cannot find beacon_frame in tx demo")
    values = [int(v, 16) for v in re.findall(r"0x([0-9a-fA-F]{2})", match.group(1))]
    if len(values) < 20:
        raise SystemExit("beacon_frame too short")
    # First 10 bytes are radiotap; the rest is the 802.11 probe request.
    return values[:10], values[10:]


def main():
    args = sys.argv[1:]
    mode = args[0] if args else "rx"
    seconds = float(args[1]) if len(args) > 1 and args[1].replace(".", "", 1).isdigit() else 5.0
    tx_count = 0
    if "--tx" in args:
        tx_count = int(args[args.index("--tx") + 1])
    if mode == "tx":
        tx_count = int(args[1])
        seconds = 1.0

    f = connect()
    header = read_exact(f, 8)
    msg_type, _, length = struct.unpack("<HHI", header)
    hello = read_exact(f, length)
    if msg_type != 1 or len(hello) != 8:
        raise SystemExit(f"unexpected first message type={msg_type} len={length}")
    version, channel, width, _ = struct.unpack("<IBBH", hello)
    hello_at = time.time()
    print(f"HELLO version={version} channel={channel} width={width}MHz "
          f"at {hello_at:.3f}", flush=True)

    q = queue.Queue()
    threading.Thread(target=reader, args=(f, q), daemon=True).start()

    rt, frame = canonical_frame()
    if tx_count:
        for seq in range(tx_count):
            body = bytearray(frame)
            body[22] = (seq << 4) & 0xFF
            body[23] = (seq >> 4) & 0xFF
            send(f, 3, bytes(rt) + bytes(body))
        print(f"sent {tx_count} TX frames over the pipe", flush=True)

    deadline = time.time() + seconds
    rx = 0
    bytes_rx = 0
    while time.time() < deadline:
        try:
            msg_type, payload = q.get(timeout=0.1)
        except queue.Empty:
            continue
        if msg_type == "error":
            raise SystemExit(f"oversized message {payload}")
        if msg_type == 2:
            rx += 1
            bytes_rx += len(payload)
            if rx <= 3:
                rssi = struct.unpack("<b", payload[9:10])[0] if len(payload) >= 10 else 0
                print(f"RX {len(payload)} bytes rssi={rssi} "
                      f"first={payload[10:26].hex()}", flush=True)
        else:
            print(f"msg type={msg_type} len={len(payload)}", flush=True)
    print(f"RX frames={rx} bytes={bytes_rx} "
          f"elapsed={time.time() - hello_at:.1f}s", flush=True)
    os._exit(0)


if __name__ == "__main__":
    main()
