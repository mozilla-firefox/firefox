/* Any copyright is dedicated to the Public Domain.
   http://creativecommons.org/publicdomain/zero/1.0/ */

// Ensures SessionStartup records the tab group integrity *_check_failed labels
// for parts of a session too malformed to check, and still records the counts
// for the parts that could be checked.

"use strict";

add_task(async function test_startup_records_check_failed_labels() {
  do_get_profile();
  // Not restoring automatically makes this a deferred startup.
  Services.prefs.setIntPref("browser.startup.page", 1);

  await promiseStartupWith({
    session: {
      version: ["sessionrestore", 1],
      windows: [
        // open_window_check_failed, as tabs isn't a list.
        { tabs: 5 },
        // Still checked: open_window_grouped_tab and open_window_group.
        { tabs: [{ entries: [], groupId: "open" }], groups: [{ id: "open" }] },
      ],
      // closed_window_check_failed.
      _closedWindows: [null],
      // saved_groups_check_failed, as savedGroups isn't a list.
      savedGroups: { not: "a list" },
      // last_session_check_failed.
      lastSessionState: { windows: [null] },
    },
  });

  let expectedChecked = {
    open_window_group: 1,
    open_window_grouped_tab: 1,
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
    open_window_check_failed: 1,
    closed_window_check_failed: 1,
    saved_groups_check_failed: 1,
    last_session_check_failed: 1,
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
