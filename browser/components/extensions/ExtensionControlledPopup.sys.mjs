/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/**
 * @file
 * This module exports a class that can be used to handle displaying a popup
 * doorhanger with a primary action to not show a popup for this extension again
 * and a secondary action disables the addon, or brings the user to their settings.
 *
 * The original purpose of the popup was to notify users of an extension that has
 * changed the New Tab or homepage. Users would see this popup the first time they
 * view those pages after a change to the setting in each session until they confirm
 * the change by triggering the primary action.
 */

import { ExtensionCommon } from "resource://gre/modules/ExtensionCommon.sys.mjs";

const lazy = {};

ChromeUtils.defineESModuleGetters(lazy, {
  AddonManager: "resource://gre/modules/AddonManager.sys.mjs",
  BrowserUIUtils: "resource:///modules/BrowserUIUtils.sys.mjs",
  CustomizableUI:
    "moz-src:///browser/components/customizableui/CustomizableUI.sys.mjs",
  ExtensionSettingsStore:
    "resource://gre/modules/ExtensionSettingsStore.sys.mjs",
  PrivateBrowsingUtils: "resource://gre/modules/PrivateBrowsingUtils.sys.mjs",
});

let { makeWidgetId } = ExtensionCommon;

ChromeUtils.defineLazyGetter(lazy, "strBundle", function () {
  return Services.strings.createBundle(
    "chrome://global/locale/extensions.properties"
  );
});

const PREF_BRANCH_INSTALLED_ADDON = "extensions.installedDistroAddon.";

ChromeUtils.defineLazyGetter(lazy, "distributionAddonsList", function () {
  let addonList = Services.prefs
    .getChildList(PREF_BRANCH_INSTALLED_ADDON)
    .map(id => id.replace(PREF_BRANCH_INSTALLED_ADDON, ""));
  return new Set(addonList);
});

export class ExtensionControlledPopup {
  /**
   * Provide necessary options for the popup.
   *
   * @param {object} opts Options for configuring popup.
   * @param {string} opts.confirmedType
   *                 The type to use for storing a user's confirmation in
   *                 ExtensionSettingsStore.
   * @param {string} opts.observerTopic
   *                 An observer topic to trigger the popup on with Services.obs. If the
   *                 doorhanger should appear on a specific window include it as the
   *                 subject in the observer event.
   * @param {string} opts.popupnotificationId
   *                 The id for the popupnotification element in the markup. This
   *                 element should be defined in panelUI.inc.xhtml.
   * @param {string} opts.settingType
   *                 The setting type to check in ExtensionSettingsStore to retrieve
   *                 the controlling extension.
   * @param {string} opts.settingKey
   *                 The setting key to check in ExtensionSettingsStore to retrieve
   *                 the controlling extension.
   * @param {string} opts.descriptionId
   *                 The id of the element where the description should be displayed.
   * @param {string} opts.descriptionMessageId
   *                 The message id to be used for the description. The translated
   *                 string will have the add-on's name and icon injected into it.
   * @param {string} opts.getLocalizedDescription
   *                 A function to get the localized message string. This
   *                 function is passed doc, message and addonDetails (the
   *                 add-on's icon and name). If not provided, then the add-on's
   *                 icon and name are added to the description.
   * @param {string} opts.learnMoreLink
   *                 The name of the SUMO page to link to, this is added to
   *                 app.support.baseURL.
   * @param {string} [opts.preferencesLocation]
   *                 If included, the name of the preferences tab that will be opened
   *                 by the secondary action. If not included, the secondary option will
   *                 disable the addon.
   * @param {string} [opts.preferencesEntrypoint]
   *                 The entrypoint to pass to preferences telemetry.
   * @param {Function} opts.onObserverAdded
   *                   A callback that is triggered when an observer is registered to
   *                   trigger the popup on the next observerTopic.
   * @param {Function} opts.onObserverRemoved
   *                   A callback that is triggered when the observer is removed,
   *                   either because the popup is opening or it was explicitly
   *                   cancelled by calling removeObserver.
   * @param {Function} opts.beforeDisableAddon
   *                   A function that is called before disabling an extension when the
   *                   user decides to disable the extension. If this function is async
   *                   then the extension won't be disabled until it is fulfilled.
   *                   This function gets two arguments, the ExtensionControlledPopup
   *                   instance for the panel and the window that the popup appears on.
   */
  constructor(opts) {
    this.confirmedType = opts.confirmedType;
    this.observerTopic = opts.observerTopic;
    this.popupnotificationId = opts.popupnotificationId;
    this.settingType = opts.settingType;
    this.settingKey = opts.settingKey;
    this.descriptionId = opts.descriptionId;
    this.descriptionMessageId = opts.descriptionMessageId;
    this.getLocalizedDescription = opts.getLocalizedDescription;
    this.learnMoreLink = opts.learnMoreLink;
    this.preferencesLocation = opts.preferencesLocation;
    this.preferencesEntrypoint = opts.preferencesEntrypoint;
    this.onObserverAdded = opts.onObserverAdded;
    this.onObserverRemoved = opts.onObserverRemoved;
    this.beforeDisableAddon = opts.beforeDisableAddon;
    this.observerRegistered = false;
  }

  get topWindow() {
    return Services.wm.getMostRecentWindow("navigator:browser");
  }

  userHasConfirmed(id) {
    // We don't show a doorhanger for distribution installed add-ons.
    if (lazy.distributionAddonsList.has(id)) {
      return true;
    }
    // Without a preferencesLocation the only action we can offer is disabling
    // the add-on, which enterprise policy may forbid.
    if (
      !this.preferencesLocation &&
      Services.policies &&
      !Services.policies.isAllowed(`disable-extension:${id}`)
    ) {
      return true;
    }
    let setting = lazy.ExtensionSettingsStore.getSetting(
      this.confirmedType,
      id
    );
    return !!(setting && setting.value);
  }

  async setConfirmation(id) {
    await lazy.ExtensionSettingsStore.initialize();
    return lazy.ExtensionSettingsStore.addSetting(
      id,
      this.confirmedType,
      id,
      true,
      () => false
    );
  }

  async clearConfirmation(id) {
    await lazy.ExtensionSettingsStore.initialize();
    return lazy.ExtensionSettingsStore.removeSetting(
      id,
      this.confirmedType,
      id
    );
  }

  observe(subject) {
    // Remove the observer here so we don't get multiple open() calls if we get
    // multiple observer events in quick succession.
    this.removeObserver();

    let targetWindow;
    // Some notifications (e.g. browser-open-newtab-start) do not have a window subject.
    if (subject && subject.document) {
      targetWindow = subject;
    }

    // Do this work in an idle callback to avoid interfering with new tab performance tracking.
    this.topWindow.requestIdleCallback(() => this.open(targetWindow));
  }

  removeObserver() {
    if (this.observerRegistered) {
      Services.obs.removeObserver(this, this.observerTopic);
      this.observerRegistered = false;
      if (this.onObserverRemoved) {
        this.onObserverRemoved();
      }
    }
  }

  async addObserver(extensionId) {
    await lazy.ExtensionSettingsStore.initialize();

    if (!this.observerRegistered && !this.userHasConfirmed(extensionId)) {
      Services.obs.addObserver(this, this.observerTopic);
      this.observerRegistered = true;
      if (this.onObserverAdded) {
        this.onObserverAdded();
      }
    }
  }

  // The extensionId will be looked up in ExtensionSettingsStore if it is not
  // provided using this.settingType and this.settingKey.
  async open(targetWindow, extensionId) {
    await lazy.ExtensionSettingsStore.initialize();

    // Remove the observer since it would open the same dialog again the next time
    // the observer event fires.
    this.removeObserver();

    if (!extensionId) {
      let item = lazy.ExtensionSettingsStore.getSetting(
        this.settingType,
        this.settingKey
      );
      extensionId = item && item.id;
    }

    let win = targetWindow || this.topWindow;
    let isPrivate = lazy.PrivateBrowsingUtils.isWindowPrivate(win);
    if (
      isPrivate &&
      extensionId &&
      !WebExtensionPolicy.getByID(extensionId).privateBrowsingAllowed
    ) {
      return;
    }

    // The item should have an extension and the user shouldn't have confirmed
    // the change here, but just to be sure check that it is still controlled
    // and the user hasn't already confirmed the change.
    // If there is no id, then the extension is no longer in control.
    if (!extensionId || this.userHasConfirmed(extensionId)) {
      return;
    }

    // If the window closes while waiting for focus, this might reject/throw,
    // and we should stop trying to show the popup.
    try {
      await this._ensureWindowReady(win);
    } catch (ex) {
      return;
    }

    // Find the elements we need.
    let doc = win.document;
    let panel = ExtensionControlledPopup._getAndMaybeCreatePanel(doc);
    let popupnotification = doc.getElementById(this.popupnotificationId);
    let urlBarWasFocused = win.gURLBar.focused;

    if (!popupnotification) {
      throw new Error(
        `No popupnotification found for id "${this.popupnotificationId}"`
      );
    }

    let elementsToTranslate = panel.querySelectorAll("[data-lazy-l10n-id]");
    if (elementsToTranslate.length) {
      win.MozXULElement.insertFTLIfNeeded("browser/appMenuNotifications.ftl");
      for (let el of elementsToTranslate) {
        el.setAttribute("data-l10n-id", el.getAttribute("data-lazy-l10n-id"));
        el.removeAttribute("data-lazy-l10n-id");
      }
      await win.document.l10n.translateFragment(panel);
    }
    let addon = await lazy.AddonManager.getAddonByID(extensionId);
    this.populateDescription(doc, addon);

    // Setup the buttoncommand handler.
    let handleButtonCommand = async event => {
      event.preventDefault();
      panel.hidePopup();

      // Main action is to keep changes.
      await this.setConfirmation(extensionId);

      // If the page this is appearing on is the New Tab page then the URL bar may
      // have been focused when the doorhanger stole focus away from it. Once an
      // action is taken the focus state should be restored to what the user was
      // expecting.
      if (urlBarWasFocused) {
        win.gURLBar.focus();
      }
    };
    let handleSecondaryButtonCommand = async event => {
      event.preventDefault();
      panel.hidePopup();

      if (this.preferencesLocation) {
        // Secondary action opens Preferences, if a preferencesLocation option is included.
        let options = this.Entrypoint
          ? { urlParams: { entrypoint: this.Entrypoint } }
          : {};
        win.openPreferences(this.preferencesLocation, options);
      } else {
        // Secondary action is to restore settings.
        if (this.beforeDisableAddon) {
          await this.beforeDisableAddon(this, win);
        }
        await addon.disable();
      }

      if (urlBarWasFocused) {
        win.gURLBar.focus();
      }
    };
    panel.addEventListener("buttoncommand", handleButtonCommand);
    panel.addEventListener(
      "secondarybuttoncommand",
      handleSecondaryButtonCommand
    );
    panel.addEventListener(
      "popuphidden",
      () => {
        popupnotification.hidden = true;
        panel.removeEventListener("buttoncommand", handleButtonCommand);
        panel.removeEventListener(
          "secondarybuttoncommand",
          handleSecondaryButtonCommand
        );
      },
      { once: true }
    );

    // Look for a browserAction on the toolbar.
    let action = lazy.CustomizableUI.getWidget(
      `${makeWidgetId(extensionId)}-browser-action`
    );
    if (action) {
      action =
        action.areaType == "toolbar" &&
        action
          .forWindow(win)
          .node.querySelector(".unified-extensions-item-action-button");
    }

    // Anchor to a toolbar browserAction if found, otherwise use the extensions
    // button.
    const anchor = action || doc.getElementById("unified-extensions-button");

    if (this.learnMoreLink) {
      const learnMoreURL =
        Services.urlFormatter.formatURLPref("app.support.baseURL") +
        this.learnMoreLink;
      popupnotification.setAttribute("learnmoreurl", learnMoreURL);
    } else {
      // In practice this isn't really needed because each of the
      // controlled popups use its own popupnotification instance
      // and they always have an learnMoreURL.
      popupnotification.removeAttribute("learnmoreurl");
    }
    popupnotification.show();
    if (anchor?.id == "unified-extensions-button") {
      const { gUnifiedExtensions } = anchor.documentGlobal;
      gUnifiedExtensions.recordButtonTelemetry("extension_controlled_setting");
      gUnifiedExtensions.ensureButtonShownBeforeAttachingPanel(panel);
    }
    panel.openPopup(anchor);
  }

  getAddonDetails(doc, addon) {
    const defaultIcon = "chrome://mozapps/skin/extensions/extensionGeneric.svg";

    let image = doc.createXULElement("image");
    image.setAttribute("src", addon.iconURL || defaultIcon);
    image.classList.add("extension-controlled-icon");

    let addonDetails = doc.createDocumentFragment();
    addonDetails.appendChild(image);
    addonDetails.appendChild(doc.createTextNode(" " + addon.name));

    return addonDetails;
  }

  populateDescription(doc, addon) {
    let description = doc.getElementById(this.descriptionId);
    description.textContent = "";

    let addonDetails = this.getAddonDetails(doc, addon);
    let message = lazy.strBundle.GetStringFromName(this.descriptionMessageId);
    if (this.getLocalizedDescription) {
      description.appendChild(
        this.getLocalizedDescription(doc, message, addonDetails)
      );
    } else {
      description.appendChild(
        lazy.BrowserUIUtils.getLocalizedFragment(doc, message, addonDetails)
      );
    }
  }

  async _ensureWindowReady(win) {
    if (win.closed) {
      throw new Error("window is closed");
    }
    let promises = [];
    let listenersToRemove = [];
    function promiseEvent(type) {
      promises.push(
        new Promise(resolve => {
          let listener = () => {
            win.removeEventListener(type, listener);
            resolve();
          };
          win.addEventListener(type, listener);
          listenersToRemove.push([type, listener]);
        })
      );
    }
    let { focusedWindow, activeWindow } = Services.focus;
    if (activeWindow != win) {
      promiseEvent("activate");
    }
    if (focusedWindow) {
      // We may have focused a non-remote child window, find the browser window:
      let { rootTreeItem } = focusedWindow.docShell;
      rootTreeItem.QueryInterface(Ci.nsIDocShell);
      focusedWindow = rootTreeItem.docViewer.DOMDocument.defaultView;
    }
    if (focusedWindow != win) {
      promiseEvent("focus");
    }
    if (promises.length) {
      let unloadListener;
      let unloadPromise = new Promise((resolve, reject) => {
        unloadListener = () => {
          for (let [type, listener] of listenersToRemove) {
            win.removeEventListener(type, listener);
          }
          reject(new Error("window unloaded"));
        };
        win.addEventListener("unload", unloadListener, { once: true });
      });
      try {
        let allPromises = Promise.all(promises);
        await Promise.race([allPromises, unloadPromise]);
      } finally {
        win.removeEventListener("unload", unloadListener);
      }
    }
  }

  static _getAndMaybeCreatePanel(doc) {
    // // Lazy load the extension-notification panel the first time we need to display it.
    let template = doc.getElementById("extensionNotificationTemplate");
    if (template) {
      template.replaceWith(template.content);
    }

    return doc.getElementById("extension-notification-panel");
  }
}
