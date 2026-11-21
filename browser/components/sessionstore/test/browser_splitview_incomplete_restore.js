/* Any copyright is dedicated to the Public Domain.
 * https://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

add_setup(async function () {
  await SpecialPowers.pushPrefEnv({
    set: [
      ["browser.tabs.splitView.enabled", true],
      ["browser.tabs.splitview.hasUsed", false],
      ["browser.sessionstore.restore_on_demand", false],
      ["browser.sessionstore.encryption.enabled", false],
      ["privacy.userContext.enabled", true],
    ],
  });
});

async function restoreWindowState(win, state) {
  let restoredCount = 0;
  let restored = BrowserTestUtils.waitForEvent(
    win,
    "SSTabRestored",
    false,
    () => ++restoredCount == state.tabs.length
  );
  let ready = BrowserTestUtils.waitForEvent(win, "SSWindowStateReady");
  SessionStore.setWindowState(win, { windows: [state] }, true);
  await Promise.all([ready, restored]);
  await TabStateFlusher.flushWindow(win);
}

for (let isVertical of [false, true]) {
  add_task(async function test_incomplete_splits() {
    await SpecialPowers.pushPrefEnv({
      set: [["sidebar.verticalTabs", isVertical]],
    });
    let win = await promiseNewWindowLoaded();
    registerCleanupFunction(async () => {
      if (!win.closed) {
        await BrowserTestUtils.closeWindow(win);
      }
    });

    for (let userContextId of [0, 1]) {
      let state = {
        tabs: [
          {
            entries: [
              { url: "https://example.com/", triggeringPrincipal_base64 },
            ],
            userContextId,
            splitViewId: 42,
          },
        ],
        selected: 1,
        splitViews: [{ id: 42, numberOfTabs: 2 }],
        _closedTabs: [
          {
            state: {
              entries: [
                { url: "https://example.org/", triggeringPrincipal_base64 },
              ],
            },
            title: "Closed tab",
            pos: 0,
            closedAt: Date.now(),
          },
        ],
      };
      await restoreWindowState(win, state);
      Assert.ok(
        !win.closed,
        "Restoring the sole survivor must keep its window open"
      );
      Assert.equal(win.gBrowser.tabs.length, 1);
      Assert.equal(win.gBrowser.selectedTab, win.gBrowser.tabs[0]);
      Assert.equal(win.gBrowser.selectedTab.userContextId, userContextId);
      Assert.equal(
        win.gBrowser.splitViews.length,
        0,
        "A lone survivor is an ordinary tab"
      );
      let saved = SessionStore.getWindowState(win).windows[0];
      Assert.equal(saved.tabs[0].entries[0].url, "https://example.com/");
      Assert.ok(!("splitViewId" in saved.tabs[0]));
      Assert.equal(saved._closedTabs.length, 1, "Keep previously closed tabs");
      await restoreWindowState(win, saved);
      Assert.equal(win.gBrowser.currentURI.spec, "https://example.com/");
    }

    let urls = ["https://example.com/first", "https://example.com/second"];
    await restoreWindowState(win, {
      tabs: urls.map(url => ({
        entries: [{ url, triggeringPrincipal_base64 }],
        userContextId: 0,
        splitViewId: 43,
      })),
      selected: 2,
      splitViews: [{ id: 43, numberOfTabs: 3 }],
    });
    Assert.deepEqual(
      win.gBrowser.tabs.map(tab => tab.linkedBrowser.currentURI.spec),
      urls
    );
    Assert.equal(win.gBrowser.selectedTab, win.gBrowser.tabs[1]);
    Assert.equal(win.gBrowser.splitViews.length, 1);
    Assert.equal(win.gBrowser.splitViews[0].tabs.length, 2);
    await BrowserTestUtils.closeWindow(win);
    await SpecialPowers.popPrefEnv();
  });
}

add_task(async function test_filtered_split_written_to_disk() {
  let win = await promiseNewWindowLoaded();
  registerCleanupFunction(async () => {
    if (!win.closed) {
      await BrowserTestUtils.closeWindow(win);
    }
  });
  await restoreWindowState(win, {
    tabs: ["https://example.com/", "about:privatebrowsing"].map(url => ({
      entries: [{ url, triggeringPrincipal_base64 }],
      userContextId: 0,
      splitViewId: 44,
    })),
    selected: 1,
    splitViews: [{ id: 44, numberOfTabs: 2 }],
    extData: { splitFilterTest: "filtered" },
  });
  Assert.equal(
    win.gBrowser.tabs.length,
    2,
    "Both source histories were restored and flushed"
  );
  await SessionSaver.run();
  let state = await IOUtils.readJSON(SessionFile.Paths.recovery, {
    decompress: true,
  });
  let saved = state.windows.find(w => w.extData?.splitFilterTest == "filtered");
  Assert.ok(saved, "The session file includes the test window");
  Assert.equal(
    saved.tabs.length,
    1,
    "The save filter removed about:privatebrowsing"
  );
  Assert.deepEqual(
    saved.splitViews,
    [],
    "Do not write a split with a missing partner"
  );
  Assert.ok(!("splitViewId" in saved.tabs[0]));
  await restoreWindowState(win, saved);
  Assert.equal(win.gBrowser.currentURI.spec, "https://example.com/");
  await BrowserTestUtils.closeWindow(win);
});
