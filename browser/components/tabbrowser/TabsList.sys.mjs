/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

const lazy = {};

ChromeUtils.defineESModuleGetters(lazy, {
  PanelMultiView: "resource:///modules/PanelMultiView.sys.mjs",
  TabMetrics: "moz-src:///browser/components/tabbrowser/TabMetrics.sys.mjs",
});

const TAB_DROP_TYPE = "application/x-moz-tabbrowser-tab";

function setAttributes(element, attrs) {
  for (let [name, value] of Object.entries(attrs)) {
    if (value) {
      element.setAttribute(name, value);
    } else {
      element.removeAttribute(name);
    }
  }
}

class TabsListBase {
  /** @type {boolean} */
  #domRefreshPending = false;

  constructor({
    className,
    filterFn,
    insertBefore,
    containerNode,
    dropIndicator = null,
  }) {
    this.className = className;
    this.filterFn = filterFn;
    this.insertBefore = insertBefore;
    this.containerNode = containerNode;
    this.dropIndicator = dropIndicator;

    if (this.dropIndicator) {
      this.dropTargetRow = null;
      this.dropTargetDirection = 0;
    }

    this.doc = containerNode.ownerDocument;
    this.gBrowser = this.doc.defaultView.gBrowser;
    this.tabToElement = new Map();
    this.listenersRegistered = false;
  }

  get rows() {
    return this.tabToElement.values();
  }

  handleEvent(event) {
    switch (event.type) {
      case "TabAttrModified":
        this._tabAttrModified(event.target);
        break;
      case "TabClose":
        this._tabClose(event.target);
        break;
      case "TabGroupCollapse":
      case "TabGroupExpand":
      case "TabGroupCreate":
      case "TabGroupRemoved":
      case "TabGrouped":
      case "TabUngrouped":
        this._refreshDOM();
        break;
      case "TabMove":
        this._moveTab(event.target);
        break;
      case "TabPinned":
        if (!this.filterFn(event.target)) {
          this._tabClose(event.target);
        }
        break;
      case "command":
        this._selectTab(event.target.tab);
        break;
      case "dragstart":
        this._onDragStart(event);
        break;
      case "dragover":
        this._onDragOver(event);
        break;
      case "dragleave":
        this._onDragLeave(event);
        break;
      case "dragend":
        this._onDragEnd(event);
        break;
      case "drop":
        this._onDrop(event);
        break;
      case "click":
        this._onClick(event);
        break;
    }
  }

  _selectTab(tab) {
    if (this.gBrowser.selectedTab != tab) {
      this.gBrowser.selectedTab = tab;
    } else {
      this.gBrowser.tabContainer._handleTabSelect();
    }
  }

  /*
   * Populate the popup with menuitems and setup the listeners.
   */
  _populate() {
    this._populateDOM();
    this._setupListeners();
  }

  _populateDOM() {
    let fragment = this.doc.createDocumentFragment();
    let currentGroupId;

    for (let tab of this.gBrowser.tabs) {
      if (this.filterFn(tab)) {
        if (tab.group && tab.group.id != currentGroupId) {
          fragment.appendChild(this._createGroupRow(tab.group));
          currentGroupId = tab.group.id;
        }
        if (!tab.group?.collapsed) {
          fragment.appendChild(this._createRow(tab));
        }
      }
    }

    this._addElement(fragment);
  }

  _addElement(elementOrFragment) {
    this.containerNode.insertBefore(elementOrFragment, this.insertBefore);
  }

  /*
   * Remove the menuitems from the DOM, cleanup internal state and listeners.
   */
  _cleanup() {
    this._cleanupDOM();
    this._cleanupListeners();
    this._clearDropTarget();
  }

  _cleanupDOM() {
    this.containerNode
      .querySelectorAll(":scope .all-tabs-group-item")
      .forEach(node => node.remove());

    for (let item of this.rows) {
      item.remove();
    }
    this.tabToElement = new Map();
  }

  _refreshDOM() {
    if (!this.#domRefreshPending) {
      this.#domRefreshPending = true;
      this.containerNode.ownerGlobal.requestAnimationFrame(() => {
        if (this.#domRefreshPending) {
          this.#domRefreshPending = false;
          if (this.listenersRegistered) {
            // Only re-render the menu DOM if the menu is still open.
            this._cleanupDOM();
            this._populateDOM();
          }
        }
      });
    }
  }

  _setupListeners() {
    this.listenersRegistered = true;

    this.gBrowser.tabContainer.addEventListener("TabAttrModified", this);
    this.gBrowser.tabContainer.addEventListener("TabClose", this);
    this.gBrowser.tabContainer.addEventListener("TabMove", this);
    this.gBrowser.tabContainer.addEventListener("TabPinned", this);
    this.gBrowser.tabContainer.addEventListener("TabGroupCollapse", this);
    this.gBrowser.tabContainer.addEventListener("TabGroupExpand", this);
    this.gBrowser.tabContainer.addEventListener("TabGroupCreate", this);
    this.gBrowser.tabContainer.addEventListener("TabGroupRemoved", this);
    this.gBrowser.tabContainer.addEventListener("TabGrouped", this);
    this.gBrowser.tabContainer.addEventListener("TabUngrouped", this);

    this.containerNode.addEventListener("click", this);

    if (this.dropIndicator) {
      this.containerNode.addEventListener("dragstart", this);
      this.containerNode.addEventListener("dragover", this);
      this.containerNode.addEventListener("dragleave", this);
      this.containerNode.addEventListener("dragend", this);
      this.containerNode.addEventListener("drop", this);
    }
  }

  _cleanupListeners() {
    this.gBrowser.tabContainer.removeEventListener("TabAttrModified", this);
    this.gBrowser.tabContainer.removeEventListener("TabClose", this);
    this.gBrowser.tabContainer.removeEventListener("TabMove", this);
    this.gBrowser.tabContainer.removeEventListener("TabPinned", this);
    this.gBrowser.tabContainer.removeEventListener("TabGroupCollapse", this);
    this.gBrowser.tabContainer.removeEventListener("TabGroupExpand", this);
    this.gBrowser.tabContainer.removeEventListener("TabGroupCreate", this);
    this.gBrowser.tabContainer.removeEventListener("TabGroupRemoved", this);
    this.gBrowser.tabContainer.removeEventListener("TabGrouped", this);
    this.gBrowser.tabContainer.removeEventListener("TabUngrouped", this);

    this.containerNode.removeEventListener("click", this);

    if (this.dropIndicator) {
      this.containerNode.removeEventListener("dragstart", this);
      this.containerNode.removeEventListener("dragover", this);
      this.containerNode.removeEventListener("dragleave", this);
      this.containerNode.removeEventListener("dragend", this);
      this.containerNode.removeEventListener("drop", this);
    }

    this.listenersRegistered = false;
  }

  _tabAttrModified(tab) {
    let item = this.tabToElement.get(tab);
    if (item) {
      if (!this.filterFn(tab)) {
        // The tab no longer matches our criteria, remove it.
        this._removeItem(item, tab);
      } else {
        this._setRowAttributes(item, tab);
      }
    } else if (this.filterFn(tab)) {
      // The tab now matches our criteria, add a row for it.
      this._addTab(tab);
    }
  }

  /**
   * @param {MozTabbrowserTab} tab
   */
  _moveTab(tab) {
    let item = this.tabToElement.get(tab);
    if (item) {
      this._removeItem(item, tab);
      this._addTab(tab);
    }
  }
  _addTab(newTab) {
    if (!this.filterFn(newTab)) {
      return;
    }
    let newRow = this._createRow(newTab);
    let nextTab = newTab.nextElementSibling;

    while (nextTab && !this.filterFn(nextTab)) {
      nextTab = nextTab.nextElementSibling;
    }

    // If we found a tab after this one in the list, insert the new row before it.
    let nextRow = this.tabToElement.get(nextTab);
    if (nextRow) {
      nextRow.parentNode.insertBefore(newRow, nextRow);
    } else {
      // If there's no next tab then insert it as usual.
      this._addElement(newRow);
    }
  }
  _tabClose(tab) {
    let item = this.tabToElement.get(tab);
    if (item) {
      this._removeItem(item, tab);
    }
  }

  _removeItem(item, tab) {
    this.tabToElement.delete(tab);
    item.remove();
  }
}

const TABS_PANEL_EVENTS = {
  show: "ViewShowing",
  hide: "PanelMultiViewHidden",
};

export class TabsPanel extends TabsListBase {
  constructor(opts) {
    super({
      ...opts,
      containerNode:
        opts.containerNode ||
        opts.insertBefore?.parentNode ||
        opts.view.firstElementChild,
    });
    this.view = opts.view;
    this.view.addEventListener(TABS_PANEL_EVENTS.show, this);
    this.panelMultiView = null;
  }

  handleEvent(event) {
    switch (event.type) {
      case TABS_PANEL_EVENTS.hide:
        if (event.target == this.panelMultiView) {
          this._cleanup();
          this.panelMultiView = null;
        }
        break;
      case TABS_PANEL_EVENTS.show:
        if (!this.listenersRegistered && event.target == this.view) {
          this.panelMultiView = this.view.panelMultiView;
          this._populate(event);
          this.gBrowser.translateTabContextMenu();
        }
        break;
      case "command":
        if (event.target.classList.contains("all-tabs-mute-button")) {
          event.target.tab.toggleMuteAudio();
          break;
        }
        if (event.target.classList.contains("all-tabs-close-button")) {
          this.gBrowser.removeTab(event.target.tab, {
            telemetrySource: lazy.TabMetrics.METRIC_SOURCE.TAB_OVERFLOW_MENU,
          });
          break;
        }
        if ("tabGroupId" in event.target.dataset) {
          this.gBrowser
            .getTabGroupById(event.target.dataset.tabGroupId)
            ?.select();
        }
      // fall through
      default:
        super.handleEvent(event);
        break;
    }
  }

  _populate(event) {
    super._populate(event);

    // The loading throbber can't be set until the toolbarbutton is rendered,
    // so set the image attributes again now that the elements are in the DOM.
    for (let row of this.rows) {
      // Ensure this isn't a group label
      if (row.tab) {
        this._setImageAttributes(row, row.tab);
      }
    }
  }

  _selectTab(tab) {
    super._selectTab(tab);
    lazy.PanelMultiView.hidePopup(this.view.closest("panel"));
  }

  _setupListeners() {
    super._setupListeners();
    this.panelMultiView.addEventListener(TABS_PANEL_EVENTS.hide, this);
  }

  _cleanupListeners() {
    super._cleanupListeners();
    this.panelMultiView.removeEventListener(TABS_PANEL_EVENTS.hide, this);
  }

  _createRow(tab) {
    let { doc } = this;
    let row = doc.createXULElement("toolbaritem");
    row.setAttribute("class", "all-tabs-item");
    row.setAttribute("context", "tabContextMenu");
    if (this.className) {
      row.classList.add(this.className);
    }
    row.tab = tab;
    row.addEventListener("command", this);
    this.tabToElement.set(tab, row);

    let button = doc.createXULElement("toolbarbutton");
    button.setAttribute(
      "class",
      "all-tabs-button subviewbutton subviewbutton-iconic"
    );
    button.setAttribute("flex", "1");
    button.setAttribute("crop", "end");
    button.tab = tab;

    if (tab.userContextId) {
      tab.classList.forEach(property => {
        if (property.startsWith("identity-color")) {
          button.classList.add(property);
          button.classList.add("all-tabs-container-indicator");
        }
      });
    }

    if (tab.group) {
      row.classList.add("grouped");
    }

    row.appendChild(button);

    let muteButton = doc.createXULElement("toolbarbutton");
    muteButton.classList.add(
      "all-tabs-mute-button",
      "all-tabs-secondary-button",
      "subviewbutton"
    );
    muteButton.setAttribute("closemenu", "none");
    muteButton.tab = tab;
    row.appendChild(muteButton);

    if (!tab.pinned) {
      let closeButton = doc.createXULElement("toolbarbutton");
      closeButton.classList.add(
        "all-tabs-close-button",
        "all-tabs-secondary-button",
        "subviewbutton"
      );
      closeButton.setAttribute("closemenu", "none");
      doc.l10n.setAttributes(closeButton, "tabbrowser-manager-close-tab");
      closeButton.tab = tab;
      row.appendChild(closeButton);
    }

    this._setRowAttributes(row, tab);

    return row;
  }

  /**
   * @param {MozTabbrowserTabGroup} group
   * @returns {XULElement}
   */
  _createGroupRow(group) {
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
    row.addEventListener("command", this);
    let button = doc.createXULElement("toolbarbutton");
    button.setAttribute("context", "open-tab-group-context-menu");
    button.dataset.tabGroupId = group.id;
    button.classList.add(
      "all-tabs-button",
      "all-tabs-group-button",
      "subviewbutton",
      "subviewbutton-iconic",
      group.collapsed ? "tab-group-icon-collapsed" : "tab-group-icon"
    );
    button.setAttribute("flex", "1");
    button.setAttribute("crop", "end");

    let setName = tabGroupName => {
      doc.l10n.setAttributes(
        button,
        "tabbrowser-manager-current-window-tab-group",
        { tabGroupName }
      );
    };

    if (group.label) {
      setName(group.label);
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

  _setRowAttributes(row, tab) {
    setAttributes(row, { selected: tab.selected });

    let tooltiptext = this.gBrowser.getTabTooltip(tab);
    let busy = tab.getAttribute("busy");
    let button = row.firstElementChild;
    setAttributes(button, {
      busy,
      label: tab.label,
      tooltiptext,
      image: !busy && tab.getAttribute("image"),
      iconloadingprincipal: tab.getAttribute("iconloadingprincipal"),
    });

    this._setImageAttributes(row, tab);

    let muteButton = row.querySelector(".all-tabs-mute-button");
    let muteButtonTooltipString = tab.muted
      ? "tabbrowser-manager-unmute-tab"
      : "tabbrowser-manager-mute-tab";
    this.doc.l10n.setAttributes(muteButton, muteButtonTooltipString);

    setAttributes(muteButton, {
      muted: tab.muted,
      soundplaying: tab.soundPlaying,
      hidden: !(tab.muted || tab.soundPlaying),
    });
  }

  _setImageAttributes(row, tab) {
    let button = row.firstElementChild;
    let image = button.icon;

    if (image) {
      let busy = tab.getAttribute("busy");
      let progress = tab.getAttribute("progress");
      setAttributes(image, { busy, progress });
      if (busy) {
        image.classList.add("tab-throbber-tabslist");
      } else {
        image.classList.remove("tab-throbber-tabslist");
      }
    }
  }

  _onDragStart(event) {
    const row = this._getTargetRowFromEvent(event);
    if (!row) {
      return;
    }

    this.gBrowser.tabContainer.startTabDrag(event, row.firstElementChild.tab, {
      fromTabList: true,
    });
  }

  _getTargetRowFromEvent(event) {
    return event.target.closest("toolbaritem");
  }

  _isMovingTabs(event) {
    var effects = this.gBrowser.tabContainer.getDropEffectForTabDrag(event);
    return effects == "move";
  }

  _onDragOver(event) {
    if (!this._isMovingTabs(event)) {
      return;
    }

    if (!this._updateDropTarget(event)) {
      return;
    }

    event.preventDefault();
    event.stopPropagation();
  }

  _getRowIndex(row) {
    return Array.prototype.indexOf.call(this.containerNode.children, row);
  }

  _onDrop(event) {
    if (!this._isMovingTabs(event)) {
      return;
    }

    if (!this._updateDropTarget(event)) {
      return;
    }

    event.preventDefault();
    event.stopPropagation();

    let draggedTab = event.dataTransfer.mozGetDataAt(TAB_DROP_TYPE, 0);

    if (draggedTab === this.dropTargetRow.firstElementChild.tab) {
      this._clearDropTarget();
      return;
    }

    const targetTab = this.dropTargetRow.firstElementChild.tab;

    // NOTE: Given the list is opened only when the window is focused,
    //       we don't have to check `draggedTab.container`.

    let pos;
    if (draggedTab._tPos < targetTab._tPos) {
      pos = targetTab._tPos + this.dropTargetDirection;
    } else {
      pos = targetTab._tPos + this.dropTargetDirection + 1;
    }
    this.gBrowser.moveTabTo(draggedTab, { tabIndex: pos });

    this._clearDropTarget();
  }

  _onDragLeave(event) {
    if (!this._isMovingTabs(event)) {
      return;
    }

    let target = event.relatedTarget;
    while (target && target != this.containerNode) {
      target = target.parentNode;
    }
    if (target) {
      return;
    }

    this._clearDropTarget();
  }

  _onDragEnd(event) {
    if (!this._isMovingTabs(event)) {
      return;
    }

    this._clearDropTarget();
  }

  _updateDropTarget(event) {
    const row = this._getTargetRowFromEvent(event);
    if (!row) {
      return false;
    }

    const rect = row.getBoundingClientRect();
    const index = this._getRowIndex(row);
    if (index === -1) {
      return false;
    }

    const threshold = rect.height * 0.5;
    if (event.clientY < rect.top + threshold) {
      this._setDropTarget(row, -1);
    } else {
      this._setDropTarget(row, 0);
    }

    return true;
  }

  _setDropTarget(row, direction) {
    this.dropTargetRow = row;
    this.dropTargetDirection = direction;

    const holder = this.dropIndicator.parentNode;
    const holderOffset = holder.getBoundingClientRect().top;

    // Set top to before/after the target row.
    let top;
    if (this.dropTargetDirection === -1) {
      if (this.dropTargetRow.previousSibling) {
        const rect = this.dropTargetRow.previousSibling.getBoundingClientRect();
        top = rect.top + rect.height;
      } else {
        const rect = this.dropTargetRow.getBoundingClientRect();
        top = rect.top;
      }
    } else {
      const rect = this.dropTargetRow.getBoundingClientRect();
      top = rect.top + rect.height;
    }

    // Avoid overflowing the sub view body.
    const indicatorHeight = 12;
    const subViewBody = holder.parentNode;
    const subViewBodyRect = subViewBody.getBoundingClientRect();
    top = Math.min(top, subViewBodyRect.bottom - indicatorHeight);

    this.dropIndicator.style.top = `${top - holderOffset - 12}px`;
    this.dropIndicator.collapsed = false;
  }

  _clearDropTarget() {
    if (this.dropTargetRow) {
      this.dropTargetRow = null;
    }

    if (this.dropIndicator) {
      this.dropIndicator.style.top = `0px`;
      this.dropIndicator.collapsed = true;
    }
  }

  _onClick(event) {
    if (event.button == 1) {
      const row = this._getTargetRowFromEvent(event);
      if (!row) {
        return;
      }

      this.gBrowser.removeTab(row.tab, {
        animate: true,
      });
    }
  }
}
