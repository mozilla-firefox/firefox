/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

import { AppConstants } from "resource://gre/modules/AppConstants.sys.mjs";

/**
 * Customization handler prepares this browser window for entering and exiting
 * customization mode by handling customizationstarting and aftercustomization
 * events.
 */
export class CustomizationHandler {
  /**
   * Set by CustomizeMode while the window enters customization mode.
   */
  isEnteringCustomizeMode = false;

  /**
   * Set by CustomizeMode while the window exits customization mode.
   */
  isExitingCustomizeMode = false;

  #win;

  /**
   * @param {Window} win
   *   The browser window this handler belongs to.
   */
  constructor(win) {
    this.#win = win;
  }

  handleEvent(aEvent) {
    switch (aEvent.type) {
      case "customizationstarting":
        this.#customizationStarting();
        break;
      case "aftercustomization":
        this.#afterCustomization();
        break;
    }
  }

  isCustomizing() {
    return this.#win.document.documentElement.hasAttribute("customizing");
  }

  #customizationStarting() {
    let { document, PlacesToolbarHelper, UpdateUrlbarSearchSplitterState } =
      this.#win;

    // Disable the toolbar context menu items
    let menubar = document.getElementById("main-menubar");
    for (let childNode of menubar.children) {
      childNode.setAttribute("disabled", true);
    }

    if (!Services.policies.isAllowed("profileImport")) {
      document.documentElement.setAttribute("disableprofileimport", "true");
    }

    UpdateUrlbarSearchSplitterState();

    PlacesToolbarHelper.customizeStart();
  }

  #afterCustomization() {
    let {
      document,
      gBrowser,
      gURLBar,
      PlacesToolbarHelper,
      updateEditUIVisibility,
      UpdateUrlbarSearchSplitterState,
      XULBrowserWindow,
    } = this.#win;

    // Update global UI elements that may have been added or removed
    if (AppConstants.platform != "macosx") {
      updateEditUIVisibility();
    }

    PlacesToolbarHelper.customizeDone();

    XULBrowserWindow.asyncUpdateUI();
    // Re-enable parts of the UI we disabled during the dialog
    let menubar = document.getElementById("main-menubar");
    for (let childNode of menubar.children) {
      childNode.removeAttribute("disabled");
    }

    gBrowser.selectedBrowser.focus();

    // Update the urlbar
    gURLBar.setURI();
    UpdateUrlbarSearchSplitterState();
  }
}

/**
 * Shows the autohiding menubar toolbar while it is in use and hides it again
 * afterwards. Each browser window has its own instance.
 */
export class AutoHideMenubar {
  /**
   * @param {Window} win
   *   The browser window whose menubar toolbar should autohide.
   */
  static init(win) {
    new AutoHideMenubar(win).#start();
  }

  #win;
  #node;
  #inactiveTimeout = null;
  #contextMenuListener;

  #events = [
    "DOMMenuBarInactive",
    "DOMMenuBarActive",
    "popupshowing",
    "mousedown",
  ];

  /**
   * @param {Window} win
   *   The browser window whose menubar toolbar should autohide.
   */
  constructor(win) {
    this.#win = win;
    this.#node = win.document.getElementById("toolbar-menubar");

    let owner = this;
    this.#contextMenuListener = {
      contextMenu: null,

      get active() {
        return !!this.contextMenu;
      },

      init(event) {
        // Ignore mousedowns in <menupopup>s.
        if (event.target.closest("menupopup")) {
          return;
        }

        let contextMenuId = owner.#node.getAttribute("context");
        this.contextMenu = owner.#win.document.getElementById(contextMenuId);
        this.contextMenu.addEventListener("popupshown", this);
        this.contextMenu.addEventListener("popuphiding", this);
        owner.#node.addEventListener("mousemove", this);
      },
      handleEvent(event) {
        switch (event.type) {
          case "popupshown":
            owner.#node.removeEventListener("mousemove", this);
            break;
          case "popuphiding":
          case "mousemove":
            owner.#setInactiveAsync();
            owner.#node.removeEventListener("mousemove", this);
            this.contextMenu.removeEventListener("popuphiding", this);
            this.contextMenu.removeEventListener("popupshown", this);
            this.contextMenu = null;
            break;
        }
      },
    };
  }

  #start() {
    this.#node.addEventListener("toolbarvisibilitychange", this);
    if (this.#node.hasAttribute("autohide")) {
      this.#enable();
    }
  }

  #updateState() {
    if (this.#node.hasAttribute("autohide")) {
      this.#enable();
    } else {
      this.#disable();
    }
  }

  #enable() {
    this.#node.setAttribute("inactive", "true");
    for (let event of this.#events) {
      this.#node.addEventListener(event, this);
    }
  }

  #disable() {
    this.#setActive();
    for (let event of this.#events) {
      this.#node.removeEventListener(event, this);
    }
  }

  handleEvent(event) {
    switch (event.type) {
      case "toolbarvisibilitychange":
        this.#updateState();
        break;
      case "popupshowing":
      // fall through
      case "DOMMenuBarActive":
        this.#setActive();
        break;
      case "mousedown":
        if (event.button == 2) {
          this.#contextMenuListener.init(event);
        }
        break;
      case "DOMMenuBarInactive":
        if (!this.#contextMenuListener.active) {
          this.#setInactiveAsync();
        }
        break;
    }
  }

  #setInactiveAsync() {
    this.#inactiveTimeout = this.#win.setTimeout(() => {
      if (this.#node.hasAttribute("autohide")) {
        this.#inactiveTimeout = null;
        this.#node.setAttribute("inactive", "true");
      }
    }, 0);
  }

  #setActive() {
    if (this.#inactiveTimeout) {
      this.#win.clearTimeout(this.#inactiveTimeout);
      this.#inactiveTimeout = null;
    }
    this.#node.removeAttribute("inactive");
  }
}
