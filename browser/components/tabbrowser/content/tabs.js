/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/* eslint-env mozilla/browser-window */

"use strict";

// This is loaded into all browser windows. Wrap in a block to prevent
// leaking to window scope.
{
  const lazy = {};
  ChromeUtils.defineESModuleGetters(lazy, {
    TabMetrics: "moz-src:///browser/components/tabbrowser/TabMetrics.sys.mjs",
  });

  const TAB_PREVIEW_PREF = "browser.tabs.hoverPreview.enabled";

  const DIRECTION_BACKWARD = -1;
  const DIRECTION_FORWARD = 1;

  const isTab = element => gBrowser.isTab(element);
  const isTabGroup = element => gBrowser.isTabGroup(element);
  const isTabGroupLabel = element => gBrowser.isTabGroupLabel(element);

  class MozTabbrowserTabs extends MozElements.TabsBase {
    static observedAttributes = ["orient"];

    #dragOverCreateGroupTimer;
    #dragTime = 0;
    #maxTabsPerRow;
    #mustUpdateTabMinHeight = false;
    #tabMinHeight = 36;

    constructor() {
      super();

      this.addEventListener("TabSelect", this);
      this.addEventListener("TabClose", this);
      this.addEventListener("TabAttrModified", this);
      this.addEventListener("TabHide", this);
      this.addEventListener("TabShow", this);
      this.addEventListener("TabHoverStart", this);
      this.addEventListener("TabHoverEnd", this);
      this.addEventListener("TabGroupExpand", this);
      this.addEventListener("TabGroupCollapse", this);
      this.addEventListener("TabGroupCreate", this);
      this.addEventListener("TabGroupRemoved", this);
      this.addEventListener("transitionend", this);
      this.addEventListener("dblclick", this);
      this.addEventListener("click", this);
      this.addEventListener("click", this, true);
      this.addEventListener("keydown", this, { mozSystemGroup: true });
      this.addEventListener("dragstart", this);
      this.addEventListener("dragover", this);
      this.addEventListener("drop", this);
      this.addEventListener("dragend", this);
      this.addEventListener("dragleave", this);
      this.addEventListener("mouseleave", this);
      this.addEventListener("focusin", this);
      this.addEventListener("focusout", this);
      this.addEventListener("contextmenu", this);
    }

    init() {
      this.startupTime = Services.startup.getStartupInfo().start.getTime();

      this.arrowScrollbox = document.getElementById(
        "tabbrowser-arrowscrollbox"
      );
      this.arrowScrollbox.addEventListener("wheel", this, true);
      this.arrowScrollbox.addEventListener("underflow", this);
      this.arrowScrollbox.addEventListener("overflow", this);
      this.verticalPinnedTabsContainer = document.getElementById(
        "vertical-pinned-tabs-container"
      );
      // Override arrowscrollbox.js method, since our scrollbox's children are
      // inherited from the scrollbox binding parent (this).
      this.arrowScrollbox._getScrollableElements = () => {
        return this.ariaFocusableItems.filter(
          this.arrowScrollbox._canScrollToElement
        );
      };
      this.arrowScrollbox._canScrollToElement = element => {
        if (isTab(element)) {
          return !element.pinned || !this.hasAttribute("positionpinnedtabs");
        }
        return true;
      };

      // Override for performance reasons. This is the size of a single element
      // that can be scrolled when using mouse wheel scrolling. If we don't do
      // this then arrowscrollbox computes this value by calling
      // _getScrollableElements and dividing the box size by that number.
      // However in the tabstrip case we already know the answer to this as,
      // when we're overflowing, it is always the same as the tab min width or
      // height. For tab group labels, the number won't exactly match, but
      // that shouldn't be a problem in practice since the arrowscrollbox
      // stops at element bounds when finishing scrolling.
      Object.defineProperty(this.arrowScrollbox, "lineScrollAmount", {
        get: () =>
          this.verticalMode ? this.#tabMinHeight : this._tabMinWidthPref,
      });

      this.baseConnect();

      this._blockDblClick = false;
      this._tabDropIndicator = this.querySelector(".tab-drop-indicator");
      this._closeButtonsUpdatePending = false;
      this._closingTabsSpacer = this.querySelector(".closing-tabs-spacer");
      this._tabDefaultMaxWidth = NaN;
      this._lastTabClosedByMouse = false;
      this._hasTabTempMaxWidth = false;
      this._scrollButtonWidth = 0;
      this._lastNumPinned = 0;
      this._pinnedTabsLayoutCache = null;
      this._animateElement = this.arrowScrollbox;
      this._tabClipWidth = Services.prefs.getIntPref(
        "browser.tabs.tabClipWidth"
      );
      this._hiddenSoundPlayingTabs = new Set();
      this.previewPanel = null;

      this.allTabs[0].label = this.emptyTabTitle;

      // Hide the secondary text for locales where it is unsupported due to size constraints.
      const language = Services.locale.appLocaleAsBCP47;
      const unsupportedLocales = Services.prefs.getCharPref(
        "browser.tabs.secondaryTextUnsupportedLocales"
      );
      this.toggleAttribute(
        "secondarytext-unsupported",
        unsupportedLocales.split(",").includes(language.split("-")[0])
      );

      this.newTabButton.setAttribute(
        "aria-label",
        GetDynamicShortcutTooltipText("tabs-newtab-button")
      );

      let handleResize = () => {
        this._updateCloseButtons();
        this._handleTabSelect(true);
      };
      window.addEventListener("resize", handleResize);
      this._fullscreenMutationObserver = new MutationObserver(handleResize);
      this._fullscreenMutationObserver.observe(document.documentElement, {
        attributeFilter: ["inFullscreen", "inDOMFullscreen"],
      });

      this.boundObserve = (...args) => this.observe(...args);
      Services.prefs.addObserver("privacy.userContext", this.boundObserve);
      this.observe(null, "nsPref:changed", "privacy.userContext.enabled");

      document
        .getElementById("vertical-tabs-newtab-button")
        .addEventListener("keypress", this);
      document
        .getElementById("tabs-newtab-button")
        .addEventListener("keypress", this);

      XPCOMUtils.defineLazyPreferenceGetter(
        this,
        "_tabMinWidthPref",
        "browser.tabs.tabMinWidth",
        null,
        (pref, prevValue, newValue) => this.#updateTabMinWidth(newValue),
        newValue => {
          const LIMIT = 50;
          return Math.max(newValue, LIMIT);
        }
      );
      this.#updateTabMinWidth(this._tabMinWidthPref);
      this.#updateTabMinHeight();

      CustomizableUI.addListener(this);
      this._updateNewTabVisibility();

      XPCOMUtils.defineLazyPreferenceGetter(
        this,
        "_closeTabByDblclick",
        "browser.tabs.closeTabByDblclick",
        false
      );

      XPCOMUtils.defineLazyPreferenceGetter(
        this,
        "_sidebarVisibility",
        "sidebar.visibility",
        "always-show"
      );

      XPCOMUtils.defineLazyPreferenceGetter(
        this,
        "_sidebarPositionStart",
        "sidebar.position_start",
        true
      );

      if (gMultiProcessBrowser) {
        this.tabbox.tabpanels.setAttribute("async", "true");
      }

      XPCOMUtils.defineLazyPreferenceGetter(
        this,
        "_showCardPreviews",
        TAB_PREVIEW_PREF,
        false
      );
      this.tooltip = "tabbrowser-tab-tooltip";
    }

    attributeChangedCallback(name, oldValue, newValue) {
      if (name != "orient") {
        return;
      }

      if (oldValue == "vertical" && newValue == "horizontal") {
        this._resetVerticalPinnedTabs();
      }
      if (this.overflowing) {
        // reset this value so we don't have incorrect styling for vertical tabs
        this.removeAttribute("overflow");
      }
      this._positionPinnedTabs();

      this.#updateTabMinWidth();
      this.#updateTabMinHeight();

      super.attributeChangedCallback(name, oldValue, newValue);
    }

    on_TabSelect() {
      this._handleTabSelect();
    }

    on_TabClose(event) {
      this._hiddenSoundPlayingStatusChanged(event.target, { closed: true });
    }

    on_TabAttrModified(event) {
      if (
        event.detail.changed.includes("soundplaying") &&
        !event.target.visible
      ) {
        this._hiddenSoundPlayingStatusChanged(event.target);
      }
      if (
        event.detail.changed.includes("soundplaying") ||
        event.detail.changed.includes("muted") ||
        event.detail.changed.includes("activemedia-blocked")
      ) {
        this.updateTabSoundLabel(event.target);
      }
    }

    on_TabHide(event) {
      if (event.target.soundPlaying) {
        this._hiddenSoundPlayingStatusChanged(event.target);
      }
    }

    on_TabShow(event) {
      if (event.target.soundPlaying) {
        this._hiddenSoundPlayingStatusChanged(event.target);
      }
    }

    on_TabHoverStart(event) {
      if (!this._showCardPreviews) {
        return;
      }
      if (!this.previewPanel) {
        // load the tab preview component
        const TabHoverPreviewPanel = ChromeUtils.importESModule(
          "chrome://browser/content/tabbrowser/tab-hover-preview.mjs"
        ).default;
        this.previewPanel = new TabHoverPreviewPanel(
          document.getElementById("tab-preview-panel")
        );
      }
      this.previewPanel.activate(event.target);
    }

    on_TabHoverEnd(event) {
      this.previewPanel?.deactivate(event.target);
    }

    on_TabGroupExpand() {
      this._invalidateCachedVisibleTabs();
    }

    on_TabGroupCollapse(event) {
      this._invalidateCachedVisibleTabs();
      this._unlockTabSizing();

      // If the user's selected tab is in the collapsing group, kick them off
      // the tab. If no tabs exist outside the group, create a new one and
      // select it.
      const group = event.target;
      if (gBrowser.selectedTab.group === group && !this.#isMovingTab()) {
        gBrowser.selectedTab =
          gBrowser._findTabToBlurTo(
            gBrowser.selectedTab,
            gBrowser.tabsInCollapsedTabGroups
          ) ||
          gBrowser.addTrustedTab(BROWSER_NEW_TAB_URL, { skipAnimation: true });
      }
    }

    on_TabGroupCreate() {
      this._invalidateCachedTabs();
    }

    on_TabGroupRemoved() {
      this._invalidateCachedTabs();
    }

    on_transitionend(event) {
      if (event.propertyName != "max-width") {
        return;
      }

      let tab = event.target ? event.target.closest("tab") : null;

      if (tab.hasAttribute("fadein")) {
        if (tab._fullyOpen) {
          this._updateCloseButtons();
        } else {
          this._handleNewTab(tab);
        }
      } else if (tab.closing) {
        gBrowser._endRemoveTab(tab);
      }

      let evt = new CustomEvent("TabAnimationEnd", { bubbles: true });
      tab.dispatchEvent(evt);
    }

    on_dblclick(event) {
      // When the tabbar has an unified appearance with the titlebar
      // and menubar, a double-click in it should have the same behavior
      // as double-clicking the titlebar
      if (CustomTitlebar.enabled && !this.verticalMode) {
        return;
      }

      // Make sure it is the primary button, we are hitting our arrowscrollbox,
      // and we're not hitting the scroll buttons.
      if (
        event.button != 0 ||
        event.target != this.arrowScrollbox ||
        event.composedTarget.localName == "toolbarbutton"
      ) {
        return;
      }

      if (!this._blockDblClick) {
        BrowserCommands.openTab();
      }

      event.preventDefault();
    }

    on_click(event) {
      if (event.eventPhase == Event.CAPTURING_PHASE && event.button == 0) {
        /* Catches extra clicks meant for the in-tab close button.
         * Placed here to avoid leaking (a temporary handler added from the
         * in-tab close button binding would close over the tab and leak it
         * until the handler itself was removed). (bug 897751)
         *
         * The only sequence in which a second click event (i.e. dblclik)
         * can be dispatched on an in-tab close button is when it is shown
         * after the first click (i.e. the first click event was dispatched
         * on the tab). This happens when we show the close button only on
         * the active tab. (bug 352021)
         * The only sequence in which a third click event can be dispatched
         * on an in-tab close button is when the tab was opened with a
         * double click on the tabbar. (bug 378344)
         * In both cases, it is most likely that the close button area has
         * been accidentally clicked, therefore we do not close the tab.
         *
         * We don't want to ignore processing of more than one click event,
         * though, since the user might actually be repeatedly clicking to
         * close many tabs at once.
         */
        let target = event.originalTarget;
        if (target.classList.contains("tab-close-button")) {
          // We preemptively set this to allow the closing-multiple-tabs-
          // in-a-row case.
          if (this._blockDblClick) {
            target._ignoredCloseButtonClicks = true;
          } else if (event.detail > 1 && !target._ignoredCloseButtonClicks) {
            target._ignoredCloseButtonClicks = true;
            event.stopPropagation();
            return;
          } else {
            // Reset the "ignored click" flag
            target._ignoredCloseButtonClicks = false;
          }
        }

        /* Protects from close-tab-button errant doubleclick:
         * Since we're removing the event target, if the user
         * double-clicks the button, the dblclick event will be dispatched
         * with the tabbar as its event target (and explicit/originalTarget),
         * which treats that as a mouse gesture for opening a new tab.
         * In this context, we're manually blocking the dblclick event.
         */
        if (this._blockDblClick) {
          if (!("_clickedTabBarOnce" in this)) {
            this._clickedTabBarOnce = true;
            return;
          }
          delete this._clickedTabBarOnce;
          this._blockDblClick = false;
        }
      } else if (
        event.eventPhase == Event.BUBBLING_PHASE &&
        event.button == 1
      ) {
        let tab = event.target?.closest("tab");
        if (tab) {
          if (tab.multiselected) {
            gBrowser.removeMultiSelectedTabs();
          } else {
            gBrowser.removeTab(tab, {
              animate: true,
              triggeringEvent: event,
            });
          }
        } else if (isTabGroupLabel(event.target)) {
          event.target.group.saveAndClose();
        } else if (
          event.originalTarget.closest("scrollbox") &&
          !Services.prefs.getBoolPref(
            "widget.gtk.titlebar-action-middle-click-enabled"
          )
        ) {
          // Check whether the click
          // was dispatched on the open space of it.
          let visibleTabs = this.visibleTabs;
          let lastTab = visibleTabs.at(-1);
          let winUtils = window.windowUtils;
          let endOfTab =
            winUtils.getBoundsWithoutFlushing(lastTab)[
              (this.verticalMode && "bottom") ||
                (this.#rtlMode ? "left" : "right")
            ];
          if (
            (this.verticalMode && event.clientY > endOfTab) ||
            (!this.verticalMode &&
              (this.#rtlMode
                ? event.clientX < endOfTab
                : event.clientX > endOfTab))
          ) {
            BrowserCommands.openTab();
          }
        } else {
          return;
        }

        event.preventDefault();
        event.stopPropagation();
      }
    }

    on_keydown(event) {
      let { altKey, shiftKey } = event;
      let [accel, nonAccel] =
        AppConstants.platform == "macosx"
          ? [event.metaKey, event.ctrlKey]
          : [event.ctrlKey, event.metaKey];

      let keyComboForFocusedElement =
        !accel && !shiftKey && !altKey && !nonAccel;
      let keyComboForMove = accel && shiftKey && !altKey && !nonAccel;
      let keyComboForFocus = accel && !shiftKey && !altKey && !nonAccel;

      if (!keyComboForFocusedElement && !keyComboForMove && !keyComboForFocus) {
        return;
      }

      if (keyComboForFocusedElement) {
        let ariaFocusedItem = this.ariaFocusedItem;
        if (isTabGroupLabel(ariaFocusedItem)) {
          switch (event.keyCode) {
            case KeyEvent.DOM_VK_SPACE:
            case KeyEvent.DOM_VK_RETURN: {
              ariaFocusedItem.click();
              event.preventDefault();
            }
          }
        }
      } else if (keyComboForMove) {
        switch (event.keyCode) {
          case KeyEvent.DOM_VK_UP:
            gBrowser.moveTabBackward();
            break;
          case KeyEvent.DOM_VK_DOWN:
            gBrowser.moveTabForward();
            break;
          case KeyEvent.DOM_VK_RIGHT:
            if (RTL_UI) {
              gBrowser.moveTabBackward();
            } else {
              gBrowser.moveTabForward();
            }
            break;
          case KeyEvent.DOM_VK_LEFT:
            if (RTL_UI) {
              gBrowser.moveTabForward();
            } else {
              gBrowser.moveTabBackward();
            }
            break;
          case KeyEvent.DOM_VK_HOME:
            gBrowser.moveTabToStart();
            break;
          case KeyEvent.DOM_VK_END:
            gBrowser.moveTabToEnd();
            break;
          default:
            // Consume the keydown event for the above keyboard
            // shortcuts only.
            return;
        }

        event.preventDefault();
      } else if (keyComboForFocus) {
        switch (event.keyCode) {
          case KeyEvent.DOM_VK_UP:
            this.#advanceFocus(DIRECTION_BACKWARD);
            break;
          case KeyEvent.DOM_VK_DOWN:
            this.#advanceFocus(DIRECTION_FORWARD);
            break;
          case KeyEvent.DOM_VK_RIGHT:
            if (RTL_UI) {
              this.#advanceFocus(DIRECTION_BACKWARD);
            } else {
              this.#advanceFocus(DIRECTION_FORWARD);
            }
            break;
          case KeyEvent.DOM_VK_LEFT:
            if (RTL_UI) {
              this.#advanceFocus(DIRECTION_FORWARD);
            } else {
              this.#advanceFocus(DIRECTION_BACKWARD);
            }
            break;
          case KeyEvent.DOM_VK_HOME:
            this.ariaFocusedItem = this.ariaFocusableItems.at(0);
            break;
          case KeyEvent.DOM_VK_END:
            this.ariaFocusedItem = this.ariaFocusableItems.at(-1);
            break;
          case KeyEvent.DOM_VK_SPACE: {
            let ariaFocusedItem = this.ariaFocusedItem;
            if (isTab(ariaFocusedItem)) {
              if (ariaFocusedItem.multiselected) {
                gBrowser.removeFromMultiSelectedTabs(ariaFocusedItem);
              } else {
                gBrowser.addToMultiSelectedTabs(ariaFocusedItem);
              }
            }
            break;
          }
          default:
            // Consume the keydown event for the above keyboard
            // shortcuts only.
            return;
        }

        event.preventDefault();
      }
    }

    /**
     * @param {FocusEvent} event
     */
    on_focusin(event) {
      if (event.target == this.selectedItem) {
        this.tablistHasFocus = true;
        if (!this.ariaFocusedItem) {
          // If the active tab is receiving focus and there isn't a keyboard
          // focus target yet, set the keyboard focus target to the active
          // tab. Do not override the keyboard-focused item if the user
          // already set a keyboard focus.
          this.ariaFocusedItem = this.selectedItem;
        }
      }
    }

    /**
     * @param {FocusEvent} event
     */
    on_focusout(event) {
      if (event.target == this.selectedItem) {
        this.tablistHasFocus = false;
      }
    }

    /**
     * Moves the ARIA focus in the tab strip left or right, as appropriate, to
     * the next tab or tab group label.
     *
     * @param {-1|1} direction
     */
    #advanceFocus(direction) {
      let currentIndex = this.ariaFocusableItems.indexOf(this.ariaFocusedItem);
      let newIndex = currentIndex + direction;

      // Clamp the index so that the focus stops at the edges of the tab strip
      newIndex = Math.min(
        this.ariaFocusableItems.length - 1,
        Math.max(0, newIndex)
      );

      let itemToFocus = this.ariaFocusableItems[newIndex];
      this.ariaFocusedItem = itemToFocus;
    }

    /**
     * Changes the selected tab or tab group label on the tab strip
     * relative to the ARIA-focused tab strip element or the active tab. This
     * is intended for traversing the tab strip visually, e.g by using keyboard
     * arrows. For cases where keyboard shortcuts or other logic should only
     * select tabs (and never tab group labels), see `advanceSelectedTab`.
     *
     * @override
     * @param {-1|1} direction
     * @param {boolean} shouldWrap
     */
    advanceSelectedItem(aDir, aWrap) {
      let { ariaFocusableItems, ariaFocusedIndex } = this;

      // Advance relative to the ARIA-focused item if set, otherwise advance
      // relative to the active tab.
      let currentItemIndex =
        ariaFocusedIndex >= 0
          ? ariaFocusedIndex
          : ariaFocusableItems.indexOf(this.selectedItem);

      let newItemIndex = currentItemIndex + aDir;

      if (aWrap) {
        if (newItemIndex >= ariaFocusableItems.length) {
          newItemIndex = 0;
        } else if (newItemIndex < 0) {
          newItemIndex = ariaFocusableItems.length - 1;
        }
      } else {
        newItemIndex = Math.min(
          ariaFocusableItems.length - 1,
          Math.max(0, newItemIndex)
        );
      }

      if (currentItemIndex == newItemIndex) {
        return;
      }

      // If the next item is a tab, select it. If the next item is a tab group
      // label, keep the active tab selected and just set ARIA focus on the tab
      // group label.
      let newItem = ariaFocusableItems[newItemIndex];
      if (isTab(newItem)) {
        this._selectNewTab(newItem, aDir, aWrap);
      }
      this.ariaFocusedItem = newItem;
    }

    on_keypress(event) {
      if (event.defaultPrevented) {
        return;
      }
      if (event.key == " " || event.key == "Enter") {
        event.preventDefault();
        event.target.click();
      }
    }

    on_dragstart(event) {
      if (this._isCustomizing) {
        return;
      }

      let tab = this.#getDragTarget(event);
      if (!tab) {
        return;
      }

      this.previewPanel?.deactivate();
      this.startTabDrag(event, tab);
    }

    startTabDrag(event, tab, { fromTabList = false } = {}) {
      if (this.#isContainerVerticalPinnedGrid(tab)) {
        // In expanded vertical mode, the max number of pinned tabs per row is dynamic
        // Set this before adjusting dragged tab's position
        let pinnedTabs = this.visibleTabs.slice(0, gBrowser.pinnedTabCount);
        let tabsPerRow = 0;
        let position = 0;
        for (let pinnedTab of pinnedTabs) {
          let tabPosition =
            window.windowUtils.getBoundsWithoutFlushing(pinnedTab).left;
          if (tabPosition < position) {
            break;
          }
          tabsPerRow++;
          position = tabPosition;
        }
        this.#maxTabsPerRow = tabsPerRow;
      }

      if (tab.multiselected) {
        for (let multiselectedTab of gBrowser.selectedTabs.filter(
          t => t.pinned != tab.pinned
        )) {
          gBrowser.removeFromMultiSelectedTabs(multiselectedTab);
        }
      }

      let dataTransferOrderedTabs;
      if (fromTabList || isTabGroupLabel(tab)) {
        // Dragging a group label or an item in the all tabs menu doesn't
        // change the currently selected tabs, and it's not possible to select
        // multiple tabs from the list, thus handle only the dragged tab in
        // this case.
        dataTransferOrderedTabs = [tab];
      } else {
        let selectedTabs = gBrowser.selectedTabs;
        let otherSelectedTabs = selectedTabs.filter(
          selectedTab => selectedTab != tab
        );
        dataTransferOrderedTabs = [tab].concat(otherSelectedTabs);
      }

      let dt = event.dataTransfer;
      for (let i = 0; i < dataTransferOrderedTabs.length; i++) {
        let dtTab = dataTransferOrderedTabs[i];
        dt.mozSetDataAt(TAB_DROP_TYPE, dtTab, i);
        if (isTab(dtTab)) {
          let dtBrowser = dtTab.linkedBrowser;

          // We must not set text/x-moz-url or text/plain data here,
          // otherwise trying to detach the tab by dropping it on the desktop
          // may result in an "internet shortcut"
          dt.mozSetDataAt(
            "text/x-moz-text-internal",
            dtBrowser.currentURI.spec,
            i
          );
        }
      }

      // Set the cursor to an arrow during tab drags.
      dt.mozCursor = "default";

      // Set the tab as the source of the drag, which ensures we have a stable
      // node to deliver the `dragend` event.  See bug 1345473.
      dt.addElement(tab);

      let expandGroupOnDrop;
      if (!fromTabList && this.getDropEffectForTabDrag(event) == "move") {
        this.#setMovingTabMode(true);

        if (tab.multiselected) {
          this.#moveTogetherSelectedTabs(tab);
        } else if (isTabGroupLabel(tab) && !tab.group.collapsed) {
          this._lockTabSizing();
          this.#keepTabSizeLocked = true;
          tab.group.collapsed = true;
          expandGroupOnDrop = true;
        }
      }

      // Create a canvas to which we capture the current tab.
      // Until canvas is HiDPI-aware (bug 780362), we need to scale the desired
      // canvas size (in CSS pixels) to the window's backing resolution in order
      // to get a full-resolution drag image for use on HiDPI displays.
      let scale = window.devicePixelRatio;
      let canvas = this._dndCanvas;
      if (!canvas) {
        this._dndCanvas = canvas = document.createElementNS(
          "http://www.w3.org/1999/xhtml",
          "canvas"
        );
        canvas.style.width = "100%";
        canvas.style.height = "100%";
        canvas.mozOpaque = true;
      }

      canvas.width = 160 * scale;
      canvas.height = 90 * scale;
      let toDrag = canvas;
      let dragImageOffset = -16;
      let browser = isTab(tab) && tab.linkedBrowser;
      if (isTabGroupLabel(tab)) {
        toDrag = document.getElementById("tab-drag-empty-feedback");
      } else if (gMultiProcessBrowser) {
        var context = canvas.getContext("2d");
        context.fillStyle = "white";
        context.fillRect(0, 0, canvas.width, canvas.height);

        let captureListener;
        let platform = AppConstants.platform;
        // On Windows and Mac we can update the drag image during a drag
        // using updateDragImage. On Linux, we can use a panel.
        if (platform == "win" || platform == "macosx") {
          captureListener = function () {
            dt.updateDragImage(canvas, dragImageOffset, dragImageOffset);
          };
        } else {
          // Create a panel to use it in setDragImage
          // which will tell xul to render a panel that follows
          // the pointer while a dnd session is on.
          if (!this._dndPanel) {
            this._dndCanvas = canvas;
            this._dndPanel = document.createXULElement("panel");
            this._dndPanel.className = "dragfeedback-tab";
            this._dndPanel.setAttribute("type", "drag");
            let wrapper = document.createElementNS(
              "http://www.w3.org/1999/xhtml",
              "div"
            );
            wrapper.style.width = "160px";
            wrapper.style.height = "90px";
            wrapper.appendChild(canvas);
            this._dndPanel.appendChild(wrapper);
            document.documentElement.appendChild(this._dndPanel);
          }
          toDrag = this._dndPanel;
        }
        // PageThumb is async with e10s but that's fine
        // since we can update the image during the dnd.
        PageThumbs.captureToCanvas(browser, canvas)
          .then(captureListener)
          .catch(e => console.error(e));
      } else {
        // For the non e10s case we can just use PageThumbs
        // sync, so let's use the canvas for setDragImage.
        PageThumbs.captureToCanvas(browser, canvas).catch(e =>
          console.error(e)
        );
        dragImageOffset = dragImageOffset * scale;
      }
      dt.setDragImage(toDrag, dragImageOffset, dragImageOffset);

      // _dragData.offsetX/Y give the coordinates that the mouse should be
      // positioned relative to the corner of the new window created upon
      // dragend such that the mouse appears to have the same position
      // relative to the corner of the dragged tab.
      let clientPos = ele => {
        const rect = ele.getBoundingClientRect();
        return this.verticalMode ? rect.top : rect.left;
      };

      let tabOffset = clientPos(tab) - clientPos(this);

      tab._dragData = {
        offsetX: this.verticalMode
          ? event.screenX - window.screenX
          : event.screenX - window.screenX - tabOffset,
        offsetY: this.verticalMode
          ? event.screenY - window.screenY - tabOffset
          : event.screenY - window.screenY,
        scrollPos:
          this.verticalMode && tab.pinned
            ? this.verticalPinnedTabsContainer.scrollPosition
            : this.arrowScrollbox.scrollPosition,
        screenX: event.screenX,
        screenY: event.screenY,
        movingTabs: tab.multiselected ? gBrowser.selectedTabs : [tab],
        fromTabList,
        tabGroupCreationColor: gBrowser.tabGroupMenu.nextUnusedColor,
        expandGroupOnDrop,
      };

      event.stopPropagation();

      if (fromTabList) {
        Glean.browserUiInteraction.allTabsPanelDragstartTabEventCount.add(1);
      }
    }

    on_dragover(event) {
      var effects = this.getDropEffectForTabDrag(event);

      var ind = this._tabDropIndicator;
      if (effects == "" || effects == "none") {
        ind.hidden = true;
        return;
      }
      event.preventDefault();
      event.stopPropagation();

      var arrowScrollbox = this.arrowScrollbox;

      // autoscroll the tab strip if we drag over the scroll
      // buttons, even if we aren't dragging a tab, but then
      // return to avoid drawing the drop indicator
      var pixelsToScroll = 0;
      if (this.overflowing) {
        switch (event.originalTarget) {
          case arrowScrollbox._scrollButtonUp:
            pixelsToScroll = arrowScrollbox.scrollIncrement * -1;
            break;
          case arrowScrollbox._scrollButtonDown:
            pixelsToScroll = arrowScrollbox.scrollIncrement;
            break;
        }
        if (pixelsToScroll) {
          arrowScrollbox.scrollByPixels(
            (this.#rtlMode ? -1 : 1) * pixelsToScroll,
            true
          );
        }
      }

      let draggedTab = event.dataTransfer.mozGetDataAt(TAB_DROP_TYPE, 0);
      if (
        (effects == "move" || effects == "copy") &&
        document == draggedTab.ownerDocument &&
        !draggedTab._dragData.fromTabList
      ) {
        ind.hidden = true;

        if (this.#isAnimatingMoveTogetherSelectedTabs()) {
          // Wait for moving selected tabs together animation to finish.
          return;
        }
        this.finishMoveTogetherSelectedTabs(draggedTab);

        if (effects == "move") {
          this.#setMovingTabMode(true);

          // Pinned tabs in expanded vertical mode are on a grid format and require
          // different logic to drag and drop.
          if (this.#isContainerVerticalPinnedGrid(draggedTab)) {
            this.#animateExpandedPinnedTabMove(event);
            return;
          }
          this._animateTabMove(event);
          return;
        }
      }

      this.finishAnimateTabMove();

      if (effects == "link") {
        let target = this.#getDragTarget(event, { ignoreSides: true });
        if (target) {
          if (!this.#dragTime) {
            this.#dragTime = Date.now();
          }
          let overGroupLabel = isTabGroupLabel(target);
          if (
            Date.now() >=
            this.#dragTime +
              Services.prefs.getIntPref(
                overGroupLabel
                  ? "browser.tabs.dragDrop.expandGroup.delayMS"
                  : "browser.tabs.dragDrop.selectTab.delayMS"
              )
          ) {
            if (overGroupLabel) {
              target.group.collapsed = false;
            } else {
              this.selectedItem = target;
            }
          }
          if (isTab(target)) {
            // Dropping on the target tab would replace the loaded page rather
            // than opening a new tab, so hide the drop indicator.
            ind.hidden = true;
            return;
          }
        }
      }

      var rect = arrowScrollbox.getBoundingClientRect();
      var newMargin;
      if (pixelsToScroll) {
        // if we are scrolling, put the drop indicator at the edge
        // so that it doesn't jump while scrolling
        let scrollRect = arrowScrollbox.scrollClientRect;
        let minMargin = this.verticalMode
          ? scrollRect.top - rect.top
          : scrollRect.left - rect.left;
        let maxMargin = this.verticalMode
          ? Math.min(minMargin + scrollRect.height, scrollRect.bottom)
          : Math.min(minMargin + scrollRect.width, scrollRect.right);
        if (this.#rtlMode) {
          [minMargin, maxMargin] = [
            this.clientWidth - maxMargin,
            this.clientWidth - minMargin,
          ];
        }
        newMargin = pixelsToScroll > 0 ? maxMargin : minMargin;
      } else {
        let newIndex = this.#getDropIndex(event);
        let children = this.ariaFocusableItems;
        if (newIndex == children.length) {
          let itemRect = children.at(-1).getBoundingClientRect();
          if (this.verticalMode) {
            newMargin = itemRect.bottom - rect.top;
          } else if (this.#rtlMode) {
            newMargin = rect.right - itemRect.left;
          } else {
            newMargin = itemRect.right - rect.left;
          }
        } else {
          let itemRect = children[newIndex].getBoundingClientRect();
          if (this.verticalMode) {
            newMargin = rect.top - itemRect.bottom;
          } else if (this.#rtlMode) {
            newMargin = rect.right - itemRect.right;
          } else {
            newMargin = itemRect.left - rect.left;
          }
        }
      }

      ind.hidden = false;
      newMargin += this.verticalMode ? ind.clientHeight : ind.clientWidth / 2;
      if (this.#rtlMode) {
        newMargin *= -1;
      }
      ind.style.transform = this.verticalMode
        ? "translateY(" + Math.round(newMargin) + "px)"
        : "translateX(" + Math.round(newMargin) + "px)";
    }

    #setMovingTabMode(movingTab) {
      this.toggleAttribute("movingtab", movingTab);
      gNavToolbox.toggleAttribute("movingtab", movingTab);
    }

    #isMovingTab() {
      return this.hasAttribute("movingtab");
    }

    #expandGroupOnDrop(draggedTab) {
      if (
        isTabGroupLabel(draggedTab) &&
        draggedTab._dragData?.expandGroupOnDrop
      ) {
        draggedTab.group.collapsed = false;
        this.#keepTabSizeLocked = false;
        this._unlockTabSizing();
      }
    }

    // eslint-disable-next-line complexity
    on_drop(event) {
      var dt = event.dataTransfer;
      var dropEffect = dt.dropEffect;
      var draggedTab;
      let movingTabs;
      /** @type {TabMetricsContext} */
      const dropMetricsContext = lazy.TabMetrics.userTriggeredContext(
        lazy.TabMetrics.METRIC_SOURCE.DRAG_AND_DROP
      );
      if (dt.mozTypesAt(0)[0] == TAB_DROP_TYPE) {
        // tab copy or move
        draggedTab = dt.mozGetDataAt(TAB_DROP_TYPE, 0);
        // not our drop then
        if (!draggedTab) {
          return;
        }
        movingTabs = draggedTab._dragData.movingTabs;
        draggedTab.container.finishMoveTogetherSelectedTabs(draggedTab);
      }

      this._tabDropIndicator.hidden = true;
      event.stopPropagation();
      if (draggedTab && dropEffect == "copy") {
        let duplicatedDraggedTab;
        let duplicatedTabs = [];
        let dropTarget = this.ariaFocusableItems[this.#getDropIndex(event)];
        for (let tab of movingTabs) {
          let duplicatedTab = gBrowser.duplicateTab(tab);
          duplicatedTabs.push(duplicatedTab);
          if (tab == draggedTab) {
            duplicatedDraggedTab = duplicatedTab;
          }
        }
        gBrowser.moveTabsBefore(duplicatedTabs, dropTarget, dropMetricsContext);
        if (draggedTab.container != this || event.shiftKey) {
          this.selectedItem = duplicatedDraggedTab;
        }
      } else if (draggedTab && draggedTab.container == this) {
        let oldTranslateX = Math.round(draggedTab._dragData.translateX);
        let oldTranslateY = Math.round(draggedTab._dragData.translateY);
        let tabWidth = Math.round(draggedTab._dragData.tabWidth);
        let tabHeight = Math.round(draggedTab._dragData.tabHeight);
        let translateOffsetX = oldTranslateX % tabWidth;
        let translateOffsetY = oldTranslateY % tabHeight;
        let newTranslateX = oldTranslateX - translateOffsetX;
        let newTranslateY = oldTranslateY - translateOffsetY;

        if (this.#isContainerVerticalPinnedGrid(draggedTab)) {
          // Update both translate axis for pinned vertical expanded tabs
          if (oldTranslateX > 0 && translateOffsetX > tabWidth / 2) {
            newTranslateX += tabWidth;
          } else if (oldTranslateX < 0 && -translateOffsetX > tabWidth / 2) {
            newTranslateX -= tabWidth;
          }
          if (oldTranslateY > 0 && translateOffsetY > tabHeight / 2) {
            newTranslateY += tabHeight;
          } else if (oldTranslateY < 0 && -translateOffsetY > tabHeight / 2) {
            newTranslateY -= tabHeight;
          }
        } else {
          let isPinned = draggedTab.pinned;
          let numPinned = gBrowser.pinnedTabCount;
          let tabs = this.ariaFocusableItems.slice(
            isPinned ? 0 : numPinned,
            isPinned ? numPinned : undefined
          );
          let size = this.verticalMode ? "height" : "width";
          let screenAxis = this.verticalMode ? "screenY" : "screenX";
          let tabSize = this.verticalMode ? tabHeight : tabWidth;
          let firstTab = tabs[0];
          let lastTab = tabs.at(-1);
          let lastMovingTabScreen = movingTabs.at(-1)[screenAxis];
          let firstMovingTabScreen = movingTabs[0][screenAxis];
          let firstBound = firstTab[screenAxis] - firstMovingTabScreen;
          let lastBound =
            lastTab[screenAxis] +
            window.windowUtils.getBoundsWithoutFlushing(lastTab)[size] -
            (lastMovingTabScreen + tabSize);

          if (this.verticalMode) {
            newTranslateY = Math.min(
              Math.max(oldTranslateY, firstBound),
              lastBound
            );
          } else {
            newTranslateX = RTL_UI
              ? Math.min(Math.max(oldTranslateX, lastBound), firstBound)
              : Math.min(Math.max(oldTranslateX, firstBound), lastBound);
          }
        }

        let { dropElement, dropBefore, shouldCreateGroupOnDrop, fromTabList } =
          draggedTab._dragData;

        let dropIndex;
        let directionForward = false;
        if (fromTabList) {
          dropIndex = this.#getDropIndex(event);
          if (dropIndex && dropIndex > movingTabs[0].elementIndex) {
            dropIndex--;
            directionForward = true;
          }
        }

        let shouldTranslate =
          !gReduceMotion &&
          !shouldCreateGroupOnDrop &&
          !isTabGroupLabel(draggedTab);
        if (this.#isContainerVerticalPinnedGrid(draggedTab)) {
          shouldTranslate &&=
            (oldTranslateX && oldTranslateX != newTranslateX) ||
            (oldTranslateY && oldTranslateY != newTranslateY);
        } else if (this.verticalMode) {
          shouldTranslate &&= oldTranslateY && oldTranslateY != newTranslateY;
        } else {
          shouldTranslate &&= oldTranslateX && oldTranslateX != newTranslateX;
        }

        let moveTabs = () => {
          if (dropIndex !== undefined) {
            for (let tab of movingTabs) {
              gBrowser.moveTabTo(
                tab,
                { elementIndex: dropIndex },
                dropMetricsContext
              );
              if (!directionForward) {
                dropIndex++;
              }
            }
          } else if (dropBefore) {
            gBrowser.moveTabsBefore(
              movingTabs,
              dropElement,
              dropMetricsContext
            );
          } else {
            gBrowser.moveTabsAfter(movingTabs, dropElement, dropMetricsContext);
          }
          this.#expandGroupOnDrop(draggedTab);
        };

        if (shouldTranslate) {
          let translationPromises = [];
          for (let item of movingTabs) {
            if (isTabGroupLabel(item)) {
              // Shift the `.tab-group-label-container` to shift the label element.
              item = item.parentElement;
            }
            let translationPromise = new Promise(resolve => {
              item.toggleAttribute("tabdrop-samewindow", true);
              item.style.transform = `translate(${newTranslateX}px, ${newTranslateY}px)`;
              let postTransitionCleanup = () => {
                item.removeAttribute("tabdrop-samewindow");
                resolve();
              };
              if (gReduceMotion) {
                postTransitionCleanup();
              } else {
                let onTransitionEnd = transitionendEvent => {
                  if (
                    transitionendEvent.propertyName != "transform" ||
                    transitionendEvent.originalTarget != item
                  ) {
                    return;
                  }
                  item.removeEventListener("transitionend", onTransitionEnd);

                  postTransitionCleanup();
                };
                item.addEventListener("transitionend", onTransitionEnd);
              }
            });
            translationPromises.push(translationPromise);
          }
          Promise.all(translationPromises).then(() => {
            this.finishAnimateTabMove();
            moveTabs();
          });
        } else {
          this.finishAnimateTabMove();
          if (shouldCreateGroupOnDrop) {
            // This makes the tab group contents reflect the visual order of
            // the tabs right before dropping.
            let tabsInGroup = dropBefore
              ? [...movingTabs, dropElement]
              : [dropElement, ...movingTabs];
            gBrowser.addTabGroup(tabsInGroup, {
              insertBefore: dropElement,
              isUserTriggered: true,
              color: draggedTab._dragData.tabGroupCreationColor,
              telemetryUserCreateSource: "drag",
            });
          } else {
            moveTabs();
          }
        }
      } else if (isTabGroupLabel(draggedTab)) {
        gBrowser.adoptTabGroup(draggedTab.group, this.#getDropIndex(event));
      } else if (draggedTab) {
        // Move the tabs into this window. To avoid multiple tab-switches in
        // the original window, the selected tab should be adopted last.
        const dropIndex = this.#getDropIndex(event);
        let newIndex = dropIndex;
        let selectedTab;
        let indexForSelectedTab;
        for (let i = 0; i < movingTabs.length; ++i) {
          const tab = movingTabs[i];
          if (tab.selected) {
            selectedTab = tab;
            indexForSelectedTab = newIndex;
          } else {
            const newTab = gBrowser.adoptTab(tab, {
              elementIndex: newIndex,
              selectTab: tab == draggedTab,
            });
            if (newTab) {
              ++newIndex;
            }
          }
        }
        if (selectedTab) {
          const newTab = gBrowser.adoptTab(selectedTab, {
            elementIndex: indexForSelectedTab,
            selectTab: selectedTab == draggedTab,
          });
          if (newTab) {
            ++newIndex;
          }
        }

        // Restore tab selection
        gBrowser.addRangeToMultiSelectedTabs(
          this.ariaFocusableItems[dropIndex],
          this.ariaFocusableItems[newIndex - 1]
        );
      } else {
        // Pass true to disallow dropping javascript: or data: urls
        let links;
        try {
          links = Services.droppedLinkHandler.dropLinks(event, true);
        } catch (ex) {}

        if (!links || links.length === 0) {
          return;
        }

        let inBackground = Services.prefs.getBoolPref(
          "browser.tabs.loadInBackground"
        );
        if (event.shiftKey) {
          inBackground = !inBackground;
        }

        let targetTab = this.#getDragTarget(event, { ignoreSides: true });
        let userContextId = this.selectedItem.getAttribute("usercontextid");
        let replace = isTab(targetTab);
        let newIndex = this.#getDropIndex(event);
        let urls = links.map(link => link.url);
        let csp = Services.droppedLinkHandler.getCsp(event);
        let triggeringPrincipal =
          Services.droppedLinkHandler.getTriggeringPrincipal(event);

        (async () => {
          if (
            urls.length >=
            Services.prefs.getIntPref("browser.tabs.maxOpenBeforeWarn")
          ) {
            // Sync dialog cannot be used inside drop event handler.
            let answer = await OpenInTabsUtils.promiseConfirmOpenInTabs(
              urls.length,
              window
            );
            if (!answer) {
              return;
            }
          }

          let nextItem = this.ariaFocusableItems[newIndex];
          let tabGroup = isTab(nextItem) && nextItem.group;
          gBrowser.loadTabs(urls, {
            inBackground,
            replace,
            allowThirdPartyFixup: true,
            targetTab,
            elementIndex: newIndex,
            tabGroup,
            userContextId,
            triggeringPrincipal,
            csp,
          });
        })();
      }

      if (draggedTab) {
        delete draggedTab._dragData;
      }
    }

    on_dragend(event) {
      var dt = event.dataTransfer;
      var draggedTab = dt.mozGetDataAt(TAB_DROP_TYPE, 0);

      // Prevent this code from running if a tabdrop animation is
      // running since calling finishAnimateTabMove would clear
      // any CSS transition that is running.
      if (draggedTab.hasAttribute("tabdrop-samewindow")) {
        return;
      }

      this.finishMoveTogetherSelectedTabs(draggedTab);
      this.finishAnimateTabMove();
      this.#expandGroupOnDrop(draggedTab);

      if (
        dt.mozUserCancelled ||
        dt.dropEffect != "none" ||
        !Services.prefs.getBoolPref("browser.tabs.allowTabDetach") ||
        this._isCustomizing
      ) {
        delete draggedTab._dragData;
        return;
      }

      // Disable detach within the browser toolbox
      let [tabAxisPos, tabAxisStart, tabAxisEnd] = this.verticalMode
        ? [event.screenY, window.screenY, window.screenY + window.outerHeight]
        : [event.screenX, window.screenX, window.screenX + window.outerWidth];

      if (tabAxisPos > tabAxisStart && tabAxisPos < tabAxisEnd) {
        // also avoid detaching if the tab was dropped too close to
        // the tabbar (half a tab)
        let rect = window.windowUtils.getBoundsWithoutFlushing(
          this.arrowScrollbox
        );
        let crossAxisPos = this.verticalMode ? event.screenX : event.screenY;
        let crossAxisStart, crossAxisEnd;
        if (this.verticalMode) {
          if (
            (RTL_UI && this._sidebarPositionStart) ||
            (!RTL_UI && !this._sidebarPositionStart)
          ) {
            crossAxisStart =
              window.mozInnerScreenX + rect.right - 1.5 * rect.width;
            crossAxisEnd = window.screenX + window.outerWidth;
          } else {
            crossAxisStart = window.screenX;
            crossAxisEnd =
              window.mozInnerScreenX + rect.left + 1.5 * rect.width;
          }
        } else {
          crossAxisStart = window.screenY;
          crossAxisEnd = window.mozInnerScreenY + rect.top + 1.5 * rect.height;
        }
        if (crossAxisPos > crossAxisStart && crossAxisPos < crossAxisEnd) {
          return;
        }
      }

      // screen.availLeft et. al. only check the screen that this window is on,
      // but we want to look at the screen the tab is being dropped onto.
      var screen = event.screen;
      var availX = {},
        availY = {},
        availWidth = {},
        availHeight = {};
      // Get available rect in desktop pixels.
      screen.GetAvailRectDisplayPix(availX, availY, availWidth, availHeight);
      availX = availX.value;
      availY = availY.value;
      availWidth = availWidth.value;
      availHeight = availHeight.value;

      // Compute the final window size in desktop pixels ensuring that the new
      // window entirely fits within `screen`.
      let ourCssToDesktopScale =
        window.devicePixelRatio / window.desktopToDeviceScale;
      let screenCssToDesktopScale =
        screen.defaultCSSScaleFactor / screen.contentsScaleFactor;

      // NOTE(emilio): Multiplying the sizes here for screenCssToDesktopScale
      // means that we'll try to create a window that has the same amount of CSS
      // pixels than our current window, not the same amount of device pixels.
      // There are pros and cons of both conversions, though this matches the
      // pre-existing intended behavior.
      var winWidth = Math.min(
        window.outerWidth * screenCssToDesktopScale,
        availWidth
      );
      var winHeight = Math.min(
        window.outerHeight * screenCssToDesktopScale,
        availHeight
      );

      // This is slightly tricky: _dragData.offsetX/Y is an offset in CSS
      // pixels. Since we're doing the sizing above based on those, we also need
      // to apply the offset with pixels relative to the screen's scale rather
      // than our scale.
      var left = Math.min(
        Math.max(
          event.screenX * ourCssToDesktopScale -
            draggedTab._dragData.offsetX * screenCssToDesktopScale,
          availX
        ),
        availX + availWidth - winWidth
      );
      var top = Math.min(
        Math.max(
          event.screenY * ourCssToDesktopScale -
            draggedTab._dragData.offsetY * screenCssToDesktopScale,
          availY
        ),
        availY + availHeight - winHeight
      );

      // Convert back left and top to our CSS pixel space.
      left /= ourCssToDesktopScale;
      top /= ourCssToDesktopScale;

      delete draggedTab._dragData;

      if (gBrowser.tabs.length == 1) {
        // resize _before_ move to ensure the window fits the new screen.  if
        // the window is too large for its screen, the window manager may do
        // automatic repositioning.
        //
        // Since we're resizing before moving to our new screen, we need to use
        // sizes relative to the current screen. If we moved, then resized, then
        // we could avoid this special-case and share this with the else branch
        // below...
        winWidth /= ourCssToDesktopScale;
        winHeight /= ourCssToDesktopScale;

        window.resizeTo(winWidth, winHeight);
        window.moveTo(left, top);
        window.focus();
      } else {
        // We're opening a new window in a new screen, so make sure to use sizes
        // relative to the new screen.
        winWidth /= screenCssToDesktopScale;
        winHeight /= screenCssToDesktopScale;

        let props = { screenX: left, screenY: top, suppressanimation: 1 };
        gBrowser.replaceTabsWithWindow(draggedTab, props);
      }
      event.stopPropagation();
    }

    on_dragleave(event) {
      this.#dragTime = 0;

      // This does not work at all (see bug 458613)
      var target = event.relatedTarget;
      while (target && target != this) {
        target = target.parentNode;
      }
      if (target) {
        return;
      }

      this._tabDropIndicator.hidden = true;
      event.stopPropagation();
    }

    on_wheel(event) {
      if (
        Services.prefs.getBoolPref("toolkit.tabbox.switchByScrolling", false)
      ) {
        event.stopImmediatePropagation();
      }
    }

    on_overflow(event) {
      // Ignore overflow events from nested scrollable elements
      if (event.target != this.arrowScrollbox) {
        return;
      }

      this.toggleAttribute("overflow", true);
      this._positionPinnedTabs();
      this._updateCloseButtons();
      this._handleTabSelect(true);

      document
        .getElementById("tab-preview-panel")
        ?.setAttribute("rolluponmousewheel", true);
    }

    on_underflow(event) {
      // Ignore underflow events:
      // - from nested scrollable elements
      // - corresponding to an overflow event that we ignored
      if (event.target != this.arrowScrollbox || !this.overflowing) {
        return;
      }

      this.removeAttribute("overflow");

      if (this._lastTabClosedByMouse) {
        this._expandSpacerBy(this._scrollButtonWidth);
      }

      for (let tab of gBrowser._removingTabs) {
        gBrowser.removeTab(tab);
      }

      this._positionPinnedTabs();
      this._updateCloseButtons();

      document
        .getElementById("tab-preview-panel")
        ?.removeAttribute("rolluponmousewheel");
    }

    on_contextmenu(event) {
      // When pressing the context menu key (as opposed to right-clicking)
      // while a tab group label has aria focus (as opposed to DOM focus),
      // open the tab group context menu as if the label had DOM focus.
      // The button property is used to differentiate between key and mouse.
      if (event.button == 0 && isTabGroupLabel(this.ariaFocusedItem)) {
        gBrowser.tabGroupMenu.openEditModal(this.ariaFocusedItem.group);
        event.preventDefault();
      }
    }

    get emptyTabTitle() {
      // Normal tab title is used also in the permanent private browsing mode.
      const l10nId =
        PrivateBrowsingUtils.isWindowPrivate(window) &&
        !Services.prefs.getBoolPref("browser.privatebrowsing.autostart")
          ? "tabbrowser-empty-private-tab-title"
          : "tabbrowser-empty-tab-title";
      return gBrowser.tabLocalization.formatValueSync(l10nId);
    }

    get tabbox() {
      return document.getElementById("tabbrowser-tabbox");
    }

    get newTabButton() {
      return this.querySelector("#tabs-newtab-button");
    }

    get verticalMode() {
      return this.getAttribute("orient") == "vertical";
    }

    get expandOnHover() {
      return this._sidebarVisibility == "expand-on-hover";
    }

    get #rtlMode() {
      return !this.verticalMode && RTL_UI;
    }

    get overflowing() {
      return this.hasAttribute("overflow");
    }

    #allTabs;
    get allTabs() {
      if (this.#allTabs) {
        return this.#allTabs;
      }
      let children = Array.from(this.arrowScrollbox.children);
      // remove arrowScrollbox periphery element
      children.pop();

      // explode tab groups
      // Iterate backwards over the array to preserve indices while we modify
      // things in place
      for (let i = children.length - 1; i >= 0; i--) {
        if (children[i].tagName == "tab-group") {
          children.splice(i, 1, ...children[i].tabs);
        }
      }

      this.#allTabs = [
        ...this.verticalPinnedTabsContainer.children,
        ...children,
      ];
      return this.#allTabs;
    }

    get allGroups() {
      let children = Array.from(this.arrowScrollbox.children);
      return children.filter(node => node.tagName == "tab-group");
    }

    /**
     * Returns all tabs in the current window, including hidden tabs and tabs
     * in collapsed groups, but excluding closing tabs and the Firefox View tab.
     */
    get openTabs() {
      if (!this.#openTabs) {
        this.#openTabs = this.allTabs.filter(tab => tab.isOpen);
      }
      return this.#openTabs;
    }
    #openTabs;

    /**
     * Same as `openTabs` but excluding hidden tabs and tabs in collapsed groups.
     */
    get visibleTabs() {
      if (!this.#visibleTabs) {
        this.#visibleTabs = this.openTabs.filter(tab => tab.visible);
      }
      return this.#visibleTabs;
    }
    #visibleTabs;

    /**
     * @returns {boolean} true if the keyboard focus is on the active tab
     */
    get tablistHasFocus() {
      return this.hasAttribute("tablist-has-focus");
    }

    /**
     * @param {boolean} hasFocus true if the keyboard focus is on the active tab
     */
    set tablistHasFocus(hasFocus) {
      this.toggleAttribute("tablist-has-focus", hasFocus);
    }

    /** @typedef {MozTabbrowserTab|MozTextLabel} FocusableItem */

    /** @type {FocusableItem[]} */
    #focusableItems;

    /**
     * @returns {FocusableItem[]}
     * @override tabbox.js:TabsBase
     */
    get ariaFocusableItems() {
      if (this.#focusableItems) {
        return this.#focusableItems;
      }

      let elementIndex = 0;
      let verticalPinnedTabsContainer = document.getElementById(
        "vertical-pinned-tabs-container"
      );
      for (let i = 0; i < verticalPinnedTabsContainer.childElementCount; i++) {
        verticalPinnedTabsContainer.children[i].elementIndex = elementIndex++;
      }
      let children = Array.from(this.arrowScrollbox.children);

      let focusableItems = [];
      for (let child of children) {
        if (isTab(child) && child.visible) {
          child.elementIndex = elementIndex++;
          focusableItems.push(child);
        } else if (isTabGroup(child)) {
          child.labelElement.elementIndex = elementIndex++;
          focusableItems.push(child.labelElement);
          if (!child.collapsed) {
            let visibleTabsInGroup = child.tabs.filter(tab => tab.visible);
            visibleTabsInGroup.forEach(tab => {
              tab.elementIndex = elementIndex++;
            });
            focusableItems.push(...visibleTabsInGroup);
          }
        }
      }

      this.#focusableItems = [
        ...verticalPinnedTabsContainer.children,
        ...focusableItems,
      ];

      return this.#focusableItems;
    }

    _invalidateCachedTabs() {
      this.#allTabs = null;
      this._invalidateCachedVisibleTabs();
    }

    _invalidateCachedVisibleTabs() {
      this.#openTabs = null;
      this.#visibleTabs = null;
      // Focusable items must also be visible, but they do not depend on
      // this.#visibleTabs, so changes to visible tabs need to also invalidate
      // the focusable items cache
      this.#focusableItems = null;
    }

    #isContainerVerticalPinnedGrid(tab) {
      return (
        this.verticalMode &&
        tab.hasAttribute("pinned") &&
        this.hasAttribute("expanded") &&
        !this.expandOnHover
      );
    }

    appendChild(tab) {
      return this.insertBefore(tab, null);
    }

    insertBefore(tab, node) {
      if (!this.arrowScrollbox) {
        throw new Error("Shouldn't call this without arrowscrollbox");
      }

      if (node == null) {
        // We have a container for non-tab elements at the end of the scrollbox.
        node = this.arrowScrollbox.lastChild;
      }

      node.before(tab);

      if (this.#mustUpdateTabMinHeight) {
        this.#updateTabMinHeight();
      }
    }

    #updateTabMinWidth(val) {
      this.style.setProperty(
        "--tab-min-width-pref",
        (val ?? this._tabMinWidthPref) + "px"
      );
    }

    #updateTabMinHeight() {
      if (!this.verticalMode || !window.toolbar.visible) {
        this.#mustUpdateTabMinHeight = false;
        return;
      }

      // Find at least one tab we can scroll to.
      let firstScrollableTab = this.visibleTabs.find(
        this.arrowScrollbox._canScrollToElement
      );

      if (!firstScrollableTab) {
        // If not, we're in a pickle. We should never get here except if we
        // also don't use the outcome of this work (because there's nothing to
        // scroll so we don't care about the scrollbox size).
        // So just set a flag so we re-run once we do have a new tab.
        this.#mustUpdateTabMinHeight = true;
        return;
      }

      let { height } =
        window.windowUtils.getBoundsWithoutFlushing(firstScrollableTab);

      // Use the current known height or a sane default.
      this.#tabMinHeight = height || 36;

      // The height we got may be incorrect if a flush is pending so re-check it after
      // a flush completes.
      window
        .promiseDocumentFlushed(() => {})
        .then(
          () => {
            height =
              window.windowUtils.getBoundsWithoutFlushing(
                firstScrollableTab
              ).height;

            if (height) {
              this.#tabMinHeight = height;
            }
          },
          () => {
            /* ignore errors */
          }
        );
    }

    get _isCustomizing() {
      return document.documentElement.hasAttribute("customizing");
    }

    // This overrides the TabsBase _selectNewTab method so that we can
    // potentially interrupt keyboard tab switching when sharing the
    // window or screen.
    _selectNewTab(aNewTab, aFallbackDir, aWrap) {
      if (!gSharedTabWarning.willShowSharedTabWarning(aNewTab)) {
        super._selectNewTab(aNewTab, aFallbackDir, aWrap);
      }
    }

    observe(aSubject, aTopic) {
      switch (aTopic) {
        case "nsPref:changed":
          // This is has to deal with changes in
          // privacy.userContext.enabled and
          // privacy.userContext.newTabContainerOnLeftClick.enabled.
          let containersEnabled =
            Services.prefs.getBoolPref("privacy.userContext.enabled") &&
            !PrivateBrowsingUtils.isWindowPrivate(window);

          // This pref won't change so often, so just recreate the menu.
          const newTabLeftClickOpensContainersMenu = Services.prefs.getBoolPref(
            "privacy.userContext.newTabContainerOnLeftClick.enabled"
          );

          // There are separate "new tab" buttons for horizontal tabs toolbar, vertical tabs and
          // for when the tab strip is overflowed (which is shared by vertical and horizontal tabs);
          // Attach the long click popup to all of them.
          const newTab = document.getElementById("new-tab-button");
          const newTab2 = this.newTabButton;
          const newTabVertical = document.getElementById(
            "vertical-tabs-newtab-button"
          );

          for (let parent of [newTab, newTab2, newTabVertical]) {
            if (!parent) {
              continue;
            }

            parent.removeAttribute("type");
            if (parent.menupopup) {
              parent.menupopup.remove();
            }

            if (containersEnabled) {
              parent.setAttribute("context", "new-tab-button-popup");

              let popup = document
                .getElementById("new-tab-button-popup")
                .cloneNode(true);
              popup.removeAttribute("id");
              popup.className = "new-tab-popup";
              popup.setAttribute("position", "after_end");
              popup.addEventListener("popupshowing", CreateContainerTabMenu);
              parent.prepend(popup);
              parent.setAttribute("type", "menu");
              // Update tooltip text
              nodeToTooltipMap[parent.id] = newTabLeftClickOpensContainersMenu
                ? "newTabAlwaysContainer.tooltip"
                : "newTabContainer.tooltip";
            } else {
              nodeToTooltipMap[parent.id] = "newTabButton.tooltip";
              parent.removeAttribute("context", "new-tab-button-popup");
            }
            // evict from tooltip cache
            gDynamicTooltipCache.delete(parent.id);

            // If containers and press-hold container menu are both used,
            // add to gClickAndHoldListenersOnElement; otherwise, remove.
            if (containersEnabled && !newTabLeftClickOpensContainersMenu) {
              gClickAndHoldListenersOnElement.add(parent);
            } else {
              gClickAndHoldListenersOnElement.remove(parent);
            }
          }

          break;
      }
    }

    _updateCloseButtons() {
      if (this.overflowing) {
        // Tabs are at their minimum widths.
        this.setAttribute("closebuttons", "activetab");
        return;
      }

      if (this._closeButtonsUpdatePending) {
        return;
      }
      this._closeButtonsUpdatePending = true;

      // Wait until after the next paint to get current layout data from
      // getBoundsWithoutFlushing.
      window.requestAnimationFrame(() => {
        window.requestAnimationFrame(() => {
          this._closeButtonsUpdatePending = false;

          // The scrollbox may have started overflowing since we checked
          // overflow earlier, so check again.
          if (this.overflowing) {
            this.setAttribute("closebuttons", "activetab");
            return;
          }

          // Check if tab widths are below the threshold where we want to
          // remove close buttons from background tabs so that people don't
          // accidentally close tabs by selecting them.
          let rect = ele => {
            return window.windowUtils.getBoundsWithoutFlushing(ele);
          };
          let tab = this.visibleTabs[gBrowser.pinnedTabCount];
          if (tab && rect(tab).width <= this._tabClipWidth) {
            this.setAttribute("closebuttons", "activetab");
          } else {
            this.removeAttribute("closebuttons");
          }
        });
      });
    }

    _handleTabSelect(aInstant) {
      let selectedTab = this.selectedItem;
      if (this.overflowing) {
        this.arrowScrollbox.ensureElementIsVisible(selectedTab, aInstant);
      }

      selectedTab._notselectedsinceload = false;
    }

    #keepTabSizeLocked;

    /**
     * Try to keep the active tab's close button under the mouse cursor
     */
    _lockTabSizing(aClosingTab, aTabWidth) {
      if (this.verticalMode) {
        return;
      }

      let tabs = this.visibleTabs;
      let numPinned = gBrowser.pinnedTabCount;

      if (tabs.length <= numPinned) {
        // There are no unpinned tabs left.
        return;
      }

      let isEndTab = aClosingTab && aClosingTab._tPos > tabs.at(-1)._tPos;

      if (!this._tabDefaultMaxWidth) {
        this._tabDefaultMaxWidth = parseFloat(
          window.getComputedStyle(tabs[numPinned]).maxWidth
        );
      }
      this._lastTabClosedByMouse = true;
      this._scrollButtonWidth = window.windowUtils.getBoundsWithoutFlushing(
        this.arrowScrollbox._scrollButtonDown
      ).width;
      if (aTabWidth === undefined) {
        aTabWidth = window.windowUtils.getBoundsWithoutFlushing(
          tabs[numPinned]
        ).width;
      }

      if (this.overflowing) {
        // Don't need to do anything if we're in overflow mode and aren't scrolled
        // all the way to the right, or if we're closing the last tab.
        if (isEndTab || !this.arrowScrollbox.hasAttribute("scrolledtoend")) {
          return;
        }
        // If the tab has an owner that will become the active tab, the owner will
        // be to the left of it, so we actually want the left tab to slide over.
        // This can't be done as easily in non-overflow mode, so we don't bother.
        if (aClosingTab?.owner) {
          return;
        }
        this._expandSpacerBy(aTabWidth);
      } /* non-overflow mode */ else {
        if (isEndTab && !this._hasTabTempMaxWidth) {
          // Locking is neither in effect nor needed, so let tabs expand normally.
          return;
        }
        // Force tabs to stay the same width, unless we're closing the last tab,
        // which case we need to let them expand just enough so that the overall
        // tabbar width is the same.
        if (isEndTab) {
          let numNormalTabs = tabs.length - numPinned;
          aTabWidth = (aTabWidth * (numNormalTabs + 1)) / numNormalTabs;
          if (aTabWidth > this._tabDefaultMaxWidth) {
            aTabWidth = this._tabDefaultMaxWidth;
          }
        }
        aTabWidth += "px";
        let tabsToReset = [];
        for (let i = numPinned; i < tabs.length; i++) {
          let tab = tabs[i];
          tab.style.setProperty("max-width", aTabWidth, "important");
          if (!isEndTab) {
            // keep tabs the same width
            tab.style.transition = "none";
            tabsToReset.push(tab);
          }
        }

        if (tabsToReset.length) {
          window
            .promiseDocumentFlushed(() => {})
            .then(() => {
              window.requestAnimationFrame(() => {
                for (let tab of tabsToReset) {
                  tab.style.transition = "";
                }
              });
            });
        }

        this._hasTabTempMaxWidth = true;
        gBrowser.addEventListener("mousemove", this);
        window.addEventListener("mouseout", this);
      }
    }

    _expandSpacerBy(pixels) {
      let spacer = this._closingTabsSpacer;
      spacer.style.width = parseFloat(spacer.style.width) + pixels + "px";
      this.toggleAttribute("using-closing-tabs-spacer", true);
      gBrowser.addEventListener("mousemove", this);
      window.addEventListener("mouseout", this);
    }

    _unlockTabSizing() {
      if (this.#keepTabSizeLocked) {
        return;
      }

      gBrowser.removeEventListener("mousemove", this);
      window.removeEventListener("mouseout", this);

      if (this._hasTabTempMaxWidth) {
        this._hasTabTempMaxWidth = false;
        // Only visible tabs have their sizes locked, but those visible tabs
        // could become invisible before being unlocked (e.g. by being inside
        // of a collapsing tab group), so it's better to reset all tabs.
        let tabs = this.allTabs;
        for (let i = 0; i < tabs.length; i++) {
          tabs[i].style.maxWidth = "";
        }
      }

      if (this.hasAttribute("using-closing-tabs-spacer")) {
        this.removeAttribute("using-closing-tabs-spacer");
        this._closingTabsSpacer.style.width = 0;
      }
    }

    uiDensityChanged() {
      this._positionPinnedTabs();
      this._updateCloseButtons();
      this.#updateTabMinHeight();
      this._handleTabSelect(true);
    }

    _updateVerticalPinnedTabs() {
      // Move pinned tabs to another container when the tabstrip is toggled to vertical
      // and when session restore code calls _positionPinnedTabs; update styling whenever
      // the number of pinned tabs changes.
      let verticalTabsContainer = document.getElementById(
        "vertical-pinned-tabs-container"
      );
      let numPinned = gBrowser.pinnedTabCount;

      if (gBrowser.pinnedTabCount !== verticalTabsContainer.children.length) {
        let tabs = this.visibleTabs;
        for (let i = 0; i < numPinned; i++) {
          tabs[i].style.marginInlineStart = "";
          verticalTabsContainer.appendChild(tabs[i]);
        }
      }

      this.style.removeProperty("--tab-overflow-pinned-tabs-width");
    }

    _resetVerticalPinnedTabs() {
      let verticalTabsContainer = document.getElementById(
        "vertical-pinned-tabs-container"
      );

      if (!verticalTabsContainer.children.length) {
        return;
      }
      for (const child of Array.from(
        verticalTabsContainer.children
      ).reverse()) {
        this.arrowScrollbox.prepend(child);
      }
    }

    _positionPinnedTabs() {
      let tabs = this.visibleTabs;
      let numPinned = gBrowser.pinnedTabCount;
      let absPositionHorizontalTabs =
        this.overflowing && tabs.length > numPinned && numPinned > 0;

      this.toggleAttribute("haspinnedtabs", !!numPinned);
      this.toggleAttribute("positionpinnedtabs", absPositionHorizontalTabs);

      if (this.verticalMode) {
        this._updateVerticalPinnedTabs();
      } else if (absPositionHorizontalTabs) {
        let layoutData = this._pinnedTabsLayoutCache;
        let uiDensity = document.documentElement.getAttribute("uidensity");
        if (!layoutData || layoutData.uiDensity != uiDensity) {
          let arrowScrollbox = this.arrowScrollbox;
          layoutData = this._pinnedTabsLayoutCache = {
            uiDensity,
            pinnedTabWidth: tabs[0].getBoundingClientRect().width,
            scrollStartOffset:
              arrowScrollbox.scrollbox.getBoundingClientRect().left -
              arrowScrollbox.getBoundingClientRect().left +
              parseFloat(
                getComputedStyle(arrowScrollbox.scrollbox).paddingInlineStart
              ),
          };
        }

        let width = 0;
        for (let i = numPinned - 1; i >= 0; i--) {
          let tab = tabs[i];
          width += layoutData.pinnedTabWidth;
          tab.style.setProperty(
            "margin-inline-start",
            -(width + layoutData.scrollStartOffset) + "px",
            "important"
          );
        }
        this.style.setProperty(
          "--tab-overflow-pinned-tabs-width",
          width + "px"
        );
      } else {
        for (let i = 0; i < numPinned; i++) {
          let tab = tabs[i];
          tab.style.marginInlineStart = "";
        }

        this.style.removeProperty("--tab-overflow-pinned-tabs-width");
      }

      if (this._lastNumPinned != numPinned) {
        this._lastNumPinned = numPinned;
        this._handleTabSelect(true);
      }
    }

    #animateExpandedPinnedTabMove(event) {
      let draggedTab = event.dataTransfer.mozGetDataAt(TAB_DROP_TYPE, 0);
      let dragData = draggedTab._dragData;
      let movingTabs = dragData.movingTabs;

      dragData.animLastScreenX ??= dragData.screenX;
      dragData.animLastScreenY ??= dragData.screenY;

      let screenX = event.screenX;
      let screenY = event.screenY;

      if (
        screenY == dragData.animLastScreenY &&
        screenX == dragData.animLastScreenX
      ) {
        return;
      }

      let tabs = this.visibleTabs.slice(0, gBrowser.pinnedTabCount);

      let directionX = screenX > dragData.animLastScreenX;
      let directionY = screenY > dragData.animLastScreenY;
      dragData.animLastScreenY = screenY;
      dragData.animLastScreenX = screenX;

      let { width: tabWidth, height: tabHeight } =
        draggedTab.getBoundingClientRect();
      let shiftSizeX = tabWidth * movingTabs.length;
      let shiftSizeY = tabHeight;
      dragData.tabWidth = tabWidth;
      dragData.tabHeight = tabHeight;

      // Move the dragged tab based on the mouse position.
      let firstTabInRow;
      let lastTabInRow;
      let lastTab = tabs.at(-1);
      if (RTL_UI) {
        firstTabInRow =
          tabs.length >= this.#maxTabsPerRow
            ? tabs[this.#maxTabsPerRow - 1]
            : lastTab;
        lastTabInRow = tabs[0];
      } else {
        firstTabInRow = tabs[0];
        lastTabInRow =
          tabs.length >= this.#maxTabsPerRow
            ? tabs[this.#maxTabsPerRow - 1]
            : lastTab;
      }
      let lastMovingTabScreenX = movingTabs.at(-1).screenX;
      let lastMovingTabScreenY = movingTabs.at(-1).screenY;
      let firstMovingTabScreenX = movingTabs[0].screenX;
      let firstMovingTabScreenY = movingTabs[0].screenY;
      let translateX = screenX - dragData.screenX;
      let translateY = screenY - dragData.screenY;
      translateY +=
        this.verticalPinnedTabsContainer.scrollPosition - dragData.scrollPos;
      let firstBoundX = firstTabInRow.screenX - firstMovingTabScreenX;
      let firstBoundY = firstTabInRow.screenY - firstMovingTabScreenY;
      let lastBoundX =
        lastTabInRow.screenX +
        lastTabInRow.getBoundingClientRect().width -
        (lastMovingTabScreenX + tabWidth);
      let lastBoundY =
        lastTab.screenY +
        lastTab.getBoundingClientRect().height -
        (lastMovingTabScreenY + tabHeight);
      translateX = Math.min(Math.max(translateX, firstBoundX), lastBoundX);
      translateY = Math.min(Math.max(translateY, firstBoundY), lastBoundY);

      for (let tab of movingTabs) {
        tab.style.transform = `translate(${translateX}px, ${translateY}px)`;
      }

      dragData.translateX = translateX;
      dragData.translateY = translateY;

      // Determine what tab we're dragging over.
      // * Single tab dragging: Point of reference is the center of the dragged tab. If that
      //   point touches a background tab, the dragged tab would take that
      //   tab's position when dropped.
      // * Multiple tabs dragging: All dragged tabs are one "giant" tab with two
      //   points of reference (center of tabs on the extremities). When
      //   mouse is moving from top to bottom, the bottom reference gets activated,
      //   otherwise the top reference will be used. Everything else works the same
      //   as single tab dragging.
      // * We're doing a binary search in order to reduce the amount of
      //   tabs we need to check.

      tabs = tabs.filter(t => !movingTabs.includes(t) || t == draggedTab);
      let firstTabCenterX = firstMovingTabScreenX + translateX + tabWidth / 2;
      let lastTabCenterX = lastMovingTabScreenX + translateX + tabWidth / 2;
      let tabCenterX = directionX ? lastTabCenterX : firstTabCenterX;
      let firstTabCenterY = firstMovingTabScreenY + translateY + tabHeight / 2;
      let lastTabCenterY = lastMovingTabScreenY + translateY + tabHeight / 2;
      let tabCenterY = directionY ? lastTabCenterY : firstTabCenterY;

      let shiftNumber = this.#maxTabsPerRow - movingTabs.length;

      let getTabShift = (tab, dropIndex) => {
        if (tab._tPos < draggedTab._tPos && tab._tPos >= dropIndex) {
          // If tab is at the end of a row, shift back and down
          let tabRow = Math.ceil((tab._tPos + 1) / this.#maxTabsPerRow);
          let shiftedTabRow = Math.ceil(
            (tab._tPos + 1 + movingTabs.length) / this.#maxTabsPerRow
          );
          if (tab._tPos && tabRow != shiftedTabRow) {
            return [
              RTL_UI ? tabWidth * shiftNumber : -tabWidth * shiftNumber,
              shiftSizeY,
            ];
          }
          return [RTL_UI ? -shiftSizeX : shiftSizeX, 0];
        }
        if (tab._tPos > draggedTab._tPos && tab._tPos < dropIndex) {
          // If tab is not index 0 and at the start of a row, shift across and up
          let tabRow = Math.floor(tab._tPos / this.#maxTabsPerRow);
          let shiftedTabRow = Math.floor(
            (tab._tPos - movingTabs.length) / this.#maxTabsPerRow
          );
          if (tab._tPos && tabRow != shiftedTabRow) {
            return [
              RTL_UI ? -tabWidth * shiftNumber : tabWidth * shiftNumber,
              -shiftSizeY,
            ];
          }
          return [RTL_UI ? shiftSizeX : -shiftSizeX, 0];
        }
        return [0, 0];
      };

      let low = 0;
      let high = tabs.length - 1;
      let newIndex = -1;
      let oldIndex = dragData.animDropIndex ?? movingTabs[0]._tPos;
      while (low <= high) {
        let mid = Math.floor((low + high) / 2);
        if (tabs[mid] == draggedTab && ++mid > high) {
          break;
        }
        let [shiftX, shiftY] = getTabShift(tabs[mid], oldIndex);
        screenX = tabs[mid].screenX + shiftX;
        screenY = tabs[mid].screenY + shiftY;

        if (screenY + tabHeight < tabCenterY) {
          low = mid + 1;
        } else if (screenY > tabCenterY) {
          high = mid - 1;
        } else if (screenX > tabCenterX) {
          high = mid - 1;
        } else if (screenX + tabWidth < tabCenterX) {
          low = mid + 1;
        } else {
          newIndex = tabs[mid]._tPos;
          break;
        }
      }

      if (newIndex >= oldIndex && newIndex < tabs.length) {
        newIndex++;
      }

      if (newIndex < 0) {
        newIndex = oldIndex;
      }

      if (newIndex == dragData.animDropIndex) {
        return;
      }
      dragData.animDropIndex = newIndex;
      dragData.dropElement = tabs[newIndex];
      dragData.dropBefore = newIndex < tabs.length;

      // Shift background tabs to leave a gap where the dragged tab
      // would currently be dropped.
      for (let tab of tabs) {
        if (tab != draggedTab) {
          let [shiftX, shiftY] = getTabShift(tab, newIndex);
          tab.style.transform =
            shiftX || shiftY ? `translate(${shiftX}px, ${shiftY}px)` : "";
        }
      }
    }

    _animateTabMove(event) {
      let draggedTab = event.dataTransfer.mozGetDataAt(TAB_DROP_TYPE, 0);
      let dragData = draggedTab._dragData;
      let movingTabs = dragData.movingTabs;

      dragData.animLastScreenPos ??= this.verticalMode
        ? dragData.screenY
        : dragData.screenX;
      let screen = this.verticalMode ? event.screenY : event.screenX;
      if (screen == dragData.animLastScreenPos) {
        return;
      }
      let screenForward = screen > dragData.animLastScreenPos;
      dragData.animLastScreenPos = screen;

      this.#clearDragOverCreateGroupTimer();

      let isPinned = draggedTab.pinned;
      let numPinned = gBrowser.pinnedTabCount;
      let tabs = this.ariaFocusableItems.slice(
        isPinned ? 0 : numPinned,
        isPinned ? numPinned : undefined
      );

      if (this.#rtlMode) {
        tabs.reverse();
        // Copy moving tabs array to avoid infinite reversing.
        movingTabs = [...movingTabs].reverse();
      }

      let bounds = ele => window.windowUtils.getBoundsWithoutFlushing(ele);
      let logicalForward = screenForward != this.#rtlMode;
      let screenAxis = this.verticalMode ? "screenY" : "screenX";
      let size = this.verticalMode ? "height" : "width";
      let translateAxis = this.verticalMode ? "translateY" : "translateX";
      let scrollDirection = this.verticalMode ? "scrollTop" : "scrollLeft";
      let { width: tabWidth, height: tabHeight } = bounds(draggedTab);
      let translateX = event.screenX - dragData.screenX;
      let translateY = event.screenY - dragData.screenY;

      dragData.tabWidth = tabWidth;
      dragData.tabHeight = tabHeight;
      dragData.translateX = translateX;
      dragData.translateY = translateY;

      // Move the dragged tab based on the mouse position.
      let firstTab = tabs[0];
      let lastTab = tabs.at(-1);
      let lastMovingTab = movingTabs.at(-1);
      let firstMovingTab = movingTabs[0];
      let endEdge = ele => ele[screenAxis] + bounds(ele)[size];
      let lastMovingTabScreen = endEdge(lastMovingTab);
      let firstMovingTabScreen = firstMovingTab[screenAxis];
      let shiftSize = lastMovingTabScreen - firstMovingTabScreen;
      let translate = screen - dragData[screenAxis];
      if (!isPinned) {
        translate +=
          this.arrowScrollbox.scrollbox[scrollDirection] - dragData.scrollPos;
      } else if (isPinned && this.verticalMode) {
        translate +=
          this.verticalPinnedTabsContainer.scrollPosition - dragData.scrollPos;
      }
      // Constrain the range over which the moving tabs can move:
      // - for pinned tabs, between the first and last pinned tab
      // - for unpinned tabs, between the first and last unpinned tab
      let firstBound = firstTab[screenAxis] - firstMovingTabScreen;
      let lastBound = endEdge(lastTab) - lastMovingTabScreen;
      translate = Math.min(Math.max(translate, firstBound), lastBound);

      for (let item of movingTabs) {
        if (isTabGroupLabel(item)) {
          // Shift the `.tab-group-label-container` to shift the label element.
          item = item.parentElement;
        }
        item.style.transform = `${translateAxis}(${translate}px)`;
      }

      dragData.translatePos = translate;

      tabs = tabs.filter(t => !movingTabs.includes(t) || t == draggedTab);

      /**
       * When the `draggedTab` is just starting to move, the `draggedTab` is in
       * its original location and the `dropElementIndex == draggedTab.elementIndex`.
       * Any tabs or tab group labels passed in as `item` will result in a 0 shift
       * because all of those items should also continue to appear in their original
       * locations.
       *
       * Once the `draggedTab` is more "backward" in the tab strip than its original
       * position, any tabs or tab group labels between the `draggedTab`'s original
       * `elementIndex` and the current `dropElementIndex` should shift "forward"
       * out of the way of the dragging tabs.
       *
       * When the `draggedTab` is more "forward" in the tab strip than its original
       * position, any tabs or tab group labels between the `draggedTab`'s original
       * `elementIndex` and the current `dropElementIndex` should shift "backward"
       * out of the way of the dragging tabs.
       *
       * @param {MozTabbrowserTab|MozTabbrowserTabGroup.label} item
       * @param {number} dropElementIndex
       * @returns {number}
       */
      let getTabShift = (item, dropElementIndex) => {
        if (
          item.elementIndex < draggedTab.elementIndex &&
          item.elementIndex >= dropElementIndex
        ) {
          return this.#rtlMode ? -shiftSize : shiftSize;
        }
        if (
          item.elementIndex > draggedTab.elementIndex &&
          item.elementIndex < dropElementIndex
        ) {
          return this.#rtlMode ? shiftSize : -shiftSize;
        }
        return 0;
      };

      let oldDropElementIndex =
        dragData.animDropElementIndex ?? movingTabs[0].elementIndex;

      /**
       * Returns the higher % by which one element overlaps another
       * in the tab strip.
       *
       * When element 1 is further forward in the tab strip:
       *
       *   p1            p2      p1+s1    p2+s2
       *    |             |        |        |
       *    ---------------------------------
       *    ========================
       *               s1
       *                  ===================
       *                           s2
       *                  ==========
       *                   overlap
       *
       * When element 2 is further forward in the tab strip:
       *
       *   p2            p1      p2+s2    p1+s1
       *    |             |        |        |
       *    ---------------------------------
       *    ========================
       *               s2
       *                  ===================
       *                           s1
       *                  ==========
       *                   overlap
       *
       * @param {number} p1
       *   Position (x or y value in screen coordinates) of element 1.
       * @param {number} s1
       *   Size (width or height) of element 1.
       * @param {number} p2
       *   Position (x or y value in screen coordinates) of element 2.
       * @param {number} s2
       *   Size (width or height) of element 1.
       * @returns {number}
       *   Percent between 0.0 and 1.0 (inclusive) of element 1 or element 2
       *   that is overlapped by the other element. If the elements have
       *   different sizes, then this returns the larger overlap percentage.
       */
      function greatestOverlap(p1, s1, p2, s2) {
        let overlapSize;
        if (p1 < p2) {
          // element 1 starts first
          overlapSize = p1 + s1 - p2;
        } else {
          // element 2 starts first
          overlapSize = p2 + s2 - p1;
        }

        // No overlap if size is <= 0
        if (overlapSize <= 0) {
          return 0;
        }

        // Calculate the overlap fraction from each element's perspective.
        let overlapPercent = Math.max(overlapSize / s1, overlapSize / s2);

        return Math.min(overlapPercent, 1);
      }

      /**
       * Determine what tab/tab group label we're dragging over.
       *
       * When dragging right or downwards, the reference point for overlap is
       * the right or bottom edge of the most forward moving tab.
       *
       * When dragging left or upwards, the reference point for overlap is the
       * left or top edge of the most backward moving tab.
       *
       * @returns {Element|null}
       *   The tab or tab group label that should be used to visually shift tab
       *   strip elements out of the way of the dragged tab(s) during a drag
       *   operation. Note: this is not used to determine where the dragged
       *   tab(s) will be dropped, it is only used for visual animation at this
       *   time.
       */
      let getOverlappedElement = () => {
        let point =
          (screenForward ? lastMovingTabScreen : firstMovingTabScreen) +
          translate;
        let low = 0;
        let high = tabs.length - 1;
        while (low <= high) {
          let mid = Math.floor((low + high) / 2);
          if (tabs[mid] == draggedTab && ++mid > high) {
            break;
          }
          let element = tabs[mid];
          let elementForSize = isTabGroupLabel(element)
            ? element.parentElement
            : element;
          screen =
            elementForSize[screenAxis] +
            getTabShift(element, oldDropElementIndex);

          if (screen > point) {
            high = mid - 1;
          } else if (screen + bounds(elementForSize)[size] < point) {
            low = mid + 1;
          } else {
            return element;
          }
        }
        return null;
      };

      let dropElement = getOverlappedElement();
      if (!dropElement) {
        dropElement = this.ariaFocusableItems[oldDropElementIndex];
      }
      let newDropElementIndex = dropElement
        ? dropElement.elementIndex
        : oldDropElementIndex;
      let moveOverThreshold;
      let overlapPercent;
      let shouldCreateGroupOnDrop;
      let dropBefore;
      if (dropElement) {
        let dropElementForOverlap = isTabGroupLabel(dropElement)
          ? dropElement.parentElement
          : dropElement;

        let dropElementScreen = dropElementForOverlap[screenAxis];
        let dropElementPos =
          dropElementScreen + getTabShift(dropElement, oldDropElementIndex);
        let dropElementSize = bounds(dropElementForOverlap)[size];
        let firstMovingTabPos = firstMovingTabScreen + translate;
        overlapPercent = greatestOverlap(
          firstMovingTabPos,
          shiftSize,
          dropElementPos,
          dropElementSize
        );

        moveOverThreshold = gBrowser._tabGroupsEnabled
          ? Services.prefs.getIntPref(
              "browser.tabs.dragDrop.moveOverThresholdPercent"
            ) / 100
          : 0.5;
        moveOverThreshold = Math.min(1, Math.max(0, moveOverThreshold));
        let shouldMoveOver = overlapPercent > moveOverThreshold;
        if (logicalForward && shouldMoveOver) {
          newDropElementIndex++;
        } else if (!logicalForward && !shouldMoveOver) {
          newDropElementIndex++;
          if (newDropElementIndex > oldDropElementIndex) {
            // FIXME: Not quite sure what's going on here, but this check
            // prevents jittery back-and-forth movement of background tabs
            // in certain cases.
            newDropElementIndex = oldDropElementIndex;
          }
        }

        // Recalculate the overlap with the updated drop index for when the
        // drop element moves over.
        dropElementPos =
          dropElementScreen + getTabShift(dropElement, newDropElementIndex);
        overlapPercent = greatestOverlap(
          firstMovingTabPos,
          shiftSize,
          dropElementPos,
          dropElementSize
        );
        dropBefore = firstMovingTabPos < dropElementPos;
        if (this.#rtlMode) {
          dropBefore = !dropBefore;
        }

        // Constrain drop direction at the boundary between pinned and
        // unpinned tabs so that they don't mix together.
        let isOutOfBounds = isPinned
          ? dropElement.elementIndex >= numPinned
          : dropElement.elementIndex < numPinned;
        if (isOutOfBounds) {
          // Drop after last pinned tab
          dropElement = this.ariaFocusableItems[numPinned - 1];
          dropBefore = false;
        }
      }

      if (gBrowser._tabGroupsEnabled && isTab(draggedTab) && !isPinned) {
        let dragOverGroupingThreshold = 1 - moveOverThreshold;

        // When dragging tab(s) over an ungrouped tab, signal to the user
        // that dropping the tab(s) will create a new tab group.
        shouldCreateGroupOnDrop =
          dropElement != draggedTab &&
          isTab(dropElement) &&
          !dropElement?.group &&
          overlapPercent > dragOverGroupingThreshold;

        if (shouldCreateGroupOnDrop) {
          this.#dragOverCreateGroupTimer = setTimeout(
            () => this.#triggerDragOverCreateGroup(dragData, dropElement),
            Services.prefs.getIntPref(
              "browser.tabs.dragDrop.createGroup.delayMS"
            )
          );
        } else {
          this.removeAttribute("movingtab-createGroup");
          document
            .querySelector("[dragover-createGroup]")
            ?.removeAttribute("dragover-createGroup");
          delete dragData.shouldCreateGroupOnDrop;

          // Default to dropping into `dropElement`'s tab group, if it exists.
          let dropElementGroup = dropElement?.group;
          let colorCode = dropElementGroup?.color;

          if (
            isTab(dropElement) &&
            dropElementGroup &&
            dropElement == dropElementGroup.tabs.at(-1) &&
            !dropBefore &&
            overlapPercent < dragOverGroupingThreshold
          ) {
            // Dragging tab over the last tab of a tab group, but not enough
            // for it to drop into the tab group. Drop it after the tab group instead.
            dropElement = dropElementGroup;
            colorCode = undefined;
          } else if (isTabGroupLabel(dropElement)) {
            if (dropBefore) {
              // Dropping right before the tab group.
              dropElement = dropElementGroup;
              colorCode = undefined;
            } else if (dropElementGroup.collapsed) {
              // Dropping right after the collapsed tab group.
              dropElement = dropElementGroup;
              colorCode = undefined;
            } else {
              // Dropping right before the first tab in the tab group.
              dropElement = dropElementGroup.tabs[0];
              dropBefore = true;
            }
          }
          this.#setDragOverGroupColor(colorCode);
          this.toggleAttribute("movingtab-ungroup", !colorCode);
        }
      }

      if (
        newDropElementIndex == oldDropElementIndex &&
        dropBefore == dragData.dropBefore &&
        dropElement == dragData.dropElement
      ) {
        return;
      }

      dragData.dropElement = dropElement;
      dragData.dropBefore = dropBefore;
      dragData.animDropElementIndex = newDropElementIndex;

      // Shift background tabs to leave a gap where the dragged tab
      // would currently be dropped.
      for (let item of tabs) {
        if (item == draggedTab) {
          continue;
        }

        let shift = getTabShift(item, newDropElementIndex);
        let transform = shift ? `${translateAxis}(${shift}px)` : "";
        if (isTabGroupLabel(item)) {
          // Shift the `.tab-group-label-container` to shift the label element.
          item = item.parentElement;
        }
        item.style.transform = transform;
      }
    }

    /**
     * @param {object} dragData
     * @param {MozTabbrowserTab} dropElement
     */
    #triggerDragOverCreateGroup(dragData, dropElement) {
      this.#clearDragOverCreateGroupTimer();

      dragData.shouldCreateGroupOnDrop = true;

      this.toggleAttribute("movingtab-createGroup", true);
      this.removeAttribute("movingtab-ungroup");
      dropElement.toggleAttribute("dragover-createGroup", true);

      this.#setDragOverGroupColor(dragData.tabGroupCreationColor);
    }

    #clearDragOverCreateGroupTimer() {
      if (this.#dragOverCreateGroupTimer) {
        clearTimeout(this.#dragOverCreateGroupTimer);
        this.#dragOverCreateGroupTimer = 0;
      }
    }

    #setDragOverGroupColor(groupColorCode) {
      this.toggleAttribute("movingtab-addToGroup", groupColorCode);

      if (!groupColorCode) {
        this.style.removeProperty("--dragover-tab-group-color");
        this.style.removeProperty("--dragover-tab-group-color-invert");
        this.style.removeProperty("--dragover-tab-group-color-pale");
        return;
      }

      this.style.setProperty(
        "--dragover-tab-group-color",
        `var(--tab-group-color-${groupColorCode})`
      );
      this.style.setProperty(
        "--dragover-tab-group-color-invert",
        `var(--tab-group-color-${groupColorCode}-invert)`
      );
      this.style.setProperty(
        "--dragover-tab-group-color-pale",
        `var(--tab-group-color-${groupColorCode}-pale)`
      );
    }

    finishAnimateTabMove() {
      if (!this.#isMovingTab()) {
        return;
      }

      this.#setMovingTabMode(false);

      for (let item of this.ariaFocusableItems) {
        if (isTabGroupLabel(item)) {
          // Unshift the `.tab-group-label-container` to unshift the label element.
          item = item.parentElement;
        }
        item.style.transform = "";
        item.removeAttribute("dragover-createGroup");
      }
      this.removeAttribute("movingtab-createGroup");
      this.removeAttribute("movingtab-ungroup");
      this.#setDragOverGroupColor(null);
      this.#clearDragOverCreateGroupTimer();
    }

    /**
     * Move together all selected tabs around the tab in param.
     */
    #moveTogetherSelectedTabs(tab) {
      let draggedTabIndex = tab.elementIndex;
      let selectedTabs = gBrowser.selectedTabs;
      if (selectedTabs.some(t => t.pinned != tab.pinned)) {
        throw new Error(
          "Cannot move together a mix of pinned and unpinned tabs."
        );
      }
      let animate = !gReduceMotion;

      tab._moveTogetherSelectedTabsData = {
        finished: !animate,
      };

      let addAnimationData = (movingTab, isBeforeSelectedTab) => {
        let lowerIndex = Math.min(movingTab.elementIndex, draggedTabIndex) + 1;
        let higherIndex = Math.max(movingTab.elementIndex, draggedTabIndex);
        let middleItems = this.ariaFocusableItems
          .slice(lowerIndex, higherIndex)
          .filter(item => !item.multiselected);
        if (!middleItems.length) {
          // movingTab is already at the right position and thus doesn't need
          // to be animated.
          return;
        }

        movingTab._moveTogetherSelectedTabsData = {
          translatePos: 0,
          animate: true,
        };
        movingTab.toggleAttribute("multiselected-move-together", true);

        let postTransitionCleanup = () => {
          movingTab._moveTogetherSelectedTabsData.animate = false;
        };
        if (gReduceMotion) {
          postTransitionCleanup();
        } else {
          let onTransitionEnd = transitionendEvent => {
            if (
              transitionendEvent.propertyName != "transform" ||
              transitionendEvent.originalTarget != movingTab
            ) {
              return;
            }
            movingTab.removeEventListener("transitionend", onTransitionEnd);
            postTransitionCleanup();
          };

          movingTab.addEventListener("transitionend", onTransitionEnd);
        }

        // Add animation data for tabs and tab group labels between movingTab
        // (multiselected tab moving towards the dragged tab) and draggedTab. Those items
        // in the middle should move in the opposite direction of movingTab.

        let movingTabSize =
          movingTab.getBoundingClientRect()[
            this.verticalMode ? "height" : "width"
          ];

        for (let middleItem of middleItems) {
          if (isTab(middleItem)) {
            if (middleItem.pinned != movingTab.pinned) {
              // Don't mix pinned and unpinned tabs
              break;
            }
            if (middleItem.multiselected) {
              // Skip because this multiselected tab should
              // be shifted towards the dragged Tab.
              continue;
            }
          }
          if (isTabGroupLabel(middleItem)) {
            // Shift the `.tab-group-label-container` to shift the label element.
            middleItem = middleItem.parentElement;
          }
          let middleItemSize =
            middleItem.getBoundingClientRect()[
              this.verticalMode ? "height" : "width"
            ];

          if (!middleItem._moveTogetherSelectedTabsData?.translatePos) {
            middleItem._moveTogetherSelectedTabsData = { translatePos: 0 };
          }
          movingTab._moveTogetherSelectedTabsData.translatePos +=
            isBeforeSelectedTab ? middleItemSize : -middleItemSize;
          middleItem._moveTogetherSelectedTabsData.translatePos =
            isBeforeSelectedTab ? -movingTabSize : movingTabSize;

          middleItem.toggleAttribute("multiselected-move-together", true);
        }
      };

      let tabIndex = selectedTabs.indexOf(tab);

      // Animate left or top selected tabs
      for (let i = 0; i < tabIndex; i++) {
        let movingTab = selectedTabs[i];
        if (animate) {
          addAnimationData(movingTab, true);
        } else {
          gBrowser.moveTabBefore(movingTab, tab);
        }
      }

      // Animate right or bottom selected tabs
      for (let i = selectedTabs.length - 1; i > tabIndex; i--) {
        let movingTab = selectedTabs[i];
        if (animate) {
          addAnimationData(movingTab, false);
        } else {
          gBrowser.moveTabAfter(movingTab, tab);
        }
      }

      // Slide the relevant tabs to their new position.
      for (let item of this.ariaFocusableItems) {
        if (isTabGroupLabel(item)) {
          // Shift the `.tab-group-label-container` to shift the label element.
          item = item.parentElement;
        }
        if (item._moveTogetherSelectedTabsData?.translatePos) {
          let translatePos =
            (this.#rtlMode ? -1 : 1) *
            item._moveTogetherSelectedTabsData.translatePos;
          item.style.transform = `translate${
            this.verticalMode ? "Y" : "X"
          }(${translatePos}px)`;
        }
      }
    }

    finishMoveTogetherSelectedTabs(tab) {
      if (
        !tab._moveTogetherSelectedTabsData ||
        tab._moveTogetherSelectedTabsData.finished
      ) {
        return;
      }

      tab._moveTogetherSelectedTabsData.finished = true;

      let selectedTabs = gBrowser.selectedTabs;
      let tabIndex = selectedTabs.indexOf(tab);

      // Moving left or top tabs
      for (let i = 0; i < tabIndex; i++) {
        gBrowser.moveTabBefore(selectedTabs[i], tab);
      }

      // Moving right or bottom tabs
      for (let i = selectedTabs.length - 1; i > tabIndex; i--) {
        gBrowser.moveTabAfter(selectedTabs[i], tab);
      }

      for (let item of this.ariaFocusableItems) {
        if (isTabGroupLabel(item)) {
          // Shift the `.tab-group-label-container` to shift the label element.
          item = item.parentElement;
        }
        item.style.transform = "";
        item.removeAttribute("multiselected-move-together");
        delete item._moveTogetherSelectedTabsData;
      }
    }

    #isAnimatingMoveTogetherSelectedTabs() {
      for (let tab of gBrowser.selectedTabs) {
        if (tab._moveTogetherSelectedTabsData?.animate) {
          return true;
        }
      }
      return false;
    }

    handleEvent(aEvent) {
      switch (aEvent.type) {
        case "mouseout":
          // If the "related target" (the node to which the pointer went) is not
          // a child of the current document, the mouse just left the window.
          let relatedTarget = aEvent.relatedTarget;
          if (relatedTarget && relatedTarget.ownerDocument == document) {
            break;
          }
        // fall through
        case "mousemove":
          if (
            document.getElementById("tabContextMenu").state != "open" &&
            !this.#isMovingTab()
          ) {
            this._unlockTabSizing();
          }
          break;
        case "mouseleave":
          this.previewPanel?.deactivate();
          break;
        default:
          let methodName = `on_${aEvent.type}`;
          if (methodName in this) {
            this[methodName](aEvent);
          } else {
            throw new Error(`Unexpected event ${aEvent.type}`);
          }
      }
    }

    _notifyBackgroundTab(aTab) {
      if (aTab.pinned || !aTab.visible || !this.overflowing) {
        return;
      }

      this._lastTabToScrollIntoView = aTab;
      if (!this._backgroundTabScrollPromise) {
        this._backgroundTabScrollPromise = window
          .promiseDocumentFlushed(() => {
            let lastTabRect =
              this._lastTabToScrollIntoView.getBoundingClientRect();
            let selectedTab = this.selectedItem;
            if (selectedTab.pinned) {
              selectedTab = null;
            } else {
              selectedTab = selectedTab.getBoundingClientRect();
              selectedTab = {
                left: selectedTab.left,
                right: selectedTab.right,
                top: selectedTab.top,
                bottom: selectedTab.bottom,
              };
            }
            return [
              this._lastTabToScrollIntoView,
              this.arrowScrollbox.scrollClientRect,
              lastTabRect,
              selectedTab,
            ];
          })
          .then(([tabToScrollIntoView, scrollRect, tabRect, selectedRect]) => {
            // First off, remove the promise so we can re-enter if necessary.
            delete this._backgroundTabScrollPromise;
            // Then, if the layout info isn't for the last-scrolled-to-tab, re-run
            // the code above to get layout info for *that* tab, and don't do
            // anything here, as we really just want to run this for the last-opened tab.
            if (this._lastTabToScrollIntoView != tabToScrollIntoView) {
              this._notifyBackgroundTab(this._lastTabToScrollIntoView);
              return;
            }
            delete this._lastTabToScrollIntoView;
            // Is the new tab already completely visible?
            if (
              this.verticalMode
                ? scrollRect.top <= tabRect.top &&
                  tabRect.bottom <= scrollRect.bottom
                : scrollRect.left <= tabRect.left &&
                  tabRect.right <= scrollRect.right
            ) {
              return;
            }

            if (this.arrowScrollbox.smoothScroll) {
              // Can we make both the new tab and the selected tab completely visible?
              if (
                !selectedRect ||
                (this.verticalMode
                  ? Math.max(
                      tabRect.bottom - selectedRect.top,
                      selectedRect.bottom - tabRect.top
                    ) <= scrollRect.height
                  : Math.max(
                      tabRect.right - selectedRect.left,
                      selectedRect.right - tabRect.left
                    ) <= scrollRect.width)
              ) {
                this.arrowScrollbox.ensureElementIsVisible(tabToScrollIntoView);
                return;
              }

              let scrollPixels;
              if (this.verticalMode) {
                scrollPixels = tabRect.top - selectedRect.top;
              } else if (this.#rtlMode) {
                scrollPixels = selectedRect.right - scrollRect.right;
              } else {
                scrollPixels = selectedRect.left - scrollRect.left;
              }
              this.arrowScrollbox.scrollByPixels(scrollPixels);
            }

            if (!this._animateElement.hasAttribute("highlight")) {
              this._animateElement.toggleAttribute("highlight", true);
              setTimeout(
                function (ele) {
                  ele.removeAttribute("highlight");
                },
                150,
                this._animateElement
              );
            }
          });
      }
    }

    /**
     * Returns the tab or tab group label where an event happened, or null if
     * it didn't occur on a tab or tab group label.
     *
     * @param {Event} event
     *   The event for which we want to know on which element it happened.
     * @param {object} options
     * @param {boolean} options.ignoreSides
     *   If set to true: events will only be associated with an element if they
     *   happened on its central part (from 25% to 75%); if they happened on the
     *   left or right sides of the tab, the method will return null.
     */
    #getDragTarget(event, { ignoreSides = false } = {}) {
      let { target } = event;
      while (target) {
        if (isTab(target) || isTabGroupLabel(target)) {
          break;
        }
        target = target.parentNode;
      }
      if (target && ignoreSides) {
        let { width, height } = target.getBoundingClientRect();
        if (
          event.screenX < target.screenX + width * 0.25 ||
          event.screenX > target.screenX + width * 0.75 ||
          ((event.screenY < target.screenY + height * 0.25 ||
            event.screenY > target.screenY + height * 0.75) &&
            this.verticalMode)
        ) {
          return null;
        }
      }
      return target;
    }

    #getDropIndex(event) {
      let item = this.#getDragTarget(event);
      if (!item) {
        return this.ariaFocusableItems.length;
      }
      let isBeforeMiddle;

      let elementForSize = isTabGroupLabel(item) ? item.parentElement : item;
      if (this.verticalMode) {
        let middle =
          elementForSize.screenY +
          elementForSize.getBoundingClientRect().height / 2;
        isBeforeMiddle = event.screenY < middle;
      } else {
        let middle =
          elementForSize.screenX +
          elementForSize.getBoundingClientRect().width / 2;
        isBeforeMiddle = this.#rtlMode
          ? event.screenX > middle
          : event.screenX < middle;
      }
      return item.elementIndex + (isBeforeMiddle ? 0 : 1);
    }

    getDropEffectForTabDrag(event) {
      var dt = event.dataTransfer;

      let isMovingTab = dt.mozItemCount > 0;
      for (let i = 0; i < dt.mozItemCount; i++) {
        // tabs are always added as the first type
        let types = dt.mozTypesAt(0);
        if (types[0] != TAB_DROP_TYPE) {
          isMovingTab = false;
          break;
        }
      }

      if (isMovingTab) {
        let sourceNode = dt.mozGetDataAt(TAB_DROP_TYPE, 0);
        if (
          (isTab(sourceNode) || isTabGroupLabel(sourceNode)) &&
          sourceNode.ownerGlobal.isChromeWindow &&
          sourceNode.ownerDocument.documentElement.getAttribute("windowtype") ==
            "navigator:browser"
        ) {
          // Do not allow transfering a private tab to a non-private window
          // and vice versa.
          if (
            PrivateBrowsingUtils.isWindowPrivate(window) !=
            PrivateBrowsingUtils.isWindowPrivate(sourceNode.ownerGlobal)
          ) {
            return "none";
          }

          if (
            window.gMultiProcessBrowser !=
            sourceNode.ownerGlobal.gMultiProcessBrowser
          ) {
            return "none";
          }

          if (
            window.gFissionBrowser != sourceNode.ownerGlobal.gFissionBrowser
          ) {
            return "none";
          }

          return dt.dropEffect == "copy" ? "copy" : "move";
        }
      }

      if (Services.droppedLinkHandler.canDropLink(event, true)) {
        return "link";
      }
      return "none";
    }

    _handleNewTab(tab) {
      if (tab.container != this) {
        return;
      }
      tab._fullyOpen = true;
      gBrowser.tabAnimationsInProgress--;

      this._updateCloseButtons();

      if (tab.hasAttribute("selected")) {
        this._handleTabSelect();
      } else if (!tab.hasAttribute("skipbackgroundnotify")) {
        this._notifyBackgroundTab(tab);
      }

      // If this browser isn't lazy (indicating it's probably created by
      // session restore), preload the next about:newtab if we don't
      // already have a preloaded browser.
      if (tab.linkedPanel) {
        NewTabPagePreloading.maybeCreatePreloadedBrowser(window);
      }

      if (UserInteraction.running("browser.tabs.opening", window)) {
        UserInteraction.finish("browser.tabs.opening", window);
      }
    }

    _canAdvanceToTab(aTab) {
      return !aTab.closing;
    }

    /**
     * Returns the panel associated with a tab if it has a connected browser
     * and/or it is the selected tab.
     * For background lazy browsers, this will return null.
     */
    getRelatedElement(aTab) {
      if (!aTab) {
        return null;
      }

      // Cannot access gBrowser before it's initialized.
      if (!gBrowser._initialized) {
        return this.tabbox.tabpanels.firstElementChild;
      }

      // If the tab's browser is lazy, we need to `_insertBrowser` in order
      // to have a linkedPanel.  This will also serve to bind the browser
      // and make it ready to use. We only do this if the tab is selected
      // because otherwise, callers might end up unintentionally binding the
      // browser for lazy background tabs.
      if (!aTab.linkedPanel) {
        if (!aTab.selected) {
          return null;
        }
        gBrowser._insertBrowser(aTab);
      }
      return document.getElementById(aTab.linkedPanel);
    }

    _updateNewTabVisibility() {
      // Helper functions to help deal with customize mode wrapping some items
      let wrap = n =>
        n.parentNode.localName == "toolbarpaletteitem" ? n.parentNode : n;
      let unwrap = n =>
        n && n.localName == "toolbarpaletteitem" ? n.firstElementChild : n;

      // Starting from the tabs element, find the next sibling that:
      // - isn't hidden; and
      // - isn't the all-tabs button.
      // If it's the new tab button, consider the new tab button adjacent to the tabs.
      // If the new tab button is marked as adjacent and the tabstrip doesn't
      // overflow, we'll display the 'new tab' button inline in the tabstrip.
      // In all other cases, the separate new tab button is displayed in its
      // customized location.
      let sib = this;
      do {
        sib = unwrap(wrap(sib).nextElementSibling);
      } while (sib && (sib.hidden || sib.id == "alltabs-button"));

      this.toggleAttribute(
        "hasadjacentnewtabbutton",
        sib && sib.id == "new-tab-button"
      );
    }

    onWidgetAfterDOMChange(aNode, aNextNode, aContainer) {
      if (
        aContainer.ownerDocument == document &&
        aContainer.id == "TabsToolbar-customization-target"
      ) {
        this._updateNewTabVisibility();
      }
    }

    onAreaNodeRegistered(aArea, aContainer) {
      if (aContainer.ownerDocument == document && aArea == "TabsToolbar") {
        this._updateNewTabVisibility();
      }
    }

    onAreaReset(aArea, aContainer) {
      this.onAreaNodeRegistered(aArea, aContainer);
    }

    _hiddenSoundPlayingStatusChanged(tab, opts) {
      let closed = opts && opts.closed;
      if (!closed && tab.soundPlaying && !tab.visible) {
        this._hiddenSoundPlayingTabs.add(tab);
        this.toggleAttribute("hiddensoundplaying", true);
      } else {
        this._hiddenSoundPlayingTabs.delete(tab);
        if (this._hiddenSoundPlayingTabs.size == 0) {
          this.removeAttribute("hiddensoundplaying");
        }
      }
    }

    destroy() {
      if (this.boundObserve) {
        Services.prefs.removeObserver("privacy.userContext", this.boundObserve);
      }
      CustomizableUI.removeListener(this);
    }

    updateTabSoundLabel(tab) {
      // Add aria-label for inline audio button
      const [unmute, mute, unblock] =
        gBrowser.tabLocalization.formatMessagesSync([
          "tabbrowser-unmute-tab-audio-aria-label",
          "tabbrowser-mute-tab-audio-aria-label",
          "tabbrowser-unblock-tab-audio-aria-label",
        ]);
      if (tab.audioButton) {
        if (tab.hasAttribute("muted") || tab.hasAttribute("soundplaying")) {
          let ariaLabel;
          tab.linkedBrowser.audioMuted
            ? (ariaLabel = unmute.attributes[0].value)
            : (ariaLabel = mute.attributes[0].value);
          tab.audioButton.setAttribute("aria-label", ariaLabel);
        } else if (tab.hasAttribute("activemedia-blocked")) {
          tab.audioButton.setAttribute(
            "aria-label",
            unblock.attributes[0].value
          );
        }
      }
    }
  }

  customElements.define("tabbrowser-tabs", MozTabbrowserTabs, {
    extends: "tabs",
  });
}
