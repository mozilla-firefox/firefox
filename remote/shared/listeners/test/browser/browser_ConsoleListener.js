/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

add_task(async function test_message_properties() {
  const listenerId = await listenToConsoleMessage("error");
  await logConsoleMessage({ message: "foo" });
  const { level, message, timeStamp, stack } =
    await getConsoleMessage(listenerId);

  is(level, "error", "Received expected log level");
  is(message, "foo", "Received expected log message");
  // Services.console.logMessage() doesn't include a stack.
  is(stack, undefined, "No stack present");
  is(typeof timeStamp, "number", "timestamp is of expected type number");

  // Clear the console to avoid side effects with other tests in this file.
  await clearConsole();
});

add_task(async function test_level() {
  for (const level of ["error", "info", "warn"]) {
    const listenerId = await listenToConsoleMessage(level);
    await logConsoleMessage({ message: "foo", level });
    const message = await getConsoleMessage(listenerId);

    is(message.level, level, "Received expected log level");
  }

  // Clear the console to avoid side effects with other tests in this file.
  await clearConsole();
});

add_task(async function test_stacktrace() {
  const script = `
    function foo() { throw new Error("cheese"); }
    function bar() { foo(); }
    bar();
  `;

  const listenerId = await listenToConsoleMessage("error");
  await createScriptNode(script);
  const { message, level, stacktrace } = await getConsoleMessage(listenerId);
  is(level, "error", "Received expected log level");
  is(message, "Error: cheese", "Received expected log message");
  ok(Array.isArray(stacktrace), "frames is of expected type Array");
  Assert.greaterOrEqual(stacktrace.length, 4, "Got at least 4 stack frames");

  // First 3 stack frames are from the injected script and one more frame comes
  // from head.js (chrome scope) where we inject the script.
  checkStackFrame(stacktrace[0], "about:blank", "foo", 2, 28);
  checkStackFrame(stacktrace[1], "about:blank", "bar", 3, 22);
  checkStackFrame(stacktrace[2], "about:blank", "", 4, 5);
  checkStackFrame(
    stacktrace[3],
    "chrome://mochitests/content/browser/remote/shared/listeners/test/browser/head.js",
    "",
    53,
    29
  );

  // Clear the console to avoid side effects with other tests in this file.
  await clearConsole();
});

add_task(async function test_messages_from_other_windows_not_retained() {
  const tab = addTab(
    gBrowser,
    "https://example.com/document-builder.sjs?html=test"
  );
  await BrowserTestUtils.browserLoaded(tab.linkedBrowser);

  await SpecialPowers.spawn(tab.linkedBrowser, [], async () => {
    const { ConsoleListener } = ChromeUtils.importESModule(
      "chrome://remote/content/shared/listeners/ConsoleListener.sys.mjs"
    );
    const listener = new ConsoleListener(
      content.windowGlobalChild.innerWindowId
    );
    listener.startListening();

    const iframe = content.document.createElement("iframe");
    const onLoad = new Promise(resolve =>
      iframe.addEventListener("load", resolve, { once: true })
    );
    iframe.srcdoc = "<div>frame</div>";
    content.document.body.append(iframe);
    await onLoad;

    const frameWindowId = iframe.contentWindow.windowGlobalChild.innerWindowId;
    const onFrameError = new Promise(resolve => {
      const onMessage = message => {
        if (
          message instanceof Ci.nsIScriptError &&
          message.innerWindowID === frameWindowId
        ) {
          Services.console.unregisterListener(onMessage);
          resolve();
        }
      };
      Services.console.registerListener(onMessage);
    });

    const frameDocument = iframe.contentDocument;
    const script = frameDocument.createElement("script");
    script.append(
      frameDocument.createTextNode(`(() => { throw new Error("foo"); })()`)
    );
    frameDocument.body.append(script);
    await onFrameError;

    content.backup = {
      listener,
      frameDocumentRef: Cu.getWeakReference(frameDocument),
    };
    iframe.remove();
  });

  // Use a separate task so that local references to the iframe's
  // document from the previous task no longer keep it alive.
  await SpecialPowers.spawn(tab.linkedBrowser, [], async () => {
    for (let i = 0; i < 3; i++) {
      Cu.forceGC();
      Cu.forceCC();
      await new Promise(resolve => Cu.schedulePreciseShrinkingGC(resolve));
    }

    const { listener, frameDocumentRef } = content.backup;
    is(
      frameDocumentRef.get(),
      null,
      "Document of the removed iframe has been garbage collected"
    );

    listener.destroy();
    delete content.backup;
  });

  // Clear the console to avoid side effects with other tests in this file.
  await clearConsole();

  await BrowserTestUtils.removeTab(tab);
});

function logConsoleMessage(options = {}) {
  info(`Log console message ${options.message}`);
  return SpecialPowers.spawn(gBrowser.selectedBrowser, [options], _options => {
    const { level = "error" } = _options;

    const levelToFlags = {
      error: Ci.nsIScriptError.errorFlag,
      info: Ci.nsIScriptError.infoFlag,
      warn: Ci.nsIScriptError.warningFlag,
    };

    const scriptError = Cc["@mozilla.org/scripterror;1"].createInstance(
      Ci.nsIScriptError
    );
    scriptError.initWithWindowID(
      _options.message,
      _options.sourceName || "sourceName",
      _options.lineNumber || 0,
      _options.columnNumber || 0,
      levelToFlags[level],
      _options.category || "javascript",
      content.windowGlobalChild.innerWindowId
    );

    Services.console.logMessage(scriptError);
  });
}

function listenToConsoleMessage(level) {
  info("Listen to a console message in content");
  return SpecialPowers.spawn(
    gBrowser.selectedBrowser,
    [level],
    async _level => {
      const innerWindowId = content.windowGlobalChild.innerWindowId;
      const { ConsoleListener } = ChromeUtils.importESModule(
        "chrome://remote/content/shared/listeners/ConsoleListener.sys.mjs"
      );
      const consoleListener = new ConsoleListener(innerWindowId);
      const onMessage = consoleListener.once(_level);
      consoleListener.startListening();

      const listenerId = Math.random();
      content[listenerId] = { consoleListener, onMessage };
      return listenerId;
    }
  );
}

function getConsoleMessage(listenerId) {
  info("Retrieve the message event captured for listener: " + listenerId);
  return SpecialPowers.spawn(
    gBrowser.selectedBrowser,
    [listenerId],
    async _listenerId => {
      const { consoleListener, onMessage } = content[_listenerId];
      const message = await onMessage;

      consoleListener.destroy();

      return message;
    }
  );
}

function checkStackFrame(
  frame,
  filename,
  functionName,
  lineNumber,
  columnNumber
) {
  is(frame.filename, filename, "Received expected filename for frame");
  is(
    frame.functionName,
    functionName,
    "Received expected function name for frame"
  );
  is(frame.lineNumber, lineNumber, "Received expected line for frame");
  is(frame.columnNumber, columnNumber, "Received expected column for frame");
}
