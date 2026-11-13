/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/* eslint-disable no-use-before-define */
const lazy = {};

ChromeUtils.defineESModuleGetters(lazy, {
  ASRouter: "resource:///modules/asrouter/ASRouter.sys.mjs",
  PrivateBrowsingUtils: "resource://gre/modules/PrivateBrowsingUtils.sys.mjs",
  RemoteL10n: "resource:///modules/asrouter/RemoteL10n.sys.mjs",
  SpecialMessageActions:
    "resource://messaging-system/lib/SpecialMessageActions.sys.mjs",
});

const TYPES = {
  UNIVERSAL: "universal",
  GLOBAL: "global",
};

const FTL_FILES = [
  "browser/newtab/asrouter.ftl",
  "browser/defaultBrowserNotification.ftl",
  "browser/policy-messages.ftl",
  "browser/profiles.ftl",
  "browser/termsofuse.ftl",
];

class InfoBarNotification {
  constructor(message, dispatch) {
    this._dispatch = dispatch;
    this.dispatchUserAction = this.dispatchUserAction.bind(this);
    this.buttonCallback = this.buttonCallback.bind(this);
    this.infobarCallback = this.infobarCallback.bind(this);
    this.message = message;
    this.notification = null;
    const dismissPrefConfig = message?.content?.dismissOnPrefChange;
    // If set, these are the prefs to watch for changes to auto-dismiss the infobar.
    if (Array.isArray(dismissPrefConfig)) {
      this._dismissPrefs = dismissPrefConfig;
    } else if (dismissPrefConfig) {
      this._dismissPrefs = [dismissPrefConfig];
    } else {
      this._dismissPrefs = [];
    }
    this._prefObserver = null;
  }

  /**
   * Ensure a hidden container of <a data-l10n-name> templates exists, and
   * inject the request links using hrefs from message.content.linkUrls.
   */
  _ensureLinkTemplatesFor(doc, names) {
    let container = doc.getElementById("infobar-link-templates");
    // We inject a hidden <div> of <a data-l10n-name> templates into the
    // document because Fluent’s DOM-overlay scans the page for those
    // placeholders.
    if (!container) {
      container = doc.createElement("div");
      container.id = "infobar-link-templates";
      container.hidden = true;
      doc.body.appendChild(container);
    }

    const linkUrls = this.message.content.linkUrls || {};
    for (let name of names) {
      if (!container.querySelector(`a[data-l10n-name="${name}"]`)) {
        const a = doc.createElement("a");
        a.dataset.l10nName = name;
        a.href = linkUrls[name];
        container.appendChild(a);
      }
    }
  }

  /**
   * Async helper to render a Fluent string. If the translation contains `<a
   * data-l10n-name>`, it will parse and inject the associated link contained
   * in the message.
   * text: the message's text object, including at least a string_id field
   * attributes: Fluent arguments to be used in substitutions in the string specified by the string_id
   */
  async _buildMessageFragment(doc, browser, text, attributes) {
    // Get the raw HTML translation
    const html = await lazy.RemoteL10n.formatLocalizableText(text, attributes);

    // If no inline anchors, just return a span
    if (!html.includes('data-l10n-name="')) {
      return lazy.RemoteL10n.createElement(doc, "span", {
        content: text,
        attributes,
      });
    }

    // Otherwise parse it and set up a fragment
    const temp = new DOMParser().parseFromString(html, "text/html").body;
    const frag = doc.createDocumentFragment();

    // Prepare <a data-l10n-name> templates
    const names = [...temp.querySelectorAll("a[data-l10n-name]")].map(
      a => a.dataset.l10nName
    );
    this._ensureLinkTemplatesFor(doc, names);

    // Import each node and wire up any anchors it contains
    for (const node of temp.childNodes) {
      // Nodes from DOMParser belong to a different document, so importNode()
      // clones them into our target doc
      const importedNode = doc.importNode(node, true);

      if (importedNode.nodeType === Node.ELEMENT_NODE) {
        // collect this node if it's an anchor, and all child anchors
        const anchors = [];
        if (importedNode.matches("a[data-l10n-name]")) {
          anchors.push(importedNode);
        }
        anchors.push(...importedNode.querySelectorAll("a[data-l10n-name]"));

        const linkActions = this.message.content.linkActions || {};

        for (const a of anchors) {
          const name = a.dataset.l10nName;
          const template = doc
            .getElementById("infobar-link-templates")
            .querySelector(`a[data-l10n-name="${name}"]`);
          if (!template) {
            continue;
          }
          a.href = template.href;
          a.addEventListener("click", e => {
            e.preventDefault();
            // Open link URL
            try {
              lazy.SpecialMessageActions.handleAction(
                {
                  type: "OPEN_URL",
                  data: { args: a.href, where: text.args?.where || "tab" },
                },
                browser
              );
            } catch (err) {
              console.error(`Error handling OPEN_URL action:`, err);
            }
            // Then fire the defined actions for that link, if applicable
            if (linkActions[name]) {
              try {
                lazy.SpecialMessageActions.handleAction(
                  linkActions[name],
                  browser
                );
              } catch (err) {
                console.error(
                  `Error handling ${linkActions[name]} action:`,
                  err
                );
              }
              if (linkActions[name].dismiss) {
                this.notification?.dismiss();
              }
            }
          });
        }
      }

      frag.appendChild(importedNode);
    }

    return frag;
  }

  /**
   * Displays the infobar notification in the specified browser and sends an impression ping.
   * Formats the message and buttons, and appends the notification.
   * For universal infobars, only records an impression for the first instance.
   *
   * @param {object} browser - The browser reference for the currently selected tab.
   */
  async showNotification(browser) {
    let { content } = this.message;
    let { gBrowser } = browser.documentGlobal;
    let doc = gBrowser.ownerDocument;
    let notificationContainer;
    if ([TYPES.GLOBAL, TYPES.UNIVERSAL].includes(content.type)) {
      notificationContainer = browser.documentGlobal.gNotificationBox;
    } else {
      notificationContainer = gBrowser.getNotificationBox(browser);
    }

    let priority = content.priority || notificationContainer.PRIORITY_SYSTEM;

    let labelNode = await this.formatMessageConfig(
      doc,
      browser,
      content.text,
      content.attributes
    );

    this.notification = await notificationContainer.appendNotification(
      this.message.id,
      {
        image: content.icon || "chrome://branding/content/icon64.png",
        priority,
        eventCallback: this.infobarCallback,
        style: content.style || {},
      },
      content.buttons.map(b => this.formatButtonConfig(b)),
      true, // Disables clickjacking protections
      content.dismissable
    );

    // Slot into light DOM so global-shared link rules reach inline anchors
    const messageSlot = doc.createElement("span");
    messageSlot.setAttribute("slot", "message");
    messageSlot.appendChild(labelNode);
    this.notification.appendChild(messageSlot);
    // If the infobar is universal, only record an impression for the first
    // instance.
    if (
      content.type !== TYPES.UNIVERSAL ||
      !InfoBar._universalInfobars.length
    ) {
      this.addImpression(browser);
    }

    // Only add if the universal infobar is still active. Prevents race condition
    // where a notification could add itself after removeUniversalInfobars().
    if (
      content.type === TYPES.UNIVERSAL &&
      InfoBar._activeInfobar?.message?.id === this.message.id
    ) {
      InfoBar._universalInfobars.push({
        box: notificationContainer,
        notification: this.notification,
      });
    }

    // After the notification exists, attach a pref observer if applicable.
    this._maybeAttachPrefObserver();
  }

  /**
   * Create a clickable anchor node
   * attributes: Fluent arguments to be used in substitutions in the string specified by the string_id
   */
  _createLinkNode(
    doc,
    browser,
    { href, where = "tab", string_id, raw },
    attributes
  ) {
    const a = doc.createElement("a");
    a.href = href;
    a.addEventListener("click", e => {
      e.preventDefault();
      lazy.SpecialMessageActions.handleAction(
        { type: "OPEN_URL", data: { args: a.href, where } },
        browser
      );
    });

    if (string_id) {
      // wrap a localized span inside
      const span = lazy.RemoteL10n.createElement(doc, "span", {
        content: { string_id },
        attributes,
      });
      a.appendChild(span);
    } else {
      a.textContent = raw || "";
    }

    return a;
  }

  /**
   * format a message that may include localizable text
   * text: the text object of the message. If it is localizable, inlcudes a string_id field
   * attributes: Fluent arguments to be used in substitutions in the string specified by the string_id
   */
  async formatMessageConfig(doc, browser, text, attributes) {
    const frag = doc.createDocumentFragment();
    const parts = Array.isArray(text) ? text : [text];
    for (const part of parts) {
      if (!part) {
        continue;
      }
      if (part.href) {
        frag.appendChild(this._createLinkNode(doc, browser, part, attributes));
        continue;
      }

      if (part.string_id) {
        const subFrag = await this._buildMessageFragment(
          doc,
          browser,
          {
            string_id: part.string_id,
            args: part.args,
          },
          attributes
        );
        frag.appendChild(subFrag);
        continue;
      }

      if (typeof part === "string") {
        frag.appendChild(doc.createTextNode(part));
        continue;
      }

      if (part.raw && typeof part.raw === "string") {
        frag.appendChild(doc.createTextNode(part.raw));
      }
    }

    return frag;
  }

  formatButtonConfig(button) {
    let btnConfig = { callback: this.buttonCallback, ...button };
    // notificationbox will set correct data-l10n-id attributes if passed in
    // using the l10n-id key. Otherwise the `button.label` text is used.
    if (button.label.string_id) {
      btnConfig["l10n-id"] = button.label.string_id;
    }

    return btnConfig;
  }

  handleImpressionAction(browser) {
    const ALLOWED_IMPRESSION_ACTIONS = ["SET_PREF"];
    const impressionAction = this.message.content.impression_action;
    const actions =
      impressionAction.type === "MULTI_ACTION"
        ? impressionAction.data.actions
        : [impressionAction];

    actions.forEach(({ type, data, once }) => {
      if (!ALLOWED_IMPRESSION_ACTIONS.includes(type)) {
        return;
      }

      let { messageImpressions } = lazy.ASRouter.state;
      // If we only want to perform the action on first impression, ensure no
      // impressions exist for this message.
      if (once && messageImpressions[this.message.id]?.length) {
        return;
      }

      data.onImpression = true;
      try {
        lazy.SpecialMessageActions.handleAction({ type, data }, browser);
      } catch (err) {
        console.error(`Error handling ${type} impression action:`, err);
      }
    });
  }

  addImpression(browser) {
    // If the message has an impression action, handle it before dispatching the
    // impression. `this._dispatch` may be async and we want to ensure we have a
    // consistent impression count when handling impression actions that should
    // only occur once.
    if (this.message.content.impression_action) {
      this.handleImpressionAction(browser);
    }
    // Record an impression in ASRouter for frequency capping
    this._dispatch({ type: "IMPRESSION", data: this.message });
    // Send a user impression telemetry ping
    this.sendUserEventTelemetry("IMPRESSION");
  }

  /**
   * Callback fired when a button in the infobar is clicked.
   *
   * @param {Element} notificationBox - The `<notification-message>` element representing the infobar.
   * @param {object} btnDescription - An object describing the button, includes the label, the action with an optional dismiss property, and primary button styling.
   * @param {Element} target - The <button> DOM element that was clicked.
   * @returns {boolean} `true` to keep the infobar open, `false` to dismiss it.
   */
  buttonCallback(notificationBox, btnDescription, target) {
    this.dispatchUserAction(
      btnDescription.action,
      target.documentGlobal.gBrowser.selectedBrowser
    );
    let isPrimary = target.classList.contains("primary");
    let eventName = isPrimary
      ? "CLICK_PRIMARY_BUTTON"
      : "CLICK_SECONDARY_BUTTON";
    this.sendUserEventTelemetry(eventName);

    // Prevents infobar dismissal when dismiss is explicitly set to `false`
    return btnDescription.action?.dismiss === false;
  }

  dispatchUserAction(action, selectedBrowser) {
    this._dispatch({ type: "USER_ACTION", data: action }, selectedBrowser);
  }

  /**
   * Handles infobar events triggered by the notification interactions (excluding button clicks).
   * Cleans up the notification and active infobar state when the infobar is removed or dismissed.
   * If the removed infobar is universal, ensures all universal infobars and related observers are also removed.
   *
   * @param {string} eventType - The type of event (e.g., "removed").
   */
  infobarCallback(eventType) {
    // Clean up the pref observer on any removal/dismissal path.
    this._removePrefObserver();
    const wasUniversal = this.message.content.type === TYPES.UNIVERSAL;
    const isActiveMessage =
      InfoBar._activeInfobar?.message?.id === this.message.id;
    if (eventType === "removed") {
      this.notification = null;
      if (isActiveMessage) {
        InfoBar._activeInfobar = null;
      }
    } else if (this.notification) {
      this.sendUserEventTelemetry("DISMISSED");
      this.notification = null;

      if (isActiveMessage) {
        InfoBar._activeInfobar = null;
      }
    }
    // If one instance of universal infobar is removed, remove all instances and
    // the new window observer
    if (wasUniversal && isActiveMessage && InfoBar._universalInfobars.length) {
      this.removeUniversalInfobars();
    }
  }

  /**
   * If content.dismissOnPrefChange is set (string or array), observe those
   * pref(s) and dismiss the infobar whenever any of them changes (including
   * when it is set for the first time).
   */
  _maybeAttachPrefObserver() {
    if (!this._dismissPrefs?.length || this._prefObserver) {
      return;
    }
    // Weak observer to avoid leaks.
    this._prefObserver = {
      QueryInterface: ChromeUtils.generateQI([
        "nsIObserver",
        "nsISupportsWeakReference",
      ]),
      observe: (subject, topic, data) => {
        if (topic === "nsPref:changed" && this._dismissPrefs.includes(data)) {
          try {
            this.notification?.dismiss();
          } catch (e) {
            console.error("Failed to dismiss infobar on pref change:", e);
          }
        }
      },
    };
    try {
      // Register each pref with a weak observer and ignore per-pref failures.
      for (const pref of this._dismissPrefs) {
        try {
          Services.prefs.addObserver(pref, this._prefObserver, true);
        } catch (_) {}
      }
    } catch (e) {
      console.error(
        "Failed to add prefs observer(s) for dismissOnPrefChange:",
        e
      );
    }
  }

  _removePrefObserver() {
    if (!this._dismissPrefs?.length || !this._prefObserver) {
      return;
    }
    for (const pref of this._dismissPrefs) {
      try {
        Services.prefs.removeObserver(pref, this._prefObserver);
      } catch (_) {
        // Ignore as the observer might already be removed during shutdown/teardown.
      }
    }
    this._prefObserver = null;
  }

  /**
   * Removes all active universal infobars from each window.
   * Unregisters the observer for new windows, clears the tracking array, and resets the
   * active infobar state.
   */
  removeUniversalInfobars() {
    // Remove the new window observer
    if (InfoBar._observingWindowOpened) {
      InfoBar._observingWindowOpened = false;
      Services.obs.removeObserver(InfoBar, "domwindowopened");
    }
    // Remove the universal infobar
    InfoBar._universalInfobars.forEach(({ box, notification }) => {
      try {
        if (box && notification) {
          box.removeNotification(notification);
        }
      } catch (error) {
        console.error("Failed to remove notification: ", error);
      }
    });
    InfoBar._universalInfobars = [];

    if (InfoBar._activeInfobar?.message.content.type === TYPES.UNIVERSAL) {
      InfoBar._activeInfobar = null;
    }
  }

  sendUserEventTelemetry(event) {
    const ping = {
      message_id: this.message.id,
      event,
    };
    this._dispatch({
      type: "INFOBAR_TELEMETRY",
      data: { action: "infobar_user_event", ...ping },
    });
  }
}

export const InfoBar = {
  _activeInfobar: null,
  _universalInfobars: [],
  _observingWindowOpened: false,

  maybeLoadCustomElement(win) {
    if (!win.customElements.get("remote-text")) {
      Services.scriptloader.loadSubScript(
        "chrome://browser/content/asrouter/components/remote-text.js",
        win
      );
    }
  },

  maybeInsertFTL(win) {
    FTL_FILES.forEach(path => win.MozXULElement.insertFTLIfNeeded(path));
  },

  /**
   * Helper to check the window's state and whether it's a
   * private browsing window, a popup or a taskbar tab.
   *
   * @returns {boolean} `true` if the window is valid for showing an infobar.
   */
  isValidInfobarWindow(win) {
    if (!win || win.closed) {
      return false;
    }
    if (lazy.PrivateBrowsingUtils.isWindowPrivate(win)) {
      return false;
    }
    if (!win.toolbar?.visible) {
      // Popups don't have a visible toolbar
      return false;
    }
    if (win.document.documentElement.hasAttribute("taskbartab")) {
      return false;
    }
    return true;
  },

  /**
   * Displays the universal infobar in all open, fully loaded browser windows.
   *
   * @param {InfoBarNotification} notification - The notification instance to display.
   */
  async showNotificationAllWindows(notification) {
    for (let win of Services.wm.getEnumerator("navigator:browser")) {
      if (
        !win.gBrowser ||
        win.document?.readyState !== "complete" ||
        !this.isValidInfobarWindow(win)
      ) {
        continue;
      }
      this.maybeLoadCustomElement(win);
      this.maybeInsertFTL(win);
      const browser = win.gBrowser.selectedBrowser;
      await notification.showNotification(browser);
    }
  },

  _maybeReplaceActiveInfoBar(nextMessage) {
    if (!this._activeInfobar) {
      return false;
    }
    const replacementEligible = nextMessage?.content?.canReplace || [];
    const activeId = this._activeInfobar.message?.id;
    if (!replacementEligible.includes(activeId)) {
      return false;
    }
    const activeType = this._activeInfobar.message?.content?.type;
    if (activeType === TYPES.UNIVERSAL) {
      this._activeInfobar.notification?.removeUniversalInfobars();
    } else {
      try {
        this._activeInfobar.notification?.notification.dismiss();
      } catch (e) {
        console.error("Failed to dismiss active infobar:", e);
      }
    }
    this._activeInfobar = null;
    return true;
  },

  /**
   * Displays an infobar notification in the specified browser window.
   * For the first universal infobar, shows the notification in all open browser windows
   * and sets up an observer to handle new windows.
   * For non-universal, displays the notification only in the given window.
   *
   * @param {object} browser - The browser reference for the currently selected tab.
   * @param {object} message - The message object describing the infobar content.
   * @param {function} dispatch - The dispatch function for actions.
   * @param {boolean} universalInNewWin - `True` if this is a universal infobar for a new window.
   * @returns {Promise<InfoBarNotification|null>} The notification instance, or null if not shown.
   */
  async showInfoBarMessage(browser, message, dispatch, universalInNewWin) {
    const win = browser?.documentGlobal;
    if (!this.isValidInfobarWindow(win)) {
      return null;
    }
    const isUniversal = message.content.type === TYPES.UNIVERSAL;
    // Check if this is the first instance of a universal infobar
    const isFirstUniversal = !universalInNewWin && isUniversal;
    // Prevent stacking multiple infobars
    if (this._activeInfobar && !universalInNewWin) {
      // Check if infobar is configured to replace the current infobar.
      if (!this._maybeReplaceActiveInfoBar(message)) {
        return null;
      }
    }

    this.maybeLoadCustomElement(win);
    this.maybeInsertFTL(win);

    let notification = new InfoBarNotification(message, dispatch);

    if (!universalInNewWin) {
      this._activeInfobar = { message, dispatch, notification };
    }

    if (isFirstUniversal) {
      await this.showNotificationAllWindows(notification);
      if (!this._observingWindowOpened) {
        this._observingWindowOpened = true;
        Services.obs.addObserver(this, "domwindowopened");
      } else {
        // TODO: At least during testing it seems that we can get here more
        // than once without passing through removeUniversalInfobars(). Is
        // this expected?
        console.warn(
          "InfoBar: Already observing new windows for universal infobar."
        );
      }
    } else {
      await notification.showNotification(browser);
    }

    if (!universalInNewWin) {
      this._activeInfobar = { message, dispatch, notification };
      // If the window closes before the user interacts with the active infobar,
      // clear it
      win.addEventListener(
        "unload",
        () => {
          // Remove this window’s stale entry
          InfoBar._universalInfobars = InfoBar._universalInfobars.filter(
            ({ box }) => box.documentGlobal !== win
          );

          if (isUniversal) {
            // If there’s still at least one live universal infobar,
            // make it the active infobar; otherwise clear the active infobar
            const nextEntry = InfoBar._universalInfobars.find(
              ({ box }) => !box.documentGlobal?.closed
            );
            const nextNotification = nextEntry?.notification;
            InfoBar._activeInfobar = nextNotification
              ? { message, dispatch, nextNotification }
              : null;
          } else {
            // Non-universal always clears on unload
            InfoBar._activeInfobar = null;
          }
        },
        { once: true }
      );
    }

    return notification;
  },

  /**
   * Observer callback fired when a new window is opened.
   * If the topic is "domwindowopened" and the window is a valid target,
   * the universal infobar will be shown in the new window once loaded.
   *
   * @param {Window} aSubject - The newly opened window.
   * @param {string} aTopic - The topic of the observer notification.
   */
  observe(aSubject, aTopic) {
    if (aTopic !== "domwindowopened") {
      return;
    }
    const win = aSubject;

    if (!this.isValidInfobarWindow(win)) {
      return;
    }

    const { message, dispatch } = this._activeInfobar || {};
    if (!message || message.content.type !== TYPES.UNIVERSAL) {
      return;
    }

    const onWindowReady = () => {
      if (!win.gBrowser || win.closed) {
        return;
      }
      if (
        !InfoBar._activeInfobar ||
        InfoBar._activeInfobar.message !== message
      ) {
        return;
      }
      this.showInfoBarMessage(
        win.gBrowser.selectedBrowser,
        message,
        dispatch,
        true
      );
    };

    win.addEventListener("load", onWindowReady, { once: true });
  },
};
