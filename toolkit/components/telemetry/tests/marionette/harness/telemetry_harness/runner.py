# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

from marionette_harness import BaseMarionetteTestRunner

from telemetry_harness.testcase import TelemetryTestCase

SERVER_URL = "http://localhost:8000"


class TelemetryTestRunner(BaseMarionetteTestRunner):
    """TestRunner for the telemetry-tests-client suite."""

    def __init__(self, **kwargs):
        """Set test variables and preferences specific to Firefox client
        telemetry.
        """

        # Select the appropriate GeckoInstance
        kwargs["app"] = "fxdesktop"

        prefs = kwargs.pop("prefs", {})

        prefs["fission.autostart"] = True
        if kwargs["disable_fission"]:
            prefs["fission.autostart"] = False

        # Set Firefox Client Telemetry specific preferences
        prefs.update({
            # Clear the region detection url to
            #   * avoid net access in tests
            #   * stabilize browser.search.region to avoid extra subsessions (bug 1579840#c40)
            "browser.region.network.url": "",
            # Disable smart sizing because it changes prefs at startup. (bug 1547750)
            "browser.cache.disk.smart_size.enabled": False,
            "toolkit.telemetry.server": f"{SERVER_URL}/pings",
            "telemetry.fog.test.localhost_port": -1,
            "toolkit.telemetry.initDelay": 1,
            "toolkit.telemetry.minSubsessionLength": 0,
            "datareporting.healthreport.uploadEnabled": True,
            "datareporting.policy.dataSubmissionEnabled": True,
            "datareporting.policy.dataSubmissionPolicyBypassNotification": True,
            "datareporting.usage.uploadEnabled": True,
            "toolkit.telemetry.log.level": "Trace",
            "toolkit.telemetry.log.dump": True,
            "toolkit.telemetry.send.overrideOfficialCheck": True,
            "toolkit.telemetry.testing.disableFuzzingDelay": True,
            # Disable Nimbus to avoid extra subsessions due to Experiment
            # activation in tests (bug 1641571, bug 1608807)
            "app.shield.optoutstudies.enabled": False,
            "nimbus.rollouts.enabled": False,
            # Bug 1789727: Keep the screenshots extension disabled to avoid
            # disabling the addon resulting in extra subsessions
            "screenshots.browser.component.enabled": False,
        })

        super().__init__(prefs=prefs, **kwargs)

        self.testvars["server_root"] = kwargs["server_root"]
        self.testvars["server_url"] = SERVER_URL

        self.test_handlers = [TelemetryTestCase]
