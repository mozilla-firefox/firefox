/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/**
 * @import MozButton from "chrome://global/content/elements/moz-button.mjs";
 * @import { PartialSearchEngine } from "chrome://browser/content/urlbar/SearchEngineStore.mjs"
 * @import { OpenSearchData } from "moz-src:///browser/components/search/OpenSearchManager.sys.mjs"
 * @import { LocalSearchMode } from "chrome://browser/content/urlbar/UrlbarShared.mjs"
 * @import { PanelItem, PanelList } from "chrome://global/content/elements/panel-list.mjs"
 */

import UrlbarPrefs from "chrome://browser/content/urlbar/UrlbarContentPrefs.mjs";
import * as UrlbarContentUtils from "chrome://browser/content/urlbar/UrlbarContentUtils.mjs";
import { UrlbarShared } from "chrome://browser/content/urlbar/UrlbarShared.mjs";

const lazy = typeof ChromeUtils != "undefined" ? {} : null;

if (lazy) {
  ChromeUtils.defineESModuleGetters(lazy, {
    OpenSearchManager:
      "moz-src:///browser/components/search/OpenSearchManager.sys.mjs",
    SearchUIUtils: "moz-src:///browser/components/search/SearchUIUtils.sys.mjs",
  });
}

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

  // The value of the urlbar the last time a search mode was changed.
  #lastInputValue;

  // Keep a cache of the engine list as the keyboard functionality
  // needs sync access to them.
  #engines = [];
  // Keep track of the currently selected engine when the user is cycling
  // through them with Accel+Up/Down.
  #selectedIndex = 0;

  /**
   * @param {UrlbarInput} input
   */
  constructor(input) {
    this.#input = input;

    this.#panelList = input.querySelector(".searchmode-switcher-panel-list");
    this.#button = input.querySelector(".searchmode-switcher");
    this.#closebutton = input.querySelector(".searchmode-switcher-close");

    // MozButton and PanelList have to be hooked up via id.
    this.#panelList.id = "searchmode-switcher-panel-list-" + input.sapName;
    this.#button.setAttribute("menuid", this.#panelList.id);

    // In XUL documents, wrap in a XUL panel to make sure it's
    // on top of the overflow panel and catches all keypresses.
    let doc = this.#panelList.ownerDocument;
    if (doc.createXULElement) {
      let panel = doc.createXULElement("panel");
      panel.setAttribute("level", "top");
      panel.setAttribute("consumeoutsideclicks", "false");
      panel.classList.add("searchmode-switcher-panel", "toolbar-menupopup");
      this.#panelList.replaceWith(panel);
      panel.appendChild(this.#panelList);
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
      this.#input.sapName == "searchbar"
    );
  }

  async #onPopupShowing() {
    // Discard event to avoid recording an abandonment.
    this.#input.controller.engagementEvent.discard();
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
    this.#input.window.openPreferences("paneSearch");

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
    this.#selectedIndex = 0;
    this.#engines = [];
    // Update the result by the default engine.
    this.#input.startQuery();
  }

  /**
   * Called when the value of the searchMode attribute on UrlbarInput is changed.
   */
  onSearchModeChanged() {
    if (!this.#input.window || this.#input.window.closed) {
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
      if (this.#input.document.activeElement == this.#button) {
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
        // Prevent the keystroke from generating a
        // click event and reopening the switcher.
        keyboardEvent.preventDefault();
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
    if (!this.#input.window || this.#input.window.closed) {
      return;
    }

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
    if (!this.#input.window || this.#input.window.closed) {
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

    if (this.#input.sapName == "searchbar") {
      // The searchbar cares about neither of the two remaining prefs.
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
    this.#selectedIndex += event.keyCode == KeyEvent.DOM_VK_UP ? -1 : 1;
    if (this.#selectedIndex > this.#engines.length - 1) {
      this.#selectedIndex = 0;
    }
    if (this.#selectedIndex < 0) {
      this.#selectedIndex = this.#engines.length - 1;
    }
    let selectedEngine = this.#engines[this.#selectedIndex];
    this.#input.setSearchMode(
      {
        entry: "searchbutton",
        isPreview: false,
        source: selectedEngine?.source || UrlbarShared.RESULT_SOURCE.SEARCH,
        engineName: selectedEngine?.name,
      },
      this.#input.window.gBrowser.selectedBrowser
    );

    let searchString = this.#getSearchString();
    if (searchString) {
      this.#input.startQuery({ allowAutofill: false });
    }
  }

  async #populateEngines() {
    let searchEngines = [];

    try {
      await this.#input.controller.engineStore.init();
      searchEngines = this.#input.controller.engineStore
        .getEngines()
        .filter(engine => !engine.hideOneOffButton);
    } catch {
      // Search service failed but we still offer local search modes.
    }

    if (this.#input.sapName != "urlbar") {
      this.#engines = searchEngines;
    } else {
      // After the settings redesign we no longer use the prefs to hide local
      // search modes. Hence when the settings redesign is enabled we show
      // all local search modes regardless of the prefs.
      this.#engines = searchEngines.concat(
        UrlbarShared.LOCAL_SEARCH_MODES.filter(
          engine =>
            UrlbarPrefs.get("browser.settings-redesign.enabled") ||
            UrlbarPrefs.get(engine.pref)
        )
      );
    }
  }

  /**
   * Badges the unified search button to indicate that the current page offers
   * engines that can be added.
   *
   * @param {boolean} show
   */
  toggleAddEnginesBadge(show) {
    this.#button.toggleAttribute("addengines", show);
  }

  /**
   * Update the icon shown in the urlbar.
   *
   * @param {object} [options]
   * @param [options.searchModeChanged]
   *        Optional flag to note whether the icon is being updated due
   *        the search mode being changed.
   */

  async updateSearchIcon(options = {}) {
    let { label, icon } = await this.#getSearchIcon(options);
    if (!icon) {
      return;
    }
    this.#button.setAttribute("iconsrc", icon);

    if (label) {
      this.#input.document.l10n.setAttributes(
        this.#button,
        "urlbar-searchmode-button3",
        { engine: label }
      );
    } else {
      this.#input.document.l10n.setAttributes(
        this.#button,
        "urlbar-searchmode-button-no-engine2"
      );
    }

    let labelEl = this.#input.querySelector(".searchmode-switcher-title");
    if (!this.#input.searchMode) {
      labelEl.replaceChildren();
    } else {
      labelEl.textContent = label;
    }

    if (
      !UrlbarPrefs.get("keyword.enabled") &&
      this.#input.sapName != "searchbar"
    ) {
      this.#input.document.l10n.setAttributes(
        this.#button,
        "urlbar-searchmode-no-keyword2"
      );
    }
  }

  async #getSearchIcon({ searchModeChanged = false }) {
    let searchMode = this.#input.searchMode;

    try {
      await this.#input.controller.engineStore.init();
    } catch {
      // Search service failed but we continue anyways.
    }

    if (
      this.#input.sapName != "searchbar" &&
      !UrlbarPrefs.get("keyword.enabled") &&
      !searchMode
    ) {
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

  async #getSearchModeLabel(source) {
    let mode = UrlbarShared.LOCAL_SEARCH_MODES.find(m => m.source == source);
    let [str] = await getL10n().formatMessages([{ id: mode.uiLabel }]);
    return str.value;
  }

  async #getDisplayedEngineDetails(searchMode = null) {
    if (!searchMode || searchMode.engineName) {
      let engine = searchMode
        ? this.#input.controller.engineStore.getEngineByName(
            searchMode.engineName
          )
        : this.#input.controller.engineStore.default;
      if (!engine) {
        return { label: null, icon: SearchModeSwitcher.ICON_GLASS };
      }
      let icon = (await engine.getIconURL()) ?? SearchModeSwitcher.ICON_GLASS;
      return { label: engine.name, icon };
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

    let browser = this.#input.window.gBrowser;
    let installedEngineSeparator = this.#panelList.querySelector(
      ".searchmode-switcher-panel-installed-engine-separator"
    );
    let footerSeparator = this.#panelList.querySelector(
      ".searchmode-switcher-panel-footer-separator"
    );

    await this.#populateEngines();
    for (let engine of this.#engines) {
      if (engine.source) {
        footerSeparator.before(await this.#buildLocalSearchButton(engine));
      } else if (engine.name) {
        let menuitem = await this.#buildEngineSearchButton(engine);
        installedEngineSeparator.before(menuitem);
      }
    }
    this.#buildSettingsButton();

    // Add engines that can be installed. Only a browser window has a page to
    // offer them from.
    let openSearchEngines = browser
      ? lazy.OpenSearchManager.getInstallableEngines(browser.selectedBrowser)
      : [];
    openSearchEngines = openSearchEngines.slice(
      0,
      SearchModeSwitcher.MAX_OPENSEARCH_ENGINES
    );

    for (let engine of openSearchEngines) {
      let menuitem = this.#createButton(engine.icon);
      this.#input.document.l10n.setAttributes(
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
    menuitem.setAttribute("data-l10n-attrs", "accesskey");
    this.#input.document.l10n.setAttributes(
      menuitem,
      UrlbarPrefs.get("browser.nova.enabled")
        ? "urlbar-searchmode-popup-settings"
        : "urlbar-searchmode-popup-search-settings"
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
    menuitem.setAttribute("accesskey", engine.name[0]);
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
    menuitem.setAttribute("data-l10n-attrs", "accesskey");
    this.#addCommandListeners(menuitem);
    this.#input.document.l10n.setAttributes(menuitem, mode.uiLabel);
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
      this.#input.document.createElementNS(
        "http://www.w3.org/1999/xhtml",
        "panel-item"
      )
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
      this.#input.window.gBrowser.selectedBrowser.browsingContext
    );
  }
}
