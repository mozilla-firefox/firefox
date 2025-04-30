/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

import { AppConstants } from "resource://gre/modules/AppConstants.sys.mjs";
import { XPCOMUtils } from "resource://gre/modules/XPCOMUtils.sys.mjs";

const BACKUP_STATE_PREF = "sidebar.backupState";
const VISIBILITY_SETTING_PREF = "sidebar.visibility";
const SIDEBAR_TOOLS = "sidebar.main.tools";

// New panels that are ready to be introduced to new sidebar users should be added to this list;
// ensure your feature flag is enabled at the same time you do this and that its the same value as
// what you added to .
const DEFAULT_LAUNCHER_TOOLS = "aichat,syncedtabs,history,bookmarks";
const lazy = {};
ChromeUtils.defineESModuleGetters(lazy, {
  BrowserWindowTracker: "resource:///modules/BrowserWindowTracker.sys.mjs",
  CustomizableUI: "resource:///modules/CustomizableUI.sys.mjs",
  ExperimentAPI: "resource://nimbus/ExperimentAPI.sys.mjs",
  NimbusFeatures: "resource://nimbus/ExperimentAPI.sys.mjs",
  PrefUtils: "resource://normandy/lib/PrefUtils.sys.mjs",
  SidebarState: "resource:///modules/SidebarState.sys.mjs",
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
  "sidebar.verticalTabs",
  false,
  (pref, oldVal, newVal) => {
    SidebarManager.handleVerticalTabsPrefChange(newVal, true);
  }
);

XPCOMUtils.defineLazyPreferenceGetter(
  lazy,
  "sidebarRevampEnabled",
  "sidebar.revamp",
  false,
  () => SidebarManager.updateDefaultTools()
);

XPCOMUtils.defineLazyPreferenceGetter(lazy, "sidebarTools", SIDEBAR_TOOLS, "");

XPCOMUtils.defineLazyPreferenceGetter(
  lazy,
  "newSidebarHasBeenUsed",
  "sidebar.new-sidebar.has-used",
  false,
  () => SidebarManager.updateDefaultTools()
);

export const SidebarManager = {
  /**
   * Handle startup tasks like telemetry, adding listeners.
   */
  init() {
    // Handle nimbus feature pref setting updates on init and enrollment
    const featureId = "sidebar";
    lazy.NimbusFeatures[featureId].onUpdate(() => {
      // Set prefs only if we have an enrollment that's new
      const feature = { featureId };
      const enrollment =
        lazy.ExperimentAPI.getExperimentMetaData(feature) ??
        lazy.ExperimentAPI.getRolloutMetaData(feature);
      if (!enrollment) {
        return;
      }
      const slug = enrollment.slug + ":" + enrollment.branch.slug;
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
    });

    lazy.CustomizableUI.addListener(this);

    Services.prefs.addObserver(
      "sidebar.newTool.migration.",
      this.updateDefaultTools.bind(this)
    );
    this.updateDefaultTools();

    // if there's no user visibility pref, we may need to update it to the default value for the tab orientation
    const shouldResetVisibility = !Services.prefs.prefHasUserValue(
      VISIBILITY_SETTING_PREF
    );
    this.handleVerticalTabsPrefChange(
      lazy.verticalTabsEnabled,
      shouldResetVisibility
    );
  },

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
        Services.prefs.setStringPref(VISIBILITY_SETTING_PREF, "hide-sidebar");
        this.closeAllSidebars();
      }
    }
  },

  /**
   * Convenience method to tell all sidebars to close when the toolbar button
   * is removed.
   */
  closeAllSidebars() {
    for (let w of lazy.BrowserWindowTracker.getOrderedWindows()) {
      if (w.SidebarController.isOpen) {
        w.SidebarController.hide();
      }
      w.SidebarController._state.loadInitialState({
        ...lazy.SidebarState.defaultProperties,
      });
    }
  },

  /**
   * Adjust for a change to the verticalTabs pref.
   */
  handleVerticalTabsPrefChange(isEnabled, resetVisibility = true) {
    if (!isEnabled) {
      // horizontal tabs can only have visibility of "hide-sidebar"
      Services.prefs.setStringPref(VISIBILITY_SETTING_PREF, "hide-sidebar");
    } else if (resetVisibility) {
      // only reset visibility pref when switching to vertical tabs and explictly indicated
      Services.prefs.setStringPref(VISIBILITY_SETTING_PREF, "always-show");
    }
  },

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
  },

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
  },

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
  },

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
  },
};

// Initialize on first import
SidebarManager.init();
