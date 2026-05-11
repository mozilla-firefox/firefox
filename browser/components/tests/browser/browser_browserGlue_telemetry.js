/* Any copyright is dedicated to the Public Domain.
http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

ChromeUtils.defineESModuleGetters(this, {
  ShellService: "moz-src:///browser/components/shell/ShellService.sys.mjs",
  sinon: "resource://testing-common/Sinon.sys.mjs",
  StartupTelemetry: "moz-src:///browser/components/StartupTelemetry.sys.mjs",
});

// Check that telemetry reports Firefox is not pinned on any OS at startup.
add_task(function check_startup_pinned_telemetry() {
  const scalars = TelemetryTestUtils.getProcessScalars("parent");

  // Check the appropriate telemetry is set or not reported by platform.
  switch (AppConstants.platform) {
    case "win":
      if (
        AppConstants.platform === "win" &&
        Services.sysinfo.getProperty("hasWinPackageId")
      ) {
        TelemetryTestUtils.assertScalar(
          scalars,
          "os.environment.is_taskbar_pinned",
          false,
          "Pin set on win MSIX"
        );
        // Bug 1911343: Pinning regular browsing on MSIX
        // causes false positives when checking for private
        // browsing. As a result no telemetry is logged regarding
        // private pin status.
        TelemetryTestUtils.assertScalarUnset(
          scalars,
          "os.environment.is_taskbar_pinned_private"
        );
      } else {
        TelemetryTestUtils.assertScalar(
          scalars,
          "os.environment.is_taskbar_pinned",
          false,
          "Pin set on win"
        );
        TelemetryTestUtils.assertScalar(
          scalars,
          "os.environment.is_taskbar_pinned_private",
          false,
          "Pin private set on win"
        );
      }
      TelemetryTestUtils.assertScalarUnset(
        scalars,
        "os.environment.is_kept_in_dock"
      );
      break;
    case "macosx":
      TelemetryTestUtils.assertScalarUnset(
        scalars,
        "os.environment.is_taskbar_pinned"
      );
      TelemetryTestUtils.assertScalarUnset(
        scalars,
        "os.environment.is_taskbar_pinned_private"
      );
      TelemetryTestUtils.assertScalar(
        scalars,
        "os.environment.is_kept_in_dock",
        false,
        "Dock set on mac"
      );
      break;
    default:
      TelemetryTestUtils.assertScalarUnset(
        scalars,
        "os.environment.is_taskbar_pinned"
      );
      TelemetryTestUtils.assertScalarUnset(
        scalars,
        "os.environment.is_taskbar_pinned_private"
      );
      TelemetryTestUtils.assertScalarUnset(
        scalars,
        "os.environment.is_kept_in_dock"
      );
      break;
  }
});

// Check that telemetry reports whether Firefox is the default PDF handler.
// This is safe without any explicit coordination because idle tasks are
// guaranteed to have been invokedbefore the test harness invokes the test.  See
// https://searchfox.org/mozilla-central/rev/1674b86019a96f076e0f98f1d0f5f3ab9d4e9020/browser/components/BrowserGlue.jsm#2320-2324
// and
// https://searchfox.org/mozilla-central/rev/1674b86019a96f076e0f98f1d0f5f3ab9d4e9020/browser/base/content/browser.js#2364.
add_task(function check_is_default_handler_telemetry() {
  const scalars = TelemetryTestUtils.getProcessScalars("parent", true);

  const handlers = [".pdf", "mailto"];

  // Check the appropriate telemetry is set or not reported by platform.
  switch (AppConstants.platform) {
    case "win": {
      // We should always set whether we're the default PDF handler.
      Assert.ok("os.environment.is_default_handler" in scalars);

      const keys = Object.keys(scalars["os.environment.is_default_handler"]);
      handlers.every(x => {
        Assert.ok(keys.includes(x), `${x} handler present in telemetry`);
        return true;
      });

      if (Cu.isInAutomation) {
        // But only in automation can we assume we're not the default handler.
        handlers.every(x => {
          TelemetryTestUtils.assertKeyedScalar(
            scalars,
            `os.environment.is_default_handler`,
            x,
            false,
            `Not default ${x} handler on Windows`
          );
          return true;
        });
      }
      break;
    }
    default:
      TelemetryTestUtils.assertScalarUnset(
        scalars,
        "os.environment.is_default_handler"
      );
      break;
  }
});

add_task(async function check_desktop_entry_telemetry() {
  if (AppConstants.platform != "linux") {
    Assert.strictEqual(
      Glean.osEnvironment.desktopEntryExists.testGetValue(),
      null,
      "desktop_entry_exists is unset on non-Linux platforms"
    );
    return;
  }

  // Check that it was set to something before. (This is meant to check that it
  // was set at startup, although if the tests repeat then it might bleed
  // through.)
  Assert.ok(
    Glean.osEnvironment.desktopEntryExists.testGetValue(),
    "desktop_entry_exists was set before"
  );

  let enumValues = {
    [Ci.nsIGNOMEShellService.DESKTOP_ENTRY_ABSENT]: "absent",
    [Ci.nsIGNOMEShellService.DESKTOP_ENTRY_INVISIBLE]: "invisible",
    [Ci.nsIGNOMEShellService.DESKTOP_ENTRY_VISIBLE]: "visible",
    [12345]: "other",
  };
  for (let key of Object.getOwnPropertyNames(enumValues)) {
    let sandbox = sinon.createSandbox();
    let requested = [];
    sandbox.stub(ShellService, "shellService").value({
      getDesktopEntryStatus(name) {
        requested.push(name);
        return key;
      },
    });
    StartupTelemetry.desktopEntryStatus();
    Assert.deepEqual(
      requested,
      [Services.appinfo.remotingName + ".desktop"],
      "The remoting name is passed as the desired desktop entry"
    );
    Assert.equal(
      Glean.osEnvironment.desktopEntryExists.testGetValue(),
      enumValues[key],
      "The telemetry was set to the expected value"
    );
    sandbox.restore();
  }

  let sandbox = sinon.createSandbox();
  sandbox.stub(ShellService, "shellService").value({
    getDesktopEntryStatus(_name) {
      Assert.ok(false, "should not be reached");
      throw new Error("should not be reached");
    },
  });

  StartupTelemetry.desktopEntryStatus({ isRunningUnderFlatpak: true });
  Assert.equal(
    Glean.osEnvironment.desktopEntryExists.testGetValue(),
    "sandboxed",
    "Telemetry indicates the browser is sandboxed when running under Flatpak"
  );

  StartupTelemetry.desktopEntryStatus({ isRunningUnderSnap: true });
  Assert.equal(
    Glean.osEnvironment.desktopEntryExists.testGetValue(),
    "sandboxed",
    "Telemetry indicates the browser is sandboxed when running under Snap"
  );

  sandbox.restore();
});
