/* -*- Mode: IDL; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

enum TextGenerationRole {
  "system",
  "user",
  "assistant",
};

enum TextGenerationSamplerType {
  "logit-bias",
  "top-k",
  "top-p",
  "temperature",
  "dist",
};

enum TextGenerationFinishReason {
  "eos",
  "length",
  /**
   * Never reported today: the backend stops before delivering the token it
   * stopped on, so a run ended by a stop token reports "eos".
   */
  "stop-token",
  "cancelled",
};

enum TextGenerationKVCacheDtype {
  "f32",
  "f16",
  "q8_0",
  "q5_1",
  "q5_0",
  "q4_1",
  "q4_0",
};

dictionary TextGeneratorCreateOptions {
  unsigned long contextSize = 1024;
  /** 0 resolves to the physical-core count. Values above the backend limit reject. */
  unsigned long numThreads = 0;
  /** Decode-phase thread count; 0 follows numThreads. */
  unsigned long numThreadsDecoding = 0;
  unsigned long batchSize = 2048;
  unsigned long ubatchSize = 512;
  /** Quantized values require flashAttn; create() rejects otherwise. */
  TextGenerationKVCacheDtype kvCacheDtype = "f16";
  boolean flashAttn = false;
  /** Names the feature asking, in profiler markers only. */
  UTF8String featureId = "";
};

dictionary TextGenerationMessage {
  required TextGenerationRole role;
  required UTF8String content;
};

dictionary TextGenerationLogitBias {
  required long token;
  required float bias;
};

dictionary TextGenerationSampler {
  required TextGenerationSamplerType type;
  long topK = 40;
  float topP = 0.95;
  float temp = 0.80;
  sequence<TextGenerationLogitBias> logitBias = [];
  unsigned long seed;
};

dictionary TextGenerationRequest {
  required sequence<TextGenerationMessage> messages;
  unsigned long maxTokens = 512;
  /** Generated tokens batched into one delta callback. */
  unsigned long bufferLength = 20;
  sequence<TextGenerationSampler> samplers = [];
  sequence<long> stopTokens = [];
  boolean stopOnEndOfGenerationTokens = true;
};

[GenerateConversionToJS]
dictionary TextGenerationTimings {
  required double prefillMs;
  required double decodeMs;
};

[GenerateConversionToJS]
dictionary TextGenerationUsage {
  required unsigned long promptTokens;
  /** Unicode code points in the templated prompt. */
  required unsigned long promptCharacters;
  required unsigned long generatedTokens;
  required TextGenerationTimings timings;
};

[GenerateConversionToJS]
dictionary TextGenerationResourceSnapshot {
  /** Cumulative CPU time of the generator process since it started. */
  required unsigned long long cpuTimeMs;
  /** Private physical bytes of the generator process. */
  required unsigned long long memoryBytes;
};

[GenerateConversionToJS]
dictionary TextGenerationResources {
  required TextGenerationResourceSnapshot before;
  required TextGenerationResourceSnapshot after;
};

[GenerateConversionToJS]
dictionary TextGenerationResult {
  required UTF8String content;
  required TextGenerationFinishReason reason;
  required TextGenerationUsage usage;
  required TextGenerationResources resources;
};

callback TextGenerationDeltaCallback = undefined (UTF8String text);

[ChromeOnly, Exposed=Window]
interface TextGenerator {
  /** The model Blob must be file-backed. Rejects with the load error. */
  [Throws]
  static Promise<TextGenerator> create(Blob model,
                                       optional TextGeneratorCreateOptions options = {});

  /**
   * A second generate() before the promise settles, or one after terminate(),
   * rejects with InvalidStateError; one after the inference process went away
   * rejects with AbortError. Only request messages are appended to the
   * history, and only once the generation succeeds; a multi-turn caller
   * re-sends the assistant reply as an "assistant" message.
   */
  [Throws]
  Promise<TextGenerationResult> generate(TextGenerationRequest request,
                                         optional TextGenerationDeltaCallback onDelta);

  /** Throws InvalidStateError while a generate() is pending. */
  [Throws]
  undefined clear();

  /** The pending generate() still resolves, with reason "cancelled". */
  undefined cancel();

  /** Pending generate() promises reject with AbortError. Idempotent. */
  undefined terminate();
};
