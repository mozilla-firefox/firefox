/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

"use strict";

// This is loaded into all XUL windows. Wrap in a block to prevent
// leaking to window scope.
{
  class MozArrowScrollbox extends MozElements.BaseControl {
    static #startEndVertical = ["top", "bottom"];
    static #startEndHorizontal = ["left", "right"];
    #scrollButtonUpdatePending = false;

    static get inheritedAttributes() {
      return {
        "#scrollbutton-up": "disabled=scrolledtostart",
        scrollbox: "orient,align,pack,dir,smoothscroll",
        "#scrollbutton-down": "disabled=scrolledtoend",
      };
    }

    get markup() {
      return `
      <html:link rel="stylesheet" href="chrome://global/skin/toolbarbutton.css"/>
      <html:link rel="stylesheet" href="chrome://global/skin/arrowscrollbox.css"/>
      <toolbarbutton id="scrollbutton-up" part="scrollbutton-up" keyNav="false" data-l10n-id="overflow-scroll-button-backwards"/>
      <spacer part="overflow-start-indicator"/>
      <scrollbox part="scrollbox" flex="1">
        <html:slot part="items-wrapper"/>
      </scrollbox>
      <spacer part="overflow-end-indicator"/>
      <toolbarbutton id="scrollbutton-down" part="scrollbutton-down" keyNav="false" data-l10n-id="overflow-scroll-button-forwards"/>
      `;
    }

    constructor() {
      super();
      this.attachShadow({ mode: "open" });
      this.shadowRoot.appendChild(this.fragment);

      this.scrollbox = this.shadowRoot.querySelector("scrollbox");
      this._scrollButtonUp = this.shadowRoot.getElementById("scrollbutton-up");
      this._scrollButtonDown =
        this.shadowRoot.getElementById("scrollbutton-down");

      MozXULElement.insertFTLIfNeeded("toolkit/global/arrowscrollbox.ftl");

      this._arrowScrollAnim = {
        scrollbox: this,
        requestHandle: 0,
        /* 0 indicates there is no pending request */
        start() {
          this.lastFrameTime = window.performance.now();
          if (!this.requestHandle) {
            this.requestHandle = window.requestAnimationFrame(
              this.sample.bind(this)
            );
          }
        },
        stop() {
          window.cancelAnimationFrame(this.requestHandle);
          this.requestHandle = 0;
        },
        sample(timeStamp) {
          const scrollIndex = this.scrollbox._scrollIndex;
          const timePassed = timeStamp - this.lastFrameTime;
          this.lastFrameTime = timeStamp;

          const scrollDelta = 0.5 * timePassed * scrollIndex;
          this.scrollbox.scrollByPixels(scrollDelta, true);
          this.requestHandle = window.requestAnimationFrame(
            this.sample.bind(this)
          );
        },
      };

      this._scrollIndex = 0;
      this._scrollIncrement = null;
      this._ensureElementIsVisibleAnimationFrame = 0;
      this._prevMouseScrolls = [null, null];
      this._touchStart = -1;
      this._isScrolling = false;
      this._destination = 0;
      this._direction = 0;

      this.addEventListener("wheel", this);
      this.addEventListener("touchstart", this);
      this.addEventListener("touchmove", this);
      this.addEventListener("touchend", this);
      this.shadowRoot.addEventListener("click", this);
      this.shadowRoot.addEventListener("mousedown", this);
      this.shadowRoot.addEventListener("mouseover", this);
      this.shadowRoot.addEventListener("mouseup", this);
      this.shadowRoot.addEventListener("mouseout", this);
      this.scrollbox.addEventListener("scroll", this);
      this.scrollbox.addEventListener("scrollend", this);

      let slot = this.shadowRoot.querySelector("slot");
      let overflowObserver = new ResizeObserver(_ => {
        let contentSize =
          slot.getBoundingClientRect()[this.#verticalMode ? "height" : "width"];
        // NOTE(emilio): This should be contentSize > scrollClientSize, but due
        // to how Gecko internally rounds in those cases, we allow for some
        // minor differences (the internal Gecko layout size is 1/60th of a
        // pixel, so 0.02 should cover it).
        let overflowing = contentSize - this.scrollClientSize > 0.02;
        if (overflowing == this.overflowing) {
          if (overflowing) {
            // Update scroll buttons' disabled state when the slot or scrollbox
            // size changes while we were already overflowing.
            this.#updateScrollButtonsDisabledState(/* aRafCount = */ 1);
          }
          return;
        }
        window.requestAnimationFrame(() => {
          this.toggleAttribute("overflowing", overflowing);
          this.#updateScrollButtonsDisabledState(/* aRafCount = */ 1);
          this.dispatchEvent(
            new CustomEvent(overflowing ? "overflow" : "underflow")
          );
        });
      });
      overflowObserver.observe(slot);
      overflowObserver.observe(this.scrollbox);
    }

    connectedCallback() {
      this.removeAttribute("overflowing");

      if (this.hasConnected) {
        return;
      }
      this.hasConnected = true;

      document.l10n.connectRoot(this.shadowRoot);

      if (!this.hasAttribute("smoothscroll")) {
        this.smoothScroll = Services.prefs.getBoolPref(
          "toolkit.scrollbox.smoothScroll",
          true
        );
      }

      this.initializeAttributeInheritance();
      this.#updateScrollButtonsDisabledState();
    }

    get overflowing() {
      return this.hasAttribute("overflowing");
    }

    get fragment() {
      if (!this.constructor.hasOwnProperty("_fragment")) {
        this.constructor._fragment = MozXULElement.parseXULToFragment(
          this.markup
        );
      }
      return document.importNode(this.constructor._fragment, true);
    }

    get #verticalMode() {
      return this.getAttribute("orient") == "vertical";
    }

    get _clickToScroll() {
      return this.hasAttribute("clicktoscroll");
    }

    get _scrollDelay() {
      if (this._clickToScroll) {
        return Services.prefs.getIntPref(
          "toolkit.scrollbox.clickToScroll.scrollDelay",
          150
        );
      }

      // Use the same REPEAT_DELAY as "nsRepeatService.h".
      return /Mac/.test(navigator.platform) ? 25 : 50;
    }

    get scrollIncrement() {
      if (this._scrollIncrement === null) {
        this._scrollIncrement = Services.prefs.getIntPref(
          "toolkit.scrollbox.scrollIncrement",
          20
        );
      }
      return this._scrollIncrement;
    }

    set smoothScroll(val) {
      this.setAttribute("smoothscroll", !!val);
    }

    get smoothScroll() {
      return this.getAttribute("smoothscroll") == "true";
    }

    get scrollClientRect() {
      return this.scrollbox.getBoundingClientRect();
    }

    get scrollClientSize() {
      return this.scrollbox[
        this.#verticalMode ? "clientHeightDouble" : "clientWidthDouble"
      ];
    }

    get scrollSize() {
      return this.scrollbox[
        this.#verticalMode ? "scrollHeight" : "scrollWidth"
      ];
    }

    get lineScrollAmount() {
      // line scroll amout should be the width (at horizontal scrollbox) or
      // the height (at vertical scrollbox) of the scrolled elements.
      // However, the elements may have different width or height.  So,
      // for consistent speed, let's use average width of the elements.
      var elements = this._getScrollableElements();
      return elements.length && this.scrollSize / elements.length;
    }

    get scrollPosition() {
      return this.scrollbox[this.#verticalMode ? "scrollTop" : "scrollLeft"];
    }

    get startEndProps() {
      return this.#verticalMode
        ? MozArrowScrollbox.#startEndVertical
        : MozArrowScrollbox.#startEndHorizontal;
    }

    get isRTLScrollbox() {
      if (this.#verticalMode) {
        return false;
      }
      if ("RTL_UI" in window) {
        return window.RTL_UI;
      }
      if (!("_isRTLScrollbox" in this)) {
        this._isRTLScrollbox =
          document.defaultView.getComputedStyle(this.scrollbox).direction ==
          "rtl";
      }
      return this._isRTLScrollbox;
    }

    _onButtonClick(event) {
      if (this._clickToScroll) {
        this._distanceScroll(event);
      }
    }

    _onButtonMouseDown(event, index) {
      if (this._clickToScroll && event.button == 0) {
        this._startScroll(index);
      }
    }

    _onButtonMouseUp(event) {
      if (this._clickToScroll && event.button == 0) {
        this._stopScroll();
      }
    }

    _onButtonMouseOver(index) {
      if (this._clickToScroll) {
        this._continueScroll(index);
      } else {
        this._startScroll(index);
      }
    }

    _onButtonMouseOut() {
      if (this._clickToScroll) {
        this._pauseScroll();
      } else {
        this._stopScroll();
      }
    }

    _boundsWithoutFlushing(element) {
      if (!("_DOMWindowUtils" in this)) {
        this._DOMWindowUtils = window.windowUtils;
      }

      return this._DOMWindowUtils
        ? this._DOMWindowUtils.getBoundsWithoutFlushing(element)
        : element.getBoundingClientRect();
    }

    _canScrollToElement(element) {
      if (element.hidden) {
        return false;
      }

      // See if the element is hidden via CSS without the hidden attribute.
      // If we get only zeros for the client rect, this means the element
      // is hidden. As a performance optimization, we don't flush layout
      // here which means that on the fly changes aren't fully supported.
      let rect = this._boundsWithoutFlushing(element);
      return !!(rect.top || rect.left || rect.width || rect.height);
    }

    ensureElementIsVisible(element, aInstant) {
      if (!this._canScrollToElement(element)) {
        return;
      }

      if (this._ensureElementIsVisibleAnimationFrame) {
        window.cancelAnimationFrame(this._ensureElementIsVisibleAnimationFrame);
      }
      this._ensureElementIsVisibleAnimationFrame = window.requestAnimationFrame(
        () => {
          element.scrollIntoView({
            block: "nearest",
            behavior: aInstant ? "instant" : "auto",
          });
          this._ensureElementIsVisibleAnimationFrame = 0;
        }
      );
    }

    scrollByIndex(index, aInstant) {
      if (index == 0) {
        return;
      }

      var rect = this.scrollClientRect;
      var [start, end] = this.startEndProps;
      var x = index > 0 ? rect[end] + 1 : rect[start] - 1;
      var nextElement = this._elementFromPoint(x, index);
      if (!nextElement) {
        return;
      }

      var targetElement;
      if (this.isRTLScrollbox) {
        index *= -1;
      }
      while (index < 0 && nextElement) {
        if (this._canScrollToElement(nextElement)) {
          targetElement = nextElement;
        }
        nextElement = nextElement.previousElementSibling;
        index++;
      }
      while (index > 0 && nextElement) {
        if (this._canScrollToElement(nextElement)) {
          targetElement = nextElement;
        }
        nextElement = nextElement.nextElementSibling;
        index--;
      }
      if (!targetElement) {
        return;
      }

      this.ensureElementIsVisible(targetElement, aInstant);
    }

    _getScrollableElements() {
      let nodes = this.children;
      if (nodes.length == 1) {
        let node = nodes[0];
        if (
          node.localName == "slot" &&
          node.namespaceURI == "http://www.w3.org/1999/xhtml"
        ) {
          nodes = node.getRootNode().host.children;
        }
      }
      return Array.prototype.filter.call(nodes, this._canScrollToElement, this);
    }

    _elementFromPoint(aX, aPhysicalScrollDir) {
      var elements = this._getScrollableElements();
      if (!elements.length) {
        return null;
      }

      if (this.isRTLScrollbox) {
        elements.reverse();
      }

      var [start, end] = this.startEndProps;
      var low = 0;
      var high = elements.length - 1;

      if (
        aX < elements[low].getBoundingClientRect()[start] ||
        aX > elements[high].getBoundingClientRect()[end]
      ) {
        return null;
      }

      var mid, rect;
      while (low <= high) {
        mid = Math.floor((low + high) / 2);
        rect = elements[mid].getBoundingClientRect();
        if (rect[start] > aX) {
          high = mid - 1;
        } else if (rect[end] < aX) {
          low = mid + 1;
        } else {
          return elements[mid];
        }
      }

      // There's no element at the requested coordinate, but the algorithm
      // from above yields an element next to it, in a random direction.
      // The desired scrolling direction leads to the correct element.

      if (!aPhysicalScrollDir) {
        return null;
      }

      if (aPhysicalScrollDir < 0 && rect[start] > aX) {
        mid = Math.max(mid - 1, 0);
      } else if (aPhysicalScrollDir > 0 && rect[end] < aX) {
        mid = Math.min(mid + 1, elements.length - 1);
      }

      return elements[mid];
    }

    _startScroll(index) {
      if (this.isRTLScrollbox) {
        index *= -1;
      }

      if (this._clickToScroll) {
        this._scrollIndex = index;
        this._mousedown = true;

        if (this.smoothScroll) {
          this._arrowScrollAnim.start();
          return;
        }
      }

      if (!this._scrollTimer) {
        this._scrollTimer = Cc["@mozilla.org/timer;1"].createInstance(
          Ci.nsITimer
        );
      } else {
        this._scrollTimer.cancel();
      }

      let callback;
      if (this._clickToScroll) {
        callback = () => {
          if (!document && this._scrollTimer) {
            this._scrollTimer.cancel();
          }
          this.scrollByIndex(this._scrollIndex);
        };
      } else {
        callback = () => this.scrollByPixels(this.scrollIncrement * index);
      }

      this._scrollTimer.initWithCallback(
        callback,
        this._scrollDelay,
        Ci.nsITimer.TYPE_REPEATING_SLACK
      );

      callback();
    }

    _stopScroll() {
      if (this._scrollTimer) {
        this._scrollTimer.cancel();
      }

      if (this._clickToScroll) {
        this._mousedown = false;
        if (!this._scrollIndex || !this.smoothScroll) {
          return;
        }

        this.scrollByIndex(this._scrollIndex);
        this._scrollIndex = 0;

        this._arrowScrollAnim.stop();
      }
    }

    _pauseScroll() {
      if (this._mousedown) {
        this._stopScroll();
        this._mousedown = true;

        let mouseUpOrBlur = aEvent => {
          if (
            aEvent.type == "mouseup" ||
            (aEvent.type == "blur" && aEvent.target == document)
          ) {
            this._mousedown = false;
            document.removeEventListener("mouseup", mouseUpOrBlur);
            document.removeEventListener("blur", mouseUpOrBlur, true);
          }
        };
        document.addEventListener("mouseup", mouseUpOrBlur);
        document.addEventListener("blur", mouseUpOrBlur, true);
      }
    }

    _continueScroll(index) {
      if (this._mousedown) {
        this._startScroll(index);
      }
    }

    _distanceScroll(aEvent) {
      if (aEvent.detail < 2 || aEvent.detail > 3) {
        return;
      }

      var scrollBack = aEvent.originalTarget == this._scrollButtonUp;
      var scrollLeftOrUp = this.isRTLScrollbox ? !scrollBack : scrollBack;
      var targetElement;

      if (aEvent.detail == 2) {
        // scroll by the size of the scrollbox
        let [start, end] = this.startEndProps;
        let x;
        if (scrollLeftOrUp) {
          x = this.scrollClientRect[start] - this.scrollClientSize;
        } else {
          x = this.scrollClientRect[end] + this.scrollClientSize;
        }
        targetElement = this._elementFromPoint(x, scrollLeftOrUp ? -1 : 1);

        // the next partly-hidden element will become fully visible,
        // so don't scroll too far
        if (targetElement) {
          targetElement = scrollBack
            ? targetElement.nextElementSibling
            : targetElement.previousElementSibling;
        }
      }

      if (!targetElement) {
        // scroll to the first resp. last element
        let elements = this._getScrollableElements();
        targetElement = scrollBack
          ? elements[0]
          : elements[elements.length - 1];
      }

      this.ensureElementIsVisible(targetElement);
    }

    scrollByPixels(aPixels, aInstant) {
      let scrollOptions = { behavior: aInstant ? "instant" : "auto" };
      scrollOptions[this.startEndProps[0]] = aPixels;
      this.scrollbox.scrollBy(scrollOptions);
    }

    // @param aRafCount how many animation frames we need to wait to get
    // current layout data from getBoundsWithoutFlushing.
    #updateScrollButtonsDisabledState(aRafCount = 2) {
      if (!this.overflowing) {
        this.toggleAttribute("scrolledtoend", true);
        this.toggleAttribute("scrolledtostart", true);
        this.#scrollButtonUpdatePending = false;
        return;
      }

      if (aRafCount) {
        if (this.#scrollButtonUpdatePending) {
          return;
        }

        this.#scrollButtonUpdatePending = true;
        let oneIter = () => {
          if (aRafCount--) {
            if (this.#scrollButtonUpdatePending && this.isConnected) {
              window.requestAnimationFrame(oneIter);
            }
          } else {
            this.#updateScrollButtonsDisabledState(0);
          }
        };
        oneIter();
        return;
      }

      this.#scrollButtonUpdatePending = false;

      let scrolledToStart = false;
      let scrolledToEnd = false;

      if (!this.overflowing) {
        scrolledToStart = true;
        scrolledToEnd = true;
      } else {
        let isAtEdge = (element, start) => {
          let edge = start ? this.startEndProps[0] : this.startEndProps[1];
          let scrollEdge = this._boundsWithoutFlushing(this.scrollbox)[edge];
          let elementEdge = this._boundsWithoutFlushing(element)[edge];
          // This is enough slop (>2/3) so that no subpixel value should
          // get us confused about whether we reached the end.
          const EPSILON = 0.7;
          if (start) {
            return scrollEdge <= elementEdge + EPSILON;
          }
          return elementEdge <= scrollEdge + EPSILON;
        };

        let elements = this._getScrollableElements();
        let [startElement, endElement] = [
          elements[0],
          elements[elements.length - 1],
        ];
        if (this.isRTLScrollbox) {
          [startElement, endElement] = [endElement, startElement];
        }
        scrolledToStart =
          startElement && isAtEdge(startElement, /* start = */ true);
        scrolledToEnd = endElement && isAtEdge(endElement, /* start = */ false);
        if (this.isRTLScrollbox) {
          [scrolledToStart, scrolledToEnd] = [scrolledToEnd, scrolledToStart];
        }
      }

      this.toggleAttribute("scrolledtoend", scrolledToEnd);
      this.toggleAttribute("scrolledtostart", scrolledToStart);
    }

    disconnectedCallback() {
      // Release timer to avoid reference cycles.
      if (this._scrollTimer) {
        this._scrollTimer.cancel();
        this._scrollTimer = null;
      }
      document.l10n.disconnectRoot(this.shadowRoot);
    }

    on_wheel(event) {
      // Don't consume the event if we can't scroll.
      if (!this.overflowing) {
        return;
      }

      const { deltaMode } = event;
      let doScroll = false;
      let instant;
      let scrollAmount = 0;
      if (this.#verticalMode) {
        doScroll = true;
        scrollAmount = event.deltaY;
      } else {
        // We allow vertical scrolling to scroll a horizontal scrollbox
        // because many users have a vertical scroll wheel but no
        // horizontal support.
        // Because of this, we need to avoid scrolling chaos on trackpads
        // and mouse wheels that support simultaneous scrolling in both axes.
        // We do this by scrolling only when the last two scroll events were
        // on the same axis as the current scroll event.
        // For diagonal scroll events we only respect the dominant axis.
        let isVertical = Math.abs(event.deltaY) > Math.abs(event.deltaX);
        let delta = isVertical ? event.deltaY : event.deltaX;
        let scrollByDelta = isVertical && this.isRTLScrollbox ? -delta : delta;

        if (this._prevMouseScrolls.every(prev => prev == isVertical)) {
          doScroll = true;
          scrollAmount = scrollByDelta;
          if (deltaMode == event.DOM_DELTA_PIXEL) {
            instant = true;
          }
        }

        if (this._prevMouseScrolls.length > 1) {
          this._prevMouseScrolls.shift();
        }
        this._prevMouseScrolls.push(isVertical);
      }

      if (doScroll) {
        let direction = scrollAmount < 0 ? -1 : 1;

        if (deltaMode == event.DOM_DELTA_PAGE) {
          scrollAmount *= this.scrollClientSize;
        } else if (deltaMode == event.DOM_DELTA_LINE) {
          // Try to not scroll by more than one page when using line scrolling,
          // so that all elements are scrollable.
          let lineAmount = this.lineScrollAmount;
          let clientSize = this.scrollClientSize;
          if (Math.abs(scrollAmount * lineAmount) > clientSize) {
            // NOTE: This still tries to scroll a non-fractional amount of
            // items per line scrolled.
            scrollAmount =
              Math.max(1, Math.floor(clientSize / lineAmount)) * direction;
          }
          scrollAmount *= lineAmount;
        } else {
          // DOM_DELTA_PIXEL, leave scrollAmount untouched.
        }
        let startPos = this.scrollPosition;

        if (!this._isScrolling || this._direction != direction) {
          this._destination = startPos + scrollAmount;
          this._direction = direction;
        } else {
          // We were already in the process of scrolling in this direction
          this._destination = this._destination + scrollAmount;
          scrollAmount = this._destination - startPos;
        }
        this.scrollByPixels(scrollAmount, instant);
      }

      event.stopPropagation();
      event.preventDefault();
    }

    on_touchstart(event) {
      if (event.touches.length > 1) {
        // Multiple touch points detected, abort. In particular this aborts
        // the panning gesture when the user puts a second finger down after
        // already panning with one finger. Aborting at this point prevents
        // the pan gesture from being resumed until all fingers are lifted
        // (as opposed to when the user is back down to one finger).
        this._touchStart = -1;
      } else {
        this._touchStart =
          event.touches[0][this.#verticalMode ? "screenY" : "screenX"];
      }
    }

    on_touchmove(event) {
      if (event.touches.length == 1 && this._touchStart >= 0) {
        let touchPoint =
          event.touches[0][this.#verticalMode ? "screenY" : "screenX"];
        let delta = this._touchStart - touchPoint;
        if (Math.abs(delta) > 0) {
          this.scrollByPixels(delta, true);
          this._touchStart = touchPoint;
        }
        event.preventDefault();
      }
    }

    on_touchend() {
      this._touchStart = -1;
    }

    on_scroll() {
      this._isScrolling = true;
      this.#updateScrollButtonsDisabledState();
      this.dispatchEvent(new Event("scroll"));
    }

    on_scrollend() {
      this._isScrolling = false;
      this._destination = 0;
      this._direction = 0;
      this.dispatchEvent(new Event("scrollend"));
    }

    on_click(event) {
      if (
        event.originalTarget != this._scrollButtonUp &&
        event.originalTarget != this._scrollButtonDown
      ) {
        return;
      }
      this._onButtonClick(event);
    }

    on_mousedown(event) {
      if (event.originalTarget == this._scrollButtonUp) {
        this._onButtonMouseDown(event, -1);
      }
      if (event.originalTarget == this._scrollButtonDown) {
        this._onButtonMouseDown(event, 1);
      }
    }

    on_mouseup(event) {
      if (
        event.originalTarget != this._scrollButtonUp &&
        event.originalTarget != this._scrollButtonDown
      ) {
        return;
      }
      this._onButtonMouseUp(event);
    }

    on_mouseover(event) {
      if (event.originalTarget == this._scrollButtonUp) {
        this._onButtonMouseOver(-1);
      }
      if (event.originalTarget == this._scrollButtonDown) {
        this._onButtonMouseOver(1);
      }
    }

    on_mouseout(event) {
      if (
        event.originalTarget != this._scrollButtonUp &&
        event.originalTarget != this._scrollButtonDown
      ) {
        return;
      }
      this._onButtonMouseOut();
    }
  }

  customElements.define("arrowscrollbox", MozArrowScrollbox);
}
