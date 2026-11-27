/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

const { LocalModeMappings } = ChromeUtils.importESModule(
  "resource://devtools/client/framework/LocalModeMappings.sys.mjs"
);
const { PromiseTestUtils } = ChromeUtils.importESModule(
  "resource://testing-common/PromiseTestUtils.sys.mjs"
);
// Some navigateTo commands never resolve due to missing onStateChange updates
// (see intermittent bug 2050460).
PromiseTestUtils.allowMatchingRejectionsGlobally(/Connection closed/);

const TEST_FOLDER = PathUtils.join(
  PathUtils.tempDir,
  "devtools-local-mode-" + Services.uuid.generateUUID()
);

registerCleanupFunction(async () => {
  Services.prefs
    .getBranch(LocalModeMappings.LOCAL_MODE_MAPPINGS_PREF_PREFIX)
    .deleteBranch("");
  await IOUtils.remove(TEST_FOLDER, { recursive: true });
});

add_setup(async function () {
  await IOUtils.makeDirectory(TEST_FOLDER);
  await IOUtils.writeUTF8(
    PathUtils.join(TEST_FOLDER, "index.html"),
    "<!doctype html><title>Local Mode test</title>"
  );
});

add_task(test_add_mapping_from_notice);

async function test_add_mapping_from_notice() {
  await pushPref("devtools.settings.redesign-enabled", true);
  const tab = await addTab(PathUtils.toFileURI(TEST_FOLDER));
  const toolbox = await gDevTools.showToolboxForTab(tab, {
    toolId: "webconsole",
  });
  const notificationBox = await waitFor(() =>
    toolbox.doc.querySelector(".notificationbox")
  );
  const [addButton] = notificationBox.querySelectorAll(".notificationButton");

  const waitForDevToolsReload = await watchForDevToolsReload(
    gBrowser.selectedBrowser
  );
  addButton.click();
  await waitForDevToolsReload();
  is(
    gBrowser.selectedBrowser.currentURI.spec,
    "https://firefox.localhost/",
    "Adding the mapping navigates to it"
  );

  const root = toolbox.getCurrentPanel().element;
  const editor = root.shadowRoot.querySelector("#settings-local-mode-mappings");
  const item = editor.shadowRoot.querySelector("li");
  is(
    item.querySelector(".origin").value,
    "firefox.localhost",
    "The settings list the new mapping"
  );
  await waitFor(
    () => item.classList.contains("highlighted"),
    "The new mapping is highlighted"
  );

  await closeTabAndToolbox();
}
