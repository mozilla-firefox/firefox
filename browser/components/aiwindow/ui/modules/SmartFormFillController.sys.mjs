/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/. */

const lazy = {};

ChromeUtils.defineESModuleGetters(lazy, {
  AutofillDataTypes: "resource://gre/modules/shared/AutofillDataTypes.sys.mjs",
  clearTimeout: "resource://gre/modules/Timer.sys.mjs",
  MAX_SELECTED_TABS:
    "chrome://browser/content/aiwindow/modules/SmartFormFillConstants.mjs",
  getTabList: "moz-src:///browser/components/aiwindow/models/Tools.sys.mjs",
  FormAutofill: "resource://autofill/FormAutofill.sys.mjs",
  formAutofillStorage: "resource://autofill/FormAutofillStorage.sys.mjs",
  FormAutofillUtils: "resource://gre/modules/shared/FormAutofillUtils.sys.mjs",
  FormHistory: "resource://gre/modules/FormHistory.sys.mjs",
  MemoriesManager:
    "moz-src:///browser/components/aiwindow/models/memories/MemoriesManager.sys.mjs",
  setTimeout: "resource://gre/modules/Timer.sys.mjs",
  SmartFormFillModel:
    "moz-src:///browser/components/aiwindow/models/SmartFormFillModel.sys.mjs",
});

/** @typedef {import("moz-src:///browser/components/aiwindow/models/Tools.sys.mjs").TabInfo} TabInfo */
/** @typedef {import("moz-src:///browser/components/aiwindow/models/SmartFormFillModel.sys.mjs").Candidate} Candidate */
/** @typedef {import("moz-src:///browser/components/aiwindow/models/SmartFormFillModel.sys.mjs").ClassificationResponse} ClassificationResponse */
/** @typedef {import("moz-src:///browser/components/aiwindow/models/SmartFormFillModel.sys.mjs").ClassifyFieldsRequestBody} ClassifyFieldsRequestBody */
/** @typedef {import("moz-src:///browser/components/aiwindow/models/SmartFormFillModel.sys.mjs").FieldData} FieldData */
/** @typedef {import("moz-src:///browser/components/aiwindow/models/SmartFormFillModel.sys.mjs").FieldDataForClassification} FieldDataForClassification */
/** @typedef {import("moz-src:///browser/components/aiwindow/models/SmartFormFillModel.sys.mjs").GenerateFormValuesResponse} GenerateFormValuesResponse */
/** @typedef {import("moz-src:///browser/components/aiwindow/models/SmartFormFillModel.sys.mjs").PageInfo} PageInfo */
/** @typedef {import("moz-src:///browser/components/aiwindow/models/SmartFormFillModel.sys.mjs").RelevantTabsResponse} RelevantTabsResponse */
/** @typedef {import("moz-src:///browser/components/aiwindow/models/SmartFormFillModel.sys.mjs").RelevantTabRequestBody} RelevantTabRequestBody */
/** @typedef {import("moz-src:///browser/components/aiwindow/models/SmartFormFillModel.sys.mjs").TabData} TabData */
/** @typedef {import("moz-src:///browser/components/aiwindow/models/SmartFormFillModel.sys.mjs").RelevantTab} RelevantTab */
/** @typedef {import("moz-src:///browser/components/aiwindow/ui/modules/SmartFormFillDocument.sys.mjs").FormData} FormData */
/** @typedef {import("moz-src:///browser/components/aiwindow/ui/actors/SmartFormFillParent.sys.mjs").RequestObserver} RequestObserver */

/**
 * @typedef {{
 *   id: string,
 *   value: string,
 * }} FillInstruction
 */

/**
 * @typedef {{
 *   id: string,
 *   fields: Array<FillInstruction>,
 * }} FillFormResult
 */

/**
 * @typedef {{
 *   candidates: Array<Candidate>,
 *   valuesByToken: Map<string, string>,
 *   tokensByFieldId: Map<string, string>,
 * }} CandidateResult
 */

/**
 * @typedef {{
 *   relevantTabsCompleted: boolean,
 *   classificationsCompleted: boolean,
 * }} InitializationResult
 */

// Confidence levels the model can answer with, ordered so a threshold can be
// read as "at least this level". A Map rather than an object so a value the
// model made up cannot resolve to an inherited property: anything that is not
// one of these ranks below all of them, so it is never filled.
const CONFIDENCE_RANK = new Map([
  ["low", 0],
  ["medium", 1],
  ["high", 2],
]);

// The lowest confidence a model's value must reach to be filled. Reported as
// the threshold of the generation request, so both have to move together.
const FILL_CONFIDENCE_THRESHOLD = "medium";

// the lowest confidence allowed for tab relevance
const TAB_RELEVANCE_THRESHOLD = "medium";

/**
 * @typedef {{
 *   id: string,
 * }} SelectedTab
 */

// TODO: Adjust this based on evals for optimal amount
const MAX_TABS = 30;

const REQUEST_RETRY_BASE_DELAY_MS = 1000;
const REQUEST_RETRY_JITTER_MS = 250;
const MAX_REQUEST_RETRIES = 3;

/**
 * Smart Form Fill controller, orchestrates logic for SFF
 */
export class SmartFormFillController {
  /**
   * The relevant page info for the form
   *
   * @type {PageInfo}
   */
  #pageInfo;

  /**
   * Serializable form data by stable form ID.
   *
   * @type {Map<string, FormData> | null}
   */
  #formDataById;

  /**
   * Model-facing tab data.
   *
   * @type {Array<TabData> | null | undefined}
   */
  #tabList;

  /**
   * Tab data for model calls by stable ID.
   *
   * @type {Map<string, TabData> | null}
   */
  #tabsById;

  /**
   * Counter for stable tab IDs.
   *
   * @type {number}
   */
  #tabCounter;

  /**
   * Relevant-tab request controllers by form ID.
   *
   * @type {Map<string, AbortController> | null}
   */
  #abortRelevantTabsControllers;

  /**
   * Classification request controllers by form ID.
   *
   * @type {Map<string, AbortController> | null}
   */
  #abortClassificationControllers;

  /**
   * Value-generation request controllers by form ID.
   *
   * @type {Map<string, AbortController> | null}
   */
  #abortValueGenerationControllers;

  /**
   * Relevant-tab responses by form ID.
   *
   * @type {Map<string, RelevantTabsResponse> | null}
   */
  #relevantTabsByFormId;

  /**
   * Classification responses by form ID.
   *
   * @type {Map<string, ClassificationResponse> | null}
   */
  #classifiedFieldsByFormId;

  /**
   * Tells the parent what the model was asked and what it answered. The token a
   * dispatch callback returns is opaque here: it is handed back on the matching
   * answer or failure so the parent can pair them.
   *
   * @type {RequestObserver}
   */
  #requestObserver;

  /**
   * Creates a controller for Smart Form Fill
   *
   * @param {PageInfo} pageInfo The page info for the tab
   * @param {RequestObserver} observer Notified of the requests this controller
   * dispatches
   */
  constructor(pageInfo, observer) {
    this.#pageInfo = pageInfo;
    this.#formDataById = new Map();
    this.#requestObserver = observer;
    this.#tabCounter = 0;
    this.#tabsById = new Map();
    this.#relevantTabsByFormId = new Map();
    this.#classifiedFieldsByFormId = new Map();

    this.#abortRelevantTabsControllers = new Map();
    this.#abortClassificationControllers = new Map();
    this.#abortValueGenerationControllers = new Map();
  }

  /**
   * Gets list of relevant tabs as selected by LLM
   *
   * @param {string} formId
   *
   * @returns {Array<RelevantTab>}
   */
  getRelevantTabsFor(formId) {
    return this.#relevantTabsByFormId.get(formId)?.selectedTabs ?? [];
  }

  /**
   * Whether an eligible context tab exists in addition to the form tab.
   *
   * @type {boolean}
   */
  get hasSourceTabs() {
    return (this.#tabList?.length ?? 0) > 1;
  }

  /**
   * Gets TabData for a stable tab ID
   *
   * @param {string} tabId
   *
   * @returns {TabData | undefined}
   */
  getTabData(tabId) {
    return this.#tabsById.get(tabId);
  }

  /**
   * Caches serialized data for a form without starting model work.
   *
   * @param {FormData} formData Form data to cache.
   */
  #setFormData(formData) {
    this.#formDataById.set(formData.id, formData);
  }

  /**
   * Requests and caches relevant tabs for one form.
   *
   * @param {FormData} formData Form for which tabs should be selected.
   * @returns {Promise<RelevantTabsResponse>}
   */
  async findRelevantTabs(formData) {
    this.#setFormData(formData);
    this.#ensureTabData();

    this.#abortRelevantTabsControllers.get(formData.id)?.abort();

    const abortController = new AbortController();
    this.#abortRelevantTabsControllers.set(formData.id, abortController);

    try {
      return await this.#requestWithRetries(
        () => this.#findRelevantTabsForForm(formData, abortController.signal),
        abortController.signal
      );
    } finally {
      this.#removeAbortController(
        this.#abortRelevantTabsControllers,
        formData.id,
        abortController
      );
    }
  }

  /**
   * Requests and caches field classifications for one form.
   *
   * @param {FormData} formData Form whose fields should be classified.
   * @returns {Promise<ClassificationResponse>}
   */
  async classifyFields(formData) {
    this.#setFormData(formData);

    this.#abortClassificationControllers.get(formData.id)?.abort();

    const abortController = new AbortController();
    this.#abortClassificationControllers.set(formData.id, abortController);

    try {
      return await this.#requestWithRetries(
        () => this.#classifyFormFields(formData, abortController.signal),
        abortController.signal
      );
    } finally {
      this.#removeAbortController(
        this.#abortClassificationControllers,
        formData.id,
        abortController
      );
    }
  }

  /**
   * Invalidates relevant-tab metadata for one form.
   *
   * @param {string} formId Form whose metadata should be invalidated.
   */
  #invalidateRelevantTabs(formId) {
    this.#abortRelevantTabsControllers.get(formId)?.abort();
    this.#abortRelevantTabsControllers.delete(formId);
    this.#relevantTabsByFormId.delete(formId);
  }

  /**
   * Invalidates all model metadata for one form.
   *
   * @param {string} formId Form whose metadata should be invalidated.
   */
  invalidateForm(formId) {
    this.#invalidateRelevantTabs(formId);
    this.#abortClassificationControllers.get(formId)?.abort();
    this.#abortClassificationControllers.delete(formId);
    this.#classifiedFieldsByFormId.delete(formId);
    this.#formDataById.delete(formId);
  }

  /**
   * Invalidates metadata derived from the current open-tab collection.
   */
  invalidateTabs() {
    this.#abortRequests(this.#abortRelevantTabsControllers);
    this.#relevantTabsByFormId.clear();
    this.#tabList = null;
  }

  /**
   * Gets the current open-tab data.
   *
   * @returns {Array<TabData>}
   */
  getTabs() {
    return this.#tabList?.map(tab => ({ ...tab })) ?? [];
  }

  /**
   * Cancels value generation for a form.
   *
   * @param {string} formId The stable form ID.
   *
   * @returns {void}
   */
  cancelAutofill(formId) {
    this.#abortValueGenerationControllers.get(formId)?.abort();
  }

  /**
   * Generates values for a form.
   *
   * @param {string} formId
   * @param {Set<string>} emptyFieldIds
   * @param {Array<SelectedTab>} selectedTabs
   * @param {Map<string, string>} tabContentById
   * @param {string} pageText
   *
   * @returns {Promise<FillFormResult | null>}
   */
  async autofill(
    formId,
    emptyFieldIds,
    selectedTabs,
    tabContentById,
    pageText
  ) {
    const formData = this.#formDataById.get(formId);
    if (!formData) {
      return null;
    }

    const emptyFields = formData.fields.filter(field =>
      emptyFieldIds.has(field.id)
    );
    if (!emptyFields.length) {
      return null;
    }

    return this.#generateFormValues(
      formId,
      emptyFields,
      formData.fields,
      selectedTabs,
      tabContentById,
      pageText
    );
  }

  /**
   * Generates values for one form.
   *
   * @param {string} id
   * @param {Array<FieldData>} fields The fields to fill
   * @param {Array<FieldData>} formFields Every field of the form, as it was
   * when the fields to fill were picked. Read here rather than when the
   * response lands so a form update cannot renumber the fields that were sent
   * @param {Array<SelectedTab>} selectedTabs
   * @param {Map<string, string>} tabContentById
   * @param {string} pageText
   *
   * @returns {Promise<FillFormResult>}
   */
  async #generateFormValues(
    id,
    fields,
    formFields,
    selectedTabs,
    tabContentById,
    pageText
  ) {
    this.#abortValueGenerationControllers.get(id)?.abort();

    const abortCtrl = new AbortController();
    this.#abortValueGenerationControllers.set(id, abortCtrl);

    const classifications = new Map(
      (this.#classifiedFieldsByFormId.get(id)?.fields ?? []).map(result => [
        result.id,
        result,
      ])
    );

    const task = "generate";
    const page = this.#pageInfo;

    // NOTE: These are disabled for v0, will enable in later version
    //const relevantMemories = await this.#getMemories(page, fields);
    const relevantMemories = [];

    const memories = relevantMemories.map(memory => ({
      id: memory.id,
      memory_summary: memory.memory_summary,
    }));
    const similarityByMemory = new Map(
      relevantMemories.map(memory => [memory.id, memory.similarity])
    );

    const relevantTabs = selectedTabs.map(selectedTab => {
      const { title, url } = this.#tabsById.get(selectedTab.id);

      return {
        title,
        url,
        tabContent: tabContentById.get(selectedTab.id) ?? "",
      };
    });

    const context = { pageText, relevantTabs, memories };

    // Null until the request is dispatched: a failure while gathering the
    // candidates means no request was sent, so it gets no response event.
    let flow = null;

    try {
      const { candidates, valuesByToken, tokensByFieldId } =
        await this.#getCandidates(fields);
      abortCtrl.signal.throwIfAborted();

      const request = {
        task,
        page,
        fields: this.#getFieldDataForClassification(fields).map(field => {
          const classification = classifications.get(field.id);
          const { localGuess, localConfidence, ...fieldData } = field;
          let classificationConfidence = "low";
          if (localConfidence > 0.6) {
            classificationConfidence = "high";
          } else if (localConfidence > 0.3) {
            classificationConfidence = "medium";
          }

          return {
            ...fieldData,
            type: classification?.type ?? localGuess ?? "unknown",
            classificationConfidence:
              classification?.confidence ?? classificationConfidence,
          };
        }),
        candidates,
        context,
      };

      const values = await lazy.SmartFormFillModel.generateFormValues(request, {
        signal: abortCtrl.signal,
        // Every batch that sends reports, so only the first one opens the
        // round: it is one round no matter how many batches it took.
        onDispatch: modelInfo => {
          flow ??= this.#requestObserver.onGenerateDispatched(id, request, {
            valuesByToken,
            modelInfo,
            threshold: FILL_CONFIDENCE_THRESHOLD,
            similarityByMemory,
          });
        },
      });
      abortCtrl.signal.throwIfAborted();

      const fillInstructions = this.#getFillInstructions(
        values,
        fields,
        valuesByToken
      );

      this.#requestObserver.onGenerateAnswered(flow, {
        formId: id,
        fieldsFilled: fillInstructions.length,
        response: values,
        fields,
        formFields,
        classifications,
        tokensByFieldId,
        similarityByMemory,
      });

      return {
        id,
        fields: fillInstructions,
      };
    } catch (error) {
      if (flow && !abortCtrl.signal.aborted) {
        this.#requestObserver.onGenerateFailed(flow, error);
      }
      throw error;
    } finally {
      this.#removeAbortController(
        this.#abortValueGenerationControllers,
        id,
        abortCtrl
      );
    }
  }

  /**
   * Gets relevant memories for a set of fields on a page
   *
   * @param {PageInfo} pageInfo
   * @param {Array<FieldData>} fields
   *
   * @returns {Promise<Array<{
   *   id: string,
   *   memory_summary: string,
   *   similarity: number,
   * }>>}
   */
  // eslint-disable-next-line no-unused-private-class-members -- will be enabled in v0+
  async #getMemories(pageInfo, fields) {
    const hostname = URL.parse(pageInfo.url)?.hostname ?? "";
    const site = hostname ? `Site: ${hostname}` : "";
    const contextMessage = [
      `The user is completing a form on "${pageInfo.title}".`,
      site,
      "The form requests:",
      ...fields.map(field =>
        [
          field.label,
          field.inputType,
          field.placeholder,
          field.textBefore,
          field.textAfter,
        ]
          .filter(Boolean)
          .join(" ")
      ),
    ].join("\n");

    const relevantMemories =
      await lazy.MemoriesManager.getRelevantMemories(contextMessage);

    return relevantMemories.map(relevant_memory => {
      const { id, memory_summary, similarity } = relevant_memory;

      return { id, memory_summary, similarity };
    });
  }

  /**
   * Resolves model results into fill instructions.
   *
   * @param {GenerateFormValuesResponse} values
   * @param {Array<FieldData>} fields
   * @param {Map<string, string>} valuesByToken
   *
   * @returns {Array<FillInstruction>}
   */
  #getFillInstructions(values, fields, valuesByToken) {
    const fieldIds = new Set(fields.map(field => field.id));
    const resolvedFieldIds = new Set();
    const fillInstructions = [];

    for (const result of values.fields) {
      if (
        (CONFIDENCE_RANK.get(result.confidence) ?? -1) <
          CONFIDENCE_RANK.get(FILL_CONFIDENCE_THRESHOLD) ||
        !fieldIds.has(result.id) ||
        resolvedFieldIds.has(result.id)
      ) {
        continue;
      }

      let value;
      switch (result.action) {
        case "fill_from_token":
          value = result.value ? valuesByToken.get(result.value) : undefined;
          break;

        case "generate":
          value = result.value;
          break;

        default:
          continue;
      }

      if (!value || typeof value !== "string") {
        continue;
      }

      fillInstructions.push({ id: result.id, value });
      resolvedFieldIds.add(result.id);
    }

    return fillInstructions;
  }

  /**
   * Builds stored-value candidates for fields.
   *
   * @param {Array<FieldData>} fields
   * @returns {Promise<CandidateResult>}
   */
  async #getCandidates(fields) {
    const candidates = [];
    const valuesByToken = new Map();
    const tokensByFieldId = new Map();
    const typeCounts = new Map();
    // Read once, and only for a form that has a field to spend it on.
    let savedAddresses;

    for (const field of fields) {
      const type = field.localGuess;
      if (!type) {
        continue;
      }

      let value;
      if (lazy.FormAutofillUtils.isAddressField(type)) {
        savedAddresses ??= await this.#getSavedAddresses();
        value = savedAddresses.find(address => address[type])?.[type];
      }
      value ??= await this.#getFormHistoryValue(field);

      if (!value) {
        continue;
      }

      const count = (typeCounts.get(type) ?? 0) + 1;
      typeCounts.set(type, count);

      const token = `§${type.toUpperCase().replaceAll("-", "_")}_${count}§`;

      candidates.push({ token, type });
      valuesByToken.set(token, value);
      tokensByFieldId.set(field.id, type);
    }

    return { candidates, valuesByToken, tokensByFieldId };
  }

  /**
   * Gets the saved addresses autofill would offer, most recently used first.
   * The local guess a field carries is the canonical autofill field name, so
   * it is also the key these records hold their values under.
   * Only addresses: a saved card's `cc-number` comes back masked and the real
   * one needs an OSKeyStore decrypt, which prompts the user to reauthenticate.
   *
   * @returns {Promise<Array<object>>}
   */
  async #getSavedAddresses() {
    if (
      !lazy.FormAutofill.isAutofillTypeEnabled(lazy.AutofillDataTypes.ADDRESS)
    ) {
      return [];
    }

    try {
      await lazy.formAutofillStorage.initialize();
      const addresses = await lazy.formAutofillStorage.addresses.getAll();

      return addresses.sort(
        (a, b) => (b.timeLastUsed ?? 0) - (a.timeLastUsed ?? 0)
      );
    } catch {
      return [];
    }
  }

  /**
   * Gets the latest Form History value for a field.
   *
   * @param {FieldData} field
   * @returns {Promise<string | null>}
   */
  async #getFormHistoryValue(field) {
    if (!field.formHistoryName) {
      return null;
    }

    try {
      const results = await lazy.FormHistory.search(["value", "lastUsed"], {
        fieldname: field.formHistoryName,
      });

      if (!results?.length) {
        return null;
      }

      results.sort((a, b) => (b.lastUsed ?? 0) - (a.lastUsed ?? 0));

      return results[0].value ?? null;
    } catch {
      return null;
    }
  }

  /**
   * Aborts requests and clears their controller maps.
   *
   * @param {...Map<string, AbortController>} controllerMaps
   */
  #abortRequests(...controllerMaps) {
    const controllers = controllerMaps.flatMap(map => [...map.values()]);

    for (const controller of controllers) {
      controller.abort();
    }

    for (const map of controllerMaps) {
      map.clear();
    }
  }

  /**
   * Removes a request controller if it is still current.
   *
   * @param {Map<string, AbortController> | null} controllers
   * @param {string} id
   * @param {AbortController} controller
   */
  #removeAbortController(controllers, id, controller) {
    if (controllers?.get(id) === controller) {
      controllers.delete(id);
    }
  }

  /**
   * Cancels requests and clears controller state.
   */
  destroy() {
    this.#formDataById.clear();
    this.#formDataById = null;
    this.#tabCounter = 0;
    this.#tabList = null;

    this.#tabsById.clear();
    this.#tabsById = null;

    this.#abortRequests(
      this.#abortRelevantTabsControllers,
      this.#abortClassificationControllers,
      this.#abortValueGenerationControllers
    );

    this.#abortRelevantTabsControllers = null;
    this.#abortClassificationControllers = null;
    this.#abortValueGenerationControllers = null;

    this.#relevantTabsByFormId.clear();
    this.#relevantTabsByFormId = null;

    this.#classifiedFieldsByFormId.clear();
    this.#classifiedFieldsByFormId = null;
  }

  /**
   * Initializes model-facing tab data when it is first needed.
   */
  #ensureTabData() {
    if (this.#tabList) {
      return;
    }

    this.#tabCounter = 0;
    this.#tabsById.clear();
    this.#tabList = this.#getTabData(lazy.getTabList(MAX_TABS));
  }

  /**
   * Runs a model request with exponential-backoff retries.
   *
   * @template T
   * @param {() => Promise<T>} request Request operation.
   * @param {AbortSignal} signal Signal used to cancel retries.
   *
   * @returns {Promise<T>} The successful request result.
   */
  async #requestWithRetries(request, signal) {
    for (let retryIndex = 0; retryIndex <= MAX_REQUEST_RETRIES; retryIndex++) {
      try {
        return await request();
      } catch (error) {
        signal.throwIfAborted();

        if (
          !lazy.SmartFormFillModel.isRetryableRequestError(error) ||
          retryIndex === MAX_REQUEST_RETRIES
        ) {
          throw error;
        }

        await this.#waitBeforeRetry(retryIndex, signal);
      }
    }

    throw new Error("Smart Form Fill request did not complete");
  }

  /**
   * Waits before retrying a model request.
   *
   * @param {number} retryIndex Zero-based retry number.
   * @param {AbortSignal} signal Signal used to cancel the delay.
   *
   * @returns {Promise<void>}
   */
  #waitBeforeRetry(retryIndex, signal) {
    const delay =
      REQUEST_RETRY_BASE_DELAY_MS * Math.pow(2, retryIndex) +
      Math.floor(Math.random() * REQUEST_RETRY_JITTER_MS);

    return new Promise((resolve, reject) => {
      const onAbort = () => {
        lazy.clearTimeout(timer);
        reject(signal.reason);
      };

      const timer = lazy.setTimeout(() => {
        signal.removeEventListener("abort", onAbort);
        resolve();
      }, delay);

      signal.addEventListener("abort", onAbort, { once: true });
    });
  }

  /**
   * Returns relevant tabs with valid, unique IDs.
   *
   * @param {Array<RelevantTab>} selectedTabs
   * @returns {Array<RelevantTab>}
   */
  #getValidRelevantTabs(selectedTabs) {
    if (!Array.isArray(selectedTabs)) {
      return [];
    }

    const seen = new Set();

    return selectedTabs
      .filter(tab => {
        const id = tab?.id;
        if (!this.#tabsById.has(id) || seen.has(id)) {
          return false;
        }

        if (
          (CONFIDENCE_RANK.get(tab?.relevance) ?? -1) <
          CONFIDENCE_RANK.get(TAB_RELEVANCE_THRESHOLD)
        ) {
          return false;
        }

        seen.add(id);
        return true;
      })
      .slice(0, lazy.MAX_SELECTED_TABS);
  }

  /**
   * Requests relevant tabs for one form.
   *
   * @param {FormData} formData Form for which tabs should be selected.
   * @param {AbortSignal} signal Signal used to cancel the request.
   *
   * @returns {Promise<RelevantTabsResponse>}
   */
  async #findRelevantTabsForForm({ id, fields }, signal) {
    const request = this.#getRelevantTabRequestBody(fields);

    // Null until the request is dispatched: a failure while resolving the model
    // or the prompt means no request was sent, so it gets no events.
    let flow = null;

    try {
      const relevantTabCandidates =
        await lazy.SmartFormFillModel.findRelevantTabs(request, {
          signal,
          onDispatch: modelInfo => {
            flow = this.#requestObserver.onRelevantTabsDispatched(
              id,
              request,
              modelInfo,
              TAB_RELEVANCE_THRESHOLD
            );
          },
        });

      signal.throwIfAborted();
      const relevantTabs = {
        selectedTabs: this.#getValidRelevantTabs(
          relevantTabCandidates?.selectedTabs
        ),
      };

      this.#relevantTabsByFormId.set(id, relevantTabs);
      this.#requestObserver.onRelevantTabsAnswered(
        flow,
        relevantTabCandidates,
        relevantTabs.selectedTabs.length
      );

      return relevantTabs;
    } catch (error) {
      if (flow && !signal.aborted) {
        this.#requestObserver.onRelevantTabsFailed(flow, error);
      }

      throw error;
    }
  }

  /**
   * Classifies fields for one form.
   *
   * @param {FormData} formData Form whose fields should be classified.
   * @param {AbortSignal} signal Signal used to cancel the request.
   * @returns {Promise<ClassificationResponse>}
   */
  async #classifyFormFields({ id, fields }, signal) {
    const request = this.#getClassifyFieldsRequestBody(fields);

    // Null until the request is dispatched: a failure while resolving the model
    // or the prompt means no request was sent, so it gets no events.
    let flow = null;

    try {
      const classifiedFields = await lazy.SmartFormFillModel.classifyFields(
        request,
        {
          signal,
          onDispatch: modelInfo => {
            flow = this.#requestObserver.onClassifyDispatched(
              id,
              request,
              modelInfo
            );
          },
        }
      );

      signal.throwIfAborted();
      this.#classifiedFieldsByFormId.set(id, classifiedFields);
      this.#requestObserver.onClassifyAnswered(flow, classifiedFields);

      return classifiedFields;
    } catch (error) {
      if (flow && !signal.aborted) {
        this.#requestObserver.onClassifyFailed(flow, error);
      }

      throw error;
    }
  }

  /**
   * Builds model-facing field data for classification and tab selection.
   *
   * @param {Array<FieldData>} fields
   * @returns {Array<FieldDataForClassification>}
   */
  #getFieldDataForClassification(fields) {
    return fields.map(field => ({
      id: field.id,
      label: field.label,
      name: field.name,
      inputType: field.inputType,
      placeholder: field.placeholder,
      autocomplete: field.autocomplete,
      maxlength: field.maxlength,
      options: field.options,
      textBefore: field.textBefore,
      textAfter: field.textAfter,
      localGuess: field.localGuess,
      localConfidence: field.localConfidence,
    }));
  }

  /**
   * Builds a field-classification request.
   *
   * @param {Array<FieldData>} fields
   * @returns {ClassifyFieldsRequestBody}
   */
  #getClassifyFieldsRequestBody(fields) {
    const task = "classify";
    const enumVersion = "sff-fieldtypes-1";
    const page = this.#pageInfo;
    const classificationFields = this.#getFieldDataForClassification(fields);

    return { task, enumVersion, page, fields: classificationFields };
  }

  /**
   * Builds a relevant-tabs request.
   *
   * @param {Array<FieldData>} fields
   * @returns {RelevantTabRequestBody}
   */
  #getRelevantTabRequestBody(fields) {
    const task = "select_tabs";
    const page = this.#pageInfo;
    const tabs = this.#tabList;
    const classificationFields = this.#getFieldDataForClassification(fields);
    const maxSelectedTabs = lazy.MAX_SELECTED_TABS;

    return {
      task,
      page,
      maxSelectedTabs,
      tabs,
      fields: classificationFields,
    };
  }

  /**
   * Converts open tabs to model-facing data.
   *
   * @param {Array<TabInfo>} tabList
   * @returns {Array<TabData>}
   */
  #getTabData(tabList) {
    return tabList.map(tab => {
      const id = `t${++this.#tabCounter}`;
      const { url, title } = tab;

      const tabData = {
        id,
        url,
        title,
      };

      this.#tabsById.set(id, tabData);

      return tabData;
    });
  }
}
