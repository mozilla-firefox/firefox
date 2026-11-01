/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/
 */

add_setup(
  { skip_if: () => !AppConstants.MOZ_MAINTENANCE_SERVICE },
  async function setup() {
    setupTestCommon();
    do_get_profile();
    Services.fog.initializeFOG();
    await initUpdateService();

    registerCleanupFunction(() => {
      Services.prefs.clearUserPref(PREF_APP_UPDATE_SERVICE_ENABLED);
    });

    let patches = getLocalPatchString({
      type: "complete",
      state: STATE_PENDING,
    });
    writeUpdatesToXMLFile(
      getLocalUpdatesXMLString(
        getLocalUpdateString({ appVersion: "2.0" }, patches)
      ),
      true
    );
    writeFile(getUpdateDirFile(FILE_UPDATE_MAR), "test mar contents");
    writeStatusFile(STATE_PENDING);
    await reloadUpdateManagerData();
    await reInitUpdateService();
  }
);

add_task(
  { skip_if: () => !AppConstants.MOZ_MAINTENANCE_SERVICE },
  async function testServicePreferenceTelemetry() {
    for (let isServiceEnabled of [true, false, null]) {
      Services.fog.testResetFOG();
      if (isServiceEnabled === null) {
        Services.prefs.clearUserPref(PREF_APP_UPDATE_SERVICE_ENABLED);
        Assert.ok(
          Services.prefs.getBoolPref(PREF_APP_UPDATE_SERVICE_ENABLED, true),
          "The default should enable the maintenance service"
        );
      } else {
        Services.prefs.setBoolPref(
          PREF_APP_UPDATE_SERVICE_ENABLED,
          isServiceEnabled
        );
      }

      Assert.equal(
        await gAUS.checkForBackgroundUpdates(),
        false,
        "A complete update is already ready, so no network check should start"
      );
      Assert.equal(
        Glean.update.pingCountSubsequent.testGetValue(),
        1,
        "The background update check should record telemetry"
      );
      Assert.equal(
        Glean.update.notPrefUpdateServiceEnabledSubsequent.testGetValue(),
        isServiceEnabled === false ? 1 : null,
        `Only a disabled service preference should be counted (${isServiceEnabled})`
      );
    }

    await doTestFinish();
  }
);
