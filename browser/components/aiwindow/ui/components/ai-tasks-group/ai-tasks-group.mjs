/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

import { html, nothing } from "chrome://global/content/vendor/lit.all.mjs";
import { MozLitElement } from "chrome://global/content/lit-utils.mjs";

// Must match the `:nth-child(n + 6)` rule in ai-tasks-group.css.
const VISIBLE_COUNT = 5;

/**
 * A titled group of task cards on the Tasks page, for example
 * "Monitors · Active  2 of 15". Cards are slotted children; past the first
 * five, the rest are hidden behind a "Show N more" / "Show less" toggle.
 *
 * @property {string} headingL10nId - Fluent ID of the group heading.
 * @property {string} countL10nId - Fluent ID of the count shown next to the heading.
 * @property {object} countL10nArgs - Fluent args for the count.
 * @property {boolean} expanded - Whether every card is shown.
 */
export class AITasksGroup extends MozLitElement {
  static properties = {
    headingL10nId: { type: String },
    countL10nId: { type: String },
    countL10nArgs: { type: Object },
    expanded: { type: Boolean, reflect: true },
    itemCount: { type: Number, state: true },
  };

  #collapsing = false;

  constructor() {
    super();
    this.headingL10nId = "";
    this.countL10nId = "";
    this.countL10nArgs = {};
    this.expanded = false;
    this.itemCount = 0;
  }

  get hiddenCount() {
    return Math.max(0, this.itemCount - VISIBLE_COUNT);
  }

  handleSlotChange(event) {
    this.itemCount = event.target.assignedElements().length;
    if (!this.hiddenCount) {
      this.expanded = false;
    }
  }

  async toggleExpanded() {
    if (this.#collapsing) {
      return;
    }
    if (!this.expanded) {
      this.expanded = true;
      return;
    }
    this.#collapsing = true;
    try {
      await this.animateCollapse();
    } finally {
      this.#collapsing = false;
      this.expanded = false;
    }
  }

  /**
   * Fades out the cards past the first five, last card first, mirroring the
   * reveal in ai-tasks-group.css. Done with Web Animations because `display`
   * can't be transitioned.
   */
  async animateCollapse() {
    if (window.matchMedia("(prefers-reduced-motion: reduce)").matches) {
      return;
    }
    const easing = getComputedStyle(this)
      .getPropertyValue("--motion-easing-standard")
      .trim();
    const animations = this.shadowRoot
      .querySelector("slot")
      .assignedElements()
      .slice(VISIBLE_COUNT)
      .reverse()
      .map((card, i) =>
        card.animate(
          {
            opacity: [1, 0],
            translate: ["0 0", "0 calc(-1 * var(--space-small))"],
          },
          {
            duration: 200,
            delay: Math.min(i, 3) * 40,
            easing: easing || "ease",
            fill: "forwards",
          }
        )
      );
    await Promise.allSettled(animations.map(animation => animation.finished));
    animations.forEach(animation => animation.cancel());
  }

  toggleTemplate() {
    if (!this.hiddenCount) {
      return nothing;
    }
    return html`<button
      class="toggle-button"
      type="button"
      aria-expanded=${this.expanded}
      data-l10n-id=${this.expanded
        ? "ai-tasks-group-show-less"
        : "ai-tasks-group-show-more"}
      data-l10n-args=${JSON.stringify({ count: this.hiddenCount })}
      @click=${this.toggleExpanded}
    ></button>`;
  }

  render() {
    return html`
      <link
        rel="stylesheet"
        href="chrome://browser/content/aiwindow/components/ai-tasks-group.css"
      />
      <section aria-labelledby="heading">
        <h3 id="heading" class="heading">
          <span data-l10n-id=${this.headingL10nId}></span>
          ${this.countL10nId
            ? html`<span
                class="count"
                data-l10n-id=${this.countL10nId}
                data-l10n-args=${JSON.stringify(this.countL10nArgs)}
              ></span>`
            : nothing}
        </h3>
        <div class="items">
          <slot @slotchange=${this.handleSlotChange}></slot>
        </div>
        ${this.toggleTemplate()}
      </section>
    `;
  }
}

customElements.define("ai-tasks-group", AITasksGroup);
