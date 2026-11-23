/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/// <reference path="./moz-message-bar.tagmap.d.ts" />

import { html, ifDefined, when } from "../vendor/lit.all.mjs";
import { MozLitElement } from "../lit-utils.mjs";
// eslint-disable-next-line import-x/no-unassigned-import
import "chrome://global/content/elements/moz-button.mjs";

window.MozXULElement?.insertFTLIfNeeded("toolkit/global/mozMessageBar.ftl");

/**
 * @typedef {"info" | "warning" | "success" | "error"} MozMessageBarType
 */

const messageTypeToIconData = {
  info: {
    iconSrc: "chrome://global/skin/icons/info-filled.svg",
    l10nId: "moz-message-bar-icon-info",
  },
  warning: {
    iconSrc: "chrome://global/skin/icons/warning.svg",
    l10nId: "moz-message-bar-icon-warning",
  },
  success: {
    iconSrc: "chrome://global/skin/icons/check-filled.svg",
    l10nId: "moz-message-bar-icon-success",
  },
  error: {
    iconSrc: "chrome://global/skin/icons/error.svg",
    l10nId: "moz-message-bar-icon-error",
  },
  critical: {
    iconSrc: "chrome://global/skin/icons/error.svg",
    l10nId: "moz-message-bar-icon-error",
  },
};

/**
 * A simple message bar element that can be used to display
 * important information to users.
 *
 * @tagname moz-message-bar
 * @fires message-bar:close
 *  Custom event indicating that message bar was closed.
 * @fires message-bar:user-dismissed
 *  Custom event indicating that message bar was dismissed by the user.
 */

export class MozMessageBar extends MozLitElement {
  static queries = {
    actionsSlot: "slot[name=actions]",
    actionsEl: ".actions",
    closeButton: "moz-button.close",
    messageEl: ".message",
    supportLinkSlot: "slot[name=support-link]",
    supportLinkHolder: ".link",
  };

  static properties = {
    type: { type: String },
    heading: { type: String, fluent: true },
    message: { type: String, fluent: true },
    dismissable: { type: Boolean },
    supportPage: { type: String },
    messageL10nId: { type: String },
    messageL10nArgs: { type: String },
    role: { type: String, reflect: true },
  };

  constructor() {
    super();

    /**
     * The type of the displayed message.
     *
     * @type {MozMessageBarType}
     */
    this.type = "info";

    /**
     * Whether or not the element is dismissable.
     *
     * @type {boolean}
     */
    this.dismissable = false;

    /**
     * The message text.
     *
     * @type {string | undefined}
     */
    this.message = undefined;

    /**
     * l10n ID for the message.
     *
     * @type {string | undefined}
     */
    this.messageL10nId = undefined;

    /**
     * Any args needed for the message l10n ID.
     *
     * @type {Record<string, string> | undefined}
     */
    this.messageL10nArgs = undefined;

    /**
     * The heading of the message.
     *
     * @type {string | undefined}
     */
    this.heading = undefined;

    /**
     * The support page stub.
     *
     * @type {string | undefined}
     */
    this.supportPage = undefined;

    /**
     * The ARIA role for the message bar.
     *
     * @type {string}
     */
    this.role = "alert";
  }

  onActionSlotchange() {
    let actions = this.actionsSlot.assignedNodes();
    this.actionsEl.classList.toggle("active", actions.length);
  }

  onLinkSlotChange() {
    this.messageEl.classList.toggle(
      "has-link-after",
      !!this.supportLinkEls.length || !!this.supportPage
    );
  }

  disconnectedCallback() {
    super.disconnectedCallback();
    this.dispatchEvent(new CustomEvent("message-bar:close"));
  }

  /** @internal */
  get supportLinkEls() {
    if (this.supportPage) {
      return this.supportLinkHolder.children;
    }
    return this.supportLinkSlot.assignedElements();
  }

  supportLinkTemplate() {
    if (this.supportPage) {
      return html`<a
        is="moz-support-link"
        support-page=${this.supportPage}
        part="support-link"
        aria-describedby="heading message"
      ></a>`;
    }
    return html`<slot
      name="support-link"
      @slotchange=${this.onLinkSlotChange}
    ></slot>`;
  }

  iconTemplate() {
    let iconData = messageTypeToIconData[this.type];
    if (iconData) {
      let { iconSrc, l10nId } = iconData;
      return html`
        <div class="icon-container">
          <img
            class="icon"
            src=${iconSrc}
            data-l10n-id=${l10nId}
            data-l10n-attrs="alt"
          />
        </div>
      `;
    }
    return "";
  }

  headingTemplate() {
    if (this.heading) {
      return html`<strong class="heading">${this.heading}</strong>`;
    }
    return "";
  }

  closeButtonTemplate({ size } = {}) {
    if (this.dismissable) {
      return html`
        <moz-button
          type="icon ghost"
          class="close"
          size=${ifDefined(size)}
          data-l10n-id="moz-message-bar-close-button"
          @click=${this.dismiss}
        ></moz-button>
      `;
    }
    return "";
  }

  render() {
    return html`
      <link
        rel="stylesheet"
        href="chrome://global/content/elements/moz-message-bar.css"
      />
      <div class="container">
        ${this.iconTemplate()}
        <div class="content">
          <div class="text-container">
            <div class="text-content">
              ${this.headingTemplate()}
              <div>
                <slot name="message">
                  <span
                    id="message"
                    class=${when(
                      this.supportPage,
                      () => "message has-link-after",
                      () => "message"
                    )}
                    data-l10n-id=${ifDefined(this.messageL10nId)}
                    data-l10n-args=${ifDefined(
                      JSON.stringify(this.messageL10nArgs)
                    )}
                  >
                    ${this.message}
                  </span>
                </slot>
                <span class="link">${this.supportLinkTemplate()}</span>
              </div>
            </div>
          </div>
          <span class="actions">
            <slot name="actions" @slotchange=${this.onActionSlotchange}></slot>
          </span>
        </div>
        ${this.closeButtonTemplate()}
      </div>
    `;
  }

  dismiss() {
    let event = new CustomEvent("message-bar:user-dismissed", {
      bubbles: true,
      cancelable: true,
    });
    this.dispatchEvent(event);
    if (!event.defaultPrevented) {
      this.close();
    }
  }

  close() {
    this.remove();
  }
}

customElements.define("moz-message-bar", MozMessageBar);
