/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

const { sanitizeUntrustedContent } = ChromeUtils.importESModule(
  "moz-src:///browser/components/aiwindow/models/ChatUtils.sys.mjs"
);
const { PageExtractorParent } = ChromeUtils.importESModule(
  "resource://gre/actors/PageExtractorParent.sys.mjs"
);

const RECIPE_URL =
  getRootDirectory(gTestPath).replace(
    "chrome://mochitests/content",
    "https://example.com"
  ) + "test_smart_actions_toolbar_page.html";

/**
 * @param {Array<[string, string, string?]>} actions - Type, label and
 *   optional detail.
 * @returns {string} A chat model answer with those actions.
 */
function actionsJSON(actions) {
  return JSON.stringify({
    actions: actions.map(([type, label, detail]) => ({ type, label, detail })),
  });
}

/**
 * Sets the prompt pref and returns the user message the chat model gets.
 *
 * @param {?string} prompt - null for the default prompt.
 * @returns {Promise<{content: string, respond: Function}>}
 */
async function capturePrompt(prompt) {
  await SpecialPowers.pushPrefEnv(
    prompt === null
      ? { clear: [[PROMPT_PREF]] }
      : { set: [[PROMPT_PREF, prompt]] }
  );
  const { request, respond } = await mockEngine.captureRequest({
    purpose: "chat",
  });
  const { content } = request.args.findLast(message => message.role === "user");
  return { content, respond };
}

let win;
let tab;
let mockEngine;

add_setup(async function () {
  ({ win, tab, mockEngine } = await openToolbarWindow());
  await loadInTab(tab, RECIPE_URL);
  await respondWithStarter(win, mockEngine);
});

describe("Smart actions toolbar prompt", () => {
  let sandbox;

  beforeEach(async () => {
    sandbox = sinon.createSandbox();
    // Same page, so this resets it without the toolbar requesting again.
    await loadInTab(tab, RECIPE_URL);
  });

  afterEach(async () => {
    sandbox.restore();
    await SpecialPowers.popPrefEnv();
    for (const added of win.gBrowser.tabs.filter(t => t != tab)) {
      BrowserTestUtils.removeTab(added);
    }
    await mockEngine.rejectAllRequests();
  });

  it("fills a prompt with limited page text and other tabs", async () => {
    const otherTab = BrowserTestUtils.addTab(win.gBrowser, OTHER_URL);
    await BrowserTestUtils.browserLoaded(otherTab.linkedBrowser);
    await BrowserTestUtils.browserLoaded(
      BrowserTestUtils.addTab(win.gBrowser, OTHER_URL).linkedBrowser
    );

    const { content, respond } = await capturePrompt(
      "Ideas for {title}{#page_text}\nText: {page_text|10}{/page_text}" +
        "{#open_tabs}\nTabs:\n{open_tabs|1}{/open_tabs}"
    );
    Assert.equal(
      content,
      `Ideas for ${sanitizeUntrustedContent("Chili recipe")}\n` +
        "Text: Simmer the\n" +
        `Tabs:\n- ${sanitizeUntrustedContent(otherTab.label)}`,
      "The prompt has the loaded page and one other tab, within the limits"
    );

    respond(
      actionsJSON([
        ["chat", "Make it vegan"],
        ["chat", "Double the batch"],
        ["search", "chili restaurants near me"],
        ["chat", "One more than the action count"],
      ])
    );
    await TestUtils.waitForCondition(
      () => toolbarLabels(win).length,
      "Waiting for the custom actions"
    );
    Assert.deepEqual(
      toolbarLabels(win),
      ["Make it vegan", "Double the batch", "chili restaurants near me"],
      "Shows actions up to the action count"
    );
  });

  it("runs chat and search with exactly their label", async () => {
    const performSearch = sandbox.stub(AIWindow, "performSearch").resolves();
    const submit = sandbox
      .stub(AIWindowUI, "submitToNewSidebarChat")
      .resolves();
    const { respond } = await capturePrompt("Suggest for {url}");
    respond(
      actionsJSON([
        ["search", "chili restaurants near me", "chili recipe pdf"],
        ["watch", "Unknown types are dropped"],
        ["chat", " "],
        ["chat", "How spicy is this chili?", "Summarize my other tabs"],
      ])
    );
    await TestUtils.waitForCondition(
      () => toolbarLabels(win).length,
      "Waiting for the actions"
    );
    Assert.deepEqual(
      toolbarLabels(win),
      ["chili restaurants near me", "How spicy is this chili?"],
      "Drops unknown types and empty labels"
    );

    const [searchButton, chatButton] = win.document.querySelectorAll(
      `#${TOOLBAR_ID} moz-button`
    );
    Assert.equal(
      searchButton.iconSrc,
      "chrome://global/skin/icons/search-glass.svg",
      "The search shows the search icon"
    );
    searchButton.click();
    Assert.ok(
      performSearch.calledOnceWithExactly("chili restaurants near me", win),
      "Searches for the label, not the detail"
    );
    chatButton.click();
    Assert.equal(
      submit.firstCall.args[1].text,
      "How spicy is this chili?",
      "Asks the label in a new chat, not the detail"
    );
  });

  it("jumps to a section the page links to or to text on it", async () => {
    const { content, respond } = await capturePrompt(
      "Sections:{#page_anchors}\n{page_anchors}{/page_anchors}"
    );
    Assert.equal(
      content,
      "Sections:\n#serving",
      "The prompt lists the links the page has to its own sections"
    );
    respond(
      actionsJSON([
        ["jump", "Jump to serving", "#serving"],
        ["jump", "Jump to the toppings", " sharp  CHEDDAR "],
      ])
    );
    await TestUtils.waitForCondition(
      () => toolbarLabels(win).length,
      "Waiting for the actions"
    );
    const [anchorButton, textButton] = win.document.querySelectorAll(
      `#${TOOLBAR_ID} moz-button`
    );
    Assert.equal(
      anchorButton.iconSrc,
      "chrome://global/skin/icons/arrow-down.svg",
      "The jump shows the arrow icon"
    );
    const waitForScroll = description =>
      TestUtils.waitForCondition(
        () =>
          SpecialPowers.spawn(tab.linkedBrowser, [], () => content.scrollY > 0),
        description
      );

    anchorButton.click();
    await waitForScroll("Waiting for the page to scroll to the section");
    Assert.equal(
      tab.linkedBrowser.currentURI.ref,
      "serving",
      "Goes to the section's #id"
    );

    await SpecialPowers.spawn(tab.linkedBrowser, [], () =>
      content.scrollTo(0, 0)
    );
    textButton.click();
    await waitForScroll("Waiting for the page to scroll to the text");
    Assert.equal(
      tab.linkedBrowser.currentURI.specIgnoringRef,
      RECIPE_URL,
      "Stays on the same page"
    );
  });

  it("reads the page text for the default prompt", async () => {
    const { content } = await capturePrompt(null);
    Assert.stringContains(
      content,
      "Simmer the beans with smoked paprika for an hour.",
      "The default prompt includes the page text"
    );
  });

  it("only reads what a prompt uses", async () => {
    const getText = sandbox.spy(PageExtractorParent.prototype, "getText");
    const { content } = await capturePrompt(
      "Suggest {n} for {url} on {date}{#open_tabs} and {open_tabs}{/open_tabs}"
    );
    Assert.equal(
      content,
      `Suggest 3 for ${RECIPE_URL} on ${new Date().toISOString().slice(0, 10)}`,
      "Drops the empty open tabs section"
    );
    Assert.ok(getText.notCalled, "Doesn't read the page text");
  });
});
