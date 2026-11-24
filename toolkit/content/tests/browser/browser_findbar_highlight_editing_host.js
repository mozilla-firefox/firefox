/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

// While it has focus, the editing host keeps the selection at its own caret,
// like script based code editors do.
const PAGE = `
  <!doctype html>
  <p>case before</p>
  <div id="editor" contenteditable><div>lowercase one</div><div>uppercase two</div></div>
  <p>case after</p>
  <script>
    document.addEventListener("selectionchange", () => {
      const selection = getSelection();
      if (
        document.activeElement == editor &&
        !(selection.isCollapsed && selection.anchorNode == editor)
      ) {
        selection.collapse(editor, 0);
      }
    });
  </script>
`;

function promiseFindAgain(findbar) {
  return new Promise(resolve => {
    const listener = {
      onFindResult() {},
      onCurrentSelection() {},
      onMatchesCountResult() {},
      onHighlightFinished() {
        findbar.browser.finder.removeResultListener(listener);
        resolve();
      },
    };
    findbar.browser.finder.addResultListener(listener);
    findbar.onFindAgainCommand(false);
  });
}

function getState(browser) {
  return SpecialPowers.spawn(browser, [], () => {
    const selection = content.getSelection();
    const range = selection.getRangeAt(0);
    return {
      match: range.collapsed
        ? "collapsed"
        : `${range.startContainer.data}@${range.startOffset}`,
      editorFocused:
        content.document.activeElement ==
        content.document.getElementById("editor"),
    };
  });
}

add_task(async function test_find_next_with_highlight_all_in_editing_host() {
  const tab = await BrowserTestUtils.openNewForegroundTab(
    gBrowser,
    "data:text/html;charset=utf-8," + encodeURIComponent(PAGE)
  );
  const browser = tab.linkedBrowser;
  const findbar = await gBrowser.getFindBar();

  await SpecialPowers.pushPrefEnv({ set: [["findbar.highlightAll", true]] });
  await promiseFindFinished(gBrowser, "case", true);
  const states = [await getState(browser)];
  for (let i = 0; i < 3; ++i) {
    await promiseFindAgain(findbar);
    states.push(await getState(browser));
  }

  Assert.deepEqual(
    states.map(state => state.match),
    ["case before@0", "lowercase one@5", "uppercase two@5", "case after@0"],
    "Find next should advance through the matches in the editing host"
  );
  ok(
    states.every(state => !state.editorFocused),
    "Highlighting the matches should not focus the editing host"
  );

  await closeFindbarAndWait(findbar);
  BrowserTestUtils.removeTab(tab);
});
