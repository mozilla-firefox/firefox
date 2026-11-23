/* Any copyright is dedicated to the Public Domain.
   https://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

/**
 * This test case ensures an existing about:translations page gains a close
 * button when it joins Split View without reloading.
 */
add_task(async function test_add_existing_page_to_split_view() {
  await AboutTranslationsTestUtils.assertSplitViewCloseButtonAfterTabChange({
    separate: false,
    moveToNewWindow: false,
  });
});

/**
 * This test case ensures a standalone page moved to another window gains its
 * close button when it joins Split View there.
 */
add_task(async function test_add_moved_page_to_split_view() {
  await AboutTranslationsTestUtils.assertSplitViewCloseButtonAfterTabChange({
    separate: false,
    moveToNewWindow: true,
  });
});

/**
 * This test case ensures leaving Split View removes the close button without
 * reloading about:translations.
 */
add_task(async function test_separate_existing_page_from_split_view() {
  await AboutTranslationsTestUtils.assertSplitViewCloseButtonAfterTabChange({
    separate: true,
    moveToNewWindow: false,
  });
});

/**
 * This test case ensures a page moved in Split View loses its close button
 * when the tabs separate in the new window.
 */
add_task(async function test_separate_moved_page_from_split_view() {
  await AboutTranslationsTestUtils.assertSplitViewCloseButtonAfterTabChange({
    separate: true,
    moveToNewWindow: true,
  });
});

/**
 * This test case ensures an unsupported companion can be closed after a URL
 * fragment change without trying to update the unavailable language controls.
 */
add_task(async function test_close_companion_when_engine_is_unsupported() {
  const { cleanup, tab: partnerTab } = await loadTestPage({
    page: SPANISH_PAGE_URL,
    languagePairs: LANGUAGE_PAIRS,
    prefs: [
      ["browser.tabs.splitView.enabled", true],
      ["browser.translations.simulateUnsupportedEngine", true],
    ],
  });
  let companionTab;

  try {
    companionTab = await BrowserTestUtils.openNewForegroundTab(
      gBrowser,
      "about:translations"
    );
    const browser = companionTab.linkedBrowser;
    await AboutTranslationsTestUtils.assertSplitViewCloseButton(
      browser,
      { visible: true },
      () =>
        SplitViewTranslationsTestUtils.openExistingTabsInSplitView(
          companionTab,
          partnerTab
        )
    );

    info("Change the URL fragment while the unsupported page is in Split View");
    await SpecialPowers.spawn(browser, [], async () => {
      const unsupportedMessage = content.document.getElementById(
        "about-translations-unsupported-info-message"
      );
      Assert.ok(!unsupportedMessage.hidden, "The engine is unsupported");

      const errors = [];
      const onError = event => errors.push(event.message);
      content.addEventListener("error", onError);

      try {
        const hashChanged = ContentTaskUtils.waitForEvent(
          content,
          "hashchange"
        );
        content.location.hash = "src=es&trg=en";
        await hashChanged;
        Assert.deepEqual(errors, [], "Changing the fragment does not throw");
      } finally {
        content.removeEventListener("error", onError);
      }
    });

    const companionClosed = BrowserTestUtils.waitForEvent(
      companionTab,
      "TabClose"
    );
    const splitViewRemoved = BrowserTestUtils.waitForEvent(
      gBrowser.tabContainer,
      "SplitViewRemoved",
      false,
      () => !partnerTab.splitview
    );
    await AboutTranslationsTestUtils.clickSplitViewCloseButton(browser);
    await Promise.all([companionClosed, splitViewRemoved]);
    ok(partnerTab.isConnected, "Closing the companion leaves its partner open");
  } finally {
    if (companionTab?.isConnected) {
      await BrowserTestUtils.removeTab(companionTab);
    }
    await cleanup();
  }
});
