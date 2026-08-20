/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/**
 * Checks that a browser-chrome test only clicks inside popups that are open.
 *
 * Anything inside a popup that is not open is invisible to the accessibility
 * APIs and not focusable, so clicking it does not do what the test intends.
 * This needs no accessibility service, so unlike the accessibility checks it
 * runs on every configuration, not only those that enable a11y_checks.
 */

"use strict";

this.ClickChecks = (function () {
  let SimpleTest = null;
  let handler = null;
  let shouldHandleClicks = true;

  // The popup the last mousedown happened in and the state it was in then, or
  // null if that mousedown was not inside a popup.
  let mouseDownState = null;

  /**
   * Find the closest popup ancestor of a node, crossing shadow boundaries.
   *
   * @param {Node} node
   * @returns {?Element} the popup, or null if the node is not inside one.
   */
  function popupAncestor(node) {
    // A click can target something that is not an element, and sometimes not
    // even a node: a document, a text node, or a window.
    for (let current = node; current; current = current.getRootNode?.().host) {
      const popup = current.closest?.("panel, menupopup");
      if (popup) {
        return popup;
      }
    }

    return null;
  }

  /**
   * @param {Element} popup
   * @param {string} [state] the state to report, defaulting to the current one.
   * @returns {string} eg. `panel#notification-popup (state="showing")`
   */
  function describeElement(element) {
    return `${element.localName}${element.id ? `#${element.id}` : ""}`;
  }

  function describePopup(popup, state = popup.state) {
    return `${describeElement(popup)} (state="${state}")`;
  }

  /**
   * Whether a node has no box, ie. it is not rendered. That means the popup was
   * never opened at all, rather than clicked before it finished opening.
   *
   * Copied from AccessibilityUtils, which needs the same thing, rather than
   * reaching into it: this check has to work when the accessibility checks are
   * disabled.
   *
   * @param {Node} node
   * @returns {boolean}
   */
  function hasEmptyBounds(node) {
    const bounds =
      node.documentGlobal?.windowUtils?.getBoundsWithoutFlushing(node);
    return !!bounds && (bounds.width == 0 || bounds.height == 0);
  }

  /**
   * Report a click that happened inside a popup that was not open.
   *
   * @param {Node} node the click target.
   * @param {?object} recorded the popup state recorded at mousedown.
   */
  function assertClickedPopupWasOpen(node, recorded) {
    const popup = popupAncestor(node);
    if (!popup) {
      return;
    }

    // Activating a menuitem hides the menu on mouseup, so a legitimately
    // clicked popup is already hiding by the time the click is dispatched.
    const state = recorded?.popup == popup ? recorded.state : popup.state;
    if (state == "open") {
      return;
    }

    const advice =
      popup.localName == "menupopup"
        ? "activate it with BrowserTestUtils.activateMenuItem(item)"
        : `wait for it with BrowserTestUtils.waitForPopupEvent(popup, "shown")`;
    const message =
      `Clicked ${describeElement(node)} inside ${describePopup(popup, state)}, ` +
      `which was not open. Its contents are only interactive while it is ` +
      `open; ${advice}.`;
    if (hasEmptyBounds(node)) {
      // The popup was never opened, which is the same mistake but too
      // widespread to fail on yet: bug 2063575.
      SimpleTest.todo(false, message);
    } else {
      SimpleTest.ok(false, message);
    }
  }

  return {
    popupAncestor,
    describePopup,

    init(simpleTest) {
      SimpleTest = simpleTest;
      shouldHandleClicks = true;
      mouseDownState = null;
      // A top level xul window's DocShell doesn't have a chromeEventHandler
      // attribute. In that case, the chrome event handler is just the global
      // window object.
      handler = window.docShell.chromeEventHandler ?? window.docShell.domWindow;
      handler.addEventListener("mousedown", this, true, true);
      handler.addEventListener("click", this, true, true);
    },

    uninit() {
      handler.removeEventListener("mousedown", this, true);
      handler.removeEventListener("click", this, true);
      handler = null;
      SimpleTest = null;
      mouseDownState = null;
    },

    /**
     * Suppress (or disable suppression of) handling of captured click events.
     * This should only be called by EventUtils, etc. when a click event will
     * be generated but we know it is not actually a click intended to activate
     * a control; e.g. drag/drop.
     */
    suppressClickHandling(shouldSuppress) {
      shouldHandleClicks = !shouldSuppress;
    },

    reset() {
      shouldHandleClicks = true;
      mouseDownState = null;
    },

    /**
     * Forget the popup recorded at the last mousedown. Called when a test ends,
     * as the recorded popup is only useful for the click that follows the
     * mousedown: keeping it until the next test starts would make the test that
     * ended on a mousedown look like it leaked the popup's window.
     */
    forgetMouseDownState() {
      mouseDownState = null;
    },

    handleEvent(event) {
      if (event.type == "mousedown") {
        const popup = popupAncestor(event.composedTarget);
        mouseDownState = popup ? { popup, state: popup.state } : null;
        return;
      }

      // Consume the recorded state even when suppressed, so that the next
      // click is not matched against the state recorded for this one.
      const recorded = mouseDownState;
      mouseDownState = null;

      if (!shouldHandleClicks) {
        return;
      }

      let node = event.composedTarget;
      if (node.localName == "slot") {
        // Events don't target text nodes, so a click on the text inside a slot
        // is retargeted to the slot, which isn't itself rendered.
        node = node.flattenedTreeParentNode;
      }

      assertClickedPopupWasOpen(node, recorded);
    },
  };
})();
