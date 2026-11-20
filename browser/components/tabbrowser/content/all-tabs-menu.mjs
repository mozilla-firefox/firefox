/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/* global FirefoxViewHandler, gBrowser, gURLBar, isElementVisible, PanelUI */

const { XPCOMUtils } = ChromeUtils.importESModule(
  "resource://gre/modules/XPCOMUtils.sys.mjs"
);
const lazy = XPCOMUtils.declareLazy({
  BrowserUsageTelemetry: "resource:///modules/BrowserUsageTelemetry.sys.mjs",
  ContextualIdentityService:
    "moz-src:///toolkit/components/contextualidentity/ContextualIdentityService.sys.mjs",
  GroupsPanel: "moz-src:///browser/components/tabbrowser/GroupsList.sys.mjs",
  PanelMultiView:
    "moz-src:///browser/components/customizableui/PanelMultiView.sys.mjs",
  PrivateBrowsingUtils: "resource://gre/modules/PrivateBrowsingUtils.sys.mjs",
  TabsPanel: "moz-src:///browser/components/tabbrowser/TabsList.sys.mjs",
  UrlbarShared: "chrome://browser/content/urlbar/UrlbarShared.mjs",
});

export const gTabsPanel = {
  kElements: {
    allTabsButton: "alltabs-button",
    allTabsView: "allTabsMenu-allTabsView",
    allTabsViewTabs: "allTabsMenu-allTabsView-tabs",
    dropIndicator: "allTabsMenu-dropIndicator",
    containerTabsView: "allTabsMenu-containerTabsView",
    hiddenTabsButton: "allTabsMenu-hiddenTabsButton",
    hiddenTabsView: "allTabsMenu-hiddenTabsView",
    hiddenTabsViewTabs: "allTabsMenu-hiddenTabsView-tabs",
    hiddenAudioTabs: "allTabsMenu-allTabsView-hiddenAudio-tabs",
    groupsView: "allTabsMenu-groupsView",
    groupsSubView: "allTabsMenu-groupsSubView",
  },
  _initialized: false,
  _initializedElements: false,

  initElements() {
    if (this._initializedElements) {
      return;
    }
    let template = /** @type {HTMLTemplateElement} */ (
      document.getElementById("allTabsMenu-container")
    );
    template.replaceWith(template.content);

    for (let [key, id] of Object.entries(this.kElements)) {
      this[key] = document.getElementById(id);
    }
    this._initializedElements = true;
  },

  hasHiddenTabsExcludingFxView() {
    // Exclude Firefox View, see Bug 1880138.
    return gBrowser.tabs.some(
      tab => tab.hidden && tab != FirefoxViewHandler.tab
    );
  },

  init() {
    if (this._initialized) {
      return;
    }

    this.initElements();

    this.hiddenAudioTabsPopup = new lazy.TabsPanel({
      view: this.allTabsView,
      containerNode: this.hiddenAudioTabs,
      filterFn: tab => tab.soundPlaying || tab.muted,
      onlyHiddenTabs: true,
    });
    this.allTabsPanel = new lazy.TabsPanel({
      view: this.allTabsView,
      containerNode: this.allTabsViewTabs,
      filterFn: tab => !tab.hidden,
      dropIndicator: this.dropIndicator,
      onlyHiddenTabs: false,
    });
    this.groupsPanel = new lazy.GroupsPanel({
      view: this.allTabsView,
      containerNode: this.groupsView,
    });
    this.showAllGroupsPanel = new lazy.GroupsPanel({
      view: this.groupsSubView,
      containerNode: document.getElementById("allTabsMenu-groupsSubView-body"),
      showAll: true,
    });

    this.allTabsView.addEventListener("ViewShowing", () => {
      PanelUI._ensureShortcutsShown(this.allTabsView);

      let containersEnabled =
        Services.prefs.getBoolPref("privacy.userContext.enabled") &&
        !lazy.PrivateBrowsingUtils.isWindowPrivate(window);
      /** @type {XULElement} */ (
        document.getElementById("allTabsMenu-containerTabsButton")
      ).hidden = !containersEnabled;

      const hasHiddenTabs = this.hasHiddenTabsExcludingFxView();
      const hiddenTabsButton = /** @type {XULElement} */ (
        document.getElementById("allTabsMenu-hiddenTabsButton")
      );
      const hiddenTabsSeparator = /** @type {XULElement} */ (
        document.getElementById("allTabsMenu-hiddenTabsSeparator")
      );
      hiddenTabsButton.hidden = !hasHiddenTabs;

      // Hidden tabs normally sit at the bottom of the list, but are bumped to
      // the top if any of them are playing audio.
      const hasHiddenAudioTabs = this.hiddenAudioTabs.hasChildNodes();
      this.hiddenAudioTabs.hidden = !hasHiddenAudioTabs;
      hiddenTabsSeparator.hidden = !hasHiddenAudioTabs;
      if (hasHiddenAudioTabs) {
        this.allTabsViewTabs.prepend(
          hiddenTabsButton,
          this.hiddenAudioTabs,
          hiddenTabsSeparator
        );
      } else {
        this.allTabsViewTabs.append(
          hiddenTabsButton,
          this.hiddenAudioTabs,
          hiddenTabsSeparator
        );
      }

      let closeDuplicateTabsItem = /** @type {XULElement} */ (
        document.getElementById("allTabsMenu-closeDuplicateTabs")
      );
      closeDuplicateTabsItem.hidden =
        !gBrowser.getAllDuplicateTabsToClose().length;
    });

    this.allTabsView.addEventListener("ViewShown", () =>
      this.allTabsView
        .querySelector(".all-tabs-item[selected]")
        ?.scrollIntoView({ block: "center" })
    );

    this.allTabsView.addEventListener("command", event => {
      let { target } = event;
      switch (target.id) {
        case "allTabsMenu-searchTabs":
          Glean.browserUiInteraction.listAllTabsAction.search_tabs.add(1);
          this.searchTabs();
          break;
        case "allTabsMenu-closeDuplicateTabs":
          Glean.browserUiInteraction.listAllTabsAction.close_all_duplicates.add(
            1
          );
          gBrowser.removeAllDuplicateTabs();
          break;
        case "allTabsMenu-containerTabsButton":
          PanelUI.showSubView(this.kElements.containerTabsView, target);
          break;
        case "allTabsMenu-hiddenTabsButton":
          PanelUI.showSubView(this.kElements.hiddenTabsView, target);
          break;
        case "allTabsMenu-viewAllTabs":
          FirefoxViewHandler.openTab("opentabs");
          break;
        case "allTabsMenu-groupsViewShowMore":
          PanelUI.showSubView(this.kElements.groupsSubView, target);
          break;
      }
    });

    let containerTabsMenuSeparator =
      this.containerTabsView.querySelector("toolbarseparator");
    this.containerTabsView.addEventListener("ViewShowing", e => {
      let elements = [];
      let frag = document.createDocumentFragment();

      lazy.ContextualIdentityService.getPublicIdentities().forEach(identity => {
        let menuitem = document.createXULElement("toolbarbutton");
        menuitem.setAttribute("class", "subviewbutton subviewbutton-iconic");
        menuitem.setAttribute(
          "label",
          lazy.ContextualIdentityService.getUserContextLabel(
            identity.userContextId
          )
        );
        // The styles depend on this.
        menuitem.setAttribute("usercontextid", identity.userContextId);
        // The command handler depends on this.
        menuitem.setAttribute("data-usercontextid", identity.userContextId);
        menuitem.classList.add("identity-icon-" + identity.icon);
        menuitem.classList.add("identity-color-" + identity.color);

        menuitem.setAttribute("command", "Browser:NewUserContextTab");
        menuitem.setAttribute("data-container-entrypoint", "all_tabs_menu");

        frag.appendChild(menuitem);
        elements.push(menuitem);
      });

      e.target.addEventListener(
        "ViewHiding",
        () => {
          for (let element of elements) {
            element.remove();
          }
        },
        { once: true }
      );
      containerTabsMenuSeparator.parentNode.insertBefore(
        frag,
        containerTabsMenuSeparator
      );
    });

    this.hiddenTabsPopup = new lazy.TabsPanel({
      view: this.hiddenTabsView,
      containerNode: this.hiddenTabsViewTabs,
      filterFn: tab => tab != FirefoxViewHandler.tab,
      onlyHiddenTabs: true,
    });

    this._initialized = true;
  },

  get canOpen() {
    this.initElements();
    return isElementVisible(this.allTabsButton);
  },

  showAllTabsPanel(event, entrypoint = "unknown") {
    // Note that event may be null.

    // Only space and enter should open the popup, ignore other keypresses:
    if (event?.type == "keypress" && event.key != "Enter" && event.key != " ") {
      return;
    }
    this.init();
    if (this.canOpen) {
      Glean.browserUiInteraction.allTabsPanelEntrypoint[entrypoint].add(1);
      lazy.BrowserUsageTelemetry.recordInteractionEvent(
        entrypoint,
        "all-tabs-panel-entrypoint"
      );
      PanelUI.showSubView(
        this.kElements.allTabsView,
        this.allTabsButton,
        event
      );
    }
  },

  hideAllTabsPanel() {
    let panel = this.allTabsView?.closest("panel");
    if (panel) {
      lazy.PanelMultiView.hidePopup(panel);
    }
  },

  showHiddenTabsPanel(event, entrypoint = "unknown") {
    this.init();
    if (!this.canOpen) {
      return;
    }
    this.allTabsView.addEventListener(
      "ViewShown",
      () => {
        PanelUI.showSubView(
          this.kElements.hiddenTabsView,
          this.hiddenTabsButton
        );
      },
      { once: true }
    );
    this.showAllTabsPanel(event, entrypoint);
  },

  searchTabs() {
    gURLBar.search(lazy.UrlbarShared.RESTRICT_TOKENS.OPENPAGE, {
      searchModeEntry: "tabmenu",
    });
  },
};
