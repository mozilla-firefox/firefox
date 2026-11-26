/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

// Check adding console calls as batch keep the order of the message.

const TEST_URI =
  "http://example.com/browser/devtools/client/webconsole/" +
  "test/browser/test-batching.html";
const {
  l10n,
} = require("resource://devtools/client/webconsole/utils/messages.js");

add_task(async function () {
  const hud = await openNewTabAndConsole(TEST_URI);
  const messageNumber = 100;
  await testSimpleBatchLogging(hud, messageNumber);
  await testBatchLoggingAndClear(hud, messageNumber);
});

async function testSimpleBatchLogging(hud, messageNumber) {
  await SpecialPowers.spawn(
    gBrowser.selectedBrowser,
    [messageNumber],
    function (numMessages) {
      content.wrappedJSObject.batchLog(numMessages);
    }
  );
  const allMessages = await waitFor(async () => {
    const msgs = await findAllMessagesVirtualized(hud);
    if (msgs.length == messageNumber) {
      return msgs;
    }
    return null;
  });
  for (let i = 0; i < messageNumber; i++) {
    const node = allMessages[i].querySelector(".message-body");
    is(
      node.textContent,
      i.toString(),
      `message at index "${i}" is the expected one`
    );
  }
}

async function testBatchLoggingAndClear(hud, messageNumber) {
  await SpecialPowers.spawn(
    gBrowser.selectedBrowser,
    [messageNumber],
    function (numMessages) {
      content.wrappedJSObject.batchLogAndClear(numMessages);
    }
  );
  await waitFor(() =>
    findConsoleAPIMessage(hud, l10n.getStr("webconsole-console-cleared"))
  );
  ok(true, "console cleared message is displayed");

  const messages = findAllMessages(hud);
  is(messages.length, 1, "console was cleared as expected");
}
