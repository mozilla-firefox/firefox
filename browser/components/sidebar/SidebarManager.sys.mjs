/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

import { AppConstants } from "resource://gre/modules/AppConstants.sys.mjs";
import { XPCOMUtils } from "resource://gre/modules/XPCOMUtils.sys.mjs";

const BACKUP_STATE_PREF = "sidebar.backupState";
const VISIBILITY_SETTING_PREF = "sidebar.visibility";
const SIDEBAR_TOOLS = "sidebar.main.tools";
const VERTICAL_TABS_PREF = "sidebar.verticalTabs";
const INSTALLED_EXTENSIONS = "sidebar.installed.extensions";
const PINNED_PROMO_PREF = "sidebar.verticalTabs.dragToPinPromo.dismissed";

// Visibility values are orientation-exclusive: each value implies the tab
// orientation it belongs to, so a stored value is never ambiguous.
const VERTICAL_VISIBILITIES = [
  "always-show",
  "expand-on-hover",
  "hide-sidebar",
];
const HORIZONTAL_VISIBILITIES = ["hide-on-close", "hide-launcher"];
const DEFAULT_VERTICAL_VISIBILITY = "always-show";
const DEFAULT_HORIZONTAL_VISIBILITY = "hide-on-close";

// New panels that are ready to be introduced to new sidebar users should be added to this list;
// ensure your feature flag is enabled at the same time you do this and that its the same value as
// what you added to .
const DEFAULT_LAUNCHER_TOOLS = "aichat,syncedtabs,history,bookmarks,opentabs";
const lazy = {};
ChromeUtils.defineESModuleGetters(lazy, {
  BrowserWindowTracker: "resource:///modules/BrowserWindowTracker.sys.mjs",
  CustomizableUI:
    "moz-src:///browser/components/customizableui/CustomizableUI.sys.mjs",
  NimbusFeatures: "resource://nimbus/ExperimentAPI.sys.mjs",
  PrefUtils: "moz-src:///toolkit/modules/PrefUtils.sys.mjs",
  SessionStore:
    "moz-src:///browser/components/sessionstore/SessionStore.sys.mjs",
  SidebarState: "moz-src:///browser/components/sidebar/SidebarState.sys.mjs",
});
XPCOMUtils.defineLazyPreferenceGetter(lazy, "sidebarNimbus", "sidebar.nimbus");

XPCOMUtils.defineLazyPreferenceGetter(
  lazy,
  "sidebarBackupState",
  BACKUP_STATE_PREF
);

XPCOMUtils.defineLazyPreferenceGetter(
  lazy,
  "verticalTabsEnabled",
  VERTICAL_TABS_PREF,
  false,
  (pref, oldVal, newVal) => {
    sidebarManager.handleVerticalTabsPrefChange(newVal);
  }
);

XPCOMUtils.defineLazyPreferenceGetter(
  lazy,
  "sidebarRevampEnabled",
  "sidebar.revamp",
  false,
  (pref, oldVal, newVal) => {
    sidebarManager.updateDefaultTools();

    if (!newVal) {
      // Disable vertical tabs if revamped sidebar is turned off
      Services.prefs.setBoolPref("sidebar.verticalTabs", false);
    } else if (newVal && !lazy.verticalTabsEnabled) {
      // Horizontal tabs with sidebar.revamp default to "hide-on-close"; users
      // can opt into the switcher-only "hide-launcher" from the customize panel.
      Services.prefs.setStringPref(
        VISIBILITY_SETTING_PREF,
        DEFAULT_HORIZONTAL_VISIBILITY
      );
    }
  }
);

XPCOMUtils.defineLazyPreferenceGetter(lazy, "sidebarTools", SIDEBAR_TOOLS, "");
XPCOMUtils.defineLazyPreferenceGetter(
  lazy,
  "sidebarExtensions",
  INSTALLED_EXTENSIONS,
  ""
);

XPCOMUtils.defineLazyPreferenceGetter(
  lazy,
  "newSidebarHasBeenUsed",
  "sidebar.new-sidebar.has-used",
  false,
  () => sidebarManager.updateDefaultTools()
);

XPCOMUtils.defineLazyPreferenceGetter(
  lazy,
  "dragToPinPromoDismissed",
  PINNED_PROMO_PREF,
  false
);

class SidebarManager extends EventTarget {
  /**
   * SidebarManager is a singleton that handles startup tasks like telemetry,
   * adding listeners and updating sidebar-related preferences.
   */
  constructor() {
    super();
    this.checkForPinnedTabsComplete = false;
  }
  #initialized = false;
  // Preserves an "expand-on-hover" choice while in horizontal tabs so it can be
  // restored when switching back to vertical tabs.
  #savedVisibility = null;
  init() {
    lazy.CustomizableUI.addListener(this);

    Services.prefs.addObserver(
      "sidebar.newTool.migration.",
      this.updateDefaultTools.bind(this)
    );
    this.updateDefaultTools();
    lazy.SessionStore.promiseAllWindowsRestored.then(() => {
      this.checkForPinnedTabs();
    });

    // Move the visibility to the orientation's default if the current value
    // isn't valid for it (e.g. a value carried over from the other orientation).
    this.handleVerticalTabsPrefChange(lazy.verticalTabsEnabled);

    // Handle nimbus feature pref setting updates on init and enrollment
    lazy.NimbusFeatures.sidebar.onUpdate(() => {
      if (this.#initialized) {
        this.onNimbusFeatureUpdate();
      } else {
        // Schedule handling the update after this module has finished initializing
        Promise.resolve().then(() => this.onNimbusFeatureUpdate());
      }
    });
    this.#initialized = true;
  }

  onNimbusFeatureUpdate() {
    const featureId = "sidebar";
    // Set prefs only if we have an enrollment that's new
    const enrollment = lazy.NimbusFeatures[featureId].getEnrollmentMetadata();
    if (!enrollment) {
      return;
    }
    const slug = enrollment.slug + ":" + enrollment.branch;
    if (slug == lazy.sidebarNimbus) {
      return;
    }

    // Enforce minimum version by skipping pref changes until Firefox restarts
    // with the appropriate version
    if (
      Services.vc.compare(
        // Support betas, e.g., 132.0b1, instead of MOZ_APP_VERSION
        AppConstants.MOZ_APP_VERSION_DISPLAY,
        // Check configured version or compare with unset handled as 0
        lazy.NimbusFeatures[featureId].getVariable("minVersion")
      ) < 0
    ) {
      return;
    }

    // Set/override user prefs to persist after experiment end
    const setPref = (pref, value) => {
      // Only set prefs with a value (so no clearing)
      if (value != null) {
        lazy.PrefUtils.setPref("sidebar." + pref, value);
      }
    };
    setPref("nimbus", slug);
    ["revamp", "verticalTabs", "visibility"].forEach(pref =>
      setPref(pref, lazy.NimbusFeatures[featureId].getVariable(pref))
    );
  }

  /**
   * Ensure the drag-to-pin promo card is not displayed to existing users who already have pinned tabs.
   */
  checkForPinnedTabs() {
    if (!lazy.dragToPinPromoDismissed) {
      for (let win of lazy.BrowserWindowTracker.getOrderedWindows()) {
        if (win.gBrowser.pinnedTabCount > 0) {
          Services.prefs.setBoolPref(PINNED_PROMO_PREF, true);
          break;
        }
      }
    }
    this.checkForPinnedTabsComplete = true;
    this.dispatchEvent(new CustomEvent("checkForPinnedTabsComplete"));
  }

  /**
   * Called when any widget is removed. We're only interested in the sidebar
   * button. Note that this is also invoked if the button is merely moved
   * to another area.
   *
   * @param {string} aWidgetId
   *   The widget being removed.
   */
  async onWidgetRemoved(aWidgetId) {
    if (aWidgetId == "sidebar-button") {
      // Wait for JS to run to completion. Once that has happened, we'll
      // know if we were _really_ removed or just moved elsewhere.
      await Promise.resolve();
      if (!lazy.CustomizableUI.getPlacementOfWidget(aWidgetId)) {
        // Removing sidebar button should force horizontal tabs (Bug 1970015).
        Services.prefs.setBoolPref(VERTICAL_TABS_PREF, false);
        this.closeAllSidebars();
      }
    }
  }

  /**
   * Convenience method to tell all sidebars to close when the toolbar button
   * is removed.
   */
  closeAllSidebars() {
    for (let w of lazy.BrowserWindowTracker.getOrderedWindows()) {
      if (w.SidebarController.isOpen) {
        w.SidebarController.hide();
      }
      w.SidebarController._state.loadCurrentState({
        ...lazy.SidebarState.defaultProperties,
      });
    }
  }

  /**
   * Adjust for a change to the verticalTabs pref. Visibility values are
   * orientation-exclusive, so we only move to an orientation's default when the
   * current visibility isn't valid for the active orientation. A value that is
   * already valid is preserved, including an explicit user choice such as
   * "hide-sidebar"; an "expand-on-hover" choice is saved when leaving vertical
   * tabs and restored when returning.
   */
  handleVerticalTabsPrefChange(isEnabled) {
    const currentVisibility = Services.prefs.getStringPref(
      VISIBILITY_SETTING_PREF,
      DEFAULT_VERTICAL_VISIBILITY
    );
    if (!isEnabled) {
      // Switching to (or initializing) horizontal tabs.
      if (currentVisibility === "expand-on-hover") {
        this.#savedVisibility = currentVisibility;
      }
      if (!HORIZONTAL_VISIBILITIES.includes(currentVisibility)) {
        Services.prefs.setStringPref(
          VISIBILITY_SETTING_PREF,
          DEFAULT_HORIZONTAL_VISIBILITY
        );
      }
    } else if (!VERTICAL_VISIBILITIES.includes(currentVisibility)) {
      // Switching to (or initializing) vertical tabs: restore a preserved
      // choice (always-show, expand-on-hover, hide-sidebar) otherwise fall
      // back to the vertical default.
      Services.prefs.setStringPref(
        VISIBILITY_SETTING_PREF,
        this.#savedVisibility ?? DEFAULT_VERTICAL_VISIBILITY
      );
      this.#savedVisibility = null;
    }
  }

  /**
   * Has the new sidebar launcher already been visible and "used" in this profile?
   */
  get hasSidebarLauncherBeenVisible() {
    // Its possible sidebar.revamp was enabled previously, but we can effectively reset if its currently false
    if (!lazy.sidebarRevampEnabled) {
      return false;
    }
    if (lazy.verticalTabsEnabled) {
      return true;
    }
    // this pref tells us a sidebar panel has been opened, so it implies the launcher has
    // been visible, but can't reliably indicate that the launcher has *not* been visible.
    if (Services.prefs.getBoolPref("sidebar.new-sidebar.has-used", false)) {
      return true;
    }
    // check if the launcher has ever been visible (in this session) in any of our open windows,
    for (let w of lazy.BrowserWindowTracker.getOrderedWindows()) {
      if (w.SidebarController.launcherEverVisible) {
        return true;
      }
    }
    return false;
  }

  /**
   * Prepopulates default tools for new sidebar users and appends any new tools defined
   * on the sidebar.newTool.migration pref branch to the sidebar.main.tools pref.
   */
  updateDefaultTools() {
    if (!lazy.sidebarRevampEnabled) {
      return;
    }
    let tools = lazy.sidebarTools;

    // For new sidebar.revamp users, we pre-populate a set of default tools to show in the launcher.
    if (!tools && !lazy.newSidebarHasBeenUsed) {
      tools = DEFAULT_LAUNCHER_TOOLS;
    }

    for (const pref of Services.prefs.getChildList(
      "sidebar.newTool.migration."
    )) {
      try {
        let options = JSON.parse(Services.prefs.getStringPref(pref));
        let newTool = pref.split(".")[3];

        if (options?.alreadyShown) {
          continue;
        }

        if (options?.visibilityPref) {
          // Will only add the tool to the launcher if the panel governing a panels sidebar visibility
          // is first enabled
          let visibilityPrefValue = Services.prefs.getBoolPref(
            options.visibilityPref
          );
          if (!visibilityPrefValue) {
            Services.prefs.addObserver(
              options.visibilityPref,
              this.updateDefaultTools.bind(this)
            );
            continue;
          }
        }
        // avoid adding a tool from the pref branch where it's already been added to the DEFAULT_LAUNCHER_TOOLS (for new users)
        if (!tools.includes(newTool)) {
          tools += "," + newTool;
        }
        options.alreadyShown = true;
        Services.prefs.setStringPref(pref, JSON.stringify(options));
      } catch (ex) {
        console.error("Failed to handle pref " + pref, ex);
      }
    }
    if (tools.length > lazy.sidebarTools.length) {
      Services.prefs.setStringPref(SIDEBAR_TOOLS, tools);
    }
  }

  updateToolsPref(toolName, remove = null) {
    const updatedTools = lazy.sidebarTools ? lazy.sidebarTools.split(",") : [];
    const index = updatedTools.indexOf(toolName);

    if ((remove && index == -1) || (!remove && index != -1)) {
      return;
    }

    if (remove) {
      updatedTools.splice(index, 1);
    } else {
      updatedTools.push(toolName);
    }

    Services.prefs.setStringPref(SIDEBAR_TOOLS, updatedTools.join());
  }

  clearExtensionsPref(toolName) {
    let installedExtensions = lazy.sidebarExtensions
      ? lazy.sidebarExtensions.split(",")
      : [];
    const index = installedExtensions.indexOf(toolName);
    if (index != -1) {
      installedExtensions.splice(index, 1);
      Services.prefs.setStringPref(
        INSTALLED_EXTENSIONS,
        installedExtensions.join()
      );
    }
  }

  cleanupPrefs(id) {
    this.clearExtensionsPref(id);
    this.updateToolsPref(id, true);
  }

  /**
   * Return a list of tool IDs that have registered a badge for notification.
   * This reads all prefs under "sidebar.notification.badge."
   *
   * @returns {Array}
   */
  getBadgeTools() {
    const BADGE_PREF_BRANCH = "sidebar.notification.badge.";
    const badgePrefs = Services.prefs.getChildList(BADGE_PREF_BRANCH);

    return badgePrefs.map(pref => pref.slice(BADGE_PREF_BRANCH.length));
  }

  /**
   * Provide a system-level "backup" state to be stored for those using "Never
   * remember history" or "Clear history when browser closes".
   *
   * If it doesn't exist or isn't parsable, return `null`.
   *
   * @returns {object}
   */
  getBackupState() {
    try {
      return JSON.parse(lazy.sidebarBackupState);
    } catch (e) {
      Services.prefs.clearUserPref(BACKUP_STATE_PREF);
      return null;
    }
  }

  /**
   * Set the backup state.
   *
   * @param {object} state
   */
  setBackupState(state) {
    if (!state) {
      return;
    }
    Services.prefs.setStringPref(BACKUP_STATE_PREF, JSON.stringify(state));
  }
}

// Initialize on first import
const sidebarManager = new SidebarManager();
sidebarManager.init();
export { sidebarManager as SidebarManager };
