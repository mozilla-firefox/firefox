/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/**
 * Base class for nodes in the split view tree.
 */
class SplitNode {
  parent = null;
}

/**
 * A leaf node representing a single tab in the split.
 */
class LeafNode extends SplitNode {
  tab = null;
  tabCycleIndex = 0;

  constructor(tab) {
    super();
    this.tab = tab;
  }

  get panelId() {
    return this.tab.linkedPanel;
  }
}

/**
 * A branch node representing a split with two children.
 */
class BranchNode extends SplitNode {
  /** @type {"horizontal"|"vertical"} */
  orientation = "vertical";
  first = null;
  second = null;

  /**
   * @param {SplitNode} first
   * @param {SplitNode} second
   * @param {"horizontal"|"vertical"} orientation
   */
  constructor(first, second, orientation) {
    super();
    this.orientation = orientation;
    this.first = first;
    this.second = second;
    first.parent = this;
    second.parent = this;
  }
}

/**
 * Manages the split view tree lifecycle. Panels stay as direct children of
 * tabpanels (browser elements cannot be reparented without losing content).
 * Visibility is controlled via CSS classes, and layout via flex ordering.
 */
export class SplitViewTree {
  root = null;
  _document = null;
  _tabpanels = null;
  _window = null;
  _splitter = null;
  _boundOnTabSelect = null;

  /**
   * @param {Document} document
   * @param {Element} tabpanels
   * @param {Window} window
   */
  constructor(document, tabpanels, window) {
    this._document = document;
    this._tabpanels = tabpanels;
    this._window = window;

    this._boundOnTabSelect = this._onTabSelect.bind(this);
    this._window.addEventListener("TabSelect", this._boundOnTabSelect);
  }

  /**
   * Create a root split from two tabs.
   *
   * @param {MozTabbrowserTab} tab1
   * @param {MozTabbrowserTab} tab2
   * @param {"horizontal"|"vertical"} orientation
   */
  split(tab1, tab2, orientation) {
    let leaf1 = new LeafNode(tab1);
    let leaf2 = new LeafNode(tab2);
    this.root = new BranchNode(leaf1, leaf2, orientation);

    this._activatePanels();

    this._splitter = this._document.createXULElement("splitter");
    this._splitter.className = "split-view-tree-splitter";
    this._splitter.setAttribute("resizebefore", "sibling");
    this._splitter.setAttribute("resizeafter", "none");
    let firstPanel = this._document.getElementById(leaf1.panelId);
    firstPanel?.after(this._splitter);

    this._tabpanels.setAttribute("splitview-tree", orientation);
  }

  destroy() {
    if (this._boundOnTabSelect) {
      this._window.removeEventListener("TabSelect", this._boundOnTabSelect);
      this._boundOnTabSelect = null;
    }

    if (!this.root) {
      return;
    }

    this._deactivatePanels();

    this._splitter?.remove();
    this._splitter = null;

    this._tabpanels.removeAttribute("splitview-tree");

    this.root = null;
  }

  _activatePanels() {
    let leaves = this.allLeaves();
    for (let i = 0; i < leaves.length; i++) {
      let panel = this._document.getElementById(leaves[i].panelId);
      if (panel) {
        panel.classList.add("split-view-panel-active", "split-tree-panel");
        panel.setAttribute("column", String(i));
      }
    }
    if (this._splitter) {
      this._splitter.hidden = false;
    }
  }

  _deactivatePanels() {
    for (let leaf of this.allLeaves()) {
      let panel = this._document.getElementById(leaf.panelId);
      if (panel) {
        panel.classList.remove("split-view-panel-active", "split-tree-panel");
        panel.removeAttribute("column");
        panel.removeAttribute("width");
        panel.style.removeProperty("width");
      }
    }
    if (this._splitter) {
      this._splitter.hidden = true;
    }
  }

  /**
   * Find the LeafNode for a given tab.
   *
   * @param {MozTabbrowserTab} tab
   * @returns {LeafNode|null}
   */
  findLeaf(tab) {
    return this._findLeafInNode(this.root, tab);
  }

  _findLeafInNode(node, tab) {
    if (!node) {
      return null;
    }
    if (node instanceof LeafNode) {
      return node.tab === tab ? node : null;
    }
    return (
      this._findLeafInNode(node.first, tab) ||
      this._findLeafInNode(node.second, tab)
    );
  }

  /**
   * @returns {LeafNode[]}
   */
  allLeaves() {
    let result = [];
    this._collectLeaves(this.root, result);
    return result;
  }

  _collectLeaves(node, result) {
    if (!node) {
      return;
    }
    if (node instanceof LeafNode) {
      result.push(node);
      return;
    }
    this._collectLeaves(node.first, result);
    this._collectLeaves(node.second, result);
  }

  /**
   * Check if a panel is large enough to split further.
   *
   * @param {LeafNode} leafNode
   * @param {"horizontal"|"vertical"} orientation
   * @returns {boolean}
   */
  canSplit(leafNode, orientation) {
    const minW = Math.floor(this._window.screen.availWidth / 4);
    const minH = Math.floor(this._window.screen.availHeight / 4);
    let panel = this._document.getElementById(leafNode.panelId);
    if (!panel) {
      return false;
    }
    const rect = panel.getBoundingClientRect();
    return orientation === "vertical"
      ? rect.width / 2 >= minW
      : rect.height / 2 >= minH;
  }

  /**
   * @param {MozTabbrowserTab} tab
   * @returns {boolean}
   */
  hasTab(tab) {
    return !!this.findLeaf(tab);
  }

  _onTabSelect(aEvent) {
    let tab = aEvent.target;
    let gBrowser = this._window.gBrowser;
    if (!gBrowser) {
      return;
    }

    if (this.hasTab(tab)) {
      this._activatePanels();
      for (let leaf of this.allLeaves()) {
        leaf.tab.linkedBrowser.docShellIsActive = true;
      }
    } else {
      this._deactivatePanels();
      for (let leaf of this.allLeaves()) {
        leaf.tab.linkedBrowser.docShellIsActive =
          gBrowser.shouldActivateDocShell(leaf.tab.linkedBrowser);
      }
    }
  }
}
