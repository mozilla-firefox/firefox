/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

import { EventEmitter } from "resource://gre/modules/EventEmitter.sys.mjs";
import { XPCOMUtils } from "resource://gre/modules/XPCOMUtils.sys.mjs";

const lazy = {};

ChromeUtils.defineESModuleGetters(lazy, {
  AMBrowserExtensionsImport: "resource://gre/modules/AddonManager.sys.mjs",
  AMTelemetry: "resource://gre/modules/AddonManager.sys.mjs",
  AddonManager: "resource://gre/modules/AddonManager.sys.mjs",
  AddonManagerPrivate: "resource://gre/modules/AddonManager.sys.mjs",
  AppMenuNotifications: "resource://gre/modules/AppMenuNotifications.sys.mjs",
  ExtensionData: "resource://gre/modules/Extension.sys.mjs",
  ExtensionPermissions: "resource://gre/modules/ExtensionPermissions.sys.mjs",
  OriginControls: "resource://gre/modules/ExtensionPermissions.sys.mjs",
  QuarantinedDomains: "resource://gre/modules/ExtensionPermissions.sys.mjs",
});

ChromeUtils.defineLazyGetter(
  lazy,
  "l10n",
  () =>
    new Localization(["browser/extensionsUI.ftl", "branding/brand.ftl"], true)
);

ChromeUtils.defineLazyGetter(lazy, "logConsole", () =>
  console.createInstance({
    prefix: "ExtensionsUI",
    maxLogLevelPref: "extensions.webextensions.log.level",
  })
);

XPCOMUtils.defineLazyPreferenceGetter(
  lazy,
  "dataCollectionPermissionsEnabled",
  "extensions.dataCollectionPermissions.enabled",
  false
);

const DEFAULT_EXTENSION_ICON =
  "chrome://mozapps/skin/extensions/extensionGeneric.svg";

function getTabBrowser(browser) {
  while (browser.ownerGlobal.docShell.itemType !== Ci.nsIDocShell.typeChrome) {
    browser = browser.ownerGlobal.docShell.chromeEventHandler;
  }
  let window = browser.ownerGlobal;
  let viewType = browser.getAttribute("webextension-view-type");
  if (viewType == "sidebar") {
    window = window.browsingContext.topChromeWindow;
  }
  if (viewType == "popup" || viewType == "sidebar") {
    browser = window.gBrowser.selectedBrowser;
  }
  return { browser, window };
}

export var ExtensionsUI = {
  sideloaded: new Set(),
  updates: new Set(),
  sideloadListener: null,

  pendingNotifications: new WeakMap(),

  async init() {
    Services.obs.addObserver(this, "webextension-permission-prompt");
    Services.obs.addObserver(this, "webextension-update-permissions");
    Services.obs.addObserver(this, "webextension-install-notify");
    Services.obs.addObserver(this, "webextension-optional-permission-prompt");
    Services.obs.addObserver(this, "webextension-defaultsearch-prompt");
    Services.obs.addObserver(this, "webextension-imported-addons-cancelled");
    Services.obs.addObserver(this, "webextension-imported-addons-complete");
    Services.obs.addObserver(this, "webextension-imported-addons-pending");

    await Services.wm.getMostRecentWindow("navigator:browser")
      .delayedStartupPromise;

    this._checkForSideloaded();
  },

  async _checkForSideloaded() {
    let sideloaded = await lazy.AddonManagerPrivate.getNewSideloads();

    if (!sideloaded.length) {
      // No new side-loads. We're done.
      return;
    }

    // The ordering shouldn't matter, but tests depend on notifications
    // happening in a specific order.
    sideloaded.sort((a, b) => a.id.localeCompare(b.id));

    if (!this.sideloadListener) {
      this.sideloadListener = {
        onEnabled: addon => {
          if (!this.sideloaded.has(addon)) {
            return;
          }

          this.sideloaded.delete(addon);
          this._updateNotifications();

          if (this.sideloaded.size == 0) {
            lazy.AddonManager.removeAddonListener(this.sideloadListener);
            this.sideloadListener = null;
          }
        },
      };
      lazy.AddonManager.addAddonListener(this.sideloadListener);
    }

    for (let addon of sideloaded) {
      this.sideloaded.add(addon);
    }
    this._updateNotifications();
  },

  _updateNotifications() {
    const { sideloaded, updates } = this;
    const { importedAddonIDs } = lazy.AMBrowserExtensionsImport;

    if (importedAddonIDs.length + sideloaded.size + updates.size == 0) {
      lazy.AppMenuNotifications.removeNotification("addon-alert");
    } else {
      lazy.AppMenuNotifications.showBadgeOnlyNotification("addon-alert");
    }
    this.emit("change");
  },

  showAddonsManager(
    tabbrowser,
    strings,
    icon,
    {
      addon = undefined,
      shouldShowIncognitoCheckbox = false,
      shouldShowTechnicalAndInteractionCheckbox = false,
    } = {}
  ) {
    let global = tabbrowser.selectedBrowser.ownerGlobal;
    return global.BrowserAddonUI.openAddonsMgr("addons://list/extension").then(
      aomWin => {
        let aomBrowser = aomWin.docShell.chromeEventHandler;
        return this.showPermissionsPrompt(aomBrowser, strings, icon, {
          addon,
          shouldShowIncognitoCheckbox,
          shouldShowTechnicalAndInteractionCheckbox,
        });
      }
    );
  },

  showSideloaded(tabbrowser, addon) {
    addon.markAsSeen();
    this.sideloaded.delete(addon);
    this._updateNotifications();

    let strings = this._buildStrings({
      addon,
      permissions: addon.installPermissions,
      type: "sideload",
    });

    lazy.AMTelemetry.recordManageEvent(addon, "sideload_prompt", {
      num_strings: strings.msgs.length,
    });

    this.showAddonsManager(tabbrowser, strings, addon.iconURL, {
      addon,
      shouldShowIncognitoCheckbox: true,
      shouldShowTechnicalAndInteractionCheckbox:
        lazy.dataCollectionPermissionsEnabled,
    }).then(async answer => {
      if (answer) {
        await addon.enable();

        this._updateNotifications();
      }
      this.emit("sideload-response");
    });
  },

  showUpdate(browser, info) {
    lazy.AMTelemetry.recordInstallEvent(info.install, {
      step: "permissions_prompt",
      num_strings: info.strings.msgs.length,
    });

    this.showAddonsManager(browser, info.strings, info.addon.iconURL).then(
      answer => {
        if (answer) {
          info.resolve();
        } else {
          info.reject();
        }
        // At the moment, this prompt will re-appear next time we do an update
        // check.  See bug 1332360 for proposal to avoid this.
        this.updates.delete(info);
        this._updateNotifications();
      }
    );
  },

  observe(subject, topic) {
    if (topic == "webextension-permission-prompt") {
      let { target, info } = subject.wrappedJSObject;

      let { browser, window } = getTabBrowser(target);

      // Dismiss the progress notification.  Note that this is bad if
      // there are multiple simultaneous installs happening, see
      // bug 1329884 for a longer explanation.
      let progressNotification = window.PopupNotifications.getNotification(
        "addon-progress",
        browser
      );
      if (progressNotification) {
        progressNotification.remove();
      }

      info.unsigned =
        info.addon.signedState <= lazy.AddonManager.SIGNEDSTATE_MISSING;
      if (
        info.unsigned &&
        Cu.isInAutomation &&
        Services.prefs.getBoolPref(
          "extensions.ui.showAddonIconForUnsigned",
          false
        )
      ) {
        info.unsigned = false;
      }

      let strings = this._buildStrings(info);

      // If this is an update with no promptable permissions, just apply it
      if (
        info.type == "update" &&
        !strings.msgs.length &&
        !strings.dataCollectionPermissions?.msg
      ) {
        info.resolve();
        return;
      }

      let icon = info.unsigned
        ? "chrome://global/skin/icons/warning.svg"
        : info.icon;

      if (info.type == "sideload") {
        lazy.AMTelemetry.recordManageEvent(info.addon, "sideload_prompt", {
          num_strings: strings.msgs.length,
        });
      } else {
        lazy.AMTelemetry.recordInstallEvent(info.install, {
          step: "permissions_prompt",
          num_strings: strings.msgs.length,
        });
      }

      // We don't want to show the incognito checkbox in the update prompt or
      // optional prompt (which shouldn't be possible in this case), but it's
      // fine for installs (including sideload).
      const isInstallDialog = !info.type || info.type === "sideload";
      const shouldShowIncognitoCheckbox = isInstallDialog;
      // Same for the data collection checkbox.
      const shouldShowTechnicalAndInteractionCheckbox =
        lazy.dataCollectionPermissionsEnabled && isInstallDialog;

      this.showPermissionsPrompt(browser, strings, icon, {
        addon: info.addon,
        shouldShowIncognitoCheckbox,
        shouldShowTechnicalAndInteractionCheckbox,
      }).then(answer => {
        if (answer) {
          info.resolve();
        } else {
          info.reject();
        }
      });
    } else if (topic == "webextension-update-permissions") {
      let info = subject.wrappedJSObject;
      info.type = "update";
      let strings = this._buildStrings(info);

      // If we don't prompt for any new permissions, just apply it
      if (!strings.msgs.length && !strings.dataCollectionPermissions?.msg) {
        info.resolve();
        return;
      }

      let update = {
        strings,
        permissions: info.permissions,
        install: info.install,
        addon: info.addon,
        resolve: info.resolve,
        reject: info.reject,
      };

      this.updates.add(update);
      this._updateNotifications();
    } else if (topic == "webextension-install-notify") {
      let { target, addon, callback } = subject.wrappedJSObject;
      this.showInstallNotification(target, addon).then(() => {
        if (callback) {
          callback();
        }
      });
    } else if (topic == "webextension-optional-permission-prompt") {
      let { browser, name, icon, permissions, resolve } =
        subject.wrappedJSObject;
      let strings = this._buildStrings({
        type: "optional",
        addon: { name },
        permissions,
      });

      // If we don't have any promptable permissions, just proceed
      if (!strings.msgs.length && !strings.dataCollectionPermissions?.msg) {
        resolve(true);
        return;
      }
      // "userScripts" is an OptionalOnlyPermission, which means that it can
      // only be requested through the permissions.request() API, without other
      // permissions in the same request.
      let isUserScriptsRequest =
        permissions.permissions.length === 1 &&
        permissions.permissions[0] === "userScripts";
      resolve(
        this.showPermissionsPrompt(browser, strings, icon, {
          shouldShowIncognitoCheckbox: false,
          shouldShowTechnicalAndInteractionCheckbox: false,
          isUserScriptsRequest,
        })
      );
    } else if (topic == "webextension-defaultsearch-prompt") {
      let { browser, name, icon, respond, currentEngine, newEngine } =
        subject.wrappedJSObject;

      const [searchDesc, searchYes, searchNo] = lazy.l10n.formatMessagesSync([
        {
          id: "webext-default-search-description",
          args: { addonName: "<>", currentEngine, newEngine },
        },
        "webext-default-search-yes",
        "webext-default-search-no",
      ]);

      const strings = { addonName: name, text: searchDesc.value };
      for (let attr of searchYes.attributes) {
        if (attr.name === "label") {
          strings.acceptText = attr.value;
        } else if (attr.name === "accesskey") {
          strings.acceptKey = attr.value;
        }
      }
      for (let attr of searchNo.attributes) {
        if (attr.name === "label") {
          strings.cancelText = attr.value;
        } else if (attr.name === "accesskey") {
          strings.cancelKey = attr.value;
        }
      }

      this.showDefaultSearchPrompt(browser, strings, icon).then(respond);
    } else if (
      [
        "webextension-imported-addons-cancelled",
        "webextension-imported-addons-complete",
        "webextension-imported-addons-pending",
      ].includes(topic)
    ) {
      this._updateNotifications();
    }
  },

  // Create a set of formatted strings for a permission prompt
  _buildStrings(info) {
    const strings = lazy.ExtensionData.formatPermissionStrings(info, {
      fullDomainsList: true,
    });
    strings.addonName = info.addon.name;
    return strings;
  },

  async showPermissionsPrompt(
    target,
    strings,
    icon,
    {
      addon = undefined,
      shouldShowIncognitoCheckbox = false,
      shouldShowTechnicalAndInteractionCheckbox = false,
      isUserScriptsRequest = false,
    } = {}
  ) {
    let { browser, window } = getTabBrowser(target);

    let showIncognitoCheckbox = shouldShowIncognitoCheckbox;
    if (showIncognitoCheckbox) {
      showIncognitoCheckbox = !!(
        addon.permissions &
        lazy.AddonManager.PERM_CAN_CHANGE_PRIVATEBROWSING_ACCESS
      );
    }

    let showTechnicalAndInteractionCheckbox =
      shouldShowTechnicalAndInteractionCheckbox &&
      !!strings.dataCollectionPermissions?.collectsTechnicalAndInteractionData;

    const incognitoPermissionName = "internal:privateBrowsingAllowed";
    let grantPrivateBrowsingAllowed = false;
    if (showIncognitoCheckbox) {
      let { permissions } = await lazy.ExtensionPermissions.get(addon.id);
      grantPrivateBrowsingAllowed = permissions.includes(
        incognitoPermissionName
      );
    }

    const technicalAndInteractionDataName = "technicalAndInteraction";
    // This is an opt-out setting.
    let grantTechnicalAndInteractionDataCollection = true;

    // Wait for any pending prompts to complete before showing the next one.
    let pending;
    while ((pending = this.pendingNotifications.get(browser))) {
      await pending;
    }

    let promise = new Promise(resolve => {
      function eventCallback(topic) {
        if (topic == "swapping") {
          return true;
        }
        if (topic == "removed") {
          Services.tm.dispatchToMainThread(() => {
            resolve(false);
          });
        }
        return false;
      }

      // Show the SUMO link already part of the popupnotification by setting
      // learnMoreURL option if there are permissions to be granted to the
      // addon being installed, or if the private browsing checkbox is shown,
      // or if the data collection checkbox is shown.
      const learnMoreURL =
        strings.msgs.length ||
        strings.dataCollectionPermissions?.msg ||
        showIncognitoCheckbox ||
        showTechnicalAndInteractionCheckbox
          ? Services.urlFormatter.formatURLPref("app.support.baseURL") +
            "extension-permissions"
          : undefined;

      let options = {
        hideClose: true,
        popupIconURL: icon || DEFAULT_EXTENSION_ICON,
        popupIconClass: icon ? "" : "addon-warning-icon",
        learnMoreURL,
        persistent: true,
        eventCallback,
        removeOnDismissal: true,
        popupOptions: {
          position: "bottomright topright",
        },
        // Pass additional options used internally by the
        // addon-webext-permissions-notification custom element
        // (defined and registered by browser-addons.js).
        customElementOptions: {
          strings,
          showIncognitoCheckbox,
          grantPrivateBrowsingAllowed,
          onPrivateBrowsingAllowedChanged(value) {
            grantPrivateBrowsingAllowed = value;
          },
          showTechnicalAndInteractionCheckbox,
          grantTechnicalAndInteractionDataCollection,
          onTechnicalAndInteractionDataChanged(value) {
            grantTechnicalAndInteractionDataCollection = value;
          },
          isUserScriptsRequest,
        },
      };
      // The prompt/notification machinery has a special affordance wherein
      // certain subsets of the header string can be designated "names", and
      // referenced symbolically as "<>" and "{}" to receive special formatting.
      // That code assumes that the existence of |name| and |secondName| in the
      // options object imply the presence of "<>" and "{}" (respectively) in
      // in the string.
      //
      // At present, WebExtensions use this affordance while SitePermission
      // add-ons don't, so we need to conditionally set the |name| field.
      //
      // NB: This could potentially be cleaned up, see bug 1799710.
      if (strings.header.includes("<>")) {
        options.name = strings.addonName;
      }

      let action = {
        label: strings.acceptText,
        accessKey: strings.acceptKey,
        callback: () => {
          resolve(true);
        },
      };
      let secondaryActions = [
        {
          label: strings.cancelText,
          accessKey: strings.cancelKey,
          callback: () => {
            resolve(false);
          },
        },
      ];

      window.PopupNotifications.show(
        browser,
        "addon-webext-permissions",
        strings.header,
        browser.ownerGlobal.gUnifiedExtensions.getPopupAnchorID(
          browser,
          window
        ),
        action,
        secondaryActions,
        options
      );
    });

    this.pendingNotifications.set(browser, promise);
    promise.finally(() => this.pendingNotifications.delete(browser));
    // NOTE: this method is also called from showQuarantineConfirmation and some of its
    // related test cases (from browser_ext_originControls.js) seem to be hitting a race
    // if the promise returned requires an additional tick to be resolved.
    // Look more into the failure and determine a better option to avoid those failures.
    if (!showIncognitoCheckbox && !showTechnicalAndInteractionCheckbox) {
      return promise;
    }

    return promise.then(continueInstall => {
      if (!continueInstall) {
        return continueInstall;
      }

      const permsToUpdate = [];
      if (showIncognitoCheckbox) {
        permsToUpdate.push([
          incognitoPermissionName,
          "permissions",
          grantPrivateBrowsingAllowed,
        ]);
      }
      if (showTechnicalAndInteractionCheckbox) {
        permsToUpdate.push([
          technicalAndInteractionDataName,
          "data_collection",
          grantTechnicalAndInteractionDataCollection,
        ]);
      }
      // We need two update promises because the checkboxes are independent
      // from each other, and one can add its permission while the other could
      // remove its corresponding permission.
      const promises = permsToUpdate.map(([name, key, value]) => {
        const perms = { permissions: [], origins: [], data_collection: [] };
        perms[key] = [name];

        if (value) {
          return lazy.ExtensionPermissions.add(addon.id, perms).catch(err =>
            lazy.logConsole.warn(
              `Error on adding "${name}" permission to addon id "${addon.id}`,
              err
            )
          );
        }
        return lazy.ExtensionPermissions.remove(addon.id, perms).catch(err =>
          lazy.logConsole.warn(
            `Error on removing "${name}" permission from addon id "${addon.id}`,
            err
          )
        );
      });
      return Promise.all(promises).then(() => continueInstall);
    });
  },

  showDefaultSearchPrompt(target, strings, icon) {
    return new Promise(resolve => {
      let options = {
        hideClose: true,
        popupIconURL: icon || DEFAULT_EXTENSION_ICON,
        persistent: true,
        removeOnDismissal: true,
        eventCallback(topic) {
          if (topic == "removed") {
            resolve(false);
          }
        },
        name: strings.addonName,
      };

      let action = {
        label: strings.acceptText,
        accessKey: strings.acceptKey,
        callback: () => {
          resolve(true);
        },
      };
      let secondaryActions = [
        {
          label: strings.cancelText,
          accessKey: strings.cancelKey,
          callback: () => {
            resolve(false);
          },
        },
      ];

      let { browser, window } = getTabBrowser(target);

      window.PopupNotifications.show(
        browser,
        "addon-webext-defaultsearch",
        strings.text,
        "addons-notification-icon",
        action,
        secondaryActions,
        options
      );
    });
  },

  async showInstallNotification(target, addon) {
    let { window } = getTabBrowser(target);

    const message = await lazy.l10n.formatValue("addon-post-install-message", {
      addonName: "<>",
    });

    return new Promise(resolve => {
      let icon = addon.isWebExtension
        ? lazy.AddonManager.getPreferredIconURL(addon, 32, window) ||
          DEFAULT_EXTENSION_ICON
        : "chrome://browser/skin/addons/addon-install-installed.svg";

      if (addon.type == "theme") {
        const { previousActiveThemeID } = addon;

        async function themeActionUndo() {
          try {
            // Undoing a theme install means re-enabling the previous active theme
            // ID, and uninstalling the theme that was just installed
            const theme = await lazy.AddonManager.getAddonByID(
              previousActiveThemeID
            );

            if (theme) {
              await theme.enable();
            }

            // `addon` is the theme that was just installed
            await addon.uninstall();
          } finally {
            resolve();
          }
        }

        let themePrimaryAction = { callback: resolve };

        // Show the undo button if previousActiveThemeID is set.
        let themeSecondaryAction = previousActiveThemeID
          ? { callback: themeActionUndo }
          : null;

        let options = {
          name: addon.name,
          message,
          popupIconURL: icon,
          onDismissed: () => {
            lazy.AppMenuNotifications.removeNotification("theme-installed");
            resolve();
          },
        };
        lazy.AppMenuNotifications.showNotification(
          "theme-installed",
          themePrimaryAction,
          themeSecondaryAction,
          options
        );
      } else {
        let action = {
          callback: resolve,
        };

        let options = {
          name: addon.name,
          message,
          popupIconURL: icon,
          onDismissed: () => {
            lazy.AppMenuNotifications.removeNotification("addon-installed");
            resolve();
          },
        };
        lazy.AppMenuNotifications.showNotification(
          "addon-installed",
          action,
          null,
          options
        );
      }
    });
  },

  async showQuarantineConfirmation(browser, policy) {
    let [title, line1, line2, allow, deny] = await lazy.l10n.formatMessages([
      {
        id: "webext-quarantine-confirmation-title",
        args: { addonName: "<>" },
      },
      "webext-quarantine-confirmation-line-1",
      "webext-quarantine-confirmation-line-2",
      "webext-quarantine-confirmation-allow",
      "webext-quarantine-confirmation-deny",
    ]);

    let attr = (msg, name) => msg.attributes.find(a => a.name === name)?.value;

    let strings = {
      addonName: policy.name,
      header: title.value,
      text: line1.value + "\n\n" + line2.value,
      msgs: [],
      acceptText: attr(allow, "label"),
      acceptKey: attr(allow, "accesskey"),
      cancelText: attr(deny, "label"),
      cancelKey: attr(deny, "accesskey"),
    };

    let icon = policy.extension?.getPreferredIcon(32);

    if (await ExtensionsUI.showPermissionsPrompt(browser, strings, icon)) {
      lazy.QuarantinedDomains.setUserAllowedAddonIdPref(policy.id, true);
    }
  },

  // Populate extension toolbar popup menu with origin controls.
  originControlsMenu(popup, extensionId) {
    let policy = WebExtensionPolicy.getByID(extensionId);

    let win = popup.ownerGlobal;
    let doc = popup.ownerDocument;
    let tab = win.gBrowser.selectedTab;
    let uri = tab.linkedBrowser?.currentURI;
    let state = lazy.OriginControls.getState(policy, tab);

    let headerItem = doc.createXULElement("menuitem");
    headerItem.setAttribute("disabled", true);
    let items = [headerItem];

    // MV2 normally don't have controls, but we show the quarantined state.
    if (!policy?.extension.originControls && !state.quarantined) {
      return;
    }

    if (state.noAccess) {
      doc.l10n.setAttributes(headerItem, "origin-controls-no-access");
    } else {
      doc.l10n.setAttributes(headerItem, "origin-controls-options");
    }

    if (state.quarantined) {
      doc.l10n.setAttributes(headerItem, "origin-controls-quarantined-status");

      let allowQuarantined = doc.createXULElement("menuitem");
      doc.l10n.setAttributes(
        allowQuarantined,
        "origin-controls-quarantined-allow"
      );
      allowQuarantined.addEventListener("command", () => {
        this.showQuarantineConfirmation(tab.linkedBrowser, policy);
      });
      items.push(allowQuarantined);
    }

    if (state.allDomains) {
      let allDomains = doc.createXULElement("menuitem");
      allDomains.setAttribute("type", "radio");
      allDomains.setAttribute("checked", state.hasAccess);
      doc.l10n.setAttributes(allDomains, "origin-controls-option-all-domains");
      items.push(allDomains);
    }

    if (state.whenClicked) {
      let whenClicked = doc.createXULElement("menuitem");
      whenClicked.setAttribute("type", "radio");
      whenClicked.setAttribute("checked", !state.hasAccess);
      doc.l10n.setAttributes(
        whenClicked,
        "origin-controls-option-when-clicked"
      );
      whenClicked.addEventListener("command", async () => {
        await lazy.OriginControls.setWhenClicked(policy, uri);
        win.gUnifiedExtensions.updateAttention();
      });
      items.push(whenClicked);
    }

    if (state.alwaysOn) {
      let alwaysOn = doc.createXULElement("menuitem");
      alwaysOn.setAttribute("type", "radio");
      alwaysOn.setAttribute("checked", state.hasAccess);
      doc.l10n.setAttributes(alwaysOn, "origin-controls-option-always-on", {
        domain: uri.host,
      });
      alwaysOn.addEventListener("command", async () => {
        await lazy.OriginControls.setAlwaysOn(policy, uri);
        win.gUnifiedExtensions.updateAttention();
      });
      items.push(alwaysOn);
    }

    items.push(doc.createXULElement("menuseparator"));

    // Insert all items before Pin to toolbar OR Manage Extension, but after
    // any extension's menu items.
    let manageItem =
      popup.querySelector(".customize-context-manageExtension") ||
      popup.querySelector(".unified-extensions-context-menu-pin-to-toolbar");
    items.forEach(item => item && popup.insertBefore(item, manageItem));

    let cleanup = e => {
      if (e.target === popup) {
        items.forEach(item => item?.remove());
        popup.removeEventListener("popuphidden", cleanup);
      }
    };
    popup.addEventListener("popuphidden", cleanup);
  },
};

EventEmitter.decorate(ExtensionsUI);
