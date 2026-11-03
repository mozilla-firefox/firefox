/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "TextGenerator.h"

#include <algorithm>

#include "llama.h"

#include "mozilla/dom/Blob.h"
#include "mozilla/dom/Promise.h"
#include "mozilla/hwinference/ModelFileUtils.h"
#include "mozilla/hwinference/TextGenerationParent.h"
#include "mozilla/ml/MLProfilerMarkers.h"
#include "nsIMLUtils.h"
#include "nsServiceManagerUtils.h"

using namespace mozilla::hwinference;

namespace mozilla::dom {

NS_IMPL_CYCLE_COLLECTING_ADDREF(TextGenerator)
NS_IMPL_CYCLE_COLLECTING_RELEASE(TextGenerator)
NS_IMPL_CYCLE_COLLECTION_WRAPPERCACHE(TextGenerator, mGlobal)
NS_INTERFACE_MAP_BEGIN_CYCLE_COLLECTION(TextGenerator)
  NS_WRAPPERCACHE_INTERFACE_MAP_ENTRY
  NS_INTERFACE_MAP_ENTRY(nsISupports)
NS_INTERFACE_MAP_END

using dom::Promise;

static uint32_t ResolveNumThreads(uint32_t aRequested) {
  if (aRequested > 0) {
    return aRequested;
  }
  uint8_t cores = 1;
  nsCOMPtr<nsIMLUtils> utils = do_GetService("@mozilla.org/ml-utils;1");
  if (utils) {
    utils->GetOptimalCPUConcurrency(&cores);
  }
  return std::max<uint32_t>(1, cores);
}

// LLAMA_DEFAULT_SEED asks llama.cpp to choose a seed; it is not a fixed seed.
static constexpr uint32_t kDefaultSeed = LLAMA_DEFAULT_SEED;

static void MarkGeneratorCreate(TimeStamp aStart, double aBackendInitMs,
                                const nsCString& aFeatureId) {
  const double wallMs = (TimeStamp::Now() - aStart).ToMilliseconds();
  PROFILER_MARKER(ML_TEXT_GENERATION_TRACK, ML_SETUP,
                  MarkerTiming::IntervalUntilNowFrom(aStart),
                  MLGeneratorCreateMarker, aBackendInitMs,
                  std::max(0.0, wallMs - aBackendInitMs), aFeatureId);
}

static void MarkFailed(TimeStamp aStart, const nsCString& aPhase,
                       const nsCString& aReason) {
  PROFILER_MARKER(ML_TEXT_GENERATION_TRACK, ML_SETUP,
                  MarkerTiming::IntervalUntilNowFrom(aStart), MLFailedMarker,
                  aPhase, aReason);
}

static TextGenerationOptions ResolveTextGenerationOptions(
    const dom::TextGeneratorCreateOptions& aOptions) {
  AssertIsOnMainThread();
  const uint32_t numThreads = ResolveNumThreads(aOptions.mNumThreads);
  const uint32_t numThreadsDecoding = aOptions.mNumThreadsDecoding > 0
                                          ? aOptions.mNumThreadsDecoding
                                          : numThreads;
  return TextGenerationOptions(aOptions.mContextSize, numThreads,
                               numThreadsDecoding, aOptions.mBatchSize,
                               aOptions.mUbatchSize, aOptions.mKvCacheDtype,
                               aOptions.mFlashAttn);
}

static Result<mozilla::ipc::FileDescriptor, const char*> BlobToModelFd(
    JSContext* aCx, dom::Blob& aModel) {
  JS::Rooted<JS::Value> blobValue(aCx);
  if (!GetOrCreateDOMReflector(aCx, &aModel, &blobValue)) {
    return Err("TextGenerator.create: failed to reflect model Blob");
  }
  mozilla::ipc::FileDescriptor modelFd;
  if (NS_FAILED(BlobJSObjectToFileDescriptor(aCx, blobValue, &modelFd))) {
    return Err("TextGenerator.create: model Blob is not file-backed");
  }
  return modelFd;
}

static GenerateRequest ToGenerateRequest(
    const dom::TextGenerationRequest& aRequest) {
  GenerateRequest request;
  for (const auto& message : aRequest.mMessages) {
    request.messages().AppendElement(
        ChatMessage(message.mRole, nsCString(message.mContent)));
  }
  request.maxTokens() = aRequest.mMaxTokens;
  request.bufferLength() = aRequest.mBufferLength;
  request.stopOnEndOfGenerationTokens() = aRequest.mStopOnEndOfGenerationTokens;
  for (int32_t stopToken : aRequest.mStopTokens) {
    request.stopTokens().AppendElement(stopToken);
  }
  for (const auto& sampler : aRequest.mSamplers) {
    CopyableTArray<LogitBias> logitBias;
    for (const auto& bias : sampler.mLogitBias) {
      logitBias.AppendElement(LogitBias(bias.mToken, bias.mBias));
    }
    request.samplers().AppendElement(Sampler(
        sampler.mType, sampler.mTopK, sampler.mTopP, sampler.mTemp,
        sampler.mSeed.WasPassed() ? sampler.mSeed.Value() : kDefaultSeed,
        std::move(logitBias)));
  }
  return request;
}

static dom::TextGenerationResult ToJSResult(const GenerateResult& aResult) {
  const hwinference::Usage& usage = aResult.usage();
  const ResourceUsage& resources = aResult.resources();
  dom::TextGenerationResult jsResult;
  jsResult.mContent = aResult.content();
  jsResult.mReason = aResult.reason();
  jsResult.mUsage.mPromptTokens = usage.promptTokens();
  jsResult.mUsage.mPromptCharacters = usage.promptCharacters();
  jsResult.mUsage.mGeneratedTokens = usage.generatedTokens();
  jsResult.mUsage.mTimings.mPrefillMs = usage.timings().prefillMs();
  jsResult.mUsage.mTimings.mDecodeMs = usage.timings().decodeMs();
  jsResult.mResources.mBefore.mCpuTimeMs = resources.before().cpuTimeMs();
  jsResult.mResources.mBefore.mMemoryBytes = resources.before().memoryBytes();
  jsResult.mResources.mAfter.mCpuTimeMs = resources.after().cpuTimeMs();
  jsResult.mResources.mAfter.mMemoryBytes = resources.after().memoryBytes();
  return jsResult;
}

// Main thread, outside any script-blocked section.
MOZ_CAN_RUN_SCRIPT_BOUNDARY
static void CallDeltaCallback(dom::TextGenerationDeltaCallback& aCallback,
                              const nsACString& aText) {
  aCallback.Call(aText);
}

// Feeds the run marker only.
struct DeltaStats {
  TimeStamp mFirstDeltaAt;
  double mDeliverMs = 0.0;
};

// Empty when the caller passed no callback, which is what RecvDelta tests.
static TextGenerationParent::DeltaHandler MakeDeltaHandler(
    const dom::Optional<OwningNonNull<dom::TextGenerationDeltaCallback>>&
        aOnDelta,
    const std::shared_ptr<DeltaStats>& aStats) {
  if (!aOnDelta.WasPassed()) {
    return nullptr;
  }
  RefPtr<dom::TextGenerationDeltaCallback> callback = &aOnDelta.Value();
  const bool profiling = profiler_thread_is_being_profiled_for_markers();
  // Capturing `this` would close a cycle the cycle collector cannot see.
  return [callback, aStats, profiling](const nsCString& aText) {
    TimeStamp deltaStart;
    if (profiling) {
      deltaStart = TimeStamp::Now();
      if (aStats->mFirstDeltaAt.IsNull()) {
        aStats->mFirstDeltaAt = deltaStart;
      }
    }
    CallDeltaCallback(MOZ_KnownLive(*callback), aText);
    if (profiling) {
      aStats->mDeliverMs += (TimeStamp::Now() - deltaStart).ToMilliseconds();
    }
  };
}

static void MarkGeneratorRun(TimeStamp aStart, const GenerateResult& aResult,
                             const DeltaStats& aStats, uint32_t aChunkTokens,
                             const nsCString& aFeatureId) {
  const hwinference::Usage& usage = aResult.usage();
  const double ttfcMs = aStats.mFirstDeltaAt.IsNull()
                            ? 0.0
                            : (aStats.mFirstDeltaAt - aStart).ToMilliseconds();
  const double computeMs =
      usage.timings().prefillMs() + usage.timings().decodeMs();
  const double wallMs = (TimeStamp::Now() - aStart).ToMilliseconds();
  PROFILER_MARKER(
      ML_TEXT_GENERATION_TRACK, ML_INFERENCE,
      MarkerTiming::IntervalUntilNowFrom(aStart), MLGeneratorRunMarker,
      usage.promptTokens(), usage.generatedTokens(), ttfcMs, aChunkTokens,
      ml::TokensPerSecond(usage.promptTokens(), usage.timings().prefillMs()),
      ml::TokensPerSecond(usage.generatedTokens(), usage.timings().decodeMs()),
      computeMs, aStats.mDeliverMs,
      std::max(0.0, wallMs - computeMs - aStats.mDeliverMs),
      dom::GetEnumString(aResult.reason()), aFeatureId);
}

TextGenerator::TextGenerator(nsIGlobalObject* aGlobal,
                             RefPtr<TextGenerationParent> aActor,
                             const nsACString& aFeatureId)
    : mGlobal(aGlobal), mActor(std::move(aActor)), mFeatureId(aFeatureId) {}

TextGenerator::~TextGenerator() { Terminate(); }

JSObject* TextGenerator::WrapObject(JSContext* aCx,
                                    JS::Handle<JSObject*> aGivenProto) {
  return dom::TextGenerator_Binding::Wrap(aCx, this, aGivenProto);
}

/* static */
already_AddRefed<Promise> TextGenerator::Create(
    const dom::GlobalObject& aGlobal, dom::Blob& aModel,
    const dom::TextGeneratorCreateOptions& aOptions, ErrorResult& aRv) {
  AssertIsOnMainThread();
  nsCOMPtr<nsIGlobalObject> global = do_QueryInterface(aGlobal.GetAsSupports());
  RefPtr<Promise> promise = Promise::Create(global, aRv);
  if (!promise) {
    return nullptr;
  }

  if (aOptions.mNumThreads > GGML_MAX_N_THREADS ||
      aOptions.mNumThreadsDecoding > GGML_MAX_N_THREADS) {
    promise->MaybeRejectWithTypeError(
        "TextGenerator.create: thread count exceeds llama.cpp's limit");
    return promise.forget();
  }

  auto fd = BlobToModelFd(aGlobal.Context(), aModel);
  if (fd.isErr()) {
    promise->MaybeRejectWithDataError(nsDependentCString(fd.unwrapErr()));
    return promise.forget();
  }
  mozilla::ipc::FileDescriptor modelFd = fd.unwrap();
  TextGenerationOptions options = ResolveTextGenerationOptions(aOptions);

  TimeStamp acquireStart = TimeStamp::Now();
  RefPtr<TextGenerationParent> actor =
      TextGenerationParent::Create(modelFd, options);
  if (!actor) {
    MarkFailed(acquireStart, "process acquire"_ns,
               "no inference process available"_ns);
    promise->MaybeRejectWithOperationError(
        "TextGenerator.create: failed to start the inference process");
    return promise.forget();
  }
  TimeStamp createStart = TimeStamp::Now();
  PROFILER_MARKER(ML_TEXT_GENERATION_TRACK, ML_SETUP,
                  MarkerTiming::Interval(acquireStart, createStart),
                  MLProcessAcquireMarker, actor->ProcessReused());

  RefPtr<TextGenerator> generator =
      new TextGenerator(global, actor, aOptions.mFeatureId);
  actor->WhenReady()->Then(
      GetMainThreadSerialEventTarget(), __func__,
      [promise, generator, createStart](double aBackendInitMs) {
        MarkGeneratorCreate(createStart, aBackendInitMs, generator->mFeatureId);
        promise->MaybeResolve(generator);
      },
      [promise, generator,
       createStart](const TextGenerationParent::LoadFailure& aFailure) {
        generator->Terminate();
        MarkFailed(createStart, "generator create"_ns, aFailure.message);
        // Telemetry records the error name, so each cause gets its own.
        if (aFailure.cause ==
            TextGenerationParent::LoadFailure::Cause::ActorGone) {
          promise->MaybeRejectWithAbortError(aFailure.message);
        } else {
          promise->MaybeRejectWithNotSupportedError(aFailure.message);
        }
      });

  return promise.forget();
}

already_AddRefed<Promise> TextGenerator::Generate(
    const dom::TextGenerationRequest& aRequest,
    const dom::Optional<OwningNonNull<dom::TextGenerationDeltaCallback>>&
        aOnDelta,
    ErrorResult& aRv) {
  AssertIsOnMainThread();
  RefPtr<Promise> promise = Promise::Create(mGlobal, aRv);
  if (aRv.Failed()) {
    return nullptr;
  }
  if (mTerminated) {
    promise->MaybeRejectWithInvalidStateError(
        "TextGenerator.generate: generator is terminated");
    return promise.forget();
  }
  if (IsLost()) {
    promise->MaybeRejectWithAbortError(
        "TextGenerator.generate: the inference process went away");
    return promise.forget();
  }
  if (mGenerateInFlight) {
    promise->MaybeRejectWithInvalidStateError(
        "TextGenerator.generate: a generation is already in flight");
    return promise.forget();
  }

  GenerateRequest request = ToGenerateRequest(aRequest);

  TimeStamp generateStart = TimeStamp::Now();
  auto stats = std::make_shared<DeltaStats>();
  mActor->SetDeltaHandler(MakeDeltaHandler(aOnDelta, stats));

  mGenerateInFlight = true;
  RefPtr<TextGenerator> self = this;
  const uint32_t chunkTokens = request.bufferLength();
  mActor->SendGenerate(request)->Then(
      GetMainThreadSerialEventTarget(), __func__,
      [promise, self, generateStart, stats,
       chunkTokens](const GenerateResponse& aResponse) {
        self->OnGenerateSettled();
        if (aResponse.type() == GenerateResponse::TGenerateError) {
          // Inference-phase category; MarkFailed emits under ML_SETUP.
          PROFILER_MARKER(ML_TEXT_GENERATION_TRACK, ML_INFERENCE,
                          MarkerTiming::IntervalUntilNowFrom(generateStart),
                          MLFailedMarker, "generate"_ns,
                          aResponse.get_GenerateError().message());
          promise->MaybeRejectWithOperationError(
              aResponse.get_GenerateError().message());
          return;
        }
        const GenerateResult& result = aResponse.get_GenerateResult();
        MarkGeneratorRun(generateStart, result, *stats, chunkTokens,
                         self->mFeatureId);
        promise->MaybeResolve(ToJSResult(result));
      },
      [promise, self, generateStart](mozilla::ipc::ResponseRejectReason) {
        self->OnGenerateSettled();
        if (self->mTerminated) {
          PROFILER_MARKER(ML_TEXT_GENERATION_TRACK, ML_INFERENCE,
                          MarkerTiming::IntervalUntilNowFrom(generateStart),
                          MLCancelMarker, "teardown"_ns);
          promise->MaybeRejectWithAbortError(
              "TextGenerator.generate: the generator was terminated");
          return;
        }
        PROFILER_MARKER(ML_TEXT_GENERATION_TRACK, ML_INFERENCE,
                        MarkerTiming::IntervalUntilNowFrom(generateStart),
                        MLFailedMarker, "generate"_ns,
                        "the inference process went away"_ns);
        promise->MaybeRejectWithAbortError(
            "TextGenerator.generate: the inference process went away");
      });

  return promise.forget();
}

bool TextGenerator::IsLost() const {
  AssertIsOnMainThread();
  return !mTerminated && !mActor->CanSend();
}

void TextGenerator::OnGenerateSettled() {
  AssertIsOnMainThread();
  mGenerateInFlight = false;
  mActor->SetDeltaHandler(nullptr);
}

void TextGenerator::Clear(ErrorResult& aRv) {
  AssertIsOnMainThread();
  if (mGenerateInFlight) {
    aRv.ThrowInvalidStateError(
        "TextGenerator.clear: a generation is in flight");
    return;
  }
  if (mActor->CanSend()) {
    (void)mActor->SendClear();
  }
}

void TextGenerator::Cancel() {
  AssertIsOnMainThread();
  if (mActor->CanSend()) {
    PROFILER_MARKER(ML_TEXT_GENERATION_TRACK, ML_INFERENCE, {}, MLCancelMarker,
                    "requested"_ns);
    (void)mActor->SendCancel();
  }
}

void TextGenerator::Terminate() {
  AssertIsOnMainThread();
  if (mTerminated) {
    return;
  }
  mTerminated = true;
  if (mActor->CanSend()) {
    mActor->Close();
  }
}

}  // namespace mozilla::dom
