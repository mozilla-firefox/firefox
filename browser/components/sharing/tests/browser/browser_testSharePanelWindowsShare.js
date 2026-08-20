/* Any copyright is dedicated to the Public Domain.
   https://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

const { sinon } = ChromeUtils.importESModule(
  "resource://testing-common/Sinon.sys.mjs"
);
const TEST_URL = "https://example.com/";

let shareUrlSpy = sinon.spy();

SharingUtils.testOnlyMockUIUtils({
  shareUrl(url, title) {
    shareUrlSpy(url, title);
  },
  QueryInterface: ChromeUtils.generateQI([Ci.nsIWindowsUIUtils]),
});

registerCleanupFunction(function () {
  SharingUtils.testOnlyMockUIUtils(null);
});

/**
 * Test the "Share" submenu in the tab contextmenu on Windows.
 */
add_task(async function test_contextmenu_share_win() {
  await BrowserTestUtils.withNewTab(TEST_URL, async browser => {
    await SpecialPowers.spawn(browser, [], () => {
      let title = content.document.querySelector("title");
      if (!title) {
        title = content.document.createElement("title");
        content.document.head.append(title);
      }

      title.textContent = "Windows share url";
    });

    const panel = await openSharePanel(window);
    const shareButton = panel.querySelector("#share-panel-os-share");

    const panelHidden = BrowserTestUtils.waitForEvent(panel, "popuphidden");

    EventUtils.synthesizeMouseAtCenter(shareButton, {});

    await panelHidden;
    is(panel.state, "closed", "Share panel is closed");

    ok(shareUrlSpy.calledOnce, "shareUrl called");
    let [url, title] = shareUrlSpy.getCall(0).args;
    is(url, TEST_URL, "Shared correct URL");
    is(title, "Windows share url", "Shared correct title");

    shareUrlSpy.resetHistory();
  });
});
