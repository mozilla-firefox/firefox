/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/. */


/**
 * Represents a single message exchanged in a chat with a language model.
 *
 * This dictionary is used to define the role and content of each message in the
 * prompt passed to a llama.cpp-based LLM (e.g., user or assistant messages).
 */
dictionary LlamaChatMessage {
  /**
   * The role of the speaker in the chat conversation.
   * Common values include "system", "user", or "assistant".
   */
  required UTF8String role;
  /**
   * The textual content of the message associated with the given role.
   */
  required UTF8String content;
};

enum LlamaChatPhase {
  "prompt",
  "generation"
};

[GenerateConversionToJS]
dictionary LlamaChatResponse {
  required UTF8String piece;
  required sequence<long> tokens;
  required LlamaChatPhase phase;
  required boolean isPhaseCompleted;
};

dictionary LlamaFormatChatOptions {

  /**
   * List of role and their content to be formatted into a string prompt.
   */
  required sequence<LlamaChatMessage> messages;
  /**
   * If true, the assistant role will be added to the generated prompt
   */
  boolean addAssistant = true;

};

enum LlamaKVCacheDtype {
  "f32",
  "f16",
  "q8_0",
  "q5_1",
  "q5_0",
  "q4_1",
  "q4_0"
};

/**
 * Configuration options for creating a LLaMA context.
 * Name are chosen to map directly to the ones from llama.cpp.
 */
dictionary LlamaContextOptions {
  /**
   * Maximum combined length of input & generate tokens.
   */
  unsigned long nCtx = 2048;

  /**
   * Logical max batch size submitted to llama_decode.
   */
  unsigned long nBatch = 2048;

  /**
   * Physical max batch size for actual processing.
   */
  unsigned long nUbatch = 512;

  /**
   * Maximum number of concurrent sequences (distinct KV states).
   */
  unsigned long nSeqMax = 1;

  /**
   * Number of threads for generation.
   */
  required long nThreads;

  /**
   * Number of threads for batch/prompt processing.
   */
  required long nThreadsBatch;

  /**
   * Data type for the K (key) cache [EXPERIMENTAL].
   */
  LlamaKVCacheDtype kCacheDtype = "f16";

  /**
   * Data type for the V (value) cache [EXPERIMENTAL].
   */
  LlamaKVCacheDtype vCacheDtype = "f16";

  /**
   * If true, offload K, Q, V ops (including KV cache) to the GPU.
   * Only active if GPU is used with NGpULayers != 0 in LlamaModelOptions
   */
  boolean offloadKqv = true;

  /**
   * If true, use FlashAttention (experimental).
   */
  boolean flashAttn = false;

  /**
   * If true, disable performance measurement (no timing output).
   */
  boolean noPerf = false;

  /**
   * If true, offload host-side tensor operations to the device.
   * Only active if GPU is used with NGpULayers != 0 in LlamaModelOptions
   */
  boolean opOffload = true;

  /**
   * If true, use a full-size SWA (sliding window attention) cache.
   */
  boolean swaFull = true;
};

/**
 * Configuration options for loading a LLaMA model.
 * See comments here https://github.com/ggml-org/llama.cpp/blob/b5774/include/llama.h#L298
 * for more description on each field.
 */
dictionary LlamaModelOptions {
  /**
   * If true, use `mmap` for loading the model, if supported.
   */
  boolean useMmap = true;

  /**
   * If true, attempt to lock the model in RAM using `mlock`.
   */
  boolean useMlock = false;

  /**
   * If true, perform extra validation on model tensor data.
   */
  boolean checkTensors = false;

  /**
   * Number of model layers to offload to GPU.
   * A value of 0 disables GPU offloading.
   */
  long nGpuLayers = 0;

  /**
   * Context configuration (e.g. nCtx, threads).
   */
  LlamaContextOptions context = {};

};

enum LlamaSamplerType {
  "logit-bias",
  "dry",
  "top-k",
  "top-p",
  "top-n-sigma",
  "min-p",
  "xtc",
  "typical",
  "temperature",
  "temperature-ext",
  "infill",
  "penalties",
  "mirostat",
  "dist",
};

dictionary LlamaLogitBias {
  required long token;
  required float bias;
};

/**
 * Represents a configured sampler.
 */
dictionary LlamaSamplerConfig {
  /**
   * The sampler algorithm to use.
   */
  required LlamaSamplerType type;

  /**
   * Minimum number of tokens to keep (0 = disabled).
   */
  long minKeep = 0;

  /**
   * Top-K cutoff. If <= 0, uses full vocabulary.
   */
  long topK = 40;

  /**
   * Top-P (nucleus) sampling threshold.
   */
  float topP = 0.95;

  /**
   * Minimum P cutoff.
   */
  float minP = 0.05;

  /**
   * XTC sampling probability (0.0 = disabled).
   */
  float xtcProbability = 0.0;

  /**
   * XTC threshold (values > 0.5 disable XTC).
   */
  float xtcThreshold = 0.10;

  /**
   * Typical sampling cutoff (1.0 = disabled).
   */
  float typP = 1.0;

  /**
   * Sampling temperature (0.0 or below = greedy decoding).
   */
  float temp = 0.80;

  /**
   * Dynamic temperature range (0.0 = disabled).
   */
  float dynatempRange = 0.0;

  /**
   * Dynamic temperature exponent (entropy-to-temp mapping).
   */
  float dynatempExponent = 1.0;

  /**
   * Repetition penalty: number of tokens to track (-1 = context size).
   */
  long penaltyLastN = 64;

  /**
   * Repetition penalty multiplier (1.0 = disabled).
   */
  float penaltyRepeat = 1.0;

  /**
   * Frequency penalty (0.0 = disabled).
   */
  float penaltyFreq = 0.0;

  /**
   * Presence penalty (0.0 = disabled).
   */
  float penaltyPresent = 0.0;

  /**
   * DRY multiplier (0.0 = disabled).
   */
  float dryMultiplier = 0.0;

  /**
   * DRY base exponent (0.0 = disabled).
   */
  float dryBase = 1.75;

  /**
   * DRY allowed repetition length before penalization starts.
   */
  long dryAllowedLength = 2;

  /**
   * DRY lookback window (0 = disable, -1 = context size).
   */
  long dryPenaltyLastN = -1;

  /**
   * Mirostat mode (0 = disabled, 1 = v1, 2 = v2).
   */
  long mirostat = 0;

  /**
   * Top-n sigma sampling cutoff (-1.0 = disabled).
   */
  float topNSigma = -1.0;

  /**
   * Mirostat target entropy (tau).
   */
  float mirostatTau = 5.0;

  /**
   * Mirostat learning rate (eta).
   */
  float mirostatEta = 0.1;

  /**
   * List of token-specific logit biases.
   */
  sequence<LlamaLogitBias> logitBias = [];

  /**
   * If true, disables performance metrics.
   */
  boolean noPerf = false;

  /**
   * Random number seed for sampling.
   */
  unsigned long  seed;
};

dictionary LlamaDeTokenizationOptions {
  /**
   * Maximum number of UTF-8 characters that may be contained in a single model token.
   * This is used to reserve enough space during  detokenization.
   */
  long maxCharsPerToken = 256;

  /**
   * Whether to render special tokens such as <BOS>, <EOS>, or <UNK> in the output.
   * If false, these tokens will be omitted from the detokenized result.
   */
  boolean renderSpecialTokens = true;
};

dictionary LlamaTokenizationOptions {

  /**
   * Allow to add BOS and EOS tokens if model is configured to do so.
   */
  boolean addBosAndEos = true;


  /**
   * Allow tokenizing special and/or control tokens which otherwise are not exposed and treated
   * as plaintext. Does not insert a leading space.
   */
  boolean parseSpecilControlTokens = true;
};

dictionary LlamaChatOptions {

  /**
   * Sampler stack to apply during decoding.
   */
  sequence<LlamaSamplerConfig> samplers = [];

  /**
   * Input prompt text to process.
   */
  required UTF8String prompt;

  /**
   * Optional output buffer size (0 = no preallocation).
   */
  long minOutputBufferSize = 1;

  /**
   * Maximum number of generation steps (tokens).
   */
  long maxGeneratedTokens = 512;

  /**
   * If true, stop when encountering known model end-of-generation tokens.
   */
  boolean stopOnEndOfGenerationTokens = true;

  /**
   * List of token IDs that should stop generation.
   */
  sequence<long> stopTokens = [];

  LlamaTokenizationOptions tokenizationOptions = {};

  LlamaDeTokenizationOptions deTokenizationOptions = {};
};

[Func="LlamaRunner::InInferenceProcess", Exposed=(DedicatedWorker,Window)]
interface LlamaRunner {
  [Throws] constructor();

  [Throws] Promise<undefined> initialize(LlamaModelOptions options, Blob modelBlob);

  [Throws] Promise<UTF8String> formatChat(LlamaFormatChatOptions options);

  [NewObject, Throws] ReadableStream createGenerationStream(LlamaChatOptions options);
};
