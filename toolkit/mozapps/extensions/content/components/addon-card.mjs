/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */
/* globals windowRoot */

import { openAbuseReport } from "../abuse-reports.mjs";
import {
  AboutAddonsHTMLElement,
  attachUpdateHandler,
  checkForUpdate,
  detachUpdateHandler,
  getAddonMessageInfo,
  getOptionsType,
  getUpdateInstall,
  hasPermission,
  isAllowedInPrivateBrowsing,
  isInState,
  openOptionsInTab,
  shouldShowPermissionsPrompt,
  showPermissionsPrompt,
} from "../aboutaddons-utils.mjs";
import { gViewController } from "../view-controller.mjs";
import { shouldShowNativeThemeCheckbox } from "./native-theme-colors-checkbox.mjs";

const { AddonManager } = ChromeUtils.importESModule(
  "resource://gre/modules/AddonManager.sys.mjs"
);

const lazy = {};
ChromeUtils.defineESModuleGetters(lazy, {
  Extension: "resource://gre/modules/Extension.sys.mjs",
  ExtensionCommon: "resource://gre/modules/ExtensionCommon.sys.mjs",
  ExtensionPermissions: "resource://gre/modules/ExtensionPermissions.sys.mjs",
  recordListItemManageTelemetry: "chrome://global/content/ml/Utils.sys.mjs",
  recordRemoveConfirmationTelemetry: "chrome://global/content/ml/Utils.sys.mjs",
  recordRemoveInitiatedTelemetry: "chrome://global/content/ml/Utils.sys.mjs",
});

const PLUGIN_ICON_URL = "chrome://global/skin/icons/plugin.svg";
const EXTENSION_ICON_URL =
  "chrome://mozapps/skin/extensions/extensionGeneric.svg";
const PRIVATE_BROWSING_PERM_NAME = "internal:privateBrowsingAllowed";
const PRIVATE_BROWSING_PERMS = {
  permissions: [PRIVATE_BROWSING_PERM_NAME],
  origins: [],
};

/**
 * @typedef {object} AddonMessageInfo
 * @property {string} [messageId]
 * @property {object} [messageArgs]
 * @property {"error"|"warning"} [type]
 * @property {string} [linkUrl]
 * @property {string} [linkId]
 * @property {string} [linkSumoPage]
 */

/**
 * @typedef {object} AddonMessageInfoOptions
 * @property {boolean} isCardExpanded
 * @property {boolean} isInDisabledSection
 */

/**
 * @callback GetAddonMessageInfoHook
 * @param {AddonWrapper} addon
 * @param {AddonMessageInfoOptions} options
 * @returns {Promise<AddonMessageInfo>}
 */

/**
 * A card component for managing an add-on. It should be initialized by setting
 * the add-on with `setAddon()` before being connected to the document.
 *
 *    let card = document.createElement("addon-card");
 *    card.setAddon(addon);
 *    document.body.appendChild(card);
 */
export class AddonCard extends AboutAddonsHTMLElement {
  /** @type {GetAddonMessageInfoHook} */
  static #getAddonMessageInfoHook = getAddonMessageInfo;

  /**
   * Register embedder hooks that override addon-card behavior for downstream
   * consumers (e.g. Thunderbird).
   *
   * The caller should call this method only once with all hook callbacks to
   * be set. The callbacks missing from the hooks object will be cleared.
   *
   * The caller is also responsible for refreshing already-rendered cards if
   * needed, e.g.
   * `document.querySelectorAll("addon-card").forEach(card => card.update())`.
   *
   * @param {object} [hooks]
   * @param {GetAddonMessageInfoHook|null} [hooks.getAddonMessageInfo]
   *   Pass a function to override the default resolution of the card's
   *   banner content. The hook may delegate to the default implementation
   *   exported from aboutaddons-utils.mjs for cases it does not handle.
   *   Setting it to `null`, `undefined` or omitting it resets the hook to the
   *   default.
   */
  static setEmbedderHooks(hooks) {
    const { getAddonMessageInfo: getAddonMessageInfoHook } = hooks ?? {};
    AddonCard.#getAddonMessageInfoHook =
      getAddonMessageInfoHook ?? getAddonMessageInfo;
  }

  static get markup() {
    return `
      <template>
        <moz-card class="card addon">
          <theme-preview slot="cover-image"></theme-preview>
          <div class="addon-card-collapsed">
            <img class="card-heading-icon addon-icon" alt="" />
            <div class="card-contents">
              <div class="addon-name-container">
                <a
                  class="addon-badge addon-badge-recommended"
                  is="moz-support-link"
                  support-page="add-on-badges"
                  utm-content="promoted-addon-badge"
                  data-l10n-id="addon-badge-recommended4"
                  hidden
                >
                </a>
                <a
                  class="addon-badge addon-badge-line"
                  is="moz-support-link"
                  support-page="add-on-badges"
                  utm-content="promoted-addon-badge"
                  data-l10n-id="addon-badge-line4"
                  hidden
                >
                </a>
                <a
                  class="addon-badge addon-badge-verified"
                  is="moz-support-link"
                  support-page="add-on-badges"
                  utm-content="promoted-addon-badge"
                  data-l10n-id="addon-badge-verified4"
                  hidden
                >
                </a>
                <a
                  class="addon-badge addon-badge-private-browsing-allowed"
                  is="moz-support-link"
                  support-page="extensions-pb"
                  data-l10n-id="addon-badge-private-browsing-allowed3"
                  hidden
                >
                </a>
                <div class="spacer"></div>
                <moz-button
                  class="theme-enable-button"
                  size="small"
                  action="toggle-disabled"
                  hidden
                ></moz-button>
                <moz-toggle
                  class="extension-enable-button"
                  action="toggle-disabled"
                  data-l10n-id="extension-enable-addon-button-label"
                  hidden
                ></moz-toggle>
                <mlmodel-card-header-additions></mlmodel-card-header-additions>
                <moz-button
                  type="ghost"
                  size="small"
                  iconsrc="chrome://global/skin/icons/more.svg"
                  class="more-options-button"
                  data-l10n-id="addon-options-button"
                ></moz-button>
              </div>
              <mlmodel-card-list-additions></mlmodel-card-list-additions>
              <span class="addon-description" tabindex="-1"></span>
            </div>
          </div>
          <native-theme-colors-checkbox
            class="native-theme-checkbox-list"
            hidden
          ></native-theme-colors-checkbox>
          <moz-message-bar
            class="update-postponed-bar"
            data-l10n-id="install-postponed-message2"
            align="center"
            hidden
          >
            <button
              slot="actions"
              action="install-postponed"
              data-l10n-id="install-postponed-button"
            ></button>
          </moz-message-bar>
          <moz-message-bar class="addon-card-message" align="center" hidden>
          </moz-message-bar>
        </moz-card>
      </template>
    `;
  }

  connectedCallback() {
    // If we've already rendered we can just update, otherwise render.
    if (this.children.length) {
      this.update();
      this.focusOptionsButtonIfExpanded();
    } else {
      this.render();
    }
    this.registerListeners();
  }

  disconnectedCallback() {
    this.removeListeners();
  }

  get expanded() {
    return this.hasAttribute("expanded");
  }

  set expanded(val) {
    if (val) {
      this.setAttribute("expanded", "true");
    } else {
      this.removeAttribute("expanded");
    }
  }

  get updateInstall() {
    return this._updateInstall;
  }

  set updateInstall(install) {
    this._updateInstall = install;
    if (this.children.length) {
      this.update();
    }
  }

  get reloading() {
    return this.hasAttribute("reloading");
  }

  set reloading(val) {
    this.toggleAttribute("reloading", val);
  }

  /**
   * Set the add-on for this card. The card will be populated based on the
   * add-on when it is connected to the DOM.
   *
   * @param {AddonWrapper} addon The add-on to use.
   */
  setAddon(addon) {
    this.addon = addon;
    let install = getUpdateInstall(addon);
    if (
      install &&
      (isInState(install, "available") || isInState(install, "postponed"))
    ) {
      this.updateInstall = install;
    } else {
      this.updateInstall = null;
    }
    if (this.children.length) {
      this.render();
    }
  }

  async setAddonPermission(permission, type, action) {
    let { addon } = this;
    let perms = { origins: [], permissions: [] };

    if (!["add", "remove"].includes(action)) {
      throw new Error("invalid action for permission change");
    }

    if (type === "permission") {
      perms.permissions = [permission];
    } else if (type === "origin") {
      perms.origins = [permission];
    } else if (type === "file_scheme_access") {
      // Note: the visibility of this permission does not imply that the
      // extension has declared file:-access. If an extension removes file
      // access in an update, a previously granted internal permission is not
      // removed. We show the permission UI to allow the user to remove it, but
      // when the page is reloaded the control will disappear.
      perms.permissions = ["internal:fileSchemeAllowed"];
      // TODO: Consider also granting/revoking file: host permissions along
      // with the internal permission. These permissions can currently not
      // be controlled separately from <all_urls> in the UI (bug 1765828).
    } else if (type === "data_collection") {
      perms.data_collection = [permission];
    } else {
      throw new Error("unknown permission type changed");
    }

    if (type !== "file_scheme_access") {
      perms = lazy.ExtensionPermissions.normalizeOptional(
        perms,
        addon.optionalPermissions
      );
    }

    let policy = WebExtensionPolicy.getByID(addon.id);
    lazy.ExtensionPermissions[action](addon.id, perms, policy?.extension);
  }

  async handleEvent(e) {
    let { addon } = this;
    let action = e.target.getAttribute("action");

    if (e.type == "click") {
      switch (action) {
        case "toggle-disabled":
          // Keep the checked state the same until the add-on's state changes.
          e.target.checked = !addon.userDisabled;
          if (addon.userDisabled) {
            if (shouldShowPermissionsPrompt(addon)) {
              await showPermissionsPrompt(addon);
            } else {
              await addon.enable();
            }
          } else {
            await addon.disable();
          }
          break;
        case "always-activate":
          addon.userDisabled = false;
          break;
        case "never-activate":
          addon.userDisabled = true;
          break;
        case "update-check": {
          let { found } = await checkForUpdate(addon);
          if (!found) {
            this.sendEvent("no-update");
          }
          break;
        }
        case "install-postponed": {
          const { updateInstall } = this;
          if (updateInstall && isInState(updateInstall, "postponed")) {
            updateInstall.continuePostponedInstall();
          }
          break;
        }
        case "install-update":
          // Make sure that an update handler is attached to the install object
          // before starting the update installation (otherwise the user would
          // not be prompted for the new permissions requested if necessary),
          // and also make sure that a prompt handler attached from a closed
          // about:addons tab is replaced by the one attached by the currently
          // active about:addons tab.
          attachUpdateHandler(this.updateInstall);
          this.updateInstall.install().then(
            () => {
              detachUpdateHandler(this.updateInstall);
              // The card will update with the new add-on when it gets
              // installed.
              this.sendEvent("update-installed");
            },
            () => {
              detachUpdateHandler(this.updateInstall);
              // Update our state if the install is cancelled.
              this.update();
              this.sendEvent("update-cancelled");
            }
          );
          // Clear the install since it will be removed from the global list of
          // available updates (whether it succeeds or fails).
          this.updateInstall = null;
          break;
        case "contribute":
          windowRoot.window.openWebLinkIn(addon.contributionURL, "tab");
          break;
        case "preferences":
          if (getOptionsType(addon) == "tab") {
            openOptionsInTab(addon.optionsURL);
          } else if (getOptionsType(addon) == "inline") {
            gViewController.loadView(`detail/${this.addon.id}/preferences`);
          }
          break;
        case "remove":
          {
            this.panel.hide();
            if (!hasPermission(addon, "uninstall")) {
              this.sendEvent("remove-disabled");
              return;
            }
            if (addon.type == "mlmodel") {
              const source = e.target.nodeName == "BUTTON" ? "details" : "list";
              lazy.recordRemoveInitiatedTelemetry(addon, source);
            }
            let { BrowserAddonUI } = windowRoot.window;
            let { remove, report } =
              await BrowserAddonUI.promptRemoveExtension(addon);
            if (addon.type == "mlmodel") {
              lazy.recordRemoveConfirmationTelemetry(addon, remove);
            }
            if (remove) {
              await addon.uninstall(true);
              this.sendEvent("remove");
              if (report) {
                openAbuseReport({
                  addonId: addon.id,
                  reportEntryPoint: "uninstall",
                });
              }
            } else {
              this.sendEvent("remove-cancelled");
            }
          }
          break;
        case "expand":
          if (addon.type == "mlmodel") {
            lazy.recordListItemManageTelemetry(addon);
          }
          gViewController.loadView(`detail/${this.addon.id}`);
          break;
        case "report":
          this.panel.hide();
          openAbuseReport({ addonId: addon.id, reportEntryPoint: "menu" });
          break;
        case "link":
          if (e.target.getAttribute("url")) {
            windowRoot.window.openWebLinkIn(
              e.target.getAttribute("url"),
              "tab"
            );
          }
          break;
        default:
          // Handle a click on the card itself.
          if (
            !this.expanded &&
            (e.target === this.addonNameEl || !e.target.closest("a")) &&
            // moz-button handles its own click/toggle; exclude it here to
            // avoid navigating to the detail page when the menu is opened.
            !e.target.classList.contains("more-options-button") &&
            // Exclude the native theme checkbox so clicking it doesn't also
            // navigate to the detail page.
            !e.target.closest("native-theme-colors-checkbox")
          ) {
            e.preventDefault();
            gViewController.loadView(`detail/${this.addon.id}`);
          }
          break;
      }
    } else if (e.type == "toggle" && action == "toggle-permission") {
      let permission = e.target.getAttribute("permission-key");
      let type = e.target.getAttribute("permission-type");
      let fname = e.target.pressed ? "add" : "remove";
      this.setAddonPermission(permission, type, fname);
    } else if (e.type == "change") {
      let { name } = e.target;
      switch (name) {
        case "autoupdate": {
          addon.applyBackgroundUpdates = e.target.value;
          break;
        }
        case "private-browsing": {
          let policy = WebExtensionPolicy.getByID(addon.id);
          let extension = policy && policy.extension;

          if (e.target.value == "1") {
            await lazy.ExtensionPermissions.add(
              addon.id,
              PRIVATE_BROWSING_PERMS,
              extension
            );
          } else {
            await lazy.ExtensionPermissions.remove(
              addon.id,
              PRIVATE_BROWSING_PERMS,
              extension
            );
          }
          // Reload the extension if it is already enabled. This ensures any
          // change on the private browsing permission is properly handled.
          if (addon.isActive) {
            this.reloading = true;
            // Reloading will trigger an enable and update the card.
            addon.reload();
          } else {
            // Update the card if the add-on isn't active.
            this.update();
          }
          break;
        }
        case "quarantined-domains-user-allowed": {
          addon.quarantineIgnoredByUser = e.target.value == "1";
          break;
        }
      }
    } else if (e.type === "shown" || e.type === "hidden") {
      let panelOpen = e.type === "shown";
      // The card will be dimmed if it's disabled, but when the panel is open
      // that should be reverted so the menu items can be easily read.
      this.toggleAttribute("panelopen", panelOpen);
    }
  }

  get panel() {
    return this.card.querySelector("panel-list");
  }

  get postponedMessageBar() {
    return this.card.querySelector(".update-postponed-bar");
  }

  registerListeners() {
    this.addEventListener("change", this);
    this.addEventListener("click", this);
    this.addEventListener("toggle", this);
    this.panel.addEventListener("shown", this);
    this.panel.addEventListener("hidden", this);
  }

  removeListeners() {
    this.removeEventListener("change", this);
    this.removeEventListener("click", this);
    this.removeEventListener("toggle", this);
    this.panel.removeEventListener("shown", this);
    this.panel.removeEventListener("hidden", this);
  }

  /**
   * Update the card's contents based on the previously set add-on. This should
   * be called if there has been a change to the add-on.
   */
  update() {
    let { addon, card } = this;

    card.setAttribute("active", addon.isActive);

    card.querySelector("theme-preview").addon = addon;

    // Set the visibility of the native theme colors checkbox.
    card.querySelector(".native-theme-checkbox-list").hidden =
      this.expanded || !shouldShowNativeThemeCheckbox(addon);

    // Set the icon.
    let iconEl = card.querySelector(".addon-icon");
    if (addon.type == "theme") {
      iconEl.hidden = true;
    } else {
      iconEl.hidden = false;
      if (addon.type == "plugin") {
        iconEl.src = PLUGIN_ICON_URL;
      } else {
        iconEl.src =
          AddonManager.getPreferredIconURL(addon, 32, window) ||
          EXTENSION_ICON_URL;
      }
    }

    // Update the name.
    let name = this.addonNameEl;
    let setDisabledStyle = !(addon.isActive || addon.type === "theme");
    if (!setDisabledStyle) {
      name.textContent = addon.name;
      name.removeAttribute("data-l10n-id");
    } else {
      document.l10n.setAttributes(name, "addon-name-disabled", {
        name: addon.name,
      });
    }
    name.title = `${addon.name} ${addon.version}`;

    let toggleDisabledButton = card.querySelector('[action="toggle-disabled"]');
    if (toggleDisabledButton) {
      let toggleDisabledAction = addon.userDisabled ? "enable" : "disable";
      toggleDisabledButton.hidden = !hasPermission(addon, toggleDisabledAction);
      if (addon.type === "theme") {
        document.l10n.setAttributes(
          toggleDisabledButton,
          `${toggleDisabledAction}-addon-button`
        );
      } else if (
        addon.type === "extension" ||
        addon.type === "sitepermission"
      ) {
        toggleDisabledButton.pressed = !addon.userDisabled;
      }
    }

    // Set the items in the more options menu.
    this.options.update(this, addon, this.updateInstall);

    // Badge the more options button if there's an update.
    let moreOptionsButton = card.querySelector(".more-options-button");
    moreOptionsButton.toggleAttribute(
      "attention",
      !!(this.updateInstall && isInState(this.updateInstall, "available"))
    );

    // Postponed update addon card message bar.
    const hasPostponedInstall =
      this.updateInstall && isInState(this.updateInstall, "postponed");
    this.postponedMessageBar.hidden = !hasPostponedInstall;

    // Hide the more options button if it's empty.
    moreOptionsButton.hidden = this.options.visibleItems.length === 0;

    // Ensure all badges are initially hidden.
    for (let node of card.querySelectorAll(".addon-badge")) {
      node.hidden = true;
    }

    // Set the private browsing badge visibility.
    // TODO: We don't show the badge for SitePermsAddon for now, but this should
    // be handled in Bug 1799090.
    if (addon.incognito != "not_allowed" && addon.type == "extension") {
      // Keep update synchronous, the badge can appear later.
      isAllowedInPrivateBrowsing(addon).then(isAllowed => {
        card.querySelector(".addon-badge-private-browsing-allowed").hidden =
          !isAllowed;
      });
    }

    // Show the recommended badges if needed.
    // Plugins don't have recommendationStates, so ensure a default.
    let states = addon.recommendationStates || [];
    for (let badgeName of states) {
      let badge = card.querySelector(`.addon-badge-${badgeName}`);
      if (badge) {
        badge.hidden = false;
      }
    }

    // Update description.
    card.querySelector(".addon-description").textContent = addon.description;

    this.updateMessage();

    // Update the details if they're shown.
    if (this.details) {
      this.details.update();
    }

    if (addon.type == "mlmodel") {
      this.optionsButton.hidden = this.expanded;
      const mlmodelHeaderAdditions = this.card.querySelector(
        "mlmodel-card-header-additions"
      );
      mlmodelHeaderAdditions.setAddon(addon);
      mlmodelHeaderAdditions.expanded = this.expanded;

      const mlmodelListAdditions = this.card.querySelector(
        "mlmodel-card-list-additions"
      );
      mlmodelListAdditions.setAddon(addon);
      mlmodelListAdditions.expanded = this.expanded;
    }

    this.sendEvent("update");
  }

  async updateMessage() {
    const messageBar = this.card.querySelector(".addon-card-message");

    const resolveMessageInfo = AddonCard.#getAddonMessageInfoHook;
    const {
      linkUrl,
      linkId,
      linkSumoPage,
      messageId,
      messageArgs,
      type = "",
    } = await resolveMessageInfo(this.addon, {
      isCardExpanded: this.expanded,
      // The themes list view sections are moz-card elements, other list
      // views use section elements.
      isInDisabledSection:
        !this.expanded &&
        !!this.closest(
          `:is(section, moz-card).${this.addon.type}-disabled-section`
        ),
    });

    if (messageId) {
      document.l10n.pauseObserving();
      document.l10n.setAttributes(messageBar, messageId, messageArgs);
      messageBar.setAttribute("data-l10n-attrs", "message");

      messageBar.innerHTML = "";
      if (linkUrl) {
        const linkButton = document.createElement("button");
        document.l10n.setAttributes(linkButton, linkId);
        linkButton.setAttribute("action", "link");
        linkButton.setAttribute("url", linkUrl);
        linkButton.setAttribute("slot", "actions");
        messageBar.append(linkButton);
      }

      if (linkSumoPage) {
        const sumoLinkEl = document.createElement("a", {
          is: "moz-support-link",
        });
        sumoLinkEl.setAttribute("support-page", linkSumoPage);
        sumoLinkEl.setAttribute("slot", "support-link");
        // Set a custom fluent id for the learn more if there
        // is one (otherwise moz-support-link custom element
        // will use the default "Learn more" localized string).
        if (linkId) {
          document.l10n.setAttributes(sumoLinkEl, linkId);
        }
        messageBar.append(sumoLinkEl);
      }

      document.l10n.resumeObserving();
      await document.l10n.translateFragment(messageBar);
      messageBar.setAttribute("type", type);
      messageBar.hidden = false;
    } else {
      messageBar.hidden = true;
    }
  }

  showPrefs() {
    this.details.showPrefs();
  }

  expand() {
    if (!this.children.length) {
      this.expanded = true;
    } else {
      throw new Error("expand() is only supported before render()");
    }
  }

  render() {
    this.textContent = "";

    let { addon } = this;
    if (!addon) {
      throw new Error("addon-card must be initialized with setAddon()");
    }

    this.setAttribute("addon-id", addon.id);

    this.card = AddonCard.fragment.firstElementChild;
    let headingId = lazy.ExtensionCommon.makeWidgetId(`${addon.id}-heading`);
    this.card.setAttribute("aria-labelledby", headingId);

    // Remove the toggle-disabled button(s) based on type.
    if (addon.type != "theme") {
      this.card.querySelector(".theme-enable-button").remove();
    }
    if (addon.type != "extension" && addon.type != "sitepermission") {
      this.card.querySelector(".extension-enable-button").remove();
    }
    let nameContainer = this.card.querySelector(".addon-name-container");
    let headingLevel = this.expanded ? "h1" : "h3";
    let nameHeading = document.createElement(headingLevel);
    nameHeading.classList.add("addon-name");
    nameHeading.id = headingId;
    if (!this.expanded) {
      let name = document.createElement("a");
      name.classList.add("addon-name-link");
      name.href = `addons://detail/${addon.id}`;
      nameHeading.appendChild(name);
      this.addonNameEl = name;
    } else {
      this.addonNameEl = nameHeading;
    }
    nameContainer.prepend(nameHeading);

    let panelType = addon.type == "plugin" ? "plugin-options" : "addon-options";
    this.options = document.createElement(panelType);

    // Derive (from the add-on id) an id for the element wrapping the panel-list element
    // to wire up to the addon-card more options moz-button (the addon-options / plugin-options
    // components will derive the panel-list id from their own id set here and will return it
    // from their panelListId getter.
    this.options.id = `addon-card-options-${lazy.ExtensionCommon.makeWidgetId(addon.id)}`;

    this.options.render();
    this.card.appendChild(this.options);
    this.optionsButton = this.card.querySelector(".more-options-button");

    // Wire up the addon-card more-options moz-button with the corresponding panel-list element.
    customElements
      .whenDefined("moz-button")
      .then(() => (this.optionsButton.menuId = this.options.panelListId));

    // Set the contents.
    this.update();

    let doneRenderPromise = Promise.resolve();
    if (this.expanded) {
      if (!this.details) {
        this.details = document.createElement("addon-details");
      }
      this.details.setAddon(this.addon);
      doneRenderPromise = this.details.render();

      // If we're re-rendering we still need to append the details since the
      // entire card was emptied at the beginning of the render.
      this.card.appendChild(this.details);
    }

    this.appendChild(this.card);

    // focus() does nothing on a card that isn't in the document yet, as when
    // the detail view renders it: connectedCallback focuses it then.
    if (this.isConnected) {
      this.focusOptionsButtonIfExpanded();
    }

    // Return the promise of details rendering to wait on in DetailView.
    return doneRenderPromise;
  }

  focusOptionsButtonIfExpanded() {
    if (this.expanded) {
      requestAnimationFrame(() => this.optionsButton.focus());
    }
  }

  sendEvent(name, detail) {
    this.dispatchEvent(new CustomEvent(name, { detail }));
  }

  /**
   * AddonManager listener events.
   */

  onNewInstall(install) {
    this.updateInstall = install;
    this.sendEvent("update-found");
  }

  onInstallEnded(install) {
    this.setAddon(install.addon);
  }

  onInstallPostponed(install) {
    this.updateInstall = install;
    this.sendEvent("update-postponed");
  }

  onDisabled() {
    if (!this.reloading) {
      this.update();
    }
  }

  onEnabled() {
    this.reloading = false;
    this.update();
  }

  onInstalled() {
    // When a temporary addon is reloaded, onInstalled is triggered instead of
    // onEnabled.
    this.reloading = false;
    this.update();
  }

  onUninstalling() {
    // Dispatch a remove event, the DetailView is listening for this to get us
    // back to the list view when the current add-on is removed.
    this.sendEvent("remove");
  }

  onUpdateModeChanged() {
    this.update();
  }

  onPropertyChanged(addon, changed) {
    if (this.details && changed.includes("applyBackgroundUpdates")) {
      this.details.update();
    } else if (addon.type == "plugin" && changed.includes("userDisabled")) {
      this.update();
    }

    if (this.details && changed.includes("quarantineIgnoredByUser")) {
      this.details.updateQuarantinedDomainsUserAllowed();
    }

    if (changed.includes("blocklistState")) {
      this.update();
    }
  }

  /* Extension Permission change listener */
  async onChangePermissions(data) {
    let perms = data.added || data.removed;
    let hasAllSites = false;
    for (let permission of perms.permissions.concat(perms.origins)) {
      if (lazy.Extension.isAllSitesPermission(permission)) {
        hasAllSites = true;
        continue;
      }
      let target = document.querySelector(`[permission-key="${permission}"]`);
      let checked = !data.removed;
      if (target) {
        target.pressed = checked;
      }
    }
    if (hasAllSites) {
      // special-case for finding the all-sites target by attribute.
      let target = document.querySelector("[permission-all-sites]");
      let checked = await AddonCard.optionalAllSitesGranted(this.addon.id);
      target.pressed = checked;
    }
  }

  // Only covers optional_permissions in MV2 and all host permissions in MV3.
  static async optionalAllSitesGranted(addonId) {
    let granted = await lazy.ExtensionPermissions.get(addonId);
    return granted.origins.some(perm =>
      lazy.Extension.isAllSitesPermission(perm)
    );
  }
}
customElements.define("addon-card", AddonCard);
