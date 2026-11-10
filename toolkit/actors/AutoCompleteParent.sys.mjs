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

// How long a removal waits for the provider to reopen the dropdown.
const REMOVAL_REOPEN_TIMEOUT_MS = 5000;

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

Services.ppmm.addMessageListener(
  "AutoComplete:NavigateSecondaryAction",
  message => {
    if (compareContext(message)) {
      let actor = currentActor;
      if (actor && actor.openedPopup) {
        return actor.openedPopup.navigateSecondaryAction(message.data.reverse);
      }
    }

    return false;
  }
);

Services.ppmm.addMessageListener(
  "AutoComplete:MaybeActivateSecondaryAction",
  message => {
    if (compareContext(message)) {
      let actor = currentActor;
      if (actor && actor.openedPopup) {
        return actor.openedPopup.maybeActivateSecondaryAction();
      }
    }

    return false;
  }
);

Services.ppmm.addMessageListener(
  "AutoComplete:MaybeLeaveSecondaryAction",
  message => {
    if (compareContext(message)) {
      let actor = currentActor;
      if (actor && actor.openedPopup) {
        return actor.openedPopup.maybeLeaveSecondaryAction();
      }
    }

    return false;
  }
);

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

/**
 * The type of saved record an autocomplete result represents, as reported in
 * telemetry, or null when the result isn't a saved record.
 *
 * @param {string} style The result's style
 * @param {object} comment The result's parsed comment
 * @returns {?string} "formhistory", "address", "payment" or "password"
 */
function getSavedRecordType(style, comment) {
  switch (style) {
    case "fromhistory":
      return "formhistory";
    case "loginWithOrigin":
      return "password";
    case "autofill":
      return ["address", "payment"].includes(comment.type)
        ? comment.type
        : null;
  }
  return null;
}

/**
 * The saved record type of the first result that is a saved record, if any.
 *
 * @param {object[]} results Autocomplete results
 * @returns {?string} See getSavedRecordType
 */
function getFirstSavedRecordType(results) {
  for (const result of results) {
    let recordType;
    try {
      recordType = getSavedRecordType(
        result.style,
        JSON.parse(result.comment || "{}")
      );
    } catch (ex) {
      // Non-saved-record results (e.g. plain history suggestions) can
      // have a non-JSON comment; they're never a saved record type.
      continue;
    }
    if (recordType) {
      return recordType;
    }
  }
  return null;
}

export class AutoCompleteParent extends JSWindowActorParent {
  #reportedTelemetryInputs = new Set();

  /**
   * Identifies the current dropdown open, so that its close can be correlated
   * with it in telemetry.
   *
   * @type {?string}
   */
  #dropdownFlowId = null;

  /**
   * The saved record type shown in the current dropdown, if any.
   *
   * @type {?string}
   */
  #dropdownRecordType = null;

  /**
   * Why the current dropdown is closing; null means it was dismissed.
   *
   * @type {?string}
   */
  #dropdownCloseReason = null;

  /**
   * The input the current dropdown is shown for, as "browsingContextId|id".
   *
   * @type {?string}
   */
  #dropdownInputKey = null;

  /**
   * Whether typing left the current dropdown without saved records.
   *
   * @type {boolean}
   */
  #savedRecordsExhausted = false;

  /**
   * A record removal in progress, so the dropdown it reopens keeps its flow.
   *
   * @type {?{ flowId: string, inputKey: ?string }}
   */
  #pendingRemoval = null;

  #pendingRemovalTimer = null;

  #closingForEmptyResults = false;

  /**
   * The entry whose provider was last asked for a preview, so that the
   * provider can be told to drop it once the preview moves elsewhere.
   *
   * @type {{
   *   actor: JSWindowActorParent,
   *   fillMessageName: string,
   *   fillMessageData: object
   * } | null}
   */
  #previewedEntry = null;

  didDestroy() {
    if (this.openedPopup) {
      // Only reached on platforms (GeckoView) where the actor is torn down
      // while the popup is still open. On desktop, nsFormFillController's
      // own pagehide handling closes the popup via the generic path first,
      // so this never marks a desktop tab-close/navigation close as
      // "left_page" -- see the dropdown_closed close_reason doc in
      // metrics.yaml.
      this.#dropdownCloseReason = "left_page";
      this.openedPopup.closePopup();
    }
    this.#clearPendingRemoval();
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
    // Popups nested inside the panel (such as a row's secondary action menu)
    // bubble their own popup events up to it, so only react to the panel's.
    if (evt.target != this.openedPopup) {
      return;
    }

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

        if (this.#dropdownRecordType) {
          Glean.autocomplete.dropdownClosed.record({
            record_type: this.#dropdownRecordType,
            close_reason:
              this.#pendingRemoval && !this.#closingForEmptyResults
                ? "removal_prompt"
                : (this.#dropdownCloseReason ??
                  (this.#savedRecordsExhausted
                    ? "typed_through"
                    : "dismissed")),
            flow_id: this.#dropdownFlowId ?? "",
          });
        }
        this.#dropdownRecordType = null;
        this.#dropdownCloseReason = null;
        this.#savedRecordsExhausted = false;
        this.#dropdownFlowId = null;
        this.#dropdownInputKey = null;

        this.openedPopup = null;
        currentActor = null;
        evt.target.removeEventListener("popuphidden", this);
        evt.target.removeEventListener("popupshowing", this);
        break;
      }
    }
  }

  showPopupWithResults({
    rect,
    dir,
    isDarkBackground,
    results,
    selectedIndex,
    inputElementIdentifier,
  }) {
    if (!results.length || this.openedPopup) {
      // We shouldn't ever be showing an empty popup, and if we
      // already have a popup open, the old one needs to close before
      // we consider opening a new one.
      return;
    }

    if (!this.browsingContext.canOpenModalPicker) {
      return;
    }

    let browser = this.browsingContext.top.embedderElement;

    let tabbrowser = browser.getTabBrowser();
    if (tabbrowser && tabbrowser.selectedBrowser != browser) {
      // Overly cautious check, because AsyncTabSwitcher might delay
      // deactivating our browser.
      return;
    }

    // Non-empty result styles
    let resultStyles = new Set(results.map(r => r.style).filter(r => !!r));
    currentActor = this;
    this.openedPopup = browser.autoCompletePopup;
    // the layout varies according to different result type
    this.openedPopup.setAttribute("resultstyles", [...resultStyles].join(" "));
    this.openedPopup.hidden = false;
    this.openedPopup.style.direction = dir;
    this.openedPopup.style.colorScheme = isDarkBackground ? "dark" : "light";

    AutoCompleteResultView.setResults(this, results);

    this.openedPopup.view = AutoCompleteResultView;

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
    this.openedPopup.selectedIndex = selectedIndex;
    this._maybeRecordTelemetryEvents(results, inputElementIdentifier);
    this.#recordDropdownShown(
      results,
      inputElementIdentifier
        ? `${inputElementIdentifier.browsingContextId}|${inputElementIdentifier.id}`
        : null
    );

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
   * @param {?string} inputKey - The input the dropdown is shown for.
   */
  #recordDropdownShown(results, inputKey) {
    const reopenedAfterRemoval =
      Boolean(inputKey) && this.#pendingRemoval?.inputKey === inputKey;
    this.#dropdownCloseReason = null;
    this.#savedRecordsExhausted = false;
    this.#dropdownInputKey = inputKey;
    this.#dropdownFlowId = reopenedAfterRemoval
      ? this.#pendingRemoval.flowId
      : Services.uuid.generateUUID().toString();
    this.#clearPendingRemoval();

    const recordType = getFirstSavedRecordType(results);
    this.#dropdownRecordType = recordType;
    if (recordType) {
      Glean.autocomplete.dropdownShown.record({
        record_type: recordType,
        flow_id: this.#dropdownFlowId,
        reopened_after_removal: reopenedAfterRemoval,
      });
    }
  }

  #clearPendingRemoval() {
    lazy.clearTimeout(this.#pendingRemovalTimer);
    this.#pendingRemovalTimer = null;
    this.#pendingRemoval = null;
  }

  #onRemovalSettled(pending) {
    if (this.#pendingRemoval != pending) {
      return;
    }
    if (this.openedPopup) {
      this.#clearPendingRemoval();
      return;
    }
    this.#pendingRemovalTimer = lazy.setTimeout(() => {
      if (this.#pendingRemoval == pending) {
        this.#clearPendingRemoval();
      }
    }, REMOVAL_REOPEN_TIMEOUT_MS);
  }

  /**
   * @param {object[]} results - Non-empty array of autocomplete results.
   */
  _maybeRecordTelemetryEvents(results, inputElementIdentifier = null) {
    let actor =
      this.browsingContext.currentWindowGlobal.getActor("LoginManager");
    actor.maybeRecordPasswordGenerationShownTelemetryEvent(results);

    if (!inputElementIdentifier) {
      return;
    }
    const inputKey = `${inputElementIdentifier.browsingContextId}|${inputElementIdentifier.id}`;
    // The event is recorded once per input element: reopening the popup on the
    // same field tells us nothing new and would skew the duration data.
    if (this.#reportedTelemetryInputs.has(inputKey)) {
      return;
    }
    this.#reportedTelemetryInputs.add(inputKey);

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
      // Only set the fallback reason if a secondary action (handled in
      // selectAutoCompleteEntry) hasn't already explained this close, e.g.
      // deleting the last entry empties the dropdown via this same path.
      if (!this.#dropdownCloseReason) {
        this.#dropdownCloseReason = "typed_through";
      }
      this.#closingForEmptyResults = true;
      try {
        this.closePopup();
      } finally {
        this.#closingForEmptyResults = false;
      }
    } else {
      if (this.#dropdownRecordType) {
        // Footer rows can outlive the saved records.
        this.#savedRecordsExhausted = !getFirstSavedRecordType(results);
      }
      AutoCompleteResultView.setResults(this, results);
      this.openedPopup.invalidate();
      this.#notifyAutoCompletePopupUpdated();
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
    // Handled before the browser/popup guard below because the delegated
    // GeckoView prompt must be torn down even when its document (and browser)
    // is going away. Only sent on GeckoView (see the actor registration).
    if (
      AppConstants.MOZ_GECKOVIEW &&
      message.name == "AutoComplete:DocumentHidden"
    ) {
      lazy.GeckoViewAutocomplete.reset(this.manager?.innerWindowId);
      return false;
    }

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
          this.selectAutoCompleteEntry();
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
        let {
          results,
          rect,
          dir,
          isDarkBackground,
          inputElementIdentifier,
          formOrigin,
          selectedIndex,
        } = message.data;
        if (AppConstants.MOZ_GECKOVIEW) {
          lazy.GeckoViewAutocomplete.delegateSelection({
            browsingContext: this.browsingContext,
            options: results,
            inputElementIdentifier,
            formOrigin,
          });
        } else {
          this.showPopupWithResults({
            results,
            rect,
            dir,
            isDarkBackground,
            selectedIndex,
            inputElementIdentifier,
          });
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
    let window = this.browsingContext.top.embedderElement.documentGlobal;
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
   *
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
  // entry. LoginManager is prioritized to handle potential username fields first,
  // allowing FormAutofill to safely support single email fields without
  // manual exclusions.
  #AUTOCOMPLETE_PROVIDERS = [
    "LoginManager",
    "FormAutofill",
    "FormHistory",
    "SmartFormFill",
  ];

  /**
   * Search across multiple module to gather autocomplete entries for a given search string.
   *
   * @param {string} searchString
   *                 The input string used to query autocomplete entries across different
   *                 autocomplete providers.
   * @param {Array<object>} providers
   *                        An array of objects where each object has a `name` used to identify the actor
   *                        name of the provider and `options` that are passed to the `searchAutoCompleteEntries`
   *                        method of the actor.
   * @returns {Array<object>} An array of results objects with `name` of the provider and `entries`
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
      { prefix: "FormHistory", actor: "FormHistory" },
      { prefix: "SmartFormFill", actor: "SmartFormFill" },
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
   * Notifies displayed entry providers after an open popup is updated.
   */
  #notifyAutoCompletePopupUpdated() {
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
      actor.onAutoCompletePopupUpdated?.();
    }
  }

  /**
   * Clear the autocomplete preview
   */
  clearAutoCompletePreview() {
    const entry = this.#previewedEntry;
    this.#previewedEntry = null;
    entry?.actor?.onAutoCompleteEntryClearPreview?.(
      entry.fillMessageName,
      entry.fillMessageData
    );
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
    const actor = fillMessageName
      ? this.#getActorByMessagePrefix(fillMessageName)
      : null;

    // A provider only drops its preview when another of its own entries is
    // previewed, so an entry belonging to a different provider (such as the
    // Smart Form Fill row) has to drop the previous preview here.
    if (this.#previewedEntry?.actor != actor) {
      this.clearAutoCompletePreview();
    }

    if (!actor) {
      return;
    }

    this.#previewedEntry = { actor, fillMessageName, fillMessageData };
    actor.onAutoCompleteEntryHovered?.(fillMessageName, fillMessageData);
  }

  /**
   * When an autocomplete entry is selected, notify the actor that provides the
   * entry. The same path handles an entry's secondary action (such as the edit
   * button shown next to a saved login): when `secondary` is true we dispatch
   * the message declared by the entry's `secondaryAction` instead of its
   * primary fill message. When the secondary action is a menu, `actionIndex`
   * selects which of its `actions` to dispatch. An action that declares no
   * `fillMessageName` dispatches nothing.
   *
   * A secondary action on a saved record is recorded in telemetry, with the
   * record type inferred from the result and the action's `type`. The
   * provider also receives the dropdown's flow id as `{ flowId }`, and a
   * "delete" action keeps that flow id for the dropdown it reopens.
   *
   * @param {boolean} secondary Whether to dispatch the entry's secondary action.
   * @param {number} [actionIndex] Which secondary menu action to dispatch;
   *   omitted for a single (non-menu) secondary action.
   * @param {number} [index] The entry's index; defaults to the selected one.
   */
  selectAutoCompleteEntry(
    secondary = false,
    actionIndex,
    index = this.openedPopup?.selectedIndex
  ) {
    const result = AutoCompleteResultView.results[index];
    if (!result) {
      return;
    }
    if (secondary) {
      // A secondary action (e.g. deleting the last entry) can empty the
      // dropdown and close it via invalidate(), which would otherwise
      // default that close to "typed_through". Mark it dismissed instead,
      // since the user didn't pick a suggestion or keep typing.
      this.#dropdownCloseReason = "dismissed";
    } else {
      this.#dropdownCloseReason = "selected";
    }

    const parsedComment = JSON.parse(result.comment || "{}");
    let entry;
    if (!secondary) {
      entry = parsedComment;
    } else {
      const secondaryAction = parsedComment.secondaryAction ?? {};
      if (actionIndex === undefined) {
        entry = secondaryAction;
      } else {
        entry = secondaryAction.actions?.[actionIndex] ?? {};
      }

      const recordType = getSavedRecordType(result.style, parsedComment);
      if (recordType && entry.type) {
        Glean.autocomplete.recordInteraction.record({
          record_type: recordType,
          interaction_type: entry.type,
          flow_id: this.#dropdownFlowId ?? "",
        });
      }
    }

    const { fillMessageName, fillMessageData } = entry;
    if (!fillMessageName) {
      return;
    }

    const flowId = this.#dropdownFlowId ?? "";
    let pending = null;
    if (secondary && entry.type == "delete" && this.#dropdownFlowId) {
      this.#clearPendingRemoval();
      pending = { flowId, inputKey: this.#dropdownInputKey };
      this.#pendingRemoval = pending;
    }

    const actor = this.#getActorByMessagePrefix(fillMessageName);
    const selected = actor?.onAutoCompleteEntrySelected?.(
      fillMessageName,
      fillMessageData,
      { flowId }
    );
    if (pending) {
      Promise.resolve(selected).finally(() => this.#onRemovalSettled(pending));
    }
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
