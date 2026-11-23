/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/// <reference path="./moz-five-star.tagmap.d.ts" />

import { ifDefined, html, classMap } from "../vendor/lit.all.mjs";
import { MozLitElement } from "../lit-utils.mjs";

window.MozXULElement?.insertFTLIfNeeded("toolkit/global/mozFiveStar.ftl");

/**
 * @typedef {HTMLSpanElement} MozFiveStarRatingStarElement
 */

/**
 * @typedef {{
 *   rating: number
 *   fill: 'full' | 'half' | 'empty'
 * }} MozFiveStarRenderedStarState
 */

/**
 * The visual representation is five stars, each of them either empty,
 * half-filled or full. The fill state is derived from the rating,
 * rounded to the nearest half.
 *
 * @tagname moz-five-star
 */
export class MozFiveStar extends MozLitElement {
  static properties = {
    rating: { type: Number, reflect: true },
    title: { type: String },
    selectable: { type: Boolean },
  };

  constructor() {
    super();

    /**
     * The initial rating that is also dynamically updated to the selected
     * rating if {@link selectable} is set to true.
     *
     * @type {number}
     */
    this.rating = 0;

    /**
     * The title text.
     *
     * @type {string | undefined}
     */
    this.title = undefined;

    /**
     * Whether the stars in the component are selectable.
     *
     * @type {boolean}
     */
    this.selectable = false;
  }

  static get queries() {
    return {
      starEls: { all: ".rating-star" },
      starsWrapperEl: ".stars",
    };
  }

  /**
   * @returns {Array<MozFiveStarRenderedStarState>}
   */
  getStars() {
    /**
     * @type {Array<MozFiveStarRenderedStarState>}
     */
    let stars = [];

    let roundedRating = Math.round(this.rating * 2) / 2;

    for (let i = 1; i <= 5; i++) {
      if (i <= roundedRating) {
        stars.push({ rating: i, fill: "full" });
      } else if (i - roundedRating === 0.5) {
        stars.push({ rating: i, fill: "half" });
      } else {
        stars.push({ rating: i, fill: "empty" });
      }
    }
    return stars;
  }

  /**
   * @param {MozFiveStarRatingStarElement} ratingStarElement
   * @returns
   */
  getStarElementRating(ratingStarElement) {
    const stringRating = ratingStarElement.getAttribute("rating") || "";
    return parseInt(stringRating, 10);
  }

  /**
   * @param {MouseEvent} e
   */
  onClick(e) {
    if (!this.selectable) {
      return;
    }
    /**
     * @type {MozFiveStarRatingStarElement}
     */
    const ratingStarElement = /**@type {object} */ (e.target);

    this.rating = this.getStarElementRating(ratingStarElement);

    this.dispatchEvent(
      new CustomEvent("select", {
        detail: {
          rating: this.rating,
        },
      })
    );
  }

  render() {
    const { rating, selectable, title } = this;

    const starsTitle = title || selectable;
    return html`
      <link
        rel="stylesheet"
        href="chrome://global/content/elements/moz-five-star.css"
      />
      <div
        class="stars"
        role="img"
        data-l10n-id=${ifDefined(
          starsTitle ? undefined : "moz-five-star-rating"
        )}
        data-l10n-args=${ifDefined(
          starsTitle ? undefined : JSON.stringify({ rating })
        )}
      >
        ${this.getStars().map(({ rating: ratingValue, fill }) => {
          return html`<span
            class=${classMap({
              "rating-star": true,
              selectable,
            })}
            fill=${fill}
            rating=${ratingValue}
            @click=${this.onClick}
            data-l10n-id=${ifDefined(
              selectable ? "moz-five-star-rating-rate-text" : undefined
            )}
            data-l10n-args=${ifDefined(
              selectable
                ? JSON.stringify({
                    rating: ratingValue,
                  })
                : undefined
            )}
          ></span>`;
        })}
      </div>
    `;
  }
}
customElements.define("moz-five-star", MozFiveStar);
