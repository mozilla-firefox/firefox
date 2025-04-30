/* Any copyright is dedicated to the Public Domain.
 * https://creativecommons.org/publicdomain/zero/1.0/ */

// Test that favicons are stored.

registerCleanupFunction(async () => {
  Services.cache2.clear();
  await PlacesTestUtils.clearFavicons();
  await PlacesUtils.history.clear();
});

async function test_icon(pageUrl, iconUrl) {
  let iconPromise = waitForFaviconMessage(true, iconUrl);
  let storedIconPromise = PlacesTestUtils.waitForNotification(
    "favicon-changed",
    events => events.some(e => e.url == pageUrl)
  );
  await BrowserTestUtils.withNewTab(pageUrl, async () => {
    let { iconURL } = await iconPromise;
    Assert.equal(iconURL, iconUrl, "Should have seen the expected icon.");

    // Ensure the favicon has been stored.
    await storedIconPromise;
    let favicon = await PlacesUtils.favicons.getFaviconForPage(
      Services.io.newURI(pageUrl)
    );
    Assert.equal(
      favicon.uri.spec,
      iconUrl,
      "Should have stored the expected icon."
    );
  });
}

add_task(async function test_icon_stored() {
  for (let [pageUrl, iconUrl] of [
    [
      "https://example.net/browser/browser/base/content/test/favicons/datauri-favicon.html",
      "data:image/png;base64,iVBORw0KGgoAAAANSUhEUgAAACAAAAAgCAYAAABzenr0AAAATklEQVRYhe3SIQ4AIBADwf7/04elBAtrVlSduGnSTDJ7cuT1PQJwwO+Hl7sAGAA07gjAAfgIBeAAoHFHAA7ARygABwCNOwJwAD5CATRgAYXh+kypw86nAAAAAElFTkSuQmCC",
    ],
    [
      "https://example.net/browser/browser/base/content/test/favicons/file_favicon.html",
      "https://example.net/browser/browser/base/content/test/favicons/file_favicon.png",
    ],
  ]) {
    await test_icon(pageUrl, iconUrl);
  }
});
