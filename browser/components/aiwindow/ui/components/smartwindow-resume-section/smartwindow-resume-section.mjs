/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

import { html, nothing } from "chrome://global/content/vendor/lit.all.mjs";
import { MozLitElement } from "chrome://global/content/lit-utils.mjs";
// eslint-disable-next-line import-x/no-unassigned-import
import "chrome://global/content/elements/moz-button.mjs";
// eslint-disable-next-line import-x/no-unassigned-import
import "chrome://browser/content/aiwindow/components/smartwindow-resume-card.mjs";

const COLLAPSED_CARD_COUNT = 2;

export const RESUME_SECTION_EMPTY_REASON = {
  NO_SUGGESTIONS: "no-suggestions",
  ALL_DISMISSED: "all-dismissed",
};

const TELEMETRY_EMPTY_STATE = {
  [RESUME_SECTION_EMPTY_REASON.NO_SUGGESTIONS]: "empty_no_suggestions",
  [RESUME_SECTION_EMPTY_REASON.ALL_DISMISSED]: "empty_all_dismissed",
};

/**
 * A collapsible grid of resume cards. Card events bubble to the caller.
 *
 * @property {Array<{content: object, memory: object}>} cards - Journeys to render as cards
 * @property {string} emptyReason - Why `cards` is empty (a
 *   RESUME_SECTION_EMPTY_REASON value), or null/undefined to render nothing
 *   while empty
 * @property {boolean} loading - Whether resume content is still being generated
 * @property {() => {chat_id: string, location: string, message_seq: number}} getTelemetryContext
 *   - Returns the values recorded with every Glean event from the section
 */
export class SmartwindowResumeSection extends MozLitElement {
  static properties = {
    cards: { type: Array },
    emptyReason: { type: String },
    loading: { type: Boolean },
    getTelemetryContext: { attribute: false },
    expanded: { type: Boolean, state: true },
  };

  #visibilityHandler = null;
  #displayedFrame = 0;
  #hasRecordedDisplayed = false;

  constructor() {
    super();
    this.cards = [];
    this.emptyReason = null;
    this.loading = false;
    this.getTelemetryContext = () => ({});
    this.expanded = false;
  }

  disconnectedCallback() {
    super.disconnectedCallback();
    this.#cancelPendingDisplayed();
  }

  updated(changedProperties) {
    super.updated(changedProperties);
    // Fast lookups can resolve before the loading delay, so watch content too.
    if (
      !this.loading &&
      (changedProperties.has("loading") ||
        changedProperties.has("cards") ||
        changedProperties.has("emptyReason"))
    ) {
      this.#recordDisplayedWhenSeen();
    }
  }

  /**
   * How many cards the user can see right now. Before "Show more" is
   * clicked, this is at most two, even if there are more cards.
   *
   * @returns {number}
   */
  get #visibleCardCount() {
    return this.expanded
      ? this.cards.length
      : Math.min(this.cards.length, COLLAPSED_CARD_COUNT);
  }

  /**
   * Records the displayed event once the section is ready and visible.
   */
  #recordDisplayedWhenSeen() {
    if (this.#hasRecordedDisplayed) {
      return;
    }
    this.#cancelPendingDisplayed();
    if (this.ownerDocument.hidden) {
      this.#visibilityHandler = () => this.#recordDisplayedWhenSeen();
      this.ownerDocument.addEventListener(
        "visibilitychange",
        this.#visibilityHandler,
        { once: true }
      );
      return;
    }

    this.#displayedFrame = requestAnimationFrame(() => {
      this.#displayedFrame = 0;
      if (
        (!this.cards.length && !this.emptyReason) ||
        !this.checkVisibility()
      ) {
        return;
      }
      Glean.smartWindow.resumeCardsDisplayed.record({
        ...this.getTelemetryContext(),
        state: this.cards.length
          ? "cards"
          : TELEMETRY_EMPTY_STATE[this.emptyReason],
        cards: this.#visibleCardCount,
        total_cards: this.cards.length,
      });
      this.#hasRecordedDisplayed = true;
    });
  }

  #cancelPendingDisplayed() {
    if (this.#visibilityHandler) {
      this.ownerDocument.removeEventListener(
        "visibilitychange",
        this.#visibilityHandler
      );
      this.#visibilityHandler = null;
    }
    if (this.#displayedFrame) {
      cancelAnimationFrame(this.#displayedFrame);
      this.#displayedFrame = 0;
    }
  }

  /**
   * Records Glean event "resume_card_action". Card events reach the section
   * before ai-window acts on them, so the card is still in `cards`.
   *
   * @param {"resume"|"card_click"|"open_tabs"|"snooze"|"dismiss"} action - The
   *   action the user took.
   * @param {string} journeyId - The card the user clicked.
   */
  #recordCardAction(action, journeyId) {
    Glean.smartWindow.resumeCardAction.record({
      ...this.getTelemetryContext(),
      action,
      position: this.cards.findIndex(({ memory }) => memory.id === journeyId),
    });
  }

  #onCardResume = ({ detail }) => {
    this.#recordCardAction(
      detail.source === "resume_button" ? "resume" : "card_click",
      detail.journeyId
    );
  };

  #onCardDismiss = ({ detail }) => {
    this.#recordCardAction("dismiss", detail.journeyId);
  };

  #onCardMenuItemSelected = ({ detail }) => {
    this.#recordCardAction(detail.itemId, detail.journeyId);
  };

  /**
   * Records Glean event "resume_section_action".
   *
   * @param {"show_more"|"show_less"|"hide_section"} action - The action the
   *   user took.
   */
  #recordSectionAction(action) {
    Glean.smartWindow.resumeSectionAction.record({
      ...this.getTelemetryContext(),
      action,
    });
  }

  #dispatch(type, detail) {
    this.dispatchEvent(
      new CustomEvent(`smartwindow-resume-section:${type}`, {
        detail,
        bubbles: true,
        composed: true,
      })
    );
  }

  #onToggleClick = () => {
    this.#recordSectionAction(this.expanded ? "show_less" : "show_more");
    this.expanded = !this.expanded;
  };

  #onHideClick = () => {
    this.#recordSectionAction("hide_section");
    this.#dispatch("hide", { reason: this.emptyReason });
  };

  #renderEmptyState() {
    const isAllDismissed =
      this.emptyReason === RESUME_SECTION_EMPTY_REASON.ALL_DISMISSED;

    return html`
      <link
        rel="stylesheet"
        href="chrome://browser/content/aiwindow/components/smartwindow-resume-section.css"
      />
      <div class="resume-section-heading">
        <span
          class="resume-section-title"
          data-l10n-id="aiwindow-resume-section-heading"
        ></span>
      </div>
      <div class="resume-section-empty">
        <div
          class="resume-section-empty-heading"
          data-l10n-id=${isAllDismissed
            ? "aiwindow-resume-section-empty-all-dismissed-heading"
            : "aiwindow-resume-section-empty-no-suggestions-heading"}
        ></div>
        <div
          class="resume-section-empty-description"
          data-l10n-id=${isAllDismissed
            ? "aiwindow-resume-section-empty-all-dismissed-description"
            : "aiwindow-resume-section-empty-no-suggestions-description"}
        ></div>
        <moz-button
          class="resume-section-empty-hide"
          size="small"
          @click=${this.#onHideClick}
          data-l10n-id="aiwindow-resume-section-hide"
        ></moz-button>
      </div>
    `;
  }

  #renderSkeletonCard() {
    return html`
      <div class="resume-card-skeleton" aria-hidden="true">
        <div class="resume-card-skeleton-header">
          <span class="resume-card-skeleton-favicons">
            <span class="resume-card-skeleton-favicon"></span>
            <span class="resume-card-skeleton-favicon"></span>
            <span class="resume-card-skeleton-favicon"></span>
          </span>
          <span
            class="resume-section-skeleton-text resume-card-skeleton-tab-count"
          ></span>
        </div>
        <span
          class="resume-section-skeleton-text resume-card-skeleton-title"
        ></span>
        <span
          class="resume-section-skeleton-text resume-card-skeleton-description"
        ></span>
        <span
          class="resume-section-skeleton-text resume-card-skeleton-description"
        ></span>
        <div class="resume-card-skeleton-actions">
          <span
            class="resume-section-skeleton-text resume-card-skeleton-button"
          ></span>
          <span
            class="resume-section-skeleton-text resume-card-skeleton-button"
          ></span>
        </div>
      </div>
    `;
  }

  #renderLoadingState() {
    return html`
      <link
        rel="stylesheet"
        href="chrome://browser/content/aiwindow/components/smartwindow-resume-section.css"
      />
      <div class="resume-section-heading">
        <span
          class="resume-section-title"
          data-l10n-id="aiwindow-resume-section-heading"
        ></span>
      </div>
      <div class="resume-section-grid">
        ${Array.from({ length: COLLAPSED_CARD_COUNT }, () =>
          this.#renderSkeletonCard()
        )}
      </div>
    `;
  }

  render() {
    if (this.loading) {
      return this.#renderLoadingState();
    }

    if (!this.cards.length) {
      return this.emptyReason ? this.#renderEmptyState() : nothing;
    }

    const hiddenCount = Math.max(0, this.cards.length - COLLAPSED_CARD_COUNT);
    const visibleCards =
      this.expanded || !hiddenCount
        ? this.cards
        : this.cards.slice(0, COLLAPSED_CARD_COUNT);

    return html`
      <link
        rel="stylesheet"
        href="chrome://browser/content/aiwindow/components/smartwindow-resume-section.css"
      />
      <div class="resume-section-heading">
        <span
          class="resume-section-title"
          data-l10n-id="aiwindow-resume-section-heading"
        ></span>
        ${hiddenCount
          ? html`
              <moz-button
                class="resume-section-toggle"
                type="ghost"
                size="small"
                @click=${this.#onToggleClick}
                data-l10n-id=${this.expanded
                  ? "aiwindow-resume-section-show-less"
                  : "aiwindow-resume-section-show-more"}
                data-l10n-args=${JSON.stringify({
                  count: this.cards.length,
                })}
              ></moz-button>
            `
          : nothing}
      </div>
      <div
        class="resume-section-grid"
        @smartwindow-resume-card:resume=${this.#onCardResume}
        @smartwindow-resume-card:dismiss=${this.#onCardDismiss}
        @smartwindow-resume-card:menu-item-selected=${this
          .#onCardMenuItemSelected}
      >
        ${visibleCards.map(
          ({ content, memory }) => html`
            <smartwindow-resume-card
              .content=${content}
              .journeyId=${memory.id}
            ></smartwindow-resume-card>
          `
        )}
      </div>
    `;
  }
}

customElements.define("smartwindow-resume-section", SmartwindowResumeSection);
