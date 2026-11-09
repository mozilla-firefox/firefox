/* Any copyright is dedicated to the Public Domain.
   https://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

requestLongerTimeout(3);

const { sinon } = ChromeUtils.importESModule(
  "resource://testing-common/Sinon.sys.mjs"
);
const { LinkPreview } = ChromeUtils.importESModule(
  "moz-src:///browser/components/genai/LinkPreview.sys.mjs"
);

describe("settings ai features", () => {
  let doc, win;

  beforeEach(async function setup() {
    await SpecialPowers.pushPrefEnv({
      set: [["browser.preferences.aiControls", true]],
    });
    let aiControlsLoaded = TestUtils.topicObserved("ai-pane-loaded");
    await openPreferencesViaOpenPreferencesAPI("general", { leaveOpen: true });
    await aiControlsLoaded;
    doc = gBrowser.selectedBrowser.contentDocument;
    win = doc.documentGlobal;
  });

  afterEach(() => {
    BrowserTestUtils.removeTab(gBrowser.selectedTab);
  });

  function waitForAnimationFrame() {
    return new Promise(r => win.requestAnimationFrame(r));
  }

  describe("AI Controls visibility on General pane", () => {
    let canRunOnDeviceStub;

    beforeEach(() => {
      canRunOnDeviceStub = sinon
        .stub(LinkPreview, "canRunOnDevice")
        .get(() => true);
    });

    afterEach(() => {
      canRunOnDeviceStub.restore();
    });

    it("hides Link Preview setting when globally blocked via AI Controls toggle", async () => {
      await SpecialPowers.pushPrefEnv({
        set: [
          ["browser.ai.control.default", "available"],
          ["browser.ai.control.linkPreviewKeyPoints", "default"],
          ["browser.ml.linkPreview.enabled", true],
        ],
      });

      let aiControlsTab = gBrowser.selectedTab;
      await openAiFeaturePanel(doc, win);

      await new Promise(resolve => open_preferences(resolve));
      let generalTab = gBrowser.selectedTab;
      let generalDoc = gBrowser.selectedBrowser.contentDocument;
      let generalWin = generalDoc.documentGlobal;
      await maybeNavigateToPane("tabsBrowsing", generalWin);

      let linkPreviewSetting = generalWin.Preferences.getSetting(
        "linkPreviewKeyPoints"
      );
      let linkPreviewControl = generalDoc.getElementById(
        "linkPreviewKeyPoints"
      );
      Assert.ok(
        BrowserTestUtils.isVisible(linkPreviewControl),
        "Link Preview control is visible"
      );

      gBrowser.selectedTab = aiControlsTab;
      const toggle = doc.getElementById("aiControlDefaultToggle");
      const dialogEl = doc.querySelector("block-ai-confirmation-dialog");
      await dialogEl.updateComplete;
      let dialogShown = BrowserTestUtils.waitForEvent(
        dialogEl.dialog,
        "toggle"
      );
      EventUtils.synthesizeMouseAtCenter(toggle.buttonEl, {}, win);
      await dialogShown;
      Assert.ok(dialogEl.dialog.open, "Dialog is open");
      await waitForSettingChange(linkPreviewSetting, () =>
        EventUtils.synthesizeMouseAtCenter(dialogEl.confirmButton, {}, win)
      );

      gBrowser.selectedTab = generalTab;
      Assert.ok(
        !BrowserTestUtils.isVisible(linkPreviewControl),
        "Link Preview control is hidden after blocking"
      );
      Assert.ok(
        BrowserTestUtils.isVisible(
          generalDoc.getElementById("linkPreviewEnabled")
        ),
        "Link Preview toggle is still visible after blocking"
      );

      // Explicitly enable Link Preview while globally blocked
      gBrowser.selectedTab = aiControlsTab;
      const linkPreviewSelect = doc.getElementById(
        "aiControlLinkPreviewKeyPointsSelect"
      );
      linkPreviewSelect.scrollIntoView();
      await waitForAnimationFrame();
      linkPreviewSelect.focus();
      let pickerOpened = BrowserTestUtils.waitForSelectPopupShown(
        win.docShell.chromeEventHandler.documentGlobal
      );
      EventUtils.sendKey("space");
      const selectPopup = await pickerOpened;
      await waitForSettingChange(linkPreviewSetting, () => {
        if (selectPopup.isNativeMenu) {
          selectPopup.activateItem(selectPopup.childNodes[1]);
        } else {
          EventUtils.sendKey("up");
          EventUtils.sendKey("return");
        }
      });

      gBrowser.selectedTab = generalTab;
      Assert.ok(
        BrowserTestUtils.isVisible(linkPreviewControl),
        "Link Preview control is visible after explicitly enabling"
      );

      BrowserTestUtils.removeTab(generalTab);
    });

    it("hides Tab Group Suggestions when globally blocked", async () => {
      await SpecialPowers.pushPrefEnv({
        set: [
          ["browser.ai.control.default", "available"],
          ["browser.ai.control.smartTabGroups", "default"],
          ["browser.tabs.groups.enabled", true],
          ["browser.tabs.groups.smart.enabled", true],
          ["browser.tabs.groups.smart.userEnabled", true],
        ],
      });

      // Tab Group Suggestions is only available in en-* locales
      if (!Services.locale.appLocaleAsBCP47.startsWith("en")) {
        Assert.ok(true, "Skipping: locale is not en-*");
        return;
      }

      let aiControlsTab = gBrowser.selectedTab;
      await openAiFeaturePanel(doc, win);

      await new Promise(resolve => open_preferences(resolve));
      let generalTab = gBrowser.selectedTab;
      let generalDoc = gBrowser.selectedBrowser.contentDocument;
      let generalWin = generalDoc.documentGlobal;
      await maybeNavigateToPane("tabsBrowsing", generalWin);

      let tabGroupSetting = generalWin.Preferences.getSetting(
        "tabGroupSuggestions"
      );
      let tabGroupControl = generalDoc.getElementById("tabGroupSuggestions");
      Assert.ok(
        BrowserTestUtils.isVisible(tabGroupControl),
        "Tab Group Suggestions control is visible"
      );

      gBrowser.selectedTab = aiControlsTab;
      const toggle = doc.getElementById("aiControlDefaultToggle");
      const dialogEl = doc.querySelector("block-ai-confirmation-dialog");
      await dialogEl.updateComplete;
      let dialogShown = BrowserTestUtils.waitForEvent(
        dialogEl.dialog,
        "toggle"
      );
      EventUtils.synthesizeMouseAtCenter(toggle.buttonEl, {}, win);
      await dialogShown;
      Assert.ok(dialogEl.dialog.open, "Dialog is open");
      await waitForSettingChange(tabGroupSetting, () =>
        EventUtils.synthesizeMouseAtCenter(dialogEl.confirmButton, {}, win)
      );

      gBrowser.selectedTab = generalTab;
      Assert.ok(
        !BrowserTestUtils.isVisible(tabGroupControl),
        "Tab Group Suggestions control is hidden after blocking"
      );

      BrowserTestUtils.removeTab(generalTab);
    });

    it("hides Translations setting when globally blocked via AI Controls toggle", async () => {
      await SpecialPowers.pushPrefEnv({
        set: [
          ["browser.ai.control.default", "available"],
          ["browser.ai.control.translations", "default"],
          ["browser.translations.enable", true],
        ],
      });

      let aiControlsTab = gBrowser.selectedTab;
      await openAiFeaturePanel(doc, win);

      await new Promise(resolve => open_preferences(resolve));
      let generalTab = gBrowser.selectedTab;
      let generalDoc = gBrowser.selectedBrowser.contentDocument;
      let generalWin = generalDoc.documentGlobal;
      await maybeNavigateToPane("languages", generalWin);

      let translationsSetting =
        generalWin.Preferences.getSetting("offerTranslations");
      let translationsGroup = generalDoc.querySelector(
        'setting-group[groupid="translations"]'
      );
      Assert.ok(
        BrowserTestUtils.isVisible(translationsGroup),
        "Translations group is visible"
      );

      gBrowser.selectedTab = aiControlsTab;
      const toggle = doc.getElementById("aiControlDefaultToggle");
      const dialogEl = doc.querySelector("block-ai-confirmation-dialog");
      await dialogEl.updateComplete;
      let dialogShown = BrowserTestUtils.waitForEvent(
        dialogEl.dialog,
        "toggle"
      );
      EventUtils.synthesizeMouseAtCenter(toggle.buttonEl, {}, win);
      await dialogShown;
      Assert.ok(dialogEl.dialog.open, "Dialog is open");
      await waitForSettingChange(translationsSetting, () =>
        EventUtils.synthesizeMouseAtCenter(dialogEl.confirmButton, {}, win)
      );

      gBrowser.selectedTab = generalTab;
      await TestUtils.waitForCondition(
        () => !BrowserTestUtils.isVisible(translationsGroup),
        "Translations group is hidden after blocking"
      );

      // Explicitly enable Translations while globally blocked
      gBrowser.selectedTab = aiControlsTab;
      const translationsSelect = doc.getElementById(
        "aiControlTranslationsSelect"
      );
      translationsSelect.scrollIntoView();
      await waitForAnimationFrame();
      translationsSelect.focus();
      let pickerOpened = BrowserTestUtils.waitForSelectPopupShown(
        win.docShell.chromeEventHandler.documentGlobal
      );
      EventUtils.sendKey("space");
      const selectPopup = await pickerOpened;
      await waitForSettingChange(translationsSetting, () => {
        if (selectPopup.isNativeMenu) {
          selectPopup.activateItem(selectPopup.childNodes[0]);
        } else {
          EventUtils.sendKey("up");
          EventUtils.sendKey("return");
        }
      });

      gBrowser.selectedTab = generalTab;
      await TestUtils.waitForCondition(
        () => BrowserTestUtils.isVisible(translationsGroup),
        "Translations group is visible after explicitly enabling"
      );

      BrowserTestUtils.removeTab(generalTab);
    });

    it("shows settings when unblocked via global toggle", async () => {
      await SpecialPowers.pushPrefEnv({
        set: [
          ["browser.ai.control.default", "blocked"],
          ["browser.ai.control.linkPreviewKeyPoints", "default"],
          ["browser.ml.linkPreview.enabled", true],
          ["extensions.ml.enabled", false],
        ],
      });

      let aiControlsTab = gBrowser.selectedTab;
      await openAiFeaturePanel(doc, win);

      await new Promise(resolve => open_preferences(resolve));
      let generalTab = gBrowser.selectedTab;
      let generalDoc = gBrowser.selectedBrowser.contentDocument;
      let generalWin = generalDoc.documentGlobal;
      await maybeNavigateToPane("tabsBrowsing", generalWin);

      let linkPreviewSetting = generalWin.Preferences.getSetting(
        "linkPreviewKeyPoints"
      );
      let linkPreviewControl = generalDoc.getElementById(
        "linkPreviewKeyPoints"
      );
      Assert.ok(
        !BrowserTestUtils.isVisible(linkPreviewControl),
        "Link Preview control is hidden when blocked"
      );

      gBrowser.selectedTab = aiControlsTab;
      const toggle = doc.getElementById("aiControlDefaultToggle");
      Assert.ok(toggle.pressed, "Toggle is pressed (blocked state)");
      await waitForSettingChange(linkPreviewSetting, () =>
        EventUtils.synthesizeMouseAtCenter(toggle.buttonEl, {}, win)
      );

      gBrowser.selectedTab = generalTab;
      Assert.ok(
        BrowserTestUtils.isVisible(linkPreviewControl),
        "Link Preview control is visible after unblocking"
      );

      BrowserTestUtils.removeTab(generalTab);
    });
  });

  describe("showUnavailable pref", () => {
    it("shows Link Preview control in restricted regions when enabled", async () => {
      const { Region } = ChromeUtils.importESModule(
        "resource://gre/modules/Region.sys.mjs"
      );
      const currentRegion = Region.home;

      // We need a region to have a way to hide the row, skip if this didn't work.
      if (!currentRegion) {
        Assert.ok(true, "Skipping: Region.home is not set in test environment");
        return;
      }

      // Block the system's region.
      // Unset showUnavailable to trigger a change which will do the region check.
      await SpecialPowers.pushPrefEnv({
        set: [
          ["browser.ml.linkPreview.noKeyPointsRegions", currentRegion],
          ["browser.preferences.aiControls.showUnavailable", false],
        ],
      });

      win.SettingPaneManager.importPane("ai");

      // Manually trigger a Setting change to re-calculate visibility based on noKeyPointsRegions.
      let aiControlsShowUnavailable = win.Preferences.getSetting(
        "aiControlsShowUnavailable"
      );
      aiControlsShowUnavailable.onChange();

      await openAiFeaturePanel(doc, win);

      let linkPreviewControl = doc.getElementById(
        "aiControlLinkPreviewKeyPointsSelect"
      );
      Assert.ok(
        !BrowserTestUtils.isVisible(linkPreviewControl),
        "Link Preview control is hidden in restricted region"
      );

      // Set showUnavailable to verify the hidden row gets shown
      let linkPreviewSetting = win.Preferences.getSetting(
        "aiControlLinkPreviewKeyPointsSelect"
      );
      await waitForSettingChange(linkPreviewSetting, () => {
        Services.prefs.setBoolPref(
          "browser.preferences.aiControls.showUnavailable",
          true
        );
      });

      Assert.ok(
        BrowserTestUtils.isVisible(linkPreviewControl),
        "Link Preview control is visible when showUnavailable is enabled"
      );

      // Clear the change to showUnavailable and noKeyPointsRegions.
      await SpecialPowers.popPrefEnv();
    });
  });
});
