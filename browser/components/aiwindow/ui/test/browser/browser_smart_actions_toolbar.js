/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

const { MockEngineManager } = ChromeUtils.importESModule(
  "resource://testing-common/AIWindowTestUtils.sys.mjs"
);
const { SmartActionsToolbar } = ChromeUtils.importESModule(
  "moz-src:///browser/components/aiwindow/ui/modules/SmartActionsToolbar.sys.mjs"
);

const TOOLBAR_PREF = "browser.smartwindow.actionsToolbar.enabled";
const CONSENT_PREF = "browser.smartwindow.tos.consentTime";
const TOOLBAR_ID = "smart-actions-toolbar";
const PAGE_URL = "https://example.com/";
const OTHER_URL = "https://example.org/";
const STARTER = "What is this page about";
const STARTER_PURPOSE = "convo-starters-sidebar";

add_setup(async function () {
  await SpecialPowers.pushPrefEnv({
    set: [
      [TOOLBAR_PREF, true],
      [CONSENT_PREF, 1],
      // The sidebar's own starter requests would share the mocked purpose.
      ["browser.smartwindow.sidebar.openByDefault", false],
    ],
  });
});

function isToolbarShown(win) {
  const toolbar = win.document.getElementById(TOOLBAR_ID);
  return !!toolbar && !toolbar.hidden;
}

/**
 * Answers the toolbar's starter request and waits for the starter button.
 *
 * @param {ChromeWindow} win
 * @param {MockEngineManager} mockEngine
 * @returns {Promise<{request: object, button: MozButton}>}
 */
async function respondWithStarter(win, mockEngine) {
  const { request, respond } = await mockEngine.captureRequest({
    purpose: STARTER_PURPOSE,
  });
  respond(STARTER);
  const button = await TestUtils.waitForCondition(() => {
    const found = win.document.querySelector(`#${TOOLBAR_ID} moz-button`);
    return (
      isToolbarShown(win) &&
      found?.label === STARTER &&
      !found.disabled &&
      found
    );
  }, "Waiting for the starter to render");
  return { request, button };
}

function toolbarLabels(win) {
  return [...win.document.querySelectorAll(`#${TOOLBAR_ID} moz-button`)].map(
    button => button.label
  );
}

function loadInTab(tab, uriString) {
  return BrowserTestUtils.loadURIString({
    browser: tab.linkedBrowser,
    uriString,
  });
}

describe("Smart actions toolbar", () => {
  let win;
  let tab;
  let mockEngine;
  let sandbox;
  let request;
  let button;

  beforeEach(async () => {
    sandbox = sinon.createSandbox();
    mockEngine = new MockEngineManager();
    win = await BrowserTestUtils.openNewBrowserWindow();
    tab = await BrowserTestUtils.openNewForegroundTab(win.gBrowser, PAGE_URL);
    AIWindow.toggleAIWindow(win, true);
    ({ request, button } = await respondWithStarter(win, mockEngine));
  });

  afterEach(async () => {
    sandbox.restore();
    await BrowserTestUtils.closeWindow(win);
    await mockEngine.rejectAllRequests();
    mockEngine.cleanupMocks();
  });

  it("requests starters for the current page", () => {
    Assert.ok(
      JSON.stringify(request).includes(PAGE_URL),
      "The model request names the current page"
    );
  });

  it("starts a new sidebar chat about the page", async () => {
    const openSidebar = sandbox.spy(AIWindowUI, "openSidebar");
    Services.fog.testResetFOG();

    button.click();
    Assert.ok(button.disabled, "The button is disabled while the chat starts");
    const { request: chatRequest } = await mockEngine.captureRequest({
      purpose: "chat",
    });

    Assert.ok(
      openSidebar.calledOnceWithExactly(win),
      "The sidebar opens on a new chat"
    );
    Assert.ok(
      JSON.stringify(chatRequest).includes(STARTER),
      "The chat asks the starter"
    );
    const userMessage = AIWindow.getActiveConversation(win).messages.find(
      message => message.role === MESSAGE_ROLE.USER
    );
    Assert.equal(
      userMessage.pageUrl.href,
      PAGE_URL,
      "The chat is about the page"
    );
    Assert.deepEqual(userMessage.content.contextMentions, [
      { type: "currentTab", url: PAGE_URL, label: tab.label },
    ]);
    Assert.equal(
      Glean.smartWindow.chatSubmit.testGetValue()?.at(-1)?.extra.submit_type,
      "toolbar",
      "The chat is recorded as submitted from the toolbar"
    );
    await TestUtils.waitForCondition(
      () => !button.disabled,
      "Waiting for the button to be enabled again"
    );
  });

  it("follows tab switches", async () => {
    await BrowserTestUtils.openNewForegroundTab(win.gBrowser, OTHER_URL);
    const other = await mockEngine.captureRequest({
      purpose: STARTER_PURPOSE,
    });
    Assert.ok(
      JSON.stringify(other.request).includes(OTHER_URL),
      "The new tab's page is requested"
    );
    other.respond("Other starter");
    await TestUtils.waitForCondition(
      () => toolbarLabels(win)[0] === "Other starter",
      "Waiting for the new tab's starter"
    );

    await BrowserTestUtils.switchTab(win.gBrowser, tab);
    const { request: back } = await respondWithStarter(win, mockEngine);
    Assert.ok(
      JSON.stringify(back).includes(PAGE_URL),
      "Switching back requests the first tab's page"
    );
    Assert.deepEqual(toolbarLabels(win), [STARTER]);
  });

  it("hides when there are no starters", async () => {
    await loadInTab(tab, OTHER_URL);
    const { respond } = await mockEngine.captureRequest({
      purpose: STARTER_PURPOSE,
    });
    Assert.ok(
      win.document.querySelector(
        `#${TOOLBAR_ID} [data-l10n-id="smart-actions-loading"]`
      ),
      "Shows the loading text"
    );
    respond("");
    await TestUtils.waitForCondition(
      () => !isToolbarShown(win),
      "Waiting for the toolbar to hide"
    );
  });

  it("ignores starters for a page it has left", async () => {
    await loadInTab(tab, OTHER_URL);
    const stale = await mockEngine.captureRequest({
      purpose: STARTER_PURPOSE,
    });
    Assert.ok(
      JSON.stringify(stale.request).includes(OTHER_URL),
      "The first request is for the page being left"
    );
    await loadInTab(tab, PAGE_URL);
    stale.respond("Stale starter");

    await respondWithStarter(win, mockEngine);
    Assert.deepEqual(
      toolbarLabels(win),
      [STARTER],
      "Only the current page's starter shows"
    );
  });

  it("hides in a Classic window", async () => {
    AIWindow.toggleAIWindow(win, false);
    Assert.ok(!isToolbarShown(win), "Hidden after switching to Classic");
    const update = sandbox.spy(SmartActionsToolbar, "update");
    await loadInTab(tab, OTHER_URL);
    Assert.ok(!isToolbarShown(win), "Stays hidden on another page");
    Assert.ok(update.notCalled, "Classic windows don't follow navigation");

    AIWindow.toggleAIWindow(win, true);
    await respondWithStarter(win, mockEngine);
  });

  it("hides on the full-page assistant", async () => {
    await loadInTab(tab, AIWINDOW_URL);
    Assert.ok(!isToolbarShown(win), "Hidden on the full-page assistant");
  });

  it("hides without Terms of Use consent", async () => {
    await SpecialPowers.pushPrefEnv({ clear: [[CONSENT_PREF]] });
    await loadInTab(tab, OTHER_URL);
    Assert.ok(!isToolbarShown(win), "Hidden without consent");
    await SpecialPowers.popPrefEnv();
  });

  it("hides while its pref is off", async () => {
    await SpecialPowers.pushPrefEnv({ set: [[TOOLBAR_PREF, false]] });
    Assert.ok(!isToolbarShown(win), "Hidden when the pref is turned off");
    const update = sandbox.spy(SmartActionsToolbar, "update");
    await loadInTab(tab, OTHER_URL);
    Assert.ok(update.notCalled, "Doesn't follow navigation while off");
    await SpecialPowers.popPrefEnv();
    await respondWithStarter(win, mockEngine);
  });

  it("hides while Smart Window is disabled", async () => {
    await SpecialPowers.pushPrefEnv({
      set: [["browser.smartwindow.enabled", false]],
    });
    Assert.ok(!isToolbarShown(win), "Hidden when Smart Window is disabled");
    await SpecialPowers.popPrefEnv();
  });
});
