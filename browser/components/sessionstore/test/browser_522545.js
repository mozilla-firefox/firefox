/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

function test() {
  /** Test for Bug 522545 */

  waitForExplicitFinish();
  requestLongerTimeout(4);

  // This tests the following use case:
  // User opens a new tab which gets focus. The user types something into the
  // address bar, then crashes or quits.
  function test_newTabFocused() {
    let state = {
      windows: [
        {
          tabs: [
            { entries: [{ url: "about:mozilla", triggeringPrincipal_base64 }] },
            { entries: [], urlbarOverrideValue: "example.com" },
          ],
          selected: 2,
        },
      ],
    };

    waitForBrowserState(state, function () {
      let browser = gBrowser.selectedBrowser;
      is(
        browser.currentURI.spec,
        "about:blank",
        "No history entries still sets currentURI to about:blank"
      );
      is(
        browser.urlbarOverrideValue,
        "example.com",
        "urlbarOverrideValue was correctly restored"
      );
      ok(
        !browser.loadStartedSinceUrlbarOverride,
        "We still know that no load is ongoing"
      );
      is(
        gURLBar.value,
        "example.com",
        "Address bar's value correctly restored"
      );

      // Change tabs to make sure address bar value gets updated.  If tab is
      // lazy, wait for SSTabRestored to ensure address bar has time to update.
      let tabToSelect = gBrowser.tabContainer.getItemAtIndex(0);
      if (tabToSelect.linkedBrowser.isConnected) {
        gBrowser.selectedTab = tabToSelect;
        is(
          gURLBar.value,
          "about:mozilla",
          "Address bar's value correctly updated"
        );
        runNextTest();
      } else {
        gBrowser.tabContainer.addEventListener(
          "SSTabRestored",
          function SSTabRestored(event) {
            if (event.target == tabToSelect) {
              gBrowser.tabContainer.removeEventListener(
                "SSTabRestored",
                SSTabRestored,
                true
              );
              is(
                gURLBar.value,
                "about:mozilla",
                "Address bar's value correctly updated"
              );
              runNextTest();
            }
          },
          true
        );
        gBrowser.selectedTab = tabToSelect;
      }
    });
  }

  // This tests the following use case:
  // User opens a new tab which gets focus. The user types something into the
  // address bar, switches back to the first tab, then crashes or quits.
  function test_newTabNotFocused() {
    let state = {
      windows: [
        {
          tabs: [
            { entries: [{ url: "about:mozilla", triggeringPrincipal_base64 }] },
            { entries: [], urlbarOverrideValue: "example.org" },
          ],
          selected: 1,
        },
      ],
    };

    waitForBrowserState(state, function () {
      let browser = gBrowser.getBrowserAtIndex(1);
      is(
        browser.currentURI.spec,
        "about:blank",
        "No history entries still sets currentURI to about:blank"
      );
      is(
        browser.urlbarOverrideValue,
        "example.org",
        "urlbarOverrideValue was correctly restored"
      );
      ok(
        !browser.loadStartedSinceUrlbarOverride,
        "We still know that no load is ongoing"
      );
      is(
        gURLBar.value,
        "about:mozilla",
        "Address bar's value correctly restored"
      );

      // Change tabs to make sure address bar value gets updated.  If tab is
      // lazy, wait for SSTabRestored to ensure address bar has time to update.
      let tabToSelect = gBrowser.tabContainer.getItemAtIndex(1);
      if (tabToSelect.linkedBrowser.isConnected) {
        gBrowser.selectedTab = tabToSelect;
        is(
          gURLBar.value,
          "example.org",
          "Address bar's value correctly updated"
        );
        runNextTest();
      } else {
        gBrowser.tabContainer.addEventListener(
          "SSTabRestored",
          function SSTabRestored(event) {
            if (event.target == tabToSelect) {
              gBrowser.tabContainer.removeEventListener(
                "SSTabRestored",
                SSTabRestored,
                true
              );
              is(
                gURLBar.value,
                "example.org",
                "Address bar's value correctly updated"
              );
              runNextTest();
            }
          },
          true
        );
        gBrowser.selectedTab = tabToSelect;
      }
    });
  }

  // This tests the following use case:
  // User is in a tab with session history, then types something in the
  // address bar, then crashes or quits.
  function test_existingSHEnd_noClear() {
    let state = {
      windows: [
        {
          tabs: [
            {
              entries: [
                { url: "about:mozilla", triggeringPrincipal_base64 },
                { url: "about:config", triggeringPrincipal_base64 },
              ],
              index: 2,
              urlbarOverrideValue: "example.com",
            },
          ],
        },
      ],
    };

    waitForBrowserState(state, function () {
      let browser = gBrowser.selectedBrowser;
      is(
        browser.currentURI.spec,
        "about:config",
        "browser.currentURI set to current entry in SH"
      );
      is(
        browser.urlbarOverrideValue,
        "example.com",
        "urlbarOverrideValue was correctly restored"
      );
      ok(
        !browser.loadStartedSinceUrlbarOverride,
        "We still know that no load is ongoing"
      );
      is(
        gURLBar.value,
        "example.com",
        "Address bar's value correctly restored to urlbarOverrideValue"
      );
      runNextTest();
    });
  }

  // This tests the following use case:
  // User is in a tab with session history, presses back at some point, then
  // types something in the address bar, then crashes or quits.
  function test_existingSHMiddle_noClear() {
    let state = {
      windows: [
        {
          tabs: [
            {
              entries: [
                { url: "about:mozilla", triggeringPrincipal_base64 },
                { url: "about:config", triggeringPrincipal_base64 },
              ],
              index: 1,
              urlbarOverrideValue: "example.org",
            },
          ],
        },
      ],
    };

    waitForBrowserState(state, function () {
      let browser = gBrowser.selectedBrowser;
      is(
        browser.currentURI.spec,
        "about:mozilla",
        "browser.currentURI set to current entry in SH"
      );
      is(
        browser.urlbarOverrideValue,
        "example.org",
        "urlbarOverrideValue was correctly restored"
      );
      ok(
        !browser.loadStartedSinceUrlbarOverride,
        "We still know that no load is ongoing"
      );
      is(
        gURLBar.value,
        "example.org",
        "Address bar's value correctly restored to urlbarOverrideValue"
      );
      runNextTest();
    });
  }

  // This test simulates lots of tabs opening at once and then quitting/crashing.
  function test_getBrowserState_lotsOfTabsOpening() {
    gBrowser.stop();

    let uris = [];
    for (let i = 0; i < 25; i++) {
      uris.push("http://example.com/" + i);
    }

    // We're waiting for the first location change, which should indicate
    // one of the tabs has loaded and the others haven't. So one should
    // be in a non-urlbarOverrideValue case, while others should still have
    // urlbarOverrideValue and urlbarOverrideLoadStarted set.
    gBrowser.addTabsProgressListener({
      onLocationChange(aBrowser) {
        if (uris.indexOf(aBrowser.currentURI.spec) > -1) {
          gBrowser.removeTabsProgressListener(this);
          firstLocationChange();
        }
      },
    });

    function firstLocationChange() {
      let state = JSON.parse(ss.getBrowserState());
      let hasUTV = state.windows[0].tabs.some(function (aTab) {
        return (
          aTab.urlbarOverrideValue &&
          aTab.urlbarOverrideLoadStarted &&
          !aTab.entries.length
        );
      });

      ok(
        hasUTV,
        "At least one tab has a urlbarOverrideValue with urlbarOverrideLoadStarted with no loaded URL"
      );

      BrowserTestUtils.waitForMessage(
        gBrowser.selectedBrowser.messageManager,
        "SessionStore:update"
      ).then(firstLoad);
    }

    function firstLoad() {
      let state = JSON.parse(ss.getTabState(gBrowser.selectedTab));
      let hasSH = !("urlbarOverrideValue" in state) && state.entries[0].url;
      ok(hasSH, "The selected tab has its entry in SH");

      runNextTest();
    }

    gBrowser.loadTabs(uris, {
      triggeringPrincipal: Services.scriptSecurityManager.getSystemPrincipal(),
    });
  }

  // This simulates setting a urlbarOverrideValue and ensures that just typing in the
  // URL bar doesn't set urlbarOverrideLoadStarted as well.
  function test_getBrowserState_urlbarOverrideValue() {
    let state = {
      windows: [
        {
          tabs: [{ entries: [] }],
        },
      ],
    };

    waitForBrowserState(state, function () {
      let browser = gBrowser.selectedBrowser;
      // Make sure this tab isn't loading and state is clear before we test.
      is(
        browser.urlbarOverrideValue,
        null,
        "urlbarOverrideValue is empty to start"
      );
      ok(
        !browser.loadStartedSinceUrlbarOverride,
        "Initially, no load should be ongoing"
      );

      let inputText = "example.org";
      gURLBar.focus();
      gURLBar.value = inputText.slice(0, -1);
      EventUtils.sendString(inputText.slice(-1));

      executeSoon(function () {
        is(
          browser.urlbarOverrideValue,
          "example.org",
          "urlbarOverrideValue was set when changing URLBar value"
        );
        ok(
          !browser.loadStartedSinceUrlbarOverride,
          "No load started since changing URLBar value"
        );

        // Now make sure ss gets these values too
        let newState = JSON.parse(ss.getBrowserState());
        is(
          newState.windows[0].tabs[0].urlbarOverrideValue,
          "example.org",
          "sessionstore got correct urlbarOverrideValue"
        );
        ok(
          !("urlbarOverrideLoadStarted" in newState.windows[0].tabs[0]),
          "sessionstore didn't record a started load"
        );
        runNextTest();
      });
    });
  }

  // test_getBrowserState_lotsOfTabsOpening tested urlbarOverrideLoadStarted in a few cases,
  // but not necessarily any that had legitimate URIs in the state of loading
  // (eg, "http://example.com"), so this test will cover that case.
  function test_urlbarOverrideLoadStartedLoadURI() {
    let state = {
      windows: [
        {
          tabs: [
            {
              entries: [],
              urlbarOverrideValue: "http://example.com",
              urlbarOverrideLoadStarted: true,
            },
          ],
        },
      ],
    };

    waitForBrowserState(state, function () {
      let browser = gBrowser.selectedBrowser;
      is(
        browser.currentURI.spec,
        "http://example.com/",
        "urlbarOverrideLoadStarted caused urlbarOverrideValue to be loaded"
      );
      is(
        browser.urlbarOverrideValue,
        null,
        "urlbarOverrideValue was null after loading a URI"
      );
      ok(
        !browser.loadStartedSinceUrlbarOverride,
        "We should have reset the load state when the tab loaded"
      );
      is(
        gURLBar.value,
        BrowserUIUtils.trimURL("http://example.com/"),
        "Address bar's value set after loading URI"
      );
      runNextTest();
    });
  }

  let tests = [
    test_newTabFocused,
    test_newTabNotFocused,
    test_existingSHEnd_noClear,
    test_existingSHMiddle_noClear,
    test_getBrowserState_lotsOfTabsOpening,
    test_getBrowserState_urlbarOverrideValue,
    test_urlbarOverrideLoadStartedLoadURI,
  ];
  let originalState = JSON.parse(ss.getBrowserState());
  let state = {
    windows: [
      {
        tabs: [
          { entries: [{ url: "about:blank", triggeringPrincipal_base64 }] },
        ],
      },
    ],
  };
  function runNextTest() {
    if (tests.length) {
      waitForBrowserState(state, function () {
        gBrowser.selectedBrowser.urlbarOverrideValue = null;
        gURLBar.setURI();
        tests.shift()();
      });
    } else {
      waitForBrowserState(originalState, function () {
        gBrowser.selectedBrowser.urlbarOverrideValue = null;
        gURLBar.setURI();
        finish();
      });
    }
  }

  // Run the tests!
  runNextTest();
}
