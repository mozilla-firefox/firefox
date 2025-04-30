/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

const lazy = {};

import { MozLitElement } from "chrome://global/content/lit-utils.mjs";
import { html } from "chrome://global/content/vendor/lit.all.mjs";
// eslint-disable-next-line mozilla/reject-import-system-module-from-non-system
import { PlacesUtils } from "resource://gre/modules/PlacesUtils.sys.mjs";
// eslint-disable-next-line import/no-unassigned-import
import "chrome://browser/content/sidebar/sidebar-panel-header.mjs";

const { LightweightThemeConsumer } = ChromeUtils.importESModule(
  "resource://gre/modules/LightweightThemeConsumer.sys.mjs"
);
ChromeUtils.defineESModuleGetters(lazy, {
  BrowserUtils: "resource://gre/modules/BrowserUtils.sys.mjs",
  PlacesUIUtils: "resource:///modules/PlacesUIUtils.sys.mjs",
});

export class SidebarPage extends MozLitElement {
  constructor() {
    super();
    this.clearDocument = this.clearDocument.bind(this);
  }

  connectedCallback() {
    super.connectedCallback();
    this.ownerGlobal.addEventListener("beforeunload", this.clearDocument);
    this.ownerGlobal.addEventListener("unload", this.clearDocument);

    new LightweightThemeConsumer(document);

    this._contextMenu = this.topWindow.SidebarController.currentContextMenu;
  }

  disconnectedCallback() {
    super.disconnectedCallback();
    this.ownerGlobal.removeEventListener("beforeunload", this.clearDocument);
    this.ownerGlobal.removeEventListener("unload", this.clearDocument);
  }

  get topWindow() {
    return this.ownerGlobal.top;
  }

  get sidebarController() {
    return this.topWindow.SidebarController;
  }

  addContextMenuListeners() {
    this.addEventListener("contextmenu", this);
    this._contextMenu.addEventListener("command", this);
    this._contextMenu.addEventListener(
      "popupshowing",
      this.placesContextShowing
    );
    this._contextMenu.addEventListener("popuphiding", this.placesContextHiding);
  }

  removeContextMenuListeners() {
    this.removeEventListener("contextmenu", this);
    this._contextMenu.removeEventListener("command", this);
    this._contextMenu.removeEventListener(
      "popupshowing",
      this.placesContextShowing
    );
    this._contextMenu.removeEventListener(
      "popuphiding",
      this.placesContextHiding
    );
  }

  addSidebarFocusedListeners() {
    this.topWindow.addEventListener("SidebarFocused", this);
  }

  removeSidebarFocusedListeners() {
    this.topWindow.removeEventListener("SidebarFocused", this);
  }

  handleEvent(e) {
    switch (e.type) {
      case "contextmenu":
        this.handleContextMenuEvent?.(e);
        break;
      case "command":
        this.handleCommandEvent?.(e);
        break;
      case "SidebarFocused":
        this.handleSidebarFocusedEvent?.(e);
        break;
    }
  }

  placesContextShowing(e) {
    lazy.PlacesUIUtils.placesContextShowing(e);
  }

  placesContextHiding(e) {
    lazy.PlacesUIUtils.placesContextHiding(e);
  }

  /**
   * Check if this event comes from an element of the specified type. If it
   * does, return that element.
   *
   * @param {Event} e
   *   The event to check.
   * @param {string} localName
   *   The name of the element to match.
   * @returns {Element | null}
   *   The matching element, or `null` if no match is found.
   */
  findTriggerNode(e, localName) {
    let elements = [
      e.explicitOriginalTarget,
      e.originalTarget.flattenedTreeParentNode,
      // Event might be in shadow DOM, check the host element.
      e.explicitOriginalTarget.flattenedTreeParentNode.getRootNode().host,
    ];
    for (let el of elements) {
      if (el?.localName == localName) {
        return el;
      }
    }
    return null;
  }

  /**
   * Handle a command if it is a common one that is used in multiple pages.
   * Commands specific to a page should be handled in a subclass.
   *
   * @param {Event} e
   *   The event to handle.
   */
  handleCommandEvent(e) {
    switch (e.target.id) {
      case "sidebar-history-context-open-in-tab":
        this.topWindow.openTrustedLinkIn(this.triggerNode.url, "tab");
        break;
      case "sidebar-history-context-forget-site":
        this.forgetAboutThisSite().catch(console.error);
        break;
      case "sidebar-history-context-open-in-window":
      case "sidebar-synced-tabs-context-open-in-window":
        this.topWindow.openTrustedLinkIn(this.triggerNode.url, "window", {
          private: false,
        });
        break;
      case "sidebar-history-context-open-in-private-window":
      case "sidebar-synced-tabs-context-open-in-private-window":
        this.topWindow.openTrustedLinkIn(this.triggerNode.url, "window", {
          private: true,
        });
        break;
      case "sidebar-history-context-copy-link":
      case "sidebar-synced-tabs-context-copy-link":
        lazy.BrowserUtils.copyLink(
          this.triggerNode.url,
          this.triggerNode.title
        );
        break;
      case "sidebar-synced-tabs-context-bookmark-tab":
      case "sidebar-history-context-bookmark-page":
        this.topWindow.PlacesCommandHook.bookmarkLink(
          this.triggerNode.url,
          this.triggerNode.title
        );
        break;
    }
  }

  async forgetAboutThisSite() {
    let host;
    if (PlacesUtils.nodeIsHost(this.triggerNode)) {
      host = this.triggerNode.query.domain;
    } else {
      host = Services.io.newURI(this.triggerNode.url).host;
    }
    let baseDomain;
    try {
      baseDomain = Services.eTLD.getBaseDomainFromHost(host);
    } catch (e) {
      // If there is no baseDomain we fall back to host
    }
    await this.topWindow.gDialogBox.open(
      "chrome://browser/content/places/clearDataForSite.xhtml",
      { host, hostOrBaseDomain: baseDomain ?? host }
    );
  }

  /**
   * Clear out the document so the disconnectedCallback() will trigger properly
   * and all of the custom elements can cleanup.
   */
  clearDocument() {
    this.ownerGlobal.document.body.textContent = "";
  }

  /**
   * The common stylesheet for all sidebar pages.
   *
   * @returns {TemplateResult}
   */
  stylesheet() {
    return html`
      <link
        rel="stylesheet"
        href="chrome://browser/content/sidebar/sidebar.css"
      />
    `;
  }
}
