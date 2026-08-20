/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

import { AppConstants } from "resource://gre/modules/AppConstants.sys.mjs";

let lazy = {};
ChromeUtils.defineESModuleGetters(lazy, {
  BrowserWindowTracker: "resource:///modules/BrowserWindowTracker.sys.mjs",
  CommonDialog: "resource://gre/modules/CommonDialog.sys.mjs",
  SessionStartup:
    "moz-src:///browser/components/sessionstore/SessionStartup.sys.mjs",
});

export var DefaultBrowserCheck = {
  async prompt(win) {
    const shellService = win.getShellService();
    const needPin =
      (await shellService.doesAppNeedPin()) ||
      (await shellService.doesAppNeedStartMenuPin());

    win.MozXULElement.insertFTLIfNeeded("branding/brand.ftl");
    win.MozXULElement.insertFTLIfNeeded(
      "browser/defaultBrowserNotification.ftl"
    );
    // Record default prompt impression
    let now = Math.floor(Date.now() / 1000).toString();
    Services.prefs.setCharPref(
      "browser.shell.mostRecentDefaultPromptSeen",
      now
    );

    // Resolve the translations for the prompt elements and return only the
    // string values

    let pinMessage;
    if (AppConstants.platform == "macosx") {
      pinMessage = "default-browser-prompt-message-pin-mac";
    } else if (
      AppConstants.platform == "win" &&
      Services.sysinfo.getProperty("hasWinPackageId", false)
    ) {
      pinMessage = "default-browser-prompt-message-pin-msix";
    } else {
      pinMessage = "default-browser-prompt-message-pin";
    }
    let [promptTitle, promptMessage, askLabel, yesButton, notNowButton] = (
      await win.document.l10n.formatMessages([
        {
          id: needPin
            ? "default-browser-prompt-title-pin"
            : "default-browser-prompt-title-alt",
        },
        {
          id: needPin ? pinMessage : "default-browser-prompt-message-alt",
        },
        { id: "default-browser-prompt-checkbox-not-again-label" },
        {
          id: needPin
            ? "default-browser-prompt-button-primary-set"
            : "default-browser-prompt-button-primary-alt",
        },
        { id: "default-browser-prompt-button-secondary" },
      ])
    ).map(({ value }) => value);

    let ps = Services.prompt;
    let buttonFlags =
      ps.BUTTON_TITLE_IS_STRING * ps.BUTTON_POS_0 +
      ps.BUTTON_TITLE_IS_STRING * ps.BUTTON_POS_1 +
      ps.BUTTON_POS_0_DEFAULT;
    let rv = await ps.asyncConfirmEx(
      win.browsingContext,
      ps.MODAL_TYPE_INTERNAL_WINDOW,
      promptTitle,
      promptMessage,
      buttonFlags,
      yesButton,
      notNowButton,
      null,
      askLabel,
      false, // checkbox state
      {
        headerIconCSSValue: lazy.CommonDialog.DEFAULT_APP_ICON_CSS,
      }
    );
    let buttonNumClicked = rv.get("buttonNumClicked");
    let checkboxState = rv.get("checked");
    if (buttonNumClicked == 0) {
      // We must explicitly await pinning to the taskbar before
      // trying to set as default. If we fall back to setting
      // as default through the Windows Settings menu that interferes
      // with showing the pinning notification as we no longer have
      // window focus.
      try {
        await shellService.pinToTaskbar();
      } catch (e) {
        this.log.error("Failed to pin to taskbar", e);
      }
      try {
        await shellService.pinToStartMenu();
      } catch (e) {
        this.log.error("Failed to pin to Start Menu", e);
      }
      try {
        await shellService.setAsDefault();
      } catch (e) {
        this.log.error("Failed to set the default browser", e);
      }
    }
    if (checkboxState) {
      shellService.shouldCheckDefaultBrowser = false;
      Services.prefs.setCharPref("browser.shell.userDisabledDefaultCheck", now);
    }

    try {
      let resultEnum = buttonNumClicked * 2 + !checkboxState;
      Glean.browser.setDefaultResult.accumulateSingleSample(resultEnum);
    } catch (ex) {
      /* Don't break if Telemetry is acting up. */
    }
  },

  /**
   * Checks if the default browser check prompt will be shown.
   *
   * @param {boolean} isStartupCheck
   *   If true, prefs will be set and telemetry will be recorded.
   * @returns {boolean} True if the default browser check prompt will be shown.
   */
  async willCheckDefaultBrowser(isStartupCheck) {
    let win = lazy.BrowserWindowTracker.getTopWindow({
      allowFromInactiveWorkspace: true,
    });
    let shellService = win.getShellService();

    // Perform default browser checking.
    if (!shellService) {
      return false;
    }

    // We should never check if running under flatpak
    // as flatpak can't allow default to be set.
    // See browser/components/preferences/main.js:canCheck()
    let runningUnderFlatpak = false;
    if (Cc["@mozilla.org/gio-service;1"]) {
      let gIOSvc = Cc["@mozilla.org/gio-service;1"].getService(
        Ci.nsIGIOService
      );
      runningUnderFlatpak = gIOSvc.isRunningUnderFlatpak;
    }

    let shouldCheck =
      !AppConstants.DEBUG &&
      !runningUnderFlatpak &&
      shellService.shouldCheckDefaultBrowser;

    // Even if we shouldn't check the default browser, we still continue when
    // isStartupCheck = true to set prefs and telemetry.
    if (!shouldCheck && !isStartupCheck) {
      return false;
    }

    // Skip the "Set Default Browser" check during first-run or after the
    // browser has been run a few times.
    const skipDefaultBrowserCheck =
      Services.prefs.getBoolPref(
        "browser.shell.skipDefaultBrowserCheckOnFirstRun"
      ) &&
      !Services.prefs.getBoolPref(
        "browser.shell.didSkipDefaultBrowserCheckOnFirstRun"
      );

    let promptCount = Services.prefs.getIntPref(
      "browser.shell.defaultBrowserCheckCount",
      0
    );

    // If SessionStartup's state is not initialized, checking sessionType will set
    // its internal state to "do not restore".
    await lazy.SessionStartup.onceInitialized;
    let willRecoverSession =
      lazy.SessionStartup.sessionType == lazy.SessionStartup.RECOVER_SESSION;

    // Don't show the prompt if we're already the default browser.
    let isDefault = false;
    let isDefaultError = false;
    try {
      isDefault = shellService.isDefaultBrowser(isStartupCheck, false);
    } catch (ex) {
      isDefaultError = true;
    }

    if (isDefault && isStartupCheck) {
      let now = Math.floor(Date.now() / 1000).toString();
      Services.prefs.setCharPref(
        "browser.shell.mostRecentDateSetAsDefault",
        now
      );
    }

    let willPrompt = shouldCheck && !isDefault && !willRecoverSession;

    if (willPrompt) {
      if (skipDefaultBrowserCheck) {
        if (isStartupCheck) {
          Services.prefs.setBoolPref(
            "browser.shell.didSkipDefaultBrowserCheckOnFirstRun",
            true
          );
        }
        willPrompt = false;
      } else {
        promptCount++;
        if (isStartupCheck) {
          Services.prefs.setIntPref(
            "browser.shell.defaultBrowserCheckCount",
            promptCount
          );
        }
        if (!AppConstants.RELEASE_OR_BETA && promptCount > 3) {
          willPrompt = false;
        }
      }
    }

    if (isStartupCheck) {
      try {
        // Report default browser status on startup to telemetry
        // so we can track whether we are the default.
        Glean.browser.isUserDefault[isDefault ? "true" : "false"].add();
        Glean.browser.isUserDefaultError[
          isDefaultError ? "true" : "false"
        ].add();
        Glean.browser.setDefaultAlwaysCheck[
          shouldCheck ? "true" : "false"
        ].add();
        Glean.browser.setDefaultDialogPromptRawcount.accumulateSingleSample(
          promptCount
        );
      } catch (ex) {
        /* Don't break the default prompt if telemetry is broken. */
      }
    }

    return willPrompt;
  },
};
