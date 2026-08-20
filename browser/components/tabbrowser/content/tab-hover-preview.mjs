/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

var { XPCOMUtils } = ChromeUtils.importESModule(
  "resource://gre/modules/XPCOMUtils.sys.mjs"
);
const lazy = {};
ChromeUtils.defineESModuleGetters(lazy, {
  ContextualIdentityService:
    "moz-src:///toolkit/components/contextualidentity/ContextualIdentityService.sys.mjs",
  PageWireframes:
    "moz-src:///browser/components/sessionstore/PageWireframes.sys.mjs",
  SponsorProtection:
    "moz-src:///browser/components/newtab/SponsorProtection.sys.mjs",
  TabNotes: "moz-src:///browser/components/tabnotes/TabNotes.sys.mjs",
});

// Denotes the amount of time (in ms) that the panel will *not* respect
// ui.tooltip.delay_ms after a tab preview panel is hidden. This is to reduce
// jitter in the event that a user accidentally moves their mouse off the tab
// strip.
const ZERO_DELAY_ACTIVATION_TIME = 300;

// Denotes the amount of time (in ms) that a hover preview panel will remain
// open after the user's mouse leaves its anchor element. This is necessary to
// allow the user to move their mouse between the anchor (tab or group label)
// and the open panel without having it disappear before they get there.
const HOVER_PANEL_STICKY_TIME = 100;

/**
 * Shared module that contains logic for the tab hover preview (THP) and tab
 * group hover preview (TGHP) panels.
 */
export default class TabHoverPanelSet {
  /** @type {Window} */
  #win;

  /** @type {Set<HTMLElement>} */
  #openPopups;

  /** @type {WeakMap<HoverPanel, number>} */
  #deactivateTimers;

  /** @type {HoverPanel|null} */
  #activePanel;

  /**
   * @param {Window} win
   */
  constructor(win) {
    XPCOMUtils.defineLazyPreferenceGetter(
      this,
      "_prefDisableAutohide",
      "ui.popup.disable_autohide",
      false
    );
    XPCOMUtils.defineLazyPreferenceGetter(
      this,
      "_novaEnabled",
      "browser.nova.enabled",
      false
    );

    this.#win = win;
    this.#deactivateTimers = new WeakMap();
    this.#activePanel = null;

    this.panelOpener = new TabPreviewPanelTimedFunction(
      ZERO_DELAY_ACTIVATION_TIME,
      this.#win
    );

    const tabPreviewPanel =
      this.#win.document.getElementById("tab-preview-panel");

    this.tabPanel = new TabPanel(tabPreviewPanel, this);
    this.tabGroupPanel = new TabGroupPanel(
      this.#win.document.getElementById("tabgroup-preview-panel"),
      this
    );
    this.tabNotePanel = new TabNotePanel(
      this.#win.document.getElementById("tab-note-preview-panel"),
      this
    );

    this.#setExternalPopupListeners();
    this.#win.gBrowser.tabContainer.addEventListener("dragstart", event => {
      const target = event.target.closest?.("tab, .tab-group-label");
      if (
        target &&
        (this.#win.gBrowser.isTab(target) ||
          this.#win.gBrowser.isTabGroupLabel(target))
      ) {
        this.deactivate(null, { force: true });
      }
    });
  }

  /**
   * Activate the tab preview or tab group preview, depending on context.
   *
   * If `tabOrGroup` is a tab, the tab preview will be activated. If
   * `tabOrGroup` is a tab group, the group preview will be activated.
   * Activating a panel of one type will automatically deactivate the other
   * type.
   *
   * @param {MozTabbrowserTab|MozTabbrowserTabGroup} tabOrGroup - The tab or group to activate the panel on.
   */
  activate(tabOrGroup) {
    if (!this.shouldActivate()) {
      return;
    }

    if (this.#win.gBrowser.isTab(tabOrGroup)) {
      // Don't activate tab preview if hovering over the note icon
      if (tabOrGroup._noteIconHover) {
        return;
      }
      this.#setActivePanel(this.tabPanel);
      this.tabPanel.activate(tabOrGroup);
    } else if (this.#win.gBrowser.isTabGroup(tabOrGroup)) {
      if (!tabOrGroup.collapsed) {
        return;
      }

      this.#setActivePanel(this.tabGroupPanel);
      this.tabGroupPanel.activate(tabOrGroup);
    } else {
      throw new Error("Received activate call from unknown element");
    }
  }

  /**
   * Deactivate the tab panel, tab group panel, and/or tab note panel.
   *
   * If `tabOrGroup` is a tab, the tab preview and tab note panel will be deactivated.
   * If `tabOrGroup` is a tab group, the group preview will be deactivated.
   * If neither, all panels are deactivated.
   *
   * Panels linger briefly to allow the mouse to travel between the anchor and
   * panel; passing `force` skips that delay.
   *
   * @param {MozTabbrowserTab|MozTabbrowserTabGroup|null} tabOrGroup - The tab or group to activate the panel on.
   * @param {bool} [options.force] - If true, force immediate deactivation of the panels.
   */
  deactivate(tabOrGroup, { force = false } = {}) {
    if (this._prefDisableAutohide) {
      return;
    }

    if (this.#win.gBrowser.isTab(tabOrGroup) || !tabOrGroup) {
      this.tabPanel.deactivate(tabOrGroup, { force });
      this.tabNotePanel.deactivate(tabOrGroup, { force });
    }

    if (this.#win.gBrowser.isTabGroup(tabOrGroup) || !tabOrGroup) {
      this.tabGroupPanel.deactivate({ force });
    }
  }

  activateNotePanel(tab, anchorElement) {
    if (!this.shouldActivate()) {
      return;
    }
    this.#setActivePanel(this.tabNotePanel);
    this.tabNotePanel.activate(tab, anchorElement);
  }

  deactivateNotePanel(tab) {
    if (this._prefDisableAutohide) {
      return;
    }
    this.tabNotePanel.deactivate(tab);
  }

  #setActivePanel(panel) {
    if (this.#activePanel && this.#activePanel != panel) {
      this.requestDeactivate(this.#activePanel, { force: true });
    }

    this.#activePanel = panel;
    this.#clearDeactivateTimer(panel);
  }

  requestDeactivate(panel, { force = false } = {}) {
    this.#clearDeactivateTimer(panel);
    if (force) {
      this.#doDeactivate(panel);
      return;
    }

    const timer = this.#win.setTimeout(() => {
      this.#deactivateTimers.delete(panel);
      if (panel.hoverTargets?.some(t => t.matches(":hover"))) {
        return;
      }
      this.#doDeactivate(panel);
    }, HOVER_PANEL_STICKY_TIME);
    this.#deactivateTimers.set(panel, timer);
  }

  #clearDeactivateTimer(panel) {
    const timer = this.#deactivateTimers.get(panel);
    if (timer) {
      this.#win.clearTimeout(timer);
      this.#deactivateTimers.delete(panel);
    }
  }

  #doDeactivate(panel) {
    // Hiding a popup that has not finished showing cancels the in-flight show,
    // so popupshown never fires. Mark the panel inactive now and complete the
    // hide once the show settles, unless it gets reactivated in the meantime.
    if (panel.panelElement.state == "showing") {
      if (this.#activePanel == panel) {
        this.#activePanel = null;
      }
      panel.panelElement.addEventListener(
        "popupshown",
        () => {
          if (this.#activePanel != panel) {
            this.#doDeactivate(panel);
          }
        },
        { once: true }
      );
      return;
    }

    panel.onBeforeHide();
    panel.panelElement.hidePopup();
    this.panelOpener.clear(panel);
    this.panelOpener.setZeroDelay();

    if (this.#activePanel == panel) {
      this.#activePanel = null;
    }
  }

  forceReset() {
    for (let panel of [this.tabPanel, this.tabGroupPanel, this.tabNotePanel]) {
      this.#clearDeactivateTimer(panel);
      panel.onBeforeHide();
      panel.panelElement.hidePopup();
    }
    // Reset last: TabNotePanel.onBeforeHide re-arms the zero-delay timer, so the
    // opener must be cleared after all panels have been hidden.
    this.panelOpener.reset();
    this.#activePanel = null;
  }

  shouldActivate() {
    return (
      // All other popups are closed.
      !this.#openPopups.size &&
      !this.#win.gBrowser.tabContainer.hasAttribute("movingtab") &&
      // TODO (bug 1899556): for now disable in background windows, as there are
      // issues with windows ordering on Linux (bug 1897475), plus intermittent
      // persistence of previews after session restore (bug 1888148).
      this.#win == Services.focus.activeWindow
    );
  }

  /**
   * Listen for any panels or menupopups that open or close anywhere else in the DOM tree
   * and maintain a list of the ones that are currently open.
   * This is used to disable tab previews until such time as the other panels are closed.
   */
  #setExternalPopupListeners() {
    // Since the tab preview panel is lazy loaded, there is a possibility that panels could
    // already be open on init. Therefore we need to initialize `#openPopups` with existing panels
    // the first time.

    const initialPopups = this.#win.document.querySelectorAll(
      `panel[panelopen=true]:not(#tab-preview-panel):not(#tabgroup-preview-panel):not(#tab-note-preview-panel),
       panel[animating=true]:not(#tab-preview-panel):not(#tabgroup-preview-panel):not(#tab-note-preview-panel),
       menupopup[open=true]`.trim()
    );
    this.#openPopups = new Set(initialPopups);

    const handleExternalPopupEvent = (eventName, setMethod) => {
      this.#win.addEventListener(eventName, ev => {
        const { target } = ev;
        if (
          target !== this.tabPanel.panelElement &&
          target !== this.tabGroupPanel.panelElement &&
          target !== this.tabNotePanel.panelElement &&
          (target.nodeName == "panel" || target.nodeName == "menupopup")
        ) {
          this.#openPopups[setMethod](target);
        }
      });
    };
    handleExternalPopupEvent("popupshowing", "add");
    handleExternalPopupEvent("popuphiding", "delete");
  }
}

class HoverPanel {
  /**
   * @param {XULPopupElement} panelElement
   * @param {TabHoverPanelSet} panelSet
   */
  constructor(panelElement, panelSet) {
    this.panelElement = panelElement;
    this.panelSet = panelSet;
    this.win = this.panelElement.documentGlobal;
  }

  get isActive() {
    return this.panelElement.state == "open";
  }

  deactivate({ force = false } = {}) {
    this.panelSet.requestDeactivate(this, { force });
  }

  get hoverTargets() {
    return [this.panelElement];
  }

  onBeforeHide() {}
}

class TabPanel extends HoverPanel {
  /** @type {MozTabbrowserTab|null} */
  #tab;

  /** @type {DOMElement|null} */
  #thumbnailElement;

  /** @type {DOMElement|null} */
  #interactiveArea;

  /** @type {DOMElement|null} */
  #addNoteButton;

  constructor(panel, panelSet) {
    super(panel, panelSet);

    XPCOMUtils.defineLazyPreferenceGetter(
      this,
      "_prefDisplayThumbnail",
      "browser.tabs.hoverPreview.showThumbnails",
      false
    );
    XPCOMUtils.defineLazyPreferenceGetter(
      this,
      "_prefCollectWireframes",
      "browser.history.collectWireframes"
    );
    XPCOMUtils.defineLazyPreferenceGetter(
      this,
      "_prefUseTabNotes",
      "browser.tabs.notes.enabled",
      false
    );

    this.#tab = null;
    this.#thumbnailElement = null;
    this.#interactiveArea = this.panelElement.querySelector(
      ".tab-preview-content-interactive"
    );

    // #tabPreviewPanelTemplate is currently just the .tab-preview-add-note
    // button element, and it needs to be added the tab preview panel body manually.
    /** @type {HTMLTemplateElement} */
    const tabPreviewTemplate = this.win.document.getElementById(
      "tabPreviewPanelTemplate"
    );
    const importedFragment = this.win.document.importNode(
      tabPreviewTemplate.content,
      true
    );
    this.#addNoteButton = importedFragment.firstElementChild;

    this.#addNoteButton.addEventListener("click", () =>
      this.#openTabNotePanel()
    );
  }

  /**
   * @param {Event} e
   */
  async handleEvent(e) {
    switch (e.type) {
      case "popupshowing":
        await this.#updatePreview();
        this.mouseoutTarget?.addEventListener("mouseout", this);
        break;
      case "TabAttrModified":
        this.#updatePreview(e.target);
        break;
      case "TabSelect":
        this.deactivate(null, { force: true });
        break;
      case "mouseout":
        // Ignore mouseouts from descendant elements of the target -- we only want to know
        // when the user has moused out from the target itself
        if (
          this.mouseoutTarget &&
          !this.mouseoutTarget.contains(e.relatedTarget)
        ) {
          this.deactivate();
        }
        break;
    }
  }

  activate(tab) {
    if (this.#tab === tab && this.panelElement.state == "open") {
      return;
    }
    let originalTab = this.#tab;
    this.#tab = tab;

    // Calling `moveToAnchor` in advance of the call to `openPopup` ensures
    // that race conditions can be avoided in cases where the user hovers
    // over a different tab while the preview panel is still opening.
    // This will ensure the move operation is carried out even if the popup is
    // in an intermediary state (opening but not fully open).
    //
    // If the popup is closed this call will be ignored.
    this.#movePanel();

    originalTab?.removeEventListener("TabAttrModified", this);
    this.#tab.addEventListener("TabAttrModified", this);

    this.#thumbnailElement = null;
    this.#maybeRequestThumbnail();
    if (
      this.panelElement.state == "open" ||
      this.panelElement.state == "showing"
    ) {
      // Remove stale listener from previous activation to prevent
      // duplicate popupshown events when moveToAnchor re-triggers
      // the popup lifecycle during the "showing" state.
      this.panelElement.removeEventListener("popupshowing", this);
      this.#updatePreview();
    } else {
      this.panelSet.panelOpener.execute(() => {
        if (!this.panelSet.shouldActivate()) {
          return;
        }
        this.panelElement.openPopup(this.#tab, this.popupOptions);
      }, this);
      this.win.addEventListener("TabSelect", this);
      this.panelElement.addEventListener("popupshowing", this);
    }
  }

  /**
   * @param {MozTabbrowserTab} [leavingTab]
   * @param {object} [options]
   * @param {boolean} [options.force=false]
   */
  deactivate(leavingTab = null, { force = false } = {}) {
    if (!this._prefUseTabNotes) {
      force = true;
    }
    if (leavingTab) {
      if (this.#tab != leavingTab) {
        return;
      }
      this.win.requestAnimationFrame(() => {
        if (this.#tab == leavingTab) {
          this.deactivate(null, { force });
        }
      });
      return;
    }
    super.deactivate({ force });
  }

  onBeforeHide() {
    this.panelElement.removeEventListener("popupshowing", this);
    this.mouseoutTarget?.removeEventListener("mouseout", this);
    this.win.removeEventListener("TabSelect", this);
    this.#tab?.removeEventListener("TabAttrModified", this);
    this.#tab = null;
    this.#thumbnailElement = null;
  }

  get hoverTargets() {
    let targets = [];
    if (this.#interactiveArea.childNodes.length) {
      targets.push(this.#interactiveArea);
    }
    if (this.#tab) {
      targets.push(this.#tab);
    }
    return targets;
  }

  /**
   * If mouseoutTarget is set, the panel will register a mouseout listener on
   * the target element, causing the panel to hide when this element has been
   * moused out.
   */
  get mouseoutTarget() {
    return this.#interactiveArea.childNodes.length
      ? this.#interactiveArea
      : null;
  }

  getPrettyURI(uri) {
    let url = URL.parse(uri);
    if (!url) {
      return uri;
    }

    if (url.protocol == "about:" && url.pathname == "reader") {
      url = URL.parse(url.searchParams.get("url"));
    }

    if (url?.protocol === "about:") {
      return url.href;
    }
    return url ? url.hostname.replace(/^w{3}\./, "") : uri;
  }

  #hasValidWireframeState(tab) {
    return (
      this._prefCollectWireframes &&
      this._prefDisplayThumbnail &&
      tab &&
      !tab.selected &&
      !!lazy.PageWireframes.getWireframeState(tab)
    );
  }

  #hasValidThumbnailState(tab) {
    return (
      this._prefDisplayThumbnail &&
      tab &&
      tab.linkedBrowser &&
      !tab.getAttribute("pending") &&
      !tab.selected
    );
  }

  #maybeRequestThumbnail() {
    let tab = this.#tab;

    if (!this.#hasValidThumbnailState(tab)) {
      let wireframeElement = lazy.PageWireframes.getWireframeElementForTab(tab);
      if (wireframeElement) {
        this.#thumbnailElement = wireframeElement;
        this.#updatePreview();
      }
      return;
    }
    let thumbnailCanvas = this.win.document.createElement("canvas");
    thumbnailCanvas.width = 280 * this.win.devicePixelRatio;
    thumbnailCanvas.height = 140 * this.win.devicePixelRatio;

    this.win.PageThumbs.captureTabPreviewThumbnail(
      tab.linkedBrowser,
      thumbnailCanvas
    )
      .then(captured => {
        // in case we've changed tabs after capture started, ensure we still want to show the thumbnail
        if (captured && this.#tab == tab && this.#hasValidThumbnailState(tab)) {
          this.#thumbnailElement = thumbnailCanvas;
          this.#updatePreview();
        }
      })
      .catch(e => {
        // Most likely the window was killed before capture completed, so just log the error
        console.error(e);
      });
  }

  get #displayTitle() {
    if (!this.#tab) {
      return "";
    }
    return this.#tab.textLabel.textContent;
  }

  get #displayURI() {
    if (!this.#tab || !this.#tab.linkedBrowser) {
      return "";
    }
    return this.getPrettyURI(this.#tab.linkedBrowser.currentURI.spec);
  }

  get #displayPids() {
    const pids = this.win.gBrowser.getTabPids(this.#tab);
    if (!pids.length) {
      return "";
    }

    let pidLabel = pids.length > 1 ? "pids" : "pid";
    return `${pidLabel}: ${pids.join(", ")}`;
  }

  get #displayActiveness() {
    return this.#tab?.linkedBrowser?.docShellIsActive ? "[A]" : "";
  }

  get #displaySponsorProtection() {
    return lazy.SponsorProtection.debugEnabled &&
      lazy.SponsorProtection.isProtectedBrowser(this.#tab?.linkedBrowser)
      ? "[S]"
      : "";
  }

  #updateContainerIndicator() {
    const indicator = this.panelElement.querySelector(
      ".tab-preview-container-indicator"
    );

    for (let className of [...indicator.classList]) {
      if (
        className.startsWith("identity-color-") ||
        className.startsWith("identity-icon-")
      ) {
        indicator.classList.remove(className);
      }
    }

    const userContextId = this.#tab?.userContextId;
    const identity = userContextId
      ? lazy.ContextualIdentityService.getPublicIdentityFromId(userContextId)
      : null;
    if (!identity) {
      indicator.hidden = true;
      return;
    }

    if (identity.color) {
      indicator.classList.add(`identity-color-${identity.color}`);
    }
    if (identity.icon) {
      indicator.classList.add(`identity-icon-${identity.icon}`);
    }
    indicator.querySelector(".tab-preview-container-label").textContent =
      lazy.ContextualIdentityService.getUserContextLabel(userContextId);
    indicator.hidden = false;
  }

  /**
   * Opens the tab note menu in the context of the current tab. Since only
   * one panel should be open at a time, this also closes the tab hover preview
   * panel.
   */
  #openTabNotePanel() {
    Services.prefs.setBoolPref("browser.tabs.notes.newBadge.enabled", false);
    this.win.gBrowser.tabNoteMenu.openPanel(this.#tab, {
      telemetrySource: lazy.TabNotes.TELEMETRY_SOURCE.TAB_HOVER_PREVIEW_PANEL,
    });
    this.deactivate(this.#tab, { force: true });
  }

  async #updatePreview(tab = null) {
    if (tab) {
      this.#tab = tab;
    }

    this.panelElement.querySelector(".tab-preview-title").textContent =
      this.#displayTitle;
    this.panelElement.querySelector(".tab-preview-uri").textContent =
      this.#displayURI;

    this.#updateContainerIndicator();

    if (this.win.gBrowser.showPidAndActiveness) {
      this.panelElement.querySelector(".tab-preview-pid").textContent =
        this.#displayPids;
      this.panelElement.querySelector(".tab-preview-activeness").textContent =
        this.#displayActiveness + this.#displaySponsorProtection;
    } else {
      this.panelElement.querySelector(".tab-preview-pid").textContent = "";
      this.panelElement.querySelector(".tab-preview-activeness").textContent =
        "";
    }

    if (this._prefUseTabNotes && lazy.TabNotes.isEligible(this.#tab)) {
      let note = await lazy.TabNotes.get(this.#tab);

      if (note) {
        this.#addNoteButton.remove();
      } else {
        this.#interactiveArea.append(this.#addNoteButton);

        let showNewBadge = Services.prefs.getBoolPref(
          "browser.tabs.notes.newBadge.enabled",
          true
        );
        this.#addNoteButton
          .querySelector("moz-badge")
          .toggleAttribute("hidden", !showNewBadge);
      }
    } else {
      this.#addNoteButton.remove();
    }

    let thumbnailContainer = this.panelElement.querySelector(
      ".tab-preview-thumbnail-container"
    );
    thumbnailContainer.classList.toggle(
      "hide-thumbnail",
      !this.#hasValidThumbnailState(this.#tab) &&
        !this.#hasValidWireframeState(this.#tab)
    );
    if (thumbnailContainer.firstChild != this.#thumbnailElement) {
      thumbnailContainer.replaceChildren();
      if (this.#thumbnailElement) {
        thumbnailContainer.appendChild(this.#thumbnailElement);
      }
      this.panelElement.dispatchEvent(
        new CustomEvent("TabPreviewThumbnailUpdated", {
          detail: {
            thumbnail: this.#thumbnailElement,
          },
        })
      );
    }

    this.#movePanel();

    this.panelElement.dispatchEvent(
      new CustomEvent("TabPreviewUpdated", { bubbles: true })
    );
  }

  #movePanel() {
    if (this.#tab) {
      this.panelElement.moveToAnchor(
        this.#tab,
        this.popupOptions.position,
        this.popupOptions.x,
        this.popupOptions.y
      );
    }
  }

  get popupOptions() {
    let tabContainer = this.win.gBrowser.tabContainer;
    // Popup anchors to the bottom edge of the tab in horizontal tabs mode
    if (!tabContainer.verticalMode) {
      return {
        position: "bottomleft topleft",
        x: 0,
        y: 0,
      };
    }

    let sidebarAtStart = this.win.SidebarController._positionStart;

    // Popup anchors to the end edge of the tab in vertical mode
    let positionFromAnchor = sidebarAtStart ? "topright" : "topleft";
    let positionFromPanel = sidebarAtStart ? "topleft" : "topright";
    let positionX = 0;
    let positionY = 3;

    // Popup anchors to the corner of tabs in the vertical pinned grid
    if (tabContainer.isContainerVerticalPinnedGrid(this.#tab)) {
      positionFromAnchor = sidebarAtStart ? "bottomright" : "bottomleft";
      positionX = sidebarAtStart ? -6 : 6;
      positionY = -10;
    }

    return {
      position: `${positionFromAnchor} ${positionFromPanel}`,
      x: positionX,
      y: positionY,
    };
  }
}

class TabGroupPanel extends HoverPanel {
  /** @type {MozTabbrowserTabGroup|null} */
  #group;

  static PANEL_UPDATE_EVENTS = [
    "TabAttrModified",
    "TabClose",
    "TabGrouped",
    "TabMove",
    "TabOpen",
    "TabSelect",
    "TabUngrouped",
  ];

  constructor(panel, panelSet) {
    super(panel, panelSet);

    this.panelContent = panel.querySelector("#tabgroup-panel-content");
    this.#group = null;
  }

  activate(group) {
    if (this.#group && this.#group != group) {
      this.#removeGroupListeners();
    }

    this.#group = group;
    this.#movePanel();
    this.#updatePanelContent();
    Glean.tabgroup.groupInteractions.hover_preview.add();

    if (this.panelElement.state == "closed") {
      this.panelSet.panelOpener.execute(() => {
        if (!this.panelSet.shouldActivate() || !this.#group.collapsed) {
          return;
        }
        this.#doOpenPanel();
      }, this);
    } else {
      this.#addGroupListeners();
    }
  }

  /**
   * Move keyboard focus into the group preview panel.
   *
   * @param {-1|1} [dir] Whether to focus the beginning or end of the list.
   */
  focusPanel(dir = 1) {
    let childIndex = dir > 0 ? 0 : this.panelContent.children.length - 1;
    this.panelContent.children[childIndex].focus();
  }

  #doOpenPanel() {
    this.panelElement.addEventListener("mouseout", this);
    this.panelElement.addEventListener("command", this);

    this.#addGroupListeners();

    this.panelElement.openPopup(this.#popupTarget, this.popupOptions);
  }

  #updatePanelContent() {
    const fragment = this.win.document.createDocumentFragment();
    for (let tab of this.#group.tabs) {
      let tabbutton = this.win.document.createXULElement("toolbarbutton");
      tabbutton.setAttribute("role", "button");
      tabbutton.setAttribute("keyNav", false);
      tabbutton.setAttribute("tabindex", 0);
      tabbutton.setAttribute("label", tab.label);
      if (tab.linkedBrowser) {
        tabbutton.setAttribute(
          "image",
          "page-icon:" + tab.linkedBrowser.currentURI.spec
        );
      }
      tabbutton.setAttribute("tooltiptext", tab.label);
      tabbutton.classList.add(
        "subviewbutton",
        "subviewbutton-iconic",
        "group-preview-button"
      );
      if (tab == this.win.gBrowser.selectedTab) {
        tabbutton.classList.add("active-tab");
      }
      tabbutton.tab = tab;
      fragment.appendChild(tabbutton);
    }
    this.panelContent.replaceChildren(fragment);

    this.panelElement.dispatchEvent(
      new CustomEvent("TabGroupPreviewUpdated", { bubbles: true })
    );
  }

  handleEvent(event) {
    if (event.type == "command") {
      if (this.win.gBrowser.selectedTab == event.target.tab) {
        this.deactivate({ force: true });
        return;
      }

      // bug1984732: temporarily disable CSS transitions while tabs are
      // switching to prevent an unsightly "slide" animation when switching
      // tabs within a collapsed group
      let switchingTabs = [this.win.gBrowser.selectedTab, event.target.tab];
      if (switchingTabs.every(tab => tab.group == this.#group)) {
        for (let tab of switchingTabs) {
          tab.animationsEnabled = false;
        }

        this.win.addEventListener(
          "TabSwitchDone",
          () => {
            this.win.requestAnimationFrame(() => {
              for (let tab of switchingTabs) {
                tab.animationsEnabled = true;
              }
            });
          },
          { once: true }
        );
      }

      this.win.gBrowser.selectedTab = event.target.tab;
      this.deactivate({ force: true });
    } else if (
      event.type == "mouseout" &&
      this.hoverTargets.every(target => !target.contains(event.relatedTarget))
    ) {
      this.deactivate();
    } else if (TabGroupPanel.PANEL_UPDATE_EVENTS.includes(event.type)) {
      this.#updatePanelContent();
    }
  }

  onBeforeHide() {
    this.panelElement.removeEventListener("mouseout", this);
    this.panelElement.removeEventListener("command", this);

    this.#removeGroupListeners();
  }

  get hoverTargets() {
    let targets = [this.panelElement];
    if (this.#popupTarget) {
      targets.push(this.#popupTarget);
    }
    return targets;
  }

  get popupOptions() {
    // With Nova enabled, offset the panel by the border-radius (16px).

    const nova = this.panelSet._novaEnabled;

    if (this.win.gBrowser.tabContainer.verticalMode) {
      return {
        position: this.win.SidebarController._positionStart
          ? "topright topleft"
          : "topleft topright",
        x: 0,
        y: nova ? -16 : -5,
      };
    }

    if (!nova) {
      return { position: "bottomleft topleft", x: 0, y: 0 };
    }

    const rtl = this.win.RTL_UI;
    return {
      position: rtl ? "bottomright topright" : "bottomleft topleft",
      x: rtl ? 16 : -16,
      y: 0,
    };
  }

  get #popupTarget() {
    return this.#group?.labelContainerElement;
  }

  #addGroupListeners() {
    if (!this.#group) {
      return;
    }
    this.#group.hoverPreviewPanelActive = true;
    for (let event of TabGroupPanel.PANEL_UPDATE_EVENTS) {
      this.#group.addEventListener(event, this);
    }
  }

  #removeGroupListeners() {
    if (!this.#group) {
      return;
    }
    this.#group.hoverPreviewPanelActive = false;
    for (let event of TabGroupPanel.PANEL_UPDATE_EVENTS) {
      this.#group.removeEventListener(event, this);
    }
  }

  #movePanel() {
    if (!this.#popupTarget) {
      return;
    }
    this.panelElement.moveToAnchor(
      this.#popupTarget,
      this.popupOptions.position,
      this.popupOptions.x,
      this.popupOptions.y
    );
  }
}

class TabNotePanel extends HoverPanel {
  /** @type {MozTabbrowserTab|null} */
  #tab;

  /** @type {HTMLElement|null} */
  #anchorElement;

  constructor(panel, panelSet) {
    super(panel, panelSet);

    this.#tab = null;
    this.#anchorElement = null;

    this.panelElement
      .querySelector(".tab-note-preview-expand")
      .addEventListener("click", () => (this.#noteExpanded = true));

    // Edit icon is created in JS to avoid eagerly loading the image at
    // startup (see browser_startup_images.js).
    const actionsContainer = this.panelElement.querySelector(
      ".tab-note-preview-actions"
    );
    const editIcon = this.win.document.createElement("img");
    editIcon.className = "tab-note-preview-edit-icon";
    editIcon.src = "chrome://global/skin/icons/edit-outline.svg";
    editIcon.setAttribute("role", "button");
    editIcon.dataset.l10nId = "tab-note-preview-edit-icon";
    editIcon.addEventListener("click", () => this.#openTabNotePanel());
    actionsContainer.appendChild(editIcon);

    this.panelElement
      .querySelector(".tab-note-preview-text")
      .addEventListener("dblclick", () => this.#openTabNotePanel());
  }

  handleEvent(e) {
    switch (e.type) {
      case "popupshowing":
        this.panelElement.addEventListener("mouseout", this);
        this.#updatePanelContent();
        break;
      case "TabAttrModified":
        this.#updatePanelContent(e.target);
        break;
      case "TabSelect":
        this.deactivate(null, { force: true });
        break;
      case "mouseout":
        if (!this.panelElement.contains(e.relatedTarget)) {
          this.deactivate();
        }
        break;
    }
  }

  activate(tab, anchorElement) {
    if (this.#tab === tab && this.panelElement.state == "open") {
      return;
    }
    let originalTab = this.#tab;
    this.#tab = tab;
    this.#anchorElement = anchorElement;

    this.#movePanel();
    this.#noteExpanded = false;

    originalTab?.removeEventListener("TabAttrModified", this);
    this.#tab.addEventListener("TabAttrModified", this);

    if (
      this.panelElement.state == "open" ||
      this.panelElement.state == "showing"
    ) {
      this.#updatePanelContent();
    } else {
      this.panelSet.panelOpener.execute(() => {
        if (!this.panelSet.shouldActivate()) {
          return;
        }
        this.panelElement.openPopup(this.#anchorElement, this.popupOptions);
      }, this);
      this.win.addEventListener("TabSelect", this);
      this.panelElement.addEventListener("popupshowing", this);
    }
  }

  /**
   * @param {MozTabbrowserTab} [leavingTab]
   * @param {object} [options]
   * @param {boolean} [options.force=false]
   */
  deactivate(leavingTab = null, { force = false } = {}) {
    if (leavingTab) {
      if (this.#tab != leavingTab) {
        return;
      }
      this.win.requestAnimationFrame(() => {
        if (this.#tab == leavingTab) {
          this.deactivate(null, { force });
        }
      });
      return;
    }
    super.deactivate({ force });
  }

  onBeforeHide() {
    this.panelElement.removeEventListener("popupshowing", this);
    this.panelElement.removeEventListener("mouseout", this);
    this.win.removeEventListener("TabSelect", this);
    this.#tab?.removeEventListener("TabAttrModified", this);
    this.#tab = null;
    this.#anchorElement = null;
    this.panelSet.panelOpener.setZeroDelay();
  }

  async #updatePanelContent(tab = null) {
    if (tab) {
      this.#tab = tab;
    }

    let currentTab = this.#tab;
    let currentUrl = currentTab?.canonicalUrl;

    const noteTextContainer = this.panelElement.querySelector(
      ".tab-note-preview-text"
    );
    const note = await lazy.TabNotes.get(currentTab);

    // In case the tab navigated after the note fetch started, ensure we
    // still want to show this note
    if (this.#tab != currentTab || this.#tab?.canonicalUrl != currentUrl) {
      return;
    }

    noteTextContainer.textContent = note?.text || "";

    this.#noteOverflow =
      noteTextContainer.scrollHeight > noteTextContainer.clientHeight;

    let actionsContainer = this.panelElement.querySelector(
      ".tab-note-preview-actions"
    );
    noteTextContainer.style.setProperty(
      "--tab-note-expand-toggle-width",
      `${actionsContainer.offsetWidth}px`
    );

    this.#movePanel();

    this.panelElement.dispatchEvent(
      new CustomEvent("TabNotePreviewUpdated", { bubbles: true })
    );
  }

  #movePanel() {
    if (this.#anchorElement) {
      this.panelElement.moveToAnchor(
        this.#anchorElement,
        this.popupOptions.position,
        this.popupOptions.x,
        this.popupOptions.y
      );
    }
  }

  set #noteExpanded(val) {
    this.panelElement.toggleAttribute("note-expanded", val);
    if (val && this.#tab) {
      this.#tab.dispatchEvent(
        new CustomEvent("TabNote:Expand", { bubbles: true })
      );
    }
  }

  set #noteOverflow(val) {
    this.panelElement.toggleAttribute("note-overflow", val);
  }

  #openTabNotePanel() {
    this.win.gBrowser.tabNoteMenu.openPanel(this.#tab, {
      telemetrySource: lazy.TabNotes.TELEMETRY_SOURCE.TAB_NOTE_PREVIEW_PANEL,
    });
    this.deactivate(null, { force: true });
  }

  get popupOptions() {
    return {
      position: "bottomleft topleft",
      x: 0,
      y: -2,
    };
  }
}

/**
 * A wrapper that allows for delayed function execution, but with the
 * ability to "zero" (i.e. cancel) the delay for a predetermined period
 */
class TabPreviewPanelTimedFunction {
  /** @type {number} */
  #zeroDelayTime;

  /** @type {Window} */
  #win;

  /** @type {number | null} */
  #timer;

  /** @type {number | null} */
  #useZeroDelay;

  /** @type {function(): void | null} */
  #target;

  /** @type {TabPanel} */
  #from;

  constructor(zeroDelayTime, win) {
    XPCOMUtils.defineLazyPreferenceGetter(
      this,
      "_prefPreviewDelay",
      "ui.tooltip.delay_ms"
    );

    this.#zeroDelayTime = zeroDelayTime;
    this.#win = win;

    this.#timer = null;
    this.#useZeroDelay = false;

    this.#target = null;
    this.#from = null;
  }

  /**
   * Execute a function after a delay, according to the following rules:
   * - By default, execute the function after the time specified by `ui.tooltip.delay_ms`.
   * - If a timer is already active, the timer will not be restarted, but the
   *   function to be executed will be set to the one from the most recent
   *   call (see notes below)
   * - If the zero delay has been set with `setZeroDelay`, the function will
   *   invoke immediately
   *
   * Multiple calls to `execute` within the delay will not invoke the function
   * each time. The original delay will be preserved (i.e. the function will
   * execute after `ui.tooltip.delay_ms` from the first call) but the function
   * that is executed may be updated by subsequent calls to execute. This
   * ensures that if the panel we want to open changes (e.g. if a user hovers
   * over a tab, then quickly switches to a tab group before the delay
   * expires), the delay is not restarted, which would cause a longer than
   * usual time to open.
   *
   * @param {function(): void | null} target
   *   The function to execute
   * @param {TabPanel} from
   *   The calling panel
   */
  execute(target, from) {
    this.#target = target;
    this.#from = from;

    if (this.delayActive) {
      return;
    }

    // Always setting a timer, even in the situation where the
    // delay is zero, seems to prevent a class of race conditions
    // where multiple tabs are hovered in quick succession
    this.#timer = this.#win.setTimeout(
      () => {
        this.#timer = null;
        this.#target();
      },
      this.#useZeroDelay ? 0 : this._prefPreviewDelay
    );
  }

  /**
   * Clear the timer, if it is active, for example when a user moves off a panel.
   * This has the effect of suppressing the delayed function execution.
   *
   * @param {TabPanel} from
   *   The calling panel. This must be the same as the panel that most recently
   *   called `execute`. If it is not, the call will be ignored. This is
   *   necessary to prevent, e.g., the tab hover panel from inadvertently
   *   cancelling the opening of the tab group hover panel in cases where the
   *   user quickly hovers between tabs and tab groups before the panel fully
   *   opens.
   */
  clear(from) {
    if (from == this.#from && this.#timer) {
      this.#win.clearTimeout(this.#timer);
      this.#timer = null;
      this.#from = null;
    }
  }

  /**
   * Temporarily suppress the delay mechanism.
   *
   * The delay will automatically reactivate after a set interval, which is
   * configured by the constructor.
   */
  setZeroDelay() {
    if (this.#useZeroDelay) {
      this.#win.clearTimeout(this.#useZeroDelay);
    }

    this.#useZeroDelay = this.#win.setTimeout(() => {
      this.#useZeroDelay = null;
    }, this.#zeroDelayTime);
  }

  get delayActive() {
    return this.#timer !== null;
  }

  get zeroDelayActive() {
    return !!this.#useZeroDelay;
  }

  reset() {
    if (this.#timer) {
      this.#win.clearTimeout(this.#timer);
      this.#timer = null;
    }
    if (this.#useZeroDelay) {
      this.#win.clearTimeout(this.#useZeroDelay);
      this.#useZeroDelay = null;
    }
    this.#target = null;
    this.#from = null;
  }
}
