/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

const lazy = {};

ChromeUtils.defineESModuleGetters(lazy, {
  E10SUtils: "resource://gre/modules/E10SUtils.sys.mjs",
  SessionStore:
    "moz-src:///browser/components/sessionstore/SessionStore.sys.mjs",
  TabGroupState:
    "moz-src:///browser/components/sessionstore/TabGroupState.sys.mjs",
});

var SessionMigrationInternal = {
  /**
   * Convert the original session restore state into a minimal state. It will
   * only contain:
   * - open windows
   *   - with tabs
   *     - with history entries with only title, url, triggeringPrincipal
   *     - with pinned state
   *     - with tab group info
   *     - with selected tab info
   *   - with selected window info
   * - saved tab groups
   *
   * The complete state is then wrapped into the "about:welcomeback" page as
   * form field info to be restored when restoring the state.
   */
  convertState(aStateObj) {
    let state = {
      selectedWindow: aStateObj.selectedWindow,
      _closedWindows: [],
    };
    let savedGroups = aStateObj.savedGroups || [];
    state.windows = aStateObj.windows.map(function (oldWin) {
      var win = { extData: {} };
      if (oldWin.groups) {
        win.groups = oldWin.groups;
      }
      let groupsToSave = new Map();
      win.tabs = oldWin.tabs.map(function (oldTab) {
        var tab = {};
        // Keep only titles, urls and triggeringPrincipals for history entries
        tab.entries = oldTab.entries.map(function (entry) {
          return {
            url: entry.url,
            triggeringPrincipal_base64: entry.triggeringPrincipal_base64,
            title: entry.title,
          };
        });
        tab.index = oldTab.index;
        tab.hidden = oldTab.hidden;
        tab.pinned = oldTab.pinned;
        if (oldTab.groupId) {
          tab.groupId = oldTab.groupId;
          let groupStateToSave = oldWin.groups.find(
            groupState => groupState.id == oldTab.groupId
          );
          let groupToSave = groupsToSave.get(groupStateToSave.id);
          if (!groupToSave) {
            groupToSave =
              lazy.TabGroupState.savedInClosedWindow(groupStateToSave);
            // If the session is manually restored, these groups will be removed from the saved groups list
            // to prevent duplication.
            groupToSave.removeAfterRestore = true;
            groupsToSave.set(groupStateToSave.id, groupToSave);
          }
          groupToSave.tabs.push(
            lazy.SessionStore.formatTabStateForSavedGroup(tab)
          );
        }
        return tab;
      });
      groupsToSave.forEach(groupState => {
        const alreadySavedGroup = savedGroups.find(
          existingGroup => existingGroup.id == groupState.id
        );
        if (alreadySavedGroup) {
          alreadySavedGroup.removeAfterRestore = true;
        } else {
          savedGroups.push(groupState);
        }
      });
      win.selected = oldWin.selected;
      win._closedTabs = [];
      return win;
    });
    let url = "about:welcomeback";
    let formdata = { id: { sessionData: state }, url };
    let entry = {
      url,
      triggeringPrincipal_base64: lazy.E10SUtils.SERIALIZED_SYSTEMPRINCIPAL,
    };
    return {
      windows: [{ tabs: [{ entries: [entry], formdata }] }],
      savedGroups,
    };
  },
  /**
   * Asynchronously read session restore state (JSON) from a path
   */
  readState(aPath) {
    return IOUtils.readJSON(aPath, { decompress: true });
  },
  /**
   * Asynchronously write session restore state as JSON to a path
   */
  writeState(aPath, aState) {
    return IOUtils.writeJSON(aPath, aState, {
      compress: true,
      tmpPath: `${aPath}.tmp`,
    });
  },
};

export var SessionMigration = {
  /**
   * Migrate a limited set of session data from one path to another.
   */
  migrate(aFromPath, aToPath) {
    return (async function () {
      let inState = await SessionMigrationInternal.readState(aFromPath);
      let outState = SessionMigrationInternal.convertState(inState);
      // Unfortunately, we can't use SessionStore's own SessionFile to
      // write out the data because it has a dependency on the profile dir
      // being known. When the migration runs, there is no guarantee that
      // that's true.
      await SessionMigrationInternal.writeState(aToPath, outState);
    })();
  },
};
