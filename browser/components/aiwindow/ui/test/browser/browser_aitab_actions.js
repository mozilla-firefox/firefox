/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

/* import-globals-from head_aitab.js */
Services.scriptloader.loadSubScript(
  getRootDirectory(gTestPath) + "head_aitab.js",
  this
);

// Drives delete the way a reader does: click the trash button, confirm in the
// dialog, and check that the page and its conversation are actually gone from
// both databases and that the tab is sent home. The store-level tests in
// test_AITabDelete.js cover the SQL; this covers the wiring between the
// component, the actor pair and the two stores.
//
// Everything runs in a real Smart Window opened with openAIWindow() from
// head.js. Setting browser.smartwindow.enabled on an ordinary window is not
// enough: the tab reconciliation swaps the tab out from under a running
// content task, and the home page load hands off to about:newtab.

const { AITabStore } = ChromeUtils.importESModule(
  "moz-src:///browser/components/aiwindow/ui/modules/AITabStore.sys.mjs"
);
const { ConversationStore } = ChromeUtils.importESModule(
  "moz-src:///browser/components/aiwindow/ui/modules/ConversationStore.sys.mjs"
);
const { Conversation } = ChromeUtils.importESModule(
  "moz-src:///browser/components/aiwindow/models/Conversation.sys.mjs"
);
const { MockEngineManager } = ChromeUtils.importESModule(
  "resource://testing-common/AIWindowTestUtils.sys.mjs"
);

// AIWINDOW_URL and openAIWindow come from head.js.

const AITAB_PREF = "browser.smartwindow.aitab.enabled";
const CONV_ID = "conv-delete-flow";
const SLUG = "delete_flow";
const PAGE_URL = `about:smartpage?page=${SLUG}`;

// Every Smart Window openSeededPage() hands out, so cleanup can close any a
// failing task left behind.
const gOpenedWindows = new Set();

/**
 * Recreates the stored page and its conversation from scratch. Leaving a
 * previous task's rows behind makes create() throw, which would turn one
 * failure into several.
 */
async function seedPage() {
  await AITabStore.deleteBySlug(SLUG);
  await ConversationStore.deleteConversationById(CONV_ID);
  await ConversationStore.updateConversation(
    new Conversation({ id: CONV_ID, feature: "aitab" })
  );
  await AITabStore.create({
    convId: "conv-delete-flow-chat",
    toolConvId: CONV_ID,
    slug: SLUG,
    title: "Delete me",
  });
}

/**
 * Opens the seeded page in its own Smart Window.
 *
 * The window's existing tab is navigated rather than a new one opened: a new
 * tab starts at about:blank, which Smart Window's reconciliation redirects to
 * the AI Window home, tearing down the page mid-test.
 *
 * @returns {Promise<object>} The window and its browser, for the caller to
 *   assert against and close.
 */
async function openSeededPage() {
  const win = await openAIWindow();
  gOpenedWindows.add(win);
  const browser = win.gBrowser.selectedBrowser;
  BrowserTestUtils.startLoadingURIString(browser, PAGE_URL);
  await BrowserTestUtils.browserLoaded(browser, false, PAGE_URL);
  return { win, browser };
}

/**
 * Renders the page config and clicks the trash button, leaving the
 * confirmation dialog open.
 *
 * @param {object} browser
 */
function openDeleteDialog(browser) {
  return SpecialPowers.spawn(browser, [PAGE_CONFIG], async config => {
    await content.customElements.whenDefined("aitab-page");
    const page = content.document.querySelector("aitab-page");

    await ContentTaskUtils.waitForCondition(
      () => page.wrappedJSObject.status != "loading",
      "The page finishes its lookup"
    );

    page.wrappedJSObject.page = Cu.cloneInto(config, content);
    page.wrappedJSObject.status = "ready";
    await page.updateComplete;

    const header = page.shadowRoot.querySelector("aitab-header");
    Assert.ok(header, "The header renders");
    await header.updateComplete;

    const actions = header.shadowRoot.querySelector("aitab-page-actions");
    Assert.ok(actions, "The actions render inside the header");
    await actions.updateComplete;

    const deleteButton = actions.shadowRoot.querySelector(
      ".aitab-action-delete"
    );
    Assert.ok(deleteButton, "The delete button is present");
    Assert.ok(
      !actions.shadowRoot.querySelector("dialog[open]"),
      "No dialog is showing before the button is clicked"
    );

    // Each moz-button schedules its own first render, so awaiting the
    // parent's updateComplete is not enough: without this the button is still
    // 0x0 and a synthesized click lands on nothing.
    await deleteButton.wrappedJSObject.updateComplete;
    Assert.greater(
      deleteButton.getBoundingClientRect().width,
      0,
      "The delete button has a hit area, so a real click can reach it"
    );

    EventUtils.synthesizeMouseAtCenter(deleteButton, {}, content);
    await ContentTaskUtils.waitForCondition(
      () => actions.shadowRoot.querySelector("dialog[open]"),
      "The confirmation dialog opens"
    );
  });
}

let gMockEngineManager;
add_setup(async function () {
  gMockEngineManager = new MockEngineManager();
  await SpecialPowers.pushPrefEnv({ set: [[AITAB_PREF, true]] });

  registerCleanupFunction(() => {});
});

add_task(async function test_cancelling_the_dialog_keeps_the_page() {
  await seedPage();
  const { win, browser } = await openSeededPage();

  await openDeleteDialog(browser);

  await SpecialPowers.spawn(browser, [], async () => {
    const actions = content.document
      .querySelector("aitab-page")
      .shadowRoot.querySelector("aitab-header")
      .shadowRoot.querySelector("aitab-page-actions");

    const cancelButton = actions.shadowRoot.querySelector(
      ".aitab-delete-cancel"
    );
    Assert.ok(cancelButton, "The dialog offers a way out");

    EventUtils.synthesizeMouseAtCenter(cancelButton, {}, content);
    await ContentTaskUtils.waitForCondition(
      () => !actions.shadowRoot.querySelector("dialog[open]"),
      "The dialog closes"
    );
  });

  Assert.ok(
    await AITabStore.getBySlug(SLUG),
    "Cancelling leaves the page in place"
  );
  Assert.ok(
    await ConversationStore.findConversationById(CONV_ID),
    "Cancelling leaves the conversation in place"
  );
  Assert.equal(
    browser.currentURI.spec,
    PAGE_URL,
    "Cancelling does not navigate away"
  );

  await BrowserTestUtils.closeWindow(win);
});

add_task(async function test_delete_from_the_dialog_removes_the_page() {
  await seedPage();

  Assert.ok(await AITabStore.getBySlug(SLUG), "The page exists to begin with");
  Assert.ok(
    await ConversationStore.findConversationById(CONV_ID),
    "The conversation exists to begin with"
  );

  const { win, browser } = await openSeededPage();
  const navigated = BrowserTestUtils.browserLoaded(
    browser,
    false,
    AIWINDOW_URL
  );

  await openDeleteDialog(browser);

  await SpecialPowers.spawn(browser, [], async () => {
    const actions = content.document
      .querySelector("aitab-page")
      .shadowRoot.querySelector("aitab-header")
      .shadowRoot.querySelector("aitab-page-actions");

    const confirmButton = actions.shadowRoot.querySelector(
      ".aitab-delete-confirm"
    );
    Assert.ok(confirmButton, "The dialog offers a destructive confirm button");
    EventUtils.synthesizeMouseAtCenter(confirmButton, {}, content);
  });

  await TestUtils.waitForCondition(
    async () => !(await AITabStore.getBySlug(SLUG)),
    "The page is deleted from the page store"
  );
  Assert.equal(
    await ConversationStore.findConversationById(CONV_ID),
    null,
    "The conversation that produced the page is deleted too"
  );

  await navigated;
  Assert.equal(
    browser.currentURI.spec,
    AIWINDOW_URL,
    "The tab is sent back to the Smart Window home page"
  );

  await BrowserTestUtils.closeWindow(win);
});

add_task(async function test_smartbar_focus_after_returning_home() {
  await seedPage();
  const { win, browser } = await openSeededPage();
  await SpecialPowers.pushPrefEnv({
    set: [["browser.smartwindow.newtab.focusSmartbar", false]],
  });

  const navigated = BrowserTestUtils.browserLoaded(
    browser,
    false,
    AIWINDOW_URL
  );
  const actor = browser.browsingContext.currentWindowGlobal.getActor("AITab");
  await actor.receiveMessage({ name: "AITab:DeletePage" });
  await navigated;
  await SpecialPowers.spawn(browser, [], async () => {
    await ContentTaskUtils.waitForCondition(
      () =>
        content.document
          .querySelector("ai-window")
          ?.shadowRoot?.querySelector("#ai-window-smartbar")?.focused,
      "Smartbar receives focus when returning home after deleting a page"
    );
  });

  await SpecialPowers.popPrefEnv();
  await BrowserTestUtils.closeWindow(win);
});

add_task(async function test_delete_removes_the_page_from_history() {
  await PlacesUtils.history.clear();
  await seedPage();

  const bystanderURL = "about:smartpage?page=bystander_page";
  await PlacesTestUtils.addVisits([PAGE_URL, bystanderURL]);

  const { win, browser } = await openSeededPage();
  const actor = browser.browsingContext.currentWindowGlobal.getActor("AITab");

  const result = await actor.receiveMessage({ name: "AITab:DeletePage" });
  Assert.ok(result.success, "The delete reports success");

  Assert.ok(
    !(await PlacesUtils.history.hasVisits(PAGE_URL)),
    "The deleted page is removed from history"
  );
  Assert.ok(
    await PlacesUtils.history.hasVisits(bystanderURL),
    "History for a different generated page is kept"
  );

  await BrowserTestUtils.closeWindow(win);
  await PlacesUtils.history.clear();
});

add_task(
  async function test_delete_removes_history_when_the_tab_closes_midway() {
    await PlacesUtils.history.clear();
    await seedPage();
    await PlacesTestUtils.addVisits([PAGE_URL]);

    const sb = sinon.createSandbox();
    const { promise: stalled, resolve: release } = Promise.withResolvers();
    const conversationDelete = sb
      .stub(ConversationStore, "deleteConversationById")
      .returns(stalled);

    try {
      const { win, browser } = await openSeededPage();
      const actor =
        browser.browsingContext.currentWindowGlobal.getActor("AITab");

      const deleting = actor.receiveMessage({ name: "AITab:DeletePage" });
      await TestUtils.waitForCondition(
        () => conversationDelete.called,
        "The delete reaches the conversation store"
      );

      gOpenedWindows.delete(win);
      await BrowserTestUtils.closeWindow(win);
      release();

      const result = await deleting;
      Assert.ok(result.success, "The delete still reports success");
      Assert.ok(
        !(await PlacesUtils.history.hasVisits(PAGE_URL)),
        "The page is removed from history even though its tab closed"
      );
    } finally {
      sb.restore();
      await PlacesUtils.history.clear();
    }
  }
);

add_task(async function test_delete_ignores_a_page_name_from_content() {
  // The parent reads the page name from the tab's own URL, so a name supplied
  // by the child is ignored. Without that, a compromised content process
  // could aim a delete at a page the user is not looking at.
  await seedPage();
  await AITabStore.create({
    convId: "conv-bystander",
    slug: "bystander_page",
    title: "Not the open page",
  });

  const { win, browser } = await openSeededPage();
  const actor = browser.browsingContext.currentWindowGlobal.getActor("AITab");

  const result = await actor.receiveMessage({
    name: "AITab:DeletePage",
    data: { pageName: "bystander_page" },
  });

  Assert.ok(result.success, "The delete reports success");
  Assert.equal(
    await AITabStore.getBySlug(SLUG),
    null,
    "The page the tab is actually showing is the one deleted"
  );
  Assert.ok(
    await AITabStore.getBySlug("bystander_page"),
    "The page named by content is untouched"
  );

  await AITabStore.deleteBySlug("bystander_page");
  await BrowserTestUtils.closeWindow(win);
});

/**
 * Finds the tab showing `url` in any window, or null.
 *
 * Every window is searched because the link is opened by openWebLinkIn()
 * against topChromeWindow, and the chip asks for preferSwitchToTab, which lets
 * switchToTabHavingURI() reuse a tab in another window rather than open one.
 *
 * @param {string} url
 * @returns {?object} The tab showing url.
 */
function findTabShowing(url) {
  for (const win of Services.wm.getEnumerator("navigator:browser")) {
    const tab = win.gBrowser?.tabs.find(
      t => t.linkedBrowser?.currentURI?.spec == url
    );
    if (tab) {
      return tab;
    }
  }
  return null;
}

/**
 * Counts open tabs across every window, for the same reason findTabShowing
 * searches them all: a link may open in a window other than the one under
 * test.
 *
 * @returns {number}
 */
function countTabs() {
  let total = 0;
  for (const win of Services.wm.getEnumerator("navigator:browser")) {
    total += win.gBrowser?.tabs.length ?? 0;
  }
  return total;
}

add_task(async function test_a_non_web_link_is_refused() {
  // A link's href comes from the model, so this check is what keeps model
  // output from becoming a document: without it, data: renders model-supplied
  // markup in a tab of its own. Nothing below AITabParent checks the scheme.
  //
  // A good link is sent last and waited on. Messages are handled in order, so
  // once its tab exists the refused ones have been processed too, which
  // avoids waiting an arbitrary amount of time for something not to happen.
  const ALLOWED = "https://example.com/allowed";

  await seedPage();
  const { win, browser } = await openSeededPage();
  const tabsBefore = countTabs();

  await SpecialPowers.spawn(browser, [ALLOWED], async allowed => {
    const page = content.document.querySelector("aitab-page");

    for (const url of [
      "about:robots",
      "javascript:alert(1)",
      "data:text/html,x",
      "file:///etc/passwd",
      allowed,
    ]) {
      page.dispatchEvent(
        new content.CustomEvent("AITab:OpenLink", {
          bubbles: true,
          detail: Cu.cloneInto({ url }, content),
        })
      );
    }
  });

  await TestUtils.waitForCondition(
    () => findTabShowing(ALLOWED),
    "The allowed link opens a tab"
  );
  const tab = findTabShowing(ALLOWED);
  Assert.equal(
    countTabs(),
    tabsBefore + 1,
    "Only the http(s) link opened a tab"
  );

  BrowserTestUtils.removeTab(tab);
  await BrowserTestUtils.closeWindow(win);
});

registerCleanupFunction(async () => {
  await gMockEngineManager.rejectAllRequests();

  // A task that throws never reaches its own closeWindow(), and a leftover
  // window fails the whole file with "Found an unexpected browser window",
  // which reads louder than the failure that caused it.
  for (const win of gOpenedWindows) {
    if (!win.closed) {
      await BrowserTestUtils.closeWindow(win);
    }
  }
  gOpenedWindows.clear();

  await AITabStore.deleteBySlug(SLUG);
  await ConversationStore.deleteConversationById(CONV_ID);

  gMockEngineManager.cleanupMocks();
});
