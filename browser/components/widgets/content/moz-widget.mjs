/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

import { MozLitElement } from "chrome://global/content/lit-utils.mjs";
import { html, nothing } from "chrome://global/content/vendor/lit.all.mjs";
// eslint-disable-next-line import-x/no-unassigned-import
import "chrome://browser/content/widgets/moz-widget-header.mjs";

const WIDGET_SANDBOX =
  "allow-forms allow-same-origin allow-scripts allow-popups allow-popups-to-escape-sandbox";
const FRAME_SLOT = "frame";

/**
 * Hosts a self-contained, interactive widget for brief interactions.
 *
 * It clips embedded content to the bounds set by the `size` attribute.
 *
 * If `src` is set, the widget content is loaded in a `<browser>` child in chrome documents and an `<iframe>` in content documents.
 * Otherwise the default slot is used.
 *
 * In its error state it draws the error slot over the content and fires
 * `widget-error`. A ready widget fires `widget-ready`, whether it recovered or
 * has just loaded. A host should always slot error content, or
 * a failing widget leaves an empty card. An `<iframe>` that fails to load is an
 * error, and one that loads is ready if `readyOnLoad` is set. Otherwise its
 * host calls `reportError()` and `reportReady()` when appropriate.
 * A `<browser>` widget cannot report for itself, because it
 * is a top-level browsing context, so a chrome host calls both.
 * `reportError(reason)` passes the reason on as `widget-error`'s `detail.reason`.
 *
 * @property {string} widgetId - The widget's registered ID.
 * @property {string} title - The widget's accessible name, taken off the host element.
 * @property {string} size - `small`, `medium`, or `large`. Defaults to `large`.
 * @property {boolean} padded - Insets the header and content.
 * @property {string} menuLabel - The accessible name of the menu button drawn without a header. Defaults to "More options".
 * @property {string} src - A widget document URL.
 * @property {boolean} readyOnLoad - Whether an iframe load reports ready. Defaults to false; attribute `ready-on-load`.
 * @property {string} remoteType - The remote type a `<browser>` frame loads in.
 * @property {number} browsingContextGroupId - The browsing context group a `<browser>` frame's initial load joins.
 * @property {boolean} error - Whether the widget failed. Covers the content with the error content.
 * @slot header - A `moz-widget-header` shown above the content.
 * @slot menu - A `panel-list`, opened by the header's menu button, or by a menu button over the content without a header.
 * @slot default - Widget content used when `src` is not set.
 * @slot error - Error content shown over the widget content while `error` is set.
 */
export class MozWidget extends MozLitElement {
  static properties = {
    widgetId: { attribute: "widget-id" },
    title: { type: String, fluent: true, mapped: true },
    remoteType: { attribute: "remote-type" },
    browsingContextGroupId: { type: Number, attribute: "group-id" },
    size: { reflect: true },
    padded: { type: Boolean, reflect: true },
    menuLabel: { type: String, attribute: "menu-label", fluent: true },
    src: { reflect: true },
    readyOnLoad: { type: Boolean, attribute: "ready-on-load" },
    error: { type: Boolean, reflect: true },
    _menuId: { state: true },
    _hasHeader: { state: true },
  };

  /** @type {MozBrowser|null} The `<browser>` frame if used. */
  #browser = null;
  /** @type {HTMLIFrameElement|null} The `<iframe>` if used. */
  #iframe = null;

  #errorReason;

  #readyReported = false;

  constructor() {
    super();
    this.widgetId = null;
    this.title = null;
    this.size = null;
    this.padded = false;
    this.menuLabel = null;
    this.src = null;
    this.readyOnLoad = false;
    this.error = false;
    this.remoteType = null;
    this.browsingContextGroupId = null;
    this._menuId = null;
    this._hasHeader = false;
  }

  /**
   * Puts the widget into its error state, so the host's error content covers
   * the widget content. A host that detects the failure itself calls this, and a
   * `<browser>` host has to.
   *
   * @param {string} [reason]
   */
  reportError(reason) {
    if (!this.error) {
      this.#errorReason = reason;
    }
    this.error = true;
  }

  /**
   * Takes the widget out of its error state and fires `widget-ready` on every
   * call. A host that hears the widget report ready calls this.
   */
  reportReady() {
    this.#errorReason = undefined;
    this.#readyReported = true;
    this.error = false;
    this.dispatchEvent(new CustomEvent("widget-ready"));
  }

  #onFrameLoad = () => {
    if (this.readyOnLoad) {
      this.reportReady();
    }
  };

  /**
   * Gecko fires `error` only where the load never starts. A load that starts
   * and then fails shows an error page and fires `load` instead.
   */
  #onFrameError = () => {
    this.reportError();
  };

  /**
   * The current frame.
   *
   * @returns {HTMLIFrameElement|MozBrowser|null}
   */
  get frame() {
    if (!this.src) {
      return null;
    }
    if (this.#browser) {
      return this.#browser;
    }
    return this.#iframe;
  }

  /**
   * Whether the containing document is a chrome document.
   *
   * @returns {boolean}
   */
  get supportsXULElements() {
    return !!this.ownerDocument.createXULElement;
  }

  willUpdate(changes) {
    super.willUpdate(changes);
    if (!this.hasUpdated) {
      this.#setMenu(this.querySelector(':scope > [slot="menu"]'));
    }
    if (!this.hasUpdated || changes.has("_menuId")) {
      this.#updateHeader();
    }
    // A different document gets a fresh go.
    if (changes.has("src") && this.hasUpdated) {
      this.error = false;
    }

    this.#setupIframe();

    if (!this.src) {
      this.#setBrowser(null);
      return;
    }
    if (this.supportsXULElements) {
      this.#updateBrowser(changes);
    }
  }

  updated(changes) {
    super.updated(changes);
    // Setting `error` directly fires an event too. An undefined previous
    // value means the first render rather than a change.
    if (changes.has("error")) {
      const reason = this.#errorReason;
      const readyReported = this.#readyReported;
      this.#errorReason = undefined;
      this.#readyReported = false;
      // A ready report followed by an error in the same tick leaves `error`
      // unchanged, but the host has heard ready, so it needs the error too.
      if (
        changes.get("error") !== undefined &&
        (changes.get("error") !== this.error || (this.error && readyReported))
      ) {
        if (this.error) {
          this.dispatchEvent(
            new CustomEvent("widget-error", { detail: { reason } })
          );
        } else if (!readyReported && !changes.has("src")) {
          this.dispatchEvent(new CustomEvent("widget-ready"));
        }
      }
    }
  }

  #onMenuSlotChange(event) {
    this.#setMenu(event.target.assignedElements()[0]);
  }

  #setMenu(menu) {
    if (menu) {
      menu.id ||= `moz-widget-menu-${crypto.randomUUID()}`;
    }
    this._menuId = menu?.id || null;
  }

  #updateHeader() {
    const header = this.querySelector(':scope > [slot="header"]');
    if (header) {
      header.menuId = this._menuId;
    }
    this._hasHeader = !!header;
  }

  #setupIframe() {
    if (this.supportsXULElements || !this.src) {
      this.#iframe = null;
      return;
    }
    // A new iframe for each src gives each document its own window, so a
    // message from the one before it no longer matches.
    if (this.#iframe?.getAttribute("src") != this.src) {
      this.#iframe = this.ownerDocument.createElement("iframe");
      this.#iframe.setAttribute("sandbox", WIDGET_SANDBOX);
      this.#iframe.addEventListener("load", this.#onFrameLoad);
      this.#iframe.addEventListener("error", this.#onFrameError);
      this.#iframe.src = this.src;
    }
    if (this.title) {
      this.#iframe.setAttribute("title", this.title);
    } else {
      this.#iframe.removeAttribute("title");
    }
  }

  #updateBrowser(changes) {
    if (!this.src || !this.supportsXULElements) {
      this.#setBrowser(null);
      return;
    }
    if (
      !this.#browser ||
      changes.has("remoteType") ||
      changes.has("browsingContextGroupId")
    ) {
      this.#setBrowser(this.#renderBrowser());
    } else if (this.#browser.getAttribute("src") != this.src) {
      this.#browser.setAttribute("src", this.src);
    }
  }

  #setBrowser(browser) {
    this.#browser?.remove();
    this.#browser = browser;
    if (browser) {
      this.append(browser);
    }
  }

  #renderBrowser() {
    const browser = this.ownerDocument.createXULElement("browser");
    browser.setAttribute("slot", FRAME_SLOT);
    browser.setAttribute("type", "content");
    browser.setAttribute("disableglobalhistory", "true");
    browser.setAttribute("maychangeremoteness", "true");
    browser.setAttribute("remote", "true");
    if (this.remoteType) {
      browser.setAttribute("remoteType", this.remoteType);
    }
    if (this.remoteType == "extension") {
      // Bug 2074276 will add a webextension-view-type for widgets; until then
      // extension APIs see the widget document as a tab view.
      browser.setAttribute("messagemanagergroup", "webext-browsers");
    }
    const groupId = this.browsingContextGroupId;
    if (Number.isSafeInteger(groupId) && groupId > 0) {
      browser.setAttribute("initialBrowsingContextGroupId", groupId);
    }
    browser.setAttribute("src", this.src);
    return browser;
  }

  #renderContent() {
    // inert is an HTML attribute and the frame can be a XUL browser, so the
    // wrapper carries it.
    return html`
      <div class="content" ?inert=${this.error}>${this.#renderFrame()}</div>
      ${this.error
        ? html`<div class="error-overlay"><slot name="error"></slot></div>`
        : nothing}
    `;
  }

  #renderFrame() {
    if (!this.src) {
      return html`<slot></slot>`;
    }
    if (this.#browser) {
      return html`<slot name=${FRAME_SLOT}></slot>`;
    }
    // Use the manually created iframe since we don't want Lit creating
    // potentially dangerous elements.
    return this.#iframe;
  }

  #renderMenuButton() {
    return html`<moz-button
      id="menu-button"
      type="ghost"
      size="small"
      iconsrc="chrome://global/skin/icons/more.svg"
      .menuId=${this._menuId}
      data-l10n-id=${this.menuLabel ? nothing : "moz-button-more-options"}
      .ariaLabel=${this.menuLabel}
      .title=${this.menuLabel}
    ></moz-button>`;
  }

  render() {
    const usesIframe = this.src && !this.#browser;
    const label = this.title && !usesIframe ? this.title : nothing;
    return html`
      <link
        rel="stylesheet"
        href="chrome://browser/content/widgets/moz-widget.css"
      />
      <article class="widget" aria-label=${label}>
        <slot name="header" @slotchange=${this.#updateHeader}></slot>
        ${this._menuId && !this._hasHeader ? this.#renderMenuButton() : nothing}
        <div class="widget-content">${this.#renderContent()}</div>
        <slot name="menu" @slotchange=${this.#onMenuSlotChange}></slot>
      </article>
    `;
  }
}

customElements.define("moz-widget", MozWidget);
