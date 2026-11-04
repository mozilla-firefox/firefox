/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/**
 * Provides functions to handle status and messages in the user interface.
 */

import { AppConstants } from "resource://gre/modules/AppConstants.sys.mjs";

const lazy = {};

// BrowserWindowTracker and PrivateBrowsingUtils are only used when opening downloaded files into a browser window
ChromeUtils.defineESModuleGetters(lazy, {
  BrowserWindowTracker: "resource:///modules/BrowserWindowTracker.sys.mjs",
  PrivateBrowsingUtils: "resource://gre/modules/PrivateBrowsingUtils.sys.mjs",
});

ChromeUtils.defineLazyGetter(
  lazy,
  "l10n",
  () =>
    new Localization(
      ["toolkit/downloads/downloadUI.ftl", "toolkit/branding/brandings.ftl"],
      true
    )
);

/**
 * Provides functions to handle status and messages in the user interface.
 */
export var DownloadUIHelper = {
  /**
   * Returns an object that can be used to display prompts related to downloads.
   *
   * The prompts may be either anchored to a specified window, or anchored to
   * the most recently active window, for example if the prompt is displayed in
   * response to global notifications that are not associated with any window.
   *
   * @param aParent
   *        If specified, should reference the nsIDOMWindow to which the prompts
   *        should be attached.  If omitted, the prompts will be attached to the
   *        most recently active window.
   *
   * @return A DownloadPrompter object.
   */
  getPrompter(aParent) {
    return new DownloadPrompter(aParent || null);
  },

  /**
   * Open the given file as a file: URI in the active window.
   *
   * @param {nsIFile} file
   *        The downloaded file.
   * @param {object} [options]
   * @param {Window} [options.chromeWindow]
   *        Optional chrome window where we could open the file URI.
   * @param {string} [options.openWhere="tab"]
   *        How to open the URI, one of "window", "tab", "tabshifted".
   * @param {boolean} [options.isPrivate]
   *        Open in a private window.
   * @param {number} [options.browsingContextId=0]
   *        BrowsingContext ID of the initiating document.
   * @param {number} [options.userContextId=0]
   *        UserContextID of the initiating document.
   * @param {boolean} [options.openInBackgroundIfSwitchedBrowsingContext=false]
   *        If true, open in the background when the browsing context that initiated
   *        the download is no longer the active tab (i.e. the user switched tabs or
   *        closed the source tab while the download was in progress).
   */
  loadFileIn(
    file,
    {
      chromeWindow: browserWin,
      openWhere = "tab",
      isPrivate,
      userContextId = 0,
      browsingContextId = 0,
      openInBackgroundIfSwitchedBrowsingContext = false,
    } = {}
  ) {
    let fileURI = Services.io.newFileURI(file);
    let allowPrivate =
      isPrivate || lazy.PrivateBrowsingUtils.permanentPrivateBrowsing;

    if (
      !browserWin ||
      browserWin.document.documentElement.getAttribute("windowtype") !==
        "navigator:browser"
    ) {
      // we'll need a private window for a private download, or if we're in private-only mode
      // but otherwise we want to open files in a non-private window
      browserWin = lazy.BrowserWindowTracker.getTopWindow({
        private: allowPrivate,
      });
    }
    // if there is no suitable browser window, we'll need to open one and ignore any other `openWhere` value
    // this can happen if the library dialog is the only open window
    if (!browserWin) {
      // There is no browser window open, so open a new one.
      let args = Cc["@mozilla.org/array;1"].createInstance(Ci.nsIMutableArray);
      let strURI = Cc["@mozilla.org/supports-string;1"].createInstance(
        Ci.nsISupportsString
      );
      strURI.data = fileURI.spec;
      args.appendElement(strURI);
      let features = "chrome,dialog=no,all";
      if (isPrivate) {
        features += ",private";
      }
      browserWin = Services.ww.openWindow(
        null,
        AppConstants.BROWSER_CHROME_URL,
        null,
        features,
        args
      );
      return;
    }

    // a browser window will have the helpers from utilityOverlay.js
    let browsingContext = browserWin?.BrowsingContext.get(browsingContextId);
    let openerBrowser = browsingContext?.top?.embedderElement;

    // Open in the background if the source tab is no longer the active one.
    // Handle both the case where the user switched to a different tab or
    // the source tab was closed. Not having a browsingContextId means no source
    // context (e.g. Save As).
    let inBackground =
      openInBackgroundIfSwitchedBrowsingContext &&
      !!browsingContextId &&
      openerBrowser != browserWin.gBrowser.selectedBrowser;

    browserWin.openTrustedLinkIn(fileURI.spec, openWhere, {
      triggeringPrincipal: Services.scriptSecurityManager.getSystemPrincipal(),
      private: isPrivate,
      userContextId,
      openerBrowser,
      inBackground,
    });
  },
};

/**
 * Allows displaying prompts related to downloads.
 *
 * @param aParent
 *        The nsIDOMWindow to which prompts should be attached, or null to
 *        attach prompts to the most recently active window.
 */
var DownloadPrompter = function (aParent) {
  this._prompter = Services.ww.getNewPrompter(aParent);
};

DownloadPrompter.prototype = {
  /**
   * Constants with the different type of prompts.
   */
  ON_QUIT: "prompt-on-quit",
  ON_OFFLINE: "prompt-on-offline",
  ON_LEAVE_PRIVATE_BROWSING: "prompt-on-leave-private-browsing",

  /**
   * nsIPrompt instance for displaying messages.
   */
  _prompter: null,

  /**
   * Displays a warning message box that informs that the specified file is
   * executable, and asks whether the user wants to launch it.
   *
   * @param path
   *        String containing the full path to the file to be opened.
   *
   * @returns {Promise<boolean>}
   *   Resolves to a boolean indicating whether the launch operation can continue.
   */
  async confirmLaunchExecutable(path) {
    const kPrefSkipConfirm = "browser.download.skipConfirmLaunchExecutable";

    // Always launch in case we have no prompter implementation.
    if (!this._prompter) {
      return true;
    }

    try {
      if (Services.prefs.getBoolPref(kPrefSkipConfirm)) {
        return true;
      }
    } catch (ex) {
      // If the preference does not exist, continue with the prompt.
    }

    const title = lazy.l10n.formatValueSync(
      "download-ui-file-executable-security-warning-title"
    );
    const message = lazy.l10n.formatValueSync(
      "download-ui-file-executable-security-warning",
      { executable: PathUtils.filename(path) }
    );
    let flags =
      Ci.nsIPrompt.BUTTON_DELAY_ENABLE | Ci.nsIPrompt.STD_OK_CANCEL_BUTTONS;
    let nulls = Array(4).fill(null);
    return 0 == this._prompter.confirmEx(title, message, flags, ...nulls, {});
  },

  /**
   * Displays a warning message box that informs that there are active
   * downloads, and asks whether the user wants to cancel them or not.
   *
   * @param aDownloadsCount
   *        The current downloads count.
   * @param aPromptType
   *        The type of prompt notification depending on the observer.
   *
   * @return False to cancel the downloads and continue, true to abort the
   *         operation.
   */
  confirmCancelDownloads: function DP_confirmCancelDownload(
    aDownloadsCount,
    aPromptType
  ) {
    // Always continue in case we have no prompter implementation, or if there
    // are no active downloads.
    if (!this._prompter || aDownloadsCount <= 0) {
      return false;
    }

    let title, message, okButton;
    let cancelButton = "download-ui-confirm-cancel-button";

    switch (aPromptType) {
      case this.ON_QUIT:
        title = "download-ui-confirm-quit-title";
        message = "download-ui-confirm-quit-message";
        okButton = "download-ui-confirm-quit-button";
        break;

      case this.ON_OFFLINE:
        title = "download-ui-confirm-offline-title";
        message = "download-ui-confirm-offline-message";
        okButton = "download-ui-confirm-offline-button";
        break;

      case this.ON_LEAVE_PRIVATE_BROWSING:
        title = "download-ui-confirm-private-browsing-title";
        message = "download-ui-confirm-private-browsing-message";
        okButton = "download-ui-confirm-private-browsing-button";
        break;
    }

    const buttonFlags =
      Ci.nsIPrompt.BUTTON_TITLE_IS_STRING * Ci.nsIPrompt.BUTTON_POS_0 +
      Ci.nsIPrompt.BUTTON_TITLE_IS_STRING * Ci.nsIPrompt.BUTTON_POS_1;

    let l10nArgs = { downloadsCount: aDownloadsCount };
    let rv = this._prompter.confirmEx(
      lazy.l10n.formatValueSync(title, l10nArgs),
      lazy.l10n.formatValueSync(message, l10nArgs),
      buttonFlags,
      lazy.l10n.formatValueSync(okButton, l10nArgs),
      lazy.l10n.formatValueSync(cancelButton),
      null,
      null,
      {}
    );
    return rv == 1;
  },
};
