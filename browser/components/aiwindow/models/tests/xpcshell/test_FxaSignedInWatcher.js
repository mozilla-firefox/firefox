/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

const { FxaSignedInWatcher } = ChromeUtils.importESModule(
  "moz-src:///browser/components/aiwindow/models/auth/FxaSignedInWatcher.sys.mjs"
);
const { getFxAccountsSingleton } = ChromeUtils.importESModule(
  "resource://gre/modules/FxAccounts.sys.mjs"
);
const { UIState } = ChromeUtils.importESModule(
  "resource://services-sync/UIState.sys.mjs"
);
const { sinon } = ChromeUtils.importESModule(
  "resource://testing-common/Sinon.sys.mjs"
);

/**
 * Simulates a UIState update with the given status.
 *
 * @param {object} uiState The stubbed UIState.get.
 * @param {string} status One of the UIState.STATUS_* constants.
 */
function updateUIState(uiState, status) {
  uiState.returns({ status });
  Services.obs.notifyObservers(null, UIState.ON_UPDATE);
}

add_task(async function test_watcher() {
  const sb = sinon.createSandbox();
  sb.stub(getFxAccountsSingleton(), "getSignedInUser").resolves({
    uid: "uid",
    verified: false,
  });
  const uiState = sb.stub(UIState, "get");
  let changes = 0;
  FxaSignedInWatcher.addEventListener(
    "FxaSignedInWatcher:StateChanged",
    () => changes++
  );

  try {
    Assert.ok(
      !(await FxaSignedInWatcher.isFxaSignedIn()),
      "isFxaSignedIn waits for init and reports an unverified account"
    );
    Assert.ok(
      !FxaSignedInWatcher.signedIn,
      "An unverified account is not signed in"
    );
    Assert.equal(changes, 0, "No change from the initial state");

    updateUIState(uiState, UIState.STATUS_SIGNED_IN);
    Assert.ok(FxaSignedInWatcher.signedIn, "Signed in after verification");
    Assert.ok(
      await FxaSignedInWatcher.isFxaSignedIn(),
      "isFxaSignedIn reports the verified account"
    );
    Assert.equal(changes, 1, "The change is notified");

    updateUIState(uiState, UIState.STATUS_SIGNED_IN);
    Assert.equal(changes, 1, "An update without a change is not notified");

    updateUIState(uiState, UIState.STATUS_LOGIN_FAILED);
    Assert.ok(!FxaSignedInWatcher.signedIn, "Not signed in after a failure");
    Assert.equal(changes, 2, "The change is notified");

    updateUIState(uiState, UIState.STATUS_NOT_CONFIGURED);
    Assert.equal(changes, 2, "An update without a change is not notified");
  } finally {
    sb.restore();
  }
});
