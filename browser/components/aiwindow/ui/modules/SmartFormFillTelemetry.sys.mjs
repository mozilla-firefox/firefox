/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/. */

/** @typedef {import("moz-src:///browser/components/aiwindow/models/SmartFormFillModel.sys.mjs").ClassificationResponse} ClassificationResponse */
/** @typedef {import("moz-src:///browser/components/aiwindow/models/SmartFormFillModel.sys.mjs").ClassifyFieldsRequestBody} ClassifyFieldsRequestBody */
/** @typedef {import("moz-src:///browser/components/aiwindow/models/SmartFormFillModel.sys.mjs").GenerateFormValuesRequestBody} GenerateFormValuesRequestBody */
/** @typedef {import("moz-src:///browser/components/aiwindow/models/SmartFormFillModel.sys.mjs").GenerateFormValuesResponse} GenerateFormValuesResponse */
/** @typedef {import("moz-src:///browser/components/aiwindow/models/SmartFormFillModel.sys.mjs").RelevantTabRequestBody} RelevantTabRequestBody */
/** @typedef {import("moz-src:///browser/components/aiwindow/models/SmartFormFillModel.sys.mjs").RelevantTabsResponse} RelevantTabsResponse */
/** @typedef {import("moz-src:///browser/components/aiwindow/models/SmartFormFillModel.sys.mjs").FieldClassification} FieldClassification */
/** @typedef {import("moz-src:///browser/components/aiwindow/models/SmartFormFillModel.sys.mjs").FieldData} FieldData */
/** @typedef {import("moz-src:///browser/components/aiwindow/ui/modules/SmartFormFillController.sys.mjs").SelectedTab} SelectedTab */

/**
 * A request in flight, returned when it is dispatched so its response can be
 * recorded against the same flow. The flow id is captured here because the
 * parent may have started a new flow for the form by the time the response
 * lands.
 *
 * @typedef {{
 *   flowId: string,
 *   startTime: number,
 * }} RequestFlow
 */

/**
 * The model and prompt a request was built with, as reported by
 * SmartFormFillModel. Both can change without a Firefox update, which is why
 * they are recorded per request instead of assumed constant.
 *
 * @typedef {{
 *   model: string,
 *   promptVersion: string,
 * }} ModelInfo
 */

/**
 * What a generation request was dispatched with, beyond the request body.
 *
 * @typedef {{
 *   valuesByToken: Map<string, string>,
 *   modelInfo: ModelInfo,
 *   threshold: string,
 *   similarityByMemory: Map<string, number>,
 * }} GenerateDispatch
 */

/**
 * A completed generation round for one form: what was sent and what the model
 * answered.
 *
 * @typedef {{
 *   fields: Array<FieldData>,
 *   formFields: Array<FieldData>,
 *   classifications: Map<string, FieldClassification>,
 *   tokensByFieldId: Map<string, string>,
 *   values: GenerateFormValuesResponse,
 * }} FieldGeneration
 */

/**
 * What the model decided for one field, resolved as soon as the round ends so
 * the response itself does not have to be kept alive.
 *
 * @typedef {{
 *   fieldId: string,
 *   fieldSeq: number,
 *   inputType: string,
 *   fieldKind: string | undefined,
 *   preLLMFieldKind: string | undefined,
 *   preLLMSource: string | undefined,
 *   preLLMConfidence: number | undefined,
 *   tokenAvailable: boolean,
 *   tokenKind: string | undefined,
 *   source: "token" | "generated" | "none",
 *   confidence: string | undefined,
 * }} FieldDecision
 */

/**
 * How an open of the source editor ended. Cancel is the user backing out,
 * aborted is the client closing the dialog on their behalf, and the two are
 * kept apart because only the first is a decision the user made.
 */
export const SOURCE_EDITOR_RESULT = Object.freeze({
  DONE: "done",
  CANCEL: "cancel",
  ABORTED: "aborted",
});

/** @typedef {typeof SOURCE_EDITOR_RESULT[keyof typeof SOURCE_EDITOR_RESULT]} SourceEditorResult */

/**
 * How a flow used the source editor: how many times it was opened, and how the
 * last of those opens ended.
 *
 * @typedef {{
 *   opens: number,
 *   result: SourceEditorResult | null,
 * }} SourceEditorState
 */

/**
 * What the user's tab selection ended up being, against what the model picked.
 * The editor is absent for a flow that never opened it.
 *
 * @typedef {{
 *   suggestedTabs: Array<SelectedTab>,
 *   finalTabs: Array<SelectedTab>,
 *   editor: SourceEditorState | undefined,
 * }} TabSelectionOutcome
 */

// The reasons a request failure can be attributed to, as the layers below set
// them on the error: PromptLoader for the three config ones, Chat for the rest.
// Shared with the chat's own telemetry on purpose, so a failure both features
// hit is named the same in both.
const REQUEST_ERROR_REASONS = new Set([
  "connectionFailure",
  "fxaTokenUnavailable",
  "modelConfigUnavailable",
  "offline",
  "promptLoadFailure",
  "remoteSettingsUnavailable",
]);

// Reported for a failure outside the list above, including a bug of our own.
// The error type is lost, unlike the client error events, which keep it in a
// separate extra. Worth adding the same if triage ever needs to tell a bug from
// a backend problem.
const GENERIC_REQUEST_ERROR = "genericError";

// The model's fill actions, in the vocabulary the field metric reports. Only
// the actions that fill something are here: select_option and skip write no
// value, so they report no source. A Map rather than an object so an action the
// model made up cannot resolve to an inherited property.
const DECISION_SOURCE_BY_ACTION = new Map([
  ["fill_from_token", "token"],
  ["generate", "generated"],
]);

// What the classification step answers with when it cannot name a field
const UNTYPED_FIELD_KINDS = new Set(["other", "unknown"]);

/**
 * What a generation round produced, beyond the values the page is asked to
 * fill.
 *
 * @typedef {{
 *   formId: string,
 *   fieldsFilled: number,
 *   response: GenerateFormValuesResponse,
 *   fields: Array<FieldData>,
 *   formFields: Array<FieldData>,
 *   classifications: Map<string, FieldClassification>,
 *   tokensByFieldId: Map<string, string>,
 *   similarityByMemory: Map<string, number>,
 * }} GenerationResult
 */

/**
 * How the controller reports the requests it makes. A dispatch callback answers
 * with the flow the request belongs to, which is handed back on the matching
 * answer or failure so the two can be paired. The controller treats it as
 * opaque, so resolving flow ids stays in SmartFormFillTelemetry.
 *
 * @typedef {{
 *   onRelevantTabsDispatched: (formId: string, request: RelevantTabRequestBody, modelInfo: ModelInfo, threshold: string) => RequestFlow,
 *   onRelevantTabsAnswered: (flow: RequestFlow, response: RelevantTabsResponse, tabsUsed: number) => void,
 *   onRelevantTabsFailed: (flow: RequestFlow, error: Error) => void,
 *   onClassifyDispatched: (formId: string, request: ClassifyFieldsRequestBody, modelInfo: ModelInfo) => RequestFlow,
 *   onClassifyAnswered: (flow: RequestFlow, response: ClassificationResponse) => void,
 *   onClassifyFailed: (flow: RequestFlow, error: Error) => void,
 *   onGenerateDispatched: (formId: string, request: GenerateFormValuesRequestBody, dispatch: object) => RequestFlow,
 *   onGenerateAnswered: (flow: RequestFlow, result: GenerationResult) => void,
 *   onGenerateFailed: (flow: RequestFlow, error: Error) => void,
 * }} RequestObserver
 */

/**
 * Smart Form Fill Telemetry for one actor: tracks the flow each form is on and
 * sends the glean events of that flow.
 */
export class SmartFormFillTelemetry {
  /**
   * Flow id correlating every event of a form's current round. Minted with the
   * form's metadata and replaced whenever that metadata is invalidated, so one
   * flow is one attempt at filling one form.
   *
   * @type {Map<string, string>}
   */
  #flowIdByFormId = new Map();

  /**
   * The model's decision for each field of the latest generation round, by form
   * ID. Kept after the round ends because the field events are only complete
   * once the page reports what it filled, and the outcome of each field is only
   * known when the fill is torn down.
   *
   * @type {Map<string, {
   *   flowId: string,
   *   decisions: Array<FieldDecision>,
   *   reviewValues: Map<string, string> | null,
   * }>}
   */
  #fieldDecisionsByFormId = new Map();

  /**
   * How many times the source editor was opened in a form's current flow, and
   * how the last of those opens ended.
   *
   * @type {Map<string, SourceEditorState>}
   */
  #sourceEditorByFormId = new Map();

  /**
   * Starts a new flow for a form, superseding the round it was on so its events
   * stop sharing a flow with the ones the next round will record.
   *
   * @param {string} formId
   */
  startFlow(formId) {
    this.#flowIdByFormId.set(formId, crypto.randomUUID());
    this.#fieldDecisionsByFormId.delete(formId);
    this.#sourceEditorByFormId.delete(formId);
  }

  /**
   * @param {string} formId - A form that went away.
   */
  endFlow(formId) {
    this.#flowIdByFormId.delete(formId);
    this.#fieldDecisionsByFormId.delete(formId);
    this.#sourceEditorByFormId.delete(formId);
  }

  clear() {
    this.#flowIdByFormId.clear();
    this.#fieldDecisionsByFormId.clear();
    this.#sourceEditorByFormId.clear();
  }

  /**
   * Gets the flow id correlating the events of a form's current round. Empty
   * when the form is gone, which a request in flight can outlive.
   *
   * @param {string} formId
   *
   * @returns {string}
   */
  #getFlowId(formId) {
    return this.#flowIdByFormId.get(formId) ?? "";
  }

  /**
   * Counts an opening of the source editor in a form's flow, and records how
   * it ended.
   *
   * @param {string} formId
   * @param {Promise<SourceEditorResult>} result How the editor closed.
   */
  async recordSourceEditor(formId, result) {
    let state = this.#sourceEditorByFormId.get(formId);
    if (!state) {
      state = { opens: 0, result: null };
      this.#sourceEditorByFormId.set(formId, state);
    }
    state.opens++;
    state.result = await result;
  }

  /**
   * Records what the tab selection ended up being for a fill that is starting.
   *
   * @param {string} formId
   * @param {Array<SelectedTab>} suggestedTabs The tabs the model suggested
   * @param {Array<SelectedTab>} finalTabs The tabs the fill will run with
   */
  recordTabSelectionOutcome(formId, suggestedTabs, finalTabs) {
    this.sendRelevantTabsOutcomeTelemetry(this.#getFlowId(formId), {
      suggestedTabs,
      finalTabs,
      editor: this.#sourceEditorByFormId.get(formId),
    });
  }

  /**
   * Keeps the values that are presented for review, so what the user
   * submits can be differentiated from what the model generated.
   *
   * @param {string} formId
   * @param {Array<{id: string, value: string}>} fields The fields the review
   * will show
   */
  setReviewValues(formId, fields) {
    const round = this.#fieldDecisionsByFormId.get(formId);
    if (!round) {
      return;
    }

    round.reviewValues = new Map(fields.map(({ id, value }) => [id, value]));
  }

  /**
   * Records what the user did with each generated value, after they
   * submitted the review dialog's fill.
   *
   * @param {string} formId
   * @param {Array<{id: string, value: string}>} fields The reviewed values the
   * dialog was submitted with
   */
  recordFieldReviewOutcomes(formId, fields) {
    const round = this.#fieldDecisionsByFormId.get(formId);
    const reviewValues = round?.reviewValues;
    if (!reviewValues) {
      return;
    }

    round.reviewValues = null;

    this.sendFillFieldReviewOutcomeTelemetry(round.flowId, round.decisions, {
      generated: reviewValues,
      submitted: fields,
    });
  }

  /**
   * Records what the model decided for each field of a form, now that the page
   * has reported which of them it filled.
   *
   * @param {string} formId
   * @param {Array<string>} filledFieldIds
   */
  recordFieldsFilled(formId, filledFieldIds) {
    const round = this.#fieldDecisionsByFormId.get(formId);
    if (!round) {
      return;
    }

    this.sendFillFieldTelemetry(round.flowId, round.decisions, filledFieldIds);
  }

  /**
   * Records what became of the fields a round filled, now that the page has
   * reported the state each one ended in.
   *
   * @param {string} formId
   * @param {Array<{
   *   id: string,
   *   edited: boolean,
   *   isEmpty: boolean,
   *   filledLength: number,
   *   finalLength: number,
   * }>} fields
   */
  recordFieldOutcomes(formId, fields) {
    const round = this.#fieldDecisionsByFormId.get(formId);
    if (!round) {
      return;
    }

    this.sendFillFieldOutcomeTelemetry(round.flowId, round.decisions, fields);
  }

  /**
   * The callbacks the controller reports its requests through. The controller
   * only sees these callbacks, never this object.
   *
   * @type {RequestObserver}
   */
  get requestObserver() {
    return {
      onRelevantTabsDispatched: (formId, request, modelInfo, threshold) =>
        this.startRelevantTabsRequest(
          this.#getFlowId(formId),
          request,
          modelInfo,
          threshold
        ),

      onRelevantTabsAnswered: (flow, response, tabsUsed) =>
        this.sendRelevantTabsResponseTelemetry(flow, response, tabsUsed),

      onRelevantTabsFailed: (flow, error) =>
        this.sendRelevantTabsErrorTelemetry(flow, error),

      onClassifyDispatched: (formId, request, modelInfo) =>
        this.startClassifyRequest(this.#getFlowId(formId), request, modelInfo),

      onClassifyAnswered: (flow, response) =>
        this.sendClassifyResponseTelemetry(flow, response),

      onClassifyFailed: (flow, error) =>
        this.sendClassifyErrorTelemetry(flow, error),

      onGenerateDispatched: (formId, request, dispatch) =>
        this.startGenerateRequest(this.#getFlowId(formId), request, dispatch),

      onGenerateAnswered: (flow, result) => {
        const {
          formId,
          fieldsFilled,
          response,
          fields,
          formFields,
          classifications,
          tokensByFieldId,
          similarityByMemory,
        } = result;

        this.sendGenerateResponseTelemetry(
          flow,
          fieldsFilled,
          response,
          similarityByMemory
        );

        this.#fieldDecisionsByFormId.set(formId, {
          flowId: flow.flowId,
          decisions: this.resolveFieldDecisions({
            fields,
            formFields,
            classifications,
            tokensByFieldId,
            values: response,
          }),
          reviewValues: null,
        });
      },

      onGenerateFailed: (flow, error) =>
        this.sendGenerateErrorTelemetry(flow, error),
    };
  }

  /**
   * @param {RequestFlow} flow
   *
   * @returns {number} Time in ms the request took
   */
  #getLatency(flow) {
    return Math.round(ChromeUtils.now() - flow.startTime);
  }

  /**
   * Resolves a failed request to the code reported for it, which is only ever
   * one of the reasons the layers below attribute a failure to.
   *
   * @param {Error} error
   *
   * @returns {string}
   */
  #getErrorName(error) {
    const reason = error?.clientReason;

    return REQUEST_ERROR_REASONS.has(reason) ? reason : GENERIC_REQUEST_ERROR;
  }

  /**
   * Maps a model action to the source reported for the field. The actions that
   * fill nothing, and anything the model made up, report no source.
   *
   * @param {string | undefined} action
   *
   * @returns {"token" | "generated" | "none"}
   */
  #getDecisionSource(action) {
    return DECISION_SOURCE_BY_ACTION.get(action) ?? "none";
  }

  /**
   * Scales a 0-1 score to the whole percentage the metrics report, so a value
   * the metric type cannot hold does not have to be bucketed away.
   *
   * @param {number} score
   *
   * @returns {number}
   */
  #getPercent(score) {
    return Math.round(score * 100);
  }

  /**
   * Summarises a set of similarity scores.
   *
   * @param {Array<number>} scores
   *
   * @returns {{ min: number, max: number, avg: number } | null} Null when there
   * is nothing to summarise
   */
  #getSimilarityStats(scores) {
    if (!scores.length) {
      return null;
    }

    const total = scores.reduce((sum, score) => sum + score, 0);

    return {
      min: this.#getPercent(Math.min(...scores)),
      max: this.#getPercent(Math.max(...scores)),
      avg: this.#getPercent(total / scores.length),
    };
  }

  /**
   * Counts the fields the model classified with a type
   *
   * @param {Array<FieldClassification>} fields
   *
   * @returns {number}
   */
  #getTypedFieldCount(fields) {
    return fields.filter(
      field => field.type && !UNTYPED_FIELD_KINDS.has(field.type)
    ).length;
  }

  /**
   * Counts the fields the model answered for but declined to name
   *
   * @param {Array<FieldClassification>} fields
   *
   * @returns {number}
   */
  #getUnknownFieldCount(fields) {
    return fields.filter(field => UNTYPED_FIELD_KINDS.has(field.type)).length;
  }

  /**
   * Records a dispatched classification request.
   *
   * @param {string} flowId
   * @param {ClassifyFieldsRequestBody} request
   * @param {ModelInfo} modelInfo
   *
   * @returns {RequestFlow}
   */
  startClassifyRequest(flowId, request, modelInfo) {
    Glean.smartWindow.formFillClassifyRequest.record({
      flow_id: flowId,
      trigger: "autofill_menu", // only available option for v0
      fields_total: request.fields.length,
      model: modelInfo.model,
      prompt_version: modelInfo.promptVersion,
    });

    return { flowId, startTime: ChromeUtils.now() };
  }

  /**
   * Records a successful classification request.
   *
   * @param {RequestFlow} flow
   * @param {ClassificationResponse} response
   */
  sendClassifyResponseTelemetry(flow, response) {
    const fields = response.fields ?? [];

    Glean.smartWindow.formFillClassifyResponse.record({
      flow_id: flow.flowId,
      page_type: response.pageType, // TODO: the model doesn't classify the page yet
      fields_typed: this.#getTypedFieldCount(fields),
      fields_unknown: this.#getUnknownFieldCount(fields),
      latency_ms: this.#getLatency(flow),
      error: false,
    });
  }

  /**
   * Records a failed classification request.
   *
   * @param {RequestFlow} flow
   * @param {Error} error
   */
  sendClassifyErrorTelemetry(flow, error) {
    Glean.smartWindow.formFillClassifyResponse.record({
      flow_id: flow.flowId,
      latency_ms: this.#getLatency(flow),
      error: true,
      error_message: this.#getErrorName(error),
    });
  }

  /**
   * Records a dispatched relevant-tabs request.
   *
   * @param {string} flowId
   * @param {RelevantTabRequestBody} request
   * @param {ModelInfo} modelInfo
   * @param {string} threshold
   *
   * @returns {RequestFlow}
   */
  startRelevantTabsRequest(flowId, request, modelInfo, threshold) {
    Glean.smartWindow.formRelevantTabsRequest.record({
      flow_id: flowId,
      model: modelInfo.model,
      prompt_version: modelInfo.promptVersion,
      tabs_sent: request.tabs.length,
      threshold,
    });

    return { flowId, startTime: ChromeUtils.now() };
  }

  /**
   * Records a successful relevant-tabs request.
   *
   * @param {RequestFlow} flow
   * @param {RelevantTabsResponse} response What the model answered
   * @param {number} tabsUsed How many of those tabs survived validation and
   * became context
   */
  sendRelevantTabsResponseTelemetry(flow, response, tabsUsed) {
    const selectedTabs = response?.selectedTabs ?? [];
    const rated = level =>
      selectedTabs.filter(tab => tab?.relevance === level).length;

    Glean.smartWindow.formRelevantTabsResponse.record({
      flow_id: flow.flowId,
      tabs_selected: response?.selectedTabs?.length,
      tabs_used: tabsUsed,
      tabs_high: rated("high"),
      tabs_medium: rated("medium"),
      tabs_low: rated("low"),
      latency_ms: this.#getLatency(flow),
      error: false,
    });
  }

  /**
   * Records a failed relevant-tabs request.
   *
   * @param {RequestFlow} flow
   * @param {Error} error
   */
  sendRelevantTabsErrorTelemetry(flow, error) {
    // The tab counts are left out rather than sent as 0: no answer came back,
    // so neither is known, and 0 is a count the model can legitimately return.
    Glean.smartWindow.formRelevantTabsResponse.record({
      flow_id: flow.flowId,
      latency_ms: this.#getLatency(flow),
      error: true,
      error_message: this.#getErrorName(error),
    });
  }

  /**
   * Records what the user's tab selection ended up being
   *
   * @param {string} flowId
   * @param {TabSelectionOutcome} outcome
   */
  sendRelevantTabsOutcomeTelemetry(
    flowId,
    { suggestedTabs, finalTabs, editor }
  ) {
    const suggested = new Set(suggestedTabs.map(({ id }) => id));
    const final = new Set(finalTabs.map(({ id }) => id));

    Glean.smartWindow.formRelevantTabsOutcome.record({
      flow_id: flowId,
      tabs_final: final.size,
      tabs_kept: [...suggested].filter(id => final.has(id)).length,
      tabs_removed: [...suggested].filter(id => !final.has(id)).length,
      tabs_added: [...final].filter(id => !suggested.has(id)).length,
      editor_opens: editor?.opens ?? 0,
      editor_result: editor?.result ?? null,
    });
  }

  /**
   * Records a dispatched generation request.
   *
   * @param {string} flowId
   * @param {GenerateFormValuesRequestBody} request
   * @param {GenerateDispatch} dispatch
   *
   * @returns {RequestFlow}
   */
  startGenerateRequest(flowId, request, dispatch) {
    const { valuesByToken, modelInfo, threshold, similarityByMemory } =
      dispatch;
    const similarity = this.#getSimilarityStats([
      ...similarityByMemory.values(),
    ]);

    Glean.smartWindow.formFillGenerateRequest.record({
      flow_id: flowId,
      tokens_available: valuesByToken.size,
      tabs: request.context.relevantTabs.length,
      memories: request.context.memories.length,
      memories_similarity_min: similarity?.min,
      memories_similarity_max: similarity?.max,
      memories_similarity_avg: similarity?.avg,
      model: modelInfo.model,
      prompt_version: modelInfo.promptVersion,
      threshold,
    });

    return { flowId, startTime: ChromeUtils.now() };
  }

  /**
   * Records a successful generation request.
   *
   * @param {RequestFlow} flow
   * @param {number} fieldsFilled
   * @param {GenerateFormValuesResponse} response What the model answered. The
   * memories and tabs it reports are its own claim, not something the client
   * verifies
   * @param {Map<string, number>} similarityByMemory The score of each memory sent as context
   */
  sendGenerateResponseTelemetry(
    flow,
    fieldsFilled,
    response,
    similarityByMemory
  ) {
    const usedScores = (response.memories_used ?? [])
      .map(memoryId => similarityByMemory.get(memoryId))
      .filter(score => score !== undefined);
    const similarity = this.#getSimilarityStats(usedScores);

    Glean.smartWindow.formFillGenerateResponse.record({
      flow_id: flow.flowId,
      fields_filled: fieldsFilled,
      memories_used: response.memories_used?.length,
      memories_used_similarity_min: similarity?.min,
      memories_used_similarity_max: similarity?.max,
      memories_used_similarity_avg: similarity?.avg,
      tabs_used: response.tabs_used?.length,
      batches_total: response.batches?.total,
      batches_failed: response.batches?.failed,
      latency_ms: this.#getLatency(flow),
      error: false,
    });
  }

  /**
   * Records a failed generation request.
   *
   * @param {RequestFlow} flow
   * @param {Error} error
   */
  sendGenerateErrorTelemetry(flow, error) {
    Glean.smartWindow.formFillGenerateResponse.record({
      flow_id: flow.flowId,
      // Zero fields were filled, but the batch counts are left out: they do not
      // survive the error raised for them, so neither is known.
      fields_filled: 0,
      latency_ms: this.#getLatency(flow),
      error: true,
      error_message: this.#getErrorName(error),
    });
  }

  /**
   * Resolves what the model decided for each field sent for generation. Fields
   * the user had already filled are not sent, so they have no decision.
   *
   * @param {FieldGeneration} generation
   *
   * @returns {Array<FieldDecision>}
   */
  resolveFieldDecisions(generation) {
    const { fields, formFields, classifications, tokensByFieldId, values } =
      generation;

    return fields.map(field => {
      const result = values.fields?.find(({ id }) => id === field.id);

      return {
        fieldId: field.id,
        // Sequence within the whole form, not within the fields sent.
        fieldSeq: formFields.findIndex(({ id }) => id === field.id),
        inputType: field.inputType,
        fieldKind: classifications.get(field.id)?.type,
        preLLMFieldKind: field.localGuess,
        preLLMSource: field.localSource,
        preLLMConfidence:
          field.localConfidence === undefined
            ? undefined
            : this.#getPercent(field.localConfidence),
        tokenAvailable: tokensByFieldId.has(field.id),
        tokenKind: tokensByFieldId.get(field.id),
        source: this.#getDecisionSource(result?.action),
        confidence: result?.confidence,
      };
    });
  }

  /**
   * Records the model's decision for each field of a round, along with whether
   * the page ended up filling it.
   *
   * @param {string} flowId
   * @param {Array<FieldDecision>} decisions
   * @param {Array<string>} filledFieldIds
   */
  sendFillFieldTelemetry(flowId, decisions, filledFieldIds) {
    for (const decision of decisions) {
      Glean.smartWindow.formFillField.record({
        flow_id: flowId,
        field_seq: decision.fieldSeq,
        input_type: decision.inputType,
        field_kind: decision.fieldKind,
        pre_llm_field_kind: decision.preLLMFieldKind,
        pre_llm_source: decision.preLLMSource,
        pre_llm_confidence: decision.preLLMConfidence,
        token_available: decision.tokenAvailable,
        token_kind: decision.tokenKind,
        source: decision.source,
        confidence: decision.confidence,
        filled: filledFieldIds.includes(decision.fieldId),
      });
    }
  }

  /**
   * Names what the page saw for a filled field. The page reports state, not
   * vocabulary, so the metric's wording only exists here.
   *
   * @param {{ edited: boolean, isEmpty: boolean }} state
   *
   * @returns {"kept" | "edited" | "cleared"}
   */
  #getFieldOutcome({ edited, isEmpty }) {
    if (!edited) {
      return "kept";
    }

    return isEmpty ? "cleared" : "edited";
  }

  /**
   * Records what became of the fields a round filled, once their fill was torn
   * down by a blur, a submit or a navigation.
   *
   * @param {string} flowId
   * @param {Array<FieldDecision>} decisions The decisions of the round that
   * filled the fields
   * @param {Array<{
   *   id: string,
   *   edited: boolean,
   *   isEmpty: boolean,
   *   filledLength: number,
   *   finalLength: number,
   * }>} fields The state the page reported, one entry per filled field.
   */
  sendFillFieldOutcomeTelemetry(flowId, decisions, fields) {
    for (const field of fields) {
      const decision = decisions.find(({ fieldId }) => fieldId === field.id);
      if (!decision) {
        continue;
      }

      const outcome = this.#getFieldOutcome(field);
      const edited = outcome === "edited";

      Glean.smartWindow.formFillFieldOutcome.record({
        flow_id: flowId,
        field_seq: decision.fieldSeq,
        field_kind: decision.fieldKind,
        source: decision.source,
        confidence: decision.confidence,
        outcome,
        filled_length: edited ? field.filledLength : undefined,
        final_length: edited ? field.finalLength : undefined,
      });
    }
  }

  /**
   * Records what the user did with each generated value in the review dialog,
   * now that they have submitted its fill.
   *
   * @param {string} flowId
   * @param {Array<FieldDecision>} decisions The decisions of the round whose
   * values were reviewed
   * @param {{
   *   generated: Map<string, string>,
   *   submitted: Array<{ id: string, value: string }>,
   * }} values The values the dialog presented, keyed by field id, against the
   * ones it was submitted with.
   */
  sendFillFieldReviewOutcomeTelemetry(
    flowId,
    decisions,
    { generated, submitted }
  ) {
    for (const { id, value } of submitted) {
      const generatedValue = generated.get(id);
      if (generatedValue === undefined) {
        continue;
      }

      const decision = decisions.find(({ fieldId }) => fieldId === id);
      if (!decision) {
        continue;
      }

      const outcome = this.#getFieldOutcome({
        edited: value !== generatedValue,
        isEmpty: value.trim() === "",
      });
      const edited = outcome === "edited";

      Glean.smartWindow.formFillFieldReviewOutcome.record({
        flow_id: flowId,
        field_seq: decision.fieldSeq,
        outcome,
        generated_length: edited ? generatedValue.length : undefined,
        reviewed_length: edited ? value.length : undefined,
      });
    }
  }
}
