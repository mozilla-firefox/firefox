/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/**
 * This module exports a urlbar result class, each representing a single result
 * found by a provider that can be passed from the model to the view through
 * the controller. It is mainly defined by a result type, and a payload,
 * containing the data. A few getters allow to retrieve information common to all
 * the result types.
 *
 * This module can be imported into system and content realms. For this reason,
 * it is not possible to rely on instanceof checks or global state.
 */

import { UrlbarShared } from "chrome://browser/content/urlbar/UrlbarShared.mjs";

const { XPCOMUtils } =
  typeof ChromeUtils == "undefined"
    ? { XPCOMUtils: null }
    : ChromeUtils.importESModule("resource://gre/modules/XPCOMUtils.sys.mjs");

const lazy = XPCOMUtils?.declareLazy({
  JsonSchemaValidator:
    "resource://gre/modules/components-utils/JsonSchemaValidator.sys.mjs",
  UrlbarUtils: "moz-src:///browser/components/urlbar/UrlbarUtils.sys.mjs",
});

/**
 * @typedef UrlbarAutofillData
 * @property {string} value
 *   The value to insert for autofill.
 * @property {number} selectionStart
 *   Where to start the selection for the autofill.
 * @property {number} selectionEnd
 *   Where to end the selection for the autofill.
 * @property {"origin" | "url" | "adaptive_url" | "adaptive_origin" | "about"} [type]
 *   The type of the autofill.
 * @property {string} [adaptiveHistoryInput]
 *   The input string associated with this autofill item.
 * @property {boolean} [noVisitAction]
 *   Whether the view should not show a "Visit" action.
 */

/**
 * Class used to create a single result.
 */
export class UrlbarResult {
  /**
   * @typedef {{ [name: string]: any }} Payload
   *
   * @typedef {Values<typeof UrlbarShared.HIGHLIGHT>} HighlightType
   * @typedef {Array<[number, number]>} HighlightIndexes e.g. [[index, length],,]
   * @typedef {Record<string, HighlightType | HighlightIndexes>} Highlights
   */

  /**
   * @param {object} params
   * @param {Values<typeof UrlbarShared.RESULT_TYPE>} params.type
   * @param {Values<typeof UrlbarShared.RESULT_SOURCE>} params.source
   * @param {UrlbarAutofillData} [params.autofill]
   * @param {number} [params.exposureTelemetry]
   * @param {Values<typeof UrlbarShared.RESULT_GROUP>} [params.group]
   * @param {boolean} [params.heuristic]
   * @param {boolean} [params.hideRowLabel]
   * @param {boolean} [params.isBestMatch]
   * @param {string} [params.bestMatchType]
   * @param {boolean} [params.isBottomUrlSuggestion]
   * @param {boolean} [params.isRichSuggestion]
   * @param {boolean} [params.isSuggestedIndexRelativeToGroup]
   * @param {string} [params.providerName]
   * @param {number} [params.resultSpan]
   * @param {number} [params.richSuggestionIconSize]
   * @param {string} [params.richSuggestionIconVariation]
   * @param {{id: string, args?: L10nArgs}} [params.rowLabel]
   * @param {boolean} [params.showFeedbackMenu]
   * @param {number} [params.suggestedIndex]
   * @param {Payload} [params.payload]
   * @param {Highlights} [params.highlights]
   * @param {boolean} [params.testForceNewContent] Used for test only.
   * @param {boolean} [params.skipPayloadValidation]
   *   Skips payload schema validation. Set by `UrlbarResult.fromWire` when
   *   reconstructing a result that was already validated before serialization;
   *   the wire payload can carry internal fields added after validation.
   */
  constructor({
    type,
    source,
    autofill,
    exposureTelemetry = UrlbarShared.EXPOSURE_TELEMETRY.NONE,
    group,
    heuristic = false,
    hideRowLabel = false,
    isBestMatch = false,
    bestMatchType = undefined,
    isBottomUrlSuggestion = false,
    isRichSuggestion = false,
    isSuggestedIndexRelativeToGroup = false,
    providerName,
    resultSpan,
    richSuggestionIconSize,
    richSuggestionIconVariation,
    rowLabel,
    showFeedbackMenu = false,
    suggestedIndex,
    payload,
    highlights = null,
    testForceNewContent,
    skipPayloadValidation = false,
  }) {
    // Type describes the payload and visualization that should be used for
    // this result.
    if (!Object.values(UrlbarShared.RESULT_TYPE).includes(type)) {
      throw new Error("Invalid result type");
    }
    this.#type = type;

    // Source describes which data has been used to derive this result. In case
    // multiple sources are involved, use the more privacy restricted.
    if (!Object.values(UrlbarShared.RESULT_SOURCE).includes(source)) {
      throw new Error("Invalid result source");
    }
    this.#source = source;

    // The payload contains result data. Some of the data is common across
    // multiple types, but most of it will vary.
    if (!payload || typeof payload != "object") {
      throw new Error("Invalid result payload");
    }

    payload = Object.fromEntries(
      Object.entries(payload).filter(([_, v]) => v != undefined)
    );

    if (highlights) {
      this.#highlights = Object.freeze(highlights);
    }

    this.#payload = skipPayloadValidation
      ? payload
      : this.#validatePayload(payload);

    this.#autofill = autofill;
    this.#exposureTelemetry = exposureTelemetry;
    this.#group = group;
    this.#heuristic = heuristic;
    this.#hideRowLabel = hideRowLabel;
    this.#isBottomUrlSuggestion = isBottomUrlSuggestion;
    this.#isRichSuggestion = isRichSuggestion;
    this.#isSuggestedIndexRelativeToGroup = isSuggestedIndexRelativeToGroup;
    this.#richSuggestionIconSize = richSuggestionIconSize;
    this.#richSuggestionIconVariation = richSuggestionIconVariation;
    this.#providerName = providerName;
    this.#resultSpan = resultSpan;
    this.#rowLabel = rowLabel;
    this.#showFeedbackMenu = showFeedbackMenu;
    this.#suggestedIndex = suggestedIndex;

    if (this.#type == UrlbarShared.RESULT_TYPE.TIP) {
      this.#isRichSuggestion = true;
      this.#richSuggestionIconSize = 24;
    }

    if (bestMatchType !== undefined) {
      this.#bestMatchType = bestMatchType;
    } else if (isBestMatch) {
      // The default type is `LARGE`.
      this.#bestMatchType = UrlbarShared.BEST_MATCH_TYPE.LARGE;
    } else {
      this.#bestMatchType = UrlbarShared.BEST_MATCH_TYPE.NONE;
    }

    this.#testForceNewContent = testForceNewContent;
  }

  /**
   * The index of the row where this result is in the suggestions. This is
   * updated by UrlbarView when new result sets are displayed.
   *
   * @type {number}
   */
  rowIndex = undefined;

  /**
   * A stable id assigned once when the result is finalized by
   * UrlbarProvidersManager. Unlike rowIndex it never changes and is
   * independent of the results' order, so it matches this result to its
   * context entry and view row across the actor boundary.
   *
   * @type {number}
   */
  id = undefined;

  /**
   * The result menu commands the result's provider offers, computed eagerly
   * by the providers manager when the result is finalized.
   * Undefined if the provider offers none.
   *
   * @type {?UrlbarResultCommand[]|undefined}
   */
  commands = undefined;

  /**
   * Whether the result's URL is a search engine results page. Resolved when the
   * result is finalized, since it takes the search service, which only the
   * parent process has.
   *
   * @type {boolean}
   */
  isSERP = false;

  /**
   * The kind of result, one of `UrlbarShared.RESULT_TYPE`. It decides which
   * payload properties the result has and how the view shows it.
   */
  get type() {
    return this.#type;
  }

  /**
   * The data the result was derived from, one of `UrlbarShared.RESULT_SOURCE`.
   * A result derived from several sources uses the most privacy-restricted
   * one.
   */
  get source() {
    return this.#source;
  }

  /**
   * The text to autofill in the input when the result is selected, if any.
   *
   * @type {UrlbarAutofillData|undefined}
   */
  get autofill() {
    return this.#autofill;
  }

  /**
   * Whether exposure telemetry is recorded for the result, and whether the
   * result is then shown or hidden. One of `UrlbarShared.EXPOSURE_TELEMETRY`.
   *
   * @type {number}
   */
  get exposureTelemetry() {
    return this.#exposureTelemetry;
  }
  set exposureTelemetry(value) {
    this.#exposureTelemetry = value;
  }

  /**
   * The group the muxer places the result in, one of
   * `UrlbarShared.RESULT_GROUP`. Undefined if the muxer picks the group.
   */
  get group() {
    return this.#group;
  }

  /**
   * Whether the result is the heuristic result, the one that is picked when
   * the user presses Enter without selecting a result.
   *
   * @type {boolean}
   */
  get heuristic() {
    return this.#heuristic;
  }

  /**
   * Whether the view hides the group label above the result's row.
   *
   * @type {boolean}
   */
  get hideRowLabel() {
    return this.#hideRowLabel;
  }

  /**
   * Whether the result is a best match, which the view shows as a top pick.
   *
   * @type {boolean}
   */
  get isBestMatch() {
    return this.#bestMatchType != UrlbarShared.BEST_MATCH_TYPE.NONE;
  }

  /**
   * The result's best-match type, a `UrlbarShared.BEST_MATCH_TYPE` value.
   *
   * @type {string}
   */
  get bestMatchType() {
    return this.#bestMatchType;
  }

  /**
   * Whether the view shows the result as a URL row at the bottom of the
   * results.
   *
   * @type {boolean}
   */
  get isBottomUrlSuggestion() {
    return this.#isBottomUrlSuggestion;
  }

  /**
   * Whether the view shows the result as a rich suggestion, with a larger
   * icon and a description. Always true for a tip.
   *
   * @type {boolean}
   */
  get isRichSuggestion() {
    return this.#isRichSuggestion;
  }
  set isRichSuggestion(value) {
    this.#isRichSuggestion = value;
  }

  /**
   * Whether `suggestedIndex` is relative to the result's group instead of
   * the entire result set.
   *
   * @type {boolean}
   */
  get isSuggestedIndexRelativeToGroup() {
    return this.#isSuggestedIndexRelativeToGroup;
  }
  set isSuggestedIndexRelativeToGroup(value) {
    this.#isSuggestedIndexRelativeToGroup = value;
  }

  /**
   * The name of the provider that created the result.
   *
   * @type {string}
   */
  get providerName() {
    return this.#providerName;
  }
  set providerName(value) {
    this.#providerName = value;
  }

  /**
   * The type of the UrlbarProvider providing the result.
   *
   * @type {?Values<typeof UrlbarShared.PROVIDER_TYPE>}
   */
  get providerType() {
    return this.#providerType;
  }
  set providerType(value) {
    this.#providerType = value;
  }

  /**
   * The number of rows the result spans in the view. Undefined to use the
   * default for the result's type, which `UrlbarShared.getSpanForResult`
   * returns.
   *
   * @type {number}
   */
  get resultSpan() {
    return this.#resultSpan;
  }

  /**
   * The size in pixels of the rich suggestion icon. 24 for a tip.
   *
   * @type {number}
   */
  get richSuggestionIconSize() {
    return this.#richSuggestionIconSize;
  }

  /**
   * The variation of the rich suggestion icon, which the view sets as the
   * row's `icon-variation` attribute for the stylesheet to match.
   *
   * @type {string}
   */
  get richSuggestionIconVariation() {
    return this.#richSuggestionIconVariation;
  }
  set richSuggestionIconSize(value) {
    this.#richSuggestionIconSize = value;
  }

  /**
   * The group label to show above the result's row, as a `{ id, args }` l10n
   * object, overriding the label the view picks.
   *
   * @type {object}
   */
  get rowLabel() {
    return this.#rowLabel;
  }

  /**
   * Whether the result's menu button is labeled as a feedback menu.
   *
   * @type {boolean}
   */
  get showFeedbackMenu() {
    return this.#showFeedbackMenu;
  }

  /**
   * A preferred position for the result within the result set, or within its
   * group if `isSuggestedIndexRelativeToGroup` is true. A negative index
   * counts from the end. Undefined if the result has none.
   *
   * @type {number}
   */
  get suggestedIndex() {
    return this.#suggestedIndex;
  }
  set suggestedIndex(value) {
    this.#suggestedIndex = value;
  }

  /**
   * The result's data. Which properties it holds depends on `type`, and
   * `UrlbarUtils.RESULT_PAYLOAD_SCHEMA` describes them.
   */
  get payload() {
    return this.#payload;
  }

  get testForceNewContent() {
    return this.#testForceNewContent;
  }

  // Used only for test.
  get testHighlights() {
    return this.#highlights;
  }

  /**
   * Returns an icon url.
   *
   * @returns {string} url of the icon.
   */
  get icon() {
    return this.payload.icon;
  }

  /**
   * Returns whether the result's `suggestedIndex` property is defined.
   * `suggestedIndex` is an optional hint to the muxer that can be set to
   * suggest a specific position among the results.
   *
   * @returns {boolean} Whether `suggestedIndex` is defined.
   */
  get hasSuggestedIndex() {
    return typeof this.suggestedIndex == "number";
  }

  /**
   * Convenience getter that returns whether the result's exposure telemetry
   * indicates it should be hidden.
   *
   * @returns {boolean}
   *   Whether the result should be hidden.
   */
  get isHiddenExposure() {
    return this.exposureTelemetry == UrlbarShared.EXPOSURE_TELEMETRY.HIDDEN;
  }

  /**
   * Get value and highlights of given payloadName that can display in the view.
   *
   * @param {string} payloadName
   *   The payload name to want to get the value.
   * @param {object} options
   * @param {object} [options.tokens]
   *   Make highlighting that matches this tokens.
   *   If no specific tokens, this function returns only value.
   * @param {object} [options.isURL]
   *   If true, the value will be from UrlbarShared.prepareUrlForDisplay().
   */
  getDisplayableValueAndHighlights(payloadName, options = {}) {
    if (!this.#displayValuesCache) {
      this.#displayValuesCache = new Map();
    }

    if (this.#displayValuesCache.has(payloadName)) {
      let cached = this.#displayValuesCache.get(payloadName);
      // If the different options are specified, ignore the cache.
      // NOTE: If options.tokens is undefined, use cache as it is.
      if (
        options.isURL == cached.options.isURL &&
        (options.tokens == undefined ||
          UrlbarShared.deepEqual(options.tokens, cached.options.tokens))
      ) {
        return this.#displayValuesCache.get(payloadName);
      }
    }

    let highlightType;
    let { isURL } = options;

    let value = this.payload[payloadName];
    if (!value) {
      if (payloadName != "title" || !this.payload.url) {
        return {};
      }

      // The payload doesn't have a title but it does have a URL. A title should
      // always be shown because otherwise the result's row in the view will
      // look a little strange, so show the URL's domain as the title. Not all
      // valid URLs have a domain, so fall back to the full URL.
      highlightType = UrlbarShared.HIGHLIGHT.TYPED;
      try {
        // This will throw if `this.payload.url` isn't a valid URL. If the URL
        // is valid but doesn't have a domain, it won't throw and
        // `displayHostPort` will be an empty string.
        value = new URL(this.payload.url).URI.displayHostPort;
        isURL = !value;
      } catch (e) {
        isURL = false;
      }

      value ||= this.payload.url;
    }

    if (isURL) {
      value = UrlbarShared.prepareUrlForDisplay(value);
    }

    if (typeof value == "string") {
      value = value.substring(0, UrlbarShared.MAX_TEXT_LENGTH);
    }

    if (Array.isArray(this.#highlights?.[payloadName])) {
      return { value, highlights: this.#highlights[payloadName] };
    }

    highlightType ??= this.#highlights?.[payloadName];

    // If we are going to store the options in the map,
    // create a clone to make sure the object is in the
    // same global as the result itself to avoid leaks.
    options = structuredClone(options);

    if (!options.tokens?.length || !highlightType) {
      let cached = { value, options };
      this.#displayValuesCache.set(payloadName, cached);
      return cached;
    }

    let highlights = Array.isArray(value)
      ? value.map(subval =>
          UrlbarShared.getTokenMatches(options.tokens, subval, highlightType)
        )
      : UrlbarShared.getTokenMatches(options.tokens, value, highlightType);

    let cached = { value, highlights, options };
    this.#displayValuesCache.set(payloadName, cached);
    return cached;
  }

  /**
   * Returns the given payload if it's valid or throws an error if it's not.
   * The schemas in UrlbarUtils.RESULT_PAYLOAD_SCHEMA are used for validation.
   *
   * This must only validate the payload, never transform it or add/remove
   * properties: the constructor's skipPayloadValidation option bypasses this
   * method entirely, so any such change would make skipPayloadValidation
   * consumers (e.g. fromWire) diverge from validated results.
   *
   * @param {object} payload The payload object.
   * @returns {object} `payload` if it's valid.
   */
  #validatePayload(payload) {
    if (!lazy) {
      // The schemas and the validator live in system modules, out of reach of a
      // content realm. Skipping this does mean that in a content realm, we will
      // not validate payloads built directly by the view, such as a dismissal
      // acknowledge tip. All provider results cross the actor boundary already
      // validated.
      return payload;
    }
    let schema = lazy.UrlbarUtils.getPayloadSchema(this.type);
    if (!schema) {
      throw new Error(`Unrecognized result type: ${this.type}`);
    }
    let result = lazy.JsonSchemaValidator.validate(payload, schema, {
      allowExplicitUndefinedProperties: true,
      allowNullAsUndefinedProperties: true,
      allowAdditionalProperties: this.type == UrlbarShared.RESULT_TYPE.DYNAMIC,
    });
    if (!result.valid) {
      throw result.error;
    }
    return payload;
  }

  /**
   * This is useful for logging results. If you need the full payload, then it's
   * better to JSON.stringify the result object itself.
   *
   * @returns {string} string representation of the result.
   */
  toString() {
    if (this.payload.url) {
      return this.payload.title + " - " + this.payload.url.substr(0, 100);
    }
    if (this.payload.keyword) {
      return this.payload.keyword + " - " + this.payload.query;
    }
    if (this.payload.suggestion) {
      return this.payload.engine + " - " + this.payload.suggestion;
    }
    if (this.payload.engine) {
      return this.payload.engine + " - " + this.payload.query;
    }
    return JSON.stringify(this);
  }

  /**
   * Serializes this result to a plain, structured-cloneable object for sending
   * across the Urlbar actor boundary. Most data lives in private fields that a
   * bare structuredClone() would drop, so capture it explicitly; `id`,
   * `rowIndex`, `commands`, and `isSERP` are the public own properties.
   *
   * @returns {object} The wire representation; reconstruct with fromWire().
   */
  toWire() {
    return {
      type: this.#type,
      source: this.#source,
      autofill: this.#autofill,
      exposureTelemetry: this.#exposureTelemetry,
      group: this.#group,
      heuristic: this.#heuristic,
      hideRowLabel: this.#hideRowLabel,
      isBestMatch: this.isBestMatch,
      bestMatchType: this.#bestMatchType,
      isBottomUrlSuggestion: this.#isBottomUrlSuggestion,
      isRichSuggestion: this.#isRichSuggestion,
      isSuggestedIndexRelativeToGroup: this.#isSuggestedIndexRelativeToGroup,
      providerName: this.#providerName,
      providerType: this.#providerType,
      resultSpan: this.#resultSpan,
      richSuggestionIconSize: this.#richSuggestionIconSize,
      richSuggestionIconVariation: this.#richSuggestionIconVariation,
      rowLabel: this.#rowLabel,
      showFeedbackMenu: this.#showFeedbackMenu,
      suggestedIndex: this.#suggestedIndex,
      testForceNewContent: this.#testForceNewContent,
      payload: this.#payload,
      highlights: this.#highlights,
      id: this.id,
      rowIndex: this.rowIndex,
      commands: this.commands,
      isSERP: this.isSERP,
    };
  }

  /**
   * Reconstructs a UrlbarResult from the plain object produced by toWire(),
   * e.g. after it has crossed the Urlbar actor boundary.
   *
   * Structured clone strips data that doesn't survive it (e.g. a Rust
   * suggestion's UniFFI `Suggestion` class), so a reconstruction is a lossy
   * object distinct from the one that was serialized. Where the originals are
   * still around -- the parent's own query results -- pass them as
   * `liveResults` to get the original back instead, carrying over the
   * view-assigned `rowIndex` the wire preserves (the original never went
   * through a view).
   *
   * @param {object} wire The wire representation from toWire().
   * @param {?UrlbarResult[]} [liveResults] Results to match `wire` against by id.
   * @returns {UrlbarResult} The matching result from `liveResults`, else the
   *   reconstruction.
   */
  static fromWire(wire, liveResults = null) {
    let liveResult = liveResults?.find(r => r.id === wire.id);
    if (liveResult) {
      if (wire.rowIndex != null) {
        liveResult.rowIndex = wire.rowIndex;
      }
      return liveResult;
    }

    let result = new UrlbarResult({ ...wire, skipPayloadValidation: true });
    // The following aren't constructor parameters, so re-apply them.
    result.providerType = wire.providerType;
    result.id = wire.id;
    result.rowIndex = wire.rowIndex;
    result.commands = wire.commands;
    result.isSERP = wire.isSERP;
    return result;
  }

  #type;
  #source;
  #autofill;
  #exposureTelemetry;
  #group;
  #heuristic;
  #hideRowLabel;
  #bestMatchType;
  #isBottomUrlSuggestion;
  #isRichSuggestion;
  #isSuggestedIndexRelativeToGroup;
  #providerName;
  #providerType;
  #resultSpan;
  #richSuggestionIconSize;
  #richSuggestionIconVariation;
  #rowLabel;
  #showFeedbackMenu;
  #suggestedIndex;
  #payload;
  #highlights;
  #displayValuesCache;
  #testForceNewContent;
}

// In chrome window globals, we re-export the UrlbarResult from the system
// global. Otherwise, UrlbarResults created in a window global but cached in
// the system global would leak the window.
if (typeof ChromeUtils != "undefined" && typeof window != "undefined") {
  // @ts-ignore
  // eslint-disable-next-line no-class-assign
  ({ UrlbarResult } = ChromeUtils.importESModule(
    "chrome://browser/content/urlbar/UrlbarResult.mjs"
  ));
}
