/* Any copyright is dedicated to the Public Domain.
 * https://creativecommons.org/publicdomain/zero/1.0/ */

// Test that a favicon with Cache-Control: no-store is not stored in Places.
// Also tests that favicons added after pageshow are not stored, unless they
// are root icons.

const TEST_SITE = "http://example.net";
const ICON_URL =
  TEST_SITE + "/browser/browser/base/content/test/favicons/no-store.png";
const PAGE_URL =
  TEST_SITE + "/browser/browser/base/content/test/favicons/no-store.html";

async function cleanup() {
  Services.cache2.clear();
  await PlacesTestUtils.clearFavicons();
  await PlacesUtils.history.clear();
}

add_task(async function browser_loader() {
  await cleanup();
  let iconPromise = waitForFaviconMessage(true, ICON_URL);
  let tab = await BrowserTestUtils.openNewForegroundTab(gBrowser, PAGE_URL);
  registerCleanupFunction(async () => {
    await cleanup();
  });

  let { iconURL } = await iconPromise;
  is(iconURL, ICON_URL, "Should have seen the expected icon.");

  // Ensure the favicon has not been stored.
  /* eslint-disable mozilla/no-arbitrary-setTimeout */
  await new Promise(resolve => setTimeout(resolve, 1000));
  let favicon = await PlacesUtils.favicons.getFaviconForPage(
    Services.io.newURI(PAGE_URL)
  );
  Assert.ok(!favicon);
  BrowserTestUtils.removeTab(tab);
});

async function later_addition(iconUrl) {
  let tab = await BrowserTestUtils.openNewForegroundTab(gBrowser, PAGE_URL);
  registerCleanupFunction(async () => {
    await cleanup();
    BrowserTestUtils.removeTab(tab);
  });

  let iconPromise = waitForFaviconMessage(true, iconUrl);
  await ContentTask.spawn(gBrowser.selectedBrowser, iconUrl, href => {
    let doc = content.document;
    let head = doc.head;
    let link = doc.createElement("link");
    link.rel = "icon";
    link.href = href;
    link.type = "image/png";
    head.appendChild(link);
  });
  let { iconURL } = await iconPromise;
  is(iconURL, iconUrl, "Should have seen the expected icon.");

  // Ensure the favicon has not been stored.
  /* eslint-disable mozilla/no-arbitrary-setTimeout */
  await new Promise(resolve => setTimeout(resolve, 1000));
  let favicon = await PlacesUtils.favicons.getFaviconForPage(
    Services.io.newURI(PAGE_URL)
  );
  Assert.ok(!favicon);
  BrowserTestUtils.removeTab(tab);
}

add_task(async function test_later_addition() {
  for (let iconUrl of [
    TEST_SITE + "/browser/browser/base/content/test/favicons/moz.png",
    "data:image/png;base64,iVBORw0KGgoAAAANSUhEUgAAACAAAAAgCAYAAABzenr0AAABH0lEQVRYw2P8////f4YBBEwMAwxGHcBCUMX/91DGOSj/BpT/DkpzQChGBSjfBErLQsVZhmoI/L8LpRdD6X1QietQGhYy7FB5aAgwmkLpBKi4BZTPMThDgBGjHIDF+f9mKD0fKvGBRKNdoF7sgPL1saaJwZgGDkJ9vpZMn8PAHqg5G9FyifBgD4H/W9HyOWrU/f+DIzHhkoeZxxgzZEIAVtJ9RxX+Q6DAxCmP3byhXxkxshAs5odqbcioAY3UC1CBLyTGOTqAmsfAOWRCwBvqxV0oIUB2OQAzDy3/D+a6wB7q8mCU2vD/nw94GziYIQOtDRn9oXz+IZMGBKGMbCjNh9Ii+v8HR4uIAUeLiEEbb9twELaIRlqrmHG0bzjiHQAA1LVfww8jwM4AAAAASUVORK5CYII=",
  ]) {
    await later_addition(iconUrl);
  }
});

add_task(async function root_icon_stored() {
  XPCShellContentUtils.ensureInitialized(this);
  let server = XPCShellContentUtils.createHttpServer({
    hosts: ["www.nostore.com"],
  });
  server.registerFile(
    "/favicon.ico",
    new FileUtils.File(
      PathUtils.join(
        Services.dirsvc.get("CurWorkD", Ci.nsIFile).path,
        "browser",
        "browser",
        "base",
        "content",
        "test",
        "favicons",
        "no-store.png"
      )
    )
  );
  server.registerPathHandler("/page", (request, response) => {
    response.write("<html>A page without icon</html>");
  });

  let noStorePromise = TestUtils.topicObserved("http-on-stop-request", s => {
    let chan = s.QueryInterface(Ci.nsIHttpChannel);
    return chan?.URI.spec == "http://www.nostore.com/favicon.ico";
  }).then(([chan]) => chan.isNoStoreResponse());

  await BrowserTestUtils.withNewTab(
    {
      gBrowser,
      url: "http://www.nostore.com/page",
    },
    async function () {
      await TestUtils.waitForCondition(async () => {
        let favicon = await PlacesUtils.favicons.getFaviconForPage(
          Services.io.newURI("http://www.nostore.com/page")
        );
        return favicon?.uri.spec == "http://www.nostore.com/favicon.ico";
      }, "wait for the favicon to be stored");
      Assert.ok(await noStorePromise, "Should have received no-store header");
    }
  );
});

add_task(async function root_icon_after_pageshow_stored() {
  XPCShellContentUtils.ensureInitialized(this);
  let server = XPCShellContentUtils.createHttpServer({
    hosts: ["rootafterpageshow.com"],
  });
  server.registerFile(
    "/favicon.ico",
    new FileUtils.File(
      PathUtils.join(
        Services.dirsvc.get("CurWorkD", Ci.nsIFile).path,
        "browser",
        "browser",
        "base",
        "content",
        "test",
        "favicons",
        "no-store.png"
      )
    )
  );
  server.registerPathHandler("/page", (request, response) => {
    response.write(`<html><body>
      <link rel="shortcut icon" href="/favicon.ico">
      A page with root icon in body.
      </body></html>`);
  });

  await BrowserTestUtils.withNewTab(
    {
      gBrowser,
      url: "http://rootafterpageshow.com/page",
    },
    async function () {
      await TestUtils.waitForCondition(async () => {
        let favicon = await PlacesUtils.favicons.getFaviconForPage(
          Services.io.newURI("http://rootafterpageshow.com/page")
        );
        return favicon?.uri.spec == "http://rootafterpageshow.com/favicon.ico";
      }, "wait for the favicon to be stored");
    }
  );
});
