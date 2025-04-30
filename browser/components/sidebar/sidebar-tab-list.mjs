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
  }

  static queries = {
    ...FxviewTabListBase.queries,
    rowEls: {
      all: "sidebar-tab-row",
    },
  };

  /**
   * Only handle vertical navigation in sidebar.
   *
   * @param {KeyboardEvent} e
   */
  handleFocusElementInRow(e) {
    if (
      (e.code == "ArrowUp" && this.activeIndex > 0) ||
      (e.code == "ArrowDown" && this.activeIndex < this.rowEls.length - 1)
    ) {
      super.handleFocusElementInRow(e);
    } else if (e.code == "ArrowUp" && this.activeIndex == 0) {
      let parentCard = e.target.getRootNode().host.closest("moz-card");
      if (parentCard) {
        parentCard.summaryEl.focus();
      }
    } else if (
      e.code == "ArrowDown" &&
      this.activeIndex == this.rowEls.length - 1
    ) {
      let parentCard = e.target.getRootNode().host.closest("moz-card");
      if (
        this.sortOption == "datesite" &&
        parentCard.classList.contains("last-card")
      ) {
        // If we're going down from the last site, then focus the next date.
        const dateCard = parentCard.parentElement;
        const nextDate = dateCard.nextElementSibling;
        nextDate?.summaryEl.focus();
      }
      let nextCard = parentCard.nextElementSibling;
      if (nextCard && nextCard.localName == "moz-card") {
        nextCard.summaryEl.focus();
      }
    }
  }

  itemTemplate = (tabItem, i) => {
    let tabIndex = -1;
    if (this.sortOption == "lastvisited" && i == 0) {
      // Last Visited doesn't have a header. Make the first row focusable.
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
        .fxaDeviceId=${ifDefined(tabItem.fxaDeviceId)}
        .favicon=${tabItem.icon}
        .hasPopup=${this.hasPopup}
        .primaryL10nArgs=${ifDefined(tabItem.primaryL10nArgs)}
        .primaryL10nId=${tabItem.primaryL10nId}
        role="listitem"
        .searchQuery=${ifDefined(this.searchQuery)}
        .secondaryActionClass=${ifDefined(
          this.secondaryActionClass ?? tabItem.secondaryActionClass
        )}
        .secondaryL10nArgs=${ifDefined(tabItem.secondaryL10nArgs)}
        .secondaryL10nId=${tabItem.secondaryL10nId}
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
  /**
   * Fallback to the native implementation in sidebar. We want to focus the
   * entire row instead of delegating it to link or hover buttons.
   */
  focus() {
    HTMLElement.prototype.focus.call(this);
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
      <link
        rel="stylesheet"
        href="chrome://browser/content/sidebar/sidebar-tab-row.css"
      />
      <a
        class=${classMap({
          "fxview-tab-row-main": true,
          "no-action-button-row": this.canClose === false,
        })}
        disabled=${this.closeRequested}
        data-l10n-args=${ifDefined(this.primaryL10nArgs)}
        data-l10n-id=${ifDefined(this.primaryL10nId)}
        href=${ifDefined(this.url)}
        id="fxview-tab-row-main"
        tabindex="-1"
        title=${!this.primaryL10nId ? this.url : null}
        @click=${this.primaryActionHandler}
        @keydown=${this.primaryActionHandler}
      >
        ${this.faviconTemplate()} ${this.titleTemplate()}
      </a>
      ${this.secondaryButtonTemplate()}
    `;
  }
}
customElements.define("sidebar-tab-row", SidebarTabRow);
