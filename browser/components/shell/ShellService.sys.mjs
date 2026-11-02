/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

import { AppConstants } from "resource://gre/modules/AppConstants.sys.mjs";
import { XPCOMUtils } from "resource://gre/modules/XPCOMUtils.sys.mjs";

const lazy = {};

ChromeUtils.defineESModuleGetters(lazy, {
  NimbusFeatures: "resource://nimbus/ExperimentAPI.sys.mjs",
  ASRouter: "resource:///modules/asrouter/ASRouter.sys.mjs",
  ScheduledTask: "resource://gre/modules/ScheduledTask.sys.mjs",
  Subprocess: "resource://gre/modules/Subprocess.sys.mjs",
  WindowsSetDefaultRedirect:
    "moz-src:///browser/components/shell/WindowsSetDefaultRedirect.sys.mjs",
});

XPCOMUtils.defineLazyServiceGetter(
  lazy,
  "XreDirProvider",
  "@mozilla.org/xre/directory-provider;1",
  Ci.nsIXREDirProvider
);

XPCOMUtils.defineLazyServiceGetter(
  lazy,
  "BackgroundTasks",
  "@mozilla.org/backgroundtasks;1",
  Ci.nsIBackgroundTasks
);

XPCOMUtils.defineLazyServiceGetter(
  lazy,
  "imgTools",
  "@mozilla.org/image/tools;1",
  Ci.imgITools
);

XPCOMUtils.defineLazyServiceGetter(
  lazy,
  "iniParserFactory",
  "@mozilla.org/xpcom/ini-parser-factory;1",
  Ci.nsIINIParserFactory
);

XPCOMUtils.defineLazyServiceGetter(
  lazy,
  "secondaryTileService",
  "@mozilla.org/browser/secondary-tile-service;1",
  Ci.nsISecondaryTileService
);

XPCOMUtils.defineLazyServiceGetter(
  lazy,
  "gioService",
  "@mozilla.org/gio-service;1",
  Ci.nsIGIOService
);

XPCOMUtils.defineLazyServiceGetter(
  lazy,
  "mimeService",
  "@mozilla.org/mime;1",
  Ci.nsIMIMEService
);

XPCOMUtils.defineLazyPreferenceGetter(
  lazy,
  "DESKTOP_ENTRY_API",
  "browser.shell.desktop-entry-api",
  "disabled"
);

ChromeUtils.defineLazyGetter(lazy, "log", () => {
  let { ConsoleAPI } = ChromeUtils.importESModule(
    "resource://gre/modules/Console.sys.mjs"
  );
  let consoleOptions = {
    // tip: set maxLogLevel to "debug" and use log.debug() to create detailed
    // messages during development. See LOG_LEVELS in Console.sys.mjs for details.
    maxLogLevel: "error",
    maxLogLevelPref: "browser.shell.loglevel",
    prefix: "ShellService",
  };
  return new ConsoleAPI(consoleOptions);
});

const MSIX_PREVIOUSLY_PINNED_PREF =
  "browser.startMenu.msixPinnedWhenLastChecked";

// URLs handed to launchSetDefaultAppPicker (as the openWithArg) when setting a
// protocol default, keyed by scheme. setAsDefaultProtocolHandler passes one of
// these to the OS picker; on the round-trip WindowsSetDefaultAppCmdHandler
// matches it via the stashed redirect.
export const DEFAULT_PROTOCOL_URLS = {
  http: "http://www.firefox.com/?utm_medium=platform&utm_source=windows&utm_campaign=owl",
  https:
    "https://www.firefox.com/?utm_medium=platform&utm_source=windows&utm_campaign=owl",
  mailto: "mailto:owl@firefox.com",
};

/**
 * Internal functionality to save and restore the docShell.allow* properties.
 */
let ShellServiceInternal = {
  /**
   * Used to determine whether or not to offer "Set as desktop background"
   * functionality. Even if shell service is available it is not
   * guaranteed that it is able to set the background for every desktop
   * which is especially true for Linux with its many different desktop
   * environments.
   */
  get canSetDesktopBackground() {
    if (AppConstants.platform == "win" || AppConstants.platform == "macosx") {
      return true;
    }

    if (AppConstants.platform == "linux") {
      if (this.shellService) {
        let linuxShellService = this.shellService.QueryInterface(
          Ci.nsIGNOMEShellService
        );
        return linuxShellService.canSetDesktopBackground;
      }
    }

    return false;
  },

  /**
   * Used to determine based on the creation date of the home folder how old a
   * user profile is (and NOT the browser profile).
   */
  async getOSUserProfileAgeInDays() {
    let currentDate = new Date();
    let homeFolderCreationDate = new Date(
      (await IOUtils.stat(Services.dirsvc.get("Home", Ci.nsIFile).path))
        .creationTime
    );
    // Round and return the age (=difference between today and creation) to a
    // resolution of days.
    return Math.round(
      (currentDate - homeFolderCreationDate) /
        1000 / // ms
        60 / // sec
        60 / // min
        24 // hours
    );
  },

  /**
   * Used to determine whether or not to show a "Set Default Browser"
   * query dialog. This attribute is true if the application is starting
   * up and "browser.shell.checkDefaultBrowser" is true, otherwise it
   * is false.
   */
  _checkedThisSession: false,
  get shouldCheckDefaultBrowser() {
    // If we've already checked, the browser has been started and this is a
    // new window open, and we don't want to check again.
    if (this._checkedThisSession) {
      return false;
    }

    if (!Services.prefs.getBoolPref("browser.shell.checkDefaultBrowser")) {
      return false;
    }

    return true;
  },

  set shouldCheckDefaultBrowser(shouldCheck) {
    Services.prefs.setBoolPref(
      "browser.shell.checkDefaultBrowser",
      !!shouldCheck
    );
  },

  _attemptedSetDefaultThisSession: false,
  get attemptedSetDefaultThisSession() {
    return this._attemptedSetDefaultThisSession;
  },

  isDefaultBrowser(startupCheck, forAllTypes) {
    // If this is the first browser window, maintain internal state that we've
    // checked this session (so that subsequent window opens don't show the
    // default browser dialog).
    if (startupCheck) {
      this._checkedThisSession = true;
    }
    if (this.shellService) {
      return this.shellService.isDefaultBrowser(forAllTypes);
    }
    return false;
  },

  /**
   * Asynchronously determines whether Firefox is the default browser.
   *
   * New callers should prefer this method over isDefaultBrowser(). The
   * synchronous version may be removed once existing callers have been
   * migrated to the asynchronous API. See bug 2074009.
   *
   */
  isDefaultBrowserAsync(startupCheck, forAllTypes) {
    // If this is the first browser window, maintain internal state that we've
    // checked this session (so that subsequent window opens don't show the
    // default browser dialog).
    if (startupCheck) {
      this._checkedThisSession = true;
    }
    if (this.shellService) {
      return this.shellService.isDefaultBrowserAsync(forAllTypes);
    }
    return false;
  },

  /**
   * Check if UserChoice is impossible.
   *
   * Separated for easy stubbing in tests.
   *
   * @returns {string}
   *   Telemetry result like "Err*", or null if UserChoice is possible.
   */
  _userChoiceImpossibleTelemetryResult() {
    let winShellService = this.shellService.QueryInterface(
      Ci.nsIWindowsShellService
    );
    if (!winShellService.checkAllProgIDsExist()) {
      return "ErrProgID";
    }
    if (!winShellService.checkBrowserUserChoiceHashes()) {
      return "ErrHash";
    }
    return null;
  },

  /**
   * Whether Firefox can currently make itself the default browser by writing
   * the Windows UserChoice registry keys, instead of having to send the user
   * into the Windows Settings app to do it manually. `true` means a set-default
   * request would be honored or that Firefox is already the default (the
   * latter is a shortcut to `true` to avoid the risk of a UserChoice write
   * when we don't need to attempt to set to default anyway).
   *
   * If the UserChoice Protection Driver (UCPD) is running and Firefox isn't
   * already the default, this temporarily renames the `http` association key
   * and renames it back. UCPD versions where one-click still works permit
   * this rename, while those where it doesn't do not.
   *
   * Always `false` on macOS and Linux. Neither goes through UserChoice, and
   * neither is reliably one-click, since both can (but don't always) defer to
   * an OS consent prompt. Supporting them needs its own handling (see bug
   * 2060879).
   *
   * @returns {boolean}
   */
  isOneClickSetDefaultEnabled() {
    if (AppConstants.platform != "win" || !this.shellService) {
      return false;
    }
    if (
      !Services.prefs.getBoolPref(
        "browser.shell.setDefaultBrowserUserChoice",
        false
      )
    ) {
      return false;
    }
    if (this._userChoiceImpossibleTelemetryResult() !== null) {
      return false;
    }
    if (!this.isUserChoiceProtectionDriverRunning()) {
      return true;
    }
    if (this.isDefaultBrowser(false)) {
      return true;
    }
    // A running UCPD doesn't always block the rename a UserChoice write needs,
    // so try renaming it to determine if it's actually blocked.
    return (
      Services.prefs.getBoolPref(
        "browser.shell.setDefaultBrowserUserChoice.regRename",
        false
      ) && this.canRenameUserChoiceAssociationKey("http")
    );
  },

  /**
   * Accommodate `setDefaultPDFHandlerOnlyReplaceBrowsers` feature.
   *
   * @returns {boolean}
   *   True if Firefox should set itself as default PDF handler, false otherwise.
   */
  _shouldSetDefaultPDFHandler() {
    if (
      !lazy.NimbusFeatures.shellService.getVariable(
        "setDefaultPDFHandlerOnlyReplaceBrowsers"
      )
    ) {
      return true;
    }

    const handler = this.getDefaultPDFHandler();
    if (handler === null) {
      // We only get an exception when something went really wrong.  Fail
      // safely: don't set Firefox as default PDF handler.
      lazy.log.warn(
        "Could not determine default PDF handler: not setting Firefox as " +
          "default PDF handler!"
      );
      return false;
    }

    if (!handler.registered) {
      lazy.log.debug(
        "Current default PDF handler has no registered association; " +
          "should set as default PDF handler."
      );
      return true;
    }

    if (handler.knownBrowser) {
      lazy.log.debug(
        "Current default PDF handler progID matches known browser; should " +
          "set as default PDF handler."
      );
      return true;
    }

    lazy.log.debug(
      "Current default PDF handler progID does not match known browser " +
        "prefix; should not set as default PDF handler."
    );
    return false;
  },

  getDefaultPDFHandler() {
    const knownBrowserPrefixes = [
      "AppXq0fevzme2pys62n3e0fbqa7peapykr8v", // Edge before Blink, per https://stackoverflow.com/a/32724723.
      "AppXd4nrz8ff68srnhf9t5a8sbjyar1cr723", // Another pre-Blink Edge identifier. See Bug 1858729.
      "Brave", // For "BraveFile".
      "Chrome", // For "ChromeHTML".
      "Firefox", // For "FirefoxHTML-*" or "FirefoxPDF-*".  Need to take from other installations of Firefox!
      "IE", // Best guess.
      "MSEdge", // For "MSEdgePDF".  Edgium.
      "Opera", // For "OperaStable", presumably varying with channel.
      "Yandex", // For "YandexPDF.IHKFKZEIOKEMR6BGF62QXCRIKM", presumably varying with installation.
    ];

    let currentProgID = "";
    try {
      // Returns the empty string when no association is registered, in
      // which case the prefix matching will fail and we'll set Firefox as
      // the default PDF handler.
      currentProgID = this.queryCurrentDefaultHandlerFor(".pdf");
    } catch (e) {
      // We only get an exception when something went really wrong.  Fail
      // safely: don't set Firefox as default PDF handler.
      lazy.log.warn("Failed to queryCurrentDefaultHandlerFor:");
      return null;
    }

    if (currentProgID == "") {
      return { registered: false, knownBrowser: false };
    }

    const knownBrowserPrefix = knownBrowserPrefixes.find(it =>
      currentProgID.startsWith(it)
    );

    if (knownBrowserPrefix) {
      lazy.log.debug(`Found known browser prefix: ${knownBrowserPrefix}`);
    }

    return {
      registered: true,
      knownBrowser: !!knownBrowserPrefix,
    };
  },

  /**
   * Set the default browser through the UserChoice registry keys on Windows.
   *
   * NOTE: This does NOT open the System Settings app for manual selection
   * in case of failure. If that is desired, catch the exception and call
   * setDefaultBrowser().
   *
   * @returns {Promise<void>}
   *   Resolves when successful, rejects with Error on failure.
   */
  async setAsDefaultUserChoice() {
    if (AppConstants.platform != "win") {
      throw new Error("Windows-only");
    }

    lazy.log.info("Setting Firefox as default using UserChoice");

    let telemetryResult = "ErrOther";

    try {
      telemetryResult =
        this._userChoiceImpossibleTelemetryResult() ?? "ErrOther";
      if (telemetryResult == "ErrProgID") {
        throw new Error("checkAllProgIDsExist() failed");
      }
      if (telemetryResult == "ErrHash") {
        throw new Error("checkBrowserUserChoiceHashes() failed");
      }

      const aumi = lazy.XreDirProvider.getInstallHash();

      telemetryResult = "ErrLaunchExe";
      const extraFileExtensions = [];
      if (
        lazy.NimbusFeatures.shellService.getVariable("setDefaultPDFHandler")
      ) {
        if (this._shouldSetDefaultPDFHandler()) {
          lazy.log.info("Setting Firefox as default PDF handler");
          extraFileExtensions.push(".pdf", "FirefoxPDF");
        } else {
          lazy.log.info("Not setting Firefox as default PDF handler");
        }
      }
      try {
        await this.defaultAgent.setDefaultBrowserUserChoiceAsync(
          aumi,
          extraFileExtensions
        );
      } catch (err) {
        telemetryResult = "ErrOther";
        this._throwForWDBAResult(err.result || Cr.NS_ERROR_FAILURE);
      }
      telemetryResult = "Success";
    } catch (ex) {
      if (ex instanceof WDBAError) {
        telemetryResult = ex.telemetryResult;
      }

      throw ex;
    } finally {
      Glean.browser.setDefaultUserChoiceResult[telemetryResult].add(1);
    }
  },

  async setAsDefaultPDFHandlerUserChoice() {
    if (AppConstants.platform != "win") {
      throw new Error("Windows-only");
    }

    const aumi = lazy.XreDirProvider.getInstallHash();
    try {
      this.defaultAgent.setDefaultExtensionHandlersUserChoice(aumi, [
        ".pdf",
        "FirefoxPDF",
      ]);
    } catch (err) {
      this._throwForWDBAResult(err.result || Cr.NS_ERROR_FAILURE);
    }
  },

  async _maybeShowSetDefaultGuidanceNotification() {
    if (
      lazy.NimbusFeatures.shellService.getVariable(
        "setDefaultGuidanceNotifications"
      ) &&
      // Disable showing toast notification from Firefox Background Tasks.
      !lazy.BackgroundTasks?.isBackgroundTaskMode
    ) {
      await lazy.ASRouter.waitForInitialized;
      const win = Services.wm.getMostRecentBrowserWindow() ?? null;
      lazy.ASRouter.sendTriggerMessage({
        browser: win,
        id: "deeplinkedToWindowsSettingsUI",
      });
    }
  },

  // override nsIShellService.setDefaultBrowser() on the ShellService proxy.
  async setDefaultBrowser(forAllUsers) {
    if (!Services.policies.isAllowed("setDefaultBrowser")) {
      lazy.log.warn("Setting the default browser is disallowed by policy");
      return;
    }

    this._attemptedSetDefaultThisSession = true;

    // On Windows, our best chance is to set UserChoice, so try that first.
    if (
      AppConstants.platform == "win" &&
      Services.prefs.getBoolPref("browser.shell.setDefaultBrowserUserChoice")
    ) {
      try {
        await this.setAsDefaultUserChoice();
        return;
      } catch (err) {
        lazy.log.warn(
          "Error thrown during setAsDefaultUserChoice. Full exception:",
          err
        );

        // intentionally fall through to setting via the non-user choice pathway on error
      }
    }

    this.shellService.setDefaultBrowser(forAllUsers);
    this._maybeShowSetDefaultGuidanceNotification();
  },

  async setAsDefault() {
    let setAsDefaultError = false;
    try {
      await ShellService.setDefaultBrowser(false);
    } catch (ex) {
      setAsDefaultError = true;
      console.error(ex);
    }
    // Here isUserDefault and setUserDefaultError appear
    // to be inverse of each other, but that is only because this function is
    // called when the browser is set as the default. During startup we record
    // the isUserDefault value without recording setUserDefaultError.
    Glean.browser.isUserDefault[!setAsDefaultError ? "true" : "false"].add();
    Glean.browser.setDefaultError[setAsDefaultError ? "true" : "false"].add();
  },

  /**
   * Returns whether the current operating system is Windows 11.
   *
   * This is used so it can be mocked out in tests.
   */
  _isWindows11() {
    return Services.sysinfo.isWindows10BuildOrLater(22000);
  },

  /**
   * Returns the on-disk nsIFile for a PDF bundled under the browser directory
   * in NS_GRE_DIR.
   *
   * @param {string} aLeafName - The bundled PDF's file name, e.g.
   * "confused_fox.pdf".
   * @returns {nsIFile} The bundled file (which may not exist on disk).
   */
  getBundledPdfFile(aLeafName) {
    const file = Services.dirsvc.get("GreD", Ci.nsIFile);
    file.append("browser");
    file.append(aLeafName);
    return file;
  },

  /**
   * Set Firefox as the default PDF handler. Supported on Windows and macOS
   * (macOS 12+). The OS may prompt the user to confirm, so the returned
   * promise can take a while to settle.
   *
   * @param {boolean} [onlyIfKnownBrowser] - When true, only proceed if the
   * current default PDF handler is another browser, so we don't displace a
   * dedicated PDF app such as Preview or Acrobat.
   * @param {boolean} [openInFirefox] - Windows-only; ignored on macOS. Only
   * meaningful on the "Open with" picker code path. After the user picks
   * Firefox, the OS relaunches Firefox with the bundled stub PDF; this flag
   * decides whether we then open a PDF in a new tab (true), to land the user in
   * Firefox, or silently absorb that relaunch (false).
   * @returns {Promise<boolean>} Whether the attempt was carried out: on macOS
   * the OS reported the change applied; on Windows the chosen mechanism was
   * invoked without throwing. This is NOT a guarantee that Firefox is now the
   * default handler — the user may decline an OS prompt, or an out-of-process
   * picker may still be open. Callers that need the actual state should query
   * {@link ShellService.isDefaultHandlerFor} instead.
   */
  async setAsDefaultPDFHandler(
    onlyIfKnownBrowser = false,
    openInFirefox = false
  ) {
    if (AppConstants.platform == "macosx") {
      return this._setAsDefaultPDFHandlerMac(onlyIfKnownBrowser);
    }
    if (AppConstants.platform == "win") {
      return this._setAsDefaultPDFHandlerWin(onlyIfKnownBrowser, openInFirefox);
    }
    throw new Error(
      "Setting the default PDF handler is only supported on Windows and macOS"
    );
  },

  /**
   * macOS implementation of {@link ShellService.setAsDefaultPDFHandler}. Goes
   * through NSWorkspace, which may prompt the user for consent and resolves
   * once that interaction completes with whether the change was applied. The
   * Windows picker/settings fallback machinery doesn't apply.
   *
   * @param {boolean} onlyIfKnownBrowser - When true, only proceed if the
   * current default PDF handler is another browser.
   * @returns {Promise<boolean>} Whether NSWorkspace reported the change was
   * applied (i.e. it completed without error). Not a guarantee Firefox is the
   * default — see {@link ShellService.setAsDefaultPDFHandler}.
   */
  async _setAsDefaultPDFHandlerMac(onlyIfKnownBrowser) {
    // Unsupported on macOS older than 12; don't attempt or record an attempt.
    if (!this.shellService.canSetAsDefaultHandler) {
      return false;
    }

    if (
      onlyIfKnownBrowser &&
      !this.shellService.isDefaultHandlerAWebBrowserFor(".pdf")
    ) {
      return false;
    }

    let success = false;
    try {
      success = await this.shellService.setAsDefaultHandlerFor(".pdf");
    } catch (e) {
      lazy.log.debug("Setting default PDF handler failed", e);
    }
    // The promise resolved after the OS consent completed, so the default
    // handler state is current; sample it directly.
    Glean.browser.setDefaultPdfHandlerAttempt.record({
      method: "launch_services",
      success,
      result_is_default: this.isDefaultHandlerFor(".pdf"),
    });
    return success;
  },

  /**
   * Windows implementation of {@link ShellService.setAsDefaultPDFHandler}.
   *
   * @param {boolean} onlyIfKnownBrowser - When true, only proceed if the
   * current default PDF handler is a known browser.
   * @param {boolean} openInFirefox - See
   * {@link ShellService.setAsDefaultPDFHandler}.
   * @returns {Promise<boolean>} Whether a method was invoked without throwing.
   * Not a guarantee Firefox is the default — the user may not have picked
   * Firefox in an out-of-process picker. See
   * {@link ShellService.setAsDefaultPDFHandler}.
   */
  async _setAsDefaultPDFHandlerWin(onlyIfKnownBrowser, openInFirefox) {
    if (onlyIfKnownBrowser && !this.getDefaultPDFHandler().knownBrowser) {
      return false;
    }

    // Tracks the last method attempted and whether its API call succeeded.
    // These feed into the consolidated set_default_pdf_handler_attempt event
    // recorded at the bottom of this function.
    let method = "user_choice";
    let success = false;

    try {
      await this.setAsDefaultPDFHandlerUserChoice();
      Glean.browser.setDefaultPdfHandlerUserChoiceResult.Success.add(1);
      success = true;
    } catch (e) {
      const telemetryResult =
        e instanceof WDBAError ? e.telemetryResult : "ErrOther";
      Glean.browser.setDefaultPdfHandlerUserChoiceResult[telemetryResult].add(
        1
      );
      lazy.log.debug(
        "Setting default by user-choice failed, falling through to open with launcher",
        e
      );
    }

    // Optional second attempt via the undocumented IOpenWithLauncher API,
    // which surfaces the OS "Open with" picker so the user can pick Firefox
    // themselves. Gated by a pref so it can be remotely disabled if it
    // regresses.
    if (
      !success &&
      Services.prefs.getBoolPref(
        "browser.shell.setDefaultPDFHandler.useOpenWith",
        false
      )
    ) {
      method = "open_with";
      const openWithArg = this.getBundledPdfFile("confused_fox.pdf").path;
      // Arm the round-trip: the OS hands `openWithArg` back to Firefox if the user
      // selects us. We redirect that launch to the bundled PDF); otherwise overrideUri
      // is null and the launch is suppressed.
      const overrideUri = openInFirefox
        ? Services.io.newFileURI(this.getBundledPdfFile("blank.pdf")).spec
        : null;
      lazy.WindowsSetDefaultRedirect.arm(
        openWithArg,
        overrideUri,
        lazy.WindowsSetDefaultRedirect.TYPE.FILE
      );

      try {
        const flags = this._isWindows11()
          ? Ci.nsIWindowsShellService.OPEN_WITH_SET_HANDLER
          : Ci.nsIWindowsShellService.OPEN_WITH_SET_HANDLER_WIN10;
        this.shellService.launchSetDefaultAppPicker(openWithArg, flags);
        success = true;
      } catch (e) {
        lazy.WindowsSetDefaultRedirect.clear();
        // The picker API itself failed (e.g. COM error). Fall through to the
        // modern settings dialog rather than leaving the user without any
        // default-handler UI.
        lazy.log.debug(
          "Setting default by open with launcher failed, possibly falling through to modern settings",
          e
        );
      }
    }

    // PDF default app settings are only available in Windows 11 (build 22000+).
    if (!success && this._isWindows11()) {
      method = "settings";
      try {
        this.shellService.launchModernSettingsDialogDefaultApps();
        Glean.browser.setDefaultPdfHandlerModernSettingsResult.Success.add(1);
        success = true;
      } catch (e) {
        Glean.browser.setDefaultPdfHandlerModernSettingsResult.Failure.add(1);
        lazy.log.debug(
          "Last attempt to set as default PDF failed through modern settings",
          e
        );
      }
    }

    // Record the consolidated attempt event after a delay. For open_with and
    // settings the user is interacting with a launched dialog out-of-process,
    // so we wait before sampling isDefaultHandlerFor to give that interaction
    // time to complete. For user_choice the wait is unnecessary but harmless.
    const waitTimeMs = Services.prefs.getIntPref(
      "browser.shell.setDefaultPDFHandler.attemptWaitTimeMs",
      30000
    );
    new lazy.ScheduledTask(() => {
      Glean.browser.setDefaultPdfHandlerAttempt.record({
        method,
        success,
        result_is_default: this.isDefaultHandlerFor(".pdf"),
      });
    }, Date.now() + waitTimeMs).arm();

    return success;
  },

  /**
   * Set Firefox as the Windows default handler for a protocol (scheme).
   *
   * @param {string} protocol - The scheme to claim, e.g. "https" or "mailto".
   * Selects the default URL and is the telemetry / isDefaultHandlerFor key.
   * @param {string} [url] - The URL handed to the OS picker (the openWithArg).
   * Defaults to the DEFAULT_PROTOCOL_URLS entry for protocol.
   * @param {boolean} [openInFirefox] - After the user picks Firefox, the OS
   * relaunches Firefox with that URL; this flag decides whether we then open
   * the protocol's default URL in a new tab (true) or suppress that relaunch
   * (false).
   */
  async setAsDefaultProtocolHandler(
    protocol,
    url = DEFAULT_PROTOCOL_URLS[protocol],
    openInFirefox = false
  ) {
    if (AppConstants.platform != "win") {
      throw new Error("Windows-only");
    }

    if (!url) {
      throw new Error(
        `No URL provided and no DEFAULT_PROTOCOL_URLS fallback for protocol: ${protocol}`
      );
    }

    // Arm the round-trip for once the user picks a default: the OS hands `url`
    // back when Firefox becomes the handler. When opening in Firefox we then
    // open the protocol's default URL; otherwise the relaunch is suppressed.
    lazy.WindowsSetDefaultRedirect.arm(
      url,
      openInFirefox ? DEFAULT_PROTOCOL_URLS[protocol] : null,
      lazy.WindowsSetDefaultRedirect.TYPE.PROTOCOL
    );

    // Tracks the last method attempted and whether its API call succeeded.
    // These feed into the consolidated set_default_protocol_handler_attempt
    // event recorded at the bottom of this function.
    let method = "open_with";
    let success = false;
    const flags =
      (this._isWindows11()
        ? Ci.nsIWindowsShellService.OPEN_WITH_SET_HANDLER
        : Ci.nsIWindowsShellService.OPEN_WITH_SET_HANDLER_WIN10) |
      Ci.nsIWindowsShellService.OPEN_WITH_PROTOCOL_MESSAGING;
    try {
      this.shellService.launchSetDefaultAppPicker(url, flags);
      success = true;
    } catch (e) {
      lazy.WindowsSetDefaultRedirect.clear();
      lazy.log.debug(
        "Setting default protocol handler by open with launcher failed, " +
          "falling through to modern settings",
        e
      );
    }

    if (!success) {
      method = "settings";
      try {
        this.shellService.launchModernSettingsDialogDefaultApps();
        Glean.browser.setDefaultProtocolHandlerModernSettingsResult.Success.add(
          1
        );
        success = true;
      } catch (e) {
        Glean.browser.setDefaultProtocolHandlerModernSettingsResult.Failure.add(
          1
        );
        lazy.log.debug(
          "Last attempt to set as default protocol handler failed through " +
            "modern settings",
          e
        );
      }
    }

    // Record the consolidated attempt event after a delay so the user has
    // time to interact with the launched picker or settings dialog before we
    // sample isDefaultHandlerFor.
    const waitTimeMs = Services.prefs.getIntPref(
      "browser.shell.setDefaultProtocolHandler.attemptWaitTimeMs",
      30000
    );
    new lazy.ScheduledTask(() => {
      Glean.browser.setDefaultProtocolHandlerAttempt.record({
        method,
        success,
        protocol,
        result_is_default: this.isDefaultHandlerFor(protocol),
      });
    }, Date.now() + waitTimeMs).arm();
  },

  /**
   * Determine if we're the default handler for the given file extension (like
   * ".pdf") or protocol (like "https"). A leading "." marks a file extension;
   * anything else is treated as a protocol.
   *
   * Supported on Windows and macOS; always false on other platforms. On macOS
   * the query requires macOS 12, so it is false on older versions.
   *
   * @returns {boolean} true if we are the default handler, false otherwise.
   */
  isDefaultHandlerFor(aFileExtensionOrProtocol) {
    if (AppConstants.platform == "win" || AppConstants.platform == "macosx") {
      return this.shellService.isDefaultHandlerFor(aFileExtensionOrProtocol);
    }
    return false;
  },

  /**
   * Whether the platform supports offering to set Firefox as the default PDF
   * handler. Always true on Windows; on macOS requires macOS 12 (the
   * underlying NSWorkspace API); false on other platforms.
   *
   * @returns {boolean}
   */
  get canSetAsDefaultPDFHandler() {
    if (AppConstants.platform == "win") {
      return true;
    }
    if (AppConstants.platform == "macosx") {
      return this.shellService.canSetAsDefaultHandler;
    }
    return false;
  },

  /**
   * Checks if Firefox app can and isn't pinned to OS "taskbar."
   *
   * @throws if not called from main process.
   */
  async doesAppNeedPin(privateBrowsing = false) {
    if (
      Services.appinfo.processType !== Services.appinfo.PROCESS_TYPE_DEFAULT
    ) {
      throw new Components.Exception(
        "Can't determine pinned from child process",
        Cr.NS_ERROR_NOT_AVAILABLE
      );
    }

    // Pretend pinning is not needed/supported if remotely disabled.
    if (lazy.NimbusFeatures.shellService.getVariable("disablePin")) {
      return false;
    }

    // Bug 1758770: Pinning private browsing on MSIX is currently
    // not possible.
    if (
      privateBrowsing &&
      AppConstants.platform === "win" &&
      Services.sysinfo.getProperty("hasWinPackageId")
    ) {
      return false;
    }

    // Currently this only works on certain Windows versions.
    try {
      // First check if we can even pin the app where an exception means no.
      this.shellService.canPinToTaskbar();

      let winTaskbar = Cc["@mozilla.org/windows-taskbar;1"].getService(
        Ci.nsIWinTaskbar
      );

      // Then check if we're already pinned.
      return !(await this.shellService.isCurrentAppPinnedToTaskbar(
        privateBrowsing
          ? winTaskbar.defaultPrivateGroupId
          : winTaskbar.defaultGroupId
      ));
    } catch (ex) {}

    // Next check mac pinning to dock.
    try {
      // Accessing this.macDockSupport will ensure we're actually running
      // on Mac (it's possible to be on Linux in this block).
      const isInDock = this.macDockSupport.isAppInDock;
      // We can't pin Private Browsing mode on Mac, only a shortcut to the vanilla app
      return privateBrowsing ? false : !isInDock;
    } catch (ex) {}
    return false;
  },

  /**
   * Pin Firefox app to the OS "taskbar."
   *
   * @param {bool} privateBrowsing - Pin a private browser window.
   * @param {bool} fireAndForget - Return after pin attempt is tried, but before
   * result is known if user input is necessary.
   * @param {bool} promptOnly - On Windows, only pin through the OS prompt that
   * asks for the user's consent.
   * @returns {Promise} - Resolves either when pin attempt resolves, or when pin
   * request has been sent if fireAndForget is true.
   */
  async pinToTaskbar(
    privateBrowsing = false,
    fireAndForget = false,
    promptOnly = false
  ) {
    let needsPin = await this.doesAppNeedPin(privateBrowsing);
    if (!needsPin) {
      return;
    }

    try {
      if (AppConstants.platform == "win") {
        await this.shellService.pinCurrentAppToTaskbar(
          privateBrowsing,
          fireAndForget,
          promptOnly
        );
      } else if (AppConstants.platform == "macosx") {
        this.macDockSupport.ensureAppIsPinnedToDock();
      }
    } catch (ex) {
      console.error(ex);
    }
  },

  /**
   * On MSIX builds, pins Firefox to the Windows Start Menu
   *
   * On non-MSIX builds, this function is a no-op and always returns false.
   *
   * @returns {boolean} true if we successfully pin and false otherwise.
   */
  async pinToStartMenu() {
    if (await this.doesAppNeedStartMenuPin()) {
      try {
        let pinSuccess = await this.shellService.pinCurrentAppToStartMenu();
        Services.prefs.setBoolPref(MSIX_PREVIOUSLY_PINNED_PREF, pinSuccess);
        return pinSuccess;
      } catch (err) {
        lazy.log.warn("Error thrown during pinCurrentAppToStartMenu", err);
        Services.prefs.setBoolPref(MSIX_PREVIOUSLY_PINNED_PREF, false);
      }
    }
    return false;
  },

  /**
   * On MSIX builds, checks if Firefox app can be and is not
   * pinned to the Windows Start Menu.
   *
   * On non-MSIX builds, this function is a no-op and always returns false.
   *
   * @returns {boolean} true if this is an MSIX install and we are not yet
   *                    pinned to the Start Menu.
   *
   * @throws if not called from main process.
   */
  async doesAppNeedStartMenuPin() {
    if (
      Services.appinfo.processType !== Services.appinfo.PROCESS_TYPE_DEFAULT
    ) {
      throw new Components.Exception(
        "Can't determine pinned from child process",
        Cr.NS_ERROR_NOT_AVAILABLE
      );
    }
    if (
      Services.prefs.getBoolPref("browser.shell.disableStartMenuPin", false)
    ) {
      return false;
    }
    try {
      return (
        AppConstants.platform === "win" &&
        Services.sysinfo.getProperty("hasWinPackageId") &&
        !(await this.shellService.isCurrentAppPinnedToStartMenu())
      );
    } catch (ex) {}
    return false;
  },

  /**
   * On MSIX builds, checks if Firefox is no longer pinned to
   * the Windows Start Menu when it previously was and records
   * a Glean event if so.
   *
   * On non-MSIX builds, this function is a no-op.
   */
  async recordWasPreviouslyPinnedToStartMenu() {
    if (!Services.sysinfo.getProperty("hasWinPackageId")) {
      return;
    }
    let isPinned = await this.shellService.isCurrentAppPinnedToStartMenu();
    if (
      !isPinned &&
      Services.prefs.getBoolPref(MSIX_PREVIOUSLY_PINNED_PREF, false)
    ) {
      Services.prefs.setBoolPref(MSIX_PREVIOUSLY_PINNED_PREF, isPinned);
      Glean.startMenu.manuallyUnpinnedSinceLastLaunch.record();
    }
  },

  _throwForWDBAResult(exitCode) {
    if (exitCode != Cr.NS_OK) {
      const telemetryResult =
        new Map([
          [Cr.NS_ERROR_WDBA_NO_PROGID, "ErrExeProgID"],
          [Cr.NS_ERROR_WDBA_HASH_CHECK, "ErrExeHash"],
          [Cr.NS_ERROR_WDBA_REJECTED, "ErrExeRejected"],
          [Cr.NS_ERROR_WDBA_BUILD, "ErrBuild"],
        ]).get(exitCode) ?? "ErrExeOther";

      throw new WDBAError(exitCode, telemetryResult);
    }

    throw new Error(
      `_throwForWDBAResult called with unexpected exit code: ${exitCode}`
    );
  },

  get shortcutIconType() {
    if (AppConstants.platform === "win") {
      return { extension: "ico", mimeType: "image/vnd.microsoft.icon" };
    }

    if (AppConstants.platform === "linux") {
      return { extension: "png", mimeType: "image/png" };
    }

    throw new Error("Shortcut icons are not supported on this platform");
  },

  /**
   * This function can be used to convert compatible image formats into icons
   * compatible with the createShortcut function.
   *
   * @param {nsIFile} file - The file to write to.
   * @param {imgIContainer} imgContainer - The container holding the image.
   */
  async writeShortcutIcon(file, imgContainer) {
    let stream = lazy.imgTools.encodeScaledImage(
      imgContainer,
      ShellService.shortcutIconType.mimeType,
      256,
      256
    );
    let streamSize = stream.available();

    let bis = Cc["@mozilla.org/binaryinputstream;1"].createInstance(
      Ci.nsIBinaryInputStream
    );
    bis.setInputStream(stream);
    let newByteArray = new Uint8Array(streamSize);
    bis.readArrayBuffer(streamSize, newByteArray.buffer);
    await IOUtils.write(file.path, newByteArray);
  },

  /**
   * Creates a new Linux desktop entry for the current user.
   *
   * A Linux desktop entry is an INI-like file that complies with the
   * freedesktop.org Desktop Entry Specification [0]. It's similar to a Windows
   * shortcut, and it can appear on the desktop or application menus on
   * supported environments.
   *
   * [0]: https://specifications.freedesktop.org/desktop-entry/latest/
   *
   * @param {string} appId - The application ID that this desktop entry will be
   * used for. This should match the app_id or WM_CLASS that will be associated
   * with the window.
   * @param {string} title - The default user-visible name of the desktop
   * entry. (Localization is currently not supported.)
   * @param {string[]} argv - Arguments that should be passed to the Firefox
   * executable.
   * @param {string} iconPath - Path to the icon that should be associated with
   * the desktop entry.
   */
  async createLinuxDesktopEntry(
    appId,
    title,
    argv,
    iconPath,
    { window = null } = {}
  ) {
    if (AppConstants.platform !== "linux") {
      throw new Error(
        "createLinuxDesktopEntry is only supported on Linux-like systems"
      );
    }

    let ini = lazy.iniParserFactory.createINIParser();
    ini.QueryInterface(Ci.nsIINIParserWriter);

    // https://specifications.freedesktop.org/desktop-entry/latest/file-naming
    let isValidSegment = segment =>
      !!segment.match(/^[A-Za-z-_][A-Za-z0-9-_]*$/);
    let segments = appId.split(".");
    if (!segments || segments.map(isValidSegment).includes(false)) {
      throw new Error(`Desktop entry ID '${appId}' is invalid`);
    }

    ini.setString("Desktop Entry", "Type", "Application");
    ini.setString("Desktop Entry", "Version", "1.5");
    ini.setString("Desktop Entry", "Name", title);
    ini.setString("Desktop Entry", "Icon", iconPath);

    // All desktop files made with this must run the Firefox executable.
    argv.unshift(await ShellService._findStartupCommand());

    // https://specifications.freedesktop.org/desktop-entry/latest/exec-variables
    // (\x60 = backtick, \x24 = dollar sign, \x22 = double quote, and
    // \x5c = backslash; escaped to avoid messing with syntax highlighting)
    const escapeArg = arg => arg.replaceAll(/[\x60\x24\x22\x5c]/g, "\\$&");

    ini.setString(
      "Desktop Entry",
      "Exec",
      argv.map(arg => `"${escapeArg(arg)}"`).join(" ")
    );

    switch (this.desktopEntryApi) {
      case "filesystem":
        await IOUtils.writeUTF8(
          ShellService._getLinuxDesktopEntryPath(appId),
          ini.writeToString()
        );
        return;
      case "dynamiclauncher":
        await ShellService.requestInstallDynamicLauncher(appId, ini, window);
        return;
      default:
        throw new Error(
          "createLinuxDesktopEntry called when managing desktop entries is disabled"
        );
    }
  },

  /**
   * Tries to find a command that will reliably start this installation,
   * using the information in argv[0] if possible.
   *
   * For example, on NixOS the installation directory changes each update, so
   * we should use '/run/current-system/sw/bin/firefox', and that itself points
   * to a script which configures LD_LIBRARY_PATH so all of the dependencies
   * can be found. See bug 2021897. If we referred to the executable directly,
   * it'd point to a stale version and might be missing libraries
   *
   * Note that this won't handle cases where the script redirects over, like:
   *     #!/bin/sh
   *     exec ./firefox-bin "$@"
   * since there's no good way to tell that this runs the current installation.
   *
   * To find the best command, we look at how the browser was run initially. If
   * argv[0] is provided, the return value will either be
   *   - argv[0] itself;
   *   - the absolute path of argv[0]; or
   *   - the absolute path of a symlink _to_ argv[0] (e.g. 'firefox' or
   *     'firefox-esr').
   *
   * If argv[0] is not provided, the return value will be
   *   - the absolute path of the executable; or
   *   - the absolute path of a symlink _to_ the executable (e.g. 'firefox' or
   *     'firefox-nightly').
   *
   * This can't be perfect, but the goal is to get the best command possible.
   *
   * @returns {string} A path that, when run, should reliably start the
   * browser.
   */
  async _findStartupCommand() {
    let executableFile = Services.dirsvc.get("XREExeF", Ci.nsIFile);

    let wanted = Cc["@mozilla.org/file/local;1"].createInstance(Ci.nsIFile);
    let argv0 = ShellService.getArgv0();
    try {
      wanted.initWithPath(argv0);
    } catch (e) {
      if (argv0.includes("/")) {
        wanted.setRelativePath(
          Services.dirsvc.get("CurWorkD", Ci.nsIFile),
          argv0
        );
      } else {
        if (argv0 !== "") {
          // argv[0] doesn't seem to be a path to anything, so assume it's just
          // a command itself. If it seems to be present in the PATH, roll with
          // it, otherwise fall back to the executable.
          try {
            await lazy.Subprocess.pathSearch(argv0);
            return argv0; // if it doesn't throw
          } catch (inner) {}
        }

        // If that didn't work, or it's empty, just refer to the executable
        // directly.
        wanted.initWithFile(executableFile);
      }
    }

    let candidates = [
      wanted.leafName,
      // e.g. 'firefox-nightly'
      AppConstants.MOZ_APP_NAME + "-" + AppConstants.MOZ_UPDATE_CHANNEL,
      // e.g. 'firefox'
      AppConstants.MOZ_APP_NAME,
    ];

    let lookupPromises = candidates.map(cmd => lazy.Subprocess.pathSearch(cmd));
    let results = await Promise.allSettled(lookupPromises);

    let file = Cc["@mozilla.org/file/local;1"].createInstance(Ci.nsIFile);
    for (const { value } of results.filter(got => got.status === "fulfilled")) {
      // See if it's a symlink to this installation.
      file.initWithPath(value);

      try {
        // TODO: I think this is main thread I/O, but it looks like there's
        // no good way around it...?
        file.initWithPath(file.target);
      } catch (e) {
        // If that fails, look at the file itself (e.g. if the installation
        // directory is in $PATH).
      }

      if (file.equals(wanted)) {
        return PathUtils.filename(value);
      }
    }

    return wanted.path;
  },

  /**
   * Removes the Linux desktop entry given its app ID.
   *
   * This only removes entries within XDG_DATA_HOME as it is now, i.e. system
   * shortcuts will not be removed.
   *
   * @param {string} appId - The appId given to createLinuxDesktopEntry.
   */
  async deleteLinuxDesktopEntry(appId) {
    if (AppConstants.platform !== "linux") {
      throw new Error(
        "deleteLinuxDesktopEntry is only supported on Linux-like systems"
      );
    }

    switch (this.desktopEntryApi) {
      case "filesystem":
        await IOUtils.remove(ShellService._getLinuxDesktopEntryPath(appId));
        return;
      case "dynamiclauncher":
        await ShellService.requestUninstallDynamicLauncher(appId);
        return;
      default:
        throw new Error(
          "deleteLinuxDesktopEntry called when managing desktop entries is disabled"
        );
    }
  },

  get desktopEntryApi() {
    if (AppConstants.platform !== "linux") {
      throw new Error("desktopEntryApi is only relevant on Unix-like systems");
    }

    let pref = lazy.DESKTOP_ENTRY_API;
    if (["filesystem", "dynamiclauncher"].includes(pref)) {
      return pref;
    }

    if (pref === "disabled") {
      return null;
    }

    // If we don't recognize the pref, use the default for the current sandbox.
    if (lazy.gioService.isRunningUnderSnap) {
      // bug 2019115
      return null;
    } else if (lazy.gioService.isRunningUnderFlatpak) {
      return "dynamiclauncher";
    }

    return "filesystem";
  },

  /**
   * Determines the location of a Linux desktop entry given its app ID.
   *
   * @param {string} appId - The basename of the desktop file's name.
   * @returns {string} The path to the desktop entry.
   */
  _getLinuxDesktopEntryPath(appId) {
    if (this.desktopEntryApi !== "filesystem") {
      throw new Error(
        "Use DynamicLauncher instead of _getLinuxDesktopEntryPath when sandboxed"
      );
    }

    // TODO is there any way to reuse existing logic for this?
    // Find the location of ~/.local/share/applications.
    let dataHome = Services.env.get("XDG_DATA_HOME");
    if (!dataHome || !PathUtils.isAbsolute(dataHome)) {
      let home = Services.dirsvc.get("Home", Ci.nsIFile);
      dataHome = PathUtils.join(home.path, ".local", "share");
    }

    return PathUtils.join(dataHome, "applications", `${appId}.desktop`);
  },

  async requestCreateAndPinSecondaryTile(tileId, name, iconPath, args) {
    let resolver = Promise.withResolvers();

    lazy.secondaryTileService.requestCreateAndPin(
      tileId,
      name,
      iconPath,
      args,
      this._secondaryTileListener("Secondary tile pinning failed", resolver)
    );

    return resolver.promise;
  },

  async requestDeleteSecondaryTile(tileId) {
    let resolver = Promise.withResolvers();

    lazy.secondaryTileService.requestDelete(
      tileId,
      this._secondaryTileListener("Secondary tile unpinning failed", resolver)
    );

    return resolver.promise;
  },

  _secondaryTileListener(errorMessage, resolver) {
    return {
      QueryInterface: ChromeUtils.generateQI([Ci.nsISecondaryTileListener]),
      succeeded(outcome) {
        resolver.resolve(outcome);
      },
      failed(hresult) {
        let formatted = hresult.toString(16).padStart(8, "0");
        let error = new Error(`${errorMessage} (HRESULT ${formatted})`);
        resolver.reject(error);
      },
    };
  },

  /**
   * Sets the desktop background for the current user to the given image.
   *
   * Note that this doesn't check enterprise policies, etc.; that's considered
   * the caller's responsibility.
   *
   * @param {CanvasImageSource} drawable - The image to set.
   * @param {number} width - The width of `drawable`.
   * @param {number} height - The height of `drawable`.
   * @param {number} position - The position of the desktop background on the
   * display. This should be one of the BACKGROUND_* properties in
   * nsIShellService. (Note that macOS ignores this value.)
   */
  async setDesktopBackground(drawable, width, height, position) {
    let canvas = new OffscreenCanvas(width, height);
    let ctx = canvas.getContext("2d");
    ctx.drawImage(drawable, 0, 0);

    let [bytes, file] = await Promise.all([
      canvas.convertToBlob({ type: "image/png" }).then(blob => blob.bytes()),
      this._getDesktopBackgroundFile(),
    ]);

    await IOUtils.write(file.path, bytes);
    this.setDesktopBackgroundFile(file, position);
  },

  /**
   * Gets the path which will be used for the image saved by
   * setDesktopBackground. This is mainly separated so it can be used and
   * stubbed out by tests.
   *
   * @returns {nsIFile} The file which will be used for the background image.
   */
  async _getDesktopBackgroundFile() {
    let leafName = await new Localization([
      "browser/setDesktopBackground.ftl",
    ]).formatValue("set-desktop-background-filename");

    leafName = lazy.mimeService.validateFileNameForSaving(
      leafName + ".png",
      "image/png",
      Ci.nsIMIMEService.VALIDATE_SANITIZE_ONLY
    );

    let file = Services.dirsvc.get("AppRegD", Ci.nsIFile);
    file.append(leafName);
    return file;
  },
};

// Functions may be present or absent dependent on whether the `nsIShellService`
// has been queried for the interface implementing it, as querying the interface
// adds it's functions to the queried JS object. Coincidental querying is more
// likely to occur for Firefox Desktop than a Firefox Background Task. To force
// consistent behavior, we query the native shell interface inheriting from
// `nsIShellService` on setup.
let shellInterface;
switch (AppConstants.platform) {
  case "win":
    shellInterface = Ci.nsIWindowsShellService;
    break;
  case "macosx":
    shellInterface = Ci.nsIMacShellService;
    break;
  case "linux":
    shellInterface = Ci.nsIGNOMEShellService;
    break;
  default:
    lazy.log.warn(
      `No platform native shell service interface for ${AppConstants.platform} queried, add for new platforms.`
    );
    shellInterface = Ci.nsIShellService;
}

XPCOMUtils.defineLazyServiceGetters(ShellServiceInternal, {
  defaultAgent: ["@mozilla.org/default-agent;1", Ci.nsIDefaultAgent],
  shellService: ["@mozilla.org/browser/shell-service;1", shellInterface],
  macDockSupport: [
    "@mozilla.org/widget/macdocksupport;1",
    Ci.nsIMacDockSupport,
  ],
});

/**
 * The external API exported by this module.
 */
export var ShellService = new Proxy(ShellServiceInternal, {
  get(target, name) {
    if (name in target) {
      return target[name];
    }
    // n.b. If a native shell interface member is not present on `shellService`,
    // it may be necessary to query the native interface.
    if (target.shellService && name in target.shellService) {
      return target.shellService[name];
    }
    lazy.log.warn(
      `${name.toString()} not found in ShellService: ${target.shellService}`
    );
    return undefined;
  },
});

class WDBAError extends Error {
  constructor(exitCode, telemetryResult) {
    super(`WDBA nonzero exit code ${exitCode}: ${telemetryResult}`);

    this.exitCode = exitCode;
    this.telemetryResult = telemetryResult;
  }
}
