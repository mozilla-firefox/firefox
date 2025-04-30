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
        <label class="tab-group-label" role="button"/>
      </vbox>
      <html:slot/>
      `;

    /** @type {string} */
    #label;

    /** @type {MozTextLabel} */
    #labelElement;

    /** @type {string} */
    #colorCode;

    /** @type {MutationObserver} */
    #tabChangeObserver;

    constructor() {
      super();
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

      if (this._initialized) {
        return;
      }

      this._initialized = true;
      this.saveOnWindowClose = true;

      this.textContent = "";
      this.appendChild(this.constructor.fragment);
      this.initializeAttributeInheritance();

      this.#labelElement = this.querySelector(".tab-group-label");
      // Mirroring MozTabbrowserTab
      this.#labelElement.container = gBrowser.tabContainer;
      this.#labelElement.group = this;

      this.#labelElement.addEventListener("click", this);
      this.#labelElement.addEventListener("contextmenu", e => {
        e.preventDefault();
        gBrowser.tabGroupMenu.openEditModal(this);
        return false;
      });

      this.#updateLabelAriaAttributes();
      this.#updateCollapsedAriaAttributes();

      this.addEventListener("TabSelect", this);
    }

    disconnectedCallback() {
      this.#tabChangeObserver?.disconnect();
    }

    #observeTabChanges() {
      if (!this.#tabChangeObserver) {
        this.#tabChangeObserver = new window.MutationObserver(mutationList => {
          for (let mutation of mutationList) {
            mutation.addedNodes.forEach(node => {
              if (node.tagName === "tab") {
                node.dispatchEvent(
                  new CustomEvent("TabGrouped", {
                    bubbles: true,
                    detail: this,
                  })
                );
                node.setAttribute("aria-level", 2);
              }
            });
            mutation.removedNodes.forEach(node => {
              if (node.tagName === "tab") {
                node.dispatchEvent(
                  new CustomEvent("TabUngrouped", {
                    bubbles: true,
                    detail: this,
                  })
                );
                // Tab could have moved to be ungrouped (level 1)
                // or to a different group (level 2).
                node.setAttribute("aria-level", node.group ? 2 : 1);
                // `posinset` and `setsize` only need to be set explicitly
                // on grouped tabs so that a11y tools can tell users that a
                // given tab is "2 of 7" in the group, for example.
                node.removeAttribute("aria-posinset");
                node.removeAttribute("aria-setsize");
              }
            });
          }
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
            // Renumber tabs so that a11y tools can tell users that a given
            // tab is "2 of 7" in the group, for example.
            let tabs = this.tabs;
            let tabCount = tabs.length;
            tabs.forEach((tab, index) => {
              tab.setAttribute("aria-posinset", index + 1);
              tab.setAttribute("aria-setsize", tabCount);
            });
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
      if (diff) {
        this.dispatchEvent(
          new CustomEvent("TabGroupUpdate", { bubbles: true })
        );
      }
    }

    get id() {
      return this.getAttribute("id");
    }

    set id(val) {
      this.setAttribute("id", val);
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

      this.dataset.tooltip = val;

      this.#updateLabelAriaAttributes();
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
      this.#updateCollapsedAriaAttributes();
      const eventName = val ? "TabGroupCollapse" : "TabGroupExpand";
      this.dispatchEvent(new CustomEvent(eventName, { bubbles: true }));
    }

    #lastAddedTo = 0;
    get lastSeenActive() {
      return Math.max(
        this.#lastAddedTo,
        ...this.tabs.map(t => t.lastSeenActive)
      );
    }

    async #updateLabelAriaAttributes() {
      let tabGroupName = this.#label;
      if (!tabGroupName) {
        tabGroupName = await gBrowser.tabLocalization.formatValue(
          "tab-group-name-default"
        );
      }

      let tabGroupDescription = await gBrowser.tabLocalization.formatValue(
        "tab-group-description",
        {
          tabGroupName,
        }
      );
      this.#labelElement?.setAttribute("aria-label", tabGroupName);
      this.#labelElement?.setAttribute("aria-description", tabGroupDescription);
      this.#labelElement?.setAttribute("aria-level", 1);
    }

    #updateCollapsedAriaAttributes() {
      const ariaExpanded = this.collapsed ? "false" : "true";
      this.#labelElement?.setAttribute("aria-expanded", ariaExpanded);
    }

    get tabs() {
      return Array.from(this.children).filter(node => node.matches("tab"));
    }

    /**
     * @returns {MozTextLabel}
     */
    get labelElement() {
      return this.#labelElement;
    }

    /**
     * add tabs to the group
     *
     * @param {MozTabbrowserTab[]} tabs
     * @param {TabMetricsContext} [metricsContext]
     *   Optional context to record for metrics purposes.
     */
    addTabs(tabs, metricsContext) {
      for (let tab of tabs) {
        let tabToMove =
          this.ownerGlobal === tab.ownerGlobal
            ? tab
            : gBrowser.adoptTab(tab, {
                tabIndex: gBrowser.tabs.at(-1)._tPos + 1,
                selectTab: tab.selected,
              });
        gBrowser.moveTabToGroup(tabToMove, this, metricsContext);
      }
      this.#lastAddedTo = Date.now();
    }

    /**
     * Remove all tabs from the group and delete the group.
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
      for (let i = this.tabs.length - 1; i >= 0; i--) {
        gBrowser.ungroupTab(this.tabs[i]);
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
      if (event.target === this.#labelElement && event.button === 0) {
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

    on_TabSelect() {
      this.collapsed = false;
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
