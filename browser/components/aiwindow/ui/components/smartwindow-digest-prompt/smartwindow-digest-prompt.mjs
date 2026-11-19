/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

import { html, classMap } from "chrome://global/content/vendor/lit.all.mjs";
import { MozLitElement } from "chrome://global/content/lit-utils.mjs";
// eslint-disable-next-line import-x/no-unassigned-import
import "chrome://global/content/elements/moz-button.mjs";

const lazy = {};
if (!window.IS_STORYBOOK) {
  ChromeUtils.defineESModuleGetters(lazy, {
    DIGEST_HIGHLIGHT_CLEARED:
      "moz-src:///browser/components/aiwindow/ui/modules/DigestActivity.sys.mjs",
    DigestActivity:
      "moz-src:///browser/components/aiwindow/ui/modules/DigestActivity.sys.mjs",
    DigestManager:
      "moz-src:///browser/components/aiwindow/models/memories/digests/DigestManager.sys.mjs",
  });
}

/**
 * A digest conversation starter prompt, rendered inside smartwindow-prompts.
 * Clicking it fires prompt-selected and ends its highlight in every window.
 * It is otherwise highlighted until highlightExpiresAt, and its dismiss button
 * disables the digest and fires prompt-dismissed so the owner can remove the
 * pill.
 *
 * @property {{id: string, title: string}} prompt - The digest's ID and
 *   title, which is the pill's label.
 * @property {number} highlightExpiresAt - Epoch milliseconds when the
 *   highlight ends, or 0 for no highlight.
 */
export class SmartWindowDigestPrompt extends MozLitElement {
  static properties = {
    prompt: { type: Object },
    highlightExpiresAt: { type: Number },
    highlighted: { state: true },
  };

  #highlightTimer = null;

  constructor() {
    super();
    this.prompt = null;
    this.highlightExpiresAt = 0;
    this.highlighted = false;
  }

  // Render into smartwindow-prompts' shadow root so the pill shares its styles.
  createRenderRoot() {
    return this;
  }

  connectedCallback() {
    super.connectedCallback();
    if (!window.IS_STORYBOOK) {
      Services.obs.addObserver(
        this.#onHighlightCleared,
        lazy.DIGEST_HIGHLIGHT_CLEARED
      );
      // Unsubscribe from the highlight notifications when the tab closes, since
      // closing a tab doesn't call disconnectedCallback. Otherwise Services.obs
      // keeps this pill, and its whole page, in memory.
      window.addEventListener("unload", () => this.remove(), { once: true });
    }
  }

  disconnectedCallback() {
    super.disconnectedCallback();
    if (!window.IS_STORYBOOK) {
      Services.obs.removeObserver(
        this.#onHighlightCleared,
        lazy.DIGEST_HIGHLIGHT_CLEARED
      );
    }
    this.#clearHighlightTimer();
  }

  #onHighlightCleared = (_subject, _topic, id) => {
    if (id === this.prompt?.id) {
      this.#clearHighlightTimer();
      this.highlighted = false;
    }
  };

  willUpdate(changedProperties) {
    if (changedProperties.has("highlightExpiresAt")) {
      this.#scheduleHighlight();
    }
  }

  #scheduleHighlight() {
    this.#clearHighlightTimer();
    const remaining = this.highlightExpiresAt - Date.now();
    this.highlighted = remaining > 0;
    if (this.highlighted) {
      this.#highlightTimer = setTimeout(() => {
        this.highlighted = false;
      }, remaining);
    }
  }

  #clearHighlightTimer() {
    clearTimeout(this.#highlightTimer);
    this.#highlightTimer = null;
  }

  #dispatch(eventName, detail) {
    this.dispatchEvent(
      new CustomEvent(`SmartWindowPrompt:${eventName}`, {
        detail,
        bubbles: true,
        composed: true,
      })
    );
  }

  #onSelected() {
    this.#clearHighlightTimer();
    this.highlighted = false;
    const { id, title } = this.prompt;
    lazy.DigestActivity?.clearHighlight(id);
    this.#dispatch("prompt-selected", { text: title, type: "digest", id });
  }

  async #onDismiss(e) {
    e.stopPropagation();
    const { id } = this.prompt;
    this.#dispatch("prompt-dismissed", { type: "digest", id });
    lazy.DigestActivity?.dismiss(id);
    try {
      await lazy.DigestManager?.disableDigest(id);
    } catch (error) {
      console.error("Failed to disable digest:", error);
    }
  }

  #hasInteracted(e) {
    e.currentTarget.classList.add("has-interacted");
  }

  render() {
    if (!this.prompt) {
      return html``;
    }
    const { title } = this.prompt;
    return html`
      <span class="sw-prompt">
        <moz-button
          class=${classMap({
            "sw-prompt-button": true,
            "sw-digest-highlighted": this.highlighted,
          })}
          @click=${this.#onSelected}
          @mouseenter=${this.#hasInteracted}
          @focusin=${this.#hasInteracted}
          aria-label=${title}
        >
          ${title}
        </moz-button>
        <button
          class="sw-prompt-dismiss"
          @click=${this.#onDismiss}
          data-l10n-id="aiwindow-starter-digest-dismiss"
          data-l10n-args=${JSON.stringify({ text: title })}
        >
          <img
            class="sw-prompt-dismiss-icon"
            src="chrome://global/skin/icons/delete.svg"
            alt=""
          />
        </button>
      </span>
    `;
  }
}

customElements.define("smartwindow-digest-prompt", SmartWindowDigestPrompt);
