/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

const lazy = {};

ChromeUtils.defineESModuleGetters(lazy, {
  PrivateBrowsingUtils: "resource://gre/modules/PrivateBrowsingUtils.sys.mjs",
  SessionStore:
    "moz-src:///browser/components/sessionstore/SessionStore.sys.mjs",
});

const PREF = "sidebar.openTabsPanel.collapsedWindows";

/**
 * Process-wide owner of the per-window collapsed state for the sidebar Open
 * Tabs panel.
 *
 * State is keyed by the SessionStore window id (`window.__SSi`) and persisted
 * to the `sidebar.openTabsPanel.collapsedWindows` pref as a JSON object whose
 * keys are window ids and whose values are `true` for collapsed cards. Entries
 * are dropped when a window closes or when a hydration sweep finds an entry
 * whose window is no longer open. Private windows never write to the pref.
 *
 * Consumers add a listener for the `CollapsedWindowsChanged` event and call
 * `requestUpdate()` (or equivalent) on every dispatch. The pref is the single
 * source of truth.
 */
class SidebarCollapsedWindowsImpl extends EventTarget {
  #map = new Map();
  #initialized = false;
  #observer = null;

  #ensureInitialized() {
    if (this.#initialized) {
      return;
    }
    this.#initialized = true;
    this.#hydrate();
    const self = this;
    this.#observer = {
      QueryInterface: ChromeUtils.generateQI(["nsIObserver"]),
      observe(subject, topic) {
        if (topic === "domwindowclosed") {
          self.#onWindowClosed(subject);
        } else if (topic === "quit-application-granted") {
          Services.obs.removeObserver(self.#observer, "domwindowclosed");
          Services.obs.removeObserver(
            self.#observer,
            "quit-application-granted"
          );
        }
      },
    };
    Services.obs.addObserver(this.#observer, "domwindowclosed");
    Services.obs.addObserver(this.#observer, "quit-application-granted");
  }

  #hydrate() {
    let parsed;
    try {
      parsed = JSON.parse(Services.prefs.getStringPref(PREF, "{}"));
    } catch (ex) {
      parsed = {};
    }
    if (!parsed || typeof parsed !== "object") {
      parsed = {};
    }
    const liveIds = this.#collectLiveWindowIds();
    let trimmed = false;
    for (const [id, collapsed] of Object.entries(parsed)) {
      if (liveIds.has(id) && collapsed) {
        this.#map.set(id, true);
      } else if (!liveIds.has(id)) {
        trimmed = true;
      }
    }
    if (trimmed) {
      this.#persist();
    }
  }

  #collectLiveWindowIds() {
    const ids = new Set();
    for (const win of lazy.SessionStore.getWindows({ private: false })) {
      const id = lazy.SessionStore.getWindowId(win);
      if (id) {
        ids.add(id);
      }
    }
    return ids;
  }

  #persist() {
    Services.prefs.setStringPref(
      PREF,
      JSON.stringify(Object.fromEntries(this.#map))
    );
  }

  #setCollapsedById(windowId, collapsed) {
    this.#ensureInitialized();
    if (!windowId) {
      return;
    }
    const win = lazy.SessionStore.getWindowById(windowId);
    if (win && lazy.PrivateBrowsingUtils.isWindowPrivate(win)) {
      return;
    }
    const wasCollapsed = this.#map.get(windowId) === true;
    if (collapsed === wasCollapsed) {
      return;
    }
    if (collapsed) {
      this.#map.set(windowId, true);
    } else {
      this.#map.delete(windowId);
    }
    this.#persist();
    this.dispatchEvent(new CustomEvent("CollapsedWindowsChanged"));
  }

  #onWindowClosed(win) {
    if (lazy.PrivateBrowsingUtils.isWindowPrivate(win)) {
      return;
    }
    // The user can also quit Firefox by clicking the X on the only browser
    // window. In that path the close itself triggers shutdown, so the
    // `quit-application-granted` notification fires AFTER this observer
    // runs and our unregister never gets a chance. Count the remaining
    // non-private browser windows. At domwindowclosed time the closing
    // window is already gone from the enumerator, so zero means it WAS the
    // last one and Firefox is about to quit. Preserve the entry; the
    // hydration bookkeeping on next launch drops it if no window comes back
    // with the same id.
    if (lazy.SessionStore.getWindows({ private: false }).length === 0) {
      return;
    }
    const id = lazy.SessionStore.getWindowId(win);
    if (!id || !this.#map.has(id)) {
      return;
    }
    this.#map.delete(id);
    this.#persist();
    this.dispatchEvent(new CustomEvent("CollapsedWindowsChanged"));
  }

  isCollapsed(win) {
    this.#ensureInitialized();
    if (!win || lazy.PrivateBrowsingUtils.isWindowPrivate(win)) {
      return false;
    }
    const id = lazy.SessionStore.getWindowId(win);
    return id ? this.#map.get(id) === true : false;
  }

  collapseWindow(win) {
    if (!win) {
      return;
    }
    const id = lazy.SessionStore.getWindowId(win);
    if (id) {
      this.#setCollapsedById(id, true);
    }
  }

  expandWindow(win) {
    if (!win) {
      return;
    }
    const id = lazy.SessionStore.getWindowId(win);
    if (id) {
      this.#setCollapsedById(id, false);
    }
  }

  collapseWindowById(windowId) {
    this.#setCollapsedById(windowId, true);
  }

  expandWindowById(windowId) {
    this.#setCollapsedById(windowId, false);
  }
}

export const SidebarCollapsedWindows = new SidebarCollapsedWindowsImpl();
