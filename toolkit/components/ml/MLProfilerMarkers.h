/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef mozilla_ml_MLProfilerMarkers_h
#define mozilla_ml_MLProfilerMarkers_h

#include "mozilla/BaseProfilerMarkersPrerequisites.h"
#include "mozilla/ProfilerMarkers.h"

#define ML_TEXT_GENERATION_TRACK "ML:TextGeneration"
#define ML_HWINFERENCE_PROCESS_TRACK "ML:HWInferenceProcess"

namespace mozilla::ml {

inline double TokensPerSecond(uint32_t aTokens, double aMs) {
  return aMs > 0.0 ? (aTokens * 1000.0) / aMs : 0.0;
}

}  // namespace mozilla::ml

namespace geckoprofiler::markers {

using namespace mozilla;

struct MLProcessSpawnMarker : public BaseMarkerType<MLProcessSpawnMarker> {
  static constexpr const char* Name = "MLProcessSpawn";
  static constexpr const char* Description =
      "Cold launch of the HWInference utility process, as waited for by "
      "the consumer that found it down.";

  using MS = MarkerSchema;
  static constexpr MS::PayloadField PayloadFields[] = {
      {"launchMs", MS::InputType::Double, "Process launch",
       MS::Format::Duration},
  };
  static constexpr MS::Location Locations[] = {MS::Location::MarkerChart,
                                               MS::Location::MarkerTable,
                                               MS::Location::TimelineOverview};
  static constexpr const char* AllLabels = "spawn";
};

struct MLProcessReleaseMarker : public BaseMarkerType<MLProcessReleaseMarker> {
  static constexpr const char* Name = "MLProcessRelease";
  static constexpr const char* Description =
      "A consumer's keep-alive held past its actor for the idle grace; "
      "retired says whether dropping it shut the process down.";

  using MS = MarkerSchema;
  static constexpr MS::PayloadField PayloadFields[] = {
      {"graceMs", MS::InputType::Uint32, "Configured grace",
       MS::Format::Duration},
      {"retired", MS::InputType::Boolean, "Process retired",
       MS::Format::Integer},
  };
  static constexpr MS::Location Locations[] = {MS::Location::MarkerChart,
                                               MS::Location::MarkerTable,
                                               MS::Location::TimelineOverview};
  static constexpr const char* AllLabels = "release";
};

struct MLProcessAcquireMarker : public BaseMarkerType<MLProcessAcquireMarker> {
  static constexpr const char* Name = "MLProcessAcquire";
  static constexpr const char* Description =
      "Waiting for an HWInference process to be available.";

  using MS = MarkerSchema;
  static constexpr MS::PayloadField PayloadFields[] = {
      {"processReused", MS::InputType::Boolean, "Process reused",
       MS::Format::Integer},
  };
  static constexpr MS::Location Locations[] = {MS::Location::MarkerChart,
                                               MS::Location::MarkerTable};
  static constexpr const char* AllLabels = "process acquire";
};

struct MLGeneratorCreateMarker
    : public BaseMarkerType<MLGeneratorCreateMarker> {
  static constexpr const char* Name = "MLGeneratorCreate";
  static constexpr const char* Description =
      "Constructing a generator on a process that is already available; "
      "acquiring the process is on the process row.";

  using MS = MarkerSchema;
  static constexpr MS::PayloadField PayloadFields[] = {
      {"backendInitMs", MS::InputType::Double, "Backend init",
       MS::Format::Duration},
      {"overheadMs", MS::InputType::Double, "Overhead", MS::Format::Duration},
      {"featureId", MS::InputType::CString, "Feature", MS::Format::String},
  };
  static constexpr MS::Location Locations[] = {MS::Location::MarkerChart,
                                               MS::Location::MarkerTable,
                                               MS::Location::TimelineOverview};
  static constexpr const char* AllLabels = "generator create";
};

struct MLGeneratorRunMarker : public BaseMarkerType<MLGeneratorRunMarker> {
  static constexpr const char* Name = "MLGeneratorRun";
  static constexpr const char* Description =
      "One completed generation as the caller saw it, measured in the parent.";

  using MS = MarkerSchema;
  static constexpr MS::PayloadField PayloadFields[] = {
      {"promptTokens", MS::InputType::Uint32, "Prompt tokens",
       MS::Format::Integer},
      {"generatedTokens", MS::InputType::Uint32, "Generated tokens",
       MS::Format::Integer},
      {"ttfcMs", MS::InputType::Double, "Time to first chunk",
       MS::Format::Duration},
      {"chunkTokens", MS::InputType::Uint32, "Tokens per chunk",
       MS::Format::Integer},
      {"prefillTokensPerSecond", MS::InputType::Double, "Prefill throughput",
       MS::Format::Decimal},
      {"tokensPerSecond", MS::InputType::Double, "Decode throughput",
       MS::Format::Decimal},
      {"computeMs", MS::InputType::Double, "Model compute",
       MS::Format::Duration},
      {"deliverMs", MS::InputType::Double, "Caller callbacks",
       MS::Format::Duration},
      {"overheadMs", MS::InputType::Double, "Overhead", MS::Format::Duration},
      {"reason", MS::InputType::CString, "Finish reason", MS::Format::String},
      {"featureId", MS::InputType::CString, "Feature", MS::Format::String},
  };
  static constexpr MS::Location Locations[] = {MS::Location::MarkerChart,
                                               MS::Location::MarkerTable,
                                               MS::Location::TimelineOverview};
  static constexpr const char* AllLabels =
      "generate: {marker.data.generatedTokens} tokens";
};

struct MLFailedMarker : public BaseMarkerType<MLFailedMarker> {
  static constexpr const char* Name = "MLFailed";
  static constexpr const char* Description =
      "A phase that did not finish, and how long the caller waited.";

  using MS = MarkerSchema;
  static constexpr MS::PayloadField PayloadFields[] = {
      {"phase", MS::InputType::CString, "Phase", MS::Format::String},
      {"reason", MS::InputType::CString, "Reason", MS::Format::String},
  };
  static constexpr MS::Location Locations[] = {MS::Location::MarkerChart,
                                               MS::Location::MarkerTable,
                                               MS::Location::TimelineOverview};
  static constexpr const char* AllLabels = "failed: {marker.data.phase}";
};

struct MLChunkSendMarker : public BaseMarkerType<MLChunkSendMarker> {
  static constexpr const char* Name = "MLChunkSend";
  static constexpr const char* Description =
      "A chunk of generated text left the generator process.";

  using MS = MarkerSchema;
  static constexpr MS::PayloadField PayloadFields[] = {
      {"tokens", MS::InputType::Uint32, "Tokens", MS::Format::Integer},
      {"bytes", MS::InputType::Uint32, "Chunk size", MS::Format::Bytes},
  };
  static constexpr MS::Location Locations[] = {MS::Location::MarkerChart,
                                               MS::Location::MarkerTable};
  static constexpr const char* AllLabels = "chunk: {marker.data.tokens} tokens";
};

struct MLBackendInitMarker : public BaseMarkerType<MLBackendInitMarker> {
  static constexpr const char* Name = "MLBackendInit";
  static constexpr const char* Description =
      "Bringing the backend up in the generator process: the model "
      "descriptor, the weights, and the context.";

  using MS = MarkerSchema;
  static constexpr MS::PayloadField PayloadFields[] = {
      {"contextSize", MS::InputType::Uint32, "Context size",
       MS::Format::Integer},
      {"threads", MS::InputType::Uint32, "Prompt threads", MS::Format::Integer},
      {"threadsDecoding", MS::InputType::Uint32, "Decode threads",
       MS::Format::Integer},
      {"kvCacheDtype", MS::InputType::CString, "KV cache dtype",
       MS::Format::String},
      {"flashAttn", MS::InputType::Boolean, "Flash attention",
       MS::Format::Integer},
  };
  static constexpr MS::Location Locations[] = {MS::Location::MarkerChart,
                                               MS::Location::MarkerTable};
  static constexpr const char* AllLabels = "backend init";
};

struct MLModelPrefillMarker : public BaseMarkerType<MLModelPrefillMarker> {
  static constexpr const char* Name = "MLModelPrefill";
  static constexpr const char* Description =
      "Prompt evaluation, up to the first output token; tokenizing is inside "
      "it.";

  using MS = MarkerSchema;
  static constexpr MS::PayloadField PayloadFields[] = {
      {"promptTokens", MS::InputType::Uint32, "Prompt tokens",
       MS::Format::Integer},
      {"tokensPerSecond", MS::InputType::Double, "Prefill throughput",
       MS::Format::Decimal},
  };
  static constexpr MS::Location Locations[] = {MS::Location::MarkerChart,
                                               MS::Location::MarkerTable};
  static constexpr const char* AllLabels =
      "prefill: {marker.data.promptTokens} tokens";
};

struct MLModelDecodeMarker : public BaseMarkerType<MLModelDecodeMarker> {
  static constexpr const char* Name = "MLModelDecode";
  static constexpr const char* Description = "Token-by-token generation.";

  using MS = MarkerSchema;
  static constexpr MS::PayloadField PayloadFields[] = {
      {"generatedTokens", MS::InputType::Uint32, "Generated tokens",
       MS::Format::Integer},
      {"tokensPerSecond", MS::InputType::Double, "Decode throughput",
       MS::Format::Decimal},
      {"reason", MS::InputType::CString, "Finish reason", MS::Format::String},
  };
  static constexpr MS::Location Locations[] = {MS::Location::MarkerChart,
                                               MS::Location::MarkerTable};
  static constexpr const char* AllLabels =
      "decode: {marker.data.generatedTokens} tokens";
};

struct MLCancelMarker : public BaseMarkerType<MLCancelMarker> {
  static constexpr const char* Name = "MLCancel";
  static constexpr const char* Description =
      "One Cancel, marked twice: where it was requested and where the "
      "generator process observed it.";

  using MS = MarkerSchema;
  static constexpr MS::PayloadField PayloadFields[] = {
      {"stage", MS::InputType::CString, "Stage", MS::Format::String},
  };
  static constexpr MS::Location Locations[] = {MS::Location::MarkerChart,
                                               MS::Location::MarkerTable};
  static constexpr const char* AllLabels = "cancel: {marker.data.stage}";
};

}  // namespace geckoprofiler::markers

#endif  // mozilla_ml_MLProfilerMarkers_h
