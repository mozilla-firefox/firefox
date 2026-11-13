/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

"use strict";

// This is loaded into chrome windows with the subscript loader. Wrap in
// a block to prevent accidentally leaking globals onto `window`.
{
  ChromeUtils.defineESModuleGetters(this, {
    ScreenshotsUtils:
      "moz-src:///browser/components/screenshots/ScreenshotsUtils.sys.mjs",
  });

  const { SELECTION_MODES } = ChromeUtils.importESModule(
    "moz-src:///browser/components/screenshots/ScreenshotsSelectionModes.sys.mjs"
  );

  class ScreenshotsButtons extends MozXULElement {
    static #templates = {};

    static observedAttributes = ["mode"];

    #renderedMode = null;

    static get markup() {
      return `
        <html:link rel="stylesheet" href="chrome://global/skin/global.css" />
        <html:link rel="stylesheet" href="chrome://browser/content/screenshots/screenshots-buttons.css" />
        <html:moz-button-group>
          <html:button id="visible-page" class="screenshot-button footer-button" data-l10n-id="screenshots-save-visible-button"></html:button>
          <html:button id="full-page" class="screenshot-button footer-button primary" data-l10n-id="screenshots-save-page-button"></html:button>
        </html:moz-button-group>
      `;
    }

    static get miniWindowMarkup() {
      return `
        <html:link rel="stylesheet" href="chrome://global/skin/global.css" />
        <html:link rel="stylesheet" href="chrome://browser/content/screenshots/screenshots-buttons.css" />
        <html:div class="mini-window-chooser">
          <html:h1 class="mini-window-title" data-l10n-id="mini-window-panel-header"></html:h1>
          <html:div class="mini-window-options">
            <html:button id="move-selection" class="mini-window-option" data-default="">
              <html:img class="mini-window-option-icon" src="chrome://browser/content/miniwindow/assets/part-of-page-20.svg" role="presentation"/>
              <html:span data-l10n-id="mini-window-panel-part-of-page"></html:span>
            </html:button>
            <html:button id="move-full-tab" class="mini-window-option">
              <html:img class="mini-window-option-icon" src="chrome://browser/content/miniwindow/assets/full-page-20.svg" role="presentation"/>
              <html:span data-l10n-id="mini-window-panel-full-page"></html:span>
            </html:button>
          </html:div>
        </html:div>
      `;
    }

    static fragmentFor(mode) {
      if (!ScreenshotsButtons.#templates[mode]) {
        ScreenshotsButtons.#templates[mode] = MozXULElement.parseXULToFragment(
          mode === SELECTION_MODES.MINI_WINDOW
            ? ScreenshotsButtons.miniWindowMarkup
            : ScreenshotsButtons.markup
        );
      }
      return ScreenshotsButtons.#templates[mode];
    }

    get isMiniWindow() {
      return this.#renderedMode === SELECTION_MODES.MINI_WINDOW;
    }

    /** The element that click events bubble to, whichever mode is rendered. */
    get clickTarget() {
      return this.isMiniWindow
        ? this.shadowRoot?.querySelector(".mini-window-options")
        : this.shadowRoot?.querySelector("moz-button-group");
    }
    get visibleButton() {
      return this.shadowRoot?.getElementById("visible-page");
    }
    get fullpageButton() {
      return this.shadowRoot?.getElementById("full-page");
    }
    get moveSelectionButton() {
      return this.shadowRoot?.getElementById("move-selection");
    }
    get moveFullTabButton() {
      return this.shadowRoot?.getElementById("move-full-tab");
    }

    #render() {
      let mode = this.getAttribute("mode");
      if (this.shadowRoot && this.#renderedMode === mode) {
        return;
      }
      let shadowRoot = this.shadowRoot ?? this.attachShadow({ mode: "open" });
      this.clickTarget?.removeEventListener("click", this);
      shadowRoot.replaceChildren();
      shadowRoot.append(ScreenshotsButtons.fragmentFor(mode).cloneNode(true));
      this.#renderedMode = mode;
      this.clickTarget.addEventListener("click", this);
    }

    attributeChangedCallback() {
      if (this.isConnected) {
        this.#render();
      }
    }

    connectedCallback() {
      let alreadyRendered = !!this.shadowRoot;
      this.#render();
      this.ownerDocument.l10n.connectRoot(this.shadowRoot);
      if (alreadyRendered) {
        // #render only rewires listeners when the markup changed.
        this.clickTarget.removeEventListener("click", this);
        this.clickTarget.addEventListener("click", this);
      }
    }

    disconnectedCallback() {
      this.ownerDocument.l10n.disconnectRoot(this.shadowRoot);
      this.clickTarget?.removeEventListener("click", this);
    }

    handleEvent(event) {
      // Clicks on the container itself (the gap between options) resolve to
      // no button, and the getters for whichever mode is not rendered are
      // also null - without this they would match each other.
      let button = event.target.closest("button");
      if (!button) {
        return;
      }
      let browser = gBrowser.selectedBrowser;
      switch (button) {
        case this.visibleButton:
          ScreenshotsUtils.takeScreenshot(browser, "Visible");
          break;
        case this.fullpageButton:
          ScreenshotsUtils.takeScreenshot(browser, "FullPage");
          break;
        case this.moveSelectionButton:
          // The overlay is already up; get out of its way so the user can
          // drag a region. Focus has to follow or it dies with the panel.
          ScreenshotsUtils.closePanel(browser);
          ScreenshotsUtils.moveFocusToContent(browser);
          break;
        case this.moveFullTabButton:
          ScreenshotsUtils.miniWindowFullTab(browser).catch(console.error);
          break;
      }
    }

    /**
     * Focus a button in the panel. "first"/"last" pick the edge button in
     * either mode, so tab focus can wrap between the panel and the overlay.
     * Otherwise the default is Move selection in mini-window mode and the
     * last-used save button in Screenshots mode.
     *
     * @param {string} buttonToFocus
     */
    async focusButton(buttonToFocus) {
      if (this.isMiniWindow) {
        // The chooser is plain markup, already rendered, so focus synchronously
        // - deferring it here races the panel teardown when the user picks a
        // button. "first"/"last" let tab focus wrap; Move selection is the
        // default so Enter does what dragging would have.
        if (buttonToFocus === "last") {
          this.moveFullTabButton.focus({ focusVisible: true });
        } else {
          this.moveSelectionButton.focus({ focusVisible: true });
        }
        return;
      }
      await this.clickTarget.updateComplete;
      if (buttonToFocus === "fullpage") {
        this.fullpageButton.focus({ focusVisible: true });
      } else if (buttonToFocus === "first") {
        this.clickTarget.firstElementChild.focus({ focusVisible: true });
      } else if (buttonToFocus === "last") {
        this.clickTarget.lastElementChild.focus({ focusVisible: true });
      } else {
        this.visibleButton.focus({ focusVisible: true });
      }
    }
  }

  customElements.define("screenshots-buttons", ScreenshotsButtons);
}
