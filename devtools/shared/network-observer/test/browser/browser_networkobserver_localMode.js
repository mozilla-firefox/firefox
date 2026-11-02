/* Any copyright is dedicated to the Public Domain.
   http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

const LOCAL_FOLDER = "local-mode";

const TEST_ORIGIN = "firefox.localhost";
const TEST_URL = `https://${TEST_ORIGIN}/`;
const TEST_URL_PORT_80 = `https://${TEST_ORIGIN}:80/`;
const TEST_URL_PORT_443 = `https://${TEST_ORIGIN}:443/`;
const TEST_URL_PORT_9999 = `https://${TEST_ORIGIN}:9999/`;
const TEST_FOLDER_URL = `${TEST_URL}folder/`;
const TEST_FOLDER_PAGE_URL = `${TEST_FOLDER_URL}test.html`;
const TEST_404_URL = `${TEST_URL}404`;
// It's important that the URL ends up with two `/`:
const TEST_URL_INVALID_PATH = `https://${TEST_ORIGIN}//`;
// %00 path will be invalid when trying to lookup the filesystem
const TEST_URL_INVALID_PATH_2 = `${TEST_URL}%00`;
const TEST_UNICODE_PERCENT_ENCODED_URL = `${TEST_URL}%10`;
const TEST_MATCHING_PERCENT_ENCODED_URL = `${TEST_URL}%25`;

const TEST_UNICODE_ORIGIN = "ʂ.com";
const TEST_UNICODE_URL = `https://${TEST_UNICODE_ORIGIN}/`;

add_task(async function testLocalMode() {
  await addTab("about:blank");

  const tabBrowserId = gBrowser.selectedBrowser.browserId;
  const networkObserver = new NetworkObserver({
    ignoreChannelFunction: channel =>
      // Only intercept requests related to the first opened tab
      channel.loadInfo.browsingContext.browserId != tabBrowserId,
    onNetworkEvent: event => {
      info("received a network event");
      return createNetworkEventOwner(event);
    },
  });

  const folder = getChromeDir(getResolvedURI(gTestPath));
  folder.append(LOCAL_FOLDER);
  info(" Set Local Mode to " + folder.path + "\n");
  networkObserver.setLocalModeMappings({
    [TEST_ORIGIN]: folder.path,
    [TEST_UNICODE_ORIGIN]: folder.path,
  });

  await loadURL(gBrowser.selectedBrowser, TEST_URL);

  info(
    "Assert that the correct local file content is displayed for the mapping's home page"
  );
  await SpecialPowers.spawn(
    gBrowser.selectedBrowser,
    [TEST_URL, TEST_URL_PORT_80, TEST_URL_PORT_443, TEST_URL_PORT_9999],
    async (pageUrl, urlPort80, urlPort443, urlPort9999) => {
      is(
        content.document.contentType,
        "text/html",
        "Ensure the index.html fallback html page is served with the right mime type"
      );
      is(
        content.document.querySelector("h1").textContent,
        "Hello local mode!",
        "The content of the HTML is the local file content"
      );
      is(
        content.location.href,
        pageUrl,
        "The location of the page is the test url"
      );

      const fetch = await content.fetch(pageUrl);
      const text = await fetch.text();
      Assert.stringContains(
        text,
        "Hello local mode!",
        "Can fetch the html page"
      );

      const fetch80 = await content.fetch(urlPort80, { mode: "no-cors" });
      const text80 = await fetch80.text();
      is(
        fetch80.type,
        "opaque",
        "Can fetch with 80 TCP port, but the response is opaque because of CORS"
      );
      is(
        text80,
        "",
        "Can fetch with 80 TCP port, but the text is empty for opaque requests"
      );

      const fetch443 = await content.fetch(urlPort443);
      const text443 = await fetch443.text();
      is(text443, text, "Can fetch with 443 TCP port");

      await Assert.rejects(
        content.fetch(urlPort9999),
        /NetworkError when attempting to fetch resource./,
        "Can't fetch via a custom TCP port [9999]"
      );
    }
  );

  await loadURL(gBrowser.selectedBrowser, TEST_FOLDER_URL);

  const loaded = BrowserTestUtils.browserLoaded(gBrowser.selectedBrowser);
  info(
    "Assert that the correct local file content is displayed for the mapping's folder"
  );
  await SpecialPowers.spawn(gBrowser.selectedBrowser, [], () => {
    is(
      content.document.contentType,
      "text/html",
      "the html file is served with the right mime type"
    );
    const linkToHtmlFile = content.document.querySelector("a");
    is(
      linkToHtmlFile.textContent,
      "test.html",
      "The link is for the folder unique file"
    );
    info("Navigate via a <a> link");
    linkToHtmlFile.click();
  });
  info("Wait for click on the link to navigate to the new test page");
  await loaded;

  info(
    "Assert that the correct local file content is displayed for the mapping's opened link"
  );
  await SpecialPowers.spawn(
    gBrowser.selectedBrowser,
    [TEST_FOLDER_PAGE_URL],
    async pageUrl => {
      is(
        content.document.querySelector("h1").textContent,
        "Test in a sub folder",
        "The content of the HTML is the local file content"
      );
      is(
        content.location.href,
        pageUrl,
        "The location of the page is the test in sub folder url"
      );
    }
  );

  // Only run the following test on linux as '<' and '>' characters
  // are invalid on windows.
  if (AppConstants.platform === "linux") {
    info("Write a file to the local folder path");
    const fileWithEntitiesInName = PathUtils.join(
      folder.path,
      "folder",
      "<img>"
    );
    await IOUtils.writeUTF8(fileWithEntitiesInName, "");

    await loadURL(gBrowser.selectedBrowser, TEST_FOLDER_URL);

    const linkLoaded = BrowserTestUtils.browserLoaded(gBrowser.selectedBrowser);
    info(
      "Assert that the correct local file content is displayed for the mapping's folder"
    );
    await SpecialPowers.spawn(gBrowser.selectedBrowser, [], () => {
      is(
        content.document.contentType,
        "text/html",
        "the html file is served with the right mime type"
      );
      const [linkWithHtmlString, linkToHtmlFile] =
        content.document.querySelectorAll("a");
      is(
        linkToHtmlFile.textContent,
        "test.html",
        "The link is for the folder unique file"
      );
      is(
        linkWithHtmlString.textContent,
        "<img>",
        "The link is for the file whose name includes html entity"
      );
      info("Navigate via a <a> link");
      linkToHtmlFile.click();
    });
    info("Wait for click on the link to navigate to the new test page");
    await linkLoaded;
  }

  await loadURL(gBrowser.selectedBrowser, TEST_404_URL);
  info(
    "Assert that the 404 page shows when there is no local file found for the mapping"
  );
  await SpecialPowers.spawn(
    gBrowser.selectedBrowser,
    [TEST_404_URL],
    async pageUrl => {
      is(
        content.browsingContext.docShell.currentDocumentChannel.responseStatus,
        404,
        "The page has a 404 HTTP Response code"
      );
      is(
        content.document.querySelector("p").textContent,
        "No local file for: /404",
        "The content of the HTML is the 404 error page"
      );
      is(
        content.location.href,
        pageUrl,
        "The location of the page is the 404 url"
      );
    }
  );

  info("Assert that URL whose path is invalid are generating a 404");
  await loadURL(gBrowser.selectedBrowser, TEST_URL_INVALID_PATH);
  await SpecialPowers.spawn(
    gBrowser.selectedBrowser,
    [TEST_URL_INVALID_PATH],
    async pageUrl => {
      is(
        content.browsingContext.docShell.currentDocumentChannel.responseStatus,
        404,
        "The page has a 404 HTTP Response code"
      );
      is(
        content.document.querySelector("p").textContent,
        "No local file for: //",
        "The content of the HTML is the 400 error page"
      );
      is(
        content.location.href,
        pageUrl,
        "The location of the page is the invalid path url"
      );
    }
  );

  await loadURL(gBrowser.selectedBrowser, TEST_URL_INVALID_PATH_2);
  info("Assert that invalid URL triggers a 400 http error code");
  await SpecialPowers.spawn(
    gBrowser.selectedBrowser,
    [TEST_URL_INVALID_PATH_2],
    async pageUrl => {
      is(
        content.browsingContext.docShell.currentDocumentChannel.responseStatus,
        400,
        "The page has a 400 HTTP Response code"
      );
      is(
        content.document.querySelector("p").textContent,
        "400 Bad request",
        "The content of the HTML is the 400 error page"
      );
      is(
        content.location.href,
        pageUrl,
        "The location of the page is the invalid url"
      );
    }
  );

  await loadURL(gBrowser.selectedBrowser, TEST_UNICODE_PERCENT_ENCODED_URL);
  info(
    "Assert that percent encoded URL with a unicode character encoded via %10 which can't match any valid file name"
  );
  await SpecialPowers.spawn(
    gBrowser.selectedBrowser,
    [TEST_UNICODE_PERCENT_ENCODED_URL],
    async pageUrl => {
      is(
        content.browsingContext.docShell.currentDocumentChannel.responseStatus,
        404,
        "The page has a 404 HTTP Response code"
      );
      is(
        content.document.querySelector("p").textContent,
        "No local file for: /%10",
        "The content of the HTML is the 404 error page"
      );
      is(
        content.location.href,
        pageUrl,
        "The location of the page is the unicode percent encoded url"
      );
    }
  );

  await loadURL(gBrowser.selectedBrowser, TEST_MATCHING_PERCENT_ENCODED_URL);
  info(
    "Assert that percent encoded URL with a valid file name can be functional"
  );
  await SpecialPowers.spawn(
    gBrowser.selectedBrowser,
    [TEST_MATCHING_PERCENT_ENCODED_URL],
    async pageUrl => {
      is(
        content.browsingContext.docShell.currentDocumentChannel.responseStatus,
        200,
        "The page has a 200 HTTP Response code"
      );
      is(
        content.document.body.textContent,
        "percent file content\n",
        "The content of the content of '%' file"
      );
      is(
        content.location.href,
        pageUrl,
        "The location of the page is the percent encoded url"
      );
    }
  );

  info("Assert that we can also load local mapping via a unicode origin");
  await loadURL(gBrowser.selectedBrowser, TEST_UNICODE_URL);
  await SpecialPowers.spawn(
    gBrowser.selectedBrowser,
    [TEST_UNICODE_ORIGIN],
    async mappingOrigin => {
      is(
        content.document.querySelector("h1").textContent,
        "Hello local mode!",
        "The content of the HTML is the local file content"
      );
      is(
        content.location.host,
        // The location's host is ascii and so using "punycode" encoding
        new URL("https://" + mappingOrigin).host,
        "The location of the page is the test url"
      );
    }
  );

  info("Assert that other origins can't fetch local mode origin");
  await loadURL(
    gBrowser.selectedBrowser,
    "data:text/html,page from another origin, not a local mode page"
  );
  await SpecialPowers.spawn(
    gBrowser.selectedBrowser,
    [TEST_URL],
    async localModeUrl => {
      await Assert.rejects(
        content.fetch(localModeUrl),
        /NetworkError when attempting to fetch resource./,
        "Fetching local mode URL should be blocked by cors"
      );
      const response = await content.fetch(localModeUrl, { mode: "no-cors" });
      is(
        response.type,
        "opaque",
        "But we can do no-cors request with opaque response"
      );
    }
  );

  info(
    "Open a new tab without network interception and assert that the mappings aren't working"
  );
  const secondTab = await BrowserTestUtils.openNewForegroundTab({
    gBrowser,
    opening: TEST_URL,
    waitForLoad: false,
  });
  await BrowserTestUtils.waitForErrorPage(gBrowser.selectedBrowser);
  Assert.stringContains(
    gBrowser.selectedBrowser.documentURI.spec,
    "about:neterror"
  );

  BrowserTestUtils.removeTab(secondTab);
});
