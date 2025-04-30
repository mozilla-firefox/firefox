/* Any copyright is dedicated to the Public Domain.
   http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

/**
 * WHOA THERE: We should never be adding new things to EXPECTED_REFLOWS.
 * Instead of adding reflows to the list, you should be modifying your code to
 * avoid the reflow.
 *
 * See https://firefox-source-docs.mozilla.org/performance/bestpractices.html
 * for tips on how to do that.
 */
const EXPECTED_REFLOWS = [
  /**
   * Nothing here! Please don't add anything new!
   */
];

/*
 * This test ensures that there are no unexpected
 * uninterruptible reflows or flickering areas when opening new windows.
 */
add_task(async function () {
  // Flushing all caches helps to ensure that we get consistent
  // behaviour when opening a new window, even if windows have been
  // opened in previous tests.
  Services.obs.notifyObservers(null, "startupcache-invalidate");
  Services.obs.notifyObservers(null, "chrome-flush-caches");

  let bookmarksToolbarRect = await getBookmarksToolbarRect();

  let win = window.openDialog(
    AppConstants.BROWSER_CHROME_URL,
    "_blank",
    "chrome,all,dialog=no,remote,suppressanimation",
    "about:home"
  );

  await disableFxaBadge();

  let alreadyFocused = false;
  let inRange = (val, min, max) => min <= val && val <= max;
  let tabBoundingRect = undefined;
  let expectations = {
    expectedReflows: EXPECTED_REFLOWS,
    frames: {
      filter(rects, frame) {
        // The first screenshot we get in OSX / Windows shows an unfocused browser
        // window for some reason. See bug 1445161.
        if (!alreadyFocused && isLikelyFocusChange(rects, frame)) {
          todo(
            false,
            "bug 1445161 - the window should be focused at first paint, " +
              rects.toSource()
          );
          return [];
        }
        alreadyFocused = true;
        return rects;
      },
      exceptions: [
        {
          name: "bug 1421463 - reload toolbar icon shouldn't flicker",
          condition: r =>
            inRange(r.h, 13, 14) &&
            inRange(r.w, 14, 16) && // icon size
            inRange(r.y1, 40, 80) && // in the toolbar
            inRange(r.x1, 65, 100), // near the left side of the screen
        },
        {
          name: "bug 1555842 - the urlbar shouldn't flicker",
          condition: r => {
            let inputFieldRect = win.gURLBar.inputField.getBoundingClientRect();

            return (
              (!AppConstants.DEBUG ||
                (AppConstants.platform == "linux" && AppConstants.ASAN)) &&
              r.x1 >= inputFieldRect.left &&
              r.x2 <= inputFieldRect.right &&
              r.y1 >= inputFieldRect.top &&
              r.y2 <= inputFieldRect.bottom
            );
          },
        },
        {
          name: "Initial bookmark icon appearing after startup",
          condition: r =>
            r.w == 16 &&
            r.h == 16 && // icon size
            inRange(
              r.y1,
              bookmarksToolbarRect.top,
              bookmarksToolbarRect.top + bookmarksToolbarRect.height / 2
            ) && // in the toolbar
            inRange(r.x1, 11, 13), // very close to the left of the screen
        },
        {
          // Note that the length and x values here are a bit weird because on
          // some fonts, we appear to detect the two words separately.
          name: "Initial bookmark text ('Getting Started' or 'Get Involved') appearing after startup",
          condition: r =>
            inRange(r.w, 25, 120) && // length of text
            inRange(r.h, 9, 15) && // height of text
            inRange(
              r.y1,
              bookmarksToolbarRect.top,
              bookmarksToolbarRect.top + bookmarksToolbarRect.height / 2
            ) && // in the toolbar
            inRange(r.x1, 30, 90), // close to the left of the screen
        },
        {
          name: "Shadow around active tab should not flicker on macOS (bug 1960967)",
          condition(r) {
            const tabRect = tabBoundingRect
              ? tabBoundingRect
              : (tabBoundingRect = gBrowser.tabContainer
                  .querySelector("tab[selected=true] .tab-background")
                  .getBoundingClientRect());
            return (
              inRange(r.x1, tabRect.x - 2, tabRect.x + 2) &&
              inRange(r.y1, tabRect.y - 2, tabRect.y + 2) &&
              inRange(r.w, tabRect.width - 4, tabRect.width + 4) &&
              inRange(r.h, tabRect.height - 4, tabRect.height + 4)
            );
          },
        },
      ],
    },
  };

  await withPerfObserver(
    async function () {
      // Avoid showing the remotecontrol UI.
      await new Promise(resolve => {
        win.addEventListener(
          "DOMContentLoaded",
          () => {
            delete win.Marionette;
            win.Marionette = { running: false };
            resolve();
          },
          { once: true }
        );
      });

      await TestUtils.topicObserved(
        "browser-delayed-startup-finished",
        subject => subject == win
      );

      let promises = [
        BrowserTestUtils.firstBrowserLoaded(win, false),
        BrowserTestUtils.browserStopped(
          win.gBrowser.selectedBrowser,
          "about:home"
        ),
      ];

      await Promise.all(promises);

      await new Promise(resolve => {
        // 10 is an arbitrary value here, it needs to be at least 2 to avoid
        // races with code initializing itself using idle callbacks.
        (function waitForIdle(count = 10) {
          if (!count) {
            resolve();
            return;
          }
          Services.tm.idleDispatchToMainThread(() => {
            waitForIdle(count - 1);
          });
        })();
      });
    },
    expectations,
    win
  );

  await BrowserTestUtils.closeWindow(win);
});
