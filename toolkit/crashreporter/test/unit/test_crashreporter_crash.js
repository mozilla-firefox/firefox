add_task(async function run_test() {
  var is_win7_or_newer = false;
  var is_windows = false;
  var ph = Cc["@mozilla.org/network/protocol;1?name=http"].getService(
    Ci.nsIHttpProtocolHandler
  );
  var match = ph.userAgent.match(/Windows NT (\d+).(\d+)/);
  if (match) {
    is_windows = true;
  }
  if (
    match &&
    (parseInt(match[1]) > 6 ||
      (parseInt(match[1]) == 6 && parseInt(match[2]) >= 1))
  ) {
    is_win7_or_newer = true;
  }

  registerCleanupFunction(async () => {
    Services.prefs.clearUserPref("termsofuse.bypassNotification");
  });

  // try a basic crash
  await do_content_crash(null, function (mdump, extra) {
    Assert.ok(mdump.exists());
    Assert.greater(mdump.fileSize, 0);
    Assert.ok("StartupTime" in extra, "StartupTime annotation is present");
    Assert.ok("CrashTime" in extra, "CrashTime annotation is present");
    Assert.ok("UptimeTS" in extra, "UptimeTS annotation is present");
    Assert.ok(
      CrashTestUtils.dumpHasStream(
        mdump.path,
        CrashTestUtils.MD_THREAD_LIST_STREAM
      )
    );
    Assert.ok(CrashTestUtils.dumpHasInstructionPointerMemory(mdump.path));
    if (is_windows) {
      [
        "SystemMemoryUsePercentage",
        "TotalVirtualMemory",
        "AvailableVirtualMemory",
        "AvailablePageFile",
        "AvailablePhysicalMemory",
      ].forEach(function (prop) {
        Assert.ok(/^\d+$/.test(extra[prop].toString()));
      });
    }
    if (is_win7_or_newer) {
      Assert.ok(
        CrashTestUtils.dumpHasStream(
          mdump.path,
          CrashTestUtils.MD_MEMORY_INFO_LIST_STREAM
        )
      );
    }
  });

  // check setting some basic data
  await do_crash(
    async function () {
      // Add various annotations
      crashReporter.annotateCrashReport("TestKey", "TestValue");
      crashReporter.annotateCrashReport(
        "TestUnicode",
        "\u{1F4A9}\n\u{0000}Escape"
      );
      crashReporter.annotateCrashReport("TestBoolean", true);
      crashReporter.annotateCrashReport("TestInteger", 1000);
      crashReporter.annotateCrashReport("Add-ons", "test%40mozilla.org:0.1");
      crashReporter.appendAppNotesToCrashReport("Junk");
      crashReporter.appendAppNotesToCrashReport("MoreJunk");

      // TelemetrySession setup will trigger the session annotation
      let { TelemetryController } = ChromeUtils.importESModule(
        "resource://gre/modules/TelemetryController.sys.mjs"
      );
      await TelemetryController.testSetup();
    },
    function (mdump, extra) {
      Assert.ok("StartupTime" in extra, "StartupTime annotation is present");
      Assert.ok("CrashTime" in extra, "CrashTime annotation is present");
      Assert.ok("UptimeTS" in extra, "UptimeTS annotation is present");

      Assert.equal(extra.TestKey, "TestValue");
      Assert.equal(extra.TestUnicode, "\u{1F4A9}\n\u{0000}Escape");
      Assert.ok(
        extra.Notes.endsWith("JunkMoreJunk"),
        "Should include our notes"
      );
      Assert.equal(extra["Add-ons"], "test%40mozilla.org:0.1");
      const UUID_REGEX =
        /^[0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{12}$/i;
      Assert.ok(
        "TelemetrySessionId" in extra,
        "The TelemetrySessionId field is present in the extra file"
      );
      Assert.ok(
        UUID_REGEX.test(extra.TelemetrySessionId),
        "The TelemetrySessionId is a UUID"
      );
      Assert.ok(
        !("TelemetryClientId" in extra),
        "The TelemetryClientId field is omitted by default"
      );
      Assert.ok(
        !("TelemetryProfileGroupId" in extra),
        "The TelemetryProfileGroupId field is omitted by default"
      );
      Assert.ok(
        !("TelemetryServerURL" in extra),
        "The TelemetryServerURL field is omitted by default"
      );
    }
  );

  await do_crash(
    async function () {
      // Enable the FHR, official policy bypass (since we're in a test) and
      // specify a telemetry server & client ID.
      Services.prefs.setBoolPref("termsofuse.bypassNotification", true);
      Services.prefs.setBoolPref("browser.preonboarding.enabled", false);
      Services.prefs.setBoolPref(
        "datareporting.policy.dataSubmissionPolicyBypassNotification",
        true
      );
      Services.prefs.setBoolPref(
        "datareporting.healthreport.uploadEnabled",
        true
      );
      Services.prefs.setCharPref(
        "toolkit.telemetry.server",
        "http://a.telemetry.server"
      );
      Services.prefs.setIntPref("telemetry.fog.test.localhost_port", -1);
      Services.prefs.setCharPref(
        "toolkit.telemetry.cachedClientID",
        "f3582dee-22b9-4d73-96d1-79ef5bf2fc24"
      );
      Services.prefs.setCharPref(
        "toolkit.telemetry.cachedProfileGroupID",
        "6b63fbca-5f99-4a37-b55f-d4e4a2d3ca51"
      );

      // TelemetrySession setup will trigger the session annotation
      let { TelemetryController } = ChromeUtils.importESModule(
        "resource://gre/modules/TelemetryController.sys.mjs"
      );
      let { TelemetrySend } = ChromeUtils.importESModule(
        "resource://gre/modules/TelemetrySend.sys.mjs"
      );
      TelemetrySend.setTestModeEnabled(true);
      await TelemetryController.testSetup();
    },
    function (mdump, extra) {
      Assert.ok(
        "TelemetryClientId" in extra,
        "The TelemetryClientId field is present when the FHR is on"
      );
      Assert.equal(
        extra.TelemetryClientId,
        "f3582dee-22b9-4d73-96d1-79ef5bf2fc24",
        "The TelemetryClientId matches the expected value"
      );
      Assert.ok(
        "TelemetryProfileGroupId" in extra,
        "The TelemetryProfileGroupId field is present when the FHR is on"
      );
      Assert.equal(
        extra.TelemetryProfileGroupId,
        "6b63fbca-5f99-4a37-b55f-d4e4a2d3ca51",
        "The TelemetryProfileGroupId matches the expected value"
      );
      Assert.ok(
        "TelemetryServerURL" in extra,
        "The TelemetryServerURL field is present when the FHR is on"
      );
      Assert.equal(
        extra.TelemetryServerURL,
        "http://a.telemetry.server",
        "The TelemetryServerURL matches the expected value"
      );
    }
  );

  await do_crash(
    async function () {
      // Disable the FHR upload, no telemetry annotations should be present.
      Services.prefs.setBoolPref(
        "datareporting.policy.dataSubmissionPolicyBypassNotification",
        true
      );
      Services.prefs.setBoolPref("termsofuse.bypassNotification", true);
      Services.prefs.setBoolPref(
        "datareporting.healthreport.uploadEnabled",
        false
      );

      // TelemetrySession setup will trigger the session annotation
      let { TelemetryController } = ChromeUtils.importESModule(
        "resource://gre/modules/TelemetryController.sys.mjs"
      );
      let { TelemetrySend } = ChromeUtils.importESModule(
        "resource://gre/modules/TelemetrySend.sys.mjs"
      );
      TelemetrySend.setTestModeEnabled(true);
      await TelemetryController.testSetup();
    },
    function (mdump, extra) {
      Assert.ok(
        !("TelemetryClientId" in extra),
        "The TelemetryClientId field is omitted when FHR upload is disabled"
      );
      Assert.ok(
        !("TelemetryProfileGroupId" in extra),
        "The TelemetryProfileGroupId field is omitted when FHR upload is disabled"
      );
      Assert.ok(
        !("TelemetryServerURL" in extra),
        "The TelemetryServerURL field is omitted when FHR upload is disabled"
      );
    }
  );

  await do_crash(
    async function () {
      // No telemetry annotations should be present if the user has not been
      // notified yet
      Services.prefs.setBoolPref(
        "datareporting.policy.dataSubmissionPolicyBypassNotification",
        false
      );
      Services.prefs.setBoolPref(
        "datareporting.healthreport.uploadEnabled",
        true
      );

      // TelemetrySession setup will trigger the session annotation
      let { TelemetryController } = ChromeUtils.importESModule(
        "resource://gre/modules/TelemetryController.sys.mjs"
      );
      let { TelemetrySend } = ChromeUtils.importESModule(
        "resource://gre/modules/TelemetrySend.sys.mjs"
      );
      TelemetrySend.setTestModeEnabled(true);
      await TelemetryController.testSetup();
    },
    function (mdump, extra) {
      Assert.ok(
        !("TelemetryClientId" in extra),
        "The TelemetryClientId field is omitted when FHR upload is disabled"
      );
      Assert.ok(
        !("TelemetryProfileGroupId" in extra),
        "The TelemetryProfileGroupId field is omitted when FHR upload is disabled"
      );
      Assert.ok(
        !("TelemetryServerURL" in extra),
        "The TelemetryServerURL field is omitted when FHR upload is disabled"
      );
    }
  );
});
