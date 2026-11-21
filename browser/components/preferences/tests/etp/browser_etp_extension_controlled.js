/* Any copyright is dedicated to the Public Domain.
 * https://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

const { AddonTestUtils } = ChromeUtils.importESModule(
  "resource://testing-common/AddonTestUtils.sys.mjs"
);
AddonTestUtils.initMochitest(this);

const CAT_PREF = "browser.contentblocking.category";
const TP_PREF = "privacy.trackingprotection.enabled";
const TP_PBM_PREF = "privacy.trackingprotection.pbmode.enabled";
const BASELINE_PREF = "privacy.trackingprotection.allow_list.baseline.enabled";
const CONVENIENCE_PREF =
  "privacy.trackingprotection.allow_list.convenience.enabled";
const EXTENSION_ID = "@set_tp";
const EXTENSION_NAME = "set_tp";
const CONTROLLED_L10N_ID =
  "extension-controlling-websites-content-blocking-all-trackers";

function getMessageBar(control) {
  return control.querySelector(".extension-controlled-message-bar");
}

async function waitForMessageBar(control, shown) {
  await BrowserTestUtils.waitForMutationCondition(
    control,
    { childList: true, subtree: true },
    () => !!getMessageBar(control) == shown
  );
  await control.updateComplete;
}

function assertMessageBar(control) {
  let messageBar = getMessageBar(control);
  ok(messageBar, `${control.id} shows the extension controlled message bar`);
  is(
    messageBar.messageL10nId,
    CONTROLLED_L10N_ID,
    "Message bar uses the tracking protection l10n id"
  );
  is(
    messageBar.messageL10nArgs.name,
    EXTENSION_NAME,
    "Message bar names the controlling extension"
  );
  return messageBar;
}

function loadTPExtension() {
  return ExtensionTestUtils.loadExtension({
    useAddonManager: "permanent",
    manifest: {
      name: EXTENSION_NAME,
      browser_specific_settings: { gecko: { id: EXTENSION_ID } },
      permissions: ["privacy"],
    },
    async background() {
      await browser.privacy.websites.trackingProtectionMode.set({
        value: "always",
      });
      browser.test.sendMessage("tp-set");
    },
  });
}

let extension;
let tab;

afterEach(async () => {
  if (extension) {
    // Re-enable so the uninstall cleans up the ExtensionPreferencesManager state.
    let addon = await AddonManager.getAddonByID(EXTENSION_ID);
    await addon?.enable();
    await extension.unload();
    extension = null;
  }
  if (tab) {
    BrowserTestUtils.removeTab(tab);
    tab = null;
  }
  for (let pref of [TP_PREF, TP_PBM_PREF]) {
    Services.prefs.unlockPref(pref);
  }
  await SpecialPowers.popPrefEnv();
});

it("shows the extension controlled notice", async () => {
  await SpecialPowers.pushPrefEnv({
    set: [
      [CAT_PREF, "standard"],
      [TP_PREF, false],
    ],
  });

  let doc;
  ({ doc, tab } = await openEtpPage());
  let radioGroupControl = doc.getElementById(
    "setting-control-contentBlockingCategoryRadioGroup"
  );
  let standardRadio = getControl(doc, "etpLevelStandard");
  let strictRadio = getControl(doc, "etpLevelStrict");
  let customRadio = getControl(doc, "etpLevelCustom");
  let customizeButton = getControl(doc, "etpCustomizeButton");

  ok(!getMessageBar(radioGroupControl), "No message bar before install");
  ok(!standardRadio.disabled, "Standard radio enabled before install");
  ok(!strictRadio.disabled, "Strict radio enabled before install");

  extension = loadTPExtension();
  let messageBarShown = waitForMessageBar(radioGroupControl, true);
  await extension.startup();
  await extension.awaitMessage("tp-set");
  await messageBarShown;

  ok(Services.prefs.getBoolPref(TP_PREF), "Extension enabled tracking prot.");
  let messageBar = assertMessageBar(radioGroupControl);
  ok(standardRadio.disabled, "Standard radio disabled while controlled");
  ok(strictRadio.disabled, "Strict radio disabled while controlled");
  ok(!customRadio.disabled, "Custom radio stays enabled while controlled");

  await BrowserTestUtils.waitForMutationCondition(
    customizeButton.shadowRoot,
    { attributes: true, subtree: true, attributeFilter: ["disabled"] },
    () => customRadio.checked && !customizeButton.parentDisabled
  );

  info("Check the tracking protection toggle in the customize pane");
  let paneShown = waitForPaneChange("etpCustomize");
  synthesizeClick(customizeButton);
  await paneShown;
  let toggleControl = doc.getElementById(
    "setting-control-etpCustomTrackingProtectionEnabled"
  );
  await waitForMessageBar(toggleControl, true);
  assertMessageBar(toggleControl);
  ok(
    getControl(doc, "etpCustomTrackingProtectionEnabled").disabled,
    "Tracking protection toggle disabled while controlled"
  );
  ok(
    getControl(doc, "etpCustomTrackingProtectionEnabledContext").parentDisabled,
    "Tracking protection context select disabled while controlled"
  );

  await maybeNavigateToPane("etp");

  info("Disable the extension from the message bar");
  let disableButton = messageBar.querySelector(
    "moz-button[data-l10n-id='disable-extension']"
  );
  ok(disableButton, "Message bar has a disable extension button");
  let messageBarHidden = waitForMessageBar(radioGroupControl, false);
  synthesizeClick(disableButton);
  await messageBarHidden;

  ok(!Services.prefs.getBoolPref(TP_PREF), "Tracking protection pref reset");
  ok(
    radioGroupControl.querySelector(".reenable-extensions-message-bar"),
    "Re-enable extension message bar shown"
  );
  ok(!standardRadio.disabled, "Standard radio enabled after disabling");
  ok(!strictRadio.disabled, "Strict radio enabled after disabling");
  ok(!customRadio.disabled, "Custom radio enabled after disabling");
});

it("ignores the extension while tracking protection prefs are locked", async () => {
  await SpecialPowers.pushPrefEnv({
    set: [
      [CAT_PREF, "standard"],
      [TP_PREF, false],
    ],
  });
  for (let pref of [TP_PREF, TP_PBM_PREF]) {
    Services.prefs.lockPref(pref);
  }

  let doc;
  ({ doc, tab } = await openEtpPage());
  let radioGroupControl = doc.getElementById(
    "setting-control-contentBlockingCategoryRadioGroup"
  );
  let setting = doc.defaultView.Preferences.getSetting(
    "contentBlockingCategoryRadioGroup"
  );

  extension = loadTPExtension();
  await extension.startup();
  await extension.awaitMessage("tp-set");
  let { ExtensionPreferencesManager } = ChromeUtils.importESModule(
    "resource://gre/modules/ExtensionPreferencesManager.sys.mjs"
  );
  let storeSetting = await ExtensionPreferencesManager.getSetting(
    "websites.trackingProtectionMode"
  );
  is(storeSetting?.id, EXTENSION_ID, "Extension is recorded in the store");
  await setting._checkForControllingExtension();
  await radioGroupControl.updateComplete;

  ok(
    !setting.controllingExtensionInfo.id,
    "Extension isn't reported as controlling locked tracking protection"
  );
  ok(!getMessageBar(radioGroupControl), "No message bar while prefs locked");
  ok(
    !getControl(doc, "etpLevelStandard").disabled,
    "Standard radio enabled while prefs locked"
  );
  ok(
    !getControl(doc, "etpLevelStrict").disabled,
    "Strict radio enabled while prefs locked"
  );
});

it("keeps the strict exceptions usable while controlled", async () => {
  await SpecialPowers.pushPrefEnv({
    set: [
      [CAT_PREF, "strict"],
      [BASELINE_PREF, true],
      [CONVENIENCE_PREF, false],
    ],
  });

  let doc;
  ({ doc, tab } = await openEtpPage());
  let radioGroupControl = doc.getElementById(
    "setting-control-contentBlockingCategoryRadioGroup"
  );

  extension = loadTPExtension();
  let messageBarShown = waitForMessageBar(radioGroupControl, true);
  await extension.startup();
  await extension.awaitMessage("tp-set");
  await messageBarShown;

  is(
    Services.prefs.getStringPref(CAT_PREF),
    "strict",
    "Category stays strict while controlled"
  );
  let strictRadio = getControl(doc, "etpLevelStrict");
  ok(strictRadio.checked, "Strict radio is selected");
  ok(!strictRadio.disabled, "Selected strict radio stays enabled");
  ok(
    getControl(doc, "etpLevelStandard").disabled,
    "Standard radio disabled while controlled"
  );
  ok(
    !getControl(doc, "etpLevelCustom").disabled,
    "Custom radio stays enabled while controlled"
  );

  let baselineCheckbox = getControl(doc, "etpAllowListBaselineEnabled");
  ok(
    !baselineCheckbox.disabled && !baselineCheckbox.parentDisabled,
    "Baseline exceptions checkbox enabled while controlled"
  );
  let convenienceCheckbox = getControl(doc, "etpAllowListConvenienceEnabled");
  ok(
    !convenienceCheckbox.disabled && !convenienceCheckbox.parentDisabled,
    "Convenience exceptions checkbox enabled while controlled"
  );

  await clickEtpBaselineCheckboxWithConfirm(
    doc,
    "etpAllowListBaselineEnabled",
    BASELINE_PREF,
    false,
    1
  );
});
