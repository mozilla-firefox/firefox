/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

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
      this.setAttribute("popover", autohideDisabled ? "manual" : "auto");
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
      if (offsetParentIsBody) {
        // viewport-based
        this.style.left = `${Math.round(leftOffset + winScrollX)}px`;
        this.style.top = `${Math.round(topOffset + winScrollY)}px`;
      } else {
        // container-relative
        const offsetParentRect = this.offsetParent.getBoundingClientRect();
        this.style.left = `${Math.round(leftOffset - offsetParentRect.left)}px`;
        this.style.top = `${Math.round(topOffset - offsetParentRect.top)}px`;
      }
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
    // Allows submenus to stopPropagation when focus is already in the menu
    this.addEventListener("keydown", this);
    // We need Escape/Tab/ArrowDown to work when opened with the mouse.
    document.addEventListener("keydown", this);
    // Hide when a click is initiated outside the panel.
    document.addEventListener("mousedown", this);
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
    this.removeEventListener("keydown", this);
    document.removeEventListener("keydown", this);
    document.removeEventListener("mousedown", this);
    document.removeEventListener("focusin", this);
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
      case "scroll":
        if (!inPanelList) {
          this.hide();
        }
        break;
      case "blur":
      case "popuphidden":
        this.hide();
        break;
      case "click":
        if (inPanelList) {
          this.hide(undefined, { force: true });
        } else {
          // Avoid falling through to the default click handler of the parent.
          e.stopPropagation();
        }
        break;
      case "mousedown":
        // Close if there's a click started outside the panel.
        if (!inPanelList) {
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
        } else if (!e.metaKey && !e.ctrlKey && !e.shiftKey && !e.altKey) {
          // Check if any of the children have an accesskey for this letter.
          let item = this.querySelector(
            `[accesskey="${e.key.toLowerCase()}"],
              [accesskey="${e.key.toUpperCase()}"]`
          );
          if (item) {
            item.click();
          }
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

    // Wait for a layout flush, then find the bounds.
    let {
      anchorLeft,
      anchorWidth,
      anchorTop,
      parentPanelTop,
      panelWidth,
      clientWidth,
    } = await new Promise(resolve => {
      requestAnimationFrame(() => {
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
      });
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
    requestAnimationFrame(() => {
      this.sendEvent("hidden");
      this.lastAnchorNode?.setAttribute("aria-expanded", "false");
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

  static get observedAttributes() {
    return ["accesskey", "type", "disabled", "badge-type", "aria-haspopup"];
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
      case "mouseleave":
        this.submenuPanel.toggle(e);
        break;
      case "keydown": {
        let [arrowOpenKey, arrowCloseKey] = this.setArrowKeyRTL();
        if (e.key === arrowOpenKey) {
          this.submenuPanel.show(e, e.target);
          e.stopPropagation();
        }
        if (e.key === arrowCloseKey) {
          this.submenuPanel.hide(e, { force: true }, e.target);
          e.stopPropagation();
        }
        break;
      }
      case "mouseup": {
        let event = /** @type {MouseEvent} */ (e);
        if (
          // preventClickEvent is undefined outside of chrome contexts.
          !event.preventClickEvent ||
          this.panel?.lastAnchorNode?.role != "combobox" ||
          e.button != 0
        ) {
          break;
        }

        // A click event would be fired on the nearest common ancestor of
        // the mousedown and mouseup elements. We want to retarget the
        // click to the panel-item where mouseup happened so we prevent
        // the native click and synthesize one on the panel-list.
        // This enables opening a panel-list and choosing an item with a
        // single click.

        event.preventClickEvent();
        this.button.dispatchEvent(
          new PointerEvent("click", {
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
          })
        );
        break;
      }
    }
  }
}
customElements.define("panel-item", PanelItem);
