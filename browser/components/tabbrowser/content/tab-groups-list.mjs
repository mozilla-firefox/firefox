/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

import {
  html,
  nothing,
  repeat,
  styleMap,
} from "chrome://global/content/vendor/lit.all.mjs";
import { MozLitElement } from "chrome://global/content/lit-utils.mjs";

const lazy = {};
ChromeUtils.defineESModuleGetters(lazy, {
  PrivateBrowsingUtils: "resource://gre/modules/PrivateBrowsingUtils.sys.mjs",
  TabMetrics: "moz-src:///browser/components/tabbrowser/TabMetrics.sys.mjs",
});

class TabGroupsList extends MozLitElement {
  static properties = {
    _openGroups: { type: Array, state: true },
    _savedGroups: { type: Array, state: true },
    _defaultGroupName: { type: String, state: true },
  };

  constructor() {
    super();
    this._openGroups = [];
    this._savedGroups = [];
    this._defaultGroupName = "";
  }

  createRenderRoot() {
    return this;
  }

  get #win() {
    return this.documentGlobal;
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
      : win.SessionStore.savedGroups.toSorted(
          (a, b) => b.closedAt - a.closedAt
        );
  }

  #handleGroupClick(group, isOpen) {
    this.closest("panel")?.hidePopup();
    if (isOpen) {
      group.select();
      group.documentGlobal.focus();
    } else {
      this.#win.SessionStore.openSavedTabGroup(group.id, this.#win, {
        source: lazy.TabMetrics.METRIC_SOURCE.TAB_OVERFLOW_MENU,
      });
    }
  }

  #handleContextMenu(event, isOpen) {
    event.preventDefault();
    const menuId = isOpen
      ? "open-tab-group-context-menu"
      : "saved-tab-group-context-menu";
    const popup = this.ownerDocument.getElementById(menuId);
    popup.openPopupAtScreen(event.screenX, event.screenY, true, event);
  }

  #groupRow(group, isOpen) {
    const groupName = group.name || this._defaultGroupName;
    const style = styleMap({
      "--tab-group-color": `var(--tab-group-color-${group.color})`,
      "--tab-group-color-invert": `var(--tab-group-color-${group.color}-invert)`,
      "--tab-group-color-pale": `var(--tab-group-color-${group.color}-pale)`,
      "--tab-group-color-default": `var(--tab-group-${group.color})`,
    });
    return html`
      <button
        class="tab-group-row subviewbutton"
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
          class="tab-group-row-icon${isOpen ? "" : " tab-group-icon-closed"}"
          src="chrome://browser/skin/tabbrowser/tab-group-chicklet.svg"
          width="16"
          height="16"
        />
        <span class="tab-group-row-label">${groupName}</span>
      </button>
    `;
  }

  #emptyState() {
    return html`<div class="tab-groups-list-empty-state"></div>`;
  }

  render() {
    if (!this._openGroups.length && !this._savedGroups.length) {
      return this.#emptyState();
    }
    return html`
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
