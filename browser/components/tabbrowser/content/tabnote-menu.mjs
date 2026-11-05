/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/* global gBrowser, MozXULElement, SidebarController */

const { TabNotes } = ChromeUtils.importESModule(
  "moz-src:///browser/components/tabnotes/TabNotes.sys.mjs"
);

const OVERFLOW_WARNING_THRESHOLD = 980;
const OVERFLOW_MAX_THRESHOLD = 1000;

const OverflowState = {
  NONE: "none",
  WARN: "warn",
  OVERFLOW: "overflow",
};

export class MozTabbrowserTabNoteMenu extends MozXULElement {
  static markup = /*html*/ `
  <panel
      id="tabNotePanel"
      type="arrow"
      titlebar="normal"
      class="tab-note-editor-panel panel-no-padding"
      orient="vertical"
      role="dialog"
      ignorekeys="true"
      norolluponanchor="true"
      consumeoutsideclicks="false">

      <html:div class="panel-header" id="tab-note-editor-header" >
        <html:h1
          id="tab-note-editor-title">
        </html:h1>
      </html:div>

      <toolbarseparator id="tab-note-editor-separator" />

      <html:div
        class="panel-subview-body
        tab-note-editor-name">
        <html:textarea
          id="tab-note-text"
          name="tab-note-text"
          rows="3"
          value=""
          data-l10n-id="tab-note-editor-text-field"
        ></html:textarea>
      </html:div>

      <html:div
        class="panel-action-row panel-footer">
        <html:moz-button
            id="tab-note-editor-button-delete"
            type="icon ghost"
            data-l10n-id="tab-note-editor-button-delete">
        </html:moz-button>
        <html:div
          id="tab-note-overflow-indicator">
        </html:div>
        <html:moz-button-group
            id="tab-note-default-actions">
            <html:moz-button
                id="tab-note-editor-button-cancel"
                data-l10n-id="tab-note-editor-button-cancel">
            </html:moz-button>
            <html:moz-button
                type="primary"
                id="tab-note-editor-button-save"
                data-l10n-id="tab-note-editor-button-save">
            </html:moz-button>
        </html:moz-button-group>
      </html:div>

  </panel>
     `;

  #initialized = false;
  #panel;
  #noteField;
  #headerEl;
  #separatorEl;
  #titleNode;
  /** @type {MozTabbrowserTab} */
  #currentTab = null;
  /** @type {boolean|null} */
  #createMode = null;
  #cancelButton;
  #saveButton;
  #deleteButton;
  #overflowIndicator;
  /** @type {TabNoteTelemetrySource|null} */
  #telemetrySource = null;

  connectedCallback() {
    if (this.#initialized) {
      return;
    }

    this.textContent = "";
    this.appendChild(
      /** @type {typeof MozTabbrowserTabNoteMenu} */ (this.constructor).fragment
    );
    this.initializeAttributeInheritance();

    this.#panel = this.querySelector("panel");
    this.#noteField = document.getElementById("tab-note-text");
    this.#headerEl = this.querySelector("#tab-note-editor-header");
    this.#separatorEl = this.querySelector("#tab-note-editor-separator");
    this.#titleNode = this.querySelector("#tab-note-editor-title");
    this.#cancelButton = this.querySelector("#tab-note-editor-button-cancel");
    this.#saveButton = this.querySelector("#tab-note-editor-button-save");
    this.#deleteButton = this.querySelector("#tab-note-editor-button-delete");
    this.#overflowIndicator = this.querySelector(
      "#tab-note-overflow-indicator"
    );

    this.#cancelButton.addEventListener("click", () => {
      this.#panel.hidePopup();
    });
    this.#saveButton.addEventListener("click", () => {
      this.saveNote();
    });
    this.#deleteButton.addEventListener("click", () => {
      this.#deleteNote();
    });
    this.#panel.addEventListener("keypress", this);
    this.#panel.addEventListener("popuphidden", this);
    this.#noteField.addEventListener("input", this);

    this.#initialized = true;
  }

  on_keypress(event) {
    if (event.defaultPrevented) {
      // The event has already been consumed inside of the panel.
      return;
    }

    switch (event.keyCode) {
      case KeyEvent.DOM_VK_ESCAPE:
        this.#panel.hidePopup();
        break;
      case KeyEvent.DOM_VK_RETURN:
        if (!event.shiftKey && event.target === this.#noteField) {
          this.saveNote();
        }
        break;
    }
  }

  on_input() {
    this.#updatePanel();
  }

  on_popuphidden() {
    this.#currentTab = null;
    this.#noteField.value = "";
    this.#telemetrySource = null;
  }

  get createMode() {
    return this.#createMode;
  }

  set createMode(createModeEnabled) {
    if (this.#createMode == createModeEnabled) {
      return;
    }
    let headerL10nId = createModeEnabled
      ? "tab-note-editor-title-create"
      : "tab-note-editor-title-edit";
    this.#titleNode.innerText =
      gBrowser.tabLocalization.formatValueSync(headerL10nId);
    this.#headerEl.hidden = !createModeEnabled;
    this.#separatorEl.hidden = !createModeEnabled;
    this.#deleteButton.hidden = createModeEnabled;
    this.#panel.setAttribute(
      "aria-label",
      gBrowser.tabLocalization.formatValueSync(headerL10nId)
    );

    this.#createMode = createModeEnabled;
  }

  get #panelPosition() {
    if (gBrowser.tabContainer.verticalMode) {
      return SidebarController._positionStart
        ? "topleft topright"
        : "topright topleft";
    }
    return "bottomleft topleft";
  }

  #updatePanel() {
    const inputLength = this.#noteField.value.length;
    const trimmedLength = this.#noteField.value.trim().length;

    let overflow;
    if (inputLength > OVERFLOW_MAX_THRESHOLD) {
      overflow = OverflowState.OVERFLOW;
    } else if (inputLength > OVERFLOW_WARNING_THRESHOLD) {
      overflow = OverflowState.WARN;
    } else {
      overflow = OverflowState.NONE;
    }

    this.#saveButton.disabled =
      overflow == OverflowState.OVERFLOW || trimmedLength === 0;

    if (overflow != OverflowState.NONE) {
      this.#panel.setAttribute("overflow", overflow);
      this.#overflowIndicator.innerText =
        gBrowser.tabLocalization.formatValueSync(
          "tab-note-editor-character-limit",
          {
            totalCharacters: inputLength,
            maxAllowedCharacters: OVERFLOW_MAX_THRESHOLD,
          }
        );
    } else {
      this.#panel.removeAttribute("overflow");
    }

    // Manually adjust panel height and scroll behaviour to compensate for input size
    // CSS has a `field-sizing` attribute that does this automatically,
    // but it is not yet supported.
    // TODO bug2006439: Replace this with `field-sizing` after the implementation of bug1832409
    this.#noteField.style.height = "auto"; // Reset height so previous manual adjustments do not affect calculations
    let computedStyle = getComputedStyle(this.#noteField);
    let contentHeight =
      this.#noteField.scrollHeight -
      parseFloat(computedStyle.paddingTop) -
      parseFloat(computedStyle.paddingBottom);
    this.#noteField.style.height = `${contentHeight}px`;
  }

  /**
   * @param {MozTabbrowserTab} tab
   *   The tab whose note this panel will control.
   * @param {object} [options]
   * @param {TabNoteTelemetrySource} [options.telemetrySource]
   *   The UI surface that requested to open this panel.
   */
  openPanel(tab, options = {}) {
    if (!TabNotes.isEligible(tab)) {
      return;
    }
    // Lazily set the icon to avoid loading it at startup
    if (!this.#deleteButton.iconSrc) {
      this.#deleteButton.iconSrc = "chrome://global/skin/icons/delete.svg";
    }
    this.#currentTab = tab;
    this.#telemetrySource = options.telemetrySource;

    TabNotes.get(tab).then(note => {
      if (note) {
        this.createMode = false;
        this.#noteField.value = note.text;
      } else {
        this.createMode = true;
      }

      this.#panel.addEventListener(
        "popupshown",
        () => {
          this.#noteField.focus();
        },
        {
          once: true,
        }
      );
      this.#panel.openPopup(tab, {
        position: this.#panelPosition,
      });

      this.#updatePanel();
    });
  }

  saveNote() {
    let note = this.#noteField.value;

    if (
      TabNotes.isEligible(this.#currentTab) &&
      note.trim().length &&
      note.length <= OVERFLOW_MAX_THRESHOLD
    ) {
      TabNotes.set(this.#currentTab, note, {
        telemetrySource: this.#telemetrySource,
      });
    }

    this.#panel.hidePopup();
  }

  #deleteNote() {
    if (TabNotes.isEligible(this.#currentTab)) {
      TabNotes.delete(this.#currentTab, {
        telemetrySource: this.#telemetrySource,
      });
    }
    this.#panel.hidePopup();
  }
}

customElements.define("tabnote-menu", MozTabbrowserTabNoteMenu);
