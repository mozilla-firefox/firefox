/* Any copyright is dedicated to the Public Domain.
   https://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

const { TabStateFlusher } = ChromeUtils.importESModule(
  "moz-src:///browser/components/sessionstore/TabStateFlusher.sys.mjs"
);

// Go to example.com, do window.open() to obtain an initial about:blank with content principal
const ABOUT_BLANK_FROM_CONTENT_STATE = {
  entries: [
    {
      url: "about:blank",
      principalToInherit_base64: '{"1":{"0":"https://example.com/"}}',
      triggeringPrincipal_base64: '{"1":{"0":"https://example.com/"}}',
    },
  ],
  index: 1,
};

// Ensure ABOUT_BLANK_FROM_CONTENT_STATE matches a tab opened from a content document
add_task(async function test_about_blank_tab_state_matches_fixture() {
  const openerTab = await BrowserTestUtils.openNewForegroundTab(
    gBrowser,
    "https://example.com/"
  );

  const newTabPromise = BrowserTestUtils.waitForNewTab(
    gBrowser,
    "about:blank",
    true
  );
  await SpecialPowers.spawn(openerTab.linkedBrowser, [], () => {
    content.open("about:blank");
  });
  const aboutBlankTab = await newTabPromise;

  await TabStateFlusher.flush(aboutBlankTab.linkedBrowser);
  const state = JSON.parse(SessionStore.getTabState(aboutBlankTab));

  is(state.entries.length, 1, "Got one SH entry");
  const actualEntryFixture = {
    url: state.entries[0].url,
    principalToInherit_base64: state.entries[0].principalToInherit_base64,
    triggeringPrincipal_base64: state.entries[0].triggeringPrincipal_base64,
  };
  Assert.deepEqual(
    actualEntryFixture,
    ABOUT_BLANK_FROM_CONTENT_STATE.entries[0]
  );

  BrowserTestUtils.removeTab(aboutBlankTab);
  BrowserTestUtils.removeTab(openerTab);
});

// Crashtest for bug 2004165 and bug 2005202
add_task(
  async function test_restore_initial_about_blank_with_content_principal() {
    // Need to restore a whole window such that that restoring the about:blank
    // counts as the initial load and hits the synchronous path.
    const win = await BrowserTestUtils.openNewBrowserWindow();

    // browserLoaded doesn't work reliably for a synchronous load in a different process
    let restored = BrowserTestUtils.waitForEvent(
      win.gBrowser.tabContainer,
      "SSTabRestored"
    );

    const windowState = {
      windows: [
        {
          tabs: [ABOUT_BLANK_FROM_CONTENT_STATE],
          selected: 1,
        },
      ],
      selectedWindow: 1,
    };
    SessionStore.setWindowState(win, JSON.stringify(windowState), true);
    await restored;

    ok(true, "Did not crash");

    const tab = win.gBrowser.selectedTab;

    // Sanity check the restored tab
    await SpecialPowers.spawn(tab.linkedBrowser, [], function () {
      let principal = content.document.nodePrincipal;
      // The crash occured in the synchronous load path, so verify it was taken.
      // That should be equivalent to the document being initial and committed.
      const isInitialCommitted =
        content.document.isInitialDocument &&
        !content.document.isUncommittedInitialDocument;
      // XXX The initial fix for bug 2004165 is to skip the sync path. So
      // assert the opposite till a better fix is implemented. (bug 2005205)
      Assert.ok(
        !isInitialCommitted,
        "about:blank was not restored as initial document"
      );
      Assert.ok(
        principal.isContentPrincipal,
        "Restored about:blank document has a content principal"
      );
      Assert.equal(
        principal.origin,
        "https://example.com",
        "Restored about:blank inherits the origin from https://example.com/"
      );
    });

    BrowserTestUtils.removeTab(tab);
  }
);
