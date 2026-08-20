/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/**
 * Handle keyboard navigation for toolbars.
 * Having separate tab stops for every toolbar control results in an
 * unmanageable number of tab stops. Therefore, we group buttons under a single
 * tab stop and allow movement between them using left/right arrows.
 * However, text inputs use the arrow keys for their own purposes, so they need
 * their own tab stop. There are also groups of buttons before and after the
 * URL bar input which should get their own tab stop. The subsequent buttons on
 * the toolbar are then another tab stop after that.
 * Tab stops for groups of buttons are set using the <toolbartabstop/> element.
 * This element is invisible, but gets included in the tab order. When one of
 * these gets focus, it redirects focus to the appropriate button. This avoids
 * the need to continually manage the tabindex of toolbar buttons in response to
 * toolbarchanges.
 * In addition to linear navigation with tab and arrows, users can also type
 * the first (or first few) characters of a button's name to jump directly to
 * that button.
 * Controls can opt out of all of this with keyNav="false", or keep arrow
 * navigation but decline to be a tab stop's target with keyNav="skipTabStop",
 * which is for controls that are reachable from an adjacent tab stop.
 */

ToolbarKeyboardNavigator = {
  // Toolbars we want to be keyboard navigable.
  kToolbars: [
    CustomizableUI.AREA_TABSTRIP,
    CustomizableUI.AREA_NAVBAR,
    CustomizableUI.AREA_BOOKMARKS,
  ],
  // Delay (in ms) after which to clear any search text typed by the user if
  // the user hasn't typed anything further.
  kSearchClearTimeout: 1000,

  _isButton(aElem) {
    if (aElem.getAttribute("keyNav") === "false") {
      return false;
    }
    const XUL_NS =
      "http://www.mozilla.org/keymaster/gatekeeper/there.is.only.xul";
    const HTML_NS = "http://www.w3.org/1999/xhtml";
    return (
      (aElem.namespaceURI == XUL_NS && aElem.localName == "toolbarbutton") ||
      (aElem.namespaceURI == HTML_NS && aElem.localName == "moz-button") ||
      aElem.getAttribute("role") == "button"
    );
  },

  // Get a TreeWalker which includes only controls which should be keyboard
  // navigable.
  _getWalker(aRoot) {
    if (aRoot._toolbarKeyNavWalker) {
      return aRoot._toolbarKeyNavWalker;
    }

    let filter = aNode => {
      if (aNode.tagName == "toolbartabstop") {
        return NodeFilter.FILTER_ACCEPT;
      }

      // Special case for the "View site information" button, which isn't
      // actionable in some cases but is still visible.
      if (
        aNode.id == "identity-box" &&
        document.getElementById("urlbar").getAttribute("pageproxystate") ==
          "invalid"
      ) {
        return NodeFilter.FILTER_REJECT;
      }

      // Skip disabled elements.
      if (aNode.disabled) {
        return NodeFilter.FILTER_REJECT;
      }

      // Skip invisible elements.
      const visible = aNode.checkVisibility({
        checkVisibilityCSS: true,
        flush: false,
      });
      if (!visible) {
        return NodeFilter.FILTER_REJECT;
      }

      // This width check excludes the overflow button when there's no overflow.
      const bounds = window.windowUtils.getBoundsWithoutFlushing(aNode);
      if (bounds.width == 0) {
        return NodeFilter.FILTER_SKIP;
      }

      if (this._isButton(aNode)) {
        return NodeFilter.FILTER_ACCEPT;
      }
      return NodeFilter.FILTER_SKIP;
    };
    aRoot._toolbarKeyNavWalker = document.createTreeWalker(
      aRoot,
      NodeFilter.SHOW_ELEMENT,
      filter
    );
    return aRoot._toolbarKeyNavWalker;
  },

  _initTabStops(aRoot) {
    for (let stop of aRoot.getElementsByTagName("toolbartabstop")) {
      // These are invisible, but because they need to be in the tab order,
      // they can't get display: none or similar. They must therefore be
      // explicitly hidden for accessibility.
      stop.setAttribute("aria-hidden", "true");
      stop.addEventListener("focus", this);
    }
  },

  init() {
    if (!gToolbarKeyNavEnabled || this._initialized) {
      return;
    }
    this._initialized = true;
    for (let id of this.kToolbars) {
      let toolbar = document.getElementById(id);
      // When enabled, no toolbar buttons should themselves be tabbable.
      // We manage toolbar focus completely. This attribute ensures that CSS
      // doesn't set -moz-user-focus: normal.
      toolbar.setAttribute("keyNav", "true");
      this._initTabStops(toolbar);
      toolbar.addEventListener("keydown", this);
      toolbar.addEventListener("keypress", this);
    }
    CustomizableUI.addListener(this);
  },

  uninit() {
    if (!this._initialized) {
      return;
    }
    this._initialized = false;
    for (let id of this.kToolbars) {
      let toolbar = document.getElementById(id);
      for (let stop of toolbar.getElementsByTagName("toolbartabstop")) {
        stop.removeEventListener("focus", this);
      }
      toolbar.removeEventListener("keydown", this);
      toolbar.removeEventListener("keypress", this);
      toolbar.removeAttribute("keyNav");
    }
    CustomizableUI.removeListener(this);
  },

  // CustomizableUI event handler
  onWidgetAdded(aWidgetId, aArea) {
    if (!this.kToolbars.includes(aArea)) {
      return;
    }
    let widget = document.getElementById(aWidgetId);
    if (!widget) {
      return;
    }
    this._initTabStops(widget);
  },

  _focusButton(aButton) {
    if (aButton.hasAttribute("tabindex")) {
      // The button manages its own tabindex.
      aButton.focus();
      return;
    }
    // Toolbar buttons aren't focusable because if they were, clicking them
    // would focus them, which is undesirable. Therefore, we must make a
    // button focusable only when we want to focus it.
    aButton.setAttribute("tabindex", "-1");
    aButton.focus();
    // We could remove tabindex now, but even though the button keeps DOM
    // focus, a11y gets confused because the button reports as not being
    // focusable. This results in weirdness if the user switches windows and
    // then switches back. It also means that focus can't be restored to the
    // button when a panel is closed. Instead, remove tabindex when the button
    // loses focus.
    aButton.addEventListener("blur", this);
  },

  _onButtonBlur(aEvent) {
    if (document.activeElement == aEvent.target) {
      // This event was fired because the user switched windows. This button
      // will get focus again when the user returns.
      return;
    }
    if (aEvent.target.getAttribute("open") == "true") {
      // The button activated a panel. The button should remain
      // focusable so that focus can be restored when the panel closes.
      return;
    }
    aEvent.target.removeEventListener("blur", this);
    aEvent.target.removeAttribute("tabindex");
  },

  _onTabStopFocus(aEvent) {
    let toolbar = aEvent.target.closest("toolbar");
    let walker = this._getWalker(toolbar);

    let oldFocus = aEvent.relatedTarget;
    if (oldFocus) {
      // Save this because we might rewind focus and the subsequent focus event
      // won't get a relatedTarget.
      this._isFocusMovingBackward =
        oldFocus.compareDocumentPosition(aEvent.target) &
        Node.DOCUMENT_POSITION_PRECEDING;
      if (this._isFocusMovingBackward && oldFocus && this._isButton(oldFocus)) {
        // Shift+tabbing from a button will land on its toolbartabstop. Skip it.
        document.commandDispatcher.rewindFocus();
        return;
      }
    }

    walker.currentNode = aEvent.target;
    let button = walker.nextNode();
    while (button?.getAttribute("keyNav") == "skipTabStop") {
      button = walker.nextNode();
    }
    if (!button || !this._isButton(button)) {
      // If we think we're moving backward, and focus came from outside the
      // toolbox, we might actually have wrapped around. In this case, the
      // event target was the first tabstop. If we can't find a button, e.g.
      // because we're in a popup where most buttons are hidden, we
      // should ensure focus keeps moving forward:
      if (
        this._isFocusMovingBackward &&
        (!oldFocus || !gNavToolbox.contains(oldFocus))
      ) {
        let allStops = Array.from(
          gNavToolbox.querySelectorAll("toolbartabstop")
        );
        // Find the previous toolbartabstop:
        let earlierVisibleStopIndex = allStops.indexOf(aEvent.target) - 1;
        // Then work out if any of the earlier ones are in a visible
        // toolbar:
        while (earlierVisibleStopIndex >= 0) {
          let stopToolbar =
            allStops[earlierVisibleStopIndex].closest("toolbar");
          if (!stopToolbar.collapsed) {
            break;
          }
          earlierVisibleStopIndex--;
        }
        // If we couldn't find any earlier visible stops, we're not moving
        // backwards, we're moving forwards and wrapped around:
        if (earlierVisibleStopIndex == -1) {
          this._isFocusMovingBackward = false;
        }
      }
      // No navigable buttons for this tab stop. Skip it.
      if (this._isFocusMovingBackward) {
        document.commandDispatcher.rewindFocus();
      } else {
        document.commandDispatcher.advanceFocus();
      }
      return;
    }

    this._focusButton(button);
  },

  navigateButtons(aToolbar, aPrevious) {
    let oldFocus = document.activeElement;
    let walker = this._getWalker(aToolbar);
    // Start from the current control and walk to the next/previous control.
    walker.currentNode = oldFocus;
    let newFocus;
    if (aPrevious) {
      newFocus = walker.previousNode();
    } else {
      newFocus = walker.nextNode();
    }
    if (!newFocus || newFocus.tagName == "toolbartabstop") {
      // There are no more controls or we hit a tab stop placeholder.
      return;
    }
    this._focusButton(newFocus);
  },

  _onKeyDown(aEvent) {
    let focus = document.activeElement;
    if (
      aEvent.key != " " &&
      aEvent.key.length == 1 &&
      this._isButton(focus) &&
      // Don't handle characters if the user is focused in a panel anchored
      // to the toolbar.
      !focus.closest("panel")
    ) {
      this._onSearchChar(aEvent.currentTarget, aEvent.key);
      return;
    }
    // Anything that doesn't trigger search should clear the search.
    this._clearSearch();

    if (
      aEvent.altKey ||
      aEvent.controlKey ||
      aEvent.metaKey ||
      aEvent.shiftKey ||
      !this._isButton(focus)
    ) {
      return;
    }

    switch (aEvent.key) {
      case "ArrowLeft":
        // Previous if UI is LTR, next if UI is RTL.
        this.navigateButtons(aEvent.currentTarget, !window.RTL_UI);
        break;
      case "ArrowRight":
        // Previous if UI is RTL, next if UI is LTR.
        this.navigateButtons(aEvent.currentTarget, window.RTL_UI);
        break;
      default:
        return;
    }
    aEvent.preventDefault();
  },

  _clearSearch() {
    this._searchText = "";
    if (this._clearSearchTimeout) {
      clearTimeout(this._clearSearchTimeout);
      this._clearSearchTimeout = null;
    }
  },

  _onSearchChar(aToolbar, aChar) {
    if (this._clearSearchTimeout) {
      // The user just typed a character, so reset the timer.
      clearTimeout(this._clearSearchTimeout);
    }
    // Convert to lower case so we can do case insensitive searches.
    let char = aChar.toLowerCase();
    // If the user has only typed a single character and they type the same
    // character again, they want to move to the next item starting with that
    // same character. Effectively, it's as if there was no existing search.
    // In that case, we just leave this._searchText alone.
    if (!this._searchText) {
      this._searchText = char;
    } else if (this._searchText != char) {
      this._searchText += char;
    }
    // Clear the search if the user doesn't type anything more within the timeout.
    this._clearSearchTimeout = setTimeout(
      this._clearSearch.bind(this),
      this.kSearchClearTimeout
    );

    let oldFocus = document.activeElement;
    let walker = this._getWalker(aToolbar);
    // Search forward after the current control.
    walker.currentNode = oldFocus;
    for (
      let newFocus = walker.nextNode();
      newFocus;
      newFocus = walker.nextNode()
    ) {
      if (this._doesSearchMatch(newFocus)) {
        this._focusButton(newFocus);
        return;
      }
    }
    // No match, so search from the start until the current control.
    walker.currentNode = walker.root;
    for (
      let newFocus = walker.firstChild();
      newFocus && newFocus != oldFocus;
      newFocus = walker.nextNode()
    ) {
      if (this._doesSearchMatch(newFocus)) {
        this._focusButton(newFocus);
        return;
      }
    }
  },

  _doesSearchMatch(aElem) {
    if (!this._isButton(aElem)) {
      return false;
    }
    for (let attrib of ["aria-label", "label", "tooltiptext"]) {
      let label = aElem.getAttribute(attrib);
      if (!label) {
        continue;
      }
      // Convert to lower case so we do a case insensitive comparison.
      // (this._searchText is already lower case.)
      label = label.toLowerCase();
      if (label.startsWith(this._searchText)) {
        return true;
      }
    }
    return false;
  },

  _onKeyPress(aEvent) {
    let focus = document.activeElement;
    if (
      (aEvent.key != "Enter" && aEvent.key != " ") ||
      !this._isButton(focus)
    ) {
      return;
    }

    if (focus.getAttribute("type") == "menu") {
      focus.open = true;
      return;
    }

    // moz-button uses an html button internally
    // which already receives a click event on enter.
    if (focus.localName == "moz-button") {
      return;
    }

    // Several buttons specifically don't use command events; e.g. because
    // they want to activate for middle click. Therefore, simulate a click
    // event if we know they handle click explicitly and don't handle
    // commands.
    const usesClickInsteadOfCommand = (() => {
      if (focus.tagName != "toolbarbutton") {
        return true;
      }
      return !focus.hasAttribute("oncommand") && focus.hasAttribute("onclick");
    })();

    if (!usesClickInsteadOfCommand) {
      return;
    }
    focus.dispatchEvent(
      new PointerEvent("click", {
        bubbles: true,
        ctrlKey: aEvent.ctrlKey,
        altKey: aEvent.altKey,
        shiftKey: aEvent.shiftKey,
        metaKey: aEvent.metaKey,
      })
    );
  },

  handleEvent(aEvent) {
    switch (aEvent.type) {
      case "focus":
        this._onTabStopFocus(aEvent);
        break;
      case "keydown":
        this._onKeyDown(aEvent);
        break;
      case "keypress":
        this._onKeyPress(aEvent);
        break;
      case "blur":
        this._onButtonBlur(aEvent);
        break;
    }
  },
};
