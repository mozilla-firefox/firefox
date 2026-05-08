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
 * Measurements from ChromeUtils.cpuTimeSinceProcessStart and
 * ChromeUtils.currentProcessMemoryUsage that happen inside of the inference process
 * where work is actually happening
 */
interface ResourceMeasurement {
  cpuTime: number | null;
  memory: number | null;
}

type UntypedEngineResponse = {
  resourcesBefore: ResourceMeasurement;
  resourcesAfter: ResourceMeasurement;
};

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
