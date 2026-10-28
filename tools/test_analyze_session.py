"""Regression fixtures taken from the supervisor's actual log format."""
import unittest
import datetime as dt
from analyze_session import uploads, failure_context, netd_summary


class NetdSummaryTest(unittest.TestCase):
    """netd counters are the only host-side signal for dropped inbound frames."""

    def test_deltas_and_drop_detection(self):
        rows = netd_summary([
            '19:50:20.554 awdl_netd: ipc stats rx_seen=100 rx_bytes=140000 rx_queued=98 '
            'alloc_fail=0 copy_fail=1 input_fail=2 tx_frames=10 tx_bytes=1000 tx_fail=0',
            '19:50:25.554 awdl_netd: ipc stats rx_seen=600 rx_bytes=840000 rx_queued=590 '
            'alloc_fail=0 copy_fail=1 input_fail=12 tx_frames=60 tx_bytes=6000 tx_fail=0',
            '19:50:25.554 awdl_netd: tcp flow=1 t_ms=5 dir=rx remote_port=50000 local_port=8771 '
            'packets=600 payload=840000 accepted=590 failed=10 seq=1 ack=2 win_raw=65535 '
            'ws_offer=0 syn=1 fin=0 rst=0 zero_window=0 gap=0 overlap=0 repeat_ack=0 '
            'data_idle_ms=30123 ack_idle_ms=5',
            '19:50:25.554 awdl_netd: tcp flow=1 t_ms=5 dir=tx remote_port=50000 local_port=8771 '
            'packets=60 payload=6000 accepted=60 failed=0 seq=2 ack=1 win_raw=61440 '
            'ws_offer=2 syn=1 fin=0 rst=0 zero_window=0 gap=0 overlap=0 repeat_ack=0 '
            'data_idle_ms=-1 ack_idle_ms=5',
            '19:50:25.554 awdl_netd: proxy flow=1 t_ms=5 active=0 up_read=840000 '
            'up_written=840000 down_read=6000 down_written=6000 up_phase=lwip_eof '
            'down_phase=local_eof',
        ])
        self.assertEqual(rows['samples'], 2)
        self.assertFalse(rows['counters_reset'])
        self.assertEqual(rows['totals']['rx_seen'], 600)
        self.assertEqual(rows['totals']['rx_bytes'], 840000)
        self.assertEqual(rows['totals']['input_fail'], 12)
        self.assertEqual(rows['totals']['copy_fail'], 1)
        self.assertEqual(rows['flows']['1/rx']['payload_bytes'], 840000)
        self.assertEqual(rows['flows']['1/rx']['failed'], 10)
        self.assertEqual(rows['flows']['1/rx']['data_idle_ms_at_end'], 30123)
        self.assertEqual(rows['proxy']['1']['up_written'], 840000)
        self.assertEqual(rows['proxy']['1']['active'], 0)
        self.assertEqual(rows['proxy']['1']['up_phase'], 'lwip_eof')

    def test_restart_inside_session_uses_last_lifetime(self):
        rows = netd_summary([
            '10:00:00.001 awdl_netd: ipc stats rx_seen=900 rx_bytes=9 rx_queued=9 '
            'alloc_fail=0 copy_fail=0 input_fail=4 tx_frames=1 tx_bytes=1 tx_fail=0',
            '10:00:05.001 awdl_netd: ipc stats rx_seen=5 rx_bytes=5 rx_queued=5 '
            'alloc_fail=0 copy_fail=0 input_fail=1 tx_frames=1 tx_bytes=1 tx_fail=0',
        ])
        self.assertTrue(rows['counters_reset'])
        self.assertEqual(rows['totals']['rx_seen'], 5)
        self.assertEqual(rows['totals']['input_fail'], 1)

    def test_no_netd_log_is_not_an_error(self):
        self.assertIsNone(netd_summary([]))
        self.assertIsNone(netd_summary(['10:00:00.001 awdl_netd: AWDL data plane up']))

    def test_input_retry_field_is_optional(self):
        """Older logs have no input_retry counter; newer ones must parse it."""
        old = netd_summary([
            '10:00:00.001 awdl_netd: ipc stats rx_seen=1 rx_bytes=2 rx_queued=1 '
            'alloc_fail=0 copy_fail=0 input_fail=3 tx_frames=1 tx_bytes=2 tx_fail=0',
        ])
        self.assertEqual(old['totals']['input_fail'], 3)
        self.assertEqual(old['totals']['input_retry'], 0)
        new = netd_summary([
            '10:00:00.001 awdl_netd: ipc stats rx_seen=1 rx_bytes=2 rx_queued=1 '
            'alloc_fail=0 copy_fail=0 input_fail=0 input_retry=7 '
            'tx_frames=1 tx_bytes=2 tx_fail=0',
        ])
        self.assertEqual(new['totals']['input_retry'], 7)
        self.assertEqual(new['totals']['tx_fail'], 0)


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

    def test_receiver_timeout_keeps_context_despite_sparse_radio_frames(self):
        item = {'start': dt.datetime(2026, 10, 28, 14),
                'end': dt.datetime(2026, 10, 28, 14, 1),
                'reason': 'request body read timed out, nothing arrived for 30 s'}
        row = failure_context(item, [
            '14:00:40.000 radio: peer frames=1',
            '14:00:45.000 radio: stats cb=100 tx=10 failed=0',
            '14:00:55.000 radio: stats cb=110 tx=12 failed=0',
        ], [])
        self.assertIsNone(row['first_zero_radio_sample'])
        self.assertEqual(row['receiver_idle_start_estimate'], '2026-10-28T14:00:30')
        self.assertEqual(row['radio_during_receiver_idle']['delta']['tx'], 2)
        self.assertIsNone(row['awdl_during_receiver_idle'])


if __name__ == '__main__':
    unittest.main()
