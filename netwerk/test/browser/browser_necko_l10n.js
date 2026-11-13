/* Any copyright is dedicated to the Public Domain.
   http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

/* globals do_timeout */

const { HttpServer } = ChromeUtils.importESModule(
  "resource://testing-common/httpd.sys.mjs"
);

const gOverride = Cc["@mozilla.org/network/native-dns-override;1"].getService(
  Ci.nsINativeDNSResolverOverride
);

const DOMAIN_NAME = "example.com";
const HTTPS_FIRST = "dom.security.https_first";

let statuspanelLabel = document.getElementById("statuspanel-label");

registerCleanupFunction(function () {
  Services.prefs.clearUserPref(HTTPS_FIRST);
});

function waitForStatusChange(browser, expectedMessage) {
  return new Promise(resolve => {
    let listener = {
      QueryInterface: ChromeUtils.generateQI([
        "nsIWebProgressListener",
        "nsISupportsWeakReference",
      ]),

      onStatusChange(webProgress, request, status, message) {
        info(`onStatusChange: ${message}`);
        // When we catch the correct message
        if (message === expectedMessage) {
          browser.webProgress.removeProgressListener(listener);
          resolve({ message });
        }
      },
      onProgressChange() {},
      onLocationChange() {},
      onSecurityChange() {},
      onStateChange() {},
      onContentBlockingEvent() {},
    };

    browser.webProgress.addProgressListener(
      listener,
      Ci.nsIWebProgress.NOTIFY_STATUS
    );
  });
}

add_task(async function test_domain_change() {
  await SpecialPowers.pushPrefEnv({
    set: [[HTTPS_FIRST, false]],
  });

  gOverride.addIPOverride(DOMAIN_NAME, "127.0.0.1");
  let server = new HttpServer();
  server.start(-1);
  registerCleanupFunction(async () => {
    await server.stop();
    gOverride.clearOverrides();
  });

  // eslint-disable-next-line sdl/no-insecure-url
  let serverURL = `http://${DOMAIN_NAME}:${server.identity.primaryPort}/`;
  server.identity.add("http", DOMAIN_NAME, server.identity.primaryPort);

  server.registerPathHandler("/", (request, response) => {
    response.processAsync();
    do_timeout(1000, () => {
      response.setStatusLine(request.httpVersion, 200, "OK");
      response.setHeader("Content-Type", "text/html");
      const BODY = `testing..`;
      response.bodyOutputStream.write(BODY, BODY.length);
      response.finish();
    });
  });

  await SpecialPowers.pushPrefEnv({
    set: [["network.proxy.no_proxies_on", DOMAIN_NAME]],
  });

  let browser = gBrowser.selectedBrowser;
  let expectedMessage = `Waiting for ${DOMAIN_NAME}…`;
  let statusPromise = waitForStatusChange(browser, expectedMessage);
  BrowserTestUtils.startLoadingURIString(browser, serverURL);
  let { message } = await statusPromise;
  // The status panel only shows status messages once the content process has
  // reported the start of the load, which can arrive after the parent's
  // network status messages.
  let statuspanel = document.getElementById("statuspanel");
  await BrowserTestUtils.waitForMutationCondition(
    statuspanel,
    { attributeFilter: ["inactive"] },
    () => !statuspanel.hasAttribute("inactive")
  );
  // There's a delay in the status label being updated, so by the time the
  // progress listener sees waiting for, the statuspanel is still on
  // looking up. This may be racy.
  ok(
    statuspanelLabel.value.startsWith(`Looking up`) ||
      statuspanelLabel.value.startsWith(`Transferring data`),
    `statuspanel has expected value. got ${statuspanelLabel.value}`
  );
  is(message, expectedMessage, "Status message was received correctly");
});
