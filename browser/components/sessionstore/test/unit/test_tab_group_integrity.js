/* Any copyright is dedicated to the Public Domain.
   http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

const { TabGroupIntegrity } = ChromeUtils.importESModule(
  "moz-src:///browser/components/sessionstore/TabGroupIntegrity.sys.mjs"
);

function tab(groupId) {
  return groupId ? { entries: [], groupId } : { entries: [] };
}

function group(id, extra = {}) {
  return { id, name: id, color: "blue", collapsed: false, ...extra };
}

add_task(function test_consistent_state_has_no_problems() {
  let { checked, problems } = TabGroupIntegrity.check({
    windows: [
      {
        tabs: [tab(), tab("g1"), tab("g1")],
        groups: [group("g1")],
        closedGroups: [group("c1", { tabs: [{}] })],
      },
    ],
    _closedWindows: [{ tabs: [tab("g2")], groups: [group("g2")] }],
    savedGroups: [group("s1", { tabs: [{}] })],
  });

  Assert.deepEqual(problems, {}, "no problems are reported");
  Assert.deepEqual(
    checked,
    {
      open_window_grouped_tab: 2,
      open_window_group: 1,
      closed_group: 1,
      closed_window_grouped_tab: 1,
      closed_window_group: 1,
      saved_group: 1,
    },
    "every group and grouped tab is counted"
  );
});

add_task(function test_broken_window_references() {
  let { problems } = TabGroupIntegrity.check({
    windows: [{ tabs: [tab("missing")], groups: [group("empty")] }],
    _closedWindows: [
      { tabs: [tab("missing"), tab("missing")], groups: [group("empty")] },
    ],
  });

  Assert.deepEqual(problems, {
    open_window_tab_missing_group: 1,
    open_window_group_without_tabs: 1,
    closed_window_tab_missing_group: 2,
    closed_window_group_without_tabs: 1,
  });
});

add_task(function test_empty_and_invalid_groups() {
  let { checked, problems } = TabGroupIntegrity.check({
    windows: [{ tabs: [], closedGroups: [group("c1", { tabs: [] })] }],
    savedGroups: [group("s1", { tabs: [] }), group("s2"), null, "bogus"],
  });

  Assert.deepEqual(problems, {
    closed_group_without_tabs: 1,
    saved_group_without_tabs: 2,
    saved_group_not_object: 2,
  });
  Assert.equal(checked.saved_group, 2, "only group objects are checked");
});

add_task(function test_session_without_groups() {
  Assert.deepEqual(TabGroupIntegrity.check({ windows: [{ tabs: [tab()] }] }), {
    checked: {},
    problems: {},
  });
});

add_task(function test_restore_flag_matching_last_session_is_not_stale() {
  let { problems } = TabGroupIntegrity.check({
    windows: [],
    savedGroups: [group("g1", { tabs: [{}], removeAfterRestore: true })],
    lastSessionState: {
      windows: [{ tabs: [tab("g1")], groups: [group("g1")] }],
    },
  });

  Assert.deepEqual(problems, {}, "the last session reopens the group");
});

add_task(function test_stale_restore_flags() {
  let { problems } = TabGroupIntegrity.check({
    windows: [],
    savedGroups: [
      group("saved-in-last-session", { tabs: [{}], removeAfterRestore: true }),
      group("not-flagged", { tabs: [{}] }),
    ],
    lastSessionState: {
      windows: [{ tabs: [tab("other")], groups: [group("other")] }],
    },
  });

  Assert.deepEqual(problems, { saved_group_stale_restore_flag: 1 });
});

add_task(function test_restore_flag_without_last_session_is_not_counted() {
  let { problems } = TabGroupIntegrity.check({
    windows: [],
    savedGroups: [group("g1", { tabs: [{}], removeAfterRestore: true })],
  });

  Assert.deepEqual(
    problems,
    {},
    "nothing can be deleted without a last session"
  );
});

add_task(function test_malformed_entries_do_not_stop_the_check() {
  let { checked, problems } = TabGroupIntegrity.check({
    windows: [null, { tabs: 5 }, { tabs: [tab("missing")], groups: [] }],
    _closedWindows: "bogus",
    savedGroups: { not: "a list" },
    lastSessionState: { windows: [null] },
  });

  Assert.deepEqual(
    problems,
    {
      open_window_check_failed: 2,
      open_window_tab_missing_group: 1,
      closed_window_check_failed: 1,
      saved_groups_check_failed: 1,
      last_session_check_failed: 1,
    },
    "each malformed part is counted, and the well-formed window is still checked"
  );
  Assert.deepEqual(checked, { open_window_grouped_tab: 1 });
});

add_task(function test_failed_window_adds_no_partial_counts() {
  let { checked, problems } = TabGroupIntegrity.check({
    windows: [
      { tabs: [tab("g1")], groups: [group("g1")], closedGroups: 5 },
      {
        tabs: [tab("g2")],
        groups: [group("g2")],
        closedGroups: [group("c1", { tabs: [] })],
      },
    ],
  });

  Assert.deepEqual(problems, {
    open_window_check_failed: 1,
    closed_group_without_tabs: 1,
  });
  Assert.deepEqual(
    checked,
    { open_window_grouped_tab: 1, open_window_group: 1, closed_group: 1 },
    "only the window that could be checked is counted"
  );
});

add_task(function test_last_session_groups_not_a_list_fails_check() {
  let { problems } = TabGroupIntegrity.check({
    windows: [],
    savedGroups: [group("g1", { tabs: [{}], removeAfterRestore: true })],
    lastSessionState: { windows: [{ groups: "bogus" }] },
  });

  Assert.deepEqual(
    problems,
    { last_session_check_failed: 1 },
    "ensure that last session groups is a list"
  );
});
