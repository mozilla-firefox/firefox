/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/**
 * Policy and actions shared by every "move tab to group" menu: the tab strip's
 * tab context menu, the all tabs panel, and the Firefox View open tabs menu.
 * Each caller owns its own markup and any surface-specific follow-up (which
 * window to focus, which panel to close); everything that decides *what* the
 * menu offers and *what happens* to the tabs lives here so the surfaces can't
 * drift apart.
 *
 * Every entry point takes an array of tabs, since the tab strip acts on the
 * multiselection while Firefox View acts on a single tab.
 */

const lazy = {};

ChromeUtils.defineESModuleGetters(lazy, {
  PrivateBrowsingUtils: "resource://gre/modules/PrivateBrowsingUtils.sys.mjs",
  SessionStore:
    "moz-src:///browser/components/sessionstore/SessionStore.sys.mjs",
});

/**
 * Supported tab group colors, in the order the group editor shows them.
 *
 * @typedef {"blue"|"purple"|"cyan"|"orange"|"yellow"|"pink"|"green"|"gray"|"red"} TabGroupColor
 */

/** @type {readonly TabGroupColor[]} */
const COLORS = Object.freeze([
  "blue",
  "purple",
  "cyan",
  "orange",
  "yellow",
  "pink",
  "green",
  "gray",
  "red",
]);

/**
 * Used when a group's color isn't one we have chicklet variables for, so a
 * menu item never renders without a chicklet color.
 *
 * @type {TabGroupColor}
 */
const FALLBACK_COLOR = "gray";

/**
 * @param {MozTabbrowserTab[]} tabs
 * @returns {object?}
 */
function tabbrowserFor(tabs) {
  return tabs?.[0]?.documentGlobal?.gBrowser ?? null;
}

/**
 * The custom properties a tab group chicklet reads, resolved for one group
 * color. Callers hand these to `style.setProperty` (chrome menus) or to lit's
 * `styleMap` (Firefox View).
 *
 * The color is checked against COLORS rather than interpolated as-is, so a
 * group record can't put arbitrary text into a CSS value. An unrecognized
 * color falls back to FALLBACK_COLOR instead of producing no properties at
 * all, so a group that somehow carries a color we don't know about still gets
 * a visible chicklet in every menu.
 *
 * @param {TabGroupColor} color
 * @returns {Record<string, string>}
 */
function colorStyles(color) {
  let name = COLORS.includes(color) ? color : FALLBACK_COLOR;
  return {
    "--tab-group-color": `var(--tab-group-${name})`,
    "--tab-group-color-invert": `var(--tab-group-${name}-invert)`,
    "--tab-group-color-pale": `var(--tab-group-${name}-pale)`,
    "--tab-group-background-color": `var(--tab-group-${name})`,
  };
}

/**
 * The groups a "move tab to group" menu should offer for the given tabs.
 *
 * Open groups exclude the tabs' own group, but only when the tabs are all
 * grouped and all in the same group: a mixed selection can still be collected
 * into any of the groups it partly occupies. For a single tab this reduces to
 * "every group except mine".
 *
 * Saved groups are only offered when the tabs' window may save tabs at all.
 *
 * @param {MozTabbrowserTab[]} tabs
 * @returns {{
 *   openGroups: MozTabbrowserTabGroup[],
 *   savedGroups: object[],
 *   groupCount: number,
 * }}
 *   `groupCount` is how many distinct groups the tabs are spread across, which
 *   the tab strip also uses to label its ungroup items.
 */
function getGroupsToMoveTo(tabs) {
  let gBrowser = tabbrowserFor(tabs);
  if (!gBrowser) {
    return { openGroups: [], savedGroups: [], groupCount: 0 };
  }

  // The filter removes the "null" group for ungrouped tabs.
  let groupCount = new Set(tabs.map(tab => tab.group).filter(group => group))
    .size;

  let openGroups = gBrowser.getAllTabGroups({ sortByLastSeenActive: true });
  if (groupCount == 1) {
    let groupToFilter = tabs[0].group;
    if (groupToFilter && tabs.every(tab => tab.group)) {
      openGroups = openGroups.filter(group => group !== groupToFilter);
    }
  }

  let savedGroups = [];
  if (
    !lazy.PrivateBrowsingUtils.isWindowPrivate(tabs[0].documentGlobal) &&
    lazy.SessionStore.shouldSaveTabsToGroup(tabs)
  ) {
    savedGroups = lazy.SessionStore.getSavedTabGroups();
  }

  return { openGroups, savedGroups, groupCount };
}

/**
 * Where a group created from `anchorTab` should be inserted. A pinned tab
 * groups before the first unpinned tab, since groups can't hold pinned tabs. A
 * tab that is already grouped groups before its group rather than inside it.
 *
 * @param {MozTabbrowserTab} anchorTab
 *   The tab the menu was opened on.
 * @returns {MozTabbrowserTab|MozTabbrowserTabGroup|MozTabSplitViewWrapper?}
 */
function getNewGroupInsertBefore(anchorTab) {
  let gBrowser = anchorTab.documentGlobal.gBrowser;
  if (anchorTab.index < gBrowser.pinnedTabCount) {
    let firstUnpinnedTab = gBrowser.tabs[gBrowser.pinnedTabCount];
    // Every tab is pinned, so there is nothing to insert before.
    if (!firstUnpinnedTab) {
      return null;
    }
    return firstUnpinnedTab.splitview ?? firstUnpinnedTab;
  }
  return anchorTab.group ?? anchorTab.splitview ?? anchorTab;
}

/**
 * Creates a group holding `tabs`, positioned relative to `anchorTab`.
 *
 * @param {MozTabbrowserTab[]} tabs
 * @param {object} options
 * @param {MozTabbrowserTab} [options.anchorTab]
 *   The tab the menu was opened on, which may not be `tabs[0]` when a
 *   multiselection is involved. Defaults to the first tab.
 * @param {TabMetricsContext} options.metricsContext
 * @returns {MozTabbrowserTabGroup}
 */
function createGroupFromTabs(tabs, { anchorTab = tabs[0], metricsContext }) {
  return tabbrowserFor(tabs).addTabGroup(tabs, {
    insertBefore: getNewGroupInsertBefore(anchorTab),
    metricsContext,
  });
}

/**
 * Adds `tabs` to an open group, moving split views as a unit.
 *
 * @param {MozTabbrowserTab[]} tabs
 * @param {MozTabbrowserTabGroup} group
 * @param {TabMetricsContext} metricsContext
 */
function addTabsToGroup(tabs, group, metricsContext) {
  let elementsToMove = new Set();
  for (let tab of tabs) {
    elementsToMove.add(tab.splitview ?? tab);
  }
  group.addTabs(Array.from(elementsToMove), metricsContext);
  group.documentGlobal.focus();
}

/**
 * Removes `tabs` from whichever groups they are in. A split view leaves its
 * group as a unit, so it is ungrouped once however many of its tabs are named.
 *
 * @param {MozTabbrowserTab[]} tabs
 */
function ungroupTabs(tabs) {
  let gBrowser = tabbrowserFor(tabs);
  if (!gBrowser) {
    return;
  }
  let splitViewsSeen = new Set();
  for (let tab of tabs) {
    if (!tab.splitview) {
      gBrowser.ungroupTab(tab);
    } else if (!splitViewsSeen.has(tab.splitview)) {
      splitViewsSeen.add(tab.splitview);
      gBrowser.ungroupSplitView(tab.splitview);
    }
  }
}

/**
 * Saves `tabs` into a closed group and closes them. Split views are saved as
 * their individual tabs, since a saved group is just a list of tabs.
 *
 * @param {MozTabbrowserTab[]} tabs
 * @param {string} savedGroupId
 * @param {TabMetricsContext} metricsContext
 */
function addTabsToSavedGroup(tabs, savedGroupId, metricsContext) {
  let tabsToSave = new Set();
  for (let tab of tabs) {
    if (tab.splitview) {
      for (let splitViewTab of tab.splitview.tabs) {
        tabsToSave.add(splitViewTab);
      }
    } else {
      tabsToSave.add(tab);
    }
  }
  let gBrowser = tabbrowserFor(tabs);
  let tabsToRemove = Array.from(tabsToSave);
  lazy.SessionStore.addTabsToSavedGroup(
    savedGroupId,
    tabsToRemove,
    metricsContext
  );
  gBrowser.removeTabs(tabsToRemove, { animate: true, metricsContext });
}

export const TabGroupMenu = {
  COLORS,
  colorStyles,
  getGroupsToMoveTo,
  getNewGroupInsertBefore,
  createGroupFromTabs,
  addTabsToGroup,
  addTabsToSavedGroup,
  ungroupTabs,
};
