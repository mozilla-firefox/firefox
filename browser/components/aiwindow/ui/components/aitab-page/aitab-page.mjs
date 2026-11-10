/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

import { html, nothing } from "chrome://global/content/vendor/lit.all.mjs";
import { componentTypeName } from "chrome://browser/content/aiwindow/modules/AITabUtils.mjs";
import { MozLitElement } from "chrome://global/content/lit-utils.mjs";
// eslint-disable-next-line import-x/no-unassigned-import
import "chrome://browser/content/aiwindow/components/aitab-header.mjs";
// eslint-disable-next-line import-x/no-unassigned-import
import "chrome://browser/content/aiwindow/components/aitab-list.mjs";
// eslint-disable-next-line import-x/no-unassigned-import
import "chrome://browser/content/aiwindow/components/aitab-timeline.mjs";
// eslint-disable-next-line import-x/no-unassigned-import
import "chrome://browser/content/aiwindow/components/aitab-table.mjs";
// eslint-disable-next-line import-x/no-unassigned-import
import "chrome://browser/content/aiwindow/components/aitab-highlights.mjs";
// eslint-disable-next-line import-x/no-unassigned-import
import "chrome://browser/content/aiwindow/components/aitab-image.mjs";

// eslint-disable-next-line import-x/no-unassigned-import
import "chrome://browser/content/aiwindow/components/aitab-error.mjs";
// eslint-disable-next-line import-x/no-unassigned-import
import "chrome://browser/content/aiwindow/components/aitab-footer.mjs";
// eslint-disable-next-line import-x/no-unassigned-import
import "chrome://browser/content/aiwindow/components/aitab-text-block.mjs";

// The same names the child and parent actors use, so a message can be traced
// straight through without a translation table.
const GET_PAGE_EVENT = "AITab:GetPage";
const GET_HEADING_FONT_EVENT = "AITab:GetHeadingFont";
const DELETE_PAGE_EVENT = "AITab:DeletePage";

/**
 * Page data received from the parent actor.
 *
 * @typedef {import("../../actors/AITabParent.sys.mjs").AITabData} AITabData
 */

/**
 * Root component for about:smartpage. Looks up the page config for the
 * generated page named in the page URL and renders it.
 *
 * @property {AITabData|null} page - Page config to render, or null when there is none.
 * @property {"loading"|"unavailable"|"error"|"ready"} status - Determines which page state is rendered.
 */
export class AITabPage extends MozLitElement {
  #renderers;

  static properties = {
    page: { type: Object },
    status: { type: String },
  };

  constructor() {
    super();
    this.page = null;
    this.status = "loading";

    this.#renderers = new Map();
    this.#renderers.set("highlights", this.#renderHighlights.bind(this));
    this.#renderers.set("sourcelinks", this.#renderSourceLinks.bind(this));
    this.#renderers.set("textblock", this.#renderTextBlock.bind(this));
    this.#renderers.set("rankedtable", this.#renderRankedTable.bind(this));
    this.#renderers.set("cards", this.#renderCards.bind(this));
    this.#renderers.set("timeline", this.#renderTimeline.bind(this));
    this.#renderers.set("list", this.#renderList.bind(this));
  }

  connectedCallback() {
    super.connectedCallback();
    this.#loadPage();
  }

  disconnectedCallback() {
    super.disconnectedCallback();

    this.status = "loading";
    this.page = null;

    window.removeEventListener(
      "AITab:RefreshPageData",
      this.#onRefreshPageData
    );
  }

  /**
   * Fetches the heading font from the parent and installs it. The page does
   * not wait for this: headings use the fallback fonts until the face is
   * added, and keep them if it fails. The returned status is recorded with
   * the view and does not change what the page shows.
   *
   * @returns {Promise<"ready"|"unavailable"|"error">}
   */
  async #loadHeadingFont() {
    try {
      const response = await this.#request(GET_HEADING_FONT_EVENT);
      const buffer = response?.font;
      if (!buffer) {
        return "unavailable";
      }
      // The name comes from aitab-base.css, so the CSS and the face match.
      const family = getComputedStyle(document.documentElement)
        .getPropertyValue("--aitab-heading-font-family")
        .trim();
      const face = new FontFace(family, buffer, {
        weight: "600",
      });
      await face.load();
      document.fonts.add(face);
      return "ready";
    } catch (error) {
      console.error("Failed to load the AI Tab heading font:", error);
      return "error";
    }
  }

  /**
   * Unique name of the generated page to render, taken from the page URL. It
   * is an opaque key for the parent process to look up, never a path.
   *
   * @returns {?string}
   */
  get pageName() {
    return new URLSearchParams(window.location.search).get("page");
  }

  /**
   * Handles the AITab:RefreshPageData event so the page can be refreshed
   *
   * @param {CustomEvent<AITabData>} event - Updated page data from the parent actor.
   */
  #onRefreshPageData = event => {
    this.page = event.detail;
    this.status = this.page ? "ready" : "unavailable";
  };

  /**
   * Loads the page data for the Smart Page and loads the
   * custom font for Smart Page. Triggers a view telemetry
   * event.
   *
   * @returns {Promise<void>} Resolves after loading the page and recording its view.
   */
  async #loadPage() {
    window.addEventListener("AITab:RefreshPageData", this.#onRefreshPageData);

    let font = "unavailable";
    try {
      if (!this.pageName) {
        this.status = "unavailable";
        return;
      }

      this.#loadHeadingFont().then(fontStatus => {
        font = fontStatus;
      });
      const response = await this.#request(GET_PAGE_EVENT);
      if (!response?.success) {
        throw new Error(
          response?.error ?? "No response from the parent process"
        );
      }

      this.page = response.page ?? null;
      this.status = this.page ? "ready" : "unavailable";
    } catch (error) {
      console.error("Failed to load AI Tab page:", error);
      this.status = "error";
    } finally {
      const outcome = this.status;
      await this.updateComplete;
      this.dispatchEvent(
        new CustomEvent("AITab:RecordView", {
          bubbles: true,
          detail: { outcome, font },
        })
      );
    }
  }

  /**
   * Deletes this page, and with it the conversation that produced it. The
   * page stays open showing the unavailable state rather than closing the
   * tab, so the deletion is visible.
   */
  async #deletePage() {
    const response = await this.#request(DELETE_PAGE_EVENT);
    if (!response?.success) {
      throw new Error(response?.error ?? "No response from the parent process");
    }

    this.page = null;
    this.status = "unavailable";
  }

  /**
   * Sends a request to the AITab actor and waits for its answer.
   *
   * @param {string} eventType - Request event the child actor listens for.
   * @param {object} [detail] - Payload forwarded to the parent actor.
   * @returns {Promise<object>} Resolves with the parent actor's response.
   */
  #request(eventType, detail = null) {
    return new Promise((resolve, reject) => {
      const onResponse = event => {
        this.removeEventListener(`${eventType}:Error`, onError);
        resolve(event.detail);
      };
      const onError = event => {
        this.removeEventListener(`${eventType}:Response`, onResponse);
        reject(new Error(event.detail?.error || "The request failed"));
      };

      this.addEventListener(`${eventType}:Response`, onResponse, {
        once: true,
      });
      this.addEventListener(`${eventType}:Error`, onError, { once: true });

      this.dispatchEvent(new CustomEvent(eventType, { bubbles: true, detail }));
    });
  }

  #renderHeader(header) {
    // The title is what shows up for this page in history and in the tab strip.
    if (header.title) {
      document.title = header.title;
    }

    // The header block leaves `eyebrow` blank; the run date comes from the
    // stored page, already formatted and localized by AITabParent.
    return html`
      <aitab-header
        data-aitab-component=${componentTypeName(header)}
        .createdAt=${this.page?.createdAtLabel ?? ""}
        .title=${header.title ?? ""}
        .subhead=${header.subhead ?? ""}
        .references=${header.references?.items ?? []}
        @aitab-page-actions:delete=${() => {
          this.#deletePage().catch(error => {
            console.error("Failed to delete AI Tab page:", error);
            this.status = "error";
          });
        }}
      ></aitab-header>
    `;
  }

  #renderHighlights(highlights) {
    const markup = html`<aitab-highlights
      .title=${highlights.title}
      .items=${highlights.items}
    ></aitab-highlights>`;

    return this.#renderBlock(highlights, markup);
  }

  // TODO: Bug 2075400 - SourceLinks appear in multiple components
  // and can also appear as a child based on the schema
  // we should extract these as a component
  #renderSourceLinks(_sourceLinks) {
    return nothing;
  }

  #renderTextBlock(textBlock) {
    const markup = html`<aitab-text-block
      .heading=${textBlock.lead ?? ""}
      .paragraphs=${textBlock.paragraphs ?? []}
      .references=${textBlock.references ?? []}
    ></aitab-text-block>`;

    return this.#renderBlock(textBlock, markup);
  }

  #renderRankedTable(rankedTable) {
    const markup = html`<aitab-table
      .heading=${rankedTable.title}
      .description=${rankedTable.description}
      .columns=${rankedTable.columns}
      .rows=${rankedTable.rows}
    ></aitab-table>`;

    return this.#renderBlock(rankedTable, markup);
  }

  #renderCards(_cards) {
    return nothing;
  }

  #renderTimeline(timeline) {
    const markup = html`<aitab-timeline
      .title=${timeline.title ?? ""}
      description=${timeline.description ?? ""}
      .items=${timeline.items ?? []}
    ></aitab-timeline>`;

    return this.#renderBlock(timeline, markup);
  }

  #renderList(list) {
    const markup = html`<aitab-list
      .title=${list.title ?? ""}
      description=${list.description ?? ""}
      .groups=${list.groups ?? []}
      layout=${list.layout ?? "column"}
    ></aitab-list>`;

    return this.#renderBlock(list, markup);
  }

  #renderBlock(block, markup) {
    const type = block?.component?.toLowerCase?.();
    if (!type) {
      return nothing;
    }

    return html`
      <section
        class="aitab-block"
        data-block-type=${type}
        data-aitab-component=${componentTypeName(block)}
      >
        ${markup}
      </section>
    `;
  }

  #renderStatus() {
    if (this.status == "loading") {
      return nothing;
    }

    return html`<aitab-error .status=${this.status}></aitab-error>`;
  }

  render() {
    return html`
      <link
        rel="stylesheet"
        href="chrome://browser/content/aiwindow/components/aitab-page.css"
      />
      ${this.status == "ready" ? this.#renderPage() : this.#renderStatus()}
    `;
  }

  #renderPage() {
    const children = this.page.children ?? [];
    const hasHeader = children[0]?.component === "Header";

    return html`<main class="aitab-sheet">
        ${hasHeader ? this.#renderHeader(children[0]) : nothing}
        ${this.#renderBlocks(hasHeader ? children.slice(1) : children)}
      </main>
      <aitab-footer></aitab-footer>`;
  }

  #renderBlocks(blocks) {
    return html`
      <div class="aitab-blocks">
        ${blocks.map(block => {
          const componentKey = block.component?.toLowerCase?.();
          const renderer = this.#renderers.get(componentKey);
          return renderer ? renderer(block) : nothing;
        })}
      </div>
    `;
  }
}

customElements.define("aitab-page", AITabPage);
