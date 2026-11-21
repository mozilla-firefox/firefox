/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

export const TabSplitViewState = {
  /**
   * Removes incomplete split views, corrects the member count of the others,
   * and deletes split view metadata from tabs that are no longer in a split.
   * A split becomes incomplete when one of its tabs isn't worth saving, for
   * example an about:privatebrowsing tab. See bug 2079808.
   *
   * @param {TabStateData[]} tabs
   * @param {TabSplitViewStateData[]} splitViews
   * @returns {TabSplitViewStateData[]} The remaining split definitions.
   */
  normalize(tabs, splitViews = []) {
    let members = new Map();
    for (let tab of tabs) {
      if (tab.splitViewId && !tab.pinned) {
        members.set(tab.splitViewId, (members.get(tab.splitViewId) ?? 0) + 1);
      }
    }

    let retained = new Map();
    for (let splitView of splitViews) {
      let numberOfTabs = members.get(splitView.id) ?? 0;
      if (numberOfTabs > 1) {
        retained.set(splitView.id, { ...splitView, numberOfTabs });
      }
    }
    for (let tab of tabs) {
      if (tab.pinned || !retained.has(tab.splitViewId)) {
        delete tab.splitViewId;
      }
    }
    return [...retained.values()];
  },

  /**
   * Update split metadata after filtering a session's tab collections.
   *
   * @param {object} state
   */
  normalizeSession(state) {
    for (let window of [...state.windows, ...(state._closedWindows ?? [])]) {
      window.splitViews = this.normalize(window.tabs, window.splitViews);
      for (let group of window.closedGroups ?? []) {
        group.splitViews = this.normalize(
          group.tabs.map(tab => tab.state),
          group.splitViews
        );
      }
    }
    for (let group of state.savedGroups ?? []) {
      group.splitViews = this.normalize(
        group.tabs.map(tab => tab.state),
        group.splitViews
      );
    }
    if (state.lastSessionState) {
      this.normalizeSession(state.lastSessionState);
    }
  },
};
