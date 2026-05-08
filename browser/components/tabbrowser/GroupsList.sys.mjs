/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

import { PrivateBrowsingUtils } from "resource://gre/modules/PrivateBrowsingUtils.sys.mjs";
import { TabMetrics } from "moz-src:///browser/components/tabbrowser/TabMetrics.sys.mjs";

const MAX_INITIAL_ITEMS = 5;

export class GroupsPanel {
  constructor({ view, containerNode, showAll = false }) {
    this.view = view;
    this.#showAll = showAll;
    this.containerNode = containerNode;
    this.win = containerNode.documentGlobal;
    this.doc = containerNode.ownerDocument;
    this.panelMultiView = null;
    this.view.addEventListener("ViewShowing", this);
  }

  handleEvent(event) {
    switch (event.type) {
      case "ViewShowing":
        if (event.target == this.view) {
          this.panelMultiView = this.view.panelMultiView;
          this.#populate();
          this.#addObservers();
          this.win.addEventListener("unload", this);
        }
        break;
      case "PanelMultiViewHidden":
        if ((this.panelMultiView = event.target)) {
          this.#cleanup();
          this.#removeObservers();
          this.panelMultiView = null;
        }
        break;
      case "unload":
        if (this.panelMultiView) {
          this.#removeObservers();
        }
        break;
      case "command":
        this.#handleCommand(event);
        break;
    }
  }

  #addObservers() {
    Services.obs.addObserver(this, "sessionstore-closed-objects-changed");
    Services.obs.addObserver(this, "browser-tabgroup-removed-from-dom");
  }

  #removeObservers() {
    Services.obs.removeObserver(this, "sessionstore-closed-objects-changed");
    Services.obs.removeObserver(this, "browser-tabgroup-removed-from-dom");
  }

  observe(aSubject, aTopic) {
    switch (aTopic) {
      case "sessionstore-closed-objects-changed":
      case "browser-tabgroup-removed-from-dom":
        this.#cleanup();
        this.#populate();
        break;
    }
  }

  #handleCommand(event) {
    let { tabGroupId, command } = event.target.dataset;

    switch (command) {
      case "allTabsGroupView_selectGroup": {
        let group = this.win.gBrowser.getTabGroupById(tabGroupId);
        group.select();
        group.documentGlobal.focus();
        break;
      }

      case "allTabsGroupView_restoreGroup":
        this.win.SessionStore.openSavedTabGroup(tabGroupId, this.win, {
          source: TabMetrics.METRIC_SOURCE.TAB_OVERFLOW_MENU,
        });
        break;
    }
  }

  #setupListeners() {
    this.view.addEventListener("command", this);
    this.panelMultiView.addEventListener("PanelMultiViewHidden", this);
  }

  #cleanup() {
    this.containerNode.innerHTML = "";
    this.view.removeEventListener("command", this);
  }

  #showAll;
  #populate() {
    let fragment = this.doc.createDocumentFragment();

    let openGroups = this.win.gBrowser.getAllTabGroups({
      sortByLastSeenActive: true,
    });
    let savedGroups = [];
    if (!PrivateBrowsingUtils.isWindowPrivate(this.win)) {
      savedGroups = this.win.SessionStore.savedGroups.toSorted(
        (group1, group2) => group2.closedAt - group1.closedAt
      );
    }

    let totalItemCount = savedGroups.length + openGroups.length;
    if (totalItemCount && !this.#showAll) {
      let header = this.doc.createElement("h2");
      header.setAttribute("class", "subview-subheader");
      this.doc.l10n.setAttributes(
        header,
        "all-tabs-menu-recent-tab-groups-header"
      );
      fragment.appendChild(header);
    }

    let addShowAllButton = !this.#showAll && totalItemCount > MAX_INITIAL_ITEMS;
    let itemCount = addShowAllButton ? 1 : 0;
    for (let groupData of openGroups) {
      if (itemCount >= MAX_INITIAL_ITEMS && !this.#showAll) {
        break;
      }
      itemCount++;
      let row = this.#createRow(groupData);
      fragment.appendChild(row);
    }

    for (let groupData of savedGroups) {
      if (itemCount >= MAX_INITIAL_ITEMS && !this.#showAll) {
        break;
      }
      itemCount++;
      let row = this.#createRow(groupData, { isOpen: false });
      fragment.appendChild(row);
    }

    if (addShowAllButton) {
      let button = this.doc.createXULElement("toolbarbutton");
      button.setAttribute("id", "allTabsMenu-groupsViewShowMore");
      button.setAttribute("class", "subviewbutton subviewbutton-nav");
      button.setAttribute("closemenu", "none");
      button.setAttribute("flex", "1");
      this.doc.l10n.setAttributes(button, "all-tabs-menu-tab-groups-show-all");
      fragment.appendChild(button);
    }

    this.containerNode.replaceChildren(fragment);
    this.#setupListeners();
  }

  /**
   * @param {TabGroupStateData} group
   * @param {object} [options]
   * @param {boolean} [options.isOpen]
   *   Set to true if the group is currently open, and false if it's saved
   * @returns {XULElement}
   */
  #createRow(group, { isOpen = true } = {}) {
    let { doc } = this;
    let row = doc.createXULElement("toolbaritem");
    row.setAttribute("class", "all-tabs-item all-tabs-group-item");

    row.style.setProperty(
      "--tab-group-color",
      `var(--tab-group-color-${group.color})`
    );
    row.style.setProperty(
      "--tab-group-color-invert",
      `var(--tab-group-color-${group.color}-invert)`
    );
    row.style.setProperty(
      "--tab-group-color-pale",
      `var(--tab-group-color-${group.color}-pale)`
    );
    row.style.setProperty(
      "--tab-group-background-color",
      `var(--tab-group-${group.color})`
    );
    let button = doc.createXULElement("toolbarbutton");
    button.setAttribute(
      "class",
      "all-tabs-button subviewbutton subviewbutton-iconic all-tabs-group-action-button tab-group-icon"
    );
    button.dataset.tabGroupId = group.id;
    if (!isOpen) {
      button.classList.add(
        "all-tabs-group-saved-group",
        "tab-group-icon-closed"
      );
      button.dataset.command = "allTabsGroupView_restoreGroup";
      button.setAttribute("context", "saved-tab-group-context-menu");
    } else {
      button.dataset.command = "allTabsGroupView_selectGroup";
      button.setAttribute("context", "open-tab-group-context-menu");
    }
    button.setAttribute("flex", "1");
    button.setAttribute("crop", "end");

    let setName = tabGroupName => {
      if (group.saved) {
        doc.l10n.setAttributes(button, "tabbrowser-manager-closed-tab-group", {
          tabGroupName,
        });
      } else {
        button.setAttribute("label", tabGroupName);
        button.setAttribute("tooltiptext", tabGroupName);
      }
    };

    if (group.name) {
      setName(group.name);
    } else {
      doc.l10n
        .formatValues([{ id: "tab-group-name-default" }])
        .then(([msg]) => {
          setName(msg);
        });
    }
    row.appendChild(button);
    return row;
  }
}
