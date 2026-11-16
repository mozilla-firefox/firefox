/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

const lazy = {};
ChromeUtils.defineESModuleGetters(lazy, {
  PopupNotifications: "resource://gre/modules/PopupNotifications.sys.mjs",
  SitePermissions: "resource:///modules/SitePermissions.sys.mjs",
});

const { SidebarPermissionsUI } = ChromeUtils.importESModule(
  "chrome://browser/content/sidebar/sidebar-permissions-ui.mjs"
);

/**
 * Get count of queued tab notifications
 */
function getTabNotificationCount(win) {
  const tabBrowser = win.gBrowser?.selectedBrowser;
  if (!tabBrowser || !win.PopupNotifications) {
    return 0;
  }

  const notifications =
    win.PopupNotifications.getNotificationsForBrowser(tabBrowser);
  return notifications.length ?? 0;
}

/**
 * Cancel all tab notifications within same window.
 * When sidebar takes over, we cancel all pending tab notifications,
 * not just the one matching the current permission type.
 */
function cancelAllTabNotifications(win) {
  const tabBrowser = win.gBrowser?.selectedBrowser;

  if (tabBrowser && win.PopupNotifications) {
    removeNotificationsForBrowser(win.PopupNotifications, tabBrowser);
  }
}

/**
 * Remove all pending notifications for a browser from a notification system.
 *
 * @param {PopupNotifications} popupNotifications - The notification system (SidebarPopupNotifications)
 * @param {Browser} browser - The browser to remove notifications
 */
function removeNotificationsForBrowser(popupNotifications, browser) {
  const notifications = popupNotifications.getNotificationsForBrowser(browser);
  if (notifications?.length) {
    for (const notification of [...notifications]) {
      popupNotifications.remove(notification, /* withoutUserResponse = */ true);
    }
  }
}

/**
 * Per window Sidebar Permissions handles:
 * - webrtc request UI
 * - webrtc sharing UI
 * - blocked webrtc UI
 * - sidebar PopupNotification placement
 * - PopupNotification coordination between sidebar and tabs (same window and cross window)
 */
export class SidebarPermissions {
  #initialized = false;
  #window;
  #browser = null;
  #contentBrowser = null;
  #sidebarPermissionUI = null;
  #observerBound = false;
  #popupshowingHandler;
  #panelOpenHandler;
  #popuphiddenHandler;
  #currentPopupNotificationBrowser;
  #onPermissionStateChanged;
  #onSidebarBrowserChanged;
  #securityChangeListener;

  constructor(win) {
    this.#window = win;
  }

  /**
   * Getter for contentBrowser
   */
  get contentBrowser() {
    return this.#contentBrowser;
  }

  /**
   * Called by SidebarController after the sidebar nested browser is ready.
   * Sets up all the permission UI and observers.
   */
  init(browser) {
    const win = this.#window;
    this.#browser = browser;

    if (!this.#browser?.contentDocument) {
      console.warn("Sidebar browser is not ready");
      return;
    }

    // Window level setup - per window
    if (!this.#initialized) {
      this.#bindObservers();

      win.addEventListener("unload", () => this.#uninit(), {
        once: true,
      });

      this.#initialized = true;
    }

    // Re-bind on every init as onSidebarHidden will remove them
    this.#bindObservers();

    if (!this.#onSidebarBrowserChanged) {
      // Also listen to custom event as a hint, but verify with actual DOM check
      // this allows chat.js or other tools to trigger changes
      this.#onSidebarBrowserChanged = this.onContentBrowserChanged.bind(this);
      win.addEventListener(
        "sidebarbrowserchanged",
        this.#onSidebarBrowserChanged
      );
    }

    // Ensure SidebarPopupNotifications exists (may need to recreate after sidebar close/reopen)
    if (!win.SidebarPopupNotifications) {
      this.#setupSidebarPopupNotifications();
    }

    // Content level setup - runs every eligible permission sidebar tool opens
    // When sidebar is closed or change tool
    this.#sidebarPermissionUI = new SidebarPermissionsUI(win, this.#browser);
    this.#sidebarPermissionUI.build();
    this.#setupSidebarPopupNotificationWrapper();

    this.#browser.contentWindow?.addEventListener("SidebarWillHide", this);
    // Ensure onSidebarHidden gets called when switching to another sidebar panel
    this.#browser.contentWindow?.addEventListener("unload", this);

    if (this.#contentBrowser && this.#onPermissionStateChanged) {
      this.#contentBrowser.removeEventListener(
        "PermissionStateChange",
        this.#onPermissionStateChanged,
        true
      );
    }

    this.#contentBrowser =
      this.#browser.contentDocument.querySelector("browser");
    this.#sidebarPermissionUI.setContentBrowser(this.#contentBrowser);

    this.#setupPermissionStateListener();
  }

  /**
   * Remove SidebarPopupNotification.show wrappers when sidebar is hidden
   */
  onSidebarHidden() {
    const win = this.#window;

    if (this.#observerBound) {
      Services.obs.removeObserver(this, "perm-changed");
      this.#observerBound = false;
    }

    if (this.#contentBrowser?._sharingState) {
      this.#contentBrowser._sharingState = {};
    }

    // Cancel any active SidebarPopupNotification before unwrapping
    this.#cancelSidebarNotifications();

    const panel = win.document?.getElementById("notification-popup");
    if (this.#popupshowingHandler) {
      panel?.removeEventListener(
        "popupshowing",
        this.#popupshowingHandler,
        true
      );
      this.#popupshowingHandler = null;
    }
    if (this.#panelOpenHandler) {
      panel?.removeEventListener("popupshown", this.#panelOpenHandler, true);
      panel?.removeEventListener("PanelUpdated", this.#panelOpenHandler, true);
      this.#panelOpenHandler = null;
    }
    if (this.#popuphiddenHandler) {
      panel?.removeEventListener("popuphidden", this.#popuphiddenHandler, true);
      this.#popuphiddenHandler = null;
    }
    this.#currentPopupNotificationBrowser = null;

    if (this.#onSidebarBrowserChanged) {
      win.removeEventListener(
        "sidebarbrowserchanged",
        this.#onSidebarBrowserChanged
      );
      this.#onSidebarBrowserChanged = null;
    }

    // Remove wrapper when sidebar is closed/hidden
    if (win.SidebarPopupNotifications) {
      win.SidebarPopupNotifications._currentAnchorElement = null;

      if (win.SidebarPopupNotifications._wrappedBySidebarPermissions) {
        win.SidebarPopupNotifications.show =
          win.SidebarPopupNotifications._originalShow;
      }

      delete win.SidebarPopupNotifications;
    }

    if (this.#contentBrowser && this.#securityChangeListener) {
      try {
        this.#contentBrowser.removeProgressListener(
          this.#securityChangeListener
        );
      } catch (e) {}
      this.#securityChangeListener = null;
    }

    if (this.#contentBrowser && this.#onPermissionStateChanged) {
      this.#contentBrowser.removeEventListener(
        "PermissionStateChange",
        this.#onPermissionStateChanged,
        true
      );
      this.#onPermissionStateChanged = null;
    }

    this.#browser?.contentWindow?.removeEventListener("SidebarWillHide", this);
    this.#browser?.contentWindow?.removeEventListener("unload", this);

    // Clear references to prevent leaks
    this.#browser = null;
    this.#contentBrowser = null;
    this.#sidebarPermissionUI?.destroy();
    this.#sidebarPermissionUI = null;
  }

  /**
   * Clean up observers and listeners on unload.
   */
  #uninit() {
    const win = this.#window;

    if (this.#contentBrowser && this.#securityChangeListener) {
      try {
        this.#contentBrowser.removeProgressListener(
          this.#securityChangeListener
        );
      } catch (e) {
        // Listener wasn't on this browser
        console.warn("Failed to remove progress listener:", e.message);
      }
      this.#securityChangeListener = null;
    }

    if (this.#contentBrowser && this.#onPermissionStateChanged) {
      this.#contentBrowser.removeEventListener(
        "PermissionStateChange",
        this.#onPermissionStateChanged,
        true
      );
      this.#onPermissionStateChanged = null;
    }

    // This makes sure the wrappers are removed even if hide event didn't fire
    this.onSidebarHidden();

    const panel = win.document?.getElementById("notification-popup");
    if (this.#popupshowingHandler) {
      panel?.removeEventListener(
        "popupshowing",
        this.#popupshowingHandler,
        true
      );
      this.#popupshowingHandler = null;
    }

    if (this.#panelOpenHandler) {
      panel?.removeEventListener("popupshown", this.#panelOpenHandler, true);

      panel?.removeEventListener("PanelUpdated", this.#panelOpenHandler, true);
      this.#panelOpenHandler = null;
    }

    if (this.#popuphiddenHandler) {
      panel?.removeEventListener("popuphidden", this.#popuphiddenHandler, true);
      this.#popuphiddenHandler = null;
    }
    this.#currentPopupNotificationBrowser = null;

    if (this.#observerBound) {
      Services.obs.removeObserver(this, "perm-changed");
      this.#observerBound = false;
    }

    if (this.#onSidebarBrowserChanged) {
      this.#window.removeEventListener(
        "sidebarbrowserchanged",
        this.#onSidebarBrowserChanged
      );
      this.#onSidebarBrowserChanged = null;
    }

    if (win.SidebarPopupNotifications) {
      delete win.SidebarPopupNotifications;
    }

    // Reset browser override
    if (win.gPermissionPanel) {
      win.gPermissionPanel.clearBrowserOverride();
    }

    this.#sidebarPermissionUI?.destroy();
    this.#sidebarPermissionUI = null;
    this.#contentBrowser = null;
    this.#browser = null;
  }

  /**
   * Create SidebarPopupNotifications instance for this window.
   * This redirects PopupNotification to use sidebar anchors instead of browser chrome.
   * Because both sidebar and tab PopupNotifications use the same panel element
   * this function wraps SidebarPopupNotification.show() methods to cancel the others before showing to ensure only
   * one permission popup is visible at a time.
   */
  #setupSidebarPopupNotifications() {
    const win = this.#window;

    const chromeDoc = win.document;
    let panel = chromeDoc.getElementById("notification-popup");
    const iconBox = chromeDoc.getElementById("notification-popup-box");

    win.SidebarPopupNotifications = new lazy.PopupNotifications(
      this.#browser,
      panel,
      iconBox,
      {
        getVisibleAnchorElement(anchorElement) {
          if (
            anchorElement?.checkVisibility(
              lazy.PopupNotifications.CHECK_VISIBILITY_OPTIONS
            )
          ) {
            return anchorElement;
          }

          const sidebarDocument =
            win.SidebarController?.browser?.contentDocument;
          const micAnchor = sidebarDocument?.getElementById(
            "sidebar-webrtc-microphone-notification-icon"
          );
          if (
            micAnchor?.checkVisibility(
              lazy.PopupNotifications.CHECK_VISIBILITY_OPTIONS
            )
          ) {
            return micAnchor;
          }

          return iconBox;
        },
      }
    );

    // Only  when a sidebar is open, hook SidebarPopupNotification show()
    // Cancel pending all other PopupNotifications
    this.#wrapSidebarShow(win.SidebarPopupNotifications);

    // Track which browser notification is showing
    this.#currentPopupNotificationBrowser = null;

    this.#popupshowingHandler = () => {
      const firstChild = panel.firstElementChild;
      const browser = firstChild?.notification?.browser;
      const isSidebarNotification = browser === this.#contentBrowser;

      if (isSidebarNotification) {
        this.#currentPopupNotificationBrowser = browser;
        this.#sidebarPermissionUI.showMicRequestUI();
      }
    };

    this.#panelOpenHandler = () => {
      this.#handlePopupChange(panel);
    };

    this.#popuphiddenHandler = () => {
      // Ignore if we're in the middle of showing sidebar
      if (win.SidebarPopupNotifications?._isShowing) {
        return;
      }
      // Only handle sidebar PopupNotifications
      if (this.#currentPopupNotificationBrowser === this.#contentBrowser) {
        this.#onSidebarPopupNotificationHidden();
      }
    };

    // Store current PopupNotification Browser to check with this.#contentBrowser.
    panel.addEventListener("popupshowing", this.#popupshowingHandler, true);
    panel.addEventListener("popupshown", this.#panelOpenHandler, true);
    panel.addEventListener("PanelUpdated", this.#panelOpenHandler, true);
    // When the active SidebarPopupNotification is hidden just cancel the notification
    panel.addEventListener("popuphidden", this.#popuphiddenHandler, true);
  }

  #handlePopupChange(panel) {
    if (panel.state !== "open") {
      return;
    }

    const firstChild = panel.firstElementChild;
    const browser = firstChild?.notification?.browser;
    this.#currentPopupNotificationBrowser = browser;

    const isSidebarNotification = browser === this.#contentBrowser;

    if (!isSidebarNotification) {
      this.#cancelSidebarNotifications();
    }
  }

  /**
   * Setup SidebarPopupNotification wrapper for sidebar.
   * Called on every sidebar open or changed provider
   */
  #setupSidebarPopupNotificationWrapper() {
    const win = this.#window;

    if (win.SidebarPopupNotifications) {
      // if it is wrapped already it won't be rewrapped
      this.#wrapSidebarShow(win.SidebarPopupNotifications);
    }
  }

  /**
   * Called when sidebar PopupNotification is hidden
   */
  #onSidebarPopupNotificationHidden() {
    this.#cancelSidebarNotifications();
    this.updatePermissionIcons();
  }

  /**
   * Cancel all SidebarPopupNotification in current window.
   */
  #cancelSidebarNotifications() {
    const win = this.#window;

    if (this.#contentBrowser && win.SidebarPopupNotifications) {
      removeNotificationsForBrowser(
        win.SidebarPopupNotifications,
        this.#contentBrowser
      );
    }
  }

  /**
   * Wrap SidebarPopupNotifications.show() to cancel tab notifications
   * if SidebarPopupNotification is requested.
   *
   * @param {PopupNotifications} sidebarInstance - The SidebarPopupNotifications instance
   */
  #wrapSidebarShow(sidebarInstance) {
    // TODO - Bug 2009301: This approach is hacky to cancel the active tab Notification.
    //  Should revisit and find a better solution.
    if (sidebarInstance._wrappedBySidebarPermissions) {
      return;
    }

    const originalShow = sidebarInstance.show;
    const win = this.#window;
    sidebarInstance._isShowing = false;

    sidebarInstance.show = function (browser, id, ...args) {
      const panel = win.document.getElementById("notification-popup");

      sidebarInstance._isShowing = true;

      // Check how many tab notifications exist
      const tabNotificationCount = getTabNotificationCount(win);
      if (tabNotificationCount >= 1) {
        // Cancel all tab notifications
        cancelAllTabNotifications(win);
      }

      // When multiple tab notifications are queued, canceling them triggers
      // multiple popuphidden events. wait for the panels to fully closed before
      // showing the sidebar notification to avoid UI conflicts.
      if (tabNotificationCount > 1 && panel) {
        const showAfterHidden = async () => {
          await new Promise(resolve => {
            if (panel && panel.state === "closed") {
              panel.addEventListener("popuphidden", resolve, { once: true });
            } else {
              resolve();
            }
          });

          const result = originalShow.call(
            sidebarInstance,
            browser,
            id,
            ...args
          );
          sidebarInstance._isShowing = false;
          return result;
        };
        return showAfterHidden();
      }

      // No queued notifications in tab, show SidebarNotification immediately.
      const result = originalShow.call(sidebarInstance, browser, id, ...args);
      sidebarInstance._isShowing = false;
      return result;
    };

    sidebarInstance._wrappedBySidebarPermissions = true;
    sidebarInstance._originalShow = originalShow;
  }

  #bindObservers() {
    if (this.#observerBound) {
      return;
    }

    Services.obs.addObserver(this, "perm-changed");
    this.#observerBound = true;
  }

  observe(subject, topic) {
    switch (topic) {
      case "perm-changed":
        this.#onPermissionChanged(subject);
        break;
    }
  }

  handleEvent(event) {
    switch (event.type) {
      case "SidebarWillHide":
      case "unload":
        this.onSidebarHidden();
        break;
    }
  }

  onContentBrowserChanged() {
    const win = this.#window;
    // Remove listeners from old browser
    if (this.#contentBrowser && this.#onPermissionStateChanged) {
      this.#contentBrowser.removeEventListener(
        "PermissionStateChange",
        this.#onPermissionStateChanged,
        true
      );
    }

    if (this.#contentBrowser && this.#securityChangeListener) {
      try {
        this.#contentBrowser.removeProgressListener(
          this.#securityChangeListener
        );
      } catch (e) {
        // Listener wasn't on this browser
        console.warn("Failed to remove progress listener:", e.message);
      }
    }

    // Cancel any active sidebar notification for older browser
    this.#cancelSidebarNotifications();

    // Reset UI
    this.#sidebarPermissionUI?.clearUI();

    // Get the new provider browser
    this.#contentBrowser =
      this.#browser?.contentDocument?.querySelector("browser");
    this.#sidebarPermissionUI.setContentBrowser(this.#contentBrowser);

    if (this.#contentBrowser) {
      const _self = this;

      this.#securityChangeListener = {
        _previousOrigin: null,

        onSecurityChange() {
          const currentOrigin = _self.#contentBrowser?.contentPrincipal?.origin;

          if (this._previousOrigin === currentOrigin) {
            return;
          }

          this._previousOrigin = currentOrigin;
          _self.updatePermissionIcons();
        },
        QueryInterface: ChromeUtils.generateQI([
          "nsIWebProgressListener",
          "nsISupportsWeakReference",
        ]),
      };

      this.#contentBrowser.addProgressListener(
        this.#securityChangeListener,
        Ci.nsIWebProgress.NOTIFY_SECURITY
      );

      this.#setupPermissionStateListener();
    }

    // Reset browser override to gPanel
    if (win.gPermissionPanel) {
      win.gPermissionPanel.clearBrowserOverride();
    }
  }

  #onPermissionChanged(subject) {
    // Only care about microphone for now
    const permission = subject.QueryInterface(Ci.nsIPermission);
    if (permission.type !== "microphone") {
      return;
    }

    if (!this.#sidebarPermissionUI?.isReady()) {
      return;
    }

    if (this.#sidebarPermissionUI.isIdentityBoxOpen()) {
      return;
    }

    const sidebarPrincipal = this.#contentBrowser?.contentPrincipal;

    // Only care if permission is the same as sidebar's origin
    if (permission.principal?.origin !== sidebarPrincipal?.origin) {
      return;
    }

    this.updatePermissionIcons();
  }

  #setupPermissionStateListener() {
    if (!this.#contentBrowser) {
      return;
    }

    this.#onPermissionStateChanged = event => {
      const currentBrowser = event.target;

      // Sidebar browser changed
      if (currentBrowser === this.#contentBrowser) {
        this.updatePermissionIcons();
      }
    };

    // Listen on contentBrowser for sidebar permission changes
    this.#contentBrowser.addEventListener(
      "PermissionStateChange",
      this.#onPermissionStateChanged,
      true
    );
  }

  /**
   * Update permission icons based on current permission state.
   * Supporting only microphone for now
   */
  updatePermissionIcons() {
    if (!this.#contentBrowser) {
      return;
    }

    let permissions = lazy.SitePermissions.getAllForBrowser(
      this.#contentBrowser
    );

    // If no permissions, clear UI
    if (!permissions.length) {
      this.#sidebarPermissionUI.clearUI();
      return;
    }

    // Filter permission matching type (e.g. "microphone" or "microphone^DeviceName")
    const matchingMicPermissions = permissions.filter(
      p => p.id === "microphone" || p.id.startsWith("microphone^")
    );

    // Check if any matching permission is blocked
    if (
      matchingMicPermissions.some(p => p.state === lazy.SitePermissions.BLOCK)
    ) {
      this.#sidebarPermissionUI.showBlockedUI(true);
      return;
    }

    // Check if any matching permission is allowed
    if (
      matchingMicPermissions.some(p => p.state === lazy.SitePermissions.ALLOW)
    ) {
      this.#sidebarPermissionUI.showGrantedUI("microphone");
      return;
    }

    this.#sidebarPermissionUI.clearUI();
  }

  showMicRequestUI() {
    this.#sidebarPermissionUI.showMicRequestUI();
  }

  /**
   * Show the granted permission UI indicator.
   *
   * @param {string} type - Permission type (e.g. "microphone")
   */
  showGrantedUI(type) {
    this.#sidebarPermissionUI.showGrantedUI(type);
  }

  /**
   * Update sharing icon separately from browser-sitePermissionPanel.
   *
   * @param {object} sharingState - Browser sharing state object
   */
  updateFromBrowserState(sharingState) {
    this.#sidebarPermissionUI?.updateFromBrowserState(sharingState);
  }
}
