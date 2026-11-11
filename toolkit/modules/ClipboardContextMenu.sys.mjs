/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

const lazy = {};
ChromeUtils.defineESModuleGetters(lazy, {
  PromptUtils: "resource://gre/modules/PromptUtils.sys.mjs",
});

export var ClipboardContextMenu = {
  MENU_POPUP_ID: "clipboardReadPasteMenuPopup",

  // EventListener interface.
  handleEvent(aEvent) {
    switch (aEvent.type) {
      case "command": {
        this.onCommand();
        break;
      }
      case "unload":
      case "popuphiding": {
        this.onPopupHiding();
        break;
      }
      case "keydown": {
        this.onKeyDown(aEvent);
        break;
      }
    }
  },

  _pasteMenuItemClicked: false,

  onCommand() {
    // onPopupHiding is responsible for returning result by calling onComplete
    // function.
    this._pasteMenuItemClicked = true;
  },

  onPopupHiding() {
    if (!this._pendingRequest) {
      return;
    }

    // Remove the listeners before potentially sending the async message
    // below, because that might throw.
    this._removeMenupopupEventListeners();
    this._clearDelayTimer();
    this._stopWatchingForSpammyActivation();

    this._menupopup = null;
    this._menuitem = null;

    this._pendingRequest.promptWindow.removeEventListener("unload", this);
    let propBag = lazy.PromptUtils.objectToPropBag({
      ok: this._pasteMenuItemClicked,
    });
    this._pendingRequest.resolve(propBag);

    // A result has already been responded to. Reset the state to properly
    // handle further click or dismiss events.
    this._pasteMenuItemClicked = false;
    this._pendingRequest = null;
  },

  _lastBeepTime: 0,

  onKeyDown(aEvent) {
    if (!this._menuitem.disabled) {
      return;
    }

    let accesskey = this._menuitem.getAttribute("accesskey");
    if (
      aEvent.key == accesskey.toLowerCase() ||
      aEvent.key == accesskey.toUpperCase()
    ) {
      if (Date.now() - this._lastBeepTime > 1000) {
        Cc["@mozilla.org/sound;1"].getService(Ci.nsISound).beep();
        this._lastBeepTime = Date.now();
      }
      this._refreshDelayTimer();
    }
  },

  _menupopup: null,
  _menuitem: null,
  _pendingRequest: null,

  confirmUserPaste(aWindowContext) {
    return new Promise((resolve, reject) => {
      if (!aWindowContext) {
        reject(
          Components.Exception("Null window context.", Cr.NS_ERROR_INVALID_ARG)
        );
        return;
      }

      const { topChromeWindow } = aWindowContext.browsingContext;
      const { document } = topChromeWindow;
      if (!document) {
        reject(
          Components.Exception(
            "Unable to get chrome document.",
            Cr.NS_ERROR_FAILURE
          )
        );
        return;
      }

      if (this._pendingRequest) {
        reject(
          Components.Exception(
            "There is an ongoing request.",
            Cr.NS_ERROR_FAILURE
          )
        );
        return;
      }

      this._pendingRequest = { resolve, reject, promptWindow: topChromeWindow };
      this._menupopup = this._getMenupopup(document);
      this._menuitem = this._menupopup.firstElementChild;
      this._addMenupopupEventListeners();
      // We need to handle the unload event because the popuphiding event might
      // not fire when the chrome window is closed on Linux (bug 2089701).
      // This can be removed once bug 2089701 is fixed.
      topChromeWindow.addEventListener("unload", this, { once: true });

      let mouseXInCSSPixels = {};
      let mouseYInCSSPixels = {};
      topChromeWindow.windowUtils.getLastOverWindowPointerLocationInCSSPixels(
        mouseXInCSSPixels,
        mouseYInCSSPixels
      );

      this._menuitem.disabled = true;
      this._startWatchingForSpammyActivation();
      // `openPopup` is a no-op if the popup is already opened.
      // That property is used when `navigator.clipboard.readText()` or
      // `navigator.clipboard.read()`is called from two different frames, e.g.
      // an iframe and the top level frame. In that scenario, the two frames
      // correspond to different `navigator.clipboard` instances. When
      // `readText()` or `read()` is called from both frames, an actor pair is
      // instantiated for each of them. Both actor parents will call `openPopup`
      // on the same `_menupopup` object. If the popup is already open,
      // `openPopup` is a no-op. When the popup is clicked or dismissed both
      // actor parents will receive the corresponding event.
      this._menupopup.openPopup(null, {
        isContextMenu: true,
        position: "overlap",
        x: mouseXInCSSPixels.value,
        y: mouseYInCSSPixels.value,
      });

      this._refreshDelayTimer(document);
    });
  },

  _addMenupopupEventListeners() {
    this._menupopup.addEventListener("command", this);
    this._menupopup.addEventListener("popuphiding", this);
  },

  _removeMenupopupEventListeners() {
    this._menupopup.removeEventListener("command", this);
    this._menupopup.removeEventListener("popuphiding", this);
  },

  _createMenupopup(aChromeDoc) {
    let menuitem = aChromeDoc.createXULElement("menuitem");
    menuitem.id = "clipboardReadPasteMenuItem";
    aChromeDoc.l10n.setAttributes(menuitem, "text-action-paste");

    let menupopup = aChromeDoc.createXULElement("menupopup");
    menupopup.id = this.MENU_POPUP_ID;
    menupopup.setAttribute("tabspecific", "true");
    menupopup.setAttribute("locationspecific", "true");
    menupopup.appendChild(menuitem);
    return menupopup;
  },

  _getMenupopup(aChromeDoc) {
    let menupopup = aChromeDoc.getElementById(this.MENU_POPUP_ID);
    if (menupopup == null) {
      menupopup = this._createMenupopup(aChromeDoc);
      const parent =
        aChromeDoc.querySelector("popupset") || aChromeDoc.documentElement;
      parent.appendChild(menupopup);
    }

    return menupopup;
  },

  _startWatchingForSpammyActivation() {
    let doc = this._menuitem.ownerDocument;
    doc.addEventListener("keydown", this, {
      capture: true,
      mozSystemGroup: true,
    });
  },

  _stopWatchingForSpammyActivation() {
    let doc = this._menuitem.ownerDocument;
    doc.removeEventListener("keydown", this, {
      capture: true,
      mozSystemGroup: true,
    });
  },

  _delayTimer: null,

  _clearDelayTimer() {
    if (this._delayTimer) {
      let window = this._menuitem.documentGlobal;
      window.clearTimeout(this._delayTimer);
      this._delayTimer = null;
    }
  },

  _refreshDelayTimer() {
    this._clearDelayTimer();

    let window = this._menuitem.documentGlobal;
    let delay = Services.prefs.getIntPref("security.dialog_enable_delay");
    this._delayTimer = window.setTimeout(() => {
      this._menuitem.disabled = false;
      this._stopWatchingForSpammyActivation();
      this._delayTimer = null;
    }, delay);
  },
};
