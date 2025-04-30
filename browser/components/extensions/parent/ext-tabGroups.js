/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */
"use strict";

ChromeUtils.defineESModuleGetters(this, {
  PrivateBrowsingUtils: "resource://gre/modules/PrivateBrowsingUtils.sys.mjs",
});

var { ExtensionError } = ExtensionUtils;

const spellColour = color => (color === "grey" ? "gray" : color);

this.tabGroups = class extends ExtensionAPIPersistent {
  queryGroups({ collapsed, color, title, windowId } = {}) {
    color = spellColour(color);
    let glob = title != null && new MatchGlob(title);
    let window =
      windowId != null && windowTracker.getWindow(windowId, null, false);
    return windowTracker
      .browserWindows()
      .filter(
        win =>
          this.extension.canAccessWindow(win) &&
          (windowId == null || win === window)
      )
      .flatMap(win => win.gBrowser.tabGroups)
      .filter(
        group =>
          (collapsed == null || group.collapsed === collapsed) &&
          (color == null || group.color === color) &&
          (title == null || glob.matches(group.name))
      );
  }

  get(groupId) {
    let gid = getInternalTabGroupIdForExtTabGroupId(groupId);
    if (!gid) {
      throw new ExtensionError(`No group with id: ${groupId}`);
    }
    for (let group of this.queryGroups()) {
      if (group.id === gid) {
        return group;
      }
    }
    throw new ExtensionError(`No group with id: ${groupId}`);
  }

  convert(group) {
    return {
      collapsed: !!group.collapsed,
      /** Internally we use "gray", but Chrome uses "grey" @see spellColour. */
      color: group.color === "gray" ? "grey" : group.color,
      id: getExtTabGroupIdForInternalTabGroupId(group.id),
      title: group.name,
      windowId: windowTracker.getId(group.ownerGlobal),
    };
  }

  PERSISTENT_EVENTS = {
    onCreated({ fire }) {
      let onCreate = event => {
        if (event.detail.isAdoptingGroup) {
          // Tab group moved from a different window.
          return;
        }
        fire.async(this.convert(event.originalTarget));
      };
      windowTracker.addListener("TabGroupCreate", onCreate);
      return {
        unregister() {
          windowTracker.removeListener("TabGroupCreate", onCreate);
        },
        convert(_fire) {
          fire = _fire;
        },
      };
    },
    onMoved({ fire }) {
      let onMove = event => {
        fire.async(this.convert(event.originalTarget));
      };
      let onCreate = event => {
        if (event.detail.isAdoptingGroup) {
          // Tab group moved from a different window.
          fire.async(this.convert(event.originalTarget));
        }
      };
      windowTracker.addListener("TabGroupMoved", onMove);
      windowTracker.addListener("TabGroupCreate", onCreate);
      return {
        unregister() {
          windowTracker.removeListener("TabGroupMoved", onMove);
          windowTracker.removeListener("TabGroupCreate", onCreate);
        },
        convert(_fire) {
          fire = _fire;
        },
      };
    },
    onRemoved({ fire }) {
      let onRemove = event => {
        if (event.originalTarget.removedByAdoption) {
          // Tab group moved to a different window.
          return;
        }
        fire.async(this.convert(event.originalTarget));
      };
      windowTracker.addListener("TabGroupRemoved", onRemove);
      return {
        unregister() {
          windowTracker.removeListener("TabGroupRemoved", onRemove);
        },
        convert(_fire) {
          fire = _fire;
        },
      };
    },
    onUpdated({ fire }) {
      let onUpdate = event => {
        fire.async(this.convert(event.originalTarget));
      };
      windowTracker.addListener("TabGroupCollapse", onUpdate);
      windowTracker.addListener("TabGroupExpand", onUpdate);
      windowTracker.addListener("TabGroupUpdate", onUpdate);
      return {
        unregister() {
          windowTracker.removeListener("TabGroupCollapse", onUpdate);
          windowTracker.removeListener("TabGroupExpand", onUpdate);
          windowTracker.removeListener("TabGroupUpdate", onUpdate);
        },
        convert(_fire) {
          fire = _fire;
        },
      };
    },
  };

  getAPI(context) {
    const { windowManager } = this.extension;
    return {
      tabGroups: {
        get: groupId => {
          return this.convert(this.get(groupId));
        },

        move: (groupId, { index, windowId }) => {
          let group = this.get(groupId);
          let win = group.ownerGlobal;

          if (windowId != null) {
            win = windowTracker.getWindow(windowId, context);
            if (
              PrivateBrowsingUtils.isWindowPrivate(group.ownerGlobal) !==
              PrivateBrowsingUtils.isWindowPrivate(win)
            ) {
              throw new ExtensionError(
                "Can't move groups between private and non-private windows"
              );
            }
            if (windowManager.getWrapper(win).type !== "normal") {
              throw new ExtensionError(
                "Groups can only be moved to normal windows."
              );
            }
          }

          if (win !== group.ownerGlobal) {
            let last = win.gBrowser.tabContainer.ariaFocusableItems.length + 1;
            let elementIndex = index === -1 ? last : Math.min(index, last);
            group = win.gBrowser.adoptTabGroup(group, elementIndex);
          } else if (index >= 0 && index < win.gBrowser.tabs.length) {
            win.gBrowser.moveTabTo(group, { tabIndex: index });
          } else if (win.gBrowser.tabs.at(-1) !== group.tabs.at(-1)) {
            win.gBrowser.moveTabAfter(group, win.gBrowser.tabs.at(-1));
          }
          return this.convert(group);
        },

        query: query => {
          return Array.from(this.queryGroups(query ?? {}), group =>
            this.convert(group)
          );
        },

        update: (groupId, { collapsed, color, title }) => {
          let group = this.get(groupId);
          if (collapsed != null) {
            group.collapsed = collapsed;
          }
          if (color != null) {
            group.color = spellColour(color);
          }
          if (title != null) {
            group.name = title;
          }
          return this.convert(group);
        },

        onCreated: new EventManager({
          context,
          module: "tabGroups",
          event: "onCreated",
          extensionApi: this,
        }).api(),

        onMoved: new EventManager({
          context,
          module: "tabGroups",
          event: "onMoved",
          extensionApi: this,
        }).api(),

        onRemoved: new EventManager({
          context,
          module: "tabGroups",
          event: "onRemoved",
          extensionApi: this,
        }).api(),

        onUpdated: new EventManager({
          context,
          module: "tabGroups",
          event: "onUpdated",
          extensionApi: this,
        }).api(),
      },
    };
  }
};
