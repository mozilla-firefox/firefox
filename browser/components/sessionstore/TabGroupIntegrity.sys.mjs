/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

/**
 * @typedef {object} TabGroupIntegrityCounts
 * @property {Record<string, number>} checked
 *   How many groups, or tabs with a group id, were looked at, by area. Labels
 *   match session_restore.tab_group_integrity_checked.
 * @property {Record<string, number>} problems
 *   How many broken references were found, by kind. Labels match
 *   session_restore.tab_group_integrity_problems.
 */

class TabGroupIntegrityCheck {
  /** @type {Record<string, number>} */
  checked = {};
  /** @type {Record<string, number>} */
  problems = {};

  /**
   * @param {object} state
   *   Session state as read from disk.
   */
  constructor(state) {
    this.#checkEach(state.windows, "open_window_check_failed", win => {
      this.#checkWindow(win, "open_window");
    });

    this.#checkEach(state._closedWindows, "closed_window_check_failed", win => {
      this.#checkWindow(win, "closed_window");
    });

    let savedGroups = [];
    try {
      savedGroups = (state.savedGroups ?? []).filter(group => {
        const isObject = !!group && typeof group == "object";
        if (!isObject) {
          this.#count(this.problems, "saved_group_not_object");
        }
        return isObject;
      });

      for (const group of savedGroups) {
        this.#count(this.checked, "saved_group");
        if (!group.tabs?.length) {
          this.#count(this.problems, "saved_group_without_tabs");
        }
      }
    } catch {
      this.#count(this.problems, "saved_groups_check_failed");
    }

    try {
      this.#checkRestoreFlags(savedGroups, state.lastSessionState);
    } catch {
      this.#count(this.problems, "last_session_check_failed");
    }
  }

  /**
   * Runs a check on each entry of a list from the session state, so that one
   * malformed entry doesn't stop the others from being checked.
   *
   * @param {any} list
   * @param {string} failedLabel
   *   Problem label counted for each entry that couldn't be checked, or once
   *   if the list itself isn't a list.
   * @param {(item: any) => void} check
   */
  #checkEach(list, failedLabel, check) {
    if (list != null && !Array.isArray(list)) {
      this.#count(this.problems, failedLabel);
      return;
    }
    for (const item of list ?? []) {
      try {
        check(item);
      } catch {
        this.#count(this.problems, failedLabel);
      }
    }
  }

  /**
   * @param {Record<string, number>} counts
   * @param {string} label
   * @param {number} [amount]
   */
  #count(counts, label, amount = 1) {
    if (amount) {
      counts[label] = (counts[label] ?? 0) + amount;
    }
  }

  /**
   * Checks that every group in the window has a tab, every tab with a group id
   * has a group, and every closed group has a tab. Counts are only added once
   * the whole window has been checked, so a window that can't be checked adds
   * none.
   *
   * @param {object} win
   * @param {"open_window"|"closed_window"} area
   */
  #checkWindow(win, area) {
    const groups = this.#groupsOf(win);
    const groupIds = new Set(groups.map(group => group?.id));
    const referencedGroupIds = new Set();
    let groupedTabs = 0;
    let tabsMissingGroup = 0;
    let groupsWithoutTabs = 0;
    let closedGroups = 0;
    let closedGroupsWithoutTabs = 0;

    for (const tab of win.tabs ?? []) {
      if (!tab?.groupId) {
        continue;
      }
      groupedTabs++;
      referencedGroupIds.add(tab.groupId);
      if (!groupIds.has(tab.groupId)) {
        tabsMissingGroup++;
      }
    }

    for (const group of groups) {
      if (!referencedGroupIds.has(group?.id)) {
        groupsWithoutTabs++;
      }
    }

    for (const group of win.closedGroups ?? []) {
      closedGroups++;
      if (!group?.tabs?.length) {
        closedGroupsWithoutTabs++;
      }
    }

    this.#count(this.checked, `${area}_grouped_tab`, groupedTabs);
    this.#count(this.checked, `${area}_group`, groups.length);
    this.#count(this.problems, `${area}_tab_missing_group`, tabsMissingGroup);
    this.#count(this.problems, `${area}_group_without_tabs`, groupsWithoutTabs);
    this.#count(this.checked, "closed_group", closedGroups);
    this.#count(
      this.problems,
      "closed_group_without_tabs",
      closedGroupsWithoutTabs
    );
  }

  /**
   * Counts saved groups flagged removeAfterRestore that the last session
   * would not reopen. SessionStore.restoreLastSession deletes every flagged
   * group, so these would be lost if the user restored the last session.
   * Nothing is counted when there is no last session since there is no threat
   * of accidental deletion.
   *
   * @param {object[]} savedGroups
   *   Saved groups that are objects.
   * @param {object} [lastSessionState]
   */
  #checkRestoreFlags(savedGroups, lastSessionState) {
    if (!lastSessionState) {
      return;
    }

    const lastSessionGroupIds = new Set(
      (lastSessionState.windows ?? [])
        .flatMap(win => this.#groupsOf(win))
        .map(group => group?.id)
    );

    for (const group of savedGroups) {
      if (group.removeAfterRestore && !lastSessionGroupIds.has(group.id)) {
        this.#count(this.problems, "saved_group_stale_restore_flag");
      }
    }
  }

  /**
   * @param {object} win
   * @returns {object[]}
   */
  #groupsOf(win) {
    const groups = win.groups ?? [];
    if (!Array.isArray(groups)) {
      throw new TypeError("groups is not a list");
    }
    return groups;
  }
}

export const TabGroupIntegrity = {
  /**
   * Looks for broken tab group references in a session state.
   *
   * @param {object} state
   *   Session state as read from disk.
   * @returns {TabGroupIntegrityCounts}
   */
  check(state) {
    const { checked, problems } = new TabGroupIntegrityCheck(state);
    return { checked, problems };
  },
};
