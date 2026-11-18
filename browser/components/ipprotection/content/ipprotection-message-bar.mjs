/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/. */

import { MozLitElement } from "chrome://global/content/lit-utils.mjs";
import { html, ifDefined } from "chrome://global/content/vendor/lit.all.mjs";

// eslint-disable-next-line import-x/no-unassigned-import
import "chrome://global/content/elements/moz-message-bar.mjs";

const lazy = {};

ChromeUtils.defineESModuleGetters(lazy, {
  URILoadingHelper: "resource:///modules/URILoadingHelper.sys.mjs",
});

/**
 * A custom element that handles the message bar for IP Protection.
 */
export default class IPProtectionMessageBarElement extends MozLitElement {
  #MESSAGE_TYPE_MAP = new Map([
    ["infoLink", () => this.messageTemplate("info")],
    ["success", () => this.messageTemplate("success")],
    ["warning", () => this.messageTemplate("warning")],
  ]);
  DISMISS_EVENT = "ipprotection-message-bar:user-dismissed";

  static queries = {
    mozMessageBarEl: "moz-message-bar",
  };

  static properties = {
    type: { type: String },
    messageId: { type: String },
    messageLink: { type: String },
    messageLinkl10nId: { type: String },
    messageLinkL10nArgs: { type: String },
    bandwidthUsage: { type: Object },
  };

  constructor() {
    super();

    this.handleDismiss = this.handleDismiss.bind(this);
    this.handleClickSettingsLink = this.handleClickSettingsLink.bind(this);
  }

  connectedCallback() {
    super.connectedCallback();
  }

  disconnectedCallback() {
    super.disconnectedCallback();

    if (this.mozMessageBarEl) {
      this.mozMessageBarEl.removeEventListener(
        "message-bar:user-dismissed",
        this.handleDismiss
      );
    }
  }

  handleDismiss() {
    // Dispatch an ipprotection specific dismiss event to wrapping components to notify them
    // when the message was actually dismissed.
    this.dispatchEvent(
      new CustomEvent(this.DISMISS_EVENT, { bubbles: true, composed: true })
    );
  }

  messageTemplate(type) {
    if (this.messageLinkl10nId) {
      return html`
        <moz-message-bar type=${type} dismissable>
          <span
            slot="message"
            data-l10n-id=${ifDefined(this.messageId)}
            @click=${this.handleClickSettingsLink}
          >
            <a
              data-l10n-name=${ifDefined(this.messageLinkl10nId)}
              href=${ifDefined(this.messageLink)}
            ></a>
          </span>
        </moz-message-bar>
      `;
    }
    return html`
      <moz-message-bar
        type=${type}
        data-l10n-id=${ifDefined(this.messageId)}
        data-l10n-args=${ifDefined(this.messageLinkL10nArgs)}
        dismissable
      >
      </moz-message-bar>
    `;
  }

  firstUpdated() {
    this.mozMessageBarEl.addEventListener(
      "message-bar:user-dismissed",
      this.handleDismiss,
      {
        once: true,
      }
    );
  }

  handleClickSettingsLink(event) {
    if (event.target.hasAttribute("href")) {
      event.preventDefault();
      lazy.URILoadingHelper.openTrustedLinkIn(window, this.messageLink, "tab");

      this.dispatchEvent(
        new CustomEvent("IPProtection:Close", { bubbles: true, composed: true })
      );
    }
  }

  render() {
    let templateFn = this.#MESSAGE_TYPE_MAP.get(this.type);
    if (!templateFn) {
      return null;
    }

    return html` ${templateFn()} `;
  }
}

customElements.define(
  "ipprotection-message-bar",
  IPProtectionMessageBarElement
);
