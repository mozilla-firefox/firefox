/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

import {
  classMap,
  html,
  ifDefined,
  nothing,
  repeat,
  when,
} from "chrome://global/content/vendor/lit.all.mjs";
import { MozLitElement } from "chrome://global/content/lit-utils.mjs";

// eslint-disable-next-line import-x/no-unassigned-import
import "chrome://browser/content/sidebar/sidebar-opentabs-preview.mjs";

// eslint-disable-next-line import-x/no-unassigned-import
import "chrome://browser/content/sidebar/sidebar-pins-promo.mjs";

const lazy = {};
ChromeUtils.defineESModuleGetters(lazy, {
  ASRouter: "resource:///modules/asrouter/ASRouter.sys.mjs",
  ShortcutUtils: "resource://gre/modules/ShortcutUtils.sys.mjs",
  GenAI: "moz-src:///browser/components/genai/GenAI.sys.mjs",
});

/**
 * Sidebar with expanded and collapsed states that provides entry points
 * to various sidebar panels and sidebar extensions.
 */
export default class SidebarMain extends MozLitElement {
  static properties = {
    bottomActions: { type: Array },
    expanded: { type: Boolean, reflect: true },
    selectedView: { type: String },
    sidebarItems: { type: Array },
    open: { type: Boolean },
    shouldShowOverflowButton: { type: Boolean },
    isOverflowMenuOpen: { type: Boolean },
  };

  static queries = {
    allButtons: { all: "moz-button" },
    extensionButtons: { all: ".tools-and-extensions > moz-button[extension]" },
    toolButtons: {
      all: ".tools-and-extensions > moz-button[view]:not([extension])",
    },
    customizeButton: ".bottom-actions > moz-button[view=viewCustomizeSidebar]",
    buttonGroup: ".actions-list:not(.bottom-actions):not(.overflow-button)",
    moreToolsButton: ".more-tools-button",
    buttonsWrapper: ".buttons-wrapper",
  };

  get fluentStrings() {
    if (!this._fluentStrings) {
      this._fluentStrings = new Localization(
        ["browser/sidebar.ftl", "preview/genai.ftl", "preview/widgets.ftl"],
        true
      );
    }
    return this._fluentStrings;
  }

  /**
   * If `true`: On the next render, update the active launcher button to the
   * one that matches the open panel.
   */
  #shouldUpdateActiveButton = false;

  constructor() {
    super();
    this.bottomActions = [];
    this.selectedView = window.SidebarController.currentID;
    this.open = window.SidebarController.isOpen;
    this.contextMenuTarget = null;
    this.expanded = false;
    this.clickCounts = {
      genai: 0,
      totalToolsMinusGenai: 0,
    };
    this.shouldShowOverflowButton = false;
    this.overflowMenuOpen = false;
  }

  tooltips = {
    viewHistorySidebar: {
      shortcutId: "key_gotoHistory",
      openl10nId: "sidebar-menu-open-history-tooltip",
      close10nId: "sidebar-menu-close-history-tooltip",
    },
    viewBookmarksSidebar: {
      shortcutId: "viewBookmarksSidebarKb",
      openl10nId: "sidebar-menu-open-bookmarks-tooltip",
      close10nId: "sidebar-menu-close-bookmarks-tooltip",
    },
    viewGenaiChatSidebar: {
      shortcutId: "viewGenaiChatSidebarKb",
      openl10nId: "sidebar-menu-open-ai-chatbot-tooltip-generic",
      close10nId: "sidebar-menu-close-ai-chatbot-tooltip-generic",
      openProviderl10nId: "sidebar-menu-open-ai-chatbot-provider-tooltip",
      closeProviderl10nId: "sidebar-menu-close-ai-chatbot-provider-tooltip",
    },
  };

  connectedCallback() {
    super.connectedCallback();
    this._sidebarBox = document.getElementById("sidebar-box");
    this._sidebarContainer = document.getElementById("sidebar-container");
    this._contextMenu = document.getElementById("sidebar-context-menu");
    this._toolsOverflowMenu = document.getElementById("sidebar-tools-overflow");
    this._openTabsPreview = document.querySelector("sidebar-opentabs-preview");
    this._toolsOverflowButtonGroup =
      this._toolsOverflowMenu.querySelector("button-group");
    this._manageExtensionMenuItem = document.getElementById(
      "sidebar-context-menu-manage-extension"
    );
    this._removeExtensionMenuItem = document.getElementById(
      "sidebar-context-menu-remove-extension"
    );
    this._reportExtensionMenuItem = document.getElementById(
      "sidebar-context-menu-report-extension"
    );
    this._unpinExtensionMenuItem = document.getElementById(
      "sidebar-context-menu-unpin-extension"
    );
    this._hideSidebarMenuItem = document.getElementById(
      "sidebar-context-menu-hide-sidebar"
    );
    this._enableVerticalTabsMenuItem = document.getElementById(
      "sidebar-context-menu-enable-vertical-tabs"
    );
    this._customizeSidebarMenuItem = document.getElementById(
      "sidebar-context-menu-customize-sidebar"
    );
    this._menuseparator = this._contextMenu.querySelector("menuseparator");

    this._sidebarBox.addEventListener("sidebar-show", this);
    this._sidebarBox.addEventListener("sidebar-hide", this);
    this._sidebarContainer.addEventListener("contextmenu", this);
    this._contextMenu.addEventListener("popuphidden", this);
    this._contextMenu.addEventListener("command", this);
    this._toolsOverflowMenu.addEventListener("popupshown", this);
    this._toolsOverflowMenu.addEventListener("popuphidden", this);

    window.addEventListener("SidebarItemAdded", this);
    window.addEventListener("SidebarItemChanged", this);
    window.addEventListener("SidebarItemRemoved", this);

    this.setCustomize();
    this.createTabsObservers();
    this.createSplitter();
    this.createToolsObservers();
  }

  disconnectedCallback() {
    super.disconnectedCallback();
    this._sidebarBox.removeEventListener("sidebar-show", this);
    this._sidebarBox.removeEventListener("sidebar-hide", this);
    this._sidebarContainer.removeEventListener("contextmenu", this);
    this._contextMenu.removeEventListener("popuphidden", this);
    this._contextMenu.removeEventListener("command", this);
    this._toolsOverflowMenu.removeEventListener("popupshown", this);
    this._toolsOverflowMenu.removeEventListener("popuphidden", this);

    window.removeEventListener("SidebarItemAdded", this);
    window.removeEventListener("SidebarItemChanged", this);
    window.removeEventListener("SidebarItemRemoved", this);

    this._tabsOverflowObserver?.disconnect();
    this._toolsIntersectionObserver?.disconnect();
    this._toolsResizeObserver?.disconnect();
    this.ownerDocument
      .getElementById("drag-to-pin-promo-card")
      ?.disconnectedCallback();
  }

  get isToolsDragging() {
    return this.toolsSplitter.getAttribute("state") === "dragging";
  }

  createTabsObservers() {
    let tabs = document.getElementById("tabbrowser-tabs");
    let syncOverflow = () =>
      this.toggleAttribute("tabs-overflow", tabs.hasAttribute("overflow"));
    this._tabsOverflowObserver = new MutationObserver(syncOverflow);
    this._tabsOverflowObserver.observe(tabs, {
      attributes: true,
      attributeFilter: ["overflow"],
    });
    syncOverflow();
  }

  createToolsObservers() {
    this._toolsIntersectionObserver = new IntersectionObserver(
      entries => {
        // In horizontal tabs mode while the launcher is collapsed or hidden, every
        // button is considered non-intersecting by the intersection observer which
        // can cause visibility issues once the sidebar is shown again. We should
        // return early here if horizontal tabs are enabled to prevent this.
        if (!window.SidebarController.sidebarVerticalTabsEnabled) {
          return;
        }
        this.shouldShowOverflowButton = entries.some(
          entry =>
            !entry.isIntersecting &&
            window.SidebarController.sidebarVerticalTabsEnabled
        );
        let panelButtonGroup = document.getElementById("tools-overflow-list");
        for (const entry of entries) {
          let view = entry.target.getAttribute("view");
          let copyButton;
          copyButton = panelButtonGroup.querySelector(`[view='${view}']`);
          let buttonAlreadyAddedToOverflow = !!copyButton;
          if (!entry.isIntersecting && !buttonAlreadyAddedToOverflow) {
            // We can't move the original tools/extension buttons into the overflow panel
            // because Lit will lose the original references to them. We instead create copies of
            // these buttons to add to the overflow panel
            let newCopyButton = this.createCopyButton(view);
            if (!newCopyButton) {
              continue;
            }
            // The Customize button stays last in the panel.
            const customizeCopy = panelButtonGroup.querySelector(
              '[view="viewCustomizeSidebar"]'
            );
            if (customizeCopy && view !== "viewCustomizeSidebar") {
              panelButtonGroup.insertBefore(newCopyButton, customizeCopy);
            } else {
              panelButtonGroup.appendChild(newCopyButton);
            }

            // Hide original button
            entry.target.style.visibility = "hidden";
            if (entry.target === this.buttonGroup.activeChild) {
              // We're hiding the button that's currently active.
              // Select a new one to ensure the group stays keyboard navigable.
              for (const button of this.buttonGroup.children) {
                const style = window.getComputedStyle(button);
                if (style.display !== "none" && style.visibility !== "hidden") {
                  this.buttonGroup.activeChild = button;
                  break;
                }
              }
            }
          } else if (entry.isIntersecting && buttonAlreadyAddedToOverflow) {
            // Remove copy button from the panel
            copyButton.remove();

            // Show original button
            entry.target.style.visibility = "visible";
          }
        }
      },
      {
        root: this.buttonGroup,
        rootMargin: "0px",
        threshold: 0.5,
      }
    );

    this._toolsResizeObserver = new ResizeObserver(() => {
      if (this.isToolsDragging) {
        window.SidebarController._state.toolsDragActive = true;
      }
    });

    this._toolsDropHandler = () =>
      (window.SidebarController._state.toolsDragActive = false);
    this.toolsSplitter.addEventListener("command", this._toolsDropHandler);
  }

  createSplitter() {
    let toolsSplitter = document.createXULElement("splitter");
    toolsSplitter.setAttribute("orient", "vertical");
    toolsSplitter.setAttribute("id", "sidebar-tools-and-extensions-splitter");
    toolsSplitter.setAttribute("resizebefore", "none");
    toolsSplitter.setAttribute("resizeafter", "sibling");
    this.toolsSplitter = toolsSplitter;
  }

  createCopyButton(view) {
    let newButtonAction;
    if (view === "viewCustomizeSidebar") {
      newButtonAction = this.bottomActions[0];
    } else {
      newButtonAction = this.getToolsAndExtensions().get(view);
    }
    if (!newButtonAction) {
      // We can't make a button without an action.
      // This can happen if an extension or other tool was just removed
      return null;
    }
    let newButtonValues = this.getEntrypointValues(newButtonAction);
    let newButton = document.createElement("moz-button");
    if (newButtonValues) {
      newButton.classList.add("expanded-button");
      newButton.setAttribute("role", "presentation");
      newButton.setAttribute("buttonrole", "tab");
      newButton.setAttribute("view", newButtonValues.action.view);
      newButton.setAttribute("aria-label", newButtonValues.actionLabel);
      newButton.setAttribute("aria-selected", newButtonValues.isActiveView);
      newButton.setAttribute(
        "type",
        newButtonValues.isActiveView ? "icon" : "icon ghost"
      );
      newButton.addEventListener("click", async () => {
        await this.showView(newButtonValues.action.view);
        this.resetPanelButtonValues();
        const isActiveView =
          this.open && newButtonValues.action.view === this.selectedView;
        newButton.setAttribute("aria-selected", isActiveView);
        newButton.setAttribute("type", isActiveView ? "icon" : "icon ghost");
        this._toolsOverflowMenu.hidePopup();
      });
      newButton.setAttribute("title", newButtonValues.tooltip);
      newButton.iconSrc = newButtonValues.action.iconUrl;
      newButton.textContent = newButtonValues.actionLabel;
      if (newButtonValues.action.view?.includes("-sidebar-action")) {
        newButton.setAttribute("extension", "");
      }
      if (newButtonValues.action.extensionId) {
        newButton.setAttribute(
          "extensionId",
          newButtonValues.action.extensionId
        );
      }
    }
    return newButton;
  }

  resetPanelButtonValues() {
    let panelButtonGroup = document.getElementById("tools-overflow-list");
    for (const panelButton of Array.from(panelButtonGroup.children)) {
      // Reset aria-selected and button type for all panel buttons.
      const isActiveView =
        this.open && panelButton.getAttribute("view") === this.selectedView;
      panelButton.setAttribute("aria-selected", isActiveView);
      panelButton.setAttribute("type", isActiveView ? "icon" : "icon ghost");
    }
  }

  async onSidebarPopupShowing(event) {
    // Store the context menu target which holds the id required for managing sidebar items
    let targetHost = event.explicitOriginalTarget.getRootNode().host;
    let toolbarContextMenuTarget =
      event.explicitOriginalTarget.flattenedTreeParentNode
        .flattenedTreeParentNode;
    let isToolbarTarget = false;
    if (["moz-button", "sidebar-main"].includes(targetHost?.localName)) {
      this.contextMenuTarget = targetHost;
    } else if (
      document
        .getElementById("vertical-tabs")
        .contains(toolbarContextMenuTarget)
    ) {
      this.contextMenuTarget = toolbarContextMenuTarget;
      isToolbarTarget = true;
    }
    if (
      this.contextMenuTarget?.localName === "sidebar-main" &&
      !window.SidebarController.sidebarVerticalTabsEnabled
    ) {
      this.updateSidebarContextMenuItems();
      return;
    }
    if (
      this.contextMenuTarget?.getAttribute("extensionId") ||
      this.contextMenuTarget?.className.includes("tab") ||
      isToolbarTarget
    ) {
      this.updateExtensionContextMenuItems();
      return;
    }

    if (this.contextMenuTarget?.hasAttribute("contextMenu")) {
      this.hideExistingMenuItem();

      const toolId = this.contextMenuTarget.getAttribute("contextMenu");
      await this.buildToolContextMenuItems(event, toolId);

      const items = this._contextMenu.querySelectorAll(
        "[customized-tool='true']"
      );

      if (items?.length) {
        // Since we are dynamically building/customizing the sidebar context menu for tools
        // This ensures that the menu is fully updated before showing.
        this._contextMenu.openPopupAtScreen(event.screenX, event.screenY, true);
        return;
      }
    }

    event.preventDefault();
  }

  async buildToolContextMenuItems(event, toolId) {
    const menu = this._contextMenu;
    // Clear previously added custom menuitems
    menu
      .querySelectorAll("[customized-tool='true']")
      .forEach(node => node.remove());

    const menuBuilders = {
      aichat: async () => {
        if (Services.prefs.getBoolPref("browser.ml.chat.page")) {
          await lazy.GenAI.buildAskChatMenu(this._contextMenu, {
            browser: window.gBrowser.selectedBrowser,
            selectionInfo: null,
            source: "tool",
          });
        }
      },
    };

    const builder = menuBuilders[toolId];
    if (typeof builder === "function") {
      const originalAppendChild = menu.appendChild.bind(menu);

      menu.appendChild = child => {
        child.setAttribute("customized-tool", true);
        return originalAppendChild(child);
      };

      await builder(menu);
      menu.appendChild = originalAppendChild;
    }

    return menu;
  }

  hideToolMenuItems() {
    const customMenuItems = this._contextMenu.querySelectorAll(
      "[customized-tool='true']"
    );
    customMenuItems.forEach(item => (item.hidden = true));
  }

  hideExistingMenuItem() {
    this._customizeSidebarMenuItem.hidden = true;
    this._enableVerticalTabsMenuItem.hidden = true;
    this._hideSidebarMenuItem.hidden = true;
    this._unpinExtensionMenuItem.hidden = true;
    this._manageExtensionMenuItem.hidden = true;
    this._removeExtensionMenuItem.hidden = true;
    this._reportExtensionMenuItem.hidden = true;
    // Prevent the menu separator visible in Window and Linux
    this._menuseparator.hidden = true;
  }

  updateSidebarContextMenuItems() {
    this._menuseparator.hidden = true;
    this._manageExtensionMenuItem.hidden = true;
    this._removeExtensionMenuItem.hidden = true;
    this._reportExtensionMenuItem.hidden = true;
    this._unpinExtensionMenuItem.hidden = true;
    this._customizeSidebarMenuItem.hidden = false;
    this._enableVerticalTabsMenuItem.hidden = false;
    this._hideSidebarMenuItem.hidden = false;
    this.hideToolMenuItems();
  }

  async updateExtensionContextMenuItems() {
    this._customizeSidebarMenuItem.hidden = true;
    this._enableVerticalTabsMenuItem.hidden = true;
    this._hideSidebarMenuItem.hidden = true;
    this._menuseparator.hidden = false;
    this._unpinExtensionMenuItem.hidden = false;
    this._manageExtensionMenuItem.hidden = false;
    this._removeExtensionMenuItem.hidden = false;
    this._reportExtensionMenuItem.hidden = false;
    this.hideToolMenuItems();
    const extensionId = this.contextMenuTarget.getAttribute("extensionId");
    if (!extensionId) {
      return;
    }
    const addon = await window.AddonManager?.getAddonByID(extensionId);
    if (!addon) {
      // Disable all context menus items if the addon doesn't
      // exist anymore from the AddonManager perspective.
      this._manageExtensionMenuItem.disabled = true;
      this._removeExtensionMenuItem.disabled = true;
      this._reportExtensionMenuItem.disabled = true;
    } else {
      this._manageExtensionMenuItem.disabled = false;
      this._removeExtensionMenuItem.disabled = !(
        addon.permissions & AddonManager.PERM_CAN_UNINSTALL
      );
      this._reportExtensionMenuItem.disabled = !window.gAddonAbuseReportEnabled;
    }
  }

  async manageExtension() {
    await window.BrowserAddonUI.manageAddon(
      this.contextMenuTarget.getAttribute("extensionId"),
      "sidebar-context-menu"
    );
  }

  async removeExtension() {
    await window.BrowserAddonUI.removeAddon(
      this.contextMenuTarget.getAttribute("extensionId"),
      "sidebar-context-menu"
    );
  }

  async reportExtension() {
    await window.BrowserAddonUI.reportAddon(
      this.contextMenuTarget.getAttribute("extensionId"),
      "sidebar-context-menu"
    );
  }

  unpinExtension() {
    window.SidebarController.toggleTool(
      this.contextMenuTarget.getAttribute("view")
    );
  }

  getImageUrl(icon, targetURI) {
    if (window.IS_STORYBOOK) {
      return `chrome://global/skin/icons/defaultFavicon.svg`;
    }
    if (!icon) {
      if (targetURI?.startsWith("moz-extension")) {
        return "chrome://mozapps/skin/extensions/extension.svg";
      }
      return `chrome://global/skin/icons/defaultFavicon.svg`;
    }
    // If the icon is not for website (doesn't begin with http), we
    // display it directly. Otherwise we go through the page-icon
    // protocol to try to get a cached version. We don't load
    // favicons directly.
    if (icon.startsWith("http")) {
      return `page-icon:${targetURI}`;
    }
    return icon;
  }

  getToolsAndExtensions() {
    return window.SidebarController.toolsAndExtensions;
  }

  getLauncherActions() {
    const actions = [...this.getToolsAndExtensions().values()];
    if (!window.SidebarController.sidebarVerticalTabsEnabled) {
      return actions;
    }
    const settingsFirst =
      this.expanded && !window.SidebarController._positionStart;
    actions.splice(
      settingsFirst ? 0 : actions.length,
      0,
      ...this.bottomActions
    );
    return actions;
  }

  setCustomize() {
    const view = "viewCustomizeSidebar";
    const customizeSidebar = window.SidebarController.sidebars.get(view);
    this.bottomActions = [
      {
        l10nId: customizeSidebar.revampL10nId,
        iconUrl: customizeSidebar.iconUrl,
        view,
      },
    ];
  }

  async handleEvent(e) {
    switch (e.type) {
      case "command":
        switch (e.target.id) {
          case "sidebar-context-menu-manage-extension":
            await this.manageExtension();
            break;
          case "sidebar-context-menu-report-extension":
            await this.reportExtension();
            break;
          case "sidebar-context-menu-remove-extension":
            await this.removeExtension();
            break;
          case "sidebar-context-menu-unpin-extension":
            this.unpinExtension();
            break;
          case "sidebar-context-menu-hide-sidebar":
            window.SidebarController._maybeAnimate(() => {
              window.SidebarController.hide({ dismissPanel: false });
              window.SidebarController._state.userLauncherVisible = false;
              window.SidebarController.updateToolbarButton();
            });
            break;
          case "sidebar-context-menu-enable-vertical-tabs":
            await window.SidebarController.toggleVerticalTabs();
            break;
          case "sidebar-context-menu-customize-sidebar":
            await window.SidebarController.show("viewCustomizeSidebar");
            break;
        }
        break;
      case "contextmenu":
        if (
          !["tabs-newtab-button", "vertical-tabs-newtab-button"].includes(
            e.target.id
          )
        ) {
          this.onSidebarPopupShowing(e).then(() => {
            // populating the context menu can be async, so dispatch an event when ready
            this.dispatchEvent(new CustomEvent("sidebar-contextmenu-ready"));
          });
        }
        break;
      case "popuphidden":
        if (e.target === this._contextMenu) {
          this.contextMenuTarget = null;
        } else if (e.target === this._toolsOverflowMenu) {
          this.isOverflowMenuOpen = false;
          window.removeEventListener("keydown", this.handleOverflowKeypress);
        }
        break;
      case "popupshown":
        if (e.target === this._toolsOverflowMenu) {
          this.isOverflowMenuOpen = true;
          window.addEventListener("keydown", this.handleOverflowKeypress);
        }
        break;
      case "sidebar-show":
        this.selectedView = e.detail.viewId;
        this.open = true;
        this.#shouldUpdateActiveButton = true;
        break;
      case "sidebar-hide":
        this.open = false;
        break;
      case "SidebarItemAdded":
      case "SidebarItemChanged":
      case "SidebarItemRemoved":
        this.requestUpdate();
        break;
    }
  }

  handleOverflowKeypress = e => {
    if (
      (e.key == "ArrowDown" || e.key == "ArrowUp") &&
      !this._toolsOverflowButtonGroup.matches(":focus-within")
    ) {
      if (e.key == "ArrowUp") {
        this._toolsOverflowButtonGroup.activeChild =
          this._toolsOverflowButtonGroup.walker.lastChild();
      }
      this._toolsOverflowButtonGroup.activeChild.focus();
    }
  };

  async checkShouldShowCalloutSurveys(view) {
    if (view == "viewGenaiChatSidebar") {
      this.clickCounts.genai++;
    } else {
      this.clickCounts.totalToolsMinusGenai++;
    }

    await lazy.ASRouter.waitForInitialized;
    lazy.ASRouter.sendTriggerMessage({
      browser: window.gBrowser.selectedBrowser,
      id: "sidebarToolOpened",
      context: {
        view,
        clickCounts: this.clickCounts,
      },
    });
  }

  /**
   * Focus the active item in the button group.
   *
   * @returns {boolean} `true` if the item was focused successfully.
   */
  focusButtonGroup() {
    const button = this.buttonGroup.activeChild;
    button?.focus();
    return !!button && this.shadowRoot.activeElement === button;
  }

  async showView(view, triggerNode) {
    this._openTabsPreview?.hide();
    const { currentID, toolsAndExtensions } = window.SidebarController;
    let isToolOpening =
      (!currentID || (currentID && currentID !== view)) &&
      toolsAndExtensions.has(view);
    window.SidebarController.recordIconClick(view, this.expanded);
    window.SidebarController.toggle(view, triggerNode);
    if (view === "viewCustomizeSidebar") {
      Glean.sidebarCustomize.iconClick.record();
    }
    if (isToolOpening) {
      await this.checkShouldShowCalloutSurveys(view);
    }
  }

  enabledToolsAndExtensionsCount() {
    let enabledToolsAndExtensionsCount = 0;
    for (const tool of window.SidebarController.toolsAndExtensions.values()) {
      if (!tool.disabled) {
        enabledToolsAndExtensionsCount++;
      }
    }
    return enabledToolsAndExtensionsCount;
  }

  isToolsOverflowing() {
    return this.expanded && window.SidebarController.sidebarVerticalTabsEnabled;
  }

  willUpdate() {
    this._toolsIntersectionObserver?.disconnect();
    this._toolsResizeObserver?.disconnect();
  }

  updated() {
    if (this.#shouldUpdateActiveButton) {
      this.#shouldUpdateActiveButton = false;
      // ButtonGroup falls back to selecting the first element child when
      // activeChild is undefined.
      this.buttonGroup.activeChild = [...this.allButtons].find(
        button => button.getAttribute("view") === this.selectedView
      );
    }

    const isExpandOnHover =
      window.SidebarController.sidebarRevampVisibility === "expand-on-hover";

    if (
      !isExpandOnHover &&
      window.SidebarController.sidebarVerticalTabsEnabled
    ) {
      for (const buttonEl of this.allButtons) {
        if (buttonEl.hasAttribute("view")) {
          this._toolsIntersectionObserver.observe(buttonEl);
        }
      }
      this._toolsResizeObserver.observe(this.buttonGroup);
      return;
    }

    // In expand-on-hover or horizontal tabs mode we don't track tool overflow,
    // so restore any previously overflown/hidden tool buttons and stop
    // observing. In horizontal tabs mode we also clear the overflow panel
    // copies that were populated while in vertical tabs.
    this.shouldShowOverflowButton = isExpandOnHover ? !this.expanded : false;
    for (const buttonEl of this.allButtons) {
      if (buttonEl.style.visibility === "hidden") {
        buttonEl.style.visibility = "visible";
      }
    }
    if (!isExpandOnHover) {
      // The copies only mirror the buttons the observer hid, and nothing is
      // hidden here, so all of them are stale. Matching them against the
      // remaining buttons would keep the copy of a tool that was removed while
      // overflowing.
      document.getElementById("tools-overflow-list").replaceChildren();
    }
    this._toolsIntersectionObserver.disconnect();
    this._toolsResizeObserver.disconnect();
  }

  getEntrypointValues(action) {
    let providerInfo;
    if (action.view === "viewGenaiChatSidebar") {
      providerInfo = lazy.GenAI.currentChatProviderInfo;
      action.iconUrl = providerInfo.iconUrl;
      // Sets the tooltip text for the action based on the chatbot provider's name.
      // This tooltip text is also used to set the action label
      action.tooltiptext = providerInfo.name;
    }

    if (action.disabled || action.hidden) {
      return null;
    }
    const isActiveView = this.open && action.view === this.selectedView;
    let actionLabel = "";
    if (action.tooltiptext) {
      actionLabel = action.tooltiptext;
    } else if (action.l10nId) {
      const messages = this.fluentStrings.formatMessagesSync([action.l10nId]);
      const attributes = messages?.[0]?.attributes;
      actionLabel = attributes?.find(attr => attr.name === "label")?.value;
    }

    let tooltip = actionLabel;
    const tooltipInfo = this.tooltips[action.view];
    if (tooltipInfo) {
      const { shortcutId, openl10nId, close10nId } = tooltipInfo;
      let l10nId = isActiveView ? close10nId : openl10nId;
      let tooltipData = {};

      if (action.view === "viewGenaiChatSidebar") {
        const provider = providerInfo?.name;

        if (provider) {
          tooltipData.provider = provider;
          l10nId = isActiveView
            ? tooltipInfo.closeProviderl10nId
            : tooltipInfo.openProviderl10nId;
        }
      }

      if (shortcutId) {
        const shortcut = lazy.ShortcutUtils.prettifyShortcut(
          document.getElementById(shortcutId)
        );
        tooltipData.shortcut = shortcut;
      }

      tooltip = this.fluentStrings.formatValueSync(l10nId, tooltipData);
    }

    let toolsOverflowing = this.isToolsOverflowing();
    return { action, isActiveView, toolsOverflowing, tooltip, actionLabel };
  }

  onEntrypointHover(e, view) {
    if (view !== "viewOpenTabsSidebar") {
      return;
    }
    if (e.currentTarget.contains(e.relatedTarget)) {
      return;
    }
    this._openTabsPreview?.activate(e.currentTarget);
  }

  onEntrypointHoverEnd(e, view) {
    if (view !== "viewOpenTabsSidebar") {
      return;
    }
    if (e.currentTarget.contains(e.relatedTarget)) {
      return;
    }
    this._openTabsPreview?.deactivate();
  }

  entrypointTemplate(action) {
    let buttonValues = this.getEntrypointValues(action);
    return html`${when(
      buttonValues,
      () => html`
        <moz-button
          role="presentation"
          buttonrole="tab"
          class=${classMap({
            "tools-overflow": buttonValues.toolsOverflowing,
          })}
          type=${buttonValues.isActiveView ? "icon" : "icon ghost"}
          aria-label=${buttonValues.actionLabel}
          aria-selected=${buttonValues.isActiveView}
          view=${buttonValues.action.view}
          @click=${e =>
            this.showView(buttonValues.action.view, e.currentTarget)}
          @mouseover=${e => this.onEntrypointHover(e, buttonValues.action.view)}
          @mouseout=${e =>
            this.onEntrypointHoverEnd(e, buttonValues.action.view)}
          title=${buttonValues.tooltip}
          .iconSrc=${buttonValues.action.iconUrl}
          ?extension=${buttonValues.action.view?.includes("-sidebar-action")}
          extensionId=${ifDefined(buttonValues.action.extensionId)}
          ?attention=${!!action?.attention}
          contextMenu=${action?.contextMenu || nothing}
        >
        </moz-button>
      `
    )}`;
  }

  showOverflowMenu(e) {
    let panel = document.getElementById("sidebar-tools-overflow");
    this.resetPanelButtonValues();
    let isKeyboardEvent = e.detail == 0;
    panel.addEventListener(
      "popupshown",
      () => {
        let group = panel.querySelector("button-group");
        group.activeChild = group.firstElementChild;
        if (isKeyboardEvent) {
          group.activeChild.focus();
        }
      },
      { once: true }
    );

    if (isKeyboardEvent) {
      panel.addEventListener(
        "popuphidden",
        () => {
          this.moreToolsButton.focus();
        },
        { once: true }
      );
    }

    panel.openPopup(
      this.moreToolsButton.shadowRoot.querySelector(".button-background"),
      window.SidebarController._positionStart
        ? "bottomright bottomleft"
        : "bottomleft bottomright"
    );
  }

  shouldUpdate() {
    const container = window.SidebarController.sidebarContainer;
    return container && !container.hidden;
  }

  render() {
    /* Add 1 to tools and extensions count for "Customize sidebar" option */
    let enabledToolsAndExtensionsCount =
      this.enabledToolsAndExtensionsCount() + 1;
    let messages = this.fluentStrings.formatMessagesSync([
      "sidebar-menu-more-tools-label",
    ]);
    const attributes = messages?.[0]?.attributes;
    let moreToolsTooltip = attributes?.find(
      attr => attr.name === "label"
    )?.value;
    return html`
      <link
        rel="stylesheet"
        href="chrome://browser/content/sidebar/sidebar.css"
      />
      <link
        rel="stylesheet"
        href="chrome://browser/content/sidebar/sidebar-main.css"
      />
      <div class="wrapper">
        <slot name="tabstrip"></slot>
        ${when(
          ((enabledToolsAndExtensionsCount > 2 && !this.expanded) ||
            this.expanded) &&
            window.SidebarController.sidebarRevampVisibility !==
              "expand-on-hover" &&
            window.SidebarController.sidebarVerticalTabsEnabled,
          () => html`${this.toolsSplitter}`
        )}
        <div
          class="buttons-wrapper"
          ?overflowing=${this.shouldShowOverflowButton}
        >
          <button-group
            class="tools-and-extensions actions-list"
            orientation=${this.isToolsOverflowing() ? "horizontal" : "vertical"}
            overflowing=${ifDefined(this.shouldShowOverflowButton)}
          >
            ${repeat(
              this.getLauncherActions(),
              action => action.view,
              action => this.entrypointTemplate(action)
            )}
          </button-group>
          ${when(
            !window.SidebarController.sidebarVerticalTabsEnabled,
            () =>
              html` <div class="bottom-actions actions-list">
                ${repeat(
                  this.bottomActions,
                  action => action.view,
                  action => this.entrypointTemplate(action)
                )}
              </div>`
          )}
          <button-group
            class="tools-and-extensions actions-list overflow-button"
            orientation="vertical"
            part="overflow-button"
            ?hidden=${!this.shouldShowOverflowButton}
          >
            <moz-button
              class="more-tools-button"
              type=${this.isOverflowMenuOpen ? "icon" : "icon ghost"}
              aria-pressed=${this.isOverflowMenuOpen}
              @click=${window.SidebarController.sidebarRevampVisibility ===
              "expand-on-hover"
                ? nothing
                : this.showOverflowMenu}
              title=${moreToolsTooltip}
              .iconSrc=${"chrome://global/skin/icons/chevron.svg"}
            >
            </moz-button>
          </button-group>
        </div>
      </div>
    `;
  }
}

customElements.define("sidebar-main", SidebarMain);
