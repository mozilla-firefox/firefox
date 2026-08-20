/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

import { BrowserTestUtils } from "resource://testing-common/BrowserTestUtils.sys.mjs";
import { SessionStore } from "moz-src:///browser/components/sessionstore/SessionStore.sys.mjs";
import { TabStateFlusher } from "moz-src:///browser/components/sessionstore/TabStateFlusher.sys.mjs";

/**
 * TabGroupTestUtils providers helpers for working with tab groups
 * in browser chrome tests.
 *
 * @class
 */
export const TabGroupTestUtils = {
  /**
   * Change a group's collapsed state and wait for its
   * animation to finish.
   *
   * @param {MozTabbrowserTabGroup} group
   * @param {boolean} [force]
   *  force the group to be collapsed (true) or expanded (false)
   *  instead of toggling previous state.
   * @returns {Promise<void>}
   */
  async toggleCollapsed(group, force) {
    let shouldCollapse;
    if (typeof force !== "undefined") {
      shouldCollapse = force;
    } else {
      shouldCollapse = !group.collapsed;
    }
    if (group.collapsed == shouldCollapse) {
      return;
    }
    let animationComplete = BrowserTestUtils.waitForEvent(
      group,
      "TabGroupAnimationComplete"
    );
    group.collapsed = shouldCollapse;
    await animationComplete;
  },

  /**
   * Ensures that tabs have all browsers fully loaded and their browser states
   * flushed into session state. This is a prerequisite for any code that
   * relies on session state, e.g. saved or closed tab groups.
   *
   * @param {MozTabbrowserTab[]} tabs
   */
  async ensureTabsLoaded(tabs) {
    await Promise.all(
      tabs.map(async tab => {
        await BrowserTestUtils.browserLoaded(tab.linkedBrowser);
        await TabStateFlusher.flush(tab.linkedBrowser);
      })
    );
  },

  /**
   * Ensures that a tab group has all browsers fully loaded and their
   * browser states flushed into session state. This is a prerequisite for any
   * code that relies on session state, e.g. saved or closed tab groups.
   *
   * @param {MozTabbrowserTabGroup} tabGroup
   */
  ensureTabGroupLoaded(tabGroup) {
    return this.ensureTabsLoaded(tabGroup.tabs);
  },

  /**
   * Removes a tab group, along with its tabs. Resolves when the tab group
   * is gone.
   *
   * @param {MozTabbrowserTabGroup} group group to be removed
   * @returns {Promise<void>}
   */
  async removeTabGroup(group) {
    if (!group.parentNode) {
      // group was already removed
      return;
    }
    let removePromise = BrowserTestUtils.waitForEvent(group, "TabGroupRemoved");
    await group.documentGlobal.gBrowser.removeTabGroup(group, {
      animate: false,
    });
    await removePromise;
  },

  /**
   * Saves and closes a tab group. Resolves when the tab group is saved and
   * available in session state.
   *
   * @param {MozTabbrowserTabGroup} group
   * @returns {Promise<void>}
   */
  async saveAndCloseTabGroup(group) {
    // The session's cached tab state may not reflect the current state of the
    // group's tabs. If `group` was just created in a test, it's possible that
    // a tab group won't be saved if the group's tabs haven't had a non-empty
    // state flushed to the session.
    await Promise.allSettled(
      group.tabs.map(tab => TabStateFlusher.flush(tab.linkedBrowser))
    );

    let promises = [
      BrowserTestUtils.waitForEvent(group, "TabGroupSaved"),
      BrowserTestUtils.waitForEvent(group, "TabGroupRemoved"),
    ];
    group.saveAndClose();
    await Promise.allSettled(promises);
  },

  /**
   * Forgets any saved tab groups that may have been automatically saved when
   * closing a test window that still had open tab group(s).
   */
  forgetSavedTabGroups() {
    for (const savedGroup of SessionStore.getSavedTabGroups()) {
      SessionStore.forgetSavedTabGroup(savedGroup.id);
    }
  },
};
