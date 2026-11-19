/*
 *  Copyright (c) 2025 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#include "modules/audio_processing/aec3/neural_residual_echo_estimator/neural_residual_echo_estimator_impl.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <map>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include "absl/base/nullability.h"
#include "api/audio/echo_canceller3_config.h"
#include "api/audio/neural_residual_echo_estimator.h"
#include "api/audio/tflite_model_handle.h"
#include "api/environment/environment.h"
#include "api/make_ref_counted.h"
#include "api/scoped_refptr.h"
#include "api/task_queue/task_queue_factory.h"
#include "api/units/timestamp.h"
#include "modules/audio_processing/aec3/aec3_common.h"
#include "modules/audio_processing/aec3/block.h"
#include "modules/audio_processing/aec3/neural_residual_echo_estimator/neural_feature_extractor.h"
#include "rtc_base/checks.h"
#ifdef WEBRTC_ANDROID_PLATFORM_BUILD
#include "external/webrtc/webrtc/modules/audio_processing/aec3/neural_residual_echo_estimator/neural_residual_echo_estimator.pb.h"
#else
#include "modules/audio_processing/aec3/neural_residual_echo_estimator/neural_residual_echo_estimator.pb.h"
#endif
#include "modules/audio_processing/logging/apm_data_dumper.h"
#include "rtc_base/logging.h"
#include "system_wrappers/include/metrics.h"
#include "third_party/tflite/src/tensorflow/lite/interpreter.h"
#include "third_party/tflite/src/tensorflow/lite/interpreter_builder.h"
#include "third_party/tflite/src/tensorflow/lite/kernels/kernel_util.h"
#include "third_party/tflite/src/tensorflow/lite/model_builder.h"
#include "third_party/tflite/src/tensorflow/lite/op_resolver.h"

namespace webrtc {
namespace {
using ModelInputEnum = FeatureExtractor::ModelInputEnum;
using ModelOutputEnum = FeatureExtractor::ModelOutputEnum;
const std::array<int, 1> kSupportedFrameSizeSamples = {256};

// Field under which the ML-REE metadata is stored in a TFLite model.
constexpr char kTfLiteMetadataKey[] = "REE_METADATA";

// TFLite model signature names.
constexpr char kMicFrameInput[] = "mic_frame";
constexpr char kLinearAecFrameInput[] = "cancelled_frame";
constexpr char kAecRefFrameInput[] = "ref_frame";
constexpr char kLstmStateInput[] = "lstm_state";
constexpr char kEchoMaskFrameOutput[] = "echo_mask_frame";
constexpr char kUnboundedEchoMaskFrameOutput[] = "unbounded_echo_mask_frame";
constexpr char kLstmStateOutput[] = "lstm_state";
constexpr char kServingDefault[] = "serving_default";

// Reads the model metadata from the TFLite model. If the metadata is not
// present, it returns a default metadata with version 1. If the metadata is
// present but cannot be parsed, it returns nullopt.
std::optional<audioproc::ReeModelMetadata> ReadModelMetadata(
    const tflite::FlatBufferModel* model) {
  audioproc::ReeModelMetadata default_metadata;
  default_metadata.set_version(1);
  const auto metadata_records = model->ReadAllMetadata();
  const auto metadata_field = metadata_records.find(kTfLiteMetadataKey);
  if (metadata_field == metadata_records.end()) {
    return default_metadata;
  }
  audioproc::ReeModelMetadata metadata;
  if (metadata.ParseFromString(metadata_field->second)) {
    return metadata;
  }
  return std::nullopt;
}

// Downsamples the model output mask to the AEC3 frequency resolution and
// transforms it from a nearend prediction to an echo power mask.
void DownsampleAndTransformMask(std::span<const float> mask,
                                std::span<float> downsampled_mask) {
  const int kDownsampleFactor =
      static_cast<int>((mask.size() - 1) / kFftLengthBy2);
  downsampled_mask[0] = mask[0];
  // Downsample by taking the maximum element in each frequency band.
  auto downsample_element = [kDownsampleFactor](const float* mask) {
    return *std::max_element(mask, mask + kDownsampleFactor);
  };
  for (size_t i = 1; i < kFftLengthBy2Plus1; ++i) {
    downsampled_mask[i] =
        downsample_element(&mask[kDownsampleFactor * (i - 1) + 1]);
  }
  // The model is trained to predict the nearend magnitude spectrum but
  // exposes 1 minus that mask. The next transformation computes the mask
  // that estimates the echo power spectrum assuming that the sum of the
  // power spectra of the nearend and the echo produces the power spectrum
  // of the input microphone signal.
  for (float& m : downsampled_mask) {
    m = 1.0f - (1.0f - m) * (1.0f - m);
  }
}

// Checks if all the expected input tensors are present in the model signature
// and have the correct sizes. This ensures the TFLite model conforms to the
// expected interface for the residual echo estimator.
bool AllExpectedInputsArePresent(
    const std::unique_ptr<tflite::Interpreter>& interpreter,
    const audioproc::ReeModelMetadata& metadata) {
  const TfLiteTensor* cancelled_frame_tensor =
      interpreter->input_tensor_by_signature(kLinearAecFrameInput,
                                             kServingDefault);
  if (cancelled_frame_tensor == nullptr) {
    return false;
  }
  const int tensor_size =
      static_cast<int>(tflite::NumElements(cancelled_frame_tensor));
  const int frame_size =
      metadata.version() == 1 ? tensor_size : (tensor_size - 1) * 2;
  if (frame_size % kBlockSize != 0) {
    return false;
  }
  if (std::none_of(kSupportedFrameSizeSamples.cbegin(),
                   kSupportedFrameSizeSamples.cend(),
                   [frame_size](int a) { return a == frame_size; })) {
    return false;
  }
  if (interpreter->input_tensor_by_signature(kLstmStateInput,
                                             kServingDefault) == nullptr) {
    return false;
  }

  for (const char* input_name : {kMicFrameInput, kAecRefFrameInput}) {
    const TfLiteTensor* input_tensor =
        interpreter->input_tensor_by_signature(input_name, kServingDefault);
    if (input_tensor == nullptr ||
        tflite::NumElements(input_tensor) != tensor_size) {
      return false;
    }
  }
  for (const char* input_name : {kMicFrameInput, kLinearAecFrameInput,
                                 kAecRefFrameInput, kLstmStateInput}) {
    const TfLiteTensor* input_tensor =
        interpreter->input_tensor_by_signature(input_name, kServingDefault);
    if (input_tensor->type != kTfLiteFloat32) {
      return false;
    }
  }
  return true;
}

// Checks if all the expected output tensors are present in the model signature
// and have the correct sizes. This ensures the TFLite model conforms to the
// expected interface for the residual echo estimator.
bool AllExpectedOutputsArePresent(
    const std::unique_ptr<tflite::Interpreter>& interpreter,
    const audioproc::ReeModelMetadata& metadata) {
  const TfLiteTensor* cancelled_frame_tensor =
      interpreter->input_tensor_by_signature(kLinearAecFrameInput,
                                             kServingDefault);
  const TfLiteTensor* lstm_state_in =
      interpreter->input_tensor_by_signature(kLstmStateInput, kServingDefault);
  if (cancelled_frame_tensor == nullptr || lstm_state_in == nullptr) {
    return false;
  }
  const int tensor_size =
      static_cast<int>(tflite::NumElements(cancelled_frame_tensor));
  const int frame_size =
      metadata.version() == 1 ? tensor_size : (tensor_size - 1) * 2;
  const TfLiteTensor* lstm_state_tensor =
      interpreter->output_tensor_by_signature(kLstmStateOutput,
                                              kServingDefault);
  if (lstm_state_tensor == nullptr || tflite::NumElements(lstm_state_tensor) !=
                                          tflite::NumElements(lstm_state_in)) {
    return false;
  }
  const TfLiteTensor* echo_mask_frame_tensor =
      interpreter->output_tensor_by_signature(kEchoMaskFrameOutput,
                                              kServingDefault);
  if (echo_mask_frame_tensor == nullptr ||
      tflite::NumElements(echo_mask_frame_tensor) != frame_size / 2 + 1) {
    return false;
  }
  // Check that the unbounded echo mask is either not present or that it has the
  // correct size.
  const TfLiteTensor* echo_mask_unbounded_frame_tensor =
      interpreter->output_tensor_by_signature(kUnboundedEchoMaskFrameOutput,
                                              kServingDefault);
  if (echo_mask_unbounded_frame_tensor != nullptr &&
      tflite::NumElements(echo_mask_unbounded_frame_tensor) !=
          frame_size / 2 + 1) {
    return false;
  }
  for (const char* output_name :
       {kEchoMaskFrameOutput, kUnboundedEchoMaskFrameOutput,
        kLstmStateOutput}) {
    const TfLiteTensor* output_tensor =
        interpreter->output_tensor_by_signature(output_name, kServingDefault);
    if (output_tensor != nullptr && output_tensor->type != kTfLiteFloat32) {
      return false;
    }
  }
  return true;
}

std::vector<size_t> GetInputTensorIndexes(
    std::unique_ptr<tflite::Interpreter>& interpreter) {
  std::vector<size_t> tensor_indexes(
      static_cast<size_t>(ModelInputEnum::kNumInputs), 0);
  const std::map<std::string, uint32_t>& signature_inputs =
      interpreter->signature_inputs(kServingDefault);
  for (int k = 0; k < static_cast<int>(ModelInputEnum::kNumInputs); ++k) {
    switch (k) {
      case static_cast<int>(ModelInputEnum::kMic):
        tensor_indexes[k] = signature_inputs.at(kMicFrameInput);
        break;
      case static_cast<int>(ModelInputEnum::kLinearAecOutput):
        tensor_indexes[k] = signature_inputs.at(kLinearAecFrameInput);
        break;
      case static_cast<int>(ModelInputEnum::kAecRef):
        tensor_indexes[k] = signature_inputs.at(kAecRefFrameInput);
        break;
      case static_cast<int>(ModelInputEnum::kModelState):
        tensor_indexes[k] = signature_inputs.at(kLstmStateInput);
        break;
      default:
        RTC_CHECK(false);
    }
  }
  return tensor_indexes;
}

std::vector<size_t> GetOutputTensorIndexes(
    std::unique_ptr<tflite::Interpreter>& interpreter,
    bool use_unbounded_mask) {
  std::vector<size_t> tensor_indexes(
      static_cast<size_t>(ModelOutputEnum::kNumOutputs), 0);
  const std::map<std::string, uint32_t>& signature_outputs =
      interpreter->signature_outputs(kServingDefault);

  for (int k = 0; k < static_cast<int>(ModelOutputEnum::kNumOutputs); ++k) {
    switch (k) {
      case static_cast<int>(ModelOutputEnum::kEchoMask):
        tensor_indexes[k] = signature_outputs.at(kEchoMaskFrameOutput);
        break;
      case static_cast<int>(ModelOutputEnum::kUnboundedEchoMask):
        tensor_indexes[k] =
            use_unbounded_mask
                ? signature_outputs.at(kUnboundedEchoMaskFrameOutput)
                : 0;
        break;
      case static_cast<int>(ModelOutputEnum::kModelState):
        tensor_indexes[k] = signature_outputs.at(kLstmStateOutput);
        break;
      default:
        RTC_CHECK(false);
    }
  }
  return tensor_indexes;
}

// Encapsulates all the NeuralResidualEchoEstimatorImpl's interaction with
// TFLite. This allows the separation of rebuffering and similar AEC3-related
// bookkeeping from the TFLite-specific code, and makes it easier to test the
// former code by mocking.
class TfLiteModelRunner : public NeuralResidualEchoEstimatorImpl::ModelRunner {
 public:
  TfLiteModelRunner(std::unique_ptr<tflite::Interpreter> tflite_interpreter,
                    audioproc::ReeModelMetadata metadata)
      : input_tensor_size_(static_cast<int>(tflite::NumElements(
            tflite_interpreter->input_tensor_by_signature(kMicFrameInput,
                                                          kServingDefault)))),
        frame_size_(metadata.version() == 1 ? input_tensor_size_
                                            : (input_tensor_size_ - 1) * 2),
        step_size_(frame_size_ / 2),
        use_unbounded_mask_(tflite_interpreter->output_tensor_by_signature(
                                kUnboundedEchoMaskFrameOutput,
                                kServingDefault) != nullptr),
        metadata_(metadata),
        model_state_(
            tflite::NumElements(
                tflite_interpreter->input_tensor_by_signature(kLstmStateInput,
                                                              kServingDefault)),
            0.0f),
        input_tensor_indexes_(GetInputTensorIndexes(tflite_interpreter)),
        output_tensor_indexes_(
            GetOutputTensorIndexes(tflite_interpreter, use_unbounded_mask_)),
        tflite_interpreter_(std::move(tflite_interpreter)) {
    for (const auto input_enum :
         {ModelInputEnum::kMic, ModelInputEnum::kLinearAecOutput,
          ModelInputEnum::kAecRef}) {
      std::span<float> input_tensor = GetInput(input_enum);
      std::fill(input_tensor.begin(), input_tensor.end(), 0.0f);
    }
  }

  ~TfLiteModelRunner() override {}

  void Reset() override {
    std::fill(model_state_.begin(), model_state_.end(), 0.0f);
    for (const auto input_enum :
         {ModelInputEnum::kMic, ModelInputEnum::kLinearAecOutput,
          ModelInputEnum::kAecRef}) {
      std::span<float> input_tensor = GetInput(input_enum);
      std::fill(input_tensor.begin(), input_tensor.end(), 0.0f);
    }
  }

  int StepSize() const override { return step_size_; }

  std::span<float> GetInput(
      FeatureExtractor::ModelInputEnum input_enum) override {
    size_t index = input_tensor_indexes_[static_cast<size_t>(input_enum)];
    TfLiteTensor* input_tensor = tflite_interpreter_->tensor(index);
    float* input_typed_tensor =
        reinterpret_cast<float*>(input_tensor->data.data);
    return std::span<float>(input_typed_tensor,
                            tflite::NumElements(input_tensor));
  }

  std::span<const float> GetOutput(
      FeatureExtractor::ModelOutputEnum output_enum) override {
    if (!use_unbounded_mask_ &&
        output_enum == ModelOutputEnum::kUnboundedEchoMask) {
      return std::span<const float>();
    }
    size_t index = output_tensor_indexes_[static_cast<size_t>(output_enum)];
    const TfLiteTensor* output_tensor = tflite_interpreter_->tensor(index);
    const float* output_typed_tensor =
        reinterpret_cast<const float*>(output_tensor->data.data);
    return std::span<const float>(output_typed_tensor,
                                  tflite::NumElements(output_tensor));
  }

  const audioproc::ReeModelMetadata& GetMetadata() const override {
    return metadata_;
  }

  bool Invoke() override {
    auto input_state = GetInput(ModelInputEnum::kModelState);
    std::copy(model_state_.begin(), model_state_.end(), input_state.begin());
    const TfLiteStatus status = tflite_interpreter_->Invoke();
    if (status != kTfLiteOk && processing_error_log_counter_ <= 0) {
      RTC_LOG(LS_ERROR) << "TfLiteModelRunner::Estimate() "
                           "invocation error, status="
                        << status;
      // Wait ~1 second before logging this error again.
      processing_error_log_counter_ = 16000 / step_size_;
      return false;
    } else if (processing_error_log_counter_ > 0) {
      --processing_error_log_counter_;
    }
    auto output_state = GetOutput(ModelOutputEnum::kModelState);
    std::copy(output_state.begin(), output_state.end(), model_state_.begin());
    constexpr float kStateDecay = 0.999f;
    for (float& state : model_state_) {
      state *= kStateDecay;
    }
    return true;
  }

 private:
  // Size of the input tensors.
  const int input_tensor_size_;

  // Frame size of the model.
  const int frame_size_;

  // Step size.
  const int step_size_;

  // Whether to use the unbounded mask;
  const bool use_unbounded_mask_;

  // Metadata of the model.
  const audioproc::ReeModelMetadata metadata_;

  // LSTM states that carry over to the next inference invocation.
  std::vector<float> model_state_;

  // Tensor indexes for the inputs.
  const std::vector<size_t> input_tensor_indexes_;

  // Tensor indexes for the outputs.
  const std::vector<size_t> output_tensor_indexes_;

  // TFLite model for residual echo estimation.
  // Must outlive `tflite_interpreter_`
  std::unique_ptr<tflite::FlatBufferModel> tflite_model_;

  // Used to run inference with `tflite_model_`.
  std::unique_ptr<tflite::Interpreter> tflite_interpreter_;

  // Counter to avoid logging processing errors too often.
  int processing_error_log_counter_ = 0;
};
}  // namespace

std::unique_ptr<NeuralResidualEchoEstimatorImpl::ModelRunner>
NeuralResidualEchoEstimatorImpl::LoadTfLiteModel(
    const tflite::FlatBufferModel* model,
    const tflite::OpResolver& op_resolver) {
  if (!model) {
    RTC_LOG(LS_ERROR) << "Nothing to load.";
    return nullptr;
  }
  std::unique_ptr<tflite::Interpreter> interpreter;
  tflite::InterpreterBuilder interpreter_builder(*model, op_resolver);
  if (interpreter_builder.SetNumThreads(1) != kTfLiteOk) {
    RTC_LOG(LS_ERROR) << "Error setting interpreter num threads";
    return nullptr;
  }
  if (interpreter_builder(&interpreter) != kTfLiteOk) {
    RTC_LOG(LS_ERROR) << "Error creating interpreter";
    return nullptr;
  }
  if (interpreter->AllocateTensors() != kTfLiteOk) {
    RTC_LOG(LS_ERROR) << "Error allocating tensors";
    return nullptr;
  }
  auto metadata = ReadModelMetadata(model);
  if (!metadata.has_value()) {
    RTC_LOG(LS_ERROR) << "Error reading model metadata";
    return nullptr;
  }
  if (metadata->version() < 1 || metadata->version() > 2) {
    RTC_LOG(LS_ERROR) << "Model version mismatch, got " << metadata->version()
                      << " expected 1 or 2.";
    return nullptr;
  }
  if (!AllExpectedInputsArePresent(interpreter, *metadata)) {
    RTC_LOG(LS_ERROR) << "Model is missing expected input tensors or they "
                         "have the wrong type/size.";
    return nullptr;
  }
  if (!AllExpectedOutputsArePresent(interpreter, *metadata)) {
    RTC_LOG(LS_ERROR)
        << "Not all the expected outputs are present in the model.";
    return nullptr;
  }
  return std::make_unique<TfLiteModelRunner>(std::move(interpreter), *metadata);
}

absl_nullable std::unique_ptr<NeuralResidualEchoEstimator>
NeuralResidualEchoEstimatorImpl::Create(const tflite::FlatBufferModel* model,
                                        const tflite::OpResolver& op_resolver) {
  std::unique_ptr<ModelRunner> model_runner =
      NeuralResidualEchoEstimatorImpl::LoadTfLiteModel(model, op_resolver);
  if (!model_runner) {
    return nullptr;
  }
  return std::make_unique<NeuralResidualEchoEstimatorImpl>(
      std::move(model_runner));
}

int NeuralResidualEchoEstimatorImpl::instance_count_ = 0;

NeuralResidualEchoEstimatorImpl::NeuralResidualEchoEstimatorImpl(
    std::unique_ptr<ModelRunner> model_runner)
    : cross_thread_state_(make_ref_counted<CrossThreadState>()),
      data_dumper_(new ApmDataDumper(++instance_count_)) {
  output_mask_.fill(0.0f);
  output_mask_unbounded_.fill(0.0f);
  if (model_runner) {
    model_bundle_ = std::make_unique<ModelBundle>();
    if (model_runner->GetMetadata().version() == 1) {
      model_bundle_->feature_extractor =
          std::make_unique<TimeDomainFeatureExtractor>(
              /*step_size=*/model_runner->StepSize());
    } else {
      model_bundle_->feature_extractor =
          std::make_unique<FrequencyDomainFeatureExtractor>(
              /*step_size=*/model_runner->StepSize());
    }
    model_bundle_->use_unbounded_mask =
        !model_runner->GetOutput(ModelOutputEnum::kUnboundedEchoMask).empty();
    model_bundle_->model_runner = std::move(model_runner);
  }
}

absl_nonnull std::unique_ptr<NeuralResidualEchoEstimator>
NeuralResidualEchoEstimatorImpl::CreateAsync(
    const Environment& env,
    std::unique_ptr<tflite::OpResolver> op_resolver,
    scoped_refptr<TfliteModelHandle> model_handle) {
  return std::unique_ptr<NeuralResidualEchoEstimatorImpl>(
      new NeuralResidualEchoEstimatorImpl(env, std::move(op_resolver),
                                          std::move(model_handle)));
}

NeuralResidualEchoEstimatorImpl::NeuralResidualEchoEstimatorImpl(
    const Environment& env,
    std::unique_ptr<tflite::OpResolver> op_resolver,
    scoped_refptr<TfliteModelHandle> model_handle)
    : cross_thread_state_(make_ref_counted<CrossThreadState>()),
      data_dumper_(new ApmDataDumper(++instance_count_)) {
  RTC_DCHECK(op_resolver);
  RTC_DCHECK(model_handle);

  output_mask_.fill(0.0f);
  output_mask_unbounded_.fill(0.0f);

  init_queue_ = env.task_queue_factory().CreateTaskQueue(
      "ReeInit", TaskQueueFactory::Priority::kNormal);

  const Timestamp post_time = env.clock().CurrentTime();

  init_queue_->PostTask([env, cross_thread_state = cross_thread_state_,
                         resolver = std::move(op_resolver),
                         model_handle = std::move(model_handle),
                         post_time]() mutable {
    const tflite::FlatBufferModel& model = model_handle->Get();
    std::unique_ptr<ModelRunner> model_runner =
        LoadTfLiteModel(&model, *resolver);
    if (!model_runner) {
      cross_thread_state->MarkFailed();
      RTC_HISTOGRAM_ENUMERATION(
          "WebRTC.Audio.NeuralResidualEchoEstimator.InitResult",
          static_cast<int>(
              NeuralResidualEchoEstimatorInitResult::kModelLoadFailed),
          static_cast<int>(
              NeuralResidualEchoEstimatorInitResult::kNumCategories));
      return;
    }

    auto bundle = std::make_unique<ModelBundle>();
    bundle->model_handle = std::move(model_handle);
    if (model_runner->GetMetadata().version() == 1) {
      bundle->feature_extractor = std::make_unique<TimeDomainFeatureExtractor>(
          /*step_size=*/model_runner->StepSize());
    } else {
      bundle->feature_extractor =
          std::make_unique<FrequencyDomainFeatureExtractor>(
              /*step_size=*/model_runner->StepSize());
    }

    bundle->use_unbounded_mask =
        !model_runner->GetOutput(ModelOutputEnum::kUnboundedEchoMask).empty();
    bundle->model_runner = std::move(model_runner);

    cross_thread_state->Set(std::move(bundle));

    const int64_t duration_ms =
        std::max<int64_t>(0, (env.clock().CurrentTime() - post_time).ms());
    RTC_HISTOGRAM_COUNTS_10000(
        "WebRTC.Audio.NeuralResidualEchoEstimator.InitDurationMs", duration_ms);
    RTC_HISTOGRAM_ENUMERATION(
        "WebRTC.Audio.NeuralResidualEchoEstimator.InitResult",
        static_cast<int>(NeuralResidualEchoEstimatorInitResult::kSuccess),
        static_cast<int>(
            NeuralResidualEchoEstimatorInitResult::kNumCategories));
  });
}

NeuralResidualEchoEstimatorImpl::~NeuralResidualEchoEstimatorImpl() {
  if (init_queue_) {
    init_queue_.reset();
    if (cross_thread_state_ && cross_thread_state_->GetResolution() ==
                                   CrossThreadState::Resolution::kPending) {
      cross_thread_state_->MarkFailed();
      RTC_HISTOGRAM_ENUMERATION(
          "WebRTC.Audio.NeuralResidualEchoEstimator.InitResult",
          static_cast<int>(
              NeuralResidualEchoEstimatorInitResult::kDestroyedBeforeResolved),
          static_cast<int>(
              NeuralResidualEchoEstimatorInitResult::kNumCategories));
    }
  }
}

bool NeuralResidualEchoEstimatorImpl::IsInitialized() {
  if (!model_bundle_) {
    model_bundle_ = cross_thread_state_->TryGet();
  }
  return !!model_bundle_;
}

void NeuralResidualEchoEstimatorImpl::Reset() {
  if (!IsInitialized()) {
    return;
  }
  RTC_DCHECK(model_bundle_);
  model_bundle_->model_runner->Reset();
  if (model_bundle_->feature_extractor) {
    model_bundle_->feature_extractor->Reset();
  }
  output_mask_.fill(0.0f);
  output_mask_unbounded_.fill(0.0f);
}

void NeuralResidualEchoEstimatorImpl::Estimate(
    const Block& render,
    std::span<const std::array<float, kBlockSize>> y,
    std::span<const std::array<float, kBlockSize>> e,
    std::span<const std::array<float, kFftLengthBy2Plus1>> S2,
    std::span<const std::array<float, kFftLengthBy2Plus1>> Y2,
    std::span<const std::array<float, kFftLengthBy2Plus1>> E2,
    bool dominant_nearend,
    std::span<std::array<float, kFftLengthBy2Plus1>> R2,
    std::span<std::array<float, kFftLengthBy2Plus1>> R2_unbounded) {
  if (!IsInitialized()) {
    for (size_t ch = 0; ch < E2.size(); ++ch) {
      std::copy(E2[ch].begin(), E2[ch].end(), R2[ch].begin());
      std::copy(E2[ch].begin(), E2[ch].end(), R2_unbounded[ch].begin());
    }
    return;
  }
  RTC_DCHECK(model_bundle_);

  DumpInputs(render, y, e);
  render_channels_.clear();
  for (int i = 0; i < render.NumChannels(); ++i) {
    render_channels_.emplace_back(render.View(/*band=*/0, i));
  }
  y_channels_.clear();
  for (size_t i = 0; i < y.size(); ++i) {
    y_channels_.emplace_back(y[i]);
  }
  e_channels_.clear();
  for (size_t i = 0; i < e.size(); ++i) {
    e_channels_.emplace_back(e[i]);
  }
  model_bundle_->feature_extractor->UpdateBuffers(y_channels_,
                                                  ModelInputEnum::kMic);
  model_bundle_->feature_extractor->UpdateBuffers(
      e_channels_, ModelInputEnum::kLinearAecOutput);
  model_bundle_->feature_extractor->UpdateBuffers(render_channels_,
                                                  ModelInputEnum::kAecRef);

  if (model_bundle_->feature_extractor->ReadyForInference()) {
    model_bundle_->feature_extractor->PrepareModelInput(
        model_bundle_->model_runner->GetInput(ModelInputEnum::kMic),
        ModelInputEnum::kMic);
    model_bundle_->feature_extractor->PrepareModelInput(
        model_bundle_->model_runner->GetInput(ModelInputEnum::kLinearAecOutput),
        ModelInputEnum::kLinearAecOutput);
    model_bundle_->feature_extractor->PrepareModelInput(
        model_bundle_->model_runner->GetInput(ModelInputEnum::kAecRef),
        ModelInputEnum::kAecRef);
    if (model_bundle_->model_runner->Invoke()) {
      // Downsample output mask to match the AEC3 frequency resolution.
      std::span<const float> output_mask =
          model_bundle_->model_runner->GetOutput(ModelOutputEnum::kEchoMask);
      DownsampleAndTransformMask(output_mask, output_mask_);
      if (model_bundle_->use_unbounded_mask) {
        std::span<const float> output_mask_unbounded =
            model_bundle_->model_runner->GetOutput(
                ModelOutputEnum::kUnboundedEchoMask);
        DownsampleAndTransformMask(output_mask_unbounded,
                                   output_mask_unbounded_);
      }
      data_dumper_->DumpRaw("ml_ree_model_mask", output_mask);
      data_dumper_->DumpRaw("ml_ree_output_mask", output_mask_);
    }
  }

  // Use the latest output mask to produce output echo power estimates.
  if (model_bundle_->use_unbounded_mask) {
    for (size_t ch = 0; ch < E2.size(); ++ch) {
      std::transform(E2[ch].begin(), E2[ch].end(),
                     output_mask_unbounded_.begin(), R2_unbounded[ch].begin(),
                     [](float power, float mask) { return power * mask; });
      // During dominant nearend, use the unbounded mask as it is less
      // conservative in terms of echo suppression.
      if (dominant_nearend) {
        std::copy(R2_unbounded[ch].begin(), R2_unbounded[ch].end(),
                  R2[ch].begin());
      } else {
        std::transform(E2[ch].begin(), E2[ch].end(), output_mask_.begin(),
                       R2[ch].begin(),
                       [](float power, float mask) { return power * mask; });
      }
    }
  } else {
    for (size_t ch = 0; ch < E2.size(); ++ch) {
      std::transform(E2[ch].begin(), E2[ch].end(), output_mask_.begin(),
                     R2[ch].begin(),
                     [](float power, float mask) { return power * mask; });
      std::copy(R2[ch].begin(), R2[ch].end(), R2_unbounded[ch].begin());
    }
  }
}

EchoCanceller3Config::Suppressor NeuralResidualEchoEstimatorImpl::AdjustConfig(
    const EchoCanceller3Config::Suppressor& suppressor_config) const {
  EchoCanceller3Config::Suppressor adjusted_suppressor_config =
      suppressor_config;
  EchoCanceller3Config::Suppressor::MaskingThresholds tuning_masking_thresholds(
      /*enr_transparent=*/0.0f, /*enr_suppress=*/1.0f,
      /*emr_transparent=*/0.3f);
  EchoCanceller3Config::Suppressor::Tuning tuning(
      /*mask_lf=*/tuning_masking_thresholds,
      /*mask_hf=*/tuning_masking_thresholds, /*max_inc_factor=*/100.0f,
      /*max_dec_factor_lf=*/0.0f);
  adjusted_suppressor_config.nearend_average_blocks = 1;
  adjusted_suppressor_config.normal_tuning = tuning;
  adjusted_suppressor_config.nearend_tuning = tuning;
  adjusted_suppressor_config.dominant_nearend_detection.enr_threshold = 0.5f;
  adjusted_suppressor_config.dominant_nearend_detection.trigger_threshold = 2;
  adjusted_suppressor_config.high_frequency_suppression.limiting_gain_band = 24;
  adjusted_suppressor_config.high_frequency_suppression.bands_in_limiting_gain =
      3;
  return adjusted_suppressor_config;
}

void NeuralResidualEchoEstimatorImpl::DumpInputs(
    const Block& render,
    std::span<const std::array<float, kBlockSize>> y,
    std::span<const std::array<float, kBlockSize>> e) {
  data_dumper_->DumpWav("ml_ree_mic_input", y[0], 16000, 1);
  data_dumper_->DumpWav("ml_ree_linear_aec_output", e[0], 16000, 1);
  data_dumper_->DumpWav("ml_ree_aec_ref", render.View(0, 0), 16000, 1);
}

}  // namespace webrtc
