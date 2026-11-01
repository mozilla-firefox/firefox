/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

import {
  AddonManagerListenerHandler,
  isPending,
  openAmoInTab,
  shouldSkipAnimations,
} from "../aboutaddons-utils.mjs";

const { AddonManager } = ChromeUtils.importESModule(
  "resource://gre/modules/AddonManager.sys.mjs"
);

const lazy = {};
ChromeUtils.defineESModuleGetters(lazy, {
  // eslint-disable-next-line mozilla/no-browser-refs-in-toolkit
  BuiltInThemes: "resource:///modules/BuiltInThemes.sys.mjs",
  recordListViewTelemetry: "chrome://global/content/ml/Utils.sys.mjs",
});

/**
 * A list view for add-ons of a certain type. It should be initialized with the
 * type of add-on to render and have section data set before being connected to
 * the document.
 *
 *    let list = document.createElement("addon-list");
 *    list.type = "plugin";
 *    list.setSections([{
 *      headingId: "plugin-section-heading",
 *      filterFn: addon => !addon.isSystem,
 *    }]);
 *    document.body.appendChild(list);
 */
export class AddonList extends HTMLElement {
  constructor() {
    super();
    this.sections = [];
    this.pendingUninstallAddons = new Set();
    this._addonsToUpdate = new Set();
    this._userFocusListenersAdded = false;
    this._listeningForInstallUpdates = false;
  }

  async connectedCallback() {
    // Register the listener and get the add-ons, these operations should
    // happpen as close to each other as possible.
    this.registerListener();
    // Don't render again if we were rendered prior to being inserted.
    if (!this.children.length) {
      // Render the initial view.
      this.render();
    }
  }

  disconnectedCallback() {
    // Remove content and stop listening until this is connected again.
    this.textContent = "";
    this.removeListener();

    // Process any pending uninstall related to this list.
    for (const addon of this.pendingUninstallAddons) {
      if (isPending(addon, "uninstall")) {
        addon.uninstall();
      }
    }
    this.pendingUninstallAddons.clear();
  }

  /**
   * Configure the sections in the list.
   *
   * Warning: if filterFn uses criteria that are not tied to add-on events,
   * make sure to add an implementation that calls updateAddon(addon) as
   * needed. Not doing so can result in missing or out-of-date add-on cards!
   *
   * @param {object[]} sections
   *        The options for the section. Each entry in the array should have:
   *          headingId: The fluent id for the section's heading.
   *          filterFn: A function that determines if an add-on belongs in
   *                    the section.
   */
  setSections(sections) {
    this.sections = sections.map(section => Object.assign({}, section));
  }

  /**
   * Set the add-on type for this list. This will be used to filter the add-ons
   * that are displayed.
   *
   * @param {string} val The type to filter on.
   */
  set type(val) {
    this.setAttribute("type", val);
  }

  get type() {
    return this.getAttribute("type");
  }

  getSection(index) {
    return this.sections[index].node;
  }

  getCards(section) {
    return section.querySelectorAll("addon-card");
  }

  getCard(addon) {
    return this.querySelector(`addon-card[addon-id="${addon.id}"]`);
  }

  getPendingUninstallBar(addon) {
    return this.querySelector(`moz-message-bar[addon-id="${addon.id}"]`);
  }

  sortByFn(aAddon, bAddon) {
    return aAddon.name.localeCompare(bAddon.name);
  }

  async getAddons() {
    if (!this.type) {
      throw new Error(`type must be set to find add-ons`);
    }

    // Find everything matching our type, null will find all types.
    let type = this.type == "all" ? null : [this.type];
    let addons = await AddonManager.getAddonsByTypes(type);

    if (type == "theme") {
      await lazy.BuiltInThemes.ensureBuiltInThemes();
    }

    if (type == "mlmodel") {
      lazy.recordListViewTelemetry(addons.length);
    }

    // Put the add-ons into the sections, an add-on goes in the first section
    // that it matches the filterFn for. It might not go in any section.
    let sectionedAddons = this.sections.map(() => []);
    for (let addon of addons) {
      let index = this.sections.findIndex(({ filterFn }) => filterFn(addon));
      if (index != -1) {
        sectionedAddons[index].push(addon);
      } else if (isPending(addon, "uninstall")) {
        // A second tab may be opened on "about:addons" (or Firefox may
        // have crashed) while there are still "pending uninstall" add-ons.
        // Ensure to list them in the pendingUninstall message-bar-stack
        // when the AddonList is initially rendered.
        this.pendingUninstallAddons.add(addon);
      }
    }

    // Sort the add-ons in each section.
    for (let [index, section] of sectionedAddons.entries()) {
      let sortByFn = this.sections[index].sortByFn || this.sortByFn;
      section.sort(sortByFn);
    }

    return sectionedAddons;
  }

  createPendingUninstallStack() {
    const stack = document.createElement("message-bar-stack");
    stack.setAttribute("class", "pending-uninstall");
    stack.setAttribute("reverse", "");
    return stack;
  }

  addPendingUninstallBar(addon) {
    const stack = this.pendingUninstallStack;
    const mb = document.createElement("moz-message-bar");
    mb.setAttribute("addon-id", addon.id);
    mb.setAttribute("type", "info");

    const undo = document.createElement("button");
    undo.setAttribute("action", "undo");
    undo.addEventListener("click", () => {
      addon.cancelUninstall();
    });
    undo.setAttribute("slot", "actions");

    document.l10n.setAttributes(mb, "pending-uninstall-description2", {
      addon: addon.name,
    });
    mb.setAttribute("data-l10n-attrs", "message");
    document.l10n.setAttributes(undo, "pending-uninstall-undo-button");

    mb.appendChild(undo);
    stack.append(mb);
  }

  removePendingUninstallBar(addon) {
    const messagebar = this.getPendingUninstallBar(addon);
    if (messagebar) {
      messagebar.remove();
    }
  }

  createSectionHeading(headingIndex) {
    let { headingId, subheadingId } = this.sections[headingIndex];
    let frag = document.createDocumentFragment();
    let heading = document.createElement("h2");
    heading.classList.add("list-section-heading");
    document.l10n.setAttributes(heading, headingId);
    frag.append(heading);

    if (subheadingId) {
      heading.className = "header-name";
      let subheading = document.createElement("h3");
      subheading.classList.add("list-section-subheading");
      document.l10n.setAttributes(subheading, subheadingId);
      frag.append(subheading);
    }

    return frag;
  }

  createEmptyListMessage() {
    let emptyMessage = "list-empty-get-extensions-message";
    let linkPref = "extensions.getAddons.link.url";

    if (this.sections && this.sections.length) {
      if (this.sections[0].headingId == "locale-enabled-heading") {
        emptyMessage = "list-empty-get-language-packs-message";
        linkPref = "browser.dictionaries.download.url";
      } else if (this.sections[0].headingId == "dictionary-enabled-heading") {
        emptyMessage = "list-empty-get-dictionaries-message";
        linkPref = "browser.dictionaries.download.url";
      }
    }

    // Empty extensions list message shown when Nova isn't enabled.
    let messageContainer = document.createElement("p");
    messageContainer.id = "empty-addons-message";
    let a = document.createElement("a");
    a.href = Services.urlFormatter.formatURLPref(linkPref);
    a.setAttribute("target", "_blank");
    a.setAttribute("data-l10n-name", "get-extensions");
    document.l10n.setAttributes(messageContainer, emptyMessage, {
      domain: a.hostname,
    });
    messageContainer.appendChild(a);

    // Empty extensions list promo shown when Nova is enabled.
    let emptyExtensionsListPromo = document.createElement("addons-promo");
    emptyExtensionsListPromo.id = "empty-addons-promo";
    emptyExtensionsListPromo.imageSrc =
      "chrome://mozapps/skin/extensions/kit-addons.svg";
    emptyExtensionsListPromo.imageAlignment = "start";
    emptyExtensionsListPromo.textCentered = true;
    document.l10n.setAttributes(
      emptyExtensionsListPromo,
      "list-empty-get-extensions-promo"
    );
    let promoBoxBtn = document.createElement("moz-button");
    promoBoxBtn.size = "large";
    promoBoxBtn.slot = "actions";
    promoBoxBtn.onclick = () =>
      openAmoInTab(null, {
        utmContent: "find-more-promo-empty-list",
      });
    document.l10n.setAttributes(
      promoBoxBtn,
      "list-empty-get-extensions-promo-button"
    );
    emptyExtensionsListPromo.append(promoBoxBtn);

    const emptyStateFragment = document.createDocumentFragment();
    emptyStateFragment.appendChild(messageContainer);
    emptyStateFragment.appendChild(emptyExtensionsListPromo);
    return emptyStateFragment;
  }

  updateSectionIfEmpty(section) {
    // Clear the entire list if there are no `addon-card` childrens.
    if (!section.querySelectorAll("addon-card").length) {
      section.textContent = "";
    }
  }

  insertCardInto(card, sectionIndex) {
    let section = this.getSection(sectionIndex);
    let sectionCards = this.getCards(section);

    // If this is the first card in the section, create the heading.
    if (!sectionCards.length && !["mlmodel", "theme"].includes(this.type)) {
      section.appendChild(this.createSectionHeading(sectionIndex));
    }

    // Find where to insert the card.
    let insertBefore = Array.from(sectionCards).find(
      otherCard => this.sortByFn(card.addon, otherCard.addon) < 0
    );
    // This will append if insertBefore is null.
    section.insertBefore(card, insertBefore || null);
  }

  addAddon(addon) {
    // Only insert add-ons of the right type.
    if (addon.type != this.type && this.type != "all") {
      this.sendEvent("skip-add", "type-mismatch");
      return;
    }

    let insertSection = this._addonSectionIndex(addon);

    // Don't add the add-on if it doesn't go in a section.
    if (insertSection == -1) {
      return;
    }

    // Create and insert the card.
    let card = document.createElement("addon-card");
    card.setAddon(addon);
    this.insertCardInto(card, insertSection);
    this.sendEvent("add", { id: addon.id });
  }

  sendEvent(name, detail) {
    this.dispatchEvent(new CustomEvent(name, { detail }));
  }

  removeAddon(addon) {
    let card = this.getCard(addon);
    if (card) {
      let section = card.parentNode;
      card.remove();
      this.updateSectionIfEmpty(section);
      this.sendEvent("remove", { id: addon.id });
    }
  }

  updateAddon(addon) {
    if (!this.getCard(addon)) {
      // Try to add the add-on right away.
      this.addAddon(addon);
    } else if (this._addonSectionIndex(addon) == -1) {
      // Try to remove the add-on right away.
      this._updateAddon(addon);
    } else if (this.isUserFocused) {
      // Queue up a change for when the focus is cleared.
      this.updateLater(addon);
    } else {
      // Not currently focused, make the change now.
      this.withCardAnimation(() => this._updateAddon(addon));
    }
  }

  updateLater(addon) {
    this._addonsToUpdate.add(addon);
    this._addUserFocusListeners();
  }

  _addUserFocusListeners() {
    if (this._userFocusListenersAdded) {
      return;
    }

    this._userFocusListenersAdded = true;
    this.addEventListener("mouseleave", this);
    this.addEventListener("hidden", this, true);
    this.addEventListener("focusout", this);
  }

  _removeUserFocusListeners() {
    if (!this._userFocusListenersAdded) {
      return;
    }

    this.removeEventListener("mouseleave", this);
    this.removeEventListener("hidden", this, true);
    this.removeEventListener("focusout", this);
    this._userFocusListenersAdded = false;
  }

  get hasMenuOpen() {
    return !!this.querySelector("panel-list[open]");
  }

  get isUserFocused() {
    return this.matches(":hover, :focus-within") || this.hasMenuOpen;
  }

  update() {
    if (this._addonsToUpdate.size) {
      this.withCardAnimation(() => {
        for (let addon of this._addonsToUpdate) {
          this._updateAddon(addon);
        }
        this._addonsToUpdate = new Set();
      });
    }
  }

  _getChildCoords() {
    let results = new Map();
    for (let child of this.querySelectorAll("addon-card")) {
      results.set(child, child.getBoundingClientRect());
    }
    return results;
  }

  withCardAnimation(changeFn) {
    if (shouldSkipAnimations()) {
      changeFn();
      return;
    }

    let origChildCoords = this._getChildCoords();

    changeFn();

    let newChildCoords = this._getChildCoords();
    let cards = this.querySelectorAll("addon-card");
    let transitionCards = [];
    for (let card of cards) {
      let orig = origChildCoords.get(card);
      let moved = newChildCoords.get(card);
      let changeY = moved.y - (orig || moved).y;
      let cardEl = card.firstElementChild;

      if (changeY != 0) {
        cardEl.style.transform = `translateY(${changeY * -1}px)`;
        transitionCards.push(card);
      }
    }
    requestAnimationFrame(() => {
      for (let card of transitionCards) {
        card.firstElementChild.style.transition = "transform 125ms";
      }

      requestAnimationFrame(() => {
        for (let card of transitionCards) {
          let cardEl = card.firstElementChild;
          cardEl.style.transform = "";
          cardEl.addEventListener("transitionend", function handler(e) {
            if (e.target == cardEl && e.propertyName == "transform") {
              cardEl.style.transition = "";
              cardEl.removeEventListener("transitionend", handler);
            }
          });
        }
      });
    });
  }

  _addonSectionIndex(addon) {
    return this.sections.findIndex(s => s.filterFn(addon));
  }

  _updateAddon(addon) {
    if (this._listeningForInstallUpdates) {
      // For stability of the UI, do not remove the card from the updates view
      // when it is already there, even when an update is installed.
      return;
    }
    let card = this.getCard(addon);
    if (card) {
      let sectionIndex = this._addonSectionIndex(addon);
      if (sectionIndex != -1) {
        // Move the card, if needed. This will allow an animation between
        // page sections and provides clearer events for testing.
        if (card.parentNode.getAttribute("section") != sectionIndex) {
          let { activeElement } = document;
          let refocus = card.contains(activeElement);
          let oldSection = card.parentNode;
          this.insertCardInto(card, sectionIndex);
          this.updateSectionIfEmpty(oldSection);
          if (refocus) {
            activeElement.focus();
          }
          this.sendEvent("move", { id: addon.id });
        }
      } else {
        this.removeAddon(addon);
      }
    }
  }

  renderSection(addons, index) {
    const { sectionClass, accordion, headingId } = this.sections[index];

    let section = document.createElement(
      this.type === "theme" ? "moz-card" : "section"
    );
    section.setAttribute("section", index);
    if (sectionClass) {
      section.setAttribute("class", sectionClass);
    }
    if (accordion) {
      section.setAttribute("type", "accordion");
      if (accordion.expanded) {
        section.setAttribute("expanded", accordion.expanded);
      }
      section.addEventListener("toggle", accordion.ontoggle);
    }

    // Render the heading and add-ons if there are any, except for:
    // - mlmodel list view which only shows installed models
    // - themes list view which wraps the sections in a moz-card
    //   and so the headings are part of the moz-card attributes.
    if (!["mlmodel", "theme"].includes(this.type) && addons.length) {
      section.appendChild(this.createSectionHeading(index));
    }

    if (this.type === "theme") {
      section.setAttribute("data-l10n-attrs", "heading");
      section.setAttribute("data-l10n-id", headingId);
    }

    for (let addon of addons) {
      let card = document.createElement("addon-card");
      card.setAddon(addon);
      card.render();
      section.appendChild(card);
    }

    return section;
  }

  async render() {
    this.textContent = "";

    let sectionedAddons = await this.getAddons();

    let frag = document.createDocumentFragment();

    // Render the pending uninstall message-bar-stack.
    this.pendingUninstallStack = this.createPendingUninstallStack();
    for (let addon of this.pendingUninstallAddons) {
      this.addPendingUninstallBar(addon);
    }
    frag.appendChild(this.pendingUninstallStack);

    if (this.type == "mlmodel") {
      frag.appendChild(document.createElement("mlmodel-list-intro"));
    }

    // Render the sections.
    for (let i = 0; i < sectionedAddons.length; i++) {
      this.sections[i].node = this.renderSection(sectionedAddons[i], i);
      frag.appendChild(this.sections[i].node);
      if (this.type == "theme" && i == 0) {
        frag.appendChild(document.createElement("aboutaddons-themes-picker"));
      }
    }

    // Add the "empty list message" elements (but omit it in the list view
    // related to the "mlmodel" type).
    if (this.type != "mlmodel") {
      // Render the placeholder that is shown when all sections are empty.
      // This call is after rendering the sections, because its visibility
      // is controlled through the general sibling combinator relative to
      // the sections (section ~).
      let message = this.createEmptyListMessage();
      frag.appendChild(message);
    }

    // Make sure fluent has set all the strings before we render. This will
    // avoid the height changing as strings go from 0 height to having text.
    await document.l10n.translateFragment(frag);
    this.appendChild(frag);
  }

  registerListener() {
    AddonManagerListenerHandler.addListener(this);
  }

  removeListener() {
    AddonManagerListenerHandler.removeListener(this);
  }

  handleEvent(e) {
    if (!this.isUserFocused || (e.type == "mouseleave" && !this.hasMenuOpen)) {
      this._removeUserFocusListeners();
      this.update();
    }
  }

  /**
   * AddonManager listener events.
   */

  onOperationCancelled(addon) {
    if (
      this.pendingUninstallAddons.has(addon) &&
      !isPending(addon, "uninstall")
    ) {
      this.pendingUninstallAddons.delete(addon);
      this.removePendingUninstallBar(addon);
    }
    this.updateAddon(addon);
  }

  onEnabled(addon) {
    this.updateAddon(addon);
  }

  onDisabled(addon) {
    this.updateAddon(addon);
  }

  onUninstalling(addon) {
    if (
      isPending(addon, "uninstall") &&
      (this.type === "all" || addon.type === this.type)
    ) {
      this.pendingUninstallAddons.add(addon);
      this.addPendingUninstallBar(addon);
      this.updateAddon(addon);
    }
  }

  onInstalled(addon) {
    this.updateAddon(addon);
  }

  onUninstalled(addon) {
    this.pendingUninstallAddons.delete(addon);
    this.removePendingUninstallBar(addon);
    this.removeAddon(addon);
  }

  onNewInstall(install) {
    if (this._listeningForInstallUpdates) {
      this._updateOnNewInstall(install);
    }
  }

  onInstallPostponed(install) {
    if (this._listeningForInstallUpdates) {
      this._updateOnNewInstall(install);
    }
  }

  listenForUpdates() {
    this._listeningForInstallUpdates = true;
  }

  async _updateOnNewInstall(install) {
    if (!install.existingAddon) {
      // Not from an update check.
      return;
    }
    // install.existingAddon can differ from the actual add-on (bug 2007749).
    // To make sure that we use the real, live add-on state, look it up again.
    const addon = await AddonManager.getAddonByID(install.existingAddon.id);
    if (addon) {
      this.updateAddon(addon);
    }
  }
}
customElements.define("addon-list", AddonList);
