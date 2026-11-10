/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/* global gBrowser, gMultiProcessBrowser, gNavToolbox, gReduceMotion,
   MousePosTracker, RTL_UI, TAB_DROP_TYPE */

const { Tabbrowser } = ChromeUtils.importESModule(
  "moz-src:///browser/components/tabbrowser/Tabbrowser.sys.mjs"
);
const { XPCOMUtils } = ChromeUtils.importESModule(
  "resource://gre/modules/XPCOMUtils.sys.mjs"
);

const lazy = XPCOMUtils.declareLazy({
  AppConstants: "resource://gre/modules/AppConstants.sys.mjs",
  OpenInTabsUtils:
    "moz-src:///browser/components/tabbrowser/OpenInTabsUtils.sys.mjs",
  PageThumbs: "resource://gre/modules/PageThumbs.sys.mjs",
  PrivateBrowsingUtils: "resource://gre/modules/PrivateBrowsingUtils.sys.mjs",
});

const { isTab, isTabGroupLabel, isSplitViewWrapper } = Tabbrowser;

/**
 * The elements in the tab strip from `this.dragAndDropElements` that contain
 * logical information are:
 *
 * - <tab> (.tabbrowser-tab)
 * - <tab-group> label element (.tab-group-label)
 * - <tab-split-view-wrapper>
 *
 * The elements in the tab strip that contain the space inside of the <tabs>
 * element are:
 *
 * - <tab> (.tabbrowser-tab)
 * - <tab-group> label element wrapper (.tab-group-label-container)
 * - <tab-split-view-wrapper>
 *
 * When working with tab strip items, if you need logical information, you
 * can get it directly, e.g. `element.elementIndex` or `element.index`. If
 * you need spatial information like position or dimensions, then you should
 * call this function. For example, `elementToMove(element).getBoundingClientRect()`
 * or `elementToMove(element).style.top`.
 *
 * @param {MozTabbrowserTab|typeof MozTabbrowserTabGroup.labelElement} element
 * @returns {MozTabbrowserTab|vbox}
 */
const elementToMove = element => {
  if (isTab(element) || isSplitViewWrapper(element)) {
    return element;
  }
  if (isTabGroupLabel(element)) {
    return element.closest(".tab-group-label-container");
  }
  throw new Error(`Element "${element.tagName}" is not expected to move`);
};

// How often to verify that moving-tab mode still has a drag session backing
// it.
const STALE_DRAG_CHECK_INTERVAL_MS = 1000;

export class TabDragAndDrop {
  #dragTime = 0;
  #pinnedDropIndicatorTimeout = null;
  #staleDragCheckTimer = null;
  #dropAnimationFrame = 0;

  constructor(tabbrowserTabs) {
    this._tabbrowserTabs = tabbrowserTabs;
  }

  init() {
    this._pinnedDropIndicator = document.getElementById(
      "pinned-drop-indicator"
    );
    this._dragToPinPromoCard = document.getElementById(
      "drag-to-pin-promo-card"
    );
    this._tabDropIndicator = this._tabbrowserTabs.querySelector(
      ".tab-drop-indicator"
    );
  }

  // Event handlers

  handle_dragstart(event) {
    if (this._tabbrowserTabs._isCustomizing) {
      return;
    }

    let tab = this._getDragTarget(event, { findClosestTarget: false });
    if (!tab) {
      return;
    }
    if (tab.splitview) {
      tab = tab.splitview;
    }

    // Don't start a drag on a split view whose tabs are hidden.
    if (isSplitViewWrapper(tab) && !tab.visible) {
      return;
    }

    this._tabbrowserTabs.previewPanel?.deactivate(null, { force: true });
    this.startTabDrag(event, tab);
  }

  handle_dragover(event) {
    var dropEffect = this.getDropEffectForTabDrag(event);

    var ind = this._tabDropIndicator;
    if (dropEffect == "" || dropEffect == "none") {
      ind.hidden = true;
      return;
    }
    event.preventDefault();
    event.stopPropagation();

    var arrowScrollbox = this._tabbrowserTabs.arrowScrollbox;

    // autoscroll the tab strip if we drag over the scroll
    // buttons, even if we aren't dragging a tab, but then
    // return to avoid drawing the drop indicator
    var pixelsToScroll = 0;
    if (this._tabbrowserTabs.overflowing) {
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
          (this._rtlMode ? -1 : 1) * pixelsToScroll,
          true
        );
      }
    }

    let draggedTab = event.dataTransfer.mozGetDataAt(TAB_DROP_TYPE, 0);
    if (
      (dropEffect == "move" || dropEffect == "copy") &&
      document == draggedTab.ownerDocument &&
      !draggedTab._dragData.fromTabList
    ) {
      ind.hidden = true;
      if (this.#isAnimatingMoveTogetherSelectedTabs()) {
        // Wait for moving selected tabs together animation to finish.
        return;
      }
      this.finishMoveTogetherSelectedTabs(draggedTab);
      this._updateTabStylesOnDrag(draggedTab, dropEffect);
      // Collapsing the tab group needs to occur after the non dragged tab widths
      // have had their maxWidth to their current width by _updateTabStylesOnDrag.
      // This is to avoid the dragged tab label container being positioned incorrectly.
      if (
        draggedTab._dragData.expandGroupOnDrop &&
        !draggedTab.group.collapsed
      ) {
        draggedTab.group.collapsedByDrag = true;
        draggedTab.group.collapsed = true;
      }

      if (dropEffect == "move") {
        this.#setMovingTabMode(true);

        // Pinned tabs in expanded vertical mode are on a grid format and require
        // different logic to drag and drop.
        if (this._tabbrowserTabs.isContainerVerticalPinnedGrid(draggedTab)) {
          this._animateExpandedPinnedTabMove(event);
          return;
        }
        this._animateTabMove(event);
        return;
      }
    }

    this.finishAnimateTabMove();

    if (dropEffect == "link") {
      let target = this._getDragTarget(event, {
        ignoreSides: true,
      });
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
            this._tabbrowserTabs.selectedItem = target;
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
      let minMargin = this._tabbrowserTabs.verticalMode
        ? scrollRect.top - rect.top
        : scrollRect.left - rect.left;
      let maxMargin = this._tabbrowserTabs.verticalMode
        ? Math.min(minMargin + scrollRect.height, scrollRect.bottom)
        : Math.min(minMargin + scrollRect.width, scrollRect.right);
      if (this._rtlMode) {
        [minMargin, maxMargin] = [
          this._tabbrowserTabs.clientWidth - maxMargin,
          this._tabbrowserTabs.clientWidth - minMargin,
        ];
      }
      newMargin = pixelsToScroll > 0 ? maxMargin : minMargin;
    } else {
      let newIndex = this._getDropIndex(event);
      if (
        (isSplitViewWrapper(draggedTab) || isTabGroupLabel(draggedTab)) &&
        newIndex < gBrowser.pinnedTabCount
      ) {
        newIndex = gBrowser.pinnedTabCount;
      }
      let children = this._tabbrowserTabs.dragAndDropElements;
      if (newIndex == children.length) {
        let itemRect = children.at(-1).getBoundingClientRect();
        if (this._tabbrowserTabs.verticalMode) {
          newMargin = itemRect.bottom - rect.top;
        } else if (this._rtlMode) {
          newMargin = rect.right - itemRect.left;
        } else {
          newMargin = itemRect.right - rect.left;
        }
      } else {
        let itemRect = children[newIndex].getBoundingClientRect();
        if (this._tabbrowserTabs.verticalMode) {
          newMargin = rect.top - itemRect.bottom;
        } else if (this._rtlMode) {
          newMargin = rect.right - itemRect.right;
        } else {
          newMargin = itemRect.left - rect.left;
        }
      }
    }

    ind.hidden = false;
    newMargin += this._tabbrowserTabs.verticalMode
      ? ind.clientHeight
      : ind.clientWidth / 2;
    if (this._rtlMode) {
      newMargin *= -1;
    }
    ind.style.transform = this._tabbrowserTabs.verticalMode
      ? "translateY(" + Math.round(newMargin) + "px)"
      : "translateX(" + Math.round(newMargin) + "px)";
  }

  // eslint-disable-next-line complexity
  handle_drop(event) {
    var dt = event.dataTransfer;
    var dropEffect = dt.dropEffect;
    var draggedTab;
    let movingTabs;
    /** @type {TabMetricsContext} */
    const dropMetricsContext = gBrowser.TabMetrics.userTriggeredContext(
      gBrowser.TabMetrics.METRIC_SOURCE.DRAG_AND_DROP
    );
    if (dt.mozTypesAt(0)[0] == TAB_DROP_TYPE) {
      // tab copy or move
      draggedTab = dt.mozGetDataAt(TAB_DROP_TYPE, 0);
      // not our drop then
      if (!draggedTab) {
        return;
      }
      movingTabs = draggedTab._dragData.movingTabs;
      draggedTab.container.tabDragAndDrop.finishMoveTogetherSelectedTabs(
        draggedTab
      );
    }

    if (this._rtlMode) {
      // In `startTabDrag` we reverse the moving tabs order to handle
      // positioning and animation. For drop, we require the original
      // order, so reverse back.
      movingTabs?.reverse();
    }

    let overPinnedDropIndicator =
      this._pinnedDropIndicator.hasAttribute("visible") &&
      this._pinnedDropIndicator.hasAttribute("interactive");
    let rectsBeforeDrop =
      !gReduceMotion &&
      dropEffect != "copy" &&
      draggedTab?.container == this._tabbrowserTabs
        ? this.#getItemRects()
        : null;
    this._resetTabsAfterDrop(draggedTab);

    this._tabDropIndicator.hidden = true;
    event.stopPropagation();
    if (draggedTab && dropEffect == "copy") {
      let duplicatedDraggedTab;
      let duplicatedTabs = [];
      let dropTarget =
        this._tabbrowserTabs.dragAndDropElements[this._getDropIndex(event)];
      for (let tab of movingTabs) {
        let duplicatedTab = gBrowser.duplicateTab(tab);
        duplicatedTabs.push(duplicatedTab);
        if (tab == draggedTab) {
          duplicatedDraggedTab = duplicatedTab;
        }
      }
      gBrowser.moveTabsBefore(duplicatedTabs, dropTarget, {
        metricsContext: dropMetricsContext,
      });
      if (draggedTab.container != this._tabbrowserTabs || event.shiftKey) {
        this._tabbrowserTabs.selectedItem = duplicatedDraggedTab;
      }
    } else if (draggedTab && draggedTab.container == this._tabbrowserTabs) {
      let isPinned = draggedTab.pinned;
      let numPinned = gBrowser.pinnedTabCount;
      let tabs = this._tabbrowserTabs.dragAndDropElements.slice(
        isPinned ? 0 : numPinned,
        isPinned ? numPinned : undefined
      );

      let {
        dropElement,
        dropBefore,
        shouldCreateGroupOnDrop,
        shouldDropIntoCollapsedTabGroup,
        fromTabList,
      } = draggedTab._dragData;

      let dropIndex;
      let directionForward = false;
      if (fromTabList) {
        dropIndex = this._getDropIndex(event);
        if (dropIndex && dropIndex > movingTabs[0].elementIndex) {
          directionForward = true;
          if (!isSplitViewWrapper(movingTabs[0])) {
            dropIndex--;
          }
        }
      } else if (draggedTab.currentIndex > tabs[tabs.length - 1].currentIndex) {
        // There is a case where the currentIndex could be greater than the last item's in
        // the container. If this is the case, dropIndex needs to be set to the last item's
        // elementIndex to ensure the draggedTab/s are dropped in the last position.
        dropIndex = tabs[tabs.length - 1].elementIndex;
      }

      const dragToPinTargets = [
        this._tabbrowserTabs.pinnedTabsContainer,
        this._dragToPinPromoCard,
      ];
      let shouldPin =
        movingTabs.some(t => isTab(t)) &&
        !draggedTab.pinned &&
        (overPinnedDropIndicator ||
          dragToPinTargets.some(el => el.contains(event.target)));
      let shouldUnpin =
        isTab(draggedTab) &&
        draggedTab.pinned &&
        this._tabbrowserTabs.arrowScrollbox.contains(event.target);

      let shouldAnimate =
        rectsBeforeDrop &&
        !fromTabList &&
        !shouldCreateGroupOnDrop &&
        !shouldDropIntoCollapsedTabGroup &&
        !isTabGroupLabel(draggedTab) &&
        !shouldPin &&
        !shouldUnpin;

      if (shouldPin || shouldUnpin) {
        for (let item of movingTabs) {
          if (shouldPin && isTab(item)) {
            gBrowser.pinTab(item, {
              metricsContext: gBrowser.TabMetrics.userTriggeredContext(
                gBrowser.TabMetrics.METRIC_SOURCE.DRAG_AND_DROP
              ),
            });
          } else if (shouldUnpin) {
            gBrowser.unpinTab(item, {
              metricsContext: gBrowser.TabMetrics.userTriggeredContext(
                gBrowser.TabMetrics.METRIC_SOURCE.DRAG_AND_DROP
              ),
            });
          }
        }
      }

      this.finishAnimateTabMove();
      if (shouldCreateGroupOnDrop) {
        // This makes the tab group contents reflect the visual order of
        // the tabs right before dropping.
        let tabsInGroup = dropBefore
          ? [...movingTabs, dropElement]
          : [dropElement, ...movingTabs];
        gBrowser.addTabGroup(tabsInGroup, {
          insertBefore: dropElement,
          color: draggedTab._dragData.tabGroupCreationColor,
          metricsContext: gBrowser.TabMetrics.userTriggeredContext(
            gBrowser.TabMetrics.METRIC_SOURCE.DRAG_AND_DROP
          ),
        });
      } else if (
        shouldDropIntoCollapsedTabGroup &&
        isTabGroupLabel(dropElement) &&
        (isTab(draggedTab) || isSplitViewWrapper(draggedTab))
      ) {
        // If the dragged tab is the active tab in a collapsed tab group
        // and the user dropped it onto the label of its tab group, leave
        // the dragged tab where it was. Otherwise, drop it into the target
        // tab group.
        if (dropElement.group != draggedTab.group) {
          dropElement.group.addTabs(movingTabs, dropMetricsContext);
        }
      } else {
        if (dropIndex !== undefined) {
          for (let tab of movingTabs) {
            if (fromTabList && isSplitViewWrapper(tab)) {
              const dropTarget =
                this._tabbrowserTabs.dragAndDropElements[dropIndex];
              gBrowser.moveTabBefore(tab, dropTarget, {
                metricsContext: dropMetricsContext,
              });
            } else {
              gBrowser.moveTabTo(tab, {
                elementIndex: dropIndex,
                metricsContext: dropMetricsContext,
              });
              if (!directionForward) {
                dropIndex++;
              }
            }
          }
        } else if (dropElement && dropBefore) {
          gBrowser.moveTabsBefore(movingTabs, dropElement, {
            metricsContext: dropMetricsContext,
          });
        } else if (dropElement && dropBefore != undefined) {
          gBrowser.moveTabsAfter(movingTabs, dropElement, {
            metricsContext: dropMetricsContext,
          });
        }
        if (!shouldAnimate) {
          this._tabbrowserTabs._notifyBackgroundTab(movingTabs.at(-1));
        }
      }

      if (shouldAnimate) {
        // Scroll the dropped tabs into view before #animateDrop measures
        // them, so that they also slide by the distance scrolled. This waits
        // for the animation frame in which the moves scroll the selected tab
        // into view (see ensureElementIsVisible), which has to see it
        // untransformed.
        this.#dropAnimationFrame = requestAnimationFrame(() => {
          this.#dropAnimationFrame = 0;
          for (let tab of [movingTabs.at(-1), gBrowser.selectedTab]) {
            tab.scrollIntoView({ block: "nearest", behavior: "instant" });
          }
          this.#animateDrop(rectsBeforeDrop);
        });
      }

      // Every branch above has to end the tab group drag, including the ones
      // that don't move anything.
      if (isTabGroupLabel(draggedTab)) {
        this._setIsDraggingTabGroup(draggedTab.group, false);
        this._expandGroupOnDrop(draggedTab);
      }
    } else if (isTabGroupLabel(draggedTab)) {
      const dropIndex = this._getDropIndex(event);
      const droppedIntoPinnedArea = dropIndex < gBrowser.pinnedTabCount;
      gBrowser.adoptTabGroup(draggedTab.group, {
        elementIndex: droppedIntoPinnedArea
          ? gBrowser.pinnedTabCount
          : dropIndex,
      });

      // The group's tabs leave the window along with the group, so there is
      // no _expandGroupOnDrop to release the space _resetTabsAfterDrop keeps
      // reserved for them.
      let periphery = draggedTab.ownerDocument.getElementById(
        "tabbrowser-arrowscrollbox-periphery"
      );
      this.#releaseSpaceInScrolledContent(periphery);
    } else if (draggedTab) {
      // Move the tabs into this window. To avoid multiple tab-switches in
      // the original window, the selected tab should be adopted last.
      gBrowser.recordTabMetrics(
        gBrowser.TabMetrics.METRIC_ACTION.ADOPT,
        gBrowser.TabMetrics.userTriggeredContext(
          gBrowser.TabMetrics.METRIC_SOURCE.DRAG_AND_DROP
        ),
        { tabCount: movingTabs.length }
      );

      const dropIndex = this._getDropIndex(event);
      let newIndex = dropIndex;
      let selectedTab;
      let indexForSelectedTab;
      let unpinnedSplitViews = [];
      for (let i = 0; i < movingTabs.length; ++i) {
        const tab = movingTabs[i];
        if (tab.selected) {
          selectedTab = tab;
          indexForSelectedTab = newIndex;
        } else if (isSplitViewWrapper(tab)) {
          const droppedIntoPinnedArea = dropIndex < gBrowser.pinnedTabCount;
          const newSplitView = gBrowser.adoptSplitView(tab, {
            elementIndex: droppedIntoPinnedArea
              ? gBrowser.pinnedTabCount
              : newIndex,
            selectTab: true,
          });
          if (newSplitView) {
            if (droppedIntoPinnedArea) {
              unpinnedSplitViews.push(newSplitView);
            } else {
              ++newIndex;
            }
          }
        } else if (isTab(tab)) {
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

      if (movingTabs.length > 1) {
        // Restore tab selection
        let firstElement = this._tabbrowserTabs.dragAndDropElements[dropIndex];
        let firstTab = isSplitViewWrapper(firstElement)
          ? firstElement.tabs.at(0)
          : firstElement;
        let lastElement =
          this._tabbrowserTabs.dragAndDropElements[newIndex - 1];
        let lastTab = isSplitViewWrapper(lastElement)
          ? lastElement.tabs.at(-1)
          : lastElement;
        if (
          !(isSplitViewWrapper(firstElement) && firstElement == lastElement)
        ) {
          gBrowser.addRangeToMultiSelectedTabs(firstTab, lastTab);
        }
        if (unpinnedSplitViews.length) {
          let firstUnpinnedSplitView =
            this._tabbrowserTabs.dragAndDropElements[gBrowser.pinnedTabCount];
          let lastUnpinnedSplitView =
            this._tabbrowserTabs.dragAndDropElements[
              gBrowser.pinnedTabCount + unpinnedSplitViews.length - 1
            ];
          gBrowser.addRangeToMultiSelectedTabs(
            firstUnpinnedSplitView.tabs.at(0),
            lastUnpinnedSplitView.tabs.at(-1)
          );
        }
      }
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

      let targetTab = this._getDragTarget(event, { ignoreSides: true });
      let userContextId =
        this._tabbrowserTabs.selectedItem.getAttribute("usercontextid");
      let replace = isTab(targetTab);
      let newIndex = this._getDropIndex(event);
      let urls = links.map(link => link.url);
      let policyContainer =
        Services.droppedLinkHandler.getPolicyContainer(event);
      let triggeringPrincipal =
        Services.droppedLinkHandler.getTriggeringPrincipal(event);

      (async () => {
        if (
          urls.length >=
          Services.prefs.getIntPref("browser.tabs.maxOpenBeforeWarn")
        ) {
          // Sync dialog cannot be used inside drop event handler.
          let answer = await lazy.OpenInTabsUtils.promiseConfirmOpenInTabs(
            urls.length,
            window
          );
          if (!answer) {
            return;
          }
        }

        // This load could replace the current session history entry in the
        // target tab, which may not have had user interaction before. We
        // consider the drag onto the tab as being user interaction so this
        // entry is still accessible via the back-button.
        let activeEntry =
          targetTab?.linkedBrowser?.browsingContext?.activeSessionHistoryEntry;
        if (activeEntry) {
          activeEntry.hasUserInteraction = true;
        }

        let nextItem = this._tabbrowserTabs.dragAndDropElements[newIndex];
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
          policyContainer,
        });
      })();
    }

    for (let tab of this._tabbrowserTabs.dragAndDropElements) {
      delete tab.currentIndex;
    }

    if (draggedTab) {
      delete draggedTab._dragData;
    }
  }

  handle_dragend(event) {
    var dt = event.dataTransfer;
    var draggedTab = dt.mozGetDataAt(TAB_DROP_TYPE, 0);

    this.finishMoveTogetherSelectedTabs(draggedTab);
    this.finishAnimateTabMove();
    if (isTabGroupLabel(draggedTab)) {
      this._setIsDraggingTabGroup(draggedTab.group, false);
      this._expandGroupOnDrop(draggedTab);
    }
    this._resetTabsAfterDrop(draggedTab);

    if (
      dt.mozUserCancelled ||
      dt.dropEffect != "none" ||
      !Services.prefs.getBoolPref("browser.tabs.allowTabDetach") ||
      this._tabbrowserTabs._isCustomizing
    ) {
      delete draggedTab._dragData;
      return;
    }

    // Disable detach within the browser toolbox
    let [tabAxisPos, tabAxisStart, tabAxisEnd] = this._tabbrowserTabs
      .verticalMode
      ? [event.screenY, window.screenY, window.screenY + window.outerHeight]
      : [event.screenX, window.screenX, window.screenX + window.outerWidth];

    if (tabAxisPos > tabAxisStart && tabAxisPos < tabAxisEnd) {
      // also avoid detaching if the tab was dropped too close to
      // the tabbar (half a tab)
      let rect = window.windowUtils.getBoundsWithoutFlushing(
        this._tabbrowserTabs.arrowScrollbox
      );
      let crossAxisPos = this._tabbrowserTabs.verticalMode
        ? event.screenX
        : event.screenY;
      let crossAxisStart, crossAxisEnd;
      if (this._tabbrowserTabs.verticalMode) {
        if (
          (RTL_UI && this._tabbrowserTabs._sidebarPositionStart) ||
          (!RTL_UI && !this._tabbrowserTabs._sidebarPositionStart)
        ) {
          crossAxisStart =
            window.mozInnerScreenX + rect.right - 1.5 * rect.width;
          crossAxisEnd = window.screenX + window.outerWidth;
        } else {
          crossAxisStart = window.screenX;
          crossAxisEnd = window.mozInnerScreenX + rect.left + 1.5 * rect.width;
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
    var dropScreen = event.screen;
    var availX = {},
      availY = {},
      availWidth = {},
      availHeight = {};
    // Get available rect in desktop pixels.
    dropScreen.GetAvailRectDisplayPix(availX, availY, availWidth, availHeight);
    availX = availX.value;
    availY = availY.value;
    availWidth = availWidth.value;
    availHeight = availHeight.value;

    // Compute the final window size in desktop pixels ensuring that the new
    // window entirely fits within `dropScreen`.
    let ourCssToDesktopScale =
      window.devicePixelRatio / window.desktopToDeviceScale;
    let screenCssToDesktopScale =
      dropScreen.defaultCSSScaleFactor / dropScreen.contentsScaleFactor;

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
    var winLeft = Math.min(
      Math.max(
        event.screenX * ourCssToDesktopScale -
          draggedTab._dragData.offsetX * screenCssToDesktopScale,
        availX
      ),
      availX + availWidth - winWidth
    );
    var winTop = Math.min(
      Math.max(
        event.screenY * ourCssToDesktopScale -
          draggedTab._dragData.offsetY * screenCssToDesktopScale,
        availY
      ),
      availY + availHeight - winHeight
    );

    // Convert back left and top to our CSS pixel space.
    winLeft /= ourCssToDesktopScale;
    winTop /= ourCssToDesktopScale;

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
      window.moveTo(winLeft, winTop);
      window.focus();
    } else {
      let props = {
        suppressanimation: 1,
        metricsContext: gBrowser.TabMetrics.userTriggeredContext(
          gBrowser.TabMetrics.METRIC_SOURCE.DRAG_AND_DROP
        ),
      };
      if (window.fullScreen && lazy.AppConstants.platform == "macosx") {
        // `winLeft` and `winTop` above leave room for a window as large as this
        // one, and a fullscreen window covers the screen, so they collapse
        // onto the corner of it. The new window doesn't take our size
        // either, so there is no drop position to compute for it and it is
        // better off opening where any other new window would.
        //
        // The flag below keeps macOS from adopting the new window into its
        // own fullscreen space during its initial show, which can hang on a
        // synchronous transition to the Dock (bug 2051071).
        props.suppressinitialfullscreen = 1;
      } else {
        props.screenX = winLeft;
        props.screenY = winTop;
      }
      gBrowser.replaceTabsWithWindow(draggedTab, props);
    }
    event.stopPropagation();
  }

  handle_dragleave(event) {
    this.#dragTime = 0;

    // This does not work at all (see bug 458613)
    var target = event.relatedTarget;
    while (target && target != this._tabbrowserTabs) {
      target = target.parentNode;
    }
    if (target) {
      return;
    }

    this._tabDropIndicator.hidden = true;
    event.stopPropagation();
  }

  // Utilities

  get _rtlMode() {
    return !this._tabbrowserTabs.verticalMode && RTL_UI;
  }

  #setMovingTabMode(movingTab) {
    this._tabbrowserTabs.toggleAttribute("movingtab", movingTab);
    gNavToolbox.toggleAttribute("movingtab", movingTab);

    if (movingTab) {
      this.#startStaleDragCheck();
      window.addEventListener("dragleave", this.#onWindowDragLeave, {
        capture: true,
      });
    } else {
      this.#stopStaleDragCheck();
      window.removeEventListener("dragleave", this.#onWindowDragLeave, {
        capture: true,
      });
    }
  }

  /**
   * Clears the grouping and pinning feedback shown while dragging, without
   * moving any tabs back.
   */
  #resetDragFeedback() {
    for (let item of this._tabbrowserTabs.dragAndDropElements) {
      this._resetGroupTarget(item);
    }
    this._clearDragOverGroupingTimer();
    this.#resetPinnedDropIndicator();
    this._tabbrowserTabs.removeAttribute("movingtab-group");
    this._tabbrowserTabs.removeAttribute("movingtab-ungroup");
    this._tabbrowserTabs.removeAttribute("movingtab-addToGroup");
    this._setDragOverGroupColor(null);
  }

  /**
   * A dragleave without a relatedTarget means the drag left this window.
   * Clear the grouping and pinning feedback, which would otherwise keep
   * reacting to the last position seen in this window. Tabs keep their
   * positions so the strip still shows where the drop would land, and the
   * next dragover re-evaluates the feedback if the drag comes back.
   *
   * @param {DragEvent} event
   */
  #onWindowDragLeave = event => {
    if (event.relatedTarget) {
      return;
    }
    this.#resetDragFeedback();

    let dragData = event.dataTransfer?.mozGetDataAt(
      TAB_DROP_TYPE,
      0
    )?._dragData;
    if (dragData) {
      delete dragData.shouldCreateGroupOnDrop;
      delete dragData.shouldDropIntoCollapsedTabGroup;
    }
  };

  get #dragSession() {
    return Cc["@mozilla.org/widget/dragservice;1"]
      .getService(Ci.nsIDragService)
      .getCurrentSession(window);
  }

  /**
   * Moving-tab mode is only ever left by finishAnimateTabMove, which runs off
   * the drop and dragend events. Platforms don't reliably deliver those, and
   * a drag session that outlives its drag leaves the tab strip overlapping
   * the toolbar and #nav-bar with pointer-events: none, i.e. an unusable
   * window. Watch for that state and recover from it.
   */
  #startStaleDragCheck() {
    if (this.#staleDragCheckTimer) {
      return;
    }
    this.#staleDragCheckTimer = setInterval(() => {
      if (!this.#dragSession) {
        this.#recoverFromStaleDrag();
      }
    }, STALE_DRAG_CHECK_INTERVAL_MS);
    window.addEventListener("mousedown", this.#onMouseDown, {
      capture: true,
    });
  }

  #stopStaleDragCheck() {
    if (!this.#staleDragCheckTimer) {
      return;
    }
    clearInterval(this.#staleDragCheckTimer);
    this.#staleDragCheckTimer = null;
    window.removeEventListener("mousedown", this.#onMouseDown, {
      capture: true,
    });
  }

  #onMouseDown = event => {
    // Pressing the primary button requires having released it first, which
    // can't happen while the platform is running a drag loop. So this proves
    // that the drag is over even when the drag session claims otherwise.
    if (event.button == 0) {
      this.#recoverFromStaleDrag();
    }
  };

  #recoverFromStaleDrag() {
    let session = this.#dragSession;
    Glean.tab.staleDragRecovery[session ? "session_live" : "session_ended"].add(
      1
    );

    // Ending the session as an incomplete drag skips dragend at the source,
    // which would otherwise take handle_dragend's detach path and tear the
    // tab into a new window positioned from stale coordinates.
    session?.endDragSession(false);

    let draggedItem = this._tabbrowserTabs.dragAndDropElements.find(
      item => item._dragData
    );
    if (draggedItem) {
      this.finishMoveTogetherSelectedTabs(draggedItem);
      if (isTabGroupLabel(draggedItem)) {
        this._setIsDraggingTabGroup(draggedItem.group, false);
        this._expandGroupOnDrop(draggedItem);
      }
    }
    this.finishAnimateTabMove();
    this._resetTabsAfterDrop(draggedItem);
    if (draggedItem) {
      delete draggedItem._dragData;
    }
  }

  _getDropIndex(event) {
    let item = this._getDragTarget(event);
    if (!item) {
      return this._tabbrowserTabs.dragAndDropElements.length;
    }
    if (item.splitview) {
      item = item.splitview;
    }
    let isBeforeMiddle;

    let elementForSize = elementToMove(item);
    if (this._tabbrowserTabs.verticalMode) {
      let middle =
        elementForSize.screenY +
        elementForSize.getBoundingClientRect().height / 2;
      isBeforeMiddle = event.screenY < middle;
    } else {
      let middle =
        elementForSize.screenX +
        elementForSize.getBoundingClientRect().width / 2;
      isBeforeMiddle = this._rtlMode
        ? event.screenX > middle
        : event.screenX < middle;
    }
    return item.elementIndex + (isBeforeMiddle ? 0 : 1);
  }

  /**
   * Returns the tab, tab group label or split view wrapper where an event happened,
   * it didn't occur on a tab or tab group label.
   *
   * @param {Event} event
   *   The event for which we want to know on which element it happened.
   * @param {object} options
   * @param {boolean} options.ignoreSides
   *   If set to true: events will only be associated with an element if they
   *   happened on its central part (from 25% to 75%); if they happened on the
   *   left or right sides of the tab, the method will return null.
   * @param {boolean} options.findClosestTarget
   *   When the event resolves to the scrollbox itself (landed in the margins
   *   around an item rather than on one), associate it with the tab, tab group
   *   label, or split view wrapper horizontally overlapping the event's
   *   coordinates.
   */
  _getDragTarget(
    event,
    { ignoreSides = false, findClosestTarget = true } = {}
  ) {
    let { target } = event;
    if (
      findClosestTarget &&
      target === this._tabbrowserTabs.arrowScrollbox &&
      !this._tabbrowserTabs.verticalMode
    ) {
      return this.#getHorizontalScrollboxDragTarget(event, ignoreSides);
    }
    while (target) {
      if (
        isTab(target) ||
        isTabGroupLabel(target) ||
        isSplitViewWrapper(target)
      ) {
        break;
      }
      target = target.parentNode;
    }
    if (target && ignoreSides) {
      let { width, height } = target.getBoundingClientRect();

      let xMin = target.screenX + width * 0.25;
      let xMax = target.screenX + width * 0.75;
      if (isTab(target) && target.splitview) {
        let [lTab, rTab] = window.RTL_UI
          ? target.splitview.tabs.reverse()
          : target.splitview.tabs;
        xMin = lTab.screenX + lTab.getBoundingClientRect().width * 0.25;
        xMax = rTab.screenX + rTab.getBoundingClientRect().width * 0.75;
      }

      let yMin = target.screenY + height * 0.25;
      let yMax = target.screenY + height * 0.75;

      if (
        event.screenX < xMin ||
        event.screenX > xMax ||
        ((event.screenY < yMin || event.screenY > yMax) &&
          this._tabbrowserTabs.verticalMode)
      ) {
        return null;
      }
    }
    return target;
  }

  /**
   * Locates the drag target that horizontally overlaps with the event
   * coordinates.
   *
   * @param {Event} event
   *   The event for which we want to know on which element it happened.
   * @param {boolean} ignoreSides
   *   If set to true: events will only be associated with an element if they
   *   happened on its central part (from 25% to 75%).
   * @returns {Element}
   *   The element that matches the horizontal bounds of the event, or
   *   `undefined` if no matching element is found.
   */
  #getHorizontalScrollboxDragTarget(event, ignoreSides) {
    function isWithinBounds(el) {
      let { width } = window.windowUtils.getBoundsWithoutFlushing(el);
      const offset = ignoreSides ? width * 0.25 : 0;
      const startX = el.screenX + offset;
      const endX = el.screenX + width - offset;
      return startX <= event.screenX && event.screenX <= endX;
    }
    return this._tabbrowserTabs.dragAndDropElements.find(isWithinBounds);
  }

  #isMovingTab() {
    return this._tabbrowserTabs.hasAttribute("movingtab");
  }

  // Tab groups

  /**
   * When a tab group is being dragged, it fully collapses, even if it
   * contains the active tab. Since all of its tabs will become invisible,
   * the cache of visible tabs needs to be updated. Similarly, when the user
   * stops dragging the tab group, it needs to return to normal, which may
   * result in grouped tabs becoming visible again.
   *
   * @param {MozTabbrowserTabGroup} tabGroup
   * @param {boolean} isDragging
   */
  _setIsDraggingTabGroup(tabGroup, isDragging) {
    tabGroup.isBeingDragged = isDragging;
    this._tabbrowserTabs._invalidateCachedVisibleTabs();
  }

  /**
   * Undoes the collapse a drag applied to a tab group. Safe to call from
   * every way out of a drag: it does nothing for a group the user collapsed
   * themselves, and nothing once the group is already on its way back.
   *
   * @param {MozTabbrowserTab|typeof MozTabbrowserTabGroup.labelElement} [draggedTab]
   */
  _expandGroupOnDrop(draggedTab) {
    if (!isTabGroupLabel(draggedTab)) {
      return;
    }
    let group = draggedTab.group;
    if (!group.collapsedByDrag || !group.collapsed) {
      return;
    }
    let periphery = draggedTab.ownerDocument.getElementById(
      "tabbrowser-arrowscrollbox-periphery"
    );
    // The group's tabs animate back to their full size, so hold on to the
    // space reserved for them until they get there.
    group.addEventListener(
      "TabGroupAnimationComplete",
      () => {
        group.collapsedByDrag = false;
        this.#releaseSpaceInScrolledContent(periphery);
      },
      { once: true }
    );
    group.collapsed = false;
  }

  /**
   * @param {MozTabbrowserTab|typeof MozTabbrowserTabGroup.labelElement} dropElement
   */
  _triggerDragOverGrouping(dropElement) {
    this._clearDragOverGroupingTimer();

    this._tabbrowserTabs.toggleAttribute("movingtab-group", true);
    this._tabbrowserTabs.removeAttribute("movingtab-ungroup");
    dropElement.toggleAttribute("dragover-groupTarget", true);
  }

  _clearDragOverGroupingTimer() {
    if (this._dragOverGroupingTimer) {
      clearTimeout(this._dragOverGroupingTimer);
      this._dragOverGroupingTimer = 0;
    }
  }

  _setDragOverGroupColor(groupColorCode) {
    if (!groupColorCode) {
      this._tabbrowserTabs.style.removeProperty("--dragover-tab-group-color");
      this._tabbrowserTabs.style.removeProperty(
        "--dragover-tab-group-color-invert"
      );
      this._tabbrowserTabs.style.removeProperty(
        "--dragover-tab-group-color-pale"
      );
      return;
    }
    this._tabbrowserTabs.style.setProperty(
      "--dragover-tab-group-color",
      `var(--tab-group-${groupColorCode})`
    );
    this._tabbrowserTabs.style.setProperty(
      "--dragover-tab-group-color-invert",
      `var(--tab-group-${groupColorCode}-invert)`
    );
    this._tabbrowserTabs.style.setProperty(
      "--dragover-tab-group-color-pale",
      `var(--tab-group-${groupColorCode}-pale)`
    );
  }

  /**
   * @param {MozTabbrowserTab|typeof MozTabbrowserTabGroup.labelElement} [element]
   */
  _resetGroupTarget(element) {
    element?.removeAttribute("dragover-groupTarget");
  }

  // Drag start

  startTabDrag(event, tab, { fromTabList = false } = {}) {
    this.#finishDropAnimation();
    if (this.expandOnHover) {
      // Temporarily disable MousePosTracker while dragging
      MousePosTracker.removeListener(document.defaultView.SidebarController);
    }
    if (this._tabbrowserTabs.isContainerVerticalPinnedGrid(tab)) {
      // In expanded vertical mode, the max number of pinned tabs per row is dynamic
      // Set this before adjusting dragged tab's position
      let pinnedTabs = this._tabbrowserTabs.visibleTabs.slice(
        0,
        gBrowser.pinnedTabCount
      );
      let tabsPerRow = 0;
      let position = RTL_UI
        ? window.windowUtils.getBoundsWithoutFlushing(
            this._tabbrowserTabs.pinnedTabsContainer
          ).right
        : 0;
      for (let pinnedTab of pinnedTabs) {
        let tabPosition;
        let rect = window.windowUtils.getBoundsWithoutFlushing(pinnedTab);
        if (RTL_UI) {
          tabPosition = rect.right;
          if (tabPosition > position) {
            break;
          }
        } else {
          tabPosition = rect.left;
          if (tabPosition < position) {
            break;
          }
        }
        tabsPerRow++;
        position = tabPosition;
      }
      this._maxTabsPerRow = tabsPerRow;
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
      this._tabbrowserTabs.selectedItem = tab;
      let selectedElements = gBrowser.selectedElements;
      let otherSelectedElements = selectedElements.filter(
        selectedEle => selectedEle != tab
      );
      dataTransferOrderedTabs = [tab].concat(otherSelectedElements);
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

    // Create a canvas to which we capture the current tab.
    // Until canvas is HiDPI-aware (bug 780362), we need to scale the desired
    // canvas size (in CSS pixels) to the window's backing resolution in order
    // to get a full-resolution drag image for use on HiDPI displays.
    let scale = window.devicePixelRatio;
    let canvas = this._tabbrowserTabs._dndCanvas;
    if (!canvas) {
      this._tabbrowserTabs._dndCanvas = canvas = document.createElementNS(
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
    let splitViewTab;
    if (isSplitViewWrapper(tab)) {
      splitViewTab = tab.tabs.find(t => t.selected);
    }
    let browser = splitViewTab
      ? splitViewTab.linkedBrowser
      : isTab(tab) && tab.linkedBrowser;
    if (isTabGroupLabel(tab)) {
      toDrag = tab;
    } else if (gMultiProcessBrowser) {
      var context = canvas.getContext("2d");
      context.fillStyle = "white";
      context.fillRect(0, 0, canvas.width, canvas.height);

      let captureListener;
      let platform = lazy.AppConstants.platform;
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
        if (!this._tabbrowserTabs._dndPanel) {
          this._tabbrowserTabs._dndCanvas = canvas;
          this._tabbrowserTabs._dndPanel = document.createXULElement("panel");
          this._tabbrowserTabs._dndPanel.className = "dragfeedback-tab";
          this._tabbrowserTabs._dndPanel.setAttribute("type", "drag");
          let wrapper = document.createElementNS(
            "http://www.w3.org/1999/xhtml",
            "div"
          );
          wrapper.style.width = "160px";
          wrapper.style.height = "90px";
          wrapper.appendChild(canvas);
          this._tabbrowserTabs._dndPanel.appendChild(wrapper);
          document.documentElement.appendChild(this._tabbrowserTabs._dndPanel);
        }
        toDrag = this._tabbrowserTabs._dndPanel;
      }
      // PageThumb is async with e10s but that's fine
      // since we can update the image during the dnd.
      lazy.PageThumbs.captureToCanvas(browser, canvas)
        .then(captureListener)
        .catch(e => console.error(e));
    } else {
      // For the non e10s case we can just use PageThumbs
      // sync, so let's use the canvas for setDragImage.
      lazy.PageThumbs.captureToCanvas(browser, canvas).catch(e =>
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
      return this._tabbrowserTabs.verticalMode ? rect.top : rect.left;
    };

    let tabOffset = clientPos(tab) - clientPos(this._tabbrowserTabs);

    let movingTabs = tab.multiselected ? gBrowser.selectedElements : [tab];
    let movingTabsSet = new Set(movingTabs);

    let dropEffect = this.getDropEffectForTabDrag(event);
    let isMovingInTabStrip = !fromTabList && dropEffect == "move";
    let collapseTabGroupDuringDrag =
      isMovingInTabStrip && isTabGroupLabel(tab) && !tab.group.collapsed;

    tab._dragData = {
      offsetX: this._tabbrowserTabs.verticalMode
        ? event.screenX - window.screenX
        : event.screenX - window.screenX - tabOffset,
      offsetY: this._tabbrowserTabs.verticalMode
        ? event.screenY - window.screenY - tabOffset
        : event.screenY - window.screenY,
      scrollPos:
        this._tabbrowserTabs.verticalMode && tab.pinned
          ? this._tabbrowserTabs.pinnedTabsContainer.scrollPosition
          : this._tabbrowserTabs.arrowScrollbox.scrollPosition,
      screenX: event.screenX,
      screenY: event.screenY,
      movingTabs,
      movingTabsSet,
      fromTabList,
      tabGroupCreationColor: gBrowser.tabGroupMenu.nextUnusedColor,
      expandGroupOnDrop: collapseTabGroupDuringDrag,
    };
    if (this._rtlMode) {
      // Reverse order to handle positioning in `_updateTabStylesOnDrag`
      // and animation in `_animateTabMove`
      tab._dragData.movingTabs.reverse();
    }

    if (isMovingInTabStrip) {
      this.#setMovingTabMode(true);

      if (tab.multiselected) {
        this._moveTogetherSelectedTabs(tab);
      } else if (isTabGroupLabel(tab)) {
        this._setIsDraggingTabGroup(tab.group, true);
      }
    }

    event.stopPropagation();

    if (fromTabList) {
      Glean.browserUiInteraction.allTabsPanelDragstartTabEventCount.add(1);
    }
  }

  /**
   * Pads the end of the tab strip so that its scrolled content doesn't shrink
   * while items are missing from it.
   *
   * @param {Element} periphery
   * @param {number} space
   */
  #reserveSpaceInScrolledContent(periphery, space) {
    if (this._tabbrowserTabs.verticalMode) {
      periphery.style.marginBlockStart = space + "px";
    } else {
      periphery.style.marginInlineStart = space + "px";
    }
  }

  /**
   * @param {Element} periphery
   */
  #releaseSpaceInScrolledContent(periphery) {
    periphery.style.marginBlockStart = "";
    periphery.style.marginInlineStart = "";
  }

  /**
   * @param {Element} element
   * @returns {number}
   *   The space the element takes up along the tab strip's axis, margins
   *   included.
   */
  #marginBoxExtent(element) {
    let rect = window.windowUtils.getBoundsWithoutFlushing(element);
    let style = window.getComputedStyle(element);
    if (this._tabbrowserTabs.verticalMode) {
      return (
        rect.height +
        parseFloat(style.marginBlockStart) +
        parseFloat(style.marginBlockEnd)
      );
    }
    return (
      rect.width +
      parseFloat(style.marginInlineStart) +
      parseFloat(style.marginInlineEnd)
    );
  }

  /* In order to to drag tabs between both the pinned arrowscrollbox (pinned tab container)
    and unpinned arrowscrollbox (tabbrowser-arrowscrollbox), the dragged tabs need to be
    positioned absolutely. This results in a shift in the layout, filling the empty space.
    This function updates the position and widths of elements affected by this layout shift
    when the tab is first selected to be dragged.
  */
  _updateTabStylesOnDrag(tab, dropEffect) {
    let tabStripItemElement = elementToMove(tab);
    tabStripItemElement.style.pointerEvents =
      dropEffect == "copy" ? "auto" : "";
    if (tabStripItemElement.hasAttribute("dragtarget")) {
      return;
    }
    let isPinned = tab.pinned;
    let dragAndDropElements = this._tabbrowserTabs.dragAndDropElements;
    let isGrid = this._tabbrowserTabs.isContainerVerticalPinnedGrid(tab);
    let periphery = document.getElementById(
      "tabbrowser-arrowscrollbox-periphery"
    );

    if (isPinned && this._tabbrowserTabs.verticalMode) {
      this._tabbrowserTabs.pinnedTabsContainer.setAttribute("dragActive", "");
    }

    // Ensure tab containers retain size while tabs are dragged out of the layout
    let pinnedRect = window.windowUtils.getBoundsWithoutFlushing(
      this._tabbrowserTabs.pinnedTabsContainer.scrollbox
    );
    let pinnedContainerRect = window.windowUtils.getBoundsWithoutFlushing(
      this._tabbrowserTabs.pinnedTabsContainer
    );
    let unpinnedRect = window.windowUtils.getBoundsWithoutFlushing(
      this._tabbrowserTabs.arrowScrollbox.scrollbox
    );
    let tabContainerRect = window.windowUtils.getBoundsWithoutFlushing(
      this._tabbrowserTabs
    );

    if (this._tabbrowserTabs.pinnedTabsContainer.firstChild) {
      this._tabbrowserTabs.pinnedTabsContainer.scrollbox.style.height =
        pinnedRect.height + "px";
      // Use "minHeight" so as not to interfere with user preferences for height.
      this._tabbrowserTabs.pinnedTabsContainer.style.minHeight =
        pinnedContainerRect.height + "px";
      this._tabbrowserTabs.pinnedTabsContainer.scrollbox.style.width =
        pinnedRect.width + "px";
    }
    this._tabbrowserTabs.arrowScrollbox.scrollbox.style.height =
      unpinnedRect.height + "px";
    this._tabbrowserTabs.arrowScrollbox.scrollbox.style.width =
      unpinnedRect.width + "px";

    let { movingTabs, movingTabsSet, expandGroupOnDrop } = tab._dragData;
    /** @type {(MozTabbrowserTab|typeof MozTabbrowserTabGroup.labelElement)[]} */
    let suppressTransitionsFor = [];
    /** @type {Map<MozTabbrowserTab, DOMRect>} */

    const tabsOrigBounds = new Map();

    for (let t of dragAndDropElements) {
      t = elementToMove(t);
      let tabRect = window.windowUtils.getBoundsWithoutFlushing(t);

      // record where all the tabs were before we position:absolute the moving tabs
      tabsOrigBounds.set(t, tabRect);

      // Prevent flex rules from resizing non dragged tabs while the dragged
      // tabs are positioned absolutely
      t.style.maxWidth = tabRect.width + "px";
      // Prevent non-moving tab strip items from performing any animations
      // at the very beginning of the drag operation; this prevents them
      // from appearing to move while the dragged tabs are positioned absolutely
      let isTabInCollapsingGroup = expandGroupOnDrop && t.group == tab.group;
      if (!movingTabsSet.has(t) && !isTabInCollapsingGroup) {
        t.style.transition = "none";
        suppressTransitionsFor.push(t);
      }
    }

    if (suppressTransitionsFor.length) {
      window
        .promiseDocumentFlushed(() => {})
        .then(() => {
          window.requestAnimationFrame(() => {
            for (let t of suppressTransitionsFor) {
              t.style.transition = "";
            }
          });
        });
    }

    // Use .tab-group-label-container, tab-split-view-wrapper or .tabbrowser-tab for size/position
    // calculations.
    let rect = window.windowUtils.getBoundsWithoutFlushing(tabStripItemElement);
    // Vertical tabs live under the #sidebar-container element which gets animated and has a
    // transform style property, making it the containing block for all its descendants.
    // Position:absolute elements need to account for this when updating position using
    // other measurements whose origin is the viewport or documentElement's 0,0
    let movingTabsOffsetX = tabStripItemElement.offsetParent
      ? window.windowUtils.getBoundsWithoutFlushing(
          tabStripItemElement.offsetParent
        ).x
      : 0;

    for (let movingTab of movingTabs) {
      movingTab = elementToMove(movingTab);
      movingTab.style.width = rect.width + "px";
      // "dragtarget" contains the following rules which must only be set AFTER the above
      // elements have been adjusted. {z-index: 3 !important, position: absolute !important}
      movingTab.setAttribute("dragtarget", "");
      if (isTabGroupLabel(tab)) {
        if (this._tabbrowserTabs.verticalMode) {
          movingTab.style.top = rect.top - tabContainerRect.top + "px";
        } else {
          movingTab.style.left = rect.left - movingTabsOffsetX + "px";
          movingTab.style.height = rect.height + "px";
        }
      } else if (isGrid) {
        movingTab.style.top = rect.top - pinnedRect.top + "px";
        movingTab.style.left = rect.left - movingTabsOffsetX + "px";
      } else if (this._tabbrowserTabs.verticalMode) {
        movingTab.style.top = rect.top - tabContainerRect.top + "px";
      } else if (this._rtlMode) {
        movingTab.style.left = rect.left - movingTabsOffsetX + "px";
      } else {
        movingTab.style.left = rect.left - movingTabsOffsetX + "px";
      }
    }

    if (movingTabs.length == 2) {
      tabStripItemElement.setAttribute("small-stack", "");
    } else if (movingTabs.length > 2) {
      tabStripItemElement.setAttribute("big-stack", "");
    }

    if (
      !isPinned &&
      this._tabbrowserTabs.arrowScrollbox.hasAttribute("overflowing")
    ) {
      // The dragged element is taken out of the layout below, and the tabs of
      // a group that is about to collapse shrink to nothing. Letting the
      // scrolled content shrink along with them would clamp the scroll
      // position, and could even stop the tab strip from overflowing, either
      // of which shifts the items that stay put.
      let missingSpace = this.#marginBoxExtent(tabStripItemElement);
      if (expandGroupOnDrop) {
        for (let groupItem of tab.group.tabsAndSplitViews) {
          missingSpace += this.#marginBoxExtent(groupItem);
        }
      }
      this.#reserveSpaceInScrolledContent(periphery, missingSpace);
    } else if (
      isPinned &&
      this._tabbrowserTabs.pinnedTabsContainer.hasAttribute("overflowing")
    ) {
      let pinnedPeriphery = document.createXULElement("hbox");
      pinnedPeriphery.id = "pinned-tabs-container-periphery";
      pinnedPeriphery.style.width = "100%";
      pinnedPeriphery.style.marginBlockStart = rect.height + "px";
      this._tabbrowserTabs.pinnedTabsContainer.appendChild(pinnedPeriphery);
    }

    let setElPosition = el => {
      let origBounds = tabsOrigBounds.get(el);
      if (this._tabbrowserTabs.verticalMode && origBounds.top > rect.top) {
        el.style.top = rect.height + "px";
      } else if (!this._tabbrowserTabs.verticalMode) {
        if (!this._rtlMode && origBounds.left > rect.left) {
          el.style.left = rect.width + "px";
        } else if (this._rtlMode && origBounds.left < rect.left) {
          el.style.left = -rect.width + "px";
        }
      }
    };

    let setGridElPosition = el => {
      let origBounds = tabsOrigBounds.get(el);
      if (!origBounds) {
        // No bounds saved for this pinned tab
        return;
      }
      // We use getBoundingClientRect and force a reflow as we need to know their new positions
      // after making the moving tabs position:absolute
      let newBounds = el.getBoundingClientRect();
      let shiftX = origBounds.x - newBounds.x;
      let shiftY = origBounds.y - newBounds.y;

      el.style.left = shiftX + "px";
      el.style.top = shiftY + "px";
    };

    // Update tabs in the same container as the dragged tabs so as not
    // to fill the space when the dragged tabs become absolute
    for (let t of dragAndDropElements) {
      let tabIsPinned = t.pinned;
      t = elementToMove(t);
      if (!t.hasAttribute("dragtarget")) {
        if (
          (!isPinned && !tabIsPinned) ||
          (tabIsPinned && isPinned && !isGrid)
        ) {
          setElPosition(t);
        } else if (isGrid && tabIsPinned && isPinned) {
          setGridElPosition(t);
        }
      }
    }

    if (this._tabbrowserTabs.expandOnHover) {
      // Query the expanded width from sidebar launcher to ensure tabs aren't
      // cut off (Bug 1974037).
      const sidebarController = tab.documentGlobal.SidebarController;
      sidebarController.expandOnHoverComplete.then(async () => {
        const width = await window.promiseDocumentFlushed(
          () => sidebarController.sidebarMain.clientWidth
        );
        requestAnimationFrame(() => {
          for (const t of movingTabs) {
            t.style.width = width + "px";
          }
          // Allow scrollboxes to grow to expanded sidebar width.
          this._tabbrowserTabs.arrowScrollbox.scrollbox.style.width = "";
          this._tabbrowserTabs.pinnedTabsContainer.scrollbox.style.width = "";
        });
      });
    }

    // Handle the new tab button filling the space when the dragged tab
    // position becomes absolute
    if (!this._tabbrowserTabs.overflowing && !isPinned) {
      if (this._tabbrowserTabs.verticalMode) {
        periphery.style.top = `${rect.height}px`;
      } else if (this._rtlMode) {
        periphery.style.left = `${-rect.width}px`;
      } else {
        periphery.style.left = `${rect.width}px`;
      }
    }
  }

  /**
   * Move together all selected tabs around the tab in param.
   */
  _moveTogetherSelectedTabs(tab) {
    let selectedElement = elementToMove(tab);
    let selectedElements = gBrowser.selectedElements;
    let tabIndex = selectedElements.indexOf(selectedElement);
    if (selectedElements.some(t => t.pinned != tab.pinned)) {
      throw new Error(
        "Cannot move together a mix of pinned and unpinned tabs."
      );
    }
    let isGrid = this._tabbrowserTabs.isContainerVerticalPinnedGrid(tab);
    let animate = !gReduceMotion;

    tab._moveTogetherSelectedTabsData = {
      finished: !animate,
    };

    tab.toggleAttribute("multiselected-move-together", true);

    let addAnimationData = movingElement => {
      movingElement._moveTogetherSelectedTabsData = {
        translateX: 0,
        translateY: 0,
        animate: true,
      };
      movingElement.toggleAttribute("multiselected-move-together", true);

      let postTransitionCleanup = () => {
        movingElement._moveTogetherSelectedTabsData.animate = false;
      };
      if (gReduceMotion) {
        postTransitionCleanup();
      } else {
        let onTransitionEnd = transitionendEvent => {
          if (
            transitionendEvent.propertyName != "transform" ||
            transitionendEvent.originalTarget != movingElement
          ) {
            return;
          }
          movingElement.removeEventListener("transitionend", onTransitionEnd);
          postTransitionCleanup();
        };

        movingElement.addEventListener("transitionend", onTransitionEnd);
      }

      let tabRect = selectedElement.getBoundingClientRect();
      let movingTabRect = movingElement.getBoundingClientRect();
      movingElement._moveTogetherSelectedTabsData.translateX =
        tabRect.x - movingTabRect.x;
      movingElement._moveTogetherSelectedTabsData.translateY =
        tabRect.y - movingTabRect.y;
    };

    let selectedIndices = selectedElements.map(t => t.elementIndex);
    let currentIndex = 0;
    let draggedRect = selectedElement.getBoundingClientRect();
    let translateX = 0;
    let translateY = 0;

    // The currentIndex represents the indexes for all visible tab strip items after the
    // selected tabs have moved together. These values make the math in _animateTabMove and
    // _animateExpandedPinnedTabMove possible and less prone to edge cases when dragging
    // multiple tabs.
    for (let unmovingTab of this._tabbrowserTabs.dragAndDropElements) {
      if (unmovingTab.multiselected) {
        unmovingTab.currentIndex = selectedElement.elementIndex;
        // Skip because this multiselected tab should
        // be shifted towards the dragged Tab.
        continue;
      }
      if (unmovingTab.elementIndex > selectedIndices[currentIndex]) {
        while (
          selectedIndices[currentIndex + 1] &&
          unmovingTab.elementIndex > selectedIndices[currentIndex + 1]
        ) {
          let currentRect = selectedElements
            .find(t => t.elementIndex == selectedIndices[currentIndex])
            .getBoundingClientRect();
          // For everything but the grid, we need to work out the shift required based
          // on the size of the tabs being dragged together.
          translateY -= currentRect.height;
          translateX -= currentRect.width;
          currentIndex++;
        }

        // Find the new index of the tab once selected tabs have moved together to use
        // for positioning and animation
        let isAfterDraggedTab =
          unmovingTab.elementIndex - currentIndex >
          selectedElement.elementIndex;
        let newIndex = isAfterDraggedTab
          ? unmovingTab.elementIndex - currentIndex
          : unmovingTab.elementIndex - currentIndex - 1;
        let newTranslateX = isAfterDraggedTab
          ? translateX
          : translateX - draggedRect.width;
        let newTranslateY = isAfterDraggedTab
          ? translateY
          : translateY - draggedRect.height;
        unmovingTab.currentIndex = newIndex;
        unmovingTab._moveTogetherSelectedTabsData = {
          translateX: 0,
          translateY: 0,
        };
        if (isGrid) {
          // For the grid, use the position of the tab with the old index to dictate the
          // translation needed for the background tab with the new index to move there.
          let unmovingTabRect = unmovingTab.getBoundingClientRect();
          let oldTabRect =
            this._tabbrowserTabs.dragAndDropElements[
              newIndex
            ].getBoundingClientRect();
          unmovingTab._moveTogetherSelectedTabsData.translateX =
            oldTabRect.x - unmovingTabRect.x;
          unmovingTab._moveTogetherSelectedTabsData.translateY =
            oldTabRect.y - unmovingTabRect.y;
        } else if (this._tabbrowserTabs.verticalMode) {
          unmovingTab._moveTogetherSelectedTabsData.translateY = newTranslateY;
        } else {
          unmovingTab._moveTogetherSelectedTabsData.translateX = newTranslateX;
        }
      } else {
        unmovingTab.currentIndex = unmovingTab.elementIndex;
      }
    }

    // Animate left or top selected tabs
    for (let i = 0; i < tabIndex; i++) {
      let movingElement = selectedElements[i];
      addAnimationData(movingElement);
    }
    // Animate right or bottom selected tabs
    for (let i = selectedElements.length - 1; i > tabIndex; i--) {
      let movingElement = selectedElements[i];
      addAnimationData(movingElement);
    }

    // Slide the relevant tabs to their new position.
    // non-moving tabs adjust for RTL
    for (let item of this._tabbrowserTabs.dragAndDropElements) {
      if (
        !tab._dragData.movingTabsSet.has(item) &&
        (item._moveTogetherSelectedTabsData?.translateX ||
          item._moveTogetherSelectedTabsData?.translateY) &&
        ((item.pinned && tab.pinned) || (!item.pinned && !tab.pinned))
      ) {
        let element = elementToMove(item);
        if (isGrid) {
          element.style.transform = `translate(${(this._rtlMode ? -1 : 1) * item._moveTogetherSelectedTabsData.translateX}px, ${item._moveTogetherSelectedTabsData.translateY}px)`;
        } else if (this._tabbrowserTabs.verticalMode) {
          element.style.transform = `translateY(${item._moveTogetherSelectedTabsData.translateY}px)`;
        } else {
          element.style.transform = `translateX(${(this._rtlMode ? -1 : 1) * item._moveTogetherSelectedTabsData.translateX}px)`;
        }
      }
    }
    // moving tabs don't adjust for RTL
    for (let item of selectedElements) {
      if (
        item._moveTogetherSelectedTabsData?.translateX ||
        item._moveTogetherSelectedTabsData?.translateY
      ) {
        let element = elementToMove(item);
        element.style.transform = `translate(${item._moveTogetherSelectedTabsData.translateX}px, ${item._moveTogetherSelectedTabsData.translateY}px)`;
      }
    }
  }

  #isAnimatingMoveTogetherSelectedTabs() {
    for (let element of gBrowser.selectedElements) {
      if (element._moveTogetherSelectedTabsData?.animate) {
        return true;
      }
    }
    return false;
  }

  finishMoveTogetherSelectedTabs(tab) {
    if (
      !tab._moveTogetherSelectedTabsData ||
      (tab._moveTogetherSelectedTabsData.finished && !gReduceMotion)
    ) {
      return;
    }

    if (tab._moveTogetherSelectedTabsData) {
      tab._moveTogetherSelectedTabsData.finished = true;
    }

    let selectedElements = gBrowser.selectedElements;
    let tabIndex = selectedElements.indexOf(tab);
    // Moving left or top tabs
    for (let i = 0; i < tabIndex; i++) {
      gBrowser.moveTabBefore(selectedElements[i], tab);
    }

    // Moving right or bottom tabs
    for (let i = selectedElements.length - 1; i > tabIndex; i--) {
      gBrowser.moveTabAfter(selectedElements[i], tab);
    }

    for (let item of this._tabbrowserTabs.dragAndDropElements) {
      delete item._moveTogetherSelectedTabsData;
      item = elementToMove(item);
      item.style.transform = "";
      item.removeAttribute("multiselected-move-together");
    }
  }

  // Drag over

  _animateExpandedPinnedTabMove(event) {
    let draggedTab = event.dataTransfer.mozGetDataAt(TAB_DROP_TYPE, 0);
    let dragData = draggedTab._dragData;
    let movingTabs = dragData.movingTabs;

    dragData.animLastScreenX ??= dragData.screenX;
    dragData.animLastScreenY ??= dragData.screenY;

    let screenPosX = event.screenX;
    let screenPosY = event.screenY;

    if (
      screenPosY == dragData.animLastScreenY &&
      screenPosX == dragData.animLastScreenX
    ) {
      return;
    }

    let tabs = this._tabbrowserTabs.visibleTabs.slice(
      0,
      gBrowser.pinnedTabCount
    );

    dragData.animLastScreenY = screenPosY;
    dragData.animLastScreenX = screenPosX;

    let { width: tabWidth, height: tabHeight } =
      draggedTab.getBoundingClientRect();
    let shiftSizeX = tabWidth;
    let shiftSizeY = tabHeight;

    // Move the dragged tab based on the mouse position.
    let periphery = document.getElementById(
      "tabbrowser-arrowscrollbox-periphery"
    );
    let endScreenX = draggedTab.screenX + tabWidth;
    let endScreenY = draggedTab.screenY + tabHeight;
    let startScreenX = draggedTab.screenX;
    let startScreenY = draggedTab.screenY;
    let translateX = screenPosX - dragData.screenX;
    let translateY = screenPosY - dragData.screenY;
    let startBoundX = this._tabbrowserTabs.screenX - startScreenX;
    let startBoundY = this._tabbrowserTabs.screenY - startScreenY;
    let endBoundX =
      this._tabbrowserTabs.screenX +
      window.windowUtils.getBoundsWithoutFlushing(this._tabbrowserTabs).width -
      endScreenX;
    let endBoundY = periphery.screenY - endScreenY;
    translateX = Math.min(Math.max(translateX, startBoundX), endBoundX);
    translateY = Math.min(Math.max(translateY, startBoundY), endBoundY);

    // Center the tab under the cursor if the tab is not under the cursor while dragging
    if (
      screen < draggedTab.screenY + translateY ||
      screen > draggedTab.screenY + tabHeight + translateY
    ) {
      translateY = screen - draggedTab.screenY - tabHeight / 2;
    }

    for (let tab of movingTabs) {
      tab.style.transform = `translate(${translateX}px, ${translateY}px)`;
    }

    // Determine what tab we're dragging over.
    // * Single tab dragging: Point of reference is the center of the dragged tab. If that
    //   point touches a background tab, the dragged tab would take that
    //   tab's position when dropped.
    // * Multiple tabs dragging: Tabs are stacked, so we can still use the above
    //   point of reference, the center of the dragged tab.
    // * We're doing a binary search in order to reduce the amount of
    //   tabs we need to check.

    tabs = tabs.filter(t => !movingTabs.includes(t) || t == draggedTab);
    let tabCenterX = startScreenX + translateX + tabWidth / 2;
    let tabCenterY = startScreenY + translateY + tabHeight / 2;

    let shiftNumber = this._maxTabsPerRow - 1;

    let getTabShift = (tab, dropIndex) => {
      if (tab?.currentIndex == undefined) {
        tab.currentIndex = tab.elementIndex;
      }
      if (
        tab.currentIndex < draggedTab.elementIndex &&
        tab.currentIndex >= dropIndex
      ) {
        // If tab is at the end of a row, shift back and down
        let tabRow = Math.ceil((tab.currentIndex + 1) / this._maxTabsPerRow);
        let shiftedTabRow = Math.ceil(
          (tab.currentIndex + 2) / this._maxTabsPerRow
        );
        if (tab.currentIndex && tabRow != shiftedTabRow) {
          return [
            RTL_UI ? tabWidth * shiftNumber : -tabWidth * shiftNumber,
            shiftSizeY,
          ];
        }
        return [RTL_UI ? -shiftSizeX : shiftSizeX, 0];
      }
      if (
        tab.currentIndex > draggedTab.elementIndex &&
        tab.currentIndex < dropIndex
      ) {
        // If tab is not index 0 and at the start of a row, shift across and up
        let tabRow = Math.floor(tab.currentIndex / this._maxTabsPerRow);
        let shiftedTabRow = Math.floor(
          (tab.currentIndex - 1) / this._maxTabsPerRow
        );
        if (tab.currentIndex && tabRow != shiftedTabRow) {
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
    let oldIndex = dragData.animDropElementIndex ?? draggedTab.elementIndex;

    while (low <= high) {
      let mid = Math.floor((low + high) / 2);
      if (tabs[mid] == draggedTab && ++mid > high) {
        break;
      }
      let [shiftX, shiftY] = getTabShift(tabs[mid], oldIndex);
      screenPosX = tabs[mid].screenX + shiftX;
      screenPosY = tabs[mid].screenY + shiftY;

      if (screenPosY + tabHeight < tabCenterY) {
        low = mid + 1;
      } else if (screenPosY > tabCenterY) {
        high = mid - 1;
      } else if (
        RTL_UI ? screenPosX + tabWidth < tabCenterX : screenPosX > tabCenterX
      ) {
        high = mid - 1;
      } else if (
        RTL_UI ? screenPosX > tabCenterX : screenPosX + tabWidth < tabCenterX
      ) {
        low = mid + 1;
      } else {
        newIndex = tabs[mid].currentIndex;
        break;
      }
    }

    if (newIndex >= oldIndex && newIndex < tabs.length) {
      newIndex++;
    }

    if (newIndex < 0) {
      newIndex = oldIndex;
    }

    if (newIndex == dragData.animDropElementIndex) {
      return;
    }

    dragData.animDropElementIndex = newIndex;
    dragData.dropElement = tabs[Math.min(newIndex, tabs.length - 1)];
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

  // eslint-disable-next-line complexity
  _animateTabMove(event) {
    let draggedTab = event.dataTransfer.mozGetDataAt(TAB_DROP_TYPE, 0);
    let dragData = draggedTab._dragData;
    let movingTabs = dragData.movingTabs;
    let movingTabsSet = dragData.movingTabsSet;

    dragData.animLastScreenPos ??= this._tabbrowserTabs.verticalMode
      ? dragData.screenY
      : dragData.screenX;
    let screenPos = this._tabbrowserTabs.verticalMode
      ? event.screenY
      : event.screenX;
    if (screenPos == dragData.animLastScreenPos) {
      return;
    }
    let screenForward = screenPos > dragData.animLastScreenPos;
    dragData.animLastScreenPos = screenPos;

    this._clearDragOverGroupingTimer();
    this.#clearPinnedDropIndicatorTimer();

    let isPinned = draggedTab.pinned;
    let numPinned = gBrowser.pinnedTabCount;
    let dragAndDropElements = this._tabbrowserTabs.dragAndDropElements;
    let tabs = dragAndDropElements.slice(
      isPinned ? 0 : numPinned,
      isPinned ? numPinned : undefined
    );

    if (this._rtlMode) {
      tabs.reverse();
    }

    let bounds = ele => window.windowUtils.getBoundsWithoutFlushing(ele);
    let logicalForward = screenForward != this._rtlMode;
    let screenAxis = this._tabbrowserTabs.verticalMode ? "screenY" : "screenX";
    let size = this._tabbrowserTabs.verticalMode ? "height" : "width";
    let translateAxis = this._tabbrowserTabs.verticalMode
      ? "translateY"
      : "translateX";

    // Move the dragged tab based on the mouse position.
    let periphery = document.getElementById(
      "tabbrowser-arrowscrollbox-periphery"
    );
    let endEdge = ele => ele[screenAxis] + bounds(ele)[size];
    let endScreen = endEdge(draggedTab);
    let startScreen = draggedTab[screenAxis];
    let { width: tabWidth, height: tabHeight } = bounds(
      elementToMove(draggedTab)
    );
    let tabSize = this._tabbrowserTabs.verticalMode ? tabHeight : tabWidth;
    let shiftSize = tabSize;
    let translate = screenPos - dragData[screenAxis];

    // Constrain the range over which the moving tabs can move between the edge of the tabstrip and periphery.
    // Add 1 to periphery so we don't overlap it.
    let startBound = this._rtlMode
      ? endEdge(periphery) + 1 - startScreen
      : this._tabbrowserTabs[screenAxis] - startScreen;
    let endBound = this._rtlMode
      ? endEdge(this._tabbrowserTabs) - endScreen
      : periphery[screenAxis] - 1 - endScreen;
    translate = Math.min(Math.max(translate, startBound), endBound);

    // Center the tab under the cursor if the tab is not under the cursor while dragging
    let draggedTabScreenAxis = draggedTab[screenAxis] + translate;
    if (
      (screenPos < draggedTabScreenAxis ||
        screenPos > draggedTabScreenAxis + tabSize) &&
      draggedTabScreenAxis + tabSize < endBound &&
      draggedTabScreenAxis > startBound
    ) {
      translate = screenPos - draggedTab[screenAxis] - tabSize / 2;
      // Ensure, after the above calculation, we are still within bounds
      translate = Math.min(Math.max(translate, startBound), endBound);
    }

    if (!gBrowser.pinnedTabCount && !this._dragToPinPromoCard.shouldRender) {
      let pinnedDropIndicatorMargin = parseFloat(
        window.getComputedStyle(this._pinnedDropIndicator).marginInline
      );
      this._checkWithinPinnedContainerBounds({
        firstMovingTabScreen: startScreen,
        lastMovingTabScreen: endScreen,
        pinnedTabsStartEdge: this._rtlMode
          ? endEdge(this._tabbrowserTabs.arrowScrollbox) +
            pinnedDropIndicatorMargin
          : this._tabbrowserTabs[screenAxis],
        pinnedTabsEndEdge: this._rtlMode
          ? endEdge(this._tabbrowserTabs)
          : this._tabbrowserTabs.arrowScrollbox[screenAxis] -
            pinnedDropIndicatorMargin,
        translate,
        draggedTab,
      });
    }

    for (let item of movingTabs) {
      item = elementToMove(item);
      item.style.transform = `${translateAxis}(${translate}px)`;
    }

    dragData.translatePos = translate;

    tabs = tabs.filter(t => !movingTabsSet.has(t) || t == draggedTab);

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
      if (item?.currentIndex == undefined) {
        item.currentIndex = item.elementIndex;
      }
      if (
        item.currentIndex < draggedTab.elementIndex &&
        item.currentIndex >= dropElementIndex
      ) {
        return this._rtlMode ? -shiftSize : shiftSize;
      }
      if (
        item.currentIndex > draggedTab.elementIndex &&
        item.currentIndex < dropElementIndex
      ) {
        return this._rtlMode ? shiftSize : -shiftSize;
      }
      return 0;
    };

    let oldDropElementIndex =
      dragData.animDropElementIndex ?? draggedTab.elementIndex;

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
      let point = (screenForward ? endScreen : startScreen) + translate;
      let low = 0;
      let high = tabs.length - 1;
      while (low <= high) {
        let mid = Math.floor((low + high) / 2);
        if (tabs[mid] == draggedTab && ++mid > high) {
          break;
        }
        let element = tabs[mid];
        let elementForSize = elementToMove(element);
        screenPos =
          elementForSize[screenAxis] +
          getTabShift(element, oldDropElementIndex);

        if (screenPos > point) {
          high = mid - 1;
        } else if (screenPos + bounds(elementForSize)[size] < point) {
          low = mid + 1;
        } else {
          return element;
        }
      }
      return null;
    };

    let dropElement = getOverlappedElement();

    let newDropElementIndex;
    if (dropElement) {
      newDropElementIndex =
        dropElement?.currentIndex ?? dropElement.elementIndex;
    } else {
      // When the dragged element(s) moves past a tab strip item, the dragged
      // element's leading edge starts dragging over empty space, resulting in
      // no overlapping `dropElement`. In these cases, try to fall back to the
      // previous animation drop element index to avoid unstable animations
      // (tab strip items snapping back and forth to shift out of the way of
      // the dragged element(s)).
      newDropElementIndex = oldDropElementIndex;

      // We always want to have a `dropElement` so that we can determine where to
      // logically drop the dragged element(s).
      //
      // It's tempting to set `dropElement` to
      // `this.dragAndDropElements.at(oldDropElementIndex)`, and that is correct
      // for most cases, but there are edge cases:
      //
      // 1) the drop element index range needs to be one larger than the number of
      //    items that can move in the tab strip. The simplest example is when all
      //    tabs are ungrouped and unpinned: for 5 tabs, the drop element index needs
      //    to be able to go from 0 (become the first tab) to 5 (become the last tab).
      //    `this.dragAndDropElements.at(5)` would be `undefined` when dragging to the
      //    end of the tab strip. In this specific case, it works to fall back to
      //    setting the drop element to the last tab.
      //
      // 2) the `elementIndex` values of the tab strip items do not change during
      //    the drag operation. When dragging the last tab or multiple tabs at the end
      //    of the tab strip, having `dropElement` fall back to the last tab makes the
      //    drop element one of the moving tabs. This can have some unexpected behavior
      //    if not careful. Falling back to the last tab that's not moving (instead of
      //    just the last tab) helps ensure that `dropElement` is always a stable target
      //    to drop next to.
      //
      // 3) all of the elements in the tab strip are moving, in which case there can't
      //    be a drop element and it should stay `undefined`.
      //
      // 4) we just started dragging and the `oldDropElementIndex` has its default
      //    valuë of `movingTabs[0].elementIndex`. In this case, the drop element
      //    shouldn't be a moving tab, so keep it `undefined`.
      let lastPossibleDropElement = this._rtlMode
        ? tabs.find(t => t != draggedTab)
        : tabs.findLast(t => t != draggedTab);
      let maxElementIndexForDropElement =
        lastPossibleDropElement?.currentIndex ??
        lastPossibleDropElement?.elementIndex;
      if (Number.isInteger(maxElementIndexForDropElement)) {
        let index = Math.min(
          oldDropElementIndex,
          maxElementIndexForDropElement
        );
        let oldDropElementCandidate = this._tabbrowserTabs.dragAndDropElements
          .filter(t => !movingTabsSet.has(t) || t == draggedTab)
          .at(index);
        if (!movingTabsSet.has(oldDropElementCandidate)) {
          dropElement = oldDropElementCandidate;
        }
      }
    }

    let moveOverThreshold;
    let overlapPercent;
    let dropBefore;
    if (dropElement) {
      let dropElementForOverlap = elementToMove(dropElement);

      let dropElementScreen = dropElementForOverlap[screenAxis];
      let dropElementPos =
        dropElementScreen + getTabShift(dropElement, oldDropElementIndex);
      let dropElementSize = bounds(dropElementForOverlap)[size];
      let firstMovingTabPos = startScreen + translate;
      overlapPercent = greatestOverlap(
        firstMovingTabPos,
        shiftSize,
        dropElementPos,
        dropElementSize
      );

      moveOverThreshold = Tabbrowser.prefs.tabGroupsEnabled
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
      if (this._rtlMode) {
        dropBefore = !dropBefore;
      }

      // If dragging a group over another group, don't make it look like it is
      // possible to drop the dragged group inside the other group.
      if (
        isTabGroupLabel(draggedTab) &&
        dropElement?.group &&
        (!dropElement.group.collapsed ||
          (dropElement.group.collapsed && dropElement.group.hasActiveTab))
      ) {
        let overlappedGroup = dropElement.group;

        if (isTabGroupLabel(dropElement)) {
          dropBefore = true;
          newDropElementIndex =
            dropElement?.currentIndex ?? dropElement.elementIndex;
        } else {
          dropBefore = false;
          let lastVisibleTabInGroup =
            overlappedGroup.tabsAndSplitViews.findLast(ele => ele.visible);
          newDropElementIndex =
            (lastVisibleTabInGroup?.currentIndex ??
              lastVisibleTabInGroup.elementIndex) + 1;
        }

        dropElement = overlappedGroup;
      }

      // Constrain drop direction at the boundary between pinned and
      // unpinned tabs so that they don't mix together.
      let isOutOfBounds = isPinned
        ? dropElement.elementIndex >= numPinned
        : dropElement.elementIndex < numPinned;
      if (isOutOfBounds) {
        // Drop after last pinned tab
        dropElement = this._tabbrowserTabs.dragAndDropElements[numPinned - 1];
        dropBefore = false;
      }
    }

    if (
      Tabbrowser.prefs.tabGroupsEnabled &&
      (isTab(draggedTab) || isSplitViewWrapper(draggedTab)) &&
      !isPinned &&
      (!numPinned || newDropElementIndex >= numPinned)
    ) {
      let dragOverGroupingThreshold = 1 - moveOverThreshold;
      let groupingDelay = Services.prefs.getIntPref(
        "browser.tabs.dragDrop.createGroup.delayMS"
      );

      // When dragging tab(s) over an ungrouped tab, signal to the user
      // that dropping the tab(s) will create a new tab group.
      let shouldCreateGroupOnDrop =
        Services.prefs.getBoolPref(
          "browser.tabs.dragDrop.createGroup.enabled"
        ) &&
        !movingTabsSet.has(dropElement) &&
        (isTab(dropElement) || isSplitViewWrapper(dropElement)) &&
        !dropElement?.group &&
        overlapPercent > dragOverGroupingThreshold;

      // When dragging tab(s) over a collapsed tab group label, signal to the
      // user that dropping the tab(s) will add them to the group.
      let shouldDropIntoCollapsedTabGroup =
        isTabGroupLabel(dropElement) &&
        dropElement.group.collapsed &&
        overlapPercent > dragOverGroupingThreshold;

      if (shouldCreateGroupOnDrop) {
        this._dragOverGroupingTimer = setTimeout(() => {
          this._triggerDragOverGrouping(dropElement);
          dragData.shouldCreateGroupOnDrop = true;
          this._setDragOverGroupColor(dragData.tabGroupCreationColor);
        }, groupingDelay);
      } else if (shouldDropIntoCollapsedTabGroup) {
        this._dragOverGroupingTimer = setTimeout(() => {
          this._triggerDragOverGrouping(dropElement);
          dragData.shouldDropIntoCollapsedTabGroup = true;
          this._setDragOverGroupColor(dropElement.group.color);
        }, groupingDelay);
      } else {
        this._tabbrowserTabs.removeAttribute("movingtab-group");
        this._resetGroupTarget(
          document.querySelector("[dragover-groupTarget]")
        );

        delete dragData.shouldCreateGroupOnDrop;
        delete dragData.shouldDropIntoCollapsedTabGroup;

        // Default to dropping into `dropElement`'s tab group, if it exists.
        let dropElementGroup = dropElement?.group;
        let colorCode = dropElementGroup?.color;

        let lastUnmovingTabInGroup = dropElementGroup?.tabs.findLast(
          t => !movingTabsSet.has(t)
        );
        if (
          isTab(dropElement) &&
          dropElementGroup &&
          dropElement == lastUnmovingTabInGroup &&
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
        this._setDragOverGroupColor(colorCode);
        this._tabbrowserTabs.toggleAttribute("movingtab-addToGroup", colorCode);
        this._tabbrowserTabs.toggleAttribute("movingtab-ungroup", !colorCode);
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
      item = elementToMove(item);
      item.style.transform = transform;
    }
  }

  _checkWithinPinnedContainerBounds({
    firstMovingTabScreen,
    lastMovingTabScreen,
    pinnedTabsStartEdge,
    pinnedTabsEndEdge,
    translate,
    draggedTab,
  }) {
    // Display the pinned drop indicator based on the position of the moving tabs.
    // If the indicator is not yet shown, display once we are within a pinned tab width/height
    // distance.
    let firstMovingTabPosition = firstMovingTabScreen + translate;
    let lastMovingTabPosition = lastMovingTabScreen + translate;
    // Approximation of half pinned tabs width and height in horizontal or grid mode (40) is a sufficient
    // buffer to display the pinned drop indicator slightly before dragging over it. Exact value is
    // not necessary.
    let buffer = 20;
    let inPinnedRange = this._rtlMode
      ? lastMovingTabPosition >= pinnedTabsStartEdge
      : firstMovingTabPosition <= pinnedTabsEndEdge;
    let inVisibleRange = this._rtlMode
      ? lastMovingTabPosition >= pinnedTabsStartEdge - buffer
      : firstMovingTabPosition <= pinnedTabsEndEdge + buffer;
    let isVisible = this._pinnedDropIndicator.hasAttribute("visible");
    let isInteractive = this._pinnedDropIndicator.hasAttribute("interactive");

    if (
      this.#pinnedDropIndicatorTimeout &&
      !inPinnedRange &&
      !inVisibleRange &&
      !isVisible &&
      !isInteractive
    ) {
      this.#resetPinnedDropIndicator();
    } else if (
      isTab(draggedTab) &&
      ((inVisibleRange && !isVisible) || (inPinnedRange && !isInteractive))
    ) {
      // On drag into pinned container
      let tabbrowserTabsRect = window.windowUtils.getBoundsWithoutFlushing(
        this._tabbrowserTabs
      );
      if (!this._tabbrowserTabs.verticalMode) {
        // The tabbrowser container expands with the expansion of the
        // drop indicator - prevent that by setting maxWidth first.
        this._tabbrowserTabs.style.maxWidth = tabbrowserTabsRect.width + "px";
      }
      if (isVisible) {
        this._pinnedDropIndicator.setAttribute("interactive", "");
      } else if (!this.#pinnedDropIndicatorTimeout) {
        let interactionDelay = Services.prefs.getIntPref(
          "browser.tabs.dragDrop.pinInteractionCue.delayMS"
        );
        this.#pinnedDropIndicatorTimeout = setTimeout(() => {
          if (this.#isMovingTab()) {
            this._pinnedDropIndicator.setAttribute("visible", "");
            this._pinnedDropIndicator.setAttribute("interactive", "");
          }
        }, interactionDelay);
      }
    } else if (!inPinnedRange) {
      this._pinnedDropIndicator.removeAttribute("interactive");
    }
  }

  #clearPinnedDropIndicatorTimer() {
    if (this.#pinnedDropIndicatorTimeout) {
      clearTimeout(this.#pinnedDropIndicatorTimeout);
      this.#pinnedDropIndicatorTimeout = null;
    }
  }

  #resetPinnedDropIndicator() {
    this.#clearPinnedDropIndicatorTimer();
    this._pinnedDropIndicator.removeAttribute("visible");
    this._pinnedDropIndicator.removeAttribute("interactive");
  }

  finishAnimateTabMove() {
    if (!this.#isMovingTab()) {
      return;
    }

    this.#setMovingTabMode(false);

    for (let item of this._tabbrowserTabs.dragAndDropElements) {
      elementToMove(item).style.transform = "";
    }
    this.#resetDragFeedback();
  }

  /**
   * @returns {Map<Element, DOMRect>}
   *   Where each tab strip item currently appears, transforms included.
   */
  #getItemRects() {
    return new Map(
      this._tabbrowserTabs.dragAndDropElements.map(item => {
        item = elementToMove(item);
        return [item, item.getBoundingClientRect()];
      })
    );
  }

  /**
   * Slides tab strip items from where they appeared before a drop to where
   * the drop put them.
   *
   * @param {Map<Element, DOMRect>} rectsBeforeDrop
   *   What #getItemRects returned before the drop.
   */
  #animateDrop(rectsBeforeDrop) {
    let offsets = [];
    for (let [item, before] of rectsBeforeDrop) {
      let after = item.getBoundingClientRect();
      let x = before.x - after.x;
      let y = before.y - after.y;
      if (x || y) {
        offsets.push({ item, x, y });
      }
    }
    if (!offsets.length) {
      return;
    }

    for (let { item, x, y } of offsets) {
      item.style.transform = `translate(${x}px, ${y}px)`;
    }
    // Flush styles so that the offsets are where the transitions start.
    offsets[0].item.getBoundingClientRect();

    for (let { item } of offsets) {
      let onTransitionDone = event => {
        if (event.originalTarget != item || event.propertyName != "transform") {
          return;
        }
        item.removeEventListener("transitionend", onTransitionDone);
        item.removeEventListener("transitioncancel", onTransitionDone);
        item.removeAttribute("tabdrop-samewindow");
      };
      item.addEventListener("transitionend", onTransitionDone);
      item.addEventListener("transitioncancel", onTransitionDone);
      item.toggleAttribute("tabdrop-samewindow", true);
      item.style.transform = "";
    }
  }

  /**
   * Whether tab strip items are sliding into place after a drop.
   *
   * @type {boolean}
   */
  get isAnimatingDrop() {
    return !!this._tabbrowserTabs.querySelector("[tabdrop-samewindow]");
  }

  /**
   * Scrolls the selected tab into view where the drop animation leaves it.
   */
  scrollSelectedTabIntoViewAfterDrop() {
    let selectedTab = gBrowser.selectedTab;
    if (selectedTab.pinned) {
      return;
    }
    let item = elementToMove(selectedTab);
    let { arrowScrollbox, verticalMode } = this._tabbrowserTabs;
    let [start, end] = arrowScrollbox.startEndProps;
    let transform = new DOMMatrix(getComputedStyle(item).transform);
    let offset = verticalMode ? transform.m42 : transform.m41;
    let itemRect = item.getBoundingClientRect();
    let scrollRect = arrowScrollbox.scrollClientRect;
    let hiddenAtStart = scrollRect[start] - (itemRect[start] - offset);
    let hiddenAtEnd = itemRect[end] - offset - scrollRect[end];
    if (hiddenAtStart > 0) {
      arrowScrollbox.scrollByPixels(-hiddenAtStart, true);
    } else if (hiddenAtEnd > 0) {
      arrowScrollbox.scrollByPixels(hiddenAtEnd, true);
    }
  }

  #finishDropAnimation() {
    cancelAnimationFrame(this.#dropAnimationFrame);
    this.#dropAnimationFrame = 0;
    let animatingItems = this._tabbrowserTabs.dragAndDropElements
      .map(elementToMove)
      .filter(item => item.hasAttribute("tabdrop-samewindow"));
    if (!animatingItems.length) {
      return;
    }
    for (let item of animatingItems) {
      item.removeAttribute("tabdrop-samewindow");
    }
    // Drag code measures items without flushing, so settle them first.
    animatingItems[0].getBoundingClientRect();
  }

  // Drop

  /**
   * @param {MozTabbrowserTab|typeof MozTabbrowserTabGroup.labelElement} [draggedTab]
   *   The dragged item, which still belongs to the window the drag started in
   *   even if it was dropped in another one.
   */
  _resetTabsAfterDrop(draggedTab) {
    let draggedTabDocument = draggedTab?.ownerDocument ?? document;
    if (this._tabbrowserTabs.expandOnHover) {
      // Re-enable MousePosTracker after dropping
      MousePosTracker.addListener(document.defaultView.SidebarController);
    }

    let pinnedDropIndicator = draggedTabDocument.getElementById(
      "pinned-drop-indicator"
    );
    let draggedTabContainer =
      draggedTabDocument.documentGlobal.gBrowser.tabContainer;
    pinnedDropIndicator.removeAttribute("visible");
    pinnedDropIndicator.removeAttribute("interactive");
    draggedTabContainer.style.maxWidth = "";
    let allTabs = draggedTabDocument.getElementsByClassName("tabbrowser-tab");
    for (let tab of allTabs) {
      tab.style.width = "";
      tab.style.left = "";
      tab.style.top = "";
      tab.style.maxWidth = "";
      tab.style.pointerEvents = "";
      tab.removeAttribute("dragtarget");
      tab.removeAttribute("small-stack");
      tab.removeAttribute("big-stack");
    }
    for (let label of draggedTabDocument.getElementsByClassName(
      "tab-group-label-container"
    )) {
      label.style.width = "";
      label.style.maxWidth = "";
      label.style.height = "";
      label.style.left = "";
      label.style.top = "";
      label.style.pointerEvents = "";
      label.removeAttribute("dragtarget");
    }
    for (let label of draggedTabContainer.getElementsByClassName(
      "tab-group-label"
    )) {
      delete label.currentIndex;
    }
    let periphery = draggedTabDocument.getElementById(
      "tabbrowser-arrowscrollbox-periphery"
    );
    periphery.style.left = "";
    periphery.style.top = "";
    // Space reserved for the tabs of a collapsed tab group is released by
    // _expandGroupOnDrop once they are back to their full size.
    if (!isTabGroupLabel(draggedTab) || !draggedTab.group.collapsedByDrag) {
      this.#releaseSpaceInScrolledContent(periphery);
    }
    let pinnedTabsContainer = draggedTabDocument.getElementById(
      "pinned-tabs-container"
    );
    let pinnedPeriphery = draggedTabDocument.getElementById(
      "pinned-tabs-container-periphery"
    );
    pinnedPeriphery && pinnedTabsContainer.removeChild(pinnedPeriphery);
    pinnedTabsContainer.removeAttribute("dragActive");
    pinnedTabsContainer.style.minHeight = "";
    draggedTabDocument.defaultView.SidebarController.updatePinnedTabsHeightOnResize();
    pinnedTabsContainer.scrollbox.style.height = "";
    pinnedTabsContainer.scrollbox.style.width = "";
    let arrowScrollbox = draggedTabDocument.getElementById(
      "tabbrowser-arrowscrollbox"
    );
    arrowScrollbox.scrollbox.style.height = "";
    arrowScrollbox.scrollbox.style.width = "";
    for (let groupLabel of draggedTabContainer.getElementsByClassName(
      "tab-group-label-container"
    )) {
      groupLabel.style.left = "";
      groupLabel.style.top = "";
    }
    for (let splitviewWrapper of draggedTabContainer.getElementsByTagName(
      "tab-split-view-wrapper"
    )) {
      splitviewWrapper.style.width = "";
      splitviewWrapper.style.maxWidth = "";
      splitviewWrapper.style.height = "";
      splitviewWrapper.style.left = "";
      splitviewWrapper.style.top = "";
      splitviewWrapper.style.pointerEvents = "";
      splitviewWrapper.removeAttribute("dragtarget");
      splitviewWrapper.removeAttribute("small-stack");
      splitviewWrapper.removeAttribute("big-stack");
    }
  }

  /**
   * @param {DragEvent} event
   * @returns {typeof DataTransfer.prototype.dropEffect}
   */
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
        (isTab(sourceNode) ||
          isTabGroupLabel(sourceNode) ||
          isSplitViewWrapper(sourceNode)) &&
        sourceNode.documentGlobal.isChromeWindow &&
        sourceNode.ownerDocument.documentElement.getAttribute("windowtype") ==
          "navigator:browser"
      ) {
        // Do not allow transfering a private tab to a non-private window
        // and vice versa.
        if (
          lazy.PrivateBrowsingUtils.isWindowPrivate(window) !=
          lazy.PrivateBrowsingUtils.isWindowPrivate(sourceNode.documentGlobal)
        ) {
          return "none";
        }

        if (
          window.gMultiProcessBrowser !=
          sourceNode.documentGlobal.gMultiProcessBrowser
        ) {
          return "none";
        }

        if (
          window.gFissionBrowser != sourceNode.documentGlobal.gFissionBrowser
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
}
