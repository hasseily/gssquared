#!/usr/bin/env python3
"""Check that guest startup retries only the expected main-thread timeout."""
import unittest
from unittest.mock import Mock, patch

import test_appletini_guest as guest


class StartupTests(unittest.TestCase):
    def setUp(self):
        self.now = 0.0
        self.clock = patch.object(guest.time, "monotonic", side_effect=lambda: self.now)
        self.sleep = patch.object(guest.time, "sleep", side_effect=self.advance)
        self.clock.start()
        self.sleep.start()
        self.addCleanup(self.clock.stop)
        self.addCleanup(self.sleep.stop)

    def advance(self, seconds):
        self.now += seconds

    def test_retry_uses_remaining_startup_budget(self):
        c = Mock()

        def request(*args, **kwargs):
            if c.request.call_count == 1:
                self.advance(2)
                raise guest.ProtocolError(6, "timeout waiting for main thread")
            return b""

        c.request.side_effect = request
        guest.pause_when_ready(c, 10)
        self.assertEqual(c.request.call_count, 2)
        self.assertEqual(c.request.call_args_list[0].kwargs["timeout"], 10)
        self.assertAlmostEqual(c.request.call_args_list[1].kwargs["timeout"], 7.9)
        c.wait_stopped.assert_called_once_with(timeout=5)

    def test_other_protocol_errors_fail_immediately(self):
        for code, message in [(6, "no machine"), (6, "internal error"),
                              (5, "timeout waiting for main thread")]:
            with self.subTest(code=code, message=message):
                error = guest.ProtocolError(code, message)
                c = Mock()
                c.request.side_effect = error
                with self.assertRaises(guest.ProtocolError) as caught:
                    guest.pause_when_ready(c, 10)
                self.assertIs(caught.exception, error)
                c.request.assert_called_once()
                c.wait_stopped.assert_not_called()

    def test_expired_budget_sends_no_request(self):
        c = Mock()
        with self.assertRaises(TimeoutError):
            guest.pause_when_ready(c, 0)
        c.request.assert_not_called()

    def test_repeated_bridge_timeout_expires(self):
        c = Mock()

        def request(*args, **kwargs):
            self.advance(kwargs["timeout"])
            raise guest.ProtocolError(6, "timeout waiting for main thread")

        c.request.side_effect = request
        with self.assertRaises(TimeoutError):
            guest.pause_when_ready(c, 1)
        c.request.assert_called_once()
        c.wait_stopped.assert_not_called()

    def test_stop_event_uses_remaining_budget(self):
        c = Mock()
        c.request.return_value = b""
        guest.pause_when_ready(c, 0.25)
        c.wait_stopped.assert_called_once_with(timeout=0.25)

    def test_transport_timeout_and_bad_reply_do_not_retry(self):
        c = Mock()
        c.request.side_effect = TimeoutError("socket timed out")
        with self.assertRaises(TimeoutError):
            guest.pause_when_ready(c, 1)
        c.request.assert_called_once()
        c = Mock()
        c.request.return_value = b"unexpected"
        with self.assertRaises(guest.ProtocolError):
            guest.pause_when_ready(c, 1)
        c.request.assert_called_once()


if __name__ == "__main__":
    unittest.main()
