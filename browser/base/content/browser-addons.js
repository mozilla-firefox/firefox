/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

var { XPCOMUtils } = ChromeUtils.importESModule(
  "resource://gre/modules/XPCOMUtils.sys.mjs"
);

const lazy = {};

ChromeUtils.defineESModuleGetters(lazy, {
  AddonManager: "resource://gre/modules/AddonManager.sys.mjs",
  AMBrowserExtensionsImport: "resource://gre/modules/AddonManager.sys.mjs",
  AbuseReporter: "resource://gre/modules/AbuseReporter.sys.mjs",
  ExtensionCommon: "resource://gre/modules/ExtensionCommon.sys.mjs",
  ExtensionParent: "resource://gre/modules/ExtensionParent.sys.mjs",
  ExtensionPermissions: "resource://gre/modules/ExtensionPermissions.sys.mjs",
  OriginControls: "resource://gre/modules/ExtensionPermissions.sys.mjs",
  PERMISSION_L10N: "resource://gre/modules/ExtensionPermissionMessages.sys.mjs",
  SITEPERMS_ADDON_TYPE:
    "resource://gre/modules/addons/siteperms-addon-utils.sys.mjs",
  getSitePermsInstallPromptStringIds:
    "resource://gre/modules/addons/siteperms-addon-utils.sys.mjs",
});
ChromeUtils.defineLazyGetter(lazy, "l10n", function () {
  return new Localization(
    ["browser/addonNotifications.ftl", "branding/brand.ftl"],
    true
  );
});

/**
 * Mapping of error code -> [
 *   error-id,
 *   local-error-id,
 *   (optional) error-id-when-addon-name-is-missing,
 *   (optional) local-error-id-when-addon-name-is-missing,
 * ]
 *
 * error-id is used for errors in DownloadedAddonInstall,
 * local-error-id for errors in LocalAddonInstall.
 *
 * The error codes are defined in AddonManager's _errors Map.
 * Not all error codes listed there are translated,
 * since errors that are only triggered during updates
 * will never reach this code.
 */
const ERROR_L10N_IDS = new Map([
  [
    -1,
    [
      "addon-install-error-network-failure",
      "addon-local-install-error-network-failure",
    ],
  ],
  [
    -2,
    [
      "addon-install-error-incorrect-hash",
      "addon-local-install-error-incorrect-hash",
    ],
  ],
  [
    -3,
    [
      "addon-install-error-corrupt-file",
      "addon-local-install-error-corrupt-file",
    ],
  ],
  [
    -4,
    [
      "addon-install-error-file-access",
      "addon-local-install-error-file-access",
      "addon-install-error-no-addon-name-file-access",
      "addon-local-install-error-no-addon-name-file-access",
    ],
  ],
  [
    -5,
    ["addon-install-error-not-signed", "addon-local-install-error-not-signed"],
  ],
  [-8, ["addon-install-error-invalid-domain"]],
  [
    -10,
    ["addon-install-error-hard-blocked", "addon-install-error-hard-blocked"],
  ],
  [
    -11,
    ["addon-install-error-incompatible", "addon-install-error-incompatible"],
  ],
  [
    -13,
    [
      "addon-install-error-admin-install-only",
      "addon-install-error-admin-install-only",
    ],
  ],
  [
    -14,
    ["addon-install-error-soft-blocked2", "addon-install-error-soft-blocked2"],
  ],
]);

customElements.define(
  "addon-notification-blocklist-url",
  class MozAddonNotificationBlocklistURL extends HTMLAnchorElement {
    connectedCallback() {
      this.addEventListener("click", this);
    }

    disconnectedCallback() {
      this.removeEventListener("click", this);
    }

    handleEvent(e) {
      if (e.type == "click") {
        e.preventDefault();
        window.openTrustedLinkIn(this.href, "tab", {
          // Make sure the newly open tab is going to be focused, independently
          // from general user prefs.
          forceForeground: true,
        });
      }
    }
  },
  { extends: "a" }
);

customElements.define(
  "addon-webext-permissions-notification",
  class MozAddonPermissionsNotification extends customElements.get(
    "popupnotification"
  ) {
    show() {
      super.show();

      if (!this.notification) {
        return;
      }

      if (!this.notification.options?.customElementOptions) {
        throw new Error(
          "Mandatory customElementOptions property missing from notification options"
        );
      }

      this.textEl = this.querySelector("#addon-webext-perm-text");
      this.introEl = this.querySelector("#addon-webext-perm-intro");
      this.permsTitleEl = this.querySelector(
        "#addon-webext-perm-title-required"
      );
      this.permsListEl = this.querySelector("#addon-webext-perm-list-required");
      this.permsTitleDataCollectionEl = this.querySelector(
        "#addon-webext-perm-title-data-collection"
      );
      this.permsListDataCollectionEl = this.querySelector(
        "#addon-webext-perm-list-data-collection"
      );
      this.permsTitleOptionalEl = this.querySelector(
        "#addon-webext-perm-title-optional"
      );
      this.permsListOptionalEl = this.querySelector(
        "#addon-webext-perm-list-optional"
      );

      this.render();
    }

    get hasNoPermissions() {
      const {
        strings,
        showIncognitoCheckbox,
        showTechnicalAndInteractionCheckbox,
      } = this.notification.options.customElementOptions;

      return !(
        strings.msgs.length ||
        this.#dataCollectionPermissions?.msg ||
        showIncognitoCheckbox ||
        showTechnicalAndInteractionCheckbox
      );
    }

    get domainsSet() {
      if (!this.notification?.options?.customElementOptions) {
        return undefined;
      }
      const { strings } = this.notification.options.customElementOptions;
      return strings.fullDomainsList?.domainsSet;
    }

    get hasFullDomainsList() {
      return this.domainsSet?.size;
    }

    #isFullDomainsListEntryIndex(idx) {
      if (!this.hasFullDomainsList) {
        return false;
      }
      const { strings } = this.notification.options.customElementOptions;
      return strings.fullDomainsList.msgIdIndex === idx;
    }

    /**
     * @returns {{idx: number, collectsTechnicalAndInteractionData: boolean}}
     * An object with information about data collection permissions for the UI.
     */
    get #dataCollectionPermissions() {
      if (!this.notification?.options?.customElementOptions) {
        return undefined;
      }
      const { strings } = this.notification.options.customElementOptions;
      return strings.dataCollectionPermissions;
    }

    render() {
      const {
        strings,
        showIncognitoCheckbox,
        showTechnicalAndInteractionCheckbox,
        isUserScriptsRequest,
      } = this.notification.options.customElementOptions;

      const {
        textEl,
        introEl,
        permsTitleEl,
        permsListEl,
        permsTitleDataCollectionEl,
        permsListDataCollectionEl,
        permsTitleOptionalEl,
        permsListOptionalEl,
      } = this;

      const HTML_NS = "http://www.w3.org/1999/xhtml";
      const doc = this.ownerDocument;

      this.#clearChildElements();
      // Re-enable "Allow" button if it was disabled by a previous request with
      // isUserScriptsRequest=true.
      this.#setAllowButtonEnabled(true);

      if (strings.text) {
        textEl.textContent = strings.text;
        // By default, multiline strings don't get formatted properly. These
        // are presently only used in site permission add-ons, so we treat it
        // as a special case to avoid unintended effects on other things.
        if (strings.text.includes("\n\n")) {
          textEl.classList.add("addon-webext-perm-text-multiline");
        }
        textEl.hidden = false;
      }

      if (strings.listIntro) {
        introEl.textContent = strings.listIntro;
        introEl.hidden = false;
      }

      // "sitepermission" add-ons don't have section headers.
      if (strings.sectionHeaders) {
        const { required, dataCollection, optional } = strings.sectionHeaders;

        permsTitleEl.textContent = required;
        permsTitleDataCollectionEl.textContent = dataCollection;
        permsTitleOptionalEl.textContent = optional;
      }

      // Return earlier if there are no permissions to list.
      if (this.hasNoPermissions) {
        return;
      }

      // We only expect a single permission for a userScripts request per
      // https://searchfox.org/mozilla-central/rev/5fb48bf50516ed2529d533e5dfe49b4752efb8b8/browser/modules/ExtensionsUI.sys.mjs#308-313.
      if (isUserScriptsRequest) {
        // The "userScripts" permission cannot be granted until the user has
        // confirmed again in the notification's content, as described at
        // https://bugzilla.mozilla.org/show_bug.cgi?id=1917000#c1

        let { checkboxEl, warningEl } = this.#createUserScriptsPermissionItems(
          // "userScripts" can only be requested with "permissions.request()",
          // which enforces that it is the only permission in the request.
          strings.msgs[0]
        );

        this.#setAllowButtonEnabled(false);

        let item = doc.createElementNS(HTML_NS, "li");
        item.append(checkboxEl, warningEl);
        item.classList.add("webext-perm-optional");
        permsListEl.append(item);

        permsTitleEl.hidden = false;
        permsListEl.hidden = false;
      } else {
        if (strings.msgs.length) {
          for (let [idx, msg] of strings.msgs.entries()) {
            let item = doc.createElementNS(HTML_NS, "li");
            item.classList.add("webext-perm-granted");
            if (
              this.hasFullDomainsList &&
              this.#isFullDomainsListEntryIndex(idx)
            ) {
              item.append(this.#createFullDomainsListFragment(msg));
            } else {
              item.textContent = msg;
            }
            permsListEl.appendChild(item);
          }

          permsTitleEl.hidden = false;
          permsListEl.hidden = false;
        }

        if (this.#dataCollectionPermissions?.msg) {
          let item = doc.createElementNS(HTML_NS, "li");
          item.classList.add(
            "webext-perm-granted",
            "webext-data-collection-perm-granted"
          );
          item.textContent = this.#dataCollectionPermissions.msg;
          permsListDataCollectionEl.appendChild(item);
          permsTitleDataCollectionEl.hidden = false;
          permsListDataCollectionEl.hidden = false;
        }

        // Add a checkbox for the "technicalAndInteraction" optional data
        // collection permission.
        if (showTechnicalAndInteractionCheckbox) {
          let item = doc.createElementNS(HTML_NS, "li");
          item.classList.add(
            "webext-perm-optional",
            "webext-data-collection-perm-optional"
          );
          item.appendChild(this.#createTechnicalAndInteractionDataCheckbox());
          permsListOptionalEl.appendChild(item);
          permsTitleOptionalEl.hidden = false;
          permsListOptionalEl.hidden = false;
        }

        if (showIncognitoCheckbox) {
          let item = doc.createElementNS(HTML_NS, "li");
          item.classList.add(
            "webext-perm-optional",
            "webext-perm-privatebrowsing"
          );
          item.appendChild(this.#createPrivateBrowsingCheckbox());
          permsListOptionalEl.appendChild(item);
          permsTitleOptionalEl.hidden = false;
          permsListOptionalEl.hidden = false;
        }
      }
    }

    #createFullDomainsListFragment(msg) {
      const HTML_NS = "http://www.w3.org/1999/xhtml";
      const doc = this.ownerDocument;
      const label = doc.createXULElement("label");
      label.value = msg;
      const domainsList = doc.createElementNS(HTML_NS, "ul");
      domainsList.classList.add("webext-perm-domains-list");

      // Enforce max-height and ensure the domains list is
      // scrollable when there are more than 5 domains.
      if (this.domainsSet.size > 5) {
        domainsList.classList.add("scrollable-domains-list");
      }

      for (const domain of this.domainsSet) {
        let domainItem = doc.createElementNS(HTML_NS, "li");
        domainItem.textContent = domain;
        domainsList.appendChild(domainItem);
      }
      const { DocumentFragment } = this.documentGlobal;
      const fragment = new DocumentFragment();
      fragment.append(label);
      fragment.append(domainsList);
      return fragment;
    }

    #clearChildElements() {
      const {
        textEl,
        introEl,
        permsTitleEl,
        permsListEl,
        permsTitleDataCollectionEl,
        permsListDataCollectionEl,
        permsTitleOptionalEl,
        permsListOptionalEl,
      } = this;

      // Clear all changes to the child elements that may have been changed
      // by a previous call of the render method.
      textEl.textContent = "";
      textEl.hidden = true;
      textEl.classList.remove("addon-webext-perm-text-multiline");

      introEl.textContent = "";
      introEl.hidden = true;

      for (const title of [
        permsTitleEl,
        permsTitleOptionalEl,
        permsTitleDataCollectionEl,
      ]) {
        title.hidden = true;
      }

      for (const list of [
        permsListEl,
        permsListDataCollectionEl,
        permsListOptionalEl,
      ]) {
        list.textContent = "";
        list.hidden = true;
      }
    }

    #createUserScriptsPermissionItems(userScriptsPermissionMessage) {
      let checkboxEl = this.ownerDocument.createElement("moz-checkbox");
      checkboxEl.label = userScriptsPermissionMessage;
      checkboxEl.checked = false;
      checkboxEl.addEventListener("change", () => {
        // The main "Allow" button is disabled until the checkbox is checked.
        this.#setAllowButtonEnabled(checkboxEl.checked);
      });

      let warningEl = this.ownerDocument.createElement("moz-message-bar");
      warningEl.setAttribute("type", "warning");
      warningEl.setAttribute(
        "message",
        lazy.PERMISSION_L10N.formatValueSync(
          "webext-perms-extra-warning-userScripts-short"
        )
      );

      return { checkboxEl, warningEl };
    }

    #setAllowButtonEnabled(allowed) {
      let disabled = !allowed;
      // "mainactiondisabled" mirrors the "disabled" boolean attribute of the
      // "Allow" button.
      this.toggleAttribute("mainactiondisabled", disabled);

      // The "mainactiondisabled" attribute may also be toggled by the
      // PopupNotifications._setNotificationUIState() method, which can be
      // called as a side effect of toggling a checkbox within the notification
      // (via PopupNotifications._onCommand).
      //
      // To prevent PopupNotifications._setNotificationUIState() from setting
      // the "mainactiondisabled" attribute to a different state, also set the
      // "invalidselection" attribute, since _setNotificationUIState() mirrors
      // its value to "mainactiondisabled".
      //
      // TODO bug 1938623: Remove this when a better alternative exists.
      this.toggleAttribute("invalidselection", disabled);
    }

    #createPrivateBrowsingCheckbox() {
      const { grantPrivateBrowsingAllowed } =
        this.notification.options.customElementOptions;

      let checkboxEl = this.ownerDocument.createElement("moz-checkbox");
      checkboxEl.checked = grantPrivateBrowsingAllowed;
      checkboxEl.addEventListener("change", () => {
        // NOTE: the popupnotification instances will be reused
        // and so the callback function is destructured here to
        // avoid this custom element to prevent it from being
        // garbage collected.
        const { onPrivateBrowsingAllowedChanged } =
          this.notification.options.customElementOptions;
        onPrivateBrowsingAllowedChanged?.(checkboxEl.checked);
      });
      this.ownerDocument.l10n.setAttributes(
        checkboxEl,
        "popup-notification-addon-privatebrowsing-checkbox2"
      );
      return checkboxEl;
    }

    #createTechnicalAndInteractionDataCheckbox() {
      const { grantTechnicalAndInteractionDataCollection } =
        this.notification.options.customElementOptions;

      const checkboxEl = this.ownerDocument.createElement("moz-checkbox");
      this.ownerDocument.l10n.setAttributes(
        checkboxEl,
        "popup-notification-addon-technical-and-interaction-checkbox"
      );
      checkboxEl.checked = grantTechnicalAndInteractionDataCollection;
      checkboxEl.addEventListener("change", () => {
        // NOTE: the popupnotification instances will be reused
        // and so the callback function is destructured here to
        // avoid this custom element to prevent it from being
        // garbage collected.
        const { onTechnicalAndInteractionDataChanged } =
          this.notification.options.customElementOptions;
        onTechnicalAndInteractionDataChanged?.(checkboxEl.checked);
      });

      return checkboxEl;
    }
  },
  { extends: "popupnotification" }
);

customElements.define(
  "addon-progress-notification",
  class MozAddonProgressNotification extends customElements.get(
    "popupnotification"
  ) {
    show() {
      super.show();
      this.progressmeter = document.getElementById(
        "addon-progress-notification-progressmeter"
      );

      this.progresstext = document.getElementById(
        "addon-progress-notification-progresstext"
      );

      if (!this.notification) {
        return;
      }

      this.notification.options.installs.forEach(function (aInstall) {
        aInstall.addListener(this);
      }, this);

      // Calling updateProgress can sometimes cause this notification to be
      // removed in the middle of refreshing the notification panel which
      // makes the panel get refreshed again. Just initialise to the
      // undetermined state and then schedule a proper check at the next
      // opportunity
      this.setProgress(0, -1);
      this._updateProgressTimeout = setTimeout(
        this.updateProgress.bind(this),
        0
      );
    }

    disconnectedCallback() {
      this.destroy();
    }

    destroy() {
      if (!this.notification) {
        return;
      }
      this.notification.options.installs.forEach(function (aInstall) {
        aInstall.removeListener(this);
      }, this);

      clearTimeout(this._updateProgressTimeout);
    }

    setProgress(aProgress, aMaxProgress) {
      if (aMaxProgress == -1) {
        this.progressmeter.removeAttribute("value");
      } else {
        this.progressmeter.setAttribute(
          "value",
          (aProgress * 100) / aMaxProgress
        );
      }

      let now = Date.now();

      if (!this.notification.lastUpdate) {
        this.notification.lastUpdate = now;
        this.notification.lastProgress = aProgress;
        return;
      }

      let delta = now - this.notification.lastUpdate;
      if (delta < 400 && aProgress < aMaxProgress) {
        return;
      }

      // Set min. time delta to avoid division by zero in the upcoming speed calculation
      delta = Math.max(delta, 400);
      delta /= 1000;

      // This algorithm is the same used by the downloads code.
      let speed = (aProgress - this.notification.lastProgress) / delta;
      if (this.notification.speed) {
        speed = speed * 0.9 + this.notification.speed * 0.1;
      }

      this.notification.lastUpdate = now;
      this.notification.lastProgress = aProgress;
      this.notification.speed = speed;

      let status = null;
      [status, this.notification.last] = DownloadUtils.getDownloadStatus(
        aProgress,
        aMaxProgress,
        speed,
        this.notification.last
      );
      this.progresstext.setAttribute("value", status);
      this.progresstext.setAttribute("tooltiptext", status);
    }

    cancel() {
      let installs = this.notification.options.installs;
      installs.forEach(function (aInstall) {
        try {
          aInstall.cancel();
        } catch (e) {
          // Cancel will throw if the download has already failed
        }
      }, this);

      PopupNotifications.remove(this.notification);
    }

    updateProgress() {
      if (!this.notification) {
        return;
      }

      let downloadingCount = 0;
      let progress = 0;
      let maxProgress = 0;

      this.notification.options.installs.forEach(function (aInstall) {
        if (aInstall.maxProgress == -1) {
          maxProgress = -1;
        }
        progress += aInstall.progress;
        if (maxProgress >= 0) {
          maxProgress += aInstall.maxProgress;
        }
        if (aInstall.state < AddonManager.STATE_DOWNLOADED) {
          downloadingCount++;
        }
      });

      if (downloadingCount == 0) {
        this.destroy();
        this.progressmeter.removeAttribute("value");
        const status = lazy.l10n.formatValueSync("addon-download-verifying");
        this.progresstext.setAttribute("value", status);
        this.progresstext.setAttribute("tooltiptext", status);
      } else {
        this.setProgress(progress, maxProgress);
      }
    }

    onDownloadProgress() {
      this.updateProgress();
    }

    onDownloadFailed() {
      this.updateProgress();
    }

    onDownloadCancelled() {
      this.updateProgress();
    }

    onDownloadEnded() {
      this.updateProgress();
    }
  },
  { extends: "popupnotification" }
);

// This custom element wraps the messagebar shown in the extensions panel
// and used in both ext-browserAction.js and browser-unified-extensions.js
customElements.define(
  "unified-extensions-item-messagebar-wrapper",
  class extends HTMLElement {
    get extensionPolicy() {
      return WebExtensionPolicy.getByID(this.extensionId);
    }

    get extensionName() {
      return this.extensionPolicy?.name;
    }

    get isSoftBlocked() {
      return this.extensionPolicy?.extension?.isSoftBlocked;
    }

    connectedCallback() {
      this.messagebar = document.createElement("moz-message-bar");
      this.messagebar.classList.add("unified-extensions-item-messagebar");
      this.append(this.messagebar);
      this.refresh();
    }

    disconnectedCallback() {
      this.messagebar?.remove();
    }

    async refresh() {
      if (!this.messagebar) {
        // Nothing to refresh, the custom element has not been
        // connected to the DOM yet.
        return;
      }
      if (!customElements.get("moz-message-bar")) {
        document.createElement("moz-message-bar");
        await customElements.whenDefined("moz-message-bar");
      }
      const { messagebar } = this;
      if (this.isSoftBlocked) {
        const SOFTBLOCK_FLUENTID =
          "unified-extensions-item-messagebar-softblocked2";
        if (
          messagebar.messageL10nId === SOFTBLOCK_FLUENTID &&
          messagebar.messageL10nArgs?.extensionName === this.extensionName
        ) {
          // nothing to refresh.
          return;
        }
        messagebar.removeAttribute("hidden");
        messagebar.setAttribute("type", "warning");
        messagebar.messageL10nId = SOFTBLOCK_FLUENTID;
        messagebar.messageL10nArgs = {
          extensionName: this.extensionName,
        };
      } else {
        if (messagebar.hasAttribute("hidden")) {
          // nothing to refresh.
          return;
        }
        messagebar.setAttribute("hidden", "true");
        messagebar.messageL10nId = null;
        messagebar.messageL10nArgs = null;
      }
      messagebar.requestUpdate();
    }
  }
);

class BrowserActionWidgetObserver {
  #connected = false;
  /**
   * @param {string} addonId The ID of the extension
   * @param {function()} onButtonAreaChanged Callback that is called whenever
   *   the observer detects the presence, absence or relocation of the browser
   *   action button for the given extension.
   */
  constructor(addonId, onButtonAreaChanged) {
    this.addonId = addonId;
    // The expected ID of the browserAction widget. Keep in sync with
    // actionWidgetId logic in ext-browserAction.js.
    this.widgetId = `${lazy.ExtensionCommon.makeWidgetId(addonId)}-browser-action`;
    this.onButtonAreaChanged = onButtonAreaChanged;
  }

  startObserving() {
    if (this.#connected) {
      return;
    }
    this.#connected = true;
    CustomizableUI.addListener(this);
    window.addEventListener("unload", this);
  }

  stopObserving() {
    if (!this.#connected) {
      return;
    }
    this.#connected = false;
    CustomizableUI.removeListener(this);
    window.removeEventListener("unload", this);
  }

  hasBrowserActionUI() {
    const policy = WebExtensionPolicy.getByID(this.addonId);
    if (!policy?.canAccessWindow(window)) {
      // Add-on is not an extension, or extension has not started yet. Or it
      // was uninstalled/disabled. Or disabled in current (private) window.
      return false;
    }
    if (!gUnifiedExtensions.browserActionFor(policy)) {
      // Does not have a browser action button.
      return false;
    }
    return true;
  }

  onWidgetCreated(aWidgetId) {
    // This is triggered as soon as ext-browserAction registers the button,
    // shortly after hasBrowserActionUI() above can return true for the first
    // time since add-on installation.
    if (aWidgetId === this.widgetId) {
      this.onButtonAreaChanged();
    }
  }

  onWidgetAdded(aWidgetId) {
    if (aWidgetId === this.widgetId) {
      this.onButtonAreaChanged();
    }
  }

  onWidgetMoved(aWidgetId) {
    if (aWidgetId === this.widgetId) {
      this.onButtonAreaChanged();
    }
  }

  handleEvent(event) {
    if (event.type === "unload") {
      this.stopObserving();
    }
  }
}

customElements.define(
  "addon-installed-notification",
  class MozAddonInstalledNotification extends customElements.get(
    "popupnotification"
  ) {
    #browserActionWidgetObserver;
    connectedCallback() {
      this.descriptionEl = this.querySelector("#addon-install-description");
      this.pinExtensionEl = this.querySelector(
        "#addon-pin-toolbarbutton-checkbox"
      );

      this.addEventListener("click", this);
      this.pinExtensionEl.addEventListener("command", this);
      this.#browserActionWidgetObserver?.startObserving();
    }

    disconnectedCallback() {
      this.removeEventListener("click", this);
      this.pinExtensionEl.removeEventListener("command", this);
      this.#browserActionWidgetObserver?.stopObserving();
    }

    get #settingsLinkId() {
      return "addon-install-settings-link";
    }

    handleEvent(event) {
      const { target } = event;

      switch (event.type) {
        case "click": {
          if (target.id === this.#settingsLinkId) {
            const { addonId } = this.notification.options.customElementOptions;
            BrowserAddonUI.openAddonsMgr(
              "addons://detail/" + encodeURIComponent(addonId)
            );
            // The settings link element has its href set to "#" to be
            // accessible with keyboard navigation, and so we call
            // preventDefault to avoid the "#" href to be implicitly
            // added to the browser chrome window url (See Bug 1983869
            // for more details of the regression that the implicit
            // change to the chrome window urls triggers).
            event.preventDefault();
          }
          break;
        }
        case "command":
          if (target == this.pinExtensionEl) {
            this.#handlePinnedCheckboxStateChange();
          }
          break;
      }
    }

    show() {
      super.show();

      if (!this.notification) {
        return;
      }

      if (!this.notification.options?.customElementOptions) {
        throw new Error(
          "Mandatory customElementOptions property missing from notification options"
        );
      }

      this.#browserActionWidgetObserver?.stopObserving();
      this.#browserActionWidgetObserver = new BrowserActionWidgetObserver(
        this.notification.options.customElementOptions.addonId,
        () => this.#renderPinToolbarButtonCheckbox()
      );

      this.render();
      if (this.isConnected) {
        this.#browserActionWidgetObserver.startObserving();
      }
    }

    render() {
      let fluentId = "appmenu-addon-post-install-message3";

      this.ownerDocument.l10n.setAttributes(this.descriptionEl, null);
      this.querySelector(`#${this.#settingsLinkId}`)?.remove();

      if (this.#dataCollectionPermissionsEnabled) {
        const HTML_NS = "http://www.w3.org/1999/xhtml";
        const link = document.createElementNS(HTML_NS, "a");
        link.setAttribute("id", this.#settingsLinkId);
        link.setAttribute("data-l10n-name", "settings-link");
        // Make the link both accessible and keyboard-friendly.
        link.href = "#";
        this.descriptionEl.append(link);

        fluentId = "appmenu-addon-post-install-message-with-data-collection";
      }

      this.ownerDocument.l10n.setAttributes(this.descriptionEl, fluentId);
      this.#renderPinToolbarButtonCheckbox();
    }

    get #dataCollectionPermissionsEnabled() {
      return Services.prefs.getBoolPref(
        "extensions.dataCollectionPermissions.enabled",
        false
      );
    }

    #renderPinToolbarButtonCheckbox() {
      // If the extension has a browser action, show the checkbox to allow the
      // user to customize its location. Hide by default until we know for
      // certain that the conditions have been met.
      this.pinExtensionEl.hidden = true;

      if (!this.#browserActionWidgetObserver.hasBrowserActionUI()) {
        return;
      }
      const widgetId = this.#browserActionWidgetObserver.widgetId;

      // Extension buttons appear in AREA_ADDONS by default. There are several
      // ways for the default to differ for a specific add-on, including the
      // extension specifying default_area in its manifest.json file, an
      // enterprise policy having been configured, or the user having moved the
      // button someplace else. We only show the checkbox if it is either in
      // AREA_ADDONS or in the toolbar. This covers almost all common cases.
      const area = CustomizableUI.getPlacementOfWidget(widgetId)?.area;
      let shouldPinToToolbar = area !== CustomizableUI.AREA_ADDONS;
      if (shouldPinToToolbar && area !== CustomizableUI.AREA_NAVBAR) {
        // We only support AREA_ADDONS and AREA_NAVBAR for now.
        return;
      }
      this.pinExtensionEl.checked = shouldPinToToolbar;
      this.pinExtensionEl.hidden = false;
    }

    #handlePinnedCheckboxStateChange() {
      if (!this.#browserActionWidgetObserver.hasBrowserActionUI()) {
        // Unexpected. #renderPinToolbarButtonCheckbox() should have hidden
        // the checkbox if there is no widget.
        const { addonId } = this.notification.options.customElementOptions;
        throw new Error(`No browser action widget found for ${addonId}!`);
      }
      const widgetId = this.#browserActionWidgetObserver.widgetId;
      const shouldPinToToolbar = this.pinExtensionEl.checked;
      if (shouldPinToToolbar) {
        gUnifiedExtensions._maybeMoveWidgetNodeBack(widgetId);
      }
      gUnifiedExtensions.pinToToolbar(widgetId, shouldPinToToolbar);
    }
  },
  { extends: "popupnotification" }
);

// Removes a doorhanger notification if all of the installs it was notifying
// about have ended in some way.
function removeNotificationOnEnd(notification, installs) {
  let count = installs.length;

  function maybeRemove(install) {
    install.removeListener(this);

    if (--count == 0) {
      // Check that the notification is still showing
      let current = PopupNotifications.getNotification(
        notification.id,
        notification.browser
      );
      if (current === notification) {
        notification.remove();
      }
    }
  }

  for (let install of installs) {
    install.addListener({
      onDownloadCancelled: maybeRemove,
      onDownloadFailed: maybeRemove,
      onInstallFailed: maybeRemove,
      onInstallEnded: maybeRemove,
    });
  }
}

function buildNotificationAction(msg, callback, options = {}) {
  let label = "";
  let accessKey = "";
  for (let { name, value } of msg.attributes) {
    switch (name) {
      case "label":
        label = value;
        break;
      case "accesskey":
        accessKey = value;
        break;
    }
  }
  const action = { label, accessKey, callback };
  if (options.disableSecurityDelay) {
    action.disableSecurityDelay = true;
  }
  return action;
}

var gXPInstallObserver = {
  pendingInstalls: new WeakMap(),

  showInstallConfirmation(browser, installInfo, height = undefined) {
    // If the confirmation notification is already open cache the installInfo
    // and the new confirmation will be shown later
    if (
      PopupNotifications.getNotification("addon-install-confirmation", browser)
    ) {
      let pending = this.pendingInstalls.get(browser);
      if (pending) {
        pending.push(installInfo);
      } else {
        this.pendingInstalls.set(browser, [installInfo]);
      }
      return;
    }

    let showNextConfirmation = () => {
      // Make sure the browser is still alive.
      if (!gBrowser.browsers.includes(browser)) {
        return;
      }

      let pending = this.pendingInstalls.get(browser);
      if (pending && pending.length) {
        this.showInstallConfirmation(browser, pending.shift());
      }
    };

    // If all installs have already been cancelled in some way then just show
    // the next confirmation
    if (
      installInfo.installs.every(i => i.state != AddonManager.STATE_DOWNLOADED)
    ) {
      showNextConfirmation();
      return;
    }

    // Make notifications persistent
    var options = {
      displayURI: installInfo.originatingURI,
      persistent: true,
      hideClose: true,
      popupOptions: {
        position: "bottomright topright",
      },
    };

    let acceptInstallation = () => {
      for (let install of installInfo.installs) {
        install.install();
      }
      installInfo = null;

      Glean.securityUi.events.accumulateSingleSample(
        Ci.nsISecurityUITelemetry.WARNING_CONFIRM_ADDON_INSTALL_CLICK_THROUGH
      );
    };

    let cancelInstallation = () => {
      if (installInfo) {
        for (let install of installInfo.installs) {
          // The notification may have been closed because the add-ons got
          // cancelled elsewhere, only try to cancel those that are still
          // pending install.
          if (install.state != AddonManager.STATE_CANCELLED) {
            install.cancel();
          }
        }
      }

      showNextConfirmation();
    };

    let unsigned = installInfo.installs.filter(
      i => i.addon.signedState <= AddonManager.SIGNEDSTATE_MISSING
    );
    let someUnsigned =
      !!unsigned.length && unsigned.length < installInfo.installs.length;

    options.eventCallback = aEvent => {
      switch (aEvent) {
        case "removed":
          cancelInstallation();
          break;
        case "shown": {
          let addonList = document.getElementById(
            "addon-install-confirmation-content"
          );
          while (addonList.firstChild) {
            addonList.firstChild.remove();
          }

          for (let install of installInfo.installs) {
            let container = document.createXULElement("hbox");

            let name = document.createXULElement("label");
            name.setAttribute("value", install.addon.name);
            name.setAttribute("class", "addon-install-confirmation-name");
            container.appendChild(name);

            if (
              someUnsigned &&
              install.addon.signedState <= AddonManager.SIGNEDSTATE_MISSING
            ) {
              let unsignedLabel = document.createXULElement("label");
              document.l10n.setAttributes(
                unsignedLabel,
                "popup-notification-addon-install-unsigned"
              );
              unsignedLabel.setAttribute(
                "class",
                "addon-install-confirmation-unsigned"
              );
              container.appendChild(unsignedLabel);
            }

            addonList.appendChild(container);
          }
          break;
        }
      }
    };

    options.learnMoreURL = Services.urlFormatter.formatURLPref(
      "app.support.baseURL"
    );

    let msgId;
    let notification = document.getElementById(
      "addon-install-confirmation-notification"
    );
    if (unsigned.length == installInfo.installs.length) {
      // None of the add-ons are verified
      msgId = "addon-confirm-install-unsigned-message";
      notification.setAttribute("warning", "true");
      options.learnMoreURL += "unsigned-addons";
    } else if (!unsigned.length) {
      // All add-ons are verified or don't need to be verified
      msgId = "addon-confirm-install-message";
      notification.removeAttribute("warning");
      options.learnMoreURL += "find-and-install-add-ons";
    } else {
      // Some of the add-ons are unverified, the list of names will indicate
      // which
      msgId = "addon-confirm-install-some-unsigned-message";
      notification.setAttribute("warning", "true");
      options.learnMoreURL += "unsigned-addons";
    }
    const addonCount = installInfo.installs.length;
    const messageString = lazy.l10n.formatValueSync(msgId, { addonCount });

    const [acceptMsg, cancelMsg] = lazy.l10n.formatMessagesSync([
      "addon-install-accept-button",
      "addon-install-cancel-button",
    ]);
    const action = buildNotificationAction(acceptMsg, acceptInstallation);
    const secondaryAction = buildNotificationAction(cancelMsg, () => {}, {
      disableSecurityDelay: true,
    });

    if (height) {
      notification.style.minHeight = height + "px";
    }

    let tab = gBrowser.getTabForBrowser(browser);
    if (tab) {
      gBrowser.selectedTab = tab;
    }

    let popup = PopupNotifications.show(
      browser,
      "addon-install-confirmation",
      messageString,
      gUnifiedExtensions.getPopupAnchorID(browser, window),
      action,
      [secondaryAction],
      options
    );

    removeNotificationOnEnd(popup, installInfo.installs);

    Glean.securityUi.events.accumulateSingleSample(
      Ci.nsISecurityUITelemetry.WARNING_CONFIRM_ADDON_INSTALL
    );
  },

  // IDs of addon install related notifications, passed by this file
  // (browser-addons.js) to PopupNotifications.show(). The only exception is
  // "addon-webext-permissions" (from browser/modules/ExtensionsUI.sys.mjs),
  // which can not only be triggered during add-on installation, but also
  // later, when the extension uses the browser.permissions.request() API.
  NOTIFICATION_IDS: [
    "addon-install-blocked",
    "addon-install-confirmation",
    "addon-install-failed",
    "addon-install-origin-blocked",
    "addon-install-webapi-blocked",
    "addon-install-policy-blocked",
    "addon-progress",
    "addon-webext-permissions",
    "xpinstall-disabled",
  ],

  /**
   * Remove all opened addon installation notifications
   *
   * @param {*} browser - Browser to remove notifications for
   * @returns {boolean} - true if notifications have been removed.
   */
  removeAllNotifications(browser) {
    let notifications = this.NOTIFICATION_IDS.map(id =>
      PopupNotifications.getNotification(id, browser)
    ).filter(notification => notification != null);

    PopupNotifications.remove(notifications, /* withoutUserResponse = */ true);

    return !!notifications.length;
  },

  logWarningFullScreenInstallBlocked() {
    // If notifications have been removed, log a warning to the website console
    let consoleMsg = Cc["@mozilla.org/scripterror;1"].createInstance(
      Ci.nsIScriptError
    );
    const message = lazy.l10n.formatValueSync(
      "addon-install-full-screen-blocked"
    );
    consoleMsg.initWithWindowID(
      message,
      gBrowser.currentURI.spec,
      0,
      0,
      Ci.nsIScriptError.warningFlag,
      "FullScreen",
      gBrowser.selectedBrowser.innerWindowID
    );
    Services.console.logMessage(consoleMsg);
  },

  // eslint-disable-next-line complexity
  async observe(aSubject, aTopic) {
    var installInfo = aSubject.wrappedJSObject;
    var browser = installInfo.browser;

    // Make sure the browser is still alive.
    if (!browser || !gBrowser.browsers.includes(browser)) {
      return;
    }

    const cancelInstallation = () => {
      for (let install of installInfo.installs) {
        if (install.state != AddonManager.STATE_CANCELLED) {
          install.cancel();
        }
      }
      if (installInfo.cancel) {
        installInfo.cancel();
      }
    };

    // Make notifications persistent
    var options = {
      displayURI: installInfo.originatingURI,
      persistent: true,
      hideClose: true,
      timeout: Date.now() + 30000,
      popupOptions: {
        position: "bottomright topright",
      },
    };

    switch (aTopic) {
      case "addon-install-disabled": {
        let msgId, action, secondaryActions;
        if (Services.prefs.prefIsLocked("xpinstall.enabled")) {
          msgId = "xpinstall-disabled-by-policy";
          action = null;
          secondaryActions = null;
        } else {
          msgId = "xpinstall-disabled";
          const [disabledMsg, cancelMsg] = await lazy.l10n.formatMessages([
            "xpinstall-disabled-button",
            "addon-install-cancel-button",
          ]);
          action = buildNotificationAction(disabledMsg, () => {
            Services.prefs.setBoolPref("xpinstall.enabled", true);
          });
          secondaryActions = [
            buildNotificationAction(cancelMsg, () => {}, {
              disableSecurityDelay: true,
            }),
          ];
        }

        PopupNotifications.show(
          browser,
          "xpinstall-disabled",
          await lazy.l10n.formatValue(msgId),
          gUnifiedExtensions.getPopupAnchorID(browser, window),
          action,
          secondaryActions,
          options
        );
        break;
      }
      case "addon-install-fullscreen-blocked": {
        // AddonManager denied installation because we are in DOM fullscreen
        this.logWarningFullScreenInstallBlocked();
        break;
      }
      case "addon-install-webapi-blocked":
      case "addon-install-policy-blocked":
      case "addon-install-origin-blocked": {
        const msgId =
          aTopic == "addon-install-policy-blocked"
            ? "addon-install-domain-blocked-by-policy"
            : "xpinstall-prompt";
        let messageString = await lazy.l10n.formatValue(msgId);
        if (Services.policies) {
          let extensionSettings = Services.policies.getExtensionSettings("*");
          if (
            extensionSettings &&
            "blocked_install_message" in extensionSettings
          ) {
            messageString += " " + extensionSettings.blocked_install_message;
          }
        }

        options.removeOnDismissal = true;
        options.persistent = false;
        Glean.securityUi.events.accumulateSingleSample(
          Ci.nsISecurityUITelemetry.WARNING_ADDON_ASKING_PREVENTED
        );
        let popup = PopupNotifications.show(
          browser,
          aTopic,
          messageString,
          gUnifiedExtensions.getPopupAnchorID(browser, window),
          null,
          null,
          options
        );
        removeNotificationOnEnd(popup, installInfo.installs);
        break;
      }
      case "addon-install-blocked": {
        // Dismiss the progress notification.  Note that this is bad if
        // there are multiple simultaneous installs happening, see
        // bug 1329884 for a longer explanation.
        let progressNotification = PopupNotifications.getNotification(
          "addon-progress",
          browser
        );
        if (progressNotification) {
          progressNotification.remove();
        }

        // The informational content differs somewhat for site permission
        // add-ons. AOM no longer supports installing multiple addons,
        // so the array handling here is vestigial.
        let isSitePermissionAddon = installInfo.installs.every(
          ({ addon }) => addon?.type === lazy.SITEPERMS_ADDON_TYPE
        );
        let hasHost = false;
        let headerId, msgId;
        if (isSitePermissionAddon) {
          const permissionType =
            installInfo.installs[0].addon.sitePermissions?.[0];
          const stringIds =
            lazy.getSitePermsInstallPromptStringIds(permissionType);

          if (stringIds?.header && stringIds?.message) {
            headerId = stringIds.header;
            msgId = stringIds.message;
          } else {
            console.error(
              `Unexpected missing or incomplete fluentIds for site permission "${permissionType}", ` +
                "siteperms-addon-utils.sys.mjs should be updated."
            );
            cancelInstallation();
            return;
          }
        } else if (options.displayURI) {
          // PopupNotifications.show replaces <> with options.name.
          headerId = { id: "xpinstall-prompt-header", args: { host: "<>" } };
          // BrowserUIUtils.getLocalizedFragment replaces %1$S with options.name.
          msgId = { id: "xpinstall-prompt-message", args: { host: "%1$S" } };
          options.name = options.displayURI.displayHost;
          hasHost = true;
        } else {
          headerId = "xpinstall-prompt-header-unknown";
          msgId = "xpinstall-prompt-message-unknown";
        }
        const [headerString, msgString] = await lazy.l10n.formatValues([
          headerId,
          msgId,
        ]);

        // displayURI becomes it's own label, so we unset it for this panel. It will become part of the
        // messageString above.
        let displayURI = options.displayURI;
        options.displayURI = undefined;

        options.eventCallback = topic => {
          if (topic !== "showing") {
            return;
          }
          let doc = browser.ownerDocument;
          let message = doc.getElementById("addon-install-blocked-message");
          // We must remove any prior use of this panel message in this window.
          while (message.firstChild) {
            message.firstChild.remove();
          }

          if (!hasHost) {
            message.textContent = msgString;
          } else {
            let b = doc.createElementNS("http://www.w3.org/1999/xhtml", "b");
            b.textContent = options.name;
            let fragment = BrowserUIUtils.getLocalizedFragment(
              doc,
              msgString,
              b
            );
            message.appendChild(fragment);
          }

          let article = isSitePermissionAddon
            ? "site-permission-addons"
            : "unlisted-extensions-risks";
          let learnMore = doc.getElementById("addon-install-blocked-info");
          learnMore.setAttribute("support-page", article);
        };
        Glean.securityUi.events.accumulateSingleSample(
          Ci.nsISecurityUITelemetry.WARNING_ADDON_ASKING_PREVENTED
        );

        const [
          installMsg,
          dontAllowMsg,
          neverAllowMsg,
          neverAllowAndReportMsg,
        ] = await lazy.l10n.formatMessages([
          "xpinstall-prompt-install",
          "xpinstall-prompt-dont-allow",
          "xpinstall-prompt-never-allow",
          "xpinstall-prompt-never-allow-and-report",
        ]);

        const action = buildNotificationAction(installMsg, () => {
          Glean.securityUi.events.accumulateSingleSample(
            Ci.nsISecurityUITelemetry
              .WARNING_ADDON_ASKING_PREVENTED_CLICK_THROUGH
          );
          installInfo.install();
        });

        const neverAllowCallback = () => {
          SitePermissions.setForPrincipal(
            browser.contentPrincipal,
            "install",
            SitePermissions.BLOCK
          );
          cancelInstallation();
        };

        const declineActions = [
          buildNotificationAction(dontAllowMsg, cancelInstallation, {
            disableSecurityDelay: true,
          }),
          buildNotificationAction(neverAllowMsg, neverAllowCallback, {
            disableSecurityDelay: true,
          }),
        ];

        if (isSitePermissionAddon) {
          // Restrict this to site permission add-ons for now pending a decision
          // from product about how to approach this for extensions.
          const permissionType =
            installInfo.installs[0].addon.sitePermissions?.[0];
          declineActions.push(
            buildNotificationAction(
              neverAllowAndReportMsg,
              () => {
                AMTelemetry.recordSuspiciousSiteEvent({
                  displayURI,
                  permissionType,
                });
                neverAllowCallback();
              },
              { disableSecurityDelay: true }
            )
          );
        }

        let popup = PopupNotifications.show(
          browser,
          aTopic,
          headerString,
          gUnifiedExtensions.getPopupAnchorID(browser, window),
          action,
          declineActions,
          options
        );
        removeNotificationOnEnd(popup, installInfo.installs);
        break;
      }
      case "addon-install-started": {
        // If all installs have already been downloaded then there is no need to
        // show the download progress
        if (
          installInfo.installs.every(
            aInstall => aInstall.state == AddonManager.STATE_DOWNLOADED
          )
        ) {
          return;
        }

        const messageString = lazy.l10n.formatValueSync(
          "addon-downloading-and-verifying",
          { addonCount: installInfo.installs.length }
        );
        options.installs = installInfo.installs;
        options.contentWindow = browser.contentWindow;
        options.sourceURI = browser.currentURI;
        options.eventCallback = function (aEvent) {
          switch (aEvent) {
            case "removed":
              options.contentWindow = null;
              options.sourceURI = null;
              break;
          }
        };

        const [acceptMsg, cancelMsg] = lazy.l10n.formatMessagesSync([
          "addon-install-accept-button",
          "addon-install-cancel-button",
        ]);

        const action = buildNotificationAction(acceptMsg, () => {});
        action.disabled = true;

        const secondaryAction = buildNotificationAction(
          cancelMsg,
          () => {
            for (let install of installInfo.installs) {
              if (install.state != AddonManager.STATE_CANCELLED) {
                install.cancel();
              }
            }
          },
          { disableSecurityDelay: true }
        );

        let notification = PopupNotifications.show(
          browser,
          "addon-progress",
          messageString,
          gUnifiedExtensions.getPopupAnchorID(browser, window),
          action,
          [secondaryAction],
          options
        );
        notification._startTime = Date.now();

        break;
      }
      case "addon-install-failed": {
        options.removeOnDismissal = true;
        options.persistent = false;

        // TODO This isn't terribly ideal for the multiple failure case
        for (let install of installInfo.installs) {
          let host;
          try {
            host = options.displayURI.host;
          } catch (e) {
            // displayURI might be missing or 'host' might throw for non-nsStandardURL nsIURIs.
          }

          if (!host) {
            host =
              install.sourceURI instanceof Ci.nsIStandardURL &&
              install.sourceURI.host;
          }

          let messageString;
          if (
            install.addon &&
            !Services.policies.mayInstallAddon({
              id: install.addon.id,
              type: install.addon.type,
              permissions: install.addon.userPermissions?.permissions,
            })
          ) {
            messageString = lazy.l10n.formatValueSync(
              "addon-installation-blocked-by-policy",
              { addonName: install.name, addonId: install.addon.id }
            );
            let extensionSettings = Services.policies.getExtensionSettings(
              install.addon.id
            );
            if (
              extensionSettings &&
              "blocked_install_message" in extensionSettings
            ) {
              messageString += " " + extensionSettings.blocked_install_message;
            }
          } else {
            // TODO bug 1834484: simplify computation of isLocal.
            const isLocal = !host;
            const fluentIds = ERROR_L10N_IDS.get(install.error);
            // We need to find the group of fluent IDs to use (error-id, local-error-id),
            // depending on whether we have the add-on name or not.
            const offset = fluentIds?.length === 4 && !install.name ? 2 : 0;
            let errorId = fluentIds?.[offset + isLocal ? 1 : 0];
            const args = {
              addonName: install.name,
              appVersion: Services.appinfo.version,
            };
            // TODO: Bug 1846725 - when there is no error ID (which shouldn't
            // happen but... we never know) we use the "incompatible" error
            // message for now but we should have a better error message
            // instead.
            if (!errorId) {
              errorId = "addon-install-error-incompatible";
            }
            messageString = lazy.l10n.formatValueSync(errorId, args);
          }

          // Add Learn More link when refusing to install an unsigned add-on
          if (install.error == AddonManager.ERROR_SIGNEDSTATE_REQUIRED) {
            options.learnMoreURL =
              Services.urlFormatter.formatURLPref("app.support.baseURL") +
              "unsigned-addons";
          }

          let notificationId = aTopic;

          const isBlocklistError = [
            AddonManager.ERROR_BLOCKLISTED,
            AddonManager.ERROR_SOFT_BLOCKED,
          ].includes(install.error);

          // On blocklist-related install failures:
          // - use "addon-install-failed-blocklist" as the notificationId
          //   (which will use the popupnotification with id
          //   "addon-install-failed-blocklist-notification" defined
          //   in popup-notification.inc)
          // - add an eventCallback that will take care of filling in the
          //   blocklistURL into the href attribute of the link element
          //   with id "addon-install-failed-blocklist-info"
          if (isBlocklistError) {
            const blocklistURL = await install.addon?.getBlocklistURL();
            notificationId = `${aTopic}-blocklist`;
            options.eventCallback = topic => {
              if (topic !== "showing") {
                return;
              }
              let doc = browser.ownerDocument;
              let blocklistURLEl = doc.getElementById(
                "addon-install-failed-blocklist-info"
              );
              if (blocklistURL) {
                blocklistURLEl.setAttribute("href", blocklistURL);
              } else {
                blocklistURLEl.removeAttribute("href");
              }
            };
          }

          PopupNotifications.show(
            browser,
            notificationId,
            messageString,
            gUnifiedExtensions.getPopupAnchorID(browser, window),
            null,
            null,
            options
          );

          // Can't have multiple notifications with the same ID, so stop here.
          break;
        }
        this._removeProgressNotification(browser);
        break;
      }
      case "addon-install-confirmation": {
        let showNotification = () => {
          let height = undefined;

          if (PopupNotifications.isPanelOpen) {
            let rect = window.windowUtils.getBoundsWithoutFlushing(
              document.getElementById("addon-progress-notification")
            );
            height = rect.height;
          }

          this._removeProgressNotification(browser);
          this.showInstallConfirmation(browser, installInfo, height);
        };

        let progressNotification = PopupNotifications.getNotification(
          "addon-progress",
          browser
        );
        if (progressNotification) {
          let downloadDuration = Date.now() - progressNotification._startTime;
          let securityDelay =
            Services.prefs.getIntPref("security.dialog_enable_delay") -
            downloadDuration;
          if (securityDelay > 0) {
            setTimeout(() => {
              // The download may have been cancelled during the security delay
              if (
                PopupNotifications.getNotification("addon-progress", browser)
              ) {
                showNotification();
              }
            }, securityDelay);
            break;
          }
        }
        showNotification();
        break;
      }
    }
  },
  _removeProgressNotification(aBrowser) {
    let notification = PopupNotifications.getNotification(
      "addon-progress",
      aBrowser
    );
    if (notification) {
      notification.remove();
    }
  },
};

var gExtensionsNotifications = {
  initialized: false,
  init() {
    this.updateAlerts();
    this.boundUpdate = this.updateAlerts.bind(this);
    ExtensionsUI.on("change", this.boundUpdate);
    this.initialized = true;
  },

  uninit() {
    // uninit() can race ahead of init() in some cases, if that happens,
    // we have no handler to remove.
    if (!this.initialized) {
      return;
    }
    ExtensionsUI.off("change", this.boundUpdate);
  },

  _createAddonButton(l10nId, addon, callback) {
    let text = addon
      ? lazy.l10n.formatValueSync(l10nId, { addonName: addon.name })
      : lazy.l10n.formatValueSync(l10nId);
    let button = document.createXULElement("toolbarbutton");
    button.setAttribute("id", l10nId);
    button.setAttribute("wrap", "true");
    button.setAttribute("label", text);
    button.setAttribute("tooltiptext", text);
    const DEFAULT_EXTENSION_ICON =
      "chrome://mozapps/skin/extensions/extensionGeneric.svg";
    button.setAttribute("image", addon?.iconURL || DEFAULT_EXTENSION_ICON);
    button.className = "addon-banner-item subviewbutton";

    button.addEventListener("command", callback);
    PanelUI.addonNotificationContainer.appendChild(button);
  },

  updateAlerts() {
    let sideloaded = ExtensionsUI.sideloaded;
    let updates = ExtensionsUI.updates;

    let container = PanelUI.addonNotificationContainer;

    while (container.firstChild) {
      container.firstChild.remove();
    }

    let items = 0;
    if (lazy.AMBrowserExtensionsImport.canCompleteOrCancelInstalls) {
      this._createAddonButton("webext-imported-addons", null, () => {
        lazy.AMBrowserExtensionsImport.completeInstalls();
      });
      items++;
    }

    for (let update of updates) {
      if (++items > 4) {
        break;
      }
      this._createAddonButton(
        "webext-perms-update-menu-item",
        update.addon,
        () => {
          ExtensionsUI.showUpdate(gBrowser, update);
        }
      );
    }

    for (let addon of sideloaded) {
      if (++items > 4) {
        break;
      }
      this._createAddonButton("webext-perms-sideload-menu-item", addon, () => {
        // We need to hide the main menu manually because the toolbarbutton is
        // removed immediately while processing this event, and PanelUI is
        // unable to identify which panel should be closed automatically.
        PanelUI.hide();
        ExtensionsUI.showSideloaded(gBrowser, addon);
      });
    }
  },
};

var BrowserAddonUI = {
  async promptRemoveExtension(addon) {
    let { name } = addon;
    let [title, btnTitle] = await lazy.l10n.formatValues([
      { id: "addon-removal-title", args: { name } },
      { id: "addon-removal-button" },
    ]);

    let {
      BUTTON_TITLE_IS_STRING: titleString,
      BUTTON_TITLE_CANCEL: titleCancel,
      BUTTON_POS_0,
      BUTTON_POS_1,
      confirmEx,
    } = Services.prompt;
    let btnFlags = BUTTON_POS_0 * titleString + BUTTON_POS_1 * titleCancel;

    // Enable abuse report checkbox in the remove extension dialog,
    // if enabled by the about:config prefs and the addon type
    // is currently supported.
    let checkboxMessage = null;
    if (
      gAddonAbuseReportEnabled &&
      ["extension", "theme"].includes(addon.type)
    ) {
      checkboxMessage = await lazy.l10n.formatValue(
        "addon-removal-abuse-report-checkbox"
      );
    }

    // If the prompt is being used for ML model removal, use a body message
    let body = null;
    if (addon.type === "mlmodel") {
      body = await lazy.l10n.formatValue("addon-mlmodel-removal-body");
    }

    let checkboxState = { value: false };
    let result = confirmEx(
      window,
      title,
      body,
      btnFlags,
      btnTitle,
      /* button1 */ null,
      /* button2 */ null,
      checkboxMessage,
      checkboxState
    );

    return { remove: result === 0, report: checkboxState.value };
  },

  async reportAddon(addonId, _reportEntryPoint) {
    let addon = addonId && (await AddonManager.getAddonByID(addonId));
    if (!addon) {
      return;
    }

    const amoUrl = lazy.AbuseReporter.getAMOFormURL({ addonId });
    window.openTrustedLinkIn(amoUrl, "tab", {
      // Make sure the newly open tab is going to be focused, independently
      // from general user prefs.
      forceForeground: true,
    });
  },

  async removeAddon(addonId) {
    let addon = addonId && (await AddonManager.getAddonByID(addonId));
    if (!addon || !(addon.permissions & AddonManager.PERM_CAN_UNINSTALL)) {
      return;
    }

    let { remove, report } = await this.promptRemoveExtension(addon);

    if (remove) {
      // Leave the extension in pending uninstall if we are also reporting the
      // add-on.
      await addon.uninstall(report);

      if (report) {
        await this.reportAddon(addon.id, "uninstall");
      }
    }
  },

  async manageAddon(addonId) {
    let addon = addonId && (await AddonManager.getAddonByID(addonId));
    if (!addon) {
      return;
    }

    this.openAddonsMgr("addons://detail/" + encodeURIComponent(addon.id));
  },

  /**
   * Open about:addons page by given view id.
   *
   * @param {string} aView
   *                 View id of page that will open.
   *                 e.g. "addons://discover/"
   * @param {object} options
   *        {
   *          selectTabByViewId: If true, if there is the tab opening page having
   *                             same view id, select the tab. Else if the current
   *                             page is blank, load on it. Otherwise, open a new
   *                             tab, then load on it.
   *                             If false, if there is the tab opening
   *                             about:addoons page, select the tab and load page
   *                             for view id on it. Otherwise, leave the loading
   *                             behavior to switchToTabHavingURI().
   *                             If no options, handles as false.
   *        }
   * @returns {Promise} When the Promise resolves, returns window object loaded the
   *                    view id.
   */
  openAddonsMgr(aView, { selectTabByViewId = false } = {}) {
    return new Promise(resolve => {
      let emWindow;
      let browserWindow;

      const receivePong = function (aSubject) {
        const browserWin = aSubject.browsingContext.topChromeWindow;
        if (!emWindow || browserWin == window /* favor the current window */) {
          if (
            selectTabByViewId &&
            aSubject.gViewController.currentViewId !== aView
          ) {
            return;
          }

          emWindow = aSubject;
          browserWindow = browserWin;
        }
      };
      Services.obs.addObserver(receivePong, "EM-pong");
      Services.obs.notifyObservers(null, "EM-ping");
      Services.obs.removeObserver(receivePong, "EM-pong");

      if (emWindow) {
        if (aView && !selectTabByViewId) {
          emWindow.loadView(aView);
        }
        let tab = browserWindow.gBrowser.getTabForBrowser(
          emWindow.docShell.chromeEventHandler
        );
        browserWindow.gBrowser.selectedTab = tab;
        emWindow.focus();
        resolve(emWindow);
        return;
      }

      if (selectTabByViewId) {
        const target = isBlankPageURL(gBrowser.currentURI.spec)
          ? "current"
          : "tab";
        openTrustedLinkIn("about:addons", target);
      } else {
        // This must be a new load, else the ping/pong would have
        // found the window above.
        switchToTabHavingURI("about:addons", true);
      }

      Services.obs.addObserver(function observer(aSubject, aTopic) {
        Services.obs.removeObserver(observer, aTopic);
        if (aView) {
          aSubject.loadView(aView);
        }
        aSubject.focus();
        resolve(aSubject);
      }, "EM-loaded");
    });
  },
};

// We must declare `gUnifiedExtensions` using `var` below to avoid a
// "redeclaration" syntax error.
var gUnifiedExtensions = {
  _initialized: false,
  // buttonAlwaysVisible: true, -- based on pref, declared later.
  _buttonShownBeforeButtonOpen: null,
  _buttonBarHasMouse: false,

  // We use a `<deck>` in the extension items to show/hide messages below each
  // extension name. We have a default message for origin controls, and
  // optionally a second message shown on hover, which describes the action
  // (when clicking on the action button). We have another message shown when
  // the menu button is hovered/focused. The constants below define the indexes
  // of each message in the `<deck>`.
  MESSAGE_DECK_INDEX_DEFAULT: 0,
  MESSAGE_DECK_INDEX_HOVER: 1,
  MESSAGE_DECK_INDEX_MENU_HOVER: 2,

  // Mutually exclusive classes set on the extensions panel empty state's <img>,
  // selecting which illustration to show when the extensions panel is empty
  // (see onPanelViewShowing, _updateEmptyStateBox and unified-extensions.css).
  EMPTY_STATE_ILLUSTRATION_CLASS: "extensions-emptypanel-state",
  EMPTY_STATE_ILLUSTRATION_ONBOARDING_CLASS: "extensions-onboardingpanel-state",

  init() {
    if (this._initialized) {
      return;
    }

    // Button is hidden by default, declared in navigator-toolbox.inc.xhtml.
    this._button = document.getElementById("unified-extensions-button");
    this._navbar = document.getElementById("nav-bar");
    this.updateButtonVisibility();
    this._buttonAttrObs = new MutationObserver(() => this.onButtonOpenChange());
    this._buttonAttrObs.observe(this._button, { attributeFilter: ["open"] });
    this._button.addEventListener("PopupNotificationsBeforeAnchor", this);
    this._updateButtonBarListeners();

    gBrowser.addTabsProgressListener(this);
    window.addEventListener("TabSelect", () => this.updateAttention());
    window.addEventListener("toolbarvisibilitychange", this);

    this.permListener = () => this.updateAttention();
    lazy.ExtensionPermissions.addListener(this.permListener);

    this.onAppMenuShowing = this.onAppMenuShowing.bind(this);
    PanelUI.mainView.addEventListener("ViewShowing", this.onAppMenuShowing);
    gNavToolbox.addEventListener("customizationstarting", this);
    gNavToolbox.addEventListener("aftercustomization", this);
    CustomizableUI.addListener(this);
    AddonManager.addManagerListener(this);

    Glean.extensionsButton.prefersHiddenButton.set(!this.buttonAlwaysVisible);

    this._initialized = true;
  },

  uninit() {
    if (!this._initialized) {
      return;
    }

    this._buttonAttrObs.disconnect();
    this._button.removeEventListener("PopupNotificationsBeforeAnchor", this);

    window.removeEventListener("toolbarvisibilitychange", this);

    lazy.ExtensionPermissions.removeListener(this.permListener);
    this.permListener = null;

    PanelUI.mainView.removeEventListener("ViewShowing", this.onAppMenuShowing);
    gNavToolbox.removeEventListener("customizationstarting", this);
    gNavToolbox.removeEventListener("aftercustomization", this);
    CustomizableUI.removeListener(this);
    AddonManager.removeManagerListener(this);
  },

  _updateButtonBarListeners() {
    // Called from init() and when the buttonAlwaysVisible flag changes.
    //
    // We don't expect the user to be interacting with the Extensions Button or
    // the navbar when the buttonAlwaysVisible flag changes. Still, we reset
    // the _buttonBarHasMouse flag to false to make sure that the button can be
    // hidden eventually if there are no other triggers:
    // - on registration, we don't know whether the mouse is on the navbar.
    // - after unregistration, the flag is no longer maintained, and false is a
    //   safe default value.
    this._buttonBarHasMouse = false;
    // We need mouse listeners on _navbar to maintain _buttonBarHasMouse,
    // but only if the button is conditionally visible/hidden.
    if (this.buttonAlwaysVisible) {
      this._navbar.removeEventListener("mouseover", this);
      this._navbar.removeEventListener("mouseout", this);
    } else {
      this._navbar.addEventListener("mouseover", this);
      this._navbar.addEventListener("mouseout", this);
    }
  },

  onBlocklistAttentionUpdated() {
    this.updateAttention();
  },

  onAppMenuShowing() {
    document.getElementById("appMenu-extensions-themes-button").hidden =
      !this.buttonAlwaysVisible;
    document.getElementById("appMenu-unified-extensions-button").hidden =
      this.buttonAlwaysVisible;
  },

  onLocationChange(browser, webProgress, _request, _uri, flags) {
    // Only update on top-level cross-document navigations in the selected tab.
    if (
      webProgress.isTopLevel &&
      browser === gBrowser.selectedBrowser &&
      !(flags & Ci.nsIWebProgressListener.LOCATION_CHANGE_SAME_DOCUMENT)
    ) {
      this.updateAttention();
    }
  },

  updateButtonVisibility() {
    // TODO: Bug 1778684 - Auto-hide button when there is no active extension.
    let shouldShowButton =
      this.buttonAlwaysVisible ||
      // If anything is anchored to the button, keep it visible.
      this._button.open ||
      // Button will be open soon - see ensureButtonShownBeforeAttachingPanel.
      this._buttonShownBeforeButtonOpen ||
      // Items in the toolbar shift when the button hides. To prevent the user
      // from clicking on something different than they intended, never hide an
      // already-visible button while the mouse is still in the toolbar.
      (!this.button.hidden && this._buttonBarHasMouse) ||
      // Attention dot - see comment at buttonIgnoresAttention.
      (!this.buttonIgnoresAttention && this.button.hasAttribute("attention")) ||
      // Always show when customizing, because even if the button should mostly
      // be hidden, the user should be able to specify the desired location for
      // cases where the button is forcibly shown.
      CustomizationHandler.isCustomizing();

    if (shouldShowButton) {
      this._button.hidden = false;
      this._navbar.setAttribute("unifiedextensionsbuttonshown", true);
    } else {
      this._button.hidden = true;
      this._navbar.removeAttribute("unifiedextensionsbuttonshown");
    }
  },

  ensureButtonShownBeforeAttachingPanel(panel) {
    if (!this.buttonAlwaysVisible && !this._button.open) {
      // When the panel is anchored to the button, its "open" attribute will be
      // set, which visually renders as a "button pressed". Until we get there,
      // we need to make sure that the button is visible so that it can serve
      // as anchor.
      this._buttonShownBeforeButtonOpen = panel;
      this.updateButtonVisibility();
    }
  },

  onButtonOpenChange() {
    if (this._button.open) {
      this._buttonShownBeforeButtonOpen = false;
    }
    if (!this.buttonAlwaysVisible && !this._button.open) {
      this.updateButtonVisibility();
    }
  },

  // Update the attention indicator for the whole unified extensions button.
  updateAttention() {
    let permissionsAttention = false;
    let quarantinedAttention = false;
    let blocklistAttention = AddonManager.shouldShowBlocklistAttention();

    // Computing the OriginControls state for all active extensions is potentially
    // more expensive, and so we don't compute it if we have already determined that
    // there is a blocklist attention to be shown.
    if (!blocklistAttention) {
      for (let policy of this.getActivePolicies()) {
        let widget = this.browserActionFor(policy)?.widget;

        // Only show for extensions which are not already visible in the toolbar.
        if (!widget || widget.areaType !== CustomizableUI.TYPE_TOOLBAR) {
          if (lazy.OriginControls.getAttentionState(policy, window).attention) {
            permissionsAttention = true;
            break;
          }
        }
      }

      // If the domain is quarantined and we have extensions not allowed, we'll
      // show a notification in the panel so we want to let the user know about
      // it.
      quarantinedAttention = this._shouldShowQuarantinedNotification();
    }

    this.button.toggleAttribute(
      "attention",
      quarantinedAttention || permissionsAttention || blocklistAttention
    );
    let msgId = permissionsAttention
      ? "unified-extensions-button-permissions-needed"
      : "unified-extensions-button";
    // Quarantined state takes precedence over anything else.
    if (quarantinedAttention) {
      msgId = "unified-extensions-button-quarantined";
    }
    // blocklistAttention state takes precedence over the other ones
    // because it is dismissible and, once dismissed, the tooltip will
    // show one of the other messages if appropriate.
    if (blocklistAttention) {
      msgId = "unified-extensions-button-blocklisted";
    }
    this.button.ownerDocument.l10n.setAttributes(this.button, msgId);
    if (!this.buttonAlwaysVisible && !this.buttonIgnoresAttention) {
      if (blocklistAttention) {
        this.recordButtonTelemetry("attention_blocklist");
      } else if (permissionsAttention || quarantinedAttention) {
        this.recordButtonTelemetry("attention_permission_denied");
      }
      this.updateButtonVisibility();
    }
  },

  // Get the anchor to use with PopupNotifications.show(). If you add a new use
  // of this method, make sure to update gXPInstallObserver.NOTIFICATION_IDS!
  // If the new ID is not added in NOTIFICATION_IDS, consider handling the case
  // in the "PopupNotificationsBeforeAnchor" handler elsewhere in this file.
  getPopupAnchorID(aBrowser, aWindow) {
    const anchorID = "unified-extensions-button";
    const attr = anchorID + "popupnotificationanchor";

    if (!aBrowser[attr]) {
      // A hacky way of setting the popup anchor outside the usual url bar
      // icon box, similar to how it was done for CFR.
      // See: https://searchfox.org/mozilla-central/rev/c5c002f81f08a73e04868e0c2bf0eb113f200b03/toolkit/modules/PopupNotifications.sys.mjs#40
      aBrowser[attr] = aWindow.document.getElementById(
        anchorID
        // Anchor on the toolbar icon to position the popup right below the
        // button.
      ).firstElementChild;
    }

    return anchorID;
  },

  get button() {
    return this._button;
  },

  /**
   * Gets a list of active WebExtensionPolicy instances of type "extension",
   * excluding hidden extensions, available to this window.
   *
   * @param {boolean} skipPBMCheck When false (the default), the result
   *                  excludes extensions that cannot access the current window
   *                  due to the window being a private browsing window that
   *                  the extension is not allowed to access.
   * @returns {Array<WebExtensionPolicy>} An array of active policies.
   */
  getActivePolicies(skipPBMCheck = false) {
    let policies = WebExtensionPolicy.getActiveExtensions();
    policies = policies.filter(policy => {
      let { extension } = policy;
      if (extension?.type !== "extension") {
        // extension can only be null due to bugs (bug 1642012).
        // Exclude non-extension types such as themes, dictionaries, etc.
        return false;
      }

      // Ignore hidden and extensions that cannot access the current window
      // (because of PB mode when we are in a private window), since users
      // cannot do anything with those extensions anyway.
      if (
        extension.isHidden ||
        // NOTE: policy.canAccessWindow() sounds generic, but it really only
        // enforces private browsing access.
        (!skipPBMCheck && !policy.canAccessWindow(window))
      ) {
        return false;
      }

      return true;
    });

    return policies;
  },

  /**
   * Returns true when there are active extensions listed/shown in the unified
   * extensions panel, and false otherwise (e.g. when extensions are pinned in
   * the toolbar OR there are 0 active extensions).
   *
   * @param {Array<WebExtensionPolicy> [policies] The list of extensions to
   *   evaluate. Defaults to the active extensions with access to this window
   *   (see getActivePolicies).
   * @returns {boolean} Whether there are extensions listed in the panel.
   */
  hasExtensionsInPanel(policies = this.getActivePolicies()) {
    return policies.some(policy => {
      let widget = this.browserActionFor(policy)?.widget;
      return (
        !widget ||
        widget.areaType !== CustomizableUI.TYPE_TOOLBAR ||
        widget.forWindow(window).overflowed
      );
    });
  },

  isPrivateWindowMissingExtensionsWithoutPBMAccess() {
    if (!PrivateBrowsingUtils.isWindowPrivate(window)) {
      return false;
    }
    const policies = this.getActivePolicies(/* skipPBMCheck */ true);
    return policies.some(p => !p.privateBrowsingAllowed);
  },

  /**
   * Returns whether there is any active extension without private browsing
   * access, for which the user can toggle the "Run in Private Windows" option.
   * This complements the isPrivateWindowMissingExtensionsWithoutPBMAccess()
   * method, by distinguishing cases where the user can enable any extension
   * in the private window, vs cases where the user cannot.
   *
   * @returns {Promise<boolean>} Whether there is any "Run in Private Windows"
   *                             option that is Off and can be set to On.
   */
  async isAtLeastOneExtensionWithPBMOptIn() {
    const addons = await AddonManager.getAddonsByTypes(["extension"]);
    return addons.some(addon => {
      if (
        // We only care about extensions shown in the panel and about:addons.
        addon.hidden ||
        // We only care about extensions whose PBM access can be toggled.
        !(
          addon.permissions &
          lazy.AddonManager.PERM_CAN_CHANGE_PRIVATEBROWSING_ACCESS
        )
      ) {
        return false;
      }
      const policy = WebExtensionPolicy.getByID(addon.id);
      // policy can be null if the extension is not active.
      return policy && !policy.privateBrowsingAllowed;
    });
  },

  async getDisabledExtensionsInfo() {
    let addons = await AddonManager.getAddonsByTypes(["extension"]);
    addons = addons.filter(a => !a.hidden && !a.isActive);
    const isAnyDisabled = !!addons.length;
    const isAnyEnableable = addons.some(
      a => a.permissions & lazy.AddonManager.PERM_CAN_ENABLE
    );
    return { isAnyDisabled, isAnyEnableable };
  },

  handleEvent(event) {
    switch (event.type) {
      case "ViewShowing":
        this.onPanelViewShowing(event.target);
        break;

      case "ViewHiding":
        this.onPanelViewHiding(event.target);
        break;

      case "PopupNotificationsBeforeAnchor":
        {
          const popupnotification = PopupNotifications.panel.firstElementChild;
          const popupid = popupnotification?.getAttribute("popupid");
          if (popupid === "addon-webext-permissions") {
            // "addon-webext-permissions" is also in NOTIFICATION_IDS, but to
            // distinguish it from other cases, give it a separate reason.
            this.recordButtonTelemetry("extension_permission_prompt");
          } else if (gXPInstallObserver.NOTIFICATION_IDS.includes(popupid)) {
            this.recordButtonTelemetry("addon_install_doorhanger");
          } else {
            console.error(`Unrecognized notification ID: ${popupid}`);
          }
          this.ensureButtonShownBeforeAttachingPanel(PopupNotifications.panel);
        }
        break;

      case "mouseover":
        this._buttonBarHasMouse = true;
        break;

      case "mouseout":
        if (
          this._buttonBarHasMouse &&
          !this._navbar.contains(event.relatedTarget)
        ) {
          this._buttonBarHasMouse = false;
          this.updateButtonVisibility();
        }
        break;

      case "customizationstarting":
        this.panel.hidePopup();
        this.recordButtonTelemetry("customize");
        this.updateButtonVisibility();
        break;

      case "aftercustomization":
        this.updateButtonVisibility();
        break;

      case "toolbarvisibilitychange":
        this.onToolbarVisibilityChange(event.target.id, event.detail.visible);
        break;
    }
  },

  onPanelViewShowing(panelview) {
    const policies = this.getActivePolicies();

    // Only add extensions that do not have a browser action in this list since
    // the extensions with browser action have CUI widgets and will appear in
    // the panel (or toolbar) via the CUI mechanism.
    const policiesForList = policies.filter(
      p => !p.extension.hasBrowserActionUI
    );
    policiesForList.sort((a, b) => a.name.localeCompare(b.name));

    const list = panelview.querySelector(".unified-extensions-list");
    for (const policy of policiesForList) {
      const item = document.createElement("unified-extensions-item");
      item.setExtension(policy.extension);
      list.appendChild(item);
    }

    if (this.hasExtensionsInPanel(policies)) {
      // Any of the extension lists are non-empty.
      this._updateEmptyStateBox({ panelview, hidden: true });
    } else if (this.isPrivateWindowMissingExtensionsWithoutPBMAccess()) {
      this._updateEmptyStateBox({
        panelview,
        hidden: false,
        headingL10nId:
          "unified-extensions-empty-reason-private-browsing-not-allowed",
        descriptionL10nId: "unified-extensions-empty-content-explain-enable2",
      });
      this.isAtLeastOneExtensionWithPBMOptIn().then(result => {
        if (!result) {
          this._updateEmptyStateBox({
            panelview,
            hidden: false,
            headingL10nId:
              "unified-extensions-empty-reason-private-browsing-not-allowed",
            // The "enable" message is somewhat misleading when the user
            // cannot enable the extension, show a generic message instead
            // (bug 1992179).
            descriptionL10nId:
              "unified-extensions-empty-content-explain-manage2",
          });
        }
      });
    } else {
      this._updateEmptyStateBox({ panelview, hidden: true });
      this.getDisabledExtensionsInfo().then(disabledExtensionsInfo => {
        if (disabledExtensionsInfo.isAnyDisabled) {
          this._updateEmptyStateBox({
            panelview,
            hidden: false,
            headingL10nId:
              "unified-extensions-empty-reason-extension-not-enabled",
            descriptionL10nId: disabledExtensionsInfo.isAnyEnableable
              ? "unified-extensions-empty-content-explain-enable2"
              : "unified-extensions-empty-content-explain-manage2",
          });
        } else if (!policies.length) {
          this._updateEmptyStateBox({
            panelview,
            hidden: false,
            onboarding: true,
            headingL10nId:
              "unified-extensions-empty-reason-zero-extensions-onboarding2",
            descriptionL10nId:
              "unified-extensions-empty-content-explain-extensions-onboarding2",
          });

          // Without the discovery pane there is nowhere to send the user, so
          // keep the "Manage Extensions" button instead.
          if (
            !Services.prefs.getBoolPref("extensions.getAddons.showPane", true)
          ) {
            return;
          }

          // Replace the "Manage Extensions" button with "Discover Extensions".
          // We add the "Discover Extensions" button, and "Manage Extensions"
          // button (#unified-extensions-manage-extensions) is hidden by CSS.
          const discoverButton = this._createDiscoverButton(panelview);

          const manageExtensionsButton = panelview.querySelector(
            "#unified-extensions-manage-extensions"
          );
          // Insert before toolbarseparator, to make it easier to hide the
          // toolbarseparator and manageExtensionsButton with CSS.
          manageExtensionsButton.previousElementSibling.before(discoverButton);
        }
      });
    }

    const container = panelview.querySelector(
      "#unified-extensions-messages-container"
    );

    if (Services.appinfo.inSafeMode) {
      this._messageBarSafemode ??= this._makeMessageBar({
        messageBarFluentId: "unified-extensions-notice-safe-mode",
        supportPage: "diagnose-firefox-issues-using-troubleshoot-mode",
        type: "info",
      });
      container.prepend(this._messageBarSafemode);
    } // No "else" case; inSafeMode flag is fixed at browser startup.

    if (this.blocklistAttentionInfo?.shouldShow) {
      this._messageBarBlocklist = this._createBlocklistMessageBar(container);
    } else {
      this._messageBarBlocklist?.remove();
      this._messageBarBlocklist = null;
    }

    const shouldShowQuarantinedNotification =
      this._shouldShowQuarantinedNotification();
    if (shouldShowQuarantinedNotification) {
      if (!this._messageBarQuarantinedDomain) {
        this._messageBarQuarantinedDomain = this._makeMessageBar({
          messageBarFluentId:
            "unified-extensions-mb-quarantined-domain-message-3",
          supportPage: "quarantined-domains",
          supportPageFluentId:
            "unified-extensions-mb-quarantined-domain-learn-more",
          dismissible: false,
        });
        this._messageBarQuarantinedDomain
          .querySelector("a")
          .addEventListener("click", () => {
            this.togglePanel();
          });
      }

      container.appendChild(this._messageBarQuarantinedDomain);
    } else if (
      !shouldShowQuarantinedNotification &&
      this._messageBarQuarantinedDomain &&
      container.contains(this._messageBarQuarantinedDomain)
    ) {
      container.removeChild(this._messageBarQuarantinedDomain);
      this._messageBarQuarantinedDomain = null;
    }
  },

  onPanelViewHiding(panelview) {
    if (window.closed) {
      return;
    }
    const list = panelview.querySelector(".unified-extensions-list");
    while (list.lastChild) {
      list.lastChild.remove();
    }
    panelview
      .querySelector("#unified-extensions-discover-extensions")
      ?.remove();

    // If temporary access was granted, (maybe) clear attention indicator.
    requestAnimationFrame(() => this.updateAttention());
  },

  onToolbarVisibilityChange(toolbarId, isVisible) {
    // A list of extension widget IDs (possibly empty).
    let widgetIDs;

    try {
      widgetIDs = CustomizableUI.getWidgetIdsInArea(toolbarId).filter(
        CustomizableUI.isWebExtensionWidget
      );
    } catch {
      // Do nothing if the area does not exist for some reason.
      return;
    }

    // The list of overflowed extensions in the extensions panel.
    const overflowedExtensionsList = this.panel.querySelector(
      "#overflowed-extensions-list"
    );

    // We are going to move all the extension widgets via DOM manipulation
    // *only* so that it looks like these widgets have moved (and users will
    // see that) but CUI still thinks the widgets haven't been moved.
    //
    // We can move the extension widgets either from the toolbar to the
    // extensions panel OR the other way around (when the toolbar becomes
    // visible again).
    for (const widgetID of widgetIDs) {
      const widget = CustomizableUI.getWidget(widgetID);
      if (!widget) {
        continue;
      }

      if (isVisible) {
        this._maybeMoveWidgetNodeBack(widget.id);
      } else {
        const { node } = widget.forWindow(window);
        // Artificially overflow the extension widget in the extensions panel
        // when the toolbar is hidden.
        node.setAttribute("overflowedItem", true);
        node.setAttribute("artificallyOverflowed", true);
        // This attribute forces browser action popups to be anchored to the
        // extensions button.
        node.setAttribute("cui-anchorid", "unified-extensions-button");
        overflowedExtensionsList.appendChild(node);

        this._updateWidgetClassName(widgetID, /* inPanel */ true);
      }
    }
  },

  _maybeMoveWidgetNodeBack(widgetID) {
    const widget = CustomizableUI.getWidget(widgetID);
    if (!widget) {
      return;
    }

    // We only want to move back widget nodes that have been manually moved
    // previously via `onToolbarVisibilityChange()`.
    const { node } = widget.forWindow(window);
    if (!node.hasAttribute("artificallyOverflowed")) {
      return;
    }

    const { area, position } = CustomizableUI.getPlacementOfWidget(widgetID);

    // This is where we are going to re-insert the extension widgets (DOM
    // nodes) but we need to account for some hidden DOM nodes already present
    // in this container when determining where to put the nodes back.
    const container = CustomizableUI.getCustomizationTarget(
      document.getElementById(area)
    );

    let moved = false;
    let currentPosition = 0;

    for (const child of container.childNodes) {
      const isSkipToolbarset = child.getAttribute("skipintoolbarset") == "true";
      if (isSkipToolbarset && child !== container.lastChild) {
        continue;
      }

      if (currentPosition === position) {
        child.before(node);
        moved = true;
        break;
      }

      if (child === container.lastChild) {
        child.after(node);
        moved = true;
        break;
      }

      currentPosition++;
    }

    if (moved) {
      // Remove the attribute set when we artificially overflow the widget.
      node.removeAttribute("overflowedItem");
      node.removeAttribute("artificallyOverflowed");
      node.removeAttribute("cui-anchorid");

      this._updateWidgetClassName(widgetID, /* inPanel */ false);
    }
  },

  _panel: null,
  get panel() {
    // Lazy load the unified-extensions-panel panel the first time we need to
    // display it.
    if (!this._panel) {
      let template = document.getElementById(
        "unified-extensions-panel-template"
      );
      template.replaceWith(template.content);
      this._panel = document.getElementById("unified-extensions-panel");
      let customizationArea = this._panel.querySelector(
        "#unified-extensions-area"
      );
      CustomizableUI.registerPanelNode(
        customizationArea,
        CustomizableUI.AREA_ADDONS
      );
      CustomizableUI.addPanelCloseListeners(this._panel);

      this._panel
        .querySelector("#unified-extensions-manage-extensions")
        .addEventListener("command", () => {
          BrowserAddonUI.openAddonsMgr("addons://list/extension");
        });

      // Lazy-load the l10n strings. Those strings are used for the CUI and
      // non-CUI extensions in the unified extensions panel.
      document
        .getElementById("unified-extensions-context-menu")
        .querySelectorAll("[data-lazy-l10n-id]")
        .forEach(el => {
          el.setAttribute("data-l10n-id", el.getAttribute("data-lazy-l10n-id"));
          el.removeAttribute("data-lazy-l10n-id");
        });
    }
    return this._panel;
  },

  // `aEvent` and `reason` are optional. If `reason` is specified, it should be
  // a valid argument to gUnifiedExtensions.recordButtonTelemetry().
  async togglePanel(aEvent, reason) {
    if (!CustomizationHandler.isCustomizing()) {
      if (aEvent) {
        if (
          // On MacOS, ctrl-click will send a context menu event from the
          // widget, so we don't want to bring up the panel when ctrl key is
          // pressed.
          (aEvent.type == "mousedown" &&
            (aEvent.button !== 0 ||
              (AppConstants.platform === "macosx" && aEvent.ctrlKey))) ||
          (aEvent.type === "keypress" &&
            aEvent.charCode !== KeyEvent.DOM_VK_SPACE &&
            aEvent.keyCode !== KeyEvent.DOM_VK_RETURN)
        ) {
          return;
        }

        // The button should directly open `about:addons` when the user does not
        // have any active extensions listed in the unified extensions panel,
        // and no alternative content is available for display in the panel.
        const policies = this.getActivePolicies();
        if (
          policies.length &&
          !this.hasExtensionsInPanel(policies) &&
          !this.isPrivateWindowMissingExtensionsWithoutPBMAccess() &&
          !(await this.getDisabledExtensionsInfo()).isAnyDisabled
        ) {
          // This may happen if the user has pinned all of their extensions.
          // In that case, the extensions panel is empty.
          await BrowserAddonUI.openAddonsMgr("addons://list/extension");
          return;
        }
      }

      this.blocklistAttentionInfo =
        await AddonManager.getBlocklistAttentionInfo();

      let panel = this.panel;

      if (!this._listView) {
        this._listView = PanelMultiView.getViewNode(
          document,
          "unified-extensions-view"
        );
        this._listView.addEventListener("ViewShowing", this);
        this._listView.addEventListener("ViewHiding", this);
      }

      if (this._button.open) {
        PanelMultiView.hidePopup(panel);
        this._button.open = false;
      } else {
        // Overflow extensions placed in collapsed toolbars, if any.
        for (const toolbarId of CustomizableUI.getCollapsedToolbarIds(window)) {
          // We pass `false` because all these toolbars are collapsed.
          this.onToolbarVisibilityChange(toolbarId, /* isVisible */ false);
        }

        panel.hidden = false;
        this.recordButtonTelemetry(reason || "extensions_panel_showing");
        this.ensureButtonShownBeforeAttachingPanel(panel);
        PanelMultiView.openPopup(panel, this._button, {
          position: "bottomright topright",
          triggerEvent: aEvent,
        });
      }
    }

    // We always dispatch an event (useful for testing purposes).
    window.dispatchEvent(new CustomEvent("UnifiedExtensionsTogglePanel"));
  },

  async openPanel(event, reason) {
    if (this._button.open) {
      throw new Error("Tried to open panel whilst a panel was already open!");
    }
    if (CustomizationHandler.isCustomizing()) {
      throw new Error("Cannot open panel while in Customize mode!");
    }

    if (event?.sourceEvent?.target.id === "appMenu-unified-extensions-button") {
      Glean.extensionsButton.openViaAppMenu.record({
        is_extensions_panel_empty: !this.hasExtensionsInPanel(),
        is_extensions_button_visible: !this._button.hidden,
      });
    }

    await this.togglePanel(event, reason);
  },

  /**
   * @returns {boolean} Whether we are showing the Extensions Panel, or another
   * (browserAction) panel anchored to the extensions button.
   */
  isPanelOpen() {
    return this._button?.open ?? false;
  },

  updateContextMenu(menu, event) {
    // When the context menu is open, `onpopupshowing` is called when menu
    // items open sub-menus. We don't want to update the context menu in this
    // case.
    if (event.target.id !== "unified-extensions-context-menu") {
      return;
    }

    const id = this._getExtensionId(menu);
    const widgetId = this._getWidgetId(menu);
    const forBrowserAction = !!widgetId;

    const pinButton = menu.querySelector(
      ".unified-extensions-context-menu-pin-to-toolbar"
    );
    const removeButton = menu.querySelector(
      ".unified-extensions-context-menu-remove-extension"
    );
    const reportButton = menu.querySelector(
      ".unified-extensions-context-menu-report-extension"
    );
    const menuSeparator = menu.querySelector(
      ".unified-extensions-context-menu-management-separator"
    );
    const moveUp = menu.querySelector(
      ".unified-extensions-context-menu-move-widget-up"
    );
    const moveDown = menu.querySelector(
      ".unified-extensions-context-menu-move-widget-down"
    );

    for (const element of [menuSeparator, pinButton, moveUp, moveDown]) {
      element.hidden = !forBrowserAction;
    }

    reportButton.hidden = !gAddonAbuseReportEnabled;
    // We use this syntax instead of async/await to not block this method that
    // updates the context menu. This avoids the context menu to be out of sync
    // on macOS.
    AddonManager.getAddonByID(id).then(addon => {
      removeButton.disabled = !(
        addon.permissions & AddonManager.PERM_CAN_UNINSTALL
      );
    });

    if (forBrowserAction) {
      let area = CustomizableUI.getPlacementOfWidget(widgetId).area;
      let inToolbar = area != CustomizableUI.AREA_ADDONS;
      pinButton.toggleAttribute("checked", inToolbar);

      const placement = CustomizableUI.getPlacementOfWidget(widgetId);
      const notInPanel = placement?.area !== CustomizableUI.AREA_ADDONS;
      // We rely on the DOM nodes because CUI widgets will always exist but
      // not necessarily with DOM nodes created depending on the window. For
      // example, in PB mode, not all extensions will be listed in the panel
      // but the CUI widgets may be all created.
      if (
        notInPanel ||
        document.querySelector("#unified-extensions-area > :first-child")
          ?.id === widgetId
      ) {
        moveUp.hidden = true;
      }

      if (
        notInPanel ||
        document.querySelector("#unified-extensions-area > :last-child")?.id ===
          widgetId
      ) {
        moveDown.hidden = true;
      }
    }

    ExtensionsUI.originControlsMenu(menu, id);

    const browserAction = this.browserActionFor(WebExtensionPolicy.getByID(id));
    if (browserAction) {
      browserAction.updateContextMenu(menu);
    }
  },

  // This is registered on the top-level unified extensions context menu.
  onContextMenuCommand(menu, event) {
    // Do not close the extensions panel automatically when we move extension
    // widgets.
    const { classList } = event.target;
    if (
      classList.contains("unified-extensions-context-menu-move-widget-up") ||
      classList.contains("unified-extensions-context-menu-move-widget-down")
    ) {
      return;
    }

    this.togglePanel();
  },

  browserActionFor(policy) {
    // Ideally, we wouldn't do that because `browserActionFor()` will only be
    // defined in `global` when at least one extension has required loading the
    // `ext-browserAction` code.
    let method = lazy.ExtensionParent.apiManager.global.browserActionFor;
    return method?.(policy?.extension);
  },

  async manageExtension(menu) {
    const id = this._getExtensionId(menu);

    await BrowserAddonUI.manageAddon(id, "unifiedExtensions");
  },

  async removeExtension(menu) {
    const id = this._getExtensionId(menu);

    await BrowserAddonUI.removeAddon(id, "unifiedExtensions");
  },

  async reportExtension(menu) {
    const id = this._getExtensionId(menu);

    await BrowserAddonUI.reportAddon(id, "unified_context_menu");
  },

  _getExtensionId(menu) {
    const { triggerNode } = menu;
    return triggerNode
      .closest(".unified-extensions-item")
      ?.querySelector("toolbarbutton")?.dataset.extensionid;
  },

  _getWidgetId(menu) {
    const { triggerNode } = menu;
    return triggerNode.closest(".unified-extensions-item")?.id;
  },

  async onPinToToolbarChange(menu, event) {
    let shouldPinToToolbar = event.target.hasAttribute("checked");
    // Revert the checkbox back to its original state. This is because the
    // addon context menu handlers are asynchronous, and there seems to be
    // a race where the checkbox state won't get set in time to show the
    // right state. So we err on the side of caution, and presume that future
    // attempts to open this context menu on an extension button will show
    // the same checked state that we started in.
    event.target.toggleAttribute("checked", !shouldPinToToolbar);

    let widgetId = this._getWidgetId(menu);
    if (!widgetId) {
      return;
    }

    // We artificially overflow extension widgets that are placed in collapsed
    // toolbars and CUI does not know about it. For end users, these widgets
    // appear in the list of overflowed extensions in the panel. When we unpin
    // and then pin one of these extensions to the toolbar, we need to first
    // move the DOM node back to where it was (i.e.  in the collapsed toolbar)
    // so that CUI can retrieve the DOM node and do the pinning correctly.
    if (shouldPinToToolbar) {
      this._maybeMoveWidgetNodeBack(widgetId);
    }

    this.pinToToolbar(widgetId, shouldPinToToolbar);
  },

  pinToToolbar(widgetId, shouldPinToToolbar) {
    let newArea = shouldPinToToolbar
      ? CustomizableUI.AREA_NAVBAR
      : CustomizableUI.AREA_ADDONS;
    let newPosition = shouldPinToToolbar ? undefined : 0;

    CustomizableUI.addWidgetToArea(widgetId, newArea, newPosition);
    // addWidgetToArea() will trigger onWidgetAdded or onWidgetMoved as needed,
    // and our handlers will call updateAttention() as needed.
  },

  async moveWidget(menu, direction) {
    // We'll move the widgets based on the DOM node positions. This is because
    // in PB mode (for example), we might not have the same extensions listed
    // in the panel but CUI does not know that. As far as CUI is concerned, all
    // extensions will likely have widgets.
    const node = menu.triggerNode.closest(".unified-extensions-item");

    // Find the element that is before or after the current widget/node to
    // move. `element` might be `null`, e.g. if the current node is the first
    // one listed in the panel (though it shouldn't be possible to call this
    // method in this case).
    let element;
    if (direction === "up" && node.previousElementSibling) {
      element = node.previousElementSibling;
    } else if (direction === "down" && node.nextElementSibling) {
      element = node.nextElementSibling;
    }

    // Now we need to retrieve the position of the CUI placement.
    const placement = CustomizableUI.getPlacementOfWidget(element?.id);
    if (placement) {
      let newPosition = placement.position;
      // That, I am not sure why this is required but it looks like we need to
      // always add one to the current position if we want to move a widget
      // down in the list.
      if (direction === "down") {
        newPosition += 1;
      }

      CustomizableUI.moveWidgetWithinArea(node.id, newPosition);
    }
  },

  onWidgetAdded(aWidgetId, aArea) {
    if (CustomizableUI.isWebExtensionWidget(aWidgetId)) {
      this.updateAttention();
    }

    // When we pin a widget to the toolbar from a narrow window, the widget
    // will be overflowed directly. In this case, we do not want to change the
    // class name since it is going to be changed by `onWidgetOverflow()`
    // below.
    if (CustomizableUI.getWidget(aWidgetId)?.forWindow(window)?.overflowed) {
      return;
    }

    const inPanel =
      CustomizableUI.getAreaType(aArea) !== CustomizableUI.TYPE_TOOLBAR;

    this._updateWidgetClassName(aWidgetId, inPanel);
  },

  onWidgetMoved(aWidgetId) {
    if (CustomizableUI.isWebExtensionWidget(aWidgetId)) {
      this.updateAttention();
    }
  },

  onWidgetOverflow(aNode) {
    // We register a CUI listener for each window so we make sure that we
    // handle the event for the right window here.
    if (window !== aNode.documentGlobal) {
      return;
    }

    this._updateWidgetClassName(aNode.getAttribute("widget-id"), true);
  },

  onWidgetUnderflow(aNode) {
    // We register a CUI listener for each window so we make sure that we
    // handle the event for the right window here.
    if (window !== aNode.documentGlobal) {
      return;
    }

    this._updateWidgetClassName(aNode.getAttribute("widget-id"), false);
  },

  onAreaNodeRegistered(aArea, aContainer) {
    // We register a CUI listener for each window so we make sure that we
    // handle the event for the right window here.
    if (window !== aContainer.documentGlobal) {
      return;
    }

    const inPanel =
      CustomizableUI.getAreaType(aArea) !== CustomizableUI.TYPE_TOOLBAR;

    for (const widgetId of CustomizableUI.getWidgetIdsInArea(aArea)) {
      this._updateWidgetClassName(widgetId, inPanel);
    }
  },

  // This internal method is used to change some CSS classnames on the action
  // and menu buttons of an extension (CUI) widget. When the widget is placed
  // in the panel, the action and menu buttons should have the `.subviewbutton`
  // class and not the `.toolbarbutton-1` one. When NOT placed in the panel,
  // it is the other way around.
  _updateWidgetClassName(aWidgetId, inPanel) {
    if (!CustomizableUI.isWebExtensionWidget(aWidgetId)) {
      return;
    }

    const node = CustomizableUI.getWidget(aWidgetId)?.forWindow(window)?.node;
    const actionButton = node?.querySelector(
      ".unified-extensions-item-action-button"
    );
    if (actionButton) {
      actionButton.classList.toggle("subviewbutton", inPanel);
      actionButton.classList.toggle("subviewbutton-iconic", inPanel);
      actionButton.classList.toggle("toolbarbutton-1", !inPanel);
    }
    const menuButton = node?.querySelector(
      ".unified-extensions-item-menu-button"
    );
    if (menuButton) {
      menuButton.classList.toggle("subviewbutton", inPanel);
      menuButton.classList.toggle("subviewbutton-iconic", inPanel);
      menuButton.classList.toggle("toolbarbutton-1", !inPanel);
    }
  },

  _createBlocklistMessageBar(container) {
    if (!this.blocklistAttentionInfo) {
      return null;
    }

    const { addons, extensionsCount, hasHardBlocked } =
      this.blocklistAttentionInfo;
    const type = hasHardBlocked ? "error" : "warning";

    let messageBarFluentId;
    let extensionName;
    if (extensionsCount === 1) {
      extensionName = addons[0].name;
      messageBarFluentId = hasHardBlocked
        ? "unified-extensions-mb-blocklist-error-single"
        : "unified-extensions-mb-blocklist-warning-single2";
    } else {
      messageBarFluentId = hasHardBlocked
        ? "unified-extensions-mb-blocklist-error-multiple"
        : "unified-extensions-mb-blocklist-warning-multiple2";
    }

    const messageBarBlocklist = this._makeMessageBar({
      dismissible: true,
      linkToAboutAddons: true,
      messageBarFluentId,
      messageBarFluentArgs: {
        extensionsCount,
        extensionName,
      },
      type,
    });

    messageBarBlocklist.addEventListener(
      "message-bar:user-dismissed",
      () => {
        if (messageBarBlocklist === this._messageBarBlocklist) {
          this._messageBarBlocklist = null;
        }
        this.blocklistAttentionInfo?.dismiss();
      },
      { once: true }
    );

    if (
      this._messageBarBlocklist &&
      container.contains(this._messageBarBlocklist)
    ) {
      container.replaceChild(messageBarBlocklist, this._messageBarBlocklist);
    } else if (container.contains(this._messageBarQuarantinedDomain)) {
      container.insertBefore(
        messageBarBlocklist,
        this._messageBarQuarantinedDomain
      );
    } else {
      container.appendChild(messageBarBlocklist);
    }

    return messageBarBlocklist;
  },

  _makeMessageBar({
    dismissible = false,
    messageBarFluentId,
    messageBarFluentArgs,
    supportPage = null,
    supportPageFluentId,
    linkToAboutAddons = false,
    type = "warning",
  }) {
    const messageBar = document.createElement("moz-message-bar");
    messageBar.setAttribute("type", type);
    messageBar.classList.add("unified-extensions-message-bar");

    if (dismissible) {
      // NOTE: the moz-message-bar is currently expected to be called `dismissable`.
      messageBar.setAttribute("dismissable", dismissible);
    }

    if (linkToAboutAddons) {
      const linkToAboutAddonsEl = document.createElement("a");
      linkToAboutAddonsEl.setAttribute(
        "class",
        "unified-extensions-link-to-aboutaddons"
      );
      linkToAboutAddonsEl.setAttribute("slot", "support-link");
      linkToAboutAddonsEl.addEventListener("click", () => {
        BrowserAddonUI.openAddonsMgr("addons://list/extension");
        this.togglePanel();
      });
      document.l10n.setAttributes(
        linkToAboutAddonsEl,
        "unified-extensions-mb-about-addons-link"
      );
      messageBar.append(linkToAboutAddonsEl);
    }

    document.l10n.setAttributes(
      messageBar,
      messageBarFluentId,
      messageBarFluentArgs
    );

    if (supportPage) {
      const supportUrl = document.createElement("a", {
        is: "moz-support-link",
      });
      supportUrl.setAttribute("support-page", supportPage);
      if (supportPageFluentId) {
        document.l10n.setAttributes(supportUrl, supportPageFluentId);
      }
      supportUrl.setAttribute("slot", "support-link");

      messageBar.append(supportUrl);
    }

    return messageBar;
  },

  _updateEmptyStateBox({
    panelview,
    hidden,
    onboarding = false,
    headingL10nId,
    descriptionL10nId,
  }) {
    const emptyStateBox = panelview.querySelector(
      "#unified-extensions-empty-state"
    );

    if (!hidden) {
      document.l10n.setAttributes(
        emptyStateBox.querySelector("h2"),
        headingL10nId
      );
      document.l10n.setAttributes(
        emptyStateBox.querySelector("description"),
        descriptionL10nId
      );

      const img = emptyStateBox.querySelector("img");
      img.classList.toggle(
        this.EMPTY_STATE_ILLUSTRATION_ONBOARDING_CLASS,
        onboarding
      );
      img.classList.toggle(this.EMPTY_STATE_ILLUSTRATION_CLASS, !onboarding);
    }

    emptyStateBox.hidden = hidden;

    return emptyStateBox;
  },

  _createDiscoverButton() {
    const discoverButton = document.createElement("moz-button");
    discoverButton.id = "unified-extensions-discover-extensions";
    discoverButton.type = "primary";
    discoverButton.className = "subviewbutton panel-subview-footer-button";
    document.l10n.setAttributes(
      discoverButton,
      "unified-extensions-discover-extensions"
    );

    discoverButton.addEventListener("click", () => {
      BrowserAddonUI.openAddonsMgr("addons://list/discover");
      // The panel closes automatically when the `<moz-button>` is pressed since
      // the `closepanel` attribute was not set to `none`.
    });

    return discoverButton;
  },

  _shouldShowQuarantinedNotification() {
    const { currentURI, selectedTab } = window.gBrowser;
    // We should show the quarantined notification when the domain is in the
    // list of quarantined domains and we have at least one extension
    // quarantined. In addition, we check that we have extensions in the panel
    // until Bug 1778684 is resolved.
    return (
      WebExtensionPolicy.isQuarantinedURI(currentURI) &&
      this.hasExtensionsInPanel() &&
      this.getActivePolicies().some(
        policy => lazy.OriginControls.getState(policy, selectedTab).quarantined
      )
    );
  },

  // Records telemetry when the button is about to temporarily be shown,
  // provided that the button is hidden at the time of invocation.
  //
  // `reason` is one of the labels in extensions_button.temporarily_unhidden
  // in browser/components/extensions/metrics.yaml.
  //
  // This is usually immediately before a updateButtonVisibility() call,
  // sometimes a bit earlier (if the updateButtonVisibility() call is indirect).
  recordButtonTelemetry(reason) {
    if (!this.buttonAlwaysVisible && this._button.hidden) {
      Glean.extensionsButton.temporarilyUnhidden[reason].add();
    }
  },

  hideExtensionsButtonFromToolbar() {
    // All browser windows will observe this and call updateButtonVisibility().
    Services.prefs.setBoolPref(
      "extensions.unifiedExtensions.button.always_visible",
      false
    );
    ConfirmationHint.show(
      document.getElementById("PanelUI-menu-button"),
      "confirmation-hint-extensions-button-hidden"
    );
    Glean.extensionsButton.toggleVisibility.record({
      is_customizing: CustomizationHandler.isCustomizing(),
      is_extensions_panel_empty: !this.hasExtensionsInPanel(),
      // After setting the above pref to false, the button should hide
      // immediately. If this was not the case, then something caused the
      // button to be shown temporarily.
      is_temporarily_shown: !this._button.hidden,
      should_hide: true,
    });
  },

  showExtensionsButtonInToolbar() {
    let wasShownBefore = !this.buttonAlwaysVisible && !this._button.hidden;
    // All browser windows will observe this and call updateButtonVisibility().
    Services.prefs.setBoolPref(
      "extensions.unifiedExtensions.button.always_visible",
      true
    );
    Glean.extensionsButton.toggleVisibility.record({
      is_customizing: CustomizationHandler.isCustomizing(),
      is_extensions_panel_empty: !this.hasExtensionsInPanel(),
      is_temporarily_shown: wasShownBefore,
      should_hide: false,
    });
  },
};
XPCOMUtils.defineLazyPreferenceGetter(
  gUnifiedExtensions,
  "buttonAlwaysVisible",
  "extensions.unifiedExtensions.button.always_visible",
  true,
  (prefName, oldValue, newValue) => {
    if (gUnifiedExtensions._initialized) {
      gUnifiedExtensions._updateButtonBarListeners();
      gUnifiedExtensions.updateButtonVisibility();
      Glean.extensionsButton.prefersHiddenButton.set(!newValue);
    }
  }
);
// With button.always_visible is false, we still show the button in specific
// cases when needed. The user is always empowered to dismiss the specific
// trigger that causes the button to be shown. The attention dot is the
// exception, where the button cannot easily be hidden. Users who willingly
// want to ignore the attention dot can set this preference to keep the button
// hidden even if attention is requested.
XPCOMUtils.defineLazyPreferenceGetter(
  gUnifiedExtensions,
  "buttonIgnoresAttention",
  "extensions.unifiedExtensions.button.ignore_attention",
  false,
  () => {
    if (
      gUnifiedExtensions._initialized &&
      !gUnifiedExtensions.buttonAlwaysVisible
    ) {
      gUnifiedExtensions.updateButtonVisibility();
    }
  }
);
