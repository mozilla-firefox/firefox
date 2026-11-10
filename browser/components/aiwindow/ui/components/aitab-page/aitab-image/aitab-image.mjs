/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

import { html, nothing } from "chrome://global/content/vendor/lit.all.mjs";
import { MozLitElement } from "chrome://global/content/lit-utils.mjs";

/**
 * Images are 16:9 and reserve their space while loading. Nothing renders
 * without a src or once loading fails.
 *
 * @property {string} src - Image URL.
 * @property {string} alt - Alternative text; empty marks the image decorative.
 */
export class AITabImage extends MozLitElement {
  static properties = {
    src: { type: String },
    alt: { type: String },
    failed: { type: Boolean, state: true },
  };

  constructor() {
    super();
    this.src = "";
    this.alt = "";
    this.failed = false;
  }

  willUpdate(changedProperties) {
    if (changedProperties.has("src")) {
      this.failed = false;
    }
  }

  render() {
    if (!this.src || this.failed) {
      return nothing;
    }

    return html`
      <link
        rel="stylesheet"
        href="chrome://browser/content/aiwindow/components/aitab-image.css"
      />
      <div class="aitab-image">
        <img
          class="aitab-image-img"
          src=${this.src}
          alt=${this.alt ?? ""}
          loading="lazy"
          @error=${() => (this.failed = true)}
        />
      </div>
    `;
  }
}

customElements.define("aitab-image", AITabImage);
