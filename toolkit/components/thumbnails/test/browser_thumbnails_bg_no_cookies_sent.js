/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/ */

add_task(async function thumbnails_bg_no_cookies_sent() {
  registerCleanupFunction(() =>
    Services.cookies.removeCookiesFromExactHost("mochi.test", "{}")
  );
  // Visit the test page in the browser and tell it to set a cookie.
  let url = bgTestPageURL({ setGreenCookie: true });

  await BrowserTestUtils.withNewTab(
    {
      gBrowser,
      url,
    },
    async browser => {
      // The root element of the page shouldn't be green yet.
      await SpecialPowers.spawn(browser, [], function () {
        Assert.notEqual(
          content.document.documentElement.style.backgroundColor,
          "rgb(0, 255, 0)",
          "The page shouldn't be green yet."
        );
      });

      // Cookie should be set now.  Reload the page to verify.  Its root element
      // will be green if the cookie's set.
      browser.reload();
      await BrowserTestUtils.browserLoaded(browser);
      await SpecialPowers.spawn(browser, [], function () {
        Assert.equal(
          content.document.documentElement.style.backgroundColor,
          "rgb(0, 255, 0)",
          "The page should be green now."
        );
      });

      // Capture the page.  Get the image data of the capture and verify it's not
      // green.  (Checking only the first pixel suffices.)
      await bgCapture(url);
      ok(thumbnailExists(url), "Thumbnail file should exist after capture.");

      let [r, g, b] = await retrieveImageDataForURL(url);
      isnot(
        [r, g, b].toString(),
        [0, 255, 0].toString(),
        "The captured page should not be green."
      );
      removeThumbnail(url);
    }
  );
});
