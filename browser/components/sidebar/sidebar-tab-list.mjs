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
    this.inTree = false;
    this.level = 1;
  }

  static queries = {
    ...FxviewTabListBase.queries,
    rowEls: {
      all: "sidebar-tab-row",
    },
  };

  static properties = {
    inTree: { type: Boolean },
    level: { type: Number },
    mediumView: { type: Boolean, reflect: true, attribute: "medium-view" },
    inactiveWindow: {
      type: Boolean,
      reflect: true,
      attribute: "inactive-window",
    },
    rowsDraggable: { type: Boolean },
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

  willUpdate(changedProperties) {
    if (changedProperties.has("tabItems") && Array.isArray(this.tabItems)) {
      for (const item of this.tabItems) {
        item.guid ??= Services.uuid.generateUUID().toString();
      }
    }
  }

  handleFocusElementInRow(e) {
    if (!this.treeView) {
      super.handleFocusElementInRow(e);
      return;
    }
    this.treeView.handleKeydown(e);
    if (e.defaultPrevented) {
      e.stopPropagation();
    }
  }

  toggleRowSelection(guid) {
    this.treeView?.toggleSelection(this, guid);
  }

  clearSelection() {
    this.treeView?.resetSelection();
  }

  selectAll() {
    this.treeView?.selectAllInList(this);
  }

  itemTemplate = (tabItem, i) => {
    const tabIndex = this.treeView?.isActiveNode(this, tabItem.guid) ? 0 : -1;
    let time;
    if (tabItem.time) {
      // Some APIs report the timestamp in microseconds (16 digits); the row
      // expects milliseconds.
      const stringTime = tabItem.time.toString();
      time = stringTime.length === 16 ? tabItem.time / 1000 : tabItem.time;
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
        .dateTimeFormat=${this.dateTimeFormat}
        draggable=${!!this.rowsDraggable}
        .fxaDeviceId=${ifDefined(tabItem.fxaDeviceId)}
        .favicon=${tabItem.icon}
        .guid=${tabItem.guid}
        .hasPopup=${this.hasPopup}
        .indicators=${tabItem.indicators}
        .mediumView=${this.mediumView}
        .inactiveWindow=${this.inactiveWindow}
        role=${this.inTree ? "treeitem" : "listitem"}
        aria-level=${ifDefined(this.inTree ? this.level : undefined)}
        aria-posinset=${ifDefined(this.inTree ? i + 1 : undefined)}
        aria-setsize=${ifDefined(
          this.inTree ? this.tabItems.length : undefined
        )}
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
        .time=${time}
        .timeMsPref=${this.timeMsPref}
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

  render() {
    if (!this.inTree || (this.searchQuery && !this.tabItems.length)) {
      return super.render();
    }
    return html`
      ${this.stylesheets()}
      <div
        id="fxview-tab-list"
        class="fxview-tab-list"
        role=${this.level > 1 ? "group" : "tree"}
        @keydown=${this.handleFocusElementInRow}
      >
        <virtual-list
          .activeIndex=${this.activeIndex}
          .items=${this.tabItems}
          .template=${this.itemTemplate}
          .getItemHeight=${this.getItemHeight}
        ></virtual-list>
      </div>
      <slot name="menu"></slot>
    `;
  }
}
customElements.define("sidebar-tab-list", SidebarTabList);

/**
 * A sidebar-specific tab row.
 *
 * Three Boolean states coexist on this row and they each mean something
 * different:
 *   - `active`   (inherited from FxviewTabRowBase): the row currently has
 *                keyboard focus via the parent list's activeIndex.
 *   - `selected`: the row is selected through the SidebarTreeView (user
 *                click or multi-select inside the panel).
 *   - `current`:  the row's tabElement is gBrowser.selectedTab. Tracked
 *                live via a MutationObserver on the tab's [selected]
 *                attribute.
 */
export class SidebarTabRow extends FxviewTabRowBase {
  static properties = {
    containerObj: { type: Object },
    guid: { type: String, reflect: true, attribute: "data-guid" },
    selected: { type: Boolean, reflect: true },
    current: { type: Boolean, reflect: true },
    indicators: { type: Array },
    mediumView: { type: Boolean, reflect: true, attribute: "medium-view" },
    inactiveWindow: {
      type: Boolean,
      reflect: true,
      attribute: "inactive-window",
    },
  };

  static queries = {
    ...FxviewTabRowBase.queries,
    domainEl: "#sidebar-tab-row-domain",
    timeEl: "#fxview-tab-row-time",
  };

  #tabSelectObserver = null;

  willUpdate(changedProperties) {
    super.willUpdate?.(changedProperties);
    if (changedProperties.has("tabElement")) {
      this.#tabSelectObserver?.disconnect();
      this.#tabSelectObserver = null;
      if (this.tabElement) {
        this.current = this.tabElement.selected;
        this.#tabSelectObserver = new MutationObserver(() => {
          this.current = this.tabElement?.selected ?? false;
        });
        this.#tabSelectObserver.observe(this.tabElement, {
          attributes: true,
          attributeFilter: ["selected"],
        });
      } else {
        this.current = false;
      }
    }
  }

  disconnectedCallback() {
    super.disconnectedCallback();
    this.#tabSelectObserver?.disconnect();
    this.#tabSelectObserver = null;
  }

  get tooltipText() {
    if (!this.url) {
      return null;
    }
    if (!this.title || this.title == this.url) {
      return this.url;
    }
    return `${this.title}\n${this.url}`;
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

  #getDomain() {
    if (!this.url) {
      return "";
    }
    try {
      return Services.eTLD.getBaseDomain(Services.io.newURI(this.url));
    } catch (e) {
      // No base domain (about:, file:, IP hosts, etc.); show a friendly label
      // the way Firefox View does.
      return this.formatURIForDisplay(this.url);
    }
  }

  #domainTemplate() {
    return html`<span
      class="sidebar-tab-row-domain text-truncated-ellipsis"
      id="sidebar-tab-row-domain"
    >
      ${this.#getDomain()}
    </span>`;
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
        href=${ifDefined(this.url)}
        id="fxview-tab-row-main"
        tabindex="-1"
        title=${this.tooltipText}
        @click=${this.primaryActionHandler}
        @auxclick=${this.auxActionHandler}
        @keydown=${this.primaryActionHandler}
      >
        ${this.faviconTemplate()} ${this.titleTemplate()}
        ${when(
          this.mediumView,
          () => html`${this.#domainTemplate()} ${this.timeTemplate()}`
        )}
      </a>
      ${this.#containerIndicatorTemplate()} ${this.secondaryButtonTemplate()}
    `;
  }
}
customElements.define("sidebar-tab-row", SidebarTabRow);
