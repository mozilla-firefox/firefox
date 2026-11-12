/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/**
 * @import { MozButton } from "chrome://global/content/elements/moz-button.mjs";
 * @import { PartialSearchEngine } from "chrome://browser/content/urlbar/SearchEngineStore.mjs"
 * @import { OpenSearchData } from "moz-src:///browser/components/search/OpenSearchManager.sys.mjs"
 * @import { LocalSearchMode } from "chrome://browser/content/urlbar/UrlbarShared.mjs"
 * @import { PanelItem, PanelList } from "chrome://global/content/elements/panel-list.mjs"
 */

import UrlbarPrefs from "chrome://browser/content/urlbar/UrlbarContentPrefs.mjs";
import { UrlbarContentUtils } from "chrome://browser/content/urlbar/UrlbarContentUtils.mjs";
import { UrlbarShared } from "chrome://browser/content/urlbar/UrlbarShared.mjs";

const { XPCOMUtils } =
  typeof ChromeUtils == "undefined"
    ? { XPCOMUtils: null }
    : ChromeUtils.importESModule("resource://gre/modules/XPCOMUtils.sys.mjs");

const lazy = XPCOMUtils?.declareLazy({
  CustomizableUI:
    "moz-src:///browser/components/customizableui/CustomizableUI.sys.mjs",
  OpenSearchManager:
    "moz-src:///browser/components/search/OpenSearchManager.sys.mjs",
  SearchUIUtils: "moz-src:///browser/components/search/SearchUIUtils.sys.mjs",
});

/** @type {Localization} */
let l10n;

function getL10n() {
  l10n ??= new Localization(["browser/browser.ftl"]);
  return l10n;
}

// Default icon used for engines that do not have icons loaded.
const DEFAULT_ENGINE_ICON =
  "chrome://browser/skin/search-engine-placeholder@2x.png";

const SKIP_TAB_STOP_PREF = "searchModeSwitcher.skipTabStop";

// The config engines whose wordmark the New Tab search bar's variants show in
// place of their icon and name. Keyed by the first segment of the engine's
// identifier, so that a regional or per-language engine shares its family's
// wordmark, as ebay-uk and wikipedia-fr do. The images live in
// browser/themes/shared/urlbar/engine-wordmarks/ and are picked in urlbar.css.
const WORDMARK_ENGINE_FAMILIES = new Set([
  "bing",
  "ddg",
  "ebay",
  "google",
  "perplexity",
  "startpage",
  "wikipedia",
]);

// Per-domain counts of how often the address bar install
// engine button is shown.
const ADD_ENGINES_BADGE_PREF = "browser.urlbar.addEnginesBadgeShownCount";
const MAX_ADD_ENGINES_BADGE_SHOWN = 3;

/**
 * Implements the SearchModeSwitcher in the urlbar.
 */
export class SearchModeSwitcher {
  static ICON_GLASS = UrlbarShared.ICON.SEARCH_GLASS;
  static ICON_GLOBE = UrlbarShared.ICON.GLOBE;
  /**
   * The maximum number of openSearch engines available to install
   * to display.
   */
  static MAX_OPENSEARCH_ENGINES = 3;

  /** @type {PanelList} */
  #panelList;
  /** @type {UrlbarInput} */
  #input;
  /** @type {MozButton} */
  #button;
  /** @type {HTMLButtonElement} */
  #closebutton;
  /**
   * Matches when the wordmark images, whose brand colors may not meet a
   * contrast preference, must give way to the engine's icon and name.
   *
   * @type {MediaQueryList}
   */
  #noWordmarkQuery;

  // The value of the urlbar the last time a search mode was changed.
  #lastInputValue;

  /**
   * Keep a cache of the engine list as the keyboard functionality
   * needs sync access to them.
   *
   * @type {(PartialSearchEngine|LocalSearchMode)[]}
   */
  #engines = [];

  /**
   * Store the last page each browser had its badge counted for as we
   * don't overcount page visits when badge is updated.
   *
   * @type {WeakMap<MozBrowser, string>}
   */
  #countedBadgeFor = new WeakMap();

  /**
   * @param {UrlbarInput} input
   */
  constructor(input) {
    this.#input = input;

    this.#panelList = input.querySelector(".searchmode-switcher-panel-list");
    this.#button = input.querySelector(".searchmode-switcher");
    this.#closebutton = input.querySelector(".searchmode-switcher-close");
    if (input.variantB) {
      // On its own row above the input, the button only shows a surface while
      // hovered or pressed.
      this.#button.setAttribute("type", "ghost");
    }

    this.#noWordmarkQuery = window.matchMedia("(prefers-contrast)");

    // MozButton and PanelList have to be hooked up via id.
    this.#panelList.id = "searchmode-switcher-panel-list-" + input.sapName;
    this.#button.setAttribute("menuid", this.#panelList.id);

    // In chrome documents, wrap in a XUL panel to make sure it's
    // on top of the overflow panel and catches all keypresses.
    if (document.createXULElement) {
      let panel = document.createXULElement("panel");
      panel.setAttribute("level", "top");
      panel.setAttribute("consumeoutsideclicks", "false");
      panel.classList.add("searchmode-switcher-panel", "toolbar-menupopup");
      this.#panelList.replaceWith(panel);
      panel.appendChild(this.#panelList);
    }

    if (!UrlbarShared.keywordEnabled(this.#input.sapName)) {
      // Show the keyword disabled icon immediately. For keyword enabled,
      // the icon is updated by the input once the engine store is initialized.
      this.updateSearchIcon();
    }
  }

  /**
   * Called when the input is connected.
   */
  connect() {
    UrlbarPrefs.addObserver(this);

    if (this.#isEnabled()) {
      this.#enableObservers();
    }
  }

  /**
   * Called when the input is disconnected.
   */
  disconnect() {
    UrlbarPrefs.removeObserver(this);
    this.#disableObservers();
  }

  #isEnabled() {
    return (
      UrlbarPrefs.get("scotchBonnet.enableOverride") ||
      this.#input.isSearchbarSAP
    );
  }

  async #onPopupShowing() {
    // Discard event to avoid recording an abandonment.
    this.#input.controller.session.discard();
    // Close the view before anything is awaited. Closing it collapses the
    // urlbar, and the panel aligns itself asynchronously and abandons the open
    // if its anchor moves mid-alignment.
    this.#input.view.close({ showFocusBorder: false });
    await this.#buildSearchModeList();

    if (this.#input.sapName == "urlbar") {
      Glean.urlbarUnifiedsearchbutton.opened.add(1);
    }
  }

  /**
   * Close the SearchSwitcher popup.
   */
  closePanel() {
    this.#panelList.hide(null, { force: true });
  }

  #openPreferences() {
    this.#input.parentController.openPreferences("paneSearch");

    if (this.#input.sapName == "urlbar") {
      Glean.urlbarUnifiedsearchbutton.picked.settings.add(1);
    }
  }

  /**
   * Exit the engine specific searchMode.
   *
   * @param {Event} event
   *        The event that triggered the searchMode exit.
   */
  exitSearchMode(event) {
    event.preventDefault();
    this.#input.searchMode = null;
    // Update the result by the default engine.
    this.#input.startQuery();
  }

  /**
   * Called when the value of the searchMode attribute on UrlbarInput is changed.
   */
  onSearchModeChanged() {
    if (window.closed) {
      return;
    }

    if (this.#isEnabled()) {
      this.updateSearchIcon({ searchModeChanged: true });
    }
  }

  handleEvent(event) {
    if (event.currentTarget.localName == "panel-item") {
      this.#handlePanelItemEvent(event);
      return;
    }
    if (event.currentTarget == this.#closebutton) {
      // Prevent click and mousedown from bubbling up
      // to #button which would open the popup.
      event.stopPropagation();
      if (event.type == "click") {
        this.#input.focus();
        this.exitSearchMode(event);
      }
      return;
    }
    if (event.type == "searchmodechanged") {
      this.onSearchModeChanged();
      return;
    }
    if (event.currentTarget == this.#noWordmarkQuery) {
      if (this.#input.variantA || this.#input.variantB) {
        this.updateSearchIcon();
      }
      return;
    }
    if (event.type == "focus") {
      this.#input.setUnifiedSearchButtonAvailability(true);
      return;
    }
    if (event.type == "focusin") {
      this.#button.tabIndex = 0;
      return;
    }
    if (event.type == "focusout") {
      if (!this.#input.contains(event.relatedTarget)) {
        this.#button.tabIndex = -1;
      }
      return;
    }
    if (event.type == "showing") {
      this.#onPopupShowing();
      return;
    }
    if (event.type == "hidden") {
      if (document.activeElement == this.#button) {
        // This moves the focus to the urlbar when the popup is closed.
        this.#input.focus();
      }
      return;
    }
    if (event.type == "keydown") {
      if (this.#input.view.isOpen) {
        // The urlbar view is open, which means the unified search button got
        // focus by tab key from urlbar.
        switch (event.keyCode) {
          case KeyEvent.DOM_VK_TAB: {
            // Move the focus to urlbar view to make cyclable.
            this.#input.focus();
            this.#input.view.selectBy(1, {
              reverse: event.shiftKey,
              userPressedTab: true,
            });
            event.preventDefault();
            return;
          }
          case KeyEvent.DOM_VK_ESCAPE: {
            this.#input.view.close();
            this.#input.focus();
            event.preventDefault();
            return;
          }
        }
      }

      // Manually open the popup on down.
      if (event.keyCode == KeyEvent.DOM_VK_DOWN) {
        this.#panelList.show(event);
      }
    }
  }

  /**
   * @param {MouseEvent|KeyboardEvent} event
   *  A mouseup, click or keydown event.
   *  Click is used for regular mouse clicks.
   *  Keydown is used for keyboard clicks (bug 1245292).
   *  Auxclick is used for middle clicks.
   */
  #handlePanelItemEvent(event) {
    switch (event.type) {
      case "click": {
        let mouseEvent = /** @type {MouseEvent} */ (event);
        // Prevent the panel from closing. We handle that manually.
        mouseEvent.stopPropagation();

        if (mouseEvent.inputSource == MouseEvent.MOZ_SOURCE_KEYBOARD) {
          // Keyboard clicks always have shiftKey=false due to bug 1245292.
          // For now, we handle them on keydown instead.
          return;
        }
        break;
      }
      case "keydown": {
        let keyboardEvent = /** @type {KeyboardEvent} */ (event);
        if (
          keyboardEvent.keyCode != KeyEvent.DOM_VK_SPACE &&
          keyboardEvent.keyCode != KeyEvent.DOM_VK_RETURN
        ) {
          return;
        }
        event.preventDefault();
        break;
      }
      case "auxclick": {
        let mouseEvent = /** @type {MouseEvent} */ (event);
        if (mouseEvent.button != 1) {
          // Ignore non-middle-auxclicks.
          return;
        }
        break;
      }
    }

    let panelItem = /** @type {PanelItem} */ (event.currentTarget);
    switch (panelItem.dataset.action) {
      case "openpreferences": {
        this.closePanel();
        this.#openPreferences();
        break;
      }
      case "searchmode": {
        // #remoteSearch() decides whether to close the panel or keep it open.
        let engineId = panelItem.dataset.engineId;
        let engine = this.#input.controller.engineStore.getEngine(engineId);
        this.#remoteSearch(engine, event);
        break;
      }
      case "localsearchmode": {
        this.closePanel();
        let restrict = panelItem.dataset.restrict;
        this.#localSearch(restrict);
        break;
      }
      case "installopensearch": {
        this.closePanel();
        // @ts-expect-error
        let engine = panelItem._engine;
        this.#installOpenSearchEngine(engine);
        break;
      }
    }
  }

  /**
   * @param {PanelItem} panelItem
   */
  #addCommandListeners(panelItem) {
    panelItem.addEventListener("click", this);
    panelItem.addEventListener("keydown", this);
    panelItem.addEventListener("auxclick", this);
  }

  onSearchEngineUpdate = (modifiedType, _engine) => {
    if (window.closed) {
      return;
    }

    this.#engines = [];

    switch (modifiedType) {
      case "changed":
      case "default":
        this.updateSearchIcon();
    }
  };

  /**
   * Called when a urlbar pref changes.
   *
   * @param {string} pref
   *   The name of the pref relative to `browser.urlbar`.
   */
  onPrefChanged(pref) {
    if (window.closed) {
      return;
    }

    if (pref == SKIP_TAB_STOP_PREF) {
      if (this.#isEnabled()) {
        if (UrlbarPrefs.get(pref)) {
          this.#enableSkipTabStop();
        } else {
          this.#disableSkipTabStop();
        }
      }
      return;
    }

    if (this.#input.isSearchbarSAP) {
      // A search bar cares about neither of the two remaining prefs.
      return;
    }

    switch (pref) {
      case "scotchBonnet.enableOverride": {
        if (UrlbarPrefs.get("scotchBonnet.enableOverride")) {
          this.#enableObservers();
          this.updateSearchIcon();
        } else {
          this.#disableObservers();
        }
        break;
      }
      case "keyword.enabled": {
        if (UrlbarPrefs.get("scotchBonnet.enableOverride")) {
          this.updateSearchIcon();
        }
        break;
      }
    }
  }

  /**
   * If the user presses Option+Up or Option+Down we open the engine list.
   *
   * @param {KeyboardEvent} event
   *   The key down event.
   */
  handleKeyDown(event) {
    if (
      (event.keyCode == KeyEvent.DOM_VK_UP ||
        event.keyCode == KeyEvent.DOM_VK_DOWN) &&
      (event.altKey || event.getModifierState("Accel"))
    ) {
      if (event.altKey) {
        this.#handleAltUpDown(event);
      } else if (event.getModifierState("Accel")) {
        this.#handleAccelUpDown(event);
      }
      event.stopPropagation();
      event.preventDefault();
      return true;
    }
    return false;
  }

  #handleAltUpDown(event) {
    this.#input.controller.focusOnUnifiedSearchButton();
    this.#panelList.show(event, this.#button);
  }

  async #handleAccelUpDown(event) {
    if (!this.#engines.length) {
      await this.#populateEngines();
    }

    let newIndex =
      this.#selectedIndex + (event.keyCode == KeyEvent.DOM_VK_UP ? -1 : 1);
    if (newIndex > this.#engines.length - 1) {
      newIndex = 0;
    }
    if (newIndex < 0) {
      newIndex = this.#engines.length - 1;
    }

    let selectedEngine = this.#engines[newIndex];
    if ("name" in selectedEngine) {
      this.#input.setSearchMode(
        {
          entry: "searchbutton",
          isPreview: false,
          source: UrlbarShared.RESULT_SOURCE.SEARCH,
          engineName: selectedEngine.name,
        },
        this.#input.browserWindow?.gBrowser.selectedBrowser
      );
    } else if ("source" in selectedEngine) {
      this.#input.setSearchMode(
        {
          entry: "searchbutton",
          isPreview: false,
          source: selectedEngine.source,
        },
        this.#input.browserWindow?.gBrowser.selectedBrowser
      );
    }

    this.#input.startQuery({ allowAutofill: false });
  }

  async #populateEngines() {
    try {
      await this.#input.controller.engineStore.init();
      this.#engines = this.#input.controller.engineStore
        .getEngines()
        .filter(engine => !engine.hideOneOffButton);
    } catch {
      // Search service failed but we still offer local search modes.
      this.#engines = [];
    }

    if (this.#input.sapName == "urlbar") {
      // After the settings redesign we no longer use the prefs to hide local
      // search modes. Hence when the settings redesign is enabled we show
      // all local search modes regardless of the prefs.
      this.#engines = this.#engines.concat(
        UrlbarShared.LOCAL_SEARCH_MODES.filter(
          engine =>
            UrlbarPrefs.get("browser.settings-redesign.enabled") ||
            UrlbarPrefs.get(engine.pref)
        )
      );
    }
  }

  get #selectedIndex() {
    if (!this.#input.searchMode) {
      return this.#engines.indexOf(this.#input.controller.engineStore.default);
    }
    if (this.#input.searchMode.engineName) {
      return this.#engines.findIndex(
        e => "name" in e && e.name == this.#input.searchMode.engineName
      );
    }
    return this.#engines.findIndex(
      e => "source" in e && e.source == this.#input.searchMode.source
    );
  }

  /**
   * Badges the unified search button to indicate that the current page offers
   * engines that can be added.
   *
   * @param {boolean} show
   */
  toggleAddEnginesBadge(show) {
    if (this.#input.isSearchbarSAP) {
      this.#button.toggleAttribute("addengines", show);
      return;
    }

    if (
      !show ||
      !UrlbarPrefs.get("unifiedSearchButton.always") ||
      this.#hasAdjacentSearchbar
    ) {
      this.#button.removeAttribute("addengines");
      return;
    }

    this.#badgeIfUnderSiteCap();
  }

  /**
   * Whether the dedicated search bar is in the toolbar.
   *
   * @returns {boolean}
   */
  get #hasAdjacentSearchbar() {
    if (this.#input.isSearchbarSAP) {
      throw new Error(
        "#hasAdjacentSearchbar should not be called from search bar"
      );
    }
    return !!lazy?.CustomizableUI.getPlacementOfWidget("search-container");
  }

  /**
   * @returns {nsIContentPrefService2}
   */
  get #contentPrefs() {
    return Cc["@mozilla.org/content-pref/service;1"].getService(
      Ci.nsIContentPrefService2
    );
  }

  /**
   * Shows the addEngines badge unless this site has already
   * had a badge shown 3 times.
   */
  #badgeIfUnderSiteCap() {
    // Content prefs are chrome-only.
    if (!lazy) {
      throw new Error("addEngine badge code should not be called in content");
    }
    let browser = this.#input.browserWindow?.gBrowser?.selectedBrowser;
    let uri = browser?.currentURI;
    if (!uri) {
      return;
    }
    let spec = uri.spec;
    let context = browser.loadContext;

    let apply = count => {
      // The button may have moved on to another page while an async read was
      // in flight.
      if (browser != this.#input.browserWindow?.gBrowser?.selectedBrowser) {
        return;
      }
      // Don't hide already shown badges.
      let show =
        this.#countedBadgeFor.get(browser) == spec ||
        count < MAX_ADD_ENGINES_BADGE_SHOWN;
      this.#button.toggleAttribute("addengines", show);
      if (show) {
        this.#countBadgeShown(browser, spec, count);
      }
    };

    let cached = this.#contentPrefs.getCachedByDomainAndName(
      spec,
      ADD_ENGINES_BADGE_PREF,
      context
    );
    if (cached) {
      apply(Number(cached.value) || 0);
      return;
    }

    let count = 0;
    this.#contentPrefs.getByDomainAndName(
      spec,
      ADD_ENGINES_BADGE_PREF,
      context,
      {
        handleResult(pref) {
          count = Number(pref.value) || 0;
        },
        handleError() {},
        handleCompletion: () => apply(count),
      }
    );
  }

  /**
   * Counts one showing for this page, once per page rather than once per call:
   * the badge is refreshed several times for a single visit.
   *
   * @param {MozBrowser} browser
   * @param {string} spec
   * @param {number} count
   */
  #countBadgeShown(browser, spec, count) {
    if (this.#countedBadgeFor.get(browser) == spec) {
      return;
    }
    this.#countedBadgeFor.set(browser, spec);
    this.#contentPrefs.set(
      spec,
      ADD_ENGINES_BADGE_PREF,
      /** @type {any} */ (count + 1),
      browser.loadContext,
      null
    );
  }

  /**
   * Update the icon shown in the urlbar.
   *
   * @param {object} [options]
   * @param {boolean} [options.searchModeChanged]
   *        Optional flag to note whether the icon is being updated due
   *        the search mode being changed.
   */
  async updateSearchIcon(options = {}) {
    let { source, engineName } = this.#input.searchMode ?? {};
    let { label, icon, wordmark } = await this.#getSearchIcon(options);
    let searchMode = this.#input.searchMode;
    if (
      !icon ||
      source != searchMode?.source ||
      engineName != searchMode?.engineName
    ) {
      return;
    }
    if (wordmark) {
      this.#button.removeAttribute("iconsrc");
      this.#button.setAttribute("wordmark", wordmark);
    } else {
      this.#button.setAttribute("iconsrc", icon);
      this.#button.removeAttribute("wordmark");
    }

    // The New Tab variants name the engine next to its icon unless a wordmark,
    // which already spells the name out, is taking the icon's place.
    let showLabel =
      !wordmark &&
      (!!this.#input.searchMode ||
        this.#input.variantA ||
        this.#input.variantB);
    let labelEl = this.#input.querySelector(".searchmode-switcher-title");
    if (showLabel) {
      labelEl.textContent = label;
    } else {
      labelEl.replaceChildren();
    }

    if (!UrlbarShared.keywordEnabled(this.#input.sapName)) {
      await this.#setButtonTitle("urlbar-searchmode-no-keyword2");
    } else if (label) {
      await this.#setButtonTitle("urlbar-searchmode-button3", {
        engine: label,
      });
    } else {
      await this.#setButtonTitle("urlbar-searchmode-button-no-engine2");
    }
  }

  #buttonTitleRequest = 0;

  /**
   * Sets the button's tooltip from a Fluent message's title, and mirrors it as
   * the accessible name, which would otherwise be computed from the button's
   * content: the engine's name and the close button's label in search mode.
   *
   * @param {string} id
   *   The Fluent message id.
   * @param {object} [args]
   *   The message's arguments.
   */
  async #setButtonTitle(id, args) {
    let request = ++this.#buttonTitleRequest;
    let [message] = await document.l10n.formatMessages([{ id, args }]);
    if (request != this.#buttonTitleRequest) {
      return;
    }
    let title = message.attributes.find(a => a.name == "title").value;
    this.#button.removeAttribute("data-l10n-id");
    this.#button.title = title;
    this.#button.ariaLabel = title;
  }

  async #getSearchIcon({ searchModeChanged = false }) {
    let searchMode = this.#input.searchMode;

    if (!UrlbarShared.keywordEnabled(this.#input.sapName) && !searchMode) {
      return { icon: SearchModeSwitcher.ICON_GLOBE };
    }

    // If we are updating because searchMode changed, record the value of the urlbar.
    if (searchModeChanged) {
      this.#lastInputValue = this.#input.value;
    } else if (
      this.#lastInputValue &&
      this.#lastInputValue != this.#input.value
    ) {
      // If the urlbar value is stored, only update the icon when we see a new value.
      this.#lastInputValue = null;
    }

    if (
      UrlbarPrefs.get("unifiedSearchButton.always") &&
      !this.#lastInputValue &&
      this.#input.focused &&
      this.#input.value.length
    ) {
      let result = this.#input.view?.getResultAtIndex(0);
      if (
        result &&
        (result.type == UrlbarShared.RESULT_TYPE.URL ||
          result.type == UrlbarShared.RESULT_TYPE.TAB_SWITCH)
      ) {
        // If the user has typed a url then indicate that ENTER will visit
        // that address.
        return { icon: SearchModeSwitcher.ICON_GLOBE };
      }
    }

    return this.#getDisplayedEngineDetails(searchMode);
  }

  /**
   * The wordmark to show for an engine in place of its icon and name.
   *
   * @param {PartialSearchEngine} engine
   *   The engine the button shows.
   * @returns {?string}
   *   The engine family whose wordmark to show, or null to show the engine's
   *   icon.
   */
  #getEngineWordmark(engine) {
    if (
      !(this.#input.variantA || this.#input.variantB) ||
      !engine.isConfigEngine ||
      this.#noWordmarkQuery.matches
    ) {
      return null;
    }
    let family = engine.id.split("-")[0];
    return WORDMARK_ENGINE_FAMILIES.has(family) ? family : null;
  }

  async #getSearchModeLabel(source) {
    let mode = UrlbarShared.LOCAL_SEARCH_MODES.find(m => m.source == source);
    let [str] = await getL10n().formatMessages([{ id: mode.uiLabel }]);
    return str.value;
  }

  async #getDisplayedEngineDetails(searchMode = null) {
    if (!searchMode || searchMode.engineName) {
      try {
        await this.#input.controller.engineStore.init();
      } catch {
        return { label: null, icon: SearchModeSwitcher.ICON_GLASS };
      }

      let engine = searchMode
        ? this.#input.controller.engineStore.getEngineByName(
            searchMode.engineName
          )
        : this.#input.controller.engineStore.default;
      if (!engine) {
        return { label: null, icon: SearchModeSwitcher.ICON_GLASS };
      }
      let icon = (await engine.getIconURL()) ?? SearchModeSwitcher.ICON_GLASS;
      return {
        label: engine.name,
        icon,
        wordmark: this.#getEngineWordmark(engine),
      };
    }

    let mode = UrlbarShared.LOCAL_SEARCH_MODES.find(
      m => m.source == searchMode.source
    );
    return {
      label: await this.#getSearchModeLabel(searchMode.source),
      icon: mode.icon,
    };
  }

  /**
   * Builds the popup and dispatches a rebuild event on the popup when finished.
   */
  async #buildSearchModeList() {
    for (let item of this.#panelList.querySelectorAll("panel-item")) {
      item.remove();
    }

    let installedEngineSeparator = this.#panelList.querySelector(
      ".searchmode-switcher-panel-installed-engine-separator"
    );
    let footerSeparator = this.#panelList.querySelector(
      ".searchmode-switcher-panel-footer-separator"
    );

    await this.#populateEngines();
    for (let engine of this.#engines) {
      if ("source" in engine) {
        footerSeparator.before(await this.#buildLocalSearchButton(engine));
      } else if ("name" in engine) {
        let menuitem = await this.#buildEngineSearchButton(engine);
        installedEngineSeparator.before(menuitem);
      }
    }
    this.#buildSettingsButton();

    // Add engines that can be installed. Only a browser window has a page to
    // offer them from.
    let browser = this.#input.browserWindow?.gBrowser;
    let openSearchEngines = browser
      ? lazy.OpenSearchManager.getInstallableEngines(browser.selectedBrowser)
      : [];
    openSearchEngines = openSearchEngines.slice(
      0,
      SearchModeSwitcher.MAX_OPENSEARCH_ENGINES
    );

    for (let engine of openSearchEngines) {
      let menuitem = this.#createButton(engine.icon);
      document.l10n.setAttributes(
        menuitem,
        "urlbar-searchmode-popup-add-engine",
        {
          engineName: engine.title,
        }
      );
      menuitem.classList.add("searchmode-switcher-addEngine");
      menuitem.dataset.action = "installopensearch";
      // This attribute is for testing.
      menuitem.dataset.engineName = engine.title;
      this.#addCommandListeners(menuitem);
      // @ts-expect-error
      menuitem._engine = engine;

      footerSeparator.after(menuitem);
    }

    if (this.#panelList.wasOpenedByKeyboard) {
      // Focus will not be on first item anymore because new
      // items were added after the panel list was shown.
      this.#panelList.focusWalker.currentNode = this.#panelList;
      this.#panelList.focusWalker.nextNode();
    }

    // Hide footer separator if there are no menuitems between both separators.
    footerSeparator.toggleAttribute(
      "hidden",
      footerSeparator.previousElementSibling == installedEngineSeparator
    );

    this.#panelList.dispatchEvent(new Event("rebuild"));
  }

  /**
   * @param {MouseEvent|KeyboardEvent} event
   * @returns {string}
   *   Where the search engine result page should be opened.
   */
  #whereToOpenSerp(event) {
    let where = UrlbarContentUtils.whereToOpenLink(event);
    // Usually, shift means "open in new window", but in the search
    // mode switcher it means "open SERP even if urlbar is empty",
    // so we just return tab, tabshifted or current but never window.
    if (where.startsWith("tab")) {
      return where;
    }
    return "current";
  }

  /**
   * Ideally the settings button would be in the markup because it never
   * changes but that causes an an assertion error in BindingUtils.cpp.
   */
  #buildSettingsButton() {
    // Icon is set via css based on the class.
    let menuitem = this.#createButton(undefined);
    menuitem.classList.add("searchmode-switcher-panel-search-settings-button");
    menuitem.dataset.action = "openpreferences";
    document.l10n.setAttributes(
      menuitem,
      UrlbarPrefs.get("browser.nova.enabled")
        ? "urlbar-searchmode-popup-settings2"
        : "urlbar-searchmode-popup-search-settings2"
    );
    this.#addCommandListeners(menuitem);
    this.#panelList.appendChild(menuitem);
  }

  /**
   * @param {PartialSearchEngine} engine
   */
  async #buildEngineSearchButton(engine) {
    let icon = await engine.getIconURL();
    let menuitem = this.#createButton(icon, engine.name);
    menuitem.classList.add("searchmode-switcher-installed");
    menuitem.setAttribute("label", engine.name);
    menuitem.setAttribute("title", engine.name);
    menuitem.setAttribute("closemenu", "none");

    if (engine.isNew() && engine.isAppProvided) {
      menuitem.setAttribute("badge-type", "new");
    }

    menuitem.dataset.engineId = engine.id;
    // This attribute is for testing.
    menuitem.dataset.engineName = engine.name;
    menuitem.dataset.action = "searchmode";
    this.#addCommandListeners(menuitem);
    return menuitem;
  }

  /**
   * @param {LocalSearchMode} mode
   * @returns {Promise<PanelItem>}
   */
  async #buildLocalSearchButton(mode) {
    let sourceName = UrlbarShared.getResultSourceName(mode.source);
    let { icon } = await this.#getDisplayedEngineDetails(mode);
    let menuitem = this.#createButton(icon);
    menuitem.classList.add(
      "searchmode-switcher-local",
      `search-button-${sourceName}`
    );
    menuitem.dataset.action = "localsearchmode";
    menuitem.dataset.restrict = mode.restrict;
    this.#addCommandListeners(menuitem);
    document.l10n.setAttributes(menuitem, mode.uiLabel);
    if (mode.keyId) {
      menuitem.setAttribute("key", mode.keyId);
      lazy.CustomizableUI.addShortcut(menuitem);
    }
    return menuitem;
  }

  /**
   * Enables a local search mode based on the restrict token.
   *
   * @param {string} restrict
   *   The restrict token
   */
  #localSearch(restrict) {
    this.#input.search(restrict + " " + this.#getSearchString(), {
      searchModeEntry: "searchbutton",
    });

    if (this.#input.sapName == "urlbar") {
      Glean.urlbarUnifiedsearchbutton.picked.local_search.add(1);
    }
  }

  /**
   * Enters searchmode in the urlbar or opens a SERP, depending
   * on modifier keys. Also handles closing the panel.
   *
   * @param {PartialSearchEngine} searchEngine
   *   The engine to search with.
   * @param {KeyboardEvent|MouseEvent} event
   *   The event that triggered the search.
   */
  #remoteSearch(searchEngine, event) {
    let whereToOpenSerp = this.#whereToOpenSerp(event);
    let searchString = this.#getSearchString();
    if (!event.shiftKey && whereToOpenSerp == "current") {
      // Go into searchmode.
      this.closePanel();
      this.#input.search(searchString, {
        searchEngine,
        searchModeEntry: "searchbutton",
      });
    } else {
      // Go directly to SERP.
      if (whereToOpenSerp == "current") {
        this.closePanel();
      }

      this.#input.openSearchEnginePage(searchString, {
        event,
        searchEngine,
        where: whereToOpenSerp,
        inBackground: true,
      });
    }

    if (this.#input.sapName == "urlbar") {
      // TODO do we really need to distinguish here?
      Glean.urlbarUnifiedsearchbutton.picked[
        searchEngine.isConfigEngine ? "builtin_search" : "addon_search"
      ].add(1);
    }
  }

  /**
   * The string to use when starting a search via the search mode switcher.
   *
   * @returns {string}
   */
  #getSearchString() {
    if (this.#input.getAttribute("pageproxystate") == "valid") {
      return "";
    }
    return this.#input.value;
  }

  /**
   * Returns whether the event's target is an item
   * in the search mode switcher popup.
   *
   * @param {Event|null|undefined} event
   * @returns {boolean}
   */
  eventTargetIsPanelItem(event) {
    let target = event?.target;
    if (!target || !("classList" in target)) {
      return false;
    }
    let classList = /** @type {DOMTokenList}*/ (target.classList);

    return (
      classList.contains("searchmode-switcher-addEngine") ||
      classList.contains("searchmode-switcher-installed") ||
      classList.contains("searchmode-switcher-local")
    );
  }

  #enableObservers() {
    this.#input.controller.engineStore.addObserver(this.onSearchEngineUpdate);

    this.#button.addEventListener("focus", this);
    this.#button.addEventListener("keydown", this);

    if (UrlbarPrefs.get(SKIP_TAB_STOP_PREF)) {
      this.#enableSkipTabStop();
    }

    this.#panelList.addEventListener("showing", this);
    this.#panelList.addEventListener("hidden", this);

    this.#closebutton.addEventListener("click", this);
    this.#closebutton.addEventListener("mousedown", this);

    this.#input.addEventListener("searchmodechanged", this);
    this.#noWordmarkQuery.addEventListener("change", this);
  }

  #disableObservers() {
    this.#input.controller.engineStore.removeObserver(
      this.onSearchEngineUpdate
    );

    this.#button.removeEventListener("focus", this);
    this.#button.removeEventListener("keydown", this);

    this.#disableSkipTabStop();

    this.#panelList.removeEventListener("showing", this);
    this.#panelList.removeEventListener("hidden", this);

    this.#closebutton.removeEventListener("click", this);
    this.#closebutton.removeEventListener("mousedown", this);

    this.#input.removeEventListener("searchmodechanged", this);
    this.#noWordmarkQuery.removeEventListener("change", this);
  }

  /**
   * The button precedes the input, so it's what the toolbar tab stop in front
   * of the widget redirects to. Declining that redirect and joining the tab
   * order only while the widget has focus makes Tab land on the input, with
   * the button reached by Shift+Tab from there.
   */
  #enableSkipTabStop() {
    this.#button.setAttribute("keyNav", "skipTabStop");
    this.#input.addEventListener("focusin", this);
    this.#input.addEventListener("focusout", this);
  }

  #disableSkipTabStop() {
    this.#button.removeAttribute("keyNav");
    this.#button.tabIndex = -1;
    this.#input.removeEventListener("focusin", this);
    this.#input.removeEventListener("focusout", this);
  }

  /**
   * @param {string|undefined} icon
   *   The icon. Pass undefined to use the default engine icon.
   * @param {string} [label]
   *   The label. Can be omitted when setting it via fluent.
   */
  #createButton(icon, label) {
    let panelitem = /**@type {PanelItem} */ (
      document.createElement("panel-item")
    );
    if (label) {
      panelitem.textContent = label;
    }
    panelitem.style.setProperty(
      "--icon-url",
      `url(${icon ?? DEFAULT_ENGINE_ICON})`
    );

    return panelitem;
  }

  /**
   * Installs open search engine and enters search mode.
   *
   * @param {OpenSearchData} engine
   *   The engine to install.
   */
  async #installOpenSearchEngine(engine) {
    /** @type {(_: string, newEngine: PartialSearchEngine) => void} */
    let observer = (_, newEngine) => {
      this.#input.search(this.#getSearchString(), {
        searchEngine: newEngine,
        searchModeEntry: "searchbutton",
      });

      this.#input.controller.engineStore.removeObserver(observer);
    };
    this.#input.controller.engineStore.addObserver(observer);
    if (this.#input.sapName == "urlbar") {
      Glean.urlbarUnifiedsearchbutton.picked.addon_search.add(1);
    }

    await lazy.SearchUIUtils.addOpenSearchEngine(
      engine.uri,
      engine.icon,
      this.#input.browserWindow.gBrowser.selectedBrowser.browsingContext
    );
  }
}
