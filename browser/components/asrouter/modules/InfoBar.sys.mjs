/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/* eslint-disable no-use-before-define */
const lazy = {};

ChromeUtils.defineESModuleGetters(lazy, {
  PrivateBrowsingUtils: "resource://gre/modules/PrivateBrowsingUtils.sys.mjs",
  RemoteL10n: "resource:///modules/asrouter/RemoteL10n.sys.mjs",
  SpecialMessageActions:
    "resource://messaging-system/lib/SpecialMessageActions.sys.mjs",
});

const TYPES = {
  UNIVERSAL: "universal",
  GLOBAL: "global",
};

class InfoBarNotification {
  constructor(message, dispatch) {
    this._dispatch = dispatch;
    this.dispatchUserAction = this.dispatchUserAction.bind(this);
    this.buttonCallback = this.buttonCallback.bind(this);
    this.infobarCallback = this.infobarCallback.bind(this);
    this.message = message;
    this.notification = null;
  }

  /**
   * Show the infobar notification and send an impression ping
   *
   * @param {object} browser Browser reference for the currently selected tab
   */
  async showNotification(browser) {
    let { content } = this.message;
    let { gBrowser } = browser.ownerGlobal;
    let doc = gBrowser.ownerDocument;
    let notificationContainer;
    if ([TYPES.GLOBAL, TYPES.UNIVERSAL].includes(content.type)) {
      notificationContainer = browser.ownerGlobal.gNotificationBox;
    } else {
      notificationContainer = gBrowser.getNotificationBox(browser);
    }

    let priority = content.priority || notificationContainer.PRIORITY_SYSTEM;

    this.notification = await notificationContainer.appendNotification(
      this.message.id,
      {
        label: this.formatMessageConfig(doc, browser, content.text),
        image: content.icon || "chrome://branding/content/icon64.png",
        priority,
        eventCallback: this.infobarCallback,
      },
      content.buttons.map(b => this.formatButtonConfig(b)),
      false,
      content.dismissable
    );
    // If InfoBar is universal, only record an impression for the first
    // instance.
    if (
      content.type !== TYPES.UNIVERSAL ||
      !InfoBar._universalInfobars.length
    ) {
      this.addImpression();
    }

    if (content.type === TYPES.UNIVERSAL) {
      InfoBar._universalInfobars.push({
        box: browser.ownerGlobal.gNotificationBox,
        notification: this.notification,
      });
    }
  }

  formatMessageConfig(doc, browser, content) {
    const frag = doc.createDocumentFragment();
    const parts = Array.isArray(content) ? content : [content];
    for (const part of parts) {
      let node;
      if (typeof part === "string") {
        node = doc.createTextNode(part);
        // Handle embedded link
      } else if (part.href) {
        const a = doc.createElement("a");
        a.href = part.href;
        a.addEventListener("click", e => {
          e.preventDefault();
          lazy.SpecialMessageActions.handleAction(
            { type: "OPEN_URL", data: { args: a.href, where: part.where } },
            browser
          );
        });

        if (part.string_id) {
          const l10n = lazy.RemoteL10n.createElement(doc, "span", {
            content: {
              string_id: part.string_id,
              ...(part.args && { args: part.args }),
            },
          });
          a.appendChild(l10n);
        } else {
          a.textContent = part.raw || "";
        }
        node = a;
      } else if (part.string_id) {
        node = lazy.RemoteL10n.createElement(doc, "span", {
          content: {
            string_id: part.string_id,
            ...(part.args && { args: part.args }),
          },
        });
      } else {
        const text = part.raw !== null ? part.raw : String(part);
        node = doc.createTextNode(text);
      }

      frag.appendChild(node);
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

  addImpression() {
    // Record an impression in ASRouter for frequency capping
    this._dispatch({ type: "IMPRESSION", data: this.message });
    // Send a user impression telemetry ping
    this.sendUserEventTelemetry("IMPRESSION");
  }

  /**
   * Called when one of the infobar buttons is clicked
   */
  buttonCallback(notificationBox, btnDescription, target) {
    this.dispatchUserAction(
      btnDescription.action,
      target.ownerGlobal.gBrowser.selectedBrowser
    );
    let isPrimary = target.classList.contains("primary");
    let eventName = isPrimary
      ? "CLICK_PRIMARY_BUTTON"
      : "CLICK_SECONDARY_BUTTON";
    this.sendUserEventTelemetry(eventName);
  }

  dispatchUserAction(action, selectedBrowser) {
    this._dispatch({ type: "USER_ACTION", data: action }, selectedBrowser);
  }

  /**
   * Called when interacting with the toolbar (but not through the buttons)
   */
  infobarCallback(eventType) {
    const wasUniversal =
      InfoBar._activeInfobar?.message.content.type === TYPES.UNIVERSAL;
    if (eventType === "removed") {
      this.notification = null;
      InfoBar._activeInfobar = null;
    } else if (this.notification) {
      this.sendUserEventTelemetry("DISMISSED");
      this.notification = null;
      InfoBar._activeInfobar = null;
    }
    // If one instance of universal infobar is removed, remove all instances and
    // the new window observer
    if (wasUniversal) {
      this.removeUniversalInfobars();
    }
  }

  removeUniversalInfobars() {
    try {
      Services.obs.removeObserver(InfoBar, "domwindowopened");
    } catch (error) {
      console.error(
        "Error removing domwindowopened observer on InfoBar:",
        error
      );
    }
    InfoBar._universalInfobars.forEach(({ box, notification }) => {
      box.removeNotification(notification);
    });
    InfoBar._universalInfobars = [];
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

  maybeLoadCustomElement(win) {
    if (!win.customElements.get("remote-text")) {
      Services.scriptloader.loadSubScript(
        "chrome://browser/content/asrouter/components/remote-text.js",
        win
      );
    }
  },

  maybeInsertFTL(win) {
    win.MozXULElement.insertFTLIfNeeded("browser/newtab/asrouter.ftl");
    win.MozXULElement.insertFTLIfNeeded(
      "browser/defaultBrowserNotification.ftl"
    );
  },

  async showNotificationAllWindows(notification) {
    for (let win of Services.wm.getEnumerator(null)) {
      const browser = win.gBrowser.selectedBrowser;
      await notification.showNotification(browser);
    }
  },

  async showInfoBarMessage(browser, message, dispatch, universalInNewWin) {
    // Prevent stacking multiple infobars
    if (this._activeInfobar && !universalInNewWin) {
      return null;
    }

    // Check if this is the first instance of a universal infobar
    const isFirstUniversal =
      !universalInNewWin && message.content.type === TYPES.UNIVERSAL;
    const win = browser?.ownerGlobal;

    if (!win || lazy.PrivateBrowsingUtils.isWindowPrivate(win)) {
      return null;
    }

    this.maybeLoadCustomElement(win);
    this.maybeInsertFTL(win);

    let notification = new InfoBarNotification(message, dispatch);
    if (isFirstUniversal) {
      await this.showNotificationAllWindows(notification);
      Services.obs.addObserver(this, "domwindowopened");
    } else {
      await notification.showNotification(browser);
    }
    if (!universalInNewWin) {
      this._activeInfobar = { message, dispatch };
    }

    return notification;
  },

  observe(aSubject, aTopic) {
    const { message, dispatch } = this._activeInfobar;
    if (
      aTopic !== "domwindowopened" ||
      message?.content.type !== TYPES.UNIVERSAL
    ) {
      return;
    }
    if (aSubject.document.readyState === "complete") {
      let browser = aSubject.gBrowser.selectedBrowser;
      this.showInfoBarMessage(browser, message, dispatch, true);
    } else {
      aSubject.addEventListener(
        "load",
        () => {
          let browser = aSubject.gBrowser.selectedBrowser;
          this.showInfoBarMessage(browser, message, dispatch, true);
        },
        { once: true }
      );
    }
  },
};
