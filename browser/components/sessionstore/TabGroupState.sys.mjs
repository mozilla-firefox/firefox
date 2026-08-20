/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

/**
 * @typedef {string} TabGroupId
 *   Unique ID of a tab group.
 */

/**
 * @typedef {object} TabGroupStateData
 *   State of a tab group inside of an open window.
 * @property {TabGroupId} id
 *   Unique ID of the tab group.
 * @property {string} name
 *   User-defined name of the tab group.
 * @property {"blue"|"purple"|"cyan"|"orange"|"yellow"|"pink"|"green"|"gray"|"red"} color
 *   User-selected color name for the tab group's label/icons.
 * @property {boolean} collapsed
 *   Whether the tab group is collapsed or expanded in the tab strip.
 */

/**
 * @typedef {TabGroupStateData} ClosedTabGroupStateData
 *   State of a tab group that was explicitly closed by the user.
 * @property {number} closedAt
 *   Timestamp from `Date.now()`.
 * @property {string} sourceWindowId
 *   Window that the tab group was in before it was closed.
 * @property {ClosedTabStateData[]} tabs
 *   Copy of all tab data for the tabs that were in this tab group
 *   at the time it was closed.
 */

/**
 * @typedef {TabGroupStateData} SavedTabGroupStateData
 *   State of a tab group that was explicitly saved and closed by the user
 *   or implicitly saved on behalf of the user when the user explicitly closed
 *   a window.
 * @property {true} saved
 *   Indicates that the tab group was saved explicitly by the user or
 *   automatically by the browser.
 * @property {number} closedAt
 *   Timestamp from `Date.now()`.
 * @property {string} [sourceWindowId]
 *   Window that the tab group was in before a user explicitly saved it. Not set
 *   when the tab group is saved automatically due to a window closing.
 * @property {number} [windowClosedId]
 *   `closedId` of the closed window if this tab group was saved automatically
 *   due to a window closing. Not set when a user explicitly saves a tab group.
 * @property {ClosedTabStateData[]} tabs
 *   Copy of all tab data for the tabs that were in this tab group
 *   at the time it was saved.
 * @property {TabSplitViewStateData[]} splitViews
 *   Copy of all splitview data for the splitviews that were in this tab group
 *   at the time it was saved.
 */

/**
 * Module that contains tab group state collection methods.
 */
class _TabGroupState {
  /**
   * Collect data related to a single tab group, synchronously.
   *
   * @param {MozTabbrowserTabGroup} tabGroup
   *   Tab group browser element
   * @returns {TabGroupStateData}
   *   Serialized tab group data
   */
  collect(tabGroup) {
    return {
      id: tabGroup.id,
      name: tabGroup.label,
      color: tabGroup.color,
      collapsed: tabGroup.collapsed,
      saveOnWindowClose: tabGroup.saveOnWindowClose,
    };
  }

  /**
   * Create initial state for a tab group that is about to close inside of an
   * open window.
   *
   * The caller is responsible for hydrating closed tabs data into `tabs`
   * using the `TabState` class.
   *
   * @param {MozTabbrowserTabGroup} tabGroup
   * @param {string} sourceWindowId
   *   `window.__SSi` window ID of the open window where the tab group is closing.
   * @returns {ClosedTabGroupStateData}
   */
  closed(tabGroup, sourceWindowId) {
    let closedData = this.collect(tabGroup);
    closedData.closedAt = Date.now();
    closedData.sourceWindowId = sourceWindowId;
    closedData.tabs = [];
    closedData.splitViews = [];
    return closedData;
  }

  /**
   * Create initial state for a tab group that is about to be explicitly saved
   * by the user inside of an open window.
   *
   * The caller is responsible for hydrating closed tabs data into `tabs`
   * using the `TabState` class.
   *
   * @param {MozTabbrowserTabGroup} tabGroup
   * @param {string} sourceWindowId
   *   `window.__SSi` window ID of the open window where the tab group
   *   is being saved.
   * @returns {SavedTabGroupStateData}
   */
  savedInOpenWindow(tabGroup, sourceWindowId) {
    let savedData = this.closed(tabGroup, sourceWindowId);
    savedData.saved = true;
    return savedData;
  }

  /**
   * Convert an existing tab group's state to saved tab group state. The input
   * tab group state should come from a closing/closed window; if you need the
   * state of a tab group that still exists in the browser, use `savedInOpenWindow`
   * instead. This should be used when a tab group is being saved automatically
   * due to a user closing a window containing some tab groups.
   *
   * The caller is responsible for hydrating closed tabs data into `tabs`
   * using the `TabState` class.
   *
   * @param {TabGroupStateData} tabGroupState
   * @param {number} windowClosedId
   *   `WindowStateData.closedId` of the closed window from which this tab group
   *   should be automatically saved.
   */
  savedInClosedWindow(tabGroupState, windowClosedId) {
    let savedData = tabGroupState;
    savedData.saved = true;
    savedData.closedAt = Date.now();
    savedData.windowClosedId = windowClosedId;
    savedData.tabs = [];
    savedData.splitViews = [];
    return savedData;
  }

  /**
   * In cases where we surface tab group metadata to external callers, we may want
   * to provide an abbreviated set of metadata. This can help hide internal details
   * from browser code or from extensions. Hiding those details can make the public
   * session APIs simpler and more stable.
   *
   * @param {TabGroupStateData} tabGroupState
   * @returns {TabGroupStateData}
   */
  abbreviated(tabGroupState) {
    let abbreviatedData = {
      id: tabGroupState.id,
      name: tabGroupState.name,
      color: tabGroupState.color,
      collapsed: tabGroupState.collapsed,
    };
    return abbreviatedData;
  }
}

export const TabGroupState = new _TabGroupState();
