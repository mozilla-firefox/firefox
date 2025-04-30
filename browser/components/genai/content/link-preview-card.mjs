/**
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */

import { html } from "chrome://global/content/vendor/lit.all.mjs";
import { MozLitElement } from "chrome://global/content/lit-utils.mjs";

const lazy = {};
ChromeUtils.defineESModuleGetters(lazy, {
  BrowserUtils: "resource://gre/modules/BrowserUtils.sys.mjs",
});

ChromeUtils.defineLazyGetter(
  lazy,
  "numberFormat",
  () => new Services.intl.NumberFormat()
);

ChromeUtils.defineLazyGetter(
  lazy,
  "pluralRules",
  () => new Services.intl.PluralRules()
);

const FEEDBACK_LINK =
  "https://connect.mozilla.org/t5/discussions/try-out-link-previews-on-firefox-labs/td-p/92012";

/**
 * Class representing a link preview element.
 *
 * @augments MozLitElement
 */
class LinkPreviewCard extends MozLitElement {
  // Error message to display when we can't generate a preview
  static ERROR_CARD_MESSAGE = "We can't preview this link";
  // Text for the link to visit the original URL when in error state
  static VISIT_LINK_TEXT = "Visit link";

  // Number of placeholder rows to show when loading
  static PLACEHOLDER_COUNT = 3;

  static properties = {
    generating: { type: Number }, // 0 = off, 1-4 = generating & dots state
    keyPoints: { type: Array },
    pageData: { type: Object },
    progress: { type: Number }, // -1 = off, 0-100 = download progress
  };

  constructor() {
    super();
    this.keyPoints = [];
    this.progress = -1;
  }

  addKeyPoint(text) {
    this.keyPoints.push(text);
    this.requestUpdate();
  }

  /**
   * Handles click events on the <a> element.
   *
   * @param {MouseEvent} event - The click event.
   */
  handleLink(event) {
    event.preventDefault();

    const anchor = event.target.closest("a");
    const url = anchor.href;

    const win = this.ownerGlobal;
    const params = {
      triggeringPrincipal: Services.scriptSecurityManager.createNullPrincipal(
        {}
      ),
    };

    // Determine where to open the link based on the event (e.g., new tab,
    // current tab)
    const where = lazy.BrowserUtils.whereToOpenLink(event, false, true);
    win.openLinkIn(url, where, params);

    this.dispatchEvent(
      new CustomEvent("LinkPreviewCard:dismiss", {
        detail: event.target.dataset.source ?? "error",
      })
    );
  }

  updated(properties) {
    if (properties.has("generating")) {
      if (this.generating > 0) {
        // Count up to 4 so that we can show 0 to 3 dots.
        this.dotsTimeout = setTimeout(
          () => (this.generating = (this.generating % 4) + 1),
          500
        );
      } else {
        // Setting to false or 0 means we're done generating.
        clearTimeout(this.dotsTimeout);
      }
    }
  }

  /**
   * Renders the link preview element.
   *
   * @returns {import('lit').TemplateResult} The rendered HTML template.
   */
  render() {
    const articleData = this.pageData?.article || {};
    const pageUrl = this.pageData?.url || "about:blank";
    const siteName =
      articleData.siteName || this.pageData?.urlComponents?.domain || "";

    const { title, description, imageUrl } = this.pageData.meta;

    const readingTimeMinsFast = articleData.readingTimeMinsFast || "";
    const readingTimeMinsSlow = articleData.readingTimeMinsSlow || "";
    const readingTimeMinsFastStr =
      lazy.numberFormat.format(readingTimeMinsFast);
    const readingTimeRange = lazy.numberFormat.formatRange(
      readingTimeMinsFast,
      readingTimeMinsSlow
    );

    // Check if both metadata and article text content are missing
    const isMissingAllContent = !description && !articleData.textContent;

    const filename = this.pageData?.urlComponents?.filename;

    // Error Link Preview card UI: A simplified version of the preview card showing only an error message
    // and a link to visit the URL. This is a fallback UI for cases when we don't have
    // enough metadata to generate a useful preview.
    const errorCard = html`
      <div class="og-card">
        <div class="og-card-content">
          <div class="og-error-content">
            <p class="og-error-message">
              ${LinkPreviewCard.ERROR_CARD_MESSAGE}
            </p>
            <a class="og-card-title" @click=${this.handleLink} href=${pageUrl}>
              ${LinkPreviewCard.VISIT_LINK_TEXT}
            </a>
          </div>
        </div>
      </div>
    `;

    // Normal Link Preview card UI: Shown when we have sufficient metadata (at least title and description)
    // Displays rich preview information including optional elements like site name, image,
    // reading time, and AI-generated key points if available
    const normalCard = html`
      <div class="og-card">
        <div class="og-card-content">
          ${imageUrl.startsWith("https://")
            ? html` <img class="og-card-img" src=${imageUrl} alt=${title} /> `
            : ""}
          ${siteName
            ? html`
                <div class="page-info-and-card-setting-container">
                  <span class="site-name">${siteName}</span>
                </div>
              `
            : ""}
          <h2 class="og-card-title">
            <a @click=${this.handleLink} data-source="title" href=${pageUrl}
              >${title || filename}</a
            >
          </h2>
          ${description
            ? html`<p class="og-card-description">${description}</p>`
            : ""}
          ${readingTimeMinsFast && readingTimeMinsSlow
            ? html`
                <div
                  class="og-card-reading-time"
                  data-l10n-id="link-preview-reading-time"
                  data-l10n-args=${JSON.stringify({
                    range:
                      readingTimeMinsFast === readingTimeMinsSlow
                        ? `~${readingTimeMinsFastStr}`
                        : `${readingTimeRange}`,
                    rangePlural:
                      readingTimeMinsFast === readingTimeMinsSlow
                        ? lazy.pluralRules.select(readingTimeMinsFast)
                        : lazy.pluralRules.selectRange(
                            readingTimeMinsFast,
                            readingTimeMinsSlow
                          ),
                  })}
                ></div>
              `
            : ""}
        </div>
        ${this.generating || this.keyPoints.length
          ? html`
              <div class="ai-content">
                <h3>
                  Key points
                  <img
                    class="icon"
                    xmlns="http://www.w3.org/1999/xhtml"
                    role="presentation"
                    src="chrome://global/skin/icons/highlights.svg"
                  />
                </h3>
                <ul class="keypoints-list">
                  ${
                    /* All populated content items */
                    this.keyPoints.map(
                      item => html`<li class="content-item">${item}</li>`
                    )
                  }
                  ${
                    /* Loading placeholders with three divs each */
                    this.generating
                      ? Array(
                          Math.max(
                            0,
                            LinkPreviewCard.PLACEHOLDER_COUNT -
                              this.keyPoints.length
                          )
                        )
                          .fill()
                          .map(
                            () =>
                              html` <li class="content-item loading">
                                <div></div>
                                <div></div>
                                <div></div>
                              </li>`
                          )
                      : []
                  }
                </ul>
                <div class="visit-link-container">
                  <a
                    @click=${this.handleLink}
                    data-source="visit"
                    href=${pageUrl}
                    class="visit-link"
                  >
                    Visit page
                    <img
                      class="icon"
                      xmlns="http://www.w3.org/1999/xhtml"
                      role="presentation"
                      src="chrome://global/skin/icons/open-in-new.svg"
                    />
                  </a>
                </div>
                ${this.progress >= 0
                  ? html`
                      <p>
                        First-time setup • <strong>${this.progress}%</strong>
                      </p>
                      <p>You'll see key points more quickly next time.</p>
                    `
                  : ""}
                <hr />
                <p>
                  Key points are AI-generated and may have mistakes.
                  <a
                    @click=${this.handleLink}
                    data-source="feedback"
                    href=${FEEDBACK_LINK}
                  >
                    Share feedback
                  </a>
                </p>
              </div>
            `
          : ""}
      </div>
    `;

    return html`
      <link
        rel="stylesheet"
        href="chrome://browser/content/genai/content/link-preview-card.css"
      />
      ${isMissingAllContent ? errorCard : normalCard}
    `;
  }
}

customElements.define("link-preview-card", LinkPreviewCard);
