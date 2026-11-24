/* Any copyright is dedicated to the Public Domain.
   https://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

const { TelemetryTestUtils } = ChromeUtils.importESModule(
  "resource://testing-common/TelemetryTestUtils.sys.mjs"
);

const telemetryKey = `browser.ui.interaction.preferences_${DEFAULT_PANE}`;
const defaultPaneMetric = Glean.browserUiInteraction.preferencesPaneSync;

let resetTelemetry = async () => {
  await Services.fog.testFlushAllChildren();
  Services.fog.testResetFOG();
};

registerCleanupFunction(async () => {
  await resetTelemetry();
});

async function assertSupportLinkInteraction(linkId, expectedCount) {
  await TestUtils.waitForCondition(
    () => defaultPaneMetric?.[linkId]?.testGetValue() == expectedCount,
    "wait for metric to be recorded"
  );
  Assert.equal(
    defaultPaneMetric[linkId].testGetValue(),
    expectedCount,
    `support link click should have been counted ${expectedCount} time(s)`
  );
}

async function createSettingWithSupportLink(doc, win, settingId, config) {
  win.Preferences.addSetting({
    id: settingId,
    get: () => true,
    set: () => {},
  });

  let testGroup = doc.createElement("setting-group");
  testGroup.setAttribute("data-category", DEFAULT_PANE);
  testGroup.config = { items: [config] };
  testGroup.getSetting = win.Preferences.getSetting.bind(win.Preferences);
  doc.getElementById("mainPrefPane").append(testGroup);
  testGroup.scrollIntoView();

  await testGroup.updateComplete;
  await new Promise(r => win.requestAnimationFrame(r));

  return doc.getElementById(settingId);
}

async function activateSupportLinkAndVerifyTelemetry(
  supportLink,
  linkId,
  win,
  useKeyboard = false
) {
  // Fluent fills in the link's text asynchronously, which on slow machines can
  // land after the frame createSettingWithSupportLink waits for, leaving an
  // empty link that the click can miss.
  await BrowserTestUtils.waitForMutationCondition(
    supportLink,
    { childList: true },
    () => supportLink.textContent
  );

  let linkClickPromise = BrowserTestUtils.waitForNewTab(gBrowser, null, true);

  if (useKeyboard) {
    supportLink.focus();
    EventUtils.synthesizeKey("KEY_Enter", {}, win);
  } else {
    EventUtils.synthesizeMouseAtCenter(supportLink, {}, win);
  }

  let tab = await linkClickPromise;
  Assert.ok(tab, "support link should open a new tab");
  BrowserTestUtils.removeTab(tab);

  let snapshot = TelemetryTestUtils.getProcessScalars("parent", true, true);
  TelemetryTestUtils.assertKeyedScalar(snapshot, telemetryKey, linkId, 1);

  await assertSupportLinkInteraction(linkId, 1);
}

add_task(async function testSupportLinkTelemetry() {
  await openPreferencesViaOpenPreferencesAPI(DEFAULT_PANE, { leaveOpen: true });
  await resetTelemetry();

  const doc = gBrowser.contentDocument;
  const win = doc.documentGlobal;
  const SETTING_ID = "testSupportLinkSetting";
  const LINK_ID = `${SETTING_ID}Link`;

  let settingControl = await createSettingWithSupportLink(
    doc,
    win,
    SETTING_ID,
    {
      id: SETTING_ID,
      l10nId: "forms-suggest-passwords",
      supportPage: "how-generate-secure-password-firefox",
    }
  );
  Assert.ok(settingControl, "setting control should exist");

  let supportLink = settingControl.shadowRoot.querySelector(
    "a[is='moz-support-link']"
  );
  Assert.ok(supportLink, "support link should exist in shadow DOM");

  info("clicking support link");
  await activateSupportLinkAndVerifyTelemetry(supportLink, LINK_ID, win);

  await resetTelemetry();

  info("activating support link with keyboard");
  await activateSupportLinkAndVerifyTelemetry(supportLink, LINK_ID, win, true);

  gBrowser.removeCurrentTab();
});

add_task(async function testSupportLinkWithIdOverride() {
  await openPreferencesViaOpenPreferencesAPI(DEFAULT_PANE, { leaveOpen: true });
  await resetTelemetry();

  const doc = gBrowser.contentDocument;
  const win = doc.documentGlobal;
  const SETTING_ID = "testSupportLinkWithIdSetting";
  const LINK_ID = "customSupportLinkId";

  let settingControl = await createSettingWithSupportLink(
    doc,
    win,
    SETTING_ID,
    {
      id: SETTING_ID,
      l10nId: "forms-suggest-passwords",
      options: [
        {
          id: LINK_ID,
          supportPage: "how-generate-secure-password-firefox",
          control: "a",
          slot: "support-link",
          controlAttrs: {
            is: "moz-support-link",
          },
        },
      ],
    }
  );
  Assert.ok(settingControl, "setting control should exist");

  let supportLink = settingControl.querySelector("a[is='moz-support-link']");
  Assert.ok(supportLink, "support link should exist");

  info("clicking support link with custom id");
  await activateSupportLinkAndVerifyTelemetry(supportLink, LINK_ID, win);

  await resetTelemetry();

  info("activating support link with custom id using keyboard");
  await activateSupportLinkAndVerifyTelemetry(supportLink, LINK_ID, win, true);

  gBrowser.removeCurrentTab();
});
