/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

const { XPCOMUtils } = ChromeUtils.importESModule(
  "resource://gre/modules/XPCOMUtils.sys.mjs"
);

const { AppConstants } = ChromeUtils.importESModule(
  "resource://gre/modules/AppConstants.sys.mjs"
);

/**
 * @import { UrlbarSearchOneOffs } from "moz-src:///browser/components/urlbar/UrlbarSearchOneOffs.sys.mjs"
 * @import { SearchEngine } from "moz-src:///toolkit/components/search/SearchEngine.sys.mjs"
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
 * @property {"origin" | "url" | "adaptive_url" | "adaptive_origin"} type
 *   The autofill type.
 * @property {string} adaptiveHistoryInput
 *   If the type is "adaptive_url" or "adaptive_origin", this is the matching
 *   input value from adaptive history.
 * @property {number} selectionStart
 *   The selectionStart at the time of autofill.
 * @property {number} selectionEnd
 *   The selectionEnd at the time of autofill.
 * @property {string} [untrimmedValue]
 *   The untrimmed value including the protocol.
 */

const lazy = XPCOMUtils.declareLazy({
  AIWindow:
    "moz-src:///browser/components/aiwindow/ui/modules/AIWindow.sys.mjs",
  ASRouter: "resource:///modules/asrouter/ASRouter.sys.mjs",
  BrowserSearchTelemetry:
    "moz-src:///browser/components/search/BrowserSearchTelemetry.sys.mjs",
  BrowserUIUtils: "resource:///modules/BrowserUIUtils.sys.mjs",
  BrowserUtils: "resource://gre/modules/BrowserUtils.sys.mjs",
  CustomizableUI:
    "moz-src:///browser/components/customizableui/CustomizableUI.sys.mjs",
  ExtensionSearchHandler:
    "resource://gre/modules/ExtensionSearchHandler.sys.mjs",
  ExtensionUtils: "resource://gre/modules/ExtensionUtils.sys.mjs",
  ObjectUtils: "resource://gre/modules/ObjectUtils.sys.mjs",
  PartnerLinkAttribution: "resource:///modules/PartnerLinkAttribution.sys.mjs",
  PrivateBrowsingUtils: "resource://gre/modules/PrivateBrowsingUtils.sys.mjs",
  ReaderMode: "moz-src:///toolkit/components/reader/ReaderMode.sys.mjs",
  SearchService: "moz-src:///toolkit/components/search/SearchService.sys.mjs",
  SearchModeSwitcher:
    "moz-src:///browser/components/urlbar/SearchModeSwitcher.sys.mjs",
  SharingUtils: "resource:///modules/SharingUtils.sys.mjs",
  SearchUIUtils: "moz-src:///browser/components/search/SearchUIUtils.sys.mjs",
  SearchUtils: "moz-src:///toolkit/components/search/SearchUtils.sys.mjs",
  UrlbarController:
    "moz-src:///browser/components/urlbar/UrlbarController.sys.mjs",
  UrlbarEventBufferer:
    "moz-src:///browser/components/urlbar/UrlbarEventBufferer.sys.mjs",
  UrlbarPrefs: "moz-src:///browser/components/urlbar/UrlbarPrefs.sys.mjs",
  UrlbarQueryContext:
    "moz-src:///browser/components/urlbar/UrlbarUtils.sys.mjs",
  UrlbarProviderGlobalActions:
    "moz-src:///browser/components/urlbar/UrlbarProviderGlobalActions.sys.mjs",
  UrlbarProviderOpenTabs:
    "moz-src:///browser/components/urlbar/UrlbarProviderOpenTabs.sys.mjs",
  UrlbarSearchUtils:
    "moz-src:///browser/components/urlbar/UrlbarSearchUtils.sys.mjs",
  UrlbarTokenizer:
    "moz-src:///browser/components/urlbar/UrlbarTokenizer.sys.mjs",
  UrlbarUtils: "moz-src:///browser/components/urlbar/UrlbarUtils.sys.mjs",
  UrlbarValueFormatter:
    "moz-src:///browser/components/urlbar/UrlbarValueFormatter.sys.mjs",
  UrlbarView: "moz-src:///browser/components/urlbar/UrlbarView.sys.mjs",
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
  QUERY_STRIPPING_STRIP_ON_SHARE: {
    pref: "privacy.query_stripping.strip_on_share.enabled",
    default: false,
  },
  logger: () => lazy.UrlbarUtils.getLogger({ prefix: "Input" }),
});

const UNLIMITED_MAX_RESULTS = 99;

let getBoundsWithoutFlushing = element =>
  element.documentGlobal.windowUtils.getBoundsWithoutFlushing(element);
let px = number => number.toFixed(2) + "px";

/**
 * Implements the text input part of the address bar UI.
 */
export class UrlbarInput extends HTMLElement {
  static get #markup() {
    return `
      <html:div class="urlbar-background"/>
      <html:div class="urlbar-input-container"
            pageproxystate="invalid">
        <html:moz-urlbar-slot name="remote-control-box" />

        <html:moz-button class="searchmode-switcher chromeclass-toolbar-additional"
                         iconsrc="chrome://global/skin/icons/search-glass.svg"
                         title="More options"
                         aria-label="More options"
                         data-l10n-id="urlbar-searchmode-default2"
                         tabindex="-1"
                         role="combobox">
          <!-- This span has no purpose other than making the moz-button think
               it contains text even when searchmode-switcher-title is hidden. -->
          <html:span class="urlbar-visually-hidden" aria-hidden="true">a</html:span>
          <html:span class="searchmode-switcher-content">
            <html:img class="searchmode-switcher-dropmarker"
                      data-l10n-id="urlbar-searchmode-dropmarker2"
                      draggable="false" />
            <html:span class="searchmode-switcher-title" />
            <html:button class="searchmode-switcher-close toolbarbutton-icon close-button"
                         data-l10n-id="urlbar-searchmode-exit-button2"
                         tabindex="-1"
                         keyNav="false" />
          </html:span>
        </html:moz-button>
        <!-- In XUL windows, this will be wrapped in a panel with class="searchmode-switcher-panel". -->
        <html:panel-list class="searchmode-switcher-panel-list">
          <html:div class="searchmode-switcher-panel-description" role="heading" />
${
  Services.prefs.getBoolPref("browser.nova.enabled", false)
    ? '<html:hr class="searchmode-switcher-panel-installed-engine-separator"/><html:hr class="searchmode-switcher-panel-footer-separator"/>'
    : '<html:hr/><html:hr class="searchmode-switcher-panel-installed-engine-separator searchmode-switcher-panel-footer-separator"/>'
}
        </html:panel-list>

        <html:moz-urlbar-slot name="site-info" />
        <moz-input-box tooltip="aHTMLTooltip"
                       class="urlbar-input-box"
                       flex="1">
          <!-- In the addressbar, there will be an input with id="urlbar-scheme" here. -->
          <html:input class="urlbar-input textbox-input"
                      role="combobox"
                      aria-autocomplete="both"
                      inputmode="mozAwesomebar"
                      data-l10n-id="urlbar-placeholder"/>
        </moz-input-box>
        <html:moz-urlbar-slot name="revert-button" />
        <html:img class="urlbar-icon urlbar-go-button"
               role="button"
               keyNav="false"
               data-l10n-id="urlbar-go-button2"/>
        <html:moz-urlbar-slot name="page-actions" />
      </html:div>
      <html:div class="urlbarView"
            role="group"
            tooltip="aHTMLTooltip">
        <html:div class="urlbarView-body-outer">
          <html:div class="urlbarView-body-inner">
            <html:div class="urlbarView-results"
                      role="listbox"/>
          </html:div>
        </html:div>
        <menupopup class="urlbarView-result-menu"
                   consumeoutsideclicks="false"/>
        <html:moz-urlbar-slot name="search-one-offs" />
   </html:div>`;
  }

  /** @type {DocumentFragment} */
  static get fragment() {
    if (!UrlbarInput.#fragment) {
      UrlbarInput.#fragment = window.MozXULElement.parseXULToFragment(
        UrlbarInput.#markup
      );
    }
    // @ts-ignore
    return document.importNode(UrlbarInput.#fragment, true);
  }

  /**
   * @type {DocumentFragment=}
   *
   * The cached fragment.
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
   * Whether expanding is allowed. Requires a parent
   * toolbar, and us not being read-only.
   */
  #allowBreakout = false;
  #gBrowserListenersAdded = false;
  #breakoutBlockerCount = 0;
  #isAddressbar = false;
  /**
   * The search access point name of the UrlbarInput for use with telemetry or
   * logging, e.g. `urlbar`, `searchbar`.
   *
   * @type {"searchbar"|"smartbar"|"urlbar"}
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
  #compositionState = lazy.UrlbarUtils.COMPOSITION.NONE;
  #compositionClosedPopup = false;
  #compositionHadText = false;

  valueIsTyped = false;

  // Properties accessed in tests.
  lastQueryContextPromise = Promise.resolve();

  /** @type {AutofillPlaceholder|null} */
  _autofillPlaceholder = null;
  _autofillBackspaceState = null;

  _resultForCurrentValue = null;
  _untrimmedValue = "";
  _enableAutofillPlaceholder = true;

  constructor() {
    super();

    this.window = this.documentGlobal;
    this.document = this.window.document;
    this.isPrivate = lazy.PrivateBrowsingUtils.isWindowPrivate(this.window);

    lazy.UrlbarPrefs.addObserver(this);
    window.addEventListener("unload", () => {
      // Stop listening to pref changes to make sure we don't init the new
      // searchbar in closed windows that have not been gc'd yet.
      lazy.UrlbarPrefs.removeObserver(this);
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
   * Initialization that happens once on the first connect.
   */
  #init() {
    this.#sapName = /** @type {"searchbar"|"smartbar"|"urlbar"} */ (
      this.getAttribute("sap-name")
    );
    this.#isAddressbar = this.#sapName == "urlbar";

    // This listener must be added before connecting the fragment
    // because the event could fire while or after connecting it.
    this.addEventListener(
      "moz-input-box-rebuilt",
      this.#onContextMenuRebuilt.bind(this)
    );

    this.appendChild(UrlbarInput.fragment);

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

    this.panel = this.querySelector(".urlbarView");
    this._inputContainer = this.querySelector(".urlbar-input-container");
    this.inputField = /** @type {HTMLInputElement} */ (
      this.querySelector(".urlbar-input")
    );

    let resultListboxId = this.#sapName + "-results";
    this.querySelector(".urlbarView-results").id = resultListboxId;
    this.inputField.setAttribute("aria-controls", resultListboxId);

    if (this.#isAddressbar) {
      this.inputField.id = "urlbar-input";

      let schemeField = document.createElement("input");
      schemeField.id = "urlbar-scheme";
      schemeField.required = true;
      this.inputField.before(schemeField);
    }
    if (this.#sapName == "searchbar") {
      // This adds a native clear button.
      this.inputField.setAttribute("type", "search");
    }

    this.controller = new lazy.UrlbarController({ input: this });
    this.view = new lazy.UrlbarView(this);
    this.searchModeSwitcher = new lazy.SearchModeSwitcher(this);

    let searchModeSwitcherDescription = this.querySelector(
      ".searchmode-switcher-panel-description"
    );

    searchModeSwitcherDescription.setAttribute(
      "data-l10n-id",
      this.#isAddressbar &&
        !Services.prefs.getBoolPref("browser.nova.enabled", false)
        ? "urlbar-searchmode-popup-one-off-header"
        : "urlbar-searchmode-popup-header"
    );

    // The event bufferer can be used to defer events that may affect users
    // muscle memory; for example quickly pressing DOWN+ENTER should end up
    // on a predictable result, regardless of the search status. The event
    // bufferer will invoke the handling code at the right time.
    this.eventBufferer = new lazy.UrlbarEventBufferer(this);

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
  }

  connectedCallback() {
    if (
      this.getAttribute("sap-name") == "searchbar" &&
      !lazy.UrlbarPrefs.get("browser.search.widget.new")
    ) {
      return;
    }

    this.#connectedCallback();
  }

  #connectedCallback() {
    if (!this.controller) {
      this.#init();
    }

    if (this.inOverflowPanel && this.view.isOpen) {
      this.view.close();
    }

    // Don't attach event listeners if the urlbar is readonly.
    if (this.readOnly) {
      this.#stopBreakout();
      this.#allowBreakout = false;
      // Focused won't be updated so remove it to avoid it becoming stale.
      this.removeAttribute("focused");
      return;
    }
    this.toggleAttribute("focused", this.focused);

    if (
      this.sapName == "searchbar" &&
      !document.documentElement.hasAttribute("customizing")
    ) {
      // Ensure we get persisted widths back, if we've been in the palette:
      let storedWidth = Services.xulStore.getValue(
        document.documentURI,
        this.parentElement.id,
        "width"
      );
      if (storedWidth) {
        this.parentElement.setAttribute("width", storedWidth);
        /** @type {XULElement} */ (this.parentElement).style.width =
          storedWidth + "px";
      }
    }

    this._initCopyCutController();

    for (let event of UrlbarInput.#inputFieldEvents) {
      this.inputField.addEventListener(event, this);
    }

    // These are on the window to detect focusing shortcuts like F6.
    this.window.addEventListener("keydown", this);
    this.window.addEventListener("keyup", this);

    this.window.addEventListener("mousedown", this);
    if (AppConstants.platform == "win") {
      this.window.addEventListener("draggableregionleftmousedown", this);
    }
    this.addEventListener("mousedown", this);

    // This listener handles clicks from our children too, included the search mode
    // indicator close button.
    this._inputContainer.addEventListener("click", this);
    this._inputContainer.addEventListener("auxclick", this);

    // This is used to detect commands launched from the panel, to avoid
    // recording abandonment events when the command causes a blur event.
    this.view.panel.addEventListener("command", this, true);

    this.window.addEventListener("toolbarvisibilitychange", this);
    let menuToolbar = this.window.document.getElementById("toolbar-menubar");
    if (menuToolbar) {
      menuToolbar.addEventListener("DOMMenuBarInactive", this);
      menuToolbar.addEventListener("DOMMenuBarActive", this);
    }
    this.window.addEventListener("uidensitychanged", this);

    if (this.window.gBrowser) {
      // On startup, this will be called again by browser-init.js
      // once gBrowser has been initialized.
      this.addGBrowserListeners();
    }

    // If gBrowser or the search service is not initialized yet,
    // the placeholder and icon will be updated in delayedStartupInit.
    if (
      Cu.isESModuleLoaded(
        "moz-src:///toolkit/components/search/SearchService.sys.mjs"
      ) &&
      lazy.SearchService.isInitialized
    ) {
      this.searchModeSwitcher.updateSearchIcon();
      this._updatePlaceholderFromDefaultEngine();
    }

    this.#allowBreakout =
      !!this.closest("toolbar") &&
      !document.documentElement.hasAttribute("customizing");
    if (this.#allowBreakout) {
      // TODO(emilio): This could use CSS anchor positioning rather than this
      // ResizeObserver, eventually.
      this._resizeObserver = new this.window.ResizeObserver(([entry]) => {
        this.style.setProperty(
          "--urlbar-width",
          px(entry.borderBoxSize[0].inlineSize)
        );
      });
      this._resizeObserver.observe(this.parentNode);

      this.#updateLayoutBreakout();
    } else {
      this.#stopBreakout();
    }

    this._addObservers();
  }

  disconnectedCallback() {
    if (
      this.getAttribute("sap-name") == "searchbar" &&
      !lazy.UrlbarPrefs.get("browser.search.widget.new")
    ) {
      return;
    }

    this.#disconnectedCallback();
  }

  #disconnectedCallback() {
    if (this.sapName == "searchbar") {
      // Exit search mode to make sure it doesn't become stale while the
      // searchbar is invisible. Otherwise, the engine might get deleted
      // but we don't notice because the search service observer is inactive.
      this.searchMode = null;
    }

    if (this._copyCutController) {
      this.inputField.controllers.removeController(this._copyCutController);
      delete this._copyCutController;
    }

    for (let event of UrlbarInput.#inputFieldEvents) {
      this.inputField.removeEventListener(event, this);
    }

    // These are on the window to detect focusing shortcuts like F6.
    this.window.removeEventListener("keydown", this);
    this.window.removeEventListener("keyup", this);

    this.window.removeEventListener("mousedown", this);
    if (AppConstants.platform == "win") {
      this.window.removeEventListener("draggableregionleftmousedown", this);
    }
    this.removeEventListener("mousedown", this);

    // This listener handles clicks from our children too, included the search mode
    // indicator close button.
    this._inputContainer.removeEventListener("click", this);
    this._inputContainer.removeEventListener("auxclick", this);

    // This is used to detect commands launched from the panel, to avoid
    // recording abandonment events when the command causes a blur event.
    this.view.panel.removeEventListener("command", this, true);

    this.window.removeEventListener("toolbarvisibilitychange", this);
    let menuToolbar = this.window.document.getElementById("toolbar-menubar");
    if (menuToolbar) {
      menuToolbar.removeEventListener("DOMMenuBarInactive", this);
      menuToolbar.removeEventListener("DOMMenuBarActive", this);
    }
    this.window.removeEventListener("uidensitychanged", this);

    if (this.#gBrowserListenersAdded) {
      this.window.gBrowser.tabContainer.removeEventListener("TabSelect", this);
      this.window.gBrowser.tabContainer.removeEventListener("TabClose", this);
      this.window.gBrowser.removeTabsProgressListener(this);
      this.#gBrowserListenersAdded = false;
    }

    this._resizeObserver?.disconnect();

    this._removeObservers();
  }

  /**
   * This method is used to attach new context menu options to the urlbar
   * context menu, i.e. the context menu of the moz-input-box.
   * It is called when the moz-input-box rebuilds its context menu.
   *
   * Note that it might be called before #init has finished.
   */
  #onContextMenuRebuilt() {
    this._initStripOnShare();
    this._initPasteAndGo();
    if (this.#isAddressbar && AppConstants.platform == "macosx") {
      this.#initShareURL();
    }
    if (this.sapName == "searchbar") {
      this.#initClearSearchHistory();
    }
  }

  addGBrowserListeners() {
    if (this.window.gBrowser && !this.#gBrowserListenersAdded) {
      this.window.gBrowser.tabContainer.addEventListener("TabSelect", this);
      this.window.gBrowser.tabContainer.addEventListener("TabClose", this);
      this.window.gBrowser.addTabsProgressListener(this);
      this.#gBrowserListenersAdded = true;
    }
  }

  #lazy = XPCOMUtils.declareLazy({
    valueFormatter: () => new lazy.UrlbarValueFormatter(this),
    addSearchEngineHelper: () => new AddSearchEngineHelper(this),
  });

  /**
   * Manages the Add Search Engine contextual menu entries.
   */
  get addSearchEngineHelper() {
    return this.#lazy.addSearchEngineHelper;
  }

  get sapName() {
    return this.#sapName;
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
    return lazy.AIWindow.isAIWindowActive(this.window)
      ? "smartwindow"
      : "classic";
  }

  blur() {
    this.inputField.blur();
  }

  set readOnly(val) {
    if (val != this.inputField.readOnly) {
      this.inputField.readOnly = val;
      if (this.isConnected) {
        this.#disconnectedCallback();
        this.#connectedCallback();
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
        this._updatePlaceholderFromDefaultEngine().catch(e =>
          // This can happen if the search service failed.
          console.warn("Falied to update urlbar placeholder:", e)
        );
        break;
      case "browser.search.widget.new": {
        if (this.getAttribute("sap-name") == "searchbar" && this.isConnected) {
          if (lazy.UrlbarPrefs.get("browser.search.widget.new")) {
            // The connectedCallback was skipped. Init now.
            this.#connectedCallback();
          } else {
            // Uninit now, the disconnectedCallback will be skipped.
            this.#disconnectedCallback();
          }
        }
      }
    }
  }

  /**
   * Applies styling to the text in the urlbar input, depending on the text.
   */
  formatValue() {
    // The editor may not exist if the toolbar is not visible.
    if (this.#isAddressbar && this.editor) {
      this.#lazy.valueFormatter.update();
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

  saveSelectionStateForBrowser(browser) {
    let state = this.getBrowserState(browser);
    state.selection = {
      // When the value is empty, we're either on a blank page, or the whole
      // text has been edited away. In the latter case we'll restore value to
      // the current URI, and we want to fully select it.
      start: this.value ? this.selectionStart : 0,
      end: this.value ? this.selectionEnd : Number.MAX_SAFE_INTEGER,
      // When restoring a URI from an empty value, we don't want to untrim it.
      shouldUntrim: this.value && !this._protocolIsTrimmed,
    };
  }

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
      this.window.browsingContext.isDocumentPiP &&
      uri.spec.startsWith("about:blank")
    ) {
      // If this is a Document PiP, its url will be about:blank while
      // the opener will be a secure context, i.e. no about:blank
      throw new Error("Document PiP should show its opener URL");
    }
    // We only need to update the searchModeUI on tab switch conditionally
    // as we only persist searchMode with ScotchBonnet enabled.
    if (
      dueToTabSwitch &&
      lazy.UrlbarPrefs.getScotchBonnetPref("scotchBonnet.persistSearchMode")
    ) {
      this._updateSearchModeUI(this.searchMode);
    }

    let state = this.getBrowserState(this.window.gBrowser.selectedBrowser);
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
        this.window.gBrowser.selectedBrowser.currentAuthPromptURI ||
        uri ||
        this.#isOpenedPageInBlankTargetLoading ||
        this.window.gBrowser.currentURI;
      // Strip off usernames and passwords for the location bar
      try {
        uri = Services.io.createExposableURI(uri);
      } catch (e) {}

      let isInitialPageControlledByWebContent = false;

      // Replace initial page URIs with an empty string
      // only if there's no opener (bug 370555).
      if (
        this.window.isInitialPage(uri) &&
        lazy.BrowserUIUtils.checkEmptyPageOrigin(
          this.window.gBrowser.selectedBrowser,
          uri
        )
      ) {
        value = "";
      } else {
        isInitialPageControlledByWebContent = true;

        // We should deal with losslessDecodeURI throwing for exotic URIs
        try {
          value = losslessDecodeURI(uri);
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
      this.window.isInitialPage(value) &&
      lazy.BrowserUIUtils.checkEmptyPageOrigin(
        this.window.gBrowser.selectedBrowser
      )
    ) {
      value = "";
      valid = true;
    }

    const previousUntrimmedValue = this.untrimmedValue;
    // When calculating the selection indices we must take into account a
    // trimmed protocol.
    let offset = this._protocolIsTrimmed
      ? lazy.BrowserUIUtils.trimURLProtocol.length
      : 0;
    const previousSelectionStart = this.selectionStart + offset;
    const previousSelectionEnd = this.selectionEnd + offset;

    this._setValue(value, { allowTrim: true, valueIsTyped: !valid });
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
        this.getBrowserState(this.window.gBrowser.selectedBrowser)
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
          source: lazy.UrlbarUtils.RESULT_SOURCE.SEARCH,
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
   * @param {nsIBrowser} browser
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
      browser != this.window.gBrowser.selectedBrowser &&
      !this.#canHandleAsBlankPage(locationURI.spec)
    ) {
      // If the page is loaded on background tab, make Unified Search Button
      // unavailable when back to the tab.
      this.getBrowserState(browser).isUnifiedSearchButtonAvailable = false;
    }

    // Using browser navigation buttons should potentially trigger a bounce
    // telemetry event.
    if (webProgress.loadType & Ci.nsIDocShell.LOAD_CMD_HISTORY) {
      this.controller.engagementEvent.handleBounceEventTrigger(browser);
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
   * @param {Event} [event] The event triggering the open.
   */
  handleCommand(event = null) {
    let isMouseEvent = MouseEvent.isInstance(event);
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

    if (lazy.UrlbarPrefs.get("unifiedSearchButton.always")) {
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
   * Handles an event which would cause a URL or text to be opened.
   *
   * @param {object} options
   *   Options for the navigation.
   * @param {Event} [options.event]
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
    let isComposing = this.editor.composing;

    // Use the selected element if we have one; this is usually the case
    // when the view is open.
    let selectedPrivateResult =
      result &&
      result.type == lazy.UrlbarUtils.RESULT_TYPE.SEARCH &&
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
        result.type == lazy.UrlbarUtils.RESULT_TYPE.TIP ||
        this.value == this.#getValueFromResult(result));
    if (
      !isComposing &&
      element &&
      (!oneOffParams?.engine || selectedPrivateEngineResult) &&
      safeToPickResult
    ) {
      this.pickElement(element, event);
      return;
    }

    // Use the hidden heuristic if it exists and there's no selection.
    if (
      lazy.UrlbarPrefs.get("experimental.hideHeuristic") &&
      !element &&
      !isComposing &&
      !oneOffParams?.engine &&
      this._resultForCurrentValue?.heuristic
    ) {
      this.pickResult(this._resultForCurrentValue, event);
      return;
    }

    // We don't select a heuristic result when we're autofilling a token alias,
    // but we want pressing Enter to behave like the first result was selected.
    if (!result && this.value.startsWith("@")) {
      let tokenAliasResult = this.view.getResultAtIndex(0);
      if (tokenAliasResult?.autofill && tokenAliasResult?.payload.keyword) {
        this.pickResult(tokenAliasResult, event);
        return;
      }
    }

    let url;
    let selType = this.controller.engagementEvent.typeFromElement(
      result,
      element
    );
    let typedValue = this.value;
    if (oneOffParams?.engine) {
      selType = "oneoff";
      typedValue = this._lastSearchString;
      // If there's a selected one-off button then load a search using
      // the button's engine.
      result = this._resultForCurrentValue;

      let searchString =
        (result && (result.payload.suggestion || result.payload.query)) ||
        this._lastSearchString;
      [url, openParams.postData] = lazy.UrlbarUtils.getSearchQueryUrl(
        oneOffParams.engine,
        searchString
      );
      if (oneOffParams.openWhere == "tab") {
        this.window.gBrowser.tabContainer.addEventListener(
          "TabOpen",
          tabEvent =>
            this._recordSearch(
              oneOffParams.engine,
              event,
              {},
              tabEvent.target.linkedBrowser
            ),
          { once: true }
        );
      } else {
        this._recordSearch(oneOffParams.engine, event);
      }

      lazy.UrlbarUtils.addToFormHistory(
        this,
        searchString,
        oneOffParams.engine.name
      ).catch(console.error);
    } else {
      // Use the current value if we don't have a UrlbarResult e.g. because the
      // view is closed.
      url = this.untrimmedValue;
      openParams.postData = null;
    }

    if (!url) {
      if (this.sapName == "searchbar") {
        let searchEngine;
        if (this.searchMode) {
          searchEngine = lazy.UrlbarSearchUtils.getEngineByName(
            this.searchMode.engineName
          );
        } else {
          searchEngine = lazy.UrlbarSearchUtils.getDefaultEngine(
            this.isPrivate
          );
        }

        this.openSearchEnginePage("", {
          searchEngine,
          event,
          where: this._whereToOpen(event),
        });
      }
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

    let where = oneOffParams?.openWhere || this._whereToOpen(event);
    if (selectedPrivateResult) {
      where = "window";
      openParams.private = true;
    }
    openParams.allowInheritPrincipal = false;
    url = this._maybeCanonizeURL(event, url) || url.trim();

    let selectedResult = result || this.view.selectedResult;
    this.controller.engagementEvent.record(event, {
      element,
      selType,
      searchString: typedValue,
      result: selectedResult || this._resultForCurrentValue || null,
      searchSource: this.getSearchSource(event),
      windowMode: this.windowMode,
    });

    if (this.#isAddressbar && URL.canParse(url)) {
      // Annotate if the untrimmed value contained a scheme, to later potentially
      // be upgraded by schemeless HTTPS-First.
      openParams.schemelessInput = this.#getSchemelessInput(
        this.untrimmedValue
      );
      this._loadURL(url, event, where, openParams);
      return;
    }

    // This is not a URL and there's no selected element, because likely the
    // view is closed, or paste&go was used.
    // We must act consistently here, having or not an open view should not
    // make a difference if the search string is the same.

    // If we have a result for the current value, we can just use it.
    if (!isComposing && this._resultForCurrentValue) {
      this.pickResult(this._resultForCurrentValue, event);
      return;
    }

    // Otherwise, we must fetch the heuristic result for the current value.
    // TODO (Bug 1604927): If the urlbar results are restricted to a specific
    // engine, here we must search with that specific engine; indeed the
    // docshell wouldn't know about our engine restriction.
    // Also remember to invoke this._recordSearch, after replacing url with
    // the appropriate engine submission url.
    let browser = this.window.gBrowser.selectedBrowser;
    let lastLocationChange = browser.lastLocationChange;

    // Increment rate denominator measuring how often Address Bar handleCommand fallback path is hit.
    Glean.urlbar.heuristicResultMissing.addToDenominator(1);

    lazy.UrlbarUtils.getHeuristicResultFor(url, this)
      .then(newResult => {
        // Because this happens asynchronously, we must verify that the browser
        // location did not change in the meanwhile.
        if (
          where != "current" ||
          browser.lastLocationChange == lastLocationChange
        ) {
          this.pickResult(newResult, event, null, browser);
        }
      })
      .catch(() => {
        if (url) {
          // Something went wrong, we should always have a heuristic result,
          // otherwise it means we're not able to search at all, maybe because
          // some parts of the profile are corrupt.
          // The urlbar should still allow to search or visit the typed string,
          // so that the user can look for help to resolve the problem.

          // Increment rate numerator measuring how often Address Bar handleCommand fallback path is hit.
          Glean.urlbar.heuristicResultMissing.addToNumerator(1);

          let flags =
            Ci.nsIURIFixup.FIXUP_FLAG_FIX_SCHEME_TYPOS |
            Ci.nsIURIFixup.FIXUP_FLAG_ALLOW_KEYWORD_LOOKUP;
          if (this.isPrivate) {
            flags |= Ci.nsIURIFixup.FIXUP_FLAG_PRIVATE_CONTEXT;
          }
          let {
            preferredURI: uri,
            postData,
            keywordAsSent,
          } = Services.uriFixup.getFixupURIInfo(url, flags);
          if (
            where != "current" ||
            browser.lastLocationChange == lastLocationChange
          ) {
            openParams.postData = postData;
            if (!keywordAsSent) {
              // `uri` is not a search engine url, so we annotate if the untrimmed
              // value contained a scheme, to potentially be later upgraded by
              // schemeless HTTPS-First.
              openParams.schemelessInput = this.#getSchemelessInput(
                this.untrimmedValue
              );
            }
            this._loadURL(uri.spec, event, where, openParams, null, browser);
          }
        }
      });
    // Don't add further handling here, the catch above is our last resort.
  }

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
    let state = this.getBrowserState(this.window.gBrowser.selectedBrowser);
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
    if (lazy.UrlbarPrefs.get("shouldHandOffToSearchMode") && searchEngine) {
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
    lazy.logger.debug(
      `pickElement ${element} with event ${event?.type}, result: ${result}`
    );
    if (!result) {
      return;
    }
    this.pickResult(result, event, element);
  }

  /**
   * Called when a result is picked.
   *
   * @param {UrlbarResult} result The result that was picked.
   * @param {Event} event The event that picked the result.
   * @param {HTMLElement} element the picked view element, if available.
   * @param {object} browser The browser to use for the load.
   */
  // eslint-disable-next-line complexity
  pickResult(
    result,
    event,
    element = null,
    browser = this.window.gBrowser.selectedBrowser
  ) {
    if (element?.classList.contains("urlbarView-button-menu")) {
      this.view.openResultMenu(result, element);
      return;
    }

    if (element?.dataset.command) {
      this.#pickMenuResult(result, event, element, browser);
      return;
    }

    if (
      result.providerName == lazy.UrlbarProviderGlobalActions.name &&
      this.#providesSearchMode(result)
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
        result.providerName == lazy.UrlbarProviderGlobalActions.name) ||
      (result.heuristic &&
        this.searchMode?.isPreview &&
        this.view.oneOffSearchButtons?.selectedButton)
    ) {
      this.confirmSearchMode();
      this.search(this.value);
      return;
    }

    if (
      result.type == lazy.UrlbarUtils.RESULT_TYPE.TIP &&
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
      this.view.onQueryResultRemoved(result.rowIndex);
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

    if (element?.closest("#urlbarView-context-menu")) {
      switch (element.id) {
        case "urlbar-view-context-menu-open-in-tab": {
          where = "tab";
          break;
        }
        case "urlbarView-context-menu-open-in-window": {
          where = "window";
          break;
        }
        case "urlbarView-context-menu-open-in-private-window": {
          where = "window";
          openParams.private = true;
          break;
        }
        default: {
          // Open in a container tab.
          where = "tab";
          openParams.userContextId = parseInt(
            element.getAttribute("data-usercontextid")
          );
        }
      }

      if (
        where == "tab" &&
        Services.prefs.getBoolPref("browser.tabs.loadInBackground")
      ) {
        openParams.avoidBrowserFocus = true;
        openParams.keepView = true;
        openParams.inBackground = true;
      }
    } else {
      where = this._whereToOpen(event);
      if (resultUrl && where == "current") {
        // Open help links in a new tab.
        where = "tab";
      }
    }

    if (!this.#providesSearchMode(result)) {
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
      this._loadURL(this._untrimmedValue, event, where, openParams, browser);
      return;
    }

    let { url, postData } = resultUrl
      ? { url: resultUrl, postData: null }
      : lazy.UrlbarUtils.getUrlFromResult(result, { element });
    openParams.postData = postData;

    switch (result.type) {
      case lazy.UrlbarUtils.RESULT_TYPE.URL: {
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
          // with the input value, and let it pass through to _loadURL(), and
          // finally to the docshell.
          // This also means that in some cases the heuristic result will show a
          // Visit entry, but the docshell will instead execute a search. It's a
          // rare case anyway, most likely to happen for enterprises customizing
          // the urifixup prefs.
          if (
            lazy.UrlbarPrefs.get("browser.fixup.dns_first_for_single_words") &&
            lazy.UrlbarUtils.looksLikeSingleWordHost(originalUntrimmedValue)
          ) {
            url = originalUntrimmedValue;
          }
          // Annotate if the untrimmed value contained a scheme, to later potentially
          // be upgraded by schemeless HTTPS-First.
          openParams.schemelessInput = this.#getSchemelessInput(
            originalUntrimmedValue
          );
        }
        break;
      }
      case lazy.UrlbarUtils.RESULT_TYPE.KEYWORD: {
        // If this result comes from a bookmark keyword, let it inherit the
        // current document's principal, otherwise bookmarklets would break.
        openParams.allowInheritPrincipal = true;
        break;
      }
      case lazy.UrlbarUtils.RESULT_TYPE.TAB_SWITCH: {
        // Behaviour is reversed with SecondaryActions, default behaviour is to navigate
        // and button is provided to switch to tab.
        if (
          this.hasAttribute("action-override") ||
          (lazy.UrlbarPrefs.get("secondaryActions.switchToTab") &&
            element?.dataset.action !== "tabswitch")
        ) {
          where = "current";
          break;
        }

        // Keep the searchMode for telemetry since handleRevert sets it to null.
        const searchMode = this.searchMode;
        this.handleRevert();
        let prevTab = this.window.gBrowser.selectedTab;
        let loadOpts = {
          adoptIntoActiveWindow: lazy.UrlbarPrefs.get(
            "switchTabs.adoptIntoActiveWindow"
          ),
        };

        // We cache the search string because switching tab may clear it.
        let searchString = this._lastSearchString;
        this.controller.engagementEvent.record(event, {
          result,
          element,
          searchString,
          searchMode,
          selType: this.controller.engagementEvent.typeFromElement(
            result,
            element
          ),
          searchSource: this.getSearchSource(event),
          windowMode: this.windowMode,
        });

        let activeSplitView = this.window.gBrowser.selectedTab.splitview;

        let switched = this.window.switchToTabHavingURI(
          Services.io.newURI(url),
          true,
          loadOpts,
          lazy.UrlbarProviderOpenTabs.isNonPrivateUserContextId(
            result.payload.userContextId
          )
            ? result.payload.userContextId
            : null,
          activeSplitView
        );
        if (switched && !activeSplitView && prevTab.isEmpty) {
          this.window.gBrowser.removeTab(prevTab);
        }

        if (switched && !this.isPrivate && !result.heuristic) {
          // We don't await for this, because a rejection should not interrupt
          // the load. Just reportError it.
          lazy.UrlbarUtils.addToInputHistory(url, searchString).catch(
            console.error
          );
        }

        // TODO (Bug 1865757): We should not show a "switchtotab" result for
        // tabs that are not currently open. Find out why tabs are not being
        // properly unregistered when they are being closed.
        if (!switched) {
          console.error(`Tried to switch to non-existent tab: ${url}`);
          lazy.UrlbarProviderOpenTabs.unregisterOpenTab(
            url,
            result.payload.userContextId,
            result.payload.tabGroup,
            this.isPrivate
          );
        }

        return;
      }
      case lazy.UrlbarUtils.RESULT_TYPE.SEARCH: {
        if (result.payload.providesSearchMode) {
          this.controller.engagementEvent.record(event, {
            result,
            element,
            searchString: this._lastSearchString,
            selType: this.controller.engagementEvent.typeFromElement(
              result,
              element
            ),
            searchSource: this.getSearchSource(event),
            windowMode: this.windowMode,
          });
          this.maybeConfirmSearchModeFromResult({
            result,
            checkValue: false,
          });
          return;
        }

        if (
          !this.searchMode &&
          result.heuristic &&
          // If we asked the DNS earlier, avoid the post-facto check.
          !lazy.UrlbarPrefs.get("browser.fixup.dns_first_for_single_words") &&
          // TODO (bug 1642623): for now there is no smart heuristic to skip the
          // DNS lookup, so any value above 0 will run it.
          lazy.UrlbarPrefs.get("dnsResolveSingleWordsAfterSearch") > 0 &&
          this.window.gKeywordURIFixup &&
          lazy.UrlbarUtils.looksLikeSingleWordHost(originalUntrimmedValue)
        ) {
          // When fixing a single word to a search, the docShell would also
          // query the DNS and if resolved ask the user whether they would
          // rather visit that as a host. On a positive answer, it adds the host
          // to the list that we use to make decisions.
          // Because we are directly asking for a search here, bypassing the
          // docShell, we need to do the same ourselves.
          // See also URIFixupChild.sys.mjs and keyword-uri-fixup.
          let fixupInfo = this._getURIFixupInfo(originalUntrimmedValue.trim());
          if (fixupInfo) {
            this.window.gKeywordURIFixup.check(
              this.window.gBrowser.selectedBrowser,
              fixupInfo
            );
          }
        }

        if (result.payload.inPrivateWindow) {
          where = "window";
          openParams.private = true;
        }

        const actionDetails = {
          isSuggestion: !!result.payload.suggestion,
          isFormHistory:
            result.source == lazy.UrlbarUtils.RESULT_SOURCE.HISTORY,
          alias: result.payload.keyword,
        };
        const engine = lazy.SearchService.getEngineByName(
          result.payload.engine
        );

        if (where == "tab") {
          // The TabOpen event is fired synchronously so tabEvent.target
          // is guaranteed to be our new search tab.
          this.window.gBrowser.tabContainer.addEventListener(
            "TabOpen",
            tabEvent =>
              this._recordSearch(
                engine,
                event,
                actionDetails,
                tabEvent.target.linkedBrowser
              ),
            { once: true }
          );
        } else {
          this._recordSearch(engine, event, actionDetails);
        }

        if (!result.payload.inPrivateWindow) {
          lazy.UrlbarUtils.addToFormHistory(
            this,
            result.payload.suggestion || result.payload.query,
            engine.name
          ).catch(console.error);
        }
        break;
      }
      case lazy.UrlbarUtils.RESULT_TYPE.TIP: {
        if (url) {
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
      case lazy.UrlbarUtils.RESULT_TYPE.DYNAMIC: {
        if (!url) {
          // If we're not loading a URL, the engagement is done. First revert
          // and then record the engagement since providers expect the urlbar to
          // be reverted when they're notified of the engagement, but before
          // reverting, copy the search mode since it's nulled on revert.
          const { searchMode } = this;
          if (this.sapName != "searchbar") {
            // The searchbar is not reverted so providers enabled in
            // the searchbar should be able to handle both cases.
            this.handleRevert();
          }
          this.controller.engagementEvent.record(event, {
            result,
            element,
            searchMode,
            searchString: this._lastSearchString,
            selType: this.controller.engagementEvent.typeFromElement(
              result,
              element
            ),
            searchSource: this.getSearchSource(event),
            windowMode: this.windowMode,
          });
          return;
        }
        break;
      }
      case lazy.UrlbarUtils.RESULT_TYPE.OMNIBOX: {
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
      case lazy.UrlbarUtils.RESULT_TYPE.RESTRICT: {
        this.handleRevert();
        this.controller.engagementEvent.record(event, {
          result,
          element,
          searchString: this._lastSearchString,
          selType: this.controller.engagementEvent.typeFromElement(
            result,
            element
          ),
          searchSource: this.getSearchSource(event),
          windowMode: this.windowMode,
        });
        this.maybeConfirmSearchModeFromResult({
          result,
          checkValue: false,
        });

        return;
      }
      case lazy.UrlbarUtils.RESULT_TYPE.AI_CHAT: {
        // AI Chat results handle their own navigation.
        this.controller.engagementEvent.record(event, {
          result,
          element,
          searchString: this._lastSearchString,
          selType: this.controller.engagementEvent.typeFromElement(
            result,
            element
          ),
          searchSource: this.getSearchSource(event),
          windowMode: this.windowMode,
        });
        return;
      }
    }

    if (!url) {
      throw new Error(`Invalid url for result ${JSON.stringify(result)}`);
    }

    // Record input history but only in non-private windows.
    if (!this.isPrivate) {
      let input;
      if (
        !result.heuristic &&
        result.type != lazy.UrlbarUtils.RESULT_TYPE.SEARCH
      ) {
        input = this._lastSearchString;
      } else if (
        result.autofill?.type == "adaptive_url" ||
        result.autofill?.type == "adaptive_origin"
      ) {
        input = result.autofill.adaptiveHistoryInput;
      } else if (
        lazy.UrlbarPrefs.get("autoFillAdaptiveHistoryEnabled") &&
        result.autofill?.type == "origin" &&
        // Bug: 2026227: Investigate if we want to use a higher threshold
        this._lastSearchString?.length > 0
      ) {
        // The origin root URL (e.g. http://example.com/) may not be in
        // moz_places yet. It's derived from a deep-link visit. Defer the
        // write until the navigation records the visit.
        lazy.UrlbarUtils.addToInputHistoryWhenReady(
          url,
          this._lastSearchString
        ).catch(console.error);
      }

      // `input` may be an empty string, so do a strict comparison here.
      if (input !== undefined) {
        // We don't await for this, because a rejection should not interrupt
        // the load. Just reportError it.
        lazy.UrlbarUtils.addToInputHistory(url, input).catch(console.error);
      }

      // Re-integration: If the user picks a non-autofill result for a URL
      // that has a blocked origin, clear the block.
      if (
        lazy.UrlbarPrefs.get("autoFillAdaptiveHistoryEnabled") &&
        !result.autofill &&
        result.type == lazy.UrlbarUtils.RESULT_TYPE.URL
      ) {
        let isOrigin = lazy.UrlbarUtils.isOriginUrl(url);
        if (isOrigin) {
          lazy.UrlbarUtils.clearOriginAutofillBlock(url)
            .then(wasBlocked => {
              if (wasBlocked) {
                Glean.urlbarAutofill.reintegration.origin.add(1);
              }
            })
            .catch(console.error);
        } else {
          lazy.UrlbarUtils.clearOriginPageAutofillBlock(url)
            .then(wasBlocked => {
              if (wasBlocked) {
                Glean.urlbarAutofill.reintegration.url.add(1);
              }
            })
            .catch(console.error);
        }
      }
    }

    this.controller.engagementEvent
      .startTrackingBounceEvent(browser, event, {
        result,
        element,
        searchString: this._lastSearchString,
        selType: this.controller.engagementEvent.typeFromElement(
          result,
          element
        ),
        searchSource: this.getSearchSource(event),
        windowMode: this.windowMode,
      })
      .catch(lazy.logger.error);

    this.controller.engagementEvent.record(event, {
      result,
      element,
      searchString: this._lastSearchString,
      selType: this.controller.engagementEvent.typeFromElement(result, element),
      searchSource: this.getSearchSource(event),
      windowMode: this.windowMode,
    });

    if (result.payload.sendAttributionRequest) {
      lazy.PartnerLinkAttribution.makeRequest({
        targetURL: result.payload.url,
        source: this.#sapName,
        campaignID: Services.prefs.getStringPref(
          "browser.partnerlink.campaign.topsites"
        ),
      });
      if (!this.isPrivate && result.providerName === "UrlbarProviderTopSites") {
        // The position is 1-based for telemetry
        const position = result.rowIndex + 1;
        Glean.contextualServicesTopsites.click[`urlbar_${position}`].add(1);
      }
    }

    this._loadURL(
      url,
      event,
      where,
      openParams,
      {
        source: result.source,
        type: result.type,
        searchTerm: result.payload.suggestion ?? result.payload.query,
      },
      browser
    );
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
   * @param {Element} [options.element]
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

    // We won't allow trimming when calling _setValue, since it makes too easy
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
      this._setValue(canonizedUrl);

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
            lazy.UrlbarPrefs.get("scotchBonnet.enableOverride") &&
            this.view.visibleResults.length == 1,
        });
      }
      if (!enteredSearchMode) {
        this._setValue(this.#getValueFromResult(result), {
          actionType: this.#getActionTypeFromResult(result),
        });
        this.searchMode = null;
      }
      this.setResultForCurrentValue(result);
      return false;
    }

    if (!result.autofill) {
      let value = this.#getValueFromResult(result, { urlOverride, element });
      this._setValue(value, {
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

    // Reset backspace dismissal tracking when navigating to a non-autofill
    // result. The placeholder may already be null if the user backspaced away
    // the autofill text before arrowing down.
    if (!result.autofill && this._autofillBackspaceState) {
      this._autofillBackspaceState = null;
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
   * Invoked by the controller when the first result is received.
   *
   * @param {UrlbarResult} firstResult
   *   The first result received.
   * @returns {boolean}
   *   True if this method canceled the query and started a new one.  False
   *   otherwise.
   */
  onFirstResult(firstResult) {
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
      return true;
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
      this._setValue(this.userTypedValue);
    }

    return false;
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
   */
  startQuery({
    allowAutofill,
    autofillIgnoresSelection = false,
    searchString,
    resetSearchState = true,
    event,
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
      this.controller.engagementEvent.start(event, queryContext, searchString);
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
   * @param {SearchEngine} [options.searchEngine]
   *   Search engine to use when the search is using a known alias.
   * @param {UrlbarUtils.SEARCH_MODE_ENTRY} [options.searchModeEntry]
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
    let end = trimmedValue.search(lazy.UrlUtils.REGEXP_SPACES);
    let firstToken = end == -1 ? trimmedValue : trimmedValue.substring(0, end);
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
      if (lazy.UrlUtils.REGEXP_SPACES.test(value[0])) {
        // If there was a trailing space after the restriction token/alias,
        // remove it.
        value = value.slice(1);
      }
    } else if (
      Object.values(lazy.UrlbarTokenizer.RESTRICT).includes(firstToken)
    ) {
      this.searchMode = null;
      // If the entire value is a restricted token, append a space.
      if (Object.values(lazy.UrlbarTokenizer.RESTRICT).includes(value)) {
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
        view: this.window,
        detail: 0,
      });
      this.inputField.dispatchEvent(event);
    }
  }

  /**
   * Returns a search mode object if a token should enter search mode when
   * typed. This does not handle engine aliases.
   *
   * @param {Values<typeof lazy.UrlbarTokenizer.RESTRICT>} token
   *   A restriction token to convert to search mode.
   * @returns {?object}
   *   A search mode object. Null if search mode should not be entered. See
   *   setSearchMode documentation for details.
   */
  searchModeForToken(token) {
    if (token == lazy.UrlbarTokenizer.RESTRICT.SEARCH) {
      return {
        engineName: lazy.UrlbarSearchUtils.getDefaultEngine(this.isPrivate)
          ?.name,
      };
    }

    let mode =
      this.#isAddressbar &&
      lazy.UrlbarUtils.LOCAL_SEARCH_MODES.find(m => m.restrict == token);
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
   * @param {SearchEngine} options.searchEngine
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
    let url, postData;
    if (trimmedValue) {
      [url, postData] = lazy.UrlbarUtils.getSearchQueryUrl(
        searchEngine,
        trimmedValue
      );
      if (where.startsWith("tab")) {
        // The TabOpen event is fired synchronously so tabEvent.target
        // is guaranteed to be our new search tab.
        this.window.gBrowser.tabContainer.addEventListener(
          "TabOpen",
          tabEvent =>
            this._recordSearch(
              searchEngine,
              event,
              {},
              tabEvent.target.linkedBrowser
            ),
          { once: true }
        );
      } else {
        this._recordSearch(searchEngine, event);
      }

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
            source: lazy.UrlbarUtils.RESULT_SOURCE.SEARCH,
            isPreview: false,
          },
          this.window.gBrowser.selectedBrowser
        );
      }
    } else {
      url = searchEngine.searchForm;
      lazy.BrowserSearchTelemetry.recordSearchForm(searchEngine, this.#sapName);
      if (this.#isAddressbar && where == "current") {
        this.inputField.value = url;
        this.selectionStart = -1;
      }
    }
    this._lastSearchString = trimmedValue;

    this.window.openTrustedLinkIn(url, where, { inBackground, postData });
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
   * @returns {?object}
   *   A search mode object or null if the browser/window is not in search mode.
   *   See setSearchMode documentation.
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
   * @param {object} searchMode
   *   A search mode object.
   * @param {string} searchMode.engineName
   *   The name of the search engine to restrict to.
   * @param {UrlbarUtils.RESULT_SOURCE} searchMode.source
   *   A result source to restrict to.
   * @param {string} searchMode.entry
   *   How search mode was entered. This is recorded in event telemetry. One of
   *   the values in UrlbarUtils.SEARCH_MODE_ENTRY.
   * @param {boolean} [searchMode.isPreview]
   *   If true, we will preview search mode. Search mode preview does not record
   *   telemetry and has slighly different UI behavior. The preview is exited in
   *   favor of full search mode when a query is executed. False should be
   *   passed if the caller needs to enter search mode but expects it will not
   *   be interacted with right away. Defaults to true.
   * @param {MozBrowser} browser
   *   The browser for which to set search mode.
   *   Pass the selected browser for the searchbar.
   */
  async setSearchMode(searchMode, browser) {
    let currentSearchMode = this.getSearchMode(browser);
    let areSearchModesSame =
      (!currentSearchMode && !searchMode) ||
      lazy.ObjectUtils.deepEqual(currentSearchMode, searchMode);

    // Exit search mode if the passed-in engine is invalid or hidden.
    let engine;
    if (searchMode?.engineName) {
      if (!lazy.SearchService.isInitialized) {
        await lazy.SearchService.init();
      }
      engine = lazy.SearchService.getEngineByName(searchMode.engineName);
      if (!engine || engine.hidden) {
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

    searchMode = null;

    if (engineName) {
      searchMode = {
        engineName,
        isGeneralPurposeEngine: engine.isGeneralPurposeEngine,
      };
      if (source) {
        searchMode.source = source;
      } else if (searchMode.isGeneralPurposeEngine) {
        // History results for general-purpose search engines are often not
        // useful, so we hide them in search mode. See bug 1658646 for
        // discussion.
        searchMode.source = lazy.UrlbarUtils.RESULT_SOURCE.SEARCH;
      }
    } else if (source) {
      let sourceName = lazy.UrlbarUtils.getResultSourceName(source);
      if (sourceName) {
        searchMode = { source };
      } else {
        console.error(`Unrecognized source: ${source}`);
      }
    }

    let modes = this.#getSearchModesObject(browser);

    if (searchMode) {
      searchMode.isPreview = isPreview;
      if (lazy.UrlbarUtils.SEARCH_MODE_ENTRY.has(entry)) {
        searchMode.entry = entry;
      } else {
        // If we see this value showing up in telemetry, we should review
        // search mode's entry points.
        searchMode.entry = "other";
      }

      if (!searchMode.isPreview) {
        modes.confirmed = searchMode;
        delete modes.preview;
      } else {
        modes.preview = searchMode;
      }
    } else {
      delete modes.preview;
      delete modes.confirmed;
    }

    if (restrictType) {
      searchMode.restrictType = restrictType;
    }

    // Enter search mode if the browser is selected.
    if (browser == this.window.gBrowser.selectedBrowser) {
      this._updateSearchModeUI(searchMode);
      if (searchMode) {
        // Set userTypedValue to the query string so that it's properly restored
        // when switching back to the current tab and across sessions.
        this.userTypedValue = this.untrimmedValue;
        this.valueIsTyped = true;
        if (!searchMode.isPreview && !areSearchModesSame) {
          try {
            lazy.BrowserSearchTelemetry.recordSearchMode(searchMode);
          } catch (ex) {
            console.error(ex);
          }
        }
      }
    }
  }

  /**
   * @typedef {object} SearchModesObject
   *
   * @property {object} [preview] preview search mode
   * @property {object} [confirmed] confirmed search mode
   */

  /**
   * @type {SearchModesObject|undefined}
   *
   * The (lazily initialized) search mode object for the searchbar.
   * This is needed because the searchbar has one search mode per window that
   * shouldn't change when switching tabs. For the address bar, the search mode
   * is stored per browser in #browserStates and this is always undefined.
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
      this.window.gBrowser.selectedBrowser
    ).confirmed;
  }

  /**
   * Enters search mode with the default engine.
   */
  searchModeShortcut() {
    // We restrict to search results when entering search mode from this
    // shortcut to honor historical behaviour.
    this.searchMode = {
      source: lazy.UrlbarUtils.RESULT_SOURCE.SEARCH,
      engineName: lazy.UrlbarSearchUtils.getDefaultEngine(this.isPrivate)?.name,
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
    return this.document.activeElement == this.inputField;
  }

  get goButton() {
    return this.querySelector(".urlbar-go-button");
  }

  get value() {
    return this.inputField.value;
  }

  set value(val) {
    this._setValue(val, { allowTrim: true });
  }

  get untrimmedValue() {
    return this._untrimmedValue;
  }

  get userTypedValue() {
    return this.#isAddressbar
      ? this.window.gBrowser.userTypedValue
      : this._userTypedValue;
  }

  set userTypedValue(val) {
    if (this.#isAddressbar) {
      this.window.gBrowser.userTypedValue = val;
    } else {
      this._userTypedValue = val;
    }
  }

  get lastSearchString() {
    return this._lastSearchString;
  }

  get searchMode() {
    if (!this.window.gBrowser) {
      // This only happens before DOMContentLoaded.
      return null;
    }
    return this.getSearchMode(this.window.gBrowser.selectedBrowser);
  }

  set searchMode(searchMode) {
    this.setSearchMode(searchMode, this.window.gBrowser.selectedBrowser);
    this.searchModeSwitcher?.onSearchModeChanged();
    lazy.UrlbarSearchTermsPersistence.onSearchModeChanged(this.window);
  }

  getBrowserState(browser) {
    let state = this.#browserStates.get(browser);
    if (!state) {
      state = {};
      this.#browserStates.set(browser, state);
    }
    return state;
  }

  async #updateLayoutBreakout() {
    if (!this.#allowBreakout) {
      return;
    }
    if (this.document.fullscreenElement) {
      // Toolbars are hidden in DOM fullscreen mode, so we can't get proper
      // layout information and need to retry after leaving that mode.
      this.window.addEventListener(
        "fullscreen",
        () => {
          this.#updateLayoutBreakout();
        },
        { once: true }
      );
      return;
    }
    await this.#updateLayoutBreakoutDimensions();
  }

  startLayoutExtend() {
    if (!this.#allowBreakout || this.hasAttribute("breakout-extend")) {
      // Do not expand if the Urlbar does not support being expanded or it is
      // already expanded.
      return;
    }
    if (!this.view.isOpen) {
      return;
    }

    this.#updateTextboxPosition();

    this.setAttribute("breakout-extend", "true");

    // Enable the animation only after the first extend call to ensure it
    // doesn't run when opening a new window.
    if (!this.hasAttribute("breakout-extend-animate")) {
      this.window.promiseDocumentFlushed(() => {
        this.window.requestAnimationFrame(() => {
          this.setAttribute("breakout-extend-animate", "true");
        });
      });
    }
  }

  endLayoutExtend() {
    // If reduce motion is enabled, we want to collapse the Urlbar here so the
    // user sees only sees two states: not expanded, and expanded with the view
    // open.
    if (!this.hasAttribute("breakout-extend") || this.view.isOpen) {
      return;
    }

    this.removeAttribute("breakout-extend");
    this.#updateTextboxPosition();
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
      this.window.UpdatePopupNotificationsVisibility
    ) {
      this.window.UpdatePopupNotificationsVisibility();
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

    this.searchMode = searchMode;

    let value = result.payload.query?.trimStart() || "";
    this._setValue(value);

    if (startQuery) {
      this.startQuery({ allowAutofill: false });
    }

    return true;
  }

  /**
   * @param {{wrappedJSObject: SearchEngine}} subject
   * @param {"browser-search-engine-modified"|"ai-window-state-changed"} topic
   * @param {string} data
   */
  observe(subject, topic, data) {
    switch (topic) {
      case "ai-window-state-changed":
        if (subject == this.window && data == "classic") {
          this.#updateLayoutBreakout();
        }
        break;
      case lazy.SearchUtils.TOPIC_ENGINE_MODIFIED: {
        let engine = subject.wrappedJSObject;
        switch (data) {
          case lazy.SearchUtils.MODIFIED_TYPE.CHANGED:
          case lazy.SearchUtils.MODIFIED_TYPE.REMOVED: {
            let searchMode = this.searchMode;
            if (searchMode?.engineName == engine.name) {
              // Exit search mode if the current search mode engine was removed.
              this.searchMode = searchMode;
            }
            break;
          }
          case lazy.SearchUtils.MODIFIED_TYPE.DEFAULT:
            if (!this.isPrivate) {
              this._updatePlaceholder(engine.name);
              // The cached result might use the old default engine.
              this._resultForCurrentValue = null;
            }
            break;
          case lazy.SearchUtils.MODIFIED_TYPE.DEFAULT_PRIVATE:
            if (this.isPrivate) {
              this._updatePlaceholder(engine.name);
              // The cached result might use the old default private engine.
              this._resultForCurrentValue = null;
            }
            break;
        }
        break;
      }
    }
  }

  /**
   * Get search source for telemetry.
   *
   * @param {Event} [event]
   *   The event that triggered this query.
   *   This is not needed for urlbar.* telemetry.
   * @returns {keyof typeof lazy.BrowserSearchTelemetry.KNOWN_SEARCH_SOURCES}
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

      let state = this.getBrowserState(this.window.gBrowser.selectedBrowser);
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
      result.providerName == lazy.UrlbarProviderGlobalActions.name
    ) {
      return this.view.selectedElement.dataset.providesSearchmode == "true";
    }
    return result.payload.providesSearchMode;
  }

  _addObservers() {
    this._observer ??= {
      observe: this.observe.bind(this),
      QueryInterface: ChromeUtils.generateQI([
        "nsIObserver",
        "nsISupportsWeakReference",
      ]),
    };
    Services.obs.addObserver(
      this._observer,
      lazy.SearchUtils.TOPIC_ENGINE_MODIFIED,
      true
    );
    Services.obs.addObserver(this._observer, "ai-window-state-changed", true);
  }

  _removeObservers() {
    if (this._observer) {
      Services.obs.removeObserver(
        this._observer,
        lazy.SearchUtils.TOPIC_ENGINE_MODIFIED
      );
      Services.obs.removeObserver(this._observer, "ai-window-state-changed");
      this._observer = null;
    }
  }

  _getURIFixupInfo(searchString) {
    let flags =
      Ci.nsIURIFixup.FIXUP_FLAG_FIX_SCHEME_TYPOS |
      Ci.nsIURIFixup.FIXUP_FLAG_ALLOW_KEYWORD_LOOKUP;
    if (this.isPrivate) {
      flags |= Ci.nsIURIFixup.FIXUP_FLAG_PRIVATE_CONTEXT;
    }
    try {
      return Services.uriFixup.getFixupURIInfo(searchString, flags);
    } catch (ex) {
      console.error(
        `An error occured while trying to fixup "${searchString}"`,
        ex
      );
    }
    return null;
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

  #updateTextboxPosition() {
    if (!this.view.isOpen) {
      this.style.top = "";
      return;
    }
    this.style.top = px(
      this.parentNode.getBoxQuads({
        ignoreTransforms: true,
        flush: false,
      })[0].p1.y
    );
  }

  #updateTextboxPositionNextFrame() {
    if (!this.hasAttribute("breakout")) {
      return;
    }
    // Allow for any layout changes to take place (e.g. when the menubar becomes
    // inactive) before re-measuring to position the textbox
    this.window.requestAnimationFrame(() => {
      this.window.requestAnimationFrame(() => {
        this.#updateTextboxPosition();
      });
    });
  }

  #stopBreakout() {
    this.removeAttribute("breakout");
    this.parentNode.removeAttribute("breakout");
    this.style.top = "";
    try {
      this.hidePopover();
    } catch (ex) {
      // No big deal if not a popover already.
    }
    this._layoutBreakoutUpdateKey = {};
  }

  incrementBreakoutBlockerCount() {
    this.#breakoutBlockerCount++;
    if (this.#breakoutBlockerCount == 1) {
      this.#stopBreakout();
    }
  }

  decrementBreakoutBlockerCount() {
    if (this.#breakoutBlockerCount > 0) {
      this.#breakoutBlockerCount--;
    }
    if (this.#breakoutBlockerCount === 0) {
      this.#updateLayoutBreakout();
    }
  }

  async #updateLayoutBreakoutDimensions() {
    this.#stopBreakout();

    // When this method gets called a second time before the first call
    // finishes, we need to disregard the first one.
    let updateKey = {};
    this._layoutBreakoutUpdateKey = updateKey;
    await this.window.promiseDocumentFlushed(() => {});
    await new Promise(resolve => {
      this.window.requestAnimationFrame(() => {
        if (this._layoutBreakoutUpdateKey != updateKey || !this.isConnected) {
          return;
        }

        this.parentNode.style.setProperty(
          "--urlbar-container-height",
          px(getBoundsWithoutFlushing(this.parentNode).height)
        );
        this.style.setProperty(
          "--urlbar-height",
          px(getBoundsWithoutFlushing(this).height)
        );

        if (this.#breakoutBlockerCount) {
          return;
        }

        this.setAttribute("breakout", "true");
        this.parentNode.setAttribute("breakout", "true");
        this.showPopover();
        this.#fixAddressbarSearchbarOrder();
        this.#updateTextboxPosition();

        resolve();
      });
    });
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
  _setValue(
    val,
    {
      allowTrim = false,
      untrimmedValue = null,
      valueIsTyped = false,
      actionType = undefined,
    } = {}
  ) {
    // Don't expose internal about:reader URLs to the user.
    let originalUrl = lazy.ReaderMode.getOriginalUrlObjectForDisplay(val);
    if (originalUrl) {
      val = originalUrl.displaySpec;
    }
    this._untrimmedValue = untrimmedValue ?? val;
    this._protocolIsTrimmed = false;
    if (allowTrim) {
      let oldVal = val;
      val = this._trimValue(val);
      this._protocolIsTrimmed =
        oldVal.startsWith(lazy.BrowserUIUtils.trimURLProtocol) &&
        !val.startsWith(lazy.BrowserUIUtils.trimURLProtocol);
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
    let event = this.document.createEvent("Events");
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
      case lazy.UrlbarUtils.RESULT_TYPE.KEYWORD:
        return result.payload.input;
      case lazy.UrlbarUtils.RESULT_TYPE.SEARCH: {
        let value = "";
        if (result.payload.keyword) {
          value += result.payload.keyword + " ";
        }
        value += result.payload.suggestion || result.payload.query;
        return value;
      }
      case lazy.UrlbarUtils.RESULT_TYPE.OMNIBOX:
        return result.payload.content;
      case lazy.UrlbarUtils.RESULT_TYPE.DYNAMIC:
        return (
          element?.dataset.query ||
          element?.dataset.url ||
          result.payload.input ||
          result.payload.query ||
          ""
        );
      case lazy.UrlbarUtils.RESULT_TYPE.RESTRICT:
        return result.payload.autofillKeyword + " ";
      case lazy.UrlbarUtils.RESULT_TYPE.AI_CHAT:
        return result.payload.query ?? "";
      case lazy.UrlbarUtils.RESULT_TYPE.TIP: {
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
      return url ? losslessDecodeURI(url.URI) : "";
    }

    let parsedUrl = URL.parse(result.payload.url);
    // If the url is not parsable, just return an empty string;
    if (!parsedUrl) {
      return "";
    }

    let url = losslessDecodeURI(parsedUrl.URI);
    // If the user didn't originally type a protocol, and we generated one,
    // trim the http protocol from the input value, as https-first may upgrade
    // it to https, breaking user expectations.
    let stripHttp =
      result.heuristic &&
      result.payload.url.startsWith("http://") &&
      this.userTypedValue &&
      this.#getSchemelessInput(this.userTypedValue) ==
        Ci.nsILoadInfo.SchemelessInputTypeSchemeless;
    if (!stripHttp) {
      return url;
    }
    // Attempt to trim the url. If doing so results in a string that is
    // interpreted as search (e.g. unknown single word host, or domain suffix),
    // use the unmodified url instead. Otherwise, if the user edits the url
    // and confirms the new value, we may transform the url into a search.
    let trimmedUrl = lazy.UrlbarUtils.stripPrefixAndTrim(url, { stripHttp })[0];
    let isSearch = !!this._getURIFixupInfo(trimmedUrl)?.keywordAsSent;
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
      case lazy.UrlbarUtils.RESULT_TYPE.TAB_SWITCH:
        return "switchtab";
      case lazy.UrlbarUtils.RESULT_TYPE.OMNIBOX:
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
    this._autofillBackspaceState = null;
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
      this.searchMode?.source != lazy.UrlbarUtils.RESULT_SOURCE.SEARCH;

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
        canAutofillPlaceholder = lazy.UrlbarUtils.canAutofillURL(
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
      lazy.UrlbarUtils.isTextDirectionRTL(this.value, this.window);

    this.window.promiseDocumentFlushed(() => {
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

        this.window.requestAnimationFrame(() => {
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

  /**
   * Should be directly after every showPopover to fix the popover order
   * among urlbar and searchbar.
   * Since a moz-urlbar only extends downwards when focused, the moz-urlbar
   * that's higher (along the y axis) should also be on top (along the z axis).
   *
   * Note: this is a hack necessary because of bug 2014481.
   * Once that's fixed, we can simply always show the focused one on top.
   */
  #fixAddressbarSearchbarOrder() {
    let addressbar = /** @type {?UrlbarInput} */ (
      this.document.getElementById("urlbar")
    );
    let searchbar = /** @type {?UrlbarInput} */ (
      this.document.getElementById("searchbar-new")
    );
    if (
      !searchbar?.matches(":popover-open") ||
      !addressbar?.matches(":popover-open")
    ) {
      return;
    }

    let searchbarArea =
      lazy.CustomizableUI.getPlacementOfWidget("search-container")?.area;
    if (!searchbarArea) {
      return;
    }

    const areasAboveNavbar = [
      lazy.CustomizableUI.AREA_MENUBAR,
      lazy.CustomizableUI.AREA_TABSTRIP,
    ];
    const areasBelowNavbar = [lazy.CustomizableUI.AREA_BOOKMARKS];

    // If `this` is higher than the other bar, we don't need to do anything since
    // showPopover was just called (hence we're already on top of the other one).
    if (areasAboveNavbar.includes(searchbarArea) && this != searchbar) {
      searchbar.hidePopover();
      searchbar.showPopover();
    } else if (areasBelowNavbar.includes(searchbarArea) && this != addressbar) {
      addressbar.hidePopover();
      addressbar.showPopover();
    }
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
      (this.valueIsTyped && !this._protocolIsTrimmed)
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
        ? this.window.gBrowser.selectedBrowser.browsingContext
            .nonWebControlledLoadingURI
        : this.window.gBrowser.currentURI;
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
      !lazy.UrlbarPrefs.get("decodeURLsOnCopy")
    ) {
      return displaySpec;
    }

    // Just the beginning of the URL is selected, or we want a decoded
    // url. First check for a trimmed value.

    if (
      !selectedVal.startsWith(lazy.BrowserUIUtils.trimURLProtocol) &&
      // Note _trimValue may also trim a trailing slash, thus we can't just do
      // a straight string compare to tell if the protocol was trimmed.
      !displaySpec.startsWith(this._trimValue(displaySpec))
    ) {
      selectedVal = lazy.BrowserUIUtils.trimURLProtocol + selectedVal;
    }

    // If selection starts from the beginning and part or all of the URL
    // is selected, we check for decoded characters and encode them.
    // Unless decodeURLsOnCopy is set. Do not encode data: URIs.
    if (!lazy.UrlbarPrefs.get("decodeURLsOnCopy") && !uri.schemeIs("data")) {
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
        (AppConstants.platform == "macosx"
          ? KeyEvent.DOM_VK_META
          : KeyEvent.DOM_VK_CONTROL)
    ) {
      if (event.type == "keydown") {
        this._actionOverrideKeyCount++;
        this.toggleAttribute("action-override", true);
        this.view.panel.setAttribute("action-override", true);
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
   * Records in telemetry that a search is being loaded,
   * updates an incremental total number of searches in a pref,
   * and informs ASRouter that a search has occurred via a trigger send
   *
   * @param {SearchEngine} engine
   *   The engine to generate the query for.
   * @param {Event} event
   *   The event that triggered this query.
   * @param {object} [searchActionDetails]
   *   The details associated with this search query.
   * @param {boolean} [searchActionDetails.isSuggestion]
   *   True if this query was initiated from a suggestion from the search engine.
   * @param {boolean} [searchActionDetails.alias]
   *   True if this query was initiated via a search alias.
   * @param {boolean} [searchActionDetails.isFormHistory]
   *   True if this query was initiated from a form history result.
   * @param {string} [searchActionDetails.url]
   *   The url this query was triggered with.
   * @param {MozBrowser} [browser]
   *   The browser where the search is being opened.
   *   Defaults to the window's selected browser.
   */
  _recordSearch(
    engine,
    event,
    searchActionDetails = {},
    browser = this.window.gBrowser.selectedBrowser
  ) {
    const isOneOff = this.view.oneOffSearchButtons?.eventTargetIsAOneOff(event);
    const searchSource = this.getSearchSource(event);

    // Record when the user uses the search bar to be
    // used for message targeting. This is arbitrarily capped
    // at 100, only to prevent the number from growing ifinitely.
    const totalSearches = Services.prefs.getIntPref(
      "browser.search.totalSearches"
    );
    const totalSearchesCap = 100;
    if (totalSearches < totalSearchesCap) {
      Services.prefs.setIntPref(
        "browser.search.totalSearches",
        totalSearches + 1
      );
    }

    // Record when the user uses the search bar so SearchWidgetTracker can
    // remove the search bar when it hasn't been used in a long time.
    if (this.#sapName == "searchbar") {
      Services.prefs.setStringPref(
        "browser.search.widget.lastUsed",
        new Date().toISOString()
      );
    }

    // Sending a trigger to ASRouter when a search happens
    lazy.ASRouter.sendTriggerMessage({
      browser,
      id: "onSearch",
      context: {
        isSuggestion: searchActionDetails.isSuggestion || false,
        searchSource,
        isOneOff,
      },
    });

    lazy.BrowserSearchTelemetry.recordSearch(browser, engine, searchSource, {
      ...searchActionDetails,
      isOneOff,
      newtabSessionId: this._handoffSession,
    });
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
    let trimmedValue = lazy.UrlbarPrefs.get("trimURLs")
      ? lazy.BrowserUIUtils.trimURL(val)
      : val;
    // Only trim value if the directionality doesn't change to RTL and we're not
    // showing a strikeout https protocol.
    return lazy.UrlbarUtils.isTextDirectionRTL(trimmedValue, this.window) ||
      this.#lazy.valueFormatter.willShowFormattedMixedContentProtocol(val)
      ? val
      : trimmedValue;
  }

  /**
   * Returns whether the passed-in event may represents a canonization request.
   *
   * @param {Event} event
   *   An Event to examine.
   * @returns {boolean}
   *   Whether the event is a KeyboardEvent that triggers canonization.
   */
  #isCanonizeKeyboardEvent(event) {
    if (this.sapName == "searchbar") {
      return false;
    }
    return (
      KeyboardEvent.isInstance(event) &&
      event.keyCode == KeyEvent.DOM_VK_RETURN &&
      (AppConstants.platform == "macosx" ? event.metaKey : event.ctrlKey) &&
      !event._disableCanonization &&
      lazy.UrlbarPrefs.get("ctrlCanonizesURLs")
    );
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
      !this.#isCanonizeKeyboardEvent(event) ||
      !/^\s*[^.:\/\s]+(?:\/.*|\s*)$/i.test(value)
    ) {
      return null;
    }

    let suffix = Services.locale.urlFixupSuffix;
    Glean.urlfixup.suffix.get("urlbar", suffix).add(1);
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
    this._setValue(value, { untrimmedValue });
    this.inputField.setSelectionRange(selectionStart, selectionEnd);
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
   * @param {object} browser The browser to use for the load.
   */
  #pickMenuResult(result, event, element, browser) {
    this.controller.engagementEvent.record(event, {
      result,
      element,
      searchString: this._lastSearchString,
      selType: element.dataset.command,
      searchSource: this.getSearchSource(event),
      windowMode: this.windowMode,
    });

    if (element.dataset.command == "manage") {
      this.window.openPreferences("search-locationBar");
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

    let where = this._whereToOpen(event);
    if (element.dataset.command == "help" && where == "current") {
      // Open help links in a new tab.
      where = "tab";
    }

    this.view.close({ elementPicked: true });

    this._loadURL(
      url,
      event,
      where,
      {
        allowInheritPrincipal: false,
        private: this.isPrivate,
      },
      {
        source: result.source,
        type: result.type,
      },
      browser
    );
  }

  /**
   * Loads the url in the appropriate place.
   *
   * @param {string} url
   *   The URL to open.
   * @param {string} openUILinkWhere
   *   Where we expect the result to be opened.
   * @param {object} params
   *   The parameters related to how and where the result will be opened.
   *   Further supported paramters are listed in _loadURL.
   * @param {object} [params.triggeringPrincipal]
   *   The principal that the action was triggered from.
   * @param {object} [resultDetails]
   *   Details of the selected result, if any.
   *   Further supported details are listed in _loadURL.
   * @param {string} [resultDetails.searchTerm]
   *   Search term of the result source, if any.
   * @param {object} browser the browser to use for the load.
   */
  #prepareAddressbarLoad(
    url,
    openUILinkWhere,
    params,
    resultDetails = null,
    browser
  ) {
    if (!this.#isAddressbar) {
      throw new Error(
        "Can't prepare addressbar load when this isn't an addressbar input"
      );
    }

    // No point in setting these because we'll handleRevert() a few rows below.
    if (openUILinkWhere == "current") {
      // Make sure URL is formatted properly (don't show punycode).
      let formattedURL = url;
      try {
        formattedURL = losslessDecodeURI(new URL(url).URI);
      } catch {}

      this.value =
        lazy.UrlbarPrefs.isPersistedSearchTermsEnabled() &&
        resultDetails?.searchTerm
          ? resultDetails.searchTerm
          : formattedURL;
      browser.userTypedValue = this.value;
    }

    // No point in setting this if we are loading in a new window.
    if (
      openUILinkWhere != "window" &&
      this.window.gInitialPages.includes(url)
    ) {
      browser.initialPageLoadedFromUserAction = url;
    }

    try {
      lazy.UrlbarUtils.addToUrlbarHistory(url, this.window);
    } catch (ex) {
      // Things may go wrong when adding url to session history,
      // but don't let that interfere with the loading of the url.
      console.error(ex);
    }

    // TODO: When bug 1498553 is resolved, we should be able to
    // remove the !triggeringPrincipal condition here.
    if (
      !params.triggeringPrincipal ||
      params.triggeringPrincipal.isSystemPrincipal
    ) {
      // Reset DOS mitigations for the basic auth prompt.
      delete browser.authPromptAbuseCounter;

      // Reset temporary permissions on the current tab if the user reloads
      // the tab via the urlbar.
      if (
        openUILinkWhere == "current" &&
        browser.currentURI &&
        url === browser.currentURI.spec
      ) {
        this.window.SitePermissions.clearTemporaryBlockPermissions(browser);
      }
    }

    // Specifies that the URL load was initiated by the URL bar.
    params.initiatedByURLBar = true;
  }

  /**
   * Loads the url in the appropriate place.
   *
   * @param {string} url
   *   The URL to open.
   * @param {Event} event
   *   The event that triggered to load the url.
   * @param {string} openUILinkWhere
   *   Where we expect the result to be opened.
   * @param {object} params
   *   The parameters related to how and where the result will be opened.
   *   Further supported parameters are listed in utilityOverlay.js#openUILinkIn.
   * @param {object} [params.triggeringPrincipal]
   *   The principal that the action was triggered from.
   * @param {nsIInputStream} [params.postData]
   *   The POST data associated with a search submission.
   * @param {boolean} [params.allowInheritPrincipal]
   *   Whether the principal can be inherited.
   * @param {nsILoadInfo.SchemelessInputType} [params.schemelessInput]
   *   Whether the search/URL term was without an explicit scheme.
   * @param {object} [resultDetails]
   *   Details of the selected result, if any.
   * @param {Values<typeof lazy.UrlbarUtils.RESULT_TYPE>} [resultDetails.type]
   *   Details of the result type, if any.
   * @param {string} [resultDetails.searchTerm]
   *   Search term of the result source, if any.
   * @param {Values<typeof lazy.UrlbarUtils.RESULT_SOURCE>} [resultDetails.source]
   *   Details of the result source, if any.
   * @param {object} browser [optional] the browser to use for the load.
   */
  _loadURL(
    url,
    event,
    openUILinkWhere,
    params,
    resultDetails = null,
    browser = this.window.gBrowser.selectedBrowser
  ) {
    if (this.#isAddressbar) {
      this.#prepareAddressbarLoad(
        url,
        openUILinkWhere,
        params,
        resultDetails,
        browser
      );
    }

    params.allowThirdPartyFixup = true;

    if (openUILinkWhere == "current") {
      params.targetBrowser = browser;
      params.indicateErrorPageLoad = true;
      params.allowPinnedTabHostChange = this.#isAddressbar;
      params.allowPopups = url.startsWith("javascript:");
    } else {
      params.initiatingDoc = this.window.document;
    }

    if (
      this._keyDownEnterDeferred &&
      event?.keyCode === KeyEvent.DOM_VK_RETURN &&
      openUILinkWhere === "current"
    ) {
      // In this case, we move the focus to the browser that loads the content
      // upon key up the enter key.
      // To do it, send avoidBrowserFocus flag to openTrustedLinkIn() to avoid
      // focusing on the browser in the function. And also, set loadedContent
      // flag that whether the content is loaded in the current tab by this enter
      // key. _keyDownEnterDeferred promise is processed at key up the enter,
      // focus on the browser passed by _keyDownEnterDeferred.resolve().
      params.avoidBrowserFocus = true;
      this._keyDownEnterDeferred.loadedContent = true;
      this._keyDownEnterDeferred.resolve(browser);
    }

    // Ensure the window gets the `private` feature if the current window
    // is private, unless the caller explicitly requested not to.
    if (this.isPrivate && !("private" in params)) {
      params.private = true;
    }

    // Focus the content area before triggering loads, since if the load
    // occurs in a new tab, we want focus to be restored to the content
    // area when the current tab is re-selected.
    if (!params.avoidBrowserFocus) {
      browser.focus();
      // Make sure the domain name stays visible for spoof protection and usability.
      this.inputField.setSelectionRange(0, 0);
    }

    if (openUILinkWhere != "current" && this.sapName != "searchbar") {
      this.handleRevert();
    }

    // Notify about the start of navigation.
    this.#notifyStartNavigation(resultDetails);

    try {
      this.window.openTrustedLinkIn(url, openUILinkWhere, params);
    } catch (ex) {
      // This load can throw an exception in certain cases, which means
      // we'll want to replace the URL with the loaded URL:
      if (ex.result != Cr.NS_ERROR_LOAD_SHOWED_ERRORPAGE) {
        this.handleRevert();
      }
    }

    // If we show the focus border after closing the view, it would appear to
    // flash since this._on_blur would remove it immediately after.
    this.view.close({ showFocusBorder: false });
  }

  /**
   * Determines where a URL/page should be opened.
   *
   * @param {Event} event the event triggering the opening.
   * @returns {"current" | "tabshifted" | "tab" | "save" | "window"}
   */
  _whereToOpen(event) {
    let isKeyboardEvent = KeyboardEvent.isInstance(event);
    let reuseEmpty = isKeyboardEvent;
    let where = undefined;
    if (
      isKeyboardEvent &&
      (event.altKey || event.getModifierState("AltGraph"))
    ) {
      // We support using 'alt' to open in a tab, because ctrl/shift
      // might be used for canonizing URLs:
      where = event.shiftKey ? "tabshifted" : "tab";
    } else if (this.#isCanonizeKeyboardEvent(event)) {
      // If we're allowing canonization, and this is a canonization key event,
      // open in current tab to avoid handling as new tab modifier.
      where = "current";
    } else {
      where = lazy.BrowserUtils.whereToOpenLink(event, false, false);
    }
    let openInTabPref =
      this.#sapName == "searchbar"
        ? lazy.UrlbarPrefs.get("browser.search.openintab")
        : lazy.UrlbarPrefs.get("openintab");
    if (openInTabPref) {
      if (where == "current") {
        where = "tab";
      } else if (where == "tab") {
        where = "current";
      }
      reuseEmpty = true;
    }
    if (
      where == "tab" &&
      reuseEmpty &&
      this.window.gBrowser.selectedTab.isEmpty
    ) {
      where = "current";
    }
    return where;
  }

  _initCopyCutController() {
    if (this._copyCutController) {
      return;
    }
    this._copyCutController = new CopyCutController(this);
    this.inputField.controllers.insertControllerAt(0, this._copyCutController);
  }

  /**
   * Searches the context menu for the location of a specific command.
   *
   * @param {string} menuItemCommand
   *    The command to search for.
   * @returns {HTMLElement}
   *    Html element that matches the command or
   *    the last element if we could not find the command.
   */
  #findMenuItemLocation(menuItemCommand) {
    let inputBox = this.querySelector("moz-input-box");
    let contextMenu = inputBox.menupopup;
    let insertLocation = contextMenu.firstElementChild;
    // find the location of the command
    while (
      insertLocation.nextElementSibling &&
      insertLocation.getAttribute("cmd") != menuItemCommand
    ) {
      insertLocation = insertLocation.nextElementSibling;
    }

    return insertLocation;
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
      !lazy.UrlbarPrefs.getScotchBonnetPref(
        "untrimOnUserInteraction.featureGate"
      ) ||
      !this._protocolIsTrimmed ||
      !this.focused ||
      (!ignoreSelection && this.#allTextSelected)
    ) {
      return;
    }

    let selectionStart = this.selectionStart;
    let selectionEnd = this.selectionEnd;

    // Correct the selection taking the trimmed protocol into account.
    let offset = lazy.BrowserUIUtils.trimURLProtocol.length;

    // In case of autofill, we may have to adjust its boundaries.
    if (this._autofillPlaceholder) {
      this._autofillPlaceholder.selectionStart += offset;
      this._autofillPlaceholder.selectionEnd += offset;
    }

    if (moveCursorToStart) {
      this._setValue(this._untrimmedValue, {
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
          lazy.logger.error("Should only try to untrim valid URLs");
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

    this._setValue(this._untrimmedValue, {
      valueIsTyped: this.valueIsTyped,
    });

    this.setSelectionRange(selectionStart, selectionEnd);
  }

  // The strip-on-share feature will strip known tracking/decorational
  // query params from the URI and copy the stripped version to the clipboard.
  _initStripOnShare() {
    let contextMenu = this.querySelector("moz-input-box").menupopup;
    let insertLocation = this.#findMenuItemLocation("cmd_copy");
    // set up the menu item
    let stripOnShare = this.document.createXULElement("menuitem");
    this.document.l10n.setAttributes(
      stripOnShare,
      "text-action-copy-clean-link"
    );
    stripOnShare.setAttribute("anonid", "strip-on-share");
    stripOnShare.id = "strip-on-share";

    insertLocation.insertAdjacentElement("afterend", stripOnShare);

    // Register listener that returns the stripped url or falls back
    // to the original url if nothing can be stripped.
    stripOnShare.addEventListener("command", () => {
      let strippedURI = this.#stripURI();
      lazy.ClipboardHelper.copyString(strippedURI.displaySpec);
    });

    // Register a listener that hides the menu item if there is nothing to copy.
    contextMenu.addEventListener("popupshowing", () => {
      // feature is not enabled
      if (!lazy.QUERY_STRIPPING_STRIP_ON_SHARE) {
        stripOnShare.setAttribute("hidden", true);
        return;
      }
      let controller =
        this.document.commandDispatcher.getControllerForCommand("cmd_copy");
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
    });
  }

  _initPasteAndGo() {
    let inputBox = this.querySelector("moz-input-box");
    let contextMenu = inputBox.menupopup;
    let insertLocation = this.#findMenuItemLocation("cmd_paste");
    if (!insertLocation) {
      return;
    }

    let pasteAndGo = this.document.createXULElement("menuitem");
    pasteAndGo.id = "paste-and-go";
    let label = Services.strings
      .createBundle("chrome://browser/locale/browser.properties")
      .GetStringFromName("pasteAndGo.label");
    pasteAndGo.setAttribute("label", label);
    pasteAndGo.setAttribute("anonid", "paste-and-go");
    pasteAndGo.addEventListener("command", () => {
      this._suppressStartQuery = true;

      this.select();
      this.window.goDoCommand("cmd_paste");
      this.setResultForCurrentValue(null);
      this.handleCommand();
      this.controller.clearLastQueryContextCache();

      this._suppressStartQuery = false;
    });

    contextMenu.addEventListener("popupshowing", () => {
      // Close the results pane when the input field contextual menu is open,
      // because paste and go doesn't want a result selection.
      this.view.close();

      let controller =
        this.document.commandDispatcher.getControllerForCommand("cmd_paste");
      let enabled = controller.isCommandEnabled("cmd_paste");
      if (enabled) {
        pasteAndGo.removeAttribute("disabled");
      } else {
        pasteAndGo.setAttribute("disabled", "true");
      }
    });

    insertLocation.insertAdjacentElement("afterend", pasteAndGo);
  }

  /**
   * Initializes the share URL context menu item.
   * This is only shown on the addressbar and only on macOS.
   */
  #initShareURL() {
    let contextMenu = this.querySelector("moz-input-box").menupopup;
    let insertLocation = this.#findMenuItemLocation("cmd_selectAll");

    let separator = this.document.createXULElement("menuseparator");
    insertLocation.insertAdjacentElement("afterend", separator);

    contextMenu.addEventListener("popupshowing", () => {
      let browser = this.window.gBrowser?.selectedBrowser;
      if (browser) {
        lazy.SharingUtils.ensureShareMenu(browser, null, separator);
      }
    });
  }

  /**
   * Initializes the clear search history context menu item.
   * This is only shown on the searchbar.
   */
  #initClearSearchHistory() {
    let insertLocation = this.#findMenuItemLocation("cmd_selectAll");

    let separator = this.document.createXULElement("menuseparator");
    insertLocation.after(separator);

    let clearHistory = this.document.createXULElement("menuitem");
    separator.after(clearHistory);

    clearHistory.setAttribute("anonid", "clear-search-history");
    this.document.l10n.setAttributes(clearHistory, "clear-search-history");
    clearHistory.addEventListener("command", () => {
      lazy.UrlbarUtils.clearFormHistory();
      this.handleRevert();
    });
  }

  /**
   * This notifies observers that the user has entered or selected something in
   * the URL bar which will cause navigation.
   *
   * We use the observer service, so that we don't need to load extra facilities
   * if they aren't being used, e.g. WebNavigation.
   *
   * @param {UrlbarResult} result
   *   Details of the result that was selected, if any.
   */
  #notifyStartNavigation(result) {
    if (this.#isAddressbar) {
      Services.obs.notifyObservers({ result }, "urlbar-user-start-navigation");
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
   * @returns {object} A search mode object. Null if search mode should not be
   *   entered. See setSearchMode documentation for details.
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
      if (result.type == lazy.UrlbarUtils.RESULT_TYPE.RESTRICT) {
        searchMode.restrictType = "keyword";
      } else if (
        lazy.UrlbarTokenizer.SEARCH_MODE_RESTRICT.has(result.payload.keyword)
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
   * @param {object} searchMode
   *   See setSearchMode documentation.  If null, then search mode is exited.
   */
  _updateSearchModeUI(searchMode) {
    let { engineName, source, isGeneralPurposeEngine } = searchMode || {};

    // As an optimization, bail if the given search mode is null but search mode
    // is already inactive. Otherwise, browser_preferences_usage.js fails due to
    // accessing the browser.urlbar.placeholderName pref (via the call to
    // initPlaceHolder below) too many times. That test does not enter search mode,
    // but it triggers many calls to this method with a null search mode, via setURI.
    if (!engineName && !source && !this.hasAttribute("searchmode")) {
      return;
    }

    if (this._searchModeIndicatorTitle) {
      this._searchModeIndicatorTitle.textContent = "";
      this._searchModeIndicatorTitle.removeAttribute("data-l10n-id");
    }

    if (!engineName && !source) {
      this.removeAttribute("searchmode");
      this.initPlaceHolder(true);
      return;
    }

    if (this.#isAddressbar) {
      if (engineName) {
        // Set text content for the search mode indicator.
        this._searchModeIndicatorTitle.textContent = engineName;
        this.document.l10n.setAttributes(
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
        let sourceName = lazy.UrlbarUtils.getResultSourceName(source);
        let l10nID = `urlbar-search-mode-${sourceName}`;
        this.document.l10n.setAttributes(
          this._searchModeIndicatorTitle,
          l10nID
        );
        this.document.l10n.setAttributes(
          this.inputField,
          messageIDs[sourceName]
        );
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

    this.searchModeSwitcher?.onSearchModeChanged();
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
    if (!lazy.UrlbarPrefs.isPersistedSearchTermsEnabled()) {
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
      (!this.window.gBrowser.selectedBrowser.originalURI ||
        !state.persist.originalURI.equals(
          this.window.gBrowser.selectedBrowser.originalURI
        ));

    // Capture the shouldPersist property if it exists before
    // setPersistenceState potentially modifies it.
    let wasPersisting = state.persist?.shouldPersist ?? false;

    if (firstView || cachedUriDidChange) {
      lazy.UrlbarSearchTermsPersistence.setPersistenceState(
        state,
        this.window.gBrowser.selectedBrowser.originalURI
      );
    }
    let shouldPersist =
      !hideSearchTerms &&
      lazy.UrlbarSearchTermsPersistence.shouldPersist(state, {
        dueToTabSwitch,
        isSameDocument,
        uri: uri ?? this.window.gBrowser.currentURI,
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
   * This should be called around the time of DOMContentLoaded, so that it is
   * initialized quickly before the user sees anything.
   *
   * Note: If the preference doesn't exist, we don't do anything as the default
   * placeholder is a string which doesn't have the engine name; however, this
   * can be overridden using the `force` parameter.
   *
   * @param {boolean} force If true and the preference doesn't exist, the
   *                        placeholder will be set to the default version
   *                        without an engine name ("Search or enter address").
   */
  initPlaceHolder(force = false) {
    if (!this.#isAddressbar) {
      return;
    }

    let prefName =
      "browser.urlbar.placeholderName" + (this.isPrivate ? ".private" : "");
    let engineName = Services.prefs.getStringPref(prefName, "");
    if (engineName || force) {
      // We can do this directly, since we know we're at DOMContentLoaded.
      this._setPlaceholder(engineName || null);
    }
  }

  /**
   * Asynchronously changes the urlbar placeholder to the name of the default
   * engine according to the search service when it is initialized.
   *
   * This should be called around the time of MozAfterPaint. Since the
   * placeholder was already initialized to the pre-saved engine name by
   * initPlaceHolder when this is called, the update is delayed to avoid
   * confusing the user.
   */
  async delayedStartupInit() {
    // Only delay if requested, and we're not displaying text in the URL bar
    // currently.
    if (!this.value) {
      // Delays changing the URL Bar placeholder and Unified Search Button icon
      // until the user is not going to be seeing it, e.g. when there is a value
      // entered in the bar, or if there is a tab switch to a tab which has a url
      // loaded. We delay the update until the user is out of search mode since
      // an alternative placeholder is used in search mode.
      let updateListener = () => {
        if (this.value && !this.searchMode) {
          // By the time the user has switched, they may have changed the engine
          // again, so we need to call this function again but with the
          // new engine name.
          // No need to await for this to finish, we're in a listener here anyway.
          this.searchModeSwitcher.updateSearchIcon();
          this._updatePlaceholderFromDefaultEngine();
          this.inputField.removeEventListener("input", updateListener);
          this.window.gBrowser.tabContainer.removeEventListener(
            "TabSelect",
            updateListener
          );
        }
      };

      this.inputField.addEventListener("input", updateListener);
      this.window.gBrowser.tabContainer.addEventListener(
        "TabSelect",
        updateListener
      );
    } else {
      await this._updatePlaceholderFromDefaultEngine();
    }

    // If we haven't finished initializing, ensure the placeholder
    // preference is set for the next startup.
    if (this.#isAddressbar) {
      lazy.SearchUIUtils.updatePlaceholderNamePreference(
        await this._getDefaultSearchEngine(),
        this.isPrivate
      );
    }
  }

  /**
   * Set Unified Search Button availability.
   *
   * @param {boolean} available If true Unified Search Button will be available.
   */
  setUnifiedSearchButtonAvailability(available) {
    this.toggleAttribute("unifiedsearchbutton-available", available);
    const switcher = this.querySelector(".searchmode-switcher");
    if (available) {
      switcher.removeAttribute("aria-hidden");
    } else {
      switcher.setAttribute("aria-hidden", "true");
    }
    this.getBrowserState(
      this.window.gBrowser.selectedBrowser
    ).isUnifiedSearchButtonAvailable = available;
  }

  /**
   * Returns a Promise that resolves with default search engine.
   *
   * @returns {Promise<SearchEngine>}
   */
  _getDefaultSearchEngine() {
    return this.isPrivate
      ? lazy.SearchService.getDefaultPrivate()
      : lazy.SearchService.getDefault();
  }

  /**
   * This is a wrapper around '_updatePlaceholder' that uses the appropriate
   * default engine to get the engine name.
   */
  async _updatePlaceholderFromDefaultEngine() {
    const defaultEngine = await this._getDefaultSearchEngine();
    this._updatePlaceholder(defaultEngine.name);
  }

  /**
   * Updates the URLBar placeholder for the specified engine, delaying the
   * update if required.
   *
   * Note: The engine name will only be displayed for application-provided
   * engines, as we know they should have short names.
   *
   * @param {string}  engineName     The search engine name to use for the update.
   */
  _updatePlaceholder(engineName) {
    if (!engineName) {
      throw new Error("Expected an engineName to be specified");
    }

    if (this.searchMode || !this.#isAddressbar) {
      return;
    }

    let engine = lazy.SearchService.getEngineByName(engineName);
    if (engine.isConfigEngine) {
      this._setPlaceholder(engineName);
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
    if (!this.#isAddressbar) {
      this.document.l10n.setAttributes(this.inputField, "searchbar-input");
      return;
    }

    let l10nId;
    if (lazy.UrlbarPrefs.get("keyword.enabled")) {
      l10nId = engineName
        ? "urlbar-placeholder-with-name"
        : "urlbar-placeholder";
    } else {
      l10nId = "urlbar-placeholder-keyword-disabled";
    }

    this.document.l10n.setAttributes(
      this.inputField,
      l10nId,
      l10nId == "urlbar-placeholder-with-name"
        ? { name: engineName }
        : undefined
    );
  }

  /**
   * Used to indicate if the input already has focus when a click is made, and
   * if so, then we shouldn't select all the text.
   */
  #preventClickSelectsAll = false;

  /**
   * Determines if we should select all the text in the Urlbar based on the
   *  Urlbar state, and whether the selection is empty.
   */
  #maybeSelectAll() {
    if (
      !this.#preventClickSelectsAll &&
      this.#compositionState != lazy.UrlbarUtils.COMPOSITION.COMPOSING &&
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
    lazy.logger.debug("Blur Event");
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
    if (lazy.ExtensionSearchHandler.hasActiveInputSession()) {
      lazy.ExtensionSearchHandler.handleInputCancelled();
    }

    // Respect the autohide preference for easier inspecting/debugging via
    // the browser toolbox.
    if (!lazy.UrlbarPrefs.get("ui.popup.disable_autohide")) {
      this.view.close();
    }

    // We may have hidden popup notifications, show them again if necessary.
    if (
      this.getAttribute("pageproxystate") != "valid" &&
      this.window.UpdatePopupNotificationsVisibility
    ) {
      this.window.UpdatePopupNotificationsVisibility();
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

    this._autofillBackspaceState = null;

    Services.obs.notifyObservers(null, "urlbar-blur");
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
      case this.goButton:
        this.handleCommand(event);
        break;
    }
  }

  _on_contextmenu(event) {
    this.#lazy.addSearchEngineHelper.refreshContextMenu(event);

    // Context menu opened via keyboard shortcut.
    if (!event.button) {
      return;
    }

    this.#maybeSelectAll();
  }

  _on_focus(event) {
    lazy.logger.debug("Focus Event");
    if (!this._hideFocus) {
      this.toggleAttribute("focused", true);
    }

    // If the value was trimmed, check whether we should untrim it.
    // This is necessary when a protocol was typed, but the whole url has
    // invalid parts, like the origin, then editing and confirming the trimmed
    // value would execute a search instead of visiting the typed url.
    if (this._protocolIsTrimmed) {
      let untrim = false;
      let fixedURI = this._getURIFixupInfo(this.value)?.preferredURI;
      if (fixedURI) {
        try {
          let expectedURI = Services.io.newURI(this._untrimmedValue);
          if (
            lazy.UrlbarPrefs.getScotchBonnetPref("trimHttps") &&
            this._untrimmedValue.startsWith("https://")
          ) {
            untrim =
              fixedURI.displaySpec.replace("http://", "https://") !=
              expectedURI.displaySpec; // FIXME bug 1847723: Figure out a way to do this without manually messing with the fixed up URI.
          } else {
            untrim = fixedURI.displaySpec != expectedURI.displaySpec;
          }
        } catch (ex) {
          untrim = true;
        }
      }
      if (untrim) {
        this._setValue(this._untrimmedValue);
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
      this.window.UpdatePopupNotificationsVisibility
    ) {
      this.window.UpdatePopupNotificationsVisibility();
    }

    Services.obs.notifyObservers(null, "urlbar-focus");
  }

  _on_mouseover() {
    this._updateUrlTooltip();
  }

  _on_draggableregionleftmousedown() {
    if (!lazy.UrlbarPrefs.get("ui.popup.disable_autohide")) {
      this.view.close();
    }
  }

  _on_mousedown(event) {
    switch (event.currentTarget) {
      case this: {
        this._mousedownOnUrlbarDescendant = true;
        if (
          event.composedTarget != this.inputField &&
          event.composedTarget != this._inputContainer
        ) {
          break;
        }

        this.focusedViaMousedown = !this.focused;
        this.#preventClickSelectsAll = this.focused;

        // Keep the focus status, since the attribute may be changed
        // upon calling this.focus().
        const hasFocus = this.hasAttribute("focused");
        if (event.composedTarget != this.inputField) {
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
      case this.window:
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
        if (!lazy.UrlbarPrefs.get("ui.popup.disable_autohide")) {
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
    if (
      this._autofillPlaceholder &&
      this.value === this.userTypedValue &&
      (event.inputType === "deleteContentBackward" ||
        event.inputType === "deleteContentForward")
    ) {
      // Take a telemetry if user deleted whole autofilled value.
      Glean.urlbar.autofillDeletion.add(1);
    }

    if (
      lazy.UrlbarPrefs.get("autoFillAdaptiveHistoryEnabled") &&
      event.inputType === "deleteContentBackward"
    ) {
      if (
        !this._autofillBackspaceState &&
        this._autofillPlaceholder &&
        this._autofillPlaceholder.selectionStart <
          this._autofillPlaceholder.selectionEnd
      ) {
        this._autofillBackspaceState = {
          url: this._resultForCurrentValue?.payload?.url,
          count: 0,
        };
      }
      if (this._autofillBackspaceState) {
        this._autofillBackspaceState.count++;
        if (
          this._autofillBackspaceState.count >=
          lazy.UrlbarPrefs.get("autoFill.backspaceThreshold")
        ) {
          if (!this.isPrivate) {
            let { url } = this._autofillBackspaceState;
            if (url) {
              let blockUntil =
                Date.now() +
                lazy.UrlbarPrefs.get("autoFill.backspaceBlockDurationMs");
              lazy.UrlbarUtils.blockAutofill(url, blockUntil).catch(
                console.error
              );
            }
          }
          this._autofillBackspaceState = null;
        }
      }
    } else if (this._autofillBackspaceState) {
      // Any non-backspace input resets the state.
      this._autofillBackspaceState = null;
    }

    let value = this.value;
    this.valueIsTyped = true;
    this._untrimmedValue = value;
    this._protocolIsTrimmed = false;
    this._resultForCurrentValue = null;

    this.userTypedValue = value;
    // Unset userSelectionBehavior because the user is modifying the search
    // string, thus there's no valid selection. This is also used by the view
    // to set "aria-activedescendant", thus it should never get stale.
    this.controller.userSelectionBehavior = "none";

    let compositionState = this.#compositionState;
    let compositionClosedPopup = this.#compositionClosedPopup;

    // Clear composition values if we're no more composing.
    if (this.#compositionState != lazy.UrlbarUtils.COMPOSITION.COMPOSING) {
      this.#compositionState = lazy.UrlbarUtils.COMPOSITION.NONE;
      this.#compositionClosedPopup = false;
    }

    if (
      compositionState == lazy.UrlbarUtils.COMPOSITION.COMPOSING &&
      event.data
    ) {
      this.#compositionHadText = true;
    }

    this.toggleAttribute("usertyping", value);
    this.removeAttribute("actiontype");

    if (
      this.getAttribute("pageproxystate") == "valid" &&
      this.value != this._lastValidURLStr
    ) {
      this.setPageProxyState("invalid", true);
    }

    let state = this.getBrowserState(this.window.gBrowser.selectedBrowser);
    if (
      state.persist?.shouldPersist &&
      this.value !== state.persist.searchTerms
    ) {
      state.persist.shouldPersist = false;
      this.removeAttribute("persistsearchterms");
    }

    if (this.view.isOpen) {
      // UrlbarView rolls up all popups when it opens, but we should
      // do the same for UrlbarInput when it's already open in case
      // a tab preview was opened
      this.view.maybeRollupPopups();

      if (!value && !lazy.UrlbarPrefs.get("suggest.topsites")) {
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
      !lazy.UrlbarPrefs.get("keepPanelOpenDuringImeComposition") &&
      (compositionState == lazy.UrlbarUtils.COMPOSITION.COMPOSING ||
        (compositionState == lazy.UrlbarUtils.COMPOSITION.CANCELED &&
          !compositionClosedPopup))
    ) {
      return;
    }

    // Don't autofill when the user is explicitly deleting content, pasting, or
    // undoing/redoing.
    const allowAutofill =
      (!lazy.UrlbarPrefs.get("keepPanelOpenDuringImeComposition") ||
        compositionState !== lazy.UrlbarUtils.COMPOSITION.COMPOSING) &&
      !event.inputType?.startsWith("delete") &&
      !event.inputType?.startsWith("history") &&
      !lazy.UrlbarUtils.isPasteEvent(event) &&
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
      !this.window.windowUtils.isHandlingUserInput ||
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
      Services.clipboard.kSelectionClipboard
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

    const pasteData =
      lazy.UrlbarUtils.sanitizeTextFromClipboard(originalPasteData);

    if (originalPasteData != pasteData) {
      // Unfortunately we're not allowed to set the bits being pasted
      // so cancel this event:
      event.preventDefault();
      event.stopImmediatePropagation();

      const value = oldStart + pasteData + oldEnd;
      this._setValue(value, { valueIsTyped: true });
      this.userTypedValue = value;

      // Since we prevent the default paste event, we have to ensure the
      // pageproxystate is updated. The paste event replaces the actual current
      // page's URL with user-typed content, so we should set pageproxystate to
      // invalid.
      if (this.getAttribute("pageproxystate") == "valid") {
        this.setPageProxyState("invalid");
      }
      this.toggleAttribute("usertyping", this._untrimmedValue);

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
      this.searchMode?.source != lazy.UrlbarUtils.RESULT_SOURCE.ACTIONS
        ? lazy.UrlbarPrefs.get("maxRichResults")
        : UNLIMITED_MAX_RESULTS;
    let options = {
      allowAutofill,
      isPrivate: this.isPrivate,
      sapName: this.sapName,
      maxResults,
      searchString,
      userContextId: parseInt(
        this.window.gBrowser.selectedBrowser.getAttribute("usercontextid") || 0
      ),
      tabGroup: this.window.gBrowser.selectedTab.group?.id ?? null,
      currentPage: this.window.gBrowser.currentURI.spec,
      prohibitRemoteResults:
        event &&
        lazy.UrlbarUtils.isPasteEvent(event) &&
        lazy.UrlbarPrefs.get("maxCharsForSearchSuggestions") <
          event.data?.length,
    };

    if (this.searchMode) {
      options.searchMode = this.searchMode;
      if (this.searchMode.source) {
        options.sources = [this.searchMode.source];
      }
    }

    return new lazy.UrlbarQueryContext(options);
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
    this.controller.engagementEvent.handleBounceEventTrigger(
      event.target.linkedBrowser
    );

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
    if (event.currentTarget == this.window) {
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
        event._disableCanonization =
          AppConstants.platform == "macosx"
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

  async _on_keyup(event) {
    if (event.currentTarget == this.window) {
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
    // for Enter key when any keyup event is detected.
    if (this._keyDownEnterDeferred) {
      if (this._keyDownEnterDeferred.loadedContent) {
        try {
          const loadingBrowser = await this._keyDownEnterDeferred.promise;
          // Ensure the selected browser didn't change in the meanwhile.
          if (this.window.gBrowser.selectedBrowser === loadingBrowser) {
            loadingBrowser.focus();
            // Make sure the domain name stays visible for spoof protection and usability.
            this.inputField.setSelectionRange(0, 0);
          }
        } catch (ex) {
          // Not all the Enter actions in the urlbar will cause a navigation, then it
          // is normal for this to be rejected.
          // If _keyDownEnterDeferred was rejected on keydown, we don't nullify it here
          // to ensure not overwriting the new value created by keydown.
        }
      } else {
        // Discard the _keyDownEnterDeferred promise to receive any key inputs immediately.
        this._keyDownEnterDeferred.resolve();
      }

      this._keyDownEnterDeferred = null;
    }
  }

  _on_compositionstart() {
    if (this.#compositionState == lazy.UrlbarUtils.COMPOSITION.COMPOSING) {
      throw new Error("Trying to start a nested composition?");
    }
    this.#compositionState = lazy.UrlbarUtils.COMPOSITION.COMPOSING;
    this.#compositionHadText = false;

    if (lazy.UrlbarPrefs.get("keepPanelOpenDuringImeComposition")) {
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
    if (this.#compositionState != lazy.UrlbarUtils.COMPOSITION.COMPOSING) {
      throw new Error("Trying to stop a non existing composition?");
    }

    if (!lazy.UrlbarPrefs.get("keepPanelOpenDuringImeComposition")) {
      // Clear the selection and the cached result, since they refer to the
      // state before this composition. A new input even will be generated
      // after this.
      this.view.clearSelection();
      this._resultForCurrentValue = null;
    }

    // We can't yet retrieve the committed value from the editor, since it isn't
    // completely committed yet. We'll handle it at the next input event.
    this.#compositionState = event.data
      ? lazy.UrlbarUtils.COMPOSITION.COMMIT
      : lazy.UrlbarUtils.COMPOSITION.CANCELED;

    // Certain IMEs fire a spurious empty composition after each commit without
    // a subsequent input event. If this composition was empty throughout and it
    // closed the popup, reopen it directly since we can't rely on a following
    // input event.
    if (
      !event.data &&
      !this.#compositionHadText &&
      this.#compositionClosedPopup &&
      !lazy.UrlbarPrefs.get("keepPanelOpenDuringImeComposition")
    ) {
      this.#compositionState = lazy.UrlbarUtils.COMPOSITION.NONE;
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

    let uri = this.makeURIReadable(this.window.gBrowser.currentURI);
    let href = uri.displaySpec;
    let title = this.window.gBrowser.contentTitle || href;

    event.dataTransfer.setData("text/x-moz-url", `${href}\n${title}`);
    event.dataTransfer.setData("text/plain", href);
    event.dataTransfer.setData("text/html", `<a href="${href}">${title}</a>`);
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
    let droppedString = URL.isInstance(droppedData)
      ? droppedData.href
      : droppedData;
    if (droppedString == this.window.gBrowser.currentURI.spec) {
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
    this.controller.setLastQueryContextCache(queryContext);
    this.controller.engagementEvent.start(event, queryContext);
    this.handleNavigation({ triggeringPrincipal: principal });
    // For safety reasons, in the drop case we don't want to immediately show
    // the dropped value, instead we want to keep showing the current page
    // url until an onLocationChange happens.
    // See the handling in `setURI` for further details.
    this.userTypedValue = null;
    this.setURI({ dueToTabSwitch: true });
  }

  _on_uidensitychanged() {
    if (this.#breakoutBlockerCount) {
      return;
    }
    this.#updateLayoutBreakout();
  }

  _on_toolbarvisibilitychange() {
    this.#updateTextboxPositionNextFrame();
  }

  _on_DOMMenuBarActive() {
    this.#updateTextboxPositionNextFrame();
  }

  _on_DOMMenuBarInactive() {
    this.#updateTextboxPositionNextFrame();
  }

  #allTextSelectedOnKeyDown = false;
  get #allTextSelected() {
    return this.selectionStart == 0 && this.selectionEnd == this.value.length;
  }

  /**
   * @param {string} value
   *   A untrimmed address bar input.
   * @returns {nsILoadInfo.SchemelessInputType}
   *   Returns `Ci.nsILoadInfo.SchemelessInputTypeSchemeless` if the input
   *   doesn't start with a scheme relevant for schemeless HTTPS-First
   *   (http://, https:// and file://).
   *   Returns `Ci.nsILoadInfo.SchemelessInputTypeSchemeful` if it does have a scheme.
   */
  #getSchemelessInput(value) {
    return ["http://", "https://", "file://"].every(
      scheme => !value.trim().startsWith(scheme)
    )
      ? Ci.nsILoadInfo.SchemelessInputTypeSchemeless
      : Ci.nsILoadInfo.SchemelessInputTypeSchemeful;
  }

  get #isOpenedPageInBlankTargetLoading() {
    return (
      this.window.gBrowser.selectedBrowser.browsingContext.sessionHistory
        ?.count === 0 &&
      this.window.gBrowser.selectedBrowser.browsingContext
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
    let isMac = AppConstants.platform === "macosx";
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
    return this.window.isBlankPageURL(spec) || spec == "about:privatebrowsing";
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
    if (lazy.UrlbarUtils.stripUnsafeProtocolOnPaste(href) != href) {
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
 * @param {nsIURI} aURI
 *   The URI to decode
 * @returns {string}
 *   The decoded URI
 */
function losslessDecodeURI(aURI) {
  let scheme = aURI.scheme;
  let value = aURI.displaySpec;

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
 * Handles copy and cut commands for the urlbar.
 */
class CopyCutController {
  /**
   * @param {UrlbarInput} urlbar
   *   The UrlbarInput instance to use this controller for.
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
        view: urlbar.window,
        detail: 0,
      });
      urlbar.inputField.dispatchEvent(event);
    }

    lazy.ClipboardHelper.copyString(val);
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
   * @param {UrlbarInput} input The parent UrlbarInput.
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
    return lazy.SearchModeSwitcher.MAX_OPENSEARCH_ENGINES;
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
    return lazy.ObjectUtils.deepEqual(
      engines1.map(e => e.title),
      engines2.map(e => e.title)
    );
  }

  _createMenuitem(engine, index) {
    let elt = this.input.document.createXULElement("menuitem");
    elt.setAttribute("anonid", `add-engine-${index}`);
    elt.classList.add("menuitem-iconic");
    elt.classList.add("context-menu-add-engine");
    this.input.document.l10n.setAttributes(elt, "search-one-offs-add-engine", {
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
    let elt = this.input.document.createXULElement("menu");
    elt.setAttribute("anonid", "add-engine-menu");
    elt.classList.add("menu-iconic");
    elt.classList.add("context-menu-add-engine");
    this.input.document.l10n.setAttributes(
      elt,
      "search-one-offs-add-engine-menu"
    );
    if (engine.icon) {
      elt.setAttribute("image", ChromeUtils.encodeURIForSrcset(engine.icon));
    }
    let popup = this.input.document.createXULElement("menupopup");
    elt.appendChild(popup);
    return elt;
  }

  refreshContextMenu() {
    let engines = this.engines;
    let contextMenu = this.input.querySelector("moz-input-box").menupopup;

    // Certain operations, like customization, destroy and recreate widgets,
    // so we cannot rely on cached elements.
    if (!contextMenu.querySelector(".menuseparator-add-engine")) {
      this.contextSeparator =
        this.input.document.createXULElement("menuseparator");
      this.contextSeparator.setAttribute("anonid", "add-engine-separator");
      this.contextSeparator.classList.add("menuseparator-add-engine");
      this.contextSeparator.collapsed = true;
      contextMenu.appendChild(this.contextSeparator);
    }

    this.contextSeparator.collapsed = !engines.length;
    let curElt = this.contextSeparator;
    // Remove the previous items, if any.
    for (let elt = curElt.nextElementSibling; elt; ) {
      let nextElementSibling = elt.nextElementSibling;
      elt.remove();
      elt = nextElementSibling;
    }

    // If the page provides too many engines, we only show a single menu entry
    // with engines in a submenu.
    if (engines.length > this.maxInlineEngines) {
      // Set the menu button's image to the image of the first engine.  The
      // offered engines may have differing images, so there's no perfect
      // choice here.
      let elt = this._createMenu(engines[0]);
      this.contextSeparator.insertAdjacentElement("afterend", elt);
      curElt = elt.lastElementChild;
    }

    // Insert the engines, either in the contextual menu or the sub menu.
    for (let i = 0; i < engines.length; ++i) {
      let elt = this._createMenuitem(engines[i], i);
      if (curElt.localName == "menupopup") {
        curElt.appendChild(elt);
      } else {
        curElt.insertAdjacentElement("afterend", elt);
      }
      curElt = elt;
    }
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

customElements.define("moz-urlbar", UrlbarInput);
