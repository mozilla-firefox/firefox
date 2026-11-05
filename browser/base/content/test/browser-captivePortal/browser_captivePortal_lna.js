"use strict";

const { HttpServer } = ChromeUtils.importESModule(
  "resource://testing-common/httpd.sys.mjs"
);

const PUBLIC_PAGE_URL =
  "https://example.com/browser/browser/base/content/test/browser-captivePortal/file_captivePortal_lna.html";
let SERVER_RESPONSE = "";
const CANONICAL_HTML = "<!DOCTYPE html><html><body>hello</body></html>";
let gHttpServer;
let privateServer;
let localServer;

add_setup(async function setup() {
  // Set up local HTTP server
  gHttpServer = new HttpServer();
  gHttpServer.start();
  gHttpServer.registerPathHandler("/", (request, response) => {
    response.setHeader("Access-Control-Allow-Origin", "*", false);
    response.setHeader("Content-Type", "text/html", false);
    response.setStatusLine(request.httpVersion, 200, "OK");
    response.write(SERVER_RESPONSE);
  });

  privateServer = new HttpServer();
  privateServer.start();
  privateServer.registerPathHandler("/", (request, response) => {
    response.setHeader("Access-Control-Allow-Origin", "*", false);
    response.setHeader("Content-Type", "text/plain", false);
    response.setStatusLine(request.httpVersion, 200, "OK");
    response.write("hello");
  });

  localServer = new HttpServer();
  localServer.start();
  localServer.registerPathHandler("/", (request, response) => {
    response.setHeader("Access-Control-Allow-Origin", "*", false);
    response.setHeader("Content-Type", "text/plain", false);
    response.setStatusLine(request.httpVersion, 200, "OK");
    response.write("hello");
  });

  SERVER_RESPONSE = `<!DOCTYPE html><html><body><script>fetch('http://localhost:${privateServer.identity.primaryPort}/').then(r => r.text()).then(t => document.body.textContent = t);</script></body></html>`;

  await SpecialPowers.pushPrefEnv({
    set: [
      ["network.lna.blocking", true],
      [
        "captivedetect.canonicalURL",
        `http://127.0.0.1:${gHttpServer.identity.primaryPort}/`,
      ],
      ["captivedetect.canonicalContent", CANONICAL_HTML],
      // Set up address space override to this page appear as public
      [
        "network.lna.address_space.public.override",
        `127.0.0.1:${gHttpServer.identity.primaryPort}`,
      ],
      [
        "network.lna.address_space.private.override",
        `127.0.0.1:${privateServer.identity.primaryPort}`,
      ],
    ],
  });

  registerCleanupFunction(async () => {
    await gHttpServer.stop();
    await privateServer.stop();
    await localServer.stop();
  });
});

function observeRequest(url) {
  return new Promise(resolve => {
    const observer = {
      observe(subject, topic) {
        if (topic !== "http-on-stop-request") {
          return;
        }

        let channel = subject.QueryInterface(Ci.nsIHttpChannel);
        if (!channel || channel.URI.spec !== url) {
          return;
        }

        Services.obs.removeObserver(observer, "http-on-stop-request");
        resolve(channel.status);
      },
    };
    Services.obs.addObserver(observer, "http-on-stop-request");
  });
}

// Tests that a captive portal tab making a request to a local network
// resource does not trigger an LNA permission prompt.
add_task(async function test_captivePortalTab_noLnaPrompt() {
  // Simulate portal detection
  await portalDetected();

  let canonicalURL = `http://127.0.0.1:${gHttpServer.identity.primaryPort}/`;
  let portalTabPromise = BrowserTestUtils.waitForNewTab(gBrowser, canonicalURL);

  // Wait for the captive portal notification
  let notification = await ensurePortalNotification(window);

  // Click the notification button to open the captive portal tab
  let button = notification.querySelector("button.notification-button");
  button.click();

  let portalTab = await portalTabPromise;

  // Verify the tab has the isCaptivePortalTab flag set
  ok(
    portalTab.linkedBrowser.browsingContext.isCaptivePortalTab,
    "Captive portal tab should have isCaptivePortalTab flag set"
  );

  // Wait for the fetch to complete and the page content to be updated
  await TestUtils.waitForCondition(
    () =>
      SpecialPowers.spawn(portalTab.linkedBrowser, [], () => {
        return content.document.body.textContent === "hello";
      }),
    "Waiting for fetch response to be displayed on the page"
  );

  // Verify the page content contains "hello" from the fetch response
  let bodyText = await SpecialPowers.spawn(portalTab.linkedBrowser, [], () => {
    return content.document.body.textContent;
  });
  is(bodyText, "hello", "Page should display the fetch response");

  // Verify that no LNA permission prompt appeared
  let lnaPrompt = PopupNotifications.getNotification(
    "local-network",
    portalTab.linkedBrowser
  );
  ok(
    !lnaPrompt,
    "Should not show LNA prompt for captive portal tab accessing local network"
  );

  await SpecialPowers.spawn(
    portalTab.linkedBrowser,
    [localServer.identity.primaryPort],
    port => {
      content.console.log("url", `http://localhost:${port}/`);
      content.fetch(`http://localhost:${port}/`);
    }
  );
  await TestUtils.waitForCondition(
    () =>
      PopupNotifications.getNotification(
        "loopback-network",
        portalTab.linkedBrowser
      ),
    "Waiting for localhost prompt"
  );

  // Clean up
  BrowserTestUtils.removeTab(portalTab);
  await freePortal(true);
});

// Tests that a regular tab (non-captive portal) does NOT trigger an LNA
// permission prompt when accessing local network resources during captive portal.
add_task(async function test_regularTab_noLnaPrompt_duringCaptivePortal() {
  await portalDetected();

  let canonicalURL = `http://127.0.0.1:${gHttpServer.identity.primaryPort}/`;

  // Wait for the captive portal notification
  await ensurePortalNotification(window);

  const tab = await BrowserTestUtils.openNewForegroundTab(
    gBrowser,
    canonicalURL
  );

  // Verify the tab does not have the isCaptivePortalTab flag set
  ok(
    !tab.linkedBrowser.browsingContext.isCaptivePortalTab,
    "Regular tab should not have isCaptivePortalTab flag set"
  );

  // Wait for the fetch to complete and the page content to be updated
  await TestUtils.waitForCondition(
    () =>
      SpecialPowers.spawn(tab.linkedBrowser, [], () => {
        return content.document.body.textContent === "hello";
      }),
    "Waiting for fetch response to be displayed on the page"
  );

  // Verify the page content contains "hello" from the fetch response
  let bodyText = await SpecialPowers.spawn(tab.linkedBrowser, [], () => {
    return content.document.body.textContent;
  });
  is(bodyText, "hello", "Page should display the fetch response");

  // Verify that no local network LNA permission prompt appeared
  let lnaPrompt = PopupNotifications.getNotification(
    "local-network",
    tab.linkedBrowser
  );
  ok(
    !lnaPrompt,
    "Should not show local network LNA prompt for regular tab during captive portal"
  );

  // Clean up
  BrowserTestUtils.removeTab(tab);
  await freePortal(true);
});
