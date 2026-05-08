/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

"use strict";

// This is loaded into chrome windows with the subscript loader. Wrap in
// a block to prevent accidentally leaking globals onto `window`.
{
  const { TabMetrics } = ChromeUtils.importESModule(
    "moz-src:///browser/components/tabbrowser/TabMetrics.sys.mjs"
  );

  class MozTabbrowserTabGroup extends MozXULElement {
    static markup = `
      <vbox class="tab-group-label-container" pack="center">
        <vbox class="tab-group-label-hover-highlight" pack="center">
          <label class="tab-group-label" role="button" />
        </vbox>
      </vbox>
      <html:slot/>
      <vbox class="tab-group-overflow-count-container" pack="center">
        <label class="tab-group-overflow-count" role="button" />
      </vbox>
      `;

    /** @type {string} */
    #defaultGroupName = "";

    /** @type {string} */
    #label;

    /** @type {MozTextLabel} */
    #labelElement;

    /** @type {MozXULElement} */
    #labelContainerElement;

    /** @type {MozTextLabel} */
    #overflowCountLabel;

    /** @type {MozXULElement} */
    overflowContainer;

    /** @type {string} */
    #colorCode;

    /** @type {MutationObserver} */
    #tabChangeObserver;

    /** @type {boolean} */
    #wasCreatedByAdoption = false;

    #observerRemoved = false;

    constructor() {
      super();

      XPCOMUtils.defineLazyPreferenceGetter(
        this,
        "_showTabGroupHoverPreview",
        "browser.tabs.groups.hoverPreview.enabled",
        false
      );
    }

    static get inheritedAttributes() {
      return {
        ".tab-group-label": "text=label,tooltiptext=data-tooltip",
      };
    }

    connectedCallback() {
      // Always set the mutation observer to listen for tab change events, even
      // if we are already initialized.
      // This is needed to ensure events continue to fire even if the tab group is
      // moved from the horizontal to vertical tab layout or vice-versa, which
      // causes the component to be repositioned in the DOM.
      this.#observeTabChanges();

      // Similar to above, always set up TabSelect listener, as this gets
      // removed in disconnectedCallback
      this.documentGlobal.addEventListener("TabSelect", this);
      this.addEventListener("SplitViewTabChange", this);

      if (this._initialized) {
        return;
      }

      this._initialized = true;
      this.saveOnWindowClose = true;

      this.textContent = "";
      this.appendChild(this.constructor.fragment);
      this.initializeAttributeInheritance();

      Services.obs.addObserver(
        this.resetDefaultGroupName,
        "intl:app-locales-changed"
      );
      this.documentGlobal.addEventListener("unload", this.#removeObserver);

      this.addEventListener("click", this);

      this.#labelElement = this.querySelector(".tab-group-label");
      this.#labelContainerElement = this.querySelector(
        ".tab-group-label-container"
      );
      // Mirroring MozTabbrowserTab
      this.#labelElement.container = gBrowser.tabContainer;
      this.#labelElement.group = this;

      this.#labelContainerElement.addEventListener("mouseover", this);
      this.#labelContainerElement.addEventListener("mouseout", this);
      this.#labelElement.addEventListener("contextmenu", e => {
        e.preventDefault();
        gBrowser.tabGroupMenu.openEditModal(this);
        return false;
      });

      this.#updateLabelAriaAttributes();

      this.overflowContainer = this.querySelector(
        ".tab-group-overflow-count-container"
      );
      this.#overflowCountLabel = this.overflowContainer.querySelector(
        ".tab-group-overflow-count"
      );

      let tabGroupCreateDetail = this.#wasCreatedByAdoption
        ? { isAdoptingGroup: true }
        : {};
      this.dispatchEvent(
        new CustomEvent("TabGroupCreate", {
          bubbles: true,
          detail: tabGroupCreateDetail,
        })
      );
      // Reset `wasCreatedByAdoption` to default of false so that we only
      // claim that a tab group was created by adoption the first time it
      // mounts after getting created by `Tabbrowser.adoptTabGroup`.
      this.#wasCreatedByAdoption = false;
    }

    resetDefaultGroupName = () => {
      this.#defaultGroupName = "";
      this.#updateLabelAriaAttributes();
      this.#updateTooltip();
    };

    #removeObserver = () => {
      if (this.#observerRemoved) {
        return;
      }
      this.#observerRemoved = true;
      Services.obs.removeObserver(
        this.resetDefaultGroupName,
        "intl:app-locales-changed"
      );
    };

    disconnectedCallback() {
      this.documentGlobal.removeEventListener("TabSelect", this);
      this.documentGlobal.removeEventListener("unload", this.#removeObserver);
      this.removeEventListener("SplitViewTabChange", this);
      this.#tabChangeObserver?.disconnect();
      this.#removeObserver();
    }

    appendChild(node) {
      return this.insertBefore(node, this.overflowContainer);
    }

    #observeTabChanges() {
      if (!this.#tabChangeObserver) {
        this.#tabChangeObserver = new window.MutationObserver(mutations => {
          if (!this.tabs.length) {
            this.dispatchEvent(
              new CustomEvent("TabGroupRemoved", { bubbles: true })
            );
            this.remove();
            Services.obs.notifyObservers(
              this,
              "browser-tabgroup-removed-from-dom"
            );
          } else {
            let tabs = this.tabs;
            let tabCount = tabs.length;
            let hasActiveTab = false;
            tabs.forEach((tab, index) => {
              if (tab.selected) {
                hasActiveTab = true;
              }

              // Renumber tabs so that a11y tools can tell users that a given
              // tab is "2 of 7" in the group, for example.
              tab.setAttribute("aria-posinset", index + 1);
              tab.setAttribute("aria-setsize", tabCount);
            });
            this.hasActiveTab = hasActiveTab;
            this.#updateOverflowLabel();
            this.#updateLastTabOrSplitViewAttr();
          }
          for (const mutation of mutations) {
            for (const addedNode of mutation.addedNodes) {
              if (gBrowser.isTab(addedNode)) {
                this.#updateTabAriaHidden(addedNode);
              } else if (gBrowser.isSplitViewWrapper(addedNode)) {
                for (const splitViewTab of addedNode.tabs) {
                  this.#updateTabAriaHidden(splitViewTab);
                }
              }
            }
            for (const removedNode of mutation.removedNodes) {
              if (gBrowser.isTab(removedNode)) {
                this.#updateTabAriaHidden(removedNode);
              } else if (gBrowser.isSplitViewWrapper(removedNode)) {
                for (const splitViewTab of removedNode.tabs) {
                  this.#updateTabAriaHidden(splitViewTab);
                }
              }
            }
          }
        });
      }
      this.#tabChangeObserver.observe(this, { childList: true });
    }

    get color() {
      return this.#colorCode;
    }

    set color(code) {
      let diff = code !== this.#colorCode;
      this.#colorCode = code;
      this.style.setProperty(
        "--tab-group-color",
        `var(--tab-group-color-${code})`
      );
      this.style.setProperty(
        "--tab-group-color-invert",
        `var(--tab-group-color-${code}-invert)`
      );
      this.style.setProperty(
        "--tab-group-color-pale",
        `var(--tab-group-color-${code}-pale)`
      );
      this.style.setProperty(
        "--tab-group-background-color",
        `var(--tab-group-${code})`
      );
      this.style.setProperty(
        "--tab-group-text-color",
        `var(--tab-group-${code}-content)`
      );
      this.style.setProperty(
        "--tab-group-background-color-hover",
        `var(--tab-group-${code}-hover)`
      );
      if (diff) {
        this.dispatchEvent(
          new CustomEvent("TabGroupUpdate", { bubbles: true })
        );
      }
    }

    get defaultGroupName() {
      if (!this.#defaultGroupName) {
        this.#defaultGroupName = gBrowser.tabLocalization.formatValueSync(
          "tab-group-name-default"
        );
      }
      return this.#defaultGroupName;
    }

    get id() {
      return this.getAttribute("id");
    }

    set id(val) {
      this.setAttribute("id", val);
    }

    /**
     * @returns {boolean}
     */
    get hasActiveTab() {
      return this.hasAttribute("hasactivetab");
    }

    /**
     * @param {boolean} val
     */
    set hasActiveTab(val) {
      this.toggleAttribute("hasactivetab", val);
    }

    get label() {
      return this.#label;
    }

    set label(val) {
      let diff = val !== this.#label;
      this.#label = val;

      // If the group name is empty, use a zero width space so we
      // always create a text node and get consistent layout.
      this.setAttribute("label", val || "\u200b");
      this.#updateLabelAriaAttributes();
      this.#updateTooltip();
      if (diff) {
        this.dispatchEvent(
          new CustomEvent("TabGroupUpdate", { bubbles: true })
        );
      }
    }

    // alias for label
    get name() {
      return this.label;
    }

    set name(newName) {
      this.label = newName;
    }

    get collapsed() {
      return this.hasAttribute("collapsed");
    }

    set collapsed(val) {
      if (!!val == this.collapsed) {
        return;
      }
      if (val) {
        for (let tab of this.tabs) {
          // Unlock tab sizes.
          tab.style.maxWidth = "";
        }
      }
      this.toggleAttribute("collapsed", val);
      this.#updateLabelAriaAttributes();
      this.#updateTooltip();
      this.#updateOverflowLabel();
      for (const tab of this.tabs) {
        this.#updateTabAriaHidden(tab);
      }
      gBrowser.tabContainer.previewPanel?.deactivate(this, { force: true });
      const eventName = val ? "TabGroupCollapse" : "TabGroupExpand";
      this.dispatchEvent(new CustomEvent(eventName, { bubbles: true }));

      let pendingAnimationPromises = this.tabs.flatMap(tab =>
        tab
          .getAnimations()
          .filter(anim =>
            ["min-width", "max-width"].includes(anim.transitionProperty)
          )
          .map(anim => anim.finished)
      );
      Promise.allSettled(pendingAnimationPromises).then(() => {
        this.dispatchEvent(
          new CustomEvent("TabGroupAnimationComplete", { bubbles: true })
        );
      });
    }

    #lastAddedTo = 0;
    get lastSeenActive() {
      return Math.max(
        this.#lastAddedTo,
        ...this.tabs.map(t => t.lastSeenActive)
      );
    }

    async #updateLabelAriaAttributes() {
      let tabGroupName = this.#label || this.defaultGroupName;

      this.#labelElement?.setAttribute("aria-label", tabGroupName);
      this.#labelElement?.setAttribute("aria-level", 1);

      let tabGroupDescriptionL10nID;
      if (this.collapsed) {
        this.#labelElement?.setAttribute("aria-haspopup", "menu");
        this.#labelElement?.setAttribute("aria-expanded", "false");
        tabGroupDescriptionL10nID = this.hasAttribute("previewpanelactive")
          ? "tab-group-preview-open-description"
          : "tab-group-preview-closed-description";
      } else {
        this.#labelElement?.removeAttribute("aria-haspopup");
        this.#labelElement?.setAttribute("aria-expanded", "true");
        tabGroupDescriptionL10nID = "tab-group-description";
      }
      let tabGroupDescription = await gBrowser.tabLocalization.formatValue(
        tabGroupDescriptionL10nID,
        {
          tabGroupName,
        }
      );
      this.#labelElement?.setAttribute("aria-description", tabGroupDescription);
    }

    async #updateTooltip() {
      // Disable the tooltip for collapsed groups when tab group hover preview is enabled
      if (this._showTabGroupHoverPreview && this.collapsed) {
        delete this.dataset.tooltip;
        return;
      }

      let tabGroupName = this.#label || this.defaultGroupName;
      let tooltipKey = this.collapsed
        ? "tab-group-label-tooltip-collapsed"
        : "tab-group-label-tooltip-expanded";
      await gBrowser.tabLocalization
        .formatValue(tooltipKey, {
          tabGroupName,
        })
        .then(result => {
          this.dataset.tooltip = result;
        });
    }

    /**
     * @param {MozTabbrowserTab} tab
     */
    #updateTabAriaHidden(tab) {
      if (tab.splitview) {
        if (
          tab.group?.collapsed &&
          !tab.splitview.tabs.some(splitViewTab => splitViewTab.selected)
        ) {
          tab.splitview.setAttribute("aria-hidden", "true");
        } else {
          tab.splitview.removeAttribute("aria-hidden");
        }
      } else if (tab.group?.collapsed && !tab.selected) {
        tab.setAttribute("aria-hidden", "true");
      } else {
        tab.removeAttribute("aria-hidden");
      }
    }

    #updateOverflowLabel() {
      // When a group containing the active tab is collapsed,
      // the overflow count displays the number of additional tabs
      // in the group adjacent to the active tab.
      if (this.overflowContainer) {
        let overflowCountLabel = this.overflowContainer.querySelector(
          ".tab-group-overflow-count"
        );
        let tabs = this.tabs;
        let tabCount = tabs.length;
        const overflowOffset =
          this.hasActiveTab && gBrowser.selectedTab.splitview ? 2 : 1;

        this.toggleAttribute("hasmultipletabs", tabCount > overflowOffset);

        gBrowser.tabLocalization
          .formatValue("tab-group-overflow-count", {
            tabCount: tabCount - overflowOffset,
          })
          .then(result => (overflowCountLabel.textContent = result));
        gBrowser.tabLocalization
          .formatValue("tab-group-overflow-count-tooltip", {
            tabCount: tabCount - overflowOffset,
          })
          .then(result => {
            overflowCountLabel.setAttribute("tooltiptext", result);
            overflowCountLabel.setAttribute("aria-description", result);
          });
      }
    }

    #updateLastTabOrSplitViewAttr() {
      const LAST_ITEM_ATTRIBUTE = "last-tab-or-split-view";
      let lastTab = this.tabs[this.tabs.length - 1];
      let currentLastTabOrSplitView = lastTab.splitview
        ? lastTab.splitview
        : lastTab;

      let prevLastTabOrSplitView = this.querySelector(
        `[${LAST_ITEM_ATTRIBUTE}]`
      );
      if (prevLastTabOrSplitView !== currentLastTabOrSplitView) {
        prevLastTabOrSplitView?.removeAttribute(LAST_ITEM_ATTRIBUTE);
        currentLastTabOrSplitView.setAttribute(LAST_ITEM_ATTRIBUTE, true);
      }
    }

    /**
     * @returns {MozTabbrowserTab[]}
     */
    get tabs() {
      let childrenArray = Array.from(this.children);
      for (let i = childrenArray.length - 1; i >= 0; i--) {
        if (childrenArray[i].tagName == "tab-split-view-wrapper") {
          childrenArray.splice(i, 1, ...childrenArray[i].tabs);
        }
      }
      return childrenArray.filter(node => node.matches("tab"));
    }

    /**
     * @returns {MozTabbrowserTab|MozTabSplitViewWrapper[]}
     */
    get tabsAndSplitViews() {
      return Array.from(this.children).filter(
        node => node.matches("tab") || node.tagName == "tab-split-view-wrapper"
      );
    }

    /**
     * @param {MozTabbrowserTab} tab
     * @returns {boolean}
     */
    isTabVisibleInGroup(tab) {
      if (this.isBeingDragged) {
        return false;
      }
      if (
        this.collapsed &&
        !tab.selected &&
        !tab.multiselected &&
        !tab.splitview?.hasActiveTab
      ) {
        return false;
      }
      return true;
    }

    /**
     * @returns {MozTextLabel}
     */
    get labelElement() {
      return this.#labelElement;
    }

    /**
     * @returns {MozXULElement}
     */
    get labelContainerElement() {
      return this.#labelContainerElement;
    }

    get overflowCountLabel() {
      return this.#overflowCountLabel;
    }

    /**
     * @param {boolean} value
     */
    set wasCreatedByAdoption(value) {
      this.#wasCreatedByAdoption = value;
    }

    /**
     * @returns {boolean}
     */
    get isBeingDragged() {
      return this.hasAttribute("movingtabgroup");
    }

    /**
     * @param {boolean} val
     */
    set isBeingDragged(val) {
      this.toggleAttribute("movingtabgroup", val);
    }

    /**
     * @returns {boolean}
     */
    get hoverPreviewPanelActive() {
      return this.hasAttribute("previewpanelactive");
    }

    /**
     * @param {boolean} val
     */
    set hoverPreviewPanelActive(val) {
      this.toggleAttribute("previewpanelactive", val);
      this.#updateLabelAriaAttributes();
    }

    /**
     * add tabs to the group
     *
     * @param {MozTabbrowserTab[] | MozSplitViewWrapper} tabsOrSplitViews
     * @param {TabMetricsContext} [metricsContext]
     *   Optional context to record for metrics purposes.
     */
    addTabs(tabsOrSplitViews, metricsContext = null) {
      for (let tabOrSplitView of tabsOrSplitViews) {
        if (gBrowser.isSplitViewWrapper(tabOrSplitView)) {
          let splitViewToMove =
            this.documentGlobal === tabOrSplitView.documentGlobal
              ? tabOrSplitView
              : gBrowser.adoptSplitView(tabOrSplitView, {
                  tabIndex: gBrowser.tabs.at(-1)._tPos + 1,
                });
          gBrowser.moveSplitViewToExistingGroup(
            splitViewToMove,
            this,
            metricsContext
          );
        } else {
          if (tabOrSplitView.pinned) {
            tabOrSplitView.documentGlobal.gBrowser.unpinTab(tabOrSplitView);
          }
          let tabToMove =
            this.documentGlobal === tabOrSplitView.documentGlobal
              ? tabOrSplitView
              : gBrowser.adoptTab(tabOrSplitView, {
                  tabIndex: gBrowser.tabs.at(-1)._tPos + 1,
                  selectTab: tabOrSplitView.selected,
                });
          gBrowser.moveTabToExistingGroup(tabToMove, this, metricsContext);
        }
      }
      this.#lastAddedTo = Date.now();
    }

    /**
     * Remove all tabs from the group and delete the group.
     *
     * @param {TabMetricsContext} [metricsContext]
     */
    ungroupTabs(
      metricsContext = {
        isUserTriggered: false,
        telemetrySource: TabMetrics.METRIC_SOURCE.UNKNOWN,
      }
    ) {
      this.dispatchEvent(
        new CustomEvent("TabGroupUngroup", {
          bubbles: true,
          detail: metricsContext,
        })
      );
      for (let i = this.tabsAndSplitViews.length - 1; i >= 0; i--) {
        if (gBrowser.isSplitViewWrapper(this.tabsAndSplitViews[i])) {
          gBrowser.ungroupSplitView(this.tabsAndSplitViews[i]);
        } else if (gBrowser.isTab(this.tabsAndSplitViews[i])) {
          gBrowser.ungroupTab(this.tabsAndSplitViews[i]);
        }
      }
    }

    /**
     * Save group data to session store.
     *
     * @param {object} [options]
     * @param {boolean} [options.isUserTriggered]
     *   Whether or not the save operation was explicitly called by the user.
     *   Used for telemetry. Default is false.
     */
    save({ isUserTriggered = false } = {}) {
      SessionStore.addSavedTabGroup(this);
      this.dispatchEvent(
        new CustomEvent("TabGroupSaved", {
          bubbles: true,
          detail: { isUserTriggered },
        })
      );
    }

    saveAndClose({ isUserTriggered } = {}) {
      this.save({ isUserTriggered });
      gBrowser.removeTabGroup(this);
    }

    /**
     * @param {PointerEvent} event
     */
    on_click(event) {
      let isToggleElement =
        event.target === this.#labelElement ||
        event.target === this.#overflowCountLabel;
      if (isToggleElement && event.button === 0) {
        event.preventDefault();
        this.collapsed = !this.collapsed;
        gBrowser.tabGroupMenu.close();

        /** @type {GleanCounter} */
        let interactionMetric = this.collapsed
          ? Glean.tabgroup.groupInteractions.collapse
          : Glean.tabgroup.groupInteractions.expand;
        interactionMetric.add(1);
      }
    }

    /**
     * @param {CustomEvent} event
     */
    on_mouseover(event) {
      // Only fire the event if we are entering the tab group label.
      // mouseover also fires events when moving between elements inside the tab group.
      if (!this.#labelContainerElement.contains(event.relatedTarget)) {
        this.#labelElement.dispatchEvent(
          new CustomEvent("TabGroupLabelHoverStart", { bubbles: true })
        );
      }
    }

    /**
     * @param {CustomEvent} event
     */
    on_mouseout(event) {
      // Only fire the event if we are leaving the tab group label.
      // mouseout also fires events when moving between elements inside the tab group.
      if (!this.#labelContainerElement.contains(event.relatedTarget)) {
        this.#labelElement.dispatchEvent(
          new CustomEvent("TabGroupLabelHoverEnd", { bubbles: true })
        );
      }
    }

    /**
     * @param {CustomEvent} event
     */
    on_TabSelect(event) {
      const { previousTab } = event.detail;
      this.hasActiveTab = event.target.group === this;
      if (this.hasActiveTab) {
        this.#updateTabAriaHidden(event.target);
      }
      if (previousTab.group === this) {
        this.#updateTabAriaHidden(previousTab);
      }

      this.#updateOverflowLabel();
    }

    on_SplitViewTabChange(event) {
      for (const splitViewTab of event.target.tabs) {
        this.#updateTabAriaHidden(splitViewTab);
      }

      this.#updateOverflowLabel();
    }

    /**
     * If one of this group's tabs is the selected tab, this will do nothing.
     * Otherwise, it will expand the group if collapsed, and select the first
     * tab in its list.
     */
    select() {
      this.collapsed = false;
      if (gBrowser.selectedTab.group == this) {
        return;
      }
      gBrowser.selectedTab = this.tabs[0];
    }
  }

  customElements.define("tab-group", MozTabbrowserTabGroup);
}
