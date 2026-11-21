/* Any copyright is dedicated to the Public Domain.
 * https://creativecommons.org/publicdomain/zero/1.0/ */

const { TabSplitViewState } = ChromeUtils.importESModule(
  "moz-src:///browser/components/sessionstore/TabSplitViewState.sys.mjs"
);

add_task(function test_remaining_members() {
  for (let numberOfTabs of [1, 2, 3]) {
    let tabs = [
      { entries: [{ url: "https://example.com/first" }], splitViewId: 1 },
      { entries: [{ url: "https://example.com/second" }], splitViewId: 1 },
      { entries: [{ url: "https://example.com/single" }], splitViewId: 2 },
      { entries: [{ url: "https://example.com/unknown" }], splitViewId: 3 },
    ];
    let entries = tabs.map(tab => tab.entries);
    let splitViews = TabSplitViewState.normalize(tabs, [
      { id: 1, numberOfTabs },
      { id: 2, numberOfTabs: 2 },
      { id: 4, numberOfTabs: 2 },
    ]);
    Assert.deepEqual(splitViews, [{ id: 1, numberOfTabs: 2 }]);
    Assert.deepEqual(
      tabs.map(tab => tab.splitViewId),
      [1, 1, undefined, undefined]
    );
    Assert.deepEqual(
      tabs.map(tab => tab.entries),
      entries,
      "Keep every history in order"
    );
    Assert.deepEqual(TabSplitViewState.normalize(tabs, splitViews), splitViews);
  }
});

add_task(function test_pinned_member() {
  let tabs = [{ pinned: true, splitViewId: 1 }, { splitViewId: 1 }];
  Assert.deepEqual(
    TabSplitViewState.normalize(tabs, [{ id: 1, numberOfTabs: 2 }]),
    []
  );
  Assert.deepEqual(tabs, [{ pinned: true }, {}]);
});

add_task(function test_nested_collections() {
  function group() {
    return {
      id: "group",
      tabs: [
        {
          state: { entries: [{ url: "https://example.com/" }], splitViewId: 1 },
        },
      ],
      splitViews: [{ id: 1, numberOfTabs: 2 }],
    };
  }
  function windowState() {
    return {
      tabs: [{ entries: [{ url: "https://example.com/" }], splitViewId: 1 }],
      splitViews: [{ id: 1, numberOfTabs: 2 }],
      closedGroups: [group()],
    };
  }
  let state = {
    windows: [windowState()],
    _closedWindows: [windowState()],
    savedGroups: [group()],
    lastSessionState: { windows: [windowState()] },
  };
  TabSplitViewState.normalizeSession(state);
  let windows = [
    ...state.windows,
    ...state._closedWindows,
    ...state.lastSessionState.windows,
  ];
  for (let window of windows) {
    Assert.deepEqual(window.splitViews, []);
    Assert.ok(!("splitViewId" in window.tabs[0]));
    Assert.equal(window.tabs[0].entries[0].url, "https://example.com/");
  }
  for (let savedGroup of [
    ...state.savedGroups,
    ...windows.flatMap(win => win.closedGroups),
  ]) {
    Assert.deepEqual(savedGroup.splitViews, []);
    Assert.ok(!("splitViewId" in savedGroup.tabs[0].state));
    Assert.equal(
      savedGroup.tabs[0].state.entries[0].url,
      "https://example.com/"
    );
  }
});
