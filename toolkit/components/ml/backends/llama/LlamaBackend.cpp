/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "LlamaBackend.h"

#include <cstdint>
#include <cstddef>

#include <functional>
#include "mozilla/HashTable.h"
#include "nsIFileStreams.h"
#include "nsTArray.h"

#include "mozilla/Logging.h"
#include "nsFmtString.h"
#include "GeckoProfiler.h"

mozilla::LazyLogModule gLlamaBackendLog("GeckoMLLlamaBackendNative");

namespace mozilla::llama {

void LlamaBackend::GgmlThreadpoolDeleter::operator()(
    struct ggml_threadpool* aTp) const {
  if (auto* lib = LlamaRuntimeLinker::Get()) {
    if (lib->ggml_threadpool_free) {
      lib->ggml_threadpool_free(aTp);
    }
  }
}

void LlamaBackend::LlamaModelDeleter::operator()(llama_model* aModel) const {
  if (auto* lib = LlamaRuntimeLinker::Get()) {
    if (lib->llama_model_free) {
      lib->llama_model_free(aModel);
    }
  }
}

void LlamaBackend::LlamaContextDeleter::operator()(llama_context* aCtx) const {
  if (auto* lib = LlamaRuntimeLinker::Get()) {
    if (lib->llama_free) {
      lib->llama_free(aCtx);
    }
  }
}

void LlamaBackend::LlamaSamplerDeleter::operator()(llama_sampler* aSmpl) const {
  if (auto* lib = LlamaRuntimeLinker::Get()) {
    if (lib->llama_sampler_free) {
      lib->llama_sampler_free(aSmpl);
    }
  }
}

#define LOGD(fmt, ...) \
  MOZ_LOG_FMT(gLlamaBackendLog, LogLevel::Debug, fmt, ##__VA_ARGS__)

#define LOGV(fmt, ...) \
  MOZ_LOG_FMT(gLlamaBackendLog, LogLevel::Verbose, fmt, ##__VA_ARGS__)

#define LOGW(fmt, ...) \
  MOZ_LOG_FMT(gLlamaBackendLog, LogLevel::Warning, fmt, ##__VA_ARGS__)

#define LOGE(fmt, ...) \
  MOZ_LOG_FMT(gLlamaBackendLog, LogLevel::Verbose, fmt, ##__VA_ARGS__)

ggml_type GgmlTypeFromKVCacheDtype(LlamaKVCacheDtype aDtype) {
  switch (aDtype) {
    case LlamaKVCacheDtype::F32:
      return GGML_TYPE_F32;
    case LlamaKVCacheDtype::F16:
      return GGML_TYPE_F16;
    case LlamaKVCacheDtype::Q8_0:
      return GGML_TYPE_Q8_0;
    case LlamaKVCacheDtype::Q5_1:
      return GGML_TYPE_Q5_1;
    case LlamaKVCacheDtype::Q5_0:
      return GGML_TYPE_Q5_0;
    case LlamaKVCacheDtype::Q4_1:
      return GGML_TYPE_Q4_1;
    case LlamaKVCacheDtype::Q4_0:
      return GGML_TYPE_Q4_0;
    default:
      MOZ_ASSERT_UNREACHABLE("Unhandled LlamaKVCacheQuantizationLevel");
      break;
  }
  return GGML_TYPE_F16;
}

// Whether GPU offload can actually be used on this machine. Some configurations
// register a GPU device that then fails during real backend initialization
// (e.g. Metal on CI VMs or older Intel Macs, where the device exists but the
// command queue / shader library can't be created). The vendored library is
// built without exceptions (see third_party/llama.cpp/moz-overrides.h), so
// letting llama_context hit that failure would abort instead of falling back.
// Probe the GPU backend once, up front, and treat it as unusable if there is no
// GPU device or if a real init attempt fails; callers then load on CPU.
static bool GpuOffloadUsable(const LlamaLibWrapper* aLib) {
  static const bool sUsable = [aLib]() {
    ggml_backend_dev_t dev =
        aLib->ggml_backend_dev_by_type(GGML_BACKEND_DEVICE_TYPE_GPU);
    if (!dev) {
      dev = aLib->ggml_backend_dev_by_type(GGML_BACKEND_DEVICE_TYPE_IGPU);
    }
    if (!dev) {
      return false;
    }
    // A real init: this is what llama_context does internally, and it is where
    // Metal actually fails on affected machines. ggml_backend_dev_init returns
    // null (it does not abort) on failure, so this is safe to probe.
    ggml_backend_t backend = aLib->ggml_backend_dev_init(dev, nullptr);
    if (!backend) {
      return false;
    }
    aLib->ggml_backend_free(backend);
    return true;
  }();
  return sUsable;
}

LlamaBackend::~LlamaBackend() {
  LOGD("Entered {}", __PRETTY_FUNCTION__);
  // Note: mLib is not freed here because LlamaRuntimeLinker manages
  // the library lifetime as a singleton
}

ResultStatus LlamaBackend::Reinitialize(const LlamaModelOptions& aOptions,
                                        FILE* aFp) {
  LOGV("Entered {}", __PRETTY_FUNCTION__);

  if (!mLib) {
    mLib = LlamaRuntimeLinker::Get();
  }

  if (!mLib) {
    auto msg = nsFmtCString("{}: Failed to get llama runtime linker",
                            __PRETTY_FUNCTION__);
    LOGE("{}", msg);
    return mozilla::Err(Error{std::move(msg)});
  }

  mModelOptions = aOptions;

  LOGV("{}: Initializing the model", __PRETTY_FUNCTION__);

  // initialize the model
  llama_model_params modelParams = mLib->llama_model_default_params();
  modelParams.n_gpu_layers = aOptions.mNGpuLayers;
  if (modelParams.n_gpu_layers > 0 && !GpuOffloadUsable(mLib)) {
    LOGW(
        "GPU offload was requested but no usable GPU backend is available "
        "(none "
        "present, or it failed to initialize); falling back to CPU.");
    modelParams.n_gpu_layers = 0;
  }
  modelParams.use_mmap = aOptions.mUseMmap;
  modelParams.use_mlock = aOptions.mUseMlock;
  modelParams.check_tensors = aOptions.mCheckTensors;

  mModel.reset(mLib->llama_model_load_from_file_handle(aFp, modelParams));

  if (!mModel) {
    auto msg =
        nsFmtCString("{}: Unable to load the model during initialization",
                     __PRETTY_FUNCTION__);
    LOGE("{}", msg);
    return mozilla::Err(Error{std::move(msg)});
  }

  // Preallocate buffer based on rough estimate of the size of the name
  mModelGeneralName.SetLength(256);
  auto numWritten = mLib->llama_model_meta_val_str(
      mModel.get(), "general.name", mModelGeneralName.BeginWriting(),
      mModelGeneralName.Length());
  if (numWritten >= 0) {
    // Reset the length to the number of actual bytes written
    mModelGeneralName.SetLength(numWritten);
    mModelGeneralName.Insert("Model: "_ns, 0);
  } else {
    LOGW(
        "Unable to extract the name of the model. Logging message will not "
        "include it.");
    mModelGeneralName = ""_ns;
  }

  LOGV("{}: Successfully Initialized {}", __PRETTY_FUNCTION__,
       mModelGeneralName);

  return ReinitializeContext(aOptions.mContext, aOptions.mContext.mNCtx);
}

ResultStatus LlamaBackend::ReinitializeContext(
    const LlamaContextOptions& aOptions, int aNumContext) {
  LOGV("Entered {}", __PRETTY_FUNCTION__);

  MOZ_ASSERT(mLib,
             "No shared library pointer in ReinitializeContext, fix this");

  llama_context_params ctxParams = mLib->llama_context_default_params();

  ctxParams.n_ctx = aNumContext;

  ctxParams.n_batch = aOptions.mNBatch;
  ctxParams.n_ubatch = aOptions.mNUbatch;
  ctxParams.n_seq_max = aOptions.mNSeqMax;
  ctxParams.n_threads = aOptions.mNThreads;
  ctxParams.n_threads_batch = aOptions.mNThreadsBatch;

  ctxParams.type_k = GgmlTypeFromKVCacheDtype(aOptions.mKCacheDtype);
  ctxParams.type_v = GgmlTypeFromKVCacheDtype(aOptions.mVCacheDtype);
  ctxParams.offload_kqv = aOptions.mOffloadKqv;
  ctxParams.flash_attn_type = aOptions.mFlashAttn
                                  ? LLAMA_FLASH_ATTN_TYPE_ENABLED
                                  : LLAMA_FLASH_ATTN_TYPE_DISABLED;
  ctxParams.no_perf = aOptions.mNoPerf;
  ctxParams.op_offload = aOptions.mOpOffload;
  ctxParams.swa_full = aOptions.mSwaFull;

  // llama_init_from_model does not take ownership of the model or context
  // parameters. We retain ownership of all objects passed in and must keep them
  // alive for the entire lifetime of mCtx.
  mCtx.reset(mLib->llama_init_from_model(mModel.get(), ctxParams));
  if (!mCtx) {
    auto msg = nsFmtCString("{}: failed to create the llama_context {}",
                            __PRETTY_FUNCTION__, mModelGeneralName);
    LOGE("{}", msg);
    return mozilla::Err(Error{std::move(msg)});
  }

  // ggml_threadpool_params_init/ggml_threadpool_new do not retain pointers
  // to the params structs. We own those stack-allocated structs, but they can
  // safely go out of scope after threadpool creation.
  ggml_threadpool_params tpp;
  mLib->ggml_threadpool_params_init(&tpp, ctxParams.n_threads);
  tpp.thread_create_callback = []() { PROFILER_REGISTER_THREAD("llama.cpp"); };
  tpp.thread_destroy_callback = []() { PROFILER_UNREGISTER_THREAD(); };

  ggml_threadpool_params tppBatch;
  mLib->ggml_threadpool_params_init(&tppBatch, ctxParams.n_threads_batch);
  tppBatch.thread_create_callback = []() {
    PROFILER_REGISTER_THREAD("llama.cpp");
  };
  tppBatch.thread_destroy_callback = []() { PROFILER_UNREGISTER_THREAD(); };

  mThreadpoolBatch.reset();
  if (!mLib->ggml_threadpool_params_match(&tpp, &tppBatch)) {
    mThreadpoolBatch.reset(mLib->ggml_threadpool_new(&tppBatch));
    if (!mThreadpoolBatch) {
      auto msg = nsFmtCString(
          "{}: Failed to create decoding threadpool: n_threads: {}  {}",
          __PRETTY_FUNCTION__, ctxParams.n_threads_batch, mModelGeneralName);
      LOGE("{}", msg);
      return mozilla::Err(Error{std::move(msg)});
    }
    // Start the non-batch threadpool in the paused state
    tpp.paused = true;
  }

  mThreadpool.reset(mLib->ggml_threadpool_new(&tpp));
  if (!mThreadpool) {
    auto msg = nsFmtCString("{}: Failed to create threadpool: n_threads: {} {}",
                            __PRETTY_FUNCTION__, ctxParams.n_threads,
                            mModelGeneralName);
    LOGE("{}", msg);
    return mozilla::Err(Error{std::move(msg)});
  }

  // llama_attach_threadpool does not take ownership of the threadpools.
  // We must keep mThreadpool and mThreadpoolBatch alive for as long as
  // mCtx is used, since they are accessed internally by the context.
  mLib->llama_attach_threadpool(mCtx.get(), mThreadpool.get(),
                                mThreadpoolBatch.get());

  LOGV("{}: Successfully Initialized context for model {}", __PRETTY_FUNCTION__,
       mModelGeneralName);

  return mozilla::Ok();
}

ChatMessageResult LlamaBackend::FormatChat(
    const mozilla::dom::LlamaFormatChatOptions& aOptions) {
  LOGV("Entered {}", __PRETTY_FUNCTION__);

  MOZ_ASSERT(mLib, "No shared library pointer in FormatChat, fix this");

  if (!mModel) {
    auto msg = nsFmtCString("{}: Model not loaded when trying to format chat",
                            __PRETTY_FUNCTION__);
    LOGE("{}", msg);
    return ChatMessageResult(Error{std::move(msg)});
  }

  nsTArray<llama_chat_message> chatMessages;
  chatMessages.SetCapacity(aOptions.mMessages.Length());
  for (const auto& msg : aOptions.mMessages) {
    // llama_chat_message is a simple struct that stores raw `const char*`
    // pointers. It does not take ownership of the strings — the caller must
    // ensure that msg.mRole and msg.mContent outlive any use of chatMessages.
    // Here, we build it on the stack, so the pointers are valid during the
    // function call.
    chatMessages.AppendElement(
        llama_chat_message{msg.mRole.get(), msg.mContent.get()});
  }

  // This returns a pointer to a template string stored inside mModel.
  // Since we own mModel, the pointer remains valid throughout this function.
  const char* tmpl =
      mLib->llama_model_chat_template(mModel.get(), /* name */ nullptr);

  int32_t numCharInMessages = 0;
  for (const auto& msg : aOptions.mMessages) {
    numCharInMessages += msg.mRole.Length() + msg.mContent.Length();
  }
  numCharInMessages *= 2;
  LOGD("{}: Estimated number of chars {}, for the formatted message {}",
       numCharInMessages, __PRETTY_FUNCTION__, mModelGeneralName);

  // Preallocate buffer based on rough estimate
  nsCString formatted;
  formatted.SetLength(numCharInMessages);

  // llama_chat_apply_template does not retain any of the pointers passed to it.
  // All data must remain valid only during the call.
  int32_t chatTemplateLength = mLib->llama_chat_apply_template(
      tmpl, chatMessages.Elements(), chatMessages.Length(),
      aOptions.mAddAssistant, formatted.BeginWriting(), formatted.Length());

  if (chatTemplateLength < 0) {
    auto msg = nsFmtCString("{}: failed to apply the chat template",
                            __PRETTY_FUNCTION__);
    LOGE("{}", msg);
    return ChatMessageResult(Error{std::move(msg)});
  }

  // Retry if the estimated buffer size was too small
  if (mozilla::AssertedCast<size_t>(chatTemplateLength) > formatted.Length()) {
    LOGD(
        "{}: Wrong estimate for format chat. Retrying with the correct value "
        "{}, {}",
        __PRETTY_FUNCTION__, chatTemplateLength, mModelGeneralName);

    formatted.SetLength(chatTemplateLength);
    chatTemplateLength = mLib->llama_chat_apply_template(
        tmpl, chatMessages.Elements(), chatMessages.Length(),
        aOptions.mAddAssistant, formatted.BeginWriting(), formatted.Length());
  }

  // Trim to actual size returned by llama
  formatted.SetLength(chatTemplateLength);

  LOGD("{}: Successfully formatted chat", __PRETTY_FUNCTION__);

  return ChatMessageResult(std::move(formatted));
}

LlamaBackend::SamplerResult LlamaBackend::InitializeSampler(
    const mozilla::dom::Sequence<LlamaSamplerConfig>& aSamplers,
    const llama_vocab* vocab) {
  LOGV("Entered {}", __PRETTY_FUNCTION__);

  MOZ_ASSERT(mLib, "No shared library pointer in InitializeSampler, fix this");

  // initialize the sampler. We own the pointer returned by
  // llama_sampler_chain_init. sampler_element pointer are added to sampler by
  // llama_sampler_chain_add and we need to delete them

  LlamaSamplerUPtr sampler(mLib->llama_sampler_chain_init(
      mLib->llama_sampler_chain_default_params()));

  // Default to greedy decoding when no samplers are specified.
  // This is a standard inference strategy that requires no tuning
  // and produces deterministic outputs. Often use for Summarization, QA and
  // RAG.
  if (aSamplers.IsEmpty()) {
    mLib->llama_sampler_chain_add(sampler.get(),
                                  mLib->llama_sampler_init_greedy());
  }

  auto n_vocab = mLib->llama_vocab_n_tokens(vocab);

  for (const auto& samplerConfig : aSamplers) {
    llama_sampler* samplerElement = nullptr;

    switch (samplerConfig.mType) {
      case LlamaSamplerType::Temperature:
        samplerElement = mLib->llama_sampler_init_temp(samplerConfig.mTemp);
        break;

      case LlamaSamplerType::Dist: {
        auto seed = samplerConfig.mSeed.WasPassed()
                        ? samplerConfig.mSeed.Value()
                        : LLAMA_DEFAULT_SEED;
        samplerElement = mLib->llama_sampler_init_dist(seed);
        break;
      }

      case LlamaSamplerType::Top_k:
        samplerElement = mLib->llama_sampler_init_top_k(samplerConfig.mTopK);
        break;

      case LlamaSamplerType::Top_p:
        samplerElement = mLib->llama_sampler_init_top_p(samplerConfig.mTopP,
                                                        samplerConfig.mMinKeep);
        break;

      case LlamaSamplerType::Logit_bias: {
        nsTArray<llama_logit_bias> logitBias;
        logitBias.SetCapacity(samplerConfig.mLogitBias.Length());
        for (const auto& val : samplerConfig.mLogitBias) {
          logitBias.AppendElement(llama_logit_bias{val.mToken, val.mBias});
        }

        samplerElement = mLib->llama_sampler_init_logit_bias(
            n_vocab, samplerConfig.mLogitBias.Length(), logitBias.Elements());
        break;
      }

      default:

        auto msg =
            nsFmtCString("{}: Unimplemented sampler type", __PRETTY_FUNCTION__);
        LOGE("{}", msg);
        return mozilla::Err(Error{std::move(msg)});
    }

    if (samplerElement) {
      mLib->llama_sampler_chain_add(sampler.get(), samplerElement);
    }
  }

  LOGV("{} Sampler successfully initialized.", __PRETTY_FUNCTION__);
  return SamplerResult(std::move(sampler));
}

ResultStatus LlamaBackend::Generate(
    const LlamaChatOptions& aOptions,
    std::function<ResultStatus(const LlamaChatResponse&)> aTokenCallback,
    std::function<bool()> aCancelCallback) {
  LOGV("Entered {}", __PRETTY_FUNCTION__, mModelGeneralName);

  MOZ_ASSERT(mLib, "No shared library pointer in Generate, fix this");

  auto cleanup = mozilla::MakeScopeExit([&ctx = mCtx, lib = mLib] {
    // clear the memory used by this generation session to make it ready for the
    // next session
    bool clearDataBuffers = true;
    if (ctx) {
      lib->llama_memory_clear(lib->llama_get_memory(ctx.get()),
                              clearDataBuffers);
    }
  });

  if (!mModel) {
    auto msg = nsFmtCString("{}: error: Model not loaded", __PRETTY_FUNCTION__);
    LOGE("{}", msg);
    return mozilla::Err(Error{std::move(msg)});
  }

  // Just a non-owned pointer to existing data, so fast to get each time
  const llama_vocab* vocab = mLib->llama_model_get_vocab(mModel.get());

  if (!vocab) {
    auto msg = nsFmtCString("{}: error: Unable to get model vocabulary.",
                            __PRETTY_FUNCTION__);
    LOGE("{}", msg);
    return mozilla::Err(Error{std::move(msg)});
  }

  auto samplerResult = InitializeSampler(aOptions.mSamplers, vocab);

  if (samplerResult.isErr()) {
    LOGE("{}", samplerResult.inspectErr().mMessage);
    return mozilla::Err(samplerResult.inspectErr());
  }

  auto sampler = samplerResult.unwrap();

  const size_t estimatedNumPromptTokens = aOptions.mPrompt.Length() + 1;
  LOGD("{} Estimated tokenization size is {} {}", __PRETTY_FUNCTION__,
       estimatedNumPromptTokens, mModelGeneralName);
  nsTArray<llama_token> promptTokens;
  promptTokens.SetLength(estimatedNumPromptTokens);
  // attempt to tokenize the prompt using the estimated number of prompts
  int32_t nPromptTokens = mLib->llama_tokenize(
      vocab, aOptions.mPrompt.get(), aOptions.mPrompt.Length(),
      promptTokens.Elements(), promptTokens.Length(),
      aOptions.mTokenizationOptions.mAddBosAndEos,
      aOptions.mTokenizationOptions.mParseSpecilControlTokens);

  if (nPromptTokens < 0) {
    auto msg = nsFmtCString("{}: failed to tokenize the prompt {}",
                            __PRETTY_FUNCTION__, mModelGeneralName);
    LOGE("{}", msg);
    return mozilla::Err(Error{std::move(msg)});
  }

  // If the estimate was wrong, retry with the correct number
  if (mozilla::AssertedCast<size_t>(nPromptTokens) > estimatedNumPromptTokens) {
    LOGD(
        "{} Wrong estimate for tokenization. Retrying with the correct "
        "value {} {}",
        __PRETTY_FUNCTION__, nPromptTokens, mModelGeneralName);
    promptTokens.SetLength(nPromptTokens);

    nPromptTokens = mLib->llama_tokenize(
        vocab, aOptions.mPrompt.get(), aOptions.mPrompt.Length(),
        promptTokens.Elements(), promptTokens.Length(),
        aOptions.mTokenizationOptions.mAddBosAndEos,
        aOptions.mTokenizationOptions.mParseSpecilControlTokens);
  }

  promptTokens.SetLength(nPromptTokens);

  auto seqLen = aOptions.mMaxGeneratedTokens;

  int nCtx = mLib->llama_n_ctx(mCtx.get());

  int estimatedCtx = nPromptTokens + seqLen;

  if (estimatedCtx > nCtx) {
    LOGW(
        "{} We estimated that {} will be needed but the model context was "
        "initialized "
        "with {}. Re-initializing the model context {}",
        __PRETTY_FUNCTION__, estimatedCtx, nCtx, mModelGeneralName);

    auto initContextResult =
        ReinitializeContext(mModelOptions.mContext, estimatedCtx);
    if (initContextResult.isErr()) {
      LOGE("{}", initContextResult.inspectErr().mMessage);
      return initContextResult;
    }

    nCtx = mLib->llama_n_ctx(mCtx.get());

    mModelOptions.mContext.mNCtx = estimatedCtx;
  }

  LOGD("{} Creating llama.cpp batch from prompt tokens for {}",
       __PRETTY_FUNCTION__, mModelGeneralName);
  llama_batch batch =
      mLib->llama_batch_get_one(promptTokens.Elements(), promptTokens.Length());
  // To keep track of the id of the currently generated token
  llama_token token;

  int32_t numGeneratedTokens = 0;

  // To sample the next token, we need the logits corresponding to one of the
  // previously generated tokens.
  // Specifically, we use the logits of the most recently generated token
  // (i.e., the last one).
  const int32_t lastTokenLogitIndex = -1;
  bool promptProcessingDone = false;

  // The stop tokens are user inputs and could be very big.
  // We store them in a hash set to bound the time complexity of the membership
  // check. Typically, this is just empty.
  mozilla::HashSet<int32_t> stopTokens;
  for (const auto& v : aOptions.mStopTokens) {
    if (!stopTokens.put(v)) {
      auto msg = nsFmtCString("{}: Unable to create stopTokens {}",
                              __PRETTY_FUNCTION__, mModelGeneralName);
      LOGE("{}", msg);
      return mozilla::Err(Error{std::move(msg)});
    };
  }

  auto onToken = [aTokenCallback](nsCString piece, nsTArray<int32_t> tokens,
                                  LlamaChatPhase phase, bool completed) {
    LlamaChatResponse result;
    result.mPiece = std::move(piece);
    result.mTokens = std::move(tokens);
    result.mPhase = phase;
    result.mIsPhaseCompleted = completed;
    return aTokenCallback(result);
  };
  while (true) {
    LOGV("{}: New iteration started {}", __PRETTY_FUNCTION__,
         mModelGeneralName);
    if (aCancelCallback()) {
      LOGD("{} Generation successfully cancelled. {}", __PRETTY_FUNCTION__,
           mModelGeneralName);
      return mozilla::Ok();
    }

    // check if we have enough space in the context to evaluate this batch
    int nCtxUsed =
        mLib->llama_memory_seq_pos_max(mLib->llama_get_memory(mCtx.get()), 0);
    if (nCtxUsed + batch.n_tokens > nCtx) {
      auto msg =
          nsFmtCString("{}: context size exceeded. Size is: {} Needed: {} {}",
                       __PRETTY_FUNCTION__, nCtx, nCtxUsed + batch.n_tokens,
                       mModelGeneralName);
      LOGE("{}", msg);
      return mozilla::Err(Error{std::move(msg)});
    }

    LOGV("{}: Decoding to generate next token probabilities {}",
         __PRETTY_FUNCTION__, mModelGeneralName);

    if (mLib->llama_decode(mCtx.get(), batch) != 0) {
      auto msg = nsFmtCString("{}: failed to decode {}", __PRETTY_FUNCTION__,
                              mModelGeneralName);
      LOGE("{}", msg);
      return mozilla::Err(Error{std::move(msg)});
    }

    LOGV("{}: Sampling the generated probabilities to generate next token {}",
         __PRETTY_FUNCTION__, mModelGeneralName);
    token = mLib->llama_sampler_sample(sampler.get(), mCtx.get(),
                                       lastTokenLogitIndex);

    // Sending the end of prompt once we get the first token to ensure
    // all operations are indeed completed.
    // Otherwise we would need a call to llama_synchronize(ctx)
    if (!promptProcessingDone) {
      promptProcessingDone = true;

      LOGD("{}: Prompt processing successfully completed {}",
           __PRETTY_FUNCTION__, mModelGeneralName);

      // move the promptTokens as we don't need it anymore
      auto onTokenResult = onToken(aOptions.mPrompt, std::move(promptTokens),
                                   LlamaChatPhase::Prompt, true);
      if (onTokenResult.isErr()) {
        LOGE("{}", onTokenResult.inspectErr().mMessage);
        return onTokenResult;
      }
    }

    // Check if the current token signals the end of generation:
    // - Case 1: The token is an End-of-Generation (EOG) token and stopping on
    // EOG is enabled.
    // - Case 2: The token matches any user-defined stop token.
    // If either condition is true, stop generation early.
    LOGV("{}: Checking if end of generation reached {}", __PRETTY_FUNCTION__,
         mModelGeneralName);
    if ((mLib->llama_vocab_is_eog(vocab, token) &&
         aOptions.mStopOnEndOfGenerationTokens) ||
        stopTokens.has(token)) {
      LOGD("{}: Early stopping {}", __PRETTY_FUNCTION__, mModelGeneralName);
      break;
    }

    LOGV("{}: Converting the generated token to a string {}",
         __PRETTY_FUNCTION__, mModelGeneralName);
    nsCString buffer;
    buffer.SetLength(aOptions.mDeTokenizationOptions.mMaxCharsPerToken);
    // write in the buffer from index 0
    int32_t lstrip = 0;
    auto n = mLib->llama_token_to_piece(
        vocab, token, buffer.BeginWriting(), buffer.Length(), lstrip,
        aOptions.mDeTokenizationOptions.mRenderSpecialTokens);

    if (n < 0) {
      auto msg = nsFmtCString("{}: failed to convert token to string piece {}",
                              __PRETTY_FUNCTION__, mModelGeneralName);
      LOGE("{}", msg);
      return mozilla::Err(Error{std::move(msg)});
    }
    buffer.SetLength(n);

    LOGV("{}: Sending the generated token to the callback {}",
         __PRETTY_FUNCTION__, mModelGeneralName);
    auto onTokenResult =
        onToken(std::move(buffer), {token}, LlamaChatPhase::Generation, false);
    if (onTokenResult.isErr()) {
      LOGE("{}", onTokenResult.inspectErr().mMessage);
      return onTokenResult;
    }

    LOGV("{}: Checking if desired number of tokens reached {}",
         __PRETTY_FUNCTION__, mModelGeneralName);
    numGeneratedTokens++;
    if (numGeneratedTokens >= aOptions.mMaxGeneratedTokens) {
      LOGD(
          "{} Maximum number of generation tokens reached. Stopping the "
          "generation",
          __PRETTY_FUNCTION__);
      break;
    }

    LOGV("{}: Preparing the next batch with the sampled token {}",
         __PRETTY_FUNCTION__, mModelGeneralName);
    batch = mLib->llama_batch_get_one(&token, 1);
  }

  LOGV("{}: Sending end of generation to callback {}", __PRETTY_FUNCTION__,
       mModelGeneralName);
  auto onTokenResult = onToken({}, {}, LlamaChatPhase::Generation, true);
  if (onTokenResult.isErr()) {
    LOGE("{}", onTokenResult.inspectErr().mMessage);
    return onTokenResult;
  }

  LOGD("{} LlamaBackend generation successfully complete {}",
       __PRETTY_FUNCTION__, mModelGeneralName);

  return mozilla::Ok();
}

}  // namespace mozilla::llama
