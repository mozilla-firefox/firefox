/* Any copyright is dedicated to the Public Domain.
   http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

const SELECTOR_TOOLBAR_BUTTON = "#fire-button";
const SELECTOR_PANELVIEW = "panel #fire-button-panel";
const SELECTOR_PANEL_CONFIRM_BTN = "#fire-button-panel-confirm-button";
const SELECTOR_PANEL_CANCEL_BTN = "#fire-button-panel-cancel-button";
const SELECTOR_PANEL_CHOOSE_LINK = "#fire-button-panel-choose-link";

// Confirming actually closes every browser window, which would take the test
// harness with it, so record the calls instead of running them.
let gSanitizeCalls = [];
// Whether the stubbed sanitize behaves as if a page refused to close.
let gSanitizeAborted = false;
// Likewise, opening the real clear-data dialog would block the test on a modal.
let gShowUICalls = [];
// "accept" or "cancel".
let gShowUIResult = "accept";

// The toast auto-hides after ~3s, so polling for visibility races with the
// assertions. Record the calls instead; that it really renders is covered by
// the message id and verified by hand.
let gHintCalls = [];

add_setup(function () {
  Services.fog.testResetFOG();
  let originalSanitize = Sanitizer.sanitize;
  let originalShowUI = Sanitizer.showUI;
  let originalHintShow = ConfirmationHint.show;
  ConfirmationHint.show = (anchor, messageId, options) => {
    gHintCalls.push({ anchor, messageId, options });
    return originalHintShow.call(ConfirmationHint, anchor, messageId, options);
  };
  registerCleanupFunction(() => {
    ConfirmationHint.show = originalHintShow;
  });
  Sanitizer.sanitize = (itemsToClear, options) => {
    gSanitizeCalls.push({ itemsToClear, options });
    if (gSanitizeAborted) {
      options.progress.openWindows = "aborted";
      return Promise.reject(new Error("Sanitize could not close windows"));
    }
    return Promise.resolve({});
  };
  Sanitizer.showUI = async (parentWindow, mode, options) => {
    gShowUICalls.push({ parentWindow, mode, options });
    if (gShowUIResult == "cancel") {
      return "cancel";
    }
    // The real dialog clears via Sanitizer.sanitize, whose completion is
    // what the widget waits on before showing the toast.
    Services.obs.notifyObservers(null, "sanitizer-sanitization-complete");
    return "accept";
  };
  registerCleanupFunction(() => {
    Sanitizer.sanitize = originalSanitize;
    Sanitizer.showUI = originalShowUI;
  });
});

/**
 * Wait for the completion toast, then dismiss it so the next task starts from
 * a known state.
 */
async function awaitAndDismissToast() {
  await TestUtils.waitForCondition(
    () => gHintCalls.length,
    "Waiting for the completion toast."
  );
  Assert.equal(
    gHintCalls[0].messageId,
    "fire-button-panel-complete",
    "Completion toast shows the fire button's message."
  );
  Assert.equal(
    gHintCalls[0].anchor.id,
    "fire-button",
    "Toast is anchored to the fire button."
  );
  gHintCalls = [];

  let hint = document.getElementById("confirmation-hint");
  if (hint.state == "hiding") {
    // A toast asked for while the previous one is still hiding only opens
    // once that one has finished hiding.
    await BrowserTestUtils.waitForPopupEvent(hint, "hidden");
  }
  if (hint.state == "open" || hint.state == "showing") {
    let hidden = BrowserTestUtils.waitForEvent(hint, "popuphidden");
    hint.hidePopup();
    await hidden;
  }
}

/**
 * Wait for the flow_completed event, then check it and the activated event
 * that started the flow.
 *
 * @param {object} expected - Expected extras of flow_completed.
 * @param {string} expected.flow - "panel" or "direct".
 * @param {string} expected.outcome - "deleted", "cancelled", "dismissed" or
 *   "aborted".
 * @param {string} expected.surface - "panel" or "dialog".
 * @param {boolean} [expected.chooseClicked] - Whether "Choose…" was clicked.
 */
async function assertFlowTelemetry({
  flow,
  outcome,
  surface,
  chooseClicked = false,
}) {
  await TestUtils.waitForCondition(
    () => Glean.fireButton.flowCompleted.testGetValue()?.length,
    "Waiting for flow_completed."
  );
  let activated = Glean.fireButton.activated.testGetValue();
  Assert.equal(activated?.length, 1, "Recorded one activation.");
  Assert.deepEqual(activated[0].extra, { flow }, "Activation has the flow.");

  let completed = Glean.fireButton.flowCompleted.testGetValue();
  Assert.equal(completed.length, 1, "Recorded one flow completion.");
  Assert.deepEqual(
    completed[0].extra,
    { flow, outcome, surface },
    "Flow completion has the expected extras."
  );
  Assert.equal(
    Glean.fireButton.chooseClicked.testGetValue()?.length ?? 0,
    chooseClicked ? 1 : 0,
    "Recorded the Choose… click only if it happened."
  );
  Services.fog.testResetFOG();
}

/**
 * Open the fire button panel by clicking the toolbar button.
 *
 * @param {ChromeWindow} win - Chrome window the button lives in.
 * @returns {Promise<Element>} - The visible panelview.
 */
async function openPanel(win) {
  let shown = BrowserTestUtils.waitForEvent(win.document, "popupshown", true);
  win.document.querySelector(SELECTOR_TOOLBAR_BUTTON).click();
  await shown;
  let panelview = win.document.querySelector(SELECTOR_PANELVIEW);
  Assert.ok(
    panelview && BrowserTestUtils.isVisible(panelview),
    "Fire button panel is visible."
  );
  return panelview;
}

add_task(async function test_toolbarButtonVisibility() {
  let button = document.querySelector(SELECTOR_TOOLBAR_BUTTON);
  Assert.ok(button, "Fire button exists in a normal browsing window.");
  Assert.ok(
    BrowserTestUtils.isVisible(button),
    "Fire button is visible by default when the pref is enabled."
  );

  let privateWindow = await BrowserTestUtils.openNewBrowserWindow({
    private: true,
  });
  Assert.equal(
    privateWindow.document.querySelector(SELECTOR_TOOLBAR_BUTTON),
    null,
    "Fire button is not built in a private browsing window."
  );
  await BrowserTestUtils.closeWindow(privateWindow);
});

add_task(async function test_cancelDoesNotClear() {
  gSanitizeCalls = [];

  await openPanel(window);
  let hidden = BrowserTestUtils.waitForEvent(document, "popuphidden", true);
  document.querySelector(SELECTOR_PANEL_CANCEL_BTN).click();
  await hidden;

  Assert.equal(gSanitizeCalls.length, 0, "Cancelling does not clear anything.");
  await assertFlowTelemetry({
    flow: "panel",
    outcome: "cancelled",
    surface: "panel",
  });
});

add_task(async function test_dismissPanelRecordsDismissed() {
  gSanitizeCalls = [];

  await openPanel(window);
  let hidden = BrowserTestUtils.waitForEvent(document, "popuphidden", true);
  EventUtils.synthesizeKey("KEY_Escape");
  await hidden;

  Assert.equal(gSanitizeCalls.length, 0, "Dismissing does not clear anything.");
  await assertFlowTelemetry({
    flow: "panel",
    outcome: "dismissed",
    surface: "panel",
  });
});

add_task(async function test_confirmClearsWholeSession() {
  gSanitizeCalls = [];
  gHintCalls = [];

  await openPanel(window);
  let confirmButton = document.querySelector(SELECTOR_PANEL_CONFIRM_BTN);
  Assert.equal(
    confirmButton.type,
    "destructive",
    "The delete button uses the destructive button style."
  );
  confirmButton.click();
  await TestUtils.waitForCondition(
    () => gSanitizeCalls.length,
    "Waiting for the sanitize call."
  );

  Assert.equal(gSanitizeCalls.length, 1, "Sanitized exactly once.");
  let { itemsToClear, options } = gSanitizeCalls[0];

  for (let item of [
    "history",
    "cookies",
    "cache",
    "downloads",
    "formdata",
    "sessions",
    "offlineApps",
  ]) {
    Assert.ok(itemsToClear.includes(item), `Clears ${item}.`);
  }
  Assert.ok(
    itemsToClear.includes("openWindows"),
    "Closes all windows and opens a new one."
  );
  Assert.ok(
    options.ignoreTimespan,
    "Clears the whole history rather than a time span."
  );
  Assert.equal(
    options.privateStateForNewWindow,
    "non-private",
    "The replacement window is a normal window."
  );
  Assert.ok(
    options.keepPinnedTabs,
    "The replacement window keeps the pinned tabs."
  );

  // With the real Sanitizer this anchors in the replacement window; here the
  // stub leaves the current window in place, so it anchors in this one.
  await awaitAndDismissToast();
  await assertFlowTelemetry({
    flow: "panel",
    outcome: "deleted",
    surface: "panel",
  });
});

/**
 * @returns {MozTabbrowserTab|undefined} The Settings tab, if one is open.
 */
function findSettingsTab() {
  return gBrowser.tabs.find(tab =>
    tab.linkedBrowser.currentURI.spec.startsWith("about:preferences")
  );
}

/**
 * Run the panel's "Choose…" flow and wait until the dialog has been opened.
 *
 * @param {Function} taskFn - Run once the dialog is open.
 */
async function withChooseFlow(taskFn) {
  await openPanel(window);
  EventUtils.synthesizeMouseAtCenter(
    document.querySelector(SELECTOR_PANEL_CHOOSE_LINK),
    {}
  );

  await TestUtils.waitForCondition(
    () => gShowUICalls.length,
    "Waiting for the clear-data dialog to be opened."
  );
  // The anchor keeps its open attribute until the panel has finished hiding,
  // and while it is set a click on the button closes the panel instead of
  // activating the widget again.
  await TestUtils.waitForCondition(
    () => !document.querySelector(SELECTOR_TOOLBAR_BUTTON).hasAttribute("open"),
    "Waiting for the panel anchor to reset."
  );

  await taskFn();
}

add_task(async function test_chooseOpensClearDataDialog() {
  gSanitizeCalls = [];
  gShowUICalls = [];
  gHintCalls = [];
  gShowUIResult = "accept";

  await withChooseFlow(async () => {
    Assert.ok(
      !findSettingsTab(),
      "The dialog opens in place, without a Settings detour."
    );

    Assert.equal(gShowUICalls.length, 1, "Opened the dialog exactly once.");
    Assert.equal(
      gShowUICalls[0].mode,
      "clearSiteData",
      "Opens the 'Delete browsing data' dialog."
    );
    let { triggeringEvent, ...options } = gShowUICalls[0].options;
    Assert.deepEqual(
      options,
      { entryPoint: "fire_button" },
      "The dialog keeps the windows open, whatever it deletes."
    );
    Assert.equal(
      triggeringEvent?.target.id,
      "fire-button-panel-choose-link",
      "Passes the event of the Choose… link, for the dialog's focus ring."
    );
    Assert.equal(
      gShowUICalls[0].parentWindow,
      window,
      "Dialog is parented to the window the panel was used in."
    );
    Assert.equal(
      gSanitizeCalls.length,
      0,
      "The widget does not clear anything itself; the dialog does."
    );
    await awaitAndDismissToast();
  });
  await assertFlowTelemetry({
    flow: "panel",
    outcome: "deleted",
    surface: "dialog",
    chooseClicked: true,
  });
});

add_task(async function test_directFlowSkipsPanel() {
  await SpecialPowers.pushPrefEnv({
    set: [["browser.fireButton.flow", "direct"]],
  });
  gSanitizeCalls = [];
  gShowUICalls = [];
  gHintCalls = [];
  gShowUIResult = "accept";

  Assert.equal(
    NimbusFeatures.fireButton.getVariable("flow"),
    "direct",
    "The flow variable reflects the pref before activating the button."
  );

  document.querySelector(SELECTOR_TOOLBAR_BUTTON).click();
  // Wait for either outcome, so a wrongly-shown panel fails with a useful
  // assertion rather than a bare timeout.
  await TestUtils.waitForCondition(
    () => gShowUICalls.length || document.querySelector(SELECTOR_PANELVIEW),
    "Waiting for the button to act."
  );

  let panelview = document.querySelector(SELECTOR_PANELVIEW);
  Assert.ok(
    !panelview || !BrowserTestUtils.isVisible(panelview),
    "Confirmation panel is skipped entirely."
  );
  Assert.equal(
    gShowUICalls[0].mode,
    "clearSiteData",
    "Goes straight to the clear-data dialog."
  );
  let { triggeringEvent, ...options } = gShowUICalls[0].options;
  Assert.deepEqual(
    options,
    { entryPoint: "fire_button" },
    "The dialog keeps the windows open, whatever it deletes."
  );
  Assert.equal(
    triggeringEvent?.target,
    document.querySelector(SELECTOR_TOOLBAR_BUTTON),
    "Passes the event that activated the button, for the dialog's focus ring."
  );
  Assert.equal(
    gSanitizeCalls.length,
    0,
    "Nothing is cleared without the dialog's own confirmation."
  );
  Assert.ok(!findSettingsTab(), "The dialog opens in place here too.");

  await awaitAndDismissToast();
  await assertFlowTelemetry({
    flow: "direct",
    outcome: "deleted",
    surface: "dialog",
  });
  await SpecialPowers.popPrefEnv();
});

add_task(async function test_nimbusEnrollmentSelectsFlow() {
  const { NimbusTestUtils } = ChromeUtils.importESModule(
    "resource://testing-common/NimbusTestUtils.sys.mjs"
  );

  Assert.equal(
    NimbusFeatures.fireButton.getVariable("flow"),
    "panel",
    "Falls back to the pref value when not enrolled."
  );

  let doCleanup = await NimbusTestUtils.enrollWithFeatureConfig({
    featureId: "fireButton",
    value: { flow: "direct" },
  });
  Assert.equal(
    NimbusFeatures.fireButton.getVariable("flow"),
    "direct",
    "An enrollment selects the flow without touching the pref."
  );
  Assert.equal(
    Services.prefs.getStringPref("browser.fireButton.flow"),
    "panel",
    "A fallbackPref variable is not written by the enrollment."
  );

  await doCleanup();
  Assert.equal(
    NimbusFeatures.fireButton.getVariable("flow"),
    "panel",
    "Unenrolling restores the pref-backed value."
  );
});

add_task(async function test_chooseCancelledShowsNoToast() {
  gShowUICalls = [];
  gHintCalls = [];
  gShowUIResult = "cancel";

  await withChooseFlow(async () => {
    // withChooseFlow only waits for the dialog to have been requested, so let
    // the widget run its continuation past that point before asserting on what
    // it did not do. Asserting on the recorded call rather than on visibility
    // catches a toast that was asked for but has not rendered yet.
    await TestUtils.waitForTick();
    Assert.equal(
      gHintCalls.length,
      0,
      "Backing out of the dialog shows no completion toast."
    );
  });
  await assertFlowTelemetry({
    flow: "panel",
    outcome: "cancelled",
    surface: "dialog",
    chooseClicked: true,
  });
});

/**
 * Staying on a page that asks before leaving stops the clear before anything
 * is deleted, which is reported apart from backing out of the panel.
 */
add_task(async function test_confirmAbortedRecordsAborted() {
  gSanitizeCalls = [];
  gHintCalls = [];
  gSanitizeAborted = true;

  await openPanel(window);
  document.querySelector(SELECTOR_PANEL_CONFIRM_BTN).click();
  await assertFlowTelemetry({
    flow: "panel",
    outcome: "aborted",
    surface: "panel",
  });
  Assert.equal(gSanitizeCalls.length, 1, "Tried to clear once.");
  Assert.equal(gHintCalls.length, 0, "Nothing was deleted, so no toast.");
  gSanitizeAborted = false;
});

/**
 * Exposure is recorded at the first startup after enrollment, in every
 * branch, and not in the session that enrolled.
 */
add_task(async function test_exposureAtStartupAfterEnrollment() {
  const { NimbusTestUtils } = ChromeUtils.importESModule(
    "resource://testing-common/NimbusTestUtils.sys.mjs"
  );
  const { FireButtonExposure } = ChromeUtils.importESModule(
    "moz-src:///browser/components/customizableui/CustomizableWidgets.sys.mjs"
  );
  const kPref = FireButtonExposure.kEnrollmentPref;

  Services.fog.testResetFOG();
  FireButtonExposure.recordExposure("");
  Assert.equal(
    Glean.nimbusEvents.exposure.testGetValue("events"),
    undefined,
    "Nothing is recorded without an enrollment."
  );
  Assert.ok(!Services.prefs.prefHasUserValue(kPref), "No enrollment is kept.");

  for (let [branchSlug, value] of [
    ["control", { enabled: false }],
    ["treatment-b", { enabled: true, flow: "direct" }],
  ]) {
    let slug = `fire-button-exposure-${branchSlug}`;
    let doCleanup = await NimbusTestUtils.enrollWithFeatureConfig(
      { featureId: "fireButton", value },
      { slug, branchSlug }
    );
    Assert.equal(
      Services.prefs.getStringPref(kPref, ""),
      slug,
      `Enrolling in ${branchSlug} is remembered for the next startup.`
    );

    Services.fog.testResetFOG();
    FireButtonExposure.recordExposure("");
    Assert.equal(
      Glean.nimbusEvents.exposure.testGetValue("events"),
      undefined,
      `No exposure in the session that enrolled in ${branchSlug}.`
    );

    FireButtonExposure.recordExposure(slug);
    let events = Glean.nimbusEvents.exposure.testGetValue("events");
    Assert.equal(events?.length, 1, `Exposure recorded for ${branchSlug}.`);
    Assert.deepEqual(
      events[0].extra,
      { experiment: slug, branch: branchSlug, feature_id: "fireButton" },
      `Exposure names the ${branchSlug} branch.`
    );

    await doCleanup();
    Assert.ok(
      !Services.prefs.prefHasUserValue(kPref),
      `Unenrolling from ${branchSlug} forgets the enrollment.`
    );
  }
  Services.fog.testResetFOG();
});

add_task(async function test_activationRecordsNoExposure() {
  const { NimbusTestUtils } = ChromeUtils.importESModule(
    "resource://testing-common/NimbusTestUtils.sys.mjs"
  );
  let doCleanup = await NimbusTestUtils.enrollWithFeatureConfig({
    featureId: "fireButton",
    value: { flow: "panel" },
  });
  Services.fog.testResetFOG();

  let panelview = await openPanel(window);
  Assert.equal(
    Glean.nimbusEvents.exposure.testGetValue("events"),
    undefined,
    "Pressing the button does not record exposure."
  );
  panelview.querySelector(SELECTOR_PANEL_CANCEL_BTN).click();
  await assertFlowTelemetry({
    flow: "panel",
    outcome: "cancelled",
    surface: "panel",
  });
  await doCleanup();
});
