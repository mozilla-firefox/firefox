/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

const PREF_RIGHT_CLICK_SELECTS = "ui.mouse.right_click.select_under_cursor";
const PREF_EDITABLE = "ui.mouse.right_click.select_in_editable";
const PREF_EAT_SPACE = "layout.word_select.eat_space_to_next_word";

const TEST_HTML = `<!doctype html>
<title>Right-click selection</title>
<style>
  #noselect { user-select: none; }
  .all { user-select: all; }
</style>
  <p id="p1">Test Page <a id="link1" href="https://example.com">link text here</a> additional</p>
  <span id="plain">some plain selectable text</span>
  <div id="noselect">Unselectable box with <a id="nlink" href="https://example.com">link inside</a></div>
  <div class="all">user-select: all ancestor: <a id="alink" href="https://example.com">link text here</a></div>
 `;

const EDITABLE_HTML = `<!doctype html>
<title>Editable right-click</title>
<div id="ed" contenteditable>hello world</div>
`;
const EDITABLE_PAGE = "data:text/html," + encodeURIComponent(EDITABLE_HTML);
const TEST_PAGE = "data:text/html," + encodeURIComponent(TEST_HTML);

// close context menu
async function closeContextMenu(contextMenuEl) {
  const menuHidden = BrowserTestUtils.waitForEvent(
    contextMenuEl,
    "popuphidden"
  );
  contextMenuEl.hidePopup();
  await menuHidden;
}

function assertSelectionItemsHiddenOnLink(contextMenuEl) {
  for (let id of [
    "context-copy",
    "context-print-selection",
    "context-copy-link-to-highlight",
    "context-copy-clean-link-to-highlight",
  ]) {
    ok(
      !BrowserTestUtils.isVisible(contextMenuEl.querySelector(`#${id}`)),
      `${id} is hidden on a link`
    );
  }
}

// Right-click the center of a word
async function rightClickWord(tabBrowser, elementSelector, targetWord) {
  const { x, y } = await SpecialPowers.spawn(
    tabBrowser,
    [elementSelector, targetWord],
    (selector, word) => {
      const doc = content.document;
      const element = doc.querySelector(selector);
      const textNode = element.firstChild;
      const fullText = textNode.textContent;
      const startIndex = fullText.indexOf(word);
      const range = doc.createRange();
      range.setStart(textNode, startIndex);
      range.setEnd(textNode, startIndex + word.length);
      const wordRect = range.getBoundingClientRect();
      content.focus();
      return {
        x: wordRect.left + wordRect.width / 2,
        y: wordRect.top + wordRect.height / 2,
      };
    }
  );
  // Both events are needed: the mousedown moves the caret the way a real
  // right click does, and the contextmenu is what selects and opens the menu.
  await BrowserTestUtils.synthesizeMouseAtPoint(
    x,
    y,
    { type: "mousedown", button: 2 },
    tabBrowser
  );
  await BrowserTestUtils.synthesizeMouseAtPoint(
    x,
    y,
    { type: "contextmenu", button: 2 },
    tabBrowser
  );
}

async function openContextMenuOnWord(tabBrowser, elementSelector, targetWord) {
  const contextMenuEl = document.getElementById("contentAreaContextMenu");
  const menuShown = BrowserTestUtils.waitForEvent(contextMenuEl, "popupshown");
  await rightClickWord(tabBrowser, elementSelector, targetWord);
  await menuShown;
  return contextMenuEl;
}

async function cancelContextMenuEventsInPage(tabBrowser) {
  await SpecialPowers.spawn(tabBrowser, [], () => {
    content.addEventListener("contextmenu", e => e.preventDefault());
  });
}

async function getSelectionText(tabBrowser) {
  return SpecialPowers.spawn(tabBrowser, [], () =>
    content.getSelection().toString()
  );
}

add_setup(async function () {
  await SpecialPowers.pushPrefEnv({
    set: [[PREF_EAT_SPACE, false]],
  });
});

add_task(async function right_click_plain_text_selects_clicked_word() {
  await SpecialPowers.pushPrefEnv({ set: [[PREF_RIGHT_CLICK_SELECTS, true]] });
  await BrowserTestUtils.withNewTab(TEST_PAGE, async tabBrowser => {
    const contextMenuEl = await openContextMenuOnWord(
      tabBrowser,
      "#plain",
      "plain"
    );
    const selection = await getSelectionText(tabBrowser);
    is(selection, "plain", "Right-click selected just the clicked word");
    const copy = contextMenuEl.querySelector("#context-copy");
    const copyLink = contextMenuEl.querySelector("#context-copylink");
    ok(BrowserTestUtils.isVisible(copy), "Copy is visible");
    ok(!copy.hasAttribute("disabled"), "Copy is enabled");
    ok(
      !BrowserTestUtils.isVisible(copyLink),
      "Copy Link is hidden on non-link text"
    );
    await closeContextMenu(contextMenuEl);
  });
});

add_task(async function user_select_none_container_does_not_auto_select() {
  await SpecialPowers.pushPrefEnv({ set: [[PREF_RIGHT_CLICK_SELECTS, true]] });
  await BrowserTestUtils.withNewTab(TEST_PAGE, async tabBrowser => {
    const contextMenuEl = await openContextMenuOnWord(
      tabBrowser,
      "#nlink",
      "link"
    );
    const selection = await getSelectionText(tabBrowser);
    is(selection, "", "No selection created in user-select:none region");
    const copyLink = contextMenuEl.querySelector("#context-copylink");
    ok(
      BrowserTestUtils.isVisible(copyLink),
      "Copy Link visible for link in noselect"
    );
    await closeContextMenu(contextMenuEl);
  });
});

add_task(async function right_click_link_text_shows_copy_link_only() {
  await SpecialPowers.pushPrefEnv({ set: [[PREF_RIGHT_CLICK_SELECTS, true]] });
  await BrowserTestUtils.withNewTab(TEST_PAGE, async tabBrowser => {
    const contextMenuEl = await openContextMenuOnWord(
      tabBrowser,
      "#link1",
      "link"
    );
    const selection = await getSelectionText(tabBrowser);
    is(selection, "link text here", "Right-click selected whole link text");
    const copyLink = contextMenuEl.querySelector("#context-copylink");
    assertSelectionItemsHiddenOnLink(contextMenuEl);
    ok(
      BrowserTestUtils.isVisible(copyLink),
      "Copy Link is visible for link text"
    );
    await closeContextMenu(contextMenuEl);
  });
});

add_task(async function right_click_moves_selection_to_clicked_word() {
  await SpecialPowers.pushPrefEnv({ set: [[PREF_RIGHT_CLICK_SELECTS, true]] });
  await BrowserTestUtils.withNewTab(TEST_PAGE, async tabBrowser => {
    const contextMenuEl1 = await openContextMenuOnWord(
      tabBrowser,
      "#plain",
      "text"
    );
    await closeContextMenu(contextMenuEl1);

    const contextMenuEl2 = await openContextMenuOnWord(
      tabBrowser,
      "#plain",
      "plain"
    );
    const selection = await getSelectionText(tabBrowser);
    is(selection, "plain", "Selection moved to the clicked word");
    const copy = contextMenuEl2.querySelector("#context-copy");
    ok(BrowserTestUtils.isVisible(copy), "Copy visible");
    await closeContextMenu(contextMenuEl2);
  });
});

add_task(
  async function right_click_does_not_move_existing_selection_if_already_selected() {
    await SpecialPowers.pushPrefEnv({
      set: [[PREF_RIGHT_CLICK_SELECTS, true]],
    });
    await BrowserTestUtils.withNewTab(TEST_PAGE, async tabBrowser => {
      await SpecialPowers.spawn(tabBrowser, [], () => {
        const doc = content.document;
        const element = doc.getElementById("plain");
        const range = doc.createRange();
        range.selectNodeContents(element);
        const selection = content.getSelection();
        selection.removeAllRanges();
        selection.addRange(range);
      });
      const menu = await openContextMenuOnWord(tabBrowser, "#plain", "plain");
      const selection = await getSelectionText(tabBrowser);
      is(
        selection,
        "some plain selectable text",
        "Right-click does not move an existing selection if already selected"
      );
      const copy = menu.querySelector("#context-copy");
      ok(
        BrowserTestUtils.isVisible(copy),
        "Copy visible on existing selection"
      );
      await closeContextMenu(menu);
    });
  }
);

add_task(async function pref_off_does_not_select_plain_or_link() {
  await SpecialPowers.pushPrefEnv({ set: [[PREF_RIGHT_CLICK_SELECTS, false]] });
  await BrowserTestUtils.withNewTab(TEST_PAGE, async tabBrowser => {
    let menu = await openContextMenuOnWord(tabBrowser, "#plain", "plain");
    let sel = await getSelectionText(tabBrowser);
    is(sel, "", "pref=false: no selection on plain text");
    ok(
      !BrowserTestUtils.isVisible(menu.querySelector("#context-copy")),
      "pref=false: Copy hidden without selection on plain text"
    );
    await closeContextMenu(menu);

    menu = await openContextMenuOnWord(tabBrowser, "#link1", "link");
    sel = await getSelectionText(tabBrowser);
    is(sel, "", "pref=false: no selection on link text");
    ok(
      BrowserTestUtils.isVisible(menu.querySelector("#context-copylink")),
      "pref=false: Copy Link still visible on link"
    );
    await closeContextMenu(menu);
  });
});

add_task(async function page_canceling_contextmenu_does_not_select() {
  await SpecialPowers.pushPrefEnv({ set: [[PREF_RIGHT_CLICK_SELECTS, true]] });
  await BrowserTestUtils.withNewTab(TEST_PAGE, async tabBrowser => {
    await cancelContextMenuEventsInPage(tabBrowser);

    await rightClickWord(tabBrowser, "#plain", "plain");
    is(
      await getSelectionText(tabBrowser),
      "",
      "Canceled contextmenu: no selection on plain text"
    );

    await rightClickWord(tabBrowser, "#link1", "link");
    is(
      await getSelectionText(tabBrowser),
      "",
      "Canceled contextmenu: no selection on link text"
    );
  });
});

add_task(async function page_cannot_cancel_selection_if_contextmenu_disabled() {
  await SpecialPowers.pushPrefEnv({
    set: [
      [PREF_RIGHT_CLICK_SELECTS, true],
      ["dom.event.contextmenu.enabled", false],
    ],
  });
  await BrowserTestUtils.withNewTab(TEST_PAGE, async tabBrowser => {
    await cancelContextMenuEventsInPage(tabBrowser);
    const menu = await openContextMenuOnWord(tabBrowser, "#plain", "plain");
    is(
      await getSelectionText(tabBrowser),
      "plain",
      "dom.event.contextmenu.enabled=false: the word is still selected"
    );
    await closeContextMenu(menu);
  });
});

add_task(async function user_select_all_ancestor_selects_ancestor_contents() {
  await SpecialPowers.pushPrefEnv({ set: [[PREF_RIGHT_CLICK_SELECTS, true]] });
  await BrowserTestUtils.withNewTab(TEST_PAGE, async tabBrowser => {
    const menu = await openContextMenuOnWord(tabBrowser, "#alink", "link");
    const sel = await getSelectionText(tabBrowser);
    is(
      sel,
      "link text here",
      "Select the nearest user-select:all ancestor's contents"
    );
    assertSelectionItemsHiddenOnLink(menu);
    ok(
      BrowserTestUtils.isVisible(menu.querySelector("#context-copylink")),
      "Copy Link visible on link"
    );
    await closeContextMenu(menu);
  });
});

add_task(async function editable_pref_off_does_not_select_word() {
  await SpecialPowers.pushPrefEnv({
    set: [
      [PREF_RIGHT_CLICK_SELECTS, true],
      [PREF_EDITABLE, false],
    ],
  });
  await BrowserTestUtils.withNewTab(EDITABLE_PAGE, async tabBrowser => {
    const menu = await openContextMenuOnWord(tabBrowser, "#ed", "world");
    const sel = await getSelectionText(tabBrowser);
    is(sel, "", "pref=false: no text selection should be created in editable");
    await closeContextMenu(menu);
  });
});

add_task(async function editable_pref_on_selects_word() {
  await SpecialPowers.pushPrefEnv({
    set: [
      [PREF_RIGHT_CLICK_SELECTS, true],
      [PREF_EDITABLE, true],
    ],
  });
  await BrowserTestUtils.withNewTab(EDITABLE_PAGE, async tabBrowser => {
    const menu = await openContextMenuOnWord(tabBrowser, "#ed", "world");
    const sel = await getSelectionText(tabBrowser);
    is(
      sel,
      "world",
      "pref=true: right-click selects the clicked word in editable"
    );
    ok(
      BrowserTestUtils.isVisible(menu.querySelector("#context-copy")),
      "pref=true: Copy visible with a selection"
    );
    await closeContextMenu(menu);
  });
});

add_task(async function editable_existing_selection_is_preserved() {
  await SpecialPowers.pushPrefEnv({
    set: [
      [PREF_RIGHT_CLICK_SELECTS, true],
      [PREF_EDITABLE, true],
    ],
  });
  await BrowserTestUtils.withNewTab(EDITABLE_PAGE, async tabBrowser => {
    await SpecialPowers.spawn(tabBrowser, [], () => {
      const el = content.document.getElementById("ed").firstChild;
      const r = content.document.createRange();
      const txt = el.data;
      const start = txt.indexOf("hello");
      r.setStart(el, start);
      r.setEnd(el, start + "hello world".length);
      const sel = content.getSelection();
      sel.removeAllRanges();
      sel.addRange(r);
    });

    const menu = await openContextMenuOnWord(tabBrowser, "#ed", "hello");
    const sel = await getSelectionText(tabBrowser);
    is(
      sel,
      "hello world",
      "existing non-collapsed selection in editable is preserved"
    );
    await closeContextMenu(menu);
  });
});
