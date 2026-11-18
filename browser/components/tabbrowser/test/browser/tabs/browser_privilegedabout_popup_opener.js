/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/
 */

add_task(async function privilegedabout_navigation_severs_popup_opener() {
  await BrowserTestUtils.withNewTab(
    "https://example.org/",
    async function (browser) {
      let newTab = BrowserTestUtils.waitForNewTab(gBrowser, "about:blank");

      let [browserId, bcid] = await SpecialPowers.spawn(browser, [], () => {
        content.openedWindow = content.open("about:blank");
        ok(!!content.openedWindow, "Popup was successfully opened");
        ok(!content.openedWindow.closed, "Popup has not been closed");
        return [
          content.openedWindow.browsingContext.browserId,
          content.openedWindow.browsingContext.id,
        ];
      });

      isnot(
        browser.remoteType,
        E10SUtils.PRIVILEGEDABOUT_REMOTE_TYPE,
        "original tab not in privilegedabout"
      );

      let popupTab = await newTab;
      let popupBrowser = popupTab.linkedBrowser;
      let origPopupBC = popupBrowser.browsingContext;
      is(origPopupBC.browserId, browserId, "unexpected browserId");
      is(origPopupBC.id, bcid, "unexpected bc id");
      isnot(
        popupBrowser.remoteType,
        E10SUtils.PRIVILEGEDABOUT_REMOTE_TYPE,
        "popup not in privilegedabout"
      );

      await BrowserTestUtils.loadURIString({
        browser: popupBrowser,
        uriString: "about:welcome",
      });

      let newPopupBC = popupBrowser.browsingContext;
      is(newPopupBC.browserId, browserId, "still same browserId");
      isnot(newPopupBC.id, bcid, "changed browsing context id");
      is(
        popupBrowser.remoteType,
        E10SUtils.PRIVILEGEDABOUT_REMOTE_TYPE,
        "popup in privilegedabout"
      );

      await SpecialPowers.spawn(browser, [], () => {
        ok(
          content.openedWindow.closed,
          "the pop-up appears to have been closed from opener"
        );
      });
      await SpecialPowers.spawn(popupBrowser, [], () => {
        is(content.opener, null, "about:welcome should have no opener");
      });

      BrowserTestUtils.removeTab(popupTab);
    }
  );
});
