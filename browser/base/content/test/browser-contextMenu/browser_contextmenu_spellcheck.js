/* Any copyright is dedicated to the Public Domain.
   http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

let contextMenu;

const { sinon } = ChromeUtils.importESModule(
  "resource://testing-common/Sinon.sys.mjs"
);

const example_base =
  // eslint-disable-next-line sdl/no-insecure-url
  "http://example.com/browser/browser/base/content/test/browser-contextMenu/";
const MAIN_URL = example_base + "subtst_contextmenu_input.html";

const askChatMenu = [
  "context-ask-chat",
  true,
  // Need a blank entry here because the Ask Chat submenu is dynamically built with no ids.
  "",
  null,
];

add_task(async function test_setup() {
  await BrowserTestUtils.openNewForegroundTab(gBrowser, MAIN_URL);

  const chrome_base =
    "chrome://mochitests/content/browser/browser/base/content/test/browser-contextMenu/";
  const contextmenu_common = chrome_base + "contextmenu_common.js";
  /* import-globals-from contextmenu_common.js */
  Services.scriptloader.loadSubScript(contextmenu_common, this);
});

add_task(async function test_text_input_spellcheck() {
  await test_contextmenu(
    "#input_spellcheck_no_value",
    [
      "context-undo",
      false,
      "context-redo",
      false,
      "---",
      null,
      "context-cut",
      null, // ignore the enabled/disabled states; there are race conditions
      // in the edit commands but they're not relevant for what we're testing.
      "context-copy",
      null,
      "context-paste",
      null, // ignore clipboard state
      "context-delete",
      null,
      "context-selectall",
      null,
      "---",
      null,
      ...askChatMenu,
      "---",
      null,
      "spell-check-enabled",
      true,
      "spell-dictionaries",
      true,
      [
        "spell-check-dictionary-en-US",
        true,
        "---",
        null,
        "spell-add-dictionaries",
        true,
      ],
      null,
    ],
    {
      waitForSpellCheck: true,
      async preCheckContextMenuFn() {
        await SpecialPowers.spawn(
          gBrowser.selectedBrowser,
          [],
          async function () {
            let doc = content.document;
            let input = doc.getElementById("input_spellcheck_no_value");
            input.setAttribute("spellcheck", "true");
            input.clientTop; // force layout flush
          }
        );
      },
      awaitOnMenuBuilt: {
        id: "context-ask-chat",
      },
    }
  );
});

add_task(async function test_text_input_spellcheckwrong() {
  await test_contextmenu(
    "#input_spellcheck_incorrect",
    [
      "*prodigality",
      true, // spelling suggestion
      "spell-add-to-dictionary",
      true,
      "---",
      null,
      "context-undo",
      null,
      "context-redo",
      null,
      "---",
      null,
      "context-cut",
      null,
      "context-copy",
      null,
      "context-paste",
      null, // ignore clipboard state
      "context-delete",
      null,
      "context-selectall",
      null,
      "---",
      null,
      ...askChatMenu,
      "---",
      null,
      "spell-check-enabled",
      true,
      "spell-dictionaries",
      true,
      [
        "spell-check-dictionary-en-US",
        true,
        "---",
        null,
        "spell-add-dictionaries",
        true,
      ],
      null,
    ],
    {
      waitForSpellCheck: true,
      keepMenuOpen: true,
      awaitOnMenuBuilt: {
        id: "context-ask-chat",
      },
    }
  );
  is(
    contextMenu.querySelector(".spell-suggestion").accessKey,
    "1",
    "A spelling suggestion gets a numbered accesskey"
  );
  let hidden = BrowserTestUtils.waitForPopupEvent(contextMenu, "hidden");
  contextMenu.hidePopup();
  await hidden;
});

const kCorrectItems = [
  "context-undo",
  false,
  "context-redo",
  false,
  "---",
  null,
  "context-cut",
  null,
  "context-copy",
  null,
  "context-paste",
  null, // ignore clipboard state
  "context-delete",
  null,
  "context-selectall",
  null,
  "---",
  null,
  ...askChatMenu,
  "---",
  null,
  "spell-check-enabled",
  true,
  "spell-dictionaries",
  true,
  [
    "spell-check-dictionary-en-US",
    true,
    "---",
    null,
    "spell-add-dictionaries",
    true,
  ],
  null,
];

add_task(async function test_text_input_spellcheckcorrect() {
  await test_contextmenu("#input_spellcheck_correct", kCorrectItems, {
    waitForSpellCheck: true,
    awaitOnMenuBuilt: {
      id: "context-ask-chat",
    },
  });
});

add_task(async function test_text_input_spellcheck_deadactor() {
  await test_contextmenu("#input_spellcheck_correct", kCorrectItems, {
    waitForSpellCheck: true,
    keepMenuOpen: true,
    awaitOnMenuBuilt: {
      id: "context-ask-chat",
    },
  });
  let wgp = gBrowser.selectedBrowser.browsingContext.currentWindowGlobal;

  // Now the menu is open, and spellcheck is running, switch to another tab and
  // close the original:
  let tab = gBrowser.selectedTab;
  await BrowserTestUtils.openNewForegroundTab(gBrowser, "https://example.org");
  BrowserTestUtils.removeTab(tab);
  // Ensure we've invalidated the actor
  await TestUtils.waitForCondition(
    () => wgp.isClosed,
    "Waiting for actor to be dead after tab closes"
  );
  contextMenu.hidePopup();

  // Now go back to the input testcase:
  BrowserTestUtils.startLoadingURIString(gBrowser.selectedBrowser, MAIN_URL);
  await BrowserTestUtils.browserLoaded(
    gBrowser.selectedBrowser,
    false,
    MAIN_URL
  );

  // Check the menu still looks the same, keep it open again:
  await test_contextmenu("#input_spellcheck_correct", kCorrectItems, {
    waitForSpellCheck: true,
    keepMenuOpen: true,
    awaitOnMenuBuilt: {
      id: "context-ask-chat",
    },
  });

  // Now navigate the tab, after ensuring there's an unload listener, so
  // we don't end up in bfcache:
  await SpecialPowers.spawn(gBrowser.selectedBrowser, [], async function () {
    content.document.body.setAttribute("onunload", "");
  });
  wgp = gBrowser.selectedBrowser.browsingContext.currentWindowGlobal;

  const NEW_URL = MAIN_URL.replace(".com", ".org");
  BrowserTestUtils.startLoadingURIString(gBrowser.selectedBrowser, NEW_URL);
  await BrowserTestUtils.browserLoaded(
    gBrowser.selectedBrowser,
    false,
    NEW_URL
  );
  // Ensure we've invalidated the actor
  await TestUtils.waitForCondition(
    () => wgp.isClosed,
    "Waiting for actor to be dead after onunload"
  );
  contextMenu.hidePopup();

  // Check the menu *still* looks the same (and keep it open again):
  await test_contextmenu("#input_spellcheck_correct", kCorrectItems, {
    waitForSpellCheck: true,
    keepMenuOpen: true,
    awaitOnMenuBuilt: {
      id: "context-ask-chat",
    },
  });

  // Check what happens if the actor stays alive by loading the same page
  // again; now the context menu stuff should be destroyed by the menu
  // hiding, nothing else.
  wgp = gBrowser.selectedBrowser.browsingContext.currentWindowGlobal;
  BrowserTestUtils.startLoadingURIString(gBrowser.selectedBrowser, NEW_URL);
  await BrowserTestUtils.browserLoaded(
    gBrowser.selectedBrowser,
    false,
    NEW_URL
  );
  contextMenu.hidePopup();

  // Check the menu still looks the same:
  await test_contextmenu("#input_spellcheck_correct", kCorrectItems, {
    waitForSpellCheck: true,
    awaitOnMenuBuilt: {
      id: "context-ask-chat",
    },
  });
  // And test it a last time without any navigation:
  await test_contextmenu("#input_spellcheck_correct", kCorrectItems, {
    waitForSpellCheck: true,
    awaitOnMenuBuilt: {
      id: "context-ask-chat",
    },
  });
});

add_task(async function test_text_input_spellcheck_multilingual() {
  if (AppConstants.platform == "macosx") {
    todo(
      false,
      "Need macOS support for closemenu attributes in order to " +
        "stop the spellcheck menu closing, see bug 1796007."
    );
    return;
  }
  let sandbox = sinon.createSandbox();
  registerCleanupFunction(() => sandbox.restore());

  // We need to mock InlineSpellCheckerUI.mRemote's properties, but
  // InlineSpellCheckerUI.mRemote won't exist until we initialize the context
  // menu, so do that and then manually reinit the spellcheck bits so
  // we control them:
  await test_contextmenu("#input_spellcheck_correct", kCorrectItems, {
    waitForSpellCheck: true,
    keepMenuOpen: true,
    awaitOnMenuBuilt: {
      id: "context-ask-chat",
    },
  });
  sandbox
    .stub(InlineSpellCheckerUI.mRemote, "dictionaryList")
    .get(() => ["en-US", "nl-NL"]);
  let setterSpy = sandbox.spy();
  sandbox
    .stub(InlineSpellCheckerUI.mRemote, "currentDictionaries")
    .get(() => ["en-US"])
    .set(setterSpy);
  // Re-init the spellcheck items:
  InlineSpellCheckerUI.clearDictionaryListFromMenu();
  gContextMenu.initSpellingItems();

  let dictionaryMenu = document.getElementById("spell-dictionaries-menu");
  let menuOpen = BrowserTestUtils.waitForPopupEvent(dictionaryMenu, "shown");
  dictionaryMenu.parentNode.openMenu(true);
  await menuOpen;
  checkMenu(dictionaryMenu, [
    "spell-check-dictionary-nl-NL",
    true,
    "spell-check-dictionary-en-US",
    true,
    "---",
    null,
    "spell-add-dictionaries",
    true,
  ]);
  is(
    dictionaryMenu.children.length,
    4,
    "Should have 2 dictionaries, a separator and 'add more dictionaries' item in the menu."
  );
  Assert.deepEqual(
    Array.from(
      dictionaryMenu.querySelectorAll("[data-locale-code]"),
      item => item.accessKey
    ),
    ["1", "2"],
    "Each dictionary gets a numbered accesskey"
  );

  let dictionaryEventPromise = BrowserTestUtils.waitForEvent(
    document,
    "spellcheck-changed"
  );
  dictionaryMenu.activateItem(
    dictionaryMenu.querySelector("[data-locale-code*=nl]")
  );
  let event = await dictionaryEventPromise;
  Assert.deepEqual(
    event.detail?.dictionaries,
    ["en-US", "nl-NL"],
    "Should have sent right dictionaries with event."
  );
  ok(setterSpy.called, "Should have set currentDictionaries");
  Assert.deepEqual(
    setterSpy.firstCall?.args,
    [["en-US", "nl-NL"]],
    "Should have called setter with single argument array of 2 dictionaries."
  );
  // Allow for the menu to potentially close:
  await new Promise(r => Services.tm.dispatchToMainThread(r));
  // Check it hasn't:
  is(
    dictionaryMenu.closest("menupopup").state,
    "open",
    "Main menu should still be open."
  );
  contextMenu.hidePopup();
});

add_task(async function test_cleanup() {
  BrowserTestUtils.removeTab(gBrowser.selectedTab);
});
