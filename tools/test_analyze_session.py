"""Regression fixtures taken from the supervisor's actual log format."""
import unittest
import datetime as dt
from analyze_session import uploads, failure_context


class UploadLogTest(unittest.TestCase):
    def test_prefixed_success_keeps_archive_and_file_size_distinct(self):
        rows = uploads([
            "14:00:00.001 2026-10-28 14:00:00,000 DEBUG POST /Upload headers: {'TotalBytes': '1000000'}",
            "14:00:01.001 2026-10-28 14:00:01,000 INFO upload x: 1000520 bytes received",
            "14:00:02.001 2026-10-28 14:00:02,000 INFO upload x: stored clip.MP4",
        ])
        self.assertEqual(len(rows), 1)
        self.assertEqual(rows[0]['status'], 'complete')
        self.assertEqual(rows[0]['total'], 1000000)
        self.assertEqual(rows[0]['bytes'], 1000520)
        self.assertEqual((rows[0]['end'] - rows[0]['start']).total_seconds(), 2)

    def test_timeout_then_retry_never_counts_failure_as_success(self):
        rows = uploads([
            "2026-10-28 14:00:00,000 DEBUG POST /Upload headers: {'TotalBytes': '100'}",
            "2026-10-28 14:00:30,000 WARNING /Upload rejected: upload x: stalled",
            "2026-10-28 14:00:40,000 DEBUG POST /Upload headers: {'TotalBytes': '100'}",
            "2026-10-28 14:00:41,000 INFO upload y: stored retry.MP4",
        ])
        self.assertEqual([row['status'] for row in rows], ['failed', 'complete'])
        self.assertEqual(rows[1]['name'], 'retry.MP4')

    def test_active_upload_is_visible(self):
        rows = uploads(["2026-10-28 14:00:00,000 DEBUG POST /Upload headers: {}"])
        self.assertEqual(rows[0]['status'], 'pending')

    def test_failure_context_across_midnight_preserves_missing_evidence(self):
        item = {'start': dt.datetime(2026, 10, 28, 23, 59, 58),
                'end': dt.datetime(2026, 10, 29, 0, 0, 9)}
        radio = [
            '23:59:59.000 radio: peer frames=10 fps=2 rssi=60 rssi_n=10 thermal=29/3 thermal_valid=1',
            '00:00:00.000 radio: peer frames=0 fps=0 rssi=0 rssi_n=0 thermal=0/0 thermal_valid=0',
            '00:00:01.000 radio: channel 6 -> 149 (1559 us, confirmed retune)',
            '00:00:05.000 radio: peer frames=0 fps=0 rssi=0 rssi_n=0 thermal=29/3 thermal_valid=1',
        ]
        awdl = [
            '00:00:01.000 INFO peers 1 | rx action 10, data 50 (40 joined) | tx action 20, data 5',
            '00:00:07.000 INFO peers 1 | rx action 15, data 50 (40 joined) | tx action 25, data 5',
        ]
        row = failure_context(item, radio, awdl)
        self.assertEqual(row['first_zero_radio_sample']['time'], '2026-10-29T00:00:00')
        self.assertEqual(row['zero_tail_samples'], 2)
        self.assertEqual(row['thermal_raw_range'], [29, 29])
        self.assertEqual(row['awdl_after_zero']['delta']['rx_action'], 5)
        self.assertEqual(row['awdl_after_zero']['delta']['rx_data'], 0)
        self.assertIsNone(row['radio_after_zero'])
        self.assertEqual(row['max_retune_us'], 1559)

    def test_failure_context_does_not_treat_initial_idle_as_final_stall(self):
        item = {'start': dt.datetime(2026, 10, 28, 14),
                'end': dt.datetime(2026, 10, 28, 14, 0, 20)}
        row = failure_context(item, [
            '14:00:01.000 radio: peer frames=0',
            '14:00:06.000 radio: peer frames=10',
        ], [])
        self.assertIsNone(row['first_zero_radio_sample'])
        self.assertIsNone(row['thermal_raw_range'])
        self.assertIsNone(row['max_retune_us'])


if __name__ == '__main__':
    unittest.main()
