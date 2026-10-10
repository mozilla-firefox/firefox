/* Any copyright is dedicated to the Public Domain.
   https://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

const { SpellCheckHelper } = ChromeUtils.importESModule(
  "resource://gre/modules/InlineSpellChecker.sys.mjs"
);

ChromeUtils.defineLazyGetter(this, "UrlbarTestUtils", () => {
  const { UrlbarTestUtils: module } = ChromeUtils.importESModule(
    "resource://testing-common/UrlbarTestUtils.sys.mjs"
  );
  module.init(this);
  return module;
});

const TESTS = [
  {
    action: "/search",
    method: "GET",
    charset: "UTF-8",
    fields: [
      { name: "q", value: "Some initial value", main: true },
      { name: "utf8✓", value: "✓", hidden: true },
    ],
    submission: "kitten",
    expected: "https://example.org/search?q=kitten&utf8%E2%9C%93=%E2%9C%93",
  },
  {
    action: "/search",
    method: "GET",
    charset: "windows-1252",
    fields: [
      { name: "q", main: true },
      { name: "cb", checked: true, value: "true", type: "checkbox" },
      { name: "cb2", checked: false, value: "true", type: "checkbox" },
    ],
    submission: "caff\u00E8+",
    expected: "https://example.org/search?q=caff%E8%2B&cb=true",
  },
  {
    action: "/search",
    method: "POST",
    charset: "UTF-8",
    fields: [
      { name: "q", value: "Some initial value", main: true },
      { name: "utf8✓", value: "✓", hidden: true },
    ],
    submission: "kitten",
    expected: "https://example.org/search",
    expectedPost: "q=kitten&utf8%E2%9C%93=%E2%9C%93",
  },
  {
    action: "/search",
    method: "POST",
    charset: "windows-1252",
    fields: [
      { name: "q", main: true },
      { name: "foo", value: "bar" },
    ],
    submission: "caff\u00E8+",
    expected: "https://example.org/search",
    expectedPost: "q=caff%E8%2B&foo=bar",
  },
];

const URL_UTF_8 =
  "https://example.org/browser/browser/components/search/test/browser/test.html";
const URL_WINDOWS1252 =
  "https://example.org/browser/browser/components/search/test/browser/test_windows1252.html";

async function addEngine(browser, selector, name, alias) {
  let dialogWin = await openAddEngineDialog(browser, selector);

  fillTextField("engineName", name, dialogWin);
  fillTextField("engineAlias", alias, dialogWin);

  info("Saving engine.");
  EventUtils.synthesizeKey("VK_RETURN", {}, dialogWin);
  await TestUtils.waitForCondition(
    () => gURLBar.searchMode?.engineName == name
  );
  Assert.ok(true, "Went into search mode");

  await UrlbarTestUtils.exitSearchMode(window);
  return SearchService.getEngineByName(name);
}

async function createForm({ action, method, fields }) {
  let doc = content.document;
  doc.querySelector("form")?.remove();
  let form = doc.createElement("form");
  form.method = method;
  form.action = action;
  form.role = "search";

  for (let fieldInfo of fields) {
    let input = doc.createElement("input");
    input.value = fieldInfo.value ?? "";
    input.type = fieldInfo.type ?? "text";

    if (fieldInfo.checked) {
      input.checked = "true";
    }

    input.name = fieldInfo.name;
    if (fieldInfo.main) {
      input.id = "mainInput";
    }
    if (fieldInfo.hidden) {
      input.hidden = true;
    }
    form.appendChild(input);
  }

  doc.body.appendChild(form);
}

async function navigateToCharset(charset) {
  if (charset.toUpperCase() == "UTF-8") {
    let browserLoadedPromise = BrowserTestUtils.browserLoaded(
      gBrowser,
      false,
      URL_UTF_8
    );
    BrowserTestUtils.startLoadingURIString(gBrowser, URL_UTF_8);
    await browserLoadedPromise;
  } else if (charset.toLowerCase() == "windows-1252") {
    let browserLoadedPromise = BrowserTestUtils.browserLoaded(
      gBrowser,
      false,
      URL_WINDOWS1252
    );
    BrowserTestUtils.startLoadingURIString(gBrowser, URL_WINDOWS1252);
    await browserLoadedPromise;
  } else {
    throw new Error();
  }
}

function postDataToString(postData) {
  if (!postData) {
    return undefined;
  }
  let binaryStream = Cc["@mozilla.org/binaryinputstream;1"].createInstance(
    Ci.nsIBinaryInputStream
  );
  binaryStream.setInputStream(postData.data);

  return binaryStream
    .readBytes(binaryStream.available())
    .replace("searchTerms", "%s");
}

async function openAddEngineDialog(browser, selector) {
  let contextMenu = document.getElementById("contentAreaContextMenu");
  let addEngineItem = document.getElementById("context-add-engine");

  let contextMenuPromise = BrowserTestUtils.waitForEvent(
    contextMenu,
    "popupshown"
  );
  info("Opening context menu.");
  await BrowserTestUtils.synthesizeMouseAtCenter(
    selector,
    { type: "contextmenu", button: 2 },
    browser
  );
  await contextMenuPromise;
  let dialogLoaded = TestUtils.topicObserved("subdialog-loaded");
  info("Clicking add engine.");
  contextMenu.activateItem(addEngineItem);
  let [dialogWin] = await dialogLoaded;
  await BrowserTestUtils.waitForEvent(dialogWin, "load");
  await window.gDialogBox.dialog._dialogReady;
  info("Dialog opened.");
  return dialogWin;
}

add_setup(async function () {
  // Tests get no default Top Sites. addEngine leaves search mode with an empty
  // input, and without a result the view never opens for exitSearchMode to
  // wait on.
  await SpecialPowers.pushPrefEnv({
    set: [
      [
        "browser.newtabpage.activity-stream.default.sites",
        "https://example.com/",
      ],
    ],
  });
});

add_task(async function testAddingEngines() {
  let tab = await BrowserTestUtils.openNewForegroundTab(gBrowser);
  let browser = tab.linkedBrowser;

  for (let args of TESTS) {
    await navigateToCharset(args.charset);
    await SpecialPowers.spawn(browser, [args], createForm);
    let engine = await addEngine(browser, "#mainInput", "My Engine", "alias");
    Assert.ok(!!engine, "Engine was installed");
    Assert.equal(
      engine.id,
      (await SearchService.getEngineByAlias("alias"))?.id,
      "Engine has correct alias"
    );

    Assert.equal(engine.queryCharset, args.charset);
    let submission = engine.getSubmission(args.submission);
    Assert.equal(
      submission.uri.spec,
      args.expected,
      "Submission URI is correct"
    );
    Assert.equal(
      SearchTestUtils.getPostDataString(submission),
      args.expectedPost,
      "Submission post data is correct"
    );

    await SearchService.removeEngine(engine);
  }

  // Let the dialog fully close. Otherwise, the tab cannot be closed properly.
  await TestUtils.waitForCondition(
    () => !document.documentElement.hasAttribute("window-modal-open")
  );
  BrowserTestUtils.removeTab(gBrowser.selectedTab);
});

add_task(async function testDialog() {
  let tab = await BrowserTestUtils.openNewForegroundTab(gBrowser);
  let browser = tab.linkedBrowser;

  await navigateToCharset(TESTS[0].charset);
  await SpecialPowers.spawn(browser, [TESTS[0]], createForm);
  let dialogWin = await openAddEngineDialog(browser, "#mainInput");

  // Test if title is displayed.
  Assert.equal(
    dialogWin.document.getElementById("titleContainer").style.display,
    "",
    "Adjustable title is displayed"
  );

  // Test if context menu works for inputs.
  EventUtils.synthesizeMouseAtCenter(
    dialogWin.document.getElementById("engineName"),
    { type: "contextmenu", button: 2 },
    dialogWin
  );
  let contextMenu = dialogWin.document.getElementById("textbox-contextmenu");
  Assert.ok(contextMenu, "Context menu is created");
  if (contextMenu.state == "showing") {
    await BrowserTestUtils.waitForEvent(contextMenu, "popupshown");
  }
  Assert.equal(contextMenu.state, "open", "Context menu is shown");
  contextMenu.hidePopup();
  Assert.equal(contextMenu.state, "closed", "Context menu was closed");

  info("Closing dialog.");
  EventUtils.synthesizeKey("VK_ESCAPE", {}, dialogWin);
  // Let the dialog fully close. Otherwise, the tab cannot be closed properly.
  await TestUtils.waitForCondition(
    () => !document.documentElement.hasAttribute("window-modal-open")
  );
  BrowserTestUtils.removeTab(gBrowser.selectedTab);
});

add_task(async function testSearchFieldDetection() {
  let form = document.createElement("form");
  form.method = "GET";
  form.action = "/";

  let input = document.createElement("input");
  input.type = "search";
  input.name = "q";
  form.appendChild(input);

  let isSearchField = SpellCheckHelper.isTargetASearchEngineField(
    input,
    window
  );
  Assert.equal(isSearchField, true, "Is search field initially");

  // We test mostly non search fields here since valid search fields
  // are already tested in testAddingEngines.
  delete form.removeAttribute("action");
  isSearchField = SpellCheckHelper.isTargetASearchEngineField(input, window);
  Assert.equal(isSearchField, false, "Missing action means no search field");

  form.action = "/";

  form.method = "dialog";
  isSearchField = SpellCheckHelper.isTargetASearchEngineField(input, window);
  Assert.equal(isSearchField, false, "Method=dialog means no search field");

  form.method = "POST";
  isSearchField = SpellCheckHelper.isTargetASearchEngineField(input, window);
  Assert.equal(isSearchField, false, "Method=post means no search field");

  form.role = "search";
  isSearchField = SpellCheckHelper.isTargetASearchEngineField(input, window);
  Assert.equal(isSearchField, true, "Post and role=search means search field");

  form.method = "GET";
  form.removeAttribute("role");

  input.removeAttribute("name");
  isSearchField = SpellCheckHelper.isTargetASearchEngineField(input, window);
  Assert.equal(isSearchField, false, "Missing name means no search field");

  input.name = "q";

  input.type = "url";
  isSearchField = SpellCheckHelper.isTargetASearchEngineField(input, window);
  Assert.equal(isSearchField, false, "Url input means no search field");

  input.type = "text";

  let fileInput = document.createElement("input");
  fileInput.type = "file";
  fileInput.name = "file-input";
  form.appendChild(fileInput);
  isSearchField = SpellCheckHelper.isTargetASearchEngineField(input, window);
  Assert.equal(isSearchField, false, "File input means no search field");

  fileInput.remove();
  isSearchField = SpellCheckHelper.isTargetASearchEngineField(input, window);
  Assert.equal(isSearchField, true, "Is search field again");
});
