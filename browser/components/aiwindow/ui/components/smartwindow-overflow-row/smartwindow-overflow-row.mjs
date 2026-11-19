/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/**
 * Renders and collapses overflowing items matching the defined selectors.
 *
 * @param {typeof import("chrome://global/content/lit-utils.mjs").MozLitElement} BaseElement
 * @returns {typeof BaseElement}
 */
export const SmartwindowOverflowRowMixin = BaseElement =>
  class extends BaseElement {
    static properties = {
      visibleCount: { type: Number, state: true },
    };

    #resizeObserver = null;
    #lastWidth = null;
    #measureRaf = 0;
    #widthChanged = true;

    constructor() {
      super();
      this.visibleCount = Infinity;
    }

    /**
     * @returns {string} Selector for the flex row.
     */
    get overflowContainerSelector() {
      return ".smartwindow-overflow-row";
    }

    /**
     * @returns {string} Selector for the items.
     */
    get overflowItemSelector() {
      return ":scope > [role='listitem']";
    }

    /**
     * @returns {?string} Selector for the “+n more” button.
     */
    get overflowTriggerSelector() {
      return ".overflow-more";
    }

    /**
     * @returns {number} Cap applied on top of what fits.
     */
    get maxInlineItems() {
      return Infinity;
    }

    /**
     * @returns {?number} Fixed inline count skips measurement.
     */
    get inlineItemCount() {
      return null;
    }

    /**
     * @returns {boolean} Whether the inline count comes from measuring.
     */
    get isWidthAware() {
      return !Number.isFinite(this.inlineItemCount);
    }

    /**
     * @returns {Array} The overflowing items.
     */
    get overflowItems() {
      return [];
    }

    /**
     * Whether a width-aware measure is scheduled. Exposed for tests to await.
     *
     * @returns {boolean}
     */
    get isMeasuring() {
      return !!this.#measureRaf;
    }

    connectedCallback() {
      super.connectedCallback();
      if (this.hasUpdated) {
        this.syncOverflowMode();
      }
    }

    disconnectedCallback() {
      super.disconnectedCallback();
      this.#stopMeasuring();
    }

    updated(changed) {
      /* Getters are consumer-defined and measuring is a no-op for an unchanged
         count. Run on every render to ensure we don’t measure a stale tree. */
      super.updated(changed);
      this.syncOverflowMode();
    }

    /* Apply the current overflow mode. */
    syncOverflowMode() {
      if (this.isWidthAware) {
        this.#observeWidth();
        this.scheduleOverflowMeasure();
        return;
      }
      this.#stopMeasuring();
      this.#setVisibleCount(Math.max(0, this.inlineItemCount));
    }

    /* Measure next frame so layout has settled. */
    scheduleOverflowMeasure() {
      if (this.#measureRaf || !this.isWidthAware) {
        return;
      }
      this.#measureRaf = requestAnimationFrame(() => {
        this.#measureRaf = 0;
        this.#measureOverflow();
      });
    }

    #observeWidth() {
      this.#resizeObserver ??= new ResizeObserver(entries => {
        const inlineSize = entries[0]?.contentBoxSize?.[0]?.inlineSize;
        if (inlineSize == null || inlineSize === this.#lastWidth) {
          return;
        }
        this.#lastWidth = inlineSize;
        this.#widthChanged = true;
        this.scheduleOverflowMeasure();
      });
      this.#resizeObserver.observe(this);
    }

    #stopMeasuring() {
      this.#resizeObserver?.disconnect();
      this.#resizeObserver = null;
      this.#lastWidth = null;
      cancelAnimationFrame(this.#measureRaf);
      this.#measureRaf = 0;
    }

    #setVisibleCount(count) {
      if (this.visibleCount !== count) {
        this.visibleCount = count;
      }
    }

    #measureOverflow() {
      const container = this.renderRoot?.querySelector(
        this.overflowContainerSelector
      );
      const items = this.overflowItems;
      if (!container || !items.length) {
        return;
      }

      const children = [
        ...container.querySelectorAll(this.overflowItemSelector),
      ];
      // A stale render would measure the wrong nodes.
      if (children.length !== items.length) {
        return;
      }

      const columnGap = parseFloat(getComputedStyle(container).columnGap) || 0;
      const cumulativeItemWidths = [0];
      for (const child of children) {
        cumulativeItemWidths.push(
          cumulativeItemWidths.at(-1) +
            child.getBoundingClientRect().width +
            columnGap
        );
      }

      // The trigger is only rendered if the row overflows.
      if (
        children.length <= this.maxInlineItems &&
        cumulativeItemWidths.at(-1) - columnGap <= container.clientWidth
      ) {
        this.#setVisibleCount(children.length);
        return;
      }

      const trigger = this.overflowTriggerSelector
        ? container.querySelector(this.overflowTriggerSelector)
        : null;
      const triggerWidth = trigger?.getBoundingClientRect().width ?? 0;
      let visibleCount = Math.min(children.length, this.maxInlineItems);
      while (
        visibleCount &&
        cumulativeItemWidths[visibleCount] + triggerWidth >
          container.clientWidth
      ) {
        visibleCount--;
      }
      // Only reveal more items when the row got wider.
      if (visibleCount > this.visibleCount && !this.#widthChanged) {
        return;
      }
      this.#widthChanged = false;
      this.#setVisibleCount(visibleCount);
    }
  };
