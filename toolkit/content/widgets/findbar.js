/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

"use strict";

// This is loaded into chrome windows with the subscript loader. Wrap in
// a block to prevent accidentally leaking globals onto `window`.
{
  const { AppConstants } = ChromeUtils.importESModule(
    "resource://gre/modules/AppConstants.sys.mjs"
  );

  const PREFS_TO_OBSERVE_BOOL = new Map([
    ["findAsYouType", "accessibility.typeaheadfind"],
    ["manualFAYT", "accessibility.typeaheadfind.manual"],
    ["typeAheadLinksOnly", "accessibility.typeaheadfind.linksonly"],
    ["entireWord", "findbar.entireword"],
    ["highlightAll", "findbar.highlightAll"],
    ["useModalHighlight", "findbar.modalHighlight"],
  ]);
  const PREFS_TO_OBSERVE_INT = new Map([
    ["typeAheadCaseSensitive", "accessibility.typeaheadfind.casesensitive"],
    ["matchDiacritics", "findbar.matchdiacritics"],
  ]);
  const PREFS_TO_OBSERVE_ALL = new Map([
    ...PREFS_TO_OBSERVE_BOOL,
    ...PREFS_TO_OBSERVE_INT,
  ]);
  const TOPIC_MAC_APP_ACTIVATE = "mac_app_activate";

  class MozFindbar extends MozXULElement {
    static get markup() {
      return `
      <hbox anonid="findbar-container" class="findbar-container" flex="1" align="center">
        <hbox anonid="findbar-textbox-wrapper" align="stretch">
          <html:input anonid="findbar-textbox" class="findbar-textbox" />
          <toolbarbutton anonid="find-previous" class="findbar-find-previous tabbable"
            data-l10n-attrs="tooltiptext" data-l10n-id="findbar-previous" disabled="true" />
          <toolbarbutton anonid="find-next" class="findbar-find-next tabbable"
            data-l10n-id="findbar-next" disabled="true" />
        </hbox>
        <checkbox anonid="highlight" class="findbar-highlight tabbable"
          data-l10n-id="findbar-highlight-all2"/>
        <checkbox anonid="find-case-sensitive" class="findbar-case-sensitive tabbable"
          data-l10n-id="findbar-case-sensitive"/>
        <checkbox anonid="find-match-diacritics" class="findbar-match-diacritics tabbable"
          data-l10n-id="findbar-match-diacritics"/>
        <checkbox anonid="find-entire-word" class="findbar-entire-word tabbable"
          data-l10n-id="findbar-entire-word"/>
        <label anonid="match-case-status" class="findbar-label"
          data-l10n-id="findbar-case-sensitive-status" hidden="true" />
        <label anonid="match-diacritics-status" class="findbar-label"
          data-l10n-id="findbar-match-diacritics-status" hidden="true" />
        <label anonid="entire-word-status" class="findbar-label"
          data-l10n-id="findbar-entire-word-status" hidden="true" />
        <label anonid="found-matches" class="findbar-label found-matches" hidden="true" />
        <image anonid="find-status-icon" class="find-status-icon" />
        <description anonid="find-status" control="findbar-textbox" class="findbar-label findbar-find-status" />
      </hbox>
      <toolbarbutton anonid="find-closebutton" class="findbar-closebutton tabbable close-icon"
        data-l10n-id="findbar-find-button-close"/>
      `;
    }

    constructor() {
      super();
      MozXULElement.insertFTLIfNeeded("toolkit/main-window/findbar.ftl");
      this.destroy = this.destroy.bind(this);

      // We have to guard against `this.close` being |null| due to an unknown
      // issue, which is tracked in bug 957999.
      this.addEventListener(
        "keypress",
        event => {
          if (event.keyCode == event.DOM_VK_ESCAPE) {
            if (this.close) {
              this.close();
            }
            event.preventDefault();
          }
        },
        true
      );
    }

    connectedCallback() {
      // Hide the findbar immediately without animation. This prevents a flicker in the case where
      // we'll never be shown (i.e. adopting a tab that has a previously-opened-but-now-closed
      // findbar into a new window).
      this.setAttribute("noanim", "true");
      this.hidden = true;
      this.appendChild(this.constructor.fragment);
      if (AppConstants.platform == "macosx") {
        this.insertBefore(
          this.getElement("find-closebutton"),
          this.getElement("findbar-container")
        );
      }

      /**
       * Please keep in sync with toolkit/modules/FindBarContent.sys.mjs
       */
      this.FIND_NORMAL = 0;

      this.FIND_TYPEAHEAD = 1;

      this.FIND_LINKS = 2;

      this._findMode = 0;

      this._flashFindBar = 0;

      this._initialFlashFindBarCount = 6;

      /**
       * For tests that need to know when the find bar is finished
       * initializing, we store a promise to notify on.
       */
      this._startFindDeferred = null;

      this._browser = null;

      this._destroyed = false;

      this._xulBrowserWindow = null;

      // These elements are accessed frequently and are therefore cached.
      this._findField = this.getElement("findbar-textbox");
      this._foundMatches = this.getElement("found-matches");
      this._findStatusIcon = this.getElement("find-status-icon");
      this._findStatusDesc = this.getElement("find-status");

      this._foundURL = null;

      let prefsvc = Services.prefs;

      this.quickFindTimeoutLength = prefsvc.getIntPref(
        "accessibility.typeaheadfind.timeout"
      );
      this._flashFindBar = prefsvc.getIntPref(
        "accessibility.typeaheadfind.flashBar"
      );

      let observe = (this._observe = this.observe.bind(this));
      for (let [propName, prefName] of PREFS_TO_OBSERVE_ALL) {
        prefsvc.addObserver(prefName, observe);
        let prefGetter = PREFS_TO_OBSERVE_BOOL.has(propName) ? "Bool" : "Int";
        this["_" + propName] = prefsvc[`get${prefGetter}Pref`](prefName);
      }
      Services.obs.addObserver(observe, TOPIC_MAC_APP_ACTIVATE);

      this._findResetTimeout = -1;

      // Make sure the FAYT keypress listener is attached by initializing the
      // browser property.
      if (this.getAttribute("browserid")) {
        setTimeout(() => {
          // eslint-disable-next-line no-self-assign
          this.browser = this.browser;
        }, 0);
      }

      window.addEventListener("unload", this.destroy);

      this._findField.addEventListener("input", () => {
        // We should do nothing during composition.  E.g., composing string
        // before converting may matches a forward word of expected word.
        // After that, even if user converts the composition string to the
        // expected word, it may find second or later searching word in the
        // document.
        if (this._isIMEComposing) {
          return;
        }

        const value = this._findField.value;
        if (this._hadValue && !value) {
          this._willfullyDeleted = true;
          this._hadValue = false;
        } else if (value.trim()) {
          this._hadValue = true;
          this._willfullyDeleted = false;
        }
        this._find(value);
      });

      this._findField.addEventListener("keypress", event => {
        switch (event.keyCode) {
          case KeyEvent.DOM_VK_RETURN:
            if (this.findMode == this.FIND_NORMAL) {
              let findString = this._findField;
              if (!findString.value) {
                return;
              }
              if (event.getModifierState("Accel")) {
                this.getElement("highlight").click();
                return;
              }

              this.onFindAgainCommand(event.shiftKey);
            } else {
              this._finishFAYT(event);
            }
            break;
          case KeyEvent.DOM_VK_TAB: {
            let shouldHandle =
              !event.altKey && !event.ctrlKey && !event.metaKey;
            if (shouldHandle && this.findMode != this.FIND_NORMAL) {
              this._finishFAYT(event);
            }
            break;
          }
          case KeyEvent.DOM_VK_PAGE_UP:
          case KeyEvent.DOM_VK_PAGE_DOWN:
            if (
              !event.altKey &&
              !event.ctrlKey &&
              !event.metaKey &&
              !event.shiftKey
            ) {
              this.browser.finder.keyPress(event);
              event.preventDefault();
            }
            break;
          case KeyEvent.DOM_VK_UP:
          case KeyEvent.DOM_VK_DOWN:
            this.browser.finder.keyPress(event);
            event.preventDefault();
            break;
        }
      });

      this._findField.addEventListener("blur", () => {
        // Note: This code used to remove the selection
        // if it matched an editable.
        this.browser.finder.enableSelection();
      });

      this._findField.addEventListener("focus", () => {
        this._updateBrowserWithState();
      });

      this._findField.addEventListener("compositionstart", () => {
        // Don't close the find toolbar while IME is composing.
        let findbar = this;
        findbar._isIMEComposing = true;
        if (findbar._quickFindTimeout) {
          clearTimeout(findbar._quickFindTimeout);
          findbar._quickFindTimeout = null;
          findbar._updateBrowserWithState();
        }
      });

      this._findField.addEventListener("compositionend", () => {
        this._isIMEComposing = false;
        if (this.findMode != this.FIND_NORMAL) {
          this._setFindCloseTimeout();
        }
      });

      this._findField.addEventListener("dragover", event => {
        if (event.dataTransfer.types.includes("text/plain")) {
          event.preventDefault();
        }
      });

      this._findField.addEventListener("drop", event => {
        let value = event.dataTransfer.getData("text/plain");
        this._findField.value = value;
        this._find(value);
        event.stopPropagation();
        event.preventDefault();
      });

      this.getElement("find-previous").addEventListener("command", () =>
        this.onFindAgainCommand(true)
      );
      this.getElement("find-next").addEventListener("command", () =>
        this.onFindAgainCommand(false)
      );
      this.getElement("highlight").addEventListener("command", event =>
        this.toggleHighlight(event.target.checked)
      );
      this.getElement("find-case-sensitive").addEventListener(
        "command",
        event => this._setCaseSensitivity(event.target.checked ? 1 : 0)
      );
      this.getElement("find-match-diacritics").addEventListener(
        "command",
        event => this._setDiacriticMatching(event.target.checked ? 1 : 0)
      );
      this.getElement("find-entire-word").addEventListener("command", event =>
        this.toggleEntireWord(event.target.checked)
      );
      this.getElement("find-closebutton").addEventListener("command", () =>
        this.close()
      );
    }

    connectedMoveCallback() {
      // No-op: Allows consumers to move <findbar> element in the DOM tree
      // without destroying and recreating itself (bug 2056718).
    }

    set findMode(val) {
      this._findMode = val;
      this._updateBrowserWithState();
    }

    get findMode() {
      return this._findMode;
    }

    set prefillWithSelection(val) {
      this.setAttribute("prefillwithselection", val);
    }

    get prefillWithSelection() {
      return this.getAttribute("prefillwithselection") != "false";
    }

    get hasTransactions() {
      if (this._findField.value) {
        return true;
      }

      // Watch out for lazy editor init
      if (this._findField.editor) {
        return this._findField.editor.canUndo || this._findField.editor.canRedo;
      }
      return false;
    }

    set browser(val) {
      function setFindbarInActor(browser, findbar) {
        if (!browser.frameLoader) {
          return;
        }

        let windowGlobal = browser.browsingContext.currentWindowGlobal;
        if (windowGlobal) {
          let findbarParent = windowGlobal.getActor("FindBar");
          if (findbarParent) {
            findbarParent.setFindbar(browser, findbar);
          }
        }
      }

      if (this._browser) {
        setFindbarInActor(this._browser, null);

        let finder = this._browser.finder;
        if (finder) {
          finder.removeResultListener(this);
        }
      }

      this._browser = val;
      if (this._browser) {
        // Need to do this to ensure the correct initial state.
        this._updateBrowserWithState();

        setFindbarInActor(this._browser, this);

        this._browser.finder.addResultListener(this);
      }
    }

    get browser() {
      if (!this._browser) {
        const id = this.getAttribute("browserid");
        if (id) {
          this._browser = document.getElementById(id);
        }
      }
      return this._browser;
    }

    observe(subject, topic, prefName) {
      if (topic == TOPIC_MAC_APP_ACTIVATE) {
        this._onAppActivateMac();
        return;
      }

      if (topic != "nsPref:changed") {
        return;
      }

      let prefsvc = Services.prefs;

      switch (prefName) {
        case "accessibility.typeaheadfind":
          this._findAsYouType = prefsvc.getBoolPref(prefName);
          break;
        case "accessibility.typeaheadfind.manual":
          this._manualFAYT = prefsvc.getBoolPref(prefName);
          break;
        case "accessibility.typeaheadfind.timeout":
          this.quickFindTimeoutLength = prefsvc.getIntPref(prefName);
          break;
        case "accessibility.typeaheadfind.linksonly":
          this._typeAheadLinksOnly = prefsvc.getBoolPref(prefName);
          break;
        case "accessibility.typeaheadfind.casesensitive":
          this._setCaseSensitivity(prefsvc.getIntPref(prefName));
          break;
        case "findbar.entireword":
          this._entireWord = prefsvc.getBoolPref(prefName);
          this.toggleEntireWord(this._entireWord, true);
          break;
        case "findbar.highlightAll":
          this.toggleHighlight(prefsvc.getBoolPref(prefName), true);
          break;
        case "findbar.matchdiacritics":
          this._setDiacriticMatching(prefsvc.getIntPref(prefName));
          break;
        case "findbar.modalHighlight":
          this._useModalHighlight = prefsvc.getBoolPref(prefName);
          if (this.browser.finder) {
            this.browser.finder.onModalHighlightChange(this._useModalHighlight);
          }
          break;
      }
    }

    getElement(aAnonymousID) {
      return this.querySelector(`[anonid=${aAnonymousID}]`);
    }

    /**
     * This is necessary because custom elements don't have a "real" destructor.
     * This method is called explicitly from disconnectedCallback, and from
     * an unload event handler that we add.
     */
    destroy() {
      if (this._destroyed) {
        return;
      }
      window.removeEventListener("unload", this.destroy);
      this._destroyed = true;

      this.browser?._finder?.destroy();

      // Invoking this setter also removes the message listeners.
      this.browser = null;

      let prefsvc = Services.prefs;
      let observe = this._observe;
      for (let [, prefName] of PREFS_TO_OBSERVE_ALL) {
        prefsvc.removeObserver(prefName, observe);
      }

      Services.obs.removeObserver(observe, TOPIC_MAC_APP_ACTIVATE);

      // Clear all timers that might still be running.
      this._cancelTimers();
    }

    _cancelTimers() {
      if (this._flashFindBarTimeout) {
        clearInterval(this._flashFindBarTimeout);
        this._flashFindBarTimeout = null;
      }
      if (this._quickFindTimeout) {
        clearTimeout(this._quickFindTimeout);
        this._quickFindTimeout = null;
      }
      if (this._findResetTimeout) {
        clearTimeout(this._findResetTimeout);
        this._findResetTimeout = null;
      }
    }

    _setFindCloseTimeout() {
      if (this._quickFindTimeout) {
        clearTimeout(this._quickFindTimeout);
      }

      // Don't close the find toolbar while IME is composing OR when the
      // findbar is already hidden.
      if (this._isIMEComposing || this.hidden) {
        this._quickFindTimeout = null;
        this._updateBrowserWithState();
        return;
      }

      if (this.quickFindTimeoutLength < 1) {
        this._quickFindTimeout = null;
      } else {
        this._quickFindTimeout = setTimeout(() => {
          if (this.findMode != this.FIND_NORMAL) {
            this.close();
          }
          this._quickFindTimeout = null;
        }, this.quickFindTimeoutLength);
      }
      this._updateBrowserWithState();
    }

    /**
     * Updates the search match count after each find operation on a new string.
     */
    _updateMatchesCount() {
      if (!this._dispatchFindEvent("matchescount")) {
        return;
      }

      this.browser.finder.requestMatchesCount(this._findField.value, {
        linksOnly: this.findMode == this.FIND_LINKS,
      });
    }

    /**
     * Turns highlighting of all occurrences on or off.
     *
     * @param {boolean} highlight Whether to turn the highlight on or off.
     * @param {boolean} fromPrefObserver Whether the callee is the pref
     *                                   observer, which means we should not set
     *                                   the same pref again.
     */
    toggleHighlight(highlight, fromPrefObserver) {
      if (highlight === this._highlightAll) {
        return;
      }

      this.browser.finder.onHighlightAllChange(highlight);

      this._setHighlightAll(highlight, fromPrefObserver);

      if (!this._dispatchFindEvent("highlightallchange")) {
        return;
      }

      let word = this._findField.value;
      // Bug 429723. Don't attempt to highlight ""
      if (highlight && !word) {
        return;
      }

      this.browser.finder.highlight(
        highlight,
        word,
        this.findMode == this.FIND_LINKS
      );

      // Update the matches count
      this._updateMatchesCount(Ci.nsITypeAheadFind.FIND_FOUND);
    }

    /**
     * Updates the highlight-all mode of the findbar and its UI.
     *
     * @param {boolean} highlight Whether to turn the highlight on or off.
     * @param {boolean} fromPrefObserver Whether the callee is the pref
     *                                   observer, which means we should not set
     *                                   the same pref again.
     */
    _setHighlightAll(highlight, fromPrefObserver) {
      if (typeof highlight != "boolean") {
        highlight = this._highlightAll;
      }
      if (highlight !== this._highlightAll) {
        this._highlightAll = highlight;
        if (!fromPrefObserver) {
          Glean.findbar.highlightAll.add(1);
          Services.prefs.setBoolPref("findbar.highlightAll", highlight);
        }
      }
      let checkbox = this.getElement("highlight");
      checkbox.checked = this._highlightAll;
    }

    /**
     * Updates the case-sensitivity mode of the findbar and its UI.
     *
     * @param {string} [str] The string for which case sensitivity might be
     *                       turned on. This only used when case-sensitivity is
     *                       in auto mode, see `_shouldBeCaseSensitive`. The
     *                       default value for this parameter is the find-field
     *                       value.
     * @see _shouldBeCaseSensitive
     */
    _updateCaseSensitivity(str) {
      let val = str || this._findField.value;

      let caseSensitive = this._shouldBeCaseSensitive(val);
      let checkbox = this.getElement("find-case-sensitive");
      let statusLabel = this.getElement("match-case-status");
      checkbox.checked = caseSensitive;

      // Show the checkbox on the full Find bar in non-auto mode.
      // Show the label in all other cases.
      if (
        this.findMode == this.FIND_NORMAL &&
        (this._typeAheadCaseSensitive == 0 || this._typeAheadCaseSensitive == 1)
      ) {
        checkbox.hidden = false;
        statusLabel.hidden = true;
      } else {
        checkbox.hidden = true;
        statusLabel.hidden = !caseSensitive;
      }

      this.browser.finder.caseSensitive = caseSensitive;
    }

    /**
     * Sets the findbar case-sensitivity mode.
     *
     * @param {number} caseSensitivity 0 - case insensitive,
     *                                 1 - case sensitive,
     *                                 2 - auto = case sensitive if the matching
     *                                     string contains upper case letters.
     * @see _shouldBeCaseSensitive
     */
    _setCaseSensitivity(caseSensitivity) {
      this._typeAheadCaseSensitive = caseSensitivity;
      this._updateCaseSensitivity();
      this._findFailedString = null;
      this._find();

      this._dispatchFindEvent("casesensitivitychange");
      Glean.findbar.matchCase.add(1);
    }

    /**
     * Updates the diacritic-matching mode of the findbar and its UI.
     *
     * @param {string} [str] The string for which diacritic matching might be
     *                       turned on. This is only used when diacritic
     *                       matching is in auto mode, see
     *                       `_shouldMatchDiacritics`. The default value for
     *                       this parameter is the find-field value.
     * @see _shouldMatchDiacritics.
     */
    _updateDiacriticMatching(str) {
      let val = str || this._findField.value;

      let matchDiacritics = this._shouldMatchDiacritics(val);
      let checkbox = this.getElement("find-match-diacritics");
      let statusLabel = this.getElement("match-diacritics-status");
      checkbox.checked = matchDiacritics;

      // Show the checkbox on the full Find bar in non-auto mode.
      // Show the label in all other cases.
      if (
        this.findMode == this.FIND_NORMAL &&
        (this._matchDiacritics == 0 || this._matchDiacritics == 1)
      ) {
        checkbox.hidden = false;
        statusLabel.hidden = true;
      } else {
        checkbox.hidden = true;
        statusLabel.hidden = !matchDiacritics;
      }

      this.browser.finder.matchDiacritics = matchDiacritics;
    }

    /**
     * Sets the findbar diacritic-matching mode
     *
     * @param {number} diacriticMatching 0 - ignore diacritics,
     *                                   1 - match diacritics,
     *                                   2 - auto = match diacritics if the
     *                                       matching string contains
     *                                       diacritics.
     * @see _shouldMatchDiacritics
     */
    _setDiacriticMatching(diacriticMatching) {
      this._matchDiacritics = diacriticMatching;
      this._updateDiacriticMatching();
      this._findFailedString = null;
      this._find();

      this._dispatchFindEvent("diacriticmatchingchange");

      Glean.findbar.matchDiacritics.add(1);
    }

    /**
     * Updates the entire-word mode of the findbar and its UI.
     */
    _setEntireWord() {
      let entireWord = this._entireWord;
      let checkbox = this.getElement("find-entire-word");
      let statusLabel = this.getElement("entire-word-status");
      checkbox.checked = entireWord;

      // Show the checkbox on the full Find bar.
      // Show the label in all other cases.
      if (this.findMode == this.FIND_NORMAL) {
        checkbox.hidden = false;
        statusLabel.hidden = true;
      } else {
        checkbox.hidden = true;
        statusLabel.hidden = !entireWord;
      }

      this.browser.finder.entireWord = entireWord;
    }

    /**
     * Sets the findbar entire-word mode.
     *
     * @param {boolean} entireWord Whether or not entire-word mode should be
     *                             turned on.
     */
    toggleEntireWord(entireWord, fromPrefObserver) {
      if (!fromPrefObserver) {
        // Just set the pref; our observer will change the find bar behavior.
        Services.prefs.setBoolPref("findbar.entireword", entireWord);

        Glean.findbar.wholeWords.add(1);
        return;
      }

      this._findFailedString = null;
      this._find();
    }

    /**
     * Opens and displays the find bar.
     *
     * @param {number} mode The find mode to be used, which is either
     *                      FIND_NORMAL, FIND_TYPEAHEAD or FIND_LINKS. If not
     *                      passed, we revert to the last find mode if any or
     *                      FIND_NORMAL.
     * @return {boolean} `true` if the find bar wasn't previously open, `false`
     *                   otherwise.
     */
    open(mode) {
      if (mode != undefined) {
        this.findMode = mode;
      }

      this._findFailedString = null;

      this._updateFindUI();
      if (this.hidden) {
        Glean.findbar.shown.add(1);
        this.removeAttribute("noanim");
        this.hidden = false;

        this._updateStatusUI(Ci.nsITypeAheadFind.FIND_FOUND);

        let event = document.createEvent("Events");
        event.initEvent("findbaropen", true, false);
        this.dispatchEvent(event);

        this.browser.finder.onFindbarOpen();

        return true;
      }
      return false;
    }

    /**
     * Closes the findbar.
     *
     * @param {boolean} [noAnim] Whether to disable to closing animation. Used
     *                           to close instantly and synchronously, when
     *                           other operations depend on this state.
     */
    close(noAnim) {
      if (this.hidden) {
        return;
      }

      if (noAnim) {
        this.setAttribute("noanim", true);
      }
      this.hidden = true;

      let event = document.createEvent("Events");
      event.initEvent("findbarclose", true, false);
      this.dispatchEvent(event);

      // 'focusContent()' iterates over all listeners in the chrome
      // process, so we need to call it from here.
      this.browser.finder.focusContent();
      this.browser.finder.onFindbarClose();

      this._cancelTimers();
      this._updateBrowserWithState();

      this._findFailedString = null;
    }

    clear() {
      this.browser.finder.removeSelection();
      // Clear value and undo/redo transactions
      this._findField.value = "";
      this._findField.editor?.clearUndoRedo();
      this.toggleHighlight(false);
      this._updateStatusUI();
      this._enableFindButtons(false);
    }

    _dispatchKeypressEvent(target, fakeEvent) {
      if (!target) {
        return;
      }

      // The event information comes from the child process.
      let event = new target.documentGlobal.KeyboardEvent("keypress", {
        ...fakeEvent,
        bubbles: false,
      });
      target.dispatchEvent(event);
    }

    _updateStatusUIBar(foundURL) {
      if (!this._xulBrowserWindow) {
        try {
          this._xulBrowserWindow = window.docShell.treeOwner
            .QueryInterface(Ci.nsIInterfaceRequestor)
            .getInterface(Ci.nsIAppWindow).XULBrowserWindow;
        } catch (ex) {}
        if (!this._xulBrowserWindow) {
          return false;
        }
      }

      // Call this has the same effect like hovering over link,
      // the browser shows the URL as a tooltip.
      this._xulBrowserWindow.setOverLink(foundURL || "");
      return true;
    }

    _finishFAYT(keypressEvent) {
      this.browser.finder.focusContent();

      if (keypressEvent) {
        keypressEvent.preventDefault();
      }

      this.browser.finder.keyPress(keypressEvent);

      this.close();
      return true;
    }

    _shouldBeCaseSensitive(str) {
      if (this._typeAheadCaseSensitive == 0) {
        return false;
      }
      if (this._typeAheadCaseSensitive == 1) {
        return true;
      }

      return str != str.toLowerCase();
    }

    _shouldMatchDiacritics(str) {
      if (this._matchDiacritics == 0) {
        return false;
      }
      if (this._matchDiacritics == 1) {
        return true;
      }

      return str != str.normalize("NFD");
    }

    onMouseUp() {
      if (!this.hidden && this.findMode != this.FIND_NORMAL) {
        this.close();
      }
    }

    /**
     * We get a fake event object through an IPC message when FAYT is being used
     * from within the browser. We then stuff that input in the find bar here.
     *
     * @param {object} fakeEvent Event object that looks and quacks like a
     *                           native DOM KeyPress event.
     */
    _onBrowserKeypress(fakeEvent) {
      const FAYT_LINKS_KEY = "'";
      const FAYT_TEXT_KEY = "/";

      if (!this.hidden && this._findField == document.activeElement) {
        this._dispatchKeypressEvent(this._findField, fakeEvent);
        return;
      }

      if (this.findMode != this.FIND_NORMAL && this._quickFindTimeout) {
        this._findField.select();
        this._findField.focus();
        this._dispatchKeypressEvent(this._findField, fakeEvent);
        return;
      }

      let key = fakeEvent.charCode
        ? String.fromCharCode(fakeEvent.charCode)
        : null;
      let manualstartFAYT =
        (key == FAYT_LINKS_KEY || key == FAYT_TEXT_KEY) && this._manualFAYT;
      let autostartFAYT =
        !manualstartFAYT && this._findAsYouType && key && key != " ";
      if (manualstartFAYT || autostartFAYT) {
        let mode =
          key == FAYT_LINKS_KEY || (autostartFAYT && this._typeAheadLinksOnly)
            ? this.FIND_LINKS
            : this.FIND_TYPEAHEAD;

        // Clear bar first, so that when openFindBar() calls setCaseSensitivity()
        // it doesn't get confused by a lingering value
        this._findField.value = "";

        this.open(mode);
        this._setFindCloseTimeout();
        this._findField.select();
        this._findField.focus();

        if (autostartFAYT) {
          this._dispatchKeypressEvent(this._findField, fakeEvent);
        } else {
          this._updateStatusUI(Ci.nsITypeAheadFind.FIND_FOUND);
        }
      }
    }

    _updateBrowserWithState() {
      if (this._browser) {
        this._browser.sendMessageToActor(
          "Findbar:UpdateState",
          {
            findMode: this.findMode,
            isOpenAndFocused:
              !this.hidden && document.activeElement == this._findField,
            hasQuickFindTimeout: !!this._quickFindTimeout,
          },
          "FindBar",
          "all"
        );
      }
    }

    _enableFindButtons(aEnable) {
      this.getElement("find-next").disabled = this.getElement(
        "find-previous"
      ).disabled = !aEnable;
    }

    /**
     * Determines whether minimalist or general-purpose search UI is to be
     * displayed when the find bar is activated.
     */
    _updateFindUI() {
      let showMinimalUI = this.findMode != this.FIND_NORMAL;

      let nodes = this.getElement("findbar-container").children;
      let wrapper = this.getElement("findbar-textbox-wrapper");
      let foundMatches = this._foundMatches;
      for (let node of nodes) {
        if (node == wrapper || node == foundMatches) {
          continue;
        }
        node.hidden = showMinimalUI;
      }
      this.getElement("find-next").hidden = this.getElement(
        "find-previous"
      ).hidden = showMinimalUI;
      foundMatches.hidden = showMinimalUI || !foundMatches.value;
      this._updateCaseSensitivity();
      this._updateDiacriticMatching();
      this._setEntireWord();
      this._setHighlightAll();

      if (showMinimalUI) {
        this._findField.classList.add("minimal");
      } else {
        this._findField.classList.remove("minimal");
      }

      let l10nId;
      if (this.findMode == this.FIND_TYPEAHEAD) {
        l10nId = "findbar-fast-find";
      } else if (this.findMode == this.FIND_LINKS) {
        l10nId = "findbar-fast-find-links";
      } else {
        l10nId = "findbar-normal-find";
      }
      document.l10n.setAttributes(this._findField, l10nId);
    }

    _find(value) {
      if (!this._dispatchFindEvent("")) {
        return;
      }

      let val = value || this._findField.value;

      // We have to carry around an explicit version of this, because
      // finder.searchString doesn't update on failed searches.
      this.browser._lastSearchString = val;

      // Only search on input if we don't have a last-failed string,
      // or if the current search string doesn't start with it.
      // In entire-word mode we always attemp a find; since sequential matching
      // is not guaranteed, the first character typed may not be a word (no
      // match), but the with the second character it may well be a word,
      // thus a match.
      if (
        !this._findFailedString ||
        !val.startsWith(this._findFailedString) ||
        this._entireWord
      ) {
        // Getting here means the user commanded a find op. Make sure any
        // initial prefilling is ignored if it hasn't happened yet.
        if (this._startFindDeferred) {
          this._startFindDeferred.resolve();
          this._startFindDeferred = null;
        }

        this._enableFindButtons(val);
        this._updateCaseSensitivity(val);
        this._updateDiacriticMatching(val);
        this._setEntireWord();

        this.browser.finder.fastFind(
          val,
          this.findMode == this.FIND_LINKS,
          this.findMode != this.FIND_NORMAL
        );
      }

      if (this.findMode != this.FIND_NORMAL) {
        this._setFindCloseTimeout();
      }

      if (this._findResetTimeout != -1) {
        clearTimeout(this._findResetTimeout);
      }

      // allow a search to happen on input again after a second has expired
      // since the previous input, to allow for dynamic content and/ or page
      // loading.
      this._findResetTimeout = setTimeout(() => {
        this._findFailedString = null;
        this._findResetTimeout = -1;
      }, 1000);
    }

    _flash() {
      if (this._flashFindBarCount === undefined) {
        this._flashFindBarCount = this._initialFlashFindBarCount;
      }

      if (this._flashFindBarCount-- == 0) {
        clearInterval(this._flashFindBarTimeout);
        this._findField.removeAttribute("flash");
        this._flashFindBarCount = 6;
        return;
      }

      this._findField.setAttribute(
        "flash",
        this._flashFindBarCount % 2 == 0 ? "false" : "true"
      );
    }

    _findAgain(findPrevious) {
      this.browser.finder.findAgain(
        this._findField.value,
        findPrevious,
        this.findMode == this.FIND_LINKS,
        this.findMode != this.FIND_NORMAL
      );
    }

    _updateStatusUI(res, findPrevious) {
      let statusL10nId;
      switch (res) {
        case Ci.nsITypeAheadFind.FIND_WRAPPED:
          this._findStatusIcon.setAttribute("status", "wrapped");
          this._findField.removeAttribute("status");
          statusL10nId = findPrevious
            ? "findbar-wrapped-to-bottom"
            : "findbar-wrapped-to-top";
          break;
        case Ci.nsITypeAheadFind.FIND_NOTFOUND:
          this._findStatusDesc.setAttribute("status", "notfound");
          this._findStatusIcon.setAttribute("status", "notfound");
          this._findField.setAttribute("status", "notfound");
          this._foundMatches.hidden = true;
          statusL10nId = "findbar-not-found";
          break;
        case Ci.nsITypeAheadFind.FIND_PENDING:
          this._findStatusIcon.setAttribute("status", "pending");
          this._findField.removeAttribute("status");
          this._findStatusDesc.removeAttribute("status");
          statusL10nId = "";
          break;
        case Ci.nsITypeAheadFind.FIND_FOUND:
        default:
          this._findStatusIcon.removeAttribute("status");
          this._findField.removeAttribute("status");
          this._findStatusDesc.removeAttribute("status");
          statusL10nId = "";
          break;
      }
      if (statusL10nId) {
        document.l10n.setAttributes(this._findStatusDesc, statusL10nId);
      } else {
        delete this._findStatusDesc.dataset.l10nId;
        this._findStatusDesc.textContent = "";
      }
    }

    updateControlState(result, findPrevious) {
      this._updateStatusUI(result, findPrevious);
      this._enableFindButtons(
        result !== Ci.nsITypeAheadFind.FIND_NOTFOUND && !!this._findField.value
      );
    }

    _dispatchFindEvent(type, findPrevious) {
      let event = document.createEvent("CustomEvent");
      event.initCustomEvent("find" + type, true, true, {
        query: this._findField.value,
        caseSensitive: !!this._typeAheadCaseSensitive,
        matchDiacritics: !!this._matchDiacritics,
        entireWord: this._entireWord,
        highlightAll: this._highlightAll,
        findPrevious,
      });
      return this.dispatchEvent(event);
    }

    /**
     * Opens the findbar, focuses the findfield and selects its contents.
     * Also flashes the findbar the first time it's used.
     *
     * @param {number} mode The find mode to be used, which is either
     *                      FIND_NORMAL, FIND_TYPEAHEAD or FIND_LINKS. If not
     *                      passed, we revert to the last find mode if any or
     *                      FIND_NORMAL.
     * @return {Promise} A promise that will be resolved when the findbar is
     *                   fully opened.
     */
    startFind(mode) {
      let prefsvc = Services.prefs;
      let userWantsPrefill = true;
      this.open(mode);

      if (this._flashFindBar) {
        this._flashFindBarTimeout = setInterval(() => this._flash(), 500);
        prefsvc.setIntPref(
          "accessibility.typeaheadfind.flashBar",
          --this._flashFindBar
        );
      }

      this._startFindDeferred = Promise.withResolvers();
      let startFindPromise = this._startFindDeferred.promise;

      if (this.prefillWithSelection) {
        userWantsPrefill = prefsvc.getBoolPref(
          "accessibility.typeaheadfind.prefillwithselection"
        );
      }

      if (this.prefillWithSelection && userWantsPrefill) {
        this.browser.finder.getInitialSelection();

        // NB: We have to focus this._findField here so tests that send
        // key events can open and close the find bar synchronously.
        this._findField.focus();

        // (e10s) since we focus lets also select it, otherwise that would
        // only happen in this.onCurrentSelection and, because it is async,
        // there's a chance keypresses could come inbetween, leading to
        // jumbled up queries.
        this._findField.select();

        return startFindPromise;
      }

      // If userWantsPrefill is false but prefillWithSelection is true,
      // then we might need to check the selection clipboard. Call
      // onCurrentSelection to do so.
      // Note: this.onCurrentSelection clears this._startFindDeferred.
      this.onCurrentSelection("", true);
      return startFindPromise;
    }

    /**
     * Convenient alias to startFind(gFindBar.FIND_NORMAL);
     *
     * You should generally map the window's find command to this method.
     *   e.g. <command name="cmd_find" oncommand="gFindBar.onFindCommand();"/>
     */
    onFindCommand() {
      return this.startFind(this.FIND_NORMAL);
    }

    /**
     * Stub for find-next and find-previous commands.
     *
     * @param {boolean} findPrevious `true` for find-previous, `false`
     *                               otherwise.
     */
    onFindAgainCommand(findPrevious) {
      if (findPrevious) {
        Glean.findbar.findPrev.add(1);
      } else {
        Glean.findbar.findNext.add(1);
      }

      let findString =
        this._browser.finder.searchString || this._findField.value;
      if (!findString) {
        return this.startFind();
      }

      // We dispatch the findAgain event here instead of in _findAgain since
      // if there is a find event handler that prevents the default then
      // finder.searchString will never get updated which in turn means
      // there would never be findAgain events because of the logic below.
      if (!this._dispatchFindEvent("again", findPrevious)) {
        return undefined;
      }

      // user explicitly requested another search, so do it even if we think it'll fail
      this._findFailedString = null;

      // Ensure the stored SearchString is in sync with what we want to find
      if (this._findField.value != this._browser.finder.searchString) {
        this._find(this._findField.value);
      } else {
        this._findAgain(findPrevious);
        if (this._useModalHighlight) {
          this.open();
          this._findField.focus();
        }
      }

      return undefined;
    }

    /**
     * Fetches the currently selected text and sets that as the text to search
     * next. This is a MacOS specific feature.
     */
    onFindSelectionCommand() {
      this.browser.finder.setSearchStringToSelection().then(searchInfo => {
        if (searchInfo.selectedText) {
          this._findField.value = searchInfo.selectedText;
        }
      });
    }

    _onAppActivateMac() {
      const kPref = "accessibility.typeaheadfind.prefillwithselection";
      if (this.prefillWithSelection && Services.prefs.getBoolPref(kPref)) {
        return;
      }

      let clipboardSearchString = this._browser.finder.clipboardSearchString;
      if (
        clipboardSearchString &&
        this._findField.value != clipboardSearchString &&
        !this._findField._willfullyDeleted
      ) {
        this._findField.value = clipboardSearchString;
        this._findField._hadValue = true;
        // Changing the search string makes the previous status invalid, so
        // we better clear it here.
        this._updateStatusUI();
      }
    }

    /**
     * This handles all the result changes for both type-ahead-find and
     * highlighting.
     *
     * @param {object} data A dictionary that holds the following properties:
     *                      - {Number} result  One of the FIND_* constants
     *                                         indicating the result of a search
     *                                         operation.
     *                      - {Boolean} findBackwards If the search was done
     *                                                from the bottom to the
     *                                                top. This is used for
     *                                                status messages when
     *                                                reaching "the end of the
     *                                                page".
     *                      - {String} linkURL When a link matched, then its
     *                                         URL. Always null when not in
     *                                         FIND_LINKS mode.
     */
    onFindResult(data) {
      if (data.result == Ci.nsITypeAheadFind.FIND_NOTFOUND) {
        // If an explicit Find Again command fails, re-open the toolbar.
        if (data.storeResult && this.open()) {
          this._findField.select();
          this._findField.focus();
        }
        this._findFailedString = data.searchString;
      } else {
        this._findFailedString = null;
      }

      this._updateStatusUI(data.result, data.findBackwards);
      this._updateStatusUIBar(data.linkURL);

      if (this.findMode != this.FIND_NORMAL) {
        this._setFindCloseTimeout();
      }
    }

    /**
     * This handles all the result changes for matches counts.
     *
     * @param {object} result Result Object, containing the total amount of
     *                 matches and a vector of the current result.
     *                 - {Number} total Total count number of matches found.
     *                 - {Number} limit Current setting of the number of matches
     *                                  to hit to hit the limit.
     *                 - {Number} current Vector of the current result.
     */
    onMatchesCountResult(result) {
      if (!result.total) {
        delete this._foundMatches.dataset.l10nId;
        this._foundMatches.hidden = true;
        this._foundMatches.setAttribute("value", "");
      } else {
        let l10nId, l10nArgs;
        if (result.total === -1) {
          l10nId = "findbar-found-matches-count-limit";
          l10nArgs = { limit: result.limit };
        } else {
          l10nId = "findbar-found-matches";
          l10nArgs = { current: result.current, total: result.total };
        }
        this._foundMatches.hidden = false;
        document.l10n.setAttributes(this._foundMatches, l10nId, l10nArgs);
      }
    }

    onHighlightFinished() {
      // Noop.
    }

    onCurrentSelection(selectionString, isInitialSelection) {
      // Ignore the prefill if the user has already typed in the findbar,
      // it would have been overwritten anyway. See bug 1198465.
      if (isInitialSelection && !this._startFindDeferred) {
        return;
      }

      if (
        AppConstants.platform == "macosx" &&
        isInitialSelection &&
        !selectionString
      ) {
        let clipboardSearchString = this.browser.finder.clipboardSearchString;
        if (clipboardSearchString) {
          selectionString = clipboardSearchString;
        }
      }

      if (selectionString) {
        this._findField.value = selectionString;
      }

      if (isInitialSelection) {
        this._enableFindButtons(!!this._findField.value);
        this._findField.select();
        this._findField.focus();

        this._startFindDeferred.resolve();
        this._startFindDeferred = null;
      }
    }

    /**
     * This handler may cancel a request to focus content by returning |false|
     * explicitly.
     */
    shouldFocusContent() {
      const fm = Services.focus;
      if (fm.focusedWindow != window) {
        return false;
      }

      let focusedElement = fm.focusedElement;
      if (!focusedElement) {
        return false;
      }

      let focusedParent = focusedElement.closest("findbar");
      if (focusedParent != this && focusedParent != this._findField) {
        return false;
      }

      return true;
    }

    disconnectedCallback() {
      // Empty the DOM. We will rebuild if reconnected.
      while (this.lastChild) {
        this.removeChild(this.lastChild);
      }
      this.destroy();
    }
  }

  customElements.define("findbar", MozFindbar);
}
