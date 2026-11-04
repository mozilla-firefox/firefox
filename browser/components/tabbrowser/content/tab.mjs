/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/* global FirefoxViewHandler, gBrowser, gMultiProcessBrowser, gSharedTabWarning,
   isBlankPageURL, MozElements */

const { XPCOMUtils } = ChromeUtils.importESModule(
  "resource://gre/modules/XPCOMUtils.sys.mjs"
);
const lazy = XPCOMUtils.declareLazy({
  BrowserUIUtils: "resource:///modules/BrowserUIUtils.sys.mjs",
  BrowserWindowTracker: "resource:///modules/BrowserWindowTracker.sys.mjs",
  ContextualIdentityService:
    "moz-src:///toolkit/components/contextualidentity/ContextualIdentityService.sys.mjs",
  SessionStore:
    "moz-src:///browser/components/sessionstore/SessionStore.sys.mjs",
  TabMetrics: "moz-src:///browser/components/tabbrowser/TabMetrics.sys.mjs",
  closeTabByDblclick: {
    pref: "browser.tabs.closeTabByDblclick",
    default: false,
  },
});

export class MozTabbrowserTab extends MozElements.MozTab {
  static markup = `
    <stack class="tab-stack" flex="1">
      <vbox class="tab-background">
        <hbox class="tab-context-line"/>
        <hbox class="tab-loading-burst" flex="1"/>
        <hbox class="tab-group-line"/>
      </vbox>
      <hbox class="tab-content" align="center">
        <stack class="tab-icon-stack">
          <hbox class="tab-throbber"/>
          <hbox class="tab-icon-pending"/>
          <html:img class="tab-icon-image" role="presentation" decoding="sync" />
          <image class="tab-sharing-icon-overlay" role="presentation"/>
          <image class="tab-icon-overlay" role="presentation"/>
          <image class="tab-note-icon-overlay" role="presentation"/>
        </stack>
        <html:moz-button type="icon ghost" size="small" class="tab-audio-button" tabindex="-1"></html:moz-button>
        <vbox class="tab-label-container"
              align="start"
              pack="center"
              flex="1">
          <label class="tab-text tab-label" role="presentation"/>
        </vbox>
        <image class="tab-note-icon" role="presentation"/>
        <image class="tab-close-button close-icon" role="button" data-l10n-id="tabbrowser-close-tabs-button" data-l10n-args='{"tabCount": 1}' keyNav="false"/>
      </hbox>
    </stack>
    `;

  constructor() {
    super();

    this.addEventListener("mouseover", this);
    this.addEventListener("mouseout", this);
    this.addEventListener("dragstart", this, true);
    this.addEventListener("dragstart", this);
    this.addEventListener("mousedown", this);
    this.addEventListener("mouseup", this);
    this.addEventListener("click", this);
    this.addEventListener("dblclick", this, true);
    this.addEventListener("animationstart", this);
    this.addEventListener("animationend", this);
    this.addEventListener("focus", this);
    this.addEventListener("AriaFocus", this);

    this._hover = false;
    this._selectedOnFirstMouseDown = false;
    this._noteIconHover = false;

    /**
     * Set by Tabbrowser whenever the tabs are renumbered.
     *
     * @type {number}
     */
    this._index;

    /**
     * Describes how the tab ended up in this mute state. May be any of:
     *
     * - undefined: The tabs mute state has never changed.
     * - null: The mute state was last changed through the UI.
     * - Any string: The ID was changed through an extension API. The string
     * must be the ID of the extension which changed it.
     */
    this.muteReason = undefined;

    /**
     * Whether the tab's label is the title of the page, as opposed to its URL
     * or a placeholder.
     */
    this.labelIsContentTitle = false;

    this.closing = false;

    /**
     * Whether the tab has yet to dispatch TabOpen, or is having another
     * window's tab state restored into it by `swapBrowsersAndCloseOther`.
     * Extension code ignores the tab's events while this is true. The
     * window's first tab never dispatches TabOpen; Tabbrowser clears the flag
     * for it when setting it up.
     */
    this.initializing = true;

    /**
     * The canonical URL of the tab's page: the URL the page declares through
     * `<link rel="canonical">`, `og:url` or JSON-LD, or else the page's own
     * URL. It is detected once the page has loaded, or restored with the
     * session, and is null before that, after the tab navigates, and while
     * canonical URL detection is off.
     *
     * @type {string|null}
     */
    // TODO(bug 2076466): Either tab notes or tabbrowser should fully own this.
    this.canonicalUrl = null;

    /**
     * The tab's browser. Tabbrowser sets it while adding the tab and clears
     * it when the tab is removed.
     *
     * @type {MozBrowser|null}
     */
    this.linkedBrowser = null;
  }

  static get inheritedAttributes() {
    return {
      ".tab-background":
        "selected=visuallyselected,fadein,multiselected,dragover-groupTarget",
      ".tab-group-line": "selected=visuallyselected,multiselected",
      ".tab-loading-burst": "pinned,bursting,notselectedsinceload",
      ".tab-content":
        "pinned,selected=visuallyselected,multiselected,titlechanged,attention",
      ".tab-icon-stack":
        "sharing,pictureinpicture,crashed,busy,soundplaying,soundplaying-scheduledremoval,pinned,muted,blocked,selected=visuallyselected,activemedia-blocked",
      ".tab-throbber": "fadein,pinned,busy,progress,selected=visuallyselected",
      ".tab-icon-pending":
        "fadein,pinned,busy,progress,selected=visuallyselected,pendingicon",
      ".tab-icon-image":
        "src=image,requestcontextid,fadein,pinned,selected=visuallyselected,busy,crashed,sharing,pictureinpicture,pending,discarded",
      ".tab-sharing-icon-overlay": "sharing,selected=visuallyselected,pinned",
      ".tab-icon-overlay":
        "sharing,pictureinpicture,crashed,busy,soundplaying,soundplaying-scheduledremoval,pinned,muted,blocked,selected=visuallyselected,activemedia-blocked",
      ".tab-audio-button":
        "crashed,soundplaying,soundplaying-scheduledremoval,pinned,muted,activemedia-blocked",
      ".tab-label-container": "pinned,selected=visuallyselected,labeldirection",
      ".tab-label":
        "text=label,accesskey,fadein,pinned,selected=visuallyselected,attention",
      ".tab-close-button": "fadein,pinned,selected=visuallyselected",
    };
  }

  #lastGroup;
  connectedCallback() {
    this.#updateOnTabGrouped();
    this.#updateOnTabSplit();
    this.#lastGroup = this.group;

    this.initialize();
  }

  disconnectedCallback() {
    this.#updateOnTabUngrouped();
    this.#updateOnTabUnsplit();
  }

  initialize() {
    if (this._initialized) {
      return;
    }

    this.textContent = "";
    this.appendChild(
      /** @type {typeof MozTabbrowserTab} */ (this.constructor).fragment
    );
    this.initializeAttributeInheritance();
    this.setAttribute("context", "tabContextMenu");
    this._initialized = true;

    if (!Object.hasOwn(this, "_lastAccessed")) {
      this.updateLastAccessed();
    }

    let labelContainer = this.querySelector(".tab-label-container");
    labelContainer.addEventListener("overflow", this);
    labelContainer.addEventListener("underflow", this);

    // Tabs in the tab strip default to being at the top level (level 1)
    // Tabs in tab groups are one level down (level 2); this tab will
    // update its value when it moves in and out of tab groups.
    this.setAttribute("aria-level", "1");
  }

  /**
   * This tab's index in `gBrowser.tabs`, which counts every tab. See
   * `elementIndex` for its position among the visible tab strip elements.
   *
   * @type {number}
   */
  get index() {
    return this._index;
  }

  #elementIndex;
  get elementIndex() {
    if (!this.visible) {
      throw new Error("Tab is not visible, so does not have an elementIndex");
    }
    // Make sure the index is up to date.
    this.container.dragAndDropElements;
    return this.#elementIndex;
  }

  set elementIndex(index) {
    this.#elementIndex = index;
  }

  #owner;
  get owner() {
    let owner = this.#owner?.deref();
    if (owner && !owner.closing) {
      return owner;
    }
    return null;
  }

  set owner(owner) {
    this.#owner = owner ? new WeakRef(owner) : null;
  }

  get container() {
    return gBrowser.tabContainer;
  }

  set attention(val) {
    if (val == this.hasAttribute("attention")) {
      return;
    }

    this.toggleAttribute("attention", val);
    gBrowser._tabAttrModified(this, ["attention"]);
  }

  set _visuallySelected(val) {
    if (val == this.hasAttribute("visuallyselected")) {
      return;
    }

    this.toggleAttribute("visuallyselected", val);
    gBrowser._tabAttrModified(this, ["visuallyselected"]);
  }

  set _selected(val) {
    // in e10s we want to only pseudo-select a tab before its rendering is done, so that
    // the rest of the system knows that the tab is selected, but we don't want to update its
    // visual status to selected until after we receive confirmation that its content has painted.
    this.toggleAttribute("selected", val);

    // If we're non-e10s we need to update the visual selection at the same
    // time, otherwise AsyncTabSwitcher will take care of this.
    if (!gMultiProcessBrowser) {
      this._visuallySelected = val;
    }
  }

  get pinned() {
    return this.hasAttribute("pinned");
  }

  get isOpen() {
    return this.isConnected && !this.closing && this != FirefoxViewHandler.tab;
  }

  get visible() {
    return (
      this.isOpen &&
      !this.hidden &&
      (!this.group || this.group.isTabVisibleInGroup(this))
    );
  }

  get hidden() {
    // This getter makes `hidden` read-only
    return super.hidden;
  }

  get muted() {
    return this.hasAttribute("muted");
  }

  get multiselected() {
    return this.hasAttribute("multiselected");
  }

  get userContextId() {
    return this.hasAttribute("usercontextid")
      ? parseInt(this.getAttribute("usercontextid"))
      : 0;
  }

  /**
   * The permanent key of the tab's browser, or undefined once the tab has
   * been closed.
   *
   * @type {object|undefined}
   */
  get permanentKey() {
    return this.linkedBrowser?.permanentKey;
  }

  get soundPlaying() {
    return this.hasAttribute("soundplaying");
  }

  get pictureinpicture() {
    return this.hasAttribute("pictureinpicture");
  }

  get activeMediaBlocked() {
    return this.hasAttribute("activemedia-blocked");
  }

  get undiscardable() {
    return this.hasAttribute("undiscardable");
  }

  set undiscardable(val) {
    if (val == this.hasAttribute("undiscardable")) {
      return;
    }

    this.toggleAttribute("undiscardable", val);
    gBrowser._tabAttrModified(this, ["undiscardable"]);
  }

  get animationsEnabled() {
    return this.style.transition == "";
  }

  set animationsEnabled(val) {
    this.style.transition = val ? "" : "none";
  }

  get isEmpty() {
    // Determines if a tab is "empty", usually used in the context of determining
    // if it's ok to close the tab.
    if (this.hasAttribute("busy")) {
      return false;
    }

    return this.isEmptyIgnoringLoad;
  }

  // Like isEmpty, but ignoring the load in progress. Only for callers which
  // know that load is being taken away from the tab.
  get isEmptyIgnoringLoad() {
    if (this.hasAttribute("customizemode")) {
      return false;
    }

    let browser = this.linkedBrowser;
    if (!isBlankPageURL(browser.currentURI.spec)) {
      return false;
    }

    if (!lazy.BrowserUIUtils.checkEmptyPageOrigin(browser)) {
      return false;
    }

    if (browser.canGoForward || browser.canGoBack) {
      return false;
    }

    return true;
  }

  get lastAccessed() {
    return this._lastAccessed == Infinity ? Date.now() : this._lastAccessed;
  }

  /**
   * Returns a timestamp which attempts to represent the last time the user saw this tab.
   * If the tab has not been active in this session, any lastAccessed is used. We
   * differentiate between selected and explicitly visible; a selected tab in a hidden
   * window is last seen when that window and tab were last visible.
   * We use the application start time as a fallback value when no other suitable value
   * is available.
   */
  get lastSeenActive() {
    const isForegroundWindow =
      this.documentGlobal ==
      lazy.BrowserWindowTracker.getTopWindow({ allowPopups: true });
    // the timestamp for the selected tab in the active window is always now
    if (isForegroundWindow && this.selected) {
      return Date.now();
    }
    if (this._lastSeenActive) {
      return this._lastSeenActive;
    }

    if (
      !this._lastAccessed ||
      this._lastAccessed >= this.container.startupTime
    ) {
      // When the tab was created this session but hasn't been seen by the user,
      // default to the application start time.
      return this.container.startupTime;
    }
    // The tab was restored from a previous session but never seen.
    // Use the lastAccessed as the best proxy for when the user might have seen it.
    return this._lastAccessed;
  }

  get _overPlayingIcon() {
    return this.overlayIcon?.matches(":hover");
  }

  get _overAudioButton() {
    return this.audioButton?.matches(":hover");
  }

  get overlayIcon() {
    return this.querySelector(".tab-icon-overlay");
  }

  get audioButton() {
    return this.querySelector(".tab-audio-button");
  }

  get throbber() {
    return this.querySelector(".tab-throbber");
  }

  get iconImage() {
    return this.querySelector(".tab-icon-image");
  }

  get sharingIcon() {
    return this.querySelector(".tab-sharing-icon-overlay");
  }

  get textLabel() {
    return this.querySelector(".tab-label");
  }

  get closeButton() {
    return this.querySelector(".tab-close-button");
  }

  get noteIcon() {
    return this.querySelector(".tab-note-icon");
  }

  get noteIconOverlay() {
    return this.querySelector(".tab-note-icon-overlay");
  }

  /** @returns {MozTabbrowserTabGroup|null} */
  get group() {
    return /** @type {MozTabbrowserTabGroup|null} */ (
      this.closest("tab-group")
    );
  }

  /** @returns {MozTabSplitViewWrapper|null} */
  get splitview() {
    if (this.parentElement?.tagName == "tab-split-view-wrapper") {
      return /** @type {MozTabSplitViewWrapper} */ (this.parentElement);
    }
    return null;
  }

  /**
   * @returns {boolean}
   */
  get hasTabNote() {
    return this.hasAttribute("tab-note");
  }

  /**
   * @param {boolean} val
   */
  set hasTabNote(val) {
    this.toggleAttribute("tab-note", val);
  }

  updateLastAccessed(aDate) {
    this._lastAccessed = this.selected ? Infinity : aDate || Date.now();
  }

  updateLastSeenActive() {
    this._lastSeenActive = Date.now();
  }

  updateLastUnloadedByTabUnloader() {
    this._lastUnloaded = Date.now();
    Glean.browserEngagement.tabUnloadCount.add(1);
  }

  recordTimeFromUnloadToReload() {
    if (!this._lastUnloaded) {
      return;
    }

    const diff_in_msec = Date.now() - this._lastUnloaded;
    Glean.browserEngagement.tabUnloadToReload.accumulateSingleSample(
      diff_in_msec / 1000
    );
    Glean.browserEngagement.tabReloadCount.add(1);
    delete this._lastUnloaded;
  }

  on_mouseover(event) {
    if (!this.visible) {
      return;
    }

    let tabToWarm = event.target.classList.contains("tab-close-button")
      ? gBrowser._findTabToBlurTo(this)
      : this;
    gBrowser.warmupTab(tabToWarm);

    if (this.hasTabNote) {
      const noteIcon = this.noteIcon;
      const noteIconOverlay = this.noteIconOverlay;
      const isOverNoteIcon =
        (noteIcon && noteIcon.contains(event.target)) ||
        (noteIconOverlay && noteIconOverlay.contains(event.target));

      if (isOverNoteIcon && !this._noteIconHover) {
        this._noteIconHover = true;
        this.dispatchEvent(
          new CustomEvent("TabNoteIconHoverStart", {
            bubbles: true,
            detail: {
              noteIconElement: noteIcon?.contains(event.target)
                ? noteIcon
                : noteIconOverlay,
            },
          })
        );
      }
    }

    // If the previous target wasn't part of this tab then this is a mouseenter event.
    if (!this.contains(event.relatedTarget)) {
      this._mouseenter();
    }
  }

  on_mouseout(event) {
    if (this._noteIconHover) {
      const noteIcon = this.noteIcon;
      const noteIconOverlay = this.noteIconOverlay;
      const stillOverNoteIcon =
        (noteIcon && noteIcon.contains(event.relatedTarget)) ||
        (noteIconOverlay && noteIconOverlay.contains(event.relatedTarget));

      if (!stillOverNoteIcon) {
        this._noteIconHover = false;
        this.dispatchEvent(
          new CustomEvent("TabNoteIconHoverEnd", {
            bubbles: true,
            detail: { returningToTab: this.contains(event.relatedTarget) },
          })
        );
      }
    }

    // If the new target is not part of this tab then this is a mouseleave event.
    if (!this.contains(event.relatedTarget)) {
      this._mouseleave();
    }
  }

  on_dragstart(event) {
    // We use "failed" drag end events that weren't cancelled by the user
    // to detach tabs. Ensure that we do not show the drag image returning
    // to its point of origin when this happens, as it makes the drag
    // finishing feel very slow.
    event.dataTransfer.mozShowFailAnimation = false;
    if (event.eventPhase == Event.CAPTURING_PHASE) {
      this.style.MozUserFocus = "";
    } else if (
      event.target.classList?.contains("tab-close-button") ||
      gSharedTabWarning.willShowSharedTabWarning(this)
    ) {
      event.stopPropagation();
    }
  }

  on_mousedown(event) {
    let eventMaySelectTab = true;

    if (lazy.closeTabByDblclick && event.button == 0 && event.detail == 1) {
      this._selectedOnFirstMouseDown = this.selected;
    }

    if (this.selected) {
      this.style.MozUserFocus = "ignore";
    } else if (
      event.target.classList.contains("tab-close-button") ||
      event.target.classList.contains("tab-icon-overlay") ||
      event.target.classList.contains("tab-audio-button")
    ) {
      eventMaySelectTab = false;
    }

    if (event.button == 1) {
      gBrowser.warmupTab(gBrowser._findTabToBlurTo(this));
    }

    if (event.button == 0) {
      let shiftKey = event.shiftKey;
      let accelKey = event.getModifierState("Accel");
      if (shiftKey) {
        eventMaySelectTab = false;
        const lastSelectedTab = gBrowser.lastMultiSelectedTab;
        if (!accelKey) {
          gBrowser.selectedTab = lastSelectedTab;

          // Make sure selection is cleared when tab-switch doesn't happen.
          gBrowser.clearMultiSelectedTabs();
        }
        gBrowser.addRangeToMultiSelectedTabs(lastSelectedTab, this);
      } else if (accelKey) {
        // Ctrl (Cmd for mac) key is pressed
        eventMaySelectTab = false;
        if (this.multiselected) {
          gBrowser.removeFromMultiSelectedTabs(this);
        } else if (this != gBrowser.selectedTab) {
          gBrowser.addToMultiSelectedTabs(this);
          gBrowser.lastMultiSelectedTab = this;
        }
      } else if (
        event.altKey &&
        Services.prefs.getBoolPref("browser.tabs.splitView.enabled", false)
      ) {
        eventMaySelectTab = false;
      } else if (!this.selected && this.multiselected) {
        gBrowser.lockClearMultiSelectionOnce();
      }
    }

    if (gSharedTabWarning.willShowSharedTabWarning(this)) {
      eventMaySelectTab = false;
    }

    if (eventMaySelectTab) {
      let prevTab = gBrowser.selectedTab;
      // super.on_mousedown sets gBrowser.selectedTab via the property setter,
      // which calls setSelectedTab(val) without a metricsContext. We detect
      // the change after the fact so we can supply the TAB_STRIP source.
      super.on_mousedown(event);
      if (gBrowser.selectedTab !== prevTab) {
        gBrowser.recordTabMetrics(
          gBrowser.TabMetrics.METRIC_ACTION.ACTIVATE,
          gBrowser.TabMetrics.userTriggeredContext(
            gBrowser.TabMetrics.METRIC_SOURCE.TAB_STRIP
          )
        );
      }
    }
  }

  on_mouseup() {
    // Make sure that clear-selection is released.
    // Otherwise selection using Shift key may be broken.
    gBrowser.unlockClearMultiSelection();

    this.style.MozUserFocus = "";
  }

  on_click(event) {
    if (event.button != 0) {
      return;
    }

    if (event.altKey) {
      if (
        !event.target.classList.contains("tab-close-button") &&
        !event.target.classList.contains("tab-icon-overlay") &&
        !event.target.classList.contains("tab-audio-button") &&
        !this.selected &&
        !gBrowser.selectedTab.hidden &&
        Services.prefs.getBoolPref("browser.tabs.splitView.enabled", false) &&
        !this.splitview &&
        !gBrowser.selectedTab.splitview &&
        !this.pinned &&
        !gBrowser.selectedTab.pinned
      ) {
        gBrowser.addTabSplitView([gBrowser.selectedTab, this], {
          insertBefore: gBrowser.selectedTab,
          trigger: "alt_click",
        });
      }
      return;
    }

    if (event.getModifierState("Accel") || event.shiftKey) {
      return;
    }

    if (
      gBrowser.multiSelectedTabsCount > 0 &&
      !event.target.classList.contains("tab-close-button") &&
      !event.target.classList.contains("tab-icon-overlay") &&
      !event.target.classList.contains("tab-audio-button")
    ) {
      // Tabs were previously multi-selected and user clicks on a tab
      // without holding Ctrl/Cmd Key
      gBrowser.clearMultiSelectedTabs();
    }

    if (
      event.target.classList.contains("tab-icon-overlay") ||
      event.target.classList.contains("tab-audio-button")
    ) {
      if (this.activeMediaBlocked) {
        if (this.multiselected) {
          gBrowser.resumeDelayedMediaOnMultiSelectedTabs();
        } else {
          this.resumeDelayedMedia();
        }
      } else if (this.soundPlaying || this.muted) {
        if (this.multiselected) {
          gBrowser.toggleMuteAudioOnMultiSelectedTabs(this);
        } else {
          this.toggleMuteAudio();
        }
      }
      return;
    }

    if (event.target.classList.contains("tab-close-button")) {
      if (this.multiselected) {
        gBrowser.removeMultiSelectedTabs({
          metricsContext: lazy.TabMetrics.userTriggeredContext(
            lazy.TabMetrics.METRIC_SOURCE.TAB_STRIP
          ),
        });
      } else {
        gBrowser.removeTab(this, {
          animate: true,
          triggeringEvent: event,
          metricsContext: lazy.TabMetrics.userTriggeredContext(
            lazy.TabMetrics.METRIC_SOURCE.TAB_STRIP
          ),
        });
      }
      // This enables double-click protection for the tab container
      // (see tabbrowser-tabs 'click' handler).
      gBrowser.tabContainer._blockDblClick = true;
    }
  }

  on_dblclick(event) {
    if (event.button != 0) {
      return;
    }

    // for the one-close-button case
    if (event.target.classList.contains("tab-close-button")) {
      event.stopPropagation();
    }

    if (
      lazy.closeTabByDblclick &&
      this._selectedOnFirstMouseDown &&
      this.selected &&
      !event.target.classList.contains("tab-icon-overlay")
    ) {
      gBrowser.removeTab(this, {
        animate: true,
        triggeringEvent: event,
        metricsContext: lazy.TabMetrics.userTriggeredContext(
          lazy.TabMetrics.METRIC_SOURCE.TAB_STRIP
        ),
      });
    }
  }

  on_animationstart(event) {
    if (!event.animationName.startsWith("tab-throbber-animation")) {
      return;
    }
    // The animation is on a pseudo-element so we need to use `subtree: true`
    // to get our hands on it.
    for (let animation of event.target.getAnimations({ subtree: true })) {
      if (animation.animationName === event.animationName) {
        // Ensure all tab throbber animations are synchronized by sharing an
        // start time.
        animation.startTime = 0;
      }
    }
  }

  on_animationend(event) {
    if (event.target.classList.contains("tab-loading-burst")) {
      this.removeAttribute("bursting");
    }
  }

  /**
   * Removes the listeners set up by #endHoverUnlessPointerArrives, or null
   * when no hover is waiting to be confirmed by the event state manager.
   *
   * @type {function|null}
   */
  #stopWaitingForPointer = null;

  /**
   * @param {object} [options]
   * @param {boolean} [options.withoutPointerEvent=false]
   *   Set when the tab strip moved this tab under the pointer rather than the
   *   pointer having moved onto the tab. The event state manager doesn't know
   *   the pointer is here and so won't send the mouseout that normally ends
   *   the hover, so end it from the next mouse event instead unless the event
   *   state manager has agreed the pointer is here by then.
   */
  _mouseenter({ withoutPointerEvent = false } = {}) {
    this._hover = true;

    if (this.selected) {
      this.container._handleTabSelect();
    } else if (this.linkedPanel) {
      this.linkedBrowser.unselectedTabHover(true);
    }

    // Prepare connection to host beforehand.
    lazy.SessionStore.speculativeConnectOnTabHover(this);

    this.dispatchEvent(new CustomEvent("TabHoverStart", { bubbles: true }));

    if (withoutPointerEvent) {
      this.#endHoverUnlessPointerArrives();
    }
  }

  #endHoverUnlessPointerArrives() {
    const types = ["mousemove", "mouseover"];
    const onMouseEvent = () => {
      this.#stopWaitingForPointer();
      if (!this.matches(":hover")) {
        this._mouseleave();
      }
    };
    this.#stopWaitingForPointer?.();
    this.#stopWaitingForPointer = () => {
      this.#stopWaitingForPointer = null;
      for (let type of types) {
        window.removeEventListener(type, onMouseEvent, true);
      }
    };
    for (let type of types) {
      window.addEventListener(type, onMouseEvent, true);
    }
  }

  _mouseleave() {
    this.#stopWaitingForPointer?.();
    if (!this._hover) {
      return;
    }
    this._hover = false;
    if (this.linkedPanel && !this.selected) {
      this.linkedBrowser.unselectedTabHover(false);
    }
    this.dispatchEvent(new CustomEvent("TabHoverEnd", { bubbles: true }));
  }

  resumeDelayedMedia() {
    if (this.activeMediaBlocked) {
      this.removeAttribute("activemedia-blocked");
      this.linkedBrowser.resumeMedia();
      gBrowser._tabAttrModified(this, ["activemedia-blocked"]);
    }
  }

  toggleMuteAudio(aMuteReason) {
    let browser = this.linkedBrowser;
    if (browser.audioMuted) {
      if (this.linkedPanel) {
        // "Lazy Browser" should not invoke its unmute method
        browser.browsingContext?.mediaController?.unmute();
      }
      this.removeAttribute("muted");
    } else {
      if (this.linkedPanel) {
        // "Lazy Browser" should not invoke its mute method
        browser.browsingContext?.mediaController?.mute();
      }
      this.toggleAttribute("muted", true);
    }
    this.muteReason = aMuteReason || null;

    gBrowser._tabAttrModified(this, ["muted"]);
  }

  // The handler listening to this tab's MediaController audiblechange event,
  // and the controller it is attached to. Both null when not registered. The
  // controller is remembered so the listener is removed from the exact
  // controller it was added to, even if the browsing context (and thus the
  // current controller) changed in between.
  #audibleChangeHandler = null;
  #audibleChangeController = null;

  // Drive the soundplaying attribute from the parent-process MediaController's
  // aggregated audibility (covers controllable and uncontrolled sources across
  // cross-origin iframes). Re-registers against the current controller.
  registerAudibleChangeHandler() {
    this.unregisterAudibleChangeHandler();
    let mediaController = this.linkedBrowser?.browsingContext?.mediaController;
    if (!mediaController) {
      return;
    }
    this.#audibleChangeHandler = () => {
      if (mediaController.isAudible) {
        clearTimeout(this._soundPlayingAttrRemovalTimer);
        this._soundPlayingAttrRemovalTimer = 0;

        let modifiedAttrs = [];
        if (this.hasAttribute("soundplaying-scheduledremoval")) {
          this.removeAttribute("soundplaying-scheduledremoval");
          modifiedAttrs.push("soundplaying-scheduledremoval");
        }

        if (!this.hasAttribute("soundplaying")) {
          this.toggleAttribute("soundplaying", true);
          modifiedAttrs.push("soundplaying");
        }

        if (modifiedAttrs.length) {
          // Flush style so that the opacity takes effect immediately, in
          // case the media is stopped before the style flushes naturally.
          getComputedStyle(this).opacity;
        }

        gBrowser._tabAttrModified(this, modifiedAttrs);
      } else if (this.hasAttribute("soundplaying")) {
        let removalDelay = Services.prefs.getIntPref(
          "browser.tabs.delayHidingAudioPlayingIconMS"
        );

        // When the tab is muted, the sound icon must be removed immediately
        // without any anti-flicker grace period, because muting cannot be
        // cancelled by a rapid re-audible event (loops stay inaudible while
        // muted). Otherwise, apply a 300 ms floor to prevent icon flicker at
        // loop boundaries.
        let effectiveDelay = this.linkedBrowser?.audioMuted
          ? removalDelay
          : Math.max(removalDelay, 300);

        this.style.setProperty(
          "--soundplaying-removal-delay",
          `${Math.max(effectiveDelay - 300, 0)}ms`
        );
        this.toggleAttribute("soundplaying-scheduledremoval", true);
        gBrowser._tabAttrModified(this, ["soundplaying-scheduledremoval"]);

        this._soundPlayingAttrRemovalTimer = setTimeout(() => {
          this.removeAttribute("soundplaying-scheduledremoval");
          this.removeAttribute("soundplaying");
          gBrowser._tabAttrModified(this, [
            "soundplaying",
            "soundplaying-scheduledremoval",
          ]);
        }, effectiveDelay);
      }
    };
    this.#audibleChangeController = mediaController;
    mediaController.addEventListener(
      "audiblechange",
      this.#audibleChangeHandler
    );
  }

  unregisterAudibleChangeHandler() {
    this.#audibleChangeController?.removeEventListener(
      "audiblechange",
      this.#audibleChangeHandler
    );
    this.#audibleChangeController = null;
    this.#audibleChangeHandler = null;
  }

  setUserContextId(aUserContextId) {
    if (aUserContextId) {
      if (this.linkedBrowser) {
        this.linkedBrowser.setAttribute("usercontextid", aUserContextId);
      }
      this.setAttribute("usercontextid", aUserContextId);
    } else {
      if (this.linkedBrowser) {
        this.linkedBrowser.removeAttribute("usercontextid");
      }
      this.removeAttribute("usercontextid");
    }

    lazy.ContextualIdentityService.setTabStyle(this);
  }

  updateA11yDescription() {
    let prevDescTab = gBrowser.tabContainer.querySelector(
      "tab[aria-describedby]"
    );
    if (prevDescTab) {
      // We can only have a description for the focused tab.
      prevDescTab.removeAttribute("aria-describedby");
    }
    let desc = document.getElementById("tabbrowser-tab-a11y-desc");
    desc.textContent = gBrowser.getTabTooltip(this, false);
    this.setAttribute("aria-describedby", "tabbrowser-tab-a11y-desc");
  }

  on_focus() {
    this.updateA11yDescription();
  }

  on_AriaFocus() {
    this.updateA11yDescription();
  }

  on_overflow(event) {
    event.currentTarget.toggleAttribute("textoverflow", true);
  }

  on_underflow(event) {
    event.currentTarget.removeAttribute("textoverflow");
  }

  #updateOnTabGrouped() {
    if (this.group && this.#lastGroup != this.group) {
      // Trigger TabGrouped on the tab group, not the tab itself. This is a
      // bit unorthodox, but fixes bug1964152 where tab group events are not
      // fired correctly when tabs change windows (because the tab is
      // detached from the DOM at time of the event).
      this.group.dispatchEvent(
        new CustomEvent("TabGrouped", {
          bubbles: true,
          detail: this,
        })
      );
      this.setAttribute("aria-level", "2");
    }
  }

  #updateOnTabUngrouped() {
    if (this.#lastGroup && this.#lastGroup != this.group) {
      // Trigger TabUngrouped on the tab group, not the tab itself. This is a
      // bit unorthodox, but fixes bug1964152 where tab group events are not
      // fired correctly when tabs change windows (because the tab is
      // detached from the DOM at time of the event).
      this.#lastGroup.dispatchEvent(
        new CustomEvent("TabUngrouped", {
          bubbles: true,
          detail: this,
        })
      );
      // Tab could have moved to be ungrouped (level 1)
      // or to a different group (level 2).
      this.setAttribute("aria-level", this.group ? "2" : "1");
      // `posinset` and `setsize` only need to be set explicitly
      // on grouped tabs so that a11y tools can tell users that a
      // given tab is "2 of 7" in the group, for example.
      this.removeAttribute("aria-posinset");
      this.removeAttribute("aria-setsize");
    }
  }

  #updateOnTabSplit() {
    if (this.splitview) {
      this.setAttribute("aria-level", "2");
    }
  }

  #updateOnTabUnsplit() {
    if (!this.splitview) {
      this.setAttribute("aria-level", "1");
      // `posinset` and `setsize` only need to be set explicitly
      // on split view tabs so that a11y tools can tell users that a
      // given tab is "1 of 2" in the split view, for example.
      this.removeAttribute("aria-posinset");
      this.removeAttribute("aria-setsize");
      this.removeAttribute("aria-label");
    }
  }

  /**
   * Set `aria-label` for this tab to indicate that it's in a Split View,
   * along with its position within the Split View.
   *
   * @param {number} index
   *   The index of this tab in the Split View.
   */
  updateSplitViewAriaLabel(index) {
    let l10nId = "";
    switch (index) {
      case 0:
        l10nId = window.RTL_UI
          ? "tabbrowser-tab-label-tab-split-view-right"
          : "tabbrowser-tab-label-tab-split-view-left";
        break;
      case 1:
        l10nId = window.RTL_UI
          ? "tabbrowser-tab-label-tab-split-view-left"
          : "tabbrowser-tab-label-tab-split-view-right";
        break;
    }
    if (l10nId) {
      const ariaLabel = gBrowser.tabLocalization.formatValueSync(l10nId, {
        label: this.getAttribute("label"),
      });
      this.setAttribute("aria-label", ariaLabel);
    }
  }
}

customElements.define("tabbrowser-tab", MozTabbrowserTab, {
  extends: "tab",
});
