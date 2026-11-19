/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

import {
  html,
  nothing,
  classMap,
} from "chrome://global/content/vendor/lit.all.mjs";
import { MozLitElement } from "chrome://global/content/lit-utils.mjs";
// eslint-disable-next-line import-x/no-unassigned-import
import "chrome://global/content/elements/moz-button.mjs";

// The favicon cluster shows at most this many icons; beyond that, the
// last slot becomes a "+N" overflow badge instead of an icon.
const MAX_VISIBLE_FAVICONS = 3;

/**
 * A component for displaying conversation starter prompts.
 * Renders a list of prompt buttons that can be clicked to start a conversation.
 *
 * @property {Array<{text: string, type: string, previewIcons?: Array<{iconSrc: string}>, memory?: object, content?: object}>} prompts - Array of prompt objects to display
 */
export class SmartWindowPrompts extends MozLitElement {
  static properties = {
    prompts: { type: Array },
    mode: { type: String, reflect: true },
    canScrollStart: { state: true },
    canScrollEnd: { state: true },
  };

  #resizeObserver = null;
  #observedContainer = null;

  constructor() {
    super();
    this.prompts = [];
    this.mode = "fullpage";
    this.canScrollStart = false;
    this.canScrollEnd = false;
  }

  disconnectedCallback() {
    super.disconnectedCallback();
    this.#resizeObserver?.disconnect();
    this.#resizeObserver = null;
    this.#observedContainer = null;
  }

  updated(changedProperties) {
    // The container is re-created (and thus needs re-observing) whenever
    // prompts empties out and is later repopulated, since render() omits
    // the container entirely while prompts is empty.
    const container = this.#scrollContainer;
    if (container && container !== this.#observedContainer) {
      this.#resizeObserver?.disconnect();
      this.#resizeObserver ??= new ResizeObserver(() =>
        this.#updateScrollState()
      );
      this.#resizeObserver.observe(container);
      this.#observedContainer = container;
    }

    if (changedProperties.has("prompts") || changedProperties.has("mode")) {
      this.#updateScrollState();
    }
  }

  get #scrollContainer() {
    return this.renderRoot.querySelector(".sw-prompts-container");
  }

  /**
   * Tracks fullpage overflow so only available scroll directions are shown.
   */
  #updateScrollState() {
    const container = this.#scrollContainer;
    if (this.mode !== "fullpage" || !container) {
      this.canScrollStart = false;
      this.canScrollEnd = false;
      return;
    }

    const maxScroll = container.scrollWidth - container.clientWidth;
    if (maxScroll <= 1) {
      this.canScrollStart = false;
      this.canScrollEnd = false;
      return;
    }

    // In Gecko, abs(scrollLeft) increases from logical start to end in both
    // LTR and RTL.
    const distanceFromStart = Math.abs(container.scrollLeft);
    this.canScrollStart = distanceFromStart > 1;
    this.canScrollEnd = distanceFromStart < maxScroll - 1;
  }

  /**
   * Scrolls one pill at a time, using pill boundaries because widths vary.
   *
   * @param {boolean} toEnd - Whether to scroll toward the end
   */
  #scrollToAdjacentPill(toEnd) {
    const container = this.#scrollContainer;
    if (!container) {
      return;
    }
    const pills = Array.from(container.children);
    if (!pills.length) {
      return;
    }

    const isRtl = this.matches(":dir(rtl)");
    const containerRect = container.getBoundingClientRect();
    const startEdge = isRtl ? containerRect.right : containerRect.left;
    const isAtOrPastStart = rect =>
      isRtl ? rect.right <= startEdge + 1 : rect.left >= startEdge - 1;

    const currentIndex = pills.findIndex(pill =>
      isAtOrPastStart(pill.getBoundingClientRect())
    );

    // Clamp at either end rather than wrapping.
    const targetIndex = Math.min(
      Math.max((currentIndex === -1 ? 0 : currentIndex) + (toEnd ? 1 : -1), 0),
      pills.length - 1
    );

    pills[targetIndex].scrollIntoView({ inline: "start", block: "nearest" });
  }

  #promptSelected(swPrompt) {
    const { text, type } = swPrompt;
    const detail =
      type === "resume"
        ? {
            text,
            type,
            memory: swPrompt.memory,
            content: swPrompt.content,
          }
        : { text, type };

    const event = new CustomEvent("SmartWindowPrompt:prompt-selected", {
      detail,
      bubbles: true,
      composed: true,
    });
    this.dispatchEvent(event);
  }

  #hasInteracted(e) {
    e.currentTarget.classList.add("has-interacted");
  }

  #promptDismissed(e, swPrompt) {
    e.stopPropagation();
    this.dispatchEvent(
      new CustomEvent("SmartWindowPrompt:prompt-dismissed", {
        detail: { memory: swPrompt.memory },
        bubbles: true,
        composed: true,
      })
    );
  }

  /**
   * Renders a decorative favicon cluster for a prompt's previewIcons, if
   * any. Shows up to MAX_VISIBLE_FAVICONS icons; beyond that, the last
   * slot becomes a "+N" badge for the remaining count.
   *
   * @param {Array<{iconSrc: string}>} [previewIcons]
   */
  #renderFavicons(previewIcons) {
    if (!previewIcons?.length) {
      return nothing;
    }

    const showOverflow = previewIcons.length > MAX_VISIBLE_FAVICONS;
    const visibleIcons = previewIcons.slice(
      0,
      showOverflow ? MAX_VISIBLE_FAVICONS - 1 : MAX_VISIBLE_FAVICONS
    );
    const overflowCount = previewIcons.length - visibleIcons.length;

    return html`
      <span class="sw-prompt-favicons">
        ${visibleIcons.map(
          icon => html`
            <img
              class="sw-prompt-favicon"
              src=${icon.iconSrc}
              alt=""
              @error=${e => {
                e.target.src = "chrome://global/skin/icons/defaultFavicon.svg";
              }}
            />
          `
        )}
        ${showOverflow
          ? html`<span class="sw-prompt-favicon-overflow"
              >+${overflowCount}</span
            >`
          : nothing}
      </span>
    `;
  }

  /**
   * Renders a non-interactive placeholder pill shown in a starter slot
   * while its real content is still loading.
   */
  #renderSkeletonPrompt() {
    return html`
      <span class="sw-prompt-skeleton" aria-hidden="true">
        <span class="sw-prompt-skeleton-text"></span>
      </span>
    `;
  }

  /** Renders arrows only for directions with remaining overflow. */
  #renderScrollButtons() {
    if (this.mode !== "fullpage") {
      return nothing;
    }

    return html`
      ${this.canScrollStart
        ? html`
            <moz-button
              class="sw-prompts-scroll-button sw-prompts-scroll-start"
              type="ghost icon"
              iconsrc="chrome://global/skin/icons/arrow-left.svg"
              data-l10n-id="aiwindow-starter-scroll-start"
              data-l10n-attrs="tooltiptext,aria-label"
              @click=${() => this.#scrollToAdjacentPill(false)}
            ></moz-button>
          `
        : nothing}
      ${this.canScrollEnd
        ? html`
            <moz-button
              class="sw-prompts-scroll-button sw-prompts-scroll-end"
              type="ghost icon"
              iconsrc="chrome://global/skin/icons/arrow-right.svg"
              data-l10n-id="aiwindow-starter-scroll-end"
              data-l10n-attrs="tooltiptext,aria-label"
              @click=${() => this.#scrollToAdjacentPill(true)}
            ></moz-button>
          `
        : nothing}
    `;
  }

  /**
   * Renders a starter pill with a sibling dismiss button for resume pills.
   *
   * @param {object} swPrompt
   */
  #renderPrompt(swPrompt) {
    const pillButton = html`
      <moz-button
        class="sw-prompt-button"
        @click=${() => this.#promptSelected(swPrompt)}
        @mouseenter=${this.#hasInteracted}
        @focusin=${this.#hasInteracted}
        aria-label=${swPrompt.text}
      >
        ${this.#renderFavicons(swPrompt.previewIcons)}${swPrompt.text}
      </moz-button>
    `;

    if (swPrompt.type !== "resume" || this.mode !== "fullpage") {
      return pillButton;
    }

    return html`
      <span class="sw-prompt">
        ${pillButton}
        <button
          class="sw-prompt-dismiss"
          @click=${e => this.#promptDismissed(e, swPrompt)}
          data-l10n-id="aiwindow-starter-dismiss"
          data-l10n-args=${JSON.stringify({ text: swPrompt.text })}
        >
          <img
            class="sw-prompt-dismiss-icon"
            src="chrome://global/skin/icons/close.svg"
            alt=""
          />
        </button>
      </span>
    `;
  }

  render() {
    if (!this.prompts.length) {
      return html``;
    }

    return html`
      <link
        rel="stylesheet"
        href="chrome://browser/content/aiwindow/components/smartwindow-prompts.css"
      />
      <div class="sw-prompts-wrapper">
        <!-- TODO : TODO a11y translations? -->
        <div
          class=${classMap({
            "sw-prompts-container": true,
            "can-scroll-start": this.canScrollStart,
            "can-scroll-end": this.canScrollEnd,
          })}
          role="group"
          @scroll=${() => this.#updateScrollState()}
        >
          ${this.prompts.map(swPrompt =>
            swPrompt.type === "skeleton"
              ? this.#renderSkeletonPrompt()
              : this.#renderPrompt(swPrompt)
          )}
        </div>
        ${this.#renderScrollButtons()}
      </div>
    `;
  }
}

customElements.define("smartwindow-prompts", SmartWindowPrompts);
