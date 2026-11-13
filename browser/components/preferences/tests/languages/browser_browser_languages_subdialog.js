/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/ */

requestLongerTimeout(2);

AddonTestUtils.initMochitest(this);

const BROWSER_LANGUAGES_URL =
  "chrome://browser/content/preferences/dialogs/browserLanguages.xhtml";
const DICTIONARY_ID_PL = "pl@dictionaries.addons.mozilla.org";
const TELEMETRY_CATEGORY = "intl.ui.browserLanguage";

let testLocales = ["fr", "pl", "he"];
let testLangpacks;

function createTestLangpacks() {
  if (!testLangpacks) {
    testLangpacks = Promise.all(
      testLocales.map(async locale => [locale, await createLangpack(locale)])
    );
  }
  return testLangpacks;
}

function createLocaleResult(target_locale, url) {
  return {
    guid: langpackId(target_locale),
    type: "language",
    target_locale,
    current_compatible_version: {
      version: "2.0",
      files: [
        {
          platform: "all",
          url,
        },
      ],
    },
  };
}

async function createLanguageToolsFile() {
  let langpacks = await createTestLangpacks();
  let results = langpacks.map(([locale, file]) =>
    createLocaleResult(locale, Services.io.newFileURI(file).spec)
  );

  let filename = "language-tools.json";
  let files = { [filename]: { results } };
  let tempdir = AddonTestUtils.tempDir.clone();
  let dir = await AddonTestUtils.promiseWriteFilesToDir(tempdir.path, files);
  dir.append(filename);

  return dir;
}

async function createDictionaryBrowseResults() {
  let testDir = gTestPath.substr(0, gTestPath.lastIndexOf("/"));
  // Although pl-dictionary.xpi is an unsigned dictionary, and dictionaries are
  // required to be signed, the test can install it anyway because
  // testing/profiles/common/user.js sets xpinstall.signatures.required=false.
  let dictionaryPath = testDir + "/../addons/pl-dictionary.xpi";
  let filename = "dictionaries.json";
  let response = {
    page_size: 25,
    page_count: 1,
    count: 1,
    results: [
      {
        current_version: {
          id: 1823648,
          compatibility: {
            firefox: { max: "9999", min: "4.0" },
          },
          files: [
            {
              platform: "all",
              url: dictionaryPath,
            },
          ],
          version: "1.0.20160228",
        },
        default_locale: "pl",
        description: "Polish spell-check",
        guid: DICTIONARY_ID_PL,
        name: "Polish Dictionary",
        slug: "polish-spellchecker-dictionary",
        status: "public",
        summary: "Polish dictionary",
        type: "dictionary",
      },
    ],
  };

  let files = { [filename]: response };
  let dir = await AddonTestUtils.promiseWriteFilesToDir(
    AddonTestUtils.tempDir.path,
    files
  );
  dir.append(filename);

  return dir;
}

function assertLocaleOrder(list, locales, selectedLocale) {
  is(
    list.itemCount,
    locales.split(",").length,
    "The right number of locales are in the list"
  );
  is(
    Array.from(list.children)
      .map(child => child.value)
      .join(","),
    locales,
    "The listed locales are in order"
  );
  is(
    list.selectedItem.value,
    selectedLocale,
    "The selected item locale matches"
  );
}

function assertAvailableLocales(list, locales) {
  let items = Array.from(list.menupopup.children);
  let listLocales = items.filter(item => item.value && item.value != "search");
  is(
    listLocales.length,
    locales.length,
    "The right number of locales are available"
  );
  is(
    listLocales
      .map(item => item.value)
      .sort()
      .join(","),
    locales.sort().join(","),
    "The available locales match"
  );
  is(items[0].getAttribute("class"), "label-item", "The first row is a label");
}

function getDialogId(dialogDoc) {
  return dialogDoc.documentGlobal.arguments[0].telemetryId;
}

function assertTelemetryRecorded(events) {
  let snapshot = Services.telemetry.snapshotEvents(
    Ci.nsITelemetry.DATASET_PRERELEASE_CHANNELS,
    true
  );

  // Make sure we got some data.
  ok(
    snapshot.parent && !!snapshot.parent.length,
    "Got parent telemetry events in the snapshot"
  );

  // Only look at the related events after stripping the timestamp and category.
  let relatedEvents = snapshot.parent
    .filter(([, category]) => category == TELEMETRY_CATEGORY)
    .map(relatedEvent => relatedEvent.slice(2, 6));

  // Events are now an array of: method, object[, value[, extra]] as expected.
  Assert.deepEqual(relatedEvents, events, "The events are recorded correctly");
}

async function selectLocale(localeCode, available, selected, dialogDoc) {
  let [locale] = Array.from(available.menupopup.children).filter(
    item => item.value == localeCode
  );
  available.selectedItem = locale;

  // Get ready for the selected list to change.
  let added = waitForMutation(selected, { childList: true }, target =>
    Array.from(target.children).some(el => el.value == localeCode)
  );

  // Add the locale.
  dialogDoc.getElementById("add").doCommand();

  // Wait for the list to update.
  await added;
}

// Select a locale from the list of already added locales.
function selectAddedLocale(localeCode, selected) {
  selected.selectedItem = selected.querySelector(`[value="${localeCode}"]`);
}

async function openDialog(doc, search = false) {
  let dialogLoaded = promiseLoadSubDialog(BROWSER_LANGUAGES_URL);
  if (search) {
    doc.getElementById("primaryBrowserLocaleSearch").doCommand();
    doc.getElementById("primaryBrowserLocale").menupopup.hidePopup();
  } else {
    doc.getElementById("manageBrowserLanguagesButton").doCommand();
  }
  let dialogWin = await dialogLoaded;
  let dialogDoc = dialogWin.document;
  return {
    dialog: dialogDoc.getElementById("BrowserLanguagesDialog"),
    dialogDoc,
    available: dialogDoc.getElementById("availableLocales"),
    selected: dialogDoc.getElementById("selectedLocales"),
  };
}

add_setup(function setup() {
  Services.telemetry.clearEvents();
});

add_task(async function testDisabledBrowserLanguages() {
  let langpacksFile = await createLanguageToolsFile();
  let langpacksUrl = Services.io.newFileURI(langpacksFile).spec;

  await SpecialPowers.pushPrefEnv({
    set: [
      ["intl.multilingual.enabled", true],
      ["intl.multilingual.downloadEnabled", true],
      ["intl.multilingual.liveReload", false],
      ["intl.multilingual.liveReloadBidirectional", false],
      ["intl.locale.requested", "en-US,pl,he,de"],
      ["extensions.langpacks.signatures.required", false],
      ["extensions.getAddons.langpacks.url", langpacksUrl],
    ],
  });

  // Install an old pl langpack.
  let oldLangpack = await createLangpack("pl", "1.0");
  await AddonTestUtils.promiseInstallFile(oldLangpack);

  // Install all the other available langpacks.
  let pl;
  let langpacks = await createTestLangpacks();
  let addons = await Promise.all(
    langpacks.map(async ([locale, file]) => {
      if (locale == "pl") {
        pl = await AddonManager.getAddonByID(langpackId("pl"));
        // Disable pl so it's removed from selected.
        await pl.disable();
        return pl;
      }
      let install = await AddonTestUtils.promiseInstallFile(file);
      return install.addon;
    })
  );

  await openPreferencesViaOpenPreferencesAPI("paneGeneral", {
    leaveOpen: true,
  });

  let doc = gBrowser.contentDocument;
  let { dialogDoc, available, selected } = await openDialog(doc);

  // pl is not selected since it's disabled.
  is(pl.userDisabled, true, "pl is disabled");
  is(pl.version, "1.0", "pl is the old 1.0 version");
  assertLocaleOrder(selected, "en-US,he", "en-US");

  // Wait for the children menu to be populated.
  await BrowserTestUtils.waitForCondition(
    () => !!available.children.length,
    "Children list populated"
  );

  // Only fr is enabled and not selected, so it's the only locale available.
  assertAvailableLocales(available, ["fr"]);

  // Search for more languages.
  available.menupopup.lastElementChild.doCommand();
  available.menupopup.hidePopup();
  await waitForMutation(available.menupopup, { childList: true }, () =>
    Array.from(available.menupopup.children).some(
      locale => locale.value == "pl"
    )
  );

  // pl is now available since it is available remotely.
  assertAvailableLocales(available, ["fr", "pl"]);

  let installId = null;
  AddonTestUtils.promiseInstallEvent("onInstallEnded").then(([install]) => {
    installId = install.installId;
  });

  // Add pl.
  await selectLocale("pl", available, selected, dialogDoc);
  assertLocaleOrder(selected, "pl,en-US,he", "pl");

  // Find pl again since it's been upgraded.
  pl = await AddonManager.getAddonByID(langpackId("pl"));
  is(pl.userDisabled, false, "pl is now enabled");
  is(pl.version, "2.0", "pl is upgraded to version 2.0");

  let dialogId = getDialogId(dialogDoc);
  ok(dialogId, "There's a dialogId");
  ok(installId, "There's an installId");

  await Promise.all(addons.map(addon => addon.uninstall()));
  BrowserTestUtils.removeTab(gBrowser.selectedTab);

  assertTelemetryRecorded([
    ["manage", "main", dialogId],
    ["search", "dialog", dialogId],
    ["add", "dialog", dialogId, { installId }],

    // Cancel is recorded when the tab is closed.
    ["cancel", "dialog", dialogId],
  ]);
});

add_task(async function testReorderingBrowserLanguages() {
  await SpecialPowers.pushPrefEnv({
    set: [
      ["intl.multilingual.enabled", true],
      ["intl.multilingual.downloadEnabled", true],
      ["intl.multilingual.liveReload", false],
      ["intl.multilingual.liveReloadBidirectional", false],
      ["intl.locale.requested", "en-US,pl,he,de"],
      ["extensions.langpacks.signatures.required", false],
    ],
  });

  // Install all the available langpacks.
  let langpacks = await createTestLangpacks();
  let addons = await Promise.all(
    langpacks.map(async ([, file]) => {
      let install = await AddonTestUtils.promiseInstallFile(file);
      return install.addon;
    })
  );

  await openPreferencesViaOpenPreferencesAPI("paneGeneral", {
    leaveOpen: true,
  });

  let doc = gBrowser.contentDocument;
  let messageBar = doc.getElementById("confirmBrowserLanguage");
  is(messageBar.hidden, true, "The message bar is hidden at first");

  // Open the dialog.
  let { dialog, dialogDoc, selected } = await openDialog(doc);
  let firstDialogId = getDialogId(dialogDoc);

  // The initial order is set by the pref, filtered by available.
  assertLocaleOrder(selected, "en-US,pl,he", "en-US");

  // Moving pl down changes the order.
  selectAddedLocale("pl", selected);
  dialogDoc.getElementById("down").doCommand();
  assertLocaleOrder(selected, "en-US,he,pl", "pl");

  // Accepting the change shows the confirm message bar.
  let dialogClosed = BrowserTestUtils.waitForEvent(dialog, "dialogclosing");
  dialog.acceptDialog();
  await dialogClosed;

  // The message bar uses async `formatValues` and that may resolve
  // after the dialog is closed.
  await BrowserTestUtils.waitForMutationCondition(
    messageBar,
    { attributes: true },
    () => !messageBar.hidden
  );
  is(
    messageBar.querySelector("button").getAttribute("locales"),
    "en-US,he,pl",
    "The locales are set on the message bar button"
  );

  // Open the dialog again.
  let newDialog = await openDialog(doc);
  dialog = newDialog.dialog;
  dialogDoc = newDialog.dialogDoc;
  let secondDialogId = getDialogId(dialogDoc);
  selected = newDialog.selected;

  // The initial order comes from the previous settings.
  assertLocaleOrder(selected, "en-US,he,pl", "en-US");

  // Select pl in the list.
  selectAddedLocale("pl", selected);
  // Move pl back up.
  dialogDoc.getElementById("up").doCommand();
  assertLocaleOrder(selected, "en-US,pl,he", "pl");

  // Accepting the change hides the confirm message bar.
  dialogClosed = BrowserTestUtils.waitForEvent(dialog, "dialogclosing");
  dialog.acceptDialog();
  await dialogClosed;
  is(messageBar.hidden, true, "The message bar is hidden again");

  ok(firstDialogId, "There was an id on the first dialog");
  ok(secondDialogId, "There was an id on the second dialog");
  Assert.notEqual(
    firstDialogId,
    secondDialogId,
    "The dialog ids are different"
  );
  Assert.less(
    parseInt(firstDialogId),
    parseInt(secondDialogId),
    "The second dialog id is larger than the first"
  );

  // Open the dialog yet again.
  newDialog = await openDialog(doc);
  dialog = newDialog.dialog;
  dialogDoc = newDialog.dialogDoc;
  let thirdDialogId = getDialogId(dialogDoc);
  selected = newDialog.selected;

  // Move pl to the top.
  selectAddedLocale("pl", selected);
  assertLocaleOrder(selected, "en-US,he,pl", "pl");
  dialogDoc.getElementById("up").doCommand();
  dialogDoc.getElementById("up").doCommand();
  assertLocaleOrder(selected, "pl,en-US,he", "pl");

  dialogClosed = BrowserTestUtils.waitForEvent(dialog, "dialogclosing");
  dialog.acceptDialog();
  await dialogClosed;

  await Promise.all(addons.map(addon => addon.uninstall()));
  BrowserTestUtils.removeTab(gBrowser.selectedTab);

  assertTelemetryRecorded([
    ["manage", "main", firstDialogId],
    ["reorder", "dialog", firstDialogId],
    ["accept", "dialog", firstDialogId],
    ["set_fallback", "dialog", firstDialogId],
    ["manage", "main", secondDialogId],
    ["reorder", "dialog", secondDialogId],
    ["accept", "dialog", secondDialogId],
    ["manage", "main", thirdDialogId],
    ["reorder", "dialog", thirdDialogId],
    ["reorder", "dialog", thirdDialogId],
    ["accept", "dialog", thirdDialogId],
  ]);
});

add_task(async function testAddAndRemoveSelectedLanguages() {
  await SpecialPowers.pushPrefEnv({
    set: [
      ["intl.multilingual.enabled", true],
      ["intl.multilingual.downloadEnabled", true],
      ["intl.multilingual.liveReload", false],
      ["intl.multilingual.liveReloadBidirectional", false],
      ["intl.locale.requested", "en-US"],
      ["extensions.langpacks.signatures.required", false],
    ],
  });

  let langpacks = await createTestLangpacks();
  let addons = await Promise.all(
    langpacks.map(async ([, file]) => {
      let install = await AddonTestUtils.promiseInstallFile(file);
      return install.addon;
    })
  );

  await openPreferencesViaOpenPreferencesAPI("paneGeneral", {
    leaveOpen: true,
  });

  let doc = gBrowser.contentDocument;
  let messageBar = doc.getElementById("confirmBrowserLanguage");
  is(messageBar.hidden, true, "The message bar is hidden at first");

  // Open the dialog.
  let { dialog, dialogDoc, available, selected } = await openDialog(doc);
  let dialogId = getDialogId(dialogDoc);

  // loadLocalesFromAMO is async but `initAvailableLocales` doesn't wait
  // for it to be resolved, so we have to wait for the list to be populated
  // before we test for its values.
  await BrowserTestUtils.waitForMutationCondition(
    available.menupopup,
    { attributes: true, childList: true },
    () => {
      let listLocales = Array.from(available.menupopup.children).filter(
        item => item.value && item.value != "search"
      );
      return listLocales.length == 3;
    }
  );
  // The initial order is set by the pref.
  assertLocaleOrder(selected, "en-US", "en-US");
  assertAvailableLocales(available, ["fr", "pl", "he"]);

  let removeButton = dialogDoc.getElementById("remove");
  // Cannot remove the default locale.
  is(removeButton.disabled, true, "Remove en-US should be disabled");

  // Add pl and fr to selected.
  await selectLocale("pl", available, selected, dialogDoc);
  await selectLocale("fr", available, selected, dialogDoc);

  assertLocaleOrder(selected, "fr,pl,en-US", "fr");
  assertAvailableLocales(available, ["he"]);

  // Can remove the added locale again.
  is(removeButton.disabled, false, "Remove fr should be not be disabled");

  selectAddedLocale("en-US", selected);
  // Cannot remove the default locale, even after adding more.
  is(removeButton.disabled, true, "Remove en-us should still be disabled");

  // Remove pl and fr from selected.
  selectAddedLocale("fr", selected);
  is(removeButton.disabled, false, "Remove fr should be not be disabled");
  removeButton.doCommand();
  // Selection moves to pl.
  assertLocaleOrder(selected, "pl,en-US", "pl");
  is(removeButton.disabled, false, "Remove pl should be not be disabled");
  removeButton.doCommand();
  assertLocaleOrder(selected, "en-US", "en-US");
  assertAvailableLocales(available, ["fr", "pl", "he"]);
  is(removeButton.disabled, true, "Remove en-us should be disabled at end");

  // Add he to selected.
  await selectLocale("he", available, selected, dialogDoc);
  assertLocaleOrder(selected, "he,en-US", "he");
  assertAvailableLocales(available, ["pl", "fr"]);

  // Accepting the change shows the confirm message bar.
  let dialogClosed = BrowserTestUtils.waitForEvent(dialog, "dialogclosing");
  dialog.acceptDialog();
  await dialogClosed;

  await waitForMutation(
    messageBar,
    { attributes: true, attributeFilter: ["hidden"] },
    target => !target.hidden
  );

  is(messageBar.hidden, false, "The message bar is now visible");
  is(
    messageBar.querySelector("button").getAttribute("locales"),
    "he,en-US",
    "The locales are set on the message bar button"
  );

  await Promise.all(addons.map(addon => addon.uninstall()));
  BrowserTestUtils.removeTab(gBrowser.selectedTab);

  assertTelemetryRecorded([
    ["manage", "main", dialogId],

    // Install id is not recorded since it was already installed.
    ["add", "dialog", dialogId],
    ["add", "dialog", dialogId],

    ["remove", "dialog", dialogId],
    ["remove", "dialog", dialogId],

    ["add", "dialog", dialogId],
    ["accept", "dialog", dialogId],
  ]);
});

add_task(async function testInstallFromAMO() {
  let langpacks = await AddonManager.getAddonsByTypes(["locale"]);
  is(langpacks.length, 0, "There are no langpacks installed");

  let langpacksFile = await createLanguageToolsFile();
  let langpacksUrl = Services.io.newFileURI(langpacksFile).spec;
  let dictionaryBrowseFile = await createDictionaryBrowseResults();
  let browseApiEndpoint = Services.io.newFileURI(dictionaryBrowseFile).spec;

  await SpecialPowers.pushPrefEnv({
    set: [
      ["intl.multilingual.enabled", true],
      ["intl.multilingual.downloadEnabled", true],
      ["intl.multilingual.liveReload", false],
      ["intl.multilingual.liveReloadBidirectional", false],
      ["intl.locale.requested", "en-US"],
      ["extensions.getAddons.langpacks.url", langpacksUrl],
      ["extensions.langpacks.signatures.required", false],
      ["extensions.getAddons.get.url", browseApiEndpoint],
    ],
  });

  await openPreferencesViaOpenPreferencesAPI("paneGeneral", {
    leaveOpen: true,
  });

  let doc = gBrowser.contentDocument;
  let messageBar = doc.getElementById("confirmBrowserLanguage");
  is(messageBar.hidden, true, "The message bar is hidden at first");

  // Verify only en-US is listed on the main pane.
  let getMainPaneLocales = () => {
    let available = doc.getElementById("primaryBrowserLocale");
    let availableLocales = Array.from(available.menupopup.children);
    return availableLocales
      .map(item => item.value)
      .sort()
      .join(",");
  };
  is(getMainPaneLocales(), "en-US,search", "Only en-US installed to start");

  // Open the dialog.
  let { dialog, dialogDoc, available, selected } = await openDialog(doc, true);
  let firstDialogId = getDialogId(dialogDoc);

  // Make sure the message bar is still hidden.
  is(
    messageBar.hidden,
    true,
    "The message bar is still hidden after searching"
  );

  if (available.itemCount == 1) {
    await waitForMutation(
      available.menupopup,
      { childList: true },
      () => available.itemCount > 1
    );
  }

  // The initial order is set by the pref.
  assertLocaleOrder(selected, "en-US", "en-US");
  assertAvailableLocales(available, ["fr", "he", "pl"]);
  is(
    Services.locale.availableLocales.join(","),
    "en-US",
    "There is only one installed locale"
  );

  // Verify that there are no extra dictionaries.
  let dicts = await AddonManager.getAddonsByTypes(["dictionary"]);
  is(dicts.length, 0, "There are no installed dictionaries");

  let installId = null;
  AddonTestUtils.promiseInstallEvent("onInstallEnded").then(([install]) => {
    installId = install.installId;
  });

  // Add Polish, this will install the langpack.
  await selectLocale("pl", available, selected, dialogDoc);

  ok(installId, "We got an installId for the langpack installation");

  let langpack = await AddonManager.getAddonByID(langpackId("pl"));
  Assert.deepEqual(
    langpack.installTelemetryInfo,
    { source: "about:preferences" },
    "The source is set to preferences"
  );

  // Verify the list is correct.
  assertLocaleOrder(selected, "pl,en-US", "pl");
  assertAvailableLocales(available, ["fr", "he"]);
  is(
    Services.locale.availableLocales.sort().join(","),
    "en-US,pl",
    "Polish is now installed"
  );

  await BrowserTestUtils.waitForCondition(async () => {
    let newDicts = await AddonManager.getAddonsByTypes(["dictionary"]);
    let done = !!newDicts.length;

    if (done) {
      is(
        newDicts[0].id,
        DICTIONARY_ID_PL,
        "The polish dictionary was installed"
      );
    }

    return done;
  });

  // Move pl down the list, which prevents an error since it isn't valid.
  dialogDoc.getElementById("down").doCommand();
  assertLocaleOrder(selected, "en-US,pl", "pl");

  // Test that disabling the langpack removes it from the list.
  let dialogClosed = BrowserTestUtils.waitForEvent(dialog, "dialogclosing");
  dialog.acceptDialog();
  await dialogClosed;

  // Verify pl is now available to select.
  is(getMainPaneLocales(), "en-US,pl,search", "en-US and pl now available");

  // Disable the Polish langpack.
  langpack = await AddonManager.getAddonByID("langpack-pl@firefox.mozilla.org");
  await langpack.disable();

  ({ dialogDoc, available, selected } = await openDialog(doc, true));
  let secondDialogId = getDialogId(dialogDoc);

  // Wait for the available langpacks to load.
  if (available.itemCount == 1) {
    await waitForMutation(
      available.menupopup,
      { childList: true },
      () => available.itemCount > 1
    );
  }
  assertLocaleOrder(selected, "en-US", "en-US");
  assertAvailableLocales(available, ["fr", "he", "pl"]);

  // Uninstall the langpack and dictionary.
  let installs = await AddonManager.getAddonsByTypes(["locale", "dictionary"]);
  is(installs.length, 2, "There is one langpack and one dictionary installed");
  await Promise.all(installs.map(item => item.uninstall()));

  BrowserTestUtils.removeTab(gBrowser.selectedTab);

  assertTelemetryRecorded([
    // First dialog installs a locale and accepts.
    ["search", "main", firstDialogId],
    // It has an installId since it was downloaded.
    ["add", "dialog", firstDialogId, { installId }],
    // It got moved down to avoid errors with finding translations.
    ["reorder", "dialog", firstDialogId],
    ["accept", "dialog", firstDialogId],

    // The second dialog just checks the state and is closed with the tab.
    ["search", "main", secondDialogId],
    ["cancel", "dialog", secondDialogId],
  ]);
});

let hasSearchOption = popup =>
  Array.from(popup.children).some(el => el.value == "search");

add_task(async function testDownloadEnabled() {
  await SpecialPowers.pushPrefEnv({
    set: [
      ["intl.multilingual.enabled", true],
      ["intl.multilingual.downloadEnabled", true],
      ["intl.multilingual.liveReload", false],
      ["intl.multilingual.liveReloadBidirectional", false],
    ],
  });

  await openPreferencesViaOpenPreferencesAPI("paneGeneral", {
    leaveOpen: true,
  });
  let doc = gBrowser.contentDocument;

  let defaultMenulist = doc.getElementById("primaryBrowserLocale");
  ok(
    hasSearchOption(defaultMenulist.menupopup),
    "There's a search option in the General pane"
  );

  let { available } = await openDialog(doc, false);
  ok(
    hasSearchOption(available.menupopup),
    "There's a search option in the dialog"
  );

  BrowserTestUtils.removeTab(gBrowser.selectedTab);
});

add_task(async function testDownloadDisabled() {
  await SpecialPowers.pushPrefEnv({
    set: [
      ["intl.multilingual.enabled", true],
      ["intl.multilingual.downloadEnabled", false],
      ["intl.multilingual.liveReload", false],
      ["intl.multilingual.liveReloadBidirectional", false],
    ],
  });

  await openPreferencesViaOpenPreferencesAPI("paneGeneral", {
    leaveOpen: true,
  });
  let doc = gBrowser.contentDocument;

  let defaultMenulist = doc.getElementById("primaryBrowserLocale");
  ok(
    !hasSearchOption(defaultMenulist.menupopup),
    "There's no search option in the General pane"
  );

  let { available } = await openDialog(doc, false);
  ok(
    !hasSearchOption(available.menupopup),
    "There's no search option in the dialog"
  );

  BrowserTestUtils.removeTab(gBrowser.selectedTab);
});

add_task(async function testReorderMainPane() {
  await SpecialPowers.pushPrefEnv({
    set: [
      ["intl.multilingual.enabled", true],
      ["intl.multilingual.downloadEnabled", false],
      ["intl.multilingual.liveReload", false],
      ["intl.multilingual.liveReloadBidirectional", false],
      ["intl.locale.requested", "en-US"],
      ["extensions.langpacks.signatures.required", false],
    ],
  });

  // Clear the telemetry from other tests.
  Services.telemetry.clearEvents();

  let langpacks = await createTestLangpacks();
  let addons = await Promise.all(
    langpacks.map(async ([, file]) => {
      let install = await AddonTestUtils.promiseInstallFile(file);
      return install.addon;
    })
  );

  await openPreferencesViaOpenPreferencesAPI("paneGeneral", {
    leaveOpen: true,
  });
  let doc = gBrowser.contentDocument;

  let messageBar = doc.getElementById("confirmBrowserLanguage");
  is(messageBar.hidden, true, "The message bar is hidden at first");

  let available = doc.getElementById("primaryBrowserLocale");
  let availableLocales = Array.from(available.menupopup.children);
  let availableCodes = availableLocales
    .map(item => item.value)
    .sort()
    .join(",");
  is(
    availableCodes,
    "en-US,fr,he,pl",
    "All of the available locales are listed"
  );

  is(available.selectedItem.value, "en-US", "English is selected");

  let hebrew = availableLocales.find(item => item.value == "he");
  hebrew.click();
  available.menupopup.hidePopup();

  await BrowserTestUtils.waitForCondition(
    () => !messageBar.hidden,
    "Wait for message bar to show"
  );

  is(messageBar.hidden, false, "The message bar is now shown");
  is(
    messageBar.querySelector("button").getAttribute("locales"),
    "he,en-US",
    "The locales are set on the message bar button"
  );

  await Promise.all(addons.map(addon => addon.uninstall()));
  BrowserTestUtils.removeTab(gBrowser.selectedTab);

  assertTelemetryRecorded([["reorder", "main"]]);
});

add_task(async function testLiveLanguageReloading() {
  await SpecialPowers.pushPrefEnv({
    set: [
      ["intl.multilingual.enabled", true],
      ["intl.multilingual.downloadEnabled", true],
      ["intl.multilingual.liveReload", true],
      ["intl.multilingual.liveReloadBidirectional", false],
      ["intl.locale.requested", "en-US,fr,he,de"],
      ["extensions.langpacks.signatures.required", false],
    ],
  });

  // Clear the telemetry from other tests.
  Services.telemetry.clearEvents();

  let langpacks = await createTestLangpacks();
  let addons = await Promise.all(
    langpacks.map(async ([, file]) => {
      let install = await AddonTestUtils.promiseInstallFile(file);
      return install.addon;
    })
  );

  await openPreferencesViaOpenPreferencesAPI("paneGeneral", {
    leaveOpen: true,
  });

  let doc = gBrowser.contentDocument;

  let available = doc.getElementById("primaryBrowserLocale");
  let availableLocales = Array.from(available.menupopup.children);

  is(
    Services.locale.appLocaleAsBCP47,
    "en-US",
    "The app locale starts as English."
  );

  Assert.deepEqual(
    Services.locale.requestedLocales,
    ["en-US", "fr", "he", "de"],
    "The locale order starts as what was initially requested."
  );

  // French and English are both LTR languages.
  let french = availableLocales.find(item => item.value == "fr");

  french.click();
  available.menupopup.hidePopup();

  is(
    Services.locale.appLocaleAsBCP47,
    "fr",
    "The app locale was changed to French"
  );

  Assert.deepEqual(
    Services.locale.requestedLocales,
    ["fr", "en-US", "he", "de"],
    "The locale order is switched to french first."
  );

  await Promise.all(addons.map(addon => addon.uninstall()));
  BrowserTestUtils.removeTab(gBrowser.selectedTab);

  assertTelemetryRecorded([["reorder", "main"]]);
});

add_task(async function testLiveLanguageReloadingBidiOff() {
  await SpecialPowers.pushPrefEnv({
    set: [
      ["intl.multilingual.enabled", true],
      ["intl.multilingual.downloadEnabled", true],
      ["intl.multilingual.liveReload", true],
      ["intl.multilingual.liveReloadBidirectional", false],
      ["intl.locale.requested", "en-US,fr,he,de"],
      ["extensions.langpacks.signatures.required", false],
    ],
  });

  // Clear the telemetry from other tests.
  Services.telemetry.clearEvents();

  let langpacks = await createTestLangpacks();
  let addons = await Promise.all(
    langpacks.map(async ([, file]) => {
      let install = await AddonTestUtils.promiseInstallFile(file);
      return install.addon;
    })
  );

  await openPreferencesViaOpenPreferencesAPI("paneGeneral", {
    leaveOpen: true,
  });

  let doc = gBrowser.contentDocument;

  let available = doc.getElementById("primaryBrowserLocale");
  let availableLocales = Array.from(available.menupopup.children);

  is(
    Services.locale.appLocaleAsBCP47,
    "en-US",
    "The app locale starts as English."
  );

  Assert.deepEqual(
    Services.locale.requestedLocales,
    ["en-US", "fr", "he", "de"],
    "The locale order starts as what was initially requested."
  );

  let messageBar = doc.getElementById("confirmBrowserLanguage");
  is(messageBar.hidden, true, "The message bar is hidden at first");

  // English is LTR and Hebrew is RTL.
  let hebrew = availableLocales.find(item => item.value == "he");

  hebrew.click();
  available.menupopup.hidePopup();

  await BrowserTestUtils.waitForCondition(
    () => !messageBar.hidden,
    "Wait for message bar to show"
  );

  is(messageBar.hidden, false, "The message bar is now shown");

  is(
    Services.locale.appLocaleAsBCP47,
    "en-US",
    "The app locale remains in English"
  );

  Assert.deepEqual(
    Services.locale.requestedLocales,
    ["en-US", "fr", "he", "de"],
    "The locale order did not change."
  );

  await Promise.all(addons.map(addon => addon.uninstall()));

  assertTelemetryRecorded([["reorder", "main"]]);

  BrowserTestUtils.removeTab(gBrowser.selectedTab);
});

add_task(async function testLiveLanguageReloadingBidiOn() {
  await SpecialPowers.pushPrefEnv({
    set: [
      ["intl.multilingual.enabled", true],
      ["intl.multilingual.downloadEnabled", true],
      ["intl.multilingual.liveReload", true],
      ["intl.multilingual.liveReloadBidirectional", true],
      ["intl.locale.requested", "en-US,fr,he,de"],
      ["extensions.langpacks.signatures.required", false],
    ],
  });

  // Clear the telemetry from other tests.
  Services.telemetry.clearEvents();

  let langpacks = await createTestLangpacks();
  let addons = await Promise.all(
    langpacks.map(async ([, file]) => {
      let install = await AddonTestUtils.promiseInstallFile(file);
      return install.addon;
    })
  );

  await openPreferencesViaOpenPreferencesAPI("paneGeneral", {
    leaveOpen: true,
  });

  let doc = gBrowser.contentDocument;

  let available = doc.getElementById("primaryBrowserLocale");
  let availableLocales = Array.from(available.menupopup.children);

  is(
    Services.locale.appLocaleAsBCP47,
    "en-US",
    "The app locale starts as English."
  );

  Assert.deepEqual(
    Services.locale.requestedLocales,
    ["en-US", "fr", "he", "de"],
    "The locale order starts as what was initially requested."
  );

  let messageBar = doc.getElementById("confirmBrowserLanguage");
  is(messageBar.hidden, true, "The message bar is hidden at first");

  // English is LTR and Hebrew is RTL.
  let hebrew = availableLocales.find(item => item.value == "he");

  hebrew.click();
  available.menupopup.hidePopup();

  is(messageBar.hidden, true, "The message bar is still hidden");

  is(
    Services.locale.appLocaleAsBCP47,
    "he",
    "The app locale was changed to Hebrew."
  );

  Assert.deepEqual(
    Services.locale.requestedLocales,
    ["he", "en-US", "fr", "de"],
    "The locale changed with Hebrew first."
  );

  await Promise.all(addons.map(addon => addon.uninstall()));

  assertTelemetryRecorded([["reorder", "main"]]);

  BrowserTestUtils.removeTab(gBrowser.selectedTab);
});
