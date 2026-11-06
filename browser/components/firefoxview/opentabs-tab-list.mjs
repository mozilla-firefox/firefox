/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

import {
  classMap,
  html,
  ifDefined,
  styleMap,
  when,
} from "chrome://global/content/vendor/lit.all.mjs";
import {
  FxviewTabListBase,
  FxviewTabRowBase,
} from "chrome://browser/content/firefoxview/fxview-tab-list.mjs";
// eslint-disable-next-line import-x/no-unassigned-import
import "chrome://global/content/elements/moz-button.mjs";

/**
 * A list of clickable tab items
 *
 * @property {boolean} pinnedTabsGridView - Whether to show pinned tabs in a grid view
 */

export class OpenTabsTabList extends FxviewTabListBase {
  constructor() {
    super();
    this.pinnedTabsGridView = false;
    this.pinnedTabs = [];
    this.unpinnedTabs = [];
  }

  static properties = {
    pinnedTabsGridView: { type: Boolean },
  };

  static queries = {
    ...FxviewTabListBase.queries,
    rowEls: {
      all: "opentabs-tab-row",
    },
  };

  willUpdate(changes) {
    this.activeIndex = Math.min(
      Math.max(this.activeIndex, 0),
      this.tabItems.length - 1
    );

    if (changes.has("dateTimeFormat") || changes.has("updatesPaused")) {
      this.clearIntervalTimer();
      if (!this.updatesPaused && this.dateTimeFormat == "relative") {
        this.startIntervalTimer();
        this.onIntervalUpdate();
      }
    }

    // Move pinned tabs to the beginning of the list
    if (this.pinnedTabsGridView) {
      // Can set maxTabsLength to -1 to have no max
      this.unpinnedTabs = this.tabItems.filter(
        tab => !tab.indicators.includes("pinned")
      );
      this.pinnedTabs = this.tabItems.filter(tab =>
        tab.indicators.includes("pinned")
      );
      if (this.maxTabsLength > 0) {
        this.unpinnedTabs = this.unpinnedTabs.slice(0, this.maxTabsLength);
      }
      this.tabItems = [...this.pinnedTabs, ...this.unpinnedTabs];
    } else if (this.maxTabsLength > 0) {
      this.tabItems = this.tabItems.slice(0, this.maxTabsLength);
    }
  }

  /**
   * Focuses the expected element (either the link or button) within fxview-tab-row
   * The currently focused/active element ID within a row is stored in this.currentActiveElementId
   */
  handleFocusElementInRow(e) {
    let fxviewTabRow = e.target;
    if (e.code == "ArrowUp") {
      // Focus either the link or button of the previous row based on this.currentActiveElementId
      e.preventDefault();
      if (
        (this.pinnedTabsGridView &&
          this.activeIndex >= this.pinnedTabs.length) ||
        !this.pinnedTabsGridView
      ) {
        this.focusPrevRow();
      }
    } else if (e.code == "ArrowDown") {
      // Focus either the link or button of the next row based on this.currentActiveElementId
      e.preventDefault();
      if (
        this.pinnedTabsGridView &&
        this.activeIndex < this.pinnedTabs.length
      ) {
        this.focusIndex(this.pinnedTabs.length);
      } else {
        this.focusNextRow();
      }
    } else if (e.code == "ArrowRight") {
      // Focus either the link or the button in the current row and
      // set this.currentActiveElementId to that element's ID
      e.preventDefault();
      if (document.dir == "rtl") {
        fxviewTabRow.moveFocusLeft();
      } else {
        fxviewTabRow.moveFocusRight();
      }
    } else if (e.code == "ArrowLeft") {
      // Focus either the link or the button in the current row and
      // set this.currentActiveElementId to that element's ID
      e.preventDefault();
      if (document.dir == "rtl") {
        fxviewTabRow.moveFocusRight();
      } else {
        fxviewTabRow.moveFocusLeft();
      }
    }
  }

  async focusIndex(index) {
    // Focus link or button of item
    if (
      (this.pinnedTabsGridView && index > this.pinnedTabs.length) ||
      !this.pinnedTabsGridView
    ) {
      let row = this.rootVirtualListEl.getItem(index - this.pinnedTabs.length);
      if (!row) {
        return;
      }
      let subList = this.rootVirtualListEl.getSubListForItem(
        index - this.pinnedTabs.length
      );
      if (!subList) {
        return;
      }
      this.activeIndex = index;

      // In Bug 1866845, these manual updates to the sublists should be removed
      // and scrollIntoView() should also be iterated on so that we aren't constantly
      // moving the focused item to the center of the viewport
      for (const sublist of Array.from(this.rootVirtualListEl.children)) {
        await sublist.requestUpdate();
        await sublist.updateComplete;
      }
      row.scrollIntoView({ block: "center" });
      row.focus();
    } else if (index >= 0 && index < this.rowEls?.length) {
      this.rowEls[index].focus();
      this.activeIndex = index;
    }
  }

  #getTabListWrapperClasses() {
    let wrapperClasses = ["fxview-tab-list"];
    let tabsToCheck = this.pinnedTabsGridView
      ? this.unpinnedTabs
      : this.tabItems;
    if (tabsToCheck.some(tab => tab.containerObj)) {
      wrapperClasses.push(`hasContainerTab`);
    }
    return wrapperClasses;
  }

  itemTemplate = (tabItem, i) => {
    let time;
    if (tabItem.time || tabItem.closedAt) {
      let stringTime = (tabItem.time || tabItem.closedAt).toString();
      // Different APIs return time in different units, so we use
      // the length to decide if it's milliseconds or nanoseconds.
      if (stringTime.length === 16) {
        time = (tabItem.time || tabItem.closedAt) / 1000;
      } else {
        time = tabItem.time || tabItem.closedAt;
      }
    }

    return html`<opentabs-tab-row
      ?active=${i == this.activeIndex}
      class=${classMap({
        pinned:
          this.pinnedTabsGridView && tabItem.indicators?.includes("pinned"),
      })}
      .currentActiveElementId=${this.currentActiveElementId}
      .favicon=${tabItem.icon}
      .compact=${this.compactRows}
      .containerObj=${ifDefined(tabItem.containerObj)}
      .indicators=${tabItem.indicators}
      .pinnedTabsGridView=${ifDefined(this.pinnedTabsGridView)}
      .primaryL10nId=${tabItem.primaryL10nId}
      .primaryL10nArgs=${ifDefined(tabItem.primaryL10nArgs)}
      .secondaryL10nId=${tabItem.secondaryL10nId}
      .secondaryL10nArgs=${ifDefined(tabItem.secondaryL10nArgs)}
      .tertiaryL10nId=${ifDefined(tabItem.tertiaryL10nId)}
      .tertiaryL10nArgs=${ifDefined(tabItem.tertiaryL10nArgs)}
      .secondaryActionClass=${this.secondaryActionClass}
      .tertiaryActionClass=${ifDefined(this.tertiaryActionClass)}
      .sourceClosedId=${ifDefined(tabItem.sourceClosedId)}
      .sourceWindowId=${ifDefined(tabItem.sourceWindowId)}
      .closedId=${ifDefined(tabItem.closedId || tabItem.closedId)}
      role=${tabItem.pinned && this.pinnedTabsGridView ? "tab" : "listitem"}
      .tabElement=${ifDefined(tabItem.tabElement)}
      .time=${ifDefined(time)}
      .title=${tabItem.title}
      .url=${tabItem.url}
      .searchQuery=${ifDefined(this.searchQuery)}
      .timeMsPref=${ifDefined(this.timeMsPref)}
      .hasPopup=${this.hasPopup}
      .dateTimeFormat=${this.dateTimeFormat}
    ></opentabs-tab-row>`;
  };

  render() {
    if (this.searchQuery && this.tabItems.length === 0) {
      return this.emptySearchResultsTemplate();
    }
    return html`
      ${this.stylesheets()}
      <link
        rel="stylesheet"
        href="chrome://browser/content/firefoxview/opentabs-tab-list.css"
      />
      ${when(
        this.pinnedTabsGridView && this.pinnedTabs.length,
        () => html`
          <div
            id="fxview-tab-list"
            class="fxview-tab-list pinned"
            data-l10n-id="firefoxview-pinned-tabs"
            role="tablist"
            @keydown=${this.handleFocusElementInRow}
          >
            ${this.pinnedTabs.map((tabItem, i) =>
              this.customItemTemplate
                ? this.customItemTemplate(tabItem, i)
                : this.itemTemplate(tabItem, i)
            )}
          </div>
        `
      )}
      <div
        id="fxview-tab-list"
        class=${this.#getTabListWrapperClasses().join(" ")}
        data-l10n-id="firefoxview-tabs"
        role="list"
        @keydown=${this.handleFocusElementInRow}
      >
        <virtual-list
          .activeIndex=${this.activeIndex}
          .pinnedTabsIndexOffset=${this.pinnedTabsGridView
            ? this.pinnedTabs.length
            : 0}
          .items=${this.pinnedTabsGridView ? this.unpinnedTabs : this.tabItems}
          .template=${this.itemTemplate}
        ></virtual-list>
      </div>
      <slot name="menu"></slot>
    `;
  }
}
customElements.define("opentabs-tab-list", OpenTabsTabList);

/**
 * A tab item that displays favicon, title, url, and time of last access
 *
 * @property {object} containerObj - Info about an open tab's container if within one
 * @property {string} indicators - An array of tab indicators if any are present
 * @property {boolean} pinnedTabsGridView - Whether the show pinned tabs in a grid view
 */

export class OpenTabsTabRow extends FxviewTabRowBase {
  constructor() {
    super();
    this.indicators = [];
    this.pinnedTabsGridView = false;
  }

  static properties = {
    ...FxviewTabRowBase.properties,
    containerObj: { type: Object },
    indicators: { type: Array },
    pinnedTabsGridView: { type: Boolean },
  };

  static queries = {
    ...FxviewTabRowBase.queries,
    mediaButtonEl: "#fxview-tab-row-media-button",
    pinnedTabButtonEl: "moz-button#fxview-tab-row-main",
  };

  connectedCallback() {
    super.connectedCallback();
    this.addEventListener("keydown", this.handleKeydown);
  }

  disconnectedCallback() {
    super.disconnectedCallback();
    this.removeEventListener("keydown", this.handleKeydown);
  }

  handleKeydown(e) {
    if (
      this.active &&
      this.pinnedTabsGridView &&
      this.indicators?.includes("pinned") &&
      e.key === "m" &&
      e.ctrlKey
    ) {
      this.muteOrUnmuteTab();
    }
  }

  moveFocusRight() {
    let tabList = this.getRootNode().host;
    if (this.pinnedTabsGridView && this.indicators?.includes("pinned")) {
      tabList.focusNextRow();
    } else if (
      (this.indicators?.includes("soundplaying") ||
        this.indicators?.includes("muted")) &&
      this.currentActiveElementId === "fxview-tab-row-main"
    ) {
      this.focusMediaButton();
    } else if (
      this.currentActiveElementId === "fxview-tab-row-media-button" ||
      this.currentActiveElementId === "fxview-tab-row-main"
    ) {
      this.focusSecondaryButton();
    } else if (
      this.tertiaryButtonEl &&
      this.currentActiveElementId === "fxview-tab-row-secondary-button"
    ) {
      this.focusTertiaryButton();
    }
  }

  moveFocusLeft() {
    let tabList = this.getRootNode().host;
    if (
      this.pinnedTabsGridView &&
      (this.indicators?.includes("pinned") ||
        (tabList.currentActiveElementId === "fxview-tab-row-main" &&
          tabList.activeIndex === tabList.pinnedTabs.length))
    ) {
      tabList.focusPrevRow();
    } else if (
      tabList.currentActiveElementId === "fxview-tab-row-tertiary-button"
    ) {
      this.focusSecondaryButton();
    } else if (
      (this.indicators?.includes("soundplaying") ||
        this.indicators?.includes("muted")) &&
      tabList.currentActiveElementId === "fxview-tab-row-secondary-button"
    ) {
      this.focusMediaButton();
    } else {
      this.focusLink();
    }
  }

  focusMediaButton() {
    let tabList = this.getRootNode().host;
    this.mediaButtonEl.focus();
    tabList.currentActiveElementId = this.mediaButtonEl.id;
  }

  #secondaryActionHandler(event) {
    if (
      (this.pinnedTabsGridView &&
        this.indicators?.includes("pinned") &&
        event.type == "contextmenu") ||
      (event.type == "click" && event.detail && !event.altKey) ||
      // detail=0 is from keyboard
      (event.type == "click" && !event.detail)
    ) {
      event.preventDefault();
      this.dispatchEvent(
        new CustomEvent("fxview-tab-list-secondary-action", {
          bubbles: true,
          composed: true,
          detail: { originalEvent: event, item: this },
        })
      );
    }
  }

  #faviconTemplate() {
    return html`<span
      class=${classMap({
        "fxview-tab-row-favicon-wrapper": true,
        pinned: this.indicators?.includes("pinned"),
        pinnedOnNewTab: this.indicators?.includes("pinnedOnNewTab"),
        attention: this.indicators?.includes("attention"),
        bookmark: this.indicators?.includes("bookmark"),
      })}
    >
      <span
        class="fxview-tab-row-favicon icon"
        id="fxview-tab-row-favicon"
        style=${styleMap({
          backgroundImage: `url(${this.getImageUrl(this.favicon, this.url)})`,
        })}
      ></span>
      ${when(
        this.pinnedTabsGridView &&
          this.indicators?.includes("pinned") &&
          (this.indicators?.includes("muted") ||
            this.indicators?.includes("soundplaying")),
        () => html`
          <button
            class="fxview-tab-row-pinned-media-button"
            id="fxview-tab-row-media-button"
            tabindex="-1"
            data-l10n-id=${this.indicators?.includes("muted")
              ? "fxviewtabrow-unmute-tab-button-no-context"
              : "fxviewtabrow-mute-tab-button-no-context"}
            muted=${this.indicators?.includes("muted")}
            soundplaying=${this.indicators?.includes("soundplaying") &&
            !this.indicators?.includes("muted")}
            @click=${this.muteOrUnmuteTab}
          ></button>
        `
      )}
    </span>`;
  }

  #getContainerClasses() {
    let containerClasses = ["fxview-tab-row-container-indicator", "icon"];
    if (this.containerObj) {
      let { icon, color } = this.containerObj;
      containerClasses.push(`identity-icon-${icon}`);
      containerClasses.push(`identity-color-${color}`);
    }
    return containerClasses;
  }

  muteOrUnmuteTab(e) {
    e?.preventDefault();
    // If the tab has no sound playing, the mute/unmute button will be removed when toggled.
    // We should move the focus to the right in that case. This does not apply to pinned tabs
    // on the Open Tabs page.
    let shouldMoveFocus =
      (!this.pinnedTabsGridView ||
        (!this.indicators.includes("pinned") && this.pinnedTabsGridView)) &&
      this.mediaButtonEl &&
      !this.indicators.includes("soundplaying") &&
      this.currentActiveElementId === "fxview-tab-row-media-button";

    // detail=0 is from keyboard
    if (e?.type == "click" && !e?.detail && shouldMoveFocus) {
      if (document.dir == "rtl") {
        this.moveFocusLeft();
      } else {
        this.moveFocusRight();
      }
    }
    this.tabElement.toggleMuteAudio();
  }

  #mediaButtonTemplate() {
    return html`${when(
      this.indicators?.includes("soundplaying") ||
        this.indicators?.includes("muted"),
      () =>
        html`<moz-button
          type="icon ghost"
          class="fxview-tab-row-button"
          id="fxview-tab-row-media-button"
          data-l10n-id=${this.indicators?.includes("muted")
            ? "fxviewtabrow-unmute-tab-button-no-context"
            : "fxviewtabrow-mute-tab-button-no-context"}
          muted=${this.indicators?.includes("muted")}
          soundplaying=${this.indicators?.includes("soundplaying") &&
          !this.indicators?.includes("muted")}
          @click=${this.muteOrUnmuteTab}
          tabindex=${this.active &&
          this.currentActiveElementId === "fxview-tab-row-media-button"
            ? "0"
            : "-1"}
        ></moz-button>`,
      () => html`<span></span>`
    )}`;
  }

  #containerIndicatorTemplate() {
    let tabList = this.getRootNode().host;
    let tabsToCheck = tabList.pinnedTabsGridView
      ? tabList.unpinnedTabs
      : tabList.tabItems;
    return html`${when(
      tabsToCheck.some(tab => tab.containerObj),
      () => html`<span class=${this.#getContainerClasses().join(" ")}></span>`
    )}`;
  }

  #pinnedTabItemTemplate() {
    return html`
      <moz-button
        type="icon ghost"
        id="fxview-tab-row-main"
        aria-haspopup=${ifDefined(this.hasPopup)}
        data-l10n-id=${ifDefined(this.primaryL10nId)}
        data-l10n-args=${ifDefined(this.primaryL10nArgs)}
        tabindex=${this.active &&
        this.currentActiveElementId === "fxview-tab-row-main"
          ? "0"
          : "-1"}
        role="tab"
        @click=${this.primaryActionHandler}
        @keydown=${this.primaryActionHandler}
        @contextmenu=${this.#secondaryActionHandler}
      >
        ${this.#faviconTemplate()}
      </moz-button>
      ${when(
        this.containerObj,
        () =>
          html`<span
            class="fxview-tab-row-pinned-context-line identity-color-${this
              .containerObj.color}"
          ></span>`
      )}
    `;
  }

  #unpinnedTabItemTemplate() {
    return html`<a
        href=${ifDefined(this.url)}
        class="fxview-tab-row-main"
        id="fxview-tab-row-main"
        tabindex=${this.active &&
        this.currentActiveElementId === "fxview-tab-row-main"
          ? "0"
          : "-1"}
        data-l10n-id=${ifDefined(this.primaryL10nId)}
        data-l10n-args=${ifDefined(this.primaryL10nArgs)}
        @click=${this.primaryActionHandler}
        @keydown=${this.primaryActionHandler}
        title=${!this.primaryL10nId ? this.url : null}
      >
        ${this.#faviconTemplate()} ${this.titleTemplate()}
        ${when(
          !this.compact,
          () =>
            html`${this.#containerIndicatorTemplate()} ${this.urlTemplate()}
            ${this.dateTemplate()} ${this.timeTemplate()}`
        )}
      </a>
      ${this.#mediaButtonTemplate()} ${this.secondaryButtonTemplate()}
      ${this.tertiaryButtonTemplate()}`;
  }

  render() {
    return html`
      ${this.stylesheets()}
      <link
        rel="stylesheet"
        href="chrome://browser/content/firefoxview/opentabs-tab-row.css"
      />
      ${when(
        this.containerObj,
        () => html`
          <link
            rel="stylesheet"
            href="chrome://browser/content/usercontext/usercontext.css"
          />
        `
      )}
      ${when(
        this.pinnedTabsGridView && this.indicators?.includes("pinned"),
        this.#pinnedTabItemTemplate.bind(this),
        this.#unpinnedTabItemTemplate.bind(this)
      )}
    `;
  }
}
customElements.define("opentabs-tab-row", OpenTabsTabRow);
