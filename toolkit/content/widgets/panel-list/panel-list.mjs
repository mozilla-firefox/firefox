/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/// <reference path="./panel-list.tagmap.d.ts" />

import { checkAccessKeys } from "chrome://global/content/elements/accesskey-check.mjs";
import {
  assignAutoAccessKeys,
  findItemForKey,
} from "chrome://global/content/elements/auto-accesskey.mjs";

export class PanelList extends HTMLElement {
  static get observedAttributes() {
    return ["open"];
  }

  static get fragment() {
    if (!this._template) {
      let parser = new DOMParser();
      let cssPath = "chrome://global/content/elements/panel-list.css";
      let doc = parser.parseFromString(
        `
          <template>
            <link rel="stylesheet" href=${cssPath}>
            <div class="arrow top" role="presentation"></div>
            <div class="list" role="presentation">
              <slot></slot>
            </div>
            <div class="arrow bottom" role="presentation"></div>
          </template>
        `,
        "text/html"
      );
      this._template = document.importNode(doc.querySelector("template"), true);
    }
    return this._template.content.cloneNode(true);
  }

  constructor() {
    super();
    this.attachShadow({ mode: "open" });
    this.shadowRoot.appendChild(this.constructor.fragment);
  }

  connectedCallback() {
    this.setAttribute("role", "menu");
    this.initializePopover();
  }

  // Let the XUL panel handle the positioning and alignment of the
  // panel-list. Submenus also don't support popover as they need
  // to be anchored to the parent panel-list.
  supportsPopover() {
    return (
      !this.parentIsXULPanel() &&
      !this.lastAnchorNode?.hasSubmenu &&
      this.getAttribute("slot") !== "submenu"
    );
  }

  initializePopover() {
    if (this.supportsPopover() && !this.hasAttribute("popover")) {
      this.setAttribute("popover", "auto");
    } else if (!this.supportsPopover() && this.hasAttribute("popover")) {
      this.removeAttribute("popover");
    }
  }

  attributeChangedCallback(name, oldVal, newVal) {
    if (name == "open" && newVal != oldVal) {
      if (this.open) {
        this.onShow();
      } else {
        this.onHide();
      }
    }
  }

  get open() {
    return this.hasAttribute("open");
  }

  set open(val) {
    this.toggleAttribute("open", val);
  }

  get stayOpen() {
    return this.hasAttribute("stay-open");
  }

  set stayOpen(val) {
    this.toggleAttribute("stay-open", val);
  }

  /**
   * Whether an item is activated by a mouse release over it even when the press
   * happened elsewhere, so that the panel can be opened and an item chosen with
   * a single click. Suitable for panels that open on mousedown.
   *
   * @type {boolean}
   */
  get clickOnMouseup() {
    return this.hasAttribute("click-on-mouseup");
  }

  set clickOnMouseup(val) {
    this.toggleAttribute("click-on-mouseup", val);
  }

  /**
   * The panel-item this list is the submenu of, if any.
   * `setSubmenuContents()` moves a submenu into its item's shadow root, so
   * that item is the list's shadow host.
   *
   * @type {?Element}
   */
  get anchorItem() {
    let host = this.getRootNode()?.host;
    return host?.localName == "panel-item" ? host : null;
  }

  getTargetForEvent(event) {
    if (!event) {
      return null;
    }
    if (event._savedComposedTarget) {
      return event._savedComposedTarget;
    }
    if (event.composed) {
      event._savedComposedTarget =
        event.composedTarget || event.composedPath()[0];
    }
    return event._savedComposedTarget || event.target;
  }

  show(triggeringEvent, target) {
    // Re-arming the triggeringEvent guard in handleEvent() on an open list
    // would make it ignore the event currently being dispatched.
    if (this.open) {
      return;
    }

    this.triggeringEvent = triggeringEvent;
    this.lastAnchorNode =
      target || this.getTargetForEvent(this.triggeringEvent);

    this.wasOpenedByKeyboard =
      triggeringEvent &&
      (triggeringEvent.inputSource == MouseEvent.MOZ_SOURCE_KEYBOARD ||
        triggeringEvent.inputSource == MouseEvent.MOZ_SOURCE_UNKNOWN ||
        triggeringEvent.key);

    if (this.supportsPopover()) {
      const autohideDisabled = this.hasServices()
        ? Services.prefs.getBoolPref("ui.popup.disable_autohide", false)
        : false;
      // A contextmenu event is dispatched during the button press on most
      // platforms, so the release that follows would light-dismiss an auto
      // popover. Manual popovers are exempt from light dismiss; the listeners
      // from addHideListeners() dismiss them.
      const lightDismissable =
        !autohideDisabled && triggeringEvent?.type != "contextmenu";
      this.setAttribute("popover", lightDismissable ? "auto" : "manual");
    }

    // Bug 2010864 - We need to set `open` to true before calling this.onShow()
    // when the panel-list supports popover, otherwise the panel
    // height and width will be 0 and will be positioned incorrectly
    // when calling setAlign.
    this.open = true;
    if (this.parentIsXULPanel()) {
      this.toggleAttribute("inxulpanel", true);
      let panel = this.parentElement;
      panel.hidden = false;
      // Bug 1842070 - There appears to be a race here where panel-lists
      // embedded in XUL panels won't appear during the first call to show()
      // without waiting for a mix of rAF and another tick of the event
      // loop.
      requestAnimationFrame(() => {
        setTimeout(() => {
          panel.openPopup(
            this.lastAnchorNode,
            "after_start",
            0,
            0,
            false,
            false,
            this.triggeringEvent
          );
        }, 0);
      });
    } else {
      this.toggleAttribute("inxulpanel", false);
    }
  }

  hide(triggeringEvent, { force = false } = {}, eventTarget) {
    // It's possible this is being used in an unprivileged context, in which
    // case it won't have access to Services / Services will be undeclared.
    const autohideDisabled = this.hasServices()
      ? Services.prefs.getBoolPref("ui.popup.disable_autohide", false)
      : false;

    if (autohideDisabled && !force) {
      // Don't hide if this wasn't "forced" (using escape or click in menu).
      return;
    }
    let openingEvent = this.triggeringEvent;
    this.triggeringEvent = triggeringEvent;

    this.open = false;
    if (this.parentIsXULPanel()) {
      // It's possible that we're being programattically hidden, in which
      // case, we need to hide the XUL panel we're embedded in. If, however,
      // we're being hidden because the XUL panel is being hidden, calling
      // hidePopup again on it is a no-op.
      let panel = this.parentElement;
      panel.hidePopup();
    }

    let target = eventTarget || this.getTargetForEvent(openingEvent);
    // Refocus the button that opened the menu if we have one.
    if (target && this.wasOpenedByKeyboard) {
      target.focus();
    }
  }

  toggle(triggeringEvent, target = null) {
    if (this.open) {
      this.hide(triggeringEvent, { force: true }, target);
    } else {
      this.show(triggeringEvent, target);
    }
  }

  hasServices() {
    // Safely check for Services without throwing a ReferenceError.
    return typeof Services !== "undefined";
  }

  // FIXME: Use the MozLitElement isDocumentRTL getter in lit-utils.mjs once
  // panel-list is a Lit element.
  isDocumentRTL() {
    if (this.hasServices()) {
      return Services.locale.isAppLocaleRTL;
    }
    return document.dir === "rtl";
  }

  parentIsXULPanel() {
    const XUL_NS =
      "http://www.mozilla.org/keymaster/gatekeeper/there.is.only.xul";
    return (
      this.parentElement?.namespaceURI == XUL_NS &&
      this.parentElement?.localName == "panel"
    );
  }

  // FIXME: Bug 2022047 - Using anchor positioning would significantly
  // reduce the complexity of this function.
  async setAlign() {
    const hostElement = this.parentElement || this.getRootNode().host;

    if (!hostElement) {
      // This could get called before we're added to the DOM.
      // Nothing to do in that case.
      return;
    }

    // Set the showing attribute to hide the panel until its alignment is set.
    this.setAttribute("showing", "true");
    // Tell the host element to hide any overflow in case the panel extends off
    // the page before the alignment is set.
    hostElement.style.overflow = "hidden";

    // Wait for a layout flush, then find the bounds.
    let {
      anchorBottom, // distance from the bottom of the anchor el to top of viewport.
      anchorLeft,
      anchorTop,
      anchorWidth,
      panelHeight,
      panelWidth,
      winHeight,
      winScrollY,
      winScrollX,
      clientWidth,
    } = await new Promise(resolve => {
      this.style.left = 0;
      this.style.top = 0;
      this.style.minWidth = "";

      requestAnimationFrame(() =>
        setTimeout(() => {
          let target =
            this.lastAnchorNode || this.getTargetForEvent(this.triggeringEvent);
          let anchorElement = target || hostElement;
          // It's possible this is being used in a context where windowUtils is
          // not available. In that case, fallback to using the element.
          let getBounds = el =>
            window.windowUtils
              ? window.windowUtils.getBoundsWithoutFlushing(el)
              : el.getBoundingClientRect();
          // Use y since top is reserved.
          let anchorBounds = getBounds(anchorElement);
          let panelBounds = getBounds(this);
          let clientWidth = document.scrollingElement.clientWidth;
          let panelHeight =
            this.scrollHeight > panelBounds.height
              ? this.scrollHeight
              : panelBounds.height;

          resolve({
            anchorBottom: anchorBounds.bottom,
            anchorHeight: anchorBounds.height,
            anchorLeft: anchorBounds.left,
            anchorTop: anchorBounds.top,
            anchorWidth: anchorBounds.width,
            panelHeight,
            panelWidth: panelBounds.width,
            winHeight: innerHeight,
            winScrollX: scrollX,
            winScrollY: scrollY,
            clientWidth,
          });
        }, 0)
      );
    });

    // If we're embedded in a XUL panel, let it handle alignment.
    if (!this.parentIsXULPanel()) {
      // Calculate the left/right alignment.
      let align;
      let leftOffset;
      let effectivePanelWidth = this.hasAttribute("min-width-from-anchor")
        ? Math.max(panelWidth, anchorWidth)
        : panelWidth;
      let leftAlignX = anchorLeft;
      let rightAlignX = anchorLeft + anchorWidth - effectivePanelWidth;

      if (this.isDocumentRTL()) {
        // Prefer aligning on the right. Fall back to left if the right-aligned
        // panel would overflow the left viewport edge (rightAlignX < 0), or if
        // the anchor's right edge exceeds the viewport width (which would place
        // the right-aligned panel off-screen on the right).
        align =
          rightAlignX < 0 || anchorLeft + anchorWidth > clientWidth
            ? "left"
            : "right";
      } else {
        // Prefer aligning on the left.
        align =
          leftAlignX + effectivePanelWidth > clientWidth ? "right" : "left";
      }
      const alignX = align === "left" ? leftAlignX : rightAlignX;
      leftOffset = Math.max(
        0,
        Math.min(alignX, clientWidth - effectivePanelWidth)
      );

      let bottomSpaceY = winHeight - anchorBottom;

      let valign;
      let topOffset;
      const VIEWPORT_PANEL_MIN_MARGIN = 10; // 10px ensures that the panel is not flush with the viewport.
      const roundedAnchorBottom = Math.round(anchorBottom);
      const roundedBottomSpaceY = Math.round(bottomSpaceY);
      const roundedAnchorTop = Math.round(anchorTop);
      const roundedPanelHeight = Math.round(panelHeight);

      // Only want to valign top when there's more space between the bottom of the anchor element and the top of the viewport.
      // If there's more space between the bottom of the anchor element and the bottom of the viewport, we valign bottom.
      if (
        roundedAnchorBottom > roundedBottomSpaceY &&
        roundedAnchorBottom + roundedPanelHeight + VIEWPORT_PANEL_MIN_MARGIN >
          winHeight
      ) {
        // Never want to have a negative value for topOffset, so ensure it's at least 10px.
        topOffset = Math.max(
          roundedAnchorTop - roundedPanelHeight,
          VIEWPORT_PANEL_MIN_MARGIN
        );
        // Provide a max-height for larger elements which will provide scrolling as needed.
        this.style.maxHeight = `${roundedAnchorTop - VIEWPORT_PANEL_MIN_MARGIN}px`;
        valign = "top";
      } else {
        topOffset = roundedAnchorBottom;
        this.style.maxHeight = `${roundedBottomSpaceY - VIEWPORT_PANEL_MIN_MARGIN}px`;
        valign = "bottom";
      }

      // Set the alignments and show the panel.
      this.setAttribute("align", align);
      this.setAttribute("valign", valign);
      hostElement.style.overflow = "";
      // Decide positioning based on where this panel will be rendered
      const offsetParentIsBody =
        this.supportsPopover() ||
        this.offsetParent === document?.body ||
        !this.offsetParent;

      let left = leftOffset;
      let top = topOffset;

      if (this.triggeringEvent?.type === "contextmenu") {
        const { clientX, clientY } = this.triggeringEvent;
        const inlineStart = this.isDocumentRTL()
          ? clientX - effectivePanelWidth
          : clientX;
        left = Math.max(
          VIEWPORT_PANEL_MIN_MARGIN,
          Math.min(
            inlineStart,
            clientWidth - effectivePanelWidth - VIEWPORT_PANEL_MIN_MARGIN
          )
        );

        top = Math.max(
          VIEWPORT_PANEL_MIN_MARGIN,
          Math.min(clientY, winHeight - panelHeight - VIEWPORT_PANEL_MIN_MARGIN)
        );
      }

      if (offsetParentIsBody) {
        left += winScrollX;
        top += winScrollY;
      } else {
        const rect = this.offsetParent.getBoundingClientRect();
        left -= rect.left;
        top -= rect.top;
      }

      this.style.left = `${Math.round(left)}px`;
      this.style.top = `${Math.round(top)}px`;
    }

    this.style.minWidth = this.hasAttribute("min-width-from-anchor")
      ? `${Math.round(anchorWidth)}px`
      : "";

    this.removeAttribute("showing");
  }

  addHideListeners() {
    if (this.hasAttribute("stay-open") && !this.lastAnchorNode?.hasSubmenu) {
      // This is intended for inspection in Storybook.
      return;
    }
    // Hide when a panel-item is clicked in the list.
    this.addEventListener("click", this);
    // Prevent contextmenus when `suppress-contextmenu` is present.
    if (this.hasAttribute("suppress-contextmenu")) {
      this.addEventListener("contextmenu", this);
    }
    // Allows submenus to stopPropagation when focus is already in the menu
    this.addEventListener("keydown", this);
    // We need Escape/Tab/ArrowDown to work when opened with the mouse.
    document.addEventListener("keydown", this);
    // Hide when a click is initiated outside the panel.
    document.addEventListener("mousedown", this);
    // Sync our state when the UA light-dismisses the popover behind our back.
    if (this.supportsPopover()) {
      this.addEventListener("toggle", this);
    }
    // Hide if focus changes and the panel isn't in focus.
    document.addEventListener("focusin", this);
    // Reset for focus tracking, we treat the first focusin differently.
    this.focusHasChanged = false;
    // Hide on resize, scroll or losing window focus
    window.addEventListener("scroll", this, { capture: true });
    window.addEventListener("resize", this);
    window.addEventListener("blur", this);
    if (this.parentIsXULPanel()) {
      this.parentElement.addEventListener("popuphidden", this);
    }
  }

  removeHideListeners() {
    this.removeEventListener("click", this);
    this.removeEventListener("contextmenu", this);
    this.removeEventListener("keydown", this);
    document.removeEventListener("keydown", this);
    document.removeEventListener("mousedown", this);
    document.removeEventListener("focusin", this);
    this.removeEventListener("toggle", this);
    window.removeEventListener("resize", this);
    window.removeEventListener("scroll", this, { capture: true });
    window.removeEventListener("blur", this);
    if (this.parentIsXULPanel()) {
      this.parentElement.removeEventListener("popuphidden", this);
    }
  }

  handleEvent(e) {
    // Ignore the event if it caused the panel to open.
    if (e == this.triggeringEvent) {
      return;
    }

    let target = this.getTargetForEvent(e);
    let inPanelList = e.composed
      ? e.composedPath().some(el => el == this)
      : e.target.closest && e.target.closest("panel-list") == this;

    switch (e.type) {
      case "resize":
        // Resize events of in-process subframes bubble up to chrome windows.
        if (e.target == window) {
          this.hide();
        }
        break;
      case "scroll":
        if (!inPanelList) {
          this.hide();
        }
        break;
      case "blur":
      case "popuphidden":
        this.hide();
        break;
      case "toggle":
        // A light dismiss closes the popover without hide() ever running,
        // ensure state stays in sync.
        if (e.newState === "closed" && this.open) {
          this.open = false;
        }
        break;
      case "click": {
        if (!inPanelList) {
          // Avoid falling through to the default click handler of the parent.
          e.stopPropagation();
          break;
        }
        // Open the submenu if user selects submenu parent.
        const item = e.composedPath().find(el => el.localName == "panel-item");
        if (item?.hasSubmenu) {
          if (item.submenuPanel && !item.submenuPanel.open) {
            item.submenuPanel.show(e, item);
          }
          break;
        }
        this.hide(undefined, { force: true });
        break;
      }
      case "contextmenu":
        e.preventDefault();
        break;
      case "mousedown":
        // Close if there's a click started outside the panel or its parent.
        if (!inPanelList && !e.composedPath().includes(this.anchorItem)) {
          this.hide();
        }
        break;
      case "keydown":
        if (e.key === "ArrowDown" || e.key === "ArrowUp" || e.key === "Tab") {
          // Ignore tabbing with a modifer other than shift.
          if (e.key === "Tab" && (e.altKey || e.ctrlKey || e.metaKey)) {
            return;
          }

          // Don't scroll the page or let the regular tab order take effect.
          e.preventDefault();

          // Prevents the host panel list from responding to these events while
          // the submenu is active.
          e.stopPropagation();

          // Keep moving to the next/previous element sibling until we find a
          // panel-item that isn't hidden.
          let moveForward =
            e.key === "ArrowDown" || (e.key === "Tab" && !e.shiftKey);

          let nextItem = moveForward
            ? this.focusWalker.nextNode()
            : this.focusWalker.previousNode();

          // If the next item wasn't found, try looping to the top/bottom.
          if (!nextItem) {
            this.focusWalker.currentNode = this;
            if (moveForward) {
              nextItem = this.focusWalker.firstChild();
            } else {
              nextItem = this.focusWalker.lastChild();
            }
          }
          break;
        } else if (e.key === "Escape") {
          this.hide(undefined, { force: true });
        } else if (
          !e.metaKey &&
          !e.ctrlKey &&
          !e.altKey &&
          this.#selectByKey(e.key)
        ) {
          // Prevent the host from receiving input events for this keypress.
          e.preventDefault();
          // The listener is registered on both this element and the
          // document, and an outer list holding a submenu gets the event
          // too, so a selection that leaves the panel open would otherwise
          // advance once per listener.
          e.stopPropagation();
        }
        break;
      case "focusin":
        if (
          this.triggeringEvent &&
          target == this.getTargetForEvent(this.triggeringEvent) &&
          !this.focusHasChanged
        ) {
          // There will be a focusin after the mousedown that opens the panel
          // using the mouse. Ignore the first focusin event if it's on the
          // triggering target.
          this.focusHasChanged = true;
        } else {
          // Just record that there was a focusin event.
          this.focusHasChanged = true;
        }
        break;
    }
  }

  /**
   * Selects the visible, enabled item that findItemForKey picks for the
   * pressed key, by accesskey or else by label. When one item matches, it is
   * activated; when several do, each press moves to the next one without
   * activating it, as in a `menupopup`. A menu listing names a user chose - a
   * container, a bookmark - can only be reached by keyboard through label
   * matches, having no message to take an accesskey from.
   *
   * Label matches stand aside while an editable field outside the panel has
   * focus. A panel-list that stays open over a focused field, as the
   * Smartbar's mention panel does, filters itself from what the field
   * receives, so the keystroke belongs to the field. Any other focus outside
   * the panel, such as the anchor button or the document body after a XUL
   * panel took focus on a mouse open, does not claim the letter. With focus
   * outside every list, an open submenu takes the key and its outer list
   * stands aside.
   *
   * @param {string} key
   *   The pressed key.
   * @returns {boolean}
   *   Whether an item was activated or selected.
   */
  #selectByKey(key) {
    if (key.length != 1) {
      return false;
    }
    // The focus chain from the document down through shadow roots, since the
    // panel may sit in a shadow tree other than the focused element's.
    let chain = [];
    for (
      let el = this.ownerDocument.activeElement;
      el;
      el = el.shadowRoot?.activeElement
    ) {
      chain.push(el);
    }
    let focused = chain.find(el => this.contains(el));
    // Both this list and its open submenu hear the keystroke through their
    // document listeners.
    if (
      !focused &&
      [...this.querySelectorAll("panel-item[submenu]")].some(
        item => item.submenuPanel?.open
      )
    ) {
      return false;
    }
    let deepest = chain.at(-1);
    let result = findItemForKey(this.getMenuItems(), key, {
      getLabel: item => item.label?.textContent ?? item.textContent,
      current: focused,
      matchLabels:
        !!focused ||
        !(
          deepest?.isContentEditable ||
          ["input", "textarea", "select"].includes(deepest?.localName)
        ),
    });
    if (!result) {
      return false;
    }
    if (result.activate) {
      result.item.click();
      return true;
    }
    result.item.focus();
    // Arrow navigation resumes from wherever the walker last stopped.
    this.focusWalker.currentNode = result.item;
    return true;
  }

  /**
   * Returns the items that keyboard navigation in this list reaches, in order,
   * like XULPopupElement.getMenuItems.
   *
   * @param {object} [options]
   * @param {boolean} [options.includeDisabled]
   *   Whether to include disabled items, which keyboard navigation skips.
   * @returns {Element[]}
   */
  getMenuItems({ includeDisabled = false } = {}) {
    return [
      ...this.querySelectorAll(
        includeDisabled
          ? "panel-item:not([hidden])"
          : "panel-item:not([hidden]):not([disabled])"
      ),
    ];
  }

  /**
   * A TreeWalker that can be used to focus elements. The returned element will
   * be the element that has gained focus based on the requested movement
   * through the tree.
   *
   * Example:
   *
   *   this.focusWalker.currentNode = this;
   *   // Focus and get the first focusable child.
   *   let focused = this.focusWalker.nextNode();
   *   // Focus the second focusable child.
   *   this.focusWalker.nextNode();
   */
  get focusWalker() {
    if (!this._focusWalker) {
      this._focusWalker = document.createTreeWalker(
        this,
        NodeFilter.SHOW_ELEMENT,
        {
          acceptNode: node => {
            // No need to look at hidden nodes.
            if (node.hidden) {
              return NodeFilter.FILTER_REJECT;
            }

            // Focus the node, if it worked then this is the node we want.
            node.focus();
            if (node === node.getRootNode().activeElement) {
              return NodeFilter.FILTER_ACCEPT;
            }

            // Continue into child nodes if the parent couldn't be focused.
            return NodeFilter.FILTER_SKIP;
          },
        }
      );
    }
    return this._focusWalker;
  }
  async setSubmenuAlign() {
    const hostElement =
      this.lastAnchorNode.parentElement || this.getRootNode().host;
    // The showing attribute allows layout of the panel while remaining hidden
    // from the user until alignment is set.
    this.setAttribute("showing", "true");

    // The rAF + setTimeout matches setAlign() so the submenu is laid out before
    // its width is measured. Without the setTimeout the width can read as 0 and
    // the overflow check picks the wrong side.
    let {
      anchorLeft,
      anchorWidth,
      anchorTop,
      parentPanelTop,
      panelWidth,
      clientWidth,
    } = await new Promise(resolve => {
      requestAnimationFrame(() =>
        setTimeout(() => {
          // It's possible this is being used in a context where windowUtils is
          // not available. In that case, fallback to using the element.
          let getBounds = el =>
            window.windowUtils
              ? window.windowUtils.getBoundsWithoutFlushing(el)
              : el.getBoundingClientRect();
          // submenu item in the parent panel list
          let anchorBounds = getBounds(this.lastAnchorNode);
          let parentPanelBounds = getBounds(hostElement);
          let panelBounds = getBounds(this);
          let clientWidth = document.scrollingElement.clientWidth;

          resolve({
            anchorLeft: anchorBounds.left,
            anchorWidth: anchorBounds.width,
            anchorTop: anchorBounds.top,
            parentPanelTop: parentPanelBounds.top,
            panelWidth: panelBounds.width,
            clientWidth,
          });
        }, 0)
      );
    });

    let align = hostElement.getAttribute("align");

    // we use document.scrollingElement.clientWidth to exclude the width
    // of vertical scrollbars, because its inclusion can cause the submenu
    // to open to the wrong side and be overlapped by the scrollbar.
    if (
      align == "left" &&
      anchorLeft + anchorWidth + panelWidth < clientWidth
    ) {
      this.style.left = `${anchorWidth}px`;
      this.style.right = "";
    } else {
      this.style.right = `${anchorWidth}px`;
      this.style.left = "";
    }

    let topOffset =
      anchorTop -
      parentPanelTop -
      (parseFloat(window.getComputedStyle(this)?.paddingTop) || 0);
    this.style.top = `${topOffset}px`;

    this.removeAttribute("showing");
  }

  async onShow() {
    this.sendEvent("showing");
    assignAutoAccessKeys(this);

    if (this.lastAnchorNode?.hasSubmenu) {
      await this.setSubmenuAlign();
    } else {
      await this.setAlign();
    }

    // If the panel was hidden during async alignment, bail out.
    if (!this.open) {
      return;
    }

    // Call showPopover() after positioning is set up
    if (this.supportsPopover()) {
      try {
        this.showPopover();
      } catch (ex) {
        console.error("Failed to show popover:", ex);
      }
    }

    // Register hide listeners after the popover is shown, so that a second
    // panel-list opening doesn't have conflicting document-level event
    // handlers with a first panel-list that hasn't been auto-dismissed yet.
    this.addHideListeners();

    // Always reset this regardless of how the panel list is opened
    // so the first child will be focusable.
    this.focusWalker.currentNode = this;

    // Wait until the next paint for the alignment to be set and panel to be
    // visible.
    requestAnimationFrame(() => {
      if (this.wasOpenedByKeyboard) {
        // Focus the first focusable panel-item if opened by keyboard.
        this.focusWalker.currentNode = this;
        this.focusWalker.nextNode();
      }

      this.lastAnchorNode?.setAttribute("aria-expanded", "true");

      checkAccessKeys(this);
      this.sendEvent("shown");
    });
  }

  onHide() {
    if (this.supportsPopover()) {
      try {
        this.hidePopover();
      } catch (ex) {
        // hidePopover may throw if the popover was already hidden or was never shown
      }
    }

    // Hold lastAnchorNode, since show() can point it at another anchor before the next
    // animation frame.
    let lastAnchorNode = this.lastAnchorNode;
    requestAnimationFrame(() => {
      this.sendEvent("hidden", { anchorNode: lastAnchorNode });
      lastAnchorNode?.setAttribute("aria-expanded", "false");
    });
    this.removeHideListeners();
  }

  sendEvent(name, detail) {
    this.dispatchEvent(
      new CustomEvent(name, { detail, bubbles: true, composed: true })
    );
  }
}
customElements.define("panel-list", PanelList);

export class PanelItem extends HTMLElement {
  #initialized = false;
  #defaultSlot;
  #badge;
  #shortcut;

  static get observedAttributes() {
    return [
      "accesskey",
      "type",
      "disabled",
      "badge-type",
      "shortcut",
      "aria-haspopup",
    ];
  }

  constructor() {
    super();
    this.attachShadow({ mode: "open" });

    let style = document.createElement("link");
    style.rel = "stylesheet";
    style.href = "chrome://global/content/elements/panel-item.css";

    this.button = document.createElement("button");
    this.#setButtonAttributes();

    this.button.setAttribute("part", "button");
    // Use a XUL label element if possible to show the accesskey.
    this.label = document.createXULElement
      ? document.createXULElement("label")
      : document.createElement("span");
    this.label.setAttribute("part", "label");

    this.button.appendChild(this.label);
    this.#updateBadge();
    this.#updateShortcut();

    let supportLinkSlot = document.createElement("slot");
    supportLinkSlot.name = "support-link";

    this.#defaultSlot = document.createElement("slot");
    this.#defaultSlot.style.display = "none";

    this.shadowRoot.append(
      style,
      this.button,
      supportLinkSlot,
      this.#defaultSlot
    );
  }

  connectedCallback() {
    if (!this._l10nRootConnected && document.l10n) {
      document.l10n.connectRoot(this.shadowRoot);
      this._l10nRootConnected = true;
    }

    this.panel =
      this.getRootNode()?.host?.closest("panel-list") ||
      this.closest("panel-list");

    if (!this.#initialized) {
      this.#initialized = true;
      // When click listeners are added to the panel-item it creates a node in
      // the a11y tree for this element. This breaks the association between the
      // menu and the button[role="menuitem"] in this shadow DOM and causes
      // announcement issues with screen readers. (bug 995064)
      this.setAttribute("role", "presentation");

      this.#setLabelContents();

      // When our content changes, move the text into the label. It doesn't work
      // with a <slot>, unfortunately.
      new MutationObserver(() => this.#setLabelContents()).observe(this, {
        characterData: true,
        childList: true,
        subtree: true,
      });

      if (this.hasSubmenu) {
        this.panel.setAttribute("has-submenu", "");
        this.icon = document.createElement("div");
        this.icon.setAttribute("class", "submenu-icon");

        this.button.appendChild(this.icon);

        this.submenuSlot = document.createElement("slot");
        this.submenuSlot.name = "submenu";

        this.shadowRoot.append(this.submenuSlot);

        this.setSubmenuContents();
      }
    }

    this.button.addEventListener("mouseup", this);

    if (this.panel) {
      this.panel.addEventListener("hidden", this);
      this.panel.addEventListener("shown", this);
    }

    if (this.hasSubmenu) {
      this.addEventListener("mouseenter", this);
      this.addEventListener("mouseleave", this);
      this.addEventListener("keydown", this);
    }
  }

  disconnectedCallback() {
    if (this._l10nRootConnected) {
      document.l10n.disconnectRoot(this.shadowRoot);
      this._l10nRootConnected = false;
    }

    this.button.removeEventListener("mouseup", this);

    if (this.panel) {
      this.panel.removeEventListener("hidden", this);
      this.panel.removeEventListener("shown", this);
      this.panel = null;
    }

    if (this.hasSubmenu) {
      this.removeEventListener("mouseenter", this);
      this.removeEventListener("mouseleave", this);
      this.removeEventListener("keydown", this);
    }
  }

  get hasSubmenu() {
    return this.hasAttribute("submenu");
  }

  attributeChangedCallback(name, oldVal, newVal) {
    if (name === "accesskey") {
      // Bug 1037709 - Accesskey doesn't work in shadow DOM.
      // Ideally we'd have the accesskey set in shadow DOM, and on
      // attributeChangedCallback we'd just update the shadow DOM accesskey.

      // Skip this change event if we caused it.
      if (this._modifyingAccessKey) {
        this._modifyingAccessKey = false;
        return;
      }

      this.label.accessKey = newVal || "";

      // Bug 1588156 - Accesskey is not ignored for hidden non-input elements.
      // Since the accesskey won't be ignored, we need to remove it ourselves
      // when the panel is closed, and move it back when it opens.
      if (!this.panel || !this.panel.open) {
        // When the panel isn't open, just store the key for later.
        this._accessKey = newVal || null;
        this._modifyingAccessKey = true;
        this.accessKey = "";
      } else {
        this._accessKey = null;
      }
    } else if (
      name === "type" ||
      name === "disabled" ||
      name === "aria-haspopup"
    ) {
      this.#setButtonAttributes();
    } else if (name === "badge-type") {
      this.#updateBadge();
    } else if (name === "shortcut") {
      this.#updateShortcut();
    }
  }

  #setButtonAttributes() {
    if (this.type == "checkbox") {
      this.button.setAttribute("role", "menuitemcheckbox");
      this.button.setAttribute("aria-checked", this.checked);
    } else {
      this.button.setAttribute("role", "menuitem");
      this.button.removeAttribute("aria-checked");
    }
    this.button.toggleAttribute("disabled", this.disabled);
    if (this.hasAttribute("aria-haspopup")) {
      this.button.setAttribute(
        "aria-haspopup",
        this.getAttribute("aria-haspopup")
      );
    } else {
      this.button.removeAttribute("aria-haspopup");
    }
  }

  #updateBadge() {
    if (this.hasAttribute("badge-type")) {
      if (!this.#badge) {
        this.#badge = document.createElement("moz-badge");
        this.label.after(this.#badge);
      }
      this.#badge.setAttribute("type", this.getAttribute("badge-type"));
    } else if (this.#badge) {
      this.#badge.remove();
      this.#badge = null;
    }
  }

  #updateShortcut() {
    if (this.hasAttribute("shortcut")) {
      if (!this.#shortcut) {
        this.#shortcut = document.createElement("span");
        this.#shortcut.className = "shortcut";
        this.#shortcut.setAttribute("part", "shortcut");
        (this.#badge ?? this.label).after(this.#shortcut);
      }
      this.#shortcut.textContent = this.getAttribute("shortcut");
    } else if (this.#shortcut) {
      this.#shortcut.remove();
      this.#shortcut = null;
    }
  }

  #setLabelContents() {
    this.label.textContent = this.#defaultSlot
      .assignedNodes()
      .map(node => node.textContent)
      .join("");
  }

  setSubmenuContents() {
    this.submenuPanel = this.submenuSlot.assignedNodes()[0];
    if (this.submenuPanel) {
      this.shadowRoot.append(this.submenuPanel);
    }
  }

  get disabled() {
    return this.hasAttribute("disabled");
  }

  set disabled(val) {
    this.toggleAttribute("disabled", val);
  }

  get checked() {
    if (this.type !== "checkbox") {
      return false;
    }
    return this.hasAttribute("checked");
  }

  set checked(val) {
    if (this.type == "checkbox") {
      this.toggleAttribute("checked", val);
      this.button.setAttribute("aria-checked", !!val);
    }
  }

  get type() {
    return this.getAttribute("type") || "button";
  }

  set type(val) {
    this.setAttribute("type", val);
  }

  click() {
    this.button.click();
  }

  focus() {
    this.button.focus();
  }

  setArrowKeyRTL() {
    let arrowOpenKey = "ArrowRight";
    let arrowCloseKey = "ArrowLeft";

    if (this.submenuPanel.isDocumentRTL()) {
      arrowOpenKey = "ArrowLeft";
      arrowCloseKey = "ArrowRight";
    }
    return [arrowOpenKey, arrowCloseKey];
  }

  handleEvent(e) {
    // Bug 1588156 - Accesskey is not ignored for hidden non-input elements.
    // Since the accesskey won't be ignored, we need to remove it ourselves
    // when the panel is closed, and move it back when it opens.
    switch (e.type) {
      case "shown":
        if (this._accessKey) {
          this.accessKey = this._accessKey;
          this._accessKey = null;
        }
        break;
      case "hidden":
        if (this.accessKey) {
          this._accessKey = this.accessKey;
          this._modifyingAccessKey = true;
          this.accessKey = "";
        }
        break;
      case "mouseenter":
        this.submenuPanel.show(e);
        break;
      case "mouseleave":
        this.submenuPanel.hide(e, { force: true });
        break;
      case "keydown": {
        let [arrowOpenKey, arrowCloseKey] = this.setArrowKeyRTL();
        if (e.key === arrowOpenKey) {
          this.submenuPanel.show(e, e.target);
          // Don't let the arrow key scroll the page, which would trigger the
          // panel's scroll-hide listener and immediately close the submenu.
          e.preventDefault();
          e.stopPropagation();
        }
        if (e.key === arrowCloseKey) {
          this.submenuPanel.hide(e, { force: true }, e.target);
          e.preventDefault();
          e.stopPropagation();
        }
        break;
      }
      case "mouseup": {
        let event = /** @type {MouseEvent} */ (e);
        if (!this.panel?.clickOnMouseup || e.button != 0) {
          break;
        }

        // A click event would be fired on the nearest common ancestor of
        // the mousedown and mouseup elements. We want to retarget the
        // click to the panel-item where mouseup happened, so we swallow the
        // one the release generates and synthesize our own on the item.
        // This enables opening a panel-list and choosing an item with a
        // single click.
        if (event.preventClickEvent) {
          event.preventClickEvent();
        } else {
          // The retargeted click follows this event synchronously.
          let swallowClick = retargeted => {
            if (retargeted.isTrusted) {
              retargeted.stopPropagation();
              retargeted.preventDefault();
              removeSwallowClick();
            }
          };
          let removeSwallowClick = () =>
            window.removeEventListener("click", swallowClick, {
              capture: true,
            });
          window.addEventListener("click", swallowClick, { capture: true });
          setTimeout(removeSwallowClick);
        }

        let click = new PointerEvent("click", {
          bubbles: true,
          composed: true,
          view: event.view,
          shiftKey: event.shiftKey,
          ctrlKey: event.ctrlKey,
          altKey: event.altKey,
          metaKey: event.metaKey,
          screenX: event.screenX,
          screenY: event.screenY,
          clientX: event.clientX,
          clientY: event.clientY,
          button: event.button,
          // The inputSource of the click event will always be MOZ_SOURCE_UNKNOWN.
        });
        this.button.dispatchEvent(click);
        break;
      }
    }
  }
}
customElements.define("panel-item", PanelItem);
