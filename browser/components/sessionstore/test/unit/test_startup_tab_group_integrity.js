/* Any copyright is dedicated to the Public Domain.
   http://creativecommons.org/publicdomain/zero/1.0/ */

// Ensures SessionStartup records every tab group integrity label for the
// session it reads, including lastSessionState, which a deferred startup
// discards. A label missing from metrics.yaml would be counted as __other__.

"use strict";

function tab(groupId) {
  return { entries: [], groupId };
}

add_task(async function test_startup_records_every_tab_group_integrity_label() {
  do_get_profile();
  // Not restoring automatically makes this a deferred startup.
  Services.prefs.setIntPref("browser.startup.page", 1);

  await promiseStartupWith({
    session: {
      version: ["sessionrestore", 1],
      windows: [
        {
          tabs: [
            // open_window_grouped_tab, with a matching group. Two tabs, so the
            // grouped tab and group counts differ.
            tab("open"),
            tab("open"),
            // open_window_grouped_tab and open_window_tab_missing_group.
            tab("open-missing"),
          ],
          groups: [
            // open_window_group, with a tab.
            { id: "open" },
            // open_window_group and open_window_group_without_tabs.
            { id: "open-empty" },
          ],
          // closed_group and closed_group_without_tabs.
          closedGroups: [{ id: "closed-empty", tabs: [] }],
        },
      ],
      _closedWindows: [
        {
          tabs: [
            // closed_window_grouped_tab, with a matching group. Two tabs, so
            // the grouped tab and group counts differ.
            tab("closed-window"),
            tab("closed-window"),
            // closed_window_grouped_tab and closed_window_tab_missing_group.
            tab("closed-window-missing"),
          ],
          groups: [
            // closed_window_group, with a tab.
            { id: "closed-window" },
            // closed_window_group and closed_window_group_without_tabs.
            { id: "closed-window-empty" },
          ],
        },
      ],
      savedGroups: [
        // saved_group and saved_group_stale_restore_flag, as lastSessionState
        // does not reopen it.
        { id: "flagged", tabs: [{}], removeAfterRestore: true },
        // saved_group and saved_group_without_tabs.
        { id: "saved-empty", tabs: [] },
        // saved_group_not_object, and not counted as a saved_group.
        null,
      ],
      // Reopens only "other", so the "flagged" saved group's flag is stale.
      lastSessionState: {
        windows: [{ tabs: [tab("other")], groups: [{ id: "other" }] }],
      },
    },
  });

  let expectedChecked = {
    open_window_group: 2,
    open_window_grouped_tab: 3,
    closed_window_group: 2,
    closed_window_grouped_tab: 3,
    closed_group: 1,
    saved_group: 2,
    __other__: null,
  };
  for (let [label, count] of Object.entries(expectedChecked)) {
    Assert.equal(
      Glean.sessionRestore.tabGroupIntegrityChecked[label].testGetValue(),
      count,
      `tab_group_integrity_checked.${label}`
    );
  }

  let expectedProblems = {
    open_window_group_without_tabs: 1,
    open_window_tab_missing_group: 1,
    closed_window_group_without_tabs: 1,
    closed_window_tab_missing_group: 1,
    closed_group_without_tabs: 1,
    saved_group_without_tabs: 1,
    saved_group_not_object: 1,
    // lastSessionState is checked before a deferred startup discards it.
    saved_group_stale_restore_flag: 1,
    __other__: null,
  };
  for (let [label, count] of Object.entries(expectedProblems)) {
    Assert.equal(
      Glean.sessionRestore.tabGroupIntegrityProblems[label].testGetValue(),
      count,
      `tab_group_integrity_problems.${label}`
    );
  }
});
