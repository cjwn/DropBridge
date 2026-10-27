#!/usr/bin/env python3
"""Q1: measure the AirDrop receiver's own throughput ceiling, without radio.

Starts the real receiver (TLS + chunked + DVZip + cpio extraction + disk) on
127.0.0.1 and uploads N MB through it. Timing is taken server-side, between
"upload started" and "stored", so client-side buffering does not flatter it.

This path is exactly what the phone drives after awdl_netd forwards TCP 8771,
minus the lwIP hop. It answers one question: can the Python receiver keep up
with a faster radio? It sends no wireless frames.

Usage:
  python tools/bench_receiver.py --mb 20 --runs 3
  python tools/bench_receiver.py --mb 20 --mode stored --runs 1
"""

from __future__ import annotations

import argparse
import hashlib
import http.client
import json
import logging
import os
import platform
import plistlib
import socket
import ssl
import statistics
import sys
import tempfile
import threading
import time
import zlib

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
AIRDROP = os.path.join(ROOT, "upstream", "winawdl", "airdrop")
sys.path.insert(0, AIRDROP)

import cpio  # noqa: E402
import receiver  # noqa: E402
import transfer  # noqa: E402

REGULAR = 0o100644
DIRECTORY = 0o040755
MB = 1_000_000


def portable_member(name: str, data: bytes = b"", mode: int = REGULAR) -> bytes:
    """Same cpio "portable" framing the iPhone's DVZip carries; mirrors
    tests/test_transfer.py so the receiver sees a realistic archive."""
    raw = name.encode() + b"\0"
    header = "070707" + "000000" * 2 + f"{mode:06o}" + "000000" * 4
    header += f"{0:011o}{len(raw):06o}{len(data):011o}"
    return header.encode() + raw + data


def dvzip_block(raw: bytes, stored: bool) -> bytes:
    body = raw if stored else zlib.compress(raw)
    size = len(body) | (0x80000000 if stored else 0)
    return size.to_bytes(4, "big") + body


def upload_payload(size: int, stored: bool) -> tuple[bytes, str]:
    data = os.urandom(size)
    archive = (portable_member(".", mode=DIRECTORY)
               + portable_member("./bench.bin", data)
               + portable_member(cpio.TRAILER, mode=0))
    return dvzip_block(archive, stored), hashlib.sha256(data).hexdigest()


class BenchWatcher(receiver.Watcher):
    """Times the upload server-side and records the outcome."""

    def __init__(self) -> None:
        self.started = None
        self.finished = None
        self.names = []
        self.failure = None

    def asked(self, ask, address, left):  # noqa: D401 - mirrors upstream
        return True

    def upload_started(self, accepted, tid, total):
        self.started = time.perf_counter()
        return True

    def stored(self, token, names):
        self.finished = time.perf_counter()
        self.names = names

    def upload_failed(self, token, reason):
        self.finished = time.perf_counter()
        self.failure = reason


def serve(keys_dir: str, dest: str, watcher: BenchWatcher):
    settings = argparse.Namespace(
        name="Bench PC", model="MacBookPro18,3", dest=dest,
        max_receive_percent=30, interface_index=0, redact=False)
    server = receiver.Receiver(0, settings, watcher=watcher, proxy=True)
    context = receiver.tls_context(keys_dir)
    server.socket = context.wrap_socket(
        server.socket, server_side=True, do_handshake_on_connect=False)
    thread = threading.Thread(target=server.serve_forever, daemon=True)
    thread.start()
    return server, thread, server.server_address[1]


def upload(port: int, payload: bytes, size: int) -> int:
    trusting = ssl.create_default_context()
    trusting.check_hostname = False
    trusting.verify_mode = ssl.CERT_NONE
    connection = http.client.HTTPSConnection(
        "127.0.0.1", port, context=trusting, timeout=180)
    try:
        ask = {"SenderComputerName": "Bench",
               "Files": [{"FileName": "bench.bin", "FileSize": size}]}
        body = plistlib.dumps(ask, fmt=plistlib.FMT_BINARY)
        connection.request("POST", "/Ask", body=iter([body]),
                           encode_chunked=True,
                           headers={"Transfer-Encoding": "chunked"})
        answer = connection.getresponse()
        answer.read()
        if answer.status != 200:
            raise RuntimeError(f"/Ask returned {answer.status}")

        def chunks():
            for start in range(0, len(payload), 64 * 1024):
                yield payload[start:start + 64 * 1024]

        connection.request(
            "POST", "/Upload", body=chunks(), encode_chunked=True,
            headers={"Content-Type": "application/x-dvzip",
                     "TotalBytes": str(size),
                     "Transfer-Encoding": "chunked"})
        done = connection.getresponse()
        answer_body = done.read()
        if done.status != 200:
            raise RuntimeError(f"/Upload returned {done.status}: {answer_body[:200]!r}")
        return done.status
    finally:
        connection.close()


def one_run(port: int, keys_dir: str, dest: str, size: int, stored: bool,
            watcher: BenchWatcher) -> dict:
    watcher.started = None
    watcher.finished = None
    watcher.failure = None
    watcher.names = []
    payload, expected = upload_payload(size, stored)
    status = upload(port, payload, size)
    if watcher.failure:
        raise RuntimeError(f"receiver refused: {watcher.failure}")
    if watcher.started is None or watcher.finished is None:
        raise RuntimeError("receiver never started or never stored")
    seconds = watcher.finished - watcher.started
    path = os.path.join(dest, "bench.bin")
    with open(path, "rb") as handle:
        digest = hashlib.sha256(handle.read()).hexdigest()
    os.unlink(path)
    return {
        "status": status,
        "seconds": seconds,
        "mb_per_s": size / MB / seconds,
        "wire_bytes": len(payload),
        "sha256_ok": digest == expected,
    }


def main(argv=None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--mb", type=int, default=20, help="payload size in MB (10^6)")
    parser.add_argument("--runs", type=int, default=3)
    parser.add_argument("--mode", choices=["compressed", "stored", "both"],
                        default="both")
    parser.add_argument("--trace", action="store_true",
                        help="log receiver DEBUG lines to build/bench-trace.log")
    parser.add_argument("--out", default=os.path.join("evidence", "bench-receiver.md"))
    args = parser.parse_args(argv)

    size = args.mb * MB
    modes = ["compressed", "stored"] if args.mode == "both" else [args.mode]
    results = {}

    with tempfile.TemporaryDirectory() as work:
        keys = os.path.join(work, "keys")
        dest = os.path.join(work, "received")
        os.makedirs(dest)
        if args.trace:
            os.makedirs(os.path.join(ROOT, "build"), exist_ok=True)
            logging.basicConfig(
                level=logging.DEBUG,
                filename=os.path.join(ROOT, "build", "bench-trace.log"),
                filemode="w", format="%(asctime)s %(levelname)s %(message)s")

        for mode in modes:
            watcher = BenchWatcher()
            server, thread, port = serve(keys, dest, watcher)
            samples = []
            try:
                for index in range(args.runs):
                    sample = one_run(port, keys, dest, size,
                                     mode == "stored", watcher)
                    sample["run"] = index + 1
                    samples.append(sample)
                    print(f"{mode} run {index + 1}: "
                          f"{sample['seconds']:.2f} s, "
                          f"{sample['mb_per_s']:.1f} MB/s, "
                          f"sha={'ok' if sample['sha256_ok'] else 'BAD'}")
            finally:
                server.shutdown()
                thread.join()
                server.reset_sessions()
                server.server_close()
            results[mode] = samples

    report = render(args, size, results)
    print()
    print(report)
    os.makedirs(os.path.join(ROOT, "evidence"), exist_ok=True)
    with open(os.path.join(ROOT, args.out), "w", encoding="utf-8") as handle:
        handle.write(report + "\n")
    return 0


def render(args, size: int, results: dict) -> str:
    lines = ["# Receiver 离线吞吐台（Q1）", ""]
    lines.append(f"- 时间：{time.strftime('%Y-%m-%d %H:%M:%S %z')}")
    lines.append(f"- 机器：{platform.platform()} / Python {platform.python_version()}")
    lines.append(f"- 负载：{args.mb} MB 随机字节，cpio + DVZip，128 KB 分块经 TLS 上传")
    lines.append("- 计时：服务端 `/Upload` 开始到 `stored`（不含客户端收尾）")
    lines.append("- 不包含：awdl_netd 的 lwIP 代理与 AWDL 空口")
    lines.append("")
    lines.append("| 模式 | 运行 | 秒 | MB/s | SHA-256 |")
    lines.append("| --- | --- | --- | --- | --- |")
    medians = {}
    for mode, samples in results.items():
        for sample in samples:
            lines.append(f"| {mode} | {sample['run']} | {sample['seconds']:.2f} | "
                         f"{sample['mb_per_s']:.1f} | "
                         f"{'ok' if sample['sha256_ok'] else 'BAD'} |")
        medians[mode] = statistics.median(s["mb_per_s"] for s in samples)
    lines.append("")
    lines.append("## 结论")
    floor = min(medians.values())
    lines.append(f"- 中位数：{', '.join(f'{m} {v:.1f} MB/s' for m, v in medians.items())}")
    if floor >= 10:
        verdict = ("≥10 MB/s：Python/TLS/zlib/磁盘不是瓶颈。速度问题在空口调度、"
                   "ACK 与 USB 帧率（Q8–Q10）。")
    elif floor >= 3:
        verdict = ("3–10 MB/s：receiver 有余量但不是无限；若空口调到 3 MB/s 以上，"
                   "先看这里是否成为第二个上限。")
    else:
        verdict = ("<3 MB/s：receiver 本身就是瓶颈，先修它，再谈射频。")
    lines.append(f"- {verdict}")
    lines.append("- 客户端与服务端同进程，测得的数字是下限；若结论接近阈值，"
                 "改用独立进程客户端复核。")
    return "\n".join(lines)


if __name__ == "__main__":
    sys.exit(main())
