/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

const { LocalModeMappings } = ChromeUtils.importESModule(
  "resource://devtools/client/framework/LocalModeMappings.sys.mjs"
);
const { LOCAL_MODE_MAPPINGS_PREF_PREFIX } = LocalModeMappings;
const TEST_FOLDER = PathUtils.join(
  PathUtils.tempDir,
  "devtools-local-mode-" + Services.uuid.generateUUID()
);
const MISSING_FOLDER = PathUtils.join(TEST_FOLDER, "missing");
const MockFilePicker = SpecialPowers.MockFilePicker;

MockFilePicker.init(window.browsingContext);
registerCleanupFunction(async () => {
  MockFilePicker.cleanup();
  clearMappings();
  await IOUtils.remove(TEST_FOLDER, { recursive: true });
});

add_setup(async function () {
  await IOUtils.makeDirectory(TEST_FOLDER);
});

add_task(test_local_mode_mappings_listed);
add_task(test_local_mode_mappings_refresh);
add_task(test_local_mode_remote_notice);
add_task(test_local_mode_add_mapping);
add_task(test_local_mode_add_mapping_cancelled);
add_task(test_local_mode_edit_origin);
add_task(test_local_mode_edit_origin_refresh);
add_task(test_local_mode_change_folder);
add_task(test_local_mode_navigate);
add_task(test_local_mode_toggle);
add_task(test_local_mode_remove);

async function test_local_mode_mappings_listed() {
  setMapping(0, { origin: "firefox.localhost", path: TEST_FOLDER });
  setMapping(1, {
    origin: "firefox1.localhost",
    path: MISSING_FOLDER,
    disabled: true,
  });
  const { root } = await openSettings();
  const editor = await getLocalModeEditor(root);

  const [first, second] = getMappingItems(editor);
  is(
    first.querySelector(".origin").value,
    "firefox.localhost",
    "The first mapping shows its origin"
  );
  is(
    first.querySelector(".folder").textContent,
    TEST_FOLDER,
    "The first mapping shows its folder"
  );
  is(first.querySelector(".folder").localName, "a", "The folder is a link");
  ok(
    !first.querySelector(".origin").disabled,
    "An enabled mapping's origin can be edited"
  );
  ok(
    !first.querySelector(".folder-error"),
    "An existing folder shows no error"
  );
  ok(
    second.querySelector(".origin").disabled,
    "A disabled mapping's origin can't be edited"
  );
  ok(second.querySelector(".folder-error"), "A missing folder shows an error");
  ok(
    !editor.shadowRoot.querySelector("moz-message-bar"),
    "There's no remote debugging notice for a local tab"
  );

  await closeTabAndToolbox();
  clearMappings();
}

async function test_local_mode_mappings_refresh() {
  const { root } = await openSettings();
  const editor = await getLocalModeEditor(root);
  is(getMappingItems(editor).length, 0, "There are no mappings");

  LocalModeMappings.createNewMapping("firefox.localhost", TEST_FOLDER);
  await waitFor(
    () => getMappingItems(editor).length == 1,
    "Creating a mapping lists it"
  );

  clearMappings();
  await waitFor(
    () => !getMappingItems(editor).length,
    "Removing a mapping removes it"
  );

  await closeTabAndToolbox();
}

async function test_local_mode_remote_notice() {
  await pushPref("devtools.settings.redesign-enabled", true);
  const tab = await addTab("about:blank");
  const commands = await CommandsFactory.forRemoteTab(
    tab.linkedBrowser.browserId
  );
  const toolbox = await gDevTools.showToolbox(commands, {
    toolId: "options",
    hostType: "window",
  });
  ok(
    !commands.descriptorFront.isLocalTab,
    "The toolbox uses a remote descriptor"
  );
  const root = toolbox.getCurrentPanel().element;
  await root.updateComplete;
  const editor = await getLocalModeEditor(root);
  const notice = editor.shadowRoot.querySelector("moz-message-bar");
  ok(notice, "A remote tab shows the Local Mode notice");

  await toolbox.destroy();
  await closeTabAndToolbox(tab);
}

async function test_local_mode_add_mapping() {
  Services.fog.testResetFOG();
  const { root } = await openSettings();
  const editor = await getLocalModeEditor(root);
  MockFilePicker.setFiles([new FileUtils.File(TEST_FOLDER)]);
  MockFilePicker.returnValue = MockFilePicker.returnOK;

  const onMappingsUpdated = LocalModeMappings.once("updated");
  getAddButton(editor).click();
  await waitFor(
    () => getMappingItems(editor).length == 1,
    "Adding a mapping lists it"
  );
  const [item] = getMappingItems(editor);
  const origin = item.querySelector(".origin");
  is(origin.value, "firefox.localhost", "The mapping uses the first origin");
  is(
    item.querySelector(".folder").textContent,
    TEST_FOLDER,
    "The mapping uses the chosen folder"
  );
  await waitFor(
    () => editor.shadowRoot.activeElement == origin,
    "The new mapping's origin is focused"
  );
  is(
    Glean.devtoolsLocalmodeMappings.addPermanentFromOptions.testGetValue(),
    1,
    "Adding a mapping is counted"
  );

  getAddButton(editor).click();
  await waitFor(
    () => getMappingItems(editor).length == 2,
    "Adding another mapping lists it"
  );
  is(
    getMappingItems(editor)[1].querySelector(".origin").value,
    "firefox1.localhost",
    "The second mapping uses the next origin"
  );

  await onMappingsUpdated;
  await closeTabAndToolbox();
  clearMappings();
}

async function test_local_mode_add_mapping_cancelled() {
  Services.fog.testResetFOG();
  const { root } = await openSettings();
  const editor = await getLocalModeEditor(root);
  MockFilePicker.returnValue = MockFilePicker.returnCancel;
  const onPickerShown = new Promise(resolve => {
    MockFilePicker.showCallback = resolve;
  });

  getAddButton(editor).click();
  await onPickerShown;
  await waitForTick();
  is(
    LocalModeMappings.getAllMappings().length,
    0,
    "Cancelling the folder picker adds no mapping"
  );
  is(
    Glean.devtoolsLocalmodeMappings.addPermanentFromOptions.testGetValue(),
    null,
    "Cancelling the folder picker isn't counted"
  );

  MockFilePicker.showCallback = null;
  await closeTabAndToolbox();
}

async function test_local_mode_edit_origin() {
  setMapping(0, { origin: "firefox.localhost", path: TEST_FOLDER });
  setMapping(1, { origin: "firefox1.localhost", path: TEST_FOLDER });
  const { root } = await openSettings();
  const editor = await getLocalModeEditor(root);
  const item = getMappingItems(editor)[1];
  const origin = item.querySelector(".origin");
  const originPref = `${LOCAL_MODE_MAPPINGS_PREF_PREFIX}1.origin`;

  typeOrigin(origin, "firefox.localhost");
  await editor.updateComplete;
  is(
    item.querySelector(".origin-error")?.dataset.l10nId,
    "devtools-settings-local-mode-origin-conflict",
    "An origin used by another mapping shows an error"
  );

  typeOrigin(origin, "not a host");
  await editor.updateComplete;
  is(
    item.querySelector(".origin-error")?.dataset.l10nId,
    "devtools-settings-local-mode-origin-invalid",
    "An invalid origin shows an error"
  );

  EventUtils.synthesizeKey("KEY_Enter", {}, origin.ownerGlobal);
  await editor.updateComplete;
  is(origin.value, "firefox1.localhost", "An invalid origin is reverted");
  is(
    Services.prefs.getStringPref(originPref),
    "firefox1.localhost",
    "An invalid origin isn't written"
  );

  const onUpdated = LocalModeMappings.once("updated");
  typeOrigin(origin, "example.localhost");
  await editor.updateComplete;
  EventUtils.synthesizeKey("KEY_Enter", {}, origin.ownerGlobal);
  is(
    Services.prefs.getStringPref(originPref),
    "example.localhost",
    "A valid origin is written"
  );
  await onUpdated;

  await closeTabAndToolbox();
  clearMappings();
}

async function test_local_mode_edit_origin_refresh() {
  setMapping(0, { origin: "firefox.localhost", path: TEST_FOLDER });
  setMapping(1, { origin: "firefox1.localhost", path: TEST_FOLDER });
  const { root } = await openSettings();
  const editor = await getLocalModeEditor(root);
  const item = getMappingItems(editor)[1];
  const origin = item.querySelector(".origin");
  const firstOriginPref = `${LOCAL_MODE_MAPPINGS_PREF_PREFIX}0.origin`;

  typeOrigin(origin, "firefox.localhost");
  await editor.updateComplete;
  ok(
    item.querySelector(".origin-error"),
    "The draft conflicts with another mapping"
  );

  const onUpdated = LocalModeMappings.once("updated");
  Services.prefs.setStringPref(firstOriginPref, "example.localhost");
  await onUpdated;
  await editor.updateComplete;
  is(
    origin.value,
    "firefox1.localhost",
    "Refreshing restores the saved origin"
  );
  ok(
    !item.querySelector(".origin-error"),
    "Refreshing clears the draft's error"
  );

  await closeTabAndToolbox();
  clearMappings();
}

async function test_local_mode_change_folder() {
  setMapping(0, { origin: "firefox.localhost", path: MISSING_FOLDER });
  const { root } = await openSettings();
  const editor = await getLocalModeEditor(root);
  const [item] = getMappingItems(editor);

  MockFilePicker.setFiles([new FileUtils.File(TEST_FOLDER)]);
  MockFilePicker.returnValue = MockFilePicker.returnOK;
  const onUpdated = LocalModeMappings.once("updated");
  item.querySelector(".choose-folder").click();
  await onUpdated;
  is(
    Services.prefs.getStringPref(`${LOCAL_MODE_MAPPINGS_PREF_PREFIX}0.path`),
    TEST_FOLDER,
    "Choosing a folder writes its path"
  );
  await editor.updateComplete;
  ok(
    !item.querySelector(".folder-error"),
    "An existing folder hides the error"
  );

  await closeTabAndToolbox();
  clearMappings();
}

async function test_local_mode_navigate() {
  setMapping(0, { origin: "firefox.localhost", path: TEST_FOLDER });
  const { toolbox, root } = await openSettings();
  const editor = await getLocalModeEditor(root);
  const [item] = getMappingItems(editor);

  const waitForDevToolsReload = await watchForDevToolsReload(
    gBrowser.selectedBrowser
  );
  item.querySelector(".navigate").click();
  await waitForDevToolsReload();
  await toolbox.commands.client.waitForRequestsToSettle();
  is(
    gBrowser.selectedBrowser.currentURI.spec,
    "https://firefox.localhost/",
    "Opening a mapping navigates to its origin"
  );

  const onUpdated = LocalModeMappings.once("updated");
  Services.prefs.setBoolPref(
    `${LOCAL_MODE_MAPPINGS_PREF_PREFIX}0.disabled`,
    true
  );
  await onUpdated;
  await editor.updateComplete;
  ok(
    item.querySelector(".navigate").disabled,
    "A disabled mapping can't be navigated to"
  );

  await closeTabAndToolbox();
  clearMappings();
}

async function test_local_mode_toggle() {
  setMapping(0, { origin: "firefox.localhost", path: TEST_FOLDER });
  const { root } = await openSettings();
  const editor = await getLocalModeEditor(root);
  const [item] = getMappingItems(editor);
  const disabledPref = `${LOCAL_MODE_MAPPINGS_PREF_PREFIX}0.disabled`;

  let onUpdated = LocalModeMappings.once("updated");
  item.querySelector(".toggle").click();
  await onUpdated;
  ok(Services.prefs.getBoolPref(disabledPref), "Disabling writes the pref");
  await editor.updateComplete;
  ok(item.querySelector(".origin").disabled, "A disabled mapping is disabled");
  is(
    item.querySelector(".toggle").dataset.l10nId,
    "devtools-settings-local-mode-enable",
    "A disabled mapping can be enabled"
  );

  onUpdated = LocalModeMappings.once("updated");
  item.querySelector(".toggle").click();
  await onUpdated;
  ok(!Services.prefs.getBoolPref(disabledPref), "Enabling writes the pref");
  await editor.updateComplete;
  ok(
    !item.querySelector(".origin").disabled,
    "An enabled mapping is not disabled"
  );

  await closeTabAndToolbox();
  clearMappings();
}

async function test_local_mode_remove() {
  setMapping(0, { origin: "firefox.localhost", path: TEST_FOLDER });
  const { root } = await openSettings();
  const editor = await getLocalModeEditor(root);
  const [item] = getMappingItems(editor);
  const originPref = `${LOCAL_MODE_MAPPINGS_PREF_PREFIX}0.origin`;

  const onCancelled = BrowserTestUtils.promiseAlertDialogOpen("cancel");
  item.querySelector(".remove").click();
  await onCancelled;
  await waitForTick();
  ok(
    Services.prefs.prefHasUserValue(originPref),
    "Cancelling the confirmation keeps the mapping"
  );

  const onUpdated = LocalModeMappings.once("updated");
  const onAccepted = BrowserTestUtils.promiseAlertDialogOpen("accept");
  item.querySelector(".remove").click();
  await onAccepted;
  await onUpdated;
  ok(
    !Services.prefs.getChildList(LOCAL_MODE_MAPPINGS_PREF_PREFIX).length,
    "Accepting the confirmation removes the mapping's prefs"
  );
  await waitFor(
    () => !getMappingItems(editor).length,
    "Removing a mapping removes it from the list"
  );

  await closeTabAndToolbox();
}

/**
 * Type in a mapping's origin field.
 *
 * @param {MozInputText} input The mapping's origin input.
 * @param {string} value The hostname to type.
 */
function typeOrigin(input, value) {
  input.focus();
  input.inputEl.select();
  EventUtils.sendString(value, input.ownerGlobal);
}

/**
 * Get the add-mapping button from the Local Mode editor.
 *
 * @param {DevToolsSettingsLocalMode} editor The Local Mode editor.
 * @returns {MozButton} The button that opens the folder picker.
 */
function getAddButton(editor) {
  return editor.shadowRoot.querySelector(
    "moz-button[data-l10n-id=devtools-settings-local-mode-add]"
  );
}

/**
 * Wait for the Local Mode editor in the settings panel to finish rendering.
 *
 * @param {DevToolsSettings} root The settings panel's root element.
 * @returns {Promise<DevToolsSettingsLocalMode>} The rendered mappings editor.
 */
async function getLocalModeEditor(root) {
  const editor = root.shadowRoot.querySelector("#settings-local-mode-mappings");
  await editor.updateComplete;
  return editor;
}

/**
 * Return the rendered mapping rows in display order.
 *
 * @param {DevToolsSettingsLocalMode} editor The Local Mode editor.
 * @returns {HTMLLIElement[]} The mapping rows.
 */
function getMappingItems(editor) {
  return [...editor.shadowRoot.querySelectorAll("li")];
}

/**
 * Seed a mapping's preferences without using the settings UI.
 *
 * @param {number} index The mapping's preference index.
 * @param {object} mapping The mapping values to store.
 * @param {string} mapping.origin The mapped hostname.
 * @param {string} mapping.path The local folder path.
 * @param {boolean} [mapping.disabled] Whether the mapping starts disabled.
 */
function setMapping(index, { origin, path, disabled = false }) {
  const prefPrefix = `${LOCAL_MODE_MAPPINGS_PREF_PREFIX}${index}.`;
  Services.prefs.setStringPref(prefPrefix + "origin", origin);
  Services.prefs.setStringPref(prefPrefix + "path", path);
  Services.prefs.setBoolPref(prefPrefix + "disabled", disabled);
}

/** Remove all Local Mode mappings. */
function clearMappings() {
  Services.prefs.getBranch(LOCAL_MODE_MAPPINGS_PREF_PREFIX).deleteBranch("");
}
