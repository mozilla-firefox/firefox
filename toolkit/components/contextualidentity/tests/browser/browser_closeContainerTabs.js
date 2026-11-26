/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

const { PromptTestUtils } = ChromeUtils.importESModule(
  "resource://testing-common/PromptTestUtils.sys.mjs"
);

const BUILDER_URL = "https://example.com/document-builder.sjs?html=";
const PAGE_MARKUP = `
<html>
<head>
  <script>
    window.onbeforeunload = function() {
      return true;
    };
  </script>
</head>
<body>TEST PAGE</body>
</html>
`;
const TEST_URL = BUILDER_URL + encodeURI(PAGE_MARKUP);

add_task(async function test_skipPermitUnload() {
  await SpecialPowers.pushPrefEnv({
    set: [["dom.require_user_interaction_for_beforeunload", false]],
  });

  info("Create a test user context");
  const userContextId = ContextualIdentityService.create("test").userContextId;
  registerCleanupFunction(() =>
    ContextualIdentityService.remove(userContextId)
  );

  // Run tests in a new window to avoid affecting the main test window.
  const win = await BrowserTestUtils.openNewBrowserWindow();
  registerCleanupFunction(async () => {
    await BrowserTestUtils.closeWindow(win);
  });

  info("Create a tab owned by the test user context");
  const tab = await BrowserTestUtils.addTab(win.gBrowser, TEST_URL, {
    userContextId,
  });
  registerCleanupFunction(() => win.gBrowser.removeTab(tab));
  await BrowserTestUtils.browserLoaded(tab.linkedBrowser);

  info("Call closeContainerTabs without skipPermitUnload");
  const unloadDialogPromise = PromptTestUtils.handleNextPrompt(
    tab.linkedBrowser,
    {
      modalType: Ci.nsIPrompt.MODAL_TYPE_CONTENT,
      promptType: "confirmEx",
    },
    // Click the cancel button.
    { buttonNumClick: 1 }
  );

  const tabCountBeforeClose = win.gBrowser.tabs.length;
  ContextualIdentityService.closeContainerTabs(userContextId);

  info("Wait for the unload dialog");
  await unloadDialogPromise;
  is(
    win.gBrowser.tabs.length,
    tabCountBeforeClose,
    "Tab was not closed because of the before unload prompt"
  );

  info("Call closeContainerTabs with skipPermitUnload");
  const closePromise = BrowserTestUtils.waitForTabClosing(tab);
  ContextualIdentityService.closeContainerTabs(userContextId, {
    skipPermitUnload: true,
  });

  info("Wait for the tab to be closed");
  await closePromise;
  is(
    win.gBrowser.tabs.length,
    tabCountBeforeClose - 1,
    "Tab was closed after ignoring the before unload prompt"
  );
});
