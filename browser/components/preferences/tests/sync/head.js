/* Any copyright is dedicated to the Public Domain.
   http://creativecommons.org/publicdomain/zero/1.0/ */

Services.scriptloader.loadSubScript(
  "chrome://mochitests/content/browser/browser/components/preferences/tests/head.js",
  this
);

// EnterprisePolicyTesting and PoliciesPrefTracker are already imported in parent head.js
async function setupPolicyEngineWithJson(json, customSchema) {
  PoliciesPrefTracker.restoreDefaultValues();
  return EnterprisePolicyTesting.setupPolicyEngineWithJson(json, customSchema);
}

/**
 * Helper function to run a test with a mocked UIState in a sync pane.
 *
 * @param {object} uiStateData - The UIState data to mock
 * @param {Function} testCallback - Async function that receives the document
 * @returns {Promise<void>}
 */
async function runSyncPaneTest(uiStateData, testCallback) {
  let { UIState } = ChromeUtils.importESModule(
    "resource://services-sync/UIState.sys.mjs"
  );

  const oldUIState = UIState.get;
  UIState.get = () => uiStateData;

  await openPreferencesViaOpenPreferencesAPI("paneSync", {
    leaveOpen: true,
  });
  let doc = gBrowser.contentDocument;

  try {
    await testCallback(doc);
  } finally {
    UIState.get = oldUIState;
    BrowserTestUtils.removeTab(gBrowser.selectedTab);
  }
}
