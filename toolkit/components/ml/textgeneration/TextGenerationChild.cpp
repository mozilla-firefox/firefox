/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "TextGenerationChild.h"

#include <algorithm>
#include <cstdio>
#include <limits>
#include <memory>

#include "mozilla/hwinference/HWInferenceLog.h"
#include "mozilla/llama/LlamaBackend.h"
#include "mozilla/CheckedInt.h"
#include "mozilla/Encoding.h"
#include "mozilla/Logging.h"
#include "mozilla/ProcInfo.h"
#include "mozilla/ScopeExit.h"
#include "mozilla/Span.h"
#include "mozilla/TimeStamp.h"
#include "mozilla/UniquePtrExtensions.h"
#include "mozilla/Utf8.h"
#include "mozilla/dom/BindingUtils.h"
#include "mozilla/ml/MLProfilerMarkers.h"
#include "nsThreadUtils.h"

#ifdef XP_WIN
#  include <fcntl.h>
#  include <io.h>
#endif

namespace mozilla::hwinference {

#define LOGD(fmt, ...) \
  MOZ_LOG_FMT(gHWInferenceLog, LogLevel::Debug, fmt, ##__VA_ARGS__)

using dom::LlamaChatMessage;
using dom::LlamaChatOptions;
using dom::LlamaChatPhase;
using dom::LlamaChatResponse;
using dom::LlamaFormatChatOptions;
using dom::LlamaKVCacheDtype;
using dom::LlamaModelOptions;
using dom::LlamaSamplerConfig;
using dom::LlamaSamplerType;
using dom::TextGenerationFinishReason;

static LlamaSamplerType ToLlamaSamplerType(
    dom::TextGenerationSamplerType aType) {
  switch (aType) {
    case dom::TextGenerationSamplerType::Logit_bias:
      return LlamaSamplerType::Logit_bias;
    case dom::TextGenerationSamplerType::Top_k:
      return LlamaSamplerType::Top_k;
    case dom::TextGenerationSamplerType::Top_p:
      return LlamaSamplerType::Top_p;
    case dom::TextGenerationSamplerType::Temperature:
      return LlamaSamplerType::Temperature;
    case dom::TextGenerationSamplerType::Dist:
      return LlamaSamplerType::Dist;
  }
  MOZ_CRASH("bad TextGenerationSamplerType");
}

static LlamaKVCacheDtype ToLlamaKVCacheDtype(
    dom::TextGenerationKVCacheDtype aDtype) {
  switch (aDtype) {
    case dom::TextGenerationKVCacheDtype::F32:
      return LlamaKVCacheDtype::F32;
    case dom::TextGenerationKVCacheDtype::F16:
      return LlamaKVCacheDtype::F16;
    case dom::TextGenerationKVCacheDtype::Q8_0:
      return LlamaKVCacheDtype::Q8_0;
    case dom::TextGenerationKVCacheDtype::Q5_1:
      return LlamaKVCacheDtype::Q5_1;
    case dom::TextGenerationKVCacheDtype::Q5_0:
      return LlamaKVCacheDtype::Q5_0;
    case dom::TextGenerationKVCacheDtype::Q4_1:
      return LlamaKVCacheDtype::Q4_1;
    case dom::TextGenerationKVCacheDtype::Q4_0:
      return LlamaKVCacheDtype::Q4_0;
  }
  MOZ_CRASH("bad TextGenerationKVCacheDtype");
}

static LlamaFormatChatOptions ToFormatChatOptions(
    const nsTArray<ChatMessage>& aHistory,
    const nsTArray<ChatMessage>& aMessages) {
  LlamaFormatChatOptions options;
  auto append = [&](const nsTArray<ChatMessage>& aSource) {
    for (const ChatMessage& message : aSource) {
      LlamaChatMessage* entry = options.mMessages.AppendElement(fallible);
      MOZ_RELEASE_ASSERT(entry);
      entry->mRole = dom::GetEnumString(message.role());
      entry->mContent = message.content();
    }
  };
  append(aHistory);
  append(aMessages);
  return options;
}

static uint32_t CountCodePoints(const nsACString& aUtf8) {
  uint32_t count = 0;
  const char* cursor = aUtf8.BeginReading();
  const char* end = aUtf8.EndReading();
  while (cursor != end) {
    const Utf8Unit lead(*cursor++);
    if (!IsAscii(lead)) {
      (void)LossyDecodeOneUtf8CodePoint(lead, &cursor, end);
    }
    ++count;
  }
  return count;
}

static LlamaModelOptions ToLlamaModelOptions(
    const TextGenerationOptions& aOptions) {
  LlamaModelOptions modelOptions;
  // mmap of a descriptor needs syscalls the utility sandbox denies.
  modelOptions.mUseMmap = false;
  modelOptions.mUseMlock = false;
  modelOptions.mContext.mNCtx = aOptions.contextSize();
  modelOptions.mContext.mNBatch = aOptions.batchSize();
  modelOptions.mContext.mNUbatch = aOptions.ubatchSize();
  modelOptions.mContext.mNThreads =
      static_cast<int32_t>(aOptions.numThreadsDecoding());
  modelOptions.mContext.mNThreadsBatch =
      static_cast<int32_t>(aOptions.numThreads());
  modelOptions.mContext.mKCacheDtype =
      ToLlamaKVCacheDtype(aOptions.kvCacheDtype());
  modelOptions.mContext.mVCacheDtype =
      ToLlamaKVCacheDtype(aOptions.kvCacheDtype());
  modelOptions.mContext.mFlashAttn = aOptions.flashAttn();
  // ggml sends big matmuls to a registered GPU device even at n_gpu_layers 0,
  // which changes results. Make tweakable when we offer GPU inference.
  modelOptions.mContext.mOpOffload = false;
  return modelOptions;
}

// The budget is an unsigned long on the surface, but an int32_t in llama.cpp,
// which sizes the context to hold the prompt plus the whole budget. Left
// unbounded it wraps negative -- ending the run after one token -- and asks
// for a context no allocation can serve. A generation cannot outrun its own
// context, so that is the bound; contextSize 0 means the model's default.
static uint32_t EffectiveMaxTokens(const GenerateRequest& aRequest,
                                   uint32_t aContextSize) {
  static constexpr uint32_t kMax =
      static_cast<uint32_t>(std::numeric_limits<int32_t>::max());
  const uint32_t budget = std::min(aRequest.maxTokens(), kMax);
  return aContextSize ? std::min(budget, aContextSize) : budget;
}

static LlamaChatOptions ToLlamaChatOptions(const GenerateRequest& aRequest,
                                           uint32_t aMaxTokens,
                                           nsCString&& aFormattedPrompt) {
  LlamaChatOptions options;
  options.mPrompt = std::move(aFormattedPrompt);
  options.mMaxGeneratedTokens = static_cast<int32_t>(aMaxTokens);
  options.mStopOnEndOfGenerationTokens = aRequest.stopOnEndOfGenerationTokens();
  options.mMinOutputBufferSize = 1;
  for (int32_t stopToken : aRequest.stopTokens()) {
    MOZ_RELEASE_ASSERT(options.mStopTokens.AppendElement(stopToken, fallible));
  }
  for (const Sampler& sampler : aRequest.samplers()) {
    LlamaSamplerConfig* config = options.mSamplers.AppendElement(fallible);
    MOZ_RELEASE_ASSERT(config);
    config->mType = ToLlamaSamplerType(sampler.type());
    config->mTopK = sampler.topK();
    config->mTopP = sampler.topP();
    config->mTemp = sampler.temp();
    config->mSeed.Construct(sampler.seed());
    for (const LogitBias& bias : sampler.logitBias()) {
      dom::LlamaLogitBias* entry = config->mLogitBias.AppendElement(fallible);
      MOZ_RELEASE_ASSERT(entry);
      entry->mToken = bias.token();
      entry->mBias = bias.bias();
    }
  }
  return options;
}

// Keeps a partial trailing sequence inside `aDecoder` (bug 2043430).
[[nodiscard]] static bool AppendDecodedUtf8(Decoder& aDecoder,
                                            const nsACString& aBytes,
                                            nsACString& aOut) {
  Span<const uint8_t> src(
      reinterpret_cast<const uint8_t*>(aBytes.BeginReading()), aBytes.Length());
  CheckedInt<size_t> capacity = aDecoder.MaxUTF8BufferLength(src.Length());
  size_t base = aOut.Length();
  if (!capacity.isValid() ||
      !aOut.SetLength(base + capacity.value(), fallible)) {
    return false;
  }
  Span<uint8_t> dst(reinterpret_cast<uint8_t*>(aOut.BeginWriting()) + base,
                    capacity.value());
  aOut.SetLength(base + std::get<2>(aDecoder.DecodeToUTF8(src, dst, false)));
  return true;
}

static ResourceSnapshot SampleResources() {
  uint64_t cpuTimeMs = 0;
  if (NS_FAILED(GetCpuTimeSinceProcessStartInMs(&cpuTimeMs))) {
    cpuTimeMs = 0;
  }
  uint64_t memoryBytes = 0;
  if (NS_FAILED(GetCurrentProcessMemoryUsage(&memoryBytes))) {
    memoryBytes = 0;
  }
  return ResourceSnapshot(cpuTimeMs, memoryBytes);
}

// One Generate request; runs on TextGenerationChild::mGenerationThread.
class TextGenerationChild::Generation {
 public:
  NS_INLINE_DECL_THREADSAFE_REFCOUNTING(Generation);

  Generation(TextGenerationChild* aChild, GenerateRequest&& aRequest,
             GenerateResolver&& aResolve)
      : mChild(aChild),
        mRequest(std::move(aRequest)),
        mResolver(std::make_shared<GenerateResolver>(std::move(aResolve))),
        mBufferLength(std::max(1u, mRequest.bufferLength())),
        mMaxTokens(
            EffectiveMaxTokens(mRequest, aChild->mOptions.contextSize())),
        mDecoder(UTF_8_ENCODING->NewDecoderWithoutBOMHandling()) {}

  void Run() {
    MOZ_ASSERT(mChild->mGenerationThread->IsOnCurrentThread());
    ResourceSnapshot resourcesBefore = SampleResources();
    if (!mChild->mBackend) {
      Reply(GenerateResponse(GenerateError(mChild->mLoadError)));
      return;
    }

    // llama.cpp checks the budget only after decoding, sampling and
    // delivering a token, so a zero budget would generate one.
    if (mMaxTokens == 0) {
      Reply(GenerateResponse(
          GenerateResult(nsCString(), TextGenerationFinishReason::Length,
                         Usage(0, 0, 0, Timings(0.0, 0.0)),
                         ResourceUsage(resourcesBefore, SampleResources()))));
      return;
    }

    auto formatted = mChild->mBackend->FormatChat(
        ToFormatChatOptions(mChild->mHistory, mRequest.messages()));
    if (formatted.isErr()) {
      Reply(GenerateResponse(GenerateError(formatted.unwrapErr().mMessage)));
      return;
    }

    nsCString prompt = formatted.unwrap();
    mPromptCodePoints = CountCodePoints(prompt);

    mGenerateStart = TimeStamp::Now();
    auto status = mChild->mBackend->Generate(
        ToLlamaChatOptions(mRequest, mMaxTokens, std::move(prompt)),
        [self = RefPtr{this}](const LlamaChatResponse& aChunk) {
          return self->HandleChunk(aChunk);
        },
        [self = RefPtr{this}]() -> bool {
          return self->mCancelled || self->mChild->mShutdown;
        });
    Flush();
    if (status.isErr()) {
      Reply(GenerateResponse(GenerateError(status.unwrapErr().mMessage)));
      return;
    }

    TimeStamp end = TimeStamp::Now();
    const TimeStamp prefillEnd = mPrefillEnd.valueOr(end);
    TextGenerationFinishReason reason = ComputeFinishReason();

    const double prefillMs = (prefillEnd - mGenerateStart).ToMilliseconds();
    const double decodeMs = (end - prefillEnd).ToMilliseconds();

    PROFILER_MARKER(ML_TEXT_GENERATION_TRACK, ML_INFERENCE,
                    MarkerTiming::Interval(mGenerateStart, prefillEnd),
                    MLModelPrefillMarker, mPromptTokens,
                    ml::TokensPerSecond(mPromptTokens, prefillMs));
    if (mPrefillEnd.isSome()) {
      PROFILER_MARKER(ML_TEXT_GENERATION_TRACK, ML_INFERENCE,
                      MarkerTiming::Interval(prefillEnd, end),
                      MLModelDecodeMarker, mGeneratedTokens,
                      ml::TokensPerSecond(mGeneratedTokens, decodeMs),
                      dom::GetEnumString(reason));
    }

    Timings timings(prefillMs, decodeMs);
    Reply(GenerateResponse(GenerateResult(
        mContent, reason,
        Usage(mPromptTokens, mPromptCodePoints, mGeneratedTokens, timings),
        ResourceUsage(resourcesBefore, SampleResources()))));
  }

  // Any thread.
  void RequestCancel() { mCancelled = true; }

 private:
  ~Generation() = default;

  llama::ResultStatus HandleChunk(const LlamaChatResponse& aChunk) {
    if (aChunk.mPhase == LlamaChatPhase::Prompt) {
      mPromptTokens += aChunk.mTokens.Length();
      if (aChunk.mIsPhaseCompleted) {
        mPrefillEnd = Some(TimeStamp::Now());
      }
      return Ok();
    }
    mGeneratedTokens += aChunk.mTokens.Length();
    if (!aChunk.mTokens.IsEmpty()) {
      mLastToken = aChunk.mTokens.LastElement();
    }
    if (!AppendDecodedUtf8(*mDecoder, aChunk.mPiece, mPendingDelta)) {
      return Err(
          llama::Error{"TextGenerator: failed to buffer decoded output"_ns});
    }
    mPendingTokens += aChunk.mTokens.Length();
    if (mPendingTokens >= mBufferLength) {
      Flush();
    }
    return Ok();
  }

  void Flush() {
    const uint32_t tokens = mPendingTokens;
    mPendingTokens = 0;
    if (mPendingDelta.IsEmpty()) {
      return;
    }
    nsCString delta = std::move(mPendingDelta);
    mPendingDelta.Truncate();
    mContent.Append(delta);
    PROFILER_MARKER(ML_TEXT_GENERATION_TRACK, ML_INFERENCE, {},
                    MLChunkSendMarker, tokens, uint32_t(delta.Length()));
    mChild->DispatchToActorThread("TextGenerationChild::Delta",
                                  [child = mChild, delta = std::move(delta)] {
                                    (void)child->SendDelta(delta);
                                  });
  }

  TextGenerationFinishReason ComputeFinishReason() const {
    if (mCancelled || mChild->mShutdown) {
      return TextGenerationFinishReason::Cancelled;
    }
    if (mGeneratedTokens >= mMaxTokens) {
      return TextGenerationFinishReason::Length;
    }
    if (mLastToken >= 0 && mRequest.stopTokens().Contains(mLastToken)) {
      return TextGenerationFinishReason::Stop_token;
    }
    return TextGenerationFinishReason::Eos;
  }

  void Reply(GenerateResponse&& aResponse) {
    MOZ_ASSERT(mChild->mGenerationThread->IsOnCurrentThread());
    // A generation that failed produced no turn, so it contributes nothing
    // to the history.
    if (aResponse.type() == GenerateResponse::TGenerateResult) {
      mChild->mHistory.AppendElements(std::move(mRequest.messages()));
    }
    mChild->DispatchToActorThread(
        "TextGenerationChild::GenerateReply",
        [self = RefPtr{this}, response = std::move(aResponse)]() mutable {
          (*self->mResolver)(std::move(response));
        });
  }

  const RefPtr<TextGenerationChild> mChild;
  GenerateRequest mRequest;
  const std::shared_ptr<GenerateResolver> mResolver;
  const uint32_t mBufferLength;
  const uint32_t mMaxTokens;
  const UniquePtr<Decoder> mDecoder;

  Atomic<bool> mCancelled{false};

  nsCString mContent;
  nsCString mPendingDelta;
  uint32_t mPendingTokens = 0;
  uint32_t mPromptTokens = 0;
  uint32_t mPromptCodePoints = 0;
  uint32_t mGeneratedTokens = 0;
  int32_t mLastToken = -1;
  TimeStamp mGenerateStart;
  Maybe<TimeStamp> mPrefillEnd;
};

static already_AddRefed<nsIThread> NewGeneratorThread() {
  nsCOMPtr<nsIThread> thread;
  MOZ_ALWAYS_SUCCEEDS(
      NS_NewNamedThread("TextGenerator", getter_AddRefs(thread)));
  return thread.forget();
}

TextGenerationChild::TextGenerationChild(const ipc::FileDescriptor& aModel,
                                         const TextGenerationOptions& aOptions)
    : mModel(aModel),
      mOptions(aOptions),
      mGenerationThread(NewGeneratorThread()),
      mActorThread(GetCurrentSerialEventTarget()) {}

TextGenerationChild::~TextGenerationChild() = default;

void TextGenerationChild::Initialize() {
  MOZ_ASSERT(mActorThread->IsOnCurrentThread());
  RefPtr<TextGenerationChild> self = this;
  MOZ_ALWAYS_SUCCEEDS(mGenerationThread->Dispatch(
      NS_NewRunnableFunction("TextGenerationChild::Load", [self] {
        TimeStamp loadStart = TimeStamp::Now();
        LoadResult result = self->LoadOnThread();
        if (result.type() == LoadResult::TLoadSuccess) {
          PROFILER_MARKER(ML_TEXT_GENERATION_TRACK, ML_SETUP,
                          MarkerTiming::IntervalUntilNowFrom(loadStart),
                          MLBackendInitMarker, self->mOptions.contextSize(),
                          self->mOptions.numThreads(),
                          self->mOptions.numThreadsDecoding(),
                          dom::GetEnumString(self->mOptions.kvCacheDtype()),
                          self->mOptions.flashAttn());
        } else {
          PROFILER_MARKER(ML_TEXT_GENERATION_TRACK, ML_SETUP,
                          MarkerTiming::IntervalUntilNowFrom(loadStart),
                          MLFailedMarker, "backend init"_ns, self->mLoadError);
        }
        self->DispatchToActorThread("TextGenerationChild::Ready",
                                    [self, result = std::move(result)] {
                                      (void)self->SendReady(result);
                                    });
      })));
}

LoadResult TextGenerationChild::LoadOnThread() {
  MOZ_ASSERT(mGenerationThread->IsOnCurrentThread());
  MOZ_ASSERT(!mBackend);
  mLoadError.AssignLiteral("TextGenerator: model load failed");
  auto fail = [this](const char* aMessage) {
    mLoadError.AssignASCII(aMessage);
    return LoadResult(LoadError(mLoadError));
  };

  TimeStamp loadStart = TimeStamp::Now();

  if (!mModel.IsValid()) {
    return fail("TextGenerator: invalid model file descriptor");
  }

  // fdopen()/_open_osfhandle() take ownership of the handle: pass a duplicate.
  UniqueFileHandle dupHandle =
      DuplicateFileHandle(mModel.ClonePlatformHandle().get());
  if (!dupHandle) {
    return fail("TextGenerator: DuplicateFileHandle failed");
  }

#ifdef XP_WIN
  int fd = _open_osfhandle(dupHandle.release(), _O_RDONLY);
  if (fd == -1) {
    return fail("TextGenerator: _open_osfhandle failed");
  }
  FILE* fp = fdopen(fd, "rb");
  if (!fp) {
    _close(fd);
  }
#else
  FILE* fp = fdopen(dupHandle.release(), "r");
#endif
  if (!fp) {
    return fail("TextGenerator: fdopen failed");
  }
  auto closeFp = MakeScopeExit([fp] { fclose(fp); });

  RefPtr<llama::LlamaBackend> backend = new llama::LlamaBackend();
  auto result = backend->Reinitialize(ToLlamaModelOptions(mOptions), fp);
  if (result.isErr()) {
    nsCString message = result.unwrapErr().mMessage;
    if (!message.IsEmpty()) {
      mLoadError = std::move(message);
    }
    return LoadResult(LoadError(mLoadError));
  }

  mBackend = std::move(backend);
  mLoadError.Truncate();
  double loadMs = (TimeStamp::Now() - loadStart).ToMilliseconds();
  LOGD("[{} - {}] model loaded in {}ms", fmt::ptr(this), __func__, loadMs);
  return LoadResult(LoadSuccess(loadMs));
}

void TextGenerationChild::DispatchToActorThread(const char* aName,
                                                std::function<void()>&& aFn) {
  RefPtr<TextGenerationChild> self = this;
  MOZ_ALWAYS_SUCCEEDS(mActorThread->Dispatch(
      NS_NewRunnableFunction(aName, [self, fn = std::move(aFn)] {
        if (self->CanSend()) {
          fn();
        }
      })));
}

ipc::IPCResult TextGenerationChild::RecvGenerate(GenerateRequest&& aRequest,
                                                 GenerateResolver&& aResolve) {
  MOZ_ASSERT(mActorThread->IsOnCurrentThread());
  LOGD("[{} - {}] {} message(s)", fmt::ptr(this), __func__,
       aRequest.messages().Length());

  RefPtr<Generation> generation =
      new Generation(this, std::move(aRequest), std::move(aResolve));
  mCurrentGeneration = generation;
  MOZ_ALWAYS_SUCCEEDS(mGenerationThread->Dispatch(NS_NewRunnableFunction(
      "TextGenerationChild::Generate", [generation] { generation->Run(); })));
  return IPC_OK();
}

ipc::IPCResult TextGenerationChild::RecvClear() {
  MOZ_ASSERT(mActorThread->IsOnCurrentThread());
  LOGD("[{} - {}]", fmt::ptr(this), __func__);
  RefPtr<TextGenerationChild> self = this;
  MOZ_ALWAYS_SUCCEEDS(mGenerationThread->Dispatch(NS_NewRunnableFunction(
      "TextGenerationChild::Clear", [self] { self->mHistory.Clear(); })));
  return IPC_OK();
}

ipc::IPCResult TextGenerationChild::RecvCancel() {
  MOZ_ASSERT(mActorThread->IsOnCurrentThread());
  LOGD("[{} - {}]", fmt::ptr(this), __func__);
  PROFILER_MARKER(ML_TEXT_GENERATION_TRACK, ML_INFERENCE, {}, MLCancelMarker,
                  "observed"_ns);
  if (mCurrentGeneration) {
    mCurrentGeneration->RequestCancel();
  }
  return IPC_OK();
}

void TextGenerationChild::ActorDestroy(ActorDestroyReason aReason) {
  MOZ_ASSERT(mActorThread->IsOnCurrentThread());
  LOGD("[{} - {}]", fmt::ptr(this), __func__);
  mShutdown = true;
  mCurrentGeneration = nullptr;
  // Freeing the model on the actor thread would stall every actor in the
  // process behind it; release it on mGenerationThread before the thread goes
  // away.
  RefPtr<TextGenerationChild> self = this;
  MOZ_ALWAYS_SUCCEEDS(mGenerationThread->Dispatch(
      NS_NewRunnableFunction("TextGenerationChild::ReleaseBackend", [self] {
        self->mHistory.Clear();
        self->mBackend = nullptr;
      })));
  mGenerationThread->AsyncShutdown();
}

#undef LOGD
#undef LOGE

}  // namespace mozilla::hwinference
