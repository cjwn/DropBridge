#!/usr/bin/env python3
"""Read a supervisor session directory and report transfer speed and airtime.

    python tools/analyze_session.py data\\logs\\20261027-234731
    python tools/analyze_session.py --latest dist\\AirBridge32-headless

Numbers come from the logs the supervisor already writes:

- receiver.log: ``POST /Upload headers`` → ``stored`` gives the wall time and
  the byte count (the project measures MB = 10^6 bytes);
- awdld.log: ``peers … rx action … data … (N joined) … tx action`` lines give
  data frames per second, which is the airtime side of the same transfer.

Nothing here sends frames or touches the radio.
"""
from __future__ import annotations

import argparse
import datetime as dt
import json
import os
import re
import statistics
import sys

UPLOAD_START = re.compile(r'^(\d{4}-\d\d-\d\d \d\d:\d\d:\d\d,\d+) .*POST /Upload headers: (\{.*\})')
UPLOAD_DONE = re.compile(r'^(\d{4}-\d\d-\d\d \d\d:\d\d:\d\d,\d+) .*upload \S+: (\d+) bytes received')
STORED = re.compile(r'^(\d{4}-\d\d-\d\d \d\d:\d\d:\d\d,\d+) .*stored (.*)$')
AWD = re.compile(r'^(\d\d:\d\d:\d\d\.\d+) .*peers (\d+) \| rx action (\d+), data (\d+)(?: \((\d+) joined\))?'
                 r' \| tx action (\d+), data (\d+)')

MB = 1_000_000


def parse_args(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('session', nargs='?', help='session directory')
    parser.add_argument('--latest', metavar='ROOT',
                        help='pick the newest session under ROOT\\data\\logs')
    parser.add_argument('--json', action='store_true')
    return parser.parse_args(argv)


def resolve(args):
    if args.latest:
        logs = os.path.join(args.latest, 'data', 'logs')
        if not os.path.isdir(logs):
            raise SystemExit(f'no log directory: {logs}')
        sessions = [os.path.join(logs, name) for name in os.listdir(logs)]
        sessions = [path for path in sessions if os.path.isdir(path)]
        if not sessions:
            raise SystemExit(f'no sessions under {logs}')
        return max(sessions, key=os.path.getmtime)
    if not args.session:
        raise SystemExit('give a session directory or --latest <package root>')
    return args.session


def stamp(text):
    return dt.datetime.strptime(text, '%Y-%m-%d %H:%M:%S,%f')


def read_lines(path):
    if not os.path.isfile(path):
        return []
    with open(path, encoding='utf-8', errors='replace') as handle:
        return handle.read().splitlines()


def uploads(receiver_lines):
    found = []
    pending = None
    for line in receiver_lines:
        start = UPLOAD_START.match(line)
        if start:
            # The receiver logs a Python dict repr, not JSON; keep json.loads as
            # a fallback in case that ever changes.
            raw = start.group(2)
            total = None
            found_total = re.search(r"['\"]TotalBytes['\"]\s*:\s*['\"]?(\d+)", raw)
            if found_total:
                total = int(found_total.group(1))
            else:
                try:
                    headers = json.loads(raw.replace("'", '"'))
                    total = int(headers.get('TotalBytes') or 0) or None
                except (ValueError, TypeError):
                    total = None
            pending = {
                'start': stamp(start.group(1)),
                'total': total,
                'name': None,
            }
            continue
        done = UPLOAD_DONE.match(line)
        if done and pending is not None:
            pending['bytes'] = int(done.group(2))
            continue
        stored = STORED.match(line)
        if stored and pending is not None:
            pending['end'] = stamp(stored.group(1))
            pending['name'] = stored.group(2)
            found.append(pending)
            pending = None
    return found


def airtime(awdl_lines):
    samples = []
    for line in awdl_lines:
        match = AWD.match(line)
        if not match:
            continue
        samples.append({
            'time': match.group(1),
            'peers': int(match.group(2)),
            'rx_data': int(match.group(4)),
            'joined': int(match.group(5) or 0),
            'tx_data': int(match.group(7)),
        })
    return samples


def main(argv=None):
    args = parse_args(argv)
    session = resolve(args)
    receiver_lines = read_lines(os.path.join(session, 'receiver.log'))
    awdl_lines = read_lines(os.path.join(session, 'awdld.log'))

    transfers = uploads(receiver_lines)
    samples = airtime(awdl_lines)

    if args.json:
        print(json.dumps({'session': session, 'transfers': [
            {key: (value.isoformat() if isinstance(value, dt.datetime) else value)
             for key, value in item.items()} for item in transfers]}, ensure_ascii=False, indent=2))
        return 0

    print(f'session: {session}')
    if not transfers:
        print('no /Upload found (no phone transfer in this session)')
    for item in transfers:
        seconds = (item['end'] - item['start']).total_seconds()
        size = item.get('bytes') or item['total'] or 0
        print(f"  {item['name']}: {size:,} bytes in {seconds:.2f} s "
              f"= {size / MB / seconds:.2f} MB/s")

    if len(samples) >= 2:
        window = samples[-1]['time'], samples[0]['time']
        joined = samples[-1]['joined'] - samples[0]['joined']
        data_frames = samples[-1]['rx_data'] - samples[0]['rx_data']
        span = (dt.datetime.strptime(samples[-1]['time'], '%H:%M:%S.%f')
                - dt.datetime.strptime(samples[0]['time'], '%H:%M:%S.%f')).total_seconds()
        print(f'awdl: {span:.1f} s, rx data frames {data_frames}, joined {joined} '
              f'({joined / span:.0f} joined/s), tx data {samples[-1]["tx_data"] - samples[0]["tx_data"]}')
        if transfers:
            item = transfers[-1]
            seconds = (item['end'] - item['start']).total_seconds()
            size = item.get('bytes') or item['total'] or 0
            if seconds > 0:
                print(f'   transfer needs {size / seconds / 1400:.0f} frames/s at 1400 B/frame; '
                      f'observed {joined / span:.0f} joined/s overall')
    elif samples:
        print(f"awdl: only one sample ({samples[0]['peers']} peers) - nothing to compare")
    return 0


if __name__ == '__main__':
    sys.exit(main())
