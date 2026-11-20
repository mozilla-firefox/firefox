"use strict";

const { FirstStartup } = ChromeUtils.importESModule(
  "resource://gre/modules/FirstStartup.sys.mjs"
);
const { updateAppInfo } = ChromeUtils.importESModule(
  "resource://testing-common/AppInfo.sys.mjs"
);

const CATEGORY_NAME = "first-startup-new-profile";
const PREF_TIMEOUT = "first-startup.timeout";

add_setup(function test_setup() {
  // FOG needs a profile directory to put its data in.
  do_get_profile();

  // FOG needs to be initialized in order for data to flow.
  Services.fog.initializeFOG();

  // Delete any categories that have been registered statically so that we're
  // just running the one here under test.
  Services.catMan.deleteCategory(CATEGORY_NAME);
});

add_task(async function test_success() {
  updateAppInfo();

  FirstStartup.resetForTesting();

  await GleanPings.firstStartup.testSubmission(
    () => {
      if (
        AppConstants.MOZ_BUILD_APP === "browser" ||
        AppConstants.MOZ_UPDATE_AGENT
      ) {
        Assert.equal(FirstStartup.state, FirstStartup.SUCCESS);
        Assert.ok(Glean.firstStartup.newProfile.testGetValue());
        Assert.equal(
          Glean.firstStartup.statusCode.testGetValue(),
          FirstStartup.SUCCESS
        );

        if (AppConstants.MOZ_BUILD_APP === "browser") {
          Assert.greater(
            Glean.firstStartup.nimbusStoreInitTime.testGetValue(),
            0
          );
          Assert.greater(
            Glean.firstStartup.nimbusManagerInitTime.testGetValue(),
            0
          );
          Assert.greater(
            Glean.firstStartup.nimbusLoaderInitTime.testGetValue(),
            0
          );
          Assert.greater(Glean.firstStartup.nimbusInitTime.testGetValue(), 0);
        }

        if (AppConstants.MOZ_UPDATE_AGENT) {
          Assert.greater(Glean.firstStartup.deleteTasksTime.testGetValue(), 0);
        }
      } else {
        Assert.equal(FirstStartup.state, FirstStartup.UNSUPPORTED);
        Assert.ok(Glean.firstStartup.newProfile.testGetValue());
        Assert.equal(
          Glean.firstStartup.statusCode.testGetValue(),
          FirstStartup.UNSUPPORTED
        );
      }
    },
    () => FirstStartup.init(/* newProfile = */ true)
  );
});

add_task(async function test_timeout() {
  updateAppInfo();
  Services.prefs.setIntPref(PREF_TIMEOUT, 0);
  FirstStartup.resetForTesting();

  await GleanPings.firstStartup.testSubmission(
    () => {
      if (
        AppConstants.MOZ_BUILD_APP === "browser" ||
        AppConstants.MOZ_UPDATE_AGENT
      ) {
        Assert.equal(FirstStartup.state, FirstStartup.TIMED_OUT);
        Assert.greater(Glean.firstStartup.elapsed.testGetValue(), 0);
        Assert.ok(Glean.firstStartup.newProfile.testGetValue());

        if (AppConstants.MOZ_BUILD_APP === "browser") {
          Assert.greater(
            Glean.firstStartup.nimbusStoreInitTime.testGetValue(),
            0
          );
          Assert.greater(
            Glean.firstStartup.nimbusManagerInitTime.testGetValue(),
            0
          );
          Assert.greater(
            Glean.firstStartup.nimbusLoaderInitTime.testGetValue(),
            0
          );
          Assert.greater(Glean.firstStartup.nimbusInitTime.testGetValue(), 0);
        }

        if (AppConstants.MOZ_UPDATE_AGENT) {
          Assert.greater(Glean.firstStartup.deleteTasksTime.testGetValue(), 0);
        }
      } else {
        Assert.equal(FirstStartup.state, FirstStartup.UNSUPPORTED);
        Assert.equal(Glean.firstStartup.elapsed.testGetValue(), 0);
        Assert.ok(Glean.firstStartup.newProfile.testGetValue());
      }
    },
    () => FirstStartup.init(/* newProfile = */ true)
  );
});

add_task(async function test_existing_profile() {
  FirstStartup.resetForTesting();

  await GleanPings.firstStartup.testSubmission(
    () => {
      Assert.equal(FirstStartup.state, FirstStartup.NOT_STARTED);
      Assert.ok(!Glean.firstStartup.newProfile.testGetValue());
    },
    () => FirstStartup.init(/* newProfile = */ false)
  );
});
