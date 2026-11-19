/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

"use strict";

// This is loaded into all XUL windows. Wrap in a block to prevent
// leaking to window scope.
{
  const { AppConstants } = ChromeUtils.importESModule(
    "resource://gre/modules/AppConstants.sys.mjs"
  );

  const DEFAULT_BUTTON_L10N_IDS = {
    accept: "dialog-button-accept",
    cancel: "dialog-button-cancel",
    disclosure: "dialog-button-disclosure",
  };

  class MozDialog extends MozXULElement {
    #subDialogObserver = null;

    constructor() {
      super();
    }

    static get observedAttributes() {
      return super.observedAttributes.concat(
        "subdialog",
        "extra1-is-secondary"
      );
    }

    #onSubDialogEnabled() {
      console.assert(
        document.documentElement.hasAttribute("subdialog"),
        `Turning off subdialog style is not supported`
      );
      this.shadowRoot.appendChild(
        MozXULElement.parseXULToFragment(this.inContentStyle)
      );
    }

    attributeChangedCallback(name, oldValue, newValue) {
      // Only move the button on UNIX, since it's already in the correct spot on Windows
      if (name === "extra1-is-secondary" && AppConstants.XP_UNIX) {
        this.getButton("cancel").after(this.getButton("extra1"));
      }
      super.attributeChangedCallback(name, oldValue, newValue);
    }

    static get inheritedAttributes() {
      return {
        ".dialog-button-box":
          "pack=buttonpack,align=buttonalign,dir=buttondir,orient=buttonorient",
        "[dlgtype='accept']": "disabled=buttondisabledaccept",
      };
    }

    get inContentStyle() {
      return `
      <html:link rel="stylesheet" href="chrome://global/skin/in-content/common.css" />
    `;
    }

    get _markup() {
      let buttons = AppConstants.XP_UNIX
        ? `
      <hbox class="dialog-button-box">
        <button part="dialog-button" dlgtype="disclosure" hidden="true"/>
        <button part="dialog-button" dlgtype="extra2" hidden="true"/>
        <button part="dialog-button" dlgtype="extra1" hidden="true"/>
        <spacer part="button-spacer" class="button-spacer" flex="1"/>
        <button part="dialog-button" dlgtype="cancel"/>
        <button part="dialog-button" dlgtype="accept"/>
      </hbox>`
        : `
      <hbox class="dialog-button-box" pack="end">
        <button part="dialog-button" dlgtype="extra2" hidden="true"/>
        <spacer part="button-spacer" class="button-spacer" flex="1" hidden="true"/>
        <button part="dialog-button" dlgtype="accept"/>
        <button part="dialog-button" dlgtype="extra1" hidden="true"/>
        <button part="dialog-button" dlgtype="cancel"/>
        <button part="dialog-button" dlgtype="disclosure" hidden="true"/>
      </hbox>`;

      return `
      <html:link rel="stylesheet" href="chrome://global/skin/button.css"/>
      <html:link rel="stylesheet" href="chrome://global/skin/dialog.css"/>
      ${
        document.documentElement.hasAttribute("subdialog")
          ? this.inContentStyle
          : ""
      }
      <vbox class="box-inherit" part="content-box">
        <html:slot></html:slot>
      </vbox>
      ${buttons}`;
    }

    connectedCallback() {
      if (this.delayConnectedCallback()) {
        return;
      }
      if (this.hasConnected) {
        return;
      }
      this.hasConnected = true;
      this.attachShadow({ mode: "open" });

      let docRoot = document.documentElement;
      docRoot.setAttribute("role", "dialog");
      if (!docRoot.hasAttribute("subdialog")) {
        this.#subDialogObserver = new MutationObserver(() => {
          this.#onSubDialogEnabled();
          this.#subDialogObserver.disconnect();
          this.#subDialogObserver = null;
        });
        this.#subDialogObserver.observe(docRoot, {
          attributes: true,
          attributeFilter: ["subdialog"],
        });
      }
      MozXULElement.insertFTLIfNeeded("toolkit/global/dialog.ftl");
      document.l10n.connectRoot(this.shadowRoot);

      this.shadowRoot.textContent = "";
      this.shadowRoot.appendChild(
        MozXULElement.parseXULToFragment(this._markup)
      );
      this.initializeAttributeInheritance();

      this._configureButtons(this.buttons);

      window.moveToAlertPosition = this.moveToAlertPosition;
      window.centerWindowOnScreen = this.centerWindowOnScreen;

      document.addEventListener(
        "keypress",
        event => {
          if (event.keyCode == KeyEvent.DOM_VK_RETURN) {
            this._hitEnter(event);
          } else if (
            event.keyCode == KeyEvent.DOM_VK_ESCAPE &&
            !event.defaultPrevented
          ) {
            this.cancelDialog();
          }
        },
        { mozSystemGroup: true }
      );

      if (AppConstants.platform == "macosx") {
        document.addEventListener(
          "keypress",
          event => {
            if (event.key == "." && event.metaKey) {
              this.cancelDialog();
            }
          },
          true
        );
      } else {
        this.addEventListener("focus", this, true);
        this.shadowRoot.addEventListener("focus", this, true);
      }

      // listen for when window is closed via native close buttons
      window.addEventListener("close", event => {
        if (!this.cancelDialog()) {
          event.preventDefault();
        }
      });

      // Call postLoadInit for things that we need to initialize after onload.
      if (document.readyState == "complete") {
        this._postLoadInit();
      } else {
        window.addEventListener("load", () => this._postLoadInit());
      }
    }

    set buttons(val) {
      this._configureButtons(val);
    }

    get buttons() {
      return this.getAttribute("buttons");
    }

    set defaultButton(val) {
      this._setDefaultButton(val);
    }

    get defaultButton() {
      if (this.hasAttribute("defaultButton")) {
        return this.getAttribute("defaultButton");
      }
      return "accept"; // default to the accept button
    }

    acceptDialog() {
      return this._doButtonCommand("accept");
    }

    cancelDialog() {
      return this._doButtonCommand("cancel");
    }

    getButton(aDlgType) {
      return this._buttons[aDlgType];
    }

    get buttonBox() {
      return this.shadowRoot.querySelector(".dialog-button-box");
    }

    // NOTE(emilio): This has to match AppWindow::IntrinsicallySizeShell, to
    // prevent flickering, see bug 1799394.
    _sizeToPreferredSize() {
      const docEl = document.documentElement;
      const prefWidth = (() => {
        if (docEl.hasAttribute("width")) {
          return parseInt(docEl.getAttribute("width"));
        }
        let prefWidthProp = docEl.getAttribute("prefwidth");
        if (prefWidthProp) {
          let minWidth = parseFloat(
            getComputedStyle(docEl).getPropertyValue(prefWidthProp)
          );
          if (isFinite(minWidth)) {
            return minWidth;
          }
        }
        return 0;
      })();
      window.sizeToContent({ prefWidth });
    }

    moveToAlertPosition() {
      // hack. we need this so the window has something like its final size
      if (window.outerWidth == 1) {
        dump(
          "Trying to position a sizeless window; caller should have called sizeToContent() or sizeTo(). See bug 75649.\n"
        );
        this._sizeToPreferredSize();
      }

      if (opener) {
        var xOffset = (opener.outerWidth - window.outerWidth) / 2;
        var yOffset = opener.outerHeight / 5;

        var newX = opener.screenX + xOffset;
        var newY = opener.screenY + yOffset;
      } else {
        newX = (screen.availWidth - window.outerWidth) / 2;
        newY = (screen.availHeight - window.outerHeight) / 2;
      }

      // ensure the window is fully onscreen (if smaller than the screen)
      if (newX < screen.availLeft) {
        newX = screen.availLeft + 20;
      }
      if (newX + window.outerWidth > screen.availLeft + screen.availWidth) {
        newX = screen.availLeft + screen.availWidth - window.outerWidth - 20;
      }

      if (newY < screen.availTop) {
        newY = screen.availTop + 20;
      }
      if (newY + window.outerHeight > screen.availTop + screen.availHeight) {
        newY = screen.availTop + screen.availHeight - window.outerHeight - 60;
      }

      window.moveTo(newX, newY);
    }

    centerWindowOnScreen() {
      var xOffset = screen.availWidth / 2 - window.outerWidth / 2;
      var yOffset = screen.availHeight / 2 - window.outerHeight / 2;

      xOffset = xOffset > 0 ? xOffset : 0;
      yOffset = yOffset > 0 ? yOffset : 0;
      window.moveTo(xOffset, yOffset);
    }

    // Give focus to the first focusable element in the dialog
    _setInitialFocusIfNeeded() {
      let focusedElt = document.commandDispatcher.focusedElement;
      if (focusedElt) {
        return;
      }

      const defaultButton = this.getButton(this.defaultButton);
      Services.focus.moveFocus(
        window,
        null,
        Services.focus.MOVEFOCUS_FORWARD,
        Services.focus.FLAG_NOPARENTFRAME
      );

      focusedElt = document.commandDispatcher.focusedElement;
      if (!focusedElt) {
        return; // No focusable element?
      }

      let firstFocusedElt = focusedElt;
      while (
        focusedElt.localName == "tab" ||
        focusedElt.getAttribute("noinitialfocus") == "true"
      ) {
        Services.focus.moveFocus(
          window,
          focusedElt,
          Services.focus.MOVEFOCUS_FORWARD,
          Services.focus.FLAG_NOPARENTFRAME
        );
        focusedElt = document.commandDispatcher.focusedElement;
        if (focusedElt == firstFocusedElt) {
          if (focusedElt.getAttribute("noinitialfocus") == "true") {
            focusedElt.blur();
          }
          // Didn't find anything else to focus, we're done.
          return;
        }
      }

      if (firstFocusedElt.localName == "tab") {
        if (focusedElt.hasAttribute("dlgtype")) {
          // We don't want to focus on anonymous OK, Cancel, etc. buttons,
          // so return focus to the tab itself
          firstFocusedElt.focus();
        }
      } else if (
        AppConstants.platform != "macosx" &&
        focusedElt.hasAttribute("dlgtype") &&
        defaultButton &&
        focusedElt != defaultButton
      ) {
        defaultButton.focus();
        if (document.commandDispatcher.focusedElement != defaultButton) {
          // If the default button is not focusable, then return focus to the
          // initial element if possible, or blur otherwise.
          if (firstFocusedElt.getAttribute("noinitialfocus") == "true") {
            focusedElt.blur();
          } else {
            firstFocusedElt.focus();
          }
        }
      }
    }

    async _postLoadInit() {
      this._setInitialFocusIfNeeded();
      let finalStep = () => {
        this._sizeToPreferredSize();
        this._snapCursorToDefaultButtonIfNeeded();
      };
      // As a hack to ensure Windows sizes the window correctly,
      // _sizeToPreferredSize() needs to happen after
      // AppWindow::OnChromeLoaded. That one is called right after the load
      // event dispatch but within the same task. Using direct dispatch let's
      // all this code run before the next task (which might be a task to
      // paint the window).
      // But, MacOS doesn't like resizing after window/dialog becoming visible.
      // Linux seems to be able to handle both cases.
      if (Services.appinfo.OS == "Darwin") {
        finalStep();
      } else {
        Services.tm.dispatchDirectTaskToCurrentThread(finalStep);
      }
    }

    // This snaps the cursor to the default button rect on windows, when
    // SPI_GETSNAPTODEFBUTTON is set.
    async _snapCursorToDefaultButtonIfNeeded() {
      const defaultButton = this.getButton(this.defaultButton);
      if (!defaultButton) {
        return;
      }
      try {
        // FIXME(emilio, bug 1797624): This setTimeout() ensures enough time
        // has passed so that the dialog vertical margin has been set by the
        // front-end. For subdialogs, cursor positioning should probably be
        // done by the opener instead, once the dialog is positioned.
        await new Promise(r => setTimeout(r, 0));
        await window.promiseDocumentFlushed(() => {});
        window.notifyDefaultButtonLoaded(defaultButton);
      } catch (e) {}
    }

    _configureButtons(aButtons) {
      // by default, get all the anonymous button elements
      var buttons = {};
      this._buttons = buttons;

      for (let type of ["accept", "cancel", "extra1", "extra2", "disclosure"]) {
        buttons[type] = this.shadowRoot.querySelector(`[dlgtype="${type}"]`);
      }

      // look for any overriding explicit button elements
      var exBtns = this.getElementsByAttribute("dlgtype", "*");
      var dlgtype;
      for (let i = 0; i < exBtns.length; ++i) {
        dlgtype = exBtns[i].getAttribute("dlgtype");
        buttons[dlgtype].hidden = true; // hide the anonymous button
        buttons[dlgtype] = exBtns[i];
      }

      // add the label and oncommand handler to each button
      for (dlgtype in buttons) {
        var button = buttons[dlgtype];
        button.addEventListener(
          "command",
          this._handleButtonCommand.bind(this),
          true
        );

        // don't override custom labels with pre-defined labels on explicit buttons
        if (!button.hasAttribute("label")) {
          // dialog attributes override the default labels in dialog.ftl
          if (this.hasAttribute("buttonlabel" + dlgtype)) {
            button.setAttribute(
              "label",
              this.getAttribute("buttonlabel" + dlgtype)
            );
            if (this.hasAttribute("buttonaccesskey" + dlgtype)) {
              button.setAttribute(
                "accesskey",
                this.getAttribute("buttonaccesskey" + dlgtype)
              );
            }
          } else if (this.hasAttribute("buttonid" + dlgtype)) {
            document.l10n.setAttributes(
              button,
              this.getAttribute("buttonid" + dlgtype)
            );
          } else if (DEFAULT_BUTTON_L10N_IDS[dlgtype]) {
            document.l10n.setAttributes(
              button,
              DEFAULT_BUTTON_L10N_IDS[dlgtype]
            );
          }
        }
      }

      // ensure that hitting enter triggers the default button command
      // eslint-disable-next-line no-self-assign
      this.defaultButton = this.defaultButton;

      // if there is a special button configuration, use it
      if (aButtons) {
        // expect a comma delimited list of dlgtype values
        var list = aButtons.split(",");

        // mark shown dlgtypes as true
        var shown = {
          accept: false,
          cancel: false,
          disclosure: false,
          extra1: false,
          extra2: false,
        };
        for (let i = 0; i < list.length; ++i) {
          shown[list[i].replace(/ /g, "")] = true;
        }

        // hide/show the buttons we want
        for (dlgtype in buttons) {
          buttons[dlgtype].hidden = !shown[dlgtype];
        }

        // show the spacer on Windows only when the extra2 button is present
        if (AppConstants.platform == "win") {
          let spacer = this.shadowRoot.querySelector(".button-spacer");
          spacer.removeAttribute("hidden");
          spacer.setAttribute("flex", shown.extra2 ? "1" : "0");
        }
      }
    }

    _setDefaultButton(aNewDefault) {
      // remove the default attribute from the previous default button, if any
      var oldDefaultButton = this.getButton(this.defaultButton);
      if (oldDefaultButton) {
        oldDefaultButton.removeAttribute("default");
      }

      var newDefaultButton = this.getButton(aNewDefault);
      if (newDefaultButton) {
        this.setAttribute("defaultButton", aNewDefault);
        newDefaultButton.setAttribute("default", "true");
      } else {
        this.setAttribute("defaultButton", "none");
        if (aNewDefault != "none") {
          dump(
            "invalid new default button: " + aNewDefault + ", assuming: none\n"
          );
        }
      }
    }

    _handleButtonCommand(aEvent) {
      return this._doButtonCommand(aEvent.target.getAttribute("dlgtype"));
    }

    _doButtonCommand(aDlgType) {
      var button = this.getButton(aDlgType);
      if (!button.disabled) {
        var noCancel = this._fireButtonEvent(aDlgType);
        if (noCancel) {
          if (aDlgType == "accept" || aDlgType == "cancel") {
            var closingEvent = new CustomEvent("dialogclosing", {
              bubbles: true,
              detail: { button: aDlgType },
            });
            this.dispatchEvent(closingEvent);
            window.close();
          }
        }
        return noCancel;
      }
      return true;
    }

    _fireButtonEvent(aDlgType) {
      var event = document.createEvent("Events");
      event.initEvent("dialog" + aDlgType, true, true);

      // handle dom event handlers
      return this.dispatchEvent(event);
    }

    _hitEnter(evt) {
      if (evt.defaultPrevented) {
        return;
      }

      var btn = this.getButton(this.defaultButton);
      if (btn && !btn.hidden) {
        this._doButtonCommand(this.defaultButton);
      }
    }

    on_focus(event) {
      let btn = this.getButton(this.defaultButton);
      if (btn) {
        btn.setAttribute(
          "default",
          event.originalTarget == btn ||
            !(
              event.originalTarget.localName == "button" ||
              event.originalTarget.localName == "toolbarbutton"
            )
        );
      }
    }
  }

  customElements.define("dialog", MozDialog);
}
