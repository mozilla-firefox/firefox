/* Any copyright is dedicated to the Public Domain.
   http://creativecommons.org/publicdomain/zero/1.0/ */

// Tests that content analysis is consulted when web content copies data to
// the clipboard, that the content process waits for the verdict so the copy is
// complete by the time execCommand("copy") returns, and that a blocked copy
// replaces the clipboard contents with a notice.

"use strict";

let mockCA = makeMockContentAnalysis();

add_setup(async function test_setup() {
  mockCA = await mockContentAnalysisService(mockCA);
});

const PAGE_URL =
  "https://example.com/browser/toolkit/components/contentanalysis/tests/browser/clipboard_copy.html";
const LINK_URL = "https://example.com/some/link";
// What selecting all of #copySource puts on the clipboard.
const COPIED_PLAIN_TEXT = "Some bold text";
const PREVIOUS_CLIPBOARD_TEXT = "Previous clipboard contents";
// Must match contentanalysis-clipboard-copy-blocked-replacement in
// toolkit/locales/en-US/toolkit/contentanalysis/contentanalysis.ftl. A blocked
// copy replaces the clipboard contents with this rather than leaving the
// previous contents in place, so the user can't silently paste stale data.
const REPLACEMENT_TEXT =
  "Copying this content is restricted by your organization.";

function setClipboardText(clipboardString) {
  const trans = Cc["@mozilla.org/widget/transferable;1"].createInstance(
    Ci.nsITransferable
  );
  trans.init(null);
  trans.addDataFlavor("text/plain");
  const str = Cc["@mozilla.org/supports-string;1"].createInstance(
    Ci.nsISupportsString
  );
  str.data = clipboardString;
  trans.setTransferData("text/plain", str);

  // No window context, so this chrome write is not analyzed.
  Services.clipboard.setData(trans, null, Ci.nsIClipboard.kGlobalClipboard);
}

function getClipboardText() {
  const trans = Cc["@mozilla.org/widget/transferable;1"].createInstance(
    Ci.nsITransferable
  );
  trans.init(null);
  trans.addDataFlavor("text/plain");
  let data = {};
  try {
    // getData can itself fail when the mock CA service is set up to error,
    // since this chrome read goes through the paste check too.
    Services.clipboard.getData(
      trans,
      Ci.nsIClipboard.kGlobalClipboard,
      window.browsingContext.currentWindowContext
    );
    trans.getTransferData("text/plain", data);
  } catch (e) {
    return "";
  }
  return data.value.QueryInterface(Ci.nsISupportsString).data;
}

function assertCopyRequest(request, expectedText, expectedRequestsCount) {
  is(request.url.spec, PAGE_URL, "request has correct URL");
  is(
    request.analysisType,
    Ci.nsIContentAnalysisRequest.eDataCopied,
    "request has correct analysisType"
  );
  is(
    request.reason,
    Ci.nsIContentAnalysisRequest.eClipboardCopy,
    "request has correct reason"
  );
  is(
    request.operationTypeForDisplay,
    Ci.nsIContentAnalysisRequest.eCopyClipboard,
    "request has correct operationTypeForDisplay"
  );
  is(request.filePath, "", "request filePath should be empty");
  if (expectedText !== null) {
    is(request.textContent, expectedText, "request textContent should match");
  }
  is(
    request.userActionRequestsCount,
    expectedRequestsCount,
    "request userActionRequestsCount should match"
  );
  ok(request.userActionId.length, "request userActionId should not be empty");
  is(request.getPrintData().length, 0, "request should have no print data");
  ok(!!request.requestToken.length, "request requestToken should not be empty");
}

function waitForCACalls(count) {
  return TestUtils.waitForCondition(
    () => mockCA.calls.length >= count,
    `waiting for ${count} content analysis call(s)`
  );
}

// Copies from content processes block until the verdict is in, so their tests
// can check the clipboard directly. Copies that start in the parent process
// (e.g. the context menu's Copy Link) are analyzed asynchronously with no
// completion signal, so wait for the clipboard to settle instead.
function waitForClipboardText(expected) {
  return TestUtils.waitForCondition(
    () => getClipboardText() === expected,
    `waiting for clipboard to contain "${expected}"`
  );
}

async function withCopyEnabled(prefs, testFn) {
  await SpecialPowers.pushPrefEnv({
    set: [
      [
        "browser.contentanalysis.interception_point.clipboard_copy.enabled",
        true,
      ],
      ["dom.events.testing.asyncClipboard", true],
      ...prefs,
    ],
  });
  try {
    await testFn();
  } finally {
    await SpecialPowers.popPrefEnv();
  }
}

async function openTestPage() {
  let tab = await BrowserTestUtils.openNewForegroundTab(gBrowser, PAGE_URL);
  await SimpleTest.promiseFocus(tab.linkedBrowser);
  return tab;
}

// Test that document.execCommand("copy") is scanned

// What to select in #copySource before copying, and what the analyzed plain
// text and HTML should then look like. `range` is null to select the whole
// element, otherwise the arguments to selectPartOfCopySource() in the page.
// Partial selections check that what gets analyzed is what was selected, not
// the element's whole contents.
const WHOLE_SOURCE = {
  range: null,
  text: COPIED_PLAIN_TEXT,
  htmlIncludes: ["<b>bold</b>"],
  htmlExcludes: [],
};
const PARTIAL_SOURCE_IN_ONE_TEXT_NODE = {
  range: ["first", 1, "first", 4],
  text: "ome",
  htmlIncludes: ["ome"],
  htmlExcludes: ["<b>", "Some"],
};
const PARTIAL_SOURCE_ACROSS_BOLD = {
  range: ["first", 2, "last", 3],
  text: "me bold te",
  htmlIncludes: ["<b>bold</b>"],
  htmlExcludes: ["Some", "text"],
};

async function testExecCommandCopy(allowCopy, plainTextOnly, selection) {
  await withCopyEnabled(
    [
      [
        "browser.contentanalysis.interception_point.clipboard_copy.plain_text_only",
        plainTextOnly,
      ],
    ],
    async () => {
      mockCA.setupForTest(allowCopy);
      setClipboardText(PREVIOUS_CLIPBOARD_TEXT);

      let tab = await openTestPage();
      let browser = tab.linkedBrowser;

      let execCommandResult = await SpecialPowers.spawn(
        browser,
        [selection.range],
        range => {
          if (range) {
            content.wrappedJSObject.selectPartOfCopySource(...range);
          } else {
            content.wrappedJSObject.selectCopySource();
          }
          return content.document.execCommand("copy");
        }
      );
      is(
        execCommandResult,
        allowCopy,
        "execCommand('copy') reports whether the copy went through"
      );

      // execCommand("copy") should be synchronous, so no need to wait.
      let expectedCalls = plainTextOnly ? 1 : 2;
      is(
        mockCA.calls.length,
        expectedCalls,
        "correct number of calls to content analysis"
      );
      assertCopyRequest(mockCA.calls[0], selection.text, expectedCalls);
      if (!plainTextOnly) {
        // The Windows-specific CF_HTML wrapper is added by the native
        // clipboard code, which runs after the content analysis check, so
        // the analyzed HTML is the bare fragment.
        assertCopyRequest(mockCA.calls[1], null, expectedCalls);
        let html = mockCA.calls[1].textContent;
        for (let expected of selection.htmlIncludes) {
          ok(
            html.includes(expected),
            `HTML request should contain "${expected}", got "${html}"`
          );
        }
        for (let unexpected of selection.htmlExcludes) {
          ok(
            !html.includes(unexpected),
            `HTML request should not contain "${unexpected}", got "${html}"`
          );
        }
        is(
          mockCA.calls[1].userActionId,
          mockCA.calls[0].userActionId,
          "both requests share a user action ID"
        );
      }

      is(
        getClipboardText(),
        allowCopy ? selection.text : REPLACEMENT_TEXT,
        "clipboard has the expected contents as soon as the copy returns"
      );

      BrowserTestUtils.removeTab(tab);
    }
  );
}

add_task(async function testExecCommandCopyAllowPlainTextOnly() {
  await testExecCommandCopy(true, true, WHOLE_SOURCE);
});

add_task(async function testExecCommandCopyAllowAllFormats() {
  await testExecCommandCopy(true, false, WHOLE_SOURCE);
});

add_task(async function testExecCommandCopyBlockPlainTextOnly() {
  await testExecCommandCopy(false, true, WHOLE_SOURCE);
});

add_task(async function testExecCommandCopyBlockAllFormats() {
  await testExecCommandCopy(false, false, WHOLE_SOURCE);
});

add_task(async function testExecCommandCopyPartialTextNodeAllow() {
  await testExecCommandCopy(true, false, PARTIAL_SOURCE_IN_ONE_TEXT_NODE);
});

add_task(async function testExecCommandCopyPartialTextNodeBlock() {
  await testExecCommandCopy(false, true, PARTIAL_SOURCE_IN_ONE_TEXT_NODE);
});

add_task(async function testExecCommandCopyAcrossElementAllow() {
  await testExecCommandCopy(true, false, PARTIAL_SOURCE_ACROSS_BOLD);
});

add_task(async function testExecCommandCopyAcrossElementBlock() {
  await testExecCommandCopy(false, false, PARTIAL_SOURCE_ACROSS_BOLD);
});

// A collapsed selection has nothing to copy, so nothing should be analyzed
// and the clipboard must be left alone rather than replaced with the notice.
add_task(async function testExecCommandCopyCollapsedSelectionNotAnalyzed() {
  await withCopyEnabled([], async () => {
    mockCA.setupForTest(false);
    setClipboardText(PREVIOUS_CLIPBOARD_TEXT);

    let tab = await openTestPage();
    let browser = tab.linkedBrowser;

    let execCommandResult = await SpecialPowers.spawn(browser, [], () => {
      content.wrappedJSObject.selectPartOfCopySource("first", 2, "first", 2);
      return content.document.execCommand("copy");
    });
    info(
      `execCommand('copy') with a collapsed selection: ${execCommandResult}`
    );

    is(mockCA.calls.length, 0, "no content analysis calls for an empty copy");
    is(
      getClipboardText(),
      PREVIOUS_CLIPBOARD_TEXT,
      "the clipboard is untouched by an empty copy"
    );

    BrowserTestUtils.removeTab(tab);
  });
});

// Ctrl+C / Ctrl+X in a text field. These go through the editor rather than
// nsCopySupport's document-selection path, so they are worth covering
// separately from execCommand("copy") above.

// `partial` selects one word out of the middle of the field, so tests can
// check that only the selected text is analyzed, copied and (for cuts)
// deleted.
const TEXT_FIELDS = [
  {
    id: "testInput",
    text: "Text field contents",
    partial: { start: 5, end: 10, text: "field" },
  },
  {
    id: "testTextArea",
    text: "Text area contents",
    partial: { start: 5, end: 9, text: "area" },
  },
];

async function selectAllIn(browser, elementId) {
  await SpecialPowers.spawn(browser, [elementId], elementId => {
    let element = content.document.getElementById(elementId);
    element.focus();
    element.select();
  });
}

async function selectRangeIn(browser, elementId, start, end, direction) {
  await SpecialPowers.spawn(
    browser,
    [elementId, start, end, direction],
    (elementId, start, end, direction) => {
      let element = content.document.getElementById(elementId);
      element.focus();
      element.setSelectionRange(start, end, direction);
    }
  );
}

async function getFieldState(browser, elementId) {
  return SpecialPowers.spawn(browser, [elementId], elementId => {
    let element = content.document.getElementById(elementId);
    return {
      value: element.value,
      selectionStart: element.selectionStart,
      selectionEnd: element.selectionEnd,
    };
  });
}

// Selects all of `field`, or just its `partial` word when `partial` is set,
// and returns the text that should be copied.
async function selectInField(browser, field, { partial, direction } = {}) {
  if (!partial) {
    await selectAllIn(browser, field.id);
    return field.text;
  }
  await selectRangeIn(
    browser,
    field.id,
    field.partial.start,
    field.partial.end,
    direction
  );
  return field.partial.text;
}

async function testTextFieldCopy(allowCopy, selectionOptions) {
  await withCopyEnabled([], async () => {
    for (let field of TEXT_FIELDS) {
      mockCA.setupForTest(allowCopy);
      setClipboardText(PREVIOUS_CLIPBOARD_TEXT);

      let tab = await openTestPage();
      let browser = tab.linkedBrowser;

      let selectedText = await selectInField(browser, field, selectionOptions);
      await BrowserTestUtils.synthesizeKey("c", { accelKey: true }, browser);

      is(
        mockCA.calls.length,
        1,
        `${field.id}: one call to content analysis for Ctrl+C`
      );
      assertCopyRequest(mockCA.calls[0], selectedText, 1);

      is(
        getClipboardText(),
        allowCopy ? selectedText : REPLACEMENT_TEXT,
        `${field.id}: clipboard has the expected contents after Ctrl+C`
      );

      // Copying never modifies the field.
      let { value } = await getFieldState(browser, field.id);
      is(value, field.text, `${field.id}: still has its contents after Ctrl+C`);

      BrowserTestUtils.removeTab(tab);
    }
  });
}

add_task(async function testTextFieldCopyAllow() {
  await testTextFieldCopy(true);
});

add_task(async function testTextFieldCopyBlock() {
  await testTextFieldCopy(false);
});

add_task(async function testTextFieldPartialCopyAllow() {
  await testTextFieldCopy(true, { partial: true });
});

add_task(async function testTextFieldPartialCopyBlock() {
  await testTextFieldCopy(false, { partial: true });
});

// A selection made right-to-left has selectionStart > the anchor; make sure
// that doesn't change what gets analyzed.
add_task(async function testTextFieldBackwardPartialCopyAllow() {
  await testTextFieldCopy(true, { partial: true, direction: "backward" });
});

// Ctrl+C with only a caret in the field copies nothing, so nothing should be
// analyzed and the clipboard must be left alone.
add_task(async function testTextFieldCaretOnlyCopyNotAnalyzed() {
  await withCopyEnabled([], async () => {
    mockCA.setupForTest(false);
    setClipboardText(PREVIOUS_CLIPBOARD_TEXT);

    let tab = await openTestPage();
    let browser = tab.linkedBrowser;

    await selectRangeIn(browser, "testInput", 3, 3);
    await BrowserTestUtils.synthesizeKey("c", { accelKey: true }, browser);

    is(mockCA.calls.length, 0, "no content analysis calls for an empty copy");
    is(
      getClipboardText(),
      PREVIOUS_CLIPBOARD_TEXT,
      "the clipboard is untouched by an empty copy"
    );

    BrowserTestUtils.removeTab(tab);
  });
});

// Ctrl+X in #testInput. When the copy is blocked, the content process learns
// the verdict before deciding whether to delete, so the text must stay put --
// otherwise it would be gone with nothing having reached the clipboard. When
// allowed, exactly the selected text is deleted.
async function testTextFieldCut(allowCopy, selectionOptions) {
  await withCopyEnabled([], async () => {
    mockCA.setupForTest(allowCopy);
    setClipboardText(PREVIOUS_CLIPBOARD_TEXT);

    let tab = await openTestPage();
    let browser = tab.linkedBrowser;

    let field = TEXT_FIELDS[0];
    let selectedText = await selectInField(browser, field, selectionOptions);
    await BrowserTestUtils.synthesizeKey("x", { accelKey: true }, browser);

    is(mockCA.calls.length, 1, "one call to content analysis for Ctrl+X");
    assertCopyRequest(mockCA.calls[0], selectedText, 1);

    let state = await getFieldState(browser, field.id);
    if (allowCopy) {
      is(getClipboardText(), selectedText, "the cut text was copied");
      let start = field.text.indexOf(selectedText);
      is(
        state.value,
        field.text.slice(0, start) +
          field.text.slice(start + selectedText.length),
        "allowed Ctrl+X deletes exactly the selected text"
      );
      is(state.selectionStart, start, "caret is where the cut text was");
      is(state.selectionEnd, start, "nothing is selected after the cut");
    } else {
      is(
        getClipboardText(),
        REPLACEMENT_TEXT,
        "the blocked content did not reach the clipboard"
      );
      is(
        state.value,
        field.text,
        "blocked Ctrl+X does not delete the field contents"
      );
    }

    BrowserTestUtils.removeTab(tab);
  });
}

add_task(async function testTextFieldCutBlocked() {
  await testTextFieldCut(false);
});

add_task(async function testTextFieldCutAllowed() {
  await testTextFieldCut(true);
});

add_task(async function testTextFieldPartialCutBlocked() {
  await testTextFieldCut(false, { partial: true });
});

add_task(async function testTextFieldPartialCutAllowed() {
  await testTextFieldCut(true, { partial: true });
});

// navigator.clipboard.writeText()

async function testWriteText(allowCopy) {
  await withCopyEnabled([], async () => {
    mockCA.setupForTest(allowCopy);
    setClipboardText(PREVIOUS_CLIPBOARD_TEXT);

    let tab = await openTestPage();
    let browser = tab.linkedBrowser;

    let result = await SpecialPowers.spawn(
      browser,
      [COPIED_PLAIN_TEXT],
      async text => {
        try {
          await content.navigator.clipboard.writeText(text);
          return "resolved";
        } catch (e) {
          return e.name;
        }
      }
    );

    is(mockCA.calls.length, 1, "one call to content analysis");
    assertCopyRequest(mockCA.calls[0], COPIED_PLAIN_TEXT, 1);

    if (allowCopy) {
      is(result, "resolved", "writeText() resolves when the copy is allowed");
      is(getClipboardText(), COPIED_PLAIN_TEXT, "clipboard has the new text");
    } else {
      is(
        result,
        "NotAllowedError",
        "writeText() rejects with NotAllowedError when the copy is blocked"
      );
      await waitForClipboardText(REPLACEMENT_TEXT);
    }

    BrowserTestUtils.removeTab(tab);
  });
}

add_task(async function testWriteTextAllow() {
  await testWriteText(true);
});

add_task(async function testWriteTextBlock() {
  await testWriteText(false);
});

// navigator.clipboard.write() with more than one format

async function testWriteMultipleFormats(plainTextOnly) {
  await withCopyEnabled(
    [
      [
        "browser.contentanalysis.interception_point.clipboard_copy.plain_text_only",
        plainTextOnly,
      ],
    ],
    async () => {
      mockCA.setupForTest(true);
      setClipboardText(PREVIOUS_CLIPBOARD_TEXT);

      let tab = await openTestPage();
      let browser = tab.linkedBrowser;

      let result = await SpecialPowers.spawn(
        browser,
        [COPIED_PLAIN_TEXT],
        async text => {
          const item = new content.ClipboardItem({
            "text/plain": new content.Blob([text], { type: "text/plain" }),
            "text/html": new content.Blob([`<b>${text}</b>`], {
              type: "text/html",
            }),
          });
          try {
            await content.navigator.clipboard.write([item]);
            return "resolved";
          } catch (e) {
            return e.name;
          }
        }
      );
      is(result, "resolved", "write() resolves when the copy is allowed");

      let expectedCalls = plainTextOnly ? 1 : 2;
      is(
        mockCA.calls.length,
        expectedCalls,
        "only plain text is analyzed when plain_text_only is set"
      );
      assertCopyRequest(mockCA.calls[0], COPIED_PLAIN_TEXT, expectedCalls);
      if (!plainTextOnly) {
        // The async clipboard API normalizes an HTML blob into a document.
        assertCopyRequest(
          mockCA.calls[1],
          `<html><head></head><body><b>${COPIED_PLAIN_TEXT}</b></body></html>`,
          expectedCalls
        );
      }

      BrowserTestUtils.removeTab(tab);
    }
  );
}

add_task(async function testWriteMultipleFormatsPlainTextOnly() {
  await testWriteMultipleFormats(true);
});

add_task(async function testWriteMultipleFormatsAllFormats() {
  await testWriteMultipleFormats(false);
});

// Pref gating

add_task(async function testCopyNotAnalyzedWhenPrefOff() {
  await SpecialPowers.pushPrefEnv({
    set: [
      [
        "browser.contentanalysis.interception_point.clipboard_copy.enabled",
        false,
      ],
      ["dom.events.testing.asyncClipboard", true],
    ],
  });
  mockCA.setupForTest(false);
  setClipboardText(PREVIOUS_CLIPBOARD_TEXT);

  let tab = await openTestPage();
  let browser = tab.linkedBrowser;

  let result = await SpecialPowers.spawn(
    browser,
    [COPIED_PLAIN_TEXT],
    async text => {
      try {
        await content.navigator.clipboard.writeText(text);
        return "resolved";
      } catch (e) {
        return e.name;
      }
    }
  );

  is(result, "resolved", "copy succeeds when the interception point is off");
  is(mockCA.calls.length, 0, "no content analysis calls for copies");
  is(getClipboardText(), COPIED_PLAIN_TEXT, "clipboard has the new text");

  // Paste analysis is gated independently and still runs.
  mockCA.clearCalls();
  await SpecialPowers.spawn(browser, [], async () => {
    content.document.getElementById("pasteTarget").focus();
  });
  await BrowserTestUtils.synthesizeKey("v", { accelKey: true }, browser);
  await waitForCACalls(1);
  is(
    mockCA.calls[0].reason,
    Ci.nsIContentAnalysisRequest.eClipboardPaste,
    "paste is still analyzed"
  );

  BrowserTestUtils.removeTab(tab);
  await SpecialPowers.popPrefEnv();
});

// The same-tab bypass must not apply to copies: the source of copied data is
// always the copying window itself.

add_task(async function testCopyIgnoresSameTabBypass() {
  await withCopyEnabled(
    [["browser.contentanalysis.bypass_for_same_tab_operations", true]],
    async () => {
      mockCA.setupForTest(false);
      setClipboardText(PREVIOUS_CLIPBOARD_TEXT);

      let tab = await openTestPage();
      let browser = tab.linkedBrowser;

      let result = await SpecialPowers.spawn(
        browser,
        [COPIED_PLAIN_TEXT],
        async text => {
          try {
            await content.navigator.clipboard.writeText(text);
            return "resolved";
          } catch (e) {
            return e.name;
          }
        }
      );

      is(
        mockCA.calls.length,
        1,
        "copy is analyzed despite the same-tab bypass"
      );
      assertCopyRequest(mockCA.calls[0], COPIED_PLAIN_TEXT, 1);
      is(result, "NotAllowedError", "the copy was blocked");
      await waitForClipboardText(REPLACEMENT_TEXT);

      BrowserTestUtils.removeTab(tab);
    }
  );
});

// The same-tab bypass only covers the copying page: after the tab navigates
// to another URL on the same origin, a paste of the same clipboard data is
// analyzed again. copyInterception says whether the copy itself goes through
// content analysis first.
async function testSameTabBypassEndsWithNavigation(copyInterception) {
  await SpecialPowers.pushPrefEnv({
    set: [
      [
        "browser.contentanalysis.interception_point.clipboard_copy.enabled",
        copyInterception,
      ],
      ["browser.contentanalysis.bypass_for_same_tab_operations", true],
    ],
  });
  try {
    mockCA.setupForTest(/* shouldAllowRequest */ true);
    setClipboardText(PREVIOUS_CLIPBOARD_TEXT);

    let tab = await openTestPage();
    let browser = tab.linkedBrowser;

    let execCommandResult = await SpecialPowers.spawn(browser, [], () => {
      content.wrappedJSObject.selectCopySource();
      return content.document.execCommand("copy");
    });
    is(execCommandResult, true, "the copy succeeded");
    await waitForClipboardText(COPIED_PLAIN_TEXT);
    is(
      mockCA.calls.length,
      copyInterception ? 1 : 0,
      "the copy was analyzed only with copy interception on"
    );

    let pasteIntoTarget = async () => {
      mockCA.setupForTest(/* shouldAllowRequest */ false);
      await SpecialPowers.spawn(browser, [], () => {
        let target = content.document.getElementById("pasteTarget");
        target.textContent = "";
        target.focus();
        content.getSelection().collapse(target, 0);
      });
      await BrowserTestUtils.synthesizeKey("v", { accelKey: true }, browser);
      return SpecialPowers.spawn(browser, [], () => {
        return content.document.getElementById("pasteTarget").textContent;
      });
    };
    let pasted = await pasteIntoTarget();
    is(pasted, COPIED_PLAIN_TEXT, "the copying page pastes the copy");
    is(mockCA.calls.length, 0, "the same-page paste was not analyzed");

    let navigated = PAGE_URL + "?navigated";
    BrowserTestUtils.startLoadingURIString(browser, navigated);
    await BrowserTestUtils.browserLoaded(browser, false, navigated);

    pasted = await pasteIntoTarget();
    is(pasted, "", "the navigated page's paste is blocked by the check");
    is(mockCA.calls.length, 1, "the paste was analyzed, not bypassed");
    is(
      mockCA.calls[0].reason,
      Ci.nsIContentAnalysisRequest.eClipboardPaste,
      "the request is the paste"
    );
    is(
      mockCA.calls[0].url.spec,
      navigated,
      "the paste request has the new URL"
    );
    is(
      mockCA.calls[0].textContent,
      COPIED_PLAIN_TEXT,
      "the paste request has the copied text"
    );

    BrowserTestUtils.removeTab(tab);
  } finally {
    await SpecialPowers.popPrefEnv();
  }
}

add_task(async function testSameTabBypassEndsWithNavigationCopyAnalyzed() {
  await testSameTabBypassEndsWithNavigation(/* copyInterception */ true);
});

add_task(async function testSameTabBypassEndsWithNavigationCopyNotAnalyzed() {
  await testSameTabBypassEndsWithNavigation(/* copyInterception */ false);
});

// An allowed copy must not pre-approve the paste of the same data.

add_task(async function testAllowedCopyDoesNotSeedPasteCache() {
  await withCopyEnabled([], async () => {
    mockCA.setupForTest(true);
    setClipboardText(PREVIOUS_CLIPBOARD_TEXT);

    let tab = await openTestPage();
    let browser = tab.linkedBrowser;

    await SpecialPowers.spawn(browser, [COPIED_PLAIN_TEXT], async text => {
      await content.navigator.clipboard.writeText(text);
    });
    is(mockCA.calls.length, 1, "the copy was analyzed");
    mockCA.clearCalls();

    await SpecialPowers.spawn(browser, [], async () => {
      content.document.getElementById("pasteTarget").focus();
    });
    await BrowserTestUtils.synthesizeKey("v", { accelKey: true }, browser);
    await waitForCACalls(1);
    is(
      mockCA.calls[0].reason,
      Ci.nsIContentAnalysisRequest.eClipboardPaste,
      "the paste consults the agent again rather than reusing the copy verdict"
    );

    BrowserTestUtils.removeTab(tab);
  });
});

// Two copies in flight at once: the second must win even though the first
// verdict arrives last, otherwise a slow-but-allowed earlier copy would
// overwrite the clipboard with stale data. The async clipboard API is the only
// way a single page can have two copies outstanding, since the execCommand and
// keyboard paths block the content process until the verdict is in.

add_task(async function testRacingCopiesLastWriteWins() {
  await withCopyEnabled([], async () => {
    mockCA.setupForTest(true, /* waitForEvent */ true);
    setClipboardText(PREVIOUS_CLIPBOARD_TEXT);

    let tab = await openTestPage();
    let browser = tab.linkedBrowser;

    // Start the first copy and wait until its check is outstanding.
    let firstStarted = new Promise(res => {
      mockCA.eventTarget.addEventListener("inAnalyzeContentRequest", res, {
        once: true,
      });
    });
    let firstCopy = SpecialPowers.spawn(browser, [], async () => {
      try {
        await content.navigator.clipboard.writeText("first copy");
      } catch (e) {
        return e.name;
      }
      return "resolved";
    });
    await firstStarted;

    // Start a second copy, which supersedes the first.
    let secondStarted = new Promise(res => {
      mockCA.eventTarget.addEventListener("inAnalyzeContentRequest", res, {
        once: true,
      });
    });
    let secondCopy = SpecialPowers.spawn(browser, [], async () => {
      try {
        await content.navigator.clipboard.writeText("second copy");
      } catch (e) {
        return e.name;
      }
      return "resolved";
    });
    await secondStarted;

    // Release both verdicts. Both listeners are registered, so one dispatch
    // frees them; dispatch twice in case they were staggered.
    mockCA.eventTarget.dispatchEvent(
      new CustomEvent("returnContentAnalysisResponse")
    );
    mockCA.eventTarget.dispatchEvent(
      new CustomEvent("returnContentAnalysisResponse")
    );

    let firstResult = await firstCopy;
    is(await secondCopy, "resolved", "the superseding copy resolves");
    isnot(
      firstResult,
      "resolved",
      "the superseded copy does not report success"
    );

    await waitForClipboardText("second copy");
    is(
      getClipboardText(),
      "second copy",
      "the later copy wins regardless of verdict ordering"
    );

    BrowserTestUtils.removeTab(tab);
  });
});

// A copy whose analysis errors out must fail closed: nothing sensitive reaches
// the clipboard. This covers browser.contentanalysis.default_result = block.

add_task(async function testCopyFailsClosedOnAgentError() {
  await withCopyEnabled([], async () => {
    mockCA.setupForTestWithError(Cr.NS_ERROR_NOT_AVAILABLE);
    setClipboardText(PREVIOUS_CLIPBOARD_TEXT);
    // The mock rethrows the error after reporting it, on every request.
    ignoreAllUncaughtExceptions();

    let tab = await openTestPage();
    let browser = tab.linkedBrowser;

    let result = await SpecialPowers.spawn(
      browser,
      [COPIED_PLAIN_TEXT],
      async text => {
        try {
          await content.navigator.clipboard.writeText(text);
          return "resolved";
        } catch (e) {
          return e.name;
        }
      }
    );

    is(
      result,
      "NotAllowedError",
      "a copy whose analysis errors is rejected, not allowed through"
    );
    is(
      getClipboardText(),
      "",
      "the blocked content did not reach the clipboard"
    );

    BrowserTestUtils.removeTab(tab);
  });
});

// The dialog shown for a blocked copy has to talk about copying. Copy reuses
// most of the paste machinery, so without a distinct operationTypeForDisplay it
// would tell the user they aren't permitted to *paste*.

add_task(async function testBlockedCopyDialogSaysCopy() {
  await withCopyEnabled([], async () => {
    mockCA.setupForTest(
      /* shouldAllowRequest */ false,
      /* waitForEvent */ false,
      /* showDialogs */ true
    );
    setClipboardText(PREVIOUS_CLIPBOARD_TEXT);

    let tab = await openTestPage();
    let browser = tab.linkedBrowser;

    let blockDialogPromise = BrowserTestUtils.promiseAlertDialogOpen();

    await selectAllIn(browser, "testInput");
    await BrowserTestUtils.synthesizeKey("c", { accelKey: true }, browser);

    let win = await blockDialogPromise;
    let title = win.document.getElementById("infoTitle").textContent;
    let body = win.document.getElementById("infoBody").textContent;
    info(`blocked copy dialog title: "${title}"`);
    info(`blocked copy dialog body: "${body}"`);

    ok(/copy/i.test(title), `title mentions copying, got "${title}"`);
    ok(!/paste/i.test(title), `title does not mention pasting, got "${title}"`);
    ok(/copy/i.test(body), `body mentions copying, got "${body}"`);
    ok(!/paste/i.test(body), `body does not mention pasting, got "${body}"`);

    win.document.querySelector("dialog").getButton("accept").click();

    BrowserTestUtils.removeTab(tab);
  });
});

// Context menu "Copy Link". This copy starts in the parent process, where the
// check stays asynchronous, so unlike the content-process paths above there is
// nothing to wait on other than the clipboard itself.

async function testCopyLink(allowCopy) {
  await withCopyEnabled([], async () => {
    mockCA.setupForTest(allowCopy);
    setClipboardText(PREVIOUS_CLIPBOARD_TEXT);

    let tab = await openTestPage();
    let browser = tab.linkedBrowser;

    let contextMenu = document.getElementById("contentAreaContextMenu");
    let popupShown = BrowserTestUtils.waitForEvent(contextMenu, "popupshown");
    await BrowserTestUtils.synthesizeMouseAtCenter(
      "#copyLink",
      { type: "contextmenu", button: 2 },
      browser
    );
    await popupShown;

    let popupHidden = BrowserTestUtils.waitForEvent(contextMenu, "popuphidden");
    contextMenu.activateItem(document.getElementById("context-copylink"));
    await popupHidden;

    await waitForCACalls(1);
    is(mockCA.calls.length, 1, "one call to content analysis");
    assertCopyRequest(mockCA.calls[0], LINK_URL, 1);

    await waitForClipboardText(allowCopy ? LINK_URL : REPLACEMENT_TEXT);

    BrowserTestUtils.removeTab(tab);
  });
}

add_task(async function testCopyLinkAllow() {
  await testCopyLink(true);
});

add_task(async function testCopyLinkBlock() {
  await testCopyLink(false);
});

// Chrome-initiated copies are exempt.

add_task(async function testChromeCopyNotAnalyzed() {
  await withCopyEnabled([], async () => {
    mockCA.setupForTest(false);
    setClipboardText(PREVIOUS_CLIPBOARD_TEXT);

    const trans = Cc["@mozilla.org/widget/transferable;1"].createInstance(
      Ci.nsITransferable
    );
    trans.init(null);
    trans.addDataFlavor("text/plain");
    const str = Cc["@mozilla.org/supports-string;1"].createInstance(
      Ci.nsISupportsString
    );
    str.data = COPIED_PLAIN_TEXT;
    trans.setTransferData("text/plain", str);

    Services.clipboard.setData(
      trans,
      null,
      Ci.nsIClipboard.kGlobalClipboard,
      window.browsingContext.currentWindowContext
    );

    is(mockCA.calls.length, 0, "chrome copies are not sent to the agent");
    is(getClipboardText(), COPIED_PLAIN_TEXT, "the chrome copy went through");
  });
});
