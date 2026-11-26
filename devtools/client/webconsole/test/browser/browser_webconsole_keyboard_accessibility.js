/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/ */

// Check that basic keyboard shortcuts work in the web console.

"use strict";

const HTML_FILENAME = `test.html`;
const HTML_CONTENT = `<!DOCTYPE html><p>Test keyboard accessibility</p>
  <script>
    for (let i = 1; i <= 100; i++) {
      console.log("console message " + i);
    }
    function logTrace() {
      const sub = () => console.trace("console trace message");
      sub();
    }
    logTrace();
  </script>
  `;

const TRACE_FRAME_LINE_REGEX = /test\.html:\d+:?\d*/;

const httpServer = createTestHTTPServer();
httpServer.registerContentType("html", "text/html");
httpServer.registerPathHandler(
  "/" + HTML_FILENAME,
  function (request, response) {
    response.setStatusLine(request.httpVersion, 200, "OK");
    response.write(HTML_CONTENT);
  }
);

const port = httpServer.identity.primaryPort;
const TEST_URI = `http://localhost:${port}/${HTML_FILENAME}`;

add_task(async function () {
  // Force tabfocus for all elements on OSX.
  SpecialPowers.pushPrefEnv({ set: [["accessibility.tabfocus", 7]] });

  const hud = await openNewTabAndConsole(TEST_URI);

  info("Web Console opened");
  const outputScroller = hud.ui.outputScroller;
  const traceMsgNode = await waitFor(
    () => findConsoleAPIMessage(hud, "console trace message"),
    "waiting for all the messages to be displayed",
    100,
    1000
  );

  // wait for all the stacktrace frames to be rendered.
  await waitFor(() =>
    traceMsgNode.querySelector(".message-body-wrapper > .stacktrace .frames")
  );

  let currentPosition = outputScroller.scrollTop;
  const bottom = currentPosition;
  hud.jsterm.focus();

  info("Check Page up keyboard shortcut");
  EventUtils.synthesizeKey("KEY_PageUp");
  isnot(
    outputScroller.scrollTop,
    currentPosition,
    "scroll position changed after page up"
  );

  info("Check Page down keyboard shortcut");
  currentPosition = outputScroller.scrollTop;
  EventUtils.synthesizeKey("KEY_PageDown");
  Assert.greater(
    outputScroller.scrollTop,
    currentPosition,
    "scroll position now at bottom"
  );

  info("Check Home keyboard shortcut");
  EventUtils.synthesizeKey("KEY_Home");
  is(outputScroller.scrollTop, 0, "scroll position now at top");

  info("Check End keyboard shortcut");
  EventUtils.synthesizeKey("KEY_End");
  const scrollTop = outputScroller.scrollTop;
  ok(
    scrollTop > 0 && Math.abs(scrollTop - bottom) <= 5,
    "scroll position now at bottom"
  );

  info("Hit Shift-Tab to focus switch to editor mode button");
  EventUtils.synthesizeKey("KEY_Tab", { shiftKey: true });
  ok(
    outputScroller.ownerDocument.activeElement.classList.contains(
      "webconsole-input-openEditorButton"
    ),
    "switch to editor mode button is focused"
  );

  info("Check stacktrace frames can be focused");
  EventUtils.synthesizeKey("KEY_Tab", { shiftKey: true });
  EventUtils.synthesizeKey("KEY_ArrowUp");
  ok(
    outputScroller.ownerDocument.activeElement.closest(".message.trace"),
    "The active element is in the trace message"
  );
  is(
    TRACE_FRAME_LINE_REGEX.exec(
      outputScroller.ownerDocument.activeElement.innerText
    )?.[0],
    "test.html:10",
    `last frame of the stacktrace is focused ${outputScroller.ownerDocument.activeElement.innerText}`
  );

  is(
    outputScroller.ownerDocument.activeElement.getAttribute("class"),
    "frame",
    "active element has expected class"
  );

  info("Hit Up Arrow to navigate to second frame of the stacktrace");
  EventUtils.synthesizeKey("KEY_ArrowUp");
  ok(
    outputScroller.ownerDocument.activeElement.closest(".message.trace"),
    "The active element is in the trace message"
  );
  is(
    TRACE_FRAME_LINE_REGEX.exec(
      outputScroller.ownerDocument.activeElement.innerText
    )?.[0],
    "test.html:8",
    "second frame of the stacktrace is focused"
  );
  is(
    outputScroller.ownerDocument.activeElement.getAttribute("class"),
    "frame",
    "active element has expected class"
  );

  info("Hit Up Arrow to navigate to first frame of the stacktrace");
  EventUtils.synthesizeKey("KEY_ArrowUp");
  ok(
    outputScroller.ownerDocument.activeElement.closest(".message.trace"),
    "The active element is in the trace message"
  );
  is(
    TRACE_FRAME_LINE_REGEX.exec(
      outputScroller.ownerDocument.activeElement.innerText
    )?.[0],
    "test.html:7",
    "third frame of the stacktrace is focused"
  );
  is(
    outputScroller.ownerDocument.activeElement.getAttribute("class"),
    "frame",
    "active element has expected class"
  );

  info("Hit Tab to navigate to the message location");
  EventUtils.synthesizeKey("KEY_Tab", { shiftKey: true });
  EventUtils.synthesizeKey("KEY_Tab", { shiftKey: true });
  ok(
    outputScroller.ownerDocument.activeElement.closest(".message.trace"),
    "The active element is in the trace message"
  );
  is(
    outputScroller.ownerDocument.activeElement.getAttribute("class"),
    "frame-link-source",
    "active element is the console trace message location"
  );
  is(
    TRACE_FRAME_LINE_REGEX.exec(
      outputScroller.ownerDocument.activeElement.innerText
    )?.[0],
    "test.html:7:33",
    "active element is expected message location"
  );

  info("Hit Tab to navigate to the previous message location");
  EventUtils.synthesizeKey("KEY_Tab", { shiftKey: true });
  ok(
    !outputScroller.ownerDocument.activeElement.closest(".message.trace"),
    "The active element is not in the trace message"
  );
  is(
    outputScroller.ownerDocument.activeElement.getAttribute("class"),
    "frame-link-source",
    "active element is the console trace message location"
  );
  is(
    TRACE_FRAME_LINE_REGEX.exec(
      outputScroller.ownerDocument.activeElement.innerText
    )[0],
    "test.html:4:15",
    "active element is expected message location"
  );

  info("Clear output");
  hud.jsterm.focus();

  info("try ctrl-l to clear output");
  let clearShortcut;
  if (Services.appinfo.OS === "Darwin") {
    clearShortcut = WCUL10n.getStr("webconsole-clear-key-macos");
  } else {
    clearShortcut = WCUL10n.getStr("webconsole-clear-key");
  }
  synthesizeKeyShortcut(clearShortcut);
  await waitFor(() => !findAllMessages(hud).length);
  ok(isInputFocused(hud), "console was cleared and input is focused");

  if (Services.appinfo.OS === "Darwin") {
    info("Log a new message from the content page");
    const onMessage = waitForMessageByType(
      hud,
      "another simple text message",
      ".console-api"
    );
    SpecialPowers.spawn(gBrowser.selectedBrowser, [], async function () {
      content.console.log("another simple text message");
    });
    await onMessage;

    info("Send Cmd-K to clear console");
    synthesizeKeyShortcut(
      WCUL10n.getStr("webconsole-clear-alternative-key-macos")
    );

    await waitFor(() => !findAllMessages(hud).length);
    ok(
      isInputFocused(hud),
      "console was cleared as expected with alternative shortcut"
    );
  }

  // Focus filter
  info("try ctrl-f to focus filter");
  synthesizeKeyShortcut(WCUL10n.getStr("webconsole-find-key"));
  ok(!isInputFocused(hud), "input is not focused");
  ok(hasFocus(getFilterInput(hud)), "filter input is focused");

  info("try ctrl-f when filter is already focused");
  synthesizeKeyShortcut(WCUL10n.getStr("webconsole-find-key"));
  ok(!isInputFocused(hud), "input is not focused");
  is(
    getFilterInput(hud),
    outputScroller.ownerDocument.activeElement,
    "filter input is focused"
  );

  info("Ctrl-U should open view:source when input is focused");
  hud.jsterm.focus();
  const onTabOpen = BrowserTestUtils.waitForNewTab(
    gBrowser,
    url => url.startsWith("view-source:"),
    true
  );
  EventUtils.synthesizeKey("u", { accelKey: true });
  await onTabOpen;
  ok(
    true,
    "The view source tab was opened with the expected keyboard shortcut"
  );
});
