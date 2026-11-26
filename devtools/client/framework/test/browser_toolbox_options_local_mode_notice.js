/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/ */

// Test the notifications show for Local Mode mappings

const TEST_FILE_URI = getSupportsFile("./local-mode/");
const TEST_URI = TEST_FILE_URI.spec;

const { LocalModeMappings } = ChromeUtils.importESModule(
  "resource://devtools/client/framework/LocalModeMappings.sys.mjs"
);

const { PromiseTestUtils } = ChromeUtils.importESModule(
  "resource://testing-common/PromiseTestUtils.sys.mjs"
);
// Some navigateTo commands never resolve due to missing onStateChange updates.
PromiseTestUtils.allowMatchingRejectionsGlobally(/Connection closed/);

add_task(async function addToSettings() {
  ok(TEST_URI.startsWith("file://"), "The test URL is a file:// URL");

  const tab = await addTab(TEST_URI);

  const toolbox = await gDevTools.showToolboxForTab(tab, {
    toolId: "webconsole",
  });
  const doc = toolbox.doc;
  const notificationBox = await waitFor(() =>
    doc.querySelector(".notificationbox")
  );

  let buttons = notificationBox.querySelectorAll(".notificationButton");
  is(buttons.length, 3);

  info("Click on the Add to Settings button");
  let waitForDevToolsReload = await watchForDevToolsReload(
    gBrowser.selectedBrowser
  );
  let onMappingsUpdated = LocalModeMappings.once("updated");
  buttons[0].click();

  info("Wait for mappings to be updated");
  await onMappingsUpdated;
  info("Wait for the navigation to the mapping URL");
  await waitForDevToolsReload();
  is(
    gBrowser.selectedBrowser.currentURI.spec,
    "https://firefox.localhost/",
    "We navigated to the mapping URL"
  );

  info("Wait for the options panel to be opened");
  await toolbox.getPanelWhenReady("options");

  const panel = toolbox.getCurrentPanel();
  const { panelDoc } = panel;

  const originInput = [
    ...panelDoc.querySelectorAll(`.local-mode-origin-line input`),
  ].at(-1);
  is(originInput.value, "firefox.localhost", "The origin is correct");

  const folderLink = [
    ...panelDoc.querySelectorAll(`.local-mode-folder-line a`),
  ].at(-1);
  is(
    folderLink.textContent,
    TEST_FILE_URI.file.path,
    "The folder path looks correct"
  );

  info(
    "Navigate to the file:// URL again to see the navigation about the existing mapping"
  );
  await navigateTo(TEST_URI);
  // Spin the event loop for a11y reason and click on the notification bar once it is visible
  await wait(0);
  buttons = notificationBox.querySelectorAll(".notificationButton");
  is(buttons.length, 2);
  info("Click on the 'navigate to' button");
  waitForDevToolsReload = await watchForDevToolsReload(
    gBrowser.selectedBrowser
  );
  buttons[0].click();
  info("Wait for the navigation to the mapping URL");
  await waitForDevToolsReload();
  is(
    gBrowser.selectedBrowser.currentURI.spec,
    "https://firefox.localhost/",
    "We navigated to the mapping URL"
  );

  info("Navigate to the file:// URL one more time");
  await navigateTo(TEST_URI);
  await wait(0);
  buttons = notificationBox.querySelectorAll(".notificationButton");
  ok(
    doc.querySelector(`.notification[data-key="local-mode-notice"]`),
    "The notification is visible"
  );
  info("Click on the 'always hide' button");
  buttons[1].click();
  ok(
    !doc.querySelector(`.notification[data-key="local-mode-notice"]`),
    "The notification should be hidden"
  );

  info(
    "Navigate one last time to the file:// URL to see the lack of notification"
  );
  await navigateTo(TEST_URI);
  ok(
    !doc.querySelector(`.notification[data-key="local-mode-notice"]`),
    "The notification isn't shown"
  );

  info("Reset the noticed pref so that following test can keep showing them");
  Services.prefs.setBoolPref("devtools.local-mode.noticed", false);

  info("Remove the mapping before switching to another test");
  const removeButton = panelDoc.querySelector(".local-mode-mapping-remove");
  onMappingsUpdated = LocalModeMappings.once("updated");
  const onConfirmAccepted = BrowserTestUtils.promiseAlertDialogOpen("accept");
  removeButton.click();
  await onConfirmAccepted;
  await onMappingsUpdated;
});

add_task(async function tryItWithFolder() {
  const tab = await addTab(TEST_URI);

  const toolbox = await gDevTools.showToolboxForTab(tab, {
    toolId: "webconsole",
  });
  const doc = toolbox.doc;
  const notificationBox = await waitFor(() =>
    doc.querySelector(".notificationbox")
  );

  const buttons = notificationBox.querySelectorAll(".notificationButton");
  is(buttons.length, 3);

  info("Click on the Try It button");
  const waitForDevToolsReload = await watchForDevToolsReload(
    gBrowser.selectedBrowser
  );
  is(
    gBrowser.selectedBrowser.currentURI.spec,
    TEST_URI,
    "Before trying the mapping, the tab's URL is the file URL"
  );
  buttons[1].click();
  info("Wait for the navigation to the mapping URL");
  await waitForDevToolsReload();
  is(
    gBrowser.selectedBrowser.currentURI.spec,
    "https://firefox.localhost/",
    "We navigated to the transient mapping URL"
  );
});

add_task(async function tryItWithFile() {
  const fileUri = TEST_URI + "index.html";
  const tab = await addTab(fileUri);

  const toolbox = await gDevTools.showToolboxForTab(tab, {
    toolId: "webconsole",
  });
  const doc = toolbox.doc;
  const notificationBox = await waitFor(() =>
    doc.querySelector(".notificationbox")
  );

  let buttons = notificationBox.querySelectorAll(".notificationButton");
  is(buttons.length, 3);

  info("Click on the Try It button");
  let waitForDevToolsReload = await watchForDevToolsReload(
    gBrowser.selectedBrowser
  );
  // Note that we can't assert the tab's URI as the file URI may be against a symlink
  // that is resolved when navigating to the HTML page ans so be different from `fileUri`.
  buttons[1].click();
  info("Wait for the navigation to the transient mapping URL");
  await waitForDevToolsReload();
  is(
    gBrowser.selectedBrowser.currentURI.spec,
    "https://firefox1.localhost/index.html",
    "We navigated to the transient mapping URL"
  );

  info("Navigate to another folder and register a second transient mapping");
  await navigateTo(TEST_URI + "folder/");
  await wait(0);
  buttons = notificationBox.querySelectorAll(".notificationButton");
  waitForDevToolsReload = await watchForDevToolsReload(
    gBrowser.selectedBrowser
  );
  buttons[1].click();
  info("Wait for the navigation to the second transient mapping URL");
  await waitForDevToolsReload();
  is(
    gBrowser.selectedBrowser.currentURI.spec,
    "https://firefox2.localhost/",
    "We navigated to the second transient mapping URL"
  );

  info(
    "Assert that we can navigate back to the first transient mapping and it works fine"
  );
  await navigateTo("https://firefox1.localhost/");
  await SpecialPowers.spawn(gBrowser.selectedBrowser, [], function () {
    is(
      content.document.documentElement.textContent,
      "Local mode HTML test page\n",
      "The opened page is the local html page"
    );
  });

  LocalModeMappings.clearTransientMappings();
});

function getSupportsFile(path) {
  const cr = Cc["@mozilla.org/chrome/chrome-registry;1"].getService(
    Ci.nsIChromeRegistry
  );
  const uri = Services.io.newURI(CHROME_URL_ROOT + path);
  const fileurl = cr.convertChromeURL(uri);
  return fileurl.QueryInterface(Ci.nsIFileURL);
}
