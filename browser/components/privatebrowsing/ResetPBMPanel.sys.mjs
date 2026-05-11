/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

/**
 * ResetPBMPanel contains the logic for the restart private browsing action.
 * The feature is exposed via a toolbar button in private browsing windows. It
 * allows users to restart their private browsing session, clearing all site
 * data and closing all PBM tabs / windows.
 * The toolbar button for triggering the panel is only shown in private browsing
 * windows or if permanent private browsing mode is enabled.
 */

import { XPCOMUtils } from "resource://gre/modules/XPCOMUtils.sys.mjs";

const ENABLED_PREF = "browser.privatebrowsing.resetPBM.enabled";
const SHOW_CONFIRM_DIALOG_PREF =
  "browser.privatebrowsing.resetPBM.showConfirmationDialog";

const lazy = {};
ChromeUtils.defineESModuleGetters(lazy, {
  CustomizableUI:
    "moz-src:///browser/components/customizableui/CustomizableUI.sys.mjs",
  PrivateBrowsingUtils: "resource://gre/modules/PrivateBrowsingUtils.sys.mjs",
  SessionStore: "resource:///modules/sessionstore/SessionStore.sys.mjs",
});

export const ResetPBMPanel = {
  // Button and view config for CustomizableUI.
  _widgetConfig: null,

  /**
   * Initialize the widget code depending on pref state.
   */
  init() {
    // Populate _widgetConfig during init to defer (lazy) CustomizableUI import.
    this._widgetConfig ??= {
      id: "reset-pbm-toolbar-button",
      l10nId: "reset-pbm-toolbar-button2",
      type: "view",
      viewId: "reset-pbm-panel",
      defaultArea: lazy.CustomizableUI.AREA_NAVBAR,
      onViewShowing(aEvent) {
        ResetPBMPanel.onViewShowing(aEvent);
      },
      onViewHiding(aEvent) {
        ResetPBMPanel.onViewHiding(aEvent);
      },
      hideInNonPrivateBrowsing: true,
    };

    if (this._enabled) {
      lazy.CustomizableUI.createWidget(this._widgetConfig);
    } else {
      lazy.CustomizableUI.destroyWidget(this._widgetConfig.id);
    }
  },

  /**
   * Called when the reset pbm panelview is showing as the result of clicking
   * the toolbar button.
   */
  async onViewShowing(event) {
    let panelview = event.target;
    let triggeringWindow = panelview.documentGlobal;

    // We may skip the confirmation panel if disabled via pref.
    if (!this._shouldConfirmClear) {
      // Prevent the panel from showing up.
      event.preventDefault();

      // If the action is triggered from the overflow menu make sure that the
      // panel gets hidden.
      lazy.CustomizableUI.hidePanelForNode(panelview);

      // Trigger the restart action.
      await this._restartPBM(triggeringWindow);

      Glean.privateBrowsingResetPbm.resetAction.record({ did_confirm: false });
      return;
    }

    panelview.addEventListener("command", this);

    // Before the panel is shown, update checkbox state based on pref.
    this._rememberCheck(triggeringWindow).checked = this._shouldConfirmClear;

    Glean.privateBrowsingResetPbm.confirmPanel.record({
      action: "show",
      reason: "toolbar-btn",
    });
  },

  onViewHiding(event) {
    let panelview = event.target;
    panelview.removeEventListener("command", this);
  },

  handleEvent(event) {
    let button = event.target;
    switch (button.id) {
      case "reset-pbm-panel-cancel-button":
        this.onCancel(button);
        break;
      case "reset-pbm-panel-confirm-button":
        this.onConfirm(button);
        break;
    }
  },

  /**
   * Handles the confirmation panel cancel button.
   *
   * @param {MozButton} button - Cancel button that triggered the action.
   */
  onCancel(button) {
    if (!this._enabled) {
      throw new Error("Not initialized.");
    }
    lazy.CustomizableUI.hidePanelForNode(button);

    Glean.privateBrowsingResetPbm.confirmPanel.record({
      action: "hide",
      reason: "cancel-btn",
    });
  },

  /**
   * Handles the confirmation panel confirm button which triggers the clear
   * action.
   *
   * @param {MozButton} button - Confirm button that triggered the action.
   */
  async onConfirm(button) {
    if (!this._enabled) {
      throw new Error("Not initialized.");
    }
    let triggeringWindow = button.documentGlobal;

    // Write the checkbox state to pref. Only do this when the user
    // confirms.
    // Setting this pref to true means there is no way to see the panel
    // again other than flipping the pref back via about:config or resetting
    // the profile. This is by design.
    Services.prefs.setBoolPref(
      SHOW_CONFIRM_DIALOG_PREF,
      this._rememberCheck(triggeringWindow).checked
    );

    lazy.CustomizableUI.hidePanelForNode(button);

    Glean.privateBrowsingResetPbm.confirmPanel.record({
      action: "hide",
      reason: "confirm-btn",
    });

    // Clear the private browsing session.
    await this._restartPBM(triggeringWindow);

    Glean.privateBrowsingResetPbm.resetAction.record({ did_confirm: true });
  },

  /**
   * Restart the private browsing session. This is achieved by closing all other
   * PBM windows, closing all tabs in the current window but
   * about:privatebrowsing and triggering PBM data clearing.
   *
   * @param {ChromeWindow} triggeringWindow - The (private browsing) chrome window which
   * triggered the restart action.
   */
  async _restartPBM(triggeringWindow) {
    if (
      !triggeringWindow ||
      !lazy.PrivateBrowsingUtils.isWindowPrivate(triggeringWindow)
    ) {
      throw new Error("Invalid triggering window.");
    }

    // 1. Close all PBM windows but the current one.
    for (let w of Services.ww.getWindowEnumerator()) {
      if (
        w != triggeringWindow &&
        lazy.PrivateBrowsingUtils.isWindowPrivate(w)
      ) {
        // This suppresses confirmation dialogs like the beforeunload
        // handler and the tab close warning.
        // Skip over windows that don't have the closeWindow method.
        w.closeWindow?.(true, null, "restart-pbm");
      }
    }

    // 2. For the current PBM window create a new tab which will be used for
    //    the initial newtab page.
    let newTab = triggeringWindow.gBrowser.addTab(
      triggeringWindow.BROWSER_NEW_TAB_URL,
      {
        triggeringPrincipal:
          Services.scriptSecurityManager.getSystemPrincipal(),
      }
    );
    if (!newTab) {
      throw new Error("Could not open new tab.");
    }

    // 3. Close all other tabs.
    triggeringWindow.gBrowser.removeAllTabsBut(newTab, {
      skipPermitUnload: true,
      // Instruct the SessionStore to not save closed tab data for these tabs.
      // We don't want to leak them into the next private browsing session.
      skipSessionStore: true,
      animate: false,
      skipWarnAboutClosingTabs: true,
      skipPinnedOrSelectedTabs: false,
    });

    // In the remaining PBM window: If the sidebar is open close it.
    triggeringWindow.SidebarController?.hide();

    // Clear session store data for the remaining PBM window.
    lazy.SessionStore.purgeDataForPrivateWindow(triggeringWindow);

    // 4. Clear private browsing data.
    //    Wait for async cleanup to complete before showing confirmation.
    await new Promise(resolve => {
      Services.clearData.clearPrivateBrowsingData({
        onDataDeleted(aFailedFlags) {
          if (aFailedFlags) {
            console.error("PBM cleanup failed with flags:", aFailedFlags);
          }
          resolve();
        },
      });
    });

    // Once clearing is complete show a toast message.

    let toolbarButton = this._toolbarButton(triggeringWindow);

    // Find the anchor used for the confirmation hint panel. If the toolbar
    // button is in the overflow menu we can't use it as an anchor. Instead we
    // anchor off the overflow button as indicated by cui-anchorid.
    let anchor;
    let anchorID = toolbarButton.getAttribute("cui-anchorid");
    if (anchorID) {
      anchor = triggeringWindow.document.getElementById(anchorID);
    }
    triggeringWindow.ConfirmationHint.show(
      anchor ?? toolbarButton,
      "reset-pbm-panel-complete",
      { position: "bottomright topright" }
    );
  },

  _toolbarButton(win) {
    return lazy.CustomizableUI.getWidget(this._widgetConfig.id).forWindow(win)
      .node;
  },

  _rememberCheck(win) {
    return win.document.getElementById("reset-pbm-panel-checkbox");
  },
};

XPCOMUtils.defineLazyPreferenceGetter(
  ResetPBMPanel,
  "_enabled",
  ENABLED_PREF,
  false,
  // On pref change update the init state.
  ResetPBMPanel.init.bind(ResetPBMPanel)
);
XPCOMUtils.defineLazyPreferenceGetter(
  ResetPBMPanel,
  "_shouldConfirmClear",
  SHOW_CONFIRM_DIALOG_PREF,
  true
);
