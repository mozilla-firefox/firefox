/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

import {
  html,
  keyed,
  nothing,
} from "chrome://global/content/vendor/lit.all.mjs";
import { MozLitElement } from "chrome://global/content/lit-utils.mjs";
// eslint-disable-next-line import-x/no-unassigned-import
import "chrome://global/content/elements/moz-button.mjs";
// eslint-disable-next-line import-x/no-unassigned-import
import "chrome://browser/content/aiwindow/components/website-chip-container.mjs";

/**
 * Renders the result of a natural language action performed by the assistant
 * (e.g. "Closed tabs"). Shows the action label, summary, and an undo button
 * when available. Clicking the header toggles the expanded state, which
 * reveals a list of stacked rows, each with its own label and optional
 * affected items (website chips).
 *
 * Dispatches a CustomEvent named action-result-undo when the user clicks undo.
 * The parent is responsible for performing the actual reversal and updating
 * the action state.
 *
 * @attribute {string} label - Header label for plain text (e.g. "Closed tab", "Closed 3 tabs")
 * @attribute {string} labelL10nId - Fluent localization ID for the header label
 * @attribute {object} labelL10nArgs - Arguments for the label localization (e.g. { count: 3 })
 * @property {object} labelLink - Optional { l10nName, href } embedded in
 *   the header label as a target=_blank link. Requires matching
 *   <a data-l10n-name> in the l10n message.
 * @attribute {string} summary - Descriptive text for the action (plain text)
 * @attribute {string} summaryL10nId - Fluent localization ID for the summary
 * @attribute {object} summaryL10nArgs - Arguments for the summary localization
 * @attribute {boolean} canUndo - Whether the undo button should be shown
 * @attribute {boolean} isExpanded - Whether the detail section is visible
 * @attribute {boolean} isLoading - Whether the action is still in progress, which
 *  gives the header label an animated gradient treatment
 * @property {Array} rows - List of stacked dot rows each shaped:
 *  {
 *    label?: string,           // Plain text label
 *    labelL10nId?: string,     // Fluent localization ID for the row label
 *    labelL10nArgs?: Object,   // Arguments for the row label localization
 *    link?: { l10nName: string, href: string },
 *                              // Optional link, same shape as labelLink
 *    items?: Array<{ url: string, label: string }>
 *  }
 */
export class AIActionResult extends MozLitElement {
  static properties = {
    label: { type: String },
    labelL10nId: { type: String },
    labelL10nArgs: { type: Object },
    labelLink: { type: Object },
    rows: { type: Array },
    summary: { type: String },
    summaryL10nId: { type: String },
    summaryL10nArgs: { type: Object },
    canUndo: { type: Boolean, attribute: "can-undo", reflect: true },
    isExpanded: { type: Boolean, attribute: "is-expanded", reflect: true },
    isLoading: { type: Boolean, attribute: "is-loading", reflect: true },
  };

  constructor() {
    super();
    this.label = "";
    this.labelL10nId = null;
    this.labelL10nArgs = null;
    this.labelLink = null;
    this.rows = [];
    this.summary = "";
    this.summaryL10nId = null;
    this.summaryL10nArgs = null;
    this.canUndo = false;
    this.isExpanded = false;
    this.isLoading = false;
  }

  // One shimmer sweep, matching the CSS `actionLogShimmer` duration.
  static SHIMMER_CYCLE_MS = 2000;
  // Fallback for finishing the sweep if the running animation can't be found
  // (e.g. reduced motion, where it isn't animating) — one cycle plus a buffer.
  static SWEEP_MAX_MS = 2200;

  #sweepTimer = null;
  #sweepAnim = null;
  #awaitingSweep = false;
  #loadingLabelSnapshot = null;

  willUpdate(changed) {
    if (this.isLoading) {
      // Snapshot the loading label so it keeps shimmering until the sweep ends.
      this.#loadingLabelSnapshot = {
        labelL10nId: this.labelL10nId,
        labelL10nArgs: this.labelL10nArgs,
        label: this.label,
        labelLink: this.labelLink,
      };
      this.#awaitingSweep = false;
      this.#cancelSweepAnim();
      this.removeAttribute("settling");
      this.#clearSweepTimer();
    } else if (
      changed.has("isLoading") &&
      changed.get("isLoading") &&
      this.#loadingLabelSnapshot
    ) {
      // Loading finished: let the shimmer finish its current sweep across the
      // label (see #finishCurrentSweep in updated) before swapping to the
      // completed text. The timer is a fallback if the running animation can't
      // be found.
      this.#awaitingSweep = true;
      this.#clearSweepTimer();
      this.#sweepTimer = setTimeout(() => {
        this.#sweepTimer = null;
        this.#finishSweep();
      }, AIActionResult.SWEEP_MAX_MS);
    }
    // Drive the shimmer treatment while loading or finishing the last sweep.
    // Clear the settle state together with the shimmer so the infinite CSS loop
    // can't re-apply for a frame (which would flash a restarted sweep).
    const shimmering = this.isLoading || this.#awaitingSweep;
    this.toggleAttribute("shimmering", shimmering);
    if (!shimmering) {
      this.removeAttribute("settling");
      this.#cancelSweepAnim();
    }
  }

  updated() {
    if (this.#awaitingSweep && !this.#sweepAnim) {
      this.#finishCurrentSweep();
    }
  }

  // Run one final sweep from the shimmer's current position to the end and hold
  // there, then swap to the completed label. The [settling] state stops the
  // infinite CSS loop (so it can't reset to the start mid-swap and cut the sweep
  // short), while a scripted animation — which the CSS can't override — drives
  // the finish continuously from wherever the loop currently is.
  #finishCurrentSweep() {
    const label = this.renderRoot?.querySelector(".action-result-label");
    const loop = label
      ?.getAnimations?.()
      .find(a => a.animationName === "actionLogShimmer");
    if (!label || !loop) {
      // Nothing is animating (e.g. reduced motion) — swap right away.
      this.#finishSweep();
      return;
    }
    const cycle = AIActionResult.SHIMMER_CYCLE_MS;
    const elapsed = (Number(loop.currentTime) || 0) % cycle;
    // Match the CSS ease-in-out so the handoff from the loop has no visible jump.
    const linear = elapsed / cycle;
    const eased =
      linear < 0.5 ? 2 * linear * linear : 1 - Math.pow(-2 * linear + 2, 2) / 2;
    // The loop sweeps background-position-x from 100% to -100% over one cycle.
    const currentX = 100 - 200 * eased;
    // The gradient repeats every 200%, so the highlight sits at the right edge
    // of the label (its end) at multiples of 200% and at the left edge at odd
    // multiples of 100%. Continue the sweep to the next right-edge position so it
    // reads as reaching the end of the label instead of snapping back to the
    // left. Scale the duration to the remaining travel so the speed is steady.
    const PERIOD = 200;
    const targetX = Math.floor(currentX / PERIOD) * PERIOD;
    const travel = currentX - targetX;
    this.setAttribute("settling", "");
    this.#sweepAnim = label.animate(
      [
        { backgroundPositionX: `${currentX}%` },
        { backgroundPositionX: `${targetX}%` },
      ],
      {
        duration: Math.min(1000, Math.max(300, (travel / PERIOD) * cycle)),
        easing: "ease-out",
        fill: "forwards",
      }
    );
    this.#sweepAnim.finished
      .then(() => this.#finishSweep())
      .catch(() => this.#finishSweep());
  }

  connectedCallback() {
    super.connectedCallback();
    // The label link is a plain <a target="_blank"> that the browser opens
    // natively. It lives inside the header toggle button, so keep its click
    // from also toggling the card. Delegated in the capture phase because
    // Fluent DOM overlays replace the anchor node, dropping per-node listeners.
    this.renderRoot.addEventListener("click", this.#handleLabelLinkClick, true);
  }

  disconnectedCallback() {
    super.disconnectedCallback();
    this.#clearSweepTimer();
    this.#cancelSweepAnim();
    this.renderRoot.removeEventListener(
      "click",
      this.#handleLabelLinkClick,
      true
    );
  }

  #clearSweepTimer() {
    if (this.#sweepTimer) {
      clearTimeout(this.#sweepTimer);
      this.#sweepTimer = null;
    }
  }

  #cancelSweepAnim() {
    if (this.#sweepAnim) {
      this.#sweepAnim.cancel();
      this.#sweepAnim = null;
    }
  }

  #finishSweep() {
    if (this.#awaitingSweep) {
      // Leave [settling] and the holding animation in place until willUpdate
      // drops the shimmer, so the end frame holds until the completed label
      // renders. willUpdate then clears both.
      this.#awaitingSweep = false;
      this.#clearSweepTimer();
      this.requestUpdate();
    }
  }

  #handleUndo() {
    this.dispatchEvent(
      new CustomEvent("action-result-undo", { bubbles: true, composed: true })
    );
  }

  #handleLabelLinkClick = event => {
    const onLabelLink = event
      .composedPath()
      .some(
        el =>
          el instanceof HTMLAnchorElement &&
          el.classList.contains("action-result-label-link")
      );
    if (onLabelLink) {
      event.stopPropagation();
    }
  };

  #handleToggle() {
    this.isExpanded = !this.isExpanded;
    this.dispatchEvent(
      new CustomEvent("action-result-toggle", {
        detail: { isExpanded: this.isExpanded },
        bubbles: true,
        composed: true,
      })
    );
  }

  // Fluent replaces the label's children (including Lit's part markers) when
  // it overlays a linked message, so callers key the label on whether it has
  // a link to get a fresh element rather than patching the translated one.
  #renderLabelContent(link, l10nId, fallbackLabel) {
    if (link) {
      return html`<a
        class="action-result-label-link"
        data-l10n-name=${link.l10nName}
        href=${link.href}
        target="_blank"
        rel="noopener"
      ></a>`;
    }
    if (l10nId) {
      return "";
    }
    return fallbackLabel;
  }

  render() {
    // While loading or finishing the last sweep, keep showing the shimmering
    // label; only swap to the completed text once the sweep ends.
    const shimmering = this.isLoading || this.#awaitingSweep;
    const label =
      shimmering && this.#loadingLabelSnapshot
        ? this.#loadingLabelSnapshot
        : {
            labelL10nId: this.labelL10nId,
            labelL10nArgs: this.labelL10nArgs,
            label: this.label,
            labelLink: this.labelLink,
          };
    return html`
      <link
        rel="stylesheet"
        href="chrome://browser/content/aiwindow/components/ai-action-result.css"
      />
      <div class="action-result-wrapper">
        <button
          type="button"
          class="action-result-header"
          aria-expanded=${this.isExpanded}
          @click=${this.#handleToggle}
        >
          ${keyed(
            !!label.labelLink,
            html`<span
              class="action-result-label"
              data-l10n-id=${label.labelL10nId || nothing}
              data-l10n-args=${label.labelL10nArgs
                ? JSON.stringify(label.labelL10nArgs)
                : nothing}
            >
              ${this.#renderLabelContent(
                label.labelLink,
                label.labelL10nId,
                label.label
              )}
            </span>`
          )}
        </button>
        ${this.#renderDetails()}
      </div>
    `;
  }

  #renderDetails() {
    return html`
      ${this.isExpanded
        ? html`
            <div class="action-result-expanded">
              ${this.rows.map(
                row => html`
                  <div class="action-result-expanded-row">
                    <div class="action-result-expanded-row-header">
                      <span class="action-result-dot" aria-hidden="true"></span>
                      ${keyed(
                        !!row.link,
                        html`<span
                          class="action-result-expanded-row-label"
                          data-l10n-id=${row.labelL10nId || nothing}
                          data-l10n-args=${row.labelL10nArgs
                            ? JSON.stringify(row.labelL10nArgs)
                            : nothing}
                        >
                          ${this.#renderLabelContent(
                            row.link,
                            row.labelL10nId,
                            row.label
                          )}
                        </span>`
                      )}
                    </div>
                    ${row.items?.length
                      ? html`
                          <website-chip-container
                            class="action-result-chips"
                            .websites=${row.items}
                            .autoOverflow=${true}
                          ></website-chip-container>
                        `
                      : nothing}
                  </div>
                `
              )}
            </div>
          `
        : nothing}
      ${this.summary || this.summaryL10nId
        ? html`<p
            class="action-result-summary"
            data-l10n-id=${this.summaryL10nId || nothing}
            data-l10n-args=${this.summaryL10nArgs
              ? JSON.stringify(this.summaryL10nArgs)
              : nothing}
          >
            ${!this.summaryL10nId ? this.summary : ""}
          </p>`
        : nothing}
      ${this.canUndo
        ? html`
            <moz-button
              class="action-result-undo"
              @click=${this.#handleUndo}
              data-l10n-id="smartwindow-nl-undo-button"
              type="ghost"
            ></moz-button>
          `
        : nothing}
    `;
  }
}

customElements.define("ai-action-result", AIActionResult);
