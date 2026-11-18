/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/**
 * This file contains the shared types for the machine learning component. The intended
 * use is for defining types to be used in JSDoc. They are used in a form that the
 * TypeScript language server can read them, and provide code hints.
 *
 * @see https://firefox-source-docs.mozilla.org/code-quality/typescript/
 */

import { type PipelineOptions } from "chrome://global/content/ml/EngineProcess.sys.mjs";
import { MLEngine } from "./actors/MLEngineParent.sys.mjs";

export type EngineStatus =
  // The engine is waiting for a previous one to shut down.
  | "SHUTTING_DOWN_PREVIOUS_ENGINE"
  // The engine dispatcher has been created, and the engine is still initializing.
  | "INITIALIZING"
  // The engine is fully ready and idle.
  | "IDLE"
  // The engine is currently processing a run request.
  | "RUNNING"
  // The engine is in the process of terminating, but hasn't fully shut down.
  | "TERMINATING"
  // The engine has been fully terminated and removed.
  | "TERMINATED";

type UntypedEngineRequest = {
  args: unknown;
  options: {};
  streamerOptions?: {};
};

export type EngineRequests = EnsureAllFeatures<{
  "about-inference": UntypedEngineRequest;
  "link-preview": UntypedEngineRequest;
  "pdfjs-alt-text": UntypedEngineRequest;
  "simple-text-embedder": UntypedEngineRequest;
  "smart-intent": UntypedEngineRequest;
  "smart-tab-embedding": UntypedEngineRequest;
  "smart-tab-topic": UntypedEngineRequest;
  "formfill-classification": UntypedEngineRequest;
  chat: UntypedEngineRequest;

  "suggest-intent-classification": {
    /**
     * The list of classification requests. Often just one.
     */
    args: string[];
    /**
     * If any options are use, type them here. Currently this just passed as a blank object.
     */
    options: {};
    streamerOptions?: {};
  };

  "suggest-NER": {
    /**
     * All of the requests for running named entity recognition.
     */
    args: string[];
    /**
     * If any options are use, type them here. Currently this just passed as a blank object.
     */
    options: {};
    streamerOptions?: {};
  };
}>;

/**
 * We key the @see {MLEngine#run} method off of the featureId and the `MLEngine` create
 * options.
 */
export type EngineFeatureIds =
  | "about-inference"
  | "link-preview"
  | "pdfjs-alt-text"
  | "simple-text-embedder"
  | "smart-intent"
  | "smart-tab-embedding"
  | "smart-tab-topic"
  | "formfill-classification"
  | "suggest-intent-classification"
  | "suggest-NER";

/**
 * If a feature is missing, this will turn the type into a `never` and cause type issues.
 */
type EnsureAllFeatures<T> =
  Exclude<EngineFeatureIds, keyof T> extends never ? T : never;

type BasicEngineOptions = Partial<{
  taskName: string;
  featureId: EngineFeatureIds;
  timeoutMS: number;
  numThreads: number;
  backend: string;
}>;

/**
 * A map of the featureId to the engine create options.
 */
export type EngineCreateOptions = EnsureAllFeatures<{
  "about-inference": BasicEngineOptions;
  "link-preview": BasicEngineOptions;
  "pdfjs-alt-text": BasicEngineOptions;
  "simple-text-embedder": BasicEngineOptions;
  "smart-intent": BasicEngineOptions;
  "smart-tab-embedding": BasicEngineOptions;
  "smart-tab-topic": BasicEngineOptions;
  "formfill-classification": BasicEngineOptions;
  "suggest-intent-classification": BasicEngineOptions;
  "suggest-NER": BasicEngineOptions;
}>;

/**
 * This is a type-friendly way to pass around engine options keyed off of the FeatureId.
 */
export type EngineOptions<FeatureId extends EngineFeatureIds> =
  EngineRequests[FeatureId]["options"];

/**
 * A production engine creation observed by a test interceptor.
 */
export interface EngineCreationInterception {
  /** The engine returned by the production creation path. */
  engine: MLEngine<unknown>;

  /** The creation start time from the parent process monotonic clock. */
  start: number;

  /** The creation end time from the parent process monotonic clock. */
  end: number;
}

/**
 * Assertions and overrides applied to an intercepted engine creation.
 */
export interface EngineCreationInterceptionOptions {
  /** Engine options that must be present on the production request. */
  expectedOptions?: Partial<PipelineOptions>;

  /** Engine options to replace before continuing production creation. */
  overrides?: Partial<PipelineOptions>;
}

/** A lifecycle measured by an ML performance scenario. */
export type MLPerfLifecycle = "first-use" | "cold" | "warm";

/** The purpose of one invocation of an ML performance scenario. */
export type MLPerfSampleKind = "latency" | "memory" | "warmup";

/** Feature-owned measurements returned by one scenario invocation. */
export type MLPerfMeasurements = Record<string, number>;

/** Identifies the lifecycle and purpose of one scenario invocation. */
export interface MLPerfScenarioContext {
  /** The engine lifecycle being prepared or measured. */
  lifecycle: MLPerfLifecycle;

  /** Whether the invocation reports latency, samples memory, or warms up. */
  sampleKind: MLPerfSampleKind;

  /** The zero-based iteration within its lifecycle and sample kind. */
  iteration: number;
}

/**
 * A production feature interaction measured by the ML performance harness.
 *
 * @param context - The lifecycle and purpose of this invocation.
 * @returns Feature-owned measurements from the interaction.
 */
export type MLPerfScenario = (
  context: MLPerfScenarioContext
) => Promise<MLPerfMeasurements>;

/** Option replacements applied while measuring an ML performance scenario. */
export type MLPerfEngineOptionOverrides = {
  [Name in keyof PipelineOptions]?: {
    /** The value that production must request. */
    expectValue: PipelineOptions[Name];

    /** The value used by the performance measurement. */
    replaceWith: PipelineOptions[Name];
  };
};

/** An engine whose activity is measured by an ML performance scenario. */
export interface MLPerfEngineConfig {
  /** The feature ID supplied by production engine options. */
  featureId: string;

  /** Component inserted into shared metric names for multi-engine scenarios. */
  metricName?: string;

  /** Number of runs expected from this engine in each invocation. Defaults to one. */
  expectedRuns?: number;

  /** Engine options replaced while preserving their expected production values. */
  overrides?: MLPerfEngineOptionOverrides;
}

/** A completed production engine creation observed during a scenario. */
export interface MLPerfEngineCreationObservation {
  /** The feature ID supplied by production engine options. */
  featureId: string;

  /** The engine-creation start time. */
  start: number;

  /** The engine-creation end time. */
  end: number;
}

/** Resource fields recorded from a completed engine run. */
export interface MLPerfObservedRunResult {
  /** Inference-process resources immediately before the run. */
  resourcesBefore?: ResourceMeasurement;

  /** Inference-process resources immediately after the run. */
  resourcesAfter?: ResourceMeasurement;

  /** Metrics reported by the inference backend. */
  metrics?: {
    decodingTime?: number;
    inputTokens?: number;
    outputTokens?: number;
  };
}

/** A completed engine run observed during a scenario. */
export interface MLPerfEngineRunObservation
  extends MLPerfObservedRunResult, MLPerfEngineRunDetails {
  /** The feature ID supplied by production engine options. */
  featureId: string;

  /** The engine used for the run. */
  engine: MLEngine<EngineFeatureIds>;

  /** The run start time. */
  start: number;

  /** The run completion time. */
  end: number;
}

/** Generation measurements recorded for an engine run. */
export interface MLPerfEngineRunDetails {
  /** Time from the generator request to its first generated token in ms. */
  timeToFirstToken?: number;

  /** Generated-token throughput observed after the first token arrival. */
  tokensPerSecond?: number;

  /** Engine-reported decoding time in ms, only for runs that ran to completion. */
  decodingTime?: number;

  /** Engine-reported input token count, only for runs that ran to completion. */
  inputTokens?: number;

  /** Number of generated tokens the feature consumed. */
  outputTokens?: number;
}

/** A scoped controller for observing engine runs during one scenario. */
export interface MLPerfEngineRunCapture {
  /** Engine runs completed while the controller was active. */
  engineRuns: MLPerfEngineRunObservation[];

  /**
   * Restores the production engine methods.
   *
   * @throws If any measured engine run remains active.
   */
  cleanup(): void;
}

/** Measurements and engine activity observed during one scenario. */
export interface MLPerfScenarioObservation {
  /** Feature-owned measurements returned by the scenario. */
  measurements: MLPerfMeasurements;

  /** Engine creations completed during the scenario. */
  engineCreations: MLPerfEngineCreationObservation[];

  /** Engine runs completed during the scenario. */
  engineRuns: MLPerfEngineRunObservation[];

  /** Sampled peak inference-process memory in MiB. */
  peakMemory?: number;
}

/** Options controlling measurement of one scenario invocation. */
export interface MLPerfScenarioInvocationOptions {
  /** Engines whose production activity should be observed. */
  engines?: MLPerfEngineConfig[];

  /** Whether engine creation should be intercepted. */
  captureEngineCreation?: boolean;

  /** Whether to sample peak inference-process memory. */
  samplePeakMemory?: boolean;

  /** Delay between inference-process memory samples in milliseconds. */
  peakMemorySampleIntervalMs?: number;
}

/** Assertions used to validate ML performance measurements. */
export interface MLPerfAssertions {
  /**
   * Verifies a condition.
   *
   * @param condition - The condition to verify.
   * @param message - The assertion description.
   */
  ok(condition: unknown, message?: string): void;

  /**
   * Verifies that a number exceeds another number.
   *
   * @param actual - The measured value.
   * @param expected - The exclusive lower bound.
   * @param message - The assertion description.
   */
  greater(actual: number, expected: number, message?: string): void;

  /**
   * Verifies that two values are equal.
   *
   * @param actual - The measured value.
   * @param expected - The expected value.
   * @param message - The assertion description.
   */
  equal(actual: unknown, expected: unknown, message?: string): void;
}

/** The test context used by shared ML performance utilities. */
export interface MLPerfTestContext {
  /**
   * Writes an informational test message.
   *
   * @param message - The message to write.
   */
  info(message: string): void;

  /** Assertions used to validate collected measurements. */
  Assert: MLPerfAssertions;

  /**
   * Registers work to run when the test finishes.
   *
   * @param cleanup - Releases state owned by the test utility.
   */
  registerCleanupFunction(cleanup: () => void): void;
}

/** A collection of named ML performance measurement series. */
export interface MLPerfJournal {
  /**
   * Adds one value to a named series.
   *
   * @param name - The complete series name.
   * @param value - The measured value.
   */
  add(name: string, value: number): void;

  /** Reports every collected series to MozPerftest. */
  report(): void;
}

/** A running inference-process peak-memory sampler. */
export interface PeakInferenceMemorySampler {
  /**
   * Stops the sampler.
   *
   * @returns The peak inference-process memory in MiB.
   */
  stop(): Promise<number>;
}

/** Configuration for a complete ML performance scenario. */
export interface RunPerfScenarioConfig {
  /** Prefix applied to every reported measurement series. */
  metricPrefix: string;

  /** Suffix applied to every reported measurement series. */
  metricSuffix?: string;

  /** Runs one production feature interaction. */
  scenario: MLPerfScenario;

  /** Engines whose production activity should be observed. */
  engines?: MLPerfEngineConfig[];

  /** Whether to report the first-use sample, which always runs once. */
  measureFirstUse?: boolean;

  /** Number of cold-engine latency samples after the first use. */
  coldIterations?: number;

  /** Number of warm-engine latency samples. */
  warmIterations?: number;

  /** Peak-memory runs for each measured cold and warm lifecycle. */
  memoryIterations?: number;

  /** Delay between inference-process memory samples in milliseconds. */
  peakMemorySampleIntervalMs?: number;
}

/**
 * Measurements from ChromeUtils.cpuTimeSinceProcessStart and
 * ChromeUtils.currentProcessMemoryUsage that happen inside of the inference process
 * where work is actually happening
 */
export interface ResourceMeasurement {
  cpuTime: number | null;
  memory: number | null;
}

type UntypedEngineResponse = {
  resourcesBefore: ResourceMeasurement;
  resourcesAfter: ResourceMeasurement;
};

type ChatEngineResponse = {
  finalOutput: string;
  metrics: unknown;
} & UntypedEngineResponse;

/**
 * Base metrics common to all pipeline runs.
 */
interface BaseMetrics {
  preprocessingTime: number;
  inferenceTime: number;
  decodingTime: number;
  runTimestamps: Array<{ name: string; when: number }>;
}

/**
 * Metrics for classification tasks (text-classification, token-classification).
 */
interface ClassificationMetrics extends BaseMetrics {
  tokenizingTime: number;
  inputTokens: number;
  outputTokens: number;
}

export type EngineResponses = EnsureAllFeatures<{
  "about-inference": UntypedEngineResponse;
  chat: ChatEngineResponse;
  "link-preview": UntypedEngineResponse;
  "pdfjs-alt-text": UntypedEngineResponse;
  "simple-text-embedder": UntypedEngineResponse;
  "smart-intent": UntypedEngineResponse;
  "smart-tab-embedding": UntypedEngineResponse;
  "smart-tab-topic": UntypedEngineResponse;
  "formfill-classification": UntypedEngineResponse;
  "suggest-intent-classification": Array<{
    label: string;
    score: number;
  }> & {
    metrics?: ClassificationMetrics;
    resourcesBefore: ResourceMeasurement;
    resourcesAfter: ResourceMeasurement;
  };
  "suggest-NER": Array<{
    label: string;
    score: number;
    entity: string;
    word: string;
  }> & {
    metrics?: ClassificationMetrics;
    resourcesBefore: ResourceMeasurement;
    resourcesAfter: ResourceMeasurement;
  };
}>;

/**
 * The EngineId is used to identify a unique engine that can be shared across multiple
 * consumers. This way a single model can be loaded into memory and used in different
 * locations, assuming the other parameters match as well.
 */
export type EngineId = string;

/**
 * Utility type to extract the data fields from a class. It removes all of the
 * functions.
 */
type DataFields<T> = {
  [K in keyof T as T[K] extends Function ? never : K]: T[K];
};

/**
 * The PipelineOptions are a nominal class that validates the options. The
 * PipelineOptionsRaw are the raw subset of those.
 */
type PipelineOptionsRaw = Partial<DataFields<PipelineOptions>>;

/**
 * Tracks the current status of the engines for about:inference. It's not used
 * for deciding any business logic of the engines, only for debug info.
 */
export type StatusByEngineId = Map<
  EngineId,
  {
    status: EngineStatus;
    options: PipelineOptions | PipelineOptionsRaw | null;
  }
>;

export type EngineNames =
  keyof GleanImpl["firefoxAiRuntime"]["engineCreationSuccess"];

export interface ParsedModelHubUrl {
  model: string;
  revision: string;
  file: string;
  modelWithHostname: string;
}

export interface SyncEvent {
  created: BaseRecord[];
  updated: Array<{ old: BaseRecord; new: BaseRecord }>;
  deleted: BaseRecord[];
}

interface BaseRecord {
  id: string; // e.g. "0931e27c-4844-4d0c-92eb-4c51bceaf3f5";
  last_modified: number; // e.g. 1730736272603
  schema: number; // e.g. 1730381905606
}

/**
 * These are the types for all of the collections in RemoteSettings. They
 * also include the BaseRecord information. RecordsML is the exported type.
 */
interface RecordsMLUnique {
  /**
   * Allow or deny URL Prefixes.
   * https://firefox.settings.services.mozilla.com/v1/buckets/main/collections/ml-model-allow-deny-list/records
   */
  "ml-model-allow-deny-list": {
    filter: "ALLOW" | "DENY";
    urlPrefix: string; // e.g. "https://huggingface.co/Mozilla/"
    description: string; // e.g. "All models we host are allowed."
  };

  /**
   * Specific configuration options for different tasks. Filters can be used
   * to select specific features, tasks or models.
   * https://firefox.settings.services.mozilla.com/v1/buckets/main/collections/ml-inference-options/records
   */
  "ml-inference-options": {
    modelId: string; // e.g. "tliumozilla/intent-detection-mobilebert";
    taskName: string; // "text-classification";
    dtype?: string; // "q8",
    featureId?: string; // "query-intent-detection";
    processorId: string; // "tliumozilla/intent-detection-mobilebert";
    tokenizerId: string; // "tliumozilla/intent-detection-mobilebert";
    modelRevision: string; // "main";
    processorRevision: string; // "main";
    tokenizerRevision: string; // "main";
    backend?: string; // "onnx-native"
    numThreads?: number;
  };
}

export type RecordsML = {
  [Collection in keyof RecordsMLUnique]: BaseRecord &
    RecordsMLUnique[Collection];
};

export interface RemoteSettingsInferenceOptions {
  modelRevision: string | null;
  modelId: string | null;
  tokenizerRevision: string | null;
  tokenizerId: string | null;
  processorRevision: string | null;
  processorId: string | null;
  dtype: string | null;
  numThreads: number | null;
  runtimeFilename: string | null;
}

export interface ChunkResponse {
  text: string;
  tokens: any;
  isPrompt: any;
  toolCalls: Array<{
    id: string;
    function: { name: string; arguments: any[] };
  }>;
  usage?: {
    prompt_tokens: number;
    completion_tokens: number;
    total_tokens: number;
  };
}

export type TypedArray =
  | Int8Array
  | Uint8Array
  | Uint8ClampedArray
  | Int16Array
  | Uint16Array
  | Int32Array
  | Uint32Array
  | Float32Array
  | Float64Array;
