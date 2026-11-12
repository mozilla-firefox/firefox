/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

import { UrlbarTelemetryUtils } from "chrome://browser/content/urlbar/UrlbarTelemetryUtils.mjs";

/**
 * @import {UrlbarChildController} from "chrome://browser/content/urlbar/UrlbarChildController.mjs"
 */

/**
 * Content-side search session collector for the urlbar actor's message path.
 * It owns the synchronous session state (`start()` info) and, on `record()`,
 * assembles the engagement snapshot plus the input/view content the recording
 * needs and ships it to the parent recorder as a `RecordEngagement` message.
 * The parent does the actual `Glean`/`Interactions` recording, so this
 * collector pulls in neither.
 *
 * On the direct path this collector isn't used: `UrlbarChildController` hands
 * back the real parent-side `UrlbarSession` and recording happens in-process.
 * It mirrors that class's `start()`/`record()`/`discard()` surface, sharing the
 * pure parts through `UrlbarTelemetryUtils`.
 */
export class UrlbarChildSession {
  /** @type {UrlbarChildController} */
  #controller;

  // Guards against re-entering record(); see the parent recorder's record().
  #handlingRecord = false;

  /**
   * The in-progress session's start info (`{timeStamp, interactionType,
   * searchString}`), or null when no session is being measured.
   *
   * @type {?object}
   */
  #startEventInfo = null;

  // The previous recorded session's search words, for the "refined" interaction
  // check. Owned here since the child builds the event (the parent recorder
  // keeps its own copy for the direct path).
  #previousSearchWords = null;

  // The session's exposures, mirroring the parent recorder's queues: results
  // exposed during the session, the deferred (hidden) ones, and a set to record
  // at most one exposure per result. Resolved and shipped at session end.
  #exposures = [];
  #tentativeExposures = [];
  #exposureResults = new WeakSet();

  /**
   * @param {UrlbarChildController} controller
   *   The paired child controller, used to reach the live input/view and to
   *   ship the engagement to the parent.
   */
  constructor(controller) {
    this.#controller = controller;
  }

  /**
   * Begins measuring a session from a user-generated event. See the parent
   * recorder's `start()`; this keeps the same once-per-session semantics.
   *
   * @param {Event} event
   *   The event that started the session.
   * @param {object} queryContext
   *   The query context, cached for the session when no query has run.
   * @param {string} [searchString]
   *   The search string related to the event, if any.
   * @param {string} [interactionType]
   *   An explicit interaction type, used in preference to the derived one.
   */
  start(event, queryContext, searchString = null, interactionType = null) {
    if (this.#startEventInfo) {
      if (this.#startEventInfo.interactionType == "topsites") {
        this.#startEventInfo.interactionType =
          interactionType ||
          UrlbarTelemetryUtils.startInteractionType(event, searchString);
        this.#startEventInfo.searchString = searchString;
      } else if (
        this.#startEventInfo.interactionType == "returned" &&
        (!searchString ||
          this.#startEventInfo.searchString[0] != searchString[0])
      ) {
        this.#startEventInfo.interactionType = "restarted";
      }
      return;
    }

    if (!event) {
      console.error("Must always provide an event");
      return;
    }
    const validEvents = [
      "click",
      "command",
      "drop",
      "input",
      "keydown",
      "mousedown",
      "paste",
      "tabswitch",
      "focus",
    ];
    if (this.#controller.input.sapName === "smartbar") {
      // The smartbar CTA can start a session when the input is not focused.
      validEvents.push("aiwindow-input-cta:on-action");
    }
    if (!validEvents.includes(event.type)) {
      console.error("Can't start recording from event type: ", event.type);
      return;
    }

    this.#startEventInfo = {
      timeStamp: event.timeStamp,
      interactionType:
        interactionType ||
        UrlbarTelemetryUtils.startInteractionType(event, searchString),
      searchString,
    };

    // Engagements that run no query would otherwise reach the provider
    // notifications with no context at all.
    if (!this.#controller.parentController._lastQueryContextWrapper) {
      this.#controller.parentController.setLastQueryContextCache(queryContext);
    }
  }

  /**
   * Builds the engagement/abandonment Glean event from the live input and view
   * and ships it to the parent recorder, which fills the parent-only fields and
   * makes the `Glean` call. The picked result rides along in wire form for the
   * provider notifications the parent runs; the disable-tracking candidate and
   * the exposures are built and resolved here and shipped in recordable form.
   *
   * @param {?Event} event
   *   The DOM event behind the engagement, or null for paste&go / drop&go.
   * @param {object} details
   *   The interaction details.
   */
  record(event, details) {
    if (this.#handlingRecord) {
      return;
    }

    // This should never throw, or it may break the urlbar.
    try {
      this.#handlingRecord = true;
      let snapshot = UrlbarTelemetryUtils.collectSnapshot(
        event,
        details,
        this.#startEventInfo
      );
      if (snapshot) {
        let { input, view } = this.#controller;
        let engagementData = UrlbarTelemetryUtils.engagementData(input, view);
        let smartbarData = UrlbarTelemetryUtils.smartbarData(input);

        let { built, previousSearchWords } =
          UrlbarTelemetryUtils.buildRecordedEngagement(
            snapshot,
            engagementData,
            smartbarData,
            this.#previousSearchWords
          );
        this.#previousSearchWords = previousSearchWords;

        // If a Suggest result was shown, build the disable-tracking candidate
        // now too, so the parent can record it should the user turn Suggest off
        // shortly after, without needing the live input at that point.
        let disableBuilt = engagementData.visibleResults.some(
          r => r.providerName == "UrlbarProviderQuickSuggest"
        )
          ? UrlbarTelemetryUtils.buildRecordedDisableCandidate(
              snapshot,
              engagementData,
              smartbarData,
              this.#previousSearchWords
            )
          : null;

        // Exposures are recorded only when the session ends, alongside the
        // engagement, so resolve and ship them with it.
        let exposures = details.isSessionOngoing
          ? null
          : this.#resolveExposures(
              view.queryContext,
              engagementData.visibleResults
            );

        this.#controller.parentController.recordEngagement(
          UrlbarTelemetryUtils.recordedEngagementToWire({
            built,
            disableBuilt,
            method: snapshot.method,
            searchSource: snapshot.internalDetails.searchSource,
            internalDetails: snapshot.internalDetails,
            exposures,
            visibleResults: engagementData.visibleResults,
          })
        );
      }
    } catch (ex) {
      console.error("Could not record event: ", ex);
    } finally {
      this.#handlingRecord = false;
      if (!details.isSessionOngoing) {
        this.#startEventInfo = null;
      }
    }
  }

  /**
   * Drops the in-progress session so it won't be recorded.
   */
  discard() {
    this.#startEventInfo = null;
  }

  /**
   * Resets cross-session recording state: the local "refined"-check words, and
   * (over the wire) the parent recorder's own copy.
   */
  reset() {
    this.#previousSearchWords = null;
    this.#controller.parentController.resetEngagement();
  }

  /**
   * Queues an exposure for a result, if it records exposure telemetry.
   *
   * @param {object} result The exposed result.
   * @param {object} queryContext The query the result belongs to.
   */
  addExposure(result, queryContext) {
    if (result.exposureTelemetry) {
      this.#addExposureInternal(result, queryContext);
    }
  }

  /**
   * Queues a tentative (hidden) exposure, to be confirmed or discarded later.
   *
   * @param {object} result The exposed result.
   * @param {object} queryContext The query the result belongs to.
   */
  addTentativeExposure(result, queryContext) {
    if (result.exposureTelemetry) {
      this.#tentativeExposures.push({ result, queryContext });
    }
  }

  /**
   * Promotes the queued tentative exposures to real exposures.
   */
  acceptTentativeExposures() {
    for (let { result, queryContext } of this.#tentativeExposures) {
      this.#addExposureInternal(result, queryContext);
    }
    this.#tentativeExposures = [];
  }

  /**
   * Drops the queued tentative exposures.
   */
  discardTentativeExposures() {
    this.#tentativeExposures = [];
  }

  #addExposureInternal(result, queryContext) {
    // Record at most one exposure per result, like the parent recorder.
    if (!this.#exposureResults.has(result)) {
      this.#exposureResults.add(result);
      let { resultType, keyword } = UrlbarTelemetryUtils.exposureEntry(
        result,
        queryContext
      );
      this.#exposures.push({ result, resultType, keyword });
    }
  }

  /**
   * Resolves the queued exposures to their recordable form (computing the
   * terminal flag against the live results) and clears the queues.
   *
   * @param {?UrlbarQueryContext} queryContext
   *   The terminal query's context, for hidden exposures' terminal flag.
   * @param {object[]} visibleResults
   *   The results shown at session end.
   * @returns {Array<{resultType: string, keyword: ?string, terminal: boolean}>}
   */
  #resolveExposures(queryContext, visibleResults) {
    let exposures = this.#exposures;
    this.#exposures = [];
    this.#tentativeExposures = [];
    return exposures.map(({ result, resultType, keyword }) => {
      this.#exposureResults.delete(result);
      return {
        resultType,
        keyword,
        terminal: UrlbarTelemetryUtils.exposureTerminal(
          result,
          queryContext,
          visibleResults
        ),
      };
    });
  }

  /**
   * Starts tracking a potential bounce after an engagement, building the Glean
   * event content-side and handing it to the parent, which owns the tracking
   * and the recording.
   *
   * @param {?number} browserId
   *   The stable browser id of the tab the engagement happened in, or null when
   *   the input has no chrome window to read one from.
   * @param {Event} event The DOM event behind the engagement.
   * @param {object} details The interaction details.
   */
  async startTrackingBounceEvent(browserId, event, details) {
    let { input, view } = this.#controller;
    let engagementData = UrlbarTelemetryUtils.engagementData(input, view);
    let smartbarData = UrlbarTelemetryUtils.smartbarData(input);

    let snapshot = UrlbarTelemetryUtils.collectBounceSnapshot(
      event,
      details,
      this.#startEventInfo,
      engagementData.visibleResults
    );
    if (!snapshot) {
      return;
    }

    // Build the Glean event now, while the input and view are live; `view_time`
    // is filled parent-side once `Interactions` reports it at trigger time.
    let { searchSource } = snapshot;
    let searchMode = snapshot.searchMode ?? engagementData.searchMode;
    let { interaction } = UrlbarTelemetryUtils.getInteractionType(
      "bounce",
      snapshot.startEventInfo,
      searchSource,
      snapshot.searchWords,
      searchMode,
      this.#previousSearchWords
    );
    let built = UrlbarTelemetryUtils.buildEventInfo({
      method: "bounce",
      action: snapshot.action,
      interaction,
      numChars: snapshot.numChars,
      numWords: snapshot.numWords,
      provider: snapshot.provider,
      searchSource,
      searchMode,
      selIndex: snapshot.selIndex,
      visibleResults: snapshot.visibleResults,
      viewIsOpen: engagementData.viewIsOpen,
      selType: snapshot.selType,
      location: snapshot.location,
      chatId: smartbarData.chatId,
      intent: smartbarData.intent,
      model: smartbarData.model,
      windowMode: snapshot.windowMode,
    });

    this.#controller.parentController.startTrackingBuiltBounce({
      built,
      searchSource,
      browserId,
    });
  }
}
