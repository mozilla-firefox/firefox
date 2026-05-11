/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */

/**
 * This tests that MAR update network requests include extras --
 * headers and query parameters -- identifying whether the request
 * comes from a browsing Firefox or from a background task (the
 * background update task).
 *
 * This test does some unusual things, compared to the other files in this
 * directory. We want to start the updates with aus.checkForBackgroundUpdates()
 * to ensure that we witness update XML requests (which should not have the
 * additional headers). Other tests start update with things like
 * aus.downloadUpdate().
 *
 * Just like `multiUpdate.js`, in order to accomplish all this, we will be using
 * app_update.sjs to serve updates XMLs and MARs. Outside of this test, this is
 * really only done by browser-chrome mochitests (in ../browser). So we have to
 * do some weird things to make it work properly in an xpcshell test. Things
 * like defining URL_HTTP_UPDATE_SJS in testConstants.js so that it can be read
 * by app_update.sjs in order to provide the correct download URL for MARs, but
 * not reading that file here, because URL_HTTP_UPDATE_SJS is already defined
 * (as something else) in xpcshellUtilsAUS.js.
 */

const { EnterprisePolicyTesting } = ChromeUtils.importESModule(
  "resource://testing-common/EnterprisePolicyTesting.sys.mjs"
);
const { setupPolicyEngineWithJson } = EnterprisePolicyTesting;
EnterprisePolicyTesting.pathResolver = path => do_get_file(path).path;

const downloadHeaders = new DownloadHeadersTest();

function setup_enterprise_policy_testing() {
  // This initializes the policy engine for xpcshell tests
  let policies = Cc["@mozilla.org/enterprisepolicies;1"].getService(
    Ci.nsIObserver
  );
  policies.observe(null, "policies-startup", null);
}

add_setup(async () => {
  setupTestCommon(true, /* aAllowBits */ false);
  setup_enterprise_policy_testing();
  downloadHeaders.startUpdateServer();
});

add_task(async function test_noTask_noBITS() {
  await downloadHeaders.test({
    useBits: false,
    backgroundTaskName: null,
    userAgentPattern: /\sGecko\//,
    expectedExtras: [
      { mode: null, name: null },
      { mode: "0", name: null },
    ],
  });
});

const canTask = {
  skip_if: () => !AppConstants.MOZ_BACKGROUNDTASKS,
};

add_task(canTask, async function test_task_noBITS() {
  await downloadHeaders.test({
    useBits: false,
    backgroundTaskName: "task_downloadHeaders",
    userAgentPattern: /\sGecko\//,
    expectedExtras: [
      { mode: null, name: null },
      { mode: "1", name: "task_downloadHeaders" },
    ],
  });
});

const canBits = {
  skip_if: () => {
    // Need to enable the pref to accurately test for BITS.
    Services.prefs.setBoolPref(PREF_APP_UPDATE_BITS_ENABLED, true);
    return !gAUS.canUsuallyUseBits;
  },
};

add_task(canBits, async function test_noTask_BITS() {
  await downloadHeaders.test({
    useBits: true,
    backgroundTaskName: null,
    userAgentPattern: /^Microsoft BITS\//,
    expectedExtras: [
      { mode: null, name: null },
      { mode: "0", name: null },
    ],
  });
});

const canTaskBits = {
  skip_if: () => {
    // Need to enable the pref to accurately test for BITS.
    Services.prefs.setBoolPref(PREF_APP_UPDATE_BITS_ENABLED, true);
    return !AppConstants.MOZ_BACKGROUNDTASKS || !gAUS.canUsuallyUseBits;
  },
};

add_task(canTaskBits, async function test_task_BITS() {
  await downloadHeaders.test({
    useBits: true,
    backgroundTaskName: "task_downloadHeaders",
    userAgentPattern: /^Microsoft BITS\//,
    expectedExtras: [
      { mode: null, name: null },
      { mode: "1", name: "task_downloadHeaders" },
    ],
  });
});

add_task(async function test_task_policies() {
  await setupPolicyEngineWithJson({
    policies: {
      // Same URL, different method of specifying it.
      AppUpdateURL: downloadHeaders.updateUrl,
    },
  });
  Assert.equal(
    Services.policies.status,
    Ci.nsIEnterprisePolicies.ACTIVE,
    "Engine is active"
  );

  // No extras when the update URL is set by policy.
  await downloadHeaders.test({
    useBits: false,
    backgroundTaskName: "task_downloadHeaders",
    userAgentPattern: /\sGecko\//,
    expectedExtras: [
      { mode: null, name: null },
      { mode: null, name: null },
    ],
  });
});

add_task(async () => {
  doTestFinish();
});
