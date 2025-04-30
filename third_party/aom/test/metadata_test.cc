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

#include "gtest/gtest.h"

#include <cstring>
#include <string>

#include "config/aom_config.h"

#include "aom/aom_codec.h"
#include "aom/aom_image.h"
#include "aom/aomcx.h"
#include "aom/internal/aom_image_internal.h"
#include "aom_scale/yv12config.h"
#include "test/codec_factory.h"
#include "test/encode_test_driver.h"
#include "test/i420_video_source.h"
#include "test/util.h"
#include "test/video_source.h"

namespace {
const size_t kMetadataPayloadSizeT35 = 24;
// 0xB5 stands for the itut t35 metadata country code for the Unites States
const uint8_t kMetadataPayloadT35[kMetadataPayloadSizeT35] = {
  0xB5, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0A, 0x0B,
  0x0C, 0x0D, 0x0E, 0x0F, 0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17
};

const size_t kMetadataPayloadSizeT35Two = 10;
// 0xB5 stands for the itut t35 metadata country code for the Unites States
const uint8_t kMetadataPayloadT35Two[kMetadataPayloadSizeT35] = {
  0xB5, 0x01, 0x02, 0x42, 0xff, 0xff, 0x00, 0x07, 0x08, 0x09
};

const size_t kMetadataPayloadSizeMdcv = 24;
// Arbitrary content.
const uint8_t kMetadataPayloadMdcv[kMetadataPayloadSizeMdcv] = {
  0x99, 0x42, 0x42, 0x42, 0x42, 0x42, 0x42, 0x42, 0x42, 0x42, 0x42, 0x42,
  0x42, 0x42, 0x42, 0x42, 0x42, 0x42, 0x42, 0x42, 0x42, 0x42, 0x42, 0x99
};

const size_t kMetadataPayloadSizeCll = 4;
const uint8_t kMetadataPayloadCll[kMetadataPayloadSizeCll] = { 0xB5, 0x01, 0x02,
                                                               0x03 };

const size_t kMetadataObuSizeT35 = 28;
const uint8_t kMetadataObuT35[kMetadataObuSizeT35] = {
  0x2A, 0x1A, 0x04, 0xB5, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06,
  0x07, 0x08, 0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F, 0x10,
  0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17, 0x80
};
const size_t kMetadataObuSizeMdcv = 28;
const uint8_t kMetadataObuMdcv[kMetadataObuSizeMdcv] = {
  0x2A, 0x1A, 0x02, 0x99, 0x42, 0x42, 0x42, 0x42, 0x42, 0x42,
  0x42, 0x42, 0x42, 0x42, 0x42, 0x42, 0x42, 0x42, 0x42, 0x42,
  0x42, 0x42, 0x42, 0x42, 0x42, 0x42, 0x99, 0x80
};
const size_t kMetadataObuSizeCll = 8;
const uint8_t kMetadataObuCll[kMetadataObuSizeCll] = { 0x2A, 0x06, 0x01, 0xB5,
                                                       0x01, 0x02, 0x03, 0x80 };

#if !CONFIG_REALTIME_ONLY
class MetadataEncodeTest
    : public ::libaom_test::CodecTestWithParam<libaom_test::TestMode>,
      public ::libaom_test::EncoderTest {
 protected:
  MetadataEncodeTest() : EncoderTest(GET_PARAM(0)) {}

  ~MetadataEncodeTest() override = default;

  void SetUp() override { InitializeConfig(GET_PARAM(1)); }

  void PreEncodeFrameHook(::libaom_test::VideoSource *video,
                          ::libaom_test::Encoder *encoder) override {
    if (video->frame() == 0) {
      encoder->Control(AOME_SET_CPUUSED, 6);  // Speed up the test.
    }
    aom_image_t *current_frame = video->img();
    if (!current_frame) {
      return;
    }
    if (current_frame->metadata) aom_img_remove_metadata(current_frame);
    // invalid: size is 0
    ASSERT_EQ(aom_img_add_metadata(current_frame, OBU_METADATA_TYPE_ITUT_T35,
                                   kMetadataPayloadT35, 0, AOM_MIF_ANY_FRAME),
              -1);
    // invalid: data is nullptr
    ASSERT_EQ(
        aom_img_add_metadata(current_frame, OBU_METADATA_TYPE_ITUT_T35, nullptr,
                             kMetadataPayloadSizeT35, AOM_MIF_ANY_FRAME),
        -1);
    // invalid: size is 0 and data is nullptr
    ASSERT_EQ(aom_img_add_metadata(current_frame, OBU_METADATA_TYPE_ITUT_T35,
                                   nullptr, 0, AOM_MIF_ANY_FRAME),
              -1);

    ASSERT_EQ(aom_img_add_metadata(current_frame, OBU_METADATA_TYPE_ITUT_T35,
                                   kMetadataPayloadT35, kMetadataPayloadSizeT35,
                                   AOM_MIF_ANY_FRAME),
              0);

    ASSERT_EQ(
        aom_img_add_metadata(current_frame, OBU_METADATA_TYPE_ITUT_T35,
                             kMetadataPayloadT35Two, kMetadataPayloadSizeT35Two,
                             AOM_MIF_ANY_FRAME_LAYER_SPECIFIC),
        0);

    ASSERT_EQ(aom_img_add_metadata(current_frame, OBU_METADATA_TYPE_HDR_MDCV,
                                   kMetadataPayloadMdcv,
                                   kMetadataPayloadSizeMdcv, AOM_MIF_KEY_FRAME),
              0);

    ASSERT_EQ(aom_img_add_metadata(current_frame, OBU_METADATA_TYPE_HDR_CLL,
                                   kMetadataPayloadCll, kMetadataPayloadSizeCll,
                                   AOM_MIF_KEY_FRAME),
              0);
  }

  void FramePktHook(const aom_codec_cx_pkt_t *pkt) override {
    if (pkt->kind == AOM_CODEC_CX_FRAME_PKT) {
      const bool is_key_frame = (pkt->data.frame.flags & AOM_FRAME_IS_KEY) != 0;

      const std::string bitstream(
          static_cast<const char *>(pkt->data.frame.buf), pkt->data.frame.sz);
      // Look for valid metadatas in bitstream.
      const bool itut_t35_metadata_found =
          bitstream.find(reinterpret_cast<const char *>(kMetadataObuT35), 0,
                         kMetadataObuSizeT35) != std::string::npos;
      const bool hdr_mdcv_metadata_found =
          bitstream.find(reinterpret_cast<const char *>(kMetadataObuMdcv), 0,
                         kMetadataObuSizeMdcv) != std::string::npos;
      const bool hdr_cll_metadata_found =
          bitstream.find(reinterpret_cast<const char *>(kMetadataObuCll), 0,
                         kMetadataObuSizeCll) != std::string::npos;

      EXPECT_TRUE(itut_t35_metadata_found);
      EXPECT_EQ(hdr_mdcv_metadata_found, is_key_frame);
      EXPECT_EQ(hdr_cll_metadata_found, is_key_frame);
    }
  }

  void DecompressedFrameHook(const aom_image_t &img,
                             aom_codec_pts_t /*pts*/) override {
    const bool is_key_frame =
        (num_decompressed_frames_ % cfg_.kf_max_dist) == 0;
    ++num_decompressed_frames_;

    ASSERT_NE(img.metadata, nullptr);

    ASSERT_EQ(img.metadata->sz, is_key_frame ? 4 : 2);

    aom_metadata_t *metadata = img.metadata->metadata_array[0];
    ASSERT_EQ(metadata->type, OBU_METADATA_TYPE_ITUT_T35);
    ASSERT_EQ(metadata->insert_flag, AOM_MIF_ANY_FRAME);
    ASSERT_EQ(metadata->sz, kMetadataPayloadSizeT35);
    EXPECT_EQ(
        memcmp(kMetadataPayloadT35, metadata->payload, kMetadataPayloadSizeT35),
        0);

    metadata = img.metadata->metadata_array[1];
    ASSERT_EQ(metadata->type, OBU_METADATA_TYPE_ITUT_T35);
    // AOM_MIF_ANY_FRAME and not AOM_MIF_ANY_FRAME_LAYER_SPECIFIC because the
    // stream does not contain layers.
    ASSERT_EQ(metadata->insert_flag, AOM_MIF_ANY_FRAME);
    ASSERT_EQ(metadata->sz, kMetadataPayloadSizeT35Two);
    EXPECT_EQ(memcmp(kMetadataPayloadT35Two, metadata->payload,
                     kMetadataPayloadSizeT35Two),
              0);

    if (is_key_frame) {
      metadata = img.metadata->metadata_array[2];
      ASSERT_EQ(metadata->type, OBU_METADATA_TYPE_HDR_MDCV);
      ASSERT_EQ(metadata->insert_flag, AOM_MIF_ANY_FRAME);
      ASSERT_EQ(metadata->sz, kMetadataPayloadSizeMdcv);
      EXPECT_EQ(memcmp(kMetadataPayloadMdcv, metadata->payload,
                       kMetadataPayloadSizeMdcv),
                0);

      metadata = img.metadata->metadata_array[3];
      ASSERT_EQ(metadata->type, OBU_METADATA_TYPE_HDR_CLL);
      ASSERT_EQ(metadata->insert_flag, AOM_MIF_ANY_FRAME);
      ASSERT_EQ(metadata->sz, kMetadataPayloadSizeCll);
      EXPECT_EQ(memcmp(kMetadataPayloadCll, metadata->payload,
                       kMetadataPayloadSizeCll),
                0);
    }
  }

 private:
  int num_decompressed_frames_ = 0;
};

TEST_P(MetadataEncodeTest, TestMetadataEncoding) {
  ::libaom_test::I420VideoSource video("hantro_collage_w352h288.yuv", 352, 288,
                                       30, 1, 0, 10);
  init_flags_ = AOM_CODEC_USE_PSNR;

  cfg_.g_w = 352;
  cfg_.g_h = 288;

  cfg_.rc_buf_initial_sz = 500;
  cfg_.rc_buf_optimal_sz = 600;
  cfg_.rc_buf_sz = 1000;
  cfg_.rc_min_quantizer = 2;
  cfg_.rc_max_quantizer = 56;
  cfg_.rc_undershoot_pct = 50;
  cfg_.rc_overshoot_pct = 50;
  cfg_.kf_mode = AOM_KF_AUTO;
  cfg_.g_lag_in_frames = 1;
  cfg_.kf_min_dist = cfg_.kf_max_dist = 5;
  // Run at low bitrate.
  cfg_.rc_target_bitrate = 40;

  ASSERT_NO_FATAL_FAILURE(RunLoop(&video));
}

AV1_INSTANTIATE_TEST_SUITE(MetadataEncodeTest,
                           ::testing::Values(::libaom_test::kOnePassGood));
#endif  // !CONFIG_REALTIME_ONLY

class MetadataMultilayerEncodeTest
    : public ::libaom_test::CodecTestWithParam<libaom_test::TestMode>,
      public ::libaom_test::EncoderTest {
 protected:
  MetadataMultilayerEncodeTest() : EncoderTest(GET_PARAM(0)) {}

  ~MetadataMultilayerEncodeTest() override = default;

  static const int kNumSpatialLayers = 3;

  void SetUp() override { InitializeConfig(GET_PARAM(1)); }

  int GetNumSpatialLayers() override { return kNumSpatialLayers; }

  void PreEncodeFrameHook(::libaom_test::VideoSource *video,
                          ::libaom_test::Encoder *encoder) override {
    aom_image_t *current_frame = video->img();
    if (!current_frame) {
      return;
    }

    // One-time initialization only done on the first frame.
    if (num_encoded_frames_ == 0) {
      encoder->Control(AOME_SET_CPUUSED, 6);  // Speed up the test.
      aom_svc_params_t svc_params = GetSvcParams();
      encoder->Control(AV1E_SET_SVC_PARAMS, &svc_params);
    }

    const int spatial_layer_id = num_encoded_frames_ % 3;
    aom_svc_layer_id_t layer_id = { spatial_layer_id, 0 };
    encoder->Control(AV1E_SET_SVC_LAYER_ID, &layer_id);

    if (current_frame->metadata) aom_img_remove_metadata(current_frame);

    ASSERT_EQ(aom_img_add_metadata(current_frame, OBU_METADATA_TYPE_ITUT_T35,
                                   kMetadataPayloadT35, kMetadataPayloadSizeT35,
                                   AOM_MIF_ANY_FRAME),
              0);

    ASSERT_EQ(
        aom_img_add_metadata(current_frame, OBU_METADATA_TYPE_ITUT_T35,
                             kMetadataPayloadT35Two, kMetadataPayloadSizeT35Two,
                             AOM_MIF_ANY_FRAME_LAYER_SPECIFIC),
        0);

    ASSERT_EQ(aom_img_add_metadata(current_frame, OBU_METADATA_TYPE_HDR_MDCV,
                                   kMetadataPayloadMdcv,
                                   kMetadataPayloadSizeMdcv, AOM_MIF_KEY_FRAME),
              0);

    ASSERT_EQ(aom_img_add_metadata(current_frame, OBU_METADATA_TYPE_HDR_CLL,
                                   kMetadataPayloadCll, kMetadataPayloadSizeCll,
                                   AOM_MIF_KEY_FRAME),
              0);

    num_encoded_frames_++;
  }

  void FramePktHook(const aom_codec_cx_pkt_t *pkt) override {
    if (pkt->kind == AOM_CODEC_CX_FRAME_PKT) {
      const bool is_key_frame = (pkt->data.frame.flags & AOM_FRAME_IS_KEY) != 0;

      const std::string bitstream(
          static_cast<const char *>(pkt->data.frame.buf), pkt->data.frame.sz);

      // Look for valid metadatas in bitstream.
      const bool itut_t35_metadata_found =
          bitstream.find(reinterpret_cast<const char *>(kMetadataObuT35), 0,
                         kMetadataObuSizeT35) != std::string::npos;
      const bool hdr_mdcv_metadata_found =
          bitstream.find(reinterpret_cast<const char *>(kMetadataObuMdcv), 0,
                         kMetadataObuSizeMdcv) != std::string::npos;
      const bool hdr_cll_metadata_found =
          bitstream.find(reinterpret_cast<const char *>(kMetadataObuCll), 0,
                         kMetadataObuSizeCll) != std::string::npos;

      EXPECT_TRUE(itut_t35_metadata_found);
      EXPECT_EQ(hdr_mdcv_metadata_found, is_key_frame);
      EXPECT_EQ(hdr_cll_metadata_found, is_key_frame);
    }
  }

  void DecompressedFrameHook(const aom_image_t &img,
                             aom_codec_pts_t /*pts*/) override {
    const bool is_key_frame = (num_decompressed_frames_ == 0);

    ++num_decompressed_frames_;

    ASSERT_NE(img.metadata, nullptr);

    ASSERT_EQ(img.metadata->sz, is_key_frame ? 4 : 2);

    aom_metadata_t *metadata = img.metadata->metadata_array[0];
    ASSERT_EQ(metadata->type, OBU_METADATA_TYPE_ITUT_T35);
    ASSERT_EQ(metadata->insert_flag, AOM_MIF_ANY_FRAME);
    ASSERT_EQ(metadata->sz, kMetadataPayloadSizeT35);
    EXPECT_EQ(
        memcmp(kMetadataPayloadT35, metadata->payload, kMetadataPayloadSizeT35),
        0);

    metadata = img.metadata->metadata_array[1];
    ASSERT_EQ(metadata->type, OBU_METADATA_TYPE_ITUT_T35);
    ASSERT_EQ(metadata->insert_flag, AOM_MIF_ANY_FRAME_LAYER_SPECIFIC);
    ASSERT_EQ(metadata->sz, kMetadataPayloadSizeT35Two);
    EXPECT_EQ(memcmp(kMetadataPayloadT35Two, metadata->payload,
                     kMetadataPayloadSizeT35Two),
              0);

    if (is_key_frame) {
      metadata = img.metadata->metadata_array[2];
      ASSERT_EQ(metadata->type, OBU_METADATA_TYPE_HDR_MDCV);
      ASSERT_EQ(metadata->insert_flag, AOM_MIF_ANY_FRAME);
      ASSERT_EQ(metadata->sz, kMetadataPayloadSizeMdcv);
      EXPECT_EQ(memcmp(kMetadataPayloadMdcv, metadata->payload,
                       kMetadataPayloadSizeMdcv),
                0);

      metadata = img.metadata->metadata_array[3];
      ASSERT_EQ(metadata->type, OBU_METADATA_TYPE_HDR_CLL);
      ASSERT_EQ(metadata->insert_flag, AOM_MIF_ANY_FRAME);
      ASSERT_EQ(metadata->sz, kMetadataPayloadSizeCll);
      EXPECT_EQ(memcmp(kMetadataPayloadCll, metadata->payload,
                       kMetadataPayloadSizeCll),
                0);
    }
  }

 private:
  aom_svc_params_t GetSvcParams() {
    aom_svc_params_t svc_params = {};
    svc_params.number_spatial_layers = kNumSpatialLayers;
    svc_params.number_temporal_layers = 1;
    for (int i = 0; i < kNumSpatialLayers; ++i) {
      svc_params.max_quantizers[i] = 60;
      svc_params.min_quantizers[i] = 2;
    }

    svc_params.framerate_factor[0] = 1;

    svc_params.layer_target_bitrate[0] = 30 * cfg_.rc_target_bitrate / 100;
    svc_params.layer_target_bitrate[1] = 60 * cfg_.rc_target_bitrate / 100;
    svc_params.layer_target_bitrate[2] = cfg_.rc_target_bitrate;

    svc_params.scaling_factor_num[0] = 1;
    svc_params.scaling_factor_den[0] = 4;
    svc_params.scaling_factor_num[1] = 1;
    svc_params.scaling_factor_den[1] = 2;
    svc_params.scaling_factor_num[2] = 1;
    svc_params.scaling_factor_den[2] = 1;

    return svc_params;
  }

  int num_encoded_frames_ = 0;
  int num_decompressed_frames_ = 0;
};

}  // namespace

TEST_P(MetadataMultilayerEncodeTest, Test) {
  cfg_.rc_buf_initial_sz = 500;
  cfg_.rc_buf_optimal_sz = 500;
  cfg_.rc_buf_sz = 1000;
  cfg_.g_lag_in_frames = 0;
  cfg_.g_error_resilient = 0;
  cfg_.rc_target_bitrate = 1200;

  ::libaom_test::I420VideoSource video("hantro_collage_w352h288.yuv", 352, 288,
                                       30, 1, 0, /*limit=*/10);
  ASSERT_NO_FATAL_FAILURE(RunLoop(&video));
}

AV1_INSTANTIATE_TEST_SUITE(MetadataMultilayerEncodeTest,
                           ::testing::Values(::libaom_test::kRealTime));

TEST(MetadataTest, MetadataAllocation) {
  aom_metadata_t *metadata =
      aom_img_metadata_alloc(OBU_METADATA_TYPE_ITUT_T35, kMetadataPayloadT35,
                             kMetadataPayloadSizeT35, AOM_MIF_ANY_FRAME);
  ASSERT_NE(metadata, nullptr);
  aom_img_metadata_free(metadata);
}

TEST(MetadataTest, MetadataArrayAllocation) {
  aom_metadata_array_t *metadata_array = aom_img_metadata_array_alloc(2);
  ASSERT_NE(metadata_array, nullptr);

  metadata_array->metadata_array[0] =
      aom_img_metadata_alloc(OBU_METADATA_TYPE_ITUT_T35, kMetadataPayloadT35,
                             kMetadataPayloadSizeT35, AOM_MIF_ANY_FRAME);
  metadata_array->metadata_array[1] =
      aom_img_metadata_alloc(OBU_METADATA_TYPE_ITUT_T35, kMetadataPayloadT35,
                             kMetadataPayloadSizeT35, AOM_MIF_ANY_FRAME);

  aom_img_metadata_array_free(metadata_array);
}

TEST(MetadataTest, AddMetadataToImage) {
  aom_image_t image;
  image.metadata = nullptr;

  ASSERT_EQ(aom_img_add_metadata(&image, OBU_METADATA_TYPE_ITUT_T35,
                                 kMetadataPayloadT35, kMetadataPayloadSizeT35,
                                 AOM_MIF_ANY_FRAME),
            0);
  aom_img_metadata_array_free(image.metadata);
  EXPECT_EQ(aom_img_add_metadata(nullptr, OBU_METADATA_TYPE_ITUT_T35,
                                 kMetadataPayloadT35, kMetadataPayloadSizeT35,
                                 AOM_MIF_ANY_FRAME),
            -1);
}

TEST(MetadataTest, AddLayerSpecificMetadataToImage) {
  aom_image_t image;
  image.metadata = nullptr;

  ASSERT_EQ(
      aom_img_add_metadata(
          &image, OBU_METADATA_TYPE_ITUT_T35, kMetadataPayloadT35,
          kMetadataPayloadSizeT35,
          (aom_metadata_insert_flags_t)(AOM_MIF_ANY_FRAME_LAYER_SPECIFIC)),
      0);
  aom_img_metadata_array_free(image.metadata);
}

TEST(MetadataTest, AddLayerSpecificMetadataToImageNotAllowed) {
  aom_image_t image;
  image.metadata = nullptr;

  // OBU_METADATA_TYPE_SCALABILITY cannot be layer specific.
  ASSERT_EQ(
      aom_img_add_metadata(
          &image, OBU_METADATA_TYPE_SCALABILITY, kMetadataPayloadT35,
          kMetadataPayloadSizeT35,
          (aom_metadata_insert_flags_t)(AOM_MIF_ANY_FRAME_LAYER_SPECIFIC)),
      -1);
  aom_img_metadata_array_free(image.metadata);
}

TEST(MetadataTest, RemoveMetadataFromImage) {
  aom_image_t image;
  image.metadata = nullptr;

  ASSERT_EQ(aom_img_add_metadata(&image, OBU_METADATA_TYPE_ITUT_T35,
                                 kMetadataPayloadT35, kMetadataPayloadSizeT35,
                                 AOM_MIF_ANY_FRAME),
            0);
  aom_img_remove_metadata(&image);
  aom_img_remove_metadata(nullptr);
}

TEST(MetadataTest, CopyMetadataToFrameBuffer) {
  YV12_BUFFER_CONFIG yvBuf;
  yvBuf.metadata = nullptr;

  aom_metadata_array_t *metadata_array = aom_img_metadata_array_alloc(1);
  ASSERT_NE(metadata_array, nullptr);

  metadata_array->metadata_array[0] =
      aom_img_metadata_alloc(OBU_METADATA_TYPE_ITUT_T35, kMetadataPayloadT35,
                             kMetadataPayloadSizeT35, AOM_MIF_ANY_FRAME);

  // Metadata_array
  int status = aom_copy_metadata_to_frame_buffer(&yvBuf, metadata_array);
  EXPECT_EQ(status, 0);
  status = aom_copy_metadata_to_frame_buffer(nullptr, metadata_array);
  EXPECT_EQ(status, -1);
  aom_img_metadata_array_free(metadata_array);

  // Metadata_array_2
  aom_metadata_array_t *metadata_array_2 = aom_img_metadata_array_alloc(0);
  ASSERT_NE(metadata_array_2, nullptr);
  status = aom_copy_metadata_to_frame_buffer(&yvBuf, metadata_array_2);
  EXPECT_EQ(status, -1);
  aom_img_metadata_array_free(metadata_array_2);

  // YV12_BUFFER_CONFIG
  status = aom_copy_metadata_to_frame_buffer(&yvBuf, nullptr);
  EXPECT_EQ(status, -1);
  aom_remove_metadata_from_frame_buffer(&yvBuf);
  aom_remove_metadata_from_frame_buffer(nullptr);
}

TEST(MetadataTest, GetMetadataFromImage) {
  aom_image_t image;
  image.metadata = nullptr;

  ASSERT_EQ(aom_img_add_metadata(&image, OBU_METADATA_TYPE_ITUT_T35,
                                 kMetadataPayloadT35, kMetadataPayloadSizeT35,
                                 AOM_MIF_ANY_FRAME),
            0);
  ASSERT_EQ(aom_img_add_metadata(&image, OBU_METADATA_TYPE_ITUT_T35,
                                 kMetadataPayloadT35, kMetadataPayloadSizeT35,
                                 AOM_MIF_ANY_FRAME_LAYER_SPECIFIC),
            0);

  EXPECT_EQ(aom_img_get_metadata(nullptr, 0), nullptr);
  EXPECT_EQ(aom_img_get_metadata(&image, 2u), nullptr);
  EXPECT_EQ(aom_img_get_metadata(&image, 10u), nullptr);

  const aom_metadata_t *metadata = aom_img_get_metadata(&image, 0);
  ASSERT_NE(metadata, nullptr);
  ASSERT_EQ(metadata->sz, kMetadataPayloadSizeT35);
  EXPECT_EQ(
      memcmp(kMetadataPayloadT35, metadata->payload, kMetadataPayloadSizeT35),
      0);
  EXPECT_EQ(metadata->insert_flag, AOM_MIF_ANY_FRAME);

  metadata = aom_img_get_metadata(&image, 1);
  ASSERT_NE(metadata, nullptr);
  ASSERT_EQ(metadata->sz, kMetadataPayloadSizeT35);
  EXPECT_EQ(
      memcmp(kMetadataPayloadT35, metadata->payload, kMetadataPayloadSizeT35),
      0);
  EXPECT_EQ(metadata->insert_flag, AOM_MIF_ANY_FRAME_LAYER_SPECIFIC);

  aom_img_metadata_array_free(image.metadata);
}

TEST(MetadataTest, ReadMetadatasFromImage) {
  aom_image_t image;
  image.metadata = nullptr;

  uint32_t types[3];
  types[0] = OBU_METADATA_TYPE_ITUT_T35;
  types[1] = OBU_METADATA_TYPE_HDR_CLL;
  types[2] = OBU_METADATA_TYPE_HDR_MDCV;

  ASSERT_EQ(aom_img_add_metadata(&image, types[0], kMetadataPayloadT35,
                                 kMetadataPayloadSizeT35, AOM_MIF_ANY_FRAME),
            0);
  ASSERT_EQ(aom_img_add_metadata(&image, types[1], kMetadataPayloadT35,
                                 kMetadataPayloadSizeT35, AOM_MIF_KEY_FRAME),
            0);
  ASSERT_EQ(aom_img_add_metadata(&image, types[2], kMetadataPayloadT35,
                                 kMetadataPayloadSizeT35, AOM_MIF_KEY_FRAME),
            0);

  size_t number_metadata = aom_img_num_metadata(&image);
  ASSERT_EQ(number_metadata, 3u);
  for (size_t i = 0; i < number_metadata; ++i) {
    const aom_metadata_t *metadata = aom_img_get_metadata(&image, i);
    ASSERT_NE(metadata, nullptr);
    ASSERT_EQ(metadata->type, types[i]);
    ASSERT_EQ(metadata->sz, kMetadataPayloadSizeT35);
    EXPECT_EQ(
        memcmp(kMetadataPayloadT35, metadata->payload, kMetadataPayloadSizeT35),
        0);
  }
  aom_img_metadata_array_free(image.metadata);
}
