/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

let schemaRegistered = false;

function ensureSchemaRegistered() {
  if (schemaRegistered) {
    return;
  }
  schemaRegistered = true;

  ChromeUtils.registerMarkerSchema({
    name: "PageExtractor",
    tooltipLabel: "{marker.data.phaseLabel}",
    tableLabel: "{marker.data.process}: {marker.data.phaseLabel}",
    chartLabel: "{marker.data.phaseLabel}",
    display: ["marker-chart", "marker-table"],
    colorField: "color",
    data: [
      { key: "process", label: "Process", format: "string" },
      {
        key: "phaseLabel",
        label: "Description",
        format: "string",
        searchable: true,
      },
      { key: "phase", label: "Phase", format: "string", searchable: true },
      {
        key: "flowId",
        label: "Flow ID",
        format: "string",
        searchable: true,
      },
      // The `PageExtractor #<n>` track this marker is drawn on. On the
      // payload as well as in the track name, so that two calls sharing a
      // flow are still tellable apart from the marker table.
      { key: "traceId", label: "Trace", format: "integer" },
      {
        key: "strategy",
        label: "Strategy",
        format: "string",
        searchable: true,
      },
      {
        key: "siteStrategy",
        label: "Site strategy",
        format: "string",
        searchable: true,
      },
      { key: "status", label: "Status", format: "string", searchable: true },
      { key: "options", label: "Selected options", format: "string" },
      // Its own field rather than part of `options`, because "url" is what
      // makes the profiler strip it from a profile shared with URLs
      // excluded. The "string" format of `options` is explicitly not
      // sanitized.
      { key: "sourceUrl", label: "Source URL", format: "url" },
      { key: "textLength", label: "Text length", format: "integer" },
      { key: "linkCount", label: "Links", format: "integer" },
      { key: "canvasCount", label: "Canvases", format: "integer" },
      {
        key: "errorName",
        label: "Error",
        format: "string",
        searchable: true,
      },
      { key: "color", hidden: true },
    ],
  });
}

/**
 * Formats only the options the caller passed to getText(), so the marker
 * shows what was selected, not defaults it never applied.
 *
 * `sourceUrl` is excluded: it goes on its own "url"-format field, which the
 * profiler sanitizes. Anything added here lands in a "string" field that it
 * does not, so keep URLs and paths out.
 *
 * @param {Record<string, any>} options
 */
function formatOptions(options) {
  return Object.entries(options)
    .filter(([key, value]) => value !== undefined && key !== "sourceUrl")
    .map(([key, value]) => `${key}=${value}`)
    .join(", ");
}

/**
 * One phase a PageExtractor call can go through. `name` is what lands in
 * the marker's `phase` field and in `page_extractor.phase`, for correlating
 * with code; `label` is the plain-language description a non-developer
 * reading a shared profile sees.
 *
 * @typedef {object} PageExtractorPhase
 * @property {string} name - The phase as recorded, e.g. "dom-extract".
 * @property {string} label - The description shown in a profile.
 */

/**
 * Identifies one trace: a single PageExtractor call and every phase nested
 * under it.
 *
 * @typedef {object} TraceId
 * @property {number} id - Numbers the trace within this session, and names
 *   the `PageExtractor #<id>` profiler track its markers are drawn on.
 * @property {string} flowId - The flow the trace belongs to, recorded as
 *   `page_extractor.phase`'s `flow_id`. One flow can span several traces,
 *   so this is the coarser of the two.
 */

// Counts traces so each gets its own profiler track: concurrent calls don't
// interleave on one, and a nested phase shares its call's track.
let nextId = 1;

const DEFAULT_MARKER_COLOR = "blue";
// Statuses other than "success" or "error" (e.g. "unavailable",
// "document-hidden", "empty") are handled outcomes, not thrown exceptions.
const HANDLED_OUTCOME_MARKER_COLOR = "yellow";
const ERROR_MARKER_COLOR = "red";

let submissionScheduled = false;

/**
 * The page-extractor ping is submitted on `idle-daily`, from the parent
 * process only. Scheduled on the first recorded event rather than at module
 * load, so a session that never extracts a page never submits, and content
 * processes (whose events reach the parent through FOG) never try to.
 *
 * Events already recorded outlive a session in Glean's database, so a
 * session that ends before `idle-daily` fires loses nothing.
 */
function scheduleSubmission() {
  if (
    submissionScheduled ||
    Services.appinfo.processType !== Services.appinfo.PROCESS_TYPE_DEFAULT
  ) {
    return;
  }
  submissionScheduled = true;
  Services.obs.addObserver(
    () => GleanPings.pageExtractor.submit(),
    "idle-daily"
  );
}

// Maps this.#data's camelCase field names to page_extractor.phase's
// extra_keys in metrics.yaml. Keep these two lists in sync.
const GLEAN_EXTRA_KEYS = {
  process: "process",
  phase: "phase",
  strategy: "strategy",
  siteStrategy: "site_strategy",
  status: "status",
  errorName: "error_name",
  textLength: "text_length",
  linkCount: "link_count",
  canvasCount: "canvas_count",
};

// Page-derived counts, bucketed before recording so an exact value can't
// fingerprint the page (data review). duration_ms measures our own work,
// so it stays exact. The profiler marker keeps exact values for everything.
const GLEAN_BUCKETED_KEYS = new Set([
  "text_length",
  "link_count",
  "canvas_count",
]);

/**
 * Rounds a count down to a power of two; 0 stays 0. Counts are string and
 * array lengths, so they fit the 32-bit range Math.clz32 works on.
 *
 * @param {number} count
 * @returns {number}
 */
function bucketToPowerOfTwo(count) {
  if (count <= 0) {
    return 0;
  }
  return 2 ** (31 - Math.clz32(count));
}

/**
 * Accumulates data for one PageExtractor instrumentation event. `finish()`
 * always records a `page_extractor.phase` Glean event, and a profiler
 * marker only while profiling; both are keyed by a `TraceId` so parent- and
 * content-process records describe the same request.
 *
 * `PageExtractorEvent.trace()` is the entry point for the common case of
 * measuring one task from start to finish. The constructor and `finish()`
 * are also exposed directly for the rarer case of an event whose outcome
 * races something other than a single task (e.g. navigateEvent in
 * PageExtractorParent.getHeadlessExtractor, settled from three different
 * callbacks).
 */
export class PageExtractorEvent {
  /**
   * Every phase this instrumentation knows about, kept in one place so
   * adding or renaming one only touches this file.
   *
   * Callers name a phase through this object rather than by string, so a
   * misspelling is `undefined` and throws below, rather than silently
   * mislabelling a marker and fragmenting `page_extractor.phase` under a
   * slice that doesn't exist.
   */
  static Phase = Object.freeze({
    headlessExtractor: {
      name: "headless-extractor",
      label: "Load page in the background",
    },
    headlessNavigate: {
      name: "headless-navigate",
      label: "Navigate to page",
    },
    waitForReady: {
      name: "wait-for-ready",
      label: "Wait for page to finish loading",
    },
    getPageMetadata: { name: "get-page-metadata", label: "Read page details" },
    getText: { name: "get-text", label: "Extract page text" },
    pdfExtract: { name: "pdf-extract", label: "Read PDF text" },
    readerParse: {
      name: "reader-parse",
      label: "Simplify page (Reader Mode)",
    },
    readerOutputParse: {
      name: "reader-output-parse",
      label: "Process simplified page",
    },
    domExtract: { name: "dom-extract", label: "Scan page content" },
    canvasCapture: { name: "canvas-capture", label: "Capture page images" },
    youtubeExtract: {
      name: "youtube-extract",
      label: "Read video transcript",
    },
    googleDocsExtract: {
      name: "google-docs-extract",
      label: "Read Google Doc",
    },
  });

  static #phases = new Set(Object.values(this.Phase));

  #data;
  #innerWindowId;
  #startTime;
  #options;
  #traceId;
  #finished = false;

  /**
   * Starts a new trace, taking the next `PageExtractor #<id>` profiler
   * track for it.
   *
   * @param {string} [flowId] - The flow this trace runs as part of, when it
   *   belongs to a larger one that spans several traces. Mints a flow of
   *   this trace's own when omitted.
   * @returns {TraceId}
   */
  static nextTraceId(flowId = crypto.randomUUID()) {
    return Object.freeze({ id: nextId++, flowId });
  }

  /**
   * @param {PageExtractorPhase} phase - A `PageExtractorEvent.Phase` member.
   * @param {object} data
   * @param {TraceId} [data.traceId] - The trace to record this phase under.
   *   Starts a trace of its own when omitted.
   * @param {string} [data.process]
   * @param {string} [data.strategy]
   * @param {number} [data.innerWindowId]
   * @param {Record<string, any>} [data.options]
   */
  constructor(phase, data) {
    if (!PageExtractorEvent.#phases.has(phase)) {
      throw new Error(
        `Not a PageExtractorEvent.Phase member: ${JSON.stringify(phase)}`
      );
    }
    // Timed from construction, not from trace() below: some events (e.g.
    // navigateEvent in PageExtractorParent.getHeadlessExtractor) are
    // finished directly, racing multiple outcomes, and never go through
    // trace() at all. Construction is the one point every event passes
    // through.
    this.#startTime = ChromeUtils.now();
    this.#innerWindowId = data.innerWindowId;
    this.#options = data.options;
    this.#traceId = data.traceId ?? PageExtractorEvent.nextTraceId();
    this.#data = {
      type: "PageExtractor",
      process: data.process,
      phase: phase.name,
      phaseLabel: phase.label,
      flowId: this.#traceId.flowId,
      traceId: this.#traceId.id,
    };
    if (data.strategy) {
      this.#data.strategy = data.strategy;
    }
  }

  /**
   * The trace this event belongs to. Pass it as a nested
   * PageExtractorEvent's `traceId`, over IPC included, so that phase lands
   * on the same profiler track and correlates with this one in
   * `page_extractor.phase`.
   *
   * @returns {TraceId}
   */
  get traceId() {
    return this.#traceId;
  }

  addData(data) {
    for (const [key, value] of Object.entries(data)) {
      if (value !== undefined && value !== null) {
        this.#data[key] = value;
      }
    }
  }

  // Idempotent so racing outcomes (e.g. navigation vs. timeout) can each
  // call finish() unconditionally; the first call wins.
  finish(data = {}) {
    if (this.#finished) {
      return;
    }
    this.#finished = true;
    this.addData(data);

    const gleanPayload = {
      flow_id: this.#traceId.flowId,
    };
    for (const [dataKey, gleanKey] of Object.entries(GLEAN_EXTRA_KEYS)) {
      const value = this.#data[dataKey];
      gleanPayload[gleanKey] =
        value !== undefined && GLEAN_BUCKETED_KEYS.has(gleanKey)
          ? bucketToPowerOfTwo(value)
          : value;
    }
    gleanPayload.duration_ms = Math.round(ChromeUtils.now() - this.#startTime);

    // Recorded before the profiler-active check: Glean is the permanent
    // record, the profiler marker isn't.
    Glean.pageExtractor.phase.record(gleanPayload);
    scheduleSubmission();

    if (!Services.profiler.IsActive()) {
      return;
    }
    ensureSchemaRegistered();
    if (this.#data.status === "success") {
      this.#data.color = DEFAULT_MARKER_COLOR;
    } else if (this.#data.status === "error") {
      this.#data.color = ERROR_MARKER_COLOR;
    } else {
      this.#data.color = HANDLED_OUTCOME_MARKER_COLOR;
    }
    const formatted = this.#options ? formatOptions(this.#options) : "";
    if (formatted) {
      this.#data.options = formatted;
    }
    if (this.#options?.sourceUrl) {
      this.#data.sourceUrl = this.#options.sourceUrl;
    }
    ChromeUtils.addProfilerMarker(
      `PageExtractor #${this.#traceId.id}`,
      {
        category: "JavaScript",
        innerWindowId: this.#innerWindowId,
        startTime: this.#startTime,
      },
      this.#data
    );
  }

  /**
   * Runs `task`, finishing with "success" on resolve or "error" (and
   * rethrowing) on throw. `task` can call `finish()` itself first for a
   * more specific status (e.g. "unavailable"); since finish() is
   * idempotent, the status set here is then a no-op.
   *
   * Private: only `trace()` below calls this, immediately after
   * construction, so that constructing an event and timing a task are
   * never two separate steps a caller can pull apart.
   *
   * @template T
   * @param {() => Promise<T> | T} task
   * @returns {Promise<T>}
   */
  async #run(task) {
    try {
      const result = await task();
      this.finish({ status: "success" });
      return result;
    } catch (error) {
      // Optional chaining because JS permits throwing anything: a task that
      // rejects with null would otherwise throw a TypeError here, replacing
      // the original rejection and leaving this event never finished.
      this.finish({ status: "error", errorName: error?.name });
      throw error;
    }
  }

  /**
   * Constructs a PageExtractorEvent for `phase` and immediately measures
   * `task` with it, passing the event to `task` so it can call
   * `addData()`/`finish()` for a more specific status before `task`
   * resolves. This is the usual way to create a PageExtractorEvent: since
   * construction and the start of the measured work happen in the same
   * call, there's no separate step where the clock could be started later.
   *
   * @template T
   * @param {PageExtractorPhase} phase - A `PageExtractorEvent.Phase` member.
   * @param {Record<string, any>} data
   * @param {(event: PageExtractorEvent) => Promise<T> | T} task
   * @returns {Promise<T>}
   */
  static trace(phase, data, task) {
    const event = new PageExtractorEvent(phase, data);
    return event.#run(() => task(event));
  }
}
