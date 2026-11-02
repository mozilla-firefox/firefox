/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

add_task(async function test_connectionFailure_error_page_elements() {
  const { browser, tab } = await loadNetErrorPage(
    "connectionFailure",
    "127.0.0.1"
  );

  await SpecialPowers.spawn(browser, [], async function () {
    await ContentTaskUtils.waitForCondition(
      () => content?.document?.querySelector("net-error-card"),
      "Wait for net-error-card to render"
    );
    const doc = content.document;
    const netErrorCard = doc.querySelector("net-error-card").wrappedJSObject;
    await netErrorCard.getUpdateComplete();

    Assert.equal(
      netErrorCard.errorTitle.dataset.l10nId,
      "connectionFailure-title",
      "Using the connectionFailure title"
    );
    Assert.equal(
      netErrorCard.errorIntro.dataset.l10nId,
      "fp-neterror-offline-intro",
      "Using the connectionFailure intro"
    );
    const list = netErrorCard.renderRoot.querySelector(".what-can-you-do-list");
    Assert.ok(list, "NetErrorCard has what-can-you-do list");
    Assert.ok(
      list.querySelector('[data-l10n-id="neterror-load-error-try-again"]'),
      "List includes try-again item"
    );
    Assert.ok(
      list.querySelector('[data-l10n-id="neterror-load-error-connection"]'),
      "List includes connection item"
    );
    Assert.ok(
      list.querySelector('[data-l10n-id="neterror-load-error-firewall"]'),
      "List includes firewall item"
    );
    Assert.ok(
      ContentTaskUtils.isVisible(netErrorCard.tryAgainButton),
      "The 'Try Again' button is shown"
    );
    Assert.ok(
      !netErrorCard.renderRoot.querySelector(
        '[data-l10n-id="fp-cert-error-code"]'
      ),
      "No error code is shown for connectionFailure"
    );
  });

  BrowserTestUtils.removeTab(tab);
});

// Returns a port that nothing is listening on, so connecting to it is refused.
function getClosedPort() {
  const server = Cc["@mozilla.org/network/server-socket;1"].createInstance(
    Ci.nsIServerSocket
  );
  server.init(-1, true, -1);
  const port = server.port;
  server.close();
  return port;
}

async function loadRefusedErrorPage(url) {
  let browser, tab;
  let pageLoaded;
  await BrowserTestUtils.openNewForegroundTab(
    gBrowser,
    () => {
      gBrowser.selectedTab = BrowserTestUtils.addTab(gBrowser, url);
      browser = gBrowser.selectedBrowser;
      tab = gBrowser.selectedTab;
      pageLoaded = BrowserTestUtils.waitForErrorPage(browser);
    },
    false
  );
  await pageLoaded;
  return { browser, tab };
}

// Unlike the test above, this loads a real URL with a non-default port, so
// the loopback check runs against the failed page's actual location.
add_task(async function test_connectionFailure_loopback_hints() {
  const port = getClosedPort();
  for (const [host, expectedIds] of [
    [
      "127.0.0.1",
      [
        "neterror-loopback-refused-not-running",
        "neterror-loopback-refused-wrong-port",
        "neterror-loopback-refused-address",
      ],
    ],
    [
      "localhost",
      [
        "neterror-loopback-refused-not-running",
        "neterror-loopback-refused-wrong-port",
      ],
    ],
  ]) {
    const { browser, tab } = await loadRefusedErrorPage(
      // eslint-disable-next-line sdl/no-insecure-url
      `http://${host}:${port}/`
    );

    await SpecialPowers.spawn(
      browser,
      [expectedIds, String(port), host],
      async function (ids, expectedPort, hostName) {
        Assert.ok(
          content.document.documentURI.includes("e=connectionFailure"),
          `A refused connection to ${hostName} shows the connectionFailure page`
        );
        await ContentTaskUtils.waitForCondition(
          () => content?.document?.querySelector("net-error-card"),
          "Wait for net-error-card to render"
        );
        const netErrorCard =
          content.document.querySelector("net-error-card").wrappedJSObject;
        await netErrorCard.getUpdateComplete();

        const items = [
          ...netErrorCard.renderRoot.querySelectorAll(
            ".what-can-you-do-list li"
          ),
        ];
        Assert.deepEqual(
          items.map(li => li.dataset.l10nId),
          ids,
          `${hostName} gets the loopback hints`
        );
        Assert.equal(
          JSON.parse(items[0]?.dataset.l10nArgs ?? "{}").port,
          expectedPort,
          `The ${hostName} hints include the port`
        );
      }
    );
    BrowserTestUtils.removeTab(tab);
  }
});
