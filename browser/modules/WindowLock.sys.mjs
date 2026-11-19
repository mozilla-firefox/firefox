/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

import { AppConstants } from "resource://gre/modules/AppConstants.sys.mjs";
import { XPCOMUtils } from "resource://gre/modules/XPCOMUtils.sys.mjs";

const lazy = XPCOMUtils.declareLazy({
  BrowserWindowTracker: "resource:///modules/BrowserWindowTracker.sys.mjs",
  PrivateBrowsingUtils: "resource://gre/modules/PrivateBrowsingUtils.sys.mjs",
  lockPrivateWindows: {
    pref: "browser.privatebrowsing.lockOnScreenLock.enabled",
    default: false,
    onUpdate: () => WindowLock._updateShortcuts(),
  },
  featureEnabled: {
    pref: "browser.privatebrowsing.lockOnScreenLock.featureGate",
    default: false,
    onUpdate: () => WindowLock._updateShortcuts(),
  },
  l10n: () =>
    new Localization(["branding/brand.ftl", "browser/windowLock.ftl"]),
});

const LOCKED_ATTRIBUTE = "windowlocked";
const DIALOG_ID = "window-lock-dialog";
const SHORTCUT_KEY_ID = "key_lockWindows";
const SUPPORTED_PLATFORMS = ["macosx", "win"];
const OS_AUTH_MESSAGE =
  AppConstants.platform == "win"
    ? "window-lock-private-os-auth-dialog-message-win"
    : "window-lock-private-os-auth-dialog-message-macosx";
// Window commands that stay available on the lock screen's titlebar.
const WINDOW_COMMANDS = [
  "cmd_minimizeWindow",
  "cmd_maximizeWindow",
  "cmd_restoreWindow",
  "cmd_closeWindow",
];

/**
 * Values of the result extra of the window_lock.authenticate event.
 */
const AuthResult = Object.freeze({
  SUCCESS: "success",
  SUCCESS_NO_PASSWORD: "success_no_password",
  FAILED: "failed",
  WINDOW_CLOSED: "window_closed",
});

/**
 * Values of the trigger extra of the window_lock.locked event.
 */
const LockTrigger = Object.freeze({
  SCREEN_LOCK: "screen_lock",
  SCREENSAVER: "screensaver",
  SLEEP: "sleep",
  SHORTCUT: "shortcut",
});

// Maps the topics windows get locked on to their LockTrigger.
const LOCK_TOPICS = {
  screen_lock_notification: LockTrigger.SCREEN_LOCK,
  screensaver_start_notification: LockTrigger.SCREENSAVER,
  sleep_notification: LockTrigger.SLEEP,
};

/**
 * Covers private windows with a lock screen when the OS session gets locked,
 * starts the screensaver or goes to sleep, or when the user presses the keyboard
 * shortcut, until the user authenticates with their OS account
 * credentials, if enabled by the
 * browser.privatebrowsing.lockOnScreenLock.enabled pref. Only supported on
 * macOS and Windows.
 */
export const WindowLock = {
  /**
   * Whether private windows are currently locked.
   */
  locked: false,

  _initialized: false,
  _pendingAuth: null,
  // Whether the last authentication attempt failed with an error.
  _authError: false,
  // Stacks keeping menus and commands disabled while locked, per window.
  _menuAndCommandDisablers: new WeakMap(),
  // Popups that had key handling disabled while locked, per window, with the
  // ignorekeys value to restore on unlock.
  _ignoreKeysPopups: new WeakMap(),
  // Browsers put into modal state while locked, which suspends timers,
  // workers and AudioContexts of their pages.
  _modalStateBrowsers: new WeakSet(),
  // Browsers muted while locked, to unmute them once unlocked.
  _mutedBrowsers: new WeakSet(),

  /**
   * Whether private windows get locked, which is false if locking windows
   * isn't available.
   */
  get enabled() {
    return lazy.featureEnabled && lazy.lockPrivateWindows;
  },

  init() {
    if (
      this._initialized ||
      !SUPPORTED_PLATFORMS.includes(AppConstants.platform)
    ) {
      return;
    }
    this._initialized = true;
    for (let topic of Object.keys(LOCK_TOPICS)) {
      Services.obs.addObserver(this, topic);
    }
    Services.obs.addObserver(this, "last-pb-context-exited");
  },

  uninit() {
    if (!this._initialized) {
      return;
    }
    this._initialized = false;
    for (let topic of Object.keys(LOCK_TOPICS)) {
      Services.obs.removeObserver(this, topic);
    }
    Services.obs.removeObserver(this, "last-pb-context-exited");
  },

  /**
   * @param {Window} window
   * @returns {boolean} Whether the window is covered by the lock screen.
   */
  isWindowLocked(window) {
    return window.document.documentElement.hasAttribute(LOCKED_ATTRIBUTE);
  },

  /**
   * Locks a newly opened private window if private windows are currently
   * locked.
   *
   * @param {Window} window
   */
  initWindow(window) {
    this._updateShortcuts(window);
    if (this.locked && lazy.PrivateBrowsingUtils.isWindowPrivate(window)) {
      this._lockWindow(window);
    }
  },

  /**
   * Locks private windows on LOCK_TOPICS and drops the lock once the last
   * private window is gone.
   *
   * @param {nsISupports} _subject
   * @param {string} topic
   */
  observe(_subject, topic) {
    if (topic == "last-pb-context-exited") {
      this.locked = false;
      this._updateShortcuts();
      return;
    }
    if (Object.hasOwn(LOCK_TOPICS, topic)) {
      this.lock(LOCK_TOPICS[topic]);
    }
  },

  /**
   * Locks private windows if enabled by the
   * browser.privatebrowsing.lockOnScreenLock.enabled pref.
   *
   * @param {string} trigger
   *        The LockTrigger value of the event causing the lock.
   */
  lock(trigger) {
    if (!this.enabled || this.locked) {
      return;
    }
    let windows = this._windows().filter(win =>
      lazy.PrivateBrowsingUtils.isWindowPrivate(win)
    );
    // No windows to lock
    if (!windows.length) {
      return;
    }
    this.locked = true;
    this._authError = false;
    for (let window of windows) {
      this._lockWindow(window);

      if (trigger == LockTrigger.SHORTCUT) {
        window.minimize();
      }
    }
    Glean.windowLock.locked.record({ trigger });
  },

  /**
   * Locks private windows in response to the keyboard shortcut.
   */
  lockFromShortcut() {
    this.lock(LockTrigger.SHORTCUT);
  },

  /**
   * Asks the user for their OS credentials and unlocks all locked windows if
   * they authenticate successfully.
   *
   * @param {Window} window
   *        The window requesting the unlock.
   * @returns {Promise<boolean>} Whether windows are unlocked.
   */
  async unlock(window) {
    if (!this.locked) {
      return true;
    }
    if (!this._pendingAuth) {
      this._setAuthError(false);
      this._pendingAuth = this._authenticate(window).finally(() => {
        this._pendingAuth = null;
      });
    }
    if (!(await this._pendingAuth)) {
      return false;
    }
    if (this.locked) {
      this.locked = false;
      this._unlockAllWindows();
    }
    return true;
  },

  /**
   * Shows the OS authentication dialog.
   *
   * @param {Window} window
   *        The window to parent the OS authentication dialog to.
   * @returns {Promise<boolean>} Whether the user authenticated successfully.
   */
  async _authenticate(window) {
    // The caption is only shown on Windows.
    let [message, caption] = await lazy.l10n.formatValues([
      OS_AUTH_MESSAGE,
      "window-lock-os-auth-dialog-caption",
    ]);
    let authenticated = false;
    let authResult = AuthResult.FAILED;
    try {
      let [reauthenticated, isBlankPassword] = await Cc[
        "@mozilla.org/security/osreauthenticator;1"
      ]
        .getService(Ci.nsIOSReauthenticator)
        .asyncReauthenticateUser(message, caption, window);
      authenticated = !!reauthenticated;
      if (authenticated) {
        authResult = isBlankPassword
          ? AuthResult.SUCCESS_NO_PASSWORD
          : AuthResult.SUCCESS;
      }
    } catch (ex) {
      console.error(ex);
      this._setAuthError(true);
    }
    Glean.windowLock.authenticate.record({ result: authResult });
    return authenticated;
  },

  /**
   * Enables the keyboard shortcut in all windows if it would lock any window,
   * so the key event reaches pages otherwise.
   *
   * @param {Window} [newWindow]
   *        A window being opened, which may not be tracked yet.
   */
  _updateShortcuts(newWindow) {
    let windows = this._windows();
    if (newWindow && !windows.includes(newWindow)) {
      windows.push(newWindow);
    }
    let enabled =
      this.enabled &&
      windows.some(win => lazy.PrivateBrowsingUtils.isWindowPrivate(win));
    for (let window of windows) {
      window.document
        .getElementById(SHORTCUT_KEY_ID)
        ?.toggleAttribute("disabled", !enabled);
    }
  },

  _windows() {
    return lazy.BrowserWindowTracker.orderedWindows.filter(win => !win.closed);
  },

  /**
   * Shows or hides the authentication error on the lock screens.
   *
   * @param {boolean} shown
   */
  _setAuthError(shown) {
    this._authError = shown;
    for (let window of this._windows()) {
      let error = window.document.querySelector(
        `#${DIALOG_ID} .window-lock-error`
      );
      if (error) {
        error.hidden = !shown;
      }
    }
  },

  _unlockAllWindows() {
    for (let window of this._windows()) {
      this._unlockWindow(window);
    }
  },

  /**
   * Covers the window with the lock screen.
   *
   * @param {Window} window
   */
  _lockWindow(window) {
    let { document } = window;
    let dialog =
      document.getElementById(DIALOG_ID) ?? this._createDialog(window);
    dialog.querySelector(".window-lock-error").hidden = !this._authError;

    let root = document.documentElement;
    if (root.hasAttribute(LOCKED_ATTRIBUTE)) {
      return;
    }
    root.setAttribute(LOCKED_ATTRIBUTE, "true");
    this._updateTitlebarHeight(window, dialog);

    for (let popup of document.querySelectorAll("panel, menupopup")) {
      if (popup.state == "open" || popup.state == "showing") {
        popup.hidePopup();
      }
    }
    for (let urlbar of document.querySelectorAll(".urlbar")) {
      urlbar.view?.close();
    }
    for (let browser of window.gBrowser.browsers) {
      if (!browser.browsingContext) {
        continue;
      }
      let controller = browser.browsingContext.mediaController;
      // Remove OS media controller so no information is leaked
      if (controller?.isActive) {
        controller.stop();
      }
      if (controller && !controller.isMuted) {
        controller.mute();
        this._mutedBrowsers.add(browser);
      }
      browser.enterModalState();
      this._modalStateBrowsers.add(browser);
    }

    if (!dialog.open) {
      dialog.showModal();
    }

    let disposables = new DisposableStack();
    window.gDialogBox?.disableMenusAndCommands(disposables);
    this._menuAndCommandDisablers.set(window, disposables);
    for (let id of WINDOW_COMMANDS) {
      document.getElementById(id)?.removeAttribute("disabled");
    }
    window.gBrowser.updateTitlebar();
    window.UpdatePopupNotificationsVisibility();
  },

  /**
   * Removes the lock screen from the window.
   *
   * @param {Window} window
   */
  _unlockWindow(window) {
    let { document } = window;
    let root = document.documentElement;
    if (!root.hasAttribute(LOCKED_ATTRIBUTE)) {
      return;
    }
    root.removeAttribute(LOCKED_ATTRIBUTE);

    document.getElementById(DIALOG_ID)?.close();

    for (let [popup, ignoreKeys] of this._ignoreKeysPopups.get(window) ?? []) {
      if (ignoreKeys == null) {
        popup.removeAttribute("ignorekeys");
      } else {
        popup.setAttribute("ignorekeys", ignoreKeys);
      }
    }
    this._ignoreKeysPopups.delete(window);

    for (let browser of window.gBrowser.browsers) {
      if (this._modalStateBrowsers.delete(browser)) {
        browser.maybeLeaveModalState();
      }
      if (this._mutedBrowsers.delete(browser)) {
        browser.browsingContext?.mediaController?.unmute();
      }
    }

    this._menuAndCommandDisablers.get(window)?.dispose();
    this._menuAndCommandDisablers.delete(window);
    window.gBrowser.updateTitlebar();
    window.UpdatePopupNotificationsVisibility();
  },

  /**
   * Makes the lock screen's titlebar as tall as the one it covers, so the
   * caption buttons stay in place.
   *
   * @param {Window} window
   * @param {HTMLDialogElement} dialog
   */
  _updateTitlebarHeight(window, dialog) {
    let buttonbox = Array.from(
      window.document.querySelectorAll(
        "#navigator-toolbox .titlebar-buttonbox-container"
      )
    ).find(element => element.checkVisibility());
    let height =
      buttonbox &&
      window.windowUtils.getBoundsWithoutFlushing(buttonbox).height;
    if (height) {
      dialog.style.setProperty("--window-lock-titlebar-height", `${height}px`);
    } else {
      dialog.style.removeProperty("--window-lock-titlebar-height");
    }
  },

  /**
   * Creates the lock screen dialog from its template and inserts it into the
   * window.
   *
   * @param {Window} window
   * @returns {HTMLDialogElement}
   */
  _createDialog(window) {
    let { document } = window;
    window.MozXULElement.insertFTLIfNeeded("browser/windowLock.ftl");

    let template = document.getElementById("window-lock-dialog-template");
    let dialog = document.importNode(template.content.firstElementChild, true);
    dialog
      .querySelector(".window-lock-button")
      .addEventListener("click", () => this.unlock(window));

    // Popups are hidden while locked, so they must not handle keys either.
    window.addEventListener(
      "popupshowing",
      event => {
        let popup = event.composedTarget;
        if (
          !document.documentElement.hasAttribute(LOCKED_ATTRIBUTE) ||
          popup.getAttribute("ignorekeys") == "true"
        ) {
          return;
        }
        let popups = this._ignoreKeysPopups.get(window);
        if (!popups) {
          popups = new Map();
          this._ignoreKeysPopups.set(window, popups);
        }
        popups.set(popup, popup.getAttribute("ignorekeys"));
        popup.setAttribute("ignorekeys", "true");
      },
      true
    );

    window.addEventListener("unload", () => {
      if (
        document.documentElement.hasAttribute(LOCKED_ATTRIBUTE) &&
        !Services.startup.shuttingDown
      ) {
        Glean.windowLock.authenticate.record({
          result: AuthResult.WINDOW_CLOSED,
        });
      }
    });

    // The lock screen must only go away once the user authenticated.
    dialog.addEventListener("cancel", event => event.preventDefault());
    dialog.addEventListener("close", () => {
      if (
        document.documentElement.hasAttribute(LOCKED_ATTRIBUTE) &&
        !dialog.open
      ) {
        dialog.showModal();
      }
    });

    template.after(dialog);
    return dialog;
  },
};
