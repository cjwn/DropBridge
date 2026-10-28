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
REJECTED = re.compile(r'^(\d{4}-\d\d-\d\d \d\d:\d\d:\d\d,\d+) .*/Upload rejected: (.*)$')
AWD = re.compile(r'^(\d\d:\d\d:\d\d\.\d+) .*peers (\d+) \| rx action (\d+), data (\d+)(?: \((\d+) joined\))?'
                 r' \| tx action (\d+), data (\d+)')

MB = 1_000_000


def parse_args(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('session', nargs='?', help='session directory')
    parser.add_argument('--latest', metavar='ROOT',
                        help='pick the newest session under ROOT\\data\\logs')
    parser.add_argument('--json', action='store_true')
    parser.add_argument('--diagnose', action='store_true',
                        help='add existing radio/AWDL evidence around failed uploads')
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
        # The tray prepends its own HH:mm:ss timestamp to Python's timestamp.
        line = re.sub(r'^\d\d:\d\d:\d\d\.\d+\s+(?=\d{4}-)', '', line)
        start = UPLOAD_START.match(line)
        if start:
            if pending is not None:
                pending['status'] = 'incomplete'
                found.append(pending)
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
                'status': 'pending',
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
            pending['status'] = 'complete'
            found.append(pending)
            pending = None
        rejected = REJECTED.match(line)
        if rejected and pending is not None:
            pending.update(end=stamp(rejected.group(1)), status='failed',
                           reason=rejected.group(2))
            found.append(pending)
            pending = None
    if pending is not None:
        found.append(pending)
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
            'rx_action': int(match.group(3)),
            'rx_data': int(match.group(4)),
            'joined': int(match.group(5) or 0),
            'tx_data': int(match.group(7)),
            'tx_action': int(match.group(6)),
        })
    return samples


def failure_context(item, radio_lines, awdl_lines):
    """Correlate host log samples, without inferring air ACKs or phone state.

    Each window must be shorter than 12 h so an HH:mm:ss clock is unambiguous
    around midnight. Missing counters stay missing, rather than becoming zero.
    """
    start, end = item['start'], item.get('end')
    if end is None:
        return {}
    if not 0 <= (end - start).total_seconds() < 12 * 3600:
        return {'unavailable': 'diagnostic window must be shorter than 12 h'}

    def timed(lines):
        for line in lines:
            clock = re.match(r'^(\d\d:\d\d:\d\d\.\d+)', line.lstrip('\ufeff'))
            if not clock:
                continue
            time = dt.datetime.combine(start.date(),
                                       dt.datetime.strptime(clock[1], '%H:%M:%S.%f').time())
            time = min((time - dt.timedelta(days=1), time, time + dt.timedelta(days=1)),
                       key=lambda value: abs((value - start).total_seconds()))
            if start <= time <= end:
                yield time, line

    radio, stats, switches = [], [], []
    for time, line in timed(radio_lines):
        fields = dict(re.findall(r'(\w+)=(-?[\d.]+(?:/-?\d+)?)', line))
        if 'peer frames=' in line:
            sample = {'time': time.isoformat(), 'frames': int(fields['frames'])}
            for key in ('fps', 'rssi', 'rssi_n'):
                if key in fields:
                    sample[key] = float(fields[key])
            if fields.get('thermal_valid') == '1':
                sample['thermal_raw'], sample['thermal_delta'] = map(int, fields['thermal'].split('/'))
            radio.append(sample)
        elif 'radio: stats ' in line:
            stats.append((time, {key: int(value) for key, value in fields.items()}))
        switch = re.search(r'channel (\d+) -> (\d+) \((\d+) us, confirmed retune\)', line)
        if switch:
            switches.append(int(switch[3]))

    positive = [row for row in radio if row['frames'] > 0]
    last = positive[-1] if positive else None
    zeros = [row for row in radio if row['frames'] == 0 and
             (last is None or row['time'] > last['time'])]
    result = {'last_positive_radio_sample': last,
              'first_zero_radio_sample': zeros[0] if zeros else None,
              'zero_tail_samples': len(zeros), 'confirmed_retunes': len(switches),
              'max_retune_us': max(switches) if switches else None}
    for key in ('thermal_raw', 'thermal_delta'):
        values = [row[key] for row in radio if key in row]
        result[key + '_range'] = [min(values), max(values)] if values else None
    idle = re.search(r'nothing arrived for (\d+(?:\.\d+)?) s', item.get('reason', ''))
    if idle:
        idle_seconds = float(idle[1])
        tail_start = end - dt.timedelta(seconds=idle_seconds)
        result['receiver_idle_seconds'] = idle_seconds
        result['receiver_idle_start_estimate'] = tail_start.isoformat()
        # A few radio data frames can be TCP probes/ACKs, not upload payload.
        # Keep the receiver's timeout window even when the radio never hits 0.
        suffix = '_during_receiver_idle'
    elif zeros:
        tail_start = dt.datetime.fromisoformat(zeros[0]['time'])
        suffix = '_after_zero'
    else:
        return result
    # Deltas begin at the first sample AFTER the selected window starts.
    # These are host counter changes, not ACK delivery evidence.
    awdl = [(time, airtime([line])[0]) for time, line in timed(awdl_lines)
            if time >= tail_start and AWD.match(line)]
    tails = {'awdl' + suffix: awdl,
             'radio' + suffix: [(time, row) for time, row in stats if time >= tail_start]}
    for key, rows in tails.items():
        if len(rows) < 2:
            result[key] = None
            continue
        first_time, first = rows[0]
        last_time, last_row = rows[-1]
        counters = ('rx_action', 'rx_data', 'joined', 'tx_action', 'tx_data') if key.startswith('awdl') else tuple(first)
        delta = {name: last_row[name] - first[name] for name in counters}
        result[key] = {'start': first_time.isoformat(), 'end': last_time.isoformat(),
                       'delta': delta, 'counters_reset': any(value < 0 for value in delta.values())}
        if key.startswith('awdl'):
            result[key]['peers_end'] = last_row['peers']
    return result


def main(argv=None):
    args = parse_args(argv)
    session = resolve(args)
    receiver_lines = read_lines(os.path.join(session, 'receiver.log'))
    awdl_lines = read_lines(os.path.join(session, 'awdld.log'))

    transfers = uploads(receiver_lines)
    completed = [item for item in transfers if item['status'] == 'complete']
    received = os.path.join(os.path.dirname(os.path.dirname(session)), 'received')
    for item in completed:
        # A single stored filename is unambiguous; do not split a list on a
        # comma that might be part of a filename. Keep wire bytes separate.
        name = item['name']
        if name and os.path.basename(name) == name:
            path = os.path.join(received, name)
            if os.path.isfile(path):
                item['file_bytes'] = os.path.getsize(path)
    samples = airtime(awdl_lines)
    if args.diagnose:
        radio_lines = read_lines(os.path.join(session, 'radio.log'))
        for item in transfers:
            if item['status'] == 'failed':
                item['link_context'] = failure_context(item, radio_lines, awdl_lines)

    if args.json:
        print(json.dumps({'session': session, 'transfers': [
            {key: (value.isoformat() if isinstance(value, dt.datetime) else value)
             for key, value in item.items()} for item in transfers]}, ensure_ascii=False, indent=2))
        return 0

    print(f'session: {session}')
    if not transfers:
        print('no /Upload found (no phone transfer in this session)')
    for item in transfers:
        if item['status'] != 'complete':
            print(f"  {item['status']}: {item.get('reason', 'no stored-file completion')}")
            if 'link_context' in item:
                print(json.dumps(item['link_context'], ensure_ascii=False, indent=2))
            continue
        seconds = (item['end'] - item['start']).total_seconds()
        size = item.get('file_bytes') or item['total'] or item.get('bytes') or 0
        basis = 'stored file' if 'file_bytes' in item else 'announced/wire; file not verified'
        print(f"  {item['name']}: {size:,} bytes in {seconds:.2f} s "
              f"= {size / MB / seconds:.2f} MB/s ({basis})")

    if len(samples) >= 2:
        window = samples[-1]['time'], samples[0]['time']
        joined = samples[-1]['joined'] - samples[0]['joined']
        data_frames = samples[-1]['rx_data'] - samples[0]['rx_data']
        span = (dt.datetime.strptime(samples[-1]['time'], '%H:%M:%S.%f')
                - dt.datetime.strptime(samples[0]['time'], '%H:%M:%S.%f')).total_seconds()
        if span < 0:
            span += 24 * 60 * 60
        if span == 0:
            return 0
        print(f'awdl: {span:.1f} s, rx data frames {data_frames}, joined {joined} '
              f'({joined / span:.0f} joined/s), tx data {samples[-1]["tx_data"] - samples[0]["tx_data"]}')
        if completed:
            item = completed[-1]
            seconds = (item['end'] - item['start']).total_seconds()
            size = item.get('file_bytes') or item['total'] or item.get('bytes') or 0
            if seconds > 0:
                print(f'   transfer needs {size / seconds / 1400:.0f} frames/s at 1400 B/frame; '
                      f'observed {joined / span:.0f} joined/s overall')
    elif samples:
        print(f"awdl: only one sample ({samples[0]['peers']} peers) - nothing to compare")
    return 0


if __name__ == '__main__':
    sys.exit(main())
