/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

import { XPCOMUtils } from "resource://gre/modules/XPCOMUtils.sys.mjs";

const lazy = XPCOMUtils.declareLazy({
  PanelMultiView:
    "moz-src:///browser/components/customizableui/PanelMultiView.sys.mjs",
  TabMetrics: "moz-src:///browser/components/tabbrowser/TabMetrics.sys.mjs",
});

const TAB_DROP_TYPE = "application/x-moz-tabbrowser-tab";

const ROW_VARIANT_TAB = "tab";
const ROW_VARIANT_TAB_GROUP = "tab-group";

/**
 * A row of a tabs list, with the tab or tab group it stands for.
 *
 * @typedef {XULElement & { _tab?: MozTabbrowserTab, _tabGroup?: MozTabbrowserTabGroup }} XulToolbarItem
 */

/**
 * A `panelview`, with the `panelmultiview` that PanelMultiView sets on it
 * while the view is showing.
 *
 * @typedef {XULElement & { panelMultiView: XULElement|null }} PanelView
 */

function setAttributes(element, attrs) {
  for (let [name, value] of Object.entries(attrs)) {
    if (value) {
      element.setAttribute(name, value);
    } else {
      element.removeAttribute(name);
    }
  }
}

/**
 * @param {Element} element
 *   One row (`toolbaritem`) of this tab list or one of its descendent
 *   elements, e.g. a `toolbarbutton`.
 * @returns {MozTabbrowserTab|undefined}
 */
function getTabFromRow(element) {
  return /** @type {XulToolbarItem} */ (element.closest("toolbaritem"))?._tab;
}

/**
 * @param {Element} element
 *   One row (`toolbaritem`) of this tab list or one of its descendent
 *   elements, e.g. a `toolbarbutton`.
 * @returns {MozTabbrowserTabGroup|undefined}
 */
function getTabGroupFromRow(element) {
  return /** @type {XulToolbarItem} */ (element.closest("toolbaritem"))
    ?._tabGroup;
}

/**
 * @param {Element} element
 *   One row (`toolbaritem`) of this tab list or one of its descendent
 *   elements, e.g. a `toolbarbutton`.
 * @returns {"tab"|"tab-group"|undefined}
 */
function getRowVariant(element) {
  return /** @type {"tab"|"tab-group"|undefined} */ (
    element.closest("toolbaritem")?.getAttribute("row-variant")
  );
}

class TabsListBase {
  /** @returns {Promise<void>} */
  get domRefreshComplete() {
    return this.#domRefreshPromise ?? Promise.resolve();
  }

  /** @type {Promise<void>|undefined} */
  #domRefreshPromise;

  /** @type {Map<MozTabbrowserTab, XulToolbarItem>} */
  tabToElement = new Map();

  /**
   * @param {object} opts
   * @param {string} opts.className
   * @param {function(MozTabbrowserTab):boolean} opts.filterFn
   * @param {Element} opts.containerNode
   * @param {XULElement} [opts.dropIndicator=null]
   * @param {boolean} opts.onlyHiddenTabs
   */
  constructor({
    className,
    filterFn,
    containerNode,
    dropIndicator = null,
    onlyHiddenTabs,
  }) {
    /** @type {string} */
    this.className = className;
    /** @type {function(MozTabbrowserTab):boolean} */
    this.filterFn = onlyHiddenTabs
      ? tab => filterFn(tab) && tab.hidden
      : filterFn;
    /** @type {Element} */
    this.containerNode = containerNode;
    /** @type {XULElement|null} */
    this.dropIndicator = dropIndicator;

    if (this.dropIndicator) {
      /** @type {XulToolbarItem|null} */
      this.dropTargetRow = null;
      /** @type {-1|0} */
      this.dropTargetDirection = 0;
      /** @type {boolean} */
      this.dropTargetOutsideList = false;
    }

    /** @type {Document} */
    this.doc = containerNode.ownerDocument;
    /** @type {Tabbrowser} */
    this.gBrowser = this.doc.defaultView.gBrowser;
    /** @type {boolean} */
    this.listenersRegistered = false;
    /** @type {boolean} */
    this.onlyHiddenTabs = onlyHiddenTabs;
  }

  /** @returns {MapIterator<XulToolbarItem>} */
  get rows() {
    return this.tabToElement.values();
  }

  /**
   * The element that listens for drag events. A list can accept drops on a
   * larger area than the rows it holds, so that a drop next to the rows still
   * reorders the tabs.
   *
   * @returns {Element}
   */
  get dropTargetNode() {
    return this.containerNode;
  }

  /**
   * Tabs lists can be nested in the DOM, e.g. the list of hidden tabs playing
   * audio sits inside the main tabs list in the All Tabs menu. Bubbling events
   * from a nested list must only be handled by the list that owns the row.
   *
   * @param {Event} event
   * @returns {boolean}
   */
  #ownsEvent(event) {
    let row = /** @type {Element} */ (event.target).closest("toolbaritem");
    return !row || row.parentNode == this.containerNode;
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
      case "TabGroupMoved":
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
        this.#ownsEvent(event) && this.#handleCommand(event);
        break;
      case "dragstart":
        this.#ownsEvent(event) && this._onDragStart(event);
        break;
      // `dragover` and `drop` are not filtered by `#ownsEvent`. They listen on
      // `dropTargetNode`, which is larger than `containerNode`, and
      // `_getDropTargetFromEvent` maps any position in that area to one of the
      // rows.
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
        this.#ownsEvent(event) && this._onClick(event);
        break;
    }
  }

  /**
   * @param {XULCommandEvent} event
   */
  #handleCommand(event) {
    const target = /** @type {Element} */ (event.target);
    if (target.classList.contains("all-tabs-mute-button")) {
      getTabFromRow(target)?.toggleMuteAudio();
    } else if (target.classList.contains("all-tabs-close-button")) {
      const tab = getTabFromRow(target);
      if (tab) {
        this.gBrowser.removeTab(tab, {
          metricsContext: lazy.TabMetrics.userTriggeredContext(
            lazy.TabMetrics.METRIC_SOURCE.TAB_OVERFLOW_MENU
          ),
        });
      }
    } else {
      const rowVariant = getRowVariant(target);
      if (rowVariant == ROW_VARIANT_TAB) {
        const tab = getTabFromRow(target);
        if (tab) {
          this._selectTab(tab);
        }
      } else if (rowVariant == ROW_VARIANT_TAB_GROUP) {
        getTabGroupFromRow(target)?.select();
      }
    }
  }

  _selectTab(tab) {
    if (this.gBrowser.selectedTab != tab) {
      this.gBrowser.setSelectedTab(
        tab,
        this.gBrowser.TabMetrics.userTriggeredContext(
          this.gBrowser.TabMetrics.METRIC_SOURCE.TAB_OVERFLOW_MENU
        )
      );
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

        let tabHiddenByGroup = tab.group?.collapsed && !tab.selected;
        if (!tabHiddenByGroup || this.onlyHiddenTabs) {
          // Don't show tabs in collapsed tab groups in the main tabs list.
          // However, in the hidden tabs lists, do show hidden tabs even if
          // they belong to collapsed tab groups.
          fragment.appendChild(this._createRow(tab));
        }
      }
    }

    this._addElement(fragment);
  }

  _addElement(elementOrFragment) {
    this.containerNode.appendChild(elementOrFragment);
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
      .querySelectorAll(":scope toolbaritem")
      .forEach(node => node.remove());
    this.tabToElement = new Map();
  }

  _refreshDOM() {
    if (!this.#domRefreshPromise) {
      this.#domRefreshPromise = new Promise(resolve => {
        this.containerNode.documentGlobal.requestAnimationFrame(() => {
          if (this.#domRefreshPromise) {
            if (this.listenersRegistered) {
              // Only re-render the menu DOM if the menu is still open.
              this._cleanupDOM();
              this._populateDOM();
            }
            resolve();
            this.#domRefreshPromise = undefined;
          }
        });
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
    this.gBrowser.tabContainer.addEventListener("TabGroupMoved", this);
    this.gBrowser.tabContainer.addEventListener("TabGrouped", this);
    this.gBrowser.tabContainer.addEventListener("TabUngrouped", this);

    this.containerNode.addEventListener("click", this);
    this.containerNode.addEventListener("command", this);

    if (this.dropIndicator) {
      this.containerNode.addEventListener("dragstart", this);
      this.dropTargetNode.addEventListener("dragover", this);
      this.dropTargetNode.addEventListener("dragleave", this);
      this.dropTargetNode.addEventListener("dragend", this);
      this.dropTargetNode.addEventListener("drop", this);
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
    this.gBrowser.tabContainer.removeEventListener("TabGroupMoved", this);
    this.gBrowser.tabContainer.removeEventListener("TabGrouped", this);
    this.gBrowser.tabContainer.removeEventListener("TabUngrouped", this);

    this.containerNode.removeEventListener("click", this);
    this.containerNode.removeEventListener("command", this);

    if (this.dropIndicator) {
      this.containerNode.removeEventListener("dragstart", this);
      this.dropTargetNode.removeEventListener("dragover", this);
      this.dropTargetNode.removeEventListener("dragleave", this);
      this.dropTargetNode.removeEventListener("dragend", this);
      this.dropTargetNode.removeEventListener("drop", this);
    }

    this.listenersRegistered = false;
  }

  /**
   * @param {MozTabbrowserTab} tab
   */
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
    for (let t of tab.splitview?.tabs ?? [tab]) {
      let item = this.tabToElement.get(t);
      if (item) {
        this._removeItem(item, t);
        this._addTab(t);
      }
    }
  }

  /**
   * @param {MozTabbrowserTab} newTab
   */
  _addTab(newTab) {
    if (!this.filterFn(newTab)) {
      return;
    }
    if (newTab.group?.collapsed && !this.onlyHiddenTabs) {
      return;
    }

    let newRow = this._createRow(newTab);
    let nextTab = this.gBrowser.tabContainer.findNextTab(newTab, {
      filter: this.filterFn,
    });
    if (!nextTab) {
      // If there's no next tab then append the new row to the end of the menu.
      this._addElement(newRow);
    } else if (!newTab.group && nextTab.group) {
      // newTab should not go right before nextTab because then it would
      // appear to be inside the tab group; instead, put newTab before
      // nextTab's tab group's row menu item.
      // Should be equivalent to `.insertBefore(newRow, nextRow.previousSiblingElement)`
      // but this is more explicit about inserting before the nextTab's tab group's
      // row menu item.
      let nextTabTabGroupRow = this.containerNode.querySelector(
        `:scope [tab-group-id="${nextTab.group.id}"]`
      );
      this.containerNode.insertBefore(newRow, nextTabTabGroupRow);
    } else {
      let nextRow = this.tabToElement.get(nextTab);
      if (!nextRow) {
        // If for some reason the next tab has no item in this menu already,
        // just add this new tab's menu item to the end.
        this._addElement(newRow);
      } else {
        this.containerNode.insertBefore(newRow, nextRow);
      }
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
    // If removing this grouped tab results in there being no more tabs from
    // this tab group in the menu list, then also remove the tab group label
    // menu item. This is only relevant right now in tabs lists that only show
    // hidden tabs. For the normal tabs list, removing the last tab in a group
    // will also remove the tab group, which re-renders the whole tabs list
    // with the side-effect of removing the tab group label menu item.
    if (
      tab.group &&
      !this.tabToElement.keys().some(t => t.group == tab.group)
    ) {
      this.containerNode
        .querySelector(`:scope [tab-group-id="${tab.group.id}"]`)
        ?.remove();
    }
  }

  /**
   * @param {MozTabbrowserTab} _tab
   * @returns {XulToolbarItem}
   * @abstract
   */
  _createRow(_tab) {
    throw new Error("Trying to access the base class, must be overridden");
  }

  /**
   * @param {MozTabbrowserTabGroup} _group
   * @returns {XulToolbarItem}
   * @abstract
   */
  _createGroupRow(_group) {
    throw new Error("Trying to access the base class, must be overridden");
  }

  /**
   * @param {XulToolbarItem} _row
   * @param {MozTabbrowserTab} _tab
   * @abstract
   */
  _setRowAttributes(_row, _tab) {
    throw new Error("Trying to access the base class, must be overridden");
  }

  /**
   * @abstract
   */
  _clearDropTarget() {
    throw new Error("Trying to access the base class, must be overridden");
  }

  /**
   * @param {DragEvent} _event
   * @abstract
   */
  _onDragStart(_event) {
    throw new Error("Trying to access the base class, must be overridden");
  }

  /**
   * @param {DragEvent} _event
   * @abstract
   */
  _onDragOver(_event) {
    throw new Error("Trying to access the base class, must be overridden");
  }

  /**
   * @param {DragEvent} _event
   * @abstract
   */
  _onDragLeave(_event) {
    throw new Error("Trying to access the base class, must be overridden");
  }

  /**
   * @param {DragEvent} _event
   * @abstract
   */
  _onDragEnd(_event) {
    throw new Error("Trying to access the base class, must be overridden");
  }

  /**
   * @param {DragEvent} _event
   * @abstract
   */
  _onDrop(_event) {
    throw new Error("Trying to access the base class, must be overridden");
  }

  /**
   * @param {MouseEvent} _event
   * @abstract
   */
  _onClick(_event) {
    throw new Error("Trying to access the base class, must be overridden");
  }
}

const TABS_PANEL_EVENTS = {
  show: "ViewShowing",
  hide: "PanelMultiViewHidden",
};

export class TabsPanel extends TabsListBase {
  /**
   * @param {object} opts
   * @param {string} opts.className
   * @param {function(MozTabbrowserTab):boolean} opts.filterFn
   * @param {Element} opts.containerNode
   * @param {XULElement} [opts.dropIndicator=null]
   * @param {PanelView} opts.view
   * @param {boolean} opts.onlyHiddenTabs
   */
  constructor(opts) {
    super({
      ...opts,
      containerNode: opts.containerNode || opts.view.firstElementChild,
    });
    this.view = opts.view;
    this.view.addEventListener(TABS_PANEL_EVENTS.show, this);
    /** @type {XULElement|null} */
    this.panelMultiView = null;
  }

  /**
   * The panel holds items that are not rows of this list, such as the
   * "View All Tabs" button. A tab dropped on one of them must still reorder
   * the tabs, so the whole panel accepts the drop.
   *
   * @returns {Element}
   */
  get dropTargetNode() {
    return this.view;
  }

  /**
   * Adds rows after the last row. Elements that are not rows, such as the
   * hidden tabs button, can follow the rows and must stay at the end.
   *
   * @param {Element|DocumentFragment} elementOrFragment
   */
  _addElement(elementOrFragment) {
    const lastRow = this._findDropCandidateRow(
      this.containerNode.lastElementChild,
      true
    );
    if (lastRow) {
      lastRow.after(elementOrFragment);
    } else {
      super._addElement(elementOrFragment);
    }
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
          this._populate();
          this.gBrowser.translateTabContextMenu();
        }
        break;
      default:
        super.handleEvent(event);
        break;
    }
  }

  _populate() {
    super._populate();

    // The loading throbber can't be set until the toolbarbutton is rendered,
    // so set the image attributes again now that the elements are in the DOM.
    for (let row of this.rows) {
      // Ensure this isn't a group label
      if (getRowVariant(row) == ROW_VARIANT_TAB) {
        this._setImageAttributes(row, getTabFromRow(row));
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

  /**
   * @param {MozTabbrowserTab} tab
   * @returns {XulToolbarItem}
   */
  _createRow(tab) {
    let { doc } = this;
    /** @type {XulToolbarItem} */
    let row = doc.createXULElement("toolbaritem");
    row.setAttribute("class", "all-tabs-item");
    if (this.className) {
      row.classList.add(this.className);
    }
    row.setAttribute("context", "tabContextMenu");
    row.setAttribute("row-variant", ROW_VARIANT_TAB);

    /**
     * Setting a new property `XulToolbarItem._tab` on the row elements
     * for internal use by this module only.
     *
     * @see getTabFromRow
     */
    row._tab = tab;
    this.tabToElement.set(tab, row);

    /** @type {XULElement & { tab?: MozTabbrowserTab }} */
    let button = doc.createXULElement("toolbarbutton");
    button.setAttribute(
      "class",
      "all-tabs-button subviewbutton subviewbutton-iconic"
    );
    button.setAttribute("flex", "1");
    button.setAttribute("crop", "end");

    /**
     * Setting a new property `MozToolbarbutton.tab` on the buttons
     * to support tab context menu integration.
     *
     * @see TabContextMenu.updateContextMenu
     */
    button.tab = tab;

    if (tab.userContextId) {
      tab.classList.forEach(property => {
        if (
          property.startsWith("identity-color") ||
          property.startsWith("identity-icon")
        ) {
          button.classList.add(property);
        }
      });
      button.classList.add("all-tabs-container-indicator");
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
      row.appendChild(closeButton);
    }

    this._setRowAttributes(row, tab);

    return row;
  }

  /**
   * @param {MozTabbrowserTabGroup} group
   * @returns {XulToolbarItem}
   */
  _createGroupRow(group) {
    let { doc } = this;
    /** @type {XulToolbarItem} */
    let row = doc.createXULElement("toolbaritem");
    row.setAttribute("class", "all-tabs-item all-tabs-group-item");
    row.setAttribute("row-variant", ROW_VARIANT_TAB_GROUP);
    row.setAttribute("tab-group-id", group.id);
    /**
     * Setting a new property `XulToolbarItem._tabGroup` on the row elements
     * for internal use by this module only.
     *
     * @see getTabGroupFromRow
     */
    row._tabGroup = group;

    row.style.setProperty(
      "--tab-group-color",
      `var(--tab-group-${group.color})`
    );
    row.style.setProperty(
      "--tab-group-color-invert",
      `var(--tab-group-${group.color}-invert)`
    );
    row.style.setProperty(
      "--tab-group-color-pale",
      `var(--tab-group-${group.color}-pale)`
    );
    row.style.setProperty(
      "--tab-group-background-color",
      `var(--tab-group-${group.color})`
    );

    let button = doc.createXULElement("toolbarbutton");
    button.setAttribute("context", "open-tab-group-context-menu");
    button.dataset.tabGroupId = group.id;
    button.classList.add(
      "all-tabs-button",
      "all-tabs-group-button",
      "subviewbutton",
      "subviewbutton-iconic",
      "tab-group-icon"
    );
    if (group.collapsed) {
      button.classList.add("tab-group-icon-collapsed");
    }
    button.setAttribute("flex", "1");
    button.setAttribute("crop", "end");

    doc.l10n.setAttributes(
      button,
      "tabbrowser-manager-current-window-tab-group",
      { tabGroupName: group.label || group.defaultGroupName }
    );
    row.appendChild(button);
    return row;
  }

  /**
   * @param {XulToolbarItem} row
   * @param {MozTabbrowserTab} tab
   */
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

  /**
   * @param {XulToolbarItem} row
   * @param {MozTabbrowserTab} tab
   */
  _setImageAttributes(row, tab) {
    let button = /** @type {MozToolbarbutton} */ (row.firstElementChild);
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

  /**
   * @param {DragEvent} event
   */
  _onDragStart(event) {
    const row = this._getTargetRowFromEvent(event);
    if (!row) {
      return;
    }

    const elementToDrag =
      getRowVariant(row) == ROW_VARIANT_TAB_GROUP
        ? getTabGroupFromRow(row).labelElement
        : getTabFromRow(row);

    this.gBrowser.tabContainer.tabDragAndDrop.startTabDrag(
      event,
      elementToDrag.splitview || elementToDrag,
      {
        fromTabList: true,
      }
    );
  }

  /**
   * @param {DragEvent|MouseEvent} event
   * @returns {XulToolbarItem|undefined}
   */
  _getTargetRowFromEvent(event) {
    return /** @type {XulToolbarItem} */ (
      /** @type {Element} */ (event.target).closest("toolbaritem")
    );
  }

  /**
   * @param {DragEvent} event
   * @returns {boolean}
   */
  _isMovingTabs(event) {
    var effects =
      this.gBrowser.tabContainer.tabDragAndDrop.getDropEffectForTabDrag(event);
    return effects == "move";
  }

  /**
   * @param {DragEvent} event
   */
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

  /**
   * @param {DragEvent} event
   */
  _onDrop(event) {
    if (!this._isMovingTabs(event)) {
      return;
    }

    if (!this._updateDropTarget(event)) {
      return;
    }

    event.preventDefault();
    event.stopPropagation();

    let draggedElement = event.dataTransfer.mozGetDataAt(TAB_DROP_TYPE, 0);
    /** @type {MozTabbrowserTab|MozTabbrowserTabGroup|MozTabbrowserTabGroupLabel} */
    let targetElement =
      getRowVariant(this.dropTargetRow) == ROW_VARIANT_TAB_GROUP
        ? getTabGroupFromRow(this.dropTargetRow).labelElement
        : getTabFromRow(this.dropTargetRow);

    // A tab dropped above or below the list, or below its last row, does not
    // join a group that starts or ends at the edge of the list.
    const group = targetElement.group;
    const groupEdgeTab =
      this.dropTargetDirection == -1 ? group?.tabs[0] : group?.tabs.at(-1);
    if (this.dropTargetOutsideList && targetElement == groupEdgeTab) {
      targetElement = group;
    }

    if (draggedElement === targetElement) {
      this._clearDropTarget();
      return;
    }

    // NOTE: Given the list is opened only when the window is focused,
    //       we don't have to check `draggedTab.container`.
    const metricsContext = lazy.TabMetrics.userTriggeredContext(
      lazy.TabMetrics.METRIC_SOURCE.TAB_OVERFLOW_MENU
    );
    if (this.dropTargetDirection == -1) {
      this.gBrowser.moveTabBefore(draggedElement, targetElement, {
        metricsContext,
      });
    } else {
      this.gBrowser.moveTabAfter(draggedElement, targetElement, {
        metricsContext,
      });
    }

    this._clearDropTarget();
  }

  /**
   * @param {DragEvent} event
   */
  _onDragLeave(event) {
    if (!this._isMovingTabs(event)) {
      return;
    }

    let target = /** @type {Node} */ (event.relatedTarget);
    while (target && target != this.dropTargetNode) {
      target = target.parentNode;
    }
    if (target) {
      return;
    }

    this._clearDropTarget();
  }

  /**
   * @param {DragEvent} event
   */
  _onDragEnd(event) {
    if (!this._isMovingTabs(event)) {
      return;
    }

    this._clearDropTarget();
  }

  /**
   * @param {Element|null} element
   * @returns {boolean}
   */
  _isDropCandidateRow(element) {
    return (
      element?.localName == "toolbaritem" &&
      element.parentNode == this.containerNode
    );
  }

  /**
   * @param {Element|null} start
   * @param {boolean} backward
   * @returns {XulToolbarItem|null}
   *   The first drop candidate row at `start` or after it, or at `start` or
   *   before it if `backward` is true.
   */
  _findDropCandidateRow(start, backward) {
    let element = start;
    while (element && !this._isDropCandidateRow(element)) {
      element = backward
        ? element.previousElementSibling
        : element.nextElementSibling;
    }
    return /** @type {XulToolbarItem|null} */ (element);
  }

  /**
   * Finds the first row that ends below `y` with a binary search.
   *
   * @param {number} y
   *   A vertical position in CSS pixels, in client coordinates. These are the
   *   coordinates of `DragEvent.clientY` and `getBoundingClientRect()`.
   * @returns {XulToolbarItem|null}
   *   The row, or the last row if no row ends below `y`, or null if the list
   *   has no rows.
   */
  _findRowEndingBelow(y) {
    // Elements that are not rows, such as the hidden tabs button, are only at
    // the start or the end of the list. Search the range between them.
    const children = this.containerNode.children;
    let low = 0;
    let high = children.length - 1;
    while (low <= high && !this._isDropCandidateRow(children[low])) {
      low++;
    }
    while (high >= low && !this._isDropCandidateRow(children[high])) {
      high--;
    }
    if (low > high) {
      return null;
    }
    while (low < high) {
      const mid = Math.floor((low + high) / 2);
      if (children[mid].getBoundingClientRect().bottom <= y) {
        low = mid + 1;
      } else {
        high = mid;
      }
    }
    return /** @type {XulToolbarItem} */ (children[low]);
  }

  /**
   * Finds the row that a drag event applies to. A position above the list
   * resolves to the first visible row, and a position below the list resolves
   * to the last visible row.
   *
   * @param {DragEvent} event
   * @returns {{row: XulToolbarItem, direction: -1|0, outsideList: boolean}|null}
   *   `outsideList` is true if the position is above or below the list, or
   *   below its last row.
   */
  _getDropTargetFromEvent(event) {
    const { clientY } = event;
    const getDirection = rect =>
      clientY < rect.top + rect.height * 0.5 ? -1 : 0;

    const hitRow = this._getTargetRowFromEvent(event);
    if (this._isDropCandidateRow(hitRow)) {
      return {
        row: hitRow,
        direction: getDirection(hitRow.getBoundingClientRect()),
        outsideList: false,
      };
    }

    const containerRect = this.containerNode.getBoundingClientRect();
    if (clientY < containerRect.top) {
      const row = this._findRowEndingBelow(containerRect.top);
      return row && { row, direction: -1, outsideList: true };
    }
    if (clientY >= containerRect.bottom) {
      const row = this._findRowEndingBelow(containerRect.bottom - 1);
      return row && { row, direction: 0, outsideList: true };
    }

    // The position is inside the list but not on a row, e.g. on the hidden
    // tabs button.
    const row = this._findRowEndingBelow(clientY);
    if (!row) {
      return null;
    }
    const rect = row.getBoundingClientRect();
    return {
      row,
      direction: getDirection(rect),
      outsideList: clientY >= rect.bottom,
    };
  }

  /**
   * @param {DragEvent} event
   * @returns {boolean}
   */
  _updateDropTarget(event) {
    const target = this._getDropTargetFromEvent(event);
    if (!target) {
      return false;
    }

    const { row, direction } = target;
    this.dropTargetOutsideList = target.outsideList;
    if (
      getRowVariant(row) === ROW_VARIANT_TAB &&
      getTabFromRow(row).splitview
    ) {
      const tab = getTabFromRow(row);
      if (tab == tab.splitview.tabs[0]) {
        this._setDropTarget(row, -1);
      } else if (tab == tab.splitview.tabs[1]) {
        this._setDropTarget(row, 0);
      }
    } else {
      this._setDropTarget(row, direction);
    }

    return true;
  }

  /**
   * @param {XulToolbarItem} row
   * @param {-1|0} direction
   */
  _setDropTarget(row, direction) {
    this.dropTargetRow = row;
    this.dropTargetDirection = direction;

    const holder = /** @type {Element} */ (this.dropIndicator.parentNode);
    const holderOffset = holder.getBoundingClientRect().top;

    // Set top to before/after the target row.
    let top;
    if (this.dropTargetDirection === -1) {
      const previousRow = this._findDropCandidateRow(
        this.dropTargetRow.previousElementSibling,
        true
      );
      if (previousRow) {
        const rect = previousRow.getBoundingClientRect();
        top = rect.top + rect.height;
      } else {
        const rect = this.dropTargetRow.getBoundingClientRect();
        top = rect.top;
      }
    } else {
      const rect = this.dropTargetRow.getBoundingClientRect();
      top = rect.top + rect.height;
    }

    // Keep the indicator in the visible part of the list. The target row can
    // be scrolled out of view.
    const containerRect = this.containerNode.getBoundingClientRect();
    top = Math.min(Math.max(top, containerRect.top), containerRect.bottom);

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

  /**
   * @param {MouseEvent} event
   */
  _onClick(event) {
    if (event.button == 1) {
      const row = this._getTargetRowFromEvent(event);
      if (!row) {
        return;
      }

      const rowVariant = getRowVariant(row);

      if (rowVariant == ROW_VARIANT_TAB) {
        const tab = getTabFromRow(row);
        this.gBrowser.removeTab(tab, {
          animate: true,
          metricsContext: lazy.TabMetrics.userTriggeredContext(
            lazy.TabMetrics.METRIC_SOURCE.TAB_OVERFLOW_MENU
          ),
        });
      } else if (rowVariant == ROW_VARIANT_TAB_GROUP) {
        getTabGroupFromRow(row)?.saveAndClose(
          lazy.TabMetrics.userTriggeredContext(
            lazy.TabMetrics.METRIC_SOURCE.TAB_OVERFLOW_MENU
          )
        );
      }
    }
  }
}
