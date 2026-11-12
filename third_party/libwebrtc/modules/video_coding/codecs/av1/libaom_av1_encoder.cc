/*
 *  Copyright (c) 2020 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */
#include "modules/video_coding/codecs/av1/libaom_av1_encoder.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <map>
#include <memory>
#include <numeric>
#include <optional>
#include <span>
#include <utility>
#include <vector>

#include "absl/algorithm/container.h"
#include "absl/base/nullability.h"
#include "absl/container/inlined_vector.h"
#include "api/environment/environment.h"
#include "api/field_trials_view.h"
#include "api/scoped_refptr.h"
#include "api/units/time_delta.h"
#include "api/units/timestamp.h"
#include "api/video/encoded_image.h"
#include "api/video/render_resolution.h"
#include "api/video/video_codec_constants.h"
#include "api/video/video_codec_type.h"
#include "api/video/video_content_type.h"
#include "api/video/video_frame.h"
#include "api/video/video_frame_buffer.h"
#include "api/video/video_frame_type.h"
#include "api/video/video_timing.h"
#include "api/video_codecs/encoder_speed_controller.h"
#include "api/video_codecs/scalability_mode.h"
#include "api/video_codecs/video_codec.h"
#include "api/video_codecs/video_encoder.h"
#include "common_video/generic_frame_descriptor/generic_frame_info.h"
#include "modules/rtp_rtcp/include/rtp_rtcp_defines.h"
#include "modules/video_coding/codecs/av1/libaom_speed_config_factory.h"
#include "modules/video_coding/include/video_codec_interface.h"
#include "modules/video_coding/include/video_error_codes.h"
#include "modules/video_coding/svc/create_scalability_structure.h"
#include "modules/video_coding/svc/scalable_video_controller.h"
#include "modules/video_coding/utility/frame_sampler.h"
#include "rtc_base/checks.h"
#include "rtc_base/experiments/encoder_info_settings.h"
#include "rtc_base/experiments/encoder_speed_experiment.h"
#include "rtc_base/logging.h"
#include "system_wrappers/include/clock.h"
#include "third_party/libaom/source/libaom/aom/aom_codec.h"
#include "third_party/libaom/source/libaom/aom/aom_encoder.h"
#include "third_party/libaom/source/libaom/aom/aom_image.h"
#include "third_party/libaom/source/libaom/aom/aomcx.h"

#if (defined(WEBRTC_ARCH_ARM) || defined(WEBRTC_ARCH_ARM64)) && \
    (defined(WEBRTC_ANDROID) || defined(WEBRTC_IOS))
#define MOBILE_ARM
#endif

#ifndef AOM_EFLAG_CALCULATE_PSNR
#define AOM_EFLAG_CALCULATE_PSNR (1 << 3)
#endif

#ifndef AOM_EFLAG_FREEZE_INTERNAL_STATE
#define AOM_EFLAG_FREEZE_INTERNAL_STATE (1 << 2)
#endif

#if defined(WEBRTC_ENCODER_PSNR_STATS)
constexpr bool kEnablePsnrStats = true;
#else
constexpr bool kEnablePsnrStats = false;
#endif

#define SET_ENCODER_PARAM_OR_RETURN_ERROR(param_id, param_value) \
  do {                                                           \
    if (!SetEncoderControlParameters(param_id, param_value)) {   \
      return WEBRTC_VIDEO_CODEC_ERROR;                           \
    }                                                            \
  } while (0)

namespace webrtc {
namespace {

// Encoder configuration parameters
constexpr int kMinQp = 10;
constexpr int kMinQindex = 40;  // Min qindex corresponding to kMinQp.
constexpr int kUsageProfile = AOM_USAGE_REALTIME;
constexpr int kLowQindex = 145;   // Low qindex threshold for QP scaling.
constexpr int kHighQindex = 205;  // High qindex threshold for QP scaling.
constexpr int kBitDepth = 8;
constexpr int kLagInFrames = 0;  // No look ahead.
constexpr double kMinFrameRateFps = 1.0;

aom_superblock_size_t GetSuperblockSize(int width, int height, int threads) {
  int resolution = width * height;
  if (threads >= 4 && resolution >= 960 * 540 && resolution < 1920 * 1080)
    return AOM_SUPERBLOCK_SIZE_64X64;
  else
    return AOM_SUPERBLOCK_SIZE_DYNAMIC;
}

void PopulateEncodedImageFromVideoFrame(const VideoFrame& frame,
                                        EncodedImage& encoded_image) {
  encoded_image.SetRtpTimestamp(frame.rtp_timestamp());
  encoded_image.SetPresentationTimestamp(frame.presentation_timestamp());
  encoded_image.capture_time_ms_ = frame.render_time_ms();
  encoded_image.rotation_ = frame.rotation();
  encoded_image.SetColorSpace(frame.color_space());
}

struct EncodeResult {
  aom_codec_err_t status_code = AOM_CODEC_OK;
  std::optional<EncodedImage> encoded_image;
  TimeDelta encode_time = TimeDelta::Zero();
};

EncoderSpeedController::EncodeResults ToSpeedControllerEncodeResult(
    const EncodeResult& encode_result,
    const EncoderSpeedController::FrameEncodingInfo& frame_info,
    int speed) {
  RTC_DCHECK(encode_result.encoded_image.has_value());
  const EncodedImage& image = *encode_result.encoded_image;
  return EncoderSpeedController::EncodeResults{
      .speed = speed,
      .encode_time = encode_result.encode_time,
      .qp = image.qp_ / 4,  // Use [0, 63] range instead of [0, 255].
      .psnr = image.psnr().has_value() ? std::optional<double>(image.psnr()->y)
                                       : std::nullopt,
      .frame_info = frame_info};
}

class LibaomAv1Encoder final : public VideoEncoder {
 public:
  LibaomAv1Encoder(const Environment& env, LibaomAv1EncoderSettings settings);
  ~LibaomAv1Encoder() override;

  int InitEncode(const VideoCodec* codec_settings,
                 const Settings& settings) override;

  int32_t RegisterEncodeCompleteCallback(
      EncodedImageCallback* encoded_image_callback) override;

  int32_t Release() override;

  int32_t Encode(const VideoFrame& frame,
                 const std::vector<VideoFrameType>* frame_types) override;

  void SetRates(const RateControlParameters& parameters) override;

  void SetComplexity(
      std::span<const VideoCodecComplexity> complexity_by_spatial_layer,
      ComplexityControlMode complexity_control_mode) override;

  EncoderInfo GetEncoderInfo() const override;

 private:
  template <typename P>
  bool SetEncoderControlParameters(int param_id, P param_value);

  // Get value to be used for encoder cpu_speed setting
  int GetCpuSpeed(int width, int height);

  // Determine number of encoder threads to use.
  int NumberOfThreads(int width, int height, int number_of_cores);

  bool SvcEnabled() const { return svc_params_.has_value(); }
  // Fills svc_params_ memeber value. Returns false on error.
  bool SetSvcParams(ScalableVideoController::StreamLayersConfig svc_config,
                    const aom_codec_enc_cfg_t& encoder_config);
  // Configures the encoder with layer for the next frame.
  void SetSvcLayerId(
      const ScalableVideoController::LayerFrameConfig& layer_frame);
  // Configures the encoder which buffers next frame updates and can
  // reference.
  void SetSvcRefFrameConfig(
      const ScalableVideoController::LayerFrameConfig& layer_frame);
  // If pixel format doesn't match, then reallocate.
  void MaybeRewrapImgWithFormat(const aom_img_fmt_t fmt,
                                unsigned int width,
                                unsigned int height);

  // Adjust sclaing factors assuming that the top active SVC layer
  // will be the input resolution.
  void AdjustScalingFactorsForTopActiveLayer();

  EncoderSpeedController::ReferenceClass AsSpeedControllerFrameType(
      const ScalableVideoController::LayerFrameConfig& layer_frame) const;

  // Returns frame interval, compensated for relative pixel count allocation.
  TimeDelta GetFrameInterval(int spatial_index) const;

  // Duration is specified in ticks based on aom_codec_enc_cfg_t::g_timebase,
  // in practice that that is kVideoPayloadTypeFrequency (90kHz).
  EncodeResult DoEncode(uint32_t duration,
                        aom_enc_frame_flags_t flags,
                        ScalableVideoController::LayerFrameConfig* layer_frame);

  CodecSpecificInfo CreateCodecSpecificInfo(
      const EncodedImage& image,
      const ScalableVideoController::LayerFrameConfig& layer_frame);

  std::unique_ptr<ScalableVideoController> svc_controller_;
  std::optional<ScalabilityMode> scalability_mode_;
  // Original scaling factors for all configured layers active and inactive.
  // `svc_params_` stores factors ignoring top inactive layers.
  std::vector<int> scaling_factors_num_;
  std::vector<int> scaling_factors_den_;
  int last_active_layer_ = 0;

  bool inited_;
  bool rates_configured_;
  std::optional<aom_svc_params_t> svc_params_;
  VideoCodec encoder_settings_;
  LibaomAv1EncoderSettings settings_;
  aom_image_t* frame_for_encode_;
  aom_codec_ctx_t ctx_;
  aom_codec_enc_cfg_t cfg_;
  EncodedImageCallback* encoded_image_callback_;
  double framerate_fps_;  // Current target frame rate.
  int64_t timestamp_;
  const Environment env_;
  const LibaomAv1EncoderInfoSettings encoder_info_override_;

  // Determine whether the frame should be sampled for PSNR.
  FrameSampler psnr_frame_sampler_;
  const bool drop_repeat_frames_on_enhancement_layers_;
  std::map<int, uint32_t> last_encoded_timestamp_by_sid_;

  const EncoderSpeedExperiment encoder_speed_experiment_;
  // One speed controller per spatial layer.
  std::vector<std::unique_ptr<webrtc::EncoderSpeedController>>
      speed_controllers_;
  // Fixed speed per spatial layer, used when `speed_controllers_` is empty.
  std::vector<int> fixed_speeds_;
  // Don't use when setting input frame timestamps!
  Clock* const realtime_clock_;
};

int32_t VerifyCodecSettings(const VideoCodec& codec_settings) {
  if (codec_settings.width < 1) {
    return WEBRTC_VIDEO_CODEC_ERR_PARAMETER;
  }
  if (codec_settings.height < 1) {
    return WEBRTC_VIDEO_CODEC_ERR_PARAMETER;
  }
  // maxBitrate == 0 represents an unspecified maxBitRate.
  if (codec_settings.maxBitrate > 0 &&
      codec_settings.minBitrate > codec_settings.maxBitrate) {
    return WEBRTC_VIDEO_CODEC_ERR_PARAMETER;
  }
  if (codec_settings.maxBitrate > 0 &&
      codec_settings.startBitrate > codec_settings.maxBitrate) {
    return WEBRTC_VIDEO_CODEC_ERR_PARAMETER;
  }
  if (codec_settings.startBitrate < codec_settings.minBitrate) {
    return WEBRTC_VIDEO_CODEC_ERR_PARAMETER;
  }
  if (codec_settings.maxFramerate < 1) {
    return WEBRTC_VIDEO_CODEC_ERR_PARAMETER;
  }
  if (codec_settings.qpMax < kMinQp || codec_settings.qpMax > 63) {
    return WEBRTC_VIDEO_CODEC_ERR_PARAMETER;
  }
  return WEBRTC_VIDEO_CODEC_OK;
}

LibaomAv1Encoder::LibaomAv1Encoder(const Environment& env,
                                   LibaomAv1EncoderSettings settings)
    : inited_(false),
      rates_configured_(false),
      settings_(std::move(settings)),
      frame_for_encode_(nullptr),
      encoded_image_callback_(nullptr),
      framerate_fps_(0),
      timestamp_(0),
      env_(env),
      encoder_info_override_(env_.field_trials()),
      psnr_frame_sampler_(FrameSampler::kDefaultPsnrFrameSamplingInterval),
      drop_repeat_frames_on_enhancement_layers_(env.field_trials().IsEnabled(
          "WebRTC-LibaomAv1Encoder-DropRepeatFramesOnEnhancementLayers")),
      encoder_speed_experiment_(env.field_trials()),
      realtime_clock_(&env_.clock()) {}

LibaomAv1Encoder::~LibaomAv1Encoder() {
  Release();
}

int LibaomAv1Encoder::InitEncode(const VideoCodec* codec_settings,
                                 const Settings& settings) {
  if (codec_settings == nullptr) {
    RTC_LOG(LS_WARNING) << "No codec settings provided to "
                           "LibaomAv1Encoder.";
    return WEBRTC_VIDEO_CODEC_ERR_PARAMETER;
  }
  if (settings.number_of_cores < 1) {
    return WEBRTC_VIDEO_CODEC_ERR_PARAMETER;
  }
  if (inited_) {
    RTC_LOG(LS_WARNING) << "Initing LibaomAv1Encoder without first releasing.";
    Release();
  }
  encoder_settings_ = *codec_settings;

  // Sanity checks for encoder configuration.
  const int32_t result = VerifyCodecSettings(encoder_settings_);
  if (result < 0) {
    RTC_LOG(LS_WARNING) << "Incorrect codec settings provided to "
                           "LibaomAv1Encoder.";
    return result;
  }
  if (encoder_settings_.numberOfSimulcastStreams > 1) {
    RTC_LOG(LS_WARNING) << "Simulcast is not implemented by LibaomAv1Encoder.";
    return result;
  }
  scalability_mode_ = encoder_settings_.GetScalabilityMode();
  if (!scalability_mode_.has_value()) {
    RTC_LOG(LS_WARNING) << "Scalability mode is not set, using 'L1T1'.";
    scalability_mode_ = ScalabilityMode::kL1T1;
  }
  svc_controller_ = CreateScalabilityStructure(*scalability_mode_);
  if (svc_controller_ == nullptr) {
    RTC_LOG(LS_WARNING) << "Failed to set scalability mode "
                        << static_cast<int>(*scalability_mode_);
    return WEBRTC_VIDEO_CODEC_ERR_PARAMETER;
  }

  // Initialize encoder configuration structure with default values
  aom_codec_err_t ret =
      aom_codec_enc_config_default(aom_codec_av1_cx(), &cfg_, kUsageProfile);
  if (ret != AOM_CODEC_OK) {
    RTC_LOG(LS_WARNING) << "LibaomAv1Encoder::EncodeInit returned " << ret
                        << " on aom_codec_enc_config_default.";
    return WEBRTC_VIDEO_CODEC_ERROR;
  }

  // Overwrite default config with input encoder settings & RTC-relevant values.
  cfg_.g_w = encoder_settings_.width;
  cfg_.g_h = encoder_settings_.height;
  cfg_.g_threads =
      NumberOfThreads(cfg_.g_w, cfg_.g_h, settings.number_of_cores);
  cfg_.g_timebase.num = 1;
  cfg_.g_timebase.den = kVideoPayloadTypeFrequency;
  cfg_.rc_target_bitrate = encoder_settings_.startBitrate;  // kilobits/sec.
  cfg_.rc_dropframe_thresh = encoder_settings_.GetFrameDropEnabled() ? 30 : 0;
  cfg_.g_input_bit_depth = kBitDepth;
  cfg_.kf_mode = AOM_KF_DISABLED;
  cfg_.rc_min_quantizer = kMinQp;
  cfg_.rc_max_quantizer = encoder_settings_.qpMax;
  cfg_.rc_undershoot_pct = 50;
  cfg_.rc_overshoot_pct = 50;
  cfg_.rc_buf_initial_sz = 600;
  cfg_.rc_buf_optimal_sz = 600;
  cfg_.rc_buf_sz = 1000;
  cfg_.g_usage = kUsageProfile;
  cfg_.g_error_resilient = 0;
  // Low-latency settings.
  cfg_.rc_end_usage = AOM_CBR;          // Constant Bit Rate (CBR) mode
  cfg_.g_pass = AOM_RC_ONE_PASS;        // One-pass rate control
  cfg_.g_lag_in_frames = kLagInFrames;  // No look ahead when lag equals 0.

  if (frame_for_encode_ != nullptr) {
    aom_img_free(frame_for_encode_);
    frame_for_encode_ = nullptr;
  }

  // Flag options: AOM_EFLAG_CALCULATE_PSNR and AOM_CODEC_USE_HIGHBITDEPTH
  aom_codec_flags_t flags = 0;

  // Initialize an encoder instance.
  ret = aom_codec_enc_init(&ctx_, aom_codec_av1_cx(), &cfg_, flags);
  if (ret != AOM_CODEC_OK) {
    RTC_LOG(LS_WARNING) << "LibaomAv1Encoder::EncodeInit returned " << ret
                        << " on aom_codec_enc_init.";
    return WEBRTC_VIDEO_CODEC_ERROR;
  }

  if (!SetSvcParams(svc_controller_->StreamConfig(), cfg_)) {
    return WEBRTC_VIDEO_CODEC_ERROR;
  }

  inited_ = true;

  // Set control parameters
  const int initial_cpu_speed = GetCpuSpeed(cfg_.g_w, cfg_.g_h);
  SET_ENCODER_PARAM_OR_RETURN_ERROR(AOME_SET_CPUUSED, initial_cpu_speed);
  fixed_speeds_.assign(SvcEnabled() ? svc_params_->number_spatial_layers : 1,
                       initial_cpu_speed);
  SET_ENCODER_PARAM_OR_RETURN_ERROR(AV1E_SET_ENABLE_CDEF, 1);
  SET_ENCODER_PARAM_OR_RETURN_ERROR(AV1E_SET_ENABLE_TPL_MODEL, 0);
  SET_ENCODER_PARAM_OR_RETURN_ERROR(AV1E_SET_DELTAQ_MODE, 0);
  SET_ENCODER_PARAM_OR_RETURN_ERROR(AV1E_SET_ENABLE_ORDER_HINT, 0);
  // AQ_MODE = 3 enables cyclic refresh.
  SET_ENCODER_PARAM_OR_RETURN_ERROR(AV1E_SET_AQ_MODE, 3);
  SET_ENCODER_PARAM_OR_RETURN_ERROR(AOME_SET_MAX_INTRA_BITRATE_PCT, 300);
  SET_ENCODER_PARAM_OR_RETURN_ERROR(AV1E_SET_COEFF_COST_UPD_FREQ, 3);
  SET_ENCODER_PARAM_OR_RETURN_ERROR(AV1E_SET_MODE_COST_UPD_FREQ, 3);
  SET_ENCODER_PARAM_OR_RETURN_ERROR(AV1E_SET_MV_COST_UPD_FREQ, 3);

  if (codec_settings->mode == VideoCodecMode::kScreensharing) {
    SET_ENCODER_PARAM_OR_RETURN_ERROR(AV1E_SET_TUNE_CONTENT,
                                      AOM_CONTENT_SCREEN);
    SET_ENCODER_PARAM_OR_RETURN_ERROR(AV1E_SET_ENABLE_PALETTE, 1);
  } else {
    SET_ENCODER_PARAM_OR_RETURN_ERROR(AV1E_SET_ENABLE_PALETTE, 0);
  }

  SET_ENCODER_PARAM_OR_RETURN_ERROR(AV1E_SET_AUTO_TILES, 1);
  SET_ENCODER_PARAM_OR_RETURN_ERROR(AV1E_SET_ROW_MT, 1);
  SET_ENCODER_PARAM_OR_RETURN_ERROR(AV1E_SET_ENABLE_OBMC, 0);
  SET_ENCODER_PARAM_OR_RETURN_ERROR(AV1E_SET_NOISE_SENSITIVITY, 0);
  SET_ENCODER_PARAM_OR_RETURN_ERROR(AV1E_SET_ENABLE_WARPED_MOTION, 0);
  SET_ENCODER_PARAM_OR_RETURN_ERROR(AV1E_SET_ENABLE_GLOBAL_MOTION, 0);
  SET_ENCODER_PARAM_OR_RETURN_ERROR(AV1E_SET_ENABLE_REF_FRAME_MVS, 0);
  SET_ENCODER_PARAM_OR_RETURN_ERROR(
      AV1E_SET_SUPERBLOCK_SIZE,
      GetSuperblockSize(cfg_.g_w, cfg_.g_h, cfg_.g_threads));
  SET_ENCODER_PARAM_OR_RETURN_ERROR(AV1E_SET_ENABLE_CFL_INTRA, 0);
  SET_ENCODER_PARAM_OR_RETURN_ERROR(AV1E_SET_ENABLE_SMOOTH_INTRA, 0);
  SET_ENCODER_PARAM_OR_RETURN_ERROR(AV1E_SET_ENABLE_ANGLE_DELTA, 0);
  SET_ENCODER_PARAM_OR_RETURN_ERROR(AV1E_SET_ENABLE_FILTER_INTRA, 0);
  SET_ENCODER_PARAM_OR_RETURN_ERROR(AV1E_SET_INTRA_DEFAULT_TX_ONLY, 1);
  SET_ENCODER_PARAM_OR_RETURN_ERROR(AV1E_SET_DISABLE_TRELLIS_QUANT, 1);
  SET_ENCODER_PARAM_OR_RETURN_ERROR(AV1E_SET_ENABLE_DIST_WTD_COMP, 0);
  SET_ENCODER_PARAM_OR_RETURN_ERROR(AV1E_SET_ENABLE_DIFF_WTD_COMP, 0);
  SET_ENCODER_PARAM_OR_RETURN_ERROR(AV1E_SET_ENABLE_DUAL_FILTER, 0);
  SET_ENCODER_PARAM_OR_RETURN_ERROR(AV1E_SET_ENABLE_INTERINTRA_COMP, 0);
  SET_ENCODER_PARAM_OR_RETURN_ERROR(AV1E_SET_ENABLE_INTERINTRA_WEDGE, 0);
  SET_ENCODER_PARAM_OR_RETURN_ERROR(AV1E_SET_ENABLE_INTRA_EDGE_FILTER, 0);
  SET_ENCODER_PARAM_OR_RETURN_ERROR(AV1E_SET_ENABLE_INTRABC, 0);
  SET_ENCODER_PARAM_OR_RETURN_ERROR(AV1E_SET_ENABLE_MASKED_COMP, 0);
  SET_ENCODER_PARAM_OR_RETURN_ERROR(AV1E_SET_ENABLE_PAETH_INTRA, 0);
  SET_ENCODER_PARAM_OR_RETURN_ERROR(AV1E_SET_ENABLE_QM, 0);
  SET_ENCODER_PARAM_OR_RETURN_ERROR(AV1E_SET_ENABLE_RECT_PARTITIONS, 0);
  SET_ENCODER_PARAM_OR_RETURN_ERROR(AV1E_SET_ENABLE_RESTORATION, 0);
  SET_ENCODER_PARAM_OR_RETURN_ERROR(AV1E_SET_ENABLE_SMOOTH_INTERINTRA, 0);
  SET_ENCODER_PARAM_OR_RETURN_ERROR(AV1E_SET_ENABLE_TX64, 0);
  SET_ENCODER_PARAM_OR_RETURN_ERROR(AV1E_SET_MAX_REFERENCE_FRAMES, 3);
  SET_ENCODER_PARAM_OR_RETURN_ERROR(AV1E_SET_MAX_CONSEC_FRAME_DROP_MS_CBR, 250);
  SET_ENCODER_PARAM_OR_RETURN_ERROR(AV1E_SET_POSTENCODE_DROP_RTC, 1);

  if (encoder_speed_experiment_.IsDynamicSpeedEnabled()) {
    const VideoCodecComplexity complexity =
        codec_settings->GetVideoEncoderComplexity();
    SetComplexity({&complexity, 1}, ComplexityControlMode::kVariable);
  }

  return WEBRTC_VIDEO_CODEC_OK;
}

template <typename P>
bool LibaomAv1Encoder::SetEncoderControlParameters(int param_id,
                                                   P param_value) {
  aom_codec_err_t error_code = aom_codec_control(&ctx_, param_id, param_value);
  if (error_code != AOM_CODEC_OK) {
    RTC_LOG(LS_WARNING)
        << "LibaomAv1Encoder::SetEncoderControlParameters returned "
        << error_code << " on id:  " << param_id << ".";
  }
  return error_code == AOM_CODEC_OK;
}

// Only positive speeds, range for real-time coding currently is: 6 - 10.
// Speed 11 is used for screen sharing.
// Lower means slower/better quality, higher means fastest/lower quality.
// Note: not used if dynamic speed controller is enabled.
int LibaomAv1Encoder::GetCpuSpeed(int width, int height) {
  if (!settings_.max_pixel_count_to_cpu_speed.empty()) {
    if (auto it =
            settings_.max_pixel_count_to_cpu_speed.lower_bound(width * height);
        it != settings_.max_pixel_count_to_cpu_speed.end()) {
      return it->second;
    }

    return 10;
  } else {
    if (encoder_settings_.mode == VideoCodecMode::kScreensharing) {
      return 11;
    }
    // For smaller resolutions, use lower speed setting (get some coding gain at
    // the cost of increased encoding complexity).
    switch (encoder_settings_.GetVideoEncoderComplexity()) {
      case VideoCodecComplexity::kComplexityHigh:
        if (width * height <= 320 * 180)
          return 8;
        else if (width * height <= 640 * 360)
          return 9;
        else
          return 10;
      case VideoCodecComplexity::kComplexityHigher:
        if (width * height <= 320 * 180)
          return 7;
        else if (width * height <= 640 * 360)
          return 8;
        else if (width * height <= 1280 * 720)
          return 9;
        else
          return 10;
      case VideoCodecComplexity::kComplexityMax:
        if (width * height <= 320 * 180)
          return 6;
        else if (width * height <= 640 * 360)
          return 7;
        else if (width * height <= 1280 * 720)
          return 8;
        else
          return 9;
      default:
        return 10;
    }
  }
}

int LibaomAv1Encoder::NumberOfThreads(int width,
                                      int height,
                                      int number_of_cores) {
  // Keep the number of encoder threads equal to the possible number of
  // column/row tiles, which is (1, 2, 4, 8). See comments below for
  // AV1E_SET_TILE_COLUMNS/ROWS.
  if (width * height > 1280 * 720 && number_of_cores > 8) {
    return 8;
  } else if (width * height >= 640 * 360 && number_of_cores > 4) {
    return 4;
  } else if (width * height >= 320 * 180 && number_of_cores > 2) {
    return 2;
  } else {
// Use 2 threads for low res on ARM.
#ifdef MOBILE_ARM
    if (width * height >= 320 * 180 && number_of_cores > 2) {
      return 2;
    }
#endif
    // 1 thread less than VGA.
    return 1;
  }
}

bool LibaomAv1Encoder::SetSvcParams(
    ScalableVideoController::StreamLayersConfig svc_config,
    const aom_codec_enc_cfg_t& encoder_config) {
  bool svc_enabled =
      svc_config.num_spatial_layers > 1 || svc_config.num_temporal_layers > 1;
  if (!svc_enabled) {
    svc_params_ = std::nullopt;
    return true;
  }
  if (svc_config.num_spatial_layers < 1 || svc_config.num_spatial_layers > 4) {
    RTC_LOG(LS_WARNING) << "Av1 supports up to 4 spatial layers. "
                        << svc_config.num_spatial_layers << " configured.";
    return false;
  }
  if (svc_config.num_temporal_layers < 1 ||
      svc_config.num_temporal_layers > 8) {
    RTC_LOG(LS_WARNING) << "Av1 supports up to 8 temporal layers. "
                        << svc_config.num_temporal_layers << " configured.";
    return false;
  }
  aom_svc_params_t& svc_params = svc_params_.emplace();
  svc_params.number_spatial_layers = svc_config.num_spatial_layers;
  svc_params.number_temporal_layers = svc_config.num_temporal_layers;

  int num_layers =
      svc_config.num_spatial_layers * svc_config.num_temporal_layers;
  for (int i = 0; i < num_layers; ++i) {
    svc_params.min_quantizers[i] = encoder_config.rc_min_quantizer;
    svc_params.max_quantizers[i] = encoder_config.rc_max_quantizer;
  }

  // Assume each temporal layer doubles framerate.
  for (int tid = 0; tid < svc_config.num_temporal_layers; ++tid) {
    svc_params.framerate_factor[tid] =
        1 << (svc_config.num_temporal_layers - tid - 1);
  }

  scaling_factors_den_.resize(svc_config.num_spatial_layers);
  scaling_factors_num_.resize(svc_config.num_spatial_layers);
  for (int sid = 0; sid < svc_config.num_spatial_layers; ++sid) {
    scaling_factors_num_[sid] = svc_config.scaling_factor_num[sid];
    svc_params.scaling_factor_num[sid] = svc_config.scaling_factor_num[sid];
    scaling_factors_den_[sid] = svc_config.scaling_factor_den[sid];
    svc_params.scaling_factor_den[sid] = svc_config.scaling_factor_den[sid];
    encoder_settings_.spatialLayers[sid].width = encoder_settings_.width *
                                                 scaling_factors_num_[sid] /
                                                 scaling_factors_den_[sid];
    encoder_settings_.spatialLayers[sid].height = encoder_settings_.height *
                                                  scaling_factors_num_[sid] /
                                                  scaling_factors_den_[sid];
  }

  // svc_params.layer_target_bitrate is set in SetRates() before svc_params is
  // passed to SetEncoderControlParameters(AV1E_SET_SVC_PARAMS).

  return true;
}

void LibaomAv1Encoder::SetSvcLayerId(
    const ScalableVideoController::LayerFrameConfig& layer_frame) {
  aom_svc_layer_id_t layer_id = {};
  layer_id.spatial_layer_id = layer_frame.SpatialId();
  layer_id.temporal_layer_id = layer_frame.TemporalId();
  SetEncoderControlParameters(AV1E_SET_SVC_LAYER_ID, &layer_id);
}

void LibaomAv1Encoder::SetSvcRefFrameConfig(
    const ScalableVideoController::LayerFrameConfig& layer_frame) {
  // Buffer name to use for each layer_frame.buffers position. In particular
  // when there are 2 buffers are referenced, prefer name them last and golden,
  // because av1 bitstream format has dedicated fields for these two names.
  // See last_frame_idx and golden_frame_idx in the av1 spec
  // https://aomediacodec.github.io/av1-spec/av1-spec.pdf
  static constexpr int kPreferedSlotName[] = {0,  // Last
                                              3,  // Golden
                                              1, 2, 4, 5, 6};
  static constexpr int kAv1NumBuffers = 8;

  aom_svc_ref_frame_config_t ref_frame_config = {};
  RTC_CHECK_LE(layer_frame.Buffers().size(), std::size(kPreferedSlotName));
  for (size_t i = 0; i < layer_frame.Buffers().size(); ++i) {
    const CodecBufferUsage& buffer = layer_frame.Buffers()[i];
    int slot_name = kPreferedSlotName[i];
    RTC_CHECK_GE(buffer.id, 0);
    RTC_CHECK_LT(buffer.id, kAv1NumBuffers);
    ref_frame_config.ref_idx[slot_name] = buffer.id;
    if (buffer.referenced) {
      ref_frame_config.reference[slot_name] = 1;
    }
    if (buffer.updated) {
      ref_frame_config.refresh[buffer.id] = 1;
    }
  }

  SetEncoderControlParameters(AV1E_SET_SVC_REF_FRAME_CONFIG, &ref_frame_config);
}

int32_t LibaomAv1Encoder::RegisterEncodeCompleteCallback(
    EncodedImageCallback* encoded_image_callback) {
  encoded_image_callback_ = encoded_image_callback;
  return WEBRTC_VIDEO_CODEC_OK;
}

int32_t LibaomAv1Encoder::Release() {
  if (frame_for_encode_ != nullptr) {
    aom_img_free(frame_for_encode_);
    frame_for_encode_ = nullptr;
  }
  if (inited_) {
    if (aom_codec_destroy(&ctx_)) {
      return WEBRTC_VIDEO_CODEC_MEMORY;
    }
    inited_ = false;
  }
  speed_controllers_.clear();
  fixed_speeds_.clear();
  rates_configured_ = false;
  return WEBRTC_VIDEO_CODEC_OK;
}

void LibaomAv1Encoder::MaybeRewrapImgWithFormat(const aom_img_fmt_t fmt,
                                                unsigned int width,
                                                unsigned int height) {
  if (!frame_for_encode_) {
    RTC_LOG(LS_INFO) << "Configuring AV1 encoder pixel format to "
                     << (fmt == AOM_IMG_FMT_NV12 ? "NV12" : "I420") << " "
                     << width << "x" << height;
    frame_for_encode_ = aom_img_wrap(nullptr, fmt, width, height, 1, nullptr);
  } else if (frame_for_encode_->fmt != fmt || frame_for_encode_->d_w != width ||
             frame_for_encode_->d_h != height) {
    RTC_LOG(LS_INFO) << "Switching AV1 encoder pixel format to "
                     << (fmt == AOM_IMG_FMT_NV12 ? "NV12" : "I420") << " "
                     << width << "x" << height;
    aom_img_free(frame_for_encode_);
    frame_for_encode_ = aom_img_wrap(nullptr, fmt, width, height, 1, nullptr);
  }
  // else no-op since the image is already in the right format.
}

void LibaomAv1Encoder::AdjustScalingFactorsForTopActiveLayer() {
  if (!SvcEnabled())
    return;
  last_active_layer_ = svc_params_->number_spatial_layers - 1;
  for (int sid = 0; sid < svc_params_->number_spatial_layers; ++sid) {
    for (int tid = 0; tid < svc_params_->number_temporal_layers; ++tid) {
      int layer_index = sid * svc_params_->number_temporal_layers + tid;
      if (svc_params_->layer_target_bitrate[layer_index] > 0) {
        last_active_layer_ = sid;
      }
    }
  }
  if (static_cast<int>(cfg_.g_w) ==
      encoder_settings_.spatialLayers[last_active_layer_].width) {
    return;
  }

  cfg_.g_w = encoder_settings_.spatialLayers[last_active_layer_].width;
  cfg_.g_h = encoder_settings_.spatialLayers[last_active_layer_].height;

  // Recalculate scaling factors ignoring top inactive layers.
  // Divide all by scaling factor of the last active layer.
  for (int i = 0; i <= last_active_layer_; ++i) {
    int n = scaling_factors_num_[i] * scaling_factors_den_[last_active_layer_];
    int d = scaling_factors_den_[i] * scaling_factors_num_[last_active_layer_];
    int gcd = std::gcd(n, d);
    svc_params_->scaling_factor_num[i] = n / gcd;
    svc_params_->scaling_factor_den[i] = d / gcd;
  }
  for (int i = last_active_layer_ + 1; i < svc_params_->number_spatial_layers;
       ++i) {
    svc_params_->scaling_factor_num[i] = 1;
    svc_params_->scaling_factor_den[i] = 1;
  }
}

EncoderSpeedController::ReferenceClass
LibaomAv1Encoder::AsSpeedControllerFrameType(
    const ScalableVideoController::LayerFrameConfig& layer_frame) const {
  if (layer_frame.IsKeyframe()) {
    return EncoderSpeedController::ReferenceClass::kKey;
  }

  int tid = layer_frame.TemporalId();
  if (tid == 0) {
    return EncoderSpeedController::ReferenceClass::kMain;
  } else if (svc_params_ && tid == svc_params_->number_temporal_layers - 1) {
    return EncoderSpeedController::ReferenceClass::kNoneReference;
  }
  return EncoderSpeedController::ReferenceClass::kIntermediate;
}

TimeDelta LibaomAv1Encoder::GetFrameInterval(int spatial_index) const {
  TimeDelta frame_interval =
      TimeDelta::Seconds(1) /
      (framerate_fps_ == 0 ? encoder_settings_.maxFramerate : framerate_fps_);

  if (!SvcEnabled()) {
    return frame_interval;
  }

  RTC_DCHECK_LT(spatial_index, svc_params_->number_spatial_layers);

  // Allocate a time slice for each spatial layer, proportional to the
  // fraction of pixels allocated for that layer.
  // E.g. if QVGA + VGA is used, 20% of the encoder time will be allocated
  // for QVGA + 80% for VGA - since VGA has 4x the number of pixels.
  int pixel_count_sum = 0;
  for (int si = 0; si < svc_params_->number_spatial_layers; ++si) {
    pixel_count_sum += encoder_settings_.spatialLayers[si].width *
                       encoder_settings_.spatialLayers[si].height;
  }

  double pixel_count_fraction =
      static_cast<double>(
          encoder_settings_.spatialLayers[spatial_index].width *
          encoder_settings_.spatialLayers[spatial_index].height) /
      pixel_count_sum;
  return frame_interval * pixel_count_fraction;
}

int32_t LibaomAv1Encoder::Encode(
    const VideoFrame& frame,
    const std::vector<VideoFrameType>* frame_types) {
  if (!inited_ || encoded_image_callback_ == nullptr || !rates_configured_) {
    return WEBRTC_VIDEO_CODEC_UNINITIALIZED;
  }

  bool keyframe_required =
      frame_types != nullptr &&
      absl::c_linear_search(*frame_types, VideoFrameType::kVideoFrameKey);

  std::vector<ScalableVideoController::LayerFrameConfig> layer_frames =
      svc_controller_->NextFrameConfig(keyframe_required);

  if (layer_frames.empty()) {
    RTC_LOG(LS_ERROR) << "SVCController returned no configuration for a frame.";
    return WEBRTC_VIDEO_CODEC_ERROR;
  }

  if (drop_repeat_frames_on_enhancement_layers_ && frame.is_repeat_frame()) {
    bool all_layers_droppable = !layer_frames.empty();
    for (const auto& layer_frame : layer_frames) {
      if (layer_frame.TemporalId() == 0) {
        all_layers_droppable = false;
        break;
      }
      if (auto it =
              last_encoded_timestamp_by_sid_.find(layer_frame.SpatialId());
          it != last_encoded_timestamp_by_sid_.end()) {
        // Get the time since the last encoded frame for this spatial layer.
        // Don't drop enhancement layer repeat frame if last encode was more
        // than 500ms ago.
        if ((frame.rtp_timestamp() - it->second) >
            kVideoPayloadTypeFrequency / 2) {
          all_layers_droppable = false;
          break;
        }
      }
    }

    if (all_layers_droppable) {
      RTC_LOG(LS_VERBOSE) << "Dropping repeat frame on enhancement layers.";
      for (const auto& layer_frame : layer_frames) {
        svc_controller_->OnEncodeDone(layer_frame);
      }
      return WEBRTC_VIDEO_CODEC_OK;  // Frame dropped
    }
  }

  scoped_refptr<VideoFrameBuffer> buffer = frame.video_frame_buffer();
  absl::InlinedVector<VideoFrameBuffer::Type, kMaxPreferredPixelFormats>
      supported_formats = {VideoFrameBuffer::Type::kI420,
                           VideoFrameBuffer::Type::kNV12};

  scoped_refptr<VideoFrameBuffer> scaled_image;
  if (!SvcEnabled() ||
      last_active_layer_ + 1 == svc_params_->number_spatial_layers) {
    scaled_image = buffer;
  } else {
    scaled_image = buffer->Scale(
        encoder_settings_.spatialLayers[last_active_layer_].width,
        encoder_settings_.spatialLayers[last_active_layer_].height);
    if (!scaled_image) {
      RTC_LOG(LS_ERROR) << "Failed to scale "
                        << VideoFrameBufferTypeToString(buffer->type())
                        << " image. Can't encode frame.";
      return WEBRTC_VIDEO_CODEC_ENCODER_FAILURE;
    }
  }

  scoped_refptr<VideoFrameBuffer> mapped_buffer;
  if (scaled_image->type() != VideoFrameBuffer::Type::kNative) {
    // `buffer` is already mapped.
    mapped_buffer = scaled_image;
  } else {
    // Attempt to map to one of the supported formats.
    mapped_buffer = scaled_image->GetMappedFrameBuffer(supported_formats);
  }

  // Convert input frame to I420, if needed.
  if (!mapped_buffer ||
      (absl::c_find(supported_formats, mapped_buffer->type()) ==
           supported_formats.end() &&
       mapped_buffer->type() != VideoFrameBuffer::Type::kI420A)) {
    scoped_refptr<I420BufferInterface> converted_buffer(buffer->ToI420());
    if (!converted_buffer) {
      RTC_LOG(LS_ERROR) << "Failed to convert "
                        << VideoFrameBufferTypeToString(
                               frame.video_frame_buffer()->type())
                        << " image to I420. Can't encode frame.";
      return WEBRTC_VIDEO_CODEC_ENCODER_FAILURE;
    }
    RTC_CHECK(converted_buffer->type() == VideoFrameBuffer::Type::kI420 ||
              converted_buffer->type() == VideoFrameBuffer::Type::kI420A);

    mapped_buffer = converted_buffer;
  }

  switch (mapped_buffer->type()) {
    case VideoFrameBuffer::Type::kI420:
    case VideoFrameBuffer::Type::kI420A: {
      // Set frame_for_encode_ data pointers and strides.
      MaybeRewrapImgWithFormat(AOM_IMG_FMT_I420, mapped_buffer->width(),
                               mapped_buffer->height());
      auto i420_buffer = mapped_buffer->GetI420();
      RTC_DCHECK(i420_buffer);

      // TODO: crbug.com/492213293 - Remove once the root cause is fixed.
      if (i420_buffer->StrideU() != i420_buffer->StrideV()) {
        RTC_LOG(LS_ERROR) << "Libaom requires the U and V strides to be equal.";
        return WEBRTC_VIDEO_CODEC_ENCODER_FAILURE;
      }

      RTC_CHECK_EQ(i420_buffer->width(), frame_for_encode_->d_w);
      RTC_CHECK_EQ(i420_buffer->height(), frame_for_encode_->d_h);
      frame_for_encode_->planes[AOM_PLANE_Y] =
          const_cast<unsigned char*>(i420_buffer->DataY());
      frame_for_encode_->planes[AOM_PLANE_U] =
          const_cast<unsigned char*>(i420_buffer->DataU());
      frame_for_encode_->planes[AOM_PLANE_V] =
          const_cast<unsigned char*>(i420_buffer->DataV());
      frame_for_encode_->stride[AOM_PLANE_Y] = i420_buffer->StrideY();
      frame_for_encode_->stride[AOM_PLANE_U] = i420_buffer->StrideU();
      frame_for_encode_->stride[AOM_PLANE_V] = i420_buffer->StrideV();
      break;
    }
    case VideoFrameBuffer::Type::kNV12: {
      MaybeRewrapImgWithFormat(AOM_IMG_FMT_NV12, mapped_buffer->width(),
                               mapped_buffer->height());
      const NV12BufferInterface* nv12_buffer = mapped_buffer->GetNV12();
      RTC_DCHECK(nv12_buffer);
      RTC_CHECK_EQ(nv12_buffer->width(), frame_for_encode_->d_w);
      RTC_CHECK_EQ(nv12_buffer->height(), frame_for_encode_->d_h);
      frame_for_encode_->planes[AOM_PLANE_Y] =
          const_cast<unsigned char*>(nv12_buffer->DataY());
      frame_for_encode_->planes[AOM_PLANE_U] =
          const_cast<unsigned char*>(nv12_buffer->DataUV());
      frame_for_encode_->planes[AOM_PLANE_V] = nullptr;
      frame_for_encode_->stride[AOM_PLANE_Y] = nv12_buffer->StrideY();
      frame_for_encode_->stride[AOM_PLANE_U] = nv12_buffer->StrideUV();
      frame_for_encode_->stride[AOM_PLANE_V] = 0;
      break;
    }
    default:
      return WEBRTC_VIDEO_CODEC_ENCODER_FAILURE;
  }

  const uint32_t duration = kVideoPayloadTypeFrequency / framerate_fps_;
  timestamp_ += duration;

  const size_t num_spatial_layers =
      svc_params_ ? svc_params_->number_spatial_layers : 1;
  auto next_layer_frame = layer_frames.begin();
  std::vector<std::pair<EncodedImage, CodecSpecificInfo>> encoded_images;
  // Index into `encoded_images` indicating the last active layer which produced
  // an encoded image. Used to correctly set `end_of_picture`.
  std::optional<size_t> last_encoded_image_index;
  for (size_t sid = 0; sid < num_spatial_layers; ++sid) {
    // The libaom AV1 encoder requires that `aom_codec_encode` is called for
    // every spatial layer, even if the configured bitrate for that layer is
    // zero. For zero bitrate spatial layers no frames will be produced.
    std::optional<ScalableVideoController::LayerFrameConfig>
        non_encoded_layer_frame;
    ScalableVideoController::LayerFrameConfig* layer_frame;
    if (next_layer_frame != layer_frames.end() &&
        next_layer_frame->SpatialId() == static_cast<int>(sid)) {
      layer_frame = &*next_layer_frame;
      ++next_layer_frame;
    } else {
      // For layers that are not encoded only the spatial id matters.
      non_encoded_layer_frame.emplace().S(sid);
      layer_frame = &*non_encoded_layer_frame;
    }

    aom_enc_frame_flags_t flags =
        layer_frame->IsKeyframe() ? AOM_EFLAG_FORCE_KF : 0;

    if (SvcEnabled()) {
      SetSvcLayerId(*layer_frame);
      SetSvcRefFrameConfig(*layer_frame);
    }

    EncodeResult output;
    if (!speed_controllers_.empty()) {
      RTC_DCHECK_GT(speed_controllers_.size(), sid);
      EncoderSpeedController& speed_controller = *speed_controllers_[sid];

      EncoderSpeedController::FrameEncodingInfo frame_info{
          .reference_type = AsSpeedControllerFrameType(*layer_frame),
          .is_repeat_frame = frame.is_repeat_frame(),
          .timestamp = Timestamp::Millis(frame.render_time_ms())};
      EncoderSpeedController::EncodeSettings settings =
          speed_controller.GetEncodeSettings(frame_info);

      if (settings.calculate_psnr && kEnablePsnrStats) {
        flags |= AOM_EFLAG_CALCULATE_PSNR;
      }

      // Encode with baseline settings in case a speed control probe is
      // requested.
      std::optional<EncodeResult> baseline_output;
      if (settings.baseline_comparison_speed.has_value()) {
        // Configure the desired speed setting.
        SET_ENCODER_PARAM_OR_RETURN_ERROR(AOME_SET_CPUUSED,
                                          *settings.baseline_comparison_speed);

        // Encode the frame, with the encoder internal state frozen - so that
        // rate control etc is not updated.
        baseline_output = DoEncode(
            duration, flags | AOM_EFLAG_FREEZE_INTERNAL_STATE, layer_frame);

        if (baseline_output->status_code != AOM_CODEC_OK) {
          RTC_LOG(LS_WARNING)
              << "LibaomAv1Encoder::Encode returned error: '"
              << aom_codec_err_to_string(baseline_output->status_code) << "'.";
          return WEBRTC_VIDEO_CODEC_ERROR;
        }

        if (!baseline_output->encoded_image.has_value()) {
          // Frame dropped, no point in trying to encode a second time.
          continue;
        }
      }

      SET_ENCODER_PARAM_OR_RETURN_ERROR(AOME_SET_CPUUSED, settings.speed);
      output = DoEncode(duration, flags, layer_frame);
      if (output.status_code != AOM_CODEC_OK) {
        RTC_LOG(LS_WARNING)
            << "LibaomAv1Encoder::Encode returned error: '"
            << aom_codec_err_to_string(output.status_code) << "'.";
        return WEBRTC_VIDEO_CODEC_ERROR;
      }

      if (non_encoded_layer_frame || !output.encoded_image.has_value()) {
        // Frame dropped, presumably by rate controller. This is not an error.
        if (baseline_output.has_value() &&
            baseline_output->encoded_image.has_value()) {
          // Second encoding dropped frame, but not the first. This is
          // unexpected. Use baseline encoding as main frame instead.
          output = *baseline_output;
          baseline_output.reset();
          RTC_LOG(LS_WARNING) << "AV1 Encoder unexpectedly dropped frame on "
                              << "second encoding of PSNR probe.";
        } else {
          EncodedImage dropped_image;
          dropped_image.SetSpatialIndex(sid);
          dropped_image.set_end_of_temporal_unit(sid == num_spatial_layers - 1);
          encoded_images.emplace_back(std::move(dropped_image),
                                      CodecSpecificInfo());
          continue;
        }
      }

      speed_controller.OnEncodedFrame(
          ToSpeedControllerEncodeResult(output, frame_info, settings.speed),
          baseline_output.has_value()
              ? std::optional<EncoderSpeedController::EncodeResults>(
                    ToSpeedControllerEncodeResult(
                        *baseline_output, frame_info,
                        *settings.baseline_comparison_speed))
              : std::nullopt);

    } else {
      // No speed controller used.

      if (kEnablePsnrStats && psnr_frame_sampler_.ShouldBeSampled(frame)) {
        flags |= AOM_EFLAG_CALCULATE_PSNR;
      }

      RTC_DCHECK_GT(fixed_speeds_.size(), sid);
      SET_ENCODER_PARAM_OR_RETURN_ERROR(AOME_SET_CPUUSED, fixed_speeds_[sid]);
      output = DoEncode(duration, flags, layer_frame);
      if (output.status_code != AOM_CODEC_OK) {
        RTC_LOG(LS_WARNING)
            << "LibaomAv1Encoder::Encode returned error: '"
            << aom_codec_err_to_string(output.status_code) << "'.";
        return WEBRTC_VIDEO_CODEC_ERROR;
      }
      if (non_encoded_layer_frame || !output.encoded_image.has_value()) {
        // Frame dropped, presumably by rate controller. This is not an error.
        EncodedImage dropped_image;
        dropped_image.SetSpatialIndex(sid);
        dropped_image.set_end_of_temporal_unit(sid == num_spatial_layers - 1);
        encoded_images.emplace_back(std::move(dropped_image),
                                    CodecSpecificInfo());
        continue;
      }
    }

    RTC_DCHECK(output.encoded_image.has_value());
    RTC_DCHECK_GT(output.encoded_image->size(), 0u);

    PopulateEncodedImageFromVideoFrame(frame, *output.encoded_image);
    CodecSpecificInfo codec_specifics =
        CreateCodecSpecificInfo(*output.encoded_image, *layer_frame);

    output.encoded_image->set_end_of_temporal_unit(sid ==
                                                   num_spatial_layers - 1);
    last_encoded_image_index = encoded_images.size();
    encoded_images.emplace_back(std::move(*output.encoded_image),
                                std::move(codec_specifics));
  }

  for (size_t i = 0; i < encoded_images.size(); ++i) {
    auto& [encoded_image, codec_specifics] = encoded_images[i];
    codec_specifics.end_of_picture = i == last_encoded_image_index;
    if (encoded_image.size() > 0) {
      encoded_image_callback_->OnEncodedImage(encoded_image, &codec_specifics);
      if (encoded_image.SpatialIndex().has_value()) {
        last_encoded_timestamp_by_sid_[*encoded_image.SpatialIndex()] =
            frame.rtp_timestamp();
      }
      continue;
    }
    // Size 0 indicates a dropped frame (inserted above).
    encoded_image_callback_->OnFrameDropped(
        frame.rtp_timestamp(), *encoded_image.SpatialIndex(),
        *encoded_image.is_end_of_temporal_unit());
  }

  return WEBRTC_VIDEO_CODEC_OK;
}

EncodeResult LibaomAv1Encoder::DoEncode(
    uint32_t duration,
    aom_enc_frame_flags_t flags,
    ScalableVideoController::LayerFrameConfig* layer_frame) {
  // Encode a frame. The presentation timestamp `pts` should not use real
  // timestamps from frames or the wall clock, as that can cause the rate
  // controller to misbehave.
  EncodeResult output;

  Timestamp start_time = realtime_clock_->CurrentTime();
  output.status_code =
      aom_codec_encode(&ctx_, frame_for_encode_, timestamp_, duration, flags);
  output.encode_time = realtime_clock_->CurrentTime() - start_time;

  if (output.status_code != AOM_CODEC_OK) {
    return output;
  }

  // Get encoded image data.
  aom_codec_iter_t iter = nullptr;
  int data_pkt_count = 0;
  output.encoded_image.emplace();
  EncodedImage& encoded_image = *output.encoded_image;
  const aom_codec_cx_pkt_t* pkt = nullptr;
  while ((pkt = aom_codec_get_cx_data(&ctx_, &iter)) != nullptr) {
    if (pkt->kind == AOM_CODEC_CX_FRAME_PKT && pkt->data.frame.sz > 0) {
      if (data_pkt_count > 0) {
        RTC_LOG(LS_WARNING) << "LibaomAv1Encoder::Encoder returned more than "
                               "one data packet for an input video frame.";
        Release();
        output.status_code = AOM_CODEC_ERROR;
        return output;
      }
      encoded_image.SetEncodedData(EncodedImageBuffer::Create(
          /*data=*/static_cast<const uint8_t*>(pkt->data.frame.buf),
          /*size=*/pkt->data.frame.sz));

      if ((pkt->data.frame.flags & AOM_FRAME_IS_KEY) != 0) {
        layer_frame->Keyframe();
      }

      encoded_image.set_frame_type(layer_frame->IsKeyframe()
                                       ? VideoFrameType::kVideoFrameKey
                                       : VideoFrameType::kVideoFrameDelta);

      encoded_image.content_type_ = VideoContentType::UNSPECIFIED;
      // If encoded image width/height info are added to aom_codec_cx_pkt_t,
      // use those values in lieu of the values in frame.
      if (svc_params_) {
        int n = scaling_factors_num_[layer_frame->SpatialId()];
        int d = scaling_factors_den_[layer_frame->SpatialId()];
        encoded_image._encodedWidth = encoder_settings_.width * n / d;
        encoded_image._encodedHeight = encoder_settings_.height * n / d;
        encoded_image.SetSpatialIndex(layer_frame->SpatialId());
        encoded_image.SetTemporalIndex(layer_frame->TemporalId());
      } else {
        encoded_image._encodedWidth = cfg_.g_w;
        encoded_image._encodedHeight = cfg_.g_h;
      }
      encoded_image.timing_.flags = VideoSendTiming::kInvalid;

      if (!SetEncoderControlParameters(AOME_GET_LAST_QUANTIZER,
                                       &encoded_image.qp_)) {
        RTC_LOG(LS_WARNING) << "Unable to fetch QP for frame.";
        output.status_code = AOM_CODEC_ERROR;
        return output;
      }

      ++data_pkt_count;
    } else if (pkt->kind == AOM_CODEC_PSNR_PKT) {
      // PSNR index: 0: total, 1: Y, 2: U, 3: V
      encoded_image.set_psnr(EncodedImage::Psnr({.y = pkt->data.psnr.psnr[1],
                                                 .u = pkt->data.psnr.psnr[2],
                                                 .v = pkt->data.psnr.psnr[3]}));
    }
  }

  if (encoded_image.size() == 0) {
    // Encode success, but no image produced. Frame as just dropped.
    output.encoded_image.reset();
  }

  return output;
}

CodecSpecificInfo LibaomAv1Encoder::CreateCodecSpecificInfo(
    const EncodedImage& image,
    const ScalableVideoController::LayerFrameConfig& layer_frame) {
  CodecSpecificInfo codec_specific_info;
  codec_specific_info.codecType = kVideoCodecAV1;
  codec_specific_info.scalability_mode = scalability_mode_;
  bool is_keyframe = layer_frame.IsKeyframe();
  codec_specific_info.generic_frame_info =
      svc_controller_->OnEncodeDone(layer_frame);
  if (is_keyframe && codec_specific_info.generic_frame_info) {
    codec_specific_info.template_structure =
        svc_controller_->DependencyStructure();
    auto& resolutions = codec_specific_info.template_structure->resolutions;
    if (SvcEnabled()) {
      resolutions.resize(svc_params_->number_spatial_layers);
      for (int sid = 0; sid < svc_params_->number_spatial_layers; ++sid) {
        int n = scaling_factors_num_[sid];
        int d = scaling_factors_den_[sid];
        resolutions[sid] = RenderResolution(encoder_settings_.width * n / d,
                                            encoder_settings_.height * n / d);
      }
    } else {
      resolutions = {RenderResolution(cfg_.g_w, cfg_.g_h)};
    }
  }
  return codec_specific_info;
}

void LibaomAv1Encoder::SetRates(const RateControlParameters& parameters) {
  if (!inited_) {
    RTC_LOG(LS_WARNING) << "SetRates() while encoder is not initialized";
    return;
  }
  if (parameters.framerate_fps < kMinFrameRateFps) {
    RTC_LOG(LS_WARNING) << "Unsupported framerate (must be >= "
                        << kMinFrameRateFps
                        << " ): " << parameters.framerate_fps;
    return;
  }
  if (parameters.bitrate.get_sum_bps() == 0) {
    RTC_LOG(LS_WARNING) << "Attempt to set target bit rate to zero";
    return;
  }

  // The bitrates caluclated internally in libaom when `AV1E_SET_SVC_PARAMS` is
  // called depends on the currently configured `rc_target_bitrate`. If the
  // total target bitrate is not updated first a division by zero could happen.
  svc_controller_->OnRatesUpdated(parameters.bitrate);
  cfg_.rc_target_bitrate = parameters.bitrate.get_sum_kbps();

  if (SvcEnabled()) {
    for (int sid = 0; sid < svc_params_->number_spatial_layers; ++sid) {
      // libaom bitrate for spatial id S and temporal id T means bitrate
      // of frames with spatial_id=S and temporal_id<=T.
      for (int tid = 0; tid < svc_params_->number_temporal_layers; ++tid) {
        int layer_index = sid * svc_params_->number_temporal_layers + tid;
        // `svc_params_->layer_target_bitrate` expects bitrate in kbps.
        svc_params_->layer_target_bitrate[layer_index] =
            parameters.bitrate.GetTemporalLayerSum(sid, tid) / 1000;
      }
    }
    AdjustScalingFactorsForTopActiveLayer();
    SetEncoderControlParameters(AV1E_SET_SVC_PARAMS, &*svc_params_);
  }

  // AdjustScalingFactorsForTopActiveLayer() may update `cfg_`.
  aom_codec_err_t error_code = aom_codec_enc_config_set(&ctx_, &cfg_);
  if (error_code != AOM_CODEC_OK) {
    RTC_LOG(LS_WARNING) << "Error configuring encoder, error code: "
                        << error_code;
  }

  framerate_fps_ = parameters.framerate_fps;
  for (size_t si = 0; si < speed_controllers_.size(); ++si) {
    speed_controllers_[si]->SetFrameInterval(GetFrameInterval(si));
  }

  rates_configured_ = true;
}

void LibaomAv1Encoder::SetComplexity(
    std::span<const VideoCodecComplexity> complexity_by_spatial_layer,
    ComplexityControlMode complexity_control_mode) {
  if (!inited_) {
    return;
  }

  const size_t num_spatial_layers =
      SvcEnabled() ? svc_params_->number_spatial_layers : 1;
  auto get_layer_complexity = [&](size_t si) {
    if (complexity_by_spatial_layer.empty()) {
      return VideoCodecComplexity::kComplexityNormal;
    }
    return complexity_by_spatial_layer[std::min(
        si, complexity_by_spatial_layer.size() - 1)];
  };

  switch (complexity_control_mode) {
    case ComplexityControlMode::kConstant:
      speed_controllers_.clear();
      for (size_t si = 0; si < num_spatial_layers; ++si) {
        switch (get_layer_complexity(si)) {
          case VideoCodecComplexity::kComplexityLow:
            fixed_speeds_[si] = 11;
            break;
          case VideoCodecComplexity::kComplexityNormal:
            fixed_speeds_[si] = 10;
            break;
          case VideoCodecComplexity::kComplexityHigh:
            fixed_speeds_[si] = 9;
            break;
          case VideoCodecComplexity::kComplexityHigher:
            fixed_speeds_[si] = 8;
            break;
          case VideoCodecComplexity::kComplexityMax:
            fixed_speeds_[si] = 5;
            break;
        }
      }
      break;
    case ComplexityControlMode::kVariable: {
      const int num_temporal_layers =
          SvcEnabled() ? svc_controller_->StreamConfig().num_temporal_layers
                       : 1;
      speed_controllers_.resize(num_spatial_layers);
      for (size_t si = 0; si < num_spatial_layers; ++si) {
        LibaomSpeedConfigFactory speed_config_factory(get_layer_complexity(si),
                                                      encoder_settings_.mode);
        const int width =
            SvcEnabled() ? std::span(encoder_settings_.spatialLayers)[si].width
                         : encoder_settings_.width;
        const int height =
            SvcEnabled() ? std::span(encoder_settings_.spatialLayers)[si].height
                         : encoder_settings_.height;
        EncoderSpeedController::Config speed_config =
            speed_config_factory.GetSpeedConfig(width, height,
                                                num_temporal_layers);
        speed_controllers_[si] =
            EncoderSpeedController::Create(speed_config, GetFrameInterval(si));
      }
      break;
    }
  }
}

VideoEncoder::EncoderInfo LibaomAv1Encoder::GetEncoderInfo() const {
  EncoderInfo info;
  info.supports_native_handle = false;
  info.implementation_name = "libaom";
  info.has_trusted_rate_controller = true;
  info.is_hardware_accelerated = false;
  info.scaling_settings =
      (inited_ && !encoder_settings_.AV1().automatic_resize_on)
          ? VideoEncoder::ScalingSettings::kOff
          : VideoEncoder::ScalingSettings(kLowQindex, kHighQindex);
  info.preferred_pixel_formats = {VideoFrameBuffer::Type::kI420,
                                  VideoFrameBuffer::Type::kNV12};
  if (inited_) {
    info.mapped_resolution = VideoEncoder::Resolution(cfg_.g_w, cfg_.g_h);
  }
  if (SvcEnabled()) {
    for (int sid = 0; sid < svc_params_->number_spatial_layers; ++sid) {
      info.fps_allocation[sid].resize(svc_params_->number_temporal_layers);
      for (int tid = 0; tid < svc_params_->number_temporal_layers; ++tid) {
        info.fps_allocation[sid][tid] = EncoderInfo::kMaxFramerateFraction /
                                        svc_params_->framerate_factor[tid];
      }
    }
  }
  if (!encoder_info_override_.resolution_bitrate_limits().empty()) {
    info.resolution_bitrate_limits =
        encoder_info_override_.resolution_bitrate_limits();
  }

  info.min_qp = kMinQindex;
  return info;
}

}  // namespace

absl_nonnull std::unique_ptr<VideoEncoder> CreateLibaomAv1Encoder(
    const Environment& env,
    LibaomAv1EncoderSettings settings) {
  return std::make_unique<LibaomAv1Encoder>(env, std::move(settings));
}

}  // namespace webrtc
