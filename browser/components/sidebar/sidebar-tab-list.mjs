/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

import {
  classMap,
  html,
  ifDefined,
  when,
} from "chrome://global/content/vendor/lit.all.mjs";

import {
  FxviewTabListBase,
  FxviewTabRowBase,
} from "chrome://browser/content/firefoxview/fxview-tab-list.mjs";

export class SidebarTabList extends FxviewTabListBase {
  constructor() {
    super();
    // Panel is open, assume we always want to react to updates.
    this.updatesPaused = false;
    this.multiSelect = true;
    this.shortcutsLocalization = new Localization(
      ["toolkit/global/textActions.ftl"],
      true
    );
  }

  static queries = {
    ...FxviewTabListBase.queries,
    rowEls: {
      all: "sidebar-tab-row",
    },
  };

  /**
   * The tree view controller that owns selection state for the page this list
   * belongs to.
   *
   * @returns {SidebarTreeView}
   */
  get treeView() {
    let host = this.getRootNode()?.host;
    while (host) {
      if (host.treeView) {
        return host.treeView;
      }
      host = host.getRootNode()?.host;
    }
    return null;
  }

  #dispatchFocusRowEvent = event => {
    const [row] = event.composedPath();
    if (row.localName !== "sidebar-tab-row") {
      return;
    }
    this.dispatchEvent(
      new CustomEvent("focus-row", {
        bubbles: true,
        composed: true,
        detail: { guid: row.guid },
      })
    );
  };

  connectedCallback() {
    super.connectedCallback();
    this.addEventListener("focusin", this.#dispatchFocusRowEvent);
  }

  disconnectedCallback() {
    super.disconnectedCallback();
    this.removeEventListener("focusin", this.#dispatchFocusRowEvent);
  }

  /**
   * Only handle vertical navigation in sidebar.
   *
   * @param {KeyboardEvent} e
   */
  handleFocusElementInRow(e) {
    // Handle vertical navigation.
    let stayedInList = false;
    if (
      (e.code == "ArrowUp" && this.activeIndex > 0) ||
      (e.code == "ArrowDown" && this.activeIndex < this.rowEls.length - 1)
    ) {
      super.handleFocusElementInRow(e);
      stayedInList = true;
    } else if (
      (e.code == "ArrowUp" && this.activeIndex == 0) ||
      e.code === "ArrowLeft"
    ) {
      this.#focusParentHeader(e);
    } else if (
      e.code == "ArrowDown" &&
      this.activeIndex == this.rowEls.length - 1
    ) {
      this.#focusNextHeader(e);
    }

    // Update or clear multi-selection (depending on whether shift key is used).
    const accelKeyDown = e.getModifierState("Accel");
    if (
      this.multiSelect &&
      (e.code === "ArrowUp" || e.code === "ArrowDown") &&
      !accelKeyDown
    ) {
      this.#updateSelection(e, stayedInList);
    }

    // (Ctrl / Cmd) + A should select all rows.
    if (accelKeyDown && e.key.toUpperCase() === this.selectAllShortcut) {
      e.preventDefault();
      this.selectAll();
    }
  }

  #focusParentHeader(e) {
    let parentCard = e.target.getRootNode().host.closest("moz-card");
    if (parentCard) {
      e.preventDefault();
      this.#focusHeader(parentCard);
    }
  }

  #focusNextHeader(e) {
    let parentCard = e.target.getRootNode().host.closest("moz-card");
    if (
      this.sortOption == "datesite" &&
      parentCard.classList.contains("last-card")
    ) {
      // If we're going down from the last site, then focus the next date.
      const dateCard = parentCard.parentElement;
      const nextDate = dateCard.nextElementSibling;
      if (nextDate) {
        e.preventDefault();
        this.#focusHeader(nextDate);
      }
      return;
    }
    let nextCard = parentCard.nextElementSibling;
    if (nextCard && nextCard.localName == "moz-card") {
      e.preventDefault();
      this.#focusHeader(nextCard);
    }
  }

  #focusHeader(card) {
    card.summaryEl.focus({ preventScroll: true });
    card.summaryEl.scrollIntoView({ block: "nearest" });
  }

  /**
   * Update multi-selection state during keyboard navigation.
   *
   * Without Shift, clears the selection and resets the anchor to the newly
   * focused row. With Shift, extends the selection from the current anchor to
   * the newly focused row.
   *
   * @param {KeyboardEvent} event
   * @param {boolean} stayedInList
   *   Whether focus remained within this list after the navigation.
   */
  #updateSelection(event, stayedInList) {
    if (!event.shiftKey) {
      this.clearSelection();
      this.dispatchEvent(
        new CustomEvent("clear-selection", {
          bubbles: true,
          composed: true,
        })
      );
      if (stayedInList) {
        const newRow = this.rowEls[this.activeIndex];
        if (newRow) {
          this.dispatchEvent(
            new CustomEvent("set-anchor", {
              bubbles: true,
              composed: true,
              detail: { guid: newRow.guid },
            })
          );
        }
      }
      return;
    }

    const newRow = this.rowEls[this.activeIndex];
    if (newRow) {
      this.dispatchEvent(
        new CustomEvent("shift-select", {
          bubbles: true,
          composed: true,
          detail: { row: newRow },
        })
      );
    }
  }

  toggleRowSelection(guid) {
    this.treeView?.toggleSelection(this, guid);
  }

  clearSelection() {
    this.treeView?.resetSelection();
  }

  get selectAllShortcut() {
    const [l10nMessage] = this.shortcutsLocalization.formatMessagesSync([
      "text-action-select-all-shortcut",
    ]);
    const shortcutKey = l10nMessage.attributes[0].value;
    return shortcutKey;
  }

  selectAll() {
    this.treeView?.selectAllInList(this);
  }

  itemTemplate = (tabItem, i) => {
    let tabIndex = -1;
    if ((this.searchQuery || this.sortOption == "lastvisited") && i == 0) {
      // Make the first row focusable if there is no header.
      tabIndex = 0;
    } else if (!this.searchQuery) {
      tabIndex = 0;
    }
    return html`
      <sidebar-tab-row
        ?active=${i == this.activeIndex}
        .canClose=${ifDefined(tabItem.canClose)}
        .closedId=${ifDefined(tabItem.closedId)}
        compact
        .currentActiveElementId=${this.currentActiveElementId}
        .closeRequested=${tabItem.closeRequested}
        .containerObj=${tabItem.containerObj}
        .fxaDeviceId=${ifDefined(tabItem.fxaDeviceId)}
        .favicon=${tabItem.icon}
        .guid=${tabItem.guid}
        .hasPopup=${this.hasPopup}
        .indicators=${tabItem.indicators}
        .primaryL10nArgs=${ifDefined(tabItem.primaryL10nArgs)}
        .primaryL10nId=${tabItem.primaryL10nId}
        role="listitem"
        .searchQuery=${ifDefined(this.searchQuery)}
        .secondaryActionClass=${ifDefined(
          this.secondaryActionClass ?? tabItem.secondaryActionClass
        )}
        .secondaryL10nArgs=${ifDefined(tabItem.secondaryL10nArgs)}
        .secondaryL10nId=${tabItem.secondaryL10nId}
        .selected=${this.isTabItemSelected(tabItem)}
        .sourceClosedId=${ifDefined(tabItem.sourceClosedId)}
        .sourceWindowId=${ifDefined(tabItem.sourceWindowId)}
        .tabElement=${ifDefined(tabItem.tabElement)}
        tabindex=${tabIndex}
        .title=${tabItem.title}
        .url=${tabItem.url}
        @keydown=${e => e.currentTarget.primaryActionHandler(e)}
      ></sidebar-tab-row>
    `;
  };

  isTabItemSelected(tabItem) {
    return !!this.treeView?.isSelected(this, tabItem.guid);
  }

  stylesheets() {
    return [
      super.stylesheets(),
      html`<link
        rel="stylesheet"
        href="chrome://browser/content/sidebar/sidebar-tab-list.css"
      />`,
    ];
  }
}
customElements.define("sidebar-tab-list", SidebarTabList);

export class SidebarTabRow extends FxviewTabRowBase {
  static properties = {
    containerObj: { type: Object },
    guid: { type: String },
    selected: { type: Boolean, reflect: true },
    indicators: { type: Array },
  };

  get tooltipText() {
    return !this.primaryL10nId ? this.url : null;
  }

  /**
   * Fallback to the native implementation in sidebar. We want to focus the
   * entire row instead of delegating it to link or hover buttons.
   */
  focus(options) {
    HTMLElement.prototype.focus.call(this, options);
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

  #containerIndicatorTemplate() {
    let tabList = this.getRootNode().host;
    let tabsToCheck = tabList.tabItems;
    return html`${when(
      tabsToCheck.some(tab => tab.containerObj),
      () => html`<span class=${this.#getContainerClasses().join(" ")}></span>`
    )}`;
  }

  secondaryButtonTemplate() {
    return html`${when(
      this.secondaryL10nId && this.secondaryActionClass,
      () =>
        html`<moz-button
          aria-haspopup=${ifDefined(this.hasPopup)}
          class=${classMap({
            "fxview-tab-row-button": true,
            [this.secondaryActionClass]: this.secondaryActionClass,
          })}
          data-l10n-args=${ifDefined(this.secondaryL10nArgs)}
          data-l10n-id=${this.secondaryL10nId}
          id="fxview-tab-row-secondary-button"
          type="icon ghost"
          @click=${this.secondaryActionHandler}
          iconSrc=${this.getIconSrc(this.secondaryActionClass)}
        ></moz-button>`
    )}`;
  }

  render() {
    return html`
      ${this.stylesheets()}
      ${when(
        this.containerObj,
        () => html`
          <link
            rel="stylesheet"
            href="chrome://browser/content/usercontext/usercontext.css"
          />
        `
      )}
      <link
        rel="stylesheet"
        href="chrome://browser/content/sidebar/sidebar-tab-row.css"
      />
      <a
        class=${classMap({
          "fxview-tab-row-main": true,
          "no-action-button-row": this.canClose === false,
          muted: this.indicators?.includes("muted"),
          attention: this.indicators?.includes("attention"),
          soundplaying: this.indicators?.includes("soundplaying"),
          "activemedia-blocked": this.indicators?.includes(
            "activemedia-blocked"
          ),
        })}
        ?disabled=${this.closeRequested}
        data-l10n-args=${ifDefined(this.primaryL10nArgs)}
        data-l10n-id=${ifDefined(this.primaryL10nId)}
        href=${ifDefined(this.url)}
        id="fxview-tab-row-main"
        tabindex="-1"
        title=${this.tooltipText}
        @click=${this.primaryActionHandler}
        @auxclick=${this.auxActionHandler}
        @keydown=${this.primaryActionHandler}
      >
        ${this.faviconTemplate()} ${this.titleTemplate()}
      </a>
      ${this.secondaryButtonTemplate()} ${this.#containerIndicatorTemplate()}
    `;
  }
}
customElements.define("sidebar-tab-row", SidebarTabRow);
