/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

import UrlbarPrefs from "chrome://browser/content/urlbar/UrlbarContentPrefs.mjs";
import { UrlbarResult } from "chrome://browser/content/urlbar/UrlbarResult.mjs";
import { UrlbarShared } from "chrome://browser/content/urlbar/UrlbarShared.mjs";

const lazy = typeof ChromeUtils != "undefined" ? {} : null;

if (lazy) {
  ChromeUtils.defineESModuleGetters(lazy, {
    UrlbarUtils: "moz-src:///browser/components/urlbar/UrlbarUtils.sys.mjs",
  });
}

/**
 * @import {SmartbarInput} from "moz-src:///browser/components/urlbar/content/SmartbarInput.mjs"
 */

/**
 * Stateless helpers for assembling the urlbar's engagement-telemetry snapshot.
 * They're split out so the parent-side recorder and a content-side collector
 * can build the same snapshot from their own `start()`/`record()` orchestration
 * without sharing state or each other's dependencies. Everything here is
 * derived purely from the DOM event, the picked result, and the search string,
 * so it runs on whichever side holds the input and view.
 */
export class UrlbarTelemetryUtils {
  /**
   * Classifies the action behind an engagement from its DOM event and the
   * picked element. This is the part of the snapshot that can only be resolved
   * content-side, where the event and element live.
   *
   * @param {?Event} event
   *   The DOM event, or null for paste&go / drop&go.
   * @param {object} details
   *   The interaction details; `details.element`/`details.selType` refine the
   *   action for picked commands and dismissals.
   * @param {string} defaultInteractionType
   *   The session's interaction type, used when there's no event.
   * @returns {string} The action string recorded in telemetry.
   */
  static actionFromEvent(event, details, defaultInteractionType) {
    if (!event) {
      return defaultInteractionType === "dropped" ? "drop_go" : "paste_go";
    }
    if (event.type === "blur") {
      return "blur";
    }
    if (event.type === "tabswitch") {
      return "tab_switch";
    }
    // dismiss_autofill temporarily blocks autofill suggestions rather than
    // removing history, but we still want to report it "dismiss" in telemetry.
    if (details.element?.dataset.command === "dismiss_autofill") {
      return "dismiss";
    }
    if (
      details.element?.dataset.command &&
      details.element.dataset.command !== "help"
    ) {
      return details.element.dataset.command;
    }
    if (details.selType === "dismiss") {
      return "dismiss";
    }
    if (UrlbarShared.isInstance(event, MouseEvent)) {
      // TODO (Bug 2018250): Don’t rely on `event` and use `selType` or
      // `details.element` if possible.
      return /** @type {HTMLElement} */ (event.target).classList.contains(
        "urlbar-go-button"
      )
        ? "go_button"
        : "click";
    }
    return "enter";
  }

  /**
   * Derives the character and word counts telemetry records from the search
   * string. The string itself is never recorded.
   *
   * numWords is not a perfect measurement, since it will return an incorrect
   * value for languages that do not use spaces or URLs containing spaces in
   * their query parameters, for example.
   *
   * @param {string} searchString
   *   The user's search string.
   * @returns {{numChars: string, numWords: string, searchWords: string[]}}
   */
  static parseSearchString(searchString) {
    let numChars = searchString.length.toString();
    let searchWords = searchString
      .substring(0, UrlbarShared.MAX_TEXT_LENGTH)
      .trim()
      .split(UrlbarShared.REGEXP_SPACES)
      .filter(t => t);
    let numWords = searchWords.length.toString();

    return {
      numChars,
      numWords,
      searchWords,
    };
  }

  /**
   * Derives the interaction type a session started with from its initiating
   * event. Callers apply any explicit interaction-type hint themselves, so this
   * only inspects the event and search string.
   *
   * @param {Event} event
   *   The event that started the session.
   * @param {string} [searchString]
   *   The search string related to the event, if any.
   * @returns {string} The interaction type.
   */
  static startInteractionType(event, searchString) {
    if (event.type == "input") {
      // We can't use `InputEvent.isInstance(event)` here because that doesn't
      // work in the newtab context.
      return UrlbarShared.isPasteEvent(/** @type {InputEvent} */ (event))
        ? "pasted"
        : "typed";
    } else if (event.type == "drop") {
      return "dropped";
    } else if (event.type == "paste") {
      return "pasted";
    } else if (searchString) {
      return "typed";
    }
    return "topsites";
  }

  /**
   * Assembles the engagement snapshot a recording needs from the picked result
   * and the DOM event, synchronously at engagement time. Values that can only
   * be derived content-side (the `action`, the picked action key) are resolved
   * here, so the recording itself needs neither the event nor the element.
   *
   * @param {?Event} event
   *   The DOM event behind the engagement, or null for paste&go / drop&go.
   * @param {object} details
   *   The interaction details. `details.isSessionOngoing` is set here, as the
   *   recording expects.
   * @param {?object} startEventInfo
   *   The session's start info from `start()`, or null when no session is in
   *   progress.
   * @returns {?object} The snapshot to record from, or null when there is no
   *   session in progress to record.
   */
  static collectSnapshot(event, details, startEventInfo) {
    if (!startEventInfo) {
      return null;
    }
    if (
      !event &&
      startEventInfo.interactionType != "pasted" &&
      startEventInfo.interactionType != "dropped"
    ) {
      // If no event is passed, we must be executing either paste&go or drop&go.
      throw new Error("Event must be defined, unless input was pasted/dropped");
    }
    if (!details) {
      throw new Error("Invalid event details: " + details);
    }

    let action = this.actionFromEvent(
      event,
      details,
      startEventInfo.interactionType
    );

    /** @type {"abandonment" | "engagement"} */
    let method =
      action == "blur" || action == "tab_switch" ? "abandonment" : "engagement";

    if (method == "engagement") {
      // Not all engagements end the search session. The session remains ongoing
      // when certain commands are picked (like dismissal) and results that
      // enter search mode are picked. We should find a generalized way to
      // determine this instead of listing all the cases like this.
      details.isSessionOngoing = !!(
        [
          "dismiss",
          "inaccurate_location",
          "not_interested",
          "not_now",
          "opt_in",
          "show_less_frequently",
        ].includes(details.selType) ||
        details.result?.payload.providesSearchMode
      );
    }

    let { numChars, numWords, searchWords } = this.parseSearchString(
      details.searchString
    );

    let internalDetails = {
      ...details,
      event,
      provider: details.result?.providerName,
      selIndex: details.result?.rowIndex ?? -1,
      pickedActionKey: details.element?.dataset.action ?? null,
    };

    return {
      method,
      action,
      startEventInfo,
      numChars,
      numWords,
      searchWords,
      internalDetails,
    };
  }

  /**
   * The input/view state the engagement recording reads, gathered from the live
   * objects. On the message path the child collector gathers this content-side
   * and ships it; the parent recorder is fed the shipped copy.
   *
   * @param {object} input
   *   The `UrlbarInput`/`SmartbarInput`.
   * @param {?object} view
   *   The `UrlbarView`, if any.
   * @returns {{searchMode: object, visibleResults: object[], viewIsOpen: boolean, searchSource: string}}
   */
  static engagementData(input, view) {
    return {
      searchMode: input.searchMode,
      visibleResults: view?.visibleResults ?? [],
      viewIsOpen: view?.isOpen ?? false,
      searchSource: input.getSearchSource(),
    };
  }

  /**
   * The smartbar-only telemetry fields, gathered from the live input. Only call
   * this when the SAP is `smartbar`, so the input is a `SmartbarInput`.
   *
   * @param {object} input
   *   The `SmartbarInput`.
   * @returns {{chatId: string, intent: string, model: string}}
   */
  static smartbarData(input) {
    const smartbar = /** @type {SmartbarInput} */ (
      /** @type {unknown} */ (input)
    );
    return {
      chatId: smartbar.conversationTelemetryInfo?.chat_id ?? "",
      intent: smartbar.smartbarAction ?? "",
      model: smartbar.modelName ?? "",
    };
  }

  /**
   * The telemetry type and (if keyword exposures are enabled) keyword recorded
   * for an exposure, computed when the exposure is added to the queue.
   *
   * @param {object} result
   *   The exposed result.
   * @param {object} queryContext
   *   The query the result belongs to.
   * @returns {{resultType: string, keyword: ?string}}
   */
  static exposureEntry(result, queryContext) {
    let resultType = UrlbarShared.searchEngagementTelemetryType(result);
    let keyword =
      !queryContext.isPrivate &&
      UrlbarPrefs.get("keywordExposureResults").has(resultType)
        ? queryContext.trimmedLowerCaseSearchString
        : null;
    return { resultType, keyword };
  }

  /**
   * Whether an exposed result was still present at session end (a "terminal"
   * exposure). This is an identity check against the live results, so it must
   * run wherever those results live (content-side for a content-process urlbar).
   *
   * @param {object} result
   *   The exposed result.
   * @param {?object} queryContext
   *   The session's query context, used for hidden exposures.
   * @param {?object[]} visibleResults
   *   The results shown at session end, used for visible exposures.
   * @returns {boolean} Whether the exposure is terminal.
   */
  static exposureTerminal(result, queryContext, visibleResults) {
    let endResults = result.isHiddenExposure
      ? queryContext?.results
      : visibleResults;
    return endResults?.includes(result) ?? false;
  }

  /**
   * Serializes a recorded engagement for the `RecordEngagement` actor message.
   * The child has already built the Glean event; the parent still needs the
   * picked result (in wire form) for the provider notifications it runs,
   * alongside the primitive built event, the disable candidate, the search
   * source, and the resolved exposure list.
   *
   * @param {object} data
   *   `{built, disableBuilt, method, searchSource, internalDetails, exposures,
   *   visibleResults}`.
   * @returns {object} The wire payload; reconstruct with
   *   `recordedEngagementFromWire()`.
   */
  static recordedEngagementToWire(data) {
    // The DOM event and element can't cross the boundary, and the parent-side
    // steps don't need them; drop them and reduce the picked result to wire form.
    let { result, ...internalDetails } = data.internalDetails;
    delete internalDetails.event;
    delete internalDetails.element;
    return {
      ...data,
      // The results shown at engagement, in wire form, so the parent can notify
      // providers' impression/abandonment hooks (which key off them) -- the
      // parent's own view has none on the message path.
      visibleResults: data.visibleResults?.map(r => r.toWire()) ?? null,
      internalDetails: {
        ...internalDetails,
        result: result?.toWire() ?? null,
      },
    };
  }

  /**
   * Reconstructs a recorded engagement from `recordedEngagementToWire()`,
   * parent-side. The dropped `event`/`element` come back as null.
   *
   * @param {object} wire
   *   The payload from `recordedEngagementToWire()`.
   * @returns {object} The reconstructed data.
   */
  static recordedEngagementFromWire(wire) {
    return {
      ...wire,
      visibleResults:
        wire.visibleResults?.map(r => UrlbarResult.fromWire(r)) ?? [],
      internalDetails: {
        ...wire.internalDetails,
        event: null,
        element: null,
        result: wire.internalDetails.result
          ? UrlbarResult.fromWire(wire.internalDetails.result)
          : null,
      },
    };
  }

  /**
   * Resolves the data a bounce recording needs from the picked result and the
   * DOM event at pick time, so the deferred recording (which runs after the
   * event is gone, and parent-side on the message path) needs neither. Like
   * `collectSnapshot()`, but for the bounce method, whose recording differs.
   *
   * @param {?Event} event
   *   The DOM event behind the engagement, or null for paste&go / drop&go.
   * @param {object} details
   *   The interaction details.
   * @param {?object} startEventInfo
   *   The session's start info, or null when no session is in progress.
   * @param {UrlbarResult[]} visibleResults
   *   The results shown at pick time. Captured into the snapshot next to
   *   `selIndex` (which indexes into it) so the deferred recording pairs them
   *   from one view read, not whatever the view shows post-navigation.
   * @returns {?object} The bounce snapshot, or null when there's no session.
   */
  static collectBounceSnapshot(event, details, startEventInfo, visibleResults) {
    if (!startEventInfo) {
      return null;
    }
    if (
      !event &&
      startEventInfo.interactionType != "pasted" &&
      startEventInfo.interactionType != "dropped"
    ) {
      // An engagement with no event that isn't paste&go / drop&go (e.g. a
      // programmatic handleCommand) can't be classified, so it isn't a
      // trackable bounce.
      return null;
    }
    if (!details) {
      throw new Error("Invalid event details: " + details);
    }

    let action = this.actionFromEvent(
      event,
      details,
      startEventInfo.interactionType
    );
    let { numChars, numWords, searchWords } = this.parseSearchString(
      details.searchString
    );

    return {
      action,
      numChars,
      numWords,
      searchWords,
      provider: details.result?.providerName,
      selIndex: details.result?.rowIndex ?? -1,
      visibleResults: visibleResults ?? [],
      selType: details.selType,
      searchSource: details.searchSource,
      searchMode: details.searchMode,
      location: details.location,
      windowMode: details.windowMode,
      startEventInfo,
    };
  }

  /**
   * The `search_mode` telemetry value for a search mode object.
   *
   * @param {?object} searchMode The active search mode, if any.
   * @returns {string} The telemetry label.
   */
  static getSearchMode(searchMode) {
    if (!searchMode) {
      return "";
    }
    if (searchMode.engineName) {
      return "search_engine";
    }
    const source = UrlbarShared.LOCAL_SEARCH_MODES.find(
      m => m.source == searchMode.source
    )?.telemetryLabel;
    return source ?? "unknown";
  }

  static #isRefined(currentSet, previousSet = null) {
    if (!previousSet) {
      return false;
    }
    const intersect = (setA, setB) => {
      let count = 0;
      for (const word of setA.values()) {
        if (setB.has(word)) {
          count += 1;
        }
      }
      return count > 0 && count != setA.size;
    };
    return (
      intersect(currentSet, previousSet) || intersect(previousSet, currentSet)
    );
  }

  /**
   * Resolves the `interaction` telemetry value, accounting for persisted search
   * terms and refined searches. The "refined" check compares against the search
   * words of the previous recorded session, so the caller threads that state in
   * and stores the returned `previousSearchWords` for next time.
   *
   * @param {string} method engagement / abandonment / disable / bounce.
   * @param {object} startEventInfo The session's start info.
   * @param {string} searchSource The search source.
   * @param {string[]} searchWords The current search words.
   * @param {?object} searchMode The active search mode.
   * @param {?Set<string>} previousSearchWords The prior session's search words.
   * @returns {{interaction: string, previousSearchWords: ?Set<string>}}
   */
  static getInteractionType(
    method,
    startEventInfo,
    searchSource,
    searchWords,
    searchMode,
    previousSearchWords
  ) {
    if (searchMode?.entry === "topsites_newtab") {
      return { interaction: "topsite_search", previousSearchWords };
    }

    let interaction = startEventInfo.interactionType;
    if (
      (interaction === "returned" || interaction === "restarted") &&
      this.#isRefined(new Set(searchWords), previousSearchWords)
    ) {
      interaction = "refined";
    }

    if (searchSource === "urlbar_persisted") {
      switch (interaction) {
        case "returned": {
          interaction = "persisted_search_terms";
          break;
        }
        case "restarted":
        case "refined": {
          interaction = `persisted_search_terms_${interaction}`;
          break;
        }
      }
    }

    let next = previousSearchWords;
    if (
      (method === "engagement" &&
        lazy?.UrlbarUtils.isPersistedSearchTermsEnabled()) ||
      method === "abandonment"
    ) {
      next = new Set(searchWords);
    } else if (method === "engagement") {
      next = null;
    }
    return { interaction, previousSearchWords: next };
  }

  /**
   * Builds the Glean event object for an engagement/abandonment/disable/bounce
   * from data available wherever the input and view live (so it runs child-side
   * on the message path). Everything here is derived from the picked result, the
   * search string, the visible results, and the session; the three fields that
   * need parent-only services -- `sap`, `search_engine_default_id`, and (for
   * engagement/abandonment) `available_semantic_sources` -- are filled in by the
   * recorder, which also makes the actual `Glean` call.
   *
   * @param {object} data
   *   The event fields.
   * @param {string} data.method
   *   One of engagement / abandonment / disable / bounce.
   * @param {string} data.action
   *   The action behind the engagement (from `actionFromEvent`).
   * @param {string} data.interaction
   *   The resolved interaction type (from `getInteractionType`).
   * @param {number} data.numChars
   *   The search string's character count.
   * @param {number} data.numWords
   *   The search string's word count.
   * @param {string} data.provider
   *   The picked result's provider name.
   * @param {string} data.searchSource
   *   The search source.
   * @param {?object} data.searchMode
   *   The active search mode, if any.
   * @param {number} data.selIndex
   *   The index of the selected result in `visibleResults`.
   * @param {UrlbarResult[]} data.visibleResults
   *   The results shown when the engagement was captured.
   * @param {boolean} data.viewIsOpen
   *   Whether the view was open.
   * @param {string} data.selType
   *   The type of the selected element.
   * @param {?string} [data.pickedActionKey]
   *   The `data-action` of the picked action button, for `selType === "action"`.
   * @param {number} [data.viewTime]
   *   The bounce view time in milliseconds.
   * @param {?object} [data.location]
   *   The interaction location; required when the sap is `smartbar`.
   * @param {string} [data.chatId]
   *   Smartbar-only chat session id.
   * @param {string} [data.intent]
   *   Smartbar-only detected intent.
   * @param {string} [data.model]
   *   Smartbar-only selected model.
   * @param {string} data.windowMode
   *   The window mode: classic, private, or smartwindow.
   * @returns {?{metric: string, eventInfo: object}} The Glean metric name and
   *   the partial event, or null for an unknown method.
   */
  static buildEventInfo({
    method,
    action,
    interaction,
    numChars,
    numWords,
    provider,
    searchSource,
    searchMode,
    selIndex,
    visibleResults,
    viewIsOpen,
    selType,
    pickedActionKey = null,
    viewTime = 0,
    location: sapLocation = null,
    chatId = "",
    intent = "",
    model: telemetryModel = "",
    windowMode,
  }) {
    const search_mode = this.getSearchMode(searchMode);
    const isSmartbar = searchSource === "smartbar";
    const smartbarExtras = isSmartbar
      ? {
          location: sapLocation,
          chat_id: chatId,
          intent,
          model: telemetryModel,
        }
      : {};
    let numResults = visibleResults.length;
    let groups = visibleResults
      .map(r => UrlbarShared.searchEngagementTelemetryGroup(r))
      .join(",");
    let results = visibleResults
      .map(r => UrlbarShared.searchEngagementTelemetryType(r))
      .join(",");
    let actions = visibleResults
      .map(r => UrlbarShared.searchEngagementTelemetryAction(r))
      .filter(v => v)
      .join(",");

    switch (method) {
      case "engagement": {
        let selected_result = UrlbarShared.searchEngagementTelemetryType(
          visibleResults[selIndex],
          selType
        );
        if (selType == "action") {
          let actionKey = UrlbarShared.searchEngagementTelemetryAction(
            visibleResults[selIndex],
            pickedActionKey
          );
          selected_result = `action_${actionKey}`;
        }
        if (selected_result === "input_field" && !viewIsOpen) {
          numResults = 0;
          groups = "";
          results = "";
        }
        return {
          metric: "engagement",
          eventInfo: {
            interaction,
            search_mode,
            n_chars: numChars.toString(),
            n_words: numWords.toString(),
            n_results: numResults.toString(),
            selected_position: (selIndex + 1).toString(),
            selected_result,
            provider,
            engagement_type:
              selType === "help" ||
              selType === "dismiss" ||
              selType === "ask_button" ||
              selType === "navigate_button" ||
              selType === "search_button"
                ? selType
                : action,
            groups,
            results,
            actions,
            window_mode: windowMode,
            ...smartbarExtras,
          },
        };
      }
      case "abandonment": {
        return {
          metric: "abandonment",
          eventInfo: {
            abandonment_type: action,
            interaction,
            search_mode,
            n_chars: numChars.toString(),
            n_words: numWords.toString(),
            n_results: numResults.toString(),
            groups,
            results,
            actions,
            window_mode: windowMode,
            ...smartbarExtras,
          },
        };
      }
      case "disable": {
        const previousEvent =
          action == "blur" || action == "tab_switch"
            ? "abandonment"
            : "engagement";
        let selected_result = "none";
        if (previousEvent == "engagement") {
          selected_result = UrlbarShared.searchEngagementTelemetryType(
            visibleResults[selIndex],
            selType
          );
        }
        return {
          metric: "disable",
          eventInfo: {
            interaction,
            search_mode,
            n_chars: numChars.toString(),
            n_words: numWords.toString(),
            n_results: numResults.toString(),
            selected_result,
            results,
            feature: "suggest",
            window_mode: windowMode,
            ...smartbarExtras,
          },
        };
      }
      case "bounce": {
        let selected_result = UrlbarShared.searchEngagementTelemetryType(
          visibleResults[selIndex],
          selType
        );
        return {
          metric: "bounce",
          eventInfo: {
            interaction,
            search_mode,
            n_chars: numChars.toString(),
            n_words: numWords.toString(),
            n_results: numResults.toString(),
            selected_result,
            selected_position: (selIndex + 1).toString(),
            provider,
            engagement_type:
              selType === "help" || selType === "dismiss" ? selType : action,
            results,
            view_time: viewTime.toString(),
            threshold: UrlbarPrefs.get(
              "events.bounce.maxSecondsFromLastSearch"
            ),
            window_mode: windowMode,
            ...smartbarExtras,
          },
        };
      }
      default: {
        console.error(`Unknown telemetry event method: ${method}`);
        return null;
      }
    }
  }

  /**
   * Builds a recorded engagement from a `collectSnapshot()` snapshot and the
   * live input/view content, resolving the interaction type against the given
   * previous-session search words (and returning the updated set for the caller
   * to store). Shared by the content-side collector and the parent's direct
   * path so both build the event identically.
   *
   * @param {object} snapshot
   *   The `collectSnapshot()` snapshot.
   * @param {object} engagementData
   *   `{searchMode, visibleResults, viewIsOpen, searchSource}`.
   * @param {object} smartbarData
   *   `{chatId, intent, model}`.
   * @param {?Set<string>} previousSearchWords
   *   The prior session's search words, for the "refined" interaction check.
   * @returns {{built: ?{metric: string, eventInfo: object}, previousSearchWords: ?Set<string>}}
   */
  static buildRecordedEngagement(
    snapshot,
    engagementData,
    smartbarData,
    previousSearchWords
  ) {
    return this.#buildRecorded(
      snapshot.method,
      snapshot,
      engagementData,
      smartbarData,
      previousSearchWords
    );
  }

  /**
   * Builds the disable-tracking candidate event for an engagement/abandonment
   * that showed a Suggest result. It's recorded later, if the user turns Suggest
   * off soon enough, but built now while the input and view are live so the
   * deferred recording needs neither. The `disable` method never advances the
   * "refined"-check words, so only the built event is returned.
   *
   * @param {object} snapshot
   *   The `collectSnapshot()` snapshot.
   * @param {object} engagementData
   *   `{searchMode, visibleResults, viewIsOpen, searchSource}`.
   * @param {object} smartbarData
   *   `{chatId, intent, model}`.
   * @param {?Set<string>} previousSearchWords
   *   The prior session's search words, for the "refined" interaction check.
   * @returns {?{metric: string, eventInfo: object}} The built event.
   */
  static buildRecordedDisableCandidate(
    snapshot,
    engagementData,
    smartbarData,
    previousSearchWords
  ) {
    return this.#buildRecorded(
      "disable",
      snapshot,
      engagementData,
      smartbarData,
      previousSearchWords
    ).built;
  }

  static #buildRecorded(
    method,
    snapshot,
    engagementData,
    smartbarData,
    previousSearchWords
  ) {
    let { internalDetails } = snapshot;
    let searchMode = internalDetails.searchMode ?? engagementData.searchMode;
    let interactionResult = this.getInteractionType(
      method,
      snapshot.startEventInfo,
      internalDetails.searchSource,
      snapshot.searchWords,
      searchMode,
      previousSearchWords
    );
    let built = this.buildEventInfo({
      method,
      action: snapshot.action,
      interaction: interactionResult.interaction,
      numChars: snapshot.numChars,
      numWords: snapshot.numWords,
      provider: internalDetails.provider,
      searchSource: internalDetails.searchSource,
      searchMode,
      selIndex: internalDetails.selIndex,
      visibleResults: engagementData.visibleResults,
      viewIsOpen: engagementData.viewIsOpen,
      selType: internalDetails.selType,
      pickedActionKey: internalDetails.pickedActionKey,
      location: internalDetails.location,
      chatId: smartbarData.chatId,
      intent: smartbarData.intent,
      model: smartbarData.model,
      windowMode: internalDetails.windowMode,
    });
    return {
      built,
      previousSearchWords: interactionResult.previousSearchWords,
    };
  }
}
