/* Any copyright is dedicated to the Public Domain.
   https://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

/* global
  ExperimentAPI,
  NimbusTestUtils,
  openPreferencesViaOpenPreferencesAPI,
  waitForSettingChange,
*/

/* exported
  GeneratedWidgetsFeature,
  CONTROL_PREF,
  DEFAULT_PREF,
  LABS_PREF,
  PROVIDER_PREF,
  cleanupEnrollment,
  prefEnvCount,
  paneDoc,
  useSharedAiPane,
  setUpGeneratedWidgetsTest,
  tearDownGeneratedWidgetsTest,
  enrolInLabs,
  enrolInLabsAndWait,
  isEnrolled,
  enableInstalledWidgets,
  blockAllAi,
  unblockAllAi,
  assertVisibility,
*/

const { GeneratedWidgetsFeature } = ChromeUtils.importESModule(
  "moz-src:///browser/components/widgets/GeneratedWidgetsFeature.sys.mjs"
);
const { _ExperimentFeature: ExperimentFeature } = ChromeUtils.importESModule(
  "resource://nimbus/ExperimentAPI.sys.mjs"
);

const CONTROL_PREF = "browser.ai.control.generatedWidgets";
const DEFAULT_PREF = "browser.ai.control.default";
const LABS_PREF = "browser.widgets.generated.enabled";
const PROVIDER_PREF = "extensions.widgets.generated.enabled";

const AI_CONTROL_PREFS = [
  ["browser.preferences.aiControls", true],
  ["extensions.ml.enabled", true],
  [DEFAULT_PREF, "available"],
  ["browser.ai.control.translations", "default"],
  ["browser.ai.control.pdfjsAltText", "default"],
  ["browser.ai.control.smartTabGroups", "default"],
  ["browser.ai.control.linkPreviewKeyPoints", "default"],
  ["browser.ai.control.sidebarChatbot", "default"],
  ["browser.ai.control.smartWindow", "default"],
  ["browser.ai.control.speechRecognition", "default"],
  [CONTROL_PREF, "default"],
];

let cleanupEnrollment, removeTestFeature, prefEnvCount, paneDoc;

/**
 * Opens the AI pane once for the whole file, so the tests share it instead of
 * loading a fresh pane each. Call it inside the outermost describe, before
 * registering `tearDownGeneratedWidgetsTest`: the afterEach hooks run in
 * reverse order, so the pane is checked after the teardown has reset it.
 */
function useSharedAiPane() {
  add_setup(async () => {
    await SpecialPowers.pushPrefEnv({ set: AI_CONTROL_PREFS });
    await openPreferencesViaOpenPreferencesAPI("ai", { leaveOpen: true });
    const tab = gBrowser.selectedTab;
    paneDoc = gBrowser.selectedBrowser.contentDocument;
    registerCleanupFunction(async () => {
      BrowserTestUtils.removeTab(tab);
      paneDoc = null;
      await SpecialPowers.popPrefEnv();
    });
  });

  afterEach(() =>
    TestUtils.waitForCondition(
      () =>
        !BrowserTestUtils.isVisible(
          paneDoc.getElementById("customWidgetsFieldset")
        ) &&
        !paneDoc.getElementById("aiControlGeneratedWidgetsSelect").disabled &&
        !paneDoc.querySelector("block-ai-confirmation-dialog")?.dialog?.open,
      "The shared pane is back at its baseline",
      100,
      100
    )
  );
}

async function setUpGeneratedWidgetsTest() {
  cleanupEnrollment = null;
  removeTestFeature = null;
  prefEnvCount = 0;
  Services.prefs.clearUserPref(LABS_PREF);
  Services.prefs.clearUserPref(PROVIDER_PREF);
  await SpecialPowers.pushPrefEnv({ set: AI_CONTROL_PREFS });
  prefEnvCount++;
  removeTestFeature = NimbusTestUtils.addTestFeatures(
    new ExperimentFeature("test-generated-widgets", {
      variables: {
        enabled: {
          type: "boolean",
          setPref: { branch: "user", pref: LABS_PREF },
        },
      },
    })
  );
}

async function tearDownGeneratedWidgetsTest() {
  try {
    if (isEnrolled()) {
      await cleanupEnrollment?.();
    }
    await NimbusTestUtils.assert.storeIsEmpty();
  } finally {
    removeTestFeature?.();
    while (prefEnvCount > 0) {
      await SpecialPowers.popPrefEnv();
      prefEnvCount--;
    }
    Services.prefs.clearUserPref(LABS_PREF);
    Services.prefs.clearUserPref(PROVIDER_PREF);
  }
}

async function enrolInLabs() {
  cleanupEnrollment = await NimbusTestUtils.enrollWithFeatureConfig(
    { featureId: "test-generated-widgets", value: { enabled: true } },
    { slug: "custom-widgets-labs" }
  );
}

async function enrolInLabsAndWait(doc) {
  await enrolInLabs();
  await TestUtils.waitForCondition(
    () =>
      BrowserTestUtils.isVisible(doc.getElementById("customWidgetsFieldset")),
    "The custom widgets row is shown"
  );
  await new Promise(resolve =>
    doc.documentGlobal.requestAnimationFrame(resolve)
  );
}

function isEnrolled() {
  return !!ExperimentAPI.manager.store.get("custom-widgets-labs")?.active;
}

async function enableInstalledWidgets() {
  await SpecialPowers.pushPrefEnv({ set: [[PROVIDER_PREF, true]] });
  prefEnvCount++;
}

async function blockAllAi(doc) {
  const win = doc.documentGlobal;
  const toggle = doc.getElementById("aiControlDefaultToggle");
  const dialogEl = doc.querySelector("block-ai-confirmation-dialog");
  await dialogEl.updateComplete;
  toggle.buttonEl.scrollIntoView();
  await new Promise(resolve => win.requestAnimationFrame(resolve));
  const dialogShown = BrowserTestUtils.waitForEvent(dialogEl.dialog, "toggle");
  EventUtils.synthesizeMouseAtCenter(toggle.buttonEl, {}, win);
  await dialogShown;
  Assert.ok(dialogEl.dialog.open, "Block confirmation is open");
  await waitForSettingChange(
    win.Preferences.getSetting("aiControlDefaultToggle"),
    () => EventUtils.synthesizeMouseAtCenter(dialogEl.confirmButton, {}, win)
  );
  Assert.equal(
    Services.prefs.getStringPref(DEFAULT_PREF),
    "blocked",
    "The global switch blocks AI"
  );
}

async function unblockAllAi(doc) {
  const win = doc.documentGlobal;
  const toggle = doc.getElementById("aiControlDefaultToggle");
  toggle.buttonEl.scrollIntoView();
  await new Promise(resolve => win.requestAnimationFrame(resolve));
  await waitForSettingChange(
    win.Preferences.getSetting("aiControlDefaultToggle"),
    () => EventUtils.synthesizeMouseAtCenter(toggle.buttonEl, {}, win)
  );
  Assert.equal(
    Services.prefs.getStringPref(DEFAULT_PREF),
    "available",
    "The global switch makes AI available"
  );
}

function assertVisibility(doc, visible) {
  for (const id of [
    "customWidgetsFieldset",
    "aiControlGeneratedWidgetsSelect",
  ]) {
    const element = doc.getElementById(id);
    Assert.ok(element, `${id} exists`);
    Assert.equal(
      BrowserTestUtils.isVisible(element),
      visible,
      `${id} is ${visible ? "visible" : "hidden"}`
    );
  }
}
