/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

import { AppConstants } from "resource://gre/modules/AppConstants.sys.mjs";

const lazy = {};

ChromeUtils.defineESModuleGetters(lazy, {
  GeckoViewAutocomplete: "resource://gre/modules/GeckoViewAutocomplete.sys.mjs",
  clearTimeout: "resource://gre/modules/Timer.sys.mjs",
  setTimeout: "resource://gre/modules/Timer.sys.mjs",
});

const PREF_SECURITY_DELAY = "security.notification_enable_delay";

// Stores the actor that has the active popup, used by formfill
let currentActor = null;

let autoCompleteListeners = new Set();

function compareContext(message) {
  if (
    !currentActor ||
    (currentActor.browsingContext != message.data.browsingContext &&
      currentActor.browsingContext.top != message.data.browsingContext)
  ) {
    return false;
  }

  return true;
}

// These are two synchronous messages sent by the child.
// The browsingContext within the message data is either the one that has
// the active autocomplete popup or the top-level of the one that has
// the active autocomplete popup.
Services.ppmm.addMessageListener("AutoComplete:GetSelectedIndex", message => {
  if (compareContext(message)) {
    let actor = currentActor;
    if (actor && actor.openedPopup) {
      return actor.openedPopup.selectedIndex;
    }
  }

  return -1;
});

Services.ppmm.addMessageListener("AutoComplete:SelectBy", message => {
  if (compareContext(message)) {
    let actor = currentActor;
    if (actor && actor.openedPopup) {
      actor.openedPopup.selectBy(message.data.reverse, message.data.page);
    }
  }
});

// AutoCompleteResultView is an abstraction around a list of results.
// It implements enough of nsIAutoCompleteController and
// nsIAutoCompleteInput to make the richlistbox popup work. Since only
// one autocomplete popup should be open at a time, this is a singleton.
var AutoCompleteResultView = {
  // nsISupports
  QueryInterface: ChromeUtils.generateQI([
    "nsIAutoCompleteController",
    "nsIAutoCompleteInput",
  ]),

  // Private variables
  results: [],

  // The AutoCompleteParent currently showing results or null otherwise.
  currentActor: null,

  // nsIAutoCompleteController
  get matchCount() {
    return this.results.length;
  },

  getValueAt(index) {
    return this.results[index].value;
  },

  getFinalCompleteValueAt(index) {
    return this.results[index].value;
  },

  getLabelAt(index) {
    return this.results[index].label;
  },

  getCommentAt(index) {
    return this.results[index].comment;
  },

  getStyleAt(index) {
    return this.results[index].style;
  },

  getImageAt(index) {
    return this.results[index].image;
  },

  handleEnter(aIsPopupSelection) {
    if (this.currentActor) {
      this.currentActor.handleEnter(aIsPopupSelection);
    }
  },

  stopSearch() {},

  searchString: "",

  // nsIAutoCompleteInput
  get controller() {
    return this;
  },

  get popup() {
    return null;
  },

  _focus() {
    if (this.currentActor) {
      this.currentActor.requestFocus();
    }
  },

  // Internal JS-only API
  clearResults() {
    this.currentActor = null;
    this.results = [];
  },

  setResults(actor, results) {
    this.currentActor = actor;
    this.results = results;
  },
};

export class AutoCompleteParent extends JSWindowActorParent {
  didDestroy() {
    if (this.openedPopup) {
      this.openedPopup.closePopup();
    }
  }

  static getCurrentActor() {
    return currentActor;
  }

  static addPopupStateListener(listener) {
    autoCompleteListeners.add(listener);
  }

  static removePopupStateListener(listener) {
    autoCompleteListeners.delete(listener);
  }

  handleEvent(evt) {
    switch (evt.type) {
      case "popupshowing": {
        this.sendAsyncMessage("AutoComplete:PopupOpened", {});
        break;
      }

      case "popuphidden": {
        let selectedIndex = this.openedPopup.selectedIndex;
        let selectedRowComment =
          selectedIndex != -1
            ? AutoCompleteResultView.getCommentAt(selectedIndex)
            : "";
        let selectedRowStyle =
          selectedIndex != -1
            ? AutoCompleteResultView.getStyleAt(selectedIndex)
            : "";

        // Normally preview is cleared after selecting/hovering on a different
        // entry. However, we also need to clear the preview when a pop is closed.
        this.clearAutoCompletePreview();

        this.sendAsyncMessage("AutoComplete:PopupClosed", {
          selectedRowComment,
          selectedRowStyle,
        });
        AutoCompleteResultView.clearResults();
        // adjustHeight clears the height from the popup so that
        // we don't have a big shrink effect if we closed with a
        // large list, and then open on a small one.
        this.openedPopup.adjustHeight();
        this.openedPopup = null;
        currentActor = null;
        evt.target.removeEventListener("popuphidden", this);
        evt.target.removeEventListener("popupshowing", this);
        break;
      }
    }
  }

  showPopupWithResults({ rect, dir, results }) {
    if (!results.length || this.openedPopup) {
      // We shouldn't ever be showing an empty popup, and if we
      // already have a popup open, the old one needs to close before
      // we consider opening a new one.
      return;
    }

    let browser = this.browsingContext.top.embedderElement;
    let window = browser.ownerGlobal;
    // Also check window top in case this is a sidebar.
    if (
      Services.focus.activeWindow !== window.top &&
      Services.focus.focusedWindow.top !== window.top
    ) {
      // We were sent a message from a window or tab that went into the
      // background, so we'll ignore it for now.
      return;
    }

    // Non-empty result styles
    let resultStyles = new Set(results.map(r => r.style).filter(r => !!r));
    currentActor = this;
    this.openedPopup = browser.autoCompletePopup;
    // the layout varies according to different result type
    this.openedPopup.setAttribute("resultstyles", [...resultStyles].join(" "));
    this.openedPopup.hidden = false;
    // don't allow the popup to become overly narrow
    this.openedPopup.style.setProperty(
      "--panel-width",
      Math.max(100, rect.width) + "px"
    );
    this.openedPopup.style.direction = dir;

    AutoCompleteResultView.setResults(this, results);

    this.openedPopup.view = AutoCompleteResultView;
    this.openedPopup.selectedIndex = -1;

    // Reset fields that were set from the last time the search popup was open
    this.openedPopup.mInput = AutoCompleteResultView;
    // Temporarily increase the maxRows as we don't want to show
    // the scrollbar in login or form autofill popups.
    if (
      resultStyles.size &&
      (resultStyles.has("autofill") || resultStyles.has("loginsFooter"))
    ) {
      this.openedPopup._normalMaxRows = this.openedPopup.maxRows;
      this.openedPopup.mInput.maxRows = 10;
    }
    browser.constrainPopup(this.openedPopup);
    this.openedPopup.addEventListener("popuphidden", this);
    this.openedPopup.addEventListener("popupshowing", this);
    this.openedPopup.openPopupAtScreenRect(
      "after_start",
      rect.left,
      rect.top,
      rect.width,
      rect.height,
      false,
      false
    );
    this.openedPopup.invalidate();
    this._maybeRecordTelemetryEvents(results);

    // This is a temporary solution. We should replace it with
    // proper meta information about the popup once such field
    // becomes available.
    let isCreditCard = results.some(result =>
      result?.comment?.includes("cc-number")
    );

    if (isCreditCard) {
      this.delayPopupInput();
    }
  }

  /**
   * @param {object[]} results - Non-empty array of autocomplete results.
   */
  _maybeRecordTelemetryEvents(results) {
    let actor =
      this.browsingContext.currentWindowGlobal.getActor("LoginManager");
    actor.maybeRecordPasswordGenerationShownTelemetryEvent(results);

    // Assume the result with the start time (loginsFooter) is last.
    let lastResult = results[results.length - 1];
    if (lastResult.style != "loginsFooter") {
      return;
    }

    // The comment field of `loginsFooter` results have many additional pieces of
    // information for telemetry purposes. After bug 1555209, this information
    // can be passed to the parent process outside of nsIAutoCompleteResult APIs
    // so we won't need this hack.
    let rawExtraData = JSON.parse(lastResult.comment).telemetryEventData;
    if (!rawExtraData.searchStartTimeMS) {
      throw new Error("Invalid autocomplete search start time");
    }

    if (rawExtraData.stringLength > 1) {
      // To reduce event volume, only record for lengths 0 and 1.
      return;
    }

    let duration =
      Services.telemetry.msSystemNow() - rawExtraData.searchStartTimeMS;
    delete rawExtraData.searchStartTimeMS;

    // Add counts by result style to rawExtraData.
    results.reduce((accumulated, r) => {
      // Ignore learn more as it is only added after importable logins.
      // Do not track generic items in the telemetry.
      if (r.style === "importableLearnMore" || r.style === "generic") {
        return accumulated;
      }

      // Keys can be a maximum of 15 characters and values must be strings.
      // Also treat both "loginWithOrigin" and "login" as "login" as extra_keys
      // is limited to 10.
      let truncatedStyle = r.style.substring(
        0,
        r.style === "loginWithOrigin" ? 5 : 15
      );
      accumulated[truncatedStyle] = (accumulated[truncatedStyle] || 0) + 1;
      return accumulated;
    }, rawExtraData);

    // Even though Glean events do not require converting extra values to
    // strings, keep doing it so booleans keep being encoded as they were for
    // Telemetry recordEvent.
    let extraStrings = Object.fromEntries(
      Object.entries(rawExtraData).map(([key, val]) => {
        let stringVal = "";
        if (typeof val == "boolean") {
          stringVal += val ? "1" : "0";
        } else {
          stringVal += val;
        }
        return [key, stringVal];
      })
    );

    extraStrings.value = duration;
    Glean.formAutocomplete.showLogins.record(extraStrings);
  }

  invalidate(results) {
    if (!this.openedPopup) {
      return;
    }

    if (!results.length) {
      this.closePopup();
    } else {
      AutoCompleteResultView.setResults(this, results);
      this.openedPopup.invalidate();
      this._maybeRecordTelemetryEvents(results);
    }
  }

  closePopup() {
    if (this.openedPopup) {
      // Note that hidePopup() closes the popup immediately,
      // so popuphiding or popuphidden events will be fired
      // and handled during this call.
      this.openedPopup.hidePopup();
    }
  }

  async receiveMessage(message) {
    let browser = this.browsingContext.top.embedderElement;

    if (
      !browser ||
      (!AppConstants.MOZ_GECKOVIEW && !browser.autoCompletePopup)
    ) {
      // If there is no browser or popup, just make sure that the popup has been closed.
      if (this.openedPopup) {
        this.openedPopup.closePopup();
      }

      // Returning false to pacify ESLint, but this return value is
      // ignored by the messaging infrastructure.
      return false;
    }

    switch (message.name) {
      // This is called when an autocomplete entry is selected by the users.
      // In the current design, when a selection is triggered from the parent
      // process (ex, select by mouse click), we still first send the "HandleEnter"
      // message to the child and then child send the "SelectEntry" message back
      // to the parent to indicate that an autocomplete entry is selected.
      case "AutoComplete:SelectEntry": {
        if (this.openedPopup) {
          this.selectAutoCompleteEntry(this.openedPopup.selectedIndex);
        }
        break;
      }

      case "AutoComplete:SetSelectedIndex": {
        let { index } = message.data;
        if (this.openedPopup) {
          this.openedPopup.selectedIndex = index;
        }
        break;
      }

      case "AutoComplete:MaybeOpenPopup": {
        let { results, rect, dir, inputElementIdentifier, formOrigin } =
          message.data;
        if (AppConstants.MOZ_GECKOVIEW) {
          lazy.GeckoViewAutocomplete.delegateSelection({
            browsingContext: this.browsingContext,
            options: results,
            inputElementIdentifier,
            formOrigin,
          });
        } else {
          this.showPopupWithResults({ results, rect, dir });
          this.notifyListeners();

          this.notifyAutoCompletePopupOpened(
            JSON.stringify(inputElementIdentifier)
          );
        }
        break;
      }

      case "AutoComplete:Invalidate": {
        let { results } = message.data;
        this.invalidate(results);
        break;
      }

      case "AutoComplete:ClosePopup": {
        if (AppConstants.MOZ_GECKOVIEW) {
          lazy.GeckoViewAutocomplete.delegateDismiss();
          break;
        }
        this.closePopup();
        break;
      }

      case "AutoComplete:StartSearch": {
        const { searchString, data } = message.data;
        const result = await this.#startSearch(searchString, data);
        return Promise.resolve(result);
      }
    }
    // Returning false to pacify ESLint, but this return value is
    // ignored by the messaging infrastructure.
    return false;
  }

  // Imposes a brief period during which the popup will not respond to
  // a click, so as to reduce the chances of a successful clickjacking
  // attempt
  delayPopupInput() {
    if (!this.openedPopup) {
      return;
    }
    const popupDelay = Services.prefs.getIntPref(PREF_SECURITY_DELAY);

    // Mochitests set this to 0, and many will fail on integration
    // if we make the popup items inactive, even briefly.
    if (!popupDelay) {
      return;
    }

    const items = Array.from(
      this.openedPopup.getElementsByTagName("richlistitem")
    );
    items.forEach(item => (item.disabled = true));

    let timerId;
    const delay = () => {
      if (timerId) {
        lazy.clearTimeout(timerId);
      }
      timerId = lazy.setTimeout(() => {
        items.forEach(item => (item.disabled = false));
        this.openedPopup?.removeEventListener("click", delay);
      }, popupDelay);
    };

    this.openedPopup.addEventListener("click", delay);
    delay();
  }

  notifyListeners() {
    let window = this.browsingContext.top.embedderElement.ownerGlobal;
    for (let listener of autoCompleteListeners) {
      try {
        listener(window);
      } catch (ex) {
        console.error(ex);
      }
    }
  }

  /**
   * Despite its name, this handleEnter is only called when the user clicks on
   * one of the items in the popup since the popup is rendered in the parent process.
   * The real controller's handleEnter is called directly in the content process
   * for other methods of completing a selection (e.g. using the tab or enter
   * keys) since the field with focus is in that process.
   * @param {boolean} aIsPopupSelection
   */
  handleEnter(aIsPopupSelection) {
    if (this.openedPopup) {
      this.sendAsyncMessage("AutoComplete:HandleEnter", {
        selectedIndex: this.openedPopup.selectedIndex,
        isPopupSelection: aIsPopupSelection,
      });
    }
  }

  // This defines the supported autocomplete providers and the prioity to show the autocomplete
  // entry.
  #AUTOCOMPLETE_PROVIDERS = ["FormAutofill", "LoginManager", "FormHistory"];

  /**
   * Search across multiple module to gather autocomplete entries for a given search string.
   *
   * @param {string} searchString
   *                 The input string used to query autocomplete entries across different
   *                 autocomplete providers.
   * @param {Array<Object>} providers
   *                        An array of objects where each object has a `name` used to identify the actor
   *                        name of the provider and `options` that are passed to the `searchAutoCompleteEntries`
   *                        method of the actor.
   * @returns {Array<Object>} An array of results objects with `name` of the provider and `entries`
   *          that are returned from the provider module's `searchAutoCompleteEntries` method.
   */
  async #startSearch(searchString, providers) {
    for (const name of this.#AUTOCOMPLETE_PROVIDERS) {
      const provider = providers.find(p => p.actorName == name);
      if (!provider) {
        continue;
      }
      const { actorName, options } = provider;
      const actor =
        this.browsingContext.currentWindowGlobal.getActor(actorName);
      const entries = await actor?.searchAutoCompleteEntries(
        searchString,
        options
      );

      // We have not yet supported showing autocomplete entries from multiple providers,
      if (entries) {
        return [{ actorName, ...entries }];
      }
    }
    return [];
  }

  stopSearch() {}

  // Hard-coded the mapping by using the message prefix to find the actor
  // to process a given message.
  #getActorByMessagePrefix(message) {
    const prefixToActor = [
      { prefix: "PasswordManager", actor: "LoginManager" },
      { prefix: "FormAutofill", actor: "FormAutofill" },
    ];

    const name = prefixToActor.find(x => message.startsWith(x.prefix))?.actor;
    return this.browsingContext.currentWindowGlobal.getActor(name);
  }

  /**
   * When an autocomplete popup is opened, we notify all the autocomplete
   * entry providers that have an entry displayed in this popup.
   *
   * @param {ElementIdentifier} elementId The element with which the autocomplete popup is associated
   */
  notifyAutoCompletePopupOpened(elementId) {
    const actors = new Set();
    for (const result of AutoCompleteResultView.results) {
      try {
        const { fillMessageName } = JSON.parse(result.comment);
        if (!fillMessageName) {
          continue;
        }

        actors.add(this.#getActorByMessagePrefix(fillMessageName));
      } catch {}
    }

    for (const actor of actors) {
      actor.onAutoCompletePopupOpened?.(elementId);
    }
  }

  /**
   * Clear the autocomplete preview
   */
  clearAutoCompletePreview() {
    const selectedIndex = this.openedPopup?.selectedIndex;
    const result = AutoCompleteResultView.results[selectedIndex];
    if (!result) {
      return;
    }

    const { fillMessageName, fillMessageData } = JSON.parse(
      result.comment || "{}"
    );
    if (!fillMessageName) {
      return;
    }

    const actor = this.#getActorByMessagePrefix(fillMessageName);
    actor?.onAutoCompleteEntryClearPreview?.(fillMessageName, fillMessageData);
  }

  /**
   * Show the autocomplete preview for the current selected entry.
   */
  previewAutoCompleteEntry() {
    const selectedIndex = this.openedPopup?.selectedIndex;
    const result = AutoCompleteResultView.results[selectedIndex];
    if (!result) {
      return;
    }

    const { fillMessageName, fillMessageData } = JSON.parse(
      result.comment || "{}"
    );
    if (!fillMessageName) {
      return;
    }

    const actor = this.#getActorByMessagePrefix(fillMessageName);
    actor?.onAutoCompleteEntryHovered?.(fillMessageName, fillMessageData);
  }

  /**
   * When an autocomplete entry is selected, notify the actor that provides the entry
   */
  selectAutoCompleteEntry() {
    const selectedIndex = this.openedPopup?.selectedIndex;
    const result = AutoCompleteResultView.results[selectedIndex];
    if (!result) {
      return;
    }

    const { fillMessageName, fillMessageData } = JSON.parse(
      result.comment || "{}"
    );
    if (!fillMessageName) {
      return;
    }

    const actor = this.#getActorByMessagePrefix(fillMessageName);
    actor?.onAutoCompleteEntrySelected?.(fillMessageName, fillMessageData);
  }

  /**
   * Sends a message to the browser that is requesting the input
   * that the open popup should be focused.
   */
  requestFocus() {
    // Bug 1582722 - See the response in AutoCompleteChild.sys.mjs for why this
    // disabled.
    /*
    if (this.openedPopup) {
      this.sendAsyncMessage("AutoComplete:Focus");
    }
    */
  }
}
