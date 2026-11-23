/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/// <reference path="./moz-promo.tagmap.d.ts" />

import { html } from "../vendor/lit.all.mjs";
import { MozLitElement } from "../lit-utils.mjs";

// eslint-disable-next-line import-x/no-unassigned-import
import "chrome://global/content/elements/moz-button.mjs";

window.MozXULElement?.insertFTLIfNeeded("toolkit/global/mozPromo.ftl");

/**
 * A promotional callout element.
 *
 * @tagname moz-promo
 * @slot image - Content rendered in place of the image when imageSrc is not set.
 * @fires promo:user-dismissed
 *  Custom event indicating that the promo was dismissed by the user.
 */
export class MozPromo extends MozLitElement {
  static queries = {
    actionsSlot: "slot[name=actions]",
    supportLinkSlot: "slot[name=support-link]",
    imageSlot: "slot[name=image]",
    actionsSupportWrapper: ".actions-and-support-link-wrapper",
    closeButton: "moz-button.close",
  };

  static properties = {
    type: { type: String, reflect: true },
    heading: { type: String, fluent: true },
    message: { type: String, fluent: true },
    imageSrc: { type: String, reflect: true },
    imageWidth: { type: String, reflect: true },
    imageAlignment: { type: String, reflect: true },
    imageDisplay: { type: String, reflect: true },
    dismissable: { type: Boolean, reflect: true },
  };

  constructor() {
    super();

    /**
     * The type of promo. Determines the colors of the promotional element.
     *
     * @type {"default" | "vibrant"}
     */
    this.type = "default";

    /**
     * The heading of the promo element.
     *
     * @type {string | undefined}
     */
    this.heading = undefined;

    /**
     * The message of the promo element.
     *
     * @type {string | undefined}
     */
    this.message = undefined;

    /**
     * The main image of the promo element.
     *
     * @type {string | undefined}
     */
    this.imageSrc = undefined;

    /**
     * How big the image is sized.
     *
     * @type {"default" | "small" | "large"}
     */
    this.imageWidth = "small";

    /**
     * How the image is aligned.
     *
     * @type {"start" | "end" | "center"}
     */
    this.imageAlignment = "start";

    /**
     * Whether the image touches the edge of the promo or has padding around
     * it.
     *
     * @type {"padded" | "cover"}
     */
    this.imageDisplay = "padded";

    /**
     * Whether the promo can be dismissed.
     *
     * @type {boolean}
     */
    this.dismissable = false;
  }

  handleSlotChange() {
    let hasActions = this.actionsSlot.assignedNodes().length;
    let hasSupport = this.supportLinkSlot.assignedNodes().length;
    this.actionsSupportWrapper.classList.toggle(
      "active",
      hasActions || hasSupport
    );
  }

  headingTemplate() {
    if (this.heading) {
      return html`<h2 class="heading heading-medium">${this.heading}</h2>`;
    }
    return "";
  }
  imageTemplate() {
    if (this.imageSrc) {
      return html`
        <div class="image-container"><img src=${this.imageSrc} alt="" /></div>
      `;
    }
    return html`<slot name="image"></slot>`;
  }

  closeButtonTemplate() {
    if (this.dismissable) {
      return html`
        <moz-button
          class="close"
          type="icon ghost"
          iconsrc="chrome://global/skin/icons/close.svg"
          data-l10n-id="moz-promo-close-button"
          @click=${this.dismiss}
        ></moz-button>
      `;
    }
    return "";
  }

  render() {
    let imageStartAligned = this.imageAlignment == "start";
    return html` <link
        rel="stylesheet"
        href="chrome://global/content/elements/moz-promo.css"
      />
      <div class="container">
        ${imageStartAligned ? this.imageTemplate() : ""}
        <div class="text-container">
          ${this.headingTemplate()}
          <p class="message">
            ${this.message}<span class="actions-and-support-link-wrapper">
              <slot name="actions" @slotchange=${this.handleSlotChange}></slot>
              <slot
                name="support-link"
                @slotchange=${this.handleSlotChange}
              ></slot>
            </span>
          </p>
        </div>
        ${!imageStartAligned ? this.imageTemplate() : ""}
        ${this.closeButtonTemplate()}
      </div>`;
  }

  dismiss() {
    let event = new CustomEvent("promo:user-dismissed", {
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
customElements.define("moz-promo", MozPromo);
