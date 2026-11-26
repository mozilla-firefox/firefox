/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

const TEST_FILE = "test-network-request.html";
const TEST_PATH =
  "https://example.com/browser/devtools/client/webconsole/" + "test/browser/";
const TEST_URI = TEST_PATH + TEST_FILE;

const NET_PREF = "devtools.webconsole.filter.net";
const XHR_PREF = "devtools.webconsole.filter.netxhr";
const {
  l10n,
} = require("resource://devtools/client/webconsole/utils/messages.js");
const LEARN_MORE_URI =
  "https://developer.mozilla.org/docs/Web/HTTP/Reference/Status/200" +
  GA_PARAMS;

pushPref(NET_PREF, true);
pushPref(XHR_PREF, true);

registerCleanupFunction(async function () {
  await new Promise(resolve => {
    Services.clearData.deleteData(Ci.nsIClearDataService.CLEAR_ALL, () =>
      resolve()
    );
  });
});

add_task(async function task() {
  const hud = await openNewTabAndConsole(TEST_URI);

  const onNetworkMessageUpdate = hud.ui.once("network-messages-updated");

  // Fire an XHR POST request.
  await SpecialPowers.spawn(gBrowser.selectedBrowser, [], function () {
    content.wrappedJSObject.testXhrPost();
  });

  info("XHR executed");
  await onNetworkMessageUpdate;

  const xhrUrl = TEST_PATH + "test-data.json";
  const messageNode = await waitFor(() =>
    findMessageByType(hud, xhrUrl, ".network")
  );
  ok(!!messageNode, "Network message found.");

  const statusCodeNode = await waitFor(() =>
    messageNode.querySelector(".status-code")
  );
  is(
    statusCodeNode.title,
    l10n.getStr("webconsole-learn-more"),
    "Status code has the expected tooltip"
  );

  info("Left click status code node and observe the link opens.");
  const { link, where } = await simulateLinkClick(statusCodeNode);
  is(link, LEARN_MORE_URI, `Clicking the provided link opens ${link}`);
  is(where, "tab", "Link opened in correct tab.");

  info("Right click status code node and observe the context menu opening.");
  await openContextMenu(hud, statusCodeNode);
  await hideContextMenu(hud);

  await new Promise(resolve => {
    Services.clearData.deleteData(Ci.nsIClearDataService.CLEAR_ALL, () =>
      resolve()
    );
  });
});
