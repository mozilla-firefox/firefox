/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

import {
  html,
  nothing,
  repeat,
  styleMap,
  classMap,
} from "chrome://global/content/vendor/lit.all.mjs";
import { MozLitElement } from "chrome://global/content/lit-utils.mjs";
// eslint-disable-next-line import-x/no-unassigned-import
import "chrome://global/content/elements/moz-button.mjs";

const { XPCOMUtils } = ChromeUtils.importESModule(
  "resource://gre/modules/XPCOMUtils.sys.mjs"
);
const lazy = XPCOMUtils.declareLazy({
  PrivateBrowsingUtils: "resource://gre/modules/PrivateBrowsingUtils.sys.mjs",
  SessionStore:
    "moz-src:///browser/components/sessionstore/SessionStore.sys.mjs",
  TabMetrics: "moz-src:///browser/components/tabbrowser/TabMetrics.sys.mjs",
});

class TabGroupsList extends MozLitElement {
  static properties = {
    rowRole: { type: String, attribute: "row-role" },
    _openGroups: { type: Array, state: true },
    _savedGroups: { type: Array, state: true },
    _defaultGroupName: { type: String, state: true },
  };

  constructor() {
    super();
    this.rowRole = "";
    this._openGroups = [];
    this._savedGroups = [];
    this._defaultGroupName = "";
  }

  createRenderRoot() {
    return this;
  }

  get #win() {
    return /** @type {BrowserWindow} */ (this.documentGlobal);
  }

  get #metricSource() {
    return this.closest("panelview")?.id == "toolbar-tabGroupsListView"
      ? lazy.TabMetrics.METRIC_SOURCE.TOOLBAR_GROUP_LIST
      : lazy.TabMetrics.METRIC_SOURCE.APP_MENU_GROUP_LIST;
  }

  connectedCallback() {
    super.connectedCallback();
    this.#populate();
  }

  async firstUpdated() {
    [this._defaultGroupName] = await this.ownerDocument.l10n.formatValues([
      { id: "tab-group-name-default" },
    ]);
  }

  #populate() {
    const win = this.#win;
    this._openGroups = win.gBrowser.getAllTabGroups({
      sortByLastSeenActive: true,
    });
    this._savedGroups = lazy.PrivateBrowsingUtils.isWindowPrivate(win)
      ? []
      : lazy.SessionStore.savedGroups.toSorted(
          (a, b) => b.closedAt - a.closedAt
        );
  }

  #handleGroupClick(group, isOpen) {
    /** @type {XULPopupElement} */ (this.closest("panel"))?.hidePopup();
    if (isOpen) {
      group.select();
      group.documentGlobal.focus();
    } else {
      lazy.SessionStore.openSavedTabGroup(group.id, this.#win, {
        source: this.#metricSource,
      });
    }
  }

  #handleContextMenu(event, isOpen) {
    event.preventDefault();
    const menuId = isOpen
      ? "open-tab-group-context-menu"
      : "saved-tab-group-context-menu";
    const popup = /** @type {XULPopupElement} */ (
      this.ownerDocument.getElementById(menuId)
    );
    popup.openPopupAtScreen(event.screenX, event.screenY, true, event);
  }

  #groupRow(group, isOpen) {
    const groupName = group.name || this._defaultGroupName;
    const style = styleMap({
      "--tab-group-color": `var(--tab-group-${group.color})`,
      "--tab-group-color-invert": `var(--tab-group-${group.color}-invert)`,
      "--tab-group-color-pale": `var(--tab-group-${group.color}-pale)`,
      "--tab-group-background-color": `var(--tab-group-${group.color})`,
    });
    return html`
      <button
        class="tab-group-row subviewbutton"
        role=${this.rowRole || nothing}
        data-tab-group-id=${group.id}
        ?data-saved=${!isOpen}
        data-l10n-id=${!isOpen ? "tab-group-menu-closed-tab-group" : nothing}
        data-l10n-args=${!isOpen
          ? JSON.stringify({ tabGroupName: groupName })
          : nothing}
        style=${style}
        @click=${() => this.#handleGroupClick(group, isOpen)}
        @contextmenu=${e => this.#handleContextMenu(e, isOpen)}
      >
        <img
          class=${classMap({
            "tab-group-row-icon": true,
            "tab-group-icon-closed": !isOpen,
          })}
          src="chrome://browser/skin/tabbrowser/tab-group-chicklet.svg"
          width="16"
          height="16"
          alt=""
          role="presentation"
        />
        <span class="tab-group-row-label">${groupName}</span>
      </button>
    `;
  }

  #emptyState() {
    return html`
      <div class="tab-groups-list-empty-state">
        <img
          class="tab-groups-list-empty-state-image"
          src="chrome://browser/skin/illustrations/tab-groups.svg"
          role="presentation"
          alt=""
          loading="lazy"
        />
        <p
          class="tab-groups-list-empty-state-header"
          data-l10n-id="tab-groups-list-empty-header"
        ></p>
        <p
          class="tab-groups-list-empty-state-description"
          data-l10n-id="tab-groups-list-empty-description"
        ></p>
        <moz-button
          type="primary"
          data-l10n-id="tab-groups-list-empty-button"
          @click=${this.#handleCreateTabGroup}
        ></moz-button>
      </div>
    `;
  }

  #handleCreateTabGroup() {
    /** @type {XULPopupElement} */ (this.closest("panel"))?.hidePopup();
    const win = this.#win;
    const newTab = win.gBrowser.addTrustedTab(win.BROWSER_NEW_TAB_URL);
    win.gBrowser.addTabGroup([newTab], {
      metricsContext: win.gBrowser.TabMetrics.userTriggeredContext(
        this.#metricSource
      ),
    });
  }

  render() {
    if (!this._openGroups.length && !this._savedGroups.length) {
      return this.#emptyState();
    }
    return html`
      <button
        id="tab-groups-list-create-group"
        class="subviewbutton"
        data-l10n-id="tab-groups-list-create-group-button"
        @click=${this.#handleCreateTabGroup}
      ></button>
      <hr />
      ${repeat(
        this._openGroups,
        group => group.id,
        group => this.#groupRow(group, true)
      )}
      ${repeat(
        this._savedGroups,
        group => group.id,
        group => this.#groupRow(group, false)
      )}
    `;
  }
}

customElements.define("tab-groups-list", TabGroupsList);
