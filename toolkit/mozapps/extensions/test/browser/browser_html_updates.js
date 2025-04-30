/* eslint max-len: ["error", 80] */

const { AddonTestUtils } = ChromeUtils.importESModule(
  "resource://testing-common/AddonTestUtils.sys.mjs"
);
const { EnterprisePolicyTesting } = ChromeUtils.importESModule(
  "resource://testing-common/EnterprisePolicyTesting.sys.mjs"
);

AddonTestUtils.initMochitest(this);

const server = AddonTestUtils.createHttpServer();

const initialAutoUpdate = AddonManager.autoUpdateDefault;
registerCleanupFunction(() => {
  AddonManager.autoUpdateDefault = initialAutoUpdate;
});

add_setup(async function () {
  await SpecialPowers.pushPrefEnv({
    set: [["extensions.checkUpdateSecurity", false]],
  });

  Services.telemetry.clearEvents();
  registerCleanupFunction(() => {
    cleanupPendingNotifications();
  });
});

function loadDetailView(win, id) {
  let doc = win.document;
  let card = doc.querySelector(`addon-card[addon-id="${id}"]`);
  let loaded = waitForViewLoad(win);
  EventUtils.synthesizeMouseAtCenter(
    card.querySelector(".addon-name-link"),
    { clickCount: 1 },
    win
  );
  return loaded;
}

add_task(async function testChangeAutoUpdates() {
  let id = "test@mochi.test";
  let extension = ExtensionTestUtils.loadExtension({
    manifest: {
      name: "Test",
      browser_specific_settings: { gecko: { id } },
    },
    // Use permanent so the add-on can be updated.
    useAddonManager: "permanent",
  });

  await extension.startup();
  let addon = await AddonManager.getAddonByID(id);

  let win = await loadInitialView("extension");
  let doc = win.document;

  let getInputs = () => ({
    default: updatesRow.querySelector('input[value="1"]'),
    on: updatesRow.querySelector('input[value="2"]'),
    off: updatesRow.querySelector('input[value="0"]'),
    checkForUpdate: updatesRow.querySelector('[action="update-check"]'),
  });

  await loadDetailView(win, id);

  let card = doc.querySelector(`addon-card[addon-id="${id}"]`);
  ok(card.querySelector("addon-details"), "The card now has details");

  let updatesRow = card.querySelector(".addon-detail-row-updates");
  let inputs = getInputs(updatesRow);
  is(addon.applyBackgroundUpdates, 1, "Default is set");
  ok(inputs.default.checked, "The default option is selected");
  ok(inputs.checkForUpdate.hidden, "Update check is hidden");

  inputs.on.click();
  is(addon.applyBackgroundUpdates, "2", "Updates are now enabled");
  ok(inputs.on.checked, "The on option is selected");
  ok(inputs.checkForUpdate.hidden, "Update check is hidden");

  inputs.off.click();
  is(addon.applyBackgroundUpdates, "0", "Updates are now disabled");
  ok(inputs.off.checked, "The off option is selected");
  ok(!inputs.checkForUpdate.hidden, "Update check is visible");

  // Go back to the list view and check the details view again.
  let loaded = waitForViewLoad(win);
  doc.querySelector(".back-button").click();
  await loaded;

  // Load the detail view again.
  await loadDetailView(win, id);

  card = doc.querySelector(`addon-card[addon-id="${id}"]`);
  updatesRow = card.querySelector(".addon-detail-row-updates");
  inputs = getInputs(updatesRow);

  ok(inputs.off.checked, "Off is still selected");

  // Disable global updates.
  let updated = BrowserTestUtils.waitForEvent(card, "update");
  AddonManager.autoUpdateDefault = false;
  await updated;

  // Updates are still the same.
  is(addon.applyBackgroundUpdates, "0", "Updates are now disabled");
  ok(inputs.off.checked, "The off option is selected");
  ok(!inputs.checkForUpdate.hidden, "Update check is visible");

  // Check default.
  inputs.default.click();
  is(addon.applyBackgroundUpdates, "1", "Default is set");
  ok(inputs.default.checked, "The default option is selected");
  ok(!inputs.checkForUpdate.hidden, "Update check is visible");

  inputs.on.click();
  is(addon.applyBackgroundUpdates, "2", "Updates are now enabled");
  ok(inputs.on.checked, "The on option is selected");
  ok(inputs.checkForUpdate.hidden, "Update check is hidden");

  // Enable updates again.
  updated = BrowserTestUtils.waitForEvent(card, "update");
  AddonManager.autoUpdateDefault = true;
  await updated;

  await closeView(win);
  await extension.unload();
});

async function setupExtensionWithUpdate(
  id,
  { releaseNotes, cancelUpdate, policy } = {}
) {
  let serverHost = `http://localhost:${server.identity.primaryPort}`;
  let updatesPath = `/ext-updates-${id}.json`;

  let baseManifest = {
    name: "Updates",
    icons: { 48: "an-icon.png" },
    browser_specific_settings: {
      gecko: {
        id,
        update_url: serverHost + updatesPath,
      },
    },
  };

  let updateXpi = AddonTestUtils.createTempWebExtensionFile({
    manifest: {
      ...baseManifest,
      version: "2",
      // Include permissions in the updated extension, to make
      // sure that we trigger the permission prompt as expected
      // (and that we can accept or cancel the update by observing
      // the underlying observerService notification).
      permissions: ["bookmarks", "http://*.example.com/*"],
    },
  });

  let releaseNotesExtra = {};
  if (releaseNotes) {
    let notesPath = "/notes.txt";
    server.registerPathHandler(notesPath, (request, response) => {
      if (releaseNotes == "ERROR") {
        response.setStatusLine(null, 404, "Not Found");
      } else {
        response.setStatusLine(null, 200, "OK");
        response.write(releaseNotes);
      }
      response.processAsync();
      response.finish();
    });
    releaseNotesExtra.update_info_url = serverHost + notesPath;
  }

  let xpiFilename = `/update-${id}.xpi`;
  server.registerFile(xpiFilename, updateXpi);
  AddonTestUtils.registerJSON(server, updatesPath, {
    addons: {
      [id]: {
        updates: [
          {
            version: "2",
            update_link: serverHost + xpiFilename,
            ...releaseNotesExtra,
          },
        ],
      },
    },
  });

  if (policy) {
    // If policy is used, we need to explicitly install the extension
    // via policy and there is no prompt.
    let originalXpi = AddonTestUtils.createTempWebExtensionFile({
      manifest: {
        ...baseManifest,
        version: "1",
      },
    });

    let xpiFilename = `/original-${id}.xpi`;
    server.registerFile(xpiFilename, originalXpi);
    await Promise.all([
      AddonTestUtils.promiseInstallEvent("onInstallEnded"),
      EnterprisePolicyTesting.setupPolicyEngineWithJson({
        policies: {
          ExtensionSettings: {
            [id]: {
              installation_mode: "force_installed",
              install_url: serverHost + xpiFilename,
            },
          },
        },
      }),
    ]);
    return null;
  }

  handlePermissionPrompt({ addonId: id, reject: cancelUpdate });

  let extension = ExtensionTestUtils.loadExtension({
    manifest: {
      ...baseManifest,
      version: "1",
    },
    // Use permanent so the add-on can be updated.
    useAddonManager: "permanent",
  });
  await extension.startup();
  return extension;
}

function disableAutoUpdates(card) {
  // Check button should be hidden.
  let updateCheckButton = card.querySelector('button[action="update-check"]');
  ok(updateCheckButton.hidden, "The button is initially hidden");

  // Disable updates, update check button is now visible.
  card.querySelector('input[name="autoupdate"][value="0"]').click();
  ok(!updateCheckButton.hidden, "The button is now visible");

  // There shouldn't be an update shown to the user.
  assertUpdateState({ card, shown: false });
}

function checkForUpdate(card, expected) {
  let updateCheckButton = card.querySelector('button[action="update-check"]');
  let updateFound = BrowserTestUtils.waitForEvent(card, expected);
  updateCheckButton.click();
  return updateFound;
}

function installUpdate(card, expected) {
  // Install the update.
  let updateInstalled = BrowserTestUtils.waitForEvent(card, expected);
  let updated = BrowserTestUtils.waitForEvent(card, "update");
  card.querySelector('panel-item[action="install-update"]').click();
  return Promise.all([updateInstalled, updated]);
}

async function findUpdatesForAddonId(id) {
  let addon = await AddonManager.getAddonByID(id);
  await new Promise(resolve => {
    addon.findUpdates(
      { onUpdateAvailable: resolve },
      AddonManager.UPDATE_WHEN_USER_REQUESTED
    );
  });
}

function assertUpdateState({
  card,
  shown,
  expanded = true,
  releaseNotes = false,
}) {
  let menuButton = card.querySelector(".more-options-button");
  Assert.equal(
    menuButton.classList.contains("more-options-button-badged"),
    shown,
    "The menu button is badged"
  );
  let installButton = card.querySelector('panel-item[action="install-update"]');
  Assert.notEqual(
    installButton.hidden,
    shown,
    `The install button is ${shown ? "hidden" : "shown"}`
  );
  if (expanded) {
    let updateCheckButton = card.querySelector('button[action="update-check"]');
    Assert.equal(
      updateCheckButton.hidden,
      shown,
      `The update check button is ${shown ? "hidden" : "shown"}`
    );

    let { tabGroup } = card.details;
    is(tabGroup.hidden, false, "The tab group is shown");
    let notesBtn = tabGroup.querySelector('[name="release-notes"]');
    is(
      notesBtn.hidden,
      !releaseNotes,
      `The release notes button is ${releaseNotes ? "shown" : "hidden"}`
    );
  }
}

add_task(async function testUpdateAvailable() {
  let id = "update@mochi.test";
  let extension = await setupExtensionWithUpdate(id);

  let win = await loadInitialView("extension");
  let doc = win.document;

  await loadDetailView(win, id);

  let card = doc.querySelector("addon-card");

  // Disable updates and then check.
  disableAutoUpdates(card);
  await checkForUpdate(card, "update-found");

  // There should now be an update.
  assertUpdateState({ card, shown: true });

  // The version was 1.
  let versionRow = card.querySelector(".addon-detail-row-version");
  is(versionRow.lastChild.textContent, "1", "The version started as 1");

  await installUpdate(card, "update-installed");

  // The version is now 2.
  versionRow = card.querySelector(".addon-detail-row-version");
  is(versionRow.lastChild.textContent, "2", "The version has updated");

  // No update is shown again.
  assertUpdateState({ card, shown: false });

  // Check for updates again, there shouldn't be an update.
  await checkForUpdate(card, "no-update");

  await closeView(win);
  await extension.unload();
});

add_task(async function testReleaseNotesLoad() {
  Services.telemetry.clearEvents();
  let id = "update-with-notes@mochi.test";
  let extension = await setupExtensionWithUpdate(id, {
    releaseNotes: `
      <html xmlns="http://www.w3.org/1999/xhtml">
        <head><link rel="stylesheet" href="remove-me.css"/></head>
        <body>
          <script src="no-scripts.js"></script>
          <h1>My release notes</h1>
          <img src="http://example.com/tracker.png"/>
          <ul>
            <li onclick="alert('hi')">A thing</li>
          </ul>
          <a href="http://example.com/">Go somewhere</a>
        </body>
      </html>
    `,
  });

  let win = await loadInitialView("extension");
  let doc = win.document;

  await loadDetailView(win, id);

  let card = doc.querySelector("addon-card");
  let { deck, tabGroup } = card.details;

  // Disable updates and then check.
  disableAutoUpdates(card);
  await checkForUpdate(card, "update-found");

  // There should now be an update.
  assertUpdateState({ card, shown: true, releaseNotes: true });

  info("Check release notes");
  let notesBtn = tabGroup.querySelector('[name="release-notes"]');
  let notes = card.querySelector("update-release-notes");
  let loading = BrowserTestUtils.waitForEvent(notes, "release-notes-loading");
  let loaded = BrowserTestUtils.waitForEvent(notes, "release-notes-loaded");
  // Don't use notesBtn.click() since it causes an assertion to fail.
  // See bug 1551621 for more info.
  EventUtils.synthesizeMouseAtCenter(notesBtn, {}, win);
  await loading;
  is(
    doc.l10n.getAttributes(notes.firstElementChild).id,
    "release-notes-loading",
    "The loading message is shown"
  );
  await loaded;
  info("Checking HTML release notes");
  let [h1, ul, a] = notes.children;
  is(h1.tagName, "H1", "There's a heading");
  is(h1.textContent, "My release notes", "The heading has content");
  is(ul.tagName, "UL", "There's a list");
  is(ul.children.length, 1, "There's one item in the list");
  let [li] = ul.children;
  is(li.tagName, "LI", "There's a list item");
  is(li.textContent, "A thing", "The text is set");
  ok(!li.hasAttribute("onclick"), "The onclick was removed");
  ok(!notes.querySelector("link"), "The link tag was removed");
  ok(!notes.querySelector("script"), "The script tag was removed");
  is(a.textContent, "Go somewhere", "The link text is preserved");
  is(a.href, "http://example.com/", "The link href is preserved");

  info("Verify the link opened in a new tab");
  let tabOpened = BrowserTestUtils.waitForNewTab(gBrowser, a.href);
  a.click();
  let tab = await tabOpened;
  BrowserTestUtils.removeTab(tab);

  let originalContent = notes.innerHTML;

  info("Switch away and back to release notes");
  // Load details view.
  let detailsBtn = tabGroup.querySelector('.tab-button[name="details"]');
  let viewChanged = BrowserTestUtils.waitForEvent(deck, "view-changed");
  detailsBtn.click();
  await viewChanged;

  // Load release notes again, verify they weren't loaded.
  viewChanged = BrowserTestUtils.waitForEvent(deck, "view-changed");
  let notesCached = BrowserTestUtils.waitForEvent(
    notes,
    "release-notes-cached"
  );
  notesBtn.click();
  await viewChanged;
  await notesCached;
  is(notes.innerHTML, originalContent, "The content didn't change");

  info("Install the update to clean it up");
  await installUpdate(card, "update-installed");

  // There's no more update but release notes are still shown.
  assertUpdateState({ card, shown: false, releaseNotes: true });

  await closeView(win);
  await extension.unload();
});

add_task(async function testReleaseNotesError() {
  let id = "update-with-notes-error@mochi.test";
  let extension = await setupExtensionWithUpdate(id, { releaseNotes: "ERROR" });

  let win = await loadInitialView("extension");
  let doc = win.document;

  await loadDetailView(win, id);

  let card = doc.querySelector("addon-card");
  let { deck, tabGroup } = card.details;

  // Disable updates and then check.
  disableAutoUpdates(card);
  await checkForUpdate(card, "update-found");

  // There should now be an update.
  assertUpdateState({ card, shown: true, releaseNotes: true });

  info("Check release notes");
  let notesBtn = tabGroup.querySelector('[name="release-notes"]');
  let notes = card.querySelector("update-release-notes");
  let loading = BrowserTestUtils.waitForEvent(notes, "release-notes-loading");
  let errored = BrowserTestUtils.waitForEvent(notes, "release-notes-error");
  // Don't use notesBtn.click() since it causes an assertion to fail.
  // See bug 1551621 for more info.
  EventUtils.synthesizeMouseAtCenter(notesBtn, {}, win);
  await loading;
  is(
    doc.l10n.getAttributes(notes.firstElementChild).id,
    "release-notes-loading",
    "The loading message is shown"
  );
  await errored;
  is(
    doc.l10n.getAttributes(notes.firstElementChild).id,
    "release-notes-error",
    "The error message is shown"
  );

  info("Switch away and back to release notes");
  // Load details view.
  let detailsBtn = tabGroup.querySelector('.tab-button[name="details"]');
  let viewChanged = BrowserTestUtils.waitForEvent(deck, "view-changed");
  detailsBtn.click();
  await viewChanged;

  // Load release notes again, verify they weren't loaded.
  viewChanged = BrowserTestUtils.waitForEvent(deck, "view-changed");
  let notesCached = BrowserTestUtils.waitForEvent(
    notes,
    "release-notes-cached"
  );
  notesBtn.click();
  await viewChanged;
  await notesCached;

  info("Install the update to clean it up");
  await installUpdate(card, "update-installed");

  await closeView(win);
  await extension.unload();
});

add_task(async function testUpdateCancelled() {
  let id = "update@mochi.test";
  let extension = await setupExtensionWithUpdate(id, { cancelUpdate: true });

  let win = await loadInitialView("extension");
  let doc = win.document;

  await loadDetailView(win, "update@mochi.test");
  let card = doc.querySelector("addon-card");

  // Disable updates and then check.
  disableAutoUpdates(card);
  await checkForUpdate(card, "update-found");

  // There should now be an update.
  assertUpdateState({ card, shown: true });

  // The add-on starts as version 1.
  let versionRow = card.querySelector(".addon-detail-row-version");
  is(versionRow.lastChild.textContent, "1", "The version started as 1");

  // Force the install to be cancelled.
  let install = card.updateInstall;
  ok(install, "There was an install found");

  await installUpdate(card, "update-cancelled");

  // The add-on is still version 1.
  versionRow = card.querySelector(".addon-detail-row-version");
  is(versionRow.lastChild.textContent, "1", "The version hasn't changed");

  // The update has been removed.
  assertUpdateState({ card, shown: false });

  await closeView(win);
  await extension.unload();
});

add_task(async function testAvailableUpdates() {
  let ids = ["update1@mochi.test", "update2@mochi.test", "update3@mochi.test"];
  let addons = await Promise.all(ids.map(id => setupExtensionWithUpdate(id)));

  // Disable global add-on updates.
  AddonManager.autoUpdateDefault = false;

  let win = await loadInitialView("extension");
  let doc = win.document;
  let updatesMessage = doc.getElementById("updates-message");
  let categoryUtils = new CategoryUtilities(win);

  let availableCat = categoryUtils.get("available-updates");

  ok(availableCat.hidden, "Available updates is hidden");
  is(availableCat.badgeCount, 0, "There are no updates");
  ok(updatesMessage, "There is an updates message");
  is_element_hidden(updatesMessage, "The message is hidden");
  ok(!updatesMessage.message.textContent, "The message is empty");
  ok(!updatesMessage.button.textContent, "The button is empty");

  // Check for all updates.
  let updatesFound = TestUtils.topicObserved("EM-update-check-finished");
  doc.querySelector('#page-options [action="check-for-updates"]').click();

  is_element_visible(updatesMessage, "The message is visible");
  ok(!updatesMessage.message.textContent, "The message is empty");
  ok(updatesMessage.button.hidden, "The view updates button is hidden");

  // Make sure the message gets populated by fluent.
  await TestUtils.waitForCondition(
    () => updatesMessage.message.textContent,
    "wait for message text"
  );

  await updatesFound;

  // The button should be visible, and should get some text from fluent.
  ok(!updatesMessage.button.hidden, "The view updates button is visible");
  await TestUtils.waitForCondition(
    () => updatesMessage.button.textContent,
    "wait for button text"
  );

  // Wait for the available updates count to finalize, it's async.
  await BrowserTestUtils.waitForCondition(() => availableCat.badgeCount == 3);

  // The category shows the correct update count.
  ok(!availableCat.hidden, "Available updates is visible");
  is(availableCat.badgeCount, 3, "There are 3 updates");

  // Go to the available updates page.
  let loaded = waitForViewLoad(win);
  availableCat.click();
  await loaded;

  // Check the updates are shown.
  let cards = doc.querySelectorAll("addon-card");
  is(cards.length, 3, "There are 3 cards");

  // Each card should have an update.
  for (let card of cards) {
    assertUpdateState({ card, shown: true, expanded: false });
  }

  // Check the detail page for the first add-on.
  await loadDetailView(win, ids[0]);
  is(
    categoryUtils.getSelectedViewId(),
    "addons://list/extension",
    "The extensions category is selected"
  );

  // Go back to the last view.
  loaded = waitForViewLoad(win);
  doc.querySelector(".back-button").click();
  await loaded;

  // We're back on the updates view.
  is(
    categoryUtils.getSelectedViewId(),
    "addons://updates/available",
    "The available updates category is selected"
  );

  // Find the cards again.
  cards = doc.querySelectorAll("addon-card");
  is(cards.length, 3, "There are 3 cards");

  // Install the first update.
  await installUpdate(cards[0], "update-installed");
  assertUpdateState({ card: cards[0], shown: false, expanded: false });

  // The count goes down but the card stays.
  is(availableCat.badgeCount, 2, "There are only 2 updates now");
  is(
    doc.querySelectorAll("addon-card").length,
    3,
    "All 3 cards are still visible on the updates page"
  );

  // Install the other two updates.
  await installUpdate(cards[1], "update-installed");
  assertUpdateState({ card: cards[1], shown: false, expanded: false });
  await installUpdate(cards[2], "update-installed");
  assertUpdateState({ card: cards[2], shown: false, expanded: false });

  // The count goes down but the card stays.
  is(availableCat.badgeCount, 0, "There are no more updates");
  is(
    doc.querySelectorAll("addon-card").length,
    3,
    "All 3 cards are still visible on the updates page"
  );

  // Enable global add-on updates again.
  AddonManager.autoUpdateDefault = true;

  await closeView(win);
  await Promise.all(addons.map(addon => addon.unload()));
});

add_task(async function testUpdatesShownOnLoad() {
  let id = "has-update@mochi.test";
  let addon = await setupExtensionWithUpdate(id);

  // Find the update for our addon.
  AddonManager.autoUpdateDefault = false;
  await findUpdatesForAddonId(id);

  let win = await loadInitialView("extension");
  let categoryUtils = new CategoryUtilities(win);
  let updatesButton = categoryUtils.get("available-updates");

  ok(!updatesButton.hidden, "The updates button is shown");
  is(updatesButton.badgeCount, 1, "There is an update");

  let loaded = waitForViewLoad(win);
  updatesButton.click();
  await loaded;

  let cards = win.document.querySelectorAll("addon-card");

  is(cards.length, 1, "There is one update card");

  let card = cards[0];
  is(card.addon.id, id, "The update is for the expected add-on");

  await installUpdate(card, "update-installed");

  ok(!updatesButton.hidden, "The updates button is still shown");
  is(updatesButton.badgeCount, 0, "There are no more updates");

  info("Check that the updates section is hidden when re-opened");
  await closeView(win);
  win = await loadInitialView("extension");
  categoryUtils = new CategoryUtilities(win);
  updatesButton = categoryUtils.get("available-updates");

  ok(updatesButton.hidden, "Available updates is hidden");
  is(updatesButton.badgeCount, 0, "There are no updates");

  AddonManager.autoUpdateDefault = true;
  await closeView(win);
  await addon.unload();
});

add_task(async function testPromptOnBackgroundUpdateCheck() {
  const id = "test-prompt-on-background-check@mochi.test";
  const extension = await setupExtensionWithUpdate(id);

  AddonManager.autoUpdateDefault = false;

  const addon = await AddonManager.getAddonByID(id);
  await AddonTestUtils.promiseFindAddonUpdates(
    addon,
    AddonManager.UPDATE_WHEN_PERIODIC_UPDATE
  );
  let win = await loadInitialView("extension");

  let card = getAddonCard(win, id);

  const popupPromise = promisePopupNotificationShown(
    "addon-webext-permissions"
  );
  const promisePromptInfo = promisePermissionPrompt(id);
  await installUpdate(card, "update-installed");
  const promptInfo = await promisePromptInfo;
  ok(promptInfo, "Got a permission prompt as expected");
  let panel = await popupPromise;
  ok(
    !panel.querySelector(".webext-perm-privatebrowsing checkbox"),
    "Expect no incognito checkbox in update prompt"
  );

  AddonManager.autoUpdateDefault = true;

  await closeView(win);
  await extension.unload();
});

add_task(async function testNoUpdateAvailableOnUnrelatedAddonCards() {
  let idNoUpdate = "no-update@mochi.test";

  let extensionNoUpdate = ExtensionTestUtils.loadExtension({
    useAddonManager: "temporary",
    manifest: {
      name: "TestAddonNoUpdate",
      browser_specific_settings: { gecko: { id: idNoUpdate } },
    },
  });
  await extensionNoUpdate.startup();

  let win = await loadInitialView("extension");

  let cardNoUpdate = getAddonCard(win, idNoUpdate);
  ok(cardNoUpdate, `Got AddonCard for ${idNoUpdate}`);

  // Assert that there is not an update badge
  assertUpdateState({ card: cardNoUpdate, shown: false, expanded: false });

  // Trigger a onNewInstall event by install another unrelated addon.
  const XPI_URL = `${SECURE_TESTROOT}../xpinstall/amosigned.xpi`;
  let install = await AddonManager.getInstallForURL(XPI_URL);
  await AddonManager.installAddonFromAOM(
    gBrowser.selectedBrowser,
    win.document.documentURIObject,
    install
  );

  // Cancel the install used to trigger the onNewInstall install event.
  await install.cancel();
  // Assert that the previously installed addon isn't marked with the
  // update available badge after installing an unrelated addon.
  assertUpdateState({ card: cardNoUpdate, shown: false, expanded: false });

  await closeView(win);
  await extensionNoUpdate.unload();
});

add_task(async function testNoPromptForPolicy() {
  const id = "test-noprompt-for-policy@mochi.test";
  await setupExtensionWithUpdate(id, { policy: true });

  AddonManager.autoUpdateDefault = false;

  let addon = await AddonManager.getAddonByID(id);
  is(addon.version, "1", "Version is 1 to start");
  let policy = WebExtensionPolicy.getByID(addon.id);
  Assert.deepEqual(policy.permissions, [], "Expected no permissions");

  await AddonTestUtils.promiseFindAddonUpdates(
    addon,
    AddonManager.UPDATE_WHEN_PERIODIC_UPDATE
  );

  let win = await loadInitialView("extension");
  let card = getAddonCard(win, id);
  await installUpdate(card, "update-installed");

  addon = await AddonManager.getAddonByID(id);
  is(addon.version, "2", "Version is 2 after update");
  policy = WebExtensionPolicy.getByID(addon.id);
  Assert.deepEqual(policy.permissions, ["bookmarks"], "Expected permissions");
  Assert.ok(
    policy.allowedOrigins.matches("http://foo.example.com/bar"),
    "Expected origins"
  );

  AddonManager.autoUpdateDefault = true;

  await closeView(win);
  await addon.uninstall();
});
