/*
 * Copyright (c) 2019, Alliance for Open Media. All rights reserved.
 *
 * This source code is subject to the terms of the BSD 2 Clause License and
 * the Alliance for Open Media Patent License 1.0. If the BSD 2 Clause License
 * was not distributed with this source code in the LICENSE file, you can
 * obtain it at www.aomedia.org/license/software. If the Alliance for Open
 * Media Patent License 1.0 was not distributed with this source code in the
 * PATENTS file, you can obtain it at www.aomedia.org/license/patent.
 */

#include <climits>
#include <vector>
#include "config/aom_config.h"
#include "gtest/gtest.h"
#include "test/codec_factory.h"
#include "test/datarate_test.h"
#include "test/encode_test_driver.h"
#include "test/i420_video_source.h"
#include "test/util.h"
#include "test/y4m_video_source.h"
#include "aom/aom_codec.h"
#include "av1/common/enums.h"
#include "av1/encoder/encoder.h"

namespace datarate_test {
namespace {

struct FrameInfo {
  FrameInfo(aom_codec_pts_t _pts, unsigned int _w, unsigned int _h)
      : pts(_pts), w(_w), h(_h) {}

  aom_codec_pts_t pts;
  unsigned int w;
  unsigned int h;
};

void ScaleForFrameNumber(unsigned int frame, unsigned int initial_w,
                         unsigned int initial_h, unsigned int *w,
                         unsigned int *h, int resize_pattern) {
  *w = initial_w;
  *h = initial_h;
  if (resize_pattern == 1) {
    if (frame < 50) {
      *w = initial_w / 4;
      *h = initial_h / 4;
    } else if (frame < 100) {
      *w = initial_w / 2;
      *h = initial_h / 2;
    } else if (frame < 150) {
      *w = initial_w;
      *h = initial_h;
    } else if (frame < 200) {
      *w = initial_w / 4;
      *h = initial_h / 4;
    } else if (frame < 250) {
      *w = initial_w / 2;
      *h = initial_h / 2;
    }
  } else if (resize_pattern == 2) {
    if (frame < 50) {
      *w = initial_w / 2;
      *h = initial_h / 2;
    } else if (frame < 100) {
      *w = initial_w / 4;
      *h = initial_h / 4;
    } else if (frame < 150) {
      *w = initial_w;
      *h = initial_h;
    } else if (frame < 200) {
      *w = initial_w / 2;
      *h = initial_h / 2;
    } else if (frame < 250) {
      *w = initial_w / 4;
      *h = initial_h / 4;
    }
  }
}

class ResizingVideoSource : public ::libaom_test::DummyVideoSource {
 public:
  explicit ResizingVideoSource(int external_resize_pattern) {
    external_resize_pattern_ = external_resize_pattern;
    SetSize(1280, 720);
    limit_ = 300;
  }
  ~ResizingVideoSource() override = default;

 protected:
  void Next() override {
    ++frame_;
    unsigned int width = 0;
    unsigned int height = 0;
    libaom_test::ACMRandom rnd(libaom_test::ACMRandom::DeterministicSeed());
    ScaleForFrameNumber(frame_, 1280, 720, &width, &height,
                        external_resize_pattern_);
    SetSize(width, height);
    FillFrame();
    unsigned char *image = img_->planes[0];
    for (size_t i = 0; i < raw_sz_; ++i) {
      image[i] = rnd.Rand8();
    }
  }

 private:
  int external_resize_pattern_;
};

class DatarateTestSVC
    : public ::libaom_test::CodecTestWith4Params<libaom_test::TestMode, int,
                                                 unsigned int, int>,
      public DatarateTest {
 public:
  DatarateTestSVC() : DatarateTest(GET_PARAM(0)) {
    set_cpu_used_ = GET_PARAM(2);
    aq_mode_ = GET_PARAM(3);
  }

 protected:
  void SetUp() override {
    InitializeConfig(GET_PARAM(1));
    ResetModel();
  }

  void DecompressedFrameHook(const aom_image_t &img,
                             aom_codec_pts_t pts) override {
    frame_info_list_.push_back(FrameInfo(pts, img.d_w, img.d_h));
    ++decoded_nframes_;
  }

  std::vector<FrameInfo> frame_info_list_;

  int GetNumSpatialLayers() override { return number_spatial_layers_; }

  void ResetModel() override {
    DatarateTest::ResetModel();
    layer_frame_cnt_ = 0;
    superframe_cnt_ = 0;
    number_temporal_layers_ = 1;
    number_spatial_layers_ = 1;
    for (int i = 0; i < AOM_MAX_LAYERS; i++) {
      target_layer_bitrate_[i] = 0;
      effective_datarate_tl[i] = 0.0;
    }
    memset(&layer_id_, 0, sizeof(aom_svc_layer_id_t));
    memset(&svc_params_, 0, sizeof(aom_svc_params_t));
    memset(&ref_frame_config_, 0, sizeof(aom_svc_ref_frame_config_t));
    memset(&ref_frame_comp_pred_, 0, sizeof(aom_svc_ref_frame_comp_pred_t));
    drop_frames_ = 0;
    for (int i = 0; i < 1000; i++) drop_frames_list_[i] = 1000;
    decoded_nframes_ = 0;
    mismatch_nframes_ = 0;
    mismatch_psnr_ = 0.0;
    set_frame_level_er_ = 0;
    multi_ref_ = 0;
    use_fixed_mode_svc_ = 0;
    comp_pred_ = 0;
    dynamic_enable_disable_mode_ = 0;
    intra_only_ = 0;
    intra_only_single_layer_ = false;
    frame_to_start_decoding_ = 0;
    layer_to_decode_ = 0;
    frame_sync_ = 0;
    current_video_frame_ = 0;
    screen_mode_ = 0;
    rps_mode_ = 0;
    rps_recovery_frame_ = 0;
    user_define_frame_qp_ = 0;
    set_speed_per_layer_ = false;
    simulcast_mode_ = false;
    use_last_as_scaled_ = false;
    use_last_as_scaled_single_ref_ = false;
    external_resize_dynamic_drop_layer_ = false;
    external_resize_pattern_ = 0;
  }

  void PreEncodeFrameHook(::libaom_test::VideoSource *video,
                          ::libaom_test::Encoder *encoder) override {
    int spatial_layer_id = 0;
    current_video_frame_ = video->frame();

    // One-time initialization only done on the first frame.
    if (video->frame() == 0 && layer_frame_cnt_ == 0) {
      initialize_svc(number_temporal_layers_, number_spatial_layers_,
                     &svc_params_);
      if (dynamic_enable_disable_mode_ == 1) {
        svc_params_.layer_target_bitrate[2] = 0;
        cfg_.rc_target_bitrate -= target_layer_bitrate_[2];
      }
      encoder->Control(AV1E_SET_SVC_PARAMS, &svc_params_);
      // TODO(aomedia:3032): Configure KSVC in fixed mode.
      encoder->Control(AV1E_SET_ENABLE_ORDER_HINT, 0);
      encoder->Control(AV1E_SET_ENABLE_TPL_MODEL, 0);
      encoder->Control(AV1E_SET_DELTAQ_MODE, 0);
      if (cfg_.g_threads > 1) {
        if (auto_tiles_) {
          encoder->Control(AV1E_SET_AUTO_TILES, 1);
        } else {
          encoder->Control(AV1E_SET_TILE_COLUMNS, tile_columns_);
          encoder->Control(AV1E_SET_TILE_ROWS, tile_rows_);
        }
        encoder->Control(AV1E_SET_ROW_MT, 1);
      }
      if (screen_mode_) {
        encoder->Control(AV1E_SET_TUNE_CONTENT, AOM_CONTENT_SCREEN);
      }
      encoder->Control(AV1E_SET_POSTENCODE_DROP_RTC, 1);
      // We want to force external resize on the very first frame.
      // Turn off frame-dropping.
      if (external_resize_dynamic_drop_layer_) {
        encoder->Control(AV1E_SET_POSTENCODE_DROP_RTC, 0);
        DatarateTest::PreEncodeFrameHook(video, encoder);
        video->Next();
      }
    }
    if (number_spatial_layers_ == 2) {
      spatial_layer_id = (layer_frame_cnt_ % 2 == 0) ? 0 : 1;
    } else if (number_spatial_layers_ == 3) {
      spatial_layer_id = (layer_frame_cnt_ % 3 == 0)         ? 0
                         : ((layer_frame_cnt_ - 1) % 3 == 0) ? 1
                                                             : 2;
    }
    // Set the reference/update flags, layer_id, and reference_map
    // buffer index.
    frame_flags_ = set_layer_pattern(
        video->frame(), &layer_id_, &ref_frame_config_, &ref_frame_comp_pred_,
        spatial_layer_id, multi_ref_, comp_pred_,
        (video->frame() % cfg_.kf_max_dist) == 0, dynamic_enable_disable_mode_,
        rps_mode_, rps_recovery_frame_, simulcast_mode_, use_last_as_scaled_,
        use_last_as_scaled_single_ref_);
    if (intra_only_ == 1 && frame_sync_ > 0) {
      // Set an Intra-only frame on SL0 at frame_sync_.
      // In order to allow decoding to start on SL0 in mid-sequence we need to
      // set and refresh all the slots used on SL0 stream, which is 0 and 3
      // for this test pattern. The other slots (1, 2, 4, 5) are used for the
      // SL > 0 layers and these slotes are not refreshed on frame_sync_, so
      // temporal prediction for the top layers can continue.
      if (spatial_layer_id == 0 && video->frame() == frame_sync_) {
        ref_frame_config_.ref_idx[0] = 0;
        ref_frame_config_.ref_idx[3] = 3;
        ref_frame_config_.refresh[0] = 1;
        ref_frame_config_.refresh[3] = 1;
        for (int i = 0; i < INTER_REFS_PER_FRAME; i++)
          ref_frame_config_.reference[i] = 0;
      }
    }
    if (intra_only_ && video->frame() == 50 && spatial_layer_id == 1) {
      // Force an intra_only frame here, for SL1.
      for (int i = 0; i < INTER_REFS_PER_FRAME; i++)
        ref_frame_config_.reference[i] = 0;
    }
    encoder->Control(AV1E_SET_SVC_LAYER_ID, &layer_id_);
    // The SET_SVC_REF_FRAME_CONFIG and AV1E_SET_SVC_REF_FRAME_COMP_PRED api is
    // for the flexible SVC mode (i.e., use_fixed_mode_svc == 0).
    if (!use_fixed_mode_svc_) {
      encoder->Control(AV1E_SET_SVC_REF_FRAME_CONFIG, &ref_frame_config_);
      encoder->Control(AV1E_SET_SVC_REF_FRAME_COMP_PRED, &ref_frame_comp_pred_);
    }
    if (set_speed_per_layer_) {
      int speed_per_layer = 10;
      if (layer_id_.spatial_layer_id == 0) {
        // For for base SL0,TL0: use the speed the test loops over.
        if (layer_id_.temporal_layer_id == 1) speed_per_layer = 7;
        if (layer_id_.temporal_layer_id == 2) speed_per_layer = 8;
      } else if (layer_id_.spatial_layer_id == 1) {
        if (layer_id_.temporal_layer_id == 0) speed_per_layer = 7;
        if (layer_id_.temporal_layer_id == 1) speed_per_layer = 8;
        if (layer_id_.temporal_layer_id == 2) speed_per_layer = 9;
      } else if (layer_id_.spatial_layer_id == 2) {
        if (layer_id_.temporal_layer_id == 0) speed_per_layer = 8;
        if (layer_id_.temporal_layer_id == 1) speed_per_layer = 9;
        if (layer_id_.temporal_layer_id == 2) speed_per_layer = 10;
      }
      encoder->Control(AOME_SET_CPUUSED, speed_per_layer);
    }
    if (set_frame_level_er_) {
      int mode =
          (layer_id_.spatial_layer_id > 0 || layer_id_.temporal_layer_id > 0);
      encoder->Control(AV1E_SET_ERROR_RESILIENT_MODE, mode);
    }
    if (dynamic_enable_disable_mode_ == 1) {
      if (layer_frame_cnt_ == 300 && spatial_layer_id == 0) {
        // Enable: set top spatial layer bitrate back to non-zero.
        svc_params_.layer_target_bitrate[2] = target_layer_bitrate_[2];
        cfg_.rc_target_bitrate += target_layer_bitrate_[2];
        encoder->Config(&cfg_);
        encoder->Control(AV1E_SET_SVC_PARAMS, &svc_params_);
      }
    } else if (dynamic_enable_disable_mode_ == 2) {
      if (layer_frame_cnt_ == 300 && spatial_layer_id == 0) {
        // Disable top spatial layer mid-stream.
        svc_params_.layer_target_bitrate[2] = 0;
        cfg_.rc_target_bitrate -= target_layer_bitrate_[2];
        encoder->Config(&cfg_);
        encoder->Control(AV1E_SET_SVC_PARAMS, &svc_params_);
      } else if (layer_frame_cnt_ == 600 && spatial_layer_id == 0) {
        // Enable top spatial layer mid-stream.
        svc_params_.layer_target_bitrate[2] = target_layer_bitrate_[2];
        cfg_.rc_target_bitrate += target_layer_bitrate_[2];
        encoder->Config(&cfg_);
        encoder->Control(AV1E_SET_SVC_PARAMS, &svc_params_);
      }
    }
    if (external_resize_dynamic_drop_layer_) {
      frame_flags_ = 0;
      for (int i = 0; i < 9; ++i) {
        svc_params_.min_quantizers[i] = 20;
        svc_params_.max_quantizers[i] = 56;
      }
      if (layer_id_.spatial_layer_id == 0 &&
          (video->frame() == 1 || video->frame() == 150)) {
        // Set the new top width/height for external resize.
        top_sl_width_ = video->img()->d_w;
        top_sl_height_ = video->img()->d_h;
        for (int i = 0; i < 9; ++i) {
          bitrate_layer_[i] = svc_params_.layer_target_bitrate[i];
        }
        if (external_resize_pattern_ == 1) {
          // Input size is 1/4. 2 top spatial layers are dropped.
          // This will trigger skip encoding/dropping of two top spatial layers.
          cfg_.rc_target_bitrate -= svc_params_.layer_target_bitrate[5] +
                                    svc_params_.layer_target_bitrate[8];
          for (int i = 3; i < 9; ++i) {
            svc_params_.layer_target_bitrate[i] = 0;
          }
          for (int sl = 0; sl < 3; sl++) {
            svc_params_.scaling_factor_num[sl] = 1;
            svc_params_.scaling_factor_den[sl] = 1;
          }
        } else if (external_resize_pattern_ == 2) {
          // Input size is 1/2. Top spatial layer is dropped.
          // This will trigger skip encoding/dropping of top spatial layer.
          cfg_.rc_target_bitrate -= svc_params_.layer_target_bitrate[8];
          for (int i = 6; i < 9; ++i) {
            svc_params_.layer_target_bitrate[i] = 0;
          }
          svc_params_.scaling_factor_num[0] = 1;
          svc_params_.scaling_factor_den[0] = 2;
          svc_params_.scaling_factor_num[1] = 1;
          svc_params_.scaling_factor_den[1] = 1;
          svc_params_.scaling_factor_num[2] = 1;
          svc_params_.scaling_factor_den[2] = 1;
        }
        encoder->Config(&cfg_);
        encoder->Control(AV1E_SET_SVC_PARAMS, &svc_params_);
      } else if (layer_id_.spatial_layer_id == 0 &&
                 (video->frame() == 50 || video->frame() == 200)) {
        top_sl_width_ = video->img()->d_w;
        top_sl_height_ = video->img()->d_h;
        if (external_resize_pattern_ == 1) {
          // Input size is 1/2. Change layer bitrates to set top layer to 0.
          // This will trigger skip encoding/dropping of top spatial layer.
          cfg_.rc_target_bitrate += bitrate_layer_[5];
          for (int i = 3; i < 6; ++i) {
            svc_params_.layer_target_bitrate[i] = bitrate_layer_[i];
          }
          svc_params_.scaling_factor_num[0] = 1;
          svc_params_.scaling_factor_den[0] = 2;
          svc_params_.scaling_factor_num[1] = 1;
          svc_params_.scaling_factor_den[1] = 1;
          svc_params_.scaling_factor_num[2] = 1;
          svc_params_.scaling_factor_den[2] = 1;
        } else if (external_resize_pattern_ == 2) {
          // Input size is 1/4. Change layer bitrates to set two top layers to
          // 0. This will trigger skip encoding/dropping of two top spatial
          // layers.
          cfg_.rc_target_bitrate -= bitrate_layer_[5];
          for (int i = 3; i < 6; ++i) {
            svc_params_.layer_target_bitrate[i] = 0;
          }
          for (int sl = 0; sl < 3; sl++) {
            svc_params_.scaling_factor_num[sl] = 1;
            svc_params_.scaling_factor_den[sl] = 1;
          }
        }
        encoder->Config(&cfg_);
        encoder->Control(AV1E_SET_SVC_PARAMS, &svc_params_);
      } else if (layer_id_.spatial_layer_id == 0 &&
                 (video->frame() == 100 || video->frame() == 250)) {
        top_sl_width_ = video->img()->d_w;
        top_sl_height_ = video->img()->d_h;
        // Input is original size. Change layer bitrates to nonzero for all
        // layers.
        cfg_.rc_target_bitrate =
            bitrate_layer_[2] + bitrate_layer_[5] + bitrate_layer_[8];
        for (int i = 0; i < 9; ++i) {
          svc_params_.layer_target_bitrate[i] = bitrate_layer_[i];
        }
        svc_params_.scaling_factor_num[0] = 1;
        svc_params_.scaling_factor_den[0] = 4;
        svc_params_.scaling_factor_num[1] = 1;
        svc_params_.scaling_factor_den[1] = 2;
        svc_params_.scaling_factor_num[2] = 1;
        svc_params_.scaling_factor_den[2] = 1;
        encoder->Config(&cfg_);
        encoder->Control(AV1E_SET_SVC_PARAMS, &svc_params_);
      }
    }
    layer_frame_cnt_++;
    DatarateTest::PreEncodeFrameHook(video, encoder);
    if (user_define_frame_qp_) {
      frame_qp_ = rnd_.PseudoUniform(63);
      encoder->Control(AV1E_SET_QUANTIZER_ONE_PASS, frame_qp_);
    }
  }

  void PostEncodeFrameHook(::libaom_test::Encoder *encoder) override {
    int num_operating_points;
    encoder->Control(AV1E_GET_NUM_OPERATING_POINTS, &num_operating_points);
    ASSERT_EQ(num_operating_points,
              number_temporal_layers_ * number_spatial_layers_);

    if (user_define_frame_qp_) {
      if (current_video_frame_ >= static_cast<unsigned int>(total_frame_))
        return;
      int qp;
      encoder->Control(AOME_GET_LAST_QUANTIZER_64, &qp);
      ASSERT_EQ(qp, frame_qp_);
    }
  }

  void FramePktHook(const aom_codec_cx_pkt_t *pkt) override {
    const size_t frame_size_in_bits = pkt->data.frame.sz * 8;
    // Update the layer cumulative  bitrate.
    for (int i = layer_id_.temporal_layer_id; i < number_temporal_layers_;
         i++) {
      int layer = layer_id_.spatial_layer_id * number_temporal_layers_ + i;
      effective_datarate_tl[layer] += 1.0 * frame_size_in_bits;
    }
    if (layer_id_.spatial_layer_id == number_spatial_layers_ - 1) {
      last_pts_ = pkt->data.frame.pts;
      superframe_cnt_++;
    }
    // For simulcast mode: verify that for first frame to start decoding,
    // for SL > 0, are Intra-only frames (not Key), whereas SL0 is Key.
    if (simulcast_mode_ && superframe_cnt_ == (int)frame_to_start_decoding_) {
      if (layer_id_.spatial_layer_id > 0) {
        EXPECT_NE(pkt->data.frame.flags & AOM_FRAME_IS_KEY, AOM_FRAME_IS_KEY);
      } else if (layer_id_.spatial_layer_id == 0) {
        EXPECT_EQ(pkt->data.frame.flags & AOM_FRAME_IS_KEY, AOM_FRAME_IS_KEY);
      }
    }
    if (external_resize_dynamic_drop_layer_) {
      // No key frame is needed for these encoding patterns, except at the
      // very first frame.
      if (layer_frame_cnt_ > 1) {
        EXPECT_NE(pkt->data.frame.flags & AOM_FRAME_IS_KEY, AOM_FRAME_IS_KEY);
      }
    }
  }

  void EndPassHook() override {
    duration_ = ((last_pts_ + 1) * timebase_);
    for (int i = 0; i < number_temporal_layers_ * number_spatial_layers_; i++) {
      effective_datarate_tl[i] = (effective_datarate_tl[i] / 1000) / duration_;
    }
  }

  bool DoDecode() const override {
    if (drop_frames_ > 0) {
      for (unsigned int i = 0; i < drop_frames_; ++i) {
        if (drop_frames_list_[i] == (unsigned int)superframe_cnt_) {
          std::cout << "             Skipping decoding frame: "
                    << drop_frames_list_[i] << "\n";
          return false;
        }
      }
    } else if (intra_only_ == 1) {
      // Only start decoding at frames_to_start_decoding_.
      if (current_video_frame_ < frame_to_start_decoding_) return false;
      // Only decode base layer for 3SL, for layer_to_decode_ = 0.
      if (layer_to_decode_ == 0 && frame_sync_ > 0 &&
          (layer_frame_cnt_ - 1) % 3 != 0)
        return false;
    } else if (simulcast_mode_) {
      // Only start decoding at frames_to_start_decoding_ and only
      // for top spatial layer SL2 (layer_to_decode_).
      if (current_video_frame_ < frame_to_start_decoding_) return false;
      if (layer_id_.spatial_layer_id < (int)layer_to_decode_) return false;
    }
    return true;
  }

  void MismatchHook(const aom_image_t *img1, const aom_image_t *img2) override {
    double mismatch_psnr = compute_psnr(img1, img2);
    mismatch_psnr_ += mismatch_psnr;
    ++mismatch_nframes_;
  }

  unsigned int GetMismatchFrames() { return mismatch_nframes_; }
  unsigned int GetDecodedFrames() { return decoded_nframes_; }

  static void ref_config_rps(aom_svc_ref_frame_config_t *ref_frame_config,
                             int frame_cnt, int rps_recovery_frame) {
    // Pattern of 3 references with (ALTREF and GOLDEN) trailing
    // LAST by 4 and 8 frame, with some switching logic to
    // only predict from longer-term reference.
    int last_idx = 0;
    int last_idx_refresh = 0;
    int gld_idx = 0;
    int alt_ref_idx = 0;
    const int lag_alt = 4;
    const int lag_gld = 8;
    const int sh = 8;  // slots 0 - 7.
    // Moving index slot for last: 0 - (sh - 1)
    if (frame_cnt > 1) last_idx = (frame_cnt - 1) % sh;
    // Moving index for refresh of last: one ahead for next frame.
    last_idx_refresh = frame_cnt % sh;
    // Moving index for gld_ref, lag behind current by lag_gld
    if (frame_cnt > lag_gld) gld_idx = (frame_cnt - lag_gld) % sh;
    // Moving index for alt_ref, lag behind LAST by lag_alt frames.
    if (frame_cnt > lag_alt) alt_ref_idx = (frame_cnt - lag_alt) % sh;
    // Set the ref_idx.
    // Default all references (7) to slot for last.
    // LAST_FRAME (0), LAST2_FRAME(1), LAST3_FRAME(2), GOLDEN_FRAME(3),
    // BWDREF_FRAME(4), ALTREF2_FRAME(5), ALTREF_FRAME(6).
    for (int i = 0; i < INTER_REFS_PER_FRAME; i++)
      ref_frame_config->ref_idx[i] = last_idx;
    // Set the ref_idx for the relevant references.
    ref_frame_config->ref_idx[0] = last_idx;
    ref_frame_config->ref_idx[1] = last_idx_refresh;
    ref_frame_config->ref_idx[3] = gld_idx;
    ref_frame_config->ref_idx[6] = alt_ref_idx;
    // Refresh this slot, which will become LAST on next frame.
    ref_frame_config->refresh[last_idx_refresh] = 1;
    // Reference LAST, ALTREF, and GOLDEN
    ref_frame_config->reference[0] = 1;
    ref_frame_config->reference[6] = 1;
    ref_frame_config->reference[3] = 1;
    if (frame_cnt == rps_recovery_frame) {
      // Switch to only reference GOLDEN at recovery_frame.
      ref_frame_config->reference[0] = 0;
      ref_frame_config->reference[6] = 0;
      ref_frame_config->reference[3] = 1;
    } else if (frame_cnt > rps_recovery_frame &&
               frame_cnt < rps_recovery_frame + 8) {
      // Go back to predicting from LAST, and after
      // 8 frames (GOLDEN is 8 frames aways) go back
      // to predicting off GOLDEN and ALTREF.
      ref_frame_config->reference[0] = 1;
      ref_frame_config->reference[6] = 0;
      ref_frame_config->reference[3] = 0;
    }
  }

  // Simulcast mode for 3 spatial and 3 temporal layers.
  // No inter-layer predicton, only prediction is temporal and single
  // reference (LAST).
  // No overlap in buffer slots between spatial layers. So for example,
  // SL0 only uses slots 0 and 1.
  // SL1 only uses slots 2 and 3.
  // SL2 only uses slots 4 and 5.
  // All 7 references for each inter-frame must only access buffer slots
  // for that spatial layer.
  // On key (super)frames: SL1 and SL2 must have no references set
  // and must refresh all the slots for that layer only (so 2 and 3
  // for SL1, 4 and 5 for SL2). The base SL0 will be labelled internally
  // as a Key frame (refresh all slots). SL1/SL2 will be labelled
  // internally as Intra-only frames that allow that stream to be decoded.
  // These conditions will allow for each spatial stream to be
  // independently decodeable.
  static void ref_config_simulcast3SL3TL(
      aom_svc_ref_frame_config_t *ref_frame_config,
      aom_svc_layer_id_t *layer_id, int is_key_frame, int superframe_cnt) {
    int i;
    // Initialize all references to 0 (don't use reference).
    for (i = 0; i < INTER_REFS_PER_FRAME; i++)
      ref_frame_config->reference[i] = 0;
    // Initialize as no refresh/update for all slots.
    for (i = 0; i < REF_FRAMES; i++) ref_frame_config->refresh[i] = 0;
    for (i = 0; i < INTER_REFS_PER_FRAME; i++) ref_frame_config->ref_idx[i] = 0;

    if (is_key_frame) {
      if (layer_id->spatial_layer_id == 0) {
        // Assign LAST/GOLDEN to slot 0/1.
        // Refesh slots 0 and 1 for SL0.
        // SL0: this will get set to KEY frame internally.
        ref_frame_config->ref_idx[0] = 0;
        ref_frame_config->ref_idx[3] = 1;
        ref_frame_config->refresh[0] = 1;
        ref_frame_config->refresh[1] = 1;
      } else if (layer_id->spatial_layer_id == 1) {
        // Assign LAST/GOLDEN to slot 2/3.
        // Refesh slots 2 and 3 for SL1.
        // This will get set to Intra-only frame internally.
        ref_frame_config->ref_idx[0] = 2;
        ref_frame_config->ref_idx[3] = 3;
        ref_frame_config->refresh[2] = 1;
        ref_frame_config->refresh[3] = 1;
      } else if (layer_id->spatial_layer_id == 2) {
        // Assign LAST/GOLDEN to slot 4/5.
        // Refresh slots 4 and 5 for SL2.
        // This will get set to Intra-only frame internally.
        ref_frame_config->ref_idx[0] = 4;
        ref_frame_config->ref_idx[3] = 5;
        ref_frame_config->refresh[4] = 1;
        ref_frame_config->refresh[5] = 1;
      }
    } else if (superframe_cnt % 4 == 0) {
      // Base temporal layer: TL0
      layer_id->temporal_layer_id = 0;
      if (layer_id->spatial_layer_id == 0) {  // SL0
        // Reference LAST. Assign all references to either slot
        // 0 or 1. Here we assign LAST to slot 0, all others to 1.
        // Update slot 0 (LAST).
        ref_frame_config->reference[0] = 1;
        for (i = 0; i < INTER_REFS_PER_FRAME; i++)
          ref_frame_config->ref_idx[i] = 1;
        ref_frame_config->ref_idx[0] = 0;
        ref_frame_config->refresh[0] = 1;
      } else if (layer_id->spatial_layer_id == 1) {  // SL1
        // Reference LAST. Assign all references to either slot
        // 2 or 3. Here we assign LAST to slot 2, all others to 3.
        // Update slot 2 (LAST).
        ref_frame_config->reference[0] = 1;
        for (i = 0; i < INTER_REFS_PER_FRAME; i++)
          ref_frame_config->ref_idx[i] = 3;
        ref_frame_config->ref_idx[0] = 2;
        ref_frame_config->refresh[2] = 1;
      } else if (layer_id->spatial_layer_id == 2) {  // SL2
        // Reference LAST. Assign all references to either slot
        // 4 or 5. Here we assign LAST to slot 4, all others to 5.
        // Update slot 4 (LAST).
        ref_frame_config->reference[0] = 1;
        for (i = 0; i < INTER_REFS_PER_FRAME; i++)
          ref_frame_config->ref_idx[i] = 5;
        ref_frame_config->ref_idx[0] = 4;
        ref_frame_config->refresh[4] = 1;
      }
    } else if ((superframe_cnt - 1) % 4 == 0) {
      // First top temporal enhancement layer: TL2
      layer_id->temporal_layer_id = 2;
      if (layer_id->spatial_layer_id == 0) {  // SL0
        // Reference LAST (slot 0). Assign other references to slot 1.
        // No update/refresh on any slots.
        ref_frame_config->reference[0] = 1;
        for (i = 0; i < INTER_REFS_PER_FRAME; i++)
          ref_frame_config->ref_idx[i] = 1;
        ref_frame_config->ref_idx[0] = 0;
      } else if (layer_id->spatial_layer_id == 1) {  // SL1
        // Reference LAST (slot 2). Assign other references to slot 3.
        // No update/refresh on any slots.
        ref_frame_config->reference[0] = 1;
        for (i = 0; i < INTER_REFS_PER_FRAME; i++)
          ref_frame_config->ref_idx[i] = 3;
        ref_frame_config->ref_idx[0] = 2;
      } else if (layer_id->spatial_layer_id == 2) {  // SL2
        // Reference LAST (slot 4). Assign other references to slot 4.
        // No update/refresh on any slots.
        ref_frame_config->reference[0] = 1;
        for (i = 0; i < INTER_REFS_PER_FRAME; i++)
          ref_frame_config->ref_idx[i] = 5;
        ref_frame_config->ref_idx[0] = 4;
      }
    } else if ((superframe_cnt - 2) % 4 == 0) {
      // Middle temporal enhancement layer: TL1
      layer_id->temporal_layer_id = 1;
      if (layer_id->spatial_layer_id == 0) {  // SL0
        // Reference LAST (slot 0).
        // Set GOLDEN to slot 1 and update slot 1.
        // This will be used as reference for next TL2.
        ref_frame_config->reference[0] = 1;
        for (i = 0; i < INTER_REFS_PER_FRAME; i++)
          ref_frame_config->ref_idx[i] = 1;
        ref_frame_config->ref_idx[0] = 0;
        ref_frame_config->refresh[1] = 1;
      } else if (layer_id->spatial_layer_id == 1) {  // SL1
        // Reference LAST (slot 2).
        // Set GOLDEN to slot 3 and update slot 3.
        // This will be used as reference for next TL2.
        ref_frame_config->reference[0] = 1;
        for (i = 0; i < INTER_REFS_PER_FRAME; i++)
          ref_frame_config->ref_idx[i] = 3;
        ref_frame_config->ref_idx[0] = 2;
        ref_frame_config->refresh[3] = 1;
      } else if (layer_id->spatial_layer_id == 2) {  // SL2
        // Reference LAST (slot 4).
        // Set GOLDEN to slot 5 and update slot 5.
        // This will be used as reference for next TL2.
        ref_frame_config->reference[0] = 1;
        for (i = 0; i < INTER_REFS_PER_FRAME; i++)
          ref_frame_config->ref_idx[i] = 5;
        ref_frame_config->ref_idx[0] = 4;
        ref_frame_config->refresh[5] = 1;
      }
    } else if ((superframe_cnt - 3) % 4 == 0) {
      // Second top temporal enhancement layer: TL2
      layer_id->temporal_layer_id = 2;
      if (layer_id->spatial_layer_id == 0) {  // SL0
        // Reference LAST (slot 1). Assign other references to slot 0.
        // No update/refresh on any slots.
        ref_frame_config->reference[0] = 1;
        for (i = 0; i < INTER_REFS_PER_FRAME; i++)
          ref_frame_config->ref_idx[i] = 0;
        ref_frame_config->ref_idx[0] = 1;
      } else if (layer_id->spatial_layer_id == 1) {  // SL1
        // Reference LAST (slot 3). Assign other references to slot 2.
        // No update/refresh on any slots.
        ref_frame_config->reference[0] = 1;
        for (i = 0; i < INTER_REFS_PER_FRAME; i++)
          ref_frame_config->ref_idx[i] = 2;
        ref_frame_config->ref_idx[0] = 3;
      } else if (layer_id->spatial_layer_id == 2) {  // SL2
        // Reference LAST (slot 5). Assign other references to slot 4.
        // No update/refresh on any slots.
        ref_frame_config->reference[0] = 1;
        for (i = 0; i < INTER_REFS_PER_FRAME; i++)
          ref_frame_config->ref_idx[i] = 4;
        ref_frame_config->ref_idx[0] = 5;
      }
    }
  }

  // 3 spatial and 3 temporal layer.
  // Overlap in the buffer slot updates: the slots 3 and 4 updated by
  // first TL2 are reused for update in TL1 superframe.
  static void ref_config_3SL3TL(aom_svc_ref_frame_config_t *ref_frame_config,
                                aom_svc_layer_id_t *layer_id, int is_key_frame,
                                int superframe_cnt) {
    if (superframe_cnt % 4 == 0) {
      // Base temporal layer.
      layer_id->temporal_layer_id = 0;
      if (layer_id->spatial_layer_id == 0) {
        // Reference LAST, update LAST.
        // Set all buffer_idx to 0.
        for (int i = 0; i < 7; i++) ref_frame_config->ref_idx[i] = 0;
        ref_frame_config->refresh[0] = 1;
      } else if (layer_id->spatial_layer_id == 1) {
        // Reference LAST and GOLDEN. Set buffer_idx for LAST to slot 1,
        // GOLDEN (and all other refs) to slot 0.
        // Update slot 1 (LAST).
        for (int i = 0; i < 7; i++) ref_frame_config->ref_idx[i] = 0;
        ref_frame_config->ref_idx[0] = 1;
        ref_frame_config->refresh[1] = 1;
      } else if (layer_id->spatial_layer_id == 2) {
        // Reference LAST and GOLDEN. Set buffer_idx for LAST to slot 2,
        // GOLDEN (and all other refs) to slot 1.
        // Update slot 2 (LAST).
        for (int i = 0; i < 7; i++) ref_frame_config->ref_idx[i] = 1;
        ref_frame_config->ref_idx[0] = 2;
        ref_frame_config->refresh[2] = 1;
      }
    } else if ((superframe_cnt - 1) % 4 == 0) {
      // First top temporal enhancement layer.
      layer_id->temporal_layer_id = 2;
      if (layer_id->spatial_layer_id == 0) {
        // Reference LAST (slot 0).
        // Set GOLDEN to slot 3 and update slot 3.
        // Set all other buffer_idx to slot 0.
        for (int i = 0; i < 7; i++) ref_frame_config->ref_idx[i] = 0;
        ref_frame_config->ref_idx[3] = 3;
        ref_frame_config->refresh[3] = 1;
      } else if (layer_id->spatial_layer_id == 1) {
        // Reference LAST and GOLDEN. Set buffer_idx for LAST to slot 1,
        // GOLDEN (and all other refs) to slot 3.
        // Set LAST2 to slot 4 and Update slot 4.
        for (int i = 0; i < 7; i++) ref_frame_config->ref_idx[i] = 3;
        ref_frame_config->ref_idx[0] = 1;
        ref_frame_config->ref_idx[1] = 4;
        ref_frame_config->refresh[4] = 1;
      } else if (layer_id->spatial_layer_id == 2) {
        // Reference LAST and GOLDEN. Set buffer_idx for LAST to slot 2,
        // GOLDEN (and all other refs) to slot 4.
        // No update.
        for (int i = 0; i < 7; i++) ref_frame_config->ref_idx[i] = 4;
        ref_frame_config->ref_idx[0] = 2;
      }
    } else if ((superframe_cnt - 2) % 4 == 0) {
      // Middle temporal enhancement layer.
      layer_id->temporal_layer_id = 1;
      if (layer_id->spatial_layer_id == 0) {
        // Reference LAST.
        // Set all buffer_idx to 0.
        // Set GOLDEN to slot 3 and update slot 3.
        for (int i = 0; i < 7; i++) ref_frame_config->ref_idx[i] = 0;
        ref_frame_config->ref_idx[3] = 3;
        ref_frame_config->refresh[3] = 1;
      } else if (layer_id->spatial_layer_id == 1) {
        // Reference LAST and GOLDEN. Set buffer_idx for LAST to slot 1,
        // GOLDEN (and all other refs) to slot 3.
        // Set LAST2 to slot 4 and update slot 4.
        for (int i = 0; i < 7; i++) ref_frame_config->ref_idx[i] = 3;
        ref_frame_config->ref_idx[0] = 1;
        ref_frame_config->ref_idx[2] = 4;
        ref_frame_config->refresh[4] = 1;
      } else if (layer_id->spatial_layer_id == 2) {
        // Reference LAST and GOLDEN. Set buffer_idx for LAST to slot 2,
        // GOLDEN (and all other refs) to slot 4.
        // Set LAST2 to slot 5 and update slot 5.
        for (int i = 0; i < 7; i++) ref_frame_config->ref_idx[i] = 4;
        ref_frame_config->ref_idx[0] = 2;
        ref_frame_config->ref_idx[2] = 5;
        ref_frame_config->refresh[5] = 1;
      }
    } else if ((superframe_cnt - 3) % 4 == 0) {
      // Second top temporal enhancement layer.
      layer_id->temporal_layer_id = 2;
      if (layer_id->spatial_layer_id == 0) {
        // Set LAST to slot 3 and reference LAST.
        // Set GOLDEN to slot 3 and update slot 3.
        // Set all other buffer_idx to 0.
        for (int i = 0; i < 7; i++) ref_frame_config->ref_idx[i] = 0;
        ref_frame_config->ref_idx[0] = 3;
        ref_frame_config->ref_idx[3] = 3;
        ref_frame_config->refresh[3] = 1;
      } else if (layer_id->spatial_layer_id == 1) {
        // Reference LAST and GOLDEN. Set buffer_idx for LAST to slot 4,
        // GOLDEN to slot 3. Set LAST2 to slot 4 and update slot 4.
        for (int i = 0; i < 7; i++) ref_frame_config->ref_idx[i] = 0;
        ref_frame_config->ref_idx[0] = 4;
        ref_frame_config->ref_idx[3] = 3;
        ref_frame_config->ref_idx[1] = 4;
        ref_frame_config->refresh[4] = 1;
      } else if (layer_id->spatial_layer_id == 2) {
        // Reference LAST and GOLDEN. Set buffer_idx for LAST to slot 5,
        // GOLDEN to slot 4. No update.
        for (int i = 0; i < 7; i++) ref_frame_config->ref_idx[i] = 0;
        ref_frame_config->ref_idx[0] = 5;
        ref_frame_config->ref_idx[3] = 4;
      }
    }
    if (layer_id->spatial_layer_id > 0) {
      // Always reference GOLDEN (inter-layer prediction).
      ref_frame_config->reference[3] = 1;
      if (is_key_frame && layer_id->spatial_layer_id > 0) {
        // On superframes whose base is key: remove LAST since GOLDEN
        // is used as reference.
        ref_frame_config->reference[0] = 0;
      }
    }
  }

  // Layer pattern configuration.
  virtual int set_layer_pattern(
      int frame_cnt, aom_svc_layer_id_t *layer_id,
      aom_svc_ref_frame_config_t *ref_frame_config,
      aom_svc_ref_frame_comp_pred_t *ref_frame_comp_pred, int spatial_layer,
      int multi_ref, int comp_pred, int is_key_frame,
      int dynamic_enable_disable_mode, int rps_mode, int rps_recovery_frame,
      int simulcast_mode, bool use_last_as_scaled,
      bool use_last_as_scaled_single_ref) {
    int lag_index = 0;
    int base_count = frame_cnt >> 2;
    layer_id->spatial_layer_id = spatial_layer;
    // Set the reference map buffer idx for the 7 references:
    // LAST_FRAME (0), LAST2_FRAME(1), LAST3_FRAME(2), GOLDEN_FRAME(3),
    // BWDREF_FRAME(4), ALTREF2_FRAME(5), ALTREF_FRAME(6).
    for (int i = 0; i < INTER_REFS_PER_FRAME; i++) {
      ref_frame_config->ref_idx[i] = i;
      ref_frame_config->reference[i] = 0;
    }
    for (int i = 0; i < REF_FRAMES; i++) ref_frame_config->refresh[i] = 0;
    if (comp_pred) {
      ref_frame_comp_pred->use_comp_pred[0] = 1;  // GOLDEN_LAST
      ref_frame_comp_pred->use_comp_pred[1] = 1;  // LAST2_LAST
      ref_frame_comp_pred->use_comp_pred[2] = 1;  // ALTREF_LAST
    }
    // Set layer_flags to 0 when using ref_frame_config->reference.
    int layer_flags = 0;
    // Always reference LAST.
    ref_frame_config->reference[0] = 1;
    if (number_temporal_layers_ == 1 && number_spatial_layers_ == 1) {
      layer_id->temporal_layer_id = 0;
      ref_frame_config->refresh[0] = 1;
      if (rps_mode)
        ref_config_rps(ref_frame_config, frame_cnt, rps_recovery_frame);
      if (intra_only_single_layer_) {
        // This repros the crash in Bug: 363016123.
        ref_frame_config->ref_idx[0] = 0;
        ref_frame_config->ref_idx[3] = 1;
        ref_frame_config->ref_idx[6] = 2;
        if (frame_cnt == 1) {
          for (int i = 0; i < INTER_REFS_PER_FRAME; i++)
            ref_frame_config->reference[i] = 0;
        }
      }
    }
    if (number_temporal_layers_ == 2 && number_spatial_layers_ == 1) {
      // 2-temporal layer.
      //    1    3    5
      //  0    2    4
      // Keep golden fixed at slot 3.
      base_count = frame_cnt >> 1;
      ref_frame_config->ref_idx[3] = 3;
      // Cyclically refresh slots 5, 6, 7, for lag alt ref.
      lag_index = 5;
      if (base_count > 0) {
        lag_index = 5 + (base_count % 3);
        if (frame_cnt % 2 != 0) lag_index = 5 + ((base_count + 1) % 3);
      }
      // Set the altref slot to lag_index.
      ref_frame_config->ref_idx[6] = lag_index;
      if (frame_cnt % 2 == 0) {
        layer_id->temporal_layer_id = 0;
        // Update LAST on layer 0, reference LAST.
        ref_frame_config->refresh[0] = 1;
        ref_frame_config->reference[0] = 1;
        // Refresh lag_index slot, needed for lagging golen.
        ref_frame_config->refresh[lag_index] = 1;
        // Refresh GOLDEN every x base layer frames.
        if (base_count % 32 == 0) ref_frame_config->refresh[3] = 1;
      } else {
        layer_id->temporal_layer_id = 1;
        // No updates on layer 1, reference LAST (TL0).
        ref_frame_config->reference[0] = 1;
      }
      // Always reference golden and altref on TL0.
      if (layer_id->temporal_layer_id == 0) {
        ref_frame_config->reference[3] = 1;
        ref_frame_config->reference[6] = 1;
      }
    } else if (number_temporal_layers_ == 3 && number_spatial_layers_ == 1) {
      // 3-layer:
      //   1    3   5    7
      //     2        6
      // 0        4        8
      if (multi_ref) {
        // Keep golden fixed at slot 3.
        ref_frame_config->ref_idx[3] = 3;
        // Cyclically refresh slots 4, 5, 6, 7, for lag altref.
        lag_index = 4 + (base_count % 4);
        // Set the altref slot to lag_index.
        ref_frame_config->ref_idx[6] = lag_index;
      }
      if (frame_cnt % 4 == 0) {
        // Base layer.
        layer_id->temporal_layer_id = 0;
        // Update LAST on layer 0, reference LAST and GF.
        ref_frame_config->refresh[0] = 1;
        ref_frame_config->reference[3] = 1;
        if (multi_ref) {
          // Refresh GOLDEN every x ~10 base layer frames.
          if (base_count % 10 == 0) ref_frame_config->refresh[3] = 1;
          // Refresh lag_index slot, needed for lagging altref.
          ref_frame_config->refresh[lag_index] = 1;
        }
      } else if ((frame_cnt - 1) % 4 == 0) {
        layer_id->temporal_layer_id = 2;
        // First top layer: no updates, only reference LAST (TL0).
      } else if ((frame_cnt - 2) % 4 == 0) {
        layer_id->temporal_layer_id = 1;
        // Middle layer (TL1): update LAST2, only reference LAST (TL0).
        ref_frame_config->refresh[1] = 1;
      } else if ((frame_cnt - 3) % 4 == 0) {
        layer_id->temporal_layer_id = 2;
        // Second top layer: no updates, only reference LAST.
        // Set buffer idx for LAST to slot 1, since that was the slot
        // updated in previous frame. So LAST is TL1 frame.
        ref_frame_config->ref_idx[0] = 1;
        ref_frame_config->ref_idx[1] = 0;
      }
      if (multi_ref) {
        // Every frame can reference GOLDEN AND ALTREF.
        ref_frame_config->reference[3] = 1;
        ref_frame_config->reference[6] = 1;
      }
    } else if (number_temporal_layers_ == 1 && number_spatial_layers_ == 2) {
      layer_id->temporal_layer_id = 0;
      if (layer_id->spatial_layer_id == 0) {
        // Reference LAST, update LAST. Keep LAST and GOLDEN in slots 0 and 3.
        ref_frame_config->ref_idx[0] = 0;
        ref_frame_config->ref_idx[3] = 3;
        ref_frame_config->refresh[0] = 1;
      } else if (layer_id->spatial_layer_id == 1) {
        // Reference LAST and GOLDEN. Set buffer_idx for LAST to slot 3
        // and GOLDEN to slot 0. Update slot 3 (LAST).
        ref_frame_config->ref_idx[0] = 3;
        ref_frame_config->ref_idx[3] = 0;
        ref_frame_config->refresh[3] = 1;
      }
      // Reference GOLDEN.
      if (layer_id->spatial_layer_id > 0) ref_frame_config->reference[3] = 1;
    } else if (number_temporal_layers_ == 1 && number_spatial_layers_ == 3) {
      // 3 spatial layers, 1 temporal.
      // Note for this case , we set the buffer idx for all references to be
      // either LAST or GOLDEN, which are always valid references, since decoder
      // will check if any of the 7 references is valid scale in
      // valid_ref_frame_size().
      layer_id->temporal_layer_id = 0;
      if (layer_id->spatial_layer_id == 0) {
        // Reference LAST, update LAST. Set all other buffer_idx to 0.
        for (int i = 0; i < 7; i++) ref_frame_config->ref_idx[i] = 0;
        ref_frame_config->refresh[0] = 1;
      } else if (layer_id->spatial_layer_id == 1) {
        // Reference LAST and GOLDEN. Set buffer_idx for LAST to slot 1
        // and GOLDEN (and all other refs) to slot 0.
        // Update slot 1 (LAST).
        for (int i = 0; i < 7; i++) ref_frame_config->ref_idx[i] = 0;
        ref_frame_config->ref_idx[0] = 1;
        if (use_last_as_scaled) {
          for (int i = 0; i < 7; i++) ref_frame_config->ref_idx[i] = 1;
          ref_frame_config->ref_idx[0] = 0;
          ref_frame_config->ref_idx[3] = 1;
        }
        ref_frame_config->refresh[1] = 1;
      } else if (layer_id->spatial_layer_id == 2) {
        // Reference LAST and GOLDEN. Set buffer_idx for LAST to slot 2
        // and GOLDEN (and all other refs) to slot 1.
        // Update slot 2 (LAST).
        for (int i = 0; i < 7; i++) ref_frame_config->ref_idx[i] = 1;
        ref_frame_config->ref_idx[0] = 2;
        ref_frame_config->refresh[2] = 1;
        if (multi_ref) {
          ref_frame_config->ref_idx[6] = 7;
          ref_frame_config->reference[6] = 1;
          if (base_count % 10 == 0) ref_frame_config->refresh[7] = 1;
        }
      }
      // Reference GOLDEN.
      if (layer_id->spatial_layer_id > 0) {
        if (use_last_as_scaled_single_ref)
          ref_frame_config->reference[3] = 0;
        else
          ref_frame_config->reference[3] = 1;
      }
    } else if (number_temporal_layers_ == 3 && number_spatial_layers_ == 3) {
      if (simulcast_mode) {
        ref_config_simulcast3SL3TL(ref_frame_config, layer_id, is_key_frame,
                                   superframe_cnt_);
      } else {
        ref_config_3SL3TL(ref_frame_config, layer_id, is_key_frame,
                          superframe_cnt_);
        // Allow for top spatial layer to use additional temporal reference.
        // Additional reference is only updated on base temporal layer, every
        // 10 TL0 frames here.
        if (multi_ref && layer_id->spatial_layer_id == 2) {
          ref_frame_config->ref_idx[6] = 7;
          if (!is_key_frame) ref_frame_config->reference[6] = 1;
          if (base_count % 10 == 0 && layer_id->temporal_layer_id == 0)
            ref_frame_config->refresh[7] = 1;
        }
      }
    }
    // If the top spatial layer is first-time encoded in mid-sequence
    // (i.e., dynamic_enable_disable_mode = 1), then don't predict from LAST,
    // since it will have been last updated on first key frame (SL0) and so
    // be different resolution from SL2.
    if (dynamic_enable_disable_mode == 1 &&
        layer_id->spatial_layer_id == number_spatial_layers_ - 1)
      ref_frame_config->reference[0] = 0;
    return layer_flags;
  }

  virtual void initialize_svc(int number_temporal_layers,
                              int number_spatial_layers,
                              aom_svc_params *svc_params) {
    svc_params->number_spatial_layers = number_spatial_layers;
    svc_params->number_temporal_layers = number_temporal_layers;
    for (int i = 0; i < number_temporal_layers * number_spatial_layers; ++i) {
      svc_params->max_quantizers[i] = 60;
      svc_params->min_quantizers[i] = 2;
      svc_params->layer_target_bitrate[i] = target_layer_bitrate_[i];
    }
    // Do at most 3 spatial or temporal layers here.
    svc_params->framerate_factor[0] = 1;
    if (number_temporal_layers == 2) {
      svc_params->framerate_factor[0] = 2;
      svc_params->framerate_factor[1] = 1;
    } else if (number_temporal_layers == 3) {
      svc_params->framerate_factor[0] = 4;
      svc_params->framerate_factor[1] = 2;
      svc_params->framerate_factor[2] = 1;
    }
    svc_params->scaling_factor_num[0] = 1;
    svc_params->scaling_factor_den[0] = 1;
    if (number_spatial_layers == 2) {
      svc_params->scaling_factor_num[0] = 1;
      svc_params->scaling_factor_den[0] = 2;
      svc_params->scaling_factor_num[1] = 1;
      svc_params->scaling_factor_den[1] = 1;
    } else if (number_spatial_layers == 3) {
      svc_params->scaling_factor_num[0] = 1;
      svc_params->scaling_factor_den[0] = 4;
      svc_params->scaling_factor_num[1] = 1;
      svc_params->scaling_factor_den[1] = 2;
      svc_params->scaling_factor_num[2] = 1;
      svc_params->scaling_factor_den[2] = 1;
    }
  }

  virtual void BasicRateTargetingSVC3TL1SLTest() {
    cfg_.rc_buf_initial_sz = 500;
    cfg_.rc_buf_optimal_sz = 500;
    cfg_.rc_buf_sz = 1000;
    cfg_.rc_dropframe_thresh = 0;
    cfg_.rc_min_quantizer = 0;
    cfg_.rc_max_quantizer = 63;
    cfg_.rc_end_usage = AOM_CBR;
    cfg_.g_lag_in_frames = 0;
    cfg_.g_error_resilient = 1;

    ::libaom_test::I420VideoSource video("hantro_collage_w352h288.yuv", 352,
                                         288, 30, 1, 0, 300);
    const int bitrate_array[2] = { 200, 550 };
    cfg_.rc_target_bitrate = bitrate_array[GET_PARAM(4)];
    ResetModel();
    number_temporal_layers_ = 3;
    target_layer_bitrate_[0] = 50 * cfg_.rc_target_bitrate / 100;
    target_layer_bitrate_[1] = 70 * cfg_.rc_target_bitrate / 100;
    target_layer_bitrate_[2] = cfg_.rc_target_bitrate;
    ASSERT_NO_FATAL_FAILURE(RunLoop(&video));
    for (int i = 0; i < number_temporal_layers_ * number_spatial_layers_; i++) {
      ASSERT_GE(effective_datarate_tl[i], target_layer_bitrate_[i] * 0.60)
          << " The datarate for the file is lower than target by too much!";
      ASSERT_LE(effective_datarate_tl[i], target_layer_bitrate_[i] * 1.60)
          << " The datarate for the file is greater than target by too much!";
    }
#if CONFIG_AV1_DECODER
    // Top temporal layers are non_reference, so exlcude them from
    // mismatch count, since loopfilter/cdef is not applied for these on
    // encoder side, but is always applied on decoder.
    // This means 150 = #frames(300) - #TL2_frames(150).
    EXPECT_EQ((int)GetMismatchFrames(), 150);
#endif
  }

  virtual void SetFrameQpSVC3TL1SLTest() {
    cfg_.rc_buf_initial_sz = 500;
    cfg_.rc_buf_optimal_sz = 500;
    cfg_.rc_buf_sz = 1000;
    cfg_.rc_dropframe_thresh = 0;
    cfg_.rc_min_quantizer = 0;
    cfg_.rc_max_quantizer = 63;
    cfg_.rc_end_usage = AOM_CBR;
    cfg_.g_lag_in_frames = 0;
    cfg_.g_error_resilient = 1;

    user_define_frame_qp_ = 1;
    total_frame_ = 300;

    ::libaom_test::I420VideoSource video("hantro_collage_w352h288.yuv", 352,
                                         288, 30, 1, 0, 300);
    const int bitrate_array[2] = { 200, 550 };
    cfg_.rc_target_bitrate = bitrate_array[GET_PARAM(4)];
    ResetModel();
    number_temporal_layers_ = 3;
    target_layer_bitrate_[0] = 50 * cfg_.rc_target_bitrate / 100;
    target_layer_bitrate_[1] = 70 * cfg_.rc_target_bitrate / 100;
    target_layer_bitrate_[2] = cfg_.rc_target_bitrate;
    ASSERT_NO_FATAL_FAILURE(RunLoop(&video));
  }

  virtual void SetFrameQpSVC3TL3SLTest() {
    cfg_.rc_buf_initial_sz = 500;
    cfg_.rc_buf_optimal_sz = 500;
    cfg_.rc_buf_sz = 1000;
    cfg_.rc_dropframe_thresh = 0;
    cfg_.rc_min_quantizer = 0;
    cfg_.rc_max_quantizer = 63;
    cfg_.rc_end_usage = AOM_CBR;
    cfg_.g_lag_in_frames = 0;
    cfg_.g_error_resilient = 0;

    user_define_frame_qp_ = 1;
    total_frame_ = 300;

    ::libaom_test::I420VideoSource video("hantro_collage_w352h288.yuv", 352,
                                         288, 30, 1, 0, 300);
    const int bitrate_array[2] = { 600, 1200 };
    cfg_.rc_target_bitrate = bitrate_array[GET_PARAM(4)];
    ResetModel();
    number_temporal_layers_ = 3;
    number_spatial_layers_ = 3;
    // SL0
    const int bitrate_sl0 = 1 * cfg_.rc_target_bitrate / 8;
    target_layer_bitrate_[0] = 50 * bitrate_sl0 / 100;
    target_layer_bitrate_[1] = 70 * bitrate_sl0 / 100;
    target_layer_bitrate_[2] = bitrate_sl0;
    // SL1
    const int bitrate_sl1 = 3 * cfg_.rc_target_bitrate / 8;
    target_layer_bitrate_[3] = 50 * bitrate_sl1 / 100;
    target_layer_bitrate_[4] = 70 * bitrate_sl1 / 100;
    target_layer_bitrate_[5] = bitrate_sl1;
    // SL2
    const int bitrate_sl2 = 4 * cfg_.rc_target_bitrate / 8;
    target_layer_bitrate_[6] = 50 * bitrate_sl2 / 100;
    target_layer_bitrate_[7] = 70 * bitrate_sl2 / 100;
    target_layer_bitrate_[8] = bitrate_sl2;
    ASSERT_NO_FATAL_FAILURE(RunLoop(&video));
  }

  virtual void BasicRateTargetingSVC3TL1SLScreenTest() {
    cfg_.rc_buf_initial_sz = 500;
    cfg_.rc_buf_optimal_sz = 500;
    cfg_.rc_buf_sz = 1000;
    cfg_.rc_dropframe_thresh = 0;
    cfg_.rc_min_quantizer = 0;
    cfg_.rc_max_quantizer = 63;
    cfg_.rc_end_usage = AOM_CBR;
    cfg_.g_lag_in_frames = 0;
    cfg_.g_error_resilient = 0;

    ::libaom_test::Y4mVideoSource video("screendata.y4m", 0, 60);

    const int bitrate_array[2] = { 1000, 1500 };
    cfg_.rc_target_bitrate = bitrate_array[GET_PARAM(4)];
    ResetModel();
    screen_mode_ = 1;
    number_temporal_layers_ = 3;
    number_spatial_layers_ = 1;
    target_layer_bitrate_[0] = 50 * cfg_.rc_target_bitrate / 100;
    target_layer_bitrate_[1] = 70 * cfg_.rc_target_bitrate / 100;
    target_layer_bitrate_[2] = cfg_.rc_target_bitrate;
    ASSERT_NO_FATAL_FAILURE(RunLoop(&video));
    for (int i = 0; i < number_temporal_layers_ * number_spatial_layers_; i++) {
      ASSERT_GE(effective_datarate_tl[i], target_layer_bitrate_[i] * 0.40)
          << " The datarate for the file is lower than target by too much!";
      ASSERT_LE(effective_datarate_tl[i], target_layer_bitrate_[i] * 2.0)
          << " The datarate for the file is greater than target by too much!";
    }
#if CONFIG_AV1_DECODER
    // Top temporal layers are non_reference, so exlcude them from
    // mismatch count, since loopfilter/cdef is not applied for these on
    // encoder side, but is always applied on decoder.
    // This means 30 = #frames(60) - #TL2_frames(30).
    // We use LE for screen since loopfilter level can become very small
    // or zero and then the frame is not a mismatch.
    EXPECT_LE((int)GetMismatchFrames(), 30);
#endif
  }

  virtual void BasicRateTargetingSVC2TL1SLScreenDropFrameTest() {
    cfg_.rc_buf_initial_sz = 50;
    cfg_.rc_buf_optimal_sz = 50;
    cfg_.rc_buf_sz = 100;
    cfg_.rc_dropframe_thresh = 30;
    cfg_.rc_min_quantizer = 0;
    cfg_.rc_max_quantizer = 52;
    cfg_.rc_end_usage = AOM_CBR;
    cfg_.g_lag_in_frames = 0;
    cfg_.g_error_resilient = 0;

    ::libaom_test::I420VideoSource video("hantro_collage_w352h288.yuv", 352,
                                         288, 30, 1, 0, 300);

    const int bitrate_array[2] = { 60, 100 };
    cfg_.rc_target_bitrate = bitrate_array[GET_PARAM(4)];
    ResetModel();
    screen_mode_ = 1;
    number_temporal_layers_ = 2;
    number_spatial_layers_ = 1;
    target_layer_bitrate_[0] = 60 * cfg_.rc_target_bitrate / 100;
    target_layer_bitrate_[1] = cfg_.rc_target_bitrate;
    ASSERT_NO_FATAL_FAILURE(RunLoop(&video));
    for (int i = 0; i < number_temporal_layers_ * number_spatial_layers_; i++) {
      ASSERT_GE(effective_datarate_tl[i], target_layer_bitrate_[i] * 0.75)
          << " The datarate for the file is lower than target by too much!";
      ASSERT_LE(effective_datarate_tl[i], target_layer_bitrate_[i] * 1.8)
          << " The datarate for the file is greater than target by too much!";
    }
#if CONFIG_AV1_DECODER
    // Top temporal layers are non_reference, so exlcude them from
    // mismatch count, since loopfilter/cdef is not applied for these on
    // encoder side, but is always applied on decoder.
    // This means 300 = #frames(300) - #TL2_frames(150).
    // We use LE for screen since loopfilter level can become very small
    // or zero and then the frame is not a mismatch.
    EXPECT_LE((int)GetMismatchFrames(), 150);
#endif
  }

  virtual void BasicRateTargetingSVC2TL1SLScreenDropFrame1920x1080Test() {
    cfg_.rc_buf_initial_sz = 50;
    cfg_.rc_buf_optimal_sz = 50;
    cfg_.rc_buf_sz = 100;
    cfg_.rc_dropframe_thresh = 30;
    cfg_.rc_min_quantizer = 0;
    cfg_.rc_max_quantizer = 52;
    cfg_.rc_end_usage = AOM_CBR;
    cfg_.g_lag_in_frames = 0;
    cfg_.g_error_resilient = 0;

    ::libaom_test::Y4mVideoSource video("screendata.1920_1080.y4m", 0, 60);

    const int bitrate_array[2] = { 60, 100 };
    cfg_.rc_target_bitrate = bitrate_array[GET_PARAM(4)];
    ResetModel();
    screen_mode_ = 1;
    number_temporal_layers_ = 2;
    number_spatial_layers_ = 1;
    target_layer_bitrate_[0] = 60 * cfg_.rc_target_bitrate / 100;
    target_layer_bitrate_[1] = cfg_.rc_target_bitrate;
    ASSERT_NO_FATAL_FAILURE(RunLoop(&video));
#if CONFIG_AV1_DECODER
    // Top temporal layers are non_reference, so exclude them from
    // mismatch count, since loopfilter/cdef is not applied for these on
    // encoder side, but is always applied on decoder.
    // This means 150 = #frames(300) - #TL2_frames(150).
    // We use LE for screen since loopfilter level can become very small
    // or zero and then the frame is not a mismatch.
    EXPECT_LE(GetMismatchFrames(), 150u);
#endif
  }

  virtual void
  BasicRateTargetingSVC2TL1SLScreenDropFrame1920x10804ThreadTest() {
    cfg_.rc_buf_initial_sz = 50;
    cfg_.rc_buf_optimal_sz = 50;
    cfg_.rc_buf_sz = 100;
    cfg_.rc_dropframe_thresh = 30;
    cfg_.rc_min_quantizer = 0;
    cfg_.rc_max_quantizer = 52;
    cfg_.rc_end_usage = AOM_CBR;
    cfg_.g_lag_in_frames = 0;
    cfg_.g_error_resilient = 0;
    cfg_.g_threads = 4;

    ::libaom_test::Y4mVideoSource video("screendata.1920_1080.y4m", 0, 60);

    const int bitrate_array[2] = { 60, 100 };
    cfg_.rc_target_bitrate = bitrate_array[GET_PARAM(4)];
    ResetModel();
    tile_columns_ = 1;
    tile_rows_ = 1;
    screen_mode_ = 1;
    number_temporal_layers_ = 2;
    number_spatial_layers_ = 1;
    target_layer_bitrate_[0] = 60 * cfg_.rc_target_bitrate / 100;
    target_layer_bitrate_[1] = cfg_.rc_target_bitrate;
    ASSERT_NO_FATAL_FAILURE(RunLoop(&video));
#if CONFIG_AV1_DECODER
    // Top temporal layers are non_reference, so exclude them from
    // mismatch count, since loopfilter/cdef is not applied for these on
    // encoder side, but is always applied on decoder.
    // This means 150 = #frames(300) - #TL2_frames(150).
    // We use LE for screen since loopfilter level can become very small
    // or zero and then the frame is not a mismatch.
    EXPECT_LE(GetMismatchFrames(), 150u);
#endif
  }

  virtual void BasicRateTargetingSVC1TL3SLScreenTest() {
    cfg_.rc_buf_initial_sz = 500;
    cfg_.rc_buf_optimal_sz = 500;
    cfg_.rc_buf_sz = 1000;
    cfg_.rc_dropframe_thresh = 0;
    cfg_.rc_min_quantizer = 0;
    cfg_.rc_max_quantizer = 63;
    cfg_.rc_end_usage = AOM_CBR;
    cfg_.g_lag_in_frames = 0;
    cfg_.g_error_resilient = 0;

    ::libaom_test::Y4mVideoSource video("niklas_1280_720_30.y4m", 0, 60);

    const int bitrate_array[2] = { 800, 1200 };
    cfg_.rc_target_bitrate = bitrate_array[GET_PARAM(4)];
    ResetModel();
    screen_mode_ = 1;
    number_temporal_layers_ = 1;
    number_spatial_layers_ = 3;
    target_layer_bitrate_[0] = 30 * cfg_.rc_target_bitrate / 100;
    target_layer_bitrate_[1] = 60 * cfg_.rc_target_bitrate / 100;
    target_layer_bitrate_[2] = cfg_.rc_target_bitrate;
    ASSERT_NO_FATAL_FAILURE(RunLoop(&video));
    for (int i = 0; i < number_temporal_layers_ * number_spatial_layers_; i++) {
      ASSERT_GE(effective_datarate_tl[i], target_layer_bitrate_[i] * 0.50)
          << " The datarate for the file is lower than target by too much!";
      ASSERT_LE(effective_datarate_tl[i], target_layer_bitrate_[i] * 1.5)
          << " The datarate for the file is greater than target by too much!";
    }
#if CONFIG_AV1_DECODER
    EXPECT_EQ((int)GetMismatchFrames(), 0);
#endif
  }

  virtual void BasicRateTargetingSVC1TL1SLScreenScCutsMotionTest() {
    cfg_.rc_buf_initial_sz = 500;
    cfg_.rc_buf_optimal_sz = 500;
    cfg_.rc_buf_sz = 1000;
    cfg_.rc_dropframe_thresh = 0;
    cfg_.rc_min_quantizer = 0;
    cfg_.rc_max_quantizer = 63;
    cfg_.rc_end_usage = AOM_CBR;
    cfg_.g_lag_in_frames = 0;
    cfg_.g_error_resilient = 0;

    ::libaom_test::I420VideoSource video("hantro_collage_w352h288.yuv", 352,
                                         288, 30, 1, 0, 300);

    const int bitrate_array[2] = { 200, 500 };
    cfg_.rc_target_bitrate = bitrate_array[GET_PARAM(4)];
    ResetModel();
    screen_mode_ = 1;
    number_temporal_layers_ = 1;
    number_spatial_layers_ = 1;
    target_layer_bitrate_[0] = cfg_.rc_target_bitrate;
    ASSERT_NO_FATAL_FAILURE(RunLoop(&video));
    for (int i = 0; i < number_temporal_layers_ * number_spatial_layers_; i++) {
      ASSERT_GE(effective_datarate_tl[i], target_layer_bitrate_[i] * 0.40)
          << " The datarate for the file is lower than target by too much!";
      ASSERT_LE(effective_datarate_tl[i], target_layer_bitrate_[i] * 1.7)
          << " The datarate for the file is greater than target by too much!";
    }
#if CONFIG_AV1_DECODER
    EXPECT_EQ((int)GetMismatchFrames(), 0);
#endif
  }

  virtual void BasicRateTargetingSVC3TL1SLResizeTest() {
    cfg_.rc_buf_initial_sz = 500;
    cfg_.rc_buf_optimal_sz = 500;
    cfg_.rc_buf_sz = 1000;
    cfg_.rc_dropframe_thresh = 0;
    cfg_.rc_min_quantizer = 0;
    cfg_.rc_max_quantizer = 63;
    cfg_.rc_end_usage = AOM_CBR;
    cfg_.g_lag_in_frames = 0;
    cfg_.g_error_resilient = 0;
    cfg_.rc_resize_mode = RESIZE_DYNAMIC;

    ::libaom_test::I420VideoSource video("niklas_640_480_30.yuv", 640, 480, 30,
                                         1, 0, 400);
    cfg_.g_w = 640;
    cfg_.g_h = 480;
    const int bitrate_array[2] = { 80, 90 };
    cfg_.rc_target_bitrate = bitrate_array[GET_PARAM(4)];
    ResetModel();
    number_temporal_layers_ = 3;
    target_layer_bitrate_[0] = 50 * cfg_.rc_target_bitrate / 100;
    target_layer_bitrate_[1] = 70 * cfg_.rc_target_bitrate / 100;
    target_layer_bitrate_[2] = cfg_.rc_target_bitrate;
    ASSERT_NO_FATAL_FAILURE(RunLoop(&video));
    for (int i = 0; i < number_temporal_layers_ * number_spatial_layers_; i++) {
      ASSERT_GE(effective_datarate_tl[i], target_layer_bitrate_[i] * 0.80)
          << " The datarate for the file is lower than target by too much!";
      ASSERT_LE(effective_datarate_tl[i], target_layer_bitrate_[i] * 1.60)
          << " The datarate for the file is greater than target by too much!";
    }
#if CONFIG_AV1_DECODER
    unsigned int last_w = cfg_.g_w;
    unsigned int last_h = cfg_.g_h;
    int resize_down_count = 0;
    for (std::vector<FrameInfo>::const_iterator info = frame_info_list_.begin();
         info != frame_info_list_.end(); ++info) {
      if (info->w != last_w || info->h != last_h) {
        // Verify that resize down occurs.
        ASSERT_LT(info->w, last_w);
        ASSERT_LT(info->h, last_h);
        last_w = info->w;
        last_h = info->h;
        resize_down_count++;
      }
    }
    // Must be at least one resize down.
    ASSERT_GE(resize_down_count, 1);
#else
    printf("Warning: AV1 decoder unavailable, unable to check resize count!\n");
#endif
  }

  virtual void BasicRateTargetingSVC1TL2SLTest() {
    cfg_.rc_buf_initial_sz = 500;
    cfg_.rc_buf_optimal_sz = 500;
    cfg_.rc_buf_sz = 1000;
    cfg_.rc_dropframe_thresh = 0;
    cfg_.rc_min_quantizer = 0;
    cfg_.rc_max_quantizer = 63;
    cfg_.rc_end_usage = AOM_CBR;
    cfg_.g_lag_in_frames = 0;
    cfg_.g_error_resilient = 0;

    ::libaom_test::I420VideoSource video("hantro_collage_w352h288.yuv", 352,
                                         288, 30, 1, 0, 300);
    const int bitrate_array[2] = { 300, 600 };
    cfg_.rc_target_bitrate = bitrate_array[GET_PARAM(4)];
    ResetModel();
    number_temporal_layers_ = 1;
    number_spatial_layers_ = 2;
    target_layer_bitrate_[0] = 2 * cfg_.rc_target_bitrate / 4;
    target_layer_bitrate_[1] = 2 * cfg_.rc_target_bitrate / 4;
    ASSERT_NO_FATAL_FAILURE(RunLoop(&video));
    for (int i = 0; i < number_temporal_layers_ * number_spatial_layers_; i++) {
      ASSERT_GE(effective_datarate_tl[i], target_layer_bitrate_[i] * 0.80)
          << " The datarate for the file is lower than target by too much!";
      ASSERT_LE(effective_datarate_tl[i], target_layer_bitrate_[i] * 1.60)
          << " The datarate for the file is greater than target by too much!";
    }
  }

  virtual void BasicRateTargetingSVC3TL3SLIntraStartDecodeBaseMidSeq() {
    cfg_.rc_buf_initial_sz = 500;
    cfg_.rc_buf_optimal_sz = 500;
    cfg_.rc_buf_sz = 1000;
    cfg_.rc_dropframe_thresh = 0;
    cfg_.rc_min_quantizer = 0;
    cfg_.rc_max_quantizer = 56;
    cfg_.rc_end_usage = AOM_CBR;
    cfg_.g_lag_in_frames = 0;
    cfg_.g_error_resilient = 0;
    ::libaom_test::I420VideoSource video("hantro_collage_w352h288.yuv", 352,
                                         288, 30, 1, 0, 300);
    const int bitrate_array[2] = { 500, 1000 };
    cfg_.rc_target_bitrate = bitrate_array[GET_PARAM(4)];
    ResetModel();
    intra_only_ = 1;
    frame_sync_ = 20;
    frame_to_start_decoding_ = frame_sync_;
    layer_to_decode_ = 0;
    number_temporal_layers_ = 3;
    number_spatial_layers_ = 3;
    // SL0
    const int bitrate_sl0 = 1 * cfg_.rc_target_bitrate / 8;
    target_layer_bitrate_[0] = 50 * bitrate_sl0 / 100;
    target_layer_bitrate_[1] = 70 * bitrate_sl0 / 100;
    target_layer_bitrate_[2] = bitrate_sl0;
    // SL1
    const int bitrate_sl1 = 3 * cfg_.rc_target_bitrate / 8;
    target_layer_bitrate_[3] = 50 * bitrate_sl1 / 100;
    target_layer_bitrate_[4] = 70 * bitrate_sl1 / 100;
    target_layer_bitrate_[5] = bitrate_sl1;
    // SL2
    const int bitrate_sl2 = 4 * cfg_.rc_target_bitrate / 8;
    target_layer_bitrate_[6] = 50 * bitrate_sl2 / 100;
    target_layer_bitrate_[7] = 70 * bitrate_sl2 / 100;
    target_layer_bitrate_[8] = bitrate_sl2;
    ASSERT_NO_FATAL_FAILURE(RunLoop(&video));
    // Only check datarate on SL0 - this is layer that is decoded starting at
    // frame_to_start_decoding_.
    for (int i = 0; i < number_temporal_layers_; i++) {
      ASSERT_GE(effective_datarate_tl[i], target_layer_bitrate_[i] * 0.50)
          << " The datarate for the file is lower than target by too much!";
      ASSERT_LE(effective_datarate_tl[i], target_layer_bitrate_[i] * 1.60)
          << " The datarate for the file is greater than target by too much!";
    }
#if CONFIG_AV1_DECODER
    // Only base spatial layer is decoded and there are no non-referenece
    // frames on S0, so #mismatch must be 0.
    EXPECT_EQ((int)GetMismatchFrames(), 0);
#endif
  }

  virtual void BasicRateTargetingSVC3TL3SLIntraMidSeqDecodeAll() {
    cfg_.rc_buf_initial_sz = 500;
    cfg_.rc_buf_optimal_sz = 500;
    cfg_.rc_buf_sz = 1000;
    cfg_.rc_dropframe_thresh = 0;
    cfg_.rc_min_quantizer = 0;
    cfg_.rc_max_quantizer = 56;
    cfg_.rc_end_usage = AOM_CBR;
    cfg_.g_lag_in_frames = 0;
    cfg_.g_error_resilient = 0;
    ::libaom_test::I420VideoSource video("hantro_collage_w352h288.yuv", 352,
                                         288, 30, 1, 0, 300);
    const int bitrate_array[2] = { 500, 1000 };
    cfg_.rc_target_bitrate = bitrate_array[GET_PARAM(4)];
    ResetModel();
    intra_only_ = 1;
    frame_sync_ = 20;
    frame_to_start_decoding_ = 0;
    layer_to_decode_ = 3;
    number_temporal_layers_ = 3;
    number_spatial_layers_ = 3;
    // SL0
    const int bitrate_sl0 = 1 * cfg_.rc_target_bitrate / 8;
    target_layer_bitrate_[0] = 50 * bitrate_sl0 / 100;
    target_layer_bitrate_[1] = 70 * bitrate_sl0 / 100;
    target_layer_bitrate_[2] = bitrate_sl0;
    // SL1
    const int bitrate_sl1 = 3 * cfg_.rc_target_bitrate / 8;
    target_layer_bitrate_[3] = 50 * bitrate_sl1 / 100;
    target_layer_bitrate_[4] = 70 * bitrate_sl1 / 100;
    target_layer_bitrate_[5] = bitrate_sl1;
    // SL2
    const int bitrate_sl2 = 4 * cfg_.rc_target_bitrate / 8;
    target_layer_bitrate_[6] = 50 * bitrate_sl2 / 100;
    target_layer_bitrate_[7] = 70 * bitrate_sl2 / 100;
    target_layer_bitrate_[8] = bitrate_sl2;
    ASSERT_NO_FATAL_FAILURE(RunLoop(&video));
    for (int i = 0; i < number_temporal_layers_ * number_spatial_layers_; i++) {
      ASSERT_GE(effective_datarate_tl[i], target_layer_bitrate_[i] * 0.585)
          << " The datarate for the file is lower than target by too much!";
      ASSERT_LE(effective_datarate_tl[i], target_layer_bitrate_[i] * 1.60)
          << " The datarate for the file is greater than target by too much!";
    }
#if CONFIG_AV1_DECODER
    // All 3 spatial layers are decoded, starting at frame 0, so there are
    // and there 300/2 = 150 non-reference frames, so mismatch is 150.
    EXPECT_EQ((int)GetMismatchFrames(), 150);
#endif
  }

  virtual void BasicRateTargetingSVC3TL3SLSimulcast() {
    cfg_.rc_buf_initial_sz = 500;
    cfg_.rc_buf_optimal_sz = 500;
    cfg_.rc_buf_sz = 1000;
    cfg_.rc_dropframe_thresh = 0;
    cfg_.rc_min_quantizer = 0;
    cfg_.rc_max_quantizer = 56;
    cfg_.rc_end_usage = AOM_CBR;
    cfg_.g_lag_in_frames = 0;
    cfg_.g_error_resilient = 0;
    cfg_.kf_max_dist = 150;
    cfg_.kf_min_dist = 150;
    int num_frames = 300;
    ::libaom_test::I420VideoSource video("hantro_collage_w352h288.yuv", 352,
                                         288, 30, 1, 0, num_frames);
    const int bitrate_array[2] = { 500, 1000 };
    cfg_.rc_target_bitrate = bitrate_array[GET_PARAM(4)];
    ResetModel();
    simulcast_mode_ = 1;
    frame_to_start_decoding_ = cfg_.kf_max_dist;
    layer_to_decode_ = 2;  // SL2
    number_temporal_layers_ = 3;
    number_spatial_layers_ = 3;
    // SL0
    const int bitrate_sl0 = 1 * cfg_.rc_target_bitrate / 8;
    target_layer_bitrate_[0] = 50 * bitrate_sl0 / 100;
    target_layer_bitrate_[1] = 70 * bitrate_sl0 / 100;
    target_layer_bitrate_[2] = bitrate_sl0;
    // SL1
    const int bitrate_sl1 = 3 * cfg_.rc_target_bitrate / 8;
    target_layer_bitrate_[3] = 50 * bitrate_sl1 / 100;
    target_layer_bitrate_[4] = 70 * bitrate_sl1 / 100;
    target_layer_bitrate_[5] = bitrate_sl1;
    // SL2
    const int bitrate_sl2 = 4 * cfg_.rc_target_bitrate / 8;
    target_layer_bitrate_[6] = 50 * bitrate_sl2 / 100;
    target_layer_bitrate_[7] = 70 * bitrate_sl2 / 100;
    target_layer_bitrate_[8] = bitrate_sl2;
    ASSERT_NO_FATAL_FAILURE(RunLoop(&video));
    // Only SL2 layer is decoded.
    for (int tl = 0; tl < number_temporal_layers_; tl++) {
      int i = layer_to_decode_ * number_temporal_layers_ + tl;
      ASSERT_GE(effective_datarate_tl[i], target_layer_bitrate_[i] * 0.6)
          << " The datarate for the file is lower than target by too much!";
      ASSERT_LE(effective_datarate_tl[i], target_layer_bitrate_[i] * 1.7)
          << " The datarate for the file is greater than target by too much!";
    }
#if CONFIG_AV1_DECODER
    // Only top spatial layer (SL2) is decoded, starting at frame 150
    // (frame_to_start_decoding_), so there (300 - 150) / 2 = 75
    // non-reference frames, so mismatch is 75.
    int num_mismatch = (num_frames - frame_to_start_decoding_) / 2;
    EXPECT_EQ((int)GetMismatchFrames(), num_mismatch);
#endif
  }

  virtual void BasicRateTargetingSVC1TL2SLIntraOnlyTest() {
    cfg_.rc_buf_initial_sz = 500;
    cfg_.rc_buf_optimal_sz = 500;
    cfg_.rc_buf_sz = 1000;
    cfg_.rc_dropframe_thresh = 0;
    cfg_.rc_min_quantizer = 0;
    cfg_.rc_max_quantizer = 63;
    cfg_.rc_end_usage = AOM_CBR;
    cfg_.g_lag_in_frames = 0;
    cfg_.g_error_resilient = 0;

    ::libaom_test::I420VideoSource video("hantro_collage_w352h288.yuv", 352,
                                         288, 30, 1, 0, 300);
    const int bitrate_array[2] = { 300, 600 };
    cfg_.rc_target_bitrate = bitrate_array[GET_PARAM(4)];
    ResetModel();
    intra_only_ = 1;
    number_temporal_layers_ = 1;
    number_spatial_layers_ = 2;
    target_layer_bitrate_[0] = 2 * cfg_.rc_target_bitrate / 4;
    target_layer_bitrate_[1] = 2 * cfg_.rc_target_bitrate / 4;
    ASSERT_NO_FATAL_FAILURE(RunLoop(&video));
    for (int i = 0; i < number_temporal_layers_ * number_spatial_layers_; i++) {
      ASSERT_GE(effective_datarate_tl[i], target_layer_bitrate_[i] * 0.80)
          << " The datarate for the file is lower than target by too much!";
      ASSERT_LE(effective_datarate_tl[i], target_layer_bitrate_[i] * 1.60)
          << " The datarate for the file is greater than target by too much!";
    }
  }

  virtual void BasicRateTargetingSVC1TL1SLIntraOnlyTest() {
    cfg_.rc_buf_initial_sz = 500;
    cfg_.rc_buf_optimal_sz = 500;
    cfg_.rc_buf_sz = 1000;
    cfg_.rc_dropframe_thresh = 0;
    cfg_.rc_min_quantizer = 0;
    cfg_.rc_max_quantizer = 63;
    cfg_.rc_end_usage = AOM_CBR;
    cfg_.g_lag_in_frames = 0;
    cfg_.g_error_resilient = 0;

    ::libaom_test::I420VideoSource video("hantro_collage_w352h288.yuv", 352,
                                         288, 30, 1, 0, 300);
    const int bitrate_array[2] = { 300, 600 };
    cfg_.rc_target_bitrate = bitrate_array[GET_PARAM(4)];
    ResetModel();
    intra_only_single_layer_ = true;
    number_temporal_layers_ = 1;
    number_spatial_layers_ = 1;
    ASSERT_NO_FATAL_FAILURE(RunLoop(&video));
    ASSERT_GE(effective_datarate_tl[0], cfg_.rc_target_bitrate * 0.80)
        << " The datarate for the file is lower than target by too much!";
    ASSERT_LE(effective_datarate_tl[0], cfg_.rc_target_bitrate * 1.60)
        << " The datarate for the file is greater than target by too much!";
  }

  virtual void BasicRateTargetingSVC1TL3SLTest() {
    cfg_.rc_buf_initial_sz = 500;
    cfg_.rc_buf_optimal_sz = 500;
    cfg_.rc_buf_sz = 1000;
    cfg_.rc_dropframe_thresh = 0;
    cfg_.rc_min_quantizer = 0;
    cfg_.rc_max_quantizer = 63;
    cfg_.rc_end_usage = AOM_CBR;
    cfg_.g_lag_in_frames = 0;
    cfg_.g_error_resilient = 0;

    ::libaom_test::I420VideoSource video("hantro_collage_w352h288.yuv", 352,
                                         288, 30, 1, 0, 300);
    const int bitrate_array[2] = { 500, 1000 };
    cfg_.rc_target_bitrate = bitrate_array[GET_PARAM(4)];
    ResetModel();
    number_temporal_layers_ = 1;
    number_spatial_layers_ = 3;
    target_layer_bitrate_[0] = 1 * cfg_.rc_target_bitrate / 8;
    target_layer_bitrate_[1] = 3 * cfg_.rc_target_bitrate / 8;
    target_layer_bitrate_[2] = 4 * cfg_.rc_target_bitrate / 8;
    ASSERT_NO_FATAL_FAILURE(RunLoop(&video));
    for (int i = 0; i < number_temporal_layers_ * number_spatial_layers_; i++) {
      ASSERT_GE(effective_datarate_tl[i], target_layer_bitrate_[i] * 0.80)
          << " The datarate for the file is lower than target by too much!";
      ASSERT_LE(effective_datarate_tl[i], target_layer_bitrate_[i] * 1.38)
          << " The datarate for the file is greater than target by too much!";
    }
  }

  virtual void BasicRateTargetingSVC1TL3SLLastIsScaledTest() {
    cfg_.rc_buf_initial_sz = 500;
    cfg_.rc_buf_optimal_sz = 500;
    cfg_.rc_buf_sz = 1000;
    cfg_.rc_dropframe_thresh = 0;
    cfg_.rc_min_quantizer = 0;
    cfg_.rc_max_quantizer = 63;
    cfg_.rc_end_usage = AOM_CBR;
    cfg_.g_lag_in_frames = 0;
    cfg_.g_error_resilient = 0;

    ::libaom_test::I420VideoSource video("hantro_collage_w352h288.yuv", 352,
                                         288, 30, 1, 0, 300);
    const int bitrate_array[2] = { 500, 1000 };
    cfg_.rc_target_bitrate = bitrate_array[GET_PARAM(4)];
    ResetModel();
    number_temporal_layers_ = 1;
    number_spatial_layers_ = 3;
    use_last_as_scaled_ = true;
    target_layer_bitrate_[0] = 1 * cfg_.rc_target_bitrate / 8;
    target_layer_bitrate_[1] = 3 * cfg_.rc_target_bitrate / 8;
    target_layer_bitrate_[2] = 4 * cfg_.rc_target_bitrate / 8;
    ASSERT_NO_FATAL_FAILURE(RunLoop(&video));
    for (int i = 0; i < number_temporal_layers_ * number_spatial_layers_; i++) {
      ASSERT_GE(effective_datarate_tl[i], target_layer_bitrate_[i] * 0.80)
          << " The datarate for the file is lower than target by too much!";
      ASSERT_LE(effective_datarate_tl[i], target_layer_bitrate_[i] * 1.38)
          << " The datarate for the file is greater than target by too much!";
    }
  }

  virtual void BasicRateTargetingSVC1TL3SLLastIsScaledSingleRefTest() {
    cfg_.rc_buf_initial_sz = 500;
    cfg_.rc_buf_optimal_sz = 500;
    cfg_.rc_buf_sz = 1000;
    cfg_.rc_dropframe_thresh = 0;
    cfg_.rc_min_quantizer = 0;
    cfg_.rc_max_quantizer = 63;
    cfg_.rc_end_usage = AOM_CBR;
    cfg_.g_lag_in_frames = 0;
    cfg_.g_error_resilient = 0;

    ::libaom_test::I420VideoSource video("hantro_collage_w352h288.yuv", 352,
                                         288, 30, 1, 0, 300);
    const int bitrate_array[2] = { 500, 1000 };
    cfg_.rc_target_bitrate = bitrate_array[GET_PARAM(4)];
    ResetModel();
    number_temporal_layers_ = 1;
    number_spatial_layers_ = 3;
    use_last_as_scaled_ = true;
    use_last_as_scaled_single_ref_ = true;
    target_layer_bitrate_[0] = 1 * cfg_.rc_target_bitrate / 8;
    target_layer_bitrate_[1] = 3 * cfg_.rc_target_bitrate / 8;
    target_layer_bitrate_[2] = 4 * cfg_.rc_target_bitrate / 8;
    ASSERT_NO_FATAL_FAILURE(RunLoop(&video));
    for (int i = 0; i < number_temporal_layers_ * number_spatial_layers_; i++) {
      ASSERT_GE(effective_datarate_tl[i], target_layer_bitrate_[i] * 0.80)
          << " The datarate for the file is lower than target by too much!";
      ASSERT_LE(effective_datarate_tl[i], target_layer_bitrate_[i] * 1.38)
          << " The datarate for the file is greater than target by too much!";
    }
  }

  virtual void BasicRateTargetingSVC1TL3SLMultiRefTest() {
    cfg_.rc_buf_initial_sz = 500;
    cfg_.rc_buf_optimal_sz = 500;
    cfg_.rc_buf_sz = 1000;
    cfg_.rc_dropframe_thresh = 0;
    cfg_.rc_min_quantizer = 0;
    cfg_.rc_max_quantizer = 63;
    cfg_.rc_end_usage = AOM_CBR;
    cfg_.g_lag_in_frames = 0;
    cfg_.g_error_resilient = 0;

    ::libaom_test::I420VideoSource video("hantro_collage_w352h288.yuv", 352,
                                         288, 30, 1, 0, 300);
    const int bitrate_array[2] = { 500, 1000 };
    cfg_.rc_target_bitrate = bitrate_array[GET_PARAM(4)];
    ResetModel();
    multi_ref_ = 1;
    number_temporal_layers_ = 1;
    number_spatial_layers_ = 3;
    target_layer_bitrate_[0] = 1 * cfg_.rc_target_bitrate / 8;
    target_layer_bitrate_[1] = 3 * cfg_.rc_target_bitrate / 8;
    target_layer_bitrate_[2] = 4 * cfg_.rc_target_bitrate / 8;
    ASSERT_NO_FATAL_FAILURE(RunLoop(&video));
    for (int i = 0; i < number_temporal_layers_ * number_spatial_layers_; i++) {
      ASSERT_GE(effective_datarate_tl[i], target_layer_bitrate_[i] * 0.80)
          << " The datarate for the file is lower than target by too much!";
      ASSERT_LE(effective_datarate_tl[i], target_layer_bitrate_[i] * 1.38)
          << " The datarate for the file is greater than target by too much!";
    }
  }

  virtual void BasicRateTargetingSVC3TL3SLTest() {
    cfg_.rc_buf_initial_sz = 500;
    cfg_.rc_buf_optimal_sz = 500;
    cfg_.rc_buf_sz = 1000;
    cfg_.rc_dropframe_thresh = 0;
    cfg_.rc_min_quantizer = 0;
    cfg_.rc_max_quantizer = 63;
    cfg_.rc_end_usage = AOM_CBR;
    cfg_.g_lag_in_frames = 0;
    cfg_.g_error_resilient = 0;

    ::libaom_test::I420VideoSource video("hantro_collage_w352h288.yuv", 352,
                                         288, 30, 1, 0, 300);
    const int bitrate_array[2] = { 600, 1200 };
    cfg_.rc_target_bitrate = bitrate_array[GET_PARAM(4)];
    ResetModel();
    number_temporal_layers_ = 3;
    number_spatial_layers_ = 3;
    // SL0
    const int bitrate_sl0 = 1 * cfg_.rc_target_bitrate / 8;
    target_layer_bitrate_[0] = 50 * bitrate_sl0 / 100;
    target_layer_bitrate_[1] = 70 * bitrate_sl0 / 100;
    target_layer_bitrate_[2] = bitrate_sl0;
    // SL1
    const int bitrate_sl1 = 3 * cfg_.rc_target_bitrate / 8;
    target_layer_bitrate_[3] = 50 * bitrate_sl1 / 100;
    target_layer_bitrate_[4] = 70 * bitrate_sl1 / 100;
    target_layer_bitrate_[5] = bitrate_sl1;
    // SL2
    const int bitrate_sl2 = 4 * cfg_.rc_target_bitrate / 8;
    target_layer_bitrate_[6] = 50 * bitrate_sl2 / 100;
    target_layer_bitrate_[7] = 70 * bitrate_sl2 / 100;
    target_layer_bitrate_[8] = bitrate_sl2;
    ASSERT_NO_FATAL_FAILURE(RunLoop(&video));
    for (int i = 0; i < number_temporal_layers_ * number_spatial_layers_; i++) {
      ASSERT_GE(effective_datarate_tl[i], target_layer_bitrate_[i] * 0.50)
          << " The datarate for the file is lower than target by too much!";
      ASSERT_LE(effective_datarate_tl[i], target_layer_bitrate_[i] * 1.38)
          << " The datarate for the file is greater than target by too much!";
    }
  }

  virtual void BasicRateTargetingSVC3TL3SLHDTest() {
    cfg_.rc_buf_initial_sz = 500;
    cfg_.rc_buf_optimal_sz = 500;
    cfg_.rc_buf_sz = 1000;
    cfg_.rc_dropframe_thresh = 0;
    cfg_.rc_min_quantizer = 0;
    cfg_.rc_max_quantizer = 63;
    cfg_.rc_end_usage = AOM_CBR;
    cfg_.g_lag_in_frames = 0;
    cfg_.g_error_resilient = 0;

    ::libaom_test::Y4mVideoSource video("niklas_1280_720_30.y4m", 0, 60);
    const int bitrate_array[2] = { 600, 1200 };
    cfg_.rc_target_bitrate = bitrate_array[GET_PARAM(4)];
    ResetModel();
    number_temporal_layers_ = 3;
    number_spatial_layers_ = 3;
    // SL0
    const int bitrate_sl0 = 1 * cfg_.rc_target_bitrate / 8;
    target_layer_bitrate_[0] = 50 * bitrate_sl0 / 100;
    target_layer_bitrate_[1] = 70 * bitrate_sl0 / 100;
    target_layer_bitrate_[2] = bitrate_sl0;
    // SL1
    const int bitrate_sl1 = 3 * cfg_.rc_target_bitrate / 8;
    target_layer_bitrate_[3] = 50 * bitrate_sl1 / 100;
    target_layer_bitrate_[4] = 70 * bitrate_sl1 / 100;
    target_layer_bitrate_[5] = bitrate_sl1;
    // SL2
    const int bitrate_sl2 = 4 * cfg_.rc_target_bitrate / 8;
    target_layer_bitrate_[6] = 50 * bitrate_sl2 / 100;
    target_layer_bitrate_[7] = 70 * bitrate_sl2 / 100;
    target_layer_bitrate_[8] = bitrate_sl2;
    ASSERT_NO_FATAL_FAILURE(RunLoop(&video));
    for (int i = 0; i < number_temporal_layers_ * number_spatial_layers_; i++) {
      ASSERT_GE(effective_datarate_tl[i], target_layer_bitrate_[i] * 0.70)
          << " The datarate for the file is lower than target by too much!";
      ASSERT_LE(effective_datarate_tl[i], target_layer_bitrate_[i] * 1.45)
          << " The datarate for the file is greater than target by too much!";
    }
  }

  virtual void BasicRateTargetingFixedModeSVC3TL3SLHDTest() {
    cfg_.rc_buf_initial_sz = 500;
    cfg_.rc_buf_optimal_sz = 500;
    cfg_.rc_buf_sz = 1000;
    cfg_.rc_dropframe_thresh = 0;
    cfg_.rc_min_quantizer = 0;
    cfg_.rc_max_quantizer = 63;
    cfg_.rc_end_usage = AOM_CBR;
    cfg_.g_lag_in_frames = 0;
    cfg_.g_error_resilient = 0;

    ::libaom_test::Y4mVideoSource video("niklas_1280_720_30.y4m", 0, 60);
    const int bitrate_array[2] = { 600, 1200 };
    cfg_.rc_target_bitrate = bitrate_array[GET_PARAM(4)];
    ResetModel();
    number_temporal_layers_ = 3;
    number_spatial_layers_ = 3;
    use_fixed_mode_svc_ = 1;
    // SL0
    const int bitrate_sl0 = 1 * cfg_.rc_target_bitrate / 8;
    target_layer_bitrate_[0] = 50 * bitrate_sl0 / 100;
    target_layer_bitrate_[1] = 70 * bitrate_sl0 / 100;
    target_layer_bitrate_[2] = bitrate_sl0;
    // SL1
    const int bitrate_sl1 = 3 * cfg_.rc_target_bitrate / 8;
    target_layer_bitrate_[3] = 50 * bitrate_sl1 / 100;
    target_layer_bitrate_[4] = 70 * bitrate_sl1 / 100;
    target_layer_bitrate_[5] = bitrate_sl1;
    // SL2
    const int bitrate_sl2 = 4 * cfg_.rc_target_bitrate / 8;
    target_layer_bitrate_[6] = 50 * bitrate_sl2 / 100;
    target_layer_bitrate_[7] = 70 * bitrate_sl2 / 100;
    target_layer_bitrate_[8] = bitrate_sl2;
    ASSERT_NO_FATAL_FAILURE(RunLoop(&video));
    for (int i = 0; i < number_temporal_layers_ * number_spatial_layers_; i++) {
      ASSERT_GE(effective_datarate_tl[i], target_layer_bitrate_[i] * 0.70)
          << " The datarate for the file is lower than target by too much!";
      ASSERT_LE(effective_datarate_tl[i], target_layer_bitrate_[i] * 1.45)
          << " The datarate for the file is greater than target by too much!";
    }
  }

  virtual void BasicRateTargetingSVC3TL3SLMultiThreadSpeedPerLayerTest() {
    cfg_.rc_buf_initial_sz = 500;
    cfg_.rc_buf_optimal_sz = 500;
    cfg_.rc_buf_sz = 1000;
    cfg_.rc_dropframe_thresh = 0;
    cfg_.rc_min_quantizer = 0;
    cfg_.rc_max_quantizer = 63;
    cfg_.rc_end_usage = AOM_CBR;
    cfg_.g_lag_in_frames = 0;
    cfg_.g_error_resilient = 0;
    cfg_.g_threads = 2;
    ::libaom_test::I420VideoSource video("niklas_640_480_30.yuv", 640, 480, 30,
                                         1, 0, 400);
    cfg_.g_w = 640;
    cfg_.g_h = 480;
    const int bitrate_array[2] = { 600, 1200 };
    cfg_.rc_target_bitrate = bitrate_array[GET_PARAM(4)];
    ResetModel();
    tile_columns_ = 1;
    tile_rows_ = 0;
    set_speed_per_layer_ = true;
    number_temporal_layers_ = 3;
    number_spatial_layers_ = 3;
    // SL0
    const int bitrate_sl0 = 1 * cfg_.rc_target_bitrate / 8;
    target_layer_bitrate_[0] = 50 * bitrate_sl0 / 100;
    target_layer_bitrate_[1] = 70 * bitrate_sl0 / 100;
    target_layer_bitrate_[2] = bitrate_sl0;
    // SL1
    const int bitrate_sl1 = 3 * cfg_.rc_target_bitrate / 8;
    target_layer_bitrate_[3] = 50 * bitrate_sl1 / 100;
    target_layer_bitrate_[4] = 70 * bitrate_sl1 / 100;
    target_layer_bitrate_[5] = bitrate_sl1;
    // SL2
    const int bitrate_sl2 = 4 * cfg_.rc_target_bitrate / 8;
    target_layer_bitrate_[6] = 50 * bitrate_sl2 / 100;
    target_layer_bitrate_[7] = 70 * bitrate_sl2 / 100;
    target_layer_bitrate_[8] = bitrate_sl2;
    ASSERT_NO_FATAL_FAILURE(RunLoop(&video));
    for (int i = 0; i < number_temporal_layers_ * number_spatial_layers_; i++) {
      ASSERT_GE(effective_datarate_tl[i], target_layer_bitrate_[i] * 0.70)
          << " The datarate for the file is lower than target by too much!";
      ASSERT_LE(effective_datarate_tl[i], target_layer_bitrate_[i] * 1.45)
          << " The datarate for the file is greater than target by too much!";
    }
  }

  virtual void BasicRateTargetingSVC3TL3SLHDMultiThread2Test() {
    cfg_.rc_buf_initial_sz = 500;
    cfg_.rc_buf_optimal_sz = 500;
    cfg_.rc_buf_sz = 1000;
    cfg_.rc_dropframe_thresh = 0;
    cfg_.rc_min_quantizer = 0;
    cfg_.rc_max_quantizer = 63;
    cfg_.rc_end_usage = AOM_CBR;
    cfg_.g_lag_in_frames = 0;
    cfg_.g_error_resilient = 0;
    cfg_.g_threads = 2;

    ::libaom_test::Y4mVideoSource video("niklas_1280_720_30.y4m", 0, 60);
    const int bitrate_array[2] = { 600, 1200 };
    cfg_.rc_target_bitrate = bitrate_array[GET_PARAM(4)];
    ResetModel();
    tile_columns_ = 1;
    tile_rows_ = 0;
    number_temporal_layers_ = 3;
    number_spatial_layers_ = 3;
    // SL0
    const int bitrate_sl0 = 1 * cfg_.rc_target_bitrate / 8;
    target_layer_bitrate_[0] = 50 * bitrate_sl0 / 100;
    target_layer_bitrate_[1] = 70 * bitrate_sl0 / 100;
    target_layer_bitrate_[2] = bitrate_sl0;
    // SL1
    const int bitrate_sl1 = 3 * cfg_.rc_target_bitrate / 8;
    target_layer_bitrate_[3] = 50 * bitrate_sl1 / 100;
    target_layer_bitrate_[4] = 70 * bitrate_sl1 / 100;
    target_layer_bitrate_[5] = bitrate_sl1;
    // SL2
    const int bitrate_sl2 = 4 * cfg_.rc_target_bitrate / 8;
    target_layer_bitrate_[6] = 50 * bitrate_sl2 / 100;
    target_layer_bitrate_[7] = 70 * bitrate_sl2 / 100;
    target_layer_bitrate_[8] = bitrate_sl2;
    ASSERT_NO_FATAL_FAILURE(RunLoop(&video));
    for (int i = 0; i < number_temporal_layers_ * number_spatial_layers_; i++) {
      ASSERT_GE(effective_datarate_tl[i], target_layer_bitrate_[i] * 0.70)
          << " The datarate for the file is lower than target by too much!";
      ASSERT_LE(effective_datarate_tl[i], target_layer_bitrate_[i] * 1.45)
          << " The datarate for the file is greater than target by too much!";
    }
  }

  virtual void BasicRateTargetingSVC2TL1SLHDMultiThread4Test() {
    cfg_.rc_buf_initial_sz = 500;
    cfg_.rc_buf_optimal_sz = 500;
    cfg_.rc_buf_sz = 1000;
    cfg_.rc_dropframe_thresh = 0;
    cfg_.rc_min_quantizer = 0;
    cfg_.rc_max_quantizer = 63;
    cfg_.rc_end_usage = AOM_CBR;
    cfg_.g_lag_in_frames = 0;
    cfg_.g_error_resilient = 0;
    cfg_.g_threads = 4;

    ::libaom_test::Y4mVideoSource video("niklas_1280_720_30.y4m", 0, 60);
    const int bitrate_array[2] = { 600, 1200 };
    cfg_.rc_target_bitrate = bitrate_array[GET_PARAM(4)];
    ResetModel();
    tile_columns_ = 1;
    tile_rows_ = 1;
    number_temporal_layers_ = 2;
    number_spatial_layers_ = 1;
    target_layer_bitrate_[0] = 60 * cfg_.rc_target_bitrate / 100;
    target_layer_bitrate_[1] = cfg_.rc_target_bitrate;
    ASSERT_NO_FATAL_FAILURE(RunLoop(&video));
    for (int i = 0; i < number_temporal_layers_ * number_spatial_layers_; i++) {
      ASSERT_GE(effective_datarate_tl[i], target_layer_bitrate_[i] * 0.70)
          << " The datarate for the file is lower than target by too much!";
      ASSERT_LE(effective_datarate_tl[i], target_layer_bitrate_[i] * 1.45)
          << " The datarate for the file is greater than target by too much!";
    }
  }

  virtual void BasicRateTargetingSVC2TL1SLHDMultiThread4AutoTilesTest() {
    cfg_.rc_buf_initial_sz = 500;
    cfg_.rc_buf_optimal_sz = 500;
    cfg_.rc_buf_sz = 1000;
    cfg_.rc_dropframe_thresh = 0;
    cfg_.rc_min_quantizer = 0;
    cfg_.rc_max_quantizer = 63;
    cfg_.rc_end_usage = AOM_CBR;
    cfg_.g_lag_in_frames = 0;
    cfg_.g_error_resilient = 0;
    cfg_.g_threads = 4;

    ::libaom_test::Y4mVideoSource video("niklas_1280_720_30.y4m", 0, 60);
    const int bitrate_array[2] = { 600, 1200 };
    cfg_.rc_target_bitrate = bitrate_array[GET_PARAM(4)];
    ResetModel();
    auto_tiles_ = 1;
    number_temporal_layers_ = 2;
    number_spatial_layers_ = 1;
    target_layer_bitrate_[0] = 60 * cfg_.rc_target_bitrate / 100;
    target_layer_bitrate_[1] = cfg_.rc_target_bitrate;
    ASSERT_NO_FATAL_FAILURE(RunLoop(&video));
    for (int i = 0; i < number_temporal_layers_ * number_spatial_layers_; i++) {
      ASSERT_GE(effective_datarate_tl[i], target_layer_bitrate_[i] * 0.70)
          << " The datarate for the file is lower than target by too much!";
      ASSERT_LE(effective_datarate_tl[i], target_layer_bitrate_[i] * 1.45)
          << " The datarate for the file is greater than target by too much!";
    }
  }

  virtual void BasicRateTargetingSVC3TL3SLHDMultiThread4Test() {
    cfg_.rc_buf_initial_sz = 500;
    cfg_.rc_buf_optimal_sz = 500;
    cfg_.rc_buf_sz = 1000;
    cfg_.rc_dropframe_thresh = 0;
    cfg_.rc_min_quantizer = 0;
    cfg_.rc_max_quantizer = 63;
    cfg_.rc_end_usage = AOM_CBR;
    cfg_.g_lag_in_frames = 0;
    cfg_.g_error_resilient = 0;
    cfg_.g_threads = 4;

    ::libaom_test::Y4mVideoSource video("niklas_1280_720_30.y4m", 0, 60);
    const int bitrate_array[2] = { 600, 1200 };
    cfg_.rc_target_bitrate = bitrate_array[GET_PARAM(4)];
    ResetModel();
    tile_columns_ = 1;
    tile_rows_ = 1;
    number_temporal_layers_ = 3;
    number_spatial_layers_ = 3;
    // SL0
    const int bitrate_sl0 = 1 * cfg_.rc_target_bitrate / 8;
    target_layer_bitrate_[0] = 50 * bitrate_sl0 / 100;
    target_layer_bitrate_[1] = 70 * bitrate_sl0 / 100;
    target_layer_bitrate_[2] = bitrate_sl0;
    // SL1
    const int bitrate_sl1 = 3 * cfg_.rc_target_bitrate / 8;
    target_layer_bitrate_[3] = 50 * bitrate_sl1 / 100;
    target_layer_bitrate_[4] = 70 * bitrate_sl1 / 100;
    target_layer_bitrate_[5] = bitrate_sl1;
    // SL2
    const int bitrate_sl2 = 4 * cfg_.rc_target_bitrate / 8;
    target_layer_bitrate_[6] = 50 * bitrate_sl2 / 100;
    target_layer_bitrate_[7] = 70 * bitrate_sl2 / 100;
    target_layer_bitrate_[8] = bitrate_sl2;
    ASSERT_NO_FATAL_FAILURE(RunLoop(&video));
    for (int i = 0; i < number_temporal_layers_ * number_spatial_layers_; i++) {
      ASSERT_GE(effective_datarate_tl[i], target_layer_bitrate_[i] * 0.70)
          << " The datarate for the file is lower than target by too much!";
      ASSERT_LE(effective_datarate_tl[i], target_layer_bitrate_[i] * 1.45)
          << " The datarate for the file is greater than target by too much!";
    }
  }

  virtual void BasicRateTargetingSVC3TL3SLHDMultiRefTest() {
    cfg_.rc_buf_initial_sz = 500;
    cfg_.rc_buf_optimal_sz = 500;
    cfg_.rc_buf_sz = 1000;
    cfg_.rc_dropframe_thresh = 0;
    cfg_.rc_min_quantizer = 0;
    cfg_.rc_max_quantizer = 63;
    cfg_.rc_end_usage = AOM_CBR;
    cfg_.g_lag_in_frames = 0;
    cfg_.g_error_resilient = 0;

    ::libaom_test::Y4mVideoSource video("niklas_1280_720_30.y4m", 0, 60);
    const int bitrate_array[2] = { 600, 1200 };
    cfg_.rc_target_bitrate = bitrate_array[GET_PARAM(4)];
    ResetModel();
    multi_ref_ = 1;
    number_temporal_layers_ = 3;
    number_spatial_layers_ = 3;
    // SL0
    const int bitrate_sl0 = 1 * cfg_.rc_target_bitrate / 8;
    target_layer_bitrate_[0] = 50 * bitrate_sl0 / 100;
    target_layer_bitrate_[1] = 70 * bitrate_sl0 / 100;
    target_layer_bitrate_[2] = bitrate_sl0;
    // SL1
    const int bitrate_sl1 = 3 * cfg_.rc_target_bitrate / 8;
    target_layer_bitrate_[3] = 50 * bitrate_sl1 / 100;
    target_layer_bitrate_[4] = 70 * bitrate_sl1 / 100;
    target_layer_bitrate_[5] = bitrate_sl1;
    // SL2
    const int bitrate_sl2 = 4 * cfg_.rc_target_bitrate / 8;
    target_layer_bitrate_[6] = 50 * bitrate_sl2 / 100;
    target_layer_bitrate_[7] = 70 * bitrate_sl2 / 100;
    target_layer_bitrate_[8] = bitrate_sl2;
    ASSERT_NO_FATAL_FAILURE(RunLoop(&video));
    for (int i = 0; i < number_temporal_layers_ * number_spatial_layers_; i++) {
      ASSERT_GE(effective_datarate_tl[i], target_layer_bitrate_[i] * 0.70)
          << " The datarate for the file is lower than target by too much!";
      ASSERT_LE(effective_datarate_tl[i], target_layer_bitrate_[i] * 1.45)
          << " The datarate for the file is greater than target by too much!";
    }
  }

  virtual void BasicRateTargetingSVC3TL3SLKfTest() {
    cfg_.rc_buf_initial_sz = 500;
    cfg_.rc_buf_optimal_sz = 500;
    cfg_.rc_buf_sz = 1000;
    cfg_.rc_dropframe_thresh = 0;
    cfg_.rc_min_quantizer = 0;
    cfg_.rc_max_quantizer = 63;
    cfg_.rc_end_usage = AOM_CBR;
    cfg_.g_lag_in_frames = 0;
    cfg_.g_error_resilient = 0;
    cfg_.kf_mode = AOM_KF_AUTO;
    cfg_.kf_min_dist = cfg_.kf_max_dist = 100;

    ::libaom_test::I420VideoSource video("hantro_collage_w352h288.yuv", 352,
                                         288, 30, 1, 0, 300);
    const int bitrate_array[2] = { 600, 1200 };
    cfg_.rc_target_bitrate = bitrate_array[GET_PARAM(4)];
    ResetModel();
    number_temporal_layers_ = 3;
    number_spatial_layers_ = 3;
    // SL0
    const int bitrate_sl0 = 1 * cfg_.rc_target_bitrate / 8;
    target_layer_bitrate_[0] = 50 * bitrate_sl0 / 100;
    target_layer_bitrate_[1] = 70 * bitrate_sl0 / 100;
    target_layer_bitrate_[2] = bitrate_sl0;
    // SL1
    const int bitrate_sl1 = 3 * cfg_.rc_target_bitrate / 8;
    target_layer_bitrate_[3] = 50 * bitrate_sl1 / 100;
    target_layer_bitrate_[4] = 70 * bitrate_sl1 / 100;
    target_layer_bitrate_[5] = bitrate_sl1;
    // SL2
    const int bitrate_sl2 = 4 * cfg_.rc_target_bitrate / 8;
    target_layer_bitrate_[6] = 50 * bitrate_sl2 / 100;
    target_layer_bitrate_[7] = 70 * bitrate_sl2 / 100;
    target_layer_bitrate_[8] = bitrate_sl2;
    ASSERT_NO_FATAL_FAILURE(RunLoop(&video));
    for (int i = 0; i < number_temporal_layers_ * number_spatial_layers_; i++) {
      ASSERT_GE(effective_datarate_tl[i], target_layer_bitrate_[i] * 0.55)
          << " The datarate for the file is lower than target by too much!";
      ASSERT_LE(effective_datarate_tl[i], target_layer_bitrate_[i] * 1.4)
          << " The datarate for the file is greater than target by too much!";
    }
  }

  virtual void BasicRateTargeting444SVC3TL3SLTest() {
    cfg_.rc_buf_initial_sz = 500;
    cfg_.rc_buf_optimal_sz = 500;
    cfg_.rc_buf_sz = 1000;
    cfg_.rc_dropframe_thresh = 0;
    cfg_.rc_min_quantizer = 0;
    cfg_.rc_max_quantizer = 63;
    cfg_.rc_end_usage = AOM_CBR;
    cfg_.g_lag_in_frames = 0;
    cfg_.g_error_resilient = 0;
    cfg_.g_profile = 1;

    ::libaom_test::Y4mVideoSource video("rush_hour_444.y4m", 0, 140);

    const int bitrate_array[2] = { 600, 1200 };
    cfg_.rc_target_bitrate = bitrate_array[GET_PARAM(4)];
    ResetModel();
    number_temporal_layers_ = 3;
    number_spatial_layers_ = 3;
    // SL0
    const int bitrate_sl0 = 1 * cfg_.rc_target_bitrate / 8;
    target_layer_bitrate_[0] = 50 * bitrate_sl0 / 100;
    target_layer_bitrate_[1] = 70 * bitrate_sl0 / 100;
    target_layer_bitrate_[2] = bitrate_sl0;
    // SL1
    const int bitrate_sl1 = 3 * cfg_.rc_target_bitrate / 8;
    target_layer_bitrate_[3] = 50 * bitrate_sl1 / 100;
    target_layer_bitrate_[4] = 70 * bitrate_sl1 / 100;
    target_layer_bitrate_[5] = bitrate_sl1;
    // SL2
    const int bitrate_sl2 = 4 * cfg_.rc_target_bitrate / 8;
    target_layer_bitrate_[6] = 50 * bitrate_sl2 / 100;
    target_layer_bitrate_[7] = 70 * bitrate_sl2 / 100;
    target_layer_bitrate_[8] = bitrate_sl2;
    ASSERT_NO_FATAL_FAILURE(RunLoop(&video));
    for (int i = 0; i < number_temporal_layers_ * number_spatial_layers_; i++) {
      ASSERT_GE(effective_datarate_tl[i], target_layer_bitrate_[i] * 0.70)
          << " The datarate for the file is lower than target by too much!";
      ASSERT_LE(effective_datarate_tl[i], target_layer_bitrate_[i] * 1.38)
          << " The datarate for the file is greater than target by too much!";
    }
  }

  virtual void BasicRateTargetingSVC3TL1SLMultiRefDropAllEnhTest() {
    cfg_.rc_buf_initial_sz = 500;
    cfg_.rc_buf_optimal_sz = 500;
    cfg_.rc_buf_sz = 1000;
    cfg_.rc_dropframe_thresh = 0;
    cfg_.rc_min_quantizer = 0;
    cfg_.rc_max_quantizer = 63;
    cfg_.rc_end_usage = AOM_CBR;
    cfg_.g_lag_in_frames = 0;
    // error_resilient can set to off/0, since for SVC the context update
    // is done per-layer.
    cfg_.g_error_resilient = 0;

    ::libaom_test::I420VideoSource video("hantro_collage_w352h288.yuv", 352,
                                         288, 30, 1, 0, 300);
    const int bitrate_array[2] = { 200, 550 };
    cfg_.rc_target_bitrate = bitrate_array[GET_PARAM(4)];
    ResetModel();
    multi_ref_ = 1;
    // Drop TL1 and TL2: #frames(300) - #TL0.
    drop_frames_ = 300 - 300 / 4;
    int n = 0;
    for (int i = 0; i < 300; i++) {
      if (i % 4 != 0) {
        drop_frames_list_[n] = i;
        n++;
      }
    }
    number_temporal_layers_ = 3;
    target_layer_bitrate_[0] = 50 * cfg_.rc_target_bitrate / 100;
    target_layer_bitrate_[1] = 70 * cfg_.rc_target_bitrate / 100;
    target_layer_bitrate_[2] = cfg_.rc_target_bitrate;
    ASSERT_NO_FATAL_FAILURE(RunLoop(&video));
    for (int i = 0; i < number_temporal_layers_ * number_spatial_layers_; i++) {
      ASSERT_GE(effective_datarate_tl[i], target_layer_bitrate_[i] * 0.60)
          << " The datarate for the file is lower than target by too much!";
      ASSERT_LE(effective_datarate_tl[i], target_layer_bitrate_[i] * 1.60)
          << " The datarate for the file is greater than target by too much!";
    }
#if CONFIG_AV1_DECODER
    // Test that no mismatches have been found.
    std::cout << "          Decoded frames: " << GetDecodedFrames() << "\n";
    std::cout << "          Mismatch frames: " << GetMismatchFrames() << "\n";
    EXPECT_EQ(300 - GetDecodedFrames(), drop_frames_);
    EXPECT_EQ((int)GetMismatchFrames(), 0);
#endif
  }

  virtual void BasicRateTargetingSVC3TL1SLDropAllEnhTest() {
    cfg_.rc_buf_initial_sz = 500;
    cfg_.rc_buf_optimal_sz = 500;
    cfg_.rc_buf_sz = 1000;
    cfg_.rc_dropframe_thresh = 0;
    cfg_.rc_min_quantizer = 0;
    cfg_.rc_max_quantizer = 63;
    cfg_.rc_end_usage = AOM_CBR;
    cfg_.g_lag_in_frames = 0;
    // error_resilient can set to off/0, since for SVC the context update
    // is done per-layer.
    cfg_.g_error_resilient = 0;

    ::libaom_test::I420VideoSource video("hantro_collage_w352h288.yuv", 352,
                                         288, 30, 1, 0, 300);
    const int bitrate_array[2] = { 200, 550 };
    cfg_.rc_target_bitrate = bitrate_array[GET_PARAM(4)];
    ResetModel();
    // Drop TL1 and TL2: #frames(300) - #TL0.
    drop_frames_ = 300 - 300 / 4;
    int n = 0;
    for (int i = 0; i < 300; i++) {
      if (i % 4 != 0) {
        drop_frames_list_[n] = i;
        n++;
      }
    }
    number_temporal_layers_ = 3;
    target_layer_bitrate_[0] = 50 * cfg_.rc_target_bitrate / 100;
    target_layer_bitrate_[1] = 70 * cfg_.rc_target_bitrate / 100;
    target_layer_bitrate_[2] = cfg_.rc_target_bitrate;
    ASSERT_NO_FATAL_FAILURE(RunLoop(&video));
    for (int i = 0; i < number_temporal_layers_ * number_spatial_layers_; i++) {
      ASSERT_GE(effective_datarate_tl[i], target_layer_bitrate_[i] * 0.60)
          << " The datarate for the file is lower than target by too much!";
      ASSERT_LE(effective_datarate_tl[i], target_layer_bitrate_[i] * 1.60)
          << " The datarate for the file is greater than target by too much!";
    }
#if CONFIG_AV1_DECODER
    // Test that no mismatches have been found.
    std::cout << "          Decoded frames: " << GetDecodedFrames() << "\n";
    std::cout << "          Mismatch frames: " << GetMismatchFrames() << "\n";
    EXPECT_EQ(300 - GetDecodedFrames(), drop_frames_);
    EXPECT_EQ((int)GetMismatchFrames(), 0);
#endif
  }

  virtual void BasicRateTargetingSVC3TL1SLDropTL2EnhTest() {
    cfg_.rc_buf_initial_sz = 500;
    cfg_.rc_buf_optimal_sz = 500;
    cfg_.rc_buf_sz = 1000;
    cfg_.rc_dropframe_thresh = 0;
    cfg_.rc_min_quantizer = 0;
    cfg_.rc_max_quantizer = 63;
    cfg_.rc_end_usage = AOM_CBR;
    cfg_.g_lag_in_frames = 0;
    // error_resilient for sequence can be off/0, since dropped frames (TL2)
    // are non-reference frames.
    cfg_.g_error_resilient = 0;

    ::libaom_test::I420VideoSource video("hantro_collage_w352h288.yuv", 352,
                                         288, 30, 1, 0, 300);
    const int bitrate_array[2] = { 200, 550 };
    cfg_.rc_target_bitrate = bitrate_array[GET_PARAM(4)];
    ResetModel();
    // Drop TL2: #frames(300) - (#TL0 + #TL1).
    drop_frames_ = 300 - 300 / 2;
    int n = 0;
    for (int i = 0; i < 300; i++) {
      if (i % 2 != 0) {
        drop_frames_list_[n] = i;
        n++;
      }
    }
    number_temporal_layers_ = 3;
    target_layer_bitrate_[0] = 50 * cfg_.rc_target_bitrate / 100;
    target_layer_bitrate_[1] = 70 * cfg_.rc_target_bitrate / 100;
    target_layer_bitrate_[2] = cfg_.rc_target_bitrate;
    ASSERT_NO_FATAL_FAILURE(RunLoop(&video));
    for (int i = 0; i < number_temporal_layers_ * number_spatial_layers_; i++) {
      ASSERT_GE(effective_datarate_tl[i], target_layer_bitrate_[i] * 0.60)
          << " The datarate for the file is lower than target by too much!";
      ASSERT_LE(effective_datarate_tl[i], target_layer_bitrate_[i] * 1.60)
          << " The datarate for the file is greater than target by too much!";
    }
#if CONFIG_AV1_DECODER
    // Test that no mismatches have been found.
    std::cout << "          Decoded frames: " << GetDecodedFrames() << "\n";
    std::cout << "          Mismatch frames: " << GetMismatchFrames() << "\n";
    EXPECT_EQ(300 - GetDecodedFrames(), drop_frames_);
    EXPECT_EQ((int)GetMismatchFrames(), 0);
#endif
  }

  virtual void BasicRateTargetingSVC3TL1SLDropAllEnhFrameERTest() {
    cfg_.rc_buf_initial_sz = 500;
    cfg_.rc_buf_optimal_sz = 500;
    cfg_.rc_buf_sz = 1000;
    cfg_.rc_dropframe_thresh = 0;
    cfg_.rc_min_quantizer = 0;
    cfg_.rc_max_quantizer = 63;
    cfg_.rc_end_usage = AOM_CBR;
    cfg_.g_lag_in_frames = 0;

    ::libaom_test::I420VideoSource video("hantro_collage_w352h288.yuv", 352,
                                         288, 30, 1, 0, 300);
    const int bitrate_array[2] = { 200, 550 };
    cfg_.rc_target_bitrate = bitrate_array[GET_PARAM(4)];
    ResetModel();
    // Set error_resilience at frame level, with codec control,
    // on/1 for enahancement layers and off/0 for base layer frames.
    set_frame_level_er_ = 1;

    // Drop TL1 and TL2: #frames(300) - #TL0.
    drop_frames_ = 300 - 300 / 4;
    int n = 0;
    for (int i = 0; i < 300; i++) {
      if (i % 4 != 0) {
        drop_frames_list_[n] = i;
        n++;
      }
    }
    number_temporal_layers_ = 3;
    target_layer_bitrate_[0] = 50 * cfg_.rc_target_bitrate / 100;
    target_layer_bitrate_[1] = 70 * cfg_.rc_target_bitrate / 100;
    target_layer_bitrate_[2] = cfg_.rc_target_bitrate;
    ASSERT_NO_FATAL_FAILURE(RunLoop(&video));
    for (int i = 0; i < number_temporal_layers_ * number_spatial_layers_; i++) {
      ASSERT_GE(effective_datarate_tl[i], target_layer_bitrate_[i] * 0.60)
          << " The datarate for the file is lower than target by too much!";
      ASSERT_LE(effective_datarate_tl[i], target_layer_bitrate_[i] * 1.60)
          << " The datarate for the file is greater than target by too much!";
    }
#if CONFIG_AV1_DECODER
    // Test that no mismatches have been found.
    std::cout << "          Decoded frames: " << GetDecodedFrames() << "\n";
    std::cout << "          Mismatch frames: " << GetMismatchFrames() << "\n";
    EXPECT_EQ(300 - GetDecodedFrames(), drop_frames_);
    EXPECT_EQ((int)GetMismatchFrames(), 0);
#endif
  }

  virtual void BasicRateTargetingSVC3TL1SLDropSetEnhFrameERTest() {
    cfg_.rc_buf_initial_sz = 500;
    cfg_.rc_buf_optimal_sz = 500;
    cfg_.rc_buf_sz = 1000;
    cfg_.rc_dropframe_thresh = 0;
    cfg_.rc_min_quantizer = 0;
    cfg_.rc_max_quantizer = 63;
    cfg_.rc_end_usage = AOM_CBR;
    cfg_.g_lag_in_frames = 0;

    ::libaom_test::I420VideoSource video("hantro_collage_w352h288.yuv", 352,
                                         288, 30, 1, 0, 300);
    const int bitrate_array[2] = { 200, 550 };
    cfg_.rc_target_bitrate = bitrate_array[GET_PARAM(4)];
    ResetModel();
    // Set error_resilience at frame level, with codec control,
    // on/1 for enahancement layers and off/0 for base layer frames.
    set_frame_level_er_ = 1;

    // Drop TL1 and TL2: for part of sequence. Start at first TL2 at
    // frame 101, and end at second T2 at frame 199. Frame 200 is TL0,
    // so we can continue decoding without mismatch (since LAST is the
    // only reference and error_resilient = 1 on TL1/TL2 frames).
    int n = 0;
#if CONFIG_AV1_DECODER
    int num_nonref = 300 / 2;
#endif
    for (int i = 101; i < 200; i++) {
      if (i % 4 != 0) {
        drop_frames_list_[n] = i;
        n++;
#if CONFIG_AV1_DECODER
        if (i % 2 != 0) num_nonref -= 1;
#endif
      }
    }
    drop_frames_ = n;
    number_temporal_layers_ = 3;
    target_layer_bitrate_[0] = 50 * cfg_.rc_target_bitrate / 100;
    target_layer_bitrate_[1] = 70 * cfg_.rc_target_bitrate / 100;
    target_layer_bitrate_[2] = cfg_.rc_target_bitrate;
    ASSERT_NO_FATAL_FAILURE(RunLoop(&video));
    for (int i = 0; i < number_temporal_layers_ * number_spatial_layers_; i++) {
      ASSERT_GE(effective_datarate_tl[i], target_layer_bitrate_[i] * 0.60)
          << " The datarate for the file is lower than target by too much!";
      ASSERT_LE(effective_datarate_tl[i], target_layer_bitrate_[i] * 1.60)
          << " The datarate for the file is greater than target by too much!";
    }
#if CONFIG_AV1_DECODER
    // Test that no mismatches have been found.
    std::cout << "          Decoded frames: " << GetDecodedFrames() << "\n";
    std::cout << "          Mismatch frames: " << GetMismatchFrames() << "\n";
    EXPECT_EQ(300 - GetDecodedFrames(), drop_frames_);
    EXPECT_EQ((int)GetMismatchFrames(), num_nonref);
#endif
  }

  virtual void BasicRateTargetingSVC2TL1SLDropSetEnhER0Test() {
    cfg_.rc_buf_initial_sz = 500;
    cfg_.rc_buf_optimal_sz = 500;
    cfg_.rc_buf_sz = 1000;
    cfg_.rc_dropframe_thresh = 0;
    cfg_.rc_min_quantizer = 0;
    cfg_.rc_max_quantizer = 63;
    cfg_.rc_end_usage = AOM_CBR;
    cfg_.g_lag_in_frames = 0;

    ::libaom_test::I420VideoSource video("hantro_collage_w352h288.yuv", 352,
                                         288, 30, 1, 0, 300);
    const int bitrate_array[2] = { 200, 550 };
    cfg_.rc_target_bitrate = bitrate_array[GET_PARAM(4)];
    ResetModel();

    // Set error_resilience off.
    cfg_.g_error_resilient = 0;

    // Drop TL1: for part of sequence. Start at first TL1 at
    // frame 101, and end at frame 199. Frame 200 is TL0,
    // so we can continue decoding without mismatch (since LAST is the
    // only reference).
    int n = 0;
#if CONFIG_AV1_DECODER
    int num_nonref = 300 / 2;
#endif
    for (int i = 101; i < 200; i++) {
      if (i % 2 != 0) {
        drop_frames_list_[n] = i;
        n++;
#if CONFIG_AV1_DECODER
        if (i % 2 != 0) num_nonref -= 1;
#endif
      }
    }
    drop_frames_ = n;
    number_temporal_layers_ = 2;
    target_layer_bitrate_[0] = 70 * cfg_.rc_target_bitrate / 100;
    target_layer_bitrate_[1] = cfg_.rc_target_bitrate;
    ASSERT_NO_FATAL_FAILURE(RunLoop(&video));
    for (int i = 0; i < number_temporal_layers_ * number_spatial_layers_; i++) {
      ASSERT_GE(effective_datarate_tl[i], target_layer_bitrate_[i] * 0.60)
          << " The datarate for the file is lower than target by too much!";
      ASSERT_LE(effective_datarate_tl[i], target_layer_bitrate_[i] * 1.60)
          << " The datarate for the file is greater than target by too much!";
    }
#if CONFIG_AV1_DECODER
    // Test that no mismatches have been found.
    std::cout << "          Decoded frames: " << GetDecodedFrames() << "\n";
    std::cout << "          Mismatch frames: " << GetMismatchFrames() << "\n";
    EXPECT_EQ(300 - GetDecodedFrames(), drop_frames_);
    EXPECT_EQ((int)GetMismatchFrames(), num_nonref);
#endif
  }

  virtual void BasicRateTargetingSVC3TL1SLDropSetEnhER0Test() {
    cfg_.rc_buf_initial_sz = 500;
    cfg_.rc_buf_optimal_sz = 500;
    cfg_.rc_buf_sz = 1000;
    cfg_.rc_dropframe_thresh = 0;
    cfg_.rc_min_quantizer = 0;
    cfg_.rc_max_quantizer = 63;
    cfg_.rc_end_usage = AOM_CBR;
    cfg_.g_lag_in_frames = 0;

    ::libaom_test::I420VideoSource video("hantro_collage_w352h288.yuv", 352,
                                         288, 30, 1, 0, 300);
    const int bitrate_array[2] = { 200, 550 };
    cfg_.rc_target_bitrate = bitrate_array[GET_PARAM(4)];
    ResetModel();

    // Set error_resilience off.
    cfg_.g_error_resilient = 0;

    // Drop TL1 and TL2: for part of sequence. Start at first TL2 at
    // frame 101, and end at second T2 at frame 199. Frame 200 is TL0,
    // so we can continue decoding without mismatch (since LAST is the
    // only reference).
    int n = 0;
#if CONFIG_AV1_DECODER
    int num_nonref = 300 / 2;
#endif
    for (int i = 101; i < 200; i++) {
      if (i % 4 != 0) {
        drop_frames_list_[n] = i;
        n++;
#if CONFIG_AV1_DECODER
        if (i % 2 != 0) num_nonref -= 1;
#endif
      }
    }
    drop_frames_ = n;
    number_temporal_layers_ = 3;
    target_layer_bitrate_[0] = 50 * cfg_.rc_target_bitrate / 100;
    target_layer_bitrate_[1] = 70 * cfg_.rc_target_bitrate / 100;
    target_layer_bitrate_[2] = cfg_.rc_target_bitrate;
    ASSERT_NO_FATAL_FAILURE(RunLoop(&video));
    for (int i = 0; i < number_temporal_layers_ * number_spatial_layers_; i++) {
      ASSERT_GE(effective_datarate_tl[i], target_layer_bitrate_[i] * 0.60)
          << " The datarate for the file is lower than target by too much!";
      ASSERT_LE(effective_datarate_tl[i], target_layer_bitrate_[i] * 1.60)
          << " The datarate for the file is greater than target by too much!";
    }
#if CONFIG_AV1_DECODER
    // Test that no mismatches have been found.
    std::cout << "          Decoded frames: " << GetDecodedFrames() << "\n";
    std::cout << "          Mismatch frames: " << GetMismatchFrames() << "\n";
    EXPECT_EQ(300 - GetDecodedFrames(), drop_frames_);
    EXPECT_EQ((int)GetMismatchFrames(), num_nonref);
#endif
  }

  virtual void BasicRateTargetingSVC3TL3SLDropSetEnhER0Test() {
    cfg_.rc_buf_initial_sz = 500;
    cfg_.rc_buf_optimal_sz = 500;
    cfg_.rc_buf_sz = 1000;
    cfg_.rc_dropframe_thresh = 0;
    cfg_.rc_min_quantizer = 0;
    cfg_.rc_max_quantizer = 63;
    cfg_.rc_end_usage = AOM_CBR;
    cfg_.g_lag_in_frames = 0;
    ::libaom_test::I420VideoSource video("hantro_collage_w352h288.yuv", 352,
                                         288, 30, 1, 0, 300);
    const int bitrate_array[2] = { 200, 550 };
    cfg_.rc_target_bitrate = bitrate_array[GET_PARAM(4)];
    ResetModel();
    // Set error_resilience off.
    cfg_.g_error_resilient = 0;
    // Drop TL1 and TL2: for part of sequence. Start at first TL2 at
    // frame 101, and end at second T2 at frame 199. Frame 200 is TL0,
    // so we can continue decoding without mismatch (since LAST is the
    // only reference).
    // Drop here means drop whole superframe.
    int n = 0;
#if CONFIG_AV1_DECODER
    int num_nonref = 300 / 2;
#endif
    for (int i = 101; i < 200; i++) {
      if (i % 4 != 0) {
        drop_frames_list_[n] = i;
        n++;
#if CONFIG_AV1_DECODER
        if (i % 2 != 0) num_nonref -= 1;
#endif
      }
    }
    number_temporal_layers_ = 3;
    number_spatial_layers_ = 3;
    multi_ref_ = 1;
    drop_frames_ = n * number_spatial_layers_;
    // SL0
    const int bitrate_sl0 = 1 * cfg_.rc_target_bitrate / 8;
    target_layer_bitrate_[0] = 50 * bitrate_sl0 / 100;
    target_layer_bitrate_[1] = 70 * bitrate_sl0 / 100;
    target_layer_bitrate_[2] = bitrate_sl0;
    // SL1
    const int bitrate_sl1 = 3 * cfg_.rc_target_bitrate / 8;
    target_layer_bitrate_[3] = 50 * bitrate_sl1 / 100;
    target_layer_bitrate_[4] = 70 * bitrate_sl1 / 100;
    target_layer_bitrate_[5] = bitrate_sl1;
    // SL2
    const int bitrate_sl2 = 4 * cfg_.rc_target_bitrate / 8;
    target_layer_bitrate_[6] = 50 * bitrate_sl2 / 100;
    target_layer_bitrate_[7] = 70 * bitrate_sl2 / 100;
    target_layer_bitrate_[8] = bitrate_sl2;
    ASSERT_NO_FATAL_FAILURE(RunLoop(&video));
    for (int i = 0; i < number_temporal_layers_ * number_spatial_layers_; i++) {
      ASSERT_GE(effective_datarate_tl[i], target_layer_bitrate_[i] * 0.60)
          << " The datarate for the file is lower than target by too much!";
      ASSERT_LE(effective_datarate_tl[i], target_layer_bitrate_[i] * 1.60)
          << " The datarate for the file is greater than target by too much!";
    }
#if CONFIG_AV1_DECODER
    // Test that no mismatches have been found.
    std::cout << "          Decoded frames: " << GetDecodedFrames() << "\n";
    std::cout << "          Mismatch frames: " << GetMismatchFrames() << "\n";
    EXPECT_EQ(300 * number_spatial_layers_ - GetDecodedFrames(), drop_frames_);
    EXPECT_EQ((int)GetMismatchFrames(), num_nonref);
#endif
  }

  virtual void BasicRateTargetingSVC3TL1SLMultiRefCompoundTest() {
    cfg_.rc_buf_initial_sz = 500;
    cfg_.rc_buf_optimal_sz = 500;
    cfg_.rc_buf_sz = 1000;
    cfg_.rc_dropframe_thresh = 0;
    cfg_.rc_min_quantizer = 0;
    cfg_.rc_max_quantizer = 63;
    cfg_.rc_end_usage = AOM_CBR;
    cfg_.g_lag_in_frames = 0;
    cfg_.g_error_resilient = 0;

    ::libaom_test::I420VideoSource video("niklas_640_480_30.yuv", 640, 480, 30,
                                         1, 0, 400);
    cfg_.g_w = 640;
    cfg_.g_h = 480;
    const int bitrate_array[2] = { 400, 800 };
    cfg_.rc_target_bitrate = bitrate_array[GET_PARAM(4)];
    ResetModel();
    multi_ref_ = 1;
    comp_pred_ = 1;
    number_temporal_layers_ = 3;
    number_spatial_layers_ = 1;
    target_layer_bitrate_[0] = 50 * cfg_.rc_target_bitrate / 100;
    target_layer_bitrate_[1] = 70 * cfg_.rc_target_bitrate / 100;
    target_layer_bitrate_[2] = cfg_.rc_target_bitrate;
    ASSERT_NO_FATAL_FAILURE(RunLoop(&video));
    for (int i = 0; i < number_temporal_layers_ * number_spatial_layers_; i++) {
      ASSERT_GE(effective_datarate_tl[i], target_layer_bitrate_[i] * 0.80)
          << " The datarate for the file is lower than target by too much!";
      ASSERT_LE(effective_datarate_tl[i], target_layer_bitrate_[i] * 1.60)
          << " The datarate for the file is greater than target by too much!";
    }
  }

  virtual void BasicRateTargetingSVC1TL3SLDynEnablTest() {
    cfg_.rc_buf_initial_sz = 500;
    cfg_.rc_buf_optimal_sz = 500;
    cfg_.rc_buf_sz = 1000;
    cfg_.rc_dropframe_thresh = 0;
    cfg_.rc_min_quantizer = 0;
    cfg_.rc_max_quantizer = 63;
    cfg_.rc_end_usage = AOM_CBR;
    cfg_.g_lag_in_frames = 0;
    cfg_.g_error_resilient = 0;

    ::libaom_test::I420VideoSource video("niklas_640_480_30.yuv", 640, 480, 30,
                                         1, 0, 400);
    const int bitrate_array[2] = { 500, 1000 };
    cfg_.rc_target_bitrate = bitrate_array[GET_PARAM(4)];
    ResetModel();
    number_temporal_layers_ = 1;
    number_spatial_layers_ = 3;
    target_layer_bitrate_[0] = 1 * cfg_.rc_target_bitrate / 8;
    target_layer_bitrate_[1] = 3 * cfg_.rc_target_bitrate / 8;
    target_layer_bitrate_[2] = 4 * cfg_.rc_target_bitrate / 8;
    dynamic_enable_disable_mode_ = 1;
    ASSERT_NO_FATAL_FAILURE(RunLoop(&video));
    // No need to check RC on top layer which is disabled part of the time.
    for (int i = 0; i < number_spatial_layers_ - 1; i++) {
      ASSERT_GE(effective_datarate_tl[i], target_layer_bitrate_[i] * 0.80)
          << " The datarate for the file is lower than target by too much!";
      ASSERT_LE(effective_datarate_tl[i], target_layer_bitrate_[i] * 1.38)
          << " The datarate for the file is greater than target by too much!";
    }
  }

  virtual void BasicRateTargetingSVC1TL3SLDynDisEnablTest() {
    cfg_.rc_buf_initial_sz = 500;
    cfg_.rc_buf_optimal_sz = 500;
    cfg_.rc_buf_sz = 1000;
    cfg_.rc_dropframe_thresh = 0;
    cfg_.rc_min_quantizer = 0;
    cfg_.rc_max_quantizer = 63;
    cfg_.rc_end_usage = AOM_CBR;
    cfg_.g_lag_in_frames = 0;
    cfg_.g_error_resilient = 0;

    ::libaom_test::I420VideoSource video("hantro_collage_w352h288.yuv", 352,
                                         288, 30, 1, 0, 300);
    const int bitrate_array[2] = { 500, 1000 };
    cfg_.rc_target_bitrate = bitrate_array[GET_PARAM(4)];
    ResetModel();
    number_temporal_layers_ = 1;
    number_spatial_layers_ = 3;
    target_layer_bitrate_[0] = 1 * cfg_.rc_target_bitrate / 8;
    target_layer_bitrate_[1] = 3 * cfg_.rc_target_bitrate / 8;
    target_layer_bitrate_[2] = 4 * cfg_.rc_target_bitrate / 8;
    dynamic_enable_disable_mode_ = 2;
    ASSERT_NO_FATAL_FAILURE(RunLoop(&video));
    // No need to check RC on top layer which is disabled part of the time.
    for (int i = 0; i < number_spatial_layers_ - 1; i++) {
      ASSERT_GE(effective_datarate_tl[i], target_layer_bitrate_[i] * 0.80)
          << " The datarate for the file is lower than target by too much!";
      ASSERT_LE(effective_datarate_tl[i], target_layer_bitrate_[i] * 1.38)
          << " The datarate for the file is greater than target by too much!";
    }
  }

  virtual void BasicRateTargetingRPS1TL1SLDropFramesTest() {
    cfg_.rc_buf_initial_sz = 500;
    cfg_.rc_buf_optimal_sz = 500;
    cfg_.rc_buf_sz = 1000;
    cfg_.rc_dropframe_thresh = 0;
    cfg_.rc_min_quantizer = 0;
    cfg_.rc_max_quantizer = 63;
    cfg_.rc_end_usage = AOM_CBR;
    cfg_.g_lag_in_frames = 0;

    ::libaom_test::I420VideoSource video("hantro_collage_w352h288.yuv", 352,
                                         288, 30, 1, 0, 300);
    const int bitrate_array[2] = { 100, 300 };
    cfg_.rc_target_bitrate = bitrate_array[GET_PARAM(4)];
    ResetModel();
    rps_mode_ = 1;
    rps_recovery_frame_ = 100;
    cfg_.g_error_resilient = 0;
    // Drop x frames before the recovery frames (where the reference
    // is switched to an older reference (golden or altref).
    // GOLDEN is 8 frames behind (for the rps pattern example) so we can't
    // drop more than 8 frames recovery frame, so choose x = 7.
    int n = 0;
    for (int i = rps_recovery_frame_ - 7; i < rps_recovery_frame_; i++) {
      drop_frames_list_[n] = i;
      n++;
    }
    drop_frames_ = n;
    number_spatial_layers_ = 1;
    number_temporal_layers_ = 1;
    target_layer_bitrate_[0] = cfg_.rc_target_bitrate;
    ASSERT_NO_FATAL_FAILURE(RunLoop(&video));
    for (int i = 0; i < number_temporal_layers_ * number_spatial_layers_; i++) {
      ASSERT_GE(effective_datarate_tl[i], target_layer_bitrate_[i] * 0.60)
          << " The datarate for the file is lower than target by too much!";
      ASSERT_LE(effective_datarate_tl[i], target_layer_bitrate_[i] * 1.60)
          << " The datarate for the file is greater than target by too much!";
    }
#if CONFIG_AV1_DECODER
    // Test that no mismatches have been found.
    std::cout << "          Decoded frames: " << GetDecodedFrames() << "\n";
    std::cout << "          Mismatch frames: " << GetMismatchFrames() << "\n";
    EXPECT_EQ(300 - GetDecodedFrames(), drop_frames_);
    EXPECT_EQ((int)GetMismatchFrames(), 0);
#endif
  }

  virtual void BasicRateTargetingSVC3TL3SLExternalResizePattern1Test() {
    cfg_.rc_buf_initial_sz = 500;
    cfg_.rc_buf_optimal_sz = 500;
    cfg_.rc_buf_sz = 1000;
    cfg_.rc_dropframe_thresh = 0;
    cfg_.rc_min_quantizer = 0;
    cfg_.rc_max_quantizer = 63;
    cfg_.rc_end_usage = AOM_CBR;
    cfg_.g_lag_in_frames = 0;
    cfg_.g_error_resilient = 0;
    const int bitrate_array[2] = { 600, 1200 };
    cfg_.rc_target_bitrate = bitrate_array[GET_PARAM(4)];
    cfg_.g_w = 1280;
    cfg_.g_h = 720;
    top_sl_width_ = 1280;
    top_sl_height_ = 720;
    ResizingVideoSource video(1);
    ResetModel();
    external_resize_dynamic_drop_layer_ = true;
    external_resize_pattern_ = 1;
    number_temporal_layers_ = 3;
    number_spatial_layers_ = 3;
    // SL0
    const int bitrate_sl0 = 1 * cfg_.rc_target_bitrate / 8;
    target_layer_bitrate_[0] = 50 * bitrate_sl0 / 100;
    target_layer_bitrate_[1] = 70 * bitrate_sl0 / 100;
    target_layer_bitrate_[2] = bitrate_sl0;
    // SL1
    const int bitrate_sl1 = 3 * cfg_.rc_target_bitrate / 8;
    target_layer_bitrate_[3] = 50 * bitrate_sl1 / 100;
    target_layer_bitrate_[4] = 70 * bitrate_sl1 / 100;
    target_layer_bitrate_[5] = bitrate_sl1;
    // SL2
    const int bitrate_sl2 = 4 * cfg_.rc_target_bitrate / 8;
    target_layer_bitrate_[6] = 50 * bitrate_sl2 / 100;
    target_layer_bitrate_[7] = 70 * bitrate_sl2 / 100;
    target_layer_bitrate_[8] = bitrate_sl2;
    ASSERT_NO_FATAL_FAILURE(RunLoop(&video));
  }

  virtual void BasicRateTargetingSVC3TL3SLExternalResizePattern2Test() {
    cfg_.rc_buf_initial_sz = 500;
    cfg_.rc_buf_optimal_sz = 500;
    cfg_.rc_buf_sz = 1000;
    cfg_.rc_dropframe_thresh = 0;
    cfg_.rc_min_quantizer = 0;
    cfg_.rc_max_quantizer = 63;
    cfg_.rc_end_usage = AOM_CBR;
    cfg_.g_lag_in_frames = 0;
    cfg_.g_error_resilient = 0;
    const int bitrate_array[2] = { 600, 1200 };
    cfg_.rc_target_bitrate = bitrate_array[GET_PARAM(4)];
    cfg_.g_w = 1280;
    cfg_.g_h = 720;
    top_sl_width_ = 1280;
    top_sl_height_ = 720;
    ResizingVideoSource video(2);
    ResetModel();
    external_resize_dynamic_drop_layer_ = true;
    external_resize_pattern_ = 2;
    number_temporal_layers_ = 3;
    number_spatial_layers_ = 3;
    // SL0
    const int bitrate_sl0 = 1 * cfg_.rc_target_bitrate / 8;
    target_layer_bitrate_[0] = 50 * bitrate_sl0 / 100;
    target_layer_bitrate_[1] = 70 * bitrate_sl0 / 100;
    target_layer_bitrate_[2] = bitrate_sl0;
    // SL1
    const int bitrate_sl1 = 3 * cfg_.rc_target_bitrate / 8;
    target_layer_bitrate_[3] = 50 * bitrate_sl1 / 100;
    target_layer_bitrate_[4] = 70 * bitrate_sl1 / 100;
    target_layer_bitrate_[5] = bitrate_sl1;
    // SL2
    const int bitrate_sl2 = 4 * cfg_.rc_target_bitrate / 8;
    target_layer_bitrate_[6] = 50 * bitrate_sl2 / 100;
    target_layer_bitrate_[7] = 70 * bitrate_sl2 / 100;
    target_layer_bitrate_[8] = bitrate_sl2;
    ASSERT_NO_FATAL_FAILURE(RunLoop(&video));
  }

  int layer_frame_cnt_;
  int superframe_cnt_;
  int number_temporal_layers_;
  int number_spatial_layers_;
  // Allow for up to 3 temporal layers.
  int target_layer_bitrate_[AOM_MAX_LAYERS];
  aom_svc_params_t svc_params_;
  aom_svc_ref_frame_config_t ref_frame_config_;
  aom_svc_ref_frame_comp_pred_t ref_frame_comp_pred_;
  aom_svc_layer_id_t layer_id_;
  double effective_datarate_tl[AOM_MAX_LAYERS];
  unsigned int drop_frames_;
  unsigned int drop_frames_list_[1000];
  unsigned int mismatch_nframes_;
  unsigned int decoded_nframes_;
  double mismatch_psnr_;
  int set_frame_level_er_;
  int multi_ref_;
  int use_fixed_mode_svc_;
  int comp_pred_;
  int dynamic_enable_disable_mode_;
  int intra_only_;
  int intra_only_single_layer_;
  unsigned int frame_to_start_decoding_;
  unsigned int layer_to_decode_;
  unsigned int frame_sync_;
  unsigned int current_video_frame_;
  int screen_mode_;
  int rps_mode_;
  int rps_recovery_frame_;
  int simulcast_mode_;
  bool use_last_as_scaled_;
  bool use_last_as_scaled_single_ref_;

  int user_define_frame_qp_;
  int frame_qp_;
  int total_frame_;
  bool set_speed_per_layer_;
  libaom_test::ACMRandom rnd_;
  bool external_resize_dynamic_drop_layer_;
  int bitrate_layer_[9];
  int external_resize_pattern_;
  int top_sl_width_;
  int top_sl_height_;
};

// Check basic rate targeting for CBR, for 3 temporal layers, 1 spatial.
TEST_P(DatarateTestSVC, BasicRateTargetingSVC3TL1SL) {
  BasicRateTargetingSVC3TL1SLTest();
}

TEST_P(DatarateTestSVC, SetFrameQpSVC3TL1SL) { SetFrameQpSVC3TL1SLTest(); }

TEST_P(DatarateTestSVC, SetFrameQpSVC3TL3SL) { SetFrameQpSVC3TL3SLTest(); }

// Check basic rate targeting for CBR, for 3 temporal layers, 1 spatial
// for screen mode.
TEST_P(DatarateTestSVC, BasicRateTargetingSVC3TL1SLScreen) {
  BasicRateTargetingSVC3TL1SLScreenTest();
}

// Check basic rate targeting for CBR, for 2 temporal layers, 1 spatial
// for screen mode, with frame dropper on at low bitrates. Use small
// values of rc_buf_initial/optimal/sz to trigger postencode frame drop.
TEST_P(DatarateTestSVC, BasicRateTargetingSVC2TL1SLScreenDropFrame) {
  BasicRateTargetingSVC2TL1SLScreenDropFrameTest();
}

// Check basic rate targeting for CBR, for 2 temporal layers, 1 spatial
// for screen mode, with frame dropper on at low bitrates. Use small
// values of rc_buf_initial/optimal/sz to trigger postencode frame drop.
// Use 1920x1080 clip.
TEST_P(DatarateTestSVC, BasicRateTargetingSVC2TL1SLScreenDropFrame1920x1080) {
  BasicRateTargetingSVC2TL1SLScreenDropFrame1920x1080Test();
}

// Check basic rate targeting for CBR, for 2 temporal layers, 1 spatial
// for screen mode, with frame dropper on at low bitrates. Use small
// values of rc_buf_initial/optimal/sz to trigger postencode frame drop.
// Use 1920x1080 clip. This test runs with 4 threads.
TEST_P(DatarateTestSVC,
       BasicRateTargetingSVC2TL1SLScreenDropFrame1920x10804Thread) {
  BasicRateTargetingSVC2TL1SLScreenDropFrame1920x10804ThreadTest();
}

// Check basic rate targeting for CBR, for 3 spatial layers, 1 temporal
// for screen mode.
TEST_P(DatarateTestSVC, BasicRateTargetingSVC1TL3SLScreen) {
  BasicRateTargetingSVC1TL3SLScreenTest();
}

// Check basic rate targeting for CBR, for 1 temporal layer, 1 spatial
// for screen mode, with source with many scene cuts and motion.
TEST_P(DatarateTestSVC, BasicRateTargetingSVC1TL1SLScreenScCutsMotion) {
  BasicRateTargetingSVC1TL1SLScreenScCutsMotionTest();
}

// Check basic rate targeting for CBR, for 3 temporal layers, 1 spatial,
// with dynamic resize on. Encode at very low bitrate and check that
// there is at least one resize (down) event.
TEST_P(DatarateTestSVC, BasicRateTargetingSVC3TL1SLResize) {
  BasicRateTargetingSVC3TL1SLResizeTest();
}

// Check basic rate targeting for CBR, for 2 spatial layers, 1 temporal.
TEST_P(DatarateTestSVC, BasicRateTargetingSVC1TL2SL) {
  BasicRateTargetingSVC1TL2SLTest();
}

// Check basic rate targeting for CBR, for 3 spatial layers, 3 temporal,
// with Intra-only frame inserted in the stream. Verify that we can start
// decoding the SL0 stream at the intra_only frame in mid-sequence.
TEST_P(DatarateTestSVC, BasicRateTargetingSVC3TL3SLIntraStartDecodeBaseMidSeq) {
  BasicRateTargetingSVC3TL3SLIntraStartDecodeBaseMidSeq();
}

// Check basic rate targeting for CBR, for 3spatial layers, 3 temporal,
// with Intra-only frame inserted in the stream. Verify that we can
// decode all frames and layers with no mismatch.
TEST_P(DatarateTestSVC, BasicRateTargetingSVC3TL3SLIntraMidSeqDecodeAll) {
  BasicRateTargetingSVC3TL3SLIntraMidSeqDecodeAll();
}

// Check simulcast mode for 3 spatial layers, 3 temporal,
// Key frame is inserted on base SLO in mid-stream, and verify that the
// top spatial layer (SL2) case be decoded, starting with an Intra-only frame.
// Verify that we can decode all frames for SL2 with no mismatch.
TEST_P(DatarateTestSVC, BasicRateTargetingSVC3TL3SLSimulcast) {
  BasicRateTargetingSVC3TL3SLSimulcast();
}

// Check basic rate targeting for CBR, for 2 spatial layers, 1 temporal,
// with Intra-only frame inserted in the stream.
TEST_P(DatarateTestSVC, BasicRateTargetingSVC1TL2SLIntraOnly) {
  BasicRateTargetingSVC1TL2SLIntraOnlyTest();
}

// Check basic rate targeting for CBR, for 1 spatial layers, 1 temporal,
// with Intra-only frame (frame with no references) inserted in the stream.
TEST_P(DatarateTestSVC, BasicRateTargetingSVC1TL1SLIntraOnly) {
  BasicRateTargetingSVC1TL1SLIntraOnlyTest();
}

// Check basic rate targeting for CBR, for 3 spatial layers, 1 temporal.
TEST_P(DatarateTestSVC, BasicRateTargetingSVC1TL3SL) {
  BasicRateTargetingSVC1TL3SLTest();
}

// Check basic rate targeting for CBR, for 3 spatial layers, 1 temporal.
// Force the spatial reference to be LAST, with a second temporal
// reference (GOLDEN).
TEST_P(DatarateTestSVC, BasicRateTargetingSVC1TL3SLLastIsScaled) {
  BasicRateTargetingSVC1TL3SLLastIsScaledTest();
}

// Check basic rate targeting for CBR, for 3 spatial layers, 1 temporal.
// Force the spatial reference to be LAST, and force only 1 reference.
TEST_P(DatarateTestSVC, BasicRateTargetingSVC1TL3SLastIsScaledSingleRef) {
  BasicRateTargetingSVC1TL3SLLastIsScaledSingleRefTest();
}

// Check basic rate targeting for CBR, for 3 spatial layers, 1 temporal,
// with additional temporal reference for top spatial layer.
TEST_P(DatarateTestSVC, BasicRateTargetingSVC1TL3SLMultiRef) {
  BasicRateTargetingSVC1TL3SLMultiRefTest();
}

// Check basic rate targeting for CBR, for 3 spatial, 3 temporal layers.
TEST_P(DatarateTestSVC, BasicRateTargetingSVC3TL3SL) {
  BasicRateTargetingSVC3TL3SLTest();
}

// Check basic rate targeting for CBR, for 3 spatial, 3 temporal layers.
TEST_P(DatarateTestSVC, BasicRateTargetingSVC3TL3SLHD) {
  BasicRateTargetingSVC3TL3SLHDTest();
}

// Check basic rate targeting for CBR, for 3 spatial, 3 temporal layers,
// for fixed mode SVC.
TEST_P(DatarateTestSVC, BasicRateTargetingFixedModeSVC3TL3SLHD) {
  BasicRateTargetingFixedModeSVC3TL3SLHDTest();
}

// Check basic rate targeting for CBR, for 3 spatial, 3 temporal layers,
// for 2 threads, 2 tile_columns, row-mt enabled, and different speed
// per layer.
TEST_P(DatarateTestSVC, BasicRateTargetingSVC3TL3SLMultiThreadSpeedPerLayer) {
  BasicRateTargetingSVC3TL3SLMultiThreadSpeedPerLayerTest();
}

// Check basic rate targeting for CBR, for 3 spatial, 3 temporal layers,
// for 2 threads, 2 tile_columns, row-mt enabled.
TEST_P(DatarateTestSVC, BasicRateTargetingSVC3TL3SLHDMultiThread2) {
  BasicRateTargetingSVC3TL3SLHDMultiThread2Test();
}

// Check basic rate targeting for CBR, for 1 spatial, 2 temporal layers,
// for 4 threads, 2 tile_columns, 2 tiles_rows, row-mt enabled.
TEST_P(DatarateTestSVC, BasicRateTargetingSVC2TL1SLHDMultiThread4) {
  BasicRateTargetingSVC2TL1SLHDMultiThread4Test();
}

// Check basic rate targeting for CBR, for 1 spatial, 2 temporal layers,
// for 4 threads, row-mt enabled, and auto_tiling enabled.
TEST_P(DatarateTestSVC, BasicRateTargetingSVC2TL1SLHDMultiThread4AutoTiles) {
  BasicRateTargetingSVC2TL1SLHDMultiThread4AutoTilesTest();
}
// Check basic rate targeting for CBR, for 3 spatial, 3 temporal layers,
// for 4 threads, 2 tile_columns, 2 tiles_rows, row-mt enabled.
TEST_P(DatarateTestSVC, BasicRateTargetingSVC3TL3SLHDMultiThread4) {
  BasicRateTargetingSVC3TL3SLHDMultiThread4Test();
}

// Check basic rate targeting for CBR, for 3 spatial, 3 temporal layers,
// with additional temporal reference for top spatial layer.
TEST_P(DatarateTestSVC, BasicRateTargetingSVC3TL3SLHDMultiRef) {
  BasicRateTargetingSVC3TL3SLHDMultiRefTest();
}

// Check basic rate targeting for CBR, for 3 spatial, 3 temporal layers,
// for auto key frame mode with short key frame period.
TEST_P(DatarateTestSVC, BasicRateTargetingSVC3TL3SLKf) {
  BasicRateTargetingSVC3TL3SLKfTest();
}

// Check basic rate targeting for CBR, for 3 spatial, 3 temporal layers,
// for 4:4:4 input.
#if defined(CONFIG_MAX_DECODE_PROFILE) && CONFIG_MAX_DECODE_PROFILE < 1
TEST_P(DatarateTestSVC, DISABLED_BasicRateTargeting444SVC3TL3SL) {
#else
TEST_P(DatarateTestSVC, BasicRateTargeting444SVC3TL3SL) {
#endif
  BasicRateTargeting444SVC3TL3SLTest();
}

// Check basic rate targeting for CBR, for 3 temporal layers, 1 spatial layer,
// with dropping of all enhancement layers (TL 1 and TL2). Check that the base
// layer (TL0) can still be decodeable (with no mismatch) with the
// error_resilient flag set to 0. This test used the pattern with multiple
// references (last, golden, and altref), updated on base layer.
TEST_P(DatarateTestSVC, BasicRateTargetingSVC3TL1SLMultiRefDropAllEnh) {
  BasicRateTargetingSVC3TL1SLMultiRefDropAllEnhTest();
}

// Check basic rate targeting for CBR, for 3 temporal layers, 1 spatial layer,
// with dropping of all enhancement layers (TL 1 and TL2). Check that the base
// layer (TL0) can still be decodeable (with no mismatch) with the
// error_resilient flag set to 0.
TEST_P(DatarateTestSVC, BasicRateTargetingSVC3TL1SLDropAllEnh) {
  BasicRateTargetingSVC3TL1SLDropAllEnhTest();
}

// Check basic rate targeting for CBR, for 3 temporal layers, 1 spatial layer,
// with dropping of the TL2 enhancement layer, which are non-reference
// (droppble) frames. For the base layer (TL0) and TL1 to still be decodeable
// (with no mismatch), the error_resilient_flag may be off (set to 0),
// since TL2 are non-reference frames.
TEST_P(DatarateTestSVC, BasicRateTargetingSVC3TL1SLDropTL2Enh) {
  BasicRateTargetingSVC3TL1SLDropTL2EnhTest();
}

// Check basic rate targeting for CBR, for 3 temporal layers, 1 spatial layer,
// with dropping of all enhancement layers (TL 1 and TL2). Test that the
// error_resilient flag can be set at frame level, with on/1 on
// enhancement layers and off/0 on base layer.
// This allows for successful decoding after dropping enhancement layer frames.
TEST_P(DatarateTestSVC, BasicRateTargetingSVC3TL1SLDropAllEnhFrameER) {
  BasicRateTargetingSVC3TL1SLDropAllEnhFrameERTest();
}

// Check basic rate targeting for CBR, for 3 temporal layers, 1 spatial layer,
// with dropping set of enhancement layers (TL 1 and TL2) in middle of sequence.
// Test that the error_resilient flag can be set at frame level, with on/1 on
// enhancement layers and off/0 on base layer.
// This allows for successful decoding after dropping a set enhancement layer
// frames in the sequence.
TEST_P(DatarateTestSVC, BasicRateTargetingSVC3TL1SLDropSetEnhFrameER) {
  BasicRateTargetingSVC3TL1SLDropSetEnhFrameERTest();
}

// Check basic rate targeting for CBR, for 2 temporal layers, 1 spatial layer,
// with dropping set of enhancement layers (TL 1) in middle of sequence.
// Test that the error_resilient flag can be 0/off for all frames.
// This allows for successful decoding after dropping a set enhancement layer
// frames in the sequence.
TEST_P(DatarateTestSVC, BasicRateTargetingSVC2TL1SLDropSetEnhER0) {
  BasicRateTargetingSVC2TL1SLDropSetEnhER0Test();
}

// Check basic rate targeting for CBR, for 3 temporal layers, 1 spatial layer,
// with dropping set of enhancement layers (TL 1 and TL2) in middle of sequence.
// Test that the error_resilient flag can be 0/off for all frames.
// This allows for successful decoding after dropping a set enhancement layer
// frames in the sequence.
TEST_P(DatarateTestSVC, BasicRateTargetingSVC3TL1SLDropSetEnhER0) {
  BasicRateTargetingSVC3TL1SLDropSetEnhER0Test();
}

// Check basic rate targeting for CBR, for 3 temporal layers, 3 spatial layers,
// with dropping set of enhancement layers (superframe TL 1 and TL2) in middle
// of sequence. Test that the error_resilient flag can be 0/off for all frames.
// This allows for successful decoding after dropping a set enhancement layer
// frames in the sequence.
TEST_P(DatarateTestSVC, BasicRateTargetingSVC3TL3SLDropSetEnhER0) {
  BasicRateTargetingSVC3TL3SLDropSetEnhER0Test();
}

// Check basic rate targeting for CBR, for 3 temporal layers, 1 spatial layer,
// with compound prediction on, for pattern with two additional refereces
// (golden and altref), both updated on base TLO frames.
TEST_P(DatarateTestSVC, BasicRateTargetingSVC3TL1SLMultiRefCompound) {
  BasicRateTargetingSVC3TL1SLMultiRefCompoundTest();
}

// Check basic rate targeting for CBR, for 3 spatial layers, 1 temporal,
// with the top spatial layer starting disabled (0 bitrate) and then
// dynamically enabled after x frames with nonzero bitrate.
TEST_P(DatarateTestSVC, BasicRateTargetingSVC1TL3SLDynEnabl) {
  BasicRateTargetingSVC1TL3SLDynEnablTest();
}

// Check basic rate targeting for CBR, for 3 spatial layers, 1 temporal,
// with the top spatial layer dynamically disabled snd enabled during the
// middle of the sequence.
TEST_P(DatarateTestSVC, BasicRateTargetingSVC1TL3SLDynDisEnabl) {
  BasicRateTargetingSVC1TL3SLDynDisEnablTest();
}

// Check basic rate targeting and encoder/decodermismatch, for RPS
// with 1 layer. A number of consecutive frames are lost midway in
// sequence, and encoder resorts to a longer term reference to recovery
// and continue decoding successfully.
TEST_P(DatarateTestSVC, BasicRateTargetingRPS1TL1SLDropFrames) {
  BasicRateTargetingRPS1TL1SLDropFramesTest();
}

// For 1 pass CBR SVC with 3 spatial and 3 temporal layers with external resize
// and denoiser enabled. The external resizer will resize down and back up,
// setting 0/nonzero bitrate on spatial enhancement layers to disable/enable
// layers. Resizing starts on first frame and the pattern is:
//  1/4 -> 1/2 -> 1 -> 1/4 -> 1/2.
TEST_P(DatarateTestSVC, BasicRateTargetingSVC3TL3SLExternalResizePattern1) {
  BasicRateTargetingSVC3TL3SLExternalResizePattern1Test();
}

// For 1 pass CBR SVC with 3 spatial and 3 temporal layers with external resize
// and denoiser enabled. The external resizer will resize down and back up,
// setting 0/nonzero bitrate on spatial enhancement layers to disable/enable
// layers. Resizing starts on first frame and the pattern is:
//  1/2 -> 1/4 -> 1 -> 1/2 -> 1/4.
TEST_P(DatarateTestSVC, BasicRateTargetingSVC3TL3SLExternalResizePattern2) {
  BasicRateTargetingSVC3TL3SLExternalResizePattern2Test();
}

TEST(SvcParams, BitrateOverflow) {
  uint8_t buf[6] = { 0 };
  aom_image_t img;
  aom_codec_ctx_t enc;
  aom_codec_enc_cfg_t cfg;

  EXPECT_EQ(&img, aom_img_wrap(&img, AOM_IMG_FMT_I420, 1, 1, 1, buf));

  aom_codec_iface_t *const iface = aom_codec_av1_cx();
  EXPECT_EQ(aom_codec_enc_config_default(iface, &cfg, AOM_USAGE_REALTIME),
            AOM_CODEC_OK);
  cfg.g_w = 1;
  cfg.g_h = 1;
  ASSERT_EQ(aom_codec_enc_init(&enc, iface, &cfg, 0), AOM_CODEC_OK);

  aom_svc_params_t svc_params = {};
  svc_params.framerate_factor[0] = 1;
  svc_params.framerate_factor[1] = 2;
  svc_params.number_spatial_layers = 1;
  svc_params.number_temporal_layers = 2;
  svc_params.layer_target_bitrate[0] = INT_MAX;
  svc_params.layer_target_bitrate[1] = INT_MAX;
  EXPECT_EQ(aom_codec_control(&enc, AV1E_SET_SVC_PARAMS, &svc_params),
            AOM_CODEC_OK);
  EXPECT_EQ(
      aom_codec_encode(&enc, &img, /*pts=*/0, /*duration=*/1, /*flags=*/0),
      AOM_CODEC_OK);
  EXPECT_EQ(aom_codec_encode(&enc, /*img=*/nullptr, /*pts=*/0, /*duration=*/0,
                             /*flags=*/0),
            AOM_CODEC_OK);
  EXPECT_EQ(aom_codec_destroy(&enc), AOM_CODEC_OK);
}

AV1_INSTANTIATE_TEST_SUITE(DatarateTestSVC,
                           ::testing::Values(::libaom_test::kRealTime),
                           ::testing::Range(7, 12), ::testing::Values(0, 3),
                           ::testing::Values(0, 1));

}  // namespace
}  // namespace datarate_test
