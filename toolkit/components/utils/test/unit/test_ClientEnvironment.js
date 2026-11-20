/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/ */
"use strict";

ChromeUtils.defineESModuleGetters(this, {
  AppConstants: "resource://gre/modules/AppConstants.sys.mjs",
  ClientEnvironmentBase:
    "resource://gre/modules/components-utils/ClientEnvironment.sys.mjs",
  Region: "resource://gre/modules/Region.sys.mjs",
  RegionTestUtils: "resource://testing-common/RegionTestUtils.sys.mjs",
  TelemetryController: "resource://gre/modules/TelemetryController.sys.mjs",
  TestUtils: "resource://testing-common/TestUtils.sys.mjs",
  updateAppInfo: "resource://testing-common/AppInfo.sys.mjs",
});

const UUID_REGEX =
  /[0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{12}/i;

add_setup(() => {
  updateAppInfo();
});

add_task(async function test_OS_data() {
  const os = ClientEnvironmentBase.os;
  Assert.notStrictEqual(
    os,
    undefined,
    "OS data should be available in the context"
  );

  equal(os.name, ClientEnvironmentBase.appinfo.OS);
  equal(typeof os.version, "string", "Version is a string");

  let osCount = 0;
  if (os.isWindows) {
    osCount += 1;
  }
  if (os.isMac) {
    osCount += 1;
  }
  if (os.isLinux) {
    osCount += 1;
  }
  Assert.lessOrEqual(osCount, 1, "At most one OS should match");

  // if on Windows, Windows versions should be set, and Mac versions should not be
  if (os.isWindows) {
    equal(
      typeof os.windowsVersion,
      "number",
      "Windows version should be a number"
    );
    equal(
      typeof os.windowsBuildNumber,
      "number",
      "Windows build number should be a number"
    );
    Assert.ok(
      os.windowsUBR === null || typeof os.windowsUBR == "number",
      "Windows UBR should be a number or null"
    );
    equal(os.macVersion, null, "Mac version should not be set");
    equal(os.darwinVersion, null, "Darwin version should not be set");
  }

  // if on Mac, Mac versions should be set, and Windows versions should not be
  if (os.isMac) {
    equal(typeof os.macVersion, "number", "Mac version should be a number");
    equal(
      typeof os.darwinVersion,
      "number",
      "Darwin version should be a number"
    );
    equal(os.windowsVersion, null, "Windows version should not be set");
    equal(
      os.windowsBuildNumber,
      null,
      "Windows build number version should not be set"
    );
    equal(os.windowsUBR, null, "Windows UBR should not be set");
  }

  // if on Linux, no versions should be set
  if (os.isLinux) {
    equal(os.macVersion, null, "Mac version should not be set");
    equal(os.darwinVersion, null, "Darwin version should not be set");
    equal(os.windowsVersion, null, "Windows version should not be set");
    equal(
      os.windowsBuildNumber,
      null,
      "Windows build number version should not be set"
    );
    equal(os.windowsUBR, null, "Windows UBR should not be set");
  }
});

add_task(
  {
    // Getting `ClientEnvironmentBase.attribution` lazy-imports
    // `AttributionCode.sys.mjs`, which lives in `browser/`. See
    // bug 2066393.
    skip_if: () => AppConstants.MOZ_BUILD_APP != "browser",
  },
  async function test_attributionData() {
    try {
      await ClientEnvironmentBase.attribution;
    } catch (ex) {
      equal(
        ex.result,
        Cr.NS_ERROR_FILE_NOT_FOUND,
        "Test environment does not have attribution data"
      );
    }
  }
);

add_task(async function testLiveTelemetry() {
  // Setup telemetry so we can read from it
  do_get_profile(true);
  await TelemetryController.testSetup();

  equal(
    ClientEnvironmentBase.liveTelemetry.main.environment.system.os.name,
    Services.sysinfo.getProperty("name"),
    "Telemetry data is available"
  );

  Assert.throws(
    () => ClientEnvironmentBase.liveTelemetry.anotherPingType,
    /Live telemetry.*anotherPingType/,
    "Non-main pings should raise an error if accessed"
  );

  // Put things back the way we found them
  await TelemetryController.testShutdown();
});

add_task(function testBuildId() {
  Assert.notStrictEqual(
    ClientEnvironmentBase.appinfo,
    undefined,
    "appinfo should be available in the context"
  );
  Assert.strictEqual(
    typeof ClientEnvironmentBase.appinfo,
    "object",
    "appinfo should be an object"
  );
  Assert.strictEqual(
    typeof ClientEnvironmentBase.appinfo.appBuildID,
    "string",
    "buildId should be a string"
  );
});

add_task(
  {
    skip_if: () => AppConstants.MOZ_BUILD_APP != "browser",
  },
  async function testLegacyRandomizationId() {
    // Should generate an id if none is set
    await Services.prefs.clearUserPref("app.normandy.user_id");
    Assert.ok(
      UUID_REGEX.test(ClientEnvironmentBase.legacyRandomizationId),
      "legacyRandomizationId should be available"
    );

    // Should read the right preference
    await Services.prefs.setStringPref("app.normandy.user_id", "fake id");
    Assert.equal(
      ClientEnvironmentBase.legacyRandomizationId,
      "fake id",
      "legacyRandomizationId should read from preferences"
    );
  }
);

add_task(async function test_country() {
  Region._setHomeRegion(null, false);
  RegionTestUtils.setNetworkRegion("FR");
  await Region._fetchRegion();

  equal(ClientEnvironmentBase.country, "FR");
});

add_task(async function test_formFactor() {
  const env = ClientEnvironmentBase;
  if (["android", "ios"].includes(AppConstants.platform)) {
    equal(env.formFactor, "phone");
  } else {
    equal(env.formFactor, "desktop");
  }
});

add_task(async function testTelemetry() {
  // setup
  Services.prefs.setBoolPref("privacy.reduceTimerPrecision", true);
  Services.prefs.setBoolPref("toolkit.telemetry.archive.enabled", true);
  do_get_profile(true);
  await TelemetryController.testReset();

  await TelemetryController.submitExternalPing("testfoo", { foo: 1 });
  await TelemetryController.submitExternalPing("testbar", { bar: 2 });
  const lastPingTime = Date.now();
  await TestUtils.waitForCondition(() => Date.now() > lastPingTime);
  await TelemetryController.submitExternalPing("testfoo", { foo: 3 });

  // Test it can access telemetry
  const telemetry = await ClientEnvironmentBase.telemetry;
  equal(typeof telemetry, "object", "Telemetry is accesible");

  // Test it reads different types of telemetry
  equal(
    telemetry.testfoo.payload.foo,
    3,
    "telemetry filters pull the latest ping from a type"
  );
  equal(
    telemetry.testbar.payload.bar,
    2,
    "telemetry filters pull from submitted telemetry pings"
  );

  await TelemetryController.testShutdown();
  Services.prefs.clearUserPref("privacy.reduceTimerPrecision");
  Services.prefs.clearUserPref("toolkit.telemetry.archive.enabled");
});

add_task(async function testDistribution() {
  // distribution id defaults to "default"
  equal(
    ClientEnvironmentBase.distribution,
    "default",
    "distribution has a default value"
  );

  // distribution id is read from a preference
  Services.prefs
    .getDefaultBranch(null)
    .setCharPref("distribution.id", "funnelcake");
  equal(
    ClientEnvironmentBase.distribution,
    "funnelcake",
    "distribution is read from preferences"
  );
  Services.prefs
    .getDefaultBranch(null)
    .setCharPref("distribution.id", "default");
});

add_task(async function testSync() {
  equal(
    ClientEnvironmentBase.syncMobileDevices,
    0,
    "syncMobileDevices defaults to zero"
  );
  equal(
    ClientEnvironmentBase.syncDesktopDevices,
    0,
    "syncDesktopDevices defaults to zero"
  );
  equal(
    ClientEnvironmentBase.syncTotalDevices,
    0,
    "syncTotalDevices defaults to zero"
  );
  Services.prefs.setIntPref("services.sync.clients.devices.mobile", 5);
  Services.prefs.setIntPref("services.sync.clients.devices.desktop", 4);
  equal(
    ClientEnvironmentBase.syncMobileDevices,
    5,
    "syncMobileDevices is read when set"
  );
  equal(
    ClientEnvironmentBase.syncDesktopDevices,
    4,
    "syncDesktopDevices is read when set"
  );
  equal(
    ClientEnvironmentBase.syncTotalDevices,
    9,
    "syncTotalDevices is read when set"
  );
  Services.prefs.clearUserPref("services.sync.clients.devices.mobile");
  Services.prefs.clearUserPref("services.sync.clients.devices.desktop");
});

add_task(async function testDoNotTrack() {
  // doNotTrack defaults to false
  ok(!ClientEnvironmentBase.doNotTrack, "doNotTrack has a default value");

  // doNotTrack is read from a preference
  Services.prefs.setBoolPref("privacy.donottrackheader.enabled", true);
  ok(ClientEnvironmentBase.doNotTrack, "doNotTrack is read from preferences");
  Services.prefs.clearUserPref("privacy.donottrackheader.enabled");
});
