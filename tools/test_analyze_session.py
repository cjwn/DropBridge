"""Regression fixtures taken from the supervisor's actual log format."""
import unittest
from analyze_session import uploads


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


if __name__ == '__main__':
    unittest.main()
