# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

from marionette_harness import parameterized
from telemetry_harness.testcase import TelemetryTestCase


class TestShutdownPingsSucced(TelemetryTestCase):
    """Test Firefox shutdown pings."""

    def tearDown(self):
        super(TestShutdownPingsSucced, self).tearDown()

        # We need a fresh profile next run in order that the "new-profile" and
        # "first-shutdown" pings are sent.
        self.marionette.profile = None

    @parameterized("pingsender1", pingsender_version=b"1.0")
    @parameterized("pingsender2", pingsender_version=b"2.0")
    def test_shutdown_pings_succeed(self, pingsender_version=b""):
        """Test that known Firefox shutdown pings are received, with the correct
        X-PingSender-Version headers."""

        pingsender2_enabled = {b"1.0": False, b"2.0": True}[pingsender_version]
        self.marionette.set_pref(
            "toolkit.telemetry.shutdownPingSender.backgroundtask.enabled",
            pingsender2_enabled,
        )

        # Map ping type to expected X-PingSender-Version header.  Not all pings
        # will be sent via pingsender, so they might have an empty (binary)
        # string version.
        ping_types = {
            "event": pingsender_version,
            "first-shutdown": pingsender_version,
            "main": b"",
            "new-profile": pingsender_version,
        }

        # You'd think we could just `quit_browser`, but the test needs a second
        # session for the "main" ping to be sent and the harness needs it for
        # cleanup, so we could `restart_browser`, but in bug 2012689 we found
        # that Windows opt could restart the browser faster than invoking
        # pingsender... so we `quit_browser` and then `start_browser`.
        pings = self.wait_for_pings(
            lambda: self.quit_browser() and self.start_browser(),
            lambda p: p["type"] in ping_types.keys(),
            len(ping_types),
        )

        self.assertEqual(len(pings), len(ping_types))
        self.assertEqual(set(ping_types.keys()), set(p["type"] for p in pings))

        self.assertEqual(
            ping_types, dict((p["type"], p["X-PingSender-Version"]) for p in pings)
        )
