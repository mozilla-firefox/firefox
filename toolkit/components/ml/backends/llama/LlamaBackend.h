/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef mozilla_llama_backend_h
#define mozilla_llama_backend_h

#include <functional>
#include "LlamaRuntimeLinker.h"
#include "ggml.h"
#include "ggml-cpu.h"
#include "mozilla/dom/LlamaRunnerBinding.h"
#include "mozilla/Result.h"
#include "mozilla/ResultVariant.h"
#include "mozilla/UniquePtr.h"

namespace mozilla::llama {

struct Error {
  nsCString mMessage;
};

using ChatMessageResult = mozilla::Result<nsCString, Error>;
using ResultStatus = mozilla::Result<mozilla::Ok, Error>;
using LlamaChatResponse = mozilla::dom::LlamaChatResponse;
using LlamaChatPhase = mozilla::dom::LlamaChatPhase;
using LlamaModelOptions = mozilla::dom::LlamaModelOptions;
using LlamaKVCacheDtype = mozilla::dom::LlamaKVCacheDtype;
using LlamaChatOptions = mozilla::dom::LlamaChatOptions;
using LlamaSamplerType = mozilla::dom::LlamaSamplerType;
using LlamaContextOptions = mozilla::dom::LlamaContextOptions;
using LlamaSamplerConfig = mozilla::dom::LlamaSamplerConfig;

ggml_type GgmlTypeFromKVCacheDtype(LlamaKVCacheDtype aDtype);

// LlamaBackend is a low-level, internal interface to the llama.cpp engine.
// It encapsulates model loading, prompt formatting, context setup, and
// token-by-token generation with streaming callbacks.
//
// This class is **not** exposed to JS or WebIDL — it's intended for internal
// orchestration only, typically via LlamaRunner or LlamaGenerateTask.
//
// Usage Pattern:
// 1. Construct the class using the default constructor.
// 2. Call `Reinitialize(...)` to load a model.
// 3. (Optional) Use `FormatChat(...)` to build a prompt from chat messages.
// 4. Call `Generate(...)` to start streaming token output.
// 5. To update **context-only** settings (not the model), call
// `ReinitializeContext(...)`.
// 6. To update both the model and context, call `Reinitialize(...)`.
//
// At the moment, this class is not thread-safe, as it holds a reference to the
// context state `mCtx`, `mThreadpool`, `mThreadpoolBatch` instead of
// re-initializing them at each generation.
class LlamaBackend {
 public:
  NS_INLINE_DECL_THREADSAFE_REFCOUNTING(LlamaBackend)

  // Default constructor. Call Reinitialize before use.
  LlamaBackend() = default;

  // Reinitializes the entire backend (model + context).
  // Use this if you need to load a different model.
  ResultStatus Reinitialize(const LlamaModelOptions& aOptions, FILE* aFp);

  // Converts structured chat messages into a flat prompt string.
  // Useful for models expecting a plain-text prompt.
  ChatMessageResult FormatChat(
      const mozilla::dom::LlamaFormatChatOptions& aOptions);

  // Generates a sequence of tokens using the current model/context.
  // Calls `aTokenCallback` with each token; supports early termination via
  // `aCancelCallback`. The generation has the prompt phase followed by the
  // generation phase.  Message sent to aTokenCallback
  //  will have an identifier for each phase along with a boolean
  // indicating if the phase is completed. Note that the callbacks are called
  // synchronously. `Reinitialize` must be call at least once before calling
  // this function.
  ResultStatus Generate(
      const LlamaChatOptions& aOptions,
      std::function<ResultStatus(const LlamaChatResponse&)> aTokenCallback,
      std::function<bool()> aCancelCallback);

  // Reinitializes the context only (same model).
  // Use this to change generation parameters like context size, temperature,
  // etc.
  ResultStatus ReinitializeContext(const LlamaContextOptions& aOptions,
                                   int aNumContext);

  // Custom deleters for managing llama.cpp and ggml resources
  struct GgmlThreadpoolDeleter {
    void operator()(struct ggml_threadpool* aTp) const;
  };

  struct LlamaModelDeleter {
    void operator()(llama_model* aModel) const;
  };

  struct LlamaContextDeleter {
    void operator()(llama_context* aCtx) const;
  };

  struct LlamaSamplerDeleter {
    void operator()(llama_sampler* aSmpl) const;
  };

  // Smart pointer types for safe resource cleanup
  using GgmlThreadpoolUPtr =
      mozilla::UniquePtr<struct ggml_threadpool, GgmlThreadpoolDeleter>;
  using LlamaModelUPtr = mozilla::UniquePtr<llama_model, LlamaModelDeleter>;
  using LlamaContextUPtr =
      mozilla::UniquePtr<llama_context, LlamaContextDeleter>;
  using LlamaSamplerUPtr =
      mozilla::UniquePtr<llama_sampler, LlamaSamplerDeleter>;

  // sampler result alias
  using SamplerResult = mozilla::Result<LlamaSamplerUPtr, Error>;

  // Protected because RefPtr requires a non-public destructor
 protected:
  ~LlamaBackend();

 private:
  SamplerResult InitializeSampler(
      const mozilla::dom::Sequence<LlamaSamplerConfig>& aSamplers,
      const llama_vocab* vocab);

  // Pointer to the dynamically loaded llama library
  LlamaLibWrapper* mLib = nullptr;

  // Holds the model data. Initialized once & reused across generation sessions.
  LlamaModelUPtr mModel;

  // Generation context. Initialized once & reused across generation sessions.
  // However, it is automatically re-initialized if the configured
  // context length is lower than the required number for a generation session.
  LlamaContextUPtr mCtx;

  // Threadpool used for processing single tokens, usually during decoding.
  // Initialized once & reused across generation sessions.
  GgmlThreadpoolUPtr mThreadpool;

  // Used for processing multiple tokens at a time during prompt processing.
  // Initialized once & reused across generation session. This uses mThreadpool
  // if the configured number of threads are identical.
  GgmlThreadpoolUPtr mThreadpoolBatch;

  LlamaModelOptions mModelOptions;

  // Cached model name/info string for logging (e.g. "Smollm2 360M 8k ...")
  nsCString mModelGeneralName;
};

// Alias for shared pointer type
using LlamaBackendSPtr = RefPtr<LlamaBackend>;

}  // namespace mozilla::llama

#endif
