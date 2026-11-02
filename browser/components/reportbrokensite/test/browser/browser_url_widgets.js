/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/ */

/* Tests of the fancy address-bar-like URL widgets */

"use strict";

add_common_setup();

const { getMozRemoteImageURL } = ChromeUtils.importESModule(
  "moz-src:///toolkit/modules/FaviconUtils.sys.mjs"
);

add_setup(setupBuiltInTestTrackers);

async function checkURLWidget(rbs, blurringClick, expectedFavicon) {
  const { url, win, urlComponent } = rbs;
  const { emphasizedUrl, faviconImg, input, reset, wrapper } = urlComponent;

  if (expectedFavicon) {
    ok(
      await isDisplayed(faviconImg),
      "have a favicon, so it should be visible"
    );
    await TestUtils.waitForCondition(
      () => expectedFavicon == faviconImg.src,
      "got the correct favicon"
    );
    ok(faviconImg.src.startsWith("moz-remote-image:"), "favicon src is remote");
  } else {
    ok(await isNotDisplayed(faviconImg), "no favicon, so should be hidden");
  }
  is(
    wrapper.matches(".has-favicon"),
    !!expectedFavicon,
    "URL widget class properly notes whether it has a favicon"
  );

  // Ensure that while blurred, the input is hidden and the highlighted origin is visible.
  ok(await isOpaque(emphasizedUrl), "emphasized URL is visible by default");
  ok(await isTransparent(input), "input is hidden by default");
  ok(await isTransparent(reset), "reset is hidden by default");

  // Ensure that tabbing to where the hidden input is will focus on it properly and show it.
  // Also ensure that pressing ESC on the input resets the URL.
  rbs.setURL(url + "#extrastuff");
  await rbs.tabTo("url-input", win);
  ok(
    await isTransparent(emphasizedUrl),
    "emphasized URL is hidden when input is focused by tabbing to it"
  );
  ok(
    await isOpaque(input),
    "input is visible when input is focused by tabbing to it"
  );
  ok(
    await isOpaque(reset),
    "reset is visible when input is focused by tabbing to it"
  );
  await rbs.pressKeyAndAwait("blur", "VK_ESCAPE");
  is(
    rbs.url,
    url,
    "pressing ESC while focused on the URL input resets the URL"
  );

  // Ensure that blurring again hides the input and shows the highlight origin.
  await rbs.pressKeyAndGetFocus("VK_TAB");
  ok(
    await isOpaque(emphasizedUrl),
    "emphasized URL is again visible after tabbing away from input"
  );
  ok(
    await isTransparent(input),
    "input is again hidden after tabbing away from input"
  );
  ok(
    await isTransparent(reset),
    "reset is again hidden after tabbing away from input"
  );

  // Ensure that clicking on where the hidden input is will also focus and show it properly.
  rbs.setURL(url + "#extrastuff");
  await rbs.click(emphasizedUrl);
  ok(
    await isTransparent(emphasizedUrl),
    "emphasized URL hidden when input is focused by clicking on it"
  );
  ok(
    await isOpaque(input),
    "input is visible when focused by focused by clicking on it"
  );
  ok(
    await isOpaque(reset),
    "reset is visible when focused by focused by clicking on it"
  );

  // Ensure progress buttons are disabled/re-enabled as the URL is changed to invalid/valid values.
  rbs.urlComponent.input.setSelectionRange(3, 3);
  await EventUtils.synthesizeKey(" ", {}, rbs.win);
  const progressButton = rbs.progressionButtons[0];
  ok(
    await isDisabled(progressButton),
    "progress buttons are disabled on invalid URL"
  );
  await EventUtils.synthesizeKey("KEY_Backspace", {}, rbs.win);
  ok(
    await isNotDisabled(progressButton),
    "progress buttons are re-enabled on valid URL"
  );

  // Ensure that clicking on the URL widget's reset button also resets the URL.
  // Also ensure that blurring again hides the input and shows the highlight origin.

  // We intentionally turn off this a11y check, because the reset button
  // is an <image> to prevent keyboard navigation from giving it focus
  // even while our fancy URL widgets have it hidden. This is not a
  // major problem, as users can still press ESC on it to reset the URL.
  AccessibilityUtils.setEnv({ mustHaveAccessibleRule: false });
  await rbs.click(reset);
  AccessibilityUtils.resetEnv();

  await blurringClick();
  is(rbs.url, url, "clicking on the reset button resets the URL");
  ok(
    await isOpaque(emphasizedUrl),
    "emphasized URL is again visible after click-blurring the input"
  );
  ok(
    await isTransparent(input),
    "input is again hidden after click-blurring the input"
  );
  ok(
    await isTransparent(input),
    "reset is again hidden after click-blurring the input"
  );
}

async function checkTabSpecificDataVisibility(rbs) {
  is(
    rbs.visibleView,
    rbs.detailsView,
    "must be on the details view for this test"
  );

  const { blockedTrackersToggle, hasBlockedTrackers, url } = rbs;
  if (hasBlockedTrackers) {
    await isNotPressed(
      blockedTrackersToggle,
      "blocked trackers toggle should start off"
    );
    await rbs.click(blockedTrackersToggle);
    await isPressed(
      blockedTrackersToggle,
      "blocked trackers toggle should toggle"
    );
  }

  // Ensure that if the URL's domain changes, tab-specific data is hidden.
  rbs.setURL("https://example2.org");
  await isNotDisplayed(
    rbs.screenshotToggle,
    "screenshot toggle hides if URL origin is changed"
  );
  await isNotDisplayed(
    rbs.blockedTrackersToggle,
    "blocked trackers toggle hides if URL origin is changed"
  );

  // Ensure that if we change to the preview slide, the tab-specific data is also hidden.
  await rbs.clickPreview();
  await isNotDisplayed(
    rbs.availableTabSpecificPreviewItems,
    "tab-specific preview items are hidden if URL origin is changed"
  );

  // Ensure that it the domain changes back, tab-specific data re-appears.
  await rbs.clickBack();
  rbs.setURL(url);
  await isDisplayed(
    rbs.screenshotToggle,
    "screenshot toggle re-appears if URL is changed back"
  );
  if (hasBlockedTrackers) {
    await isDisplayed(
      rbs.blockedTrackersToggle,
      "blocked trackers toggle re-appears if URL is changed back"
    );
  } else {
    await isNotDisplayed(
      rbs.blockedTrackersToggle,
      "blocked trackers toggle stays hidden if no blocked trackers"
    );
  }

  // Ensure that if we change to the preview slide, the tab-specific data is also back.
  await rbs.clickPreview();
  await isDisplayed(
    rbs.availableTabSpecificPreviewItems,
    "tab-specific preview items are shown if URL origin is changed back"
  );
}

async function checkURLWidgets(menu, expectedFavicon) {
  let rbs = await menu.openReportBrokenSite();

  // check the copy of the widget on the main view
  await this.checkURLWidget(
    rbs,
    () => rbs.clickReason("load"),
    expectedFavicon
  );
  await this.checkURLWidget(rbs, () => rbs.clickBack(), expectedFavicon);

  await rbs.clickReason("load");
  await checkTabSpecificDataVisibility(rbs);

  await rbs.close();
}

add_task(async function testURLWidgets() {
  ensureReportBrokenSitePreffedOn();
  enableScreenshots();

  // Test on a page without a favicon
  await withNewTab(REPORTABLE_PAGE_URL, async () => {
    await checkURLWidgets(AppMenu());
    await checkURLWidgets(ProtectionsPanel());
    await checkURLWidgets(HelpMenu());
  });

  // Test on a page with a favicon
  await withNewTab(REPORTABLE_PAGE_URL3, async (win, tab) => {
    await TestUtils.waitForCondition(
      () => tab.linkedBrowser.mIconURL,
      "Waiting for favicon"
    );
    const faviconUrl = tab.linkedBrowser.mIconURL;
    const favicon = faviconUrl
      ? getMozRemoteImageURL(faviconUrl, {
          size: 16,
        })
      : undefined;
    await checkURLWidgets(AppMenu(win), favicon);
    await checkURLWidgets(ProtectionsPanel(win), favicon);
    await checkURLWidgets(HelpMenu(win), favicon);
  });
});
