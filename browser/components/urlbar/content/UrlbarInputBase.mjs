/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

import { SearchModeSwitcher } from "chrome://browser/content/urlbar/SearchModeSwitcher.mjs";
import { UrlbarChildController } from "chrome://browser/content/urlbar/UrlbarChildController.mjs";
import { UrlbarEventBufferer } from "chrome://browser/content/urlbar/UrlbarEventBufferer.mjs";
import { UrlbarContentUtils } from "chrome://browser/content/urlbar/UrlbarContentUtils.mjs";
import UrlbarPrefs from "chrome://browser/content/urlbar/UrlbarContentPrefs.mjs";
import { UrlbarQueryContext } from "chrome://browser/content/urlbar/UrlbarQueryContext.mjs";
import { UrlbarView } from "chrome://browser/content/urlbar/UrlbarView.mjs";
import { UrlbarShared } from "chrome://browser/content/urlbar/UrlbarShared.mjs";

/**
 * @import { UrlbarSearchOneOffs } from "moz-src:///browser/components/urlbar/UrlbarSearchOneOffs.sys.mjs"
 * @import { SearchEngine } from "moz-src:///toolkit/components/search/SearchEngine.sys.mjs"
 * @import { SuggestBackendMerino } from "moz-src:///browser/components/urlbar/private/SuggestBackendMerino.sys.mjs"
 * @import { PartialSearchEngine } from "chrome://browser/content/urlbar/SearchEngineStore.mjs"
 * @import { BrowserSearchTelemetry } from "moz-src:///browser/components/search/BrowserSearchTelemetry.sys.mjs"
 * @import { UrlbarLoadRequest, LoadURLParams } from "chrome://browser/content/urlbar/UrlbarShared.mjs"
 * @import { UrlbarAutofillData } from "chrome://browser/content/urlbar/UrlbarResult.mjs"
 * @import { UrlbarSearchData } from "moz-src:///browser/components/urlbar/UrlbarParentController.sys.mjs"
 */

/**
 * The current window mode.
 *
 * @typedef {"classic" | "private" | "smartwindow"} WindowMode
 */

/**
 * @typedef {object} AutofillPlaceholder
 * @property {string} value
 *   The autofill value.
 * @property {UrlbarAutofillData["type"]} [type]
 *   The autofill type.
 * @property {string} [adaptiveHistoryInput]
 *   If the type is "adaptive_url" or "adaptive_origin", this is the matching
 *   input value from adaptive history.
 * @property {number} selectionStart
 *   The selectionStart at the time of autofill.
 * @property {number} selectionEnd
 *   The selectionEnd at the time of autofill.
 * @property {string} [untrimmedValue]
 *   The untrimmed value including the protocol.
 */

/**
 * A search mode stored by setSearchMode.
 * Either `engineName`, `source`, or both must be present.
 *
 * @typedef {object} SearchMode
 * @property {string} [engineName]
 *   The name of a search engine to restrict to.
 * @property {Values<typeof UrlbarShared.RESULT_SOURCE>} [source]
 *   A result source to restrict to.
 * @property {string} entry
 *   How search mode was entered. This is recorded in event telemetry.
 *   One of the values in {@link UrlbarShared.SEARCH_MODE_ENTRY}.
 * @property {boolean} isPreview
 *   If true, we will preview search mode. Search mode preview does not record
 *   telemetry and has slighly different UI behavior. The preview is exited in
 *   favor of full search mode when a query is executed. False should be
 *   passed if the caller needs to enter search mode but expects it will not
 *   be interacted with right away.
 * @property {"keyword" | "symbol"} [restrictType]
 *   How the user typed the restriction. Set when the mode came from a restrict
 *   keyword or symbol.
 * @property {boolean} [isGeneralPurposeEngine]
 *   Whether the engine is a general-purpose search engine. Set for
 *   engine search modes.
 */

/**
 * Partial SearchMode object used to enter search mode.
 * `entry` defaults to "other" and `isPreview` to true.
 * `isGeneralPurposeEngine` is set depending on the search engine.
 *
 * @typedef {Partial<Omit<SearchMode, "isGeneralPurposeEngine">>} SearchModeInput
 */

const { XPCOMUtils } =
  typeof ChromeUtils == "undefined"
    ? { XPCOMUtils: null }
    : ChromeUtils.importESModule("resource://gre/modules/XPCOMUtils.sys.mjs");

const lazy = XPCOMUtils?.declareLazy({
  AIWindow:
    "moz-src:///browser/components/aiwindow/ui/modules/AIWindow.sys.mjs",
  BrowserUIUtils: "resource:///modules/BrowserUIUtils.sys.mjs",
  CustomizableUI:
    "moz-src:///browser/components/customizableui/CustomizableUI.sys.mjs",
  ExtensionSearchHandler:
    "resource://gre/modules/ExtensionSearchHandler.sys.mjs",
  ExtensionUtils: "resource://gre/modules/ExtensionUtils.sys.mjs",
  handleBounceEventTrigger:
    "moz-src:///browser/components/urlbar/UrlbarParentController.sys.mjs",
  QuickSuggest: "moz-src:///browser/components/urlbar/QuickSuggest.sys.mjs",
  ReaderMode: "moz-src:///toolkit/components/reader/ReaderMode.sys.mjs",
  SharingUtils: "moz-src:///browser/components/sharing/SharingUtils.sys.mjs",
  SearchUIUtils: "moz-src:///browser/components/search/SearchUIUtils.sys.mjs",
  UrlbarTokenizer:
    "moz-src:///browser/components/urlbar/UrlbarTokenizer.sys.mjs",
  UrlbarUtils: "moz-src:///browser/components/urlbar/UrlbarUtils.sys.mjs",
  UrlbarValueFormatter:
    "moz-src:///browser/components/urlbar/UrlbarValueFormatter.sys.mjs",
  UrlbarSearchTermsPersistence:
    "moz-src:///browser/components/urlbar/UrlbarSearchTermsPersistence.sys.mjs",
  UrlUtils: "resource://gre/modules/UrlUtils.sys.mjs",
  ClipboardHelper: {
    service: "@mozilla.org/widget/clipboardhelper;1",
    iid: Ci.nsIClipboardHelper,
  },
  QueryStringStripper: {
    service: "@mozilla.org/url-query-string-stripper;1",
    iid: Ci.nsIURLQueryStringStripper,
  },
});

const logger = () => UrlbarShared.getLogger({ prefix: "Input" });

const UNLIMITED_MAX_RESULTS = 99;

// `nsILoadInfo.SchemelessInputTypeSchemeful` and `SchemelessInputTypeSchemeless`,
// which a content realm has no `Ci` to read them from.
const SCHEMELESS_INPUT_SCHEMEFUL = 1;
const SCHEMELESS_INPUT_SCHEMELESS = 2;

const XHTML_NS = "http://www.w3.org/1999/xhtml";

/**
 * Parses an input's markup into a fragment. The markup is XML in the XHTML
 * namespace.
 *
 * @param {string} markup
 *   The markup to parse.
 * @returns {DocumentFragment}
 */
function parseMarkupToFragment(markup) {
  let parser = new DOMParser();

  // A template's contents stay inert until they are imported into a document.
  let doc = parser.parseFromString(
    `<template xmlns="${XHTML_NS}">${markup}</template>`,
    "application/xml"
  );
  if (doc.documentElement.localName == "parsererror") {
    throw new Error("not well-formed XML");
  }
  let fragment = /** @type {HTMLTemplateElement} */ (doc.documentElement)
    .content;
  // The markup is indented, and keeping the whitespace between elements as text
  // nodes changes the accessibility tree the input exposes.
  let walker = doc.createTreeWalker(fragment, NodeFilter.SHOW_TEXT);
  /** @type {Text[]} */
  let blank = [];
  while (walker.nextNode()) {
    let node = /** @type {Text} */ (walker.currentNode);
    if (!node.data.trim()) {
      blank.push(node);
    }
  }
  blank.forEach(node => node.remove());
  return fragment;
}

/**
 * Implements the text input part of search access points. Each access point
 * extends this class (e.g. `UrlbarInput`, `SearchbarInput`).
 */
export class UrlbarInputBase extends HTMLElement {
  static get #markup() {
    return `
      <div class="urlbar-background"/>
      <div class="urlbar-input-container"
           role="presentation"
           pageproxystate="invalid">
        <moz-urlbar-slot name="remote-control-box" />

        <moz-button class="searchmode-switcher chromeclass-toolbar-additional"
                    type="muted"
                    iconsrc="chrome://global/skin/icons/search-glass.svg"
                    data-l10n-id="urlbar-searchmode-default2"
                    tabindex="-1"
                    role="presentation">
          <!-- This span has no purpose other than making the moz-button think
               it contains text even when searchmode-switcher-title is hidden. -->
          <span class="urlbar-visually-hidden" aria-hidden="true">a</span>
          <span class="searchmode-switcher-content">
            <span class="searchmode-switcher-wordmark" aria-hidden="true" />
            <img class="searchmode-switcher-dropmarker"
                 data-l10n-id="urlbar-searchmode-dropmarker2"
                 draggable="false" />
            <span class="searchmode-switcher-title" />
            <button class="searchmode-switcher-close toolbarbutton-icon close-button"
                    data-l10n-id="urlbar-searchmode-exit-button2"
                    tabindex="-1"
                    keyNav="false" />
          </span>
        </moz-button>
        <!-- In XUL windows, this will be wrapped in a panel with class="searchmode-switcher-panel". -->
        <panel-list class="searchmode-switcher-panel-list"
                    click-on-mouseup="">
          <div class="searchmode-switcher-panel-description" role="heading" />
${
  UrlbarPrefs.get("browser.nova.enabled")
    ? '<hr class="searchmode-switcher-panel-installed-engine-separator"/><hr class="searchmode-switcher-panel-footer-separator"/>'
    : '<hr/><hr class="searchmode-switcher-panel-installed-engine-separator searchmode-switcher-panel-footer-separator"/>'
}
        </panel-list>

        <moz-urlbar-slot name="site-info" />
        <div class="urlbar-input-box">
          <!-- In the addressbar, there will be an input with id="urlbar-scheme" here. -->
          <input class="urlbar-input textbox-input"
                 role="combobox"
                 aria-autocomplete="both"
                 inputmode="search"
                 preserveundohistory=""
                 data-l10n-id="urlbar-placeholder"/>
        </div>
        <moz-urlbar-slot name="revert-button" />
        <img class="urlbar-icon urlbar-go-button"
             role="button"
             keyNav="false"
             data-l10n-id="urlbar-go-button2"/>
        <moz-urlbar-slot name="page-actions" />
      </div>
      <div class="urlbarView"
           popover="manual"
           role="group">
        <div class="urlbarView-background"/>
        <div class="urlbarView-results"
             role="listbox"/>
        <panel-list class="urlbarView-result-menu" accesskey-conflicts-bug="2073892"></panel-list>
        <moz-urlbar-slot name="search-one-offs" />
   </div>`;
  }

  /** @type {DocumentFragment} */
  static get fragment() {
    if (!UrlbarInputBase.#fragment) {
      UrlbarInputBase.#fragment = parseMarkupToFragment(
        UrlbarInputBase.#markup
      );
    }
    // @ts-ignore
    return document.importNode(UrlbarInputBase.#fragment, true);
  }

  static get observedAttributes() {
    return ["open"];
  }

  /**
   * The cached fragment.
   *
   * @type {DocumentFragment=}
   */
  static #fragment;

  static #inputFieldEvents = [
    "compositionstart",
    "compositionend",
    "contextmenu",
    "dragover",
    "dragstart",
    "drop",
    "focus",
    "blur",
    "input",
    "beforeinput",
    "keydown",
    "keyup",
    "mouseover",
    "overflow",
    "underflow",
    "paste",
    "scrollend",
    "select",
    "selectionchange",
  ];

  /**
   * Whether the element may show its popover at all: it has something to
   * overlay (a toolbar in a chrome window, the popover attribute in a page),
   * the window isn't in customize mode and the input isn't read-only.
   */
  #popoverAllowed = false;

  #gBrowserListenersAdded = false;
  #isAddressbar = false;
  /**
   * @see {UrlbarShared.navigationEnabled}
   */
  #navigationEnabled = false;
  /**
   * The search access point name of the UrlbarInput for use with telemetry or
   * logging.
   *
   * @type {UrlbarSapName}
   */
  #sapName;
  _userTypedValue = "";
  _actionOverrideKeyCount = 0;
  _lastValidURLStr = "";
  _valueOnLastSearch = "";
  _suppressStartQuery = false;
  _suppressPrimaryAdjustment = false;
  _lastSearchString = "";
  // Tracks IME composition.
  #compositionState = UrlbarShared.COMPOSITION.NONE;
  #compositionClosedPopup = false;
  #compositionHadText = false;
  // Bumped on each input-driven search; a deferred-Enter snapshots it so its
  // async keyup can tell whether a fresh search has started since.
  #inputEpoch = 0;

  valueIsTyped = false;

  // Properties accessed in tests.
  /** @type {Promise<void|UrlbarQueryContext>} */
  lastQueryContextPromise = Promise.resolve();

  /** @type {AutofillPlaceholder|null} */
  _autofillPlaceholder = null;

  /**
   * Created on Enter keydown and resolved on keyup or blur, so that
   * input is held back until the load has started.
   *
   * @type {PromiseWithResolvers<number | void> & {
   *   loadedContent?: boolean,
   *   inputEpoch?: number,
   * } | null}
   */
  _keyDownEnterDeferred = null;

  _resultForCurrentValue = null;
  _untrimmedValue = "";
  _wwwIsTrimmed = false;
  _enableAutofillPlaceholder = true;

  /**
   * The browser window or null in a child process.
   *
   * For the addressbar and searchbar, this is the same as the global `window`.
   * For the smartbar, this is the browser window containing its content window.
   * For the newtab search bar, this is null.
   *
   * @type {?BrowserWindow}
   */
  // @ts-expect-error bug 1957626
  browserWindow = window.browsingContext?.topChromeWindow ?? null;

  constructor() {
    super();

    this.isPrivate = UrlbarContentUtils.isWindowPrivate(window);

    UrlbarPrefs.addObserver(this);
    window.addEventListener("unload", () => {
      // Stop listening to pref changes to make sure we don't init the new
      // searchbar in closed windows that have not been gc'd yet.
      UrlbarPrefs.removeObserver(this);
    });
  }

  /**
   * Populates moz-urlbar-slots by moving all children with a urlbar-slot
   * attribute into their moz-urlbar-slots and removing the slots.
   *
   * Should only be called once all children have been parsed.
   */
  #populateSlots() {
    let urlbarSlots = this.querySelectorAll("moz-urlbar-slot[name]");
    for (let slot of urlbarSlots) {
      let slotName = slot.getAttribute("name");
      let nodes = this.querySelectorAll(`:scope > [urlbar-slot="${slotName}"]`);

      for (let node of nodes) {
        slot.parentNode.insertBefore(node, slot);
      }

      slot.remove();
    }

    // Slotted elements only used by the addressbar.
    // Will be null for searchbar and others.
    this._identityBox = this.querySelector(".identity-box");
    this._revertButton = this.querySelector(".urlbar-revert-button");
    // Pre scotch bonnet search mode indicator (addressbar only).
    this._searchModeIndicator = this.querySelector(
      "#urlbar-search-mode-indicator"
    );
    this._searchModeIndicatorTitle = this._searchModeIndicator?.querySelector(
      "#urlbar-search-mode-indicator-title"
    );
    this._searchModeIndicatorClose = this._searchModeIndicator?.querySelector(
      "#urlbar-search-mode-indicator-close"
    );
  }

  /**
   * Links the stylesheets a content document needs. A chrome window imports the
   * same set through browser-shared.css.
   */
  #addStylesheet() {
    const HREF = "chrome://browser/skin/urlbar.css";
    if (
      typeof ChromeUtils != "undefined" ||
      document.querySelector(`link[href="${HREF}"]`)
    ) {
      return;
    }
    let link = document.createElement("link");
    link.rel = "stylesheet";
    link.href = HREF;
    document.head.appendChild(link);
  }

  /**
   * Initialization that happens once on the first connect.
   */
  #init() {
    this.#sapName = /** @type {UrlbarSapName} */ (
      this.getAttribute("sap-name")
    );
    this.#isAddressbar = this.#sapName == "urlbar";
    this.#navigationEnabled = UrlbarShared.navigationEnabled(this.#sapName);

    this.#addStylesheet();

    this.appendChild(UrlbarInputBase.fragment);

    // Make sure all children have been parsed before calling #populateSlots.
    if (document.readyState === "loading") {
      document.addEventListener(
        "DOMContentLoaded",
        () => this.#populateSlots(),
        { once: true }
      );
    } else {
      this.#populateSlots();
    }

    this.panel = /** @type {HTMLElement} */ (this.querySelector(".urlbarView"));
    this._inputContainer = this.querySelector(".urlbar-input-container");
    this.inputField = /** @type {HTMLInputElement} */ (
      this.querySelector(".urlbar-input")
    );

    let resultListboxId = this.#sapName + "-results";
    this.querySelector(".urlbarView-results").id = resultListboxId;
    this.inputField.setAttribute("aria-controls", resultListboxId);

    if (this.#isAddressbar) {
      this.inputField.id = "urlbar-input";
      // A URL is inherently LTR, so the value's own direction lays the field
      // out. Elsewhere the field holds free-form text and takes the locale's
      // direction like any other text input.
      this.inputField.dir = "auto";
      // A chrome-only input mode: it offers the on-screen keyboard's URL keys
      // without the IME-closing effect that "url" has.
      this.inputField.inputMode = "mozAwesomebar";

      let schemeField = document.createElement("input");
      schemeField.id = "urlbar-scheme";
      schemeField.required = true;
      this.inputField.before(schemeField);
    }
    this.sapInit();

    this.controller = new UrlbarChildController({ input: this });
    this.controller.addListener(this);
    this.view = new UrlbarView(this);
    this.searchModeSwitcher = new SearchModeSwitcher(this);

    let searchModeSwitcherDescription = this.querySelector(
      ".searchmode-switcher-panel-description"
    );

    searchModeSwitcherDescription.setAttribute(
      "data-l10n-id",
      this.#isAddressbar && !UrlbarPrefs.get("browser.nova.enabled")
        ? "urlbar-searchmode-popup-one-off-header"
        : "urlbar-searchmode-popup-header"
    );

    // The event bufferer can be used to defer events that may affect users
    // muscle memory; for example quickly pressing DOWN+ENTER should end up
    // on a predictable result, regardless of the search status. The event
    // bufferer will invoke the handling code at the right time.
    this.eventBufferer = new UrlbarEventBufferer(this);

    // Forward certain properties.
    // Note if you are extending these, you'll also need to extend the inline
    // type definitions.
    const READ_WRITE_PROPERTIES = [
      "placeholder",
      "selectionStart",
      "selectionEnd",
    ];

    for (let property of READ_WRITE_PROPERTIES) {
      Object.defineProperty(this, property, {
        enumerable: true,
        get() {
          return this.inputField[property];
        },
        set(val) {
          this.inputField[property] = val;
        },
      });
    }

    // The engine name is not known yet, but update placeholder anyway to
    // reflect value of keyword.enabled or set the searchbar placeholder.
    this._setPlaceholder(null);

    if (this.sapName != "newtab_searchbar") {
      if (this.controller.maybeInitEngineStore()) {
        // Engine store is initialized now and placeholder with
        // engine name will be set in connectedCallback.
      } else {
        // This happens on browser startup. We wait a bit before
        // initializing the search service to improve startup times.
        this.#initEngineStoreAfterPaint().then(
          () => this.#deferUpdatePlaceholder(),
          () => {} // Do nothing if search service failed.
        );
      }
    } else {
      // In newtab, we don't wait before initializing the search service.
      // We also don't use #deferUpdatePlaceholder in newtab. This causes
      // flicker but it should be less often because of newtab preloading
      // and it allows us to never show the magnifying glass placeholder.
      this.controller.engineStore.init().then(
        () => this.searchModeSwitcher.updateSearchIcon(),
        () => {} // Do nothing if search service failed.
      );
    }
  }

  attributeChangedCallback(attribute, _oldValue, _newValue) {
    if (attribute != "open") {
      return;
    }

    this.updatePopover();
  }

  /**
   * Hook for subclass-specific initialization work, called during `#init`.
   * Default no-op.
   */
  sapInit() {}

  /**
   * Hook for subclass-specific context-menu items. Default no-op.
   */
  initSapContextMenuItems() {}

  /**
   * Hook for subclass-specific work at the end of connection. Default no-op.
   */
  sapConnectedCallback() {}

  /**
   * Hook for subclass-specific work at the start of disconnection. Default
   * no-op.
   */
  sapDisconnectedCallback() {}

  connectedCallback() {
    if (!this.controller) {
      this.#init();
    }

    // After #init(), which creates the view AddSearchEngineHelper needs.
    this.#initContextMenuItems();

    this.searchModeSwitcher.connect();

    if (this.inOverflowPanel && this.view.isOpen) {
      this.view.close();
    }

    this._initCopyCutController();

    // Don't attach event listeners if the urlbar is readonly.
    if (this.readOnly) {
      this.#popoverAllowed = false;
      this.updatePopover();
      // Focused won't be updated so remove it to avoid it becoming stale.
      this.removeAttribute("focused");
      return;
    }
    this.toggleAttribute("focused", this.focused);

    for (let event of UrlbarInputBase.#inputFieldEvents) {
      this.inputField.addEventListener(event, this);
    }

    // These are on the window to detect focusing shortcuts like F6.
    window.addEventListener("keydown", this);
    window.addEventListener("keyup", this);

    window.addEventListener("mousedown", this);
    if (UrlbarContentUtils.getPlatform() == "win") {
      window.addEventListener("draggableregionleftmousedown", this);
    }
    this.addEventListener("mousedown", this);

    // This listener handles clicks from our children too, included the search mode
    // indicator close button.
    this._inputContainer.addEventListener("click", this);
    this._inputContainer.addEventListener("auxclick", this);

    // This is used to detect commands launched from the panel, to avoid
    // recording abandonment events when the command causes a blur event.
    this.view.panel.addEventListener("command", this, true);

    if (this.browserWindow?.gBrowser) {
      // On startup, this will be called again by browser-init.js
      // once gBrowser has been initialized.
      this.addGBrowserListeners();
    }

    // If the engine store is not initialized yet, the placeholder
    // and icon will be updated in deferUpdatePlaceholder.
    if (this.controller.engineStore.initialized) {
      this.searchModeSwitcher.updateSearchIcon();
      this.updatePlaceholder();
    } else {
      this.#initPlaceholderFromPref();
    }

    this.#popoverAllowed =
      // A content document has no toolbar to overlay, so there the embedder
      // saying the bar is hosted in a page is what says it may use the top
      // layer.
      (typeof ChromeUtils == "undefined"
        ? this.hasAttribute("in-page")
        : !!this.closest("toolbar")) &&
      !document.documentElement.hasAttribute("customizing");
    this.updatePopover();

    this._addObservers();

    this.sapConnectedCallback();
  }

  disconnectedCallback() {
    this.sapDisconnectedCallback();

    this.searchModeSwitcher.disconnect();

    if (this._copyCutController) {
      this.inputField.controllers.removeController(this._copyCutController);
      delete this._copyCutController;
    }

    for (let event of UrlbarInputBase.#inputFieldEvents) {
      this.inputField.removeEventListener(event, this);
    }

    // These are on the window to detect focusing shortcuts like F6.
    window.removeEventListener("keydown", this);
    window.removeEventListener("keyup", this);

    window.removeEventListener("mousedown", this);
    if (UrlbarContentUtils.getPlatform() == "win") {
      window.removeEventListener("draggableregionleftmousedown", this);
    }
    this.removeEventListener("mousedown", this);

    // This listener handles clicks from our children too, included the search mode
    // indicator close button.
    this._inputContainer.removeEventListener("click", this);
    this._inputContainer.removeEventListener("auxclick", this);

    // This is used to detect commands launched from the panel, to avoid
    // recording abandonment events when the command causes a blur event.
    this.view.panel.removeEventListener("command", this, true);

    if (this.#gBrowserListenersAdded) {
      this.browserWindow.gBrowser.tabContainer.removeEventListener(
        "TabSelect",
        this
      );
      this.browserWindow.gBrowser.tabContainer.removeEventListener(
        "TabClose",
        this
      );
      this.browserWindow.gBrowser.removeTabsProgressListener(this);
      this.#gBrowserListenersAdded = false;
    }

    this.#removeContextMenuItems();

    this._removeObservers();
  }

  /**
   * The sets this input registered with the text context menu.
   *
   * @type {object[]}
   */
  #contextMenuItemSets = [];

  /**
   * Contributes this input's own items to the text context menu. A window
   * without one, i.e. any but a chrome window, gets no items.
   */
  #initContextMenuItems() {
    if (!window.EditContextMenu) {
      return;
    }

    this.#initPasteAndGo();
    if (this.#isAddressbar && UrlbarContentUtils.getPlatform() == "macosx") {
      // The share group handles the strip-on-share item on macOS.
      this.#initShareURL();
    } else {
      this._initStripOnShare();
    }
    if (this.#isAddressbar) {
      this._initAutofillDismiss();
    }
    this.initSapContextMenuItems();
    this.#initAddSearchEngines();
  }

  /**
   * The engine items are rebuilt on every open, so the item set claims whatever
   * AddSearchEngineHelper currently owns.
   */
  #initAddSearchEngines() {
    this.addContextMenuItems({
      createItems: () => {
        let fragment = document.createDocumentFragment();
        fragment.appendChild(
          this.addSearchEngineHelper.createContextSeparator()
        );
        return fragment;
      },
      onShowing: (input, items) => {
        items.length = 0;
        items.push(...this.addSearchEngineHelper.refreshContextMenu());
      },
    });
  }

  /**
   * Registers an item set with the text context menu, scoped to this input.
   *
   * @param {object} itemSet
   *   As passed to EditContextMenu.addItems(), minus `matches`.
   */
  addContextMenuItems(itemSet) {
    this.#contextMenuItemSets.push(
      window.EditContextMenu.addItems({
        ...itemSet,
        matches: input => input == this.inputField,
      })
    );
  }

  /**
   * Unregisters the item sets, so a disconnected input stops contributing to
   * the menu it shares with the other inputs of the window.
   */
  #removeContextMenuItems() {
    for (let itemSet of this.#contextMenuItemSets) {
      window.EditContextMenu.removeItems(itemSet);
    }
    this.#contextMenuItemSets = [];
  }

  addGBrowserListeners() {
    if (this.browserWindow.gBrowser && !this.#gBrowserListenersAdded) {
      this.browserWindow.gBrowser.tabContainer.addEventListener(
        "TabSelect",
        this
      );
      this.browserWindow.gBrowser.tabContainer.addEventListener(
        "TabClose",
        this
      );
      this.browserWindow.gBrowser.addTabsProgressListener(this);
      this.#gBrowserListenersAdded = true;
    }
  }

  #addSearchEngineHelper;

  #valueFormatter;

  /**
   * Manages the Add Search Engine contextual menu entries.
   */
  get addSearchEngineHelper() {
    return (this.#addSearchEngineHelper ??= new AddSearchEngineHelper(this));
  }

  #getValueFormatter() {
    return (this.#valueFormatter ??= new lazy.UrlbarValueFormatter(this));
  }

  get sapName() {
    return this.#sapName;
  }

  get isSidebarMode() {
    return false;
  }

  /**
   * Whether this is a bar dedicated to search.
   *
   * See `UrlbarShared.isSearchbarSAP`.
   *
   * @type {boolean}
   */
  get isSearchbarSAP() {
    return UrlbarShared.isSearchbarSAP(this.#sapName);
  }

  /**
   * Whether this input shows layout variant A. New Tab's registrant sets the
   * attribute from the urlbar's `newtabVariantA` Nimbus variable.
   *
   * @type {boolean}
   */
  get variantA() {
    return this.hasAttribute("variant-a");
  }

  /**
   * Whether this input shows layout variant B, with the search engine button on
   * its own row above the input. New Tab's registrant sets the attribute from
   * the urlbar's `newtabVariantB` Nimbus variable.
   *
   * @type {boolean}
   */
  get variantB() {
    return this.hasAttribute("variant-b");
  }

  /**
   * Gets the window mode for telemetry.
   *
   * @returns {WindowMode} The window mode.
   */
  get windowMode() {
    if (this.isPrivate) {
      return "private";
    }
    return lazy?.AIWindow.isAIWindowActive(this.browserWindow)
      ? "smartwindow"
      : "classic";
  }

  get parentController() {
    return this.controller.parentController;
  }

  blur() {
    this.inputField.blur();
  }

  set readOnly(val) {
    if (val != this.inputField.readOnly) {
      this.inputField.readOnly = val;
      if (this.isConnected) {
        this.disconnectedCallback();
        this.connectedCallback();
      }
    }
  }

  /**
   * @type {boolean}
   */
  get readOnly() {
    return this.inputField.readOnly;
  }

  /**
   * @type {typeof HTMLInputElement.prototype.placeholder}
   */
  placeholder;

  /**
   * @type {typeof HTMLInputElement.prototype.selectionStart}
   */
  selectionStart;

  /**
   * @type {typeof HTMLInputElement.prototype.selectionEnd}
   */
  selectionEnd;

  /**
   * Called when a urlbar or urlbar related pref changes.
   *
   * @param {string} pref
   *   The name of the pref. Relative to `browser.urlbar` for urlbar prefs.
   */
  onPrefChanged(pref) {
    switch (pref) {
      case "keyword.enabled":
        this.updatePlaceholder();
        break;
    }
  }

  /**
   * Applies styling to the text in the urlbar input, depending on the text.
   */
  formatValue() {
    // The editor may not exist if the toolbar is not visible.
    if (this.#isAddressbar && this.editor) {
      this.#getValueFormatter().update();
    }
  }

  focus() {
    let beforeFocus = new CustomEvent("beforefocus", {
      bubbles: true,
      cancelable: true,
    });
    this.inputField.dispatchEvent(beforeFocus);
    if (beforeFocus.defaultPrevented) {
      return;
    }

    this.inputField.focus();
  }

  select() {
    let beforeSelect = new CustomEvent("beforeselect", {
      bubbles: true,
      cancelable: true,
    });
    this.inputField.dispatchEvent(beforeSelect);
    if (beforeSelect.defaultPrevented) {
      return;
    }

    // See _on_select().  HTMLInputElement.select() dispatches a "select"
    // event but does not set the primary selection.
    this._suppressPrimaryAdjustment = true;
    this.inputField.select();
    this._suppressPrimaryAdjustment = false;
  }

  setSelectionRange(selectionStart, selectionEnd) {
    let beforeSelect = new CustomEvent("beforeselect", {
      bubbles: true,
      cancelable: true,
    });
    this.inputField.dispatchEvent(beforeSelect);
    if (beforeSelect.defaultPrevented) {
      return;
    }

    // See _on_select().  HTMLInputElement.select() dispatches a "select"
    // event but does not set the primary selection.
    this._suppressPrimaryAdjustment = true;
    this.inputField.setSelectionRange(selectionStart, selectionEnd);
    this._suppressPrimaryAdjustment = false;
  }

  /**
   * @param {MozBrowser} browser
   */
  saveSelectionStateForBrowser(browser) {
    let state = this.getBrowserState(browser);
    state.selection = {
      // When the value is empty, we're either on a blank page, or the whole
      // text has been edited away. In the latter case we'll restore value to
      // the current URI, and we want to fully select it.
      start: this.value ? this.selectionStart : 0,
      end: this.value ? this.selectionEnd : Number.MAX_SAFE_INTEGER,
      // When restoring a URI from an empty value, we don't want to untrim it.
      shouldUntrim:
        this.value && !this._protocolIsTrimmed && !this._wwwIsTrimmed,
    };
  }

  /**
   * @param {MozBrowser} browser
   */
  restoreSelectionStateForBrowser(browser) {
    // Address bar must be focused to untrim and for selection to make sense.
    this.focus();
    let state = this.getBrowserState(browser);
    if (state.selection) {
      if (state.selection.shouldUntrim) {
        this.#maybeUntrimUrl();
      }
      this.setSelectionRange(
        state.selection.start,
        // When selecting all the end value may be larger than the actual value.
        Math.min(state.selection.end, this.value.length)
      );
    }
  }

  /**
   * Sets the URI to display in the location bar.
   *
   * @param {object} [options]
   * @param {?nsIURI} [options.uri]
   *        If this is unspecified, the current URI will be used.
   * @param {boolean} [options.dueToTabSwitch=false]
   *        Whether this is being called due to switching tabs.
   * @param {boolean} [options.dueToSessionRestore=false]
   *        Whether this is being called due to session restore.
   * @param {boolean} [options.hideSearchTerms=false]
   *        True if userTypedValue should not be overidden by search terms
   *        and false otherwise.
   * @param {boolean} [options.isSameDocument=false]
   *        Whether the caller loaded a new document or not (e.g. location
   *        change from an anchor scroll or a pushState event).
   */
  setURI({
    uri = null,
    dueToTabSwitch = false,
    dueToSessionRestore = false,
    hideSearchTerms = false,
    isSameDocument = false,
  } = {}) {
    if (!this.#isAddressbar) {
      throw new Error(
        "Cannot set URI for UrlbarInput that is not an address bar"
      );
    }
    if (
      window.browsingContext.isDocumentPiP &&
      uri.spec.startsWith("about:blank")
    ) {
      // If this is a Document PiP, its url will be about:blank while
      // the opener will be a secure context, i.e. no about:blank
      throw new Error("Document PiP should show its opener URL");
    }

    // A load finishing behind the open view must not overwrite the value the
    // user's selection put in the input, otherwise Enter won't pick it.
    if (uri && !dueToTabSwitch && this.view.selectedResult) {
      return;
    }

    // We only need to update the searchModeUI on tab switch conditionally
    // as we only persist searchMode with ScotchBonnet enabled.
    if (
      dueToTabSwitch &&
      UrlbarPrefs.getScotchBonnetPref("scotchBonnet.persistSearchMode")
    ) {
      this._updateSearchModeUI(this.searchMode);
    }

    let state = this.getBrowserState(
      this.browserWindow.gBrowser.selectedBrowser
    );
    this.#handlePersistedSearchTerms({
      state,
      uri,
      dueToTabSwitch,
      hideSearchTerms,
      isSameDocument,
    });

    let value = this.userTypedValue;
    let valid = false;
    let isReverting = !uri;

    // If `value` is null or if it's an empty string and we're switching tabs
    // set value to the browser's current URI. When a user empties the input,
    // switches tabs, and switches back, we want the URI to become visible again
    // so the user knows what URI they're viewing.
    // An exception to this is made in case of an auth request from a different
    // base domain. To avoid auth prompt spoofing we already display the url of
    // the cross domain resource, although the page is not loaded yet.
    // This url will be set/unset by PromptParent. See bug 791594 for reference.
    if (value === null || (!value && dueToTabSwitch)) {
      uri =
        this.browserWindow.gBrowser.selectedBrowser.currentAuthPromptURI ||
        uri ||
        this.#isOpenedPageInBlankTargetLoading ||
        this.browserWindow.gBrowser.currentURI;
      // Strip off usernames and passwords for the location bar
      try {
        uri = Services.io.createExposableURI(uri);
      } catch (e) {}

      let isInitialPageControlledByWebContent = false;

      // Replace initial page URIs with an empty string
      // only if there's no opener (bug 370555).
      if (
        this.browserWindow.isInitialPage(uri) &&
        lazy.BrowserUIUtils.checkEmptyPageOrigin(
          this.browserWindow.gBrowser.selectedBrowser,
          uri
        )
      ) {
        value = "";
      } else {
        isInitialPageControlledByWebContent = true;

        // We should deal with losslessDecodeDisplaySpec throwing for exotic URIs
        try {
          value = losslessDecodeDisplaySpec(uri.displaySpec);
        } catch (ex) {
          value = "about:blank";
        }
      }
      // If we update the URI while restoring a session, set the proxyState to
      // invalid, because we don't have a valid security state to show via site
      // identity yet. See Bug 1746383.
      valid =
        !dueToSessionRestore &&
        (!this.#canHandleAsBlankPage(uri.spec) ||
          lazy.ExtensionUtils.isExtensionUrl(uri) ||
          isInitialPageControlledByWebContent);
    } else if (
      this.browserWindow.isInitialPage(value) &&
      lazy.BrowserUIUtils.checkEmptyPageOrigin(
        this.browserWindow.gBrowser.selectedBrowser
      )
    ) {
      value = "";
      valid = true;
    }

    const previousUntrimmedValue = this.untrimmedValue;
    // When calculating the selection indices we must take into account any
    // trimmed prefix (protocol and potentially "www.").
    let offset =
      (this._protocolIsTrimmed
        ? lazy.BrowserUIUtils.trimURLProtocol.length
        : 0) + (this._wwwIsTrimmed ? "www.".length : 0);
    const previousSelectionStart = this.selectionStart + offset;
    const previousSelectionEnd = this.selectionEnd + offset;

    this.setValue(value, { allowTrim: true, valueIsTyped: !valid });
    this.toggleAttribute("usertyping", !valid && value);

    if (this.focused && value != previousUntrimmedValue) {
      if (
        previousSelectionStart != previousSelectionEnd &&
        value.substring(previousSelectionStart, previousSelectionEnd) ===
          previousUntrimmedValue.substring(
            previousSelectionStart,
            previousSelectionEnd
          )
      ) {
        // If the same text is in the same place as the previously selected text,
        // the selection is kept.
        this.inputField.setSelectionRange(
          previousSelectionStart - offset,
          previousSelectionEnd - offset
        );
      } else if (
        previousSelectionEnd &&
        (previousUntrimmedValue.length === previousSelectionEnd ||
          value.length <= previousSelectionEnd)
      ) {
        // If the previous end caret is not 0 and the caret is at the end of the
        // input or its position is beyond the end of the new value, keep the
        // position at the end.
        this.inputField.setSelectionRange(value.length, value.length);
      } else {
        // Otherwise clear selection and set the caret position to the previous
        // caret end position.
        this.inputField.setSelectionRange(
          previousSelectionEnd - offset,
          previousSelectionEnd - offset
        );
      }
    }

    // The proxystate must be set before setting search mode below because
    // search mode depends on it.
    this.setPageProxyState(
      valid ? "valid" : "invalid",
      dueToTabSwitch,
      !isReverting &&
        dueToTabSwitch &&
        this.getBrowserState(this.browserWindow.gBrowser.selectedBrowser)
          .isUnifiedSearchButtonAvailable
    );

    if (
      state.persist?.shouldPersist &&
      !lazy.UrlbarSearchTermsPersistence.searchModeMatchesState(
        this.searchMode,
        state
      )
    ) {
      // When search terms persist, on non-default engine search result pages
      // the address bar should show the same search mode. For default engines,
      // search mode should not persist.
      if (state.persist.isDefaultEngine) {
        this.searchMode = null;
      } else {
        this.searchMode = {
          engineName: state.persist.originalEngineName,
          source: UrlbarShared.RESULT_SOURCE.SEARCH,
          isPreview: false,
        };
      }
    } else if (dueToTabSwitch && !valid) {
      // If we're switching tabs, restore the tab's search mode.
      this.restoreSearchModeState();
    } else if (valid) {
      // If the URI is valid, exit search mode.  This must happen
      // after setting proxystate above because search mode depends on it.
      this.searchMode = null;
    }

    // Dispatch URIUpdate event to synchronize the tab status when switching.
    let event = new CustomEvent("SetURI", { bubbles: true });
    this.inputField.dispatchEvent(event);
  }

  /**
   * Converts an internal URI (e.g. a URI with a username or password) into one
   * which we can expose to the user.
   *
   * @param {nsIURI} uri
   *   The URI to be converted
   * @returns {nsIURI}
   *   The converted, exposable URI
   */
  makeURIReadable(uri) {
    // Avoid copying 'about:reader?url=', and always provide the original URI:
    // Reader mode ensures we call createExposableURI itself.
    let readerStrippedURI = lazy.ReaderMode.getOriginalUrlObjectForDisplay(
      uri.displaySpec
    );
    if (readerStrippedURI) {
      return readerStrippedURI;
    }

    try {
      return Services.io.createExposableURI(uri);
    } catch (ex) {}

    return uri;
  }

  /**
   * Function for tabs progress listener.
   *
   * @param {MozBrowser} browser
   * @param {nsIWebProgress} webProgress
   *   The nsIWebProgress instance that fired the notification.
   * @param {nsIRequest} request
   *   The associated nsIRequest.  This may be null in some cases.
   * @param {nsIURI} locationURI
   *   The URI of the location that is being loaded.
   */
  onLocationChange(browser, webProgress, request, locationURI) {
    if (!webProgress.isTopLevel) {
      return;
    }

    if (
      browser != this.browserWindow.gBrowser.selectedBrowser &&
      !this.#canHandleAsBlankPage(locationURI.spec)
    ) {
      // If the page is loaded on background tab, make Unified Search Button
      // unavailable when back to the tab.
      this.getBrowserState(browser).isUnifiedSearchButtonAvailable = false;
    }

    // Using browser navigation buttons should potentially trigger a bounce
    // telemetry event.
    if (webProgress.loadType & Ci.nsIDocShell.LOAD_CMD_HISTORY) {
      lazy.handleBounceEventTrigger(browser);
    }
  }

  /**
   * Passes DOM events to the _on_<event type> methods.
   *
   * @param {Event} event The event to handle.
   */
  handleEvent(event) {
    let methodName = "_on_" + event.type;
    if (methodName in this) {
      try {
        this[methodName](event);
      } catch (e) {
        console.error(`Error calling UrlbarInput::${methodName}:`, e);
      }
    } else {
      throw new Error("Unrecognized UrlbarInput event: " + event.type);
    }
  }

  /**
   * Handles an event which might open text or a URL. If the event requires
   * doing so, handleCommand forwards it to handleNavigation.
   *
   * @param {MouseEvent | KeyboardEvent} [event] The event triggering the open.
   */
  handleCommand(event = null) {
    const isMouseEvent = UrlbarShared.isInstance(event, MouseEvent);
    if (isMouseEvent && event.button == 2) {
      // Do nothing for right clicks.
      return;
    }

    // Determine whether to use the selected one-off search button.  In
    // one-off search buttons parlance, "selected" means that the button
    // has been navigated to via the keyboard.  So we want to use it if
    // the triggering event is not a mouse click -- i.e., it's a Return
    // key -- or if the one-off was mouse-clicked.
    if (this.view.isOpen) {
      let selectedOneOff = this.view.oneOffSearchButtons?.selectedButton;
      if (selectedOneOff && (!isMouseEvent || event.target == selectedOneOff)) {
        this.view.oneOffSearchButtons.handleSearchCommand(event, {
          engineName: selectedOneOff.engine?.name,
          source: selectedOneOff.source,
          entry: "oneoff",
        });
        return;
      }
    }

    if (UrlbarPrefs.get("unifiedSearchButton.always")) {
      this.searchModeSwitcher?.updateSearchIcon();
    }

    this.handleNavigation({ event });
  }

  /**
   * @typedef {object} HandleNavigationOneOffParams
   *
   * @property {string} openWhere
   *   Where we expect the result to be opened.
   * @property {object} openParams
   *   The parameters related to where the result will be opened.
   * @property {SearchEngine} engine
   *   The selected one-off's engine.
   */

  /**
   * The search-mode engine an Enter-key press should search, or null. In an
   * engine search mode the selected engine is authoritative; on the message path
   * the auto-selected heuristic can lag a search-mode change (its query is in
   * flight), so a stale heuristic would search the previous engine. Returns null
   * when an explicitly selected (non-heuristic) result should be used instead.
   *
   * @param {?UrlbarResult} result The result Enter would act on.
   * @param {boolean} isComposing Whether IME composition is active.
   * @param {?HandleNavigationOneOffParams} oneOffParams The one-off params, if any.
   * @returns {?PartialSearchEngine} The engine, or null.
   */
  #searchModeEngineForEnterKey(result, isComposing, oneOffParams) {
    if (
      // Only the address bar opens the engine's search URL directly on Enter.
      // The searchbar has no URL-loading path -- its input is always a search
      // term -- so it routes the engine search through its result-pick path;
      // handing it a URL here would get re-searched as a query.
      !this.#isAddressbar ||
      isComposing ||
      oneOffParams?.engine ||
      !this.searchMode?.engineName ||
      (result &&
        !(result.heuristic && result.type == UrlbarShared.RESULT_TYPE.SEARCH))
    ) {
      return null;
    }
    if (
      !result &&
      this._resultForCurrentValue?.type == UrlbarShared.RESULT_TYPE.URL &&
      UrlbarShared.navigationInSearchModeEnabled(this.#sapName)
    ) {
      return null;
    }
    return this.controller.engineStore.getEngineByName(
      this.searchMode.engineName
    );
  }

  /**
   * The search string for an engine search taken from a result: a selected
   * suggestion or query, falling back to the last search string.
   *
   * @param {?UrlbarResult} result The result the search string is taken from.
   * @returns {string} The search string.
   */
  #engineSearchStringForResult(result) {
    return (
      (result && (result.payload.suggestion || result.payload.query)) ||
      this._lastSearchString
    );
  }

  /**
   * The selected browser's id, or null in a window without `gBrowser`.
   *
   * @type {?number}
   */
  get #selectedBrowserId() {
    return this.browserWindow?.gBrowser?.selectedBrowser?.browserId ?? null;
  }

  /**
   * Records an engagement that loads a page, and tracks that page as a
   * potential bounce. Bounce tracking keys on the tab the engagement happened
   * in: the chrome window's selected tab, or the tab hosting an input that has
   * no chrome window, which the parent resolves. The bounce is tracked first
   * because recording the engagement ends the interaction it describes.
   *
   * @param {?Event} event The triggering event.
   * @param {object} details The engagement details, as for `record()`.
   */
  #recordEngagementAndTrackBounce(event, details) {
    this.controller.engagementEvent
      .startTrackingBounceEvent(this.#selectedBrowserId, event, details)
      .catch(e => logger().error(e));
    this.controller.engagementEvent.record(event, details);
  }

  /**
   * Records the engagement and a search against an engine, adds it to form
   * history, and loads its SERP as a search visit. Shared by the one-off and
   * search-mode Enter paths, which return once it's done. The engine only needs
   * to carry an id and name -- the parent resolves the full engine and builds
   * the submission URL -- so this stays content-safe for a message-path
   * `<moz-urlbar>`.
   *
   * @param {PartialSearchEngine|SearchEngine} engine The engine to search.
   * @param {string} searchString The string to search for.
   * @param {string} where Where the SERP will open.
   * @param {LoadURLParams} openParams The load's params.
   * @param {object} details
   * @param {?Event} details.event The triggering event.
   * @param {?HTMLElement} details.element The picked view element, if any.
   * @param {string} details.selType The engagement's selection type.
   * @param {string} details.typedValue The value the engagement records.
   * @param {?UrlbarResult} details.result The result Enter acted on, if any.
   */
  #openEngineSearch(
    engine,
    searchString,
    where,
    openParams,
    { event, element, selType, typedValue, result }
  ) {
    this.#recordEngagementAndTrackBounce(event, {
      element,
      selType,
      searchString: typedValue,
      result:
        result ||
        this.view.selectedResult ||
        this._resultForCurrentValue ||
        null,
      searchSource: this.getSearchSource(event),
      windowMode: this.windowMode,
    });
    let searchData = this.#getSearchData({
      engine,
      event,
      query: searchString,
    });
    openParams.allowInheritPrincipal = false;
    openParams.globalHistoryOptions = {
      ...openParams.globalHistoryOptions,
      triggeringSource: this.#sapName,
      triggeringSearchEngine: engine.name,
    };
    this.#loadURL({
      loadRequest: {
        engineSearch: { engineName: engine.name, query: searchString },
      },
      event,
      where,
      params: openParams,
      browserId: this.#selectedBrowserId,
      searchData,
    });
  }

  /**
   * Handles an event which would cause a URL or text to be opened.
   *
   * @param {object} options
   *   Options for the navigation.
   * @param {MouseEvent | KeyboardEvent} [options.event]
   *   The event triggering the open.
   * @param {HandleNavigationOneOffParams} [options.oneOffParams]
   *   Optional. Pass if this navigation was triggered by a one-off. Practically
   *   speaking, UrlbarSearchOneOffs passes this when the user holds certain key
   *   modifiers while picking a one-off. In those cases, we do an immediate
   *   search using the one-off's engine instead of entering search mode.
   * @param {object} [options.triggeringPrincipal]
   *   The principal that the action was triggered from.
   */
  handleNavigation({ event, oneOffParams, triggeringPrincipal }) {
    let element = this.view.selectedElement;
    let result = this.view.getResultFromElement(element);
    let openParams = oneOffParams?.openParams || { triggeringPrincipal };

    // If the value was submitted during composition, the result may not have
    // been updated yet, because the input event happens after composition end.
    // We can't trust element nor _resultForCurrentValue targets in that case,
    // so we always generate a new heuristic to load.
    let isComposing = this.isComposing;

    // Use the selected element if we have one; this is usually the case
    // when the view is open.
    let selectedPrivateResult =
      result &&
      result.type == UrlbarShared.RESULT_TYPE.SEARCH &&
      result.payload.inPrivateWindow;
    let selectedPrivateEngineResult =
      selectedPrivateResult && result.payload.isPrivateEngine;
    // Whether the user has been editing the value in the URL bar after selecting
    // the result. However, if the result type is tip, pick as it is. The result
    // heuristic is also kept the behavior as is for safety.
    let safeToPickResult =
      result &&
      (result.heuristic ||
        !this.valueIsTyped ||
        result.type == UrlbarShared.RESULT_TYPE.TIP ||
        this.value == this.#getValueFromResult(result));

    // In an engine search mode the selected engine is authoritative at Enter
    // (see #searchModeEngineForEnterKey); a stale heuristic must not be picked.
    let searchModeEngine = this.#searchModeEngineForEnterKey(
      result,
      isComposing,
      oneOffParams
    );

    if (
      !isComposing &&
      element &&
      !searchModeEngine &&
      (!oneOffParams?.engine || selectedPrivateEngineResult) &&
      safeToPickResult
    ) {
      this.pickElement(element, event);
      return;
    }

    // Use the hidden heuristic if it exists and there's no selection.
    if (
      UrlbarPrefs.get("experimental.hideHeuristic") &&
      !element &&
      !isComposing &&
      !oneOffParams?.engine &&
      !searchModeEngine &&
      this._resultForCurrentValue?.heuristic
    ) {
      this.pickResult({ result: this._resultForCurrentValue, event });
      return;
    }

    // We don't select a heuristic result when we're autofilling a token alias,
    // but we want pressing Enter to behave like the first result was selected.
    if (!result && this.value.startsWith("@")) {
      let tokenAliasResult = this.view.getResultAtIndex(0);
      if (tokenAliasResult?.autofill && tokenAliasResult?.payload.keyword) {
        this.pickResult({ result: tokenAliasResult, event });
        return;
      }
    }

    let selType = this.view.telemetryTypeFromElement(result, element);
    let typedValue = this.value;
    if (oneOffParams?.engine) {
      result = this._resultForCurrentValue;
      this.#openEngineSearch(
        oneOffParams.engine,
        this.#engineSearchStringForResult(result),
        oneOffParams.openWhere,
        openParams,
        {
          event,
          element,
          selType: "oneoff",
          typedValue: this._lastSearchString,
          result,
        }
      );
      return;
    }
    if (searchModeEngine) {
      this.#openEngineSearch(
        searchModeEngine,
        typedValue,
        this.controller.whereToOpen(event),
        openParams,
        { event, element, selType, typedValue, result }
      );
      return;
    }

    // Use the current value if we don't have a UrlbarResult e.g. because the
    // view is closed.
    let url = this.untrimmedValue;

    if (!url) {
      this.handleEmptyValueNavigation(event);
      return;
    }

    // When the user hits enter in a local search mode and there's no selected
    // result or one-off, don't do anything.
    if (
      this.searchMode &&
      !this.searchMode.engineName &&
      !result &&
      !oneOffParams
    ) {
      return;
    }

    let where = oneOffParams?.openWhere || this.controller.whereToOpen(event);
    if (selectedPrivateResult) {
      where = "window";
      openParams.private = true;
    }
    openParams.allowInheritPrincipal = false;
    url = this._maybeCanonizeURL(event, url) || url.trim();

    let selectedResult = result || this.view.selectedResult;
    this.#recordEngagementAndTrackBounce(event, {
      element,
      selType,
      searchString: typedValue,
      result: selectedResult || this._resultForCurrentValue || null,
      searchSource: this.getSearchSource(event),
      windowMode: this.windowMode,
    });

    // The load resolves asynchronously, since the parent controller owns the
    // navigation, so capture the target tab now, at the commit: a tab opened
    // before it resolves must not steal the load. The chrome address bar reads
    // its selected tab here; a content-process moz-urlbar has no gBrowser and
    // leaves the target to the parent (its own tab).
    let browserId = this.#selectedBrowserId;

    if (this.#isAddressbar && URL.canParse(url)) {
      // Annotate if the untrimmed value contained a scheme, to later potentially
      // be upgraded by schemeless HTTPS-First.
      openParams.schemelessInput = this.#getSchemelessInput(
        this.untrimmedValue
      );
      this.#loadURL({
        loadRequest: { urlLoad: { url, postData: null } },
        event,
        where,
        params: openParams,
        browserId,
      });
      return;
    }

    // This is not a URL and there's no selected element, because likely the
    // view is closed, or paste&go was used.
    // We must act consistently here, having or not an open view should not
    // make a difference if the search string is the same.

    // If we have a result for the current value, we can just use it.
    if (!isComposing && this._resultForCurrentValue) {
      this.pickResult({ result: this._resultForCurrentValue, event });
      return;
    }

    // Otherwise, we must fetch the heuristic result for the current value.
    // TODO (Bug 1604927): If the urlbar results are restricted to a specific
    // engine, here we must search with that specific engine; indeed the
    // docshell wouldn't know about our engine restriction.
    // Also remember to pass the search data from this.#getSearchData to the
    // load, after replacing url with the appropriate engine submission url.
    //
    // The heuristic fetch and its uriFixup fallback are parent-only, and they
    // depend on the current browser's per-tab data and navigation epoch, which
    // a content urlbar can't read; the parent controller owns all of it and
    // hands back either a heuristic result to pick or a fixup URL to load.
    this.controller
      .resolveFallbackNavigation({
        searchString: url,
        where,
        searchMode: this.searchMode,
        browserId,
      })
      .then(({ heuristicResult, fixup }) => {
        if (heuristicResult) {
          this.pickResult({ result: heuristicResult, event, browserId });
        } else if (fixup) {
          if (!fixup.keywordAsSent) {
            // `fixup.url` is not a search engine url, so we annotate if the
            // untrimmed value contained a scheme, to potentially be later
            // upgraded by schemeless HTTPS-First.
            openParams.schemelessInput = this.#getSchemelessInput(
              this.untrimmedValue
            );
          }
          this.#loadURL({
            loadRequest: {
              urlLoad: { url: fixup.url, postData: fixup.postData },
            },
            event,
            where,
            params: openParams,
            browserId,
          });
        }
      })
      .catch(console.error);
    // resolveFallbackNavigation is the last resort; nothing more to handle here.
  }

  /**
   * Handles navigation when there is no URL to load. The base does nothing;
   * subclasses such as the searchbar open the search engine page for the
   * active or default engine.
   *
   * @param {Event} [_event]
   *   The event triggering the open.
   */
  handleEmptyValueNavigation(_event) {}

  handleRevert() {
    this.userTypedValue = null;
    // Nullify search mode before setURI so it won't try to restore it.
    this.searchMode = null;
    if (!this.#isAddressbar) {
      this.value = "";
      this.toggleAttribute("usertyping", false);
      return;
    }

    this.setURI({
      dueToTabSwitch: true,
      hideSearchTerms: true,
    });

    if (this.value && this.focused) {
      this.select();
    }
  }

  maybeHandleRevertFromPopup(anchorElement) {
    let state = this.getBrowserState(
      this.browserWindow.gBrowser.selectedBrowser
    );
    if (anchorElement?.closest("#urlbar") && state.persist?.shouldPersist) {
      this.handleRevert();
      Glean.urlbarPersistedsearchterms.revertByPopupCount.add(1);
    }
  }

  /**
   * Called by inputs that resemble search boxes, but actually hand input off
   * to the Urlbar. We use these fake inputs on the new tab page and
   * about:privatebrowsing.
   *
   * @param {string} searchString
   *   The search string to use.
   * @param {SearchEngine} [searchEngine]
   *   Optional. If included and the right prefs are set, we will enter search
   *   mode when handing `searchString` from the fake input to the Urlbar.
   * @param {string} [newtabSessionId]
   *   Optional. The id of the newtab session that handed off this search.
   */
  handoff(searchString, searchEngine, newtabSessionId) {
    this._isHandoffSession = true;
    this._handoffSession = newtabSessionId;
    if (UrlbarPrefs.get("shouldHandOffToSearchMode") && searchEngine) {
      this.search(searchString, {
        searchEngine,
        searchModeEntry: "handoff",
      });
    } else {
      this.search(searchString);
    }
  }

  /**
   * Called when an element of the view is picked.
   *
   * @param {HTMLElement} element The element that was picked.
   * @param {Event} event The event that picked the element.
   */
  pickElement(element, event) {
    let result = this.view.getResultFromElement(element);
    logger().debug(
      `pickElement ${element} with event ${event?.type}, result: ${result}`
    );
    if (!result) {
      return;
    }
    this.pickResult({ result, event, element });
  }

  /**
   * Whether pickResult() implements the result menu's commands for opening a
   * result in a new tab or window.
   *
   * @returns {boolean}
   */
  get handlesOpenInCommands() {
    return true;
  }

  /**
   * Called when a result is picked.
   *
   * @param {object} options
   * @param {UrlbarResult} options.result The result that was picked.
   * @param {Event} options.event The event that picked the result.
   * @param {HTMLElement} [options.element] The picked view element, if available.
   * @param {number} [options.browserId]
   *   The id of the browser to load into, for a load that resolves
   *   asynchronously and must target the tab selected when it was committed.
   *   Defaults to the parent resolving the selected browser at load time.
   */
  // eslint-disable-next-line complexity
  pickResult({ result, event, element = null, browserId = null }) {
    if (element?.classList.contains("urlbarView-button-menu")) {
      this.view.openResultMenu(result, element);
      return;
    }

    if (element?.dataset.command) {
      this.#pickMenuResult(result, event, element);
      return;
    }

    if (
      UrlbarPrefs.get("autoFill.adaptiveHistory.enabled") &&
      result.autofill &&
      result.payload?.url &&
      !this.isPrivate
    ) {
      this.parentController.clearAutofillBackspaceEntryForUrl(
        result.payload.url
      );
    }

    if (
      result.providerName == "UrlbarProviderGlobalActions" &&
      this.#providesSearchMode(result) &&
      !this.view.selectedElement?.dataset.immediateSearch
    ) {
      this.maybeConfirmSearchModeFromResult({
        result,
        checkValue: false,
      });
      return;
    }

    // When a one-off is selected, we restyle heuristic results to look like
    // search results. In the unlikely event that they are clicked, instead of
    // picking the results as usual, we confirm search mode, same as if the user
    // had selected them and pressed the enter key. Restyling results in this
    // manner was agreed on as a compromise between consistent UX and
    // engineering effort. See review discussion at bug 1667766.
    if (
      (this.searchMode?.isPreview &&
        result.providerName == "UrlbarProviderGlobalActions" &&
        !this.view.selectedElement?.dataset.immediateSearch) ||
      (result.heuristic &&
        this.searchMode?.isPreview &&
        this.view.oneOffSearchButtons?.selectedButton)
    ) {
      this.confirmSearchMode();
      this.search(this.value);
      return;
    }

    if (
      result.type == UrlbarShared.RESULT_TYPE.TIP &&
      result.payload.type == "dismissalAcknowledgment"
    ) {
      // The user clicked the "Got it" button inside the dismissal
      // acknowledgment tip. Dismiss the tip.
      this.controller.engagementEvent.record(event, {
        result,
        element,
        searchString: this._lastSearchString,
        selType: "dismiss",
        searchSource: this.getSearchSource(event),
        windowMode: this.windowMode,
      });
      this.view.onQueryResultRemoved(result.id);
      return;
    }

    let resultUrl = element?.dataset.url;
    let originalUntrimmedValue = this.untrimmedValue;
    let isCanonized = this.setValueFromResult({
      result,
      event,
      element,
      urlOverride: resultUrl,
    });

    let where;
    let openParams = {
      allowInheritPrincipal: false,
      globalHistoryOptions: {
        triggeringSource: this.#sapName,
        triggeringSearchEngine: result.payload?.engine,
        triggeringSponsoredURL: result.payload?.isSponsored
          ? result.payload.url
          : undefined,
      },
      private: this.isPrivate,
    };

    let userContextId = element?.dataset.usercontextid;
    let openIn = userContextId ? "container-tab" : element?.dataset.openIn;

    if (openIn) {
      switch (openIn) {
        case "tab": {
          where = "tab";
          break;
        }
        case "container-tab": {
          where = "tab";
          openParams.userContextId = parseInt(userContextId);
          openParams.eventDetail = {
            containerSource: "urlbar_result_context_menu",
          };
          break;
        }
        case "window": {
          where = "window";
          break;
        }
        case "private-window": {
          where = "window";
          openParams.private = true;
          break;
        }
      }

      openParams.forceForeground = false;
    } else {
      where = this.controller.whereToOpen(event);
      if (resultUrl && where == "current") {
        // Open help links in a new tab.
        where = "tab";
      }

      openParams.forceForeground = true;
    }

    let keepViewOpen = UrlbarContentUtils.willLoadInBackground(
      where,
      openParams
    );
    openParams.avoidBrowserFocus = keepViewOpen;

    if (!this.#providesSearchMode(result) && !keepViewOpen) {
      this.view.close({ elementPicked: true });
    }

    if (isCanonized) {
      this.controller.engagementEvent.record(event, {
        result,
        element,
        selType: "canonized",
        searchString: this._lastSearchString,
        searchSource: this.getSearchSource(event),
        windowMode: this.windowMode,
      });
      this.#loadURL({
        loadRequest: {
          urlLoad: { url: this._untrimmedValue, postData: null },
        },
        event,
        where,
        params: openParams,
        browserId,
      });
      return;
    }

    let loadRequest = resultUrl
      ? { urlLoad: { url: resultUrl, postData: null } }
      : UrlbarShared.getLoadRequestFromResult(result, { element });
    let searchData = null;

    switch (result.type) {
      case UrlbarShared.RESULT_TYPE.URL: {
        if (result.heuristic) {
          // Bug 1578856: both the provider and the docshell run heuristics to
          // decide how to handle a non-url string, either fixing it to a url, or
          // searching for it.
          // Some preferences can control the docshell behavior, for example
          // if dns_first_for_single_words is true, the docshell looks up the word
          // against the dns server, and either loads it as an url or searches for
          // it, depending on the lookup result. The provider instead will always
          // return a fixed url in this case, because URIFixup is synchronous and
          // can't do a synchronous dns lookup. A possible long term solution
          // would involve sharing the docshell logic with the provider, along
          // with the dns lookup.
          // For now, in this specific case, we'll override the result's url
          // with the input value, and let it pass through to #loadURL(), and
          // finally to the docshell.
          // This also means that in some cases the heuristic result will show a
          // Visit entry, but the docshell will instead execute a search. It's a
          // rare case anyway, most likely to happen for enterprises customizing
          // the urifixup prefs.
          if (
            UrlbarPrefs.get("browser.fixup.dns_first_for_single_words") &&
            UrlbarShared.looksLikeSingleWordHost(originalUntrimmedValue)
          ) {
            loadRequest.urlLoad.url = originalUntrimmedValue;
          }
          // Annotate if the untrimmed value contained a scheme, to later potentially
          // be upgraded by schemeless HTTPS-First.
          openParams.schemelessInput = this.#getSchemelessInput(
            originalUntrimmedValue
          );
        }
        break;
      }
      case UrlbarShared.RESULT_TYPE.KEYWORD: {
        // If this result comes from a bookmark keyword, let it inherit the
        // current document's principal, otherwise bookmarklets would break.
        openParams.allowInheritPrincipal = true;
        break;
      }
      case UrlbarShared.RESULT_TYPE.TAB_SWITCH: {
        // Behaviour is reversed with SecondaryActions, default behaviour is to navigate
        // and button is provided to switch to tab.
        if (
          this.hasAttribute("action-override") ||
          (UrlbarPrefs.get("secondaryActions.switchToTab") &&
            element?.dataset.action !== "tabswitch")
        ) {
          where = "current";
          break;
        }

        // Keep the searchMode for telemetry since handleRevert sets it to null.
        const searchMode = this.searchMode;
        this.handleRevert();

        // We cache the search string because switching tab may clear it.
        let searchString = this._lastSearchString;
        this.controller.engagementEvent.record(event, {
          result,
          element,
          searchString,
          searchMode,
          selType: this.view.telemetryTypeFromElement(result, element),
          searchSource: this.getSearchSource(event),
          windowMode: this.windowMode,
        });

        this.parentController.switchToTab({
          url: result.payload.url,
          searchString,
          userContextId: result.payload.userContext?.id,
          tabGroup: result.payload.tabGroup,
          heuristic: result.heuristic,
        });

        return;
      }
      case UrlbarShared.RESULT_TYPE.SEARCH: {
        if (result.payload.providesSearchMode) {
          this.controller.engagementEvent.record(event, {
            result,
            element,
            searchString: this._lastSearchString,
            selType: this.view.telemetryTypeFromElement(result, element),
            searchSource: this.getSearchSource(event),
            windowMode: this.windowMode,
          });
          this.maybeConfirmSearchModeFromResult({
            result,
            checkValue: false,
          });
          return;
        }

        // TODO (bug 2058937): Consider removing this check.
        if (
          this.#isAddressbar &&
          !this.searchMode &&
          result.heuristic &&
          // If we asked the DNS earlier, avoid the post-facto check.
          !UrlbarPrefs.get("browser.fixup.dns_first_for_single_words") &&
          // TODO (bug 1642623): for now there is no smart heuristic to skip the
          // DNS lookup, so any value above 0 will run it.
          UrlbarPrefs.get("dnsResolveSingleWordsAfterSearch") > 0 &&
          UrlbarShared.looksLikeSingleWordHost(originalUntrimmedValue)
        ) {
          // When fixing a single word to a search, the docShell would also
          // query the DNS and if resolved ask the user whether they would
          // rather visit that as a host. On a positive answer, it adds the host
          // to the list that we use to make decisions.
          // Because we are directly asking for a search here, bypassing the
          // docShell, we need to do the same ourselves.
          // See also keyword-uri-fixup.
          this.parentController.checkKeywordURIFixup(
            originalUntrimmedValue.trim(),
            browserId
          );
        }

        if (result.payload.inPrivateWindow) {
          where = "window";
          openParams.private = true;
        }

        const actionDetails = {
          isSuggestion: !!result.payload.suggestion,
          isFormHistory: result.source == UrlbarShared.RESULT_SOURCE.HISTORY,
          alias: result.payload.keyword,
        };
        let engine = this.controller.engineStore.getEngineByName(
          result.payload.engine
        );

        searchData = this.#getSearchData({
          engine,
          event,
          query: result.payload.suggestion || result.payload.query,
          searchActionDetails: actionDetails,
          opensInPrivateWindow: result.payload.inPrivateWindow,
        });

        if (
          this.#isAddressbar &&
          !actionDetails.isFormHistory &&
          !result.payload.inPrivateWindow &&
          !this.isPrivate &&
          engine.isAppProvided &&
          engine == this.controller.engineStore.default
        ) {
          let merinoBackend = /** @type {?SuggestBackendMerino} */ (
            lazy.QuickSuggest.getFeature("SuggestBackendMerino")
          );
          if (merinoBackend?.isEnabled) {
            let selection = (
              result.payload.suggestion || result.payload.query
            )?.trim();
            // Don't record an empty selection, or one that looks like an origin
            // (e.g. "facebook.com"), which `allowRemoteResults` would otherwise
            // allow.
            if (
              selection &&
              lazy.UrlUtils.looksLikeOrigin(selection) ==
                lazy.UrlUtils.LOOKS_LIKE_ORIGIN.NONE
            ) {
              // Build a context around the selection so all of
              // `allowRemoteResults`'s checks apply to it rather than to the
              // originally typed string.
              let context = this.#makeQueryContext({ searchString: selection });
              context.tokens = lazy.UrlbarTokenizer.tokenize(context);
              merinoBackend
                .query(selection, { queryContext: context })
                .catch(console.error);
            }
          }
        }
        break;
      }
      case UrlbarShared.RESULT_TYPE.TIP: {
        if (loadRequest) {
          break;
        }
        this.handleRevert();
        this.controller.engagementEvent.record(event, {
          result,
          element,
          selType: "tip",
          searchString: this._lastSearchString,
          searchSource: this.getSearchSource(event),
          windowMode: this.windowMode,
        });
        return;
      }
      case UrlbarShared.RESULT_TYPE.DYNAMIC: {
        if (!loadRequest) {
          // If we're not loading anything, the engagement is done. First revert
          // and then record the engagement since providers expect the urlbar to
          // be reverted when they're notified of the engagement, but before
          // reverting, copy the search mode since it's nulled on revert.
          const { searchMode } = this;
          if (!this.isSearchbarSAP) {
            // A search bar is not reverted so providers enabled in a search bar
            // should be able to handle both cases.
            this.handleRevert();
          }
          this.controller.engagementEvent.record(event, {
            result,
            element,
            searchMode,
            searchString: this._lastSearchString,
            selType: this.view.telemetryTypeFromElement(result, element),
            searchSource: this.getSearchSource(event),
            windowMode: this.windowMode,
          });
          if (result.payload.providesSearchMode) {
            this.maybeConfirmSearchModeFromResult({
              result,
              checkValue: false,
            });
          }
          return;
        }
        break;
      }
      case UrlbarShared.RESULT_TYPE.OMNIBOX: {
        this.controller.engagementEvent.record(event, {
          result,
          element,
          selType: "extension",
          searchString: this._lastSearchString,
          searchSource: this.getSearchSource(event),
          windowMode: this.windowMode,
        });

        // The urlbar needs to revert to the loaded url when a command is
        // handled by the extension.
        this.handleRevert();
        // We don't directly handle a load when an Omnibox API result is picked,
        // instead we forward the request to the WebExtension itself, because
        // the value may not even be a url.
        // We pass the keyword and content, that actually is the retrieved value
        // prefixed by the keyword. ExtensionSearchHandler uses this keyword
        // redundancy as a sanity check.
        lazy.ExtensionSearchHandler.handleInputEntered(
          result.payload.keyword,
          result.payload.content,
          where
        );
        return;
      }
      case UrlbarShared.RESULT_TYPE.RESTRICT: {
        this.handleRevert();
        this.controller.engagementEvent.record(event, {
          result,
          element,
          searchString: this._lastSearchString,
          selType: this.view.telemetryTypeFromElement(result, element),
          searchSource: this.getSearchSource(event),
          windowMode: this.windowMode,
        });
        this.maybeConfirmSearchModeFromResult({
          result,
          checkValue: false,
        });

        return;
      }
      case UrlbarShared.RESULT_TYPE.AI_CHAT: {
        // AI Chat results handle their own navigation.
        this.controller.engagementEvent.record(event, {
          result,
          element,
          searchString: this._lastSearchString,
          selType: this.view.telemetryTypeFromElement(result, element),
          searchSource: this.getSearchSource(event),
          windowMode: this.windowMode,
        });
        return;
      }
    }

    if (!loadRequest) {
      throw new Error(`No load request for result ${JSON.stringify(result)}`);
    }

    // Record input history but only in non-private windows and for url loads.
    if (!this.isPrivate && loadRequest.urlLoad) {
      let url = loadRequest.urlLoad.url;
      let input;
      if (!result.heuristic) {
        input = this._lastSearchString;
      } else if (
        result.autofill?.type == "adaptive_url" ||
        result.autofill?.type == "adaptive_origin"
      ) {
        input = result.autofill.adaptiveHistoryInput;
      } else if (
        UrlbarPrefs.get("autoFill.adaptiveHistory.enabled") &&
        result.autofill?.type == "origin" &&
        // Bug: 2026227: Investigate if we want to use a higher threshold
        this._lastSearchString?.length > 0
      ) {
        // The origin root URL (e.g. http://example.com/) may not be in
        // moz_places yet. It's derived from a deep-link visit. Defer the
        // write until the navigation records the visit.
        this.parentController.addToInputHistory(url, this._lastSearchString, {
          whenReady: true,
        });
      }

      // `input` may be an empty string, so do a strict comparison here.
      if (input !== undefined) {
        this.parentController.addToInputHistory(url, input);
      }

      // Re-integration: If the user picks a non-autofill result, or a "url"
      // autofill from manually typing the URL for a blocked origin, clear the
      // block.
      if (
        UrlbarPrefs.get("autoFill.adaptiveHistory.enabled") &&
        (!result.autofill || result.autofill.type == "url") &&
        result.type == UrlbarShared.RESULT_TYPE.URL
      ) {
        this.parentController.handleAutofillReintegration(url);
      }
    }

    this.#recordEngagementAndTrackBounce(event, {
      result,
      element,
      searchString: this._lastSearchString,
      selType: this.view.telemetryTypeFromElement(result, element),
      searchSource: this.getSearchSource(event),
      windowMode: this.windowMode,
    });

    this.#loadURL({
      loadRequest,
      event,
      where,
      params: openParams,
      resultDetails: {
        source: result.source,
        type: result.type,
      },
      keepViewOpen,
      browserId,
      searchData,
    });
  }

  /**
   * Called by the view when moving through results with the keyboard, and when
   * picking a result.  This sets the input value to the value of the result and
   * invalidates the pageproxystate.  It also sets the result that is associated
   * with the current input value.  If you need to set this result but don't
   * want to also set the input value, then use setResultForCurrentValue.
   *
   * @param {object} options
   *   Options.
   * @param {UrlbarResult} [options.result]
   *   The result that was selected or picked, null if no result was selected.
   * @param {Event} [options.event]
   *   The event that picked the result.
   * @param {string} [options.urlOverride]
   *   Normally the URL is taken from `result.payload.url`, but if `urlOverride`
   *   is specified, it's used instead. See `#getValueFromResult()`.
   * @param {HTMLElement} [options.element]
   *   The element that was selected or picked, if available. For results that
   *   have multiple selectable children, the value may be taken from a child
   *   element rather than the result. See `#getValueFromResult()`.
   * @returns {boolean}
   *   Whether the value has been canonized
   */
  setValueFromResult({
    result = null,
    event = null,
    urlOverride = null,
    element = null,
  } = {}) {
    // Usually this is set by a previous input event, but in certain cases, like
    // when opening Top Sites on a loaded page, it wouldn't happen. To avoid
    // confusing the user, we always enforce it when a result changes our value.
    this.setPageProxyState("invalid", true);

    // A previous result may have previewed search mode. If we don't expect that
    // we might stay in a search mode of some kind, exit it now.
    if (
      this.searchMode?.isPreview &&
      !this.#providesSearchMode(result) &&
      !this.view.oneOffSearchButtons?.selectedButton
    ) {
      this.searchMode = null;
    }

    if (!result) {
      // This happens when there's no selection, for example when moving to the
      // one-offs search settings button, or to the input field when Top Sites
      // are shown; then we must reset the input value.
      // Note that for Top Sites the last search string would be empty, thus we
      // must restore the last text value.
      // Note that unselected autofill results will still arrive in this
      // function with a non-null `result`. They are handled below.
      this.value = this._lastSearchString || this._valueOnLastSearch;
      this.setResultForCurrentValue(result);
      return false;
    }

    // We won't allow trimming when calling setValue, since it makes too easy
    // for the user to wrongly transform `https` into `http`, for example by
    // picking a https://site/path_1 result and editing the path to path_2,
    // then we'd end up visiting http://site/path_2.
    // Trimming `http` would be ok, but there's other cases where it's unsafe,
    // like transforming a url into a search.
    // This choice also makes it easier to copy the full url of a result.

    // We are supporting canonization of any result, in particular this allows
    // for single word search suggestions to be converted to a .com URL.
    // For autofilled results, the value to canonize is the user typed string,
    // not the autofilled value.
    let canonizedUrl = this._maybeCanonizeURL(
      event,
      result.autofill ? this._lastSearchString : this.value
    );
    if (canonizedUrl) {
      this.setValue(canonizedUrl);

      this.setResultForCurrentValue(result);
      return true;
    }

    if (result.autofill) {
      this._autofillValue(result.autofill);
    }

    if (this.#providesSearchMode(result)) {
      let enteredSearchMode;
      // Only preview search mode if the result is selected.
      if (this.view.resultIsSelected(result)) {
        // For ScotchBonnet, As Tab and Arrow Down/Up, Page Down/Up key are used
        // for selection of the urlbar results, keep the search mode as preview
        // mode if there are multiple results.
        // If ScotchBonnet is disabled, not starting a query means we will only
        // preview search mode.
        enteredSearchMode = this.maybeConfirmSearchModeFromResult({
          result,
          checkValue: false,
          startQuery:
            UrlbarPrefs.get("scotchBonnet.enableOverride") &&
            this.view.visibleResults.length == 1,
        });
      }
      if (!enteredSearchMode) {
        this.setValue(this.#getValueFromResult(result), {
          actionType: this.#getActionTypeFromResult(result),
        });
        this.searchMode = null;
      }
      this.setResultForCurrentValue(result);
      return false;
    }

    if (!result.autofill) {
      let value = this.#getValueFromResult(result, { urlOverride, element });
      this.setValue(value, {
        actionType: this.#getActionTypeFromResult(result),
      });
    }

    this.setResultForCurrentValue(result);

    // Update placeholder selection and value to the current selected result to
    // prevent the on_selectionchange event to detect a "accent-character"
    // insertion.
    if (!result.autofill && this._autofillPlaceholder) {
      this._autofillPlaceholder.value = this.value;
      this._autofillPlaceholder.selectionStart = this.value.length;
      this._autofillPlaceholder.selectionEnd = this.value.length;
    }

    return false;
  }

  /**
   * The input keeps track of the result associated with the current input
   * value.  This result can be set by calling either setValueFromResult or this
   * method.  Use this method when you need to set the result without also
   * setting the input value.  This can be the case when either the selection is
   * cleared and no other result becomes selected, or when the result is the
   * heuristic and we don't want to modify the value the user is typing.
   *
   * @param {UrlbarResult} result
   *   The result to associate with the current input value.
   */
  setResultForCurrentValue(result) {
    this._resultForCurrentValue = result;
  }

  /**
   * Called by the controller when the first result of a new search is received.
   * If it's an autofill result, then it may need to be autofilled, subject to a
   * few restrictions.
   *
   * @param {UrlbarResult} result
   *   The first result.
   */
  _autofillFirstResult(result) {
    if (!result.autofill) {
      return;
    }

    let isPlaceholderSelected =
      this._autofillPlaceholder &&
      this.selectionEnd == this._autofillPlaceholder.value.length &&
      this.selectionStart == this._lastSearchString.length &&
      this._autofillPlaceholder.value
        .toLocaleLowerCase()
        .startsWith(this._lastSearchString.toLocaleLowerCase());

    // Don't autofill if there's already a selection (with one caveat described
    // next) or the cursor isn't at the end of the input.  But if there is a
    // selection and it's the autofill placeholder value, then do autofill.
    if (
      !isPlaceholderSelected &&
      !this._autofillIgnoresSelection &&
      (this.selectionStart != this.selectionEnd ||
        this.selectionEnd != this._lastSearchString.length)
    ) {
      return;
    }

    this.setValueFromResult({ result });
  }
  /**
   * Clears displayed autofill values and unsets the autofill placeholder.
   */
  #clearAutofill() {
    if (!this._autofillPlaceholder) {
      return;
    }
    let currentSelectionStart = this.selectionStart;
    let currentSelectionEnd = this.selectionEnd;

    // Overriding this value clears the selection.
    this.inputField.value = this.value.substring(
      0,
      this._autofillPlaceholder.selectionStart
    );
    this._autofillPlaceholder = null;
    // Restore selection
    this.setSelectionRange(currentSelectionStart, currentSelectionEnd);
  }

  /**
   * Invoked by the controller when the first result changed.
   *
   * @param {UrlbarQueryContext} queryContext
   *   The context of the query the result belongs to.
   */
  onFirstResult(queryContext) {
    let firstResult = queryContext.results[0];

    // If the heuristic result has a keyword but isn't a keyword offer, we may
    // need to enter search mode.
    if (
      firstResult.heuristic &&
      firstResult.payload.keyword &&
      !this.#providesSearchMode(firstResult) &&
      this.maybeConfirmSearchModeFromResult({
        result: firstResult,
        entry: "typed",
        checkValue: false,
      })
    ) {
      // Search mode restarts the query, so these results are obsolete.
      this.controller.discardResults(queryContext);
      return;
    }

    // To prevent selection flickering, we apply autofill on input through a
    // placeholder, without waiting for results. But, if the first result is
    // not an autofill one, the autofill prediction was wrong and we should
    // restore the original user typed string.
    if (firstResult.autofill) {
      this._autofillFirstResult(firstResult);
    } else if (
      this._autofillPlaceholder &&
      // Avoid clobbering added spaces (for token aliases, for example).
      !this.value.endsWith(" ")
    ) {
      this._autofillPlaceholder = null;
      this.setValue(this.userTypedValue);
    }
  }

  /**
   * Starts a query based on the current input value.
   *
   * @param {object} [options]
   *   Object options
   * @param {boolean} [options.allowAutofill]
   *   Whether or not to allow providers to include autofill results.
   * @param {boolean} [options.autofillIgnoresSelection]
   *   Normally we autofill only if the cursor is at the end of the string,
   *   if this is set we'll autofill regardless of selection.
   * @param {string} [options.searchString]
   *   The search string.  If not given, the current input value is used.
   *   Otherwise, the current input value must start with this value.
   * @param {boolean} [options.resetSearchState]
   *   If this is the first search of a user interaction with the input, set
   *   this to true (the default) so that search-related state from the previous
   *   interaction doesn't interfere with the new interaction.  Otherwise set it
   *   to false so that state is maintained during a single interaction.  The
   *   intended use for this parameter is that it should be set to false when
   *   this method is called due to input events.
   * @param {event} [options.event]
   *   The user-generated event that triggered the query, if any.  If given, we
   *   will record engagement event telemetry for the query.
   * @param {string} [options.interactionType]
   *   An explicit engagement interaction type for the query, used in preference
   *   to one derived from the event (e.g. "returned" when reopening a search).
   */
  startQuery({
    allowAutofill,
    autofillIgnoresSelection = false,
    searchString,
    resetSearchState = true,
    event,
    interactionType,
  } = {}) {
    if (!searchString) {
      searchString =
        this.getAttribute("pageproxystate") == "valid" ? "" : this.value;
    } else if (!this.value.startsWith(searchString)) {
      throw new Error("The current value doesn't start with the search string");
    }

    let queryContext = this.#makeQueryContext({
      allowAutofill,
      event,
      searchString,
    });

    if (event) {
      this.controller.engagementEvent.start(
        event,
        queryContext,
        searchString,
        interactionType
      );
    }

    if (this._suppressStartQuery) {
      return;
    }

    this._autofillIgnoresSelection = autofillIgnoresSelection;
    if (resetSearchState) {
      this._resetSearchState();
    }

    if (this.searchMode) {
      this.confirmSearchMode();
    }

    if (this.inOverflowPanel) {
      return;
    }

    this._lastSearchString = searchString;
    this._valueOnLastSearch = this.value;

    // TODO (Bug 1522902): This promise is necessary for tests, because some
    // tests are not listening for completion when starting a query through
    // other methods than startQuery (input events for example).
    this.lastQueryContextPromise = this.controller.startQuery(queryContext);
  }

  /**
   * Sets the input's value, starts a search, and opens the view.
   *
   * @param {string} value
   *   The input's value will be set to this value, and the search will
   *   use it as its query.
   * @param {object} [options]
   *   Object options
   * @param {PartialSearchEngine|SearchEngine} [options.searchEngine]
   *   Search engine to use when the search is using a known alias.
   * @param {string} [options.searchModeEntry]
   *   If provided, we will record this parameter as the search mode entry point
   *   in Telemetry. Consumers should provide this if they expect their call
   *   to enter search mode.
   * @param {boolean} [options.focus]
   *   If true, the urlbar will be focused.  If false, the focus will remain
   *   unchanged.
   * @param {boolean} [options.startQuery]
   *   If true, start query to show urlbar result by fireing input event. If
   *   false, not fire the event.
   */
  search(value, options = {}) {
    let { searchEngine, searchModeEntry, startQuery = true } = options;
    if (options.focus ?? true) {
      this.focus();
    }
    let trimmedValue = value.trim();
    let end = trimmedValue.search(UrlbarShared.REGEXP_SPACES);
    let firstToken = end == -1 ? trimmedValue : trimmedValue.substring(0, end);

    if (
      firstToken == UrlbarShared.RESTRICT_TOKENS.SEARCH &&
      !this.controller.engineStore.initialized &&
      !this.controller.engineStore.failed
    ) {
      // The search restrict token enters search mode with the default engine,
      // which the store only knows once it's populated, and no query has run
      // at this point to wait for it. The retry leaves the focus alone, having
      // focused above. A failed search service leaves no engine to restrict
      // to, and sets `failed`, so the retry doesn't come back here.
      this.controller.engineStore
        .init()
        .catch(() => {})
        .then(() => this.search(value, { ...options, focus: false }));
      return;
    }

    // Enter search mode if the string starts with a restriction token.
    let searchMode = this.searchModeForToken(firstToken);
    let firstTokenIsRestriction = !!searchMode;
    if (!searchMode && searchEngine) {
      searchMode = { engineName: searchEngine.name };
      firstTokenIsRestriction = searchEngine.aliases.includes(firstToken);
    }

    if (searchMode) {
      searchMode.entry = searchModeEntry;
      this.searchMode = searchMode;
      if (firstTokenIsRestriction) {
        // Remove the restriction token/alias from the string to be searched for
        // in search mode.
        value = value.replace(firstToken, "");
      }
      if (UrlbarShared.REGEXP_SPACES.test(value[0])) {
        // If there was a trailing space after the restriction token/alias,
        // remove it.
        value = value.slice(1);
      }
    } else if (
      /** @type {string[]} */ (
        Object.values(UrlbarShared.RESTRICT_TOKENS)
      ).includes(firstToken)
    ) {
      this.searchMode = null;
      // If the entire value is a restricted token, append a space.
      if (
        /** @type {string[]} */ (
          Object.values(UrlbarShared.RESTRICT_TOKENS)
        ).includes(value)
      ) {
        value += " ";
      }
    }
    this.inputField.value = value;
    // Avoid selecting the text if this method is called twice in a row.
    this.selectionStart = -1;

    if (startQuery) {
      // Note: proper IME Composition handling depends on the fact this generates
      // an input event, rather than directly invoking the controller; everything
      // goes through _on_input, that will properly skip the search until the
      // composition is committed. _on_input also skips the search when it's the
      // same as the previous search, but we want to allow consecutive searches
      // with the same string. So clear _lastSearchString first.
      this._lastSearchString = "";
      let event = new UIEvent("input", {
        bubbles: true,
        cancelable: false,
        view: window,
        detail: 0,
      });
      this.inputField.dispatchEvent(event);
    }
  }

  /**
   * Returns a search mode object if a token should enter search mode when
   * typed. This does not handle engine aliases.
   *
   * @param {string} token
   *   A restriction token to convert to search mode.
   * @returns {?SearchModeInput}
   *   Null if search mode should not be entered.
   */
  searchModeForToken(token) {
    if (token == UrlbarShared.RESTRICT_TOKENS.SEARCH) {
      return {
        engineName: this.controller.engineStore.default?.name,
      };
    }

    let mode =
      this.#isAddressbar &&
      UrlbarShared.LOCAL_SEARCH_MODES.find(m => m.restrict == token);
    if (mode) {
      // Return a copy so callers don't modify the object in LOCAL_SEARCH_MODES.
      return { ...mode };
    }

    return null;
  }

  /**
   * If value is non-empty: open the search engine result page (SERP) for value.
   * If value is empty: open the search engine home page (searchForm).
   *
   * @param {string} value
   *   The search term or empty string to open homepage.
   * @param {object} options
   * @param {PartialSearchEngine|SearchEngine} options.searchEngine
   * @param {Event} options.event
   * @param {string} options.where
   * @param {boolean} [options.inBackground]
   */
  openSearchEnginePage(
    value,
    { searchEngine, event, where, inBackground = false }
  ) {
    if (!searchEngine || !event || !where) {
      console.warn("Missing parameters");
      return;
    }

    let trimmedValue = value.trim();
    this._lastSearchString = trimmedValue;
    if (trimmedValue) {
      let searchData = this.#getSearchData({
        engine: searchEngine,
        query: trimmedValue,
        event,
      });

      if (where == "current") {
        // Enter search mode so:
        // - in the urlbar, persisted search terms work
        // - in the searchbar, the engine stays selected
        //
        // Note that this will also record telemetry and
        // search mode will be exited on the urlbar if the
        // engine does not support persisted search terms
        this.setSearchMode(
          {
            engineName: searchEngine.name,
            entry: "searchbutton",
            source: UrlbarShared.RESULT_SOURCE.SEARCH,
            isPreview: false,
          },
          this.browserWindow?.gBrowser?.selectedBrowser
        );
      }
      this.parentController.openSERP(
        searchEngine.id,
        trimmedValue,
        where,
        inBackground,
        this.#selectedBrowserId,
        searchData
      );
    } else {
      // Telemetry is handled by the function.
      this.parentController.openSearchForm(
        searchEngine.id,
        where,
        inBackground,
        this.#selectedBrowserId
      );
    }
  }

  /**
   * Focus without the focus styles.
   * This is used by Activity Stream and about:privatebrowsing for search hand-off.
   */
  setHiddenFocus() {
    this._hideFocus = true;
    if (this.focused) {
      this.removeAttribute("focused");
    } else {
      this.focus();
    }
  }

  /**
   * Restore focus styles.
   * This is used by Activity Stream and about:privatebrowsing for search hand-off.
   *
   * @param {boolean} forceSuppressFocusBorder
   *   Set true to suppress-focus-border attribute if this flag is true.
   */
  removeHiddenFocus(forceSuppressFocusBorder = false) {
    this._hideFocus = false;
    if (this.focused) {
      this.toggleAttribute("focused", true);

      if (forceSuppressFocusBorder) {
        this.toggleAttribute("suppress-focus-border", true);
      }
    }
  }

  /**
   * Addressbar: Gets the search mode for a specific browser instance.
   * Searchbar: Gets the window-global search mode.
   *
   * @param {MozBrowser} browser
   *   The search mode for this browser will be returned.
   *   Pass the selected browser for the searchbar.
   * @param {boolean} [confirmedOnly]
   *   Normally, if the browser has both preview and confirmed modes, preview
   *   mode will be returned since it takes precedence.  If this argument is
   *   true, then only confirmed search mode will be returned, or null if
   *   search mode hasn't been confirmed.
   * @returns {?SearchMode}
   *   Null if the browser/window is not in search mode.
   */
  getSearchMode(browser, confirmedOnly = false) {
    let modes = this.#getSearchModesObject(browser);

    // Return copies so that callers don't modify the stored values.
    if (!confirmedOnly && modes.preview) {
      return { ...modes.preview };
    }
    if (modes.confirmed) {
      return { ...modes.confirmed };
    }
    return null;
  }

  /**
   * Addressbar: Sets the search mode for a specific browser instance.
   * Searchbar: Sets the window-global search mode.
   * If the given browser is selected, then this will also enter search mode.
   *
   * @param {?SearchModeInput} searchMode
   *   The search mode to enter, or null to exit search mode.
   * @param {MozBrowser} browser
   *   The browser for which to set search mode.
   *   Pass the selected browser for the searchbar.
   */
  async setSearchMode(searchMode, browser) {
    let currentSearchMode = this.getSearchMode(browser);
    let areSearchModesSame =
      (!currentSearchMode && !searchMode) ||
      UrlbarShared.deepEqual(currentSearchMode, searchMode);

    // Exit search mode if the passed-in engine is invalid or hidden.
    let engine;
    if (searchMode?.engineName) {
      if (!this.controller.engineStore.initialized) {
        await this.controller.engineStore.init();
      }
      engine = this.controller.engineStore.getEngineByName(
        searchMode.engineName
      );
      if (!engine) {
        searchMode = null;
      }
    }

    let {
      engineName,
      source,
      entry,
      restrictType,
      isPreview = true,
    } = searchMode || {};

    if (!UrlbarShared.SEARCH_MODE_ENTRY.has(entry)) {
      // If we see this value showing up in telemetry, we should review
      // search mode's entry points.
      entry = "other";
    }

    /** @type {?SearchMode} */
    let newSearchMode = null;

    if (engineName) {
      newSearchMode = {
        engineName,
        isGeneralPurposeEngine: engine.isGeneralPurposeEngine,
        entry,
        isPreview,
      };
      if (source) {
        newSearchMode.source = source;
      } else if (newSearchMode.isGeneralPurposeEngine) {
        // History results for general-purpose search engines are often not
        // useful, so we hide them in search mode. See bug 1658646 for
        // discussion.
        newSearchMode.source = UrlbarShared.RESULT_SOURCE.SEARCH;
      }
    } else if (source) {
      let sourceName = UrlbarShared.getResultSourceName(source);
      if (sourceName) {
        newSearchMode = { source, entry, isPreview };
      } else {
        console.error(`Unrecognized source: ${source}`);
      }
    }

    let modes = this.#getSearchModesObject(browser);

    if (newSearchMode) {
      if (!newSearchMode.isPreview) {
        modes.confirmed = newSearchMode;
        delete modes.preview;
      } else {
        modes.preview = newSearchMode;
      }
      if (restrictType) {
        newSearchMode.restrictType = restrictType;
      }
    } else {
      delete modes.preview;
      delete modes.confirmed;
    }

    // The address bar keeps a search mode per browser, so it only enters search
    // mode for the selected one. Every other input keeps a single search mode.
    if (
      !this.#isAddressbar ||
      browser == this.browserWindow.gBrowser.selectedBrowser
    ) {
      this._updateSearchModeUI(newSearchMode);
      if (newSearchMode) {
        // Set userTypedValue to the query string so that it's properly restored
        // when switching back to the current tab and across sessions.
        this.userTypedValue = this.untrimmedValue;
        this.valueIsTyped = true;
        if (!newSearchMode.isPreview && !areSearchModesSame) {
          this.parentController.recordSearchMode(newSearchMode);
        }
      }
    }
    lazy?.UrlbarSearchTermsPersistence.onSearchModeChanged(this.browserWindow);
    this.dispatchEvent(new Event("searchmodechanged"));
  }

  /**
   * @typedef {object} SearchModesObject
   *
   * @property {SearchMode} [preview] preview search mode
   * @property {SearchMode} [confirmed] confirmed search mode
   */

  /**
   * The (lazily initialized) search mode object for the searchbar.
   * This is needed because the searchbar has one search mode per window that
   * shouldn't change when switching tabs. For the address bar, the search mode
   * is stored per browser in #browserStates and this is always undefined.
   *
   * @type {SearchModesObject|undefined}
   */
  #searchbarSearchModes;

  /**
   * Addressbar: Gets the search modes object for a specific browser instance.
   * Searchbar: Gets the window-global search modes object.
   *
   * @param {MozBrowser} browser
   *   The browser to get the search modes object for.
   *   Pass the selected browser for the searchbar.
   * @returns {SearchModesObject}
   */
  #getSearchModesObject(browser) {
    if (!this.#isAddressbar) {
      // The passed browser doesn't matter here, but it does in setSearchMode.
      this.#searchbarSearchModes ??= {};
      return this.#searchbarSearchModes;
    }

    let state = this.getBrowserState(browser);
    state.searchModes ??= {};
    return state.searchModes;
  }

  /**
   * Restores the current browser search mode from a previously stored state.
   */
  restoreSearchModeState() {
    this.searchMode = this.#getSearchModesObject(
      this.browserWindow?.gBrowser?.selectedBrowser
    ).confirmed;
  }

  /**
   * Enters search mode with the default engine.
   */
  async searchModeShortcut() {
    if (!this.controller.engineStore.initialized) {
      try {
        await this.controller.engineStore.init();
      } catch {
        // Search service failed, so there's no engine to search with.
        return;
      }
    }

    // We restrict to search results when entering search mode from this
    // shortcut to honor historical behaviour.
    this.searchMode = {
      source: UrlbarShared.RESULT_SOURCE.SEARCH,
      engineName: this.controller.engineStore.default.name,
      entry: "shortcut",
    };
    // The searchMode setter clears the input if pageproxystate is valid, so
    // we know at this point this.value will either be blank or the user's
    // typed string.
    this.search(this.value);
    this.select();
  }

  /**
   * Confirms the current search mode.
   */
  confirmSearchMode() {
    let searchMode = this.searchMode;
    if (searchMode?.isPreview) {
      searchMode.isPreview = false;
      this.searchMode = searchMode;

      // Unselect the one-off search button to ensure UI consistency.
      if (this.view.oneOffSearchButtons) {
        this.view.oneOffSearchButtons.selectedButton = null;
      }
    }
  }

  // Getters and Setters below.

  get editor() {
    return this.inputField.editor;
  }

  get focused() {
    return document.activeElement == this.inputField;
  }

  get goButton() {
    return this.querySelector(".urlbar-go-button");
  }

  get value() {
    return this.inputField.value;
  }

  set value(val) {
    this.setValue(val, { allowTrim: true });
  }

  get untrimmedValue() {
    return this._untrimmedValue;
  }

  get userTypedValue() {
    return this.#isAddressbar
      ? this.browserWindow.gBrowser.userTypedValue
      : this._userTypedValue;
  }

  set userTypedValue(val) {
    if (this.#isAddressbar) {
      this.browserWindow.gBrowser.userTypedValue = val;
    } else {
      this._userTypedValue = val;
    }
  }

  get lastSearchString() {
    return this._lastSearchString;
  }

  /**
   * Resolves once the search mode last assigned through the `searchMode`
   * setter has been applied. Applying it resolves the engine, which may have
   * to wait for the engine store.
   *
   * @type {Promise<void>}
   */
  #searchModeApplied = Promise.resolve();

  /** @returns {?SearchMode} */
  get searchMode() {
    if (this.#isAddressbar && !this.browserWindow.gBrowser) {
      // Only the address bar keys search mode by browser, and it has no
      // browser before DOMContentLoaded; #browserStates is a WeakMap, so
      // there'd be nothing to look up.
      return null;
    }
    return this.getSearchMode(this.browserWindow?.gBrowser?.selectedBrowser);
  }

  set searchMode(/** @type {?SearchModeInput} */ searchMode) {
    this.#searchModeApplied = this.setSearchMode(
      searchMode,
      this.browserWindow?.gBrowser?.selectedBrowser
    );

    this.controller.engineStore
      .getEngineByName(this.searchMode?.engineName)
      ?.markAsUsed();
  }

  /**
   * @param {MozBrowser} browser
   */
  getBrowserState(browser) {
    let state = this.#browserStates.get(browser);
    if (!state) {
      state = {};
      this.#browserStates.set(browser, state);
    }
    return state;
  }

  #openPopover() {
    if (this.panel.matches(":popover-open")) {
      return;
    }

    this.panel.showPopover();
  }

  #closePopover() {
    if (!this.panel.matches(":popover-open")) {
      return;
    }

    this.panel.hidePopover();
  }

  /**
   * Keeps the view's popover in the top layer, and the `popover-open`
   * attribute set, for as long as the view is open. `popover-open` says the
   * sheet the background paints is bigger than the input.
   */
  updatePopover() {
    let popoverOpen = this.#popoverAllowed && this.view.isOpen;
    if (popoverOpen) {
      this.#openPopover();
    } else {
      this.#closePopover();
    }
    this.toggleAttribute("popover-open", popoverOpen);
  }

  /**
   * Updates the user interface to indicate whether the URI in the address bar
   * is different than the loaded page, because it's being edited or because a
   * search result is currently selected and is displayed in the location bar.
   *
   * @param {string} state
   *        The string "valid" indicates that the security indicators and other
   *        related user interface elments should be shown because the URI in
   *        the location bar matches the loaded page. The string "invalid"
   *        indicates that the URI in the location bar is different than the
   *        loaded page.
   * @param {boolean} [updatePopupNotifications]
   *        Indicates whether we should update the PopupNotifications
   *        visibility due to this change, otherwise avoid doing so as it is
   *        being handled somewhere else.
   * @param {boolean} [forceUnifiedSearchButtonAvailable]
   *        If this parameter is true, force to make Unified Search Button available.
   *        Otherwise, the availability will be depedent on the proxy state.
   *        Default value is false.
   */
  setPageProxyState(
    state,
    updatePopupNotifications,
    forceUnifiedSearchButtonAvailable = false
  ) {
    let prevState = this.getAttribute("pageproxystate");

    this.setAttribute("pageproxystate", state);
    this._inputContainer.setAttribute("pageproxystate", state);
    this._identityBox?.setAttribute("pageproxystate", state);
    this.setUnifiedSearchButtonAvailability(
      forceUnifiedSearchButtonAvailable || state == "invalid"
    );

    if (state == "valid") {
      this._lastValidURLStr = this.value;
    }

    if (
      updatePopupNotifications &&
      prevState != state &&
      this.browserWindow?.UpdatePopupNotificationsVisibility
    ) {
      this.browserWindow.UpdatePopupNotificationsVisibility();
    }
  }

  /**
   * When switching tabs quickly, TabSelect sometimes happens before
   * _adjustFocusAfterTabSwitch and due to the focus still being on the old
   * tab, we end up flickering the results pane briefly.
   */
  afterTabSwitchFocusChange() {
    this._gotFocusChange = true;
    this._afterTabSelectAndFocusChange();
  }

  /**
   * Confirms search mode and starts a new search if appropriate for the given
   * result.  See also _searchModeForResult.
   *
   * @param {object} options
   *   Options object.
   * @param {string} [options.entry]
   *   If provided, this will be recorded as the entry point into search mode.
   *   See setSearchMode documentation for details.
   * @param {UrlbarResult} [options.result]
   *   The result to confirm. Defaults to the currently selected result.
   * @param {boolean} [options.checkValue]
   *   If true, the trimmed input value must equal the result's keyword in order
   *   to enter search mode.
   * @param {boolean} [options.startQuery]
   *   If true, start a query after entering search mode. Defaults to true.
   * @returns {boolean}
   *   True if we entered search mode and false if not.
   */
  maybeConfirmSearchModeFromResult({
    entry,
    result = this._resultForCurrentValue,
    checkValue = true,
    startQuery = true,
  }) {
    if (
      !result ||
      (checkValue &&
        this.value.trim() != result.payload.keyword?.trim() &&
        this.value.trim() != result.payload.autofillKeyword?.trim())
    ) {
      return false;
    }

    let searchMode = this._searchModeForResult(result, entry);
    if (!searchMode) {
      return false;
    }

    if (startQuery) {
      // Closing the view discards a previewed search mode, so the mode has to
      // be confirmed before the query below gets a chance to run.
      searchMode.isPreview = false;
    }
    this.searchMode = searchMode;

    let value = result.payload.query?.trimStart() || "";
    this.setValue(value);

    if (startQuery) {
      // Search mode stores the value to restore on tab switch. For a confirmed
      // mode that's the query string, not the keyword that entered it.
      this.userTypedValue = this.untrimmedValue;

      // The query has to run in the search mode we just entered.
      this.#searchModeApplied.then(() =>
        this.startQuery({ allowAutofill: false })
      );
    }

    return true;
  }

  /**
   * @param {"removed"|"changed"|"default"} modifiedType
   * @param {PartialSearchEngine} engine
   */
  onSearchEngineUpdate = (modifiedType, engine) => {
    switch (modifiedType) {
      case "removed":
      case "changed": {
        let searchMode = this.searchMode;
        if (searchMode?.engineName == engine.name) {
          // Exit search mode if the current search mode engine was removed.
          this.searchMode = searchMode;
        }
        break;
      }
      case "default":
        this.updatePlaceholder();
        // The cached result might use the old default engine.
        this._resultForCurrentValue = null;
        break;
    }
  };

  /**
   * Get search source for telemetry.
   *
   * @param {Event} [event]
   *   The event that triggered this query.
   *   This is not needed for urlbar.* telemetry.
   * @returns {keyof typeof BrowserSearchTelemetry.KNOWN_SEARCH_SOURCES}
   *   The source name.
   */
  getSearchSource(event) {
    if (this.#isAddressbar) {
      if (this._isHandoffSession) {
        return "urlbar_handoff";
      }

      if (this.searchModeSwitcher?.eventTargetIsPanelItem(event)) {
        // The search mode switcher doesn't have its own search source yet.
        return "urlbar_searchmode";
      }

      const isOneOff =
        this.view.oneOffSearchButtons?.eventTargetIsAOneOff(event);
      if (this.searchMode && !isOneOff) {
        // Without checking !isOneOff, we might record the string
        // oneoff_urlbar-searchmode in the SEARCH_COUNTS probe (in addition to
        // oneoff_urlbar and oneoff_searchbar). The extra information is not
        // necessary; the intent is the same regardless of whether the user is
        // in search mode when they do a key-modified click/enter on a one-off.
        return "urlbar_searchmode";
      }

      let state = this.getBrowserState(
        this.browserWindow.gBrowser.selectedBrowser
      );
      if (state.persist?.searchTerms && !isOneOff) {
        // Normally, we use state.persist.shouldPersist to check if search terms
        // persisted. However when the user modifies the search term, the boolean
        // will become false. Thus, we check the presence of the search terms to
        // know whether or not search terms ever persisted in the address bar.
        return "urlbar_persisted";
      }
    }
    return this.#sapName;
  }

  // Private methods below.

  /*
   * Actions can have several buttons in the same result where not all
   * will provide a searchMode so check the currently selected button
   * in that case.
   */
  #providesSearchMode(result) {
    if (!result) {
      return false;
    }
    if (
      this.view.selectedElement &&
      result.providerName == "UrlbarProviderGlobalActions"
    ) {
      return this.view.selectedElement.dataset.providesSearchmode == "true";
    }
    return result.payload.providesSearchMode;
  }

  _addObservers() {
    if (this._observersAdded) {
      return;
    }
    this.controller.engineStore.addObserver(this.onSearchEngineUpdate);
    this._observersAdded = true;
  }

  _removeObservers() {
    if (!this._observersAdded) {
      return;
    }
    this.controller.engineStore.removeObserver(this.onSearchEngineUpdate);
    this._observersAdded = false;
  }

  _afterTabSelectAndFocusChange() {
    // We must have seen both events to proceed safely.
    if (!this._gotFocusChange || !this._gotTabSelect) {
      return;
    }
    this._gotFocusChange = this._gotTabSelect = false;

    this.formatValue();
    this._resetSearchState();

    // We don't use the original TabSelect event because caching it causes
    // leaks on MacOS.
    const event = new CustomEvent("tabswitch");
    // If the urlbar is focused after a tab switch, record a potential
    // engagement event. When switching from a focused to a non-focused urlbar,
    // the blur event would record the abandonment. When switching from an
    // unfocused to a focused urlbar, there should be no search session ongoing,
    // so this will be a no-op.
    if (this.focused) {
      this.controller.engagementEvent.record(event, {
        searchString: this._lastSearchString,
        searchSource: this.getSearchSource(event),
        windowMode: this.windowMode,
      });
    }

    // Switching tabs doesn't always change urlbar focus, so we must try to
    // reopen here too, not just on focus.
    if (this.view.autoOpen({ event })) {
      return;
    }
    // The input may retain focus when switching tabs in which case we
    // need to close the view and search mode switcher popup explicitly.
    this.searchModeSwitcher.closePanel();
    this.view.close();
  }

  /**
   * Sets the input field value.
   *
   * @param {string} val The new value to set.
   * @param {object} [options] Options for setting.
   * @param {boolean} [options.allowTrim] Whether the value can be trimmed.
   * @param {string} [options.untrimmedValue] Override for this._untrimmedValue.
   * @param {boolean} [options.valueIsTyped] Override for this.valueIsTyped.
   * @param {string} [options.actionType] Value for the `actiontype` attribute.
   *
   * @returns {string} The set value.
   */
  setValue(
    val,
    {
      allowTrim = false,
      untrimmedValue = null,
      valueIsTyped = false,
      actionType = undefined,
    } = {}
  ) {
    // Don't expose internal about:reader URLs to the user.
    val = UrlbarContentUtils.getReaderOriginalDisplaySpec(val) ?? val;
    this._untrimmedValue = untrimmedValue ?? val;
    this._protocolIsTrimmed = false;
    this._wwwIsTrimmed = false;
    // `_trimValue` only trims in the address bar, so elsewhere there is no
    // prefix to derive and `BrowserUIUtils` (chrome-only) is not needed.
    if (allowTrim && this.#isAddressbar) {
      let oldVal = val;
      val = this._trimValue(val);
      // Derive what was trimmed from the authoritative prefix logic (a "www."
      // is only ever stripped together with the protocol). _trimValue may
      // decline to trim (e.g. RTL or mixed-content values), so also confirm the
      // prefix was actually removed from the displayed value.
      let trimmedPrefix = lazy.BrowserUIUtils.getTrimmedURLPrefix(oldVal);
      if (trimmedPrefix && !val.startsWith(trimmedPrefix)) {
        this._protocolIsTrimmed = trimmedPrefix.startsWith(
          lazy.BrowserUIUtils.trimURLProtocol
        );
        this._wwwIsTrimmed = trimmedPrefix.endsWith("www.");
      }
    }

    this.valueIsTyped = valueIsTyped;
    this._resultForCurrentValue = null;
    this.inputField.value = val;
    this.formatValue();

    if (actionType !== undefined) {
      this.setAttribute("actiontype", actionType);
    } else {
      this.removeAttribute("actiontype");
    }

    // Dispatch ValueChange event for accessibility.
    let event = document.createEvent("Events");
    event.initEvent("ValueChange", true, true);
    this.inputField.dispatchEvent(event);

    return val;
  }

  /**
   * Extracts a input value from a UrlbarResult, used when filling the input
   * field on selecting a result.
   *
   * Some examples:
   *  - If the result is a bookmark keyword or dynamic, the value will be
   *    its `input` property.
   *  - If the result is search, the value may be `keyword` combined with
   *    `suggestion` or `query`.
   *  - If the result is WebExtension Omnibox, the value will be extracted
   *    from `content`.
   *  - For results returning URLs the value may be `urlOverride` or `url`.
   *
   * @param {UrlbarResult} result
   *   The result to extract the value from.
   * @param {object} options
   *   Options object.
   * @param {string} [options.urlOverride]
   *   For results normally returning a url string, this allows to override
   *   it. A blank string may passed-in to clear the input.
   * @param {HTMLElement} [options.element]
   *   The element that was selected or picked, if available. For results that
   *   have multiple selectable children, the value may be taken from a child
   *   element rather than the result.
   * @returns {string} The value.
   */
  #getValueFromResult(result, { urlOverride = null, element = null } = {}) {
    switch (result.type) {
      case UrlbarShared.RESULT_TYPE.KEYWORD:
        return result.payload.input;
      case UrlbarShared.RESULT_TYPE.SEARCH: {
        let value = "";
        if (result.payload.keyword) {
          value += result.payload.keyword + " ";
        }
        value += result.payload.suggestion || result.payload.query;
        return value;
      }
      case UrlbarShared.RESULT_TYPE.OMNIBOX:
        return result.payload.content;
      case UrlbarShared.RESULT_TYPE.DYNAMIC:
        return (
          element?.dataset.query ||
          element?.dataset.url ||
          result.payload.input ||
          result.payload.query ||
          ""
        );
      case UrlbarShared.RESULT_TYPE.RESTRICT:
        return result.payload.autofillKeyword + " ";
      case UrlbarShared.RESULT_TYPE.AI_CHAT:
        return result.payload.query ?? "";
      case UrlbarShared.RESULT_TYPE.TIP: {
        let value = element?.dataset.url || element?.dataset.input;
        if (value) {
          return value;
        }
        break;
      }
    }

    // Always respect a set urlOverride property.
    if (urlOverride !== null) {
      // This returns null for the empty string, allowing callers to clear the
      // input by passing an empty string as urlOverride.
      let url = URL.parse(urlOverride);
      return url ? losslessDecodeURL(url) : "";
    }

    let parsedUrl = URL.parse(result.payload.url);
    // If the url is not parsable, just return an empty string;
    if (!parsedUrl) {
      return "";
    }

    let url = losslessDecodeURL(parsedUrl);
    // If the user didn't originally type a protocol, and we generated one,
    // trim the http protocol from the input value, as https-first may upgrade
    // it to https, breaking user expectations.
    let stripHttp =
      this.#isAddressbar &&
      result.heuristic &&
      result.payload.url.startsWith("http://") &&
      this.userTypedValue &&
      this.#getSchemelessInput(this.userTypedValue) ==
        SCHEMELESS_INPUT_SCHEMELESS;
    if (!stripHttp) {
      return url;
    }
    // Attempt to trim the url. If doing so results in a string that is
    // interpreted as search (e.g. unknown single word host, or domain suffix),
    // use the unmodified url instead. Otherwise, if the user edits the url
    // and confirms the new value, we may transform the url into a search.
    let trimmedUrl = UrlbarShared.stripPrefixAndTrim(url, { stripHttp })[0];
    let isSearch = !!UrlbarContentUtils.getFixupPrimitives(
      trimmedUrl,
      this.isPrivate
    )?.keywordAsSent;
    if (isSearch) {
      // Although https-first might not respect the shown protocol, converting
      // the result to a search would be more disruptive.
      return url;
    }
    return trimmedUrl;
  }

  /**
   * Extracts from a result the value to use for the `actiontype` attribute.
   *
   * @param {UrlbarResult} result The UrlbarResult to consider.
   *
   * @returns {string} The `actiontype` value, or undefined.
   */
  #getActionTypeFromResult(result) {
    switch (result.type) {
      case UrlbarShared.RESULT_TYPE.TAB_SWITCH:
        return "switchtab";
      case UrlbarShared.RESULT_TYPE.OMNIBOX:
        return "extension";
      default:
        return undefined;
    }
  }

  /**
   * Resets some state so that searches from the user's previous interaction
   * with the input don't interfere with searches from a new interaction.
   */
  _resetSearchState() {
    this._lastSearchString = this.value;
    this._autofillPlaceholder = null;
  }

  /**
   * Autofills the autofill placeholder string if appropriate, and determines
   * whether autofill should be allowed for the new search started by an input
   * event.
   *
   * @param {string} value
   *   The new search string.
   * @returns {boolean}
   *   Whether autofill should be allowed in the new search.
   */
  _maybeAutofillPlaceholder(value) {
    // We allow autofill in local but not remote search modes.
    let allowAutofill =
      this.selectionEnd == value.length &&
      !this.searchMode?.engineName &&
      this.searchMode?.source != UrlbarShared.RESULT_SOURCE.SEARCH;

    if (!allowAutofill) {
      this.#clearAutofill();
      return false;
    }

    // Determine whether we can autofill the placeholder.  The placeholder is a
    // value that we autofill now, when the search starts and before we wait on
    // its first result, in order to prevent a flicker in the input caused by
    // the previous autofilled substring disappearing and reappearing when the
    // first result arrives.  Of course we can only autofill the placeholder if
    // it starts with the new search string, and we shouldn't autofill anything
    // if the caret isn't at the end of the input.
    let canAutofillPlaceholder = false;
    if (this._autofillPlaceholder) {
      if (
        this._autofillPlaceholder.type == "adaptive_url" ||
        this._autofillPlaceholder.type == "adaptive_origin"
      ) {
        canAutofillPlaceholder =
          value.length >=
            this._autofillPlaceholder.adaptiveHistoryInput.length &&
          this._autofillPlaceholder.value
            .toLocaleLowerCase()
            .startsWith(value.toLocaleLowerCase());
      } else {
        canAutofillPlaceholder = UrlbarShared.canAutofillURL(
          this._autofillPlaceholder.value,
          value
        );
      }
    }

    if (!canAutofillPlaceholder) {
      this._autofillPlaceholder = null;
    } else if (
      this._autofillPlaceholder &&
      this.selectionEnd == this.value.length &&
      this._enableAutofillPlaceholder
    ) {
      let autofillValue =
        value + this._autofillPlaceholder.value.substring(value.length);
      this._autofillValue({
        value: autofillValue,
        selectionStart: value.length,
        selectionEnd: autofillValue.length,
        type: this._autofillPlaceholder.type,
        adaptiveHistoryInput: this._autofillPlaceholder.adaptiveHistoryInput,
        untrimmedValue: this._autofillPlaceholder.untrimmedValue,
      });
    }

    return true;
  }

  /**
   * Invoked on overflow/underflow/scrollend events to update attributes
   * related to the input text directionality. Overflow fade masks use these
   * attributes to appear at the proper side of the urlbar.
   */
  updateTextOverflow() {
    if (!this.#isAddressbar) {
      // The main purpose of overflow fading is to make it clear when URLs
      // overflow. We don't need this in more traditional search inputs where
      // the text is controlled by the users. Fading also doesn't work correctly
      // when the search input has a clear button.
      return;
    }

    if (!this._overflowing) {
      this.removeAttribute("textoverflow");
      return;
    }

    let isRTL =
      this.getAttribute("domaindir") === "rtl" &&
      UrlbarContentUtils.isTextDirectionRTL(this.value, window);

    window.promiseDocumentFlushed(() => {
      // Check overflow again to ensure it didn't change in the meanwhile.
      let input = this.inputField;
      if (input && this._overflowing) {
        // Normally we overflow at the end side of the text direction, though
        // RTL domains may cause us to overflow at the opposite side.
        // The outcome differs depending on the input field contents and applied
        // formatting, and reports the final state of all the scrolling into an
        // attribute available to css rules.
        // Note it's also possible to scroll an unfocused input field using
        // SHIFT + mousewheel on Windows, or with just the mousewheel / touchpad
        // scroll (without modifiers) on Mac.
        let side = "both";
        if (isRTL) {
          if (input.scrollLeft == 0) {
            side = "left";
          } else if (input.scrollLeft == input.scrollLeftMin) {
            side = "right";
          }
        } else if (input.scrollLeft == 0) {
          side = "right";
        } else if (input.scrollLeft == input.scrollLeftMax) {
          side = "left";
        }

        window.requestAnimationFrame(() => {
          // And check once again, since we might have stopped overflowing
          // since the promiseDocumentFlushed callback fired.
          if (this._overflowing) {
            this.setAttribute("textoverflow", side);
          }
        });
      }
    });
  }

  get inOverflowPanel() {
    if (!this.parentElement?.id) {
      return false;
    }
    return (
      lazy.CustomizableUI.getPlacementOfWidget(this.parentElement.id)?.area ==
        lazy.CustomizableUI.AREA_FIXED_OVERFLOW_PANEL ||
      this.parentElement.getAttribute("overflowedItem") == "true"
    );
  }

  _updateUrlTooltip() {
    if (this.focused || !this._overflowing) {
      this.inputField.removeAttribute("title");
    } else {
      this.inputField.setAttribute("title", this.untrimmedValue);
    }
  }

  _getSelectedValueForClipboard() {
    let selectedVal = this.#selectedText;

    // Handle multiple-range selection as a string for simplicity.
    if (this.editor.selection.rangeCount > 1) {
      return selectedVal;
    }

    // If the selection doesn't start at the beginning or doesn't span the
    // full domain or the URL bar is modified or there is no text at all,
    // nothing else to do here.
    // TODO (Bug 1908360): the valueIsTyped usage here is confusing, as often
    // it doesn't really indicate a user typed a value, it's rather used as
    // a way to tell if the value was modified.
    if (
      this.selectionStart > 0 ||
      selectedVal == "" ||
      (this.valueIsTyped && !this._protocolIsTrimmed && !this._wwwIsTrimmed)
    ) {
      return selectedVal;
    }

    // The selection doesn't span the full domain if it doesn't contain a slash and is
    // followed by some character other than a slash.
    if (!selectedVal.includes("/")) {
      let remainder = this.value.replace(selectedVal, "");
      if (remainder != "" && remainder[0] != "/") {
        return selectedVal;
      }
    }

    let uri;
    if (this.getAttribute("pageproxystate") == "valid") {
      uri = this.#isOpenedPageInBlankTargetLoading
        ? this.browserWindow.gBrowser.selectedBrowser.browsingContext
            .nonWebControlledLoadingURI
        : this.browserWindow.gBrowser.currentURI;
    } else {
      // The value could be:
      // 1. a trimmed url, set by selecting a result
      // 2. a search string set by selecting a result
      // 3. a url that was confirmed but didn't finish loading yet
      // If it's an url the untrimmedValue should resolve to a valid URI,
      // otherwise it's a search string that should be copied as-is.

      // If the copied text is that autofilled value, return the url including
      // the protocol from its suggestion.
      let result = this._resultForCurrentValue;

      if (result?.autofill?.value == selectedVal) {
        return result.payload.url;
      }

      uri = URL.parse(this._untrimmedValue)?.URI;
      if (!uri) {
        return selectedVal;
      }
    }
    uri = this.makeURIReadable(uri);
    let displaySpec = uri.displaySpec;

    // If the entire URL is selected, just use the actual loaded URI,
    // unless we want a decoded URI, or it's a data: or javascript: URI,
    // since those are hard to read when encoded.
    if (
      this.value == selectedVal &&
      !uri.schemeIs("javascript") &&
      !uri.schemeIs("data") &&
      !UrlbarPrefs.get("decodeURLsOnCopy")
    ) {
      return displaySpec;
    }

    // Just the beginning of the URL is selected, or we want a decoded
    // url. First check for a trimmed value.

    // _trimValue may also trim a trailing slash, so we can't compare strings
    // directly to tell what was trimmed; consult the trimmed prefix instead.
    let trimmedPrefix = lazy.BrowserUIUtils.getTrimmedURLPrefix(displaySpec);
    if (trimmedPrefix && !selectedVal.startsWith(trimmedPrefix)) {
      selectedVal = trimmedPrefix + selectedVal;
    }

    // If selection starts from the beginning and part or all of the URL
    // is selected, we check for decoded characters and encode them.
    // Unless decodeURLsOnCopy is set. Do not encode data: URIs.
    if (!UrlbarPrefs.get("decodeURLsOnCopy") && !uri.schemeIs("data")) {
      try {
        if (URL.canParse(selectedVal)) {
          // Use encodeURI instead of URL.href because we don't want
          // trailing slash.
          selectedVal = encodeURI(selectedVal);
        }
      } catch (ex) {
        // URL is invalid. Return original selected value.
      }
    }

    return selectedVal;
  }

  _toggleActionOverride(event) {
    if (
      event.keyCode == KeyEvent.DOM_VK_SHIFT ||
      event.keyCode == KeyEvent.DOM_VK_ALT ||
      event.keyCode ==
        (UrlbarContentUtils.getPlatform() == "macosx"
          ? KeyEvent.DOM_VK_META
          : KeyEvent.DOM_VK_CONTROL)
    ) {
      if (event.type == "keydown") {
        this._actionOverrideKeyCount++;
        this.toggleAttribute("action-override", true);
        this.view.panel.toggleAttribute("action-override", true);
      } else if (
        this._actionOverrideKeyCount &&
        --this._actionOverrideKeyCount == 0
      ) {
        this._clearActionOverride();
      }
    }
  }

  _clearActionOverride() {
    this._actionOverrideKeyCount = 0;
    this.removeAttribute("action-override");
    this.view.panel.removeAttribute("action-override");
  }

  /**
   * @typedef {object} SearchActionDetails
   *
   * @property {boolean} [isSuggestion]
   *   True if this query was initiated from a suggestion from the search engine.
   * @property {string} [alias]
   *   The search engine alias this query was initiated with, if any.
   * @property {boolean} [isFormHistory]
   *   True if this query was initiated from a form history result.
   * @property {string} [url]
   *   The url this query was triggered with.
   */

  /**
   * Gets the data for recording search telemetry and adding the search to form
   * history. The parent records it once the search's URL is being loaded.
   *
   * @param {object} options
   * @param {PartialSearchEngine|SearchEngine} options.engine
   *   The engine to record the query for.
   * @param {string} options.query
   *   The search query.
   * @param {Event} options.event
   *   The event that triggered this query.
   * @param {SearchActionDetails} [options.searchActionDetails]
   *   The details associated with this search query.
   * @param {boolean} [options.opensInPrivateWindow]
   *   Whether the search opens in a new private window.
   * @returns {UrlbarSearchData}
   */
  #getSearchData({
    engine,
    query,
    event,
    searchActionDetails = {},
    opensInPrivateWindow = false,
  }) {
    return {
      engineId: engine.id,
      searchSource: this.getSearchSource(event),
      query,
      opensInPrivateWindow,
      details: {
        ...searchActionDetails,
        isOneOff: this.view.oneOffSearchButtons?.eventTargetIsAOneOff(event),
        newtabSessionId: this._handoffSession,
      },
    };
  }

  /**
   * Shortens the given value, usually by removing http:// and trailing slashes.
   *
   * @param {string} val
   *   The string to be trimmed if it appears to be URI
   * @returns {string}
   *   The trimmed string
   */
  _trimValue(val) {
    if (!this.#isAddressbar) {
      return val;
    }
    let trimmedValue = UrlbarPrefs.get("trimURLs")
      ? lazy.BrowserUIUtils.trimURL(val)
      : val;
    // Only trim value if the directionality doesn't change to RTL and we're not
    // showing a strikeout https protocol.
    return UrlbarContentUtils.isTextDirectionRTL(trimmedValue, window) ||
      this.#getValueFormatter().willShowFormattedMixedContentProtocol(val)
      ? val
      : trimmedValue;
  }

  /**
   * If appropriate, this prefixes a search string with 'www.' and suffixes it
   * with Services.locale.urlFixupSuffix prior to navigating.
   *
   * @param {Event} event
   *   The event that triggered this query.
   * @param {string} value
   *   The search string that should be canonized.
   * @returns {string}
   *   Returns the canonized URL if available and null otherwise.
   */
  _maybeCanonizeURL(event, value) {
    // Only add the suffix when the URL bar value isn't already "URL-like",
    // and only if we get a keyboard event, to match user expectations.
    if (
      !this.controller.isCanonizeKeyboardEvent(event) ||
      !/^\s*[^.:\/\s]+(?:\/.*|\s*)$/i.test(value)
    ) {
      return null;
    }

    let suffix = Services.locale.urlFixupSuffix;
    if (!suffix.endsWith("/")) {
      suffix += "/";
    }

    // trim leading/trailing spaces (bug 233205)
    value = value.trim();

    // Tack www. and suffix on.  If user has appended directories, insert
    // suffix before them (bug 279035).  Be careful not to get two slashes.
    let firstSlash = value.indexOf("/");
    if (firstSlash >= 0) {
      value =
        value.substring(0, firstSlash) +
        suffix +
        value.substring(firstSlash + 1);
    } else {
      value = value + suffix;
    }

    try {
      const info = Services.uriFixup.getFixupURIInfo(
        value,
        Ci.nsIURIFixup.FIXUP_FLAGS_MAKE_ALTERNATE_URI
      );
      value = info.fixedURI.spec;
    } catch (ex) {
      console.error(`An error occured while trying to fixup "${value}"`, ex);
    }

    this.value = value;
    this.parentController.recordCanonization();
    return value;
  }

  /**
   * Autofills a value into the input. The value will be autofilled regardless
   * of the input's current value.
   *
   * @param {AutofillPlaceholder} options
   *   The autofill options.
   */
  _autofillValue({
    value,
    selectionStart,
    selectionEnd,
    type,
    adaptiveHistoryInput,
    untrimmedValue,
  }) {
    // The autofilled value may be a URL that includes a scheme at the
    // beginning.  Do not allow it to be trimmed.
    this.setValue(value, { untrimmedValue });
    // Keep the origin in view rather than the tail of the value. A long
    // autofilled path would otherwise scroll the host out of the view.
    // TODO (Bug 1566151): scrollLeftMin is the start-of-text edge in both
    // directions, but an RTL host can never autofill today because the queries
    // match the typed string against the punycode stored in Places. Revisit
    // this once IDN hosts autofill, when the RTL case becomes reachable.
    this.inputField.scrollLeft = this.inputField.scrollLeftMin;
    this.inputField.setSelectionRange(selectionStart, selectionEnd, "backward");
    this._autofillPlaceholder = {
      value,
      type,
      adaptiveHistoryInput,
      selectionStart,
      selectionEnd,
      untrimmedValue,
    };
  }

  /**
   * Called when a menu item from results menu is picked.
   *
   * @param {UrlbarResult} result The result that was picked.
   * @param {Event} event The event that picked the result.
   * @param {HTMLElement} element the picked view element, if available.
   */
  #pickMenuResult(result, event, element) {
    this.controller.engagementEvent.record(event, {
      result,
      element,
      searchString: this._lastSearchString,
      selType: element.dataset.command,
      searchSource: this.getSearchSource(event),
      windowMode: this.windowMode,
    });

    if (element.dataset.command == "manage") {
      this.parentController.openPreferences("search-locationBar");
      return;
    }

    let url;
    if (element.dataset.command == "help") {
      url = result.payload.helpUrl;
    }
    url ||= element.dataset.url;

    if (!url) {
      return;
    }

    let where = this.controller.whereToOpen(event);
    if (element.dataset.command == "help" && where == "current") {
      // Open help links in a new tab.
      where = "tab";
    }

    this.view.close({ elementPicked: true });

    this.#loadURL({
      loadRequest: { urlLoad: { url, postData: null } },
      event,
      where,
      params: {
        allowInheritPrincipal: false,
        private: this.isPrivate,
      },
      resultDetails: {
        source: result.source,
        type: result.type,
      },
    });
  }

  /**
   * @typedef {object} LoadURLResultDetails
   *   Details of the selected result, if any.
   *
   * @property {Values<typeof UrlbarShared.RESULT_TYPE>} [type]
   *   Details of the result type, if any.
   * @property {Values<typeof UrlbarShared.RESULT_SOURCE>} [source]
   *   Details of the result source, if any.
   */

  /**
   * Loads the url in the appropriate place.
   *
   * @param {object} options
   * @param {UrlbarLoadRequest} options.loadRequest
   *   What to load.
   * @param {Event} options.event
   *   The event that triggered to load the url.
   * @param {string} options.where
   *   Where we expect the result to be opened.
   * @param {LoadURLParams} options.params
   *   The parameters related to how and where the result will be opened.
   * @param {LoadURLResultDetails} [options.resultDetails]
   *   Details of the selected result, if any.
   * @param {boolean} [options.keepViewOpen]
   *   Whether the view should remain open.
   * @param {number} [options.browserId]
   *   The id of the browser to load into. Defaults to the parent resolving the
   *   selected browser at load time; pass it to pin an asynchronously-resolved
   *   load to the tab selected when it was committed.
   * @param {?UrlbarSearchData} [options.searchData]
   *   For a search, the data the parent records once the load has gone ahead.
   */
  async #loadURL({
    loadRequest,
    event,
    where,
    params,
    resultDetails = null,
    keepViewOpen = false,
    browserId = null,
    searchData = null,
  }) {
    let keyDownEnterDeferred;
    if (
      this._keyDownEnterDeferred &&
      UrlbarShared.isInstance(event, KeyboardEvent) &&
      event.keyCode === KeyEvent.DOM_VK_RETURN &&
      where === "current"
    ) {
      // In this case, we move the focus to the browser that loads the content
      // upon key up the enter key.
      // To do it, send avoidBrowserFocus flag to openTrustedLinkIn() to avoid
      // focusing on the browser in the function. And also, set loadedContent
      // flag that whether the content is loaded in the current tab by this enter
      // key. The load resolves the deferred with the loaded browser's id, which
      // key up hands to the parent to focus.
      params.avoidBrowserFocus = true;
      this._keyDownEnterDeferred.loadedContent = true;
      keyDownEnterDeferred = this._keyDownEnterDeferred;
    }

    let userTypedValue;
    if (this.#isAddressbar && where == "current") {
      if (loadRequest.engineSearch) {
        this.value = loadRequest.engineSearch.query;
      } else {
        let { url } = loadRequest.urlLoad;
        // Make sure URL is formatted properly (don't show punycode).
        try {
          this.value = losslessDecodeURL(new URL(url));
        } catch {
          this.value = url;
        }
      }
      userTypedValue = this.value;
    }

    params.allowThirdPartyFixup = true;

    if (where == "current") {
      params.indicateErrorPageLoad = true;
      params.allowPinnedTabHostChange = this.#isAddressbar;
      params.allowPopups =
        loadRequest.urlLoad?.url.startsWith("javascript:") ?? false;
    }

    // Ensure the window gets the `private` feature if the current window
    // is private, unless the caller explicitly requested not to.
    if (this.isPrivate && !("private" in params)) {
      params.private = true;
    }

    // Make sure the domain name stays visible for spoof protection and
    // usability. The browser itself is focused parent-side, where the load
    // runs against the chrome window.
    if (this.#isAddressbar && !params.avoidBrowserFocus) {
      this.inputField.setSelectionRange(0, 0);
    }

    if (where != "current" && !this.isSearchbarSAP) {
      this.handleRevert();
    }

    // Notify about the start of navigation.
    this.#notifyStartNavigation(resultDetails);

    let loadStatus = await this.parentController.loadURL({
      loadRequest,
      where,
      params,
      browserId,
      userTypedValue,
      searchData,
    });
    // Hand the loaded browser's id to the deferred-Enter key up handler so it
    // can focus it parent-side.
    keyDownEnterDeferred?.resolve(loadStatus.browserId);
    // The load can throw parent-side; unless an error page was shown we
    // replace the URL with the loaded one.
    if (loadStatus.reverted) {
      this.handleRevert();
    }
    if (!keepViewOpen) {
      // If we show the focus border after closing the view, it would appear
      // to flash since this._on_blur would remove it immediately after.
      this.view.close({ showFocusBorder: false });
    }
  }

  _initCopyCutController() {
    // This exists to put an untrimmed URL on the clipboard, and only the address
    // bar trims. Every other input keeps its native copy/cut.
    if (!this.#isAddressbar) {
      return;
    }
    if (this._copyCutController) {
      return;
    }
    this._copyCutController = new CopyCutController(this);
    this.inputField.controllers.insertControllerAt(0, this._copyCutController);
  }

  /**
   * Strips known tracking query parameters/ link decorators.
   *
   * @returns {nsIURI}
   *   The stripped URI or original URI, if nothing can be
   *   stripped
   */
  #stripURI() {
    let copyString = this._getSelectedValueForClipboard();
    if (!copyString) {
      return null;
    }
    let strippedURI = null;

    // Error check occurs during isClipboardURIValid
    let uri = Services.io.newURI(copyString);
    try {
      strippedURI = lazy.QueryStringStripper.stripForCopyOrShare(uri);
    } catch (e) {
      console.warn(`stripForCopyOrShare: ${e.message}`);
      return uri;
    }

    if (strippedURI) {
      return this.makeURIReadable(strippedURI);
    }
    return uri;
  }

  /**
   * Checks if the clipboard contains a valid URI
   *
   * @returns {true|false}
   */
  #isClipboardURIValid() {
    let copyString = this._getSelectedValueForClipboard();
    if (!copyString) {
      return false;
    }

    return URL.canParse(copyString);
  }

  /**
   * Checks if there is a query parameter that can be stripped
   *
   * @returns {true|false}
   */
  #canStrip() {
    let copyString = this._getSelectedValueForClipboard();
    if (!copyString) {
      return false;
    }
    // throws if the selected string is not a valid URI
    try {
      let uri = Services.io.newURI(copyString);
      return lazy.QueryStringStripper.canStripForShare(uri);
    } catch (e) {
      console.warn("canStrip failed!", e);
      return false;
    }
  }

  /**
   * Restores the untrimmed value in the urlbar.
   *
   * @param {object} [options]
   *  Options for untrimming.
   * @param {boolean} [options.moveCursorToStart]
   *  Whether the cursor should be moved at position 0 after untrimming.
   * @param {boolean} [options.ignoreSelection]
   *  Whether this should untrim, regardless of the current selection state.
   */
  #maybeUntrimUrl({ moveCursorToStart = false, ignoreSelection = false } = {}) {
    // Check if we can untrim the current value.
    if (
      !UrlbarPrefs.getScotchBonnetPref("untrimOnUserInteraction.featureGate") ||
      (!this._protocolIsTrimmed && !this._wwwIsTrimmed) ||
      !this.focused ||
      (!ignoreSelection && this.#allTextSelected)
    ) {
      return;
    }

    let selectionStart = this.selectionStart;
    let selectionEnd = this.selectionEnd;

    // Correct the selection taking the trimmed prefix (protocol and
    // leading "www.") into account.
    let offset =
      (this._protocolIsTrimmed
        ? lazy.BrowserUIUtils.trimURLProtocol.length
        : 0) + (this._wwwIsTrimmed ? "www.".length : 0);

    // In case of autofill, we may have to adjust its boundaries.
    if (this._autofillPlaceholder) {
      this._autofillPlaceholder.selectionStart += offset;
      this._autofillPlaceholder.selectionEnd += offset;
    }

    if (moveCursorToStart) {
      this.setValue(this._untrimmedValue, {
        valueIsTyped: this.valueIsTyped,
      });
      this.setSelectionRange(0, 0);
      return;
    }

    if (selectionStart == selectionEnd) {
      // When cursor is at the end of the string, untrimming may
      // reintroduced a trailing slash and we want to move past it.
      if (selectionEnd == this.value.length) {
        offset += 1;
      }
      selectionStart = selectionEnd += offset;
    } else {
      // There's a selection, so we must calculate both the initial
      // protocol and the eventual trailing slash.
      if (selectionStart != 0) {
        selectionStart += offset;
      } else {
        // When selection starts at the beginning, the adjusted selection will
        // include the protocol only if the selected text includes the host.
        // The port is left out, as one may want to exclude it from the copy.
        let prePathMinusPort;
        try {
          let uri = Services.io.newURI(this._untrimmedValue);
          prePathMinusPort = [uri.userPass, uri.displayHost]
            .filter(Boolean)
            .join("@");
        } catch (ex) {
          logger().error("Should only try to untrim valid URLs");
        }
        if (!this.#selectedText.startsWith(prePathMinusPort)) {
          selectionStart += offset;
        }
      }
      if (selectionEnd == this.value.length) {
        offset += 1;
      }
      selectionEnd += offset;
    }

    this.setValue(this._untrimmedValue, {
      valueIsTyped: this.valueIsTyped,
    });

    this.setSelectionRange(selectionStart, selectionEnd);
  }

  // The strip-on-share feature will strip known tracking/decorational
  // query params from the URI and copy the stripped version to the clipboard.
  _initStripOnShare() {
    this.addContextMenuItems({
      after: "edit-contextmenu-copy",
      createItems: () => {
        let fragment = document.createDocumentFragment();
        fragment.appendChild(this.#createStripOnShareItem());
        return fragment;
      },
      onShowing: (input, [stripOnShare]) => {
        this.#updateStripOnShareItem(stripOnShare);
      },
    });
  }

  /**
   * Builds the strip-on-share menuitem. On macOS the addressbar appends it to
   * the share group instead of registering its own item set.
   *
   * @returns {Element} A newly created menuitem
   */
  #createStripOnShareItem() {
    let stripOnShare = document.createXULElement("menuitem");
    document.l10n.setAttributes(stripOnShare, "text-action-copy-clean-link");
    stripOnShare.setAttribute("anonid", "strip-on-share");
    stripOnShare.id = "strip-on-share";

    // Register listener that returns the stripped url or falls back
    // to the original url if nothing can be stripped.
    stripOnShare.addEventListener("command", () => {
      let strippedURI = this.#stripURI();
      lazy.ClipboardHelper.copyString(strippedURI.displaySpec);
    });

    return stripOnShare;
  }

  // Hide the menu item if there is nothing to copy.
  #updateStripOnShareItem(stripOnShare) {
    // feature is not enabled
    if (!UrlbarPrefs.get("privacy.query_stripping.strip_on_share.enabled")) {
      stripOnShare.setAttribute("hidden", true);
      return;
    }
    let controller =
      document.commandDispatcher.getControllerForCommand("cmd_copy");
    if (
      !controller.isCommandEnabled("cmd_copy") ||
      !this.#isClipboardURIValid()
    ) {
      stripOnShare.setAttribute("hidden", true);
      return;
    }
    stripOnShare.removeAttribute("hidden");
    if (!this.#canStrip()) {
      stripOnShare.setAttribute("disabled", true);
      return;
    }
    stripOnShare.removeAttribute("disabled");
  }

  #initPasteAndGo() {
    this.addContextMenuItems({
      after: "edit-contextmenu-paste",
      createItems: () => {
        let fragment = document.createDocumentFragment();
        let pasteAndGo = document.createXULElement("menuitem");
        pasteAndGo.id = "paste-and-go";
        pasteAndGo.setAttribute("anonid", "paste-and-go");
        pasteAndGo.addEventListener("command", () => {
          this._suppressStartQuery = true;

          this.select();
          window.goDoCommand("cmd_paste");
          this.setResultForCurrentValue(null);
          this.handleCommand();
          this.parentController.clearLastQueryContextCache();

          this._suppressStartQuery = false;
        });

        fragment.appendChild(pasteAndGo);
        return fragment;
      },
      onShowing: async (input, [pasteAndGo]) => {
        // Close the results pane, because paste and go doesn't want a result
        // selection. This has to happen before the menu opens: hiding the
        // popover once it's open keeps it from showing (bug 2037468).
        this.view.close();

        // The edit context menu is shared with other inputs in chrome, and
        // only the items we add here conflict, so mark it for this opening
        // alone, removing the marker again when closed.
        let popup = window.EditContextMenu.popup;
        popup.setAttribute("accesskey-conflicts-bug", "2073891");
        popup.addEventListener(
          "popuphidden",
          () => {
            // An opening that got superseded before it finished still fires
            // popuphidden while the menu is open.
            if (popup.state == "closed") {
              popup.removeAttribute("accesskey-conflicts-bug");
            }
          },
          { once: true }
        );

        let controller =
          document.commandDispatcher.getControllerForCommand("cmd_paste");
        let enabled = controller.isCommandEnabled("cmd_paste");
        pasteAndGo.toggleAttribute("disabled", !enabled);
        if (!enabled) {
          return;
        }

        let l10nId;
        if (!this.#navigationEnabled || this.searchMode) {
          l10nId = "urlbar-menuitem-paste-and-search";
        } else if (!UrlbarShared.keywordEnabled(this.#sapName)) {
          l10nId = "urlbar-menuitem-paste-and-go";
        } else {
          let text = await UrlbarContentUtils.getClipboardTextIfSmallerThan(
            UrlbarShared.MAX_TEXT_LENGTH,
            window
          );
          let fixup =
            text && UrlbarContentUtils.getFixupPrimitives(text, this.isPrivate);
          l10nId = fixup?.keywordAsSent
            ? "urlbar-menuitem-paste-and-search"
            : "urlbar-menuitem-paste-and-go";
        }
        pasteAndGo.ownerDocument.l10n.setAttributes(pasteAndGo, l10nId);
      },
    });
  }

  // Adds "Dismiss" and "Forget this site" entries to the urlbar input context
  // menu, both hidden unless the heuristic result is autofill.
  _initAutofillDismiss() {
    this.addContextMenuItems({
      after: "edit-contextmenu-select-all",
      createItems: () => {
        let fragment = document.createDocumentFragment();

        let separator = document.createXULElement("menuseparator");
        separator.setAttribute(
          "anonid",
          "urlbar-input-autofill-dismiss-separator"
        );

        let dismiss = document.createXULElement("menuitem");
        dismiss.setAttribute("anonid", "urlbar-input-dismiss-autofill");
        document.l10n.setAttributes(dismiss, "urlbar-input-dismiss-autofill");
        dismiss.addEventListener("command", () => {
          this.#dismissAdaptiveAutofillFromContextMenu("dismiss");
        });

        let forget = document.createXULElement("menuitem");
        forget.setAttribute("anonid", "urlbar-input-remove-from-history");
        document.l10n.setAttributes(forget, "urlbar-input-remove-from-history");
        forget.addEventListener("command", () => {
          this.#dismissAdaptiveAutofillFromContextMenu("forget");
        });

        fragment.append(separator, dismiss, forget);
        return fragment;
      },
      onShowing: (input, [separator, dismiss, forget]) => {
        let { showDismiss, showForget } =
          this.#autofillDismissContextMenuVisibility();
        separator.hidden = !showDismiss && !showForget;
        dismiss.hidden = !showDismiss;
        forget.hidden = !showForget;
      },
    });
  }

  /**
   * Computes whether the autofill dismiss/forget context menu items should be
   * shown for the current heuristic autofill result.
   *
   * @returns {{ showDismiss: boolean, showForget: boolean }}
   *   showDismiss is true when the "Dismiss" item should be visible, which
   *   requires adaptive history autofill to be enabled, the current heuristic
   *   result to be an autofill of type "adaptive_url", "adaptive_origin" or
   *   "origin", and the window to not be private. showForget is true when the
   *   "Remove from history" item should be visible, which requires the
   *   autofilled URL to be a deep link.
   */
  #autofillDismissContextMenuVisibility() {
    let hidden = { showDismiss: false, showForget: false };

    if (!UrlbarPrefs.get("autoFill.adaptiveHistory.enabled")) {
      return hidden;
    }

    let result = this._resultForCurrentValue;
    if (!result?.heuristic || !result.autofill) {
      return hidden;
    }

    let type = result.autofill.type;
    if (
      type !== "adaptive_url" &&
      type !== "adaptive_origin" &&
      type !== "origin"
    ) {
      return hidden;
    }

    let isOrigin = UrlbarShared.isOriginUrl(result.payload.url);
    return {
      showDismiss: !this.isPrivate,
      showForget: !isOrigin,
    };
  }

  /**
   * Dismisses the current heuristic autofill result.
   *
   * @param {"dismiss" | "forget"} action
   *   "dismiss" blocks the autofill pairing for a period of time.
   *   "forget" removes the URL from history entirely.
   */
  async #dismissAdaptiveAutofillFromContextMenu(action) {
    let result = this._resultForCurrentValue;
    if (!result?.heuristic || !result.autofill) {
      return;
    }

    await this.parentController
      .dismissAutofill(result.payload.url, action)
      .catch(console.error);

    this.setValue(this._lastSearchString);
    this.startQuery({
      searchString: this._lastSearchString,
      allowAutofill: false,
      resetSearchState: false,
    });
  }

  /**
   * Initializes the share group: Share…, Create QR Code and Copy Clean Link.
   * This is only shown on the addressbar and only on macOS.
   */
  #initShareURL() {
    this.addContextMenuItems({
      after: "edit-contextmenu-select-all",
      createItems: () => {
        let fragment = document.createDocumentFragment();
        fragment.appendChild(document.createXULElement("menuseparator"));

        let shareItem = document.createXULElement("menuitem");
        shareItem.classList.add("share-tab-url-item", "share-mac-picker-item");
        document.l10n.setAttributes(shareItem, "urlbar-share-url");
        shareItem.addEventListener("command", () => {
          lazy.SharingUtils.shareOnMacPicker(shareItem);
        });
        fragment.appendChild(shareItem);

        let qrCodeItem = document.createXULElement("menuitem");
        qrCodeItem.classList.add("share-qrcode-item");
        qrCodeItem.id = "share-qrcode";
        document.l10n.setAttributes(qrCodeItem, "menu-file-share-qrcode3");
        qrCodeItem.addEventListener("command", () => {
          lazy.SharingUtils.showQRCode(qrCodeItem);
        });
        fragment.appendChild(qrCodeItem);
        fragment.appendChild(this.#createStripOnShareItem());

        return fragment;
      },
      onShowing: (input, [, shareItem, qrCodeItem, stripOnShare]) => {
        this.#updateStripOnShareItem(stripOnShare);
        let browser = this.browserWindow?.gBrowser?.selectedBrowser;
        let browserRef = browser ? Cu.getWeakReference(browser) : null;
        shareItem.contextBrowserToShare = browserRef;
        shareItem.browsersToShare = null;
        qrCodeItem.contextBrowserToShare = browserRef;
        let shareable =
          !!lazy.SharingUtils.getLinkToShare(shareItem).urlToShare;
        shareItem.toggleAttribute("disabled", !shareable);
        qrCodeItem.hidden = !Services.prefs.getBoolPref(
          "browser.shareqrcode.enabled",
          false
        );
        qrCodeItem.toggleAttribute("disabled", !shareable);
      },
    });
  }

  /**
   * This notifies observers that the user has entered or selected something in
   * the URL bar which will cause navigation.
   *
   * We use the observer service, so that we don't need to load extra facilities
   * if they aren't being used, e.g. WebNavigation.
   *
   * @param {LoadURLResultDetails} result
   *   Details of the result that was selected, if any.
   */
  #notifyStartNavigation(result) {
    if (this.#isAddressbar) {
      Services.obs.notifyObservers(
        /** @type {any} */ ({ result }),
        "urlbar-user-start-navigation"
      );
    }
  }

  /**
   * Returns a search mode object if a result should enter search mode when
   * selected.
   *
   * @param {UrlbarResult} result
   *   The result to check.
   * @param {string} [entry]
   *   If provided, this will be recorded as the entry point into search mode.
   *   See setSearchMode() documentation for details.
   * @returns {?SearchModeInput}
   *   The search mode to enter, or null if search mode should not be entered.
   */
  _searchModeForResult(result, entry = null) {
    // Search mode is determined by the result's keyword or engine.
    if (
      !result.payload.keyword &&
      !result.payload.engine &&
      !this.view.selectedElement.dataset?.engine
    ) {
      return null;
    }

    let searchMode = this.searchModeForToken(result.payload.keyword);
    // If result.originalEngine is set, then the user is Alt+Tabbing
    // through the one-offs, so the keyword doesn't match the engine.
    if (
      !searchMode &&
      result.payload.engine &&
      (!result.payload.originalEngine ||
        result.payload.engine == result.payload.originalEngine)
    ) {
      searchMode = { engineName: result.payload.engine };
    } else if (this.view.selectedElement?.dataset.engine) {
      searchMode = { engineName: this.view.selectedElement.dataset.engine };
    }

    if (searchMode) {
      if (result.type == UrlbarShared.RESULT_TYPE.RESTRICT) {
        searchMode.restrictType = "keyword";
      } else if (
        UrlbarShared.SEARCH_MODE_RESTRICT.has(result.payload.keyword)
      ) {
        searchMode.restrictType = "symbol";
      }
      if (entry) {
        searchMode.entry = entry;
      } else {
        switch (result.providerName) {
          case "UrlbarProviderTopSites":
            searchMode.entry = "topsites_urlbar";
            break;
          case "UrlbarProviderTabToSearch":
            if (result.payload.dynamicType) {
              searchMode.entry = "tabtosearch_onboard";
            } else {
              searchMode.entry = "tabtosearch";
            }
            break;
          default:
            searchMode.entry = "keywordoffer";
            break;
        }
      }
    }

    return searchMode;
  }

  /**
   * Updates the UI so that search mode is either entered or exited.
   *
   * @param {?SearchMode} searchMode
   *   The search mode to display, or null to exit search mode.
   */
  _updateSearchModeUI(searchMode) {
    let { engineName, source, isGeneralPurposeEngine } = searchMode || {};

    // As an optimization, bail if the given search mode is null but search mode
    // is already inactive.
    if (!engineName && !source && !this.hasAttribute("searchmode")) {
      return;
    }

    if (this._searchModeIndicatorTitle) {
      this._searchModeIndicatorTitle.textContent = "";
      this._searchModeIndicatorTitle.removeAttribute("data-l10n-id");
    }

    if (!engineName && !source) {
      this.removeAttribute("searchmode");
      this.updatePlaceholder();
      return;
    }

    if (this.#navigationEnabled) {
      if (engineName) {
        // Set text content for the search mode indicator.
        if (this._searchModeIndicatorTitle) {
          this._searchModeIndicatorTitle.textContent = engineName;
        }
        document.l10n.setAttributes(
          this.inputField,
          isGeneralPurposeEngine
            ? "urlbar-placeholder-search-mode-web-2"
            : "urlbar-placeholder-search-mode-other-engine",
          { name: engineName }
        );
      } else if (source) {
        const messageIDs = {
          actions: "urlbar-placeholder-search-mode-other-actions",
          bookmarks: "urlbar-placeholder-search-mode-other-bookmarks",
          engine: "urlbar-placeholder-search-mode-other-engine",
          history: "urlbar-placeholder-search-mode-other-history",
          tabs: "urlbar-placeholder-search-mode-other-tabs",
        };
        let sourceName = UrlbarShared.getResultSourceName(source);
        if (this._searchModeIndicatorTitle) {
          document.l10n.setAttributes(
            this._searchModeIndicatorTitle,
            `urlbar-search-mode-${sourceName}`
          );
        }
        document.l10n.setAttributes(this.inputField, messageIDs[sourceName]);
      }
    }

    this.toggleAttribute("searchmode", true);
    // Clear autofill.
    if (this._autofillPlaceholder && this.userTypedValue) {
      this.value = this.userTypedValue;
    }
    // Search mode should only be active when pageproxystate is invalid.
    if (this.getAttribute("pageproxystate") == "valid") {
      this.value = "";
      this.setPageProxyState("invalid", true);
    }

    lazy?.UrlbarSearchTermsPersistence.onSearchModeChanged(this.browserWindow);
    this.dispatchEvent(new Event("searchmodechanged"));
  }

  /**
   * Handles persisted search terms logic for the current browser. This manages
   * state and updates the UI accordingly.
   *
   * @param {object} options
   * @param {object} options.state
   *   The state object for the currently viewed browser.
   * @param {boolean} options.hideSearchTerms
   *   True if we must hide the search terms and instead show the page URL.
   * @param {boolean} options.dueToTabSwitch
   *   True if the browser was revealed again due to a tab switch.
   * @param {boolean} options.isSameDocument
   *   True if the page load was same document.
   * @param {nsIURI} [options.uri]
   *   The latest URI of the page.
   * @returns {boolean}
   *   Whether search terms should persist.
   */
  #handlePersistedSearchTerms({
    state,
    hideSearchTerms,
    dueToTabSwitch,
    isSameDocument,
    uri,
  }) {
    if (!lazy.UrlbarUtils.isPersistedSearchTermsEnabled()) {
      if (state.persist) {
        this.removeAttribute("persistsearchterms");
        delete state.persist;
      }
      return false;
    }

    // The first time the browser URI has been loaded to the input. If
    // persist is not defined, it is likely due to the tab being created in
    // the background or an existing tab moved to a new window and we have to
    // do the work for the first time.
    let firstView = (!isSameDocument && !dueToTabSwitch) || !state.persist;

    let cachedUriDidChange =
      state.persist?.originalURI &&
      (!this.browserWindow.gBrowser.selectedBrowser.originalURI ||
        !state.persist.originalURI.equals(
          this.browserWindow.gBrowser.selectedBrowser.originalURI
        ));

    // Capture the shouldPersist property if it exists before
    // setPersistenceState potentially modifies it.
    let wasPersisting = state.persist?.shouldPersist ?? false;

    if (firstView || cachedUriDidChange) {
      lazy.UrlbarSearchTermsPersistence.setPersistenceState(
        state,
        this.browserWindow.gBrowser.selectedBrowser.originalURI
      );
    }
    let shouldPersist =
      !hideSearchTerms &&
      lazy.UrlbarSearchTermsPersistence.shouldPersist(state, {
        isSameDocument,
        uri: uri ?? this.browserWindow.gBrowser.currentURI,
        userTypedValue: this.userTypedValue,
        firstView,
      });
    // When persisting, userTypedValue should have a value consistent with the
    // search terms to mimic a user typing the search terms.
    // When turning off persist, check if the userTypedValue needs to be
    // removed in order for the URL to return to the address bar. Single page
    // application SERPs will load secondary search pages (e.g. Maps, Images)
    // with the same document, which won't unset userTypedValue.
    if (shouldPersist) {
      this.userTypedValue = state.persist.searchTerms;
    } else if (wasPersisting && !shouldPersist) {
      this.userTypedValue = null;
    }

    state.persist.shouldPersist = shouldPersist;
    this.toggleAttribute("persistsearchterms", state.persist.shouldPersist);

    if (state.persist.shouldPersist && !isSameDocument) {
      Glean.urlbarPersistedsearchterms.viewCount.add(1);
    }

    return shouldPersist;
  }

  /**
   * Initializes the urlbar placeholder to the pre-saved engine name. We do this
   * via a preference, to avoid needing to synchronously init the search service.
   *
   * Note: If the preference doesn't exist, we don't do anything as the default
   * placeholder is a string which doesn't have the engine name.
   */
  #initPlaceholderFromPref() {
    if (!this.#navigationEnabled || this.controller.engineStore.failed) {
      return;
    }

    let engineName = UrlbarPrefs.get(
      "placeholderName" + (this.isPrivate ? ".private" : "")
    );
    if (engineName) {
      this._setPlaceholder(engineName);
    }
  }

  /**
   * Schedules initialization of the search engine store after first paint.
   *
   * On browser startup, engine store init will trigger initialization of
   * the search service. We don't want the search service to initialize too
   * early because of performance reasons, so we wait until after first paint.
   *
   * @returns {Promise<void>}
   *   Resolves when the search engine store has initialized successfully.
   *   Rejects if the search service (and hence the engine store) failed.
   */
  async #initEngineStoreAfterPaint() {
    if (document.readyState == "loading") {
      await new Promise(r =>
        document.addEventListener("DOMContentLoaded", r, { once: true })
      );
      await new Promise(r => window.requestIdleCallback(r));
    }

    await this.controller.engineStore.init();
  }

  /**
   * Asynchronously changes the urlbar placeholder and search mode switcher icon
   * to the name of the default engine according to the search engine store when
   * finished initializing.
   *
   * Since the placeholder was already initialized to the pre-saved engine
   * name by #initPlaceholderFromPref when this is called, the update is
   * delayed to avoid confusing the user. A placeholder that names an engine may
   * be naming a stale one, so it's updated right away.
   */
  async #deferUpdatePlaceholder() {
    if (this.sapName == "smartbar") {
      return;
    }

    if (!this.value) {
      if (this.inputField.dataset.l10nId == "urlbar-placeholder-with-name") {
        // The switcher icon has no pre-saved value that could be stale, so it
        // still waits for the listener below.
        this.updatePlaceholder();
      }

      // Only delay if requested, and we're not displaying text in the URL bar
      // currently.
      // Delays changing the URL Bar placeholder and Unified Search Button icon
      // until the user is not going to be seeing it, e.g. when there is a value
      // entered in the bar, or if there is a tab switch to a tab which has a url
      // loaded. We delay the update until the user is out of search mode since
      // an alternative placeholder is used in search mode.
      // A tab switch only hides the placeholder in the address bar, whose value
      // comes from the selected tab. Every other input keeps its own value, so
      // typing is its one cue that the user has looked away.
      let tabContainer = this.#isAddressbar
        ? this.browserWindow.gBrowser.tabContainer
        : null;
      let updateListener = () => {
        if (this.value && !this.searchMode) {
          // By the time the user has switched, they may have changed the engine
          // again, so we need to call this function again but with the
          // new engine name.
          // No need to await for this to finish, we're in a listener here anyway.
          this.searchModeSwitcher.updateSearchIcon().catch(console.error);
          this.updatePlaceholder();
          this.inputField.removeEventListener("input", updateListener);
          tabContainer?.removeEventListener("TabSelect", updateListener);
        }
      };

      this.inputField.addEventListener("input", updateListener);
      tabContainer?.addEventListener("TabSelect", updateListener);
    } else {
      this.updatePlaceholder();
    }
  }

  /**
   * Set Unified Search Button availability.
   *
   * @param {boolean} available If true Unified Search Button will be available.
   */
  setUnifiedSearchButtonAvailability(available) {
    available ||=
      this.isSearchbarSAP || UrlbarPrefs.get("unifiedSearchButton.always");
    const switcher = this.querySelector(".searchmode-switcher");
    switcher.toggleAttribute("offscreen", !available);
    if (available) {
      switcher.removeAttribute("aria-hidden");
    } else {
      switcher.setAttribute("aria-hidden", "true");
    }
    // Only the address bar keeps the availability per tab, to restore it on a
    // tab switch.
    if (this.#isAddressbar) {
      this.getBrowserState(
        this.browserWindow.gBrowser.selectedBrowser
      ).isUnifiedSearchButtonAvailable = available;
    }
  }

  /**
   * Updates the urlbar placeholder based on the default engine.
   */
  updatePlaceholder() {
    if (this.searchMode || !this.#navigationEnabled) {
      return;
    }

    let defaultEngine = this.controller.engineStore.default;
    // If the search engine store is not initialized (default is null),
    // we use the default placeholder. We only display the engine name
    // for config engines as we know they have short names.
    if (defaultEngine?.isConfigEngine) {
      this._setPlaceholder(defaultEngine.name);
    } else {
      // Display the default placeholder string.
      this._setPlaceholder(null);
    }
  }

  /**
   * Sets the URLBar placeholder to either something based on the engine name,
   * or the default placeholder.
   *
   * @param {?string} engineName
   * The name of the engine or null to use the default placeholder.
   */
  _setPlaceholder(engineName) {
    if (!this.#navigationEnabled) {
      document.l10n.setAttributes(this.inputField, "searchbar-input");
      return;
    }

    let l10nId;
    if (UrlbarShared.keywordEnabled(this.#sapName)) {
      l10nId = engineName
        ? "urlbar-placeholder-with-name"
        : "urlbar-placeholder";
    } else {
      l10nId = "urlbar-placeholder-keyword-disabled";
    }

    document.l10n.setAttributes(
      this.inputField,
      l10nId,
      l10nId == "urlbar-placeholder-with-name"
        ? { name: engineName }
        : undefined
    );
  }

  /**
   * Set when a click shouldn't select all the text: either the input already
   * had focus at mousedown, or the user typed after the focusing mousedown.
   */
  #preventClickSelectsAll = false;

  /**
   * Determines if we should select all the text in the Urlbar based on the
   *  Urlbar state, and whether the selection is empty.
   */
  #maybeSelectAll() {
    if (
      !this.#preventClickSelectsAll &&
      this.#compositionState != UrlbarShared.COMPOSITION.COMPOSING &&
      this.focused &&
      this.inputField.selectionStart == this.inputField.selectionEnd
    ) {
      this.select();
    }
  }

  // Event handlers below.

  _on_command(event) {
    // Something is executing a command, likely causing a focus change. This
    // should not be recorded as an abandonment. If the user is selecting a
    // result menu item or entering search mode from a one-off, then they are
    // in the same engagement and we should not discard.
    if (
      !event.target.classList.contains("urlbarView-result-menuitem") &&
      (!event.target.classList.contains("searchbar-engine-one-off-item") ||
        this.searchMode?.entry != "oneoff")
    ) {
      this.controller.engagementEvent.discard();
    }
  }

  _on_blur(event) {
    if (this.view.isResultMenuOpen()) {
      return;
    }

    logger().debug("Blur Event");
    // We cannot count every blur events after a missed engagement as abandoment
    // because the user may have clicked on some view element that executes
    // a command causing a focus change. For example opening preferences from
    // the oneoff settings button.
    // For now we detect that case by discarding the event on command, but we
    // may want to figure out a more robust way to detect abandonment.
    this.controller.engagementEvent.record(event, {
      searchString: this._lastSearchString,
      searchSource: this.getSearchSource(event),
      windowMode: this.windowMode,
    });

    this.focusedViaMousedown = false;
    this._handoffSession = undefined;
    this._isHandoffSession = false;
    this.removeAttribute("focused");
    // Reset this, so that it doesn't cause issues with different tests
    // when they focus and select the address bar.
    this.#preventClickSelectsAll = false;

    if (this._autofillPlaceholder && this.userTypedValue) {
      // If we were autofilling, remove the autofilled portion, by restoring
      // the value to the last typed one.
      this.value = this.userTypedValue;
    } else if (
      this.value == this._untrimmedValue &&
      !this.userTypedValue &&
      !this.focused
    ) {
      // If the value was untrimmed by _on_focus and didn't change, trim it.
      this.value = this._untrimmedValue;
    } else {
      // We're not updating the value, so just format it.
      this.formatValue();
    }

    this._resetSearchState();

    // In certain cases, like holding an override key and confirming an entry,
    // we don't key a keyup event for the override key, thus we make this
    // additional cleanup on blur.
    this._clearActionOverride();

    // The extension input sessions depends more on blur than on the fact we
    // actually cancel a running query, so we do it here.
    if (lazy?.ExtensionSearchHandler.hasActiveInputSession()) {
      lazy.ExtensionSearchHandler.handleInputCancelled();
    }

    // Respect the autohide preference for easier inspecting/debugging via
    // the browser toolbox.
    if (!UrlbarPrefs.get("ui.popup.disable_autohide")) {
      this.view.close();
    }

    // We may have hidden popup notifications, show them again if necessary.
    if (
      this.getAttribute("pageproxystate") != "valid" &&
      this.browserWindow?.UpdatePopupNotificationsVisibility
    ) {
      this.browserWindow.UpdatePopupNotificationsVisibility();
    }

    // If user move the focus to another component while pressing Enter key,
    // then keyup at that component, as we can't get the event, clear the promise.
    if (this._keyDownEnterDeferred) {
      this._keyDownEnterDeferred.resolve();
      this._keyDownEnterDeferred = null;
    }
    this._isKeyDownWithCtrl = false;
    this._isKeyDownWithMeta = false;
    this._isKeyDownWithMetaAndLeft = false;

    if (typeof ChromeUtils != "undefined") {
      Services.obs.notifyObservers(null, "urlbar-blur");
    }
  }

  _on_click(event) {
    switch (event.target) {
      case this.inputField:
      case this._inputContainer:
        this.#maybeSelectAll();
        this.#maybeUntrimUrl();
        break;

      case this._searchModeIndicatorClose:
        if (event.button != 2) {
          this.searchMode = null;
          if (this.view.oneOffSearchButtons) {
            this.view.oneOffSearchButtons.selectedButton = null;
          }
          if (this.view.isOpen) {
            this.startQuery({
              event,
            });
          }
        }
        break;

      case this._revertButton:
        this.handleRevert();
        this.select();
        break;

      case this.goButton:
        this.handleCommand(event);
        break;
    }
  }

  _on_auxclick(event) {
    switch (event.target) {
      case this.inputField:
      case this._inputContainer:
        this.#maybeSelectAll();
        this.#maybeUntrimUrl();
        break;

      case this.goButton:
        this.handleCommand(event);
        break;
    }
  }

  _on_contextmenu(event) {
    // Context menu opened via keyboard shortcut.
    if (!event.button) {
      return;
    }

    this.#maybeSelectAll();
  }

  _on_focus(event) {
    logger().debug("Focus Event");
    if (!this._hideFocus) {
      this.toggleAttribute("focused", true);
    }

    // If the value was trimmed, check whether we should untrim it.
    // This is necessary when a protocol was typed, but the whole url has
    // invalid parts, like the origin, then editing and confirming the trimmed
    // value would execute a search instead of visiting the typed url.
    // When "www." was trimmed we always untrim, so the user sees the full URL
    // and has to explicitly remove the prefix to load the bare domain.
    if (this.#isAddressbar && (this._protocolIsTrimmed || this._wwwIsTrimmed)) {
      let untrim = this._wwwIsTrimmed;
      if (!untrim) {
        let fixedDisplaySpec = UrlbarContentUtils.getFixupPrimitives(
          this.value,
          this.isPrivate
        )?.preferredURIDisplaySpec;
        if (fixedDisplaySpec) {
          let expectedDisplaySpec = UrlbarContentUtils.getDisplaySpec(
            this._untrimmedValue
          );
          if (expectedDisplaySpec == null) {
            untrim = true;
          } else if (
            UrlbarPrefs.getScotchBonnetPref("trimHttps") &&
            this._untrimmedValue.startsWith("https://")
          ) {
            untrim =
              fixedDisplaySpec.replace("http://", "https://") !=
              expectedDisplaySpec; // FIXME bug 1847723: Figure out a way to do this without manually messing with the fixed up URI.
          } else {
            untrim = fixedDisplaySpec != expectedDisplaySpec;
          }
        }
      }
      if (untrim) {
        this.setValue(this._untrimmedValue);
      }
    }

    if (this.focusedViaMousedown) {
      this.view.autoOpen({ event });
    } else {
      if (this._untrimOnFocusAfterKeydown) {
        // While the mousedown focus has more complex implications due to drag
        // and double-click select, we can untrim immediately when the urlbar is
        // focused by a keyboard shortcut.
        this.#maybeUntrimUrl({ ignoreSelection: true });
      }

      if (this.inputField.hasAttribute("refocused-by-panel")) {
        this.#maybeSelectAll();
      }
    }

    this._updateUrlTooltip();
    this.formatValue();

    // Hide popup notifications, to reduce visual noise.
    if (
      this.getAttribute("pageproxystate") != "valid" &&
      this.browserWindow?.UpdatePopupNotificationsVisibility
    ) {
      this.browserWindow.UpdatePopupNotificationsVisibility();
    }

    if (typeof ChromeUtils != "undefined") {
      // The observer service is chrome-only, and its one consumer here is the
      // macOS Touch Bar, which tracks a chrome window.
      Services.obs.notifyObservers(null, "urlbar-focus");
    }
  }

  _on_mouseover() {
    this._updateUrlTooltip();
  }

  _on_draggableregionleftmousedown() {
    if (!UrlbarPrefs.get("ui.popup.disable_autohide")) {
      this.view.close();
    }
  }

  _on_mousedown(event) {
    switch (event.currentTarget) {
      case this: {
        this._mousedownOnUrlbarDescendant = true;
        if (
          event.target != this.inputField &&
          event.target != this._inputContainer &&
          event.target != this.inputField.parentNode
        ) {
          break;
        }

        this.focusedViaMousedown = !this.focused;
        this.#preventClickSelectsAll = this.focused;

        // Keep the focus status, since the attribute may be changed
        // upon calling this.focus().
        const hasFocus = this.hasAttribute("focused");
        if (event.target != this.inputField) {
          this.focus();
        }

        // The rest of this case only cares about left clicks.
        if (event.button != 0) {
          break;
        }

        // Clear any previous selection unless we are focused, to ensure it
        // doesn't affect drag selection.
        if (this.focusedViaMousedown) {
          this.inputField.setSelectionRange(0, 0);
        }

        // Do not suppress the focus border if we are already focused. If we
        // did, we'd hide the focus border briefly then show it again if the
        // user has Top Sites disabled, creating a flashing effect.
        this.view.autoOpen({
          event,
          suppressFocusBorder: !hasFocus,
        });
        break;
      }
      case window:
        if (this._mousedownOnUrlbarDescendant) {
          this._mousedownOnUrlbarDescendant = false;
          break;
        }
        // Don't close the view when clicking on a tab; we may want to keep the
        // view open on tab switch, and the TabSelect event arrived earlier.
        if (event.target.closest?.("tab")) {
          break;
        }

        // Close the view when clicking on toolbars and other UI pieces that
        // might not automatically remove focus from the input.
        // Respect the autohide preference for easier inspecting/debugging via
        // the browser toolbox.
        if (!UrlbarPrefs.get("ui.popup.disable_autohide")) {
          if (this.view.isOpen && !this.hasAttribute("focused")) {
            // In this case, as blur event never happen from the inputField, we
            // record abandonment event explicitly.
            let blurEvent = new FocusEvent("blur", {
              relatedTarget: this.inputField,
            });
            this.controller.engagementEvent.record(blurEvent, {
              searchString: this._lastSearchString,
              searchSource: this.getSearchSource(blurEvent),
              windowMode: this.windowMode,
            });
          }

          this.view.close();
        }
        break;
    }
  }

  _on_input(event) {
    this.#preventClickSelectsAll = true;

    // A new input starts a fresh search; bump the epoch so a pending
    // deferred-Enter's async keyup won't reset this search's caret.
    this.#inputEpoch++;
    if (
      this._autofillPlaceholder &&
      this.value === this.userTypedValue &&
      (event.inputType === "deleteContentBackward" ||
        event.inputType === "deleteContentForward")
    ) {
      // Take a telemetry if user deleted whole autofilled value.
      this.parentController.recordAutofillDeletion();
    }

    if (
      UrlbarPrefs.get("autoFill.adaptiveHistory.enabled") &&
      event.inputType?.startsWith("deleteContent") &&
      !this.isPrivate &&
      this._autofillPlaceholder &&
      this.value === this.userTypedValue &&
      this._resultForCurrentValue?.payload?.url
    ) {
      this.parentController.recordAutofillBackspace(
        this._resultForCurrentValue.payload.url
      );
    }

    let value = this.value;
    this.valueIsTyped = true;
    this._untrimmedValue = value;
    this._protocolIsTrimmed = false;
    this._wwwIsTrimmed = false;
    this._resultForCurrentValue = null;

    this.userTypedValue = value;
    // Unset userSelectionBehavior because the user is modifying the search
    // string, thus there's no valid selection. This is also used by the view
    // to set "aria-activedescendant", thus it should never get stale.
    this.controller.userSelectionBehavior = "none";

    let compositionState = this.#compositionState;
    let compositionClosedPopup = this.#compositionClosedPopup;

    // Clear composition values if we're no more composing.
    if (this.#compositionState != UrlbarShared.COMPOSITION.COMPOSING) {
      this.#compositionState = UrlbarShared.COMPOSITION.NONE;
      this.#compositionClosedPopup = false;
    }

    if (compositionState == UrlbarShared.COMPOSITION.COMPOSING && event.data) {
      this.#compositionHadText = true;
    }

    this.toggleAttribute("usertyping", !!value);
    this.removeAttribute("actiontype");

    if (
      this.getAttribute("pageproxystate") == "valid" &&
      this.value != this._lastValidURLStr
    ) {
      this.setPageProxyState("invalid", true);
    }

    if (this.#isAddressbar) {
      // Search-terms persistence is an address bar feature.
      let state = this.getBrowserState(
        this.browserWindow.gBrowser.selectedBrowser
      );
      if (
        state.persist?.shouldPersist &&
        this.value !== state.persist.searchTerms
      ) {
        state.persist.shouldPersist = false;
        this.removeAttribute("persistsearchterms");
      }
    }

    if (this.view.isOpen) {
      // UrlbarView rolls up all popups when it opens, but we should
      // do the same for UrlbarInput when it's already open in case
      // a tab preview was opened
      this.view.maybeRollupPopups();

      if (!value && !UrlbarPrefs.get("suggest.topsites")) {
        this.view.clear();
        if (!this.searchMode || !this.view.oneOffSearchButtons?.hasView) {
          this.view.close();
          return;
        }
      }
    } else {
      this.view.clear();
    }

    this.view.removeAccessibleFocus();

    // During composition with an IME, the following events happen in order:
    // 1. a compositionstart event
    // 2. some input events
    // 3. a compositionend event
    // 4. an input event (some IMEs may skip this when step 3 has empty data)

    // We should do nothing during composition or if composition was canceled
    // and we didn't close the popup on composition start.
    if (
      !UrlbarPrefs.get("keepPanelOpenDuringImeComposition") &&
      (compositionState == UrlbarShared.COMPOSITION.COMPOSING ||
        (compositionState == UrlbarShared.COMPOSITION.CANCELED &&
          !compositionClosedPopup))
    ) {
      return;
    }

    // Don't autofill when the user is explicitly deleting content, pasting, or
    // undoing/redoing.
    const allowAutofill =
      (!UrlbarPrefs.get("keepPanelOpenDuringImeComposition") ||
        compositionState !== UrlbarShared.COMPOSITION.COMPOSING) &&
      !event.inputType?.startsWith("delete") &&
      !event.inputType?.startsWith("history") &&
      !UrlbarShared.isPasteEvent(event) &&
      this._maybeAutofillPlaceholder(value);

    this.startQuery({
      searchString: value,
      allowAutofill,
      resetSearchState: false,
      event,
    });
  }

  _on_selectionchange() {
    // Confirm placeholder as user text if it gets explicitly deselected. This
    // happens when the user wants to modify the autofilled text by either
    // clicking on it, or pressing HOME, END, RIGHT, …
    if (
      this._autofillPlaceholder &&
      this._autofillPlaceholder.value == this.value &&
      (this._autofillPlaceholder.selectionStart != this.selectionStart ||
        this._autofillPlaceholder.selectionEnd != this.selectionEnd)
    ) {
      this._autofillPlaceholder = null;
      this.userTypedValue = this.value;
    }
  }

  _on_select() {
    // On certain user input, AutoCopyListener::OnSelectionChange() updates
    // the primary selection with user-selected text (when supported).
    // Selection::NotifySelectionListeners() then dispatches a "select" event
    // under similar conditions via TextInputListener::OnSelectionChange().
    // This event is received here in order to replace the primary selection
    // from the editor with text having the adjustments of
    // _getSelectedValueForClipboard(), such as adding the scheme for the url.
    //
    // Other "select" events are also received, however, and must be excluded.
    if (
      // _suppressPrimaryAdjustment is set during select().  Don't update
      // the primary selection because that is not the intent of user input,
      // which may be new tab or urlbar focus.
      this._suppressPrimaryAdjustment ||
      // The check on isHandlingUserInput filters out async "select" events
      // from setSelectionRange(), which occur when autofill text is selected.
      !window.windowUtils.isHandlingUserInput ||
      !Services.clipboard.isClipboardTypeSupported(
        Services.clipboard.kSelectionClipboard
      )
    ) {
      return;
    }

    let val = this._getSelectedValueForClipboard();
    if (!val) {
      return;
    }

    lazy.ClipboardHelper.copyStringToClipboard(
      val,
      Services.clipboard.kSelectionClipboard,
      null,
      this.isPrivate
        ? Ci.nsIClipboardHelper.Sensitive
        : Ci.nsIClipboardHelper.NotSensitive
    );
  }

  _on_overflow(_event) {
    this._overflowing = true;
    this.updateTextOverflow();
  }

  _on_underflow(_event) {
    this._overflowing = false;
    this.updateTextOverflow();
    this._updateUrlTooltip();
  }

  _on_paste(event) {
    let originalPasteData = event.clipboardData.getData("text/plain");
    if (!originalPasteData) {
      return;
    }

    let oldValue = this.value;
    let oldStart = oldValue.substring(0, this.selectionStart);
    // If there is already non-whitespace content in the URL bar
    // preceding the pasted content, it's not necessary to check
    // protocols used by the pasted content:
    if (oldStart.trim()) {
      return;
    }
    let oldEnd = oldValue.substring(this.selectionEnd);

    const pasteData = UrlbarShared.sanitizeTextFromClipboard(
      originalPasteData,
      UrlbarContentUtils.getFixupPrimitives(originalPasteData, this.isPrivate)
    );

    if (originalPasteData != pasteData) {
      // Unfortunately we're not allowed to set the bits being pasted
      // so cancel this event:
      event.preventDefault();
      event.stopImmediatePropagation();

      const value = oldStart + pasteData + oldEnd;
      this.setValue(value, { valueIsTyped: true });
      this.userTypedValue = value;

      // Since we prevent the default paste event, we have to ensure the
      // pageproxystate is updated. The paste event replaces the actual current
      // page's URL with user-typed content, so we should set pageproxystate to
      // invalid.
      if (this.getAttribute("pageproxystate") == "valid") {
        this.setPageProxyState("invalid");
      }
      this.toggleAttribute("usertyping", !!this._untrimmedValue);

      // Fix up cursor/selection:
      let newCursorPos = oldStart.length + pasteData.length;
      this.inputField.setSelectionRange(newCursorPos, newCursorPos);

      this.startQuery({
        searchString: this.value,
        allowAutofill: false,
        resetSearchState: false,
        event,
      });
    }
  }

  /**
   * Generate a UrlbarQueryContext from the current context.
   *
   * @param {object} [options]
   *   Optional params
   * @param {boolean} [options.allowAutofill]
   *   Whether autofill is enabled.
   * @param {string} [options.searchString]
   *   The string being searched.
   * @param {object} [options.event]
   *   The event triggering the query.
   * @returns {UrlbarQueryContext}
   *   The queryContext object.
   */
  #makeQueryContext({
    allowAutofill = true,
    searchString = null,
    event = null,
  } = {}) {
    // When we are in actions search mode we can show more results so
    // increase the limit.
    let maxResults =
      this.searchMode?.source != UrlbarShared.RESULT_SOURCE.ACTIONS
        ? UrlbarPrefs.get("maxRichResults")
        : UNLIMITED_MAX_RESULTS;
    let options = {
      allowAutofill,
      isPrivate: this.isPrivate,
      sapName: this.sapName,
      maxResults,
      searchString,
      prohibitRemoteResults: !!(
        event &&
        UrlbarShared.isPasteEvent(event) &&
        UrlbarPrefs.get("maxCharsForSearchSuggestions") < event.data?.length
      ),
    };

    // Only add gBrowser-dependent properties if we're in a browser window.
    if (this.browserWindow?.gBrowser) {
      options.userContextId = parseInt(
        this.browserWindow.gBrowser.selectedBrowser?.getAttribute(
          "usercontextid"
        ) ?? "0"
      );
      options.tabGroup =
        this.browserWindow.gBrowser.selectedTab.group?.id ?? null;
      const currentPageSpec = this.browserWindow.gBrowser.currentURI?.spec;
      // currentURI is transiently null during a tab-drag docshell swap
      // (Bug 2025776); omit currentPage rather than passing "" which fails
      // UrlbarQueryContext validation.
      if (currentPageSpec) {
        options.currentPage = currentPageSpec;
      }
    }

    if (this.searchMode) {
      options.searchMode = this.searchMode;
      if (
        this.searchMode.source &&
        (!this.searchMode.engineName ||
          !UrlbarPrefs.get("unifiedSearchButton.historyInSearchMode"))
      ) {
        options.sources = [this.searchMode.source];
      }
    }

    return new UrlbarQueryContext(options);
  }

  _on_scrollend() {
    this.updateTextOverflow();
  }

  _on_TabSelect() {
    // TabSelect may be activated by a keyboard shortcut and cause the urlbar
    // to take focus, in this case we should not untrim.
    this._untrimOnFocusAfterKeydown = false;
    this._gotTabSelect = true;
    this._afterTabSelectAndFocusChange();
  }

  _on_TabClose(event) {
    lazy.handleBounceEventTrigger(event.target.linkedBrowser);

    if (this.view.isOpen) {
      // Refresh results when a tab is closed while the results view is open.
      // This prevents switch-to-tab results from remaining in the results
      // list after their tab is closed.
      this.startQuery();
    }
  }

  _on_beforeinput(event) {
    if (
      // Ignore char key input while processing enter key.
      (event.data && this._keyDownEnterDeferred) ||
      // Ignore space key while the result menu will be activated by space.
      (event.data == " " && this.view.shouldSpaceActivateSelectedElement())
    ) {
      event.preventDefault();
    }
  }

  _on_keydown(event) {
    // If the resultMenu is open then let them handle any key events.
    if (this.view.isResultMenuOpen()) {
      return;
    }

    if (event.currentTarget == window) {
      // It would be great if we could more easily detect the user focusing the
      // address bar through a keyboard shortcut, but F6 and TAB bypass are
      // not going through commands handling.
      // Also note we'll unset this on TabSelect, as it can focus the address
      // bar but we should not untrim in that case.
      this._untrimOnFocusAfterKeydown = !this.focused;
      return;
    }

    // Repeated KeyboardEvents can easily cause subtle bugs in this logic, if
    // not properly handled, so let's first handle things that should not be
    // evaluated repeatedly.
    if (!event.repeat) {
      this.#allTextSelectedOnKeyDown = this.#allTextSelected;

      if (event.keyCode === KeyEvent.DOM_VK_RETURN) {
        if (this._keyDownEnterDeferred) {
          this._keyDownEnterDeferred.reject();
        }
        this._keyDownEnterDeferred = Promise.withResolvers();
        this._keyDownEnterDeferred.inputEpoch = this.#inputEpoch;
        event._disableCanonization =
          UrlbarContentUtils.getPlatform() == "macosx"
            ? this._isKeyDownWithMeta
            : this._isKeyDownWithCtrl;
      }

      // Now set the keydown trackers for the current event, anything that wants
      // to check the previous events should have happened before this point.
      // The previously value is persisted until keyup, as we check if the
      // modifiers were down, even if other keys are pressed in the meanwhile.
      if (event.ctrlKey && event.keyCode != KeyEvent.DOM_VK_CONTROL) {
        this._isKeyDownWithCtrl = true;
      }
      if (event.metaKey && event.keyCode != KeyEvent.DOM_VK_META) {
        this._isKeyDownWithMeta = true;
      }
      // This is used in keyup, so it can be set every time.
      this._isKeyDownWithMetaAndLeft =
        this._isKeyDownWithMeta &&
        !event.shiftKey &&
        event.keyCode == KeyEvent.DOM_VK_LEFT;

      this._toggleActionOverride(event);
    } else if (event.keyCode == KeyEvent.DOM_VK_RETURN) {
      // Ignore repeated Enter, so holding it loads only once.
      event.preventDefault();
      return;
    }

    // Due to event deferring, it's possible preventDefault() won't be invoked
    // soon enough to actually prevent some of the default behaviors, thus we
    // have to handle the event "twice". This first immediate call passes false
    // as second argument so that handleKeyNavigation will only simulate the
    // event handling, without actually executing actions.
    // TODO (Bug 1541806): improve this handling, maybe by delaying actions
    // instead of events.
    if (this.eventBufferer.shouldDeferEvent(event)) {
      this.controller.handleKeyNavigation(event, false);
    }
    this.eventBufferer.maybeDeferEvent(event, () => {
      this.controller.handleKeyNavigation(event);
    });
  }

  _on_keyup(event) {
    if (event.currentTarget == window) {
      this._untrimOnFocusAfterKeydown = false;
      return;
    }

    if (this.#allTextSelectedOnKeyDown) {
      let moveCursorToStart = this.#isHomeKeyUpEvent(event);
      // We must set the selection immediately because:
      //  - on Mac Fn + Left is not handled properly as Home
      //  - untrim depends on text not being fully selected.
      if (moveCursorToStart) {
        this.selectionStart = this.selectionEnd = 0;
      }
      this.#maybeUntrimUrl({ moveCursorToStart });
    }
    if (event.keyCode === KeyEvent.DOM_VK_META) {
      this._isKeyDownWithMeta = false;
      this._isKeyDownWithMetaAndLeft = false;
    }
    if (event.keyCode === KeyEvent.DOM_VK_CONTROL) {
      this._isKeyDownWithCtrl = false;
    }

    this._toggleActionOverride(event);

    // Pressing Enter key while pressing Meta key, and next, even when releasing
    // Enter key before releasing Meta key, the keyup event is not fired.
    // Therefore, if Enter keydown is detecting, continue the post processing
    // for Enter key when any keyup event is detected. Keep this handler
    // synchronous -- only the rare deferred-Enter path awaits, so it runs as a
    // fire-and-forget task instead of allocating a microtask on every keyup.
    if (this._keyDownEnterDeferred && !this._finishingDeferredEnter) {
      this.#finishDeferredEnter();
    }
  }

  /**
   * Completes the deferred handling of an Enter keypress once a keyup arrives.
   * Split out of `_on_keyup` so the common keyup path stays synchronous.
   */
  async #finishDeferredEnter() {
    // Guard against a second keyup re-entering while the awaits below are
    // pending; released in the finally along with the deferred.
    this._finishingDeferredEnter = true;
    let keyDownEnterDeferred = this._keyDownEnterDeferred;
    try {
      if (keyDownEnterDeferred.loadedContent) {
        try {
          const browserId = /** @type {number} */ (
            await keyDownEnterDeferred.promise
          );
          // The parent focuses the loading browser if it's still selected,
          // since only it can reach the browser element and the chrome window.
          let { focused } = await this.parentController.focusBrowser(browserId);
          // focusBrowser resolves asynchronously; if the user began a fresh
          // search since this Enter (a later input bumped the epoch), its
          // caret must be left alone -- only keep the domain visible for our load.
          if (
            this.#isAddressbar &&
            focused &&
            keyDownEnterDeferred.inputEpoch === this.#inputEpoch
          ) {
            // Make sure the domain name stays visible for spoof protection and usability.
            this.inputField.setSelectionRange(0, 0);
          }
        } catch (ex) {
          // Not all the Enter actions in the urlbar will cause a navigation, then it
          // is normal for this to be rejected.
        }
      } else {
        // Discard the _keyDownEnterDeferred promise to receive any key inputs immediately.
        keyDownEnterDeferred.resolve();
      }
    } finally {
      // Only clear if a newer Enter keydown hasn't already replaced it, so we
      // don't overwrite that fresh deferred; then release the re-entry guard.
      if (this._keyDownEnterDeferred === keyDownEnterDeferred) {
        this._keyDownEnterDeferred = null;
      }
      this._finishingDeferredEnter = false;
    }
  }

  /**
   * Whether an IME composition is in progress. Mirrors chrome-only
   * `editor.composing`.
   *
   * @returns {boolean}
   */
  get isComposing() {
    return this.#compositionState == UrlbarShared.COMPOSITION.COMPOSING;
  }

  _on_compositionstart() {
    if (this.#compositionState == UrlbarShared.COMPOSITION.COMPOSING) {
      throw new Error("Trying to start a nested composition?");
    }
    this.#compositionState = UrlbarShared.COMPOSITION.COMPOSING;
    this.#compositionHadText = false;

    if (UrlbarPrefs.get("keepPanelOpenDuringImeComposition")) {
      return;
    }

    // Close the view. This will also stop searching.
    if (this.view.isOpen) {
      // We're closing the view, but we want to retain search mode if the
      // selected result was previewing it.
      if (this.searchMode) {
        // If we entered search mode with an empty string, clear userTypedValue,
        // otherwise confirmSearchMode may try to set it as value.
        // This can happen for example if we entered search mode typing a
        // a partial engine domain and selecting a tab-to-search result.
        if (!this.value) {
          this.userTypedValue = null;
        }
        this.confirmSearchMode();
      }
      this.#compositionClosedPopup = true;
      this.view.close();
    } else {
      this.#compositionClosedPopup = false;
    }
  }

  _on_compositionend(event) {
    if (this.#compositionState != UrlbarShared.COMPOSITION.COMPOSING) {
      throw new Error("Trying to stop a non existing composition?");
    }

    if (!UrlbarPrefs.get("keepPanelOpenDuringImeComposition")) {
      // Clear the selection and the cached result, since they refer to the
      // state before this composition. A new input even will be generated
      // after this.
      this.view.clearSelection();
      this._resultForCurrentValue = null;
    }

    // We can't yet retrieve the committed value from the editor, since it isn't
    // completely committed yet. We'll handle it at the next input event.
    this.#compositionState = event.data
      ? UrlbarShared.COMPOSITION.COMMIT
      : UrlbarShared.COMPOSITION.CANCELED;

    // Certain IMEs fire a spurious empty composition after each commit without
    // a subsequent input event. If this composition was empty throughout and it
    // closed the popup, reopen it directly since we can't rely on a following
    // input event.
    if (
      !event.data &&
      !this.#compositionHadText &&
      this.#compositionClosedPopup &&
      !UrlbarPrefs.get("keepPanelOpenDuringImeComposition")
    ) {
      this.#compositionState = UrlbarShared.COMPOSITION.NONE;
      this.#compositionClosedPopup = false;
      this.startQuery({ resetSearchState: false, event });
    }
  }

  _on_dragstart(event) {
    // Drag only if the gesture starts from the input field.
    let nodePosition = this.inputField.compareDocumentPosition(
      event.originalTarget
    );
    if (
      event.target != this.inputField &&
      !(nodePosition & Node.DOCUMENT_POSITION_CONTAINED_BY)
    ) {
      return;
    }

    // Don't cover potential drop targets on the toolbars or in content.
    this.view.close();

    // Only customize the drag data if the entire value is selected and it's a
    // loaded URI. Use default behavior otherwise.
    if (
      !this.#allTextSelected ||
      this.getAttribute("pageproxystate") != "valid"
    ) {
      return;
    }

    let uri = this.makeURIReadable(this.browserWindow.gBrowser.currentURI);
    let href = uri.displaySpec;
    let title = this.browserWindow.gBrowser.contentTitle || href;

    event.dataTransfer.setData("text/x-moz-url", `${href}\n${title}`);
    event.dataTransfer.setData("text/plain", href);
    event.dataTransfer.setData(
      "text/html",
      `<a href="${UrlbarShared.escapeHtmlEntities(href)}">${UrlbarShared.escapeHtmlEntities(title)}</a>`
    );
    event.dataTransfer.effectAllowed = "copyLink";
    event.stopPropagation();
  }

  /**
   * Handles dragover events for the input.
   *
   * @param {DragEvent} event
   */
  _on_dragover(event) {
    if (!this.#isAddressbar) {
      if (!event.dataTransfer.types.includes("text/plain")) {
        event.dataTransfer.dropEffect = "none";
      }
      return;
    }

    if (!Services.droppedLinkHandler.canDropLink(event, true)) {
      event.dataTransfer.dropEffect = "none";
    }
  }

  /**
   * Handles dropping of data on the input.
   *
   * @param {DragEvent} event
   */
  _on_drop(event) {
    if (!this.#isAddressbar) {
      // If we're a search bar, allow for getting search suggestions, changing
      // the search engine, or modifying the search term before submitting.
      // _on_input will start the query so we just clear the value to
      // make sure the dropped value replaces the old one.
      this.value = "";
      return;
    }

    // If we're an address bar, we try to detect whether the dropped item was
    // intended as a URL or a search and submit immediately.
    let droppedData = getDroppableData(event);
    if (!droppedData) {
      return;
    }
    let droppedString = UrlbarShared.isInstance(droppedData, URL)
      ? droppedData.href
      : droppedData;
    if (droppedString == this.browserWindow.gBrowser.currentURI.spec) {
      return;
    }

    this.value = droppedString;
    this.setPageProxyState("invalid");
    this.focus();

    // If we're an address bar, we automatically open the dropped address or
    // submit the dropped string to the search engine.
    let principal = Services.droppedLinkHandler.getTriggeringPrincipal(event);
    // To simplify tracking of events, register an initial event for event
    // telemetry, to replace the missing input event.
    let queryContext = this.#makeQueryContext({
      searchString: droppedString,
    });
    this.parentController.setLastQueryContextCache(queryContext);
    this.controller.engagementEvent.start(event, queryContext);
    this.handleNavigation({ triggeringPrincipal: principal });
    // For safety reasons, in the drop case we don't want to immediately show
    // the dropped value, instead we want to keep showing the current page
    // url until an onLocationChange happens.
    // See the handling in `setURI` for further details.
    this.userTypedValue = null;
    this.setURI({ dueToTabSwitch: true });
  }

  #allTextSelectedOnKeyDown = false;
  get #allTextSelected() {
    return this.selectionStart == 0 && this.selectionEnd == this.value.length;
  }

  /**
   * @param {string} value
   *   A untrimmed address bar input.
   * @returns {nsILoadInfo.SchemelessInputType}
   *   Returns `SchemelessInputTypeSchemeless` if the input doesn't start with a
   *   scheme relevant for schemeless HTTPS-First (http://, https:// and
   *   file://). Returns `SchemelessInputTypeSchemeful` if it does have a scheme.
   */
  #getSchemelessInput(value) {
    return ["http://", "https://", "file://"].every(
      scheme => !value.trim().startsWith(scheme)
    )
      ? SCHEMELESS_INPUT_SCHEMELESS
      : SCHEMELESS_INPUT_SCHEMEFUL;
  }

  get #isOpenedPageInBlankTargetLoading() {
    return (
      this.browserWindow.gBrowser.selectedBrowser.browsingContext.sessionHistory
        ?.count === 0 &&
      this.browserWindow.gBrowser.selectedBrowser.browsingContext
        .nonWebControlledLoadingURI
    );
  }

  // Search modes are per browser and are stored in the `searchModes` property of this map.
  // For a browser, search mode can be in preview mode, confirmed, or both.
  // Typically, search mode is entered in preview mode with a particular
  // source and is confirmed with the same source once a query starts.  It's
  // also possible for a confirmed search mode to be replaced with a preview
  // mode with a different source, and in those cases, we need to re-confirm
  // search mode when preview mode is exited. In addition, only confirmed
  // search modes should be restored across sessions. We therefore need to
  // keep track of both the current confirmed and preview modes, per browser.
  //
  // For each browser with a search mode, this maps the browser to an object
  // like this: { preview, confirmed }.  Both `preview` and `confirmed` are
  // search mode objects; see the setSearchMode documentation.  Either one may
  // be undefined if that particular mode is not active for the browser.

  /**
   * Tracks a state object per browser.
   *
   * @type {WeakMap<MozBrowser, object>}
   */
  #browserStates = new WeakMap();

  get #selectedText() {
    return this.editor.selection.toStringWithFormat(
      "text/plain",
      Ci.nsIDocumentEncoder.OutputPreformatted |
        Ci.nsIDocumentEncoder.OutputRaw,
      0
    );
  }

  /**
   * Check whether a key event has a similar effect as the Home key.
   *
   * @param {KeyboardEvent} event A Keyboard event
   * @returns {boolean} Whether the even will act like the Home key.
   */
  #isHomeKeyUpEvent(event) {
    let isMac = UrlbarContentUtils.getPlatform() === "macosx";
    return (
      // On MacOS this can be generated with Fn + Left.
      event.keyCode == KeyEvent.DOM_VK_HOME ||
      // Windows and Linux also support Ctrl + Left.
      (!isMac &&
        event.keyCode == KeyboardEvent.DOM_VK_LEFT &&
        event.ctrlKey &&
        !event.shiftKey) ||
      // MacOS supports other combos to move cursor at the start of the line.
      // For example Ctrl + A.
      (isMac &&
        event.keyCode == KeyboardEvent.DOM_VK_A &&
        event.ctrlKey &&
        !event.shiftKey) ||
      // And also Cmd (Meta) + Left.
      // Unfortunately on MacOS it's not possible to detect combos with the meta
      // key during the keyup event, due to how the OS handles events. Thus we
      // record the combo on keydown, and check for it here.
      (isMac &&
        event.keyCode == KeyEvent.DOM_VK_META &&
        this._isKeyDownWithMetaAndLeft)
    );
  }

  #canHandleAsBlankPage(spec) {
    return (
      this.browserWindow.isBlankPageURL(spec) || spec == "about:privatebrowsing"
    );
  }
}

/**
 * Tries to extract droppable data from a DND event.
 * This is mostly useful for the address bar since
 * it prioritizes extracting a URL.
 *
 * @param {DragEvent} event The DND event to examine.
 * @returns {URL|string|null}
 *          null if there's a security reason for which we should do nothing.
 *          A URL object if it's a value we can load.
 *          A string value otherwise.
 */
function getDroppableData(event) {
  let links;
  try {
    links = Services.droppedLinkHandler.dropLinks(event);
  } catch (ex) {
    // This is either an unexpected failure or a security exception; in either
    // case we should always return null.
    return null;
  }
  // The URL bar automatically handles inputs with newline characters,
  // so we can get away with treating text/x-moz-url flavours as text/plain.
  if (links[0]?.url) {
    event.preventDefault();
    let href = links[0].url;
    if (UrlbarShared.stripUnsafeProtocolOnPaste(href) != href) {
      // We may have stripped an unsafe protocol like javascript: and if so
      // there's no point in handling a partial drop.
      event.stopImmediatePropagation();
      return null;
    }

    // If this fails, checkLoadURIStrWithPrincipal would also fail,
    // as that's what it does with things that don't pass the IO
    // service's newURI constructor without fixup. It's conceivable we
    // may want to relax this check in the future (so e.g. www.foo.com
    // gets fixed up), but not right now.
    let url = URL.parse(href);
    if (url) {
      // If we succeed, try to pass security checks. If this works, return the
      // URL object. If the *security checks* fail, return null.
      try {
        let principal =
          Services.droppedLinkHandler.getTriggeringPrincipal(event);
        Services.scriptSecurityManager.checkLoadURIStrWithPrincipal(
          principal,
          url.href,
          Ci.nsIScriptSecurityManager.DISALLOW_INHERIT_PRINCIPAL
        );
        return url;
      } catch (ex) {
        return null;
      }
    }
    // We couldn't make a URL out of this. Continue on, and return text below.
  }
  // Handle as text.
  return event.dataTransfer.getData("text/plain");
}

/**
 * Decodes the given URI for displaying it in the address bar without losing
 * information, such that hitting Enter again will load the same URI.
 *
 * @param {string} displaySpec
 *   The URI's display spec.
 * @returns {string}
 *   The decoded URI
 */
function losslessDecodeDisplaySpec(displaySpec) {
  let scheme = displaySpec.slice(0, displaySpec.indexOf(":"));
  let value = displaySpec;

  // Try to decode as UTF-8 if there's no encoding sequence that we would break.
  if (!/%25(?:3B|2F|3F|3A|40|26|3D|2B|24|2C|23)/i.test(value)) {
    let decodeASCIIOnly = !["https", "http", "file", "ftp"].includes(scheme);
    if (decodeASCIIOnly) {
      // This only decodes ascii characters (hex) 20-7e, except 25 (%).
      // This avoids both cases stipulated below (%-related issues, and \r, \n
      // and \t, which would be %0d, %0a and %09, respectively) as well as any
      // non-US-ascii characters.
      value = value.replace(
        /%(2[0-4]|2[6-9a-f]|[3-6][0-9a-f]|7[0-9a-e])/g,
        decodeURI
      );
    } else {
      try {
        value = decodeURI(value)
          // decodeURI decodes %25 to %, which creates unintended encoding
          // sequences. Re-encode it, unless it's part of a sequence that
          // survived decodeURI, i.e. one for:
          // ';', '/', '?', ':', '@', '&', '=', '+', '$', ',', '#'
          // (RFC 3987 section 3.2)
          .replace(
            /%(?!3B|2F|3F|3A|40|26|3D|2B|24|2C|23)/gi,
            encodeURIComponent
          );
      } catch (e) {}
    }
  }

  // IMPORTANT: The following regular expressions are Unicode-aware due to /v.
  // Avoid matching high or low surrogate pairs directly, always work with
  // full Unicode scalar values.

  // Encode potentially invisible characters:
  //   U+0000-001F: C0/C1 control characters
  //   U+007F-009F: commands
  //   U+00A0, U+1680, U+2000-200A, U+202F, U+205F, U+3000: other spaces
  //   U+2028-2029: line and paragraph separators
  //   U+2800: braille empty pattern
  //   U+FFFC: object replacement character
  // Encode any trailing whitespace that may be part of a pasted URL, so that it
  // doesn't get eaten away by the location bar (bug 410726).
  // Encode all adjacent space chars (U+0020), to prevent spoofing attempts
  // where they would push part of the URL to overflow the location bar
  // (bug 1395508). A single space, or the last space if the are many, is
  // preserved to maintain readability of certain urls if it's not followed by a
  // control or separator character. We only do this for the common space,
  // because others may be eaten when copied to the clipboard,so it's safer to
  // preserve them encoded.
  value = value.replace(
    // eslint-disable-next-line no-control-regex
    /[[\p{Separator}--\u{0020}]\p{Control}\u{2800}\u{FFFC}]|\u{0020}(?=[\p{Other}\p{Separator}])|\s$/gv,
    encodeURIComponent
  );

  // Encode characters that are ignorable, can't be rendered usefully, or may
  // confuse users.
  //
  // Default ignorable characters; ZWNJ (U+200C) and ZWJ (U+200D) are excluded
  // per bug 582186:
  //   U+00AD, U+034F, U+06DD, U+070F, U+115F-1160, U+17B4, U+17B5, U+180B-180E,
  //   U+2060, U+FEFF, U+200B, U+2060-206F, U+3164, U+FE00-FE0F, U+FFA0,
  //   U+FFF0-FFFB, U+1D173-1D17A, U+E0000-E0FFF
  // Bidi control characters (RFC 3987 sections 3.2 and 4.1 paragraph 6):
  //   U+061C, U+200E, U+200F, U+202A-202E, U+2066-2069
  // Other format characters in the Cf category that are unlikely to be rendered
  // usefully:
  //   U+0600-0605, U+08E2, U+110BD, U+110CD, U+13430-13438, U+1BCA0-1BCA3
  // Mimicking UI parts:
  //   U+1F50F-1F513, U+1F6E1
  // Unassigned codepoints, sometimes shown as empty glyphs.
  value = value.replace(
    // eslint-disable-next-line no-misleading-character-class
    /[[\p{Format}--[\u{200C}\u{200D}]]\u{034F}\u{115F}\u{1160}\u{17B4}\u{17B5}\u{180B}-\u{180D}\u{3164}\u{FE00}-\u{FE0F}\u{FFA0}\u{FFF0}-\u{FFFB}\p{Unassigned}\p{Private_Use}\u{E0000}-\u{E0FFF}\u{1F50F}-\u{1F513}\u{1F6E1}]/gv,
    encodeURIComponent
  );
  return value;
}

/**
 * Decodes a URL for display, taking the Unicode form of a punycode host.
 *
 * @param {URL} url
 *   The URL to decode.
 * @returns {string}
 *   The decoded URL
 */
function losslessDecodeURL(url) {
  return losslessDecodeDisplaySpec(
    UrlbarContentUtils.getDisplaySpec(url.href) ?? url.href
  );
}

/**
 * Handles copy and cut commands for the urlbar.
 */
class CopyCutController {
  /**
   * @param {UrlbarInputBase} urlbar
   *   The UrlbarInputBase instance to use this controller for.
   */
  constructor(urlbar) {
    this.urlbar = urlbar;
  }

  /**
   * @param {string} command
   *   The name of the command to handle.
   */
  doCommand(command) {
    let urlbar = this.urlbar;
    let val = urlbar._getSelectedValueForClipboard();
    if (!val) {
      return;
    }

    if (command == "cmd_cut" && this.isCommandEnabled(command)) {
      let start = urlbar.selectionStart;
      let end = urlbar.selectionEnd;
      urlbar.inputField.value =
        urlbar.inputField.value.substring(0, start) +
        urlbar.inputField.value.substring(end);
      urlbar.inputField.setSelectionRange(start, start);

      let event = new UIEvent("input", {
        bubbles: true,
        cancelable: false,
        view: window,
        detail: 0,
      });
      urlbar.inputField.dispatchEvent(event);
    }

    lazy.ClipboardHelper.copyString(
      val,
      null,
      urlbar.isPrivate
        ? Ci.nsIClipboardHelper.Sensitive
        : Ci.nsIClipboardHelper.NotSensitive
    );
  }

  /**
   * @param {string} command
   *   The name of the command to check.
   * @returns {boolean}
   *   Whether the command is handled by this controller.
   */
  supportsCommand(command) {
    switch (command) {
      case "cmd_copy":
      case "cmd_cut":
        return true;
    }
    return false;
  }

  /**
   * @param {string} command
   *   The name of the command to check.
   * @returns {boolean}
   *   Whether the command should be enabled.
   */
  isCommandEnabled(command) {
    return (
      this.supportsCommand(command) &&
      (command != "cmd_cut" || !this.urlbar.readOnly) &&
      this.urlbar.selectionStart < this.urlbar.selectionEnd
    );
  }

  onEvent() {}
}

/**
 * Manages the Add Search Engine contextual menu entries.
 *
 * Note: setEnginesFromBrowser must be invoked from the outside when the
 *       page provided engines list changes.
 *       refreshContextMenu must be invoked when the context menu is opened.
 */
class AddSearchEngineHelper {
  /**
   * The one-off search buttons in the urlbar.
   * This will be null for the search bar.
   *
   * @type {?UrlbarSearchOneOffs}
   */
  shortcutButtons;

  /**
   * The engines the page offers, empty until OpenSearchManager reports any.
   *
   * @type {object[]}
   */
  engines = [];

  /**
   * @param {UrlbarInputBase} input The parent UrlbarInputBase.
   */
  constructor(input) {
    this.input = input;
    this.shortcutButtons = input.view.oneOffSearchButtons;
  }

  /**
   * If there's more than this number of engines, the context menu offers
   * them in a submenu.
   *
   * @returns {number}
   */
  get maxInlineEngines() {
    return SearchModeSwitcher.MAX_OPENSEARCH_ENGINES;
  }

  /**
   * Invoked by OpenSearchManager when the list of available engines changes.
   *
   * @param {object} browser The current browser.
   * @param {object} engines The updated list of available engines.
   */
  setEnginesFromBrowser(browser, engines) {
    this.browsingContext = browser.browsingContext;
    // Make a copy of the array for state comparison.
    engines = engines.slice();
    if (!this._sameEngines(this.engines, engines)) {
      this.engines = engines;
      this.shortcutButtons?.updateWebEngines();
    }
  }

  _sameEngines(engines1, engines2) {
    if (engines1?.length != engines2?.length) {
      return false;
    }
    return UrlbarShared.deepEqual(
      engines1.map(e => e.title),
      engines2.map(e => e.title)
    );
  }

  _createMenuitem(engine, index) {
    let elt = document.createXULElement("menuitem");
    elt.setAttribute("anonid", `add-engine-${index}`);
    elt.classList.add("menuitem-iconic");
    elt.classList.add("context-menu-add-engine");
    document.l10n.setAttributes(elt, "search-one-offs-add-engine", {
      engineName: engine.title,
    });
    elt.setAttribute("uri", engine.uri);
    if (engine.icon) {
      elt.setAttribute("image", engine.icon);
    } else {
      elt.removeAttribute("image");
    }
    elt.addEventListener("command", this._onCommand.bind(this));
    return elt;
  }

  _createMenu(engine) {
    let elt = document.createXULElement("menu");
    elt.setAttribute("anonid", "add-engine-menu");
    elt.classList.add("menu-iconic");
    elt.classList.add("context-menu-add-engine");
    document.l10n.setAttributes(elt, "search-one-offs-add-engine-menu");
    if (engine.icon) {
      elt.setAttribute("image", ChromeUtils.encodeURIForSrcset(engine.icon));
    }
    let popup = document.createXULElement("menupopup");
    elt.appendChild(popup);
    return elt;
  }

  /**
   * The items this helper has put in the context menu, after its separator.
   *
   * @type {Element[]}
   */
  #contextItems = [];

  /**
   * Creates the separator the engine items go after. The context menu is shared
   * with other inputs, so it's owned as part of this input's item set rather
   * than looked up in the menu.
   *
   * @returns {Element}
   *   The separator.
   */
  createContextSeparator() {
    this.contextSeparator = document.createXULElement("menuseparator");
    this.contextSeparator.setAttribute("anonid", "add-engine-separator");
    this.contextSeparator.classList.add("menuseparator-add-engine");
    this.contextSeparator.collapsed = true;
    return this.contextSeparator;
  }

  /**
   * Rebuilds the engine items.
   *
   * @returns {Element[]}
   *   The separator and the items, for the item set to claim.
   */
  refreshContextMenu() {
    let engines = this.engines;

    this.contextSeparator.collapsed = !engines.length;
    /** @type {Element} */
    let curElt = this.contextSeparator;
    // Remove the previous items, if any.
    for (let elt of this.#contextItems) {
      elt.remove();
    }
    this.#contextItems = [];

    // If the page provides too many engines, we only show a single menu entry
    // with engines in a submenu.
    if (engines.length > this.maxInlineEngines) {
      // Set the menu button's image to the image of the first engine.  The
      // offered engines may have differing images, so there's no perfect
      // choice here.
      let elt = this._createMenu(engines[0]);
      this.contextSeparator.insertAdjacentElement("afterend", elt);
      this.#contextItems.push(elt);
      curElt = elt.lastElementChild;
    }

    // Insert the engines, either in the contextual menu or the sub menu.
    for (let i = 0; i < engines.length; ++i) {
      let elt = this._createMenuitem(engines[i], i);
      if (curElt.localName == "menupopup") {
        curElt.appendChild(elt);
      } else {
        curElt.insertAdjacentElement("afterend", elt);
        this.#contextItems.push(elt);
      }
      curElt = elt;
    }

    return [this.contextSeparator, ...this.#contextItems];
  }

  async _onCommand(event) {
    let added = await lazy.SearchUIUtils.addOpenSearchEngine(
      event.target.getAttribute("uri"),
      event.target.getAttribute("image"),
      this.browsingContext
    ).catch(console.error);
    if (added) {
      // Remove the offered engine from the list. The browser updated the
      // engines list at this point, so we just have to refresh the menu.)
      this.refreshContextMenu();
    }
  }
}
