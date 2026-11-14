/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

let win;
let tab;
let mockEngine;

add_setup(async function () {
  ({ win, tab, mockEngine } = await openToolbarWindow());
});

describe("Smart actions toolbar", () => {
  let sandbox;
  let pageUrl;
  let request;
  let button;

  beforeEach(async () => {
    sandbox = sinon.createSandbox();
    pageUrl = newPageUrl();
    await loadInTab(tab, pageUrl);
    ({ request, button } = await respondWithStarter(win, mockEngine));
  });

  afterEach(async () => {
    sandbox.restore();
    AIWindowUI.closeSidebar(win);
    for (const added of win.gBrowser.tabs.filter(t => t != tab)) {
      BrowserTestUtils.removeTab(added);
    }
    await mockEngine.rejectAllRequests();
  });

  it("requests starters for the current page", () => {
    Assert.ok(
      JSON.stringify(request).includes(pageUrl),
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
      pageUrl,
      "The chat is about the page"
    );
    Assert.deepEqual(userMessage.content.contextMentions, [
      { type: "currentTab", url: pageUrl, label: tab.label },
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
      JSON.stringify(back).includes(pageUrl),
      "Switching back requests the first tab's page"
    );
    Assert.deepEqual(
      toolbarLabels(win),
      [STARTER],
      "Shows the first tab's starter again"
    );
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
    await loadInTab(tab, pageUrl);
    stale.respond("Stale starter");

    await respondWithStarter(win, mockEngine);
    Assert.deepEqual(
      toolbarLabels(win),
      [STARTER],
      "Only the current page's starter shows"
    );
  });
});
