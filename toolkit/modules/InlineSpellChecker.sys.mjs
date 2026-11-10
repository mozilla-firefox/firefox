/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

const MAX_UNDO_STACK_DEPTH = 1;

export function InlineSpellChecker(aEditor) {
  this.init(aEditor);
  this.mAddedWordStack = []; // We init this here to preserve it between init/uninit calls
}

InlineSpellChecker.prototype = {
  // Call this function to initialize for a given editor
  init(aEditor) {
    this.uninit();
    this.mEditor = aEditor;
    try {
      this.mInlineSpellChecker = this.mEditor.getInlineSpellChecker(true);
      // note: this might have been NULL if there is no chance we can spellcheck
    } catch (e) {
      this.mInlineSpellChecker = null;
    }
  },

  initFromRemote(aSpellInfo, aWindowGlobalParent) {
    if (this.mRemote) {
      // We shouldn't get here, but let's just recover instead of bricking the
      // menu by throwing exceptions:
      console.error(new Error("Unexpected remote spellchecker present!"));
      try {
        this.mRemote.uninit();
      } catch (ex) {
        console.error(ex);
      }
      this.mRemote = null;
    }
    this.uninit();

    if (!aSpellInfo) {
      return;
    }
    this.mInlineSpellChecker = this.mRemote = new RemoteSpellChecker(
      aSpellInfo,
      aWindowGlobalParent
    );
    this.mOverMisspelling = aSpellInfo.overMisspelling;
    this.mMisspelling = aSpellInfo.misspelling;
  },

  // call this to clear state
  uninit() {
    if (this.mRemote) {
      this.mRemote.uninit();
      this.mRemote = null;
    }

    this.mEditor = null;
    this.mInlineSpellChecker = null;
    this.mOverMisspelling = false;
    this.mMisspelling = "";
    this.mMenu = null;
    this.mSuggestionItems = [];
    this.mDictionaryMenu = null;
    this.mDictionaryItems = [];
    this.mWordNode = null;
  },

  // for each UI event, you must call this function, it will compute the
  // word the cursor is over
  initFromEvent(rangeParent, rangeOffset) {
    this.mOverMisspelling = false;

    if (!rangeParent || !this.mInlineSpellChecker) {
      return;
    }

    var selcon = this.mEditor.selectionController;
    var spellsel = selcon.getSelection(selcon.SELECTION_SPELLCHECK);
    if (spellsel.rangeCount == 0) {
      return;
    } // easy case - no misspellings

    var range = this.mInlineSpellChecker.getMisspelledWord(
      rangeParent,
      rangeOffset
    );
    if (!range) {
      return;
    } // not over a misspelled word

    this.mMisspelling = range.toString();
    this.mOverMisspelling = true;
    this.mWordNode = rangeParent;
    this.mWordOffset = rangeOffset;
  },

  // returns false if there should be no spellchecking UI enabled at all, true
  // means that you can at least give the user the ability to turn it on.
  get canSpellCheck() {
    // inline spell checker objects will be created only if there are actual
    // dictionaries available
    if (this.mRemote) {
      return this.mRemote.canSpellCheck;
    }
    return this.mInlineSpellChecker != null;
  },

  get initialSpellCheckPending() {
    if (this.mRemote) {
      return this.mRemote.spellCheckPending;
    }
    return !!(
      this.mInlineSpellChecker &&
      !this.mInlineSpellChecker.spellChecker &&
      this.mInlineSpellChecker.spellCheckPending
    );
  },

  // Whether spellchecking is enabled in the current box
  get enabled() {
    if (this.mRemote) {
      return this.mRemote.enableRealTimeSpell;
    }
    return (
      this.mInlineSpellChecker && this.mInlineSpellChecker.enableRealTimeSpell
    );
  },
  set enabled(isEnabled) {
    if (this.mRemote) {
      this.mRemote.setSpellcheckUserOverride(isEnabled);
    } else if (this.mInlineSpellChecker) {
      this.mEditor.setSpellcheckUserOverride(isEnabled);
    }
  },

  // returns true if the given event is over a misspelled word
  get overMisspelling() {
    return this.mOverMisspelling;
  },

  // this prepends up to "maxNumber" suggestions at the given menu position
  // for the word under the cursor. Returns the number of suggestions inserted.
  addSuggestionsToMenuOnParent(menu, insertBefore, maxNumber) {
    if (this.mRemote) {
      // This is used on parent process only.
      // If you want to add suggestions to context menu, get suggestions then
      // use addSuggestionsToMenu instead.
      return 0;
    }
    if (!this.mInlineSpellChecker || !this.mOverMisspelling) {
      return 0;
    }

    let spellchecker = this.mInlineSpellChecker.spellChecker;
    let spellSuggestions = [];

    try {
      if (!spellchecker.CheckCurrentWord(this.mMisspelling)) {
        return 0;
      }

      for (let i = 0; i < maxNumber; i++) {
        let suggestion = spellchecker.GetSuggestedWord();
        if (!suggestion.length) {
          // no more data
          break;
        }
        spellSuggestions.push(suggestion);
      }
    } catch (e) {
      return 0;
    }
    return this._addSuggestionsToMenu(menu, insertBefore, spellSuggestions);
  },

  addSuggestionsToMenu(menu, insertBefore, spellSuggestions) {
    if (
      !this.mRemote &&
      (!this.mInlineSpellChecker || !this.mOverMisspelling)
    ) {
      return 0;
    } // nothing to do

    if (!spellSuggestions?.length) {
      return 0;
    }

    return this._addSuggestionsToMenu(menu, insertBefore, spellSuggestions);
  },

  _addSuggestionsToMenu(menu, insertBefore, spellSuggestions) {
    this.mMenu = menu;
    this.mSuggestionItems = [];

    for (let suggestion of spellSuggestions) {
      var item = menu.ownerDocument.createXULElement("menuitem");
      this.mSuggestionItems.push(item);
      item.setAttribute("label", suggestion);
      item.setAttribute("value", suggestion);
      item.addEventListener(
        "command",
        this.replaceMisspelling.bind(this, suggestion),
        true
      );
      item.setAttribute("class", "spell-suggestion");
      item.setAttribute("auto-accesskey", "digit");
      menu.insertBefore(item, insertBefore);
    }
    return spellSuggestions.length;
  },

  // undoes the work of addSuggestionsToMenu for the same menu
  // (call from popup hiding)
  clearSuggestionsFromMenu() {
    for (var i = 0; i < this.mSuggestionItems.length; i++) {
      this.mMenu.removeChild(this.mSuggestionItems[i]);
    }
    this.mSuggestionItems = [];
  },

  sortDictionaryList(list) {
    var sortedList = [];
    var names = Services.intl.getLocaleDisplayNames(undefined, list);
    for (var i = 0; i < list.length; i++) {
      sortedList.push({ localeCode: list[i], displayName: names[i] });
    }
    let comparer = new Services.intl.Collator().compare;
    sortedList.sort((a, b) => comparer(a.displayName, b.displayName));
    return sortedList;
  },

  async languageMenuListener(evt) {
    let curlangs = new Set();
    if (this.mRemote) {
      curlangs = new Set(this.mRemote.currentDictionaries);
    } else if (this.mInlineSpellChecker) {
      let spellchecker = this.mInlineSpellChecker.spellChecker;
      try {
        curlangs = new Set(spellchecker.getCurrentDictionaries());
      } catch (e) {}
    }

    let localeCodes = new Set(curlangs);
    let localeCode = evt.target.dataset.localeCode;
    if (localeCodes.has(localeCode)) {
      localeCodes.delete(localeCode);
    } else {
      localeCodes.add(localeCode);
    }
    let dictionaries = Array.from(localeCodes);
    await this.selectDictionaries(dictionaries);
    if (this.mRemote) {
      // Store the new set in case the menu doesn't close.
      this.mRemote.currentDictionaries = dictionaries;
    }
    // Notify change of dictionary, especially for Thunderbird,
    // which is otherwise not notified any more.
    let view = this.mDictionaryMenu.documentGlobal;
    let spellcheckChangeEvent = new view.CustomEvent("spellcheck-changed", {
      detail: { dictionaries },
    });
    this.mDictionaryMenu.ownerDocument.dispatchEvent(spellcheckChangeEvent);
  },

  // returns the number of dictionary languages. If insertBefore is NULL, this
  // does an append to the given menu
  addDictionaryListToMenu(menu, insertBefore) {
    this.mDictionaryMenu = menu;
    this.mDictionaryItems = [];

    if (!this.enabled) {
      return 0;
    }

    let list;
    let curlangs = new Set();
    if (this.mRemote) {
      list = this.mRemote.dictionaryList;
      curlangs = new Set(this.mRemote.currentDictionaries);
    } else if (this.mInlineSpellChecker) {
      let spellchecker = this.mInlineSpellChecker.spellChecker;
      list = spellchecker.GetDictionaryList();
      try {
        curlangs = new Set(spellchecker.getCurrentDictionaries());
      } catch (e) {}
    }

    let sortedList = this.sortDictionaryList(list);
    this.languageMenuListenerBind = this.languageMenuListener.bind(this);
    menu.addEventListener("command", this.languageMenuListenerBind, true);

    for (let i = 0; i < sortedList.length; i++) {
      let item = menu.ownerDocument.createXULElement("menuitem");

      item.setAttribute(
        "id",
        "spell-check-dictionary-" + sortedList[i].localeCode
      );
      // XXX: Once Fluent has dynamic references, we could also lazily
      //      inject regionNames/languageNames FTL and localize using
      //      `l10n-id` here.
      item.setAttribute("label", sortedList[i].displayName);
      item.setAttribute("type", "checkbox");
      item.setAttribute("selection-type", "multiple");
      item.setAttribute("auto-accesskey", "digit");
      if (sortedList.length > 1) {
        item.setAttribute("closemenu", "none");
      }
      this.mDictionaryItems.push(item);
      item.dataset.localeCode = sortedList[i].localeCode;
      if (curlangs.has(sortedList[i].localeCode)) {
        item.setAttribute("checked", "true");
      }
      if (insertBefore) {
        menu.insertBefore(item, insertBefore);
      } else {
        menu.appendChild(item);
      }
    }
    return list.length;
  },

  // undoes the work of addDictionaryListToMenu for the menu
  // (call on popup hiding)
  clearDictionaryListFromMenu() {
    this.mDictionaryMenu?.removeEventListener(
      "command",
      this.languageMenuListenerBind,
      true
    );
    for (var i = 0; i < this.mDictionaryItems.length; i++) {
      this.mDictionaryMenu.removeChild(this.mDictionaryItems[i]);
    }
    this.mDictionaryItems = [];
  },

  // callback for selecting a dictionary
  async selectDictionaries(localeCodes) {
    if (this.mRemote) {
      this.mRemote.selectDictionaries(localeCodes);
      return;
    }
    if (!this.mInlineSpellChecker) {
      return;
    }
    var spellchecker = this.mInlineSpellChecker.spellChecker;
    await spellchecker.setCurrentDictionaries(localeCodes);
    this.mInlineSpellChecker.spellCheckRange(null); // causes recheck
  },

  // callback for selecting a suggested replacement
  replaceMisspelling(suggestion) {
    if (this.mRemote) {
      this.mRemote.replaceMisspelling(suggestion);
      return;
    }
    if (!this.mInlineSpellChecker || !this.mOverMisspelling) {
      return;
    }
    this.mInlineSpellChecker.replaceWord(
      this.mWordNode,
      this.mWordOffset,
      suggestion
    );
  },

  // callback for enabling or disabling spellchecking
  toggleEnabled() {
    if (this.mRemote) {
      this.mRemote.toggleEnabled();
    } else {
      this.mEditor.setSpellcheckUserOverride(
        !this.mInlineSpellChecker.enableRealTimeSpell
      );
    }
  },

  // callback for adding the current misspelling to the user-defined dictionary
  addToDictionary() {
    // Prevent the undo stack from growing over the max depth
    if (this.mAddedWordStack.length == MAX_UNDO_STACK_DEPTH) {
      this.mAddedWordStack.shift();
    }

    this.mAddedWordStack.push(this.mMisspelling);
    if (this.mRemote) {
      this.mRemote.addToDictionary();
    } else {
      this.mInlineSpellChecker.addWordToDictionary(this.mMisspelling);
    }
  },
  // callback for removing the last added word to the dictionary LIFO fashion
  undoAddToDictionary() {
    if (this.mAddedWordStack.length) {
      var word = this.mAddedWordStack.pop();
      if (this.mRemote) {
        this.mRemote.undoAddToDictionary(word);
      } else {
        this.mInlineSpellChecker.removeWordFromDictionary(word);
      }
    }
  },
  canUndo() {
    // Return true if we have words on the stack
    return !!this.mAddedWordStack.length;
  },
  ignoreWord() {
    if (this.mRemote) {
      this.mRemote.ignoreWord();
    } else {
      this.mInlineSpellChecker.ignoreWord(this.mMisspelling);
    }
  },
};

export var SpellCheckHelper = {
  // Set when over a non-read-only <textarea> or editable <input>
  // (that allows text entry of some kind, so not e.g. <input type=checkbox>)
  EDITABLE: 0x1,

  // Set when over an <input> element of any type.
  INPUT: 0x2,

  // Set when over any <textarea>.
  TEXTAREA: 0x4,

  // Set when over any text-entry <input>.
  TEXTINPUT: 0x8,

  // Set when over an <input> that can be used as a search engine.
  SEARCHENGINE: 0x10,

  // Set when over an element that otherwise would not be considered
  // "editable" but is because content editable is enabled for the document.
  CONTENTEDITABLE: 0x20,

  // Set when over an <input type="number"> or other non-text field.
  NUMERIC: 0x40,

  // Set when over an <input type="password"> field.
  PASSWORD: 0x80,

  // Set when spellcheckable. Replaces `EDITABLE`/`CONTENTEDITABLE` combination
  // specifically for spellcheck.
  SPELLCHECKABLE: 0x100,

  /**
   * Returns whether the element is counted as a search engine field.
   *
   * @param {HTMLInputElement} aNode
   *   The input to check.
   * @param {Window} window
   *   The element's window.
   * @returns {boolean}
   *   Whether it should count as a search engine field.
   */
  isTargetASearchEngineField(aNode, window) {
    if (
      !window.HTMLInputElement.isInstance(aNode) ||
      (aNode.type != "text" && aNode.type != "search") ||
      aNode.readOnly ||
      !aNode.name ||
      !aNode.form
    ) {
      return false;
    }

    let form = aNode.form;
    let method = form.method.toUpperCase();

    return (
      // Forms without an explicit action often don't work, see Bug 1960237.
      form.hasAttribute("action") &&
      // Since post forms are rarely search forms, we only count them as such
      // if they have a search role. The only other method is dialog, which
      // we never count as search forms.
      (method == "GET" || (method == "POST" && form.role == "search")) &&
      // SearchEngine objects currently only support urlencoded requests.
      form.enctype == "application/x-www-form-urlencoded" &&
      // Don't allow forms with file inputs.
      new FormData(form).values().every(v => typeof v == "string")
    );
  },

  isEditable(element, window) {
    var flags = 0;
    if (window.HTMLInputElement.isInstance(element)) {
      flags |= this.INPUT;
      if (element.mozIsTextField(false) || element.type == "number") {
        flags |= this.TEXTINPUT;
        if (!element.readOnly) {
          flags |= this.EDITABLE;
        }

        if (element.type == "number") {
          flags |= this.NUMERIC;
        }

        // Allow spellchecking UI on all text and search inputs.
        if (
          !element.readOnly &&
          (element.type == "text" || element.type == "search")
        ) {
          flags |= this.SPELLCHECKABLE;
        }
        if (this.isTargetASearchEngineField(element, window)) {
          flags |= this.SEARCHENGINE;
        }
        if (element.type == "password") {
          flags |= this.PASSWORD;
        }
      }
    } else if (window.HTMLTextAreaElement.isInstance(element)) {
      flags |= this.TEXTINPUT | this.TEXTAREA;
      if (!element.readOnly) {
        flags |= this.SPELLCHECKABLE | this.EDITABLE;
      }
    }

    if (!(flags & this.SPELLCHECKABLE)) {
      var win = element.documentGlobal;
      if (win) {
        var isSpellcheckable = false;
        try {
          var editingSession = win.docShell.editingSession;
          if (
            editingSession.windowIsEditable(win) &&
            element.matches(":read-write")
          ) {
            isSpellcheckable = true;
          }
        } catch (ex) {
          // If someone built with composer disabled, we can't get an editing session.
        }

        if (isSpellcheckable) {
          flags |= this.CONTENTEDITABLE | this.SPELLCHECKABLE;
        }
      }
    }

    return flags;
  },
};

function RemoteSpellChecker(aSpellInfo, aWindowGlobalParent) {
  this._spellInfo = aSpellInfo;
  this._suggestionGenerator = null;
  this._actor = aWindowGlobalParent.getActor("InlineSpellChecker");
  this._actor.registerDestructionObserver(this);
}

RemoteSpellChecker.prototype = {
  get canSpellCheck() {
    return this._spellInfo.canSpellCheck;
  },
  get spellCheckPending() {
    return this._spellInfo.initialSpellCheckPending;
  },
  get overMisspelling() {
    return this._spellInfo.overMisspelling;
  },
  get enableRealTimeSpell() {
    return this._spellInfo.enableRealTimeSpell;
  },
  get suggestions() {
    return this._spellInfo.spellSuggestions;
  },

  get currentDictionaries() {
    return this._spellInfo.currentDictionaries;
  },
  set currentDictionaries(dicts) {
    this._spellInfo.currentDictionaries = dicts;
  },
  get dictionaryList() {
    return this._spellInfo.dictionaryList.slice();
  },

  selectDictionaries(localeCodes) {
    this._actor.selectDictionaries({ localeCodes });
  },

  replaceMisspelling(suggestion) {
    this._actor.replaceMisspelling({ suggestion });
  },

  toggleEnabled() {
    this._actor.toggleEnabled();
  },
  addToDictionary() {
    // This is really ugly. There is an nsISpellChecker somewhere in the
    // parent that corresponds to our current element's spell checker in the
    // child, but it's hard to access it. However, we know that
    // addToDictionary adds the word to the singleton personal dictionary, so
    // we just do that here.
    // NB: We also rely on the fact that we only ever pass an empty string in
    // as the "lang".

    let dictionary = Cc[
      "@mozilla.org/spellchecker/personaldictionary;1"
    ].getService(Ci.mozIPersonalDictionary);
    dictionary.addWord(this._spellInfo.misspelling);
    this._actor.recheckSpelling();
  },
  undoAddToDictionary(word) {
    let dictionary = Cc[
      "@mozilla.org/spellchecker/personaldictionary;1"
    ].getService(Ci.mozIPersonalDictionary);
    dictionary.removeWord(word);
    this._actor.recheckSpelling();
  },
  ignoreWord() {
    let dictionary = Cc[
      "@mozilla.org/spellchecker/personaldictionary;1"
    ].getService(Ci.mozIPersonalDictionary);
    dictionary.ignoreWord(this._spellInfo.misspelling);
    this._actor.recheckSpelling();
  },
  uninit() {
    if (this._actor) {
      this._actor.uninit();
      this._actor.unregisterDestructionObserver(this);
    }
  },

  actorDestroyed() {
    // The actor lets us know if it gets destroyed, so we don't
    // later try to call `.uninit()` on it.
    this._actor = null;
  },
};
