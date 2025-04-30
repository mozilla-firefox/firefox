/*
 * Copyright (c) 2017, Alliance for Open Media. All rights reserved.
 *
 * This source code is subject to the terms of the BSD 2 Clause License and
 * the Alliance for Open Media Patent License 1.0. If the BSD 2 Clause License
 * was not distributed with this source code in the LICENSE file, you can
 * obtain it at www.aomedia.org/license/software. If the Alliance for Open
 * Media Patent License 1.0 was not distributed with this source code in the
 * PATENTS file, you can obtain it at www.aomedia.org/license/patent.
 */

#include <assert.h>
#include <stdbool.h>

#include "config/aom_config.h"
#include "config/aom_scale_rtcd.h"

#include "aom/aom_codec.h"
#include "aom_dsp/bitreader_buffer.h"
#include "aom_ports/mem_ops.h"

#include "av1/common/common.h"
#include "av1/common/obu_util.h"
#include "av1/common/timing.h"
#include "av1/decoder/decoder.h"
#include "av1/decoder/decodeframe.h"
#include "av1/decoder/obu.h"

aom_codec_err_t aom_get_num_layers_from_operating_point_idc(
    int operating_point_idc, unsigned int *number_spatial_layers,
    unsigned int *number_temporal_layers) {
  // derive number of spatial/temporal layers from operating_point_idc

  if (!number_spatial_layers || !number_temporal_layers)
    return AOM_CODEC_INVALID_PARAM;

  if (operating_point_idc == 0) {
    *number_temporal_layers = 1;
    *number_spatial_layers = 1;
  } else {
    *number_spatial_layers = 0;
    *number_temporal_layers = 0;
    for (int j = 0; j < MAX_NUM_SPATIAL_LAYERS; j++) {
      *number_spatial_layers +=
          (operating_point_idc >> (j + MAX_NUM_TEMPORAL_LAYERS)) & 0x1;
    }
    for (int j = 0; j < MAX_NUM_TEMPORAL_LAYERS; j++) {
      *number_temporal_layers += (operating_point_idc >> j) & 0x1;
    }
  }

  return AOM_CODEC_OK;
}

static int is_obu_in_current_operating_point(AV1Decoder *pbi,
                                             const ObuHeader *obu_header) {
  if (!pbi->current_operating_point || !obu_header->has_extension) {
    return 1;
  }

  if ((pbi->current_operating_point >> obu_header->temporal_layer_id) & 0x1 &&
      (pbi->current_operating_point >> (obu_header->spatial_layer_id + 8)) &
          0x1) {
    return 1;
  }
  return 0;
}

static int byte_alignment(AV1_COMMON *const cm,
                          struct aom_read_bit_buffer *const rb) {
  while (rb->bit_offset & 7) {
    if (aom_rb_read_bit(rb)) {
      cm->error->error_code = AOM_CODEC_CORRUPT_FRAME;
      return -1;
    }
  }
  return 0;
}

static uint32_t read_temporal_delimiter_obu(void) { return 0; }

// Returns a boolean that indicates success.
static int read_bitstream_level(AV1_LEVEL *seq_level_idx,
                                struct aom_read_bit_buffer *rb) {
  *seq_level_idx = aom_rb_read_literal(rb, LEVEL_BITS);
  if (!is_valid_seq_level_idx(*seq_level_idx)) return 0;
  return 1;
}

// Returns whether two sequence headers are consistent with each other.
// Note that the 'op_params' field is not compared per Section 7.5 in the spec:
//   Within a particular coded video sequence, the contents of
//   sequence_header_obu must be bit-identical each time the sequence header
//   appears except for the contents of operating_parameters_info.
static int are_seq_headers_consistent(const SequenceHeader *seq_params_old,
                                      const SequenceHeader *seq_params_new) {
  return !memcmp(seq_params_old, seq_params_new,
                 offsetof(SequenceHeader, op_params));
}

// On success, sets pbi->sequence_header_ready to 1 and returns the number of
// bytes read from 'rb'.
// On failure, sets pbi->common.error.error_code and returns 0.
static uint32_t read_sequence_header_obu(AV1Decoder *pbi,
                                         struct aom_read_bit_buffer *rb) {
  AV1_COMMON *const cm = &pbi->common;
  const uint32_t saved_bit_offset = rb->bit_offset;

  // Verify rb has been configured to report errors.
  assert(rb->error_handler);

  // Use a local variable to store the information as we decode. At the end,
  // if no errors have occurred, cm->seq_params is updated.
  SequenceHeader sh = *cm->seq_params;
  SequenceHeader *const seq_params = &sh;

  seq_params->profile = av1_read_profile(rb);
  if (seq_params->profile > CONFIG_MAX_DECODE_PROFILE) {
    pbi->error.error_code = AOM_CODEC_UNSUP_BITSTREAM;
    return 0;
  }

  // Still picture or not
  seq_params->still_picture = aom_rb_read_bit(rb);
  seq_params->reduced_still_picture_hdr = aom_rb_read_bit(rb);
  // Video must have reduced_still_picture_hdr = 0
  if (!seq_params->still_picture && seq_params->reduced_still_picture_hdr) {
    pbi->error.error_code = AOM_CODEC_UNSUP_BITSTREAM;
    return 0;
  }

  if (seq_params->reduced_still_picture_hdr) {
    seq_params->timing_info_present = 0;
    seq_params->decoder_model_info_present_flag = 0;
    seq_params->display_model_info_present_flag = 0;
    seq_params->operating_points_cnt_minus_1 = 0;
    seq_params->operating_point_idc[0] = 0;
    seq_params->has_nonzero_operating_point_idc = false;
    if (!read_bitstream_level(&seq_params->seq_level_idx[0], rb)) {
      pbi->error.error_code = AOM_CODEC_UNSUP_BITSTREAM;
      return 0;
    }
    seq_params->tier[0] = 0;
    seq_params->op_params[0].decoder_model_param_present_flag = 0;
    seq_params->op_params[0].display_model_param_present_flag = 0;
  } else {
    seq_params->timing_info_present = aom_rb_read_bit(rb);
    if (seq_params->timing_info_present) {
      av1_read_timing_info_header(&seq_params->timing_info, &pbi->error, rb);

      seq_params->decoder_model_info_present_flag = aom_rb_read_bit(rb);
      if (seq_params->decoder_model_info_present_flag)
        av1_read_decoder_model_info(&seq_params->decoder_model_info, rb);
    } else {
      seq_params->decoder_model_info_present_flag = 0;
    }
    seq_params->display_model_info_present_flag = aom_rb_read_bit(rb);
    seq_params->operating_points_cnt_minus_1 =
        aom_rb_read_literal(rb, OP_POINTS_CNT_MINUS_1_BITS);
    seq_params->has_nonzero_operating_point_idc = false;
    for (int i = 0; i < seq_params->operating_points_cnt_minus_1 + 1; i++) {
      seq_params->operating_point_idc[i] =
          aom_rb_read_literal(rb, OP_POINTS_IDC_BITS);
      if (seq_params->operating_point_idc[i] != 0)
        seq_params->has_nonzero_operating_point_idc = true;
      if (!read_bitstream_level(&seq_params->seq_level_idx[i], rb)) {
        pbi->error.error_code = AOM_CODEC_UNSUP_BITSTREAM;
        return 0;
      }
      // This is the seq_level_idx[i] > 7 check in the spec. seq_level_idx 7
      // is equivalent to level 3.3.
      if (seq_params->seq_level_idx[i] >= SEQ_LEVEL_4_0)
        seq_params->tier[i] = aom_rb_read_bit(rb);
      else
        seq_params->tier[i] = 0;
      if (seq_params->decoder_model_info_present_flag) {
        seq_params->op_params[i].decoder_model_param_present_flag =
            aom_rb_read_bit(rb);
        if (seq_params->op_params[i].decoder_model_param_present_flag)
          av1_read_op_parameters_info(&seq_params->op_params[i],
                                      seq_params->decoder_model_info
                                          .encoder_decoder_buffer_delay_length,
                                      rb);
      } else {
        seq_params->op_params[i].decoder_model_param_present_flag = 0;
      }
      if (seq_params->timing_info_present &&
          (seq_params->timing_info.equal_picture_interval ||
           seq_params->op_params[i].decoder_model_param_present_flag)) {
        seq_params->op_params[i].bitrate = av1_max_level_bitrate(
            seq_params->profile, seq_params->seq_level_idx[i],
            seq_params->tier[i]);
        // Level with seq_level_idx = 31 returns a high "dummy" bitrate to pass
        // the check
        if (seq_params->op_params[i].bitrate == 0)
          aom_internal_error(&pbi->error, AOM_CODEC_UNSUP_BITSTREAM,
                             "AV1 does not support this combination of "
                             "profile, level, and tier.");
        // Buffer size in bits/s is bitrate in bits/s * 1 s
        seq_params->op_params[i].buffer_size = seq_params->op_params[i].bitrate;
      }
      if (seq_params->timing_info_present &&
          seq_params->timing_info.equal_picture_interval &&
          !seq_params->op_params[i].decoder_model_param_present_flag) {
        // When the decoder_model_parameters are not sent for this op, set
        // the default ones that can be used with the resource availability mode
        seq_params->op_params[i].decoder_buffer_delay = 70000;
        seq_params->op_params[i].encoder_buffer_delay = 20000;
        seq_params->op_params[i].low_delay_mode_flag = 0;
      }

      if (seq_params->display_model_info_present_flag) {
        seq_params->op_params[i].display_model_param_present_flag =
            aom_rb_read_bit(rb);
        if (seq_params->op_params[i].display_model_param_present_flag) {
          seq_params->op_params[i].initial_display_delay =
              aom_rb_read_literal(rb, 4) + 1;
          if (seq_params->op_params[i].initial_display_delay > 10)
            aom_internal_error(
                &pbi->error, AOM_CODEC_UNSUP_BITSTREAM,
                "AV1 does not support more than 10 decoded frames delay");
        } else {
          seq_params->op_params[i].initial_display_delay = 10;
        }
      } else {
        seq_params->op_params[i].display_model_param_present_flag = 0;
        seq_params->op_params[i].initial_display_delay = 10;
      }
    }
  }
  // This decoder supports all levels.  Choose operating point provided by
  // external means
  int operating_point = pbi->operating_point;
  if (operating_point < 0 ||
      operating_point > seq_params->operating_points_cnt_minus_1)
    operating_point = 0;
  pbi->current_operating_point =
      seq_params->operating_point_idc[operating_point];
  if (aom_get_num_layers_from_operating_point_idc(
          pbi->current_operating_point, &pbi->number_spatial_layers,
          &pbi->number_temporal_layers) != AOM_CODEC_OK) {
    pbi->error.error_code = AOM_CODEC_ERROR;
    return 0;
  }

  av1_read_sequence_header(cm, rb, seq_params);

  av1_read_color_config(rb, pbi->allow_lowbitdepth, seq_params, &pbi->error);
  if (!(seq_params->subsampling_x == 0 && seq_params->subsampling_y == 0) &&
      !(seq_params->subsampling_x == 1 && seq_params->subsampling_y == 1) &&
      !(seq_params->subsampling_x == 1 && seq_params->subsampling_y == 0)) {
    aom_internal_error(&pbi->error, AOM_CODEC_UNSUP_BITSTREAM,
                       "Only 4:4:4, 4:2:2 and 4:2:0 are currently supported, "
                       "%d %d subsampling is not supported.\n",
                       seq_params->subsampling_x, seq_params->subsampling_y);
  }

  seq_params->film_grain_params_present = aom_rb_read_bit(rb);

  if (av1_check_trailing_bits(pbi, rb) != 0) {
    // pbi->error.error_code is already set.
    return 0;
  }

  // If a sequence header has been decoded before, we check if the new
  // one is consistent with the old one.
  if (pbi->sequence_header_ready) {
    if (!are_seq_headers_consistent(cm->seq_params, seq_params))
      pbi->sequence_header_changed = 1;
  }

  *cm->seq_params = *seq_params;
  pbi->sequence_header_ready = 1;

  return ((rb->bit_offset - saved_bit_offset + 7) >> 3);
}

// On success, returns the frame header size. On failure, calls
// aom_internal_error and does not return. If show existing frame,
// also marks the data processing to end after the frame header.
static uint32_t read_frame_header_obu(AV1Decoder *pbi,
                                      struct aom_read_bit_buffer *rb,
                                      const uint8_t *data,
                                      const uint8_t **p_data_end,
                                      int trailing_bits_present) {
  const uint32_t hdr_size =
      av1_decode_frame_headers_and_setup(pbi, rb, trailing_bits_present);
  const AV1_COMMON *cm = &pbi->common;
  if (cm->show_existing_frame) {
    *p_data_end = data + hdr_size;
  }
  return hdr_size;
}

// On success, returns the tile group header size. On failure, calls
// aom_internal_error() and returns -1.
static int32_t read_tile_group_header(AV1Decoder *pbi,
                                      struct aom_read_bit_buffer *rb,
                                      int *start_tile, int *end_tile,
                                      int tile_start_implicit) {
  AV1_COMMON *const cm = &pbi->common;
  CommonTileParams *const tiles = &cm->tiles;
  uint32_t saved_bit_offset = rb->bit_offset;
  int tile_start_and_end_present_flag = 0;
  const int num_tiles = tiles->rows * tiles->cols;

  if (!tiles->large_scale && num_tiles > 1) {
    tile_start_and_end_present_flag = aom_rb_read_bit(rb);
    if (tile_start_implicit && tile_start_and_end_present_flag) {
      aom_internal_error(
          &pbi->error, AOM_CODEC_UNSUP_BITSTREAM,
          "For OBU_FRAME type obu tile_start_and_end_present_flag must be 0");
      return -1;
    }
  }
  if (tiles->large_scale || num_tiles == 1 ||
      !tile_start_and_end_present_flag) {
    *start_tile = 0;
    *end_tile = num_tiles - 1;
  } else {
    int tile_bits = tiles->log2_rows + tiles->log2_cols;
    *start_tile = aom_rb_read_literal(rb, tile_bits);
    *end_tile = aom_rb_read_literal(rb, tile_bits);
  }
  if (*start_tile != pbi->next_start_tile) {
    aom_internal_error(&pbi->error, AOM_CODEC_CORRUPT_FRAME,
                       "tg_start (%d) must be equal to %d", *start_tile,
                       pbi->next_start_tile);
    return -1;
  }
  if (*start_tile > *end_tile) {
    aom_internal_error(
        &pbi->error, AOM_CODEC_CORRUPT_FRAME,
        "tg_end (%d) must be greater than or equal to tg_start (%d)", *end_tile,
        *start_tile);
    return -1;
  }
  if (*end_tile >= num_tiles) {
    aom_internal_error(&pbi->error, AOM_CODEC_CORRUPT_FRAME,
                       "tg_end (%d) must be less than NumTiles (%d)", *end_tile,
                       num_tiles);
    return -1;
  }
  pbi->next_start_tile = (*end_tile == num_tiles - 1) ? 0 : *end_tile + 1;

  return ((rb->bit_offset - saved_bit_offset + 7) >> 3);
}

// On success, returns the tile group OBU size. On failure, sets
// pbi->common.error.error_code and returns 0.
static uint32_t read_one_tile_group_obu(
    AV1Decoder *pbi, struct aom_read_bit_buffer *rb, int is_first_tg,
    const uint8_t *data, const uint8_t *data_end, const uint8_t **p_data_end,
    int *is_last_tg, int tile_start_implicit) {
  AV1_COMMON *const cm = &pbi->common;
  int start_tile, end_tile;
  int32_t header_size, tg_payload_size;

  assert((rb->bit_offset & 7) == 0);
  assert(rb->bit_buffer + aom_rb_bytes_read(rb) == data);

  header_size = read_tile_group_header(pbi, rb, &start_tile, &end_tile,
                                       tile_start_implicit);
  if (header_size == -1 || byte_alignment(cm, rb)) return 0;
  data += header_size;
  av1_decode_tg_tiles_and_wrapup(pbi, data, data_end, p_data_end, start_tile,
                                 end_tile, is_first_tg);

  tg_payload_size = (uint32_t)(*p_data_end - data);

  *is_last_tg = end_tile == cm->tiles.rows * cm->tiles.cols - 1;
  return header_size + tg_payload_size;
}

static void alloc_tile_list_buffer(AV1Decoder *pbi, int tile_width_in_pixels,
                                   int tile_height_in_pixels) {
  // The resolution of the output frame is read out from the bitstream. The data
  // are stored in the order of Y plane, U plane and V plane. As an example, for
  // image format 4:2:0, the output frame of U plane and V plane is 1/4 of the
  // output frame.
  AV1_COMMON *const cm = &pbi->common;
  const int output_frame_width =
      (pbi->output_frame_width_in_tiles_minus_1 + 1) * tile_width_in_pixels;
  const int output_frame_height =
      (pbi->output_frame_height_in_tiles_minus_1 + 1) * tile_height_in_pixels;
  // The output frame is used to store the decoded tile list. The decoded tile
  // list has to fit into 1 output frame.
  assert((pbi->tile_count_minus_1 + 1) <=
         (pbi->output_frame_width_in_tiles_minus_1 + 1) *
             (pbi->output_frame_height_in_tiles_minus_1 + 1));

  // Allocate the tile list output buffer.
  // Note: if cm->seq_params->use_highbitdepth is 1 and
  // cm->seq_params->bit_depth is 8, we could allocate less memory, namely, 8
  // bits/pixel.
  if (aom_alloc_frame_buffer(&pbi->tile_list_outbuf, output_frame_width,
                             output_frame_height, cm->seq_params->subsampling_x,
                             cm->seq_params->subsampling_y,
                             (cm->seq_params->use_highbitdepth &&
                              (cm->seq_params->bit_depth > AOM_BITS_8)),
                             0, cm->features.byte_alignment, false, 0))
    aom_internal_error(&pbi->error, AOM_CODEC_MEM_ERROR,
                       "Failed to allocate the tile list output buffer");
}

static void yv12_tile_copy(const YV12_BUFFER_CONFIG *src, int hstart1,
                           int hend1, int vstart1, int vend1,
                           YV12_BUFFER_CONFIG *dst, int hstart2, int vstart2,
                           int plane) {
  const int src_stride = (plane > 0) ? src->strides[1] : src->strides[0];
  const int dst_stride = (plane > 0) ? dst->strides[1] : dst->strides[0];
  int row, col;

  assert(src->flags & YV12_FLAG_HIGHBITDEPTH);
  assert(!(dst->flags & YV12_FLAG_HIGHBITDEPTH));

  const uint16_t *src16 =
      CONVERT_TO_SHORTPTR(src->buffers[plane] + vstart1 * src_stride + hstart1);
  uint8_t *dst8 = dst->buffers[plane] + vstart2 * dst_stride + hstart2;

  for (row = vstart1; row < vend1; ++row) {
    for (col = 0; col < (hend1 - hstart1); ++col) *dst8++ = (uint8_t)(*src16++);
    src16 += src_stride - (hend1 - hstart1);
    dst8 += dst_stride - (hend1 - hstart1);
  }
  return;
}

static void copy_decoded_tile_to_tile_list_buffer(AV1Decoder *pbi, int tile_idx,
                                                  int tile_width_in_pixels,
                                                  int tile_height_in_pixels) {
  AV1_COMMON *const cm = &pbi->common;
  const int ssy = cm->seq_params->subsampling_y;
  const int ssx = cm->seq_params->subsampling_x;
  const int num_planes = av1_num_planes(cm);

  YV12_BUFFER_CONFIG *cur_frame = &cm->cur_frame->buf;
  const int tr = tile_idx / (pbi->output_frame_width_in_tiles_minus_1 + 1);
  const int tc = tile_idx % (pbi->output_frame_width_in_tiles_minus_1 + 1);
  int plane;

  // Copy decoded tile to the tile list output buffer.
  for (plane = 0; plane < num_planes; ++plane) {
    const int shift_x = plane > 0 ? ssx : 0;
    const int shift_y = plane > 0 ? ssy : 0;
    const int h = tile_height_in_pixels >> shift_y;
    const int w = tile_width_in_pixels >> shift_x;

    // src offset
    int vstart1 = pbi->dec_tile_row * h;
    int vend1 = vstart1 + h;
    int hstart1 = pbi->dec_tile_col * w;
    int hend1 = hstart1 + w;
    // dst offset
    int vstart2 = tr * h;
    int hstart2 = tc * w;

    if (cm->seq_params->use_highbitdepth &&
        cm->seq_params->bit_depth == AOM_BITS_8) {
      yv12_tile_copy(cur_frame, hstart1, hend1, vstart1, vend1,
                     &pbi->tile_list_outbuf, hstart2, vstart2, plane);
    } else {
      switch (plane) {
        case 0:
          aom_yv12_partial_copy_y(cur_frame, hstart1, hend1, vstart1, vend1,
                                  &pbi->tile_list_outbuf, hstart2, vstart2);
          break;
        case 1:
          aom_yv12_partial_copy_u(cur_frame, hstart1, hend1, vstart1, vend1,
                                  &pbi->tile_list_outbuf, hstart2, vstart2);
          break;
        case 2:
          aom_yv12_partial_copy_v(cur_frame, hstart1, hend1, vstart1, vend1,
                                  &pbi->tile_list_outbuf, hstart2, vstart2);
          break;
        default: assert(0);
      }
    }
  }
}

// Only called while large_scale_tile = 1.
//
// On success, returns the tile list OBU size. On failure, sets
// pbi->common.error.error_code and returns 0.
static uint32_t read_and_decode_one_tile_list(AV1Decoder *pbi,
                                              struct aom_read_bit_buffer *rb,
                                              const uint8_t *data,
                                              const uint8_t *data_end,
                                              const uint8_t **p_data_end,
                                              int *frame_decoding_finished) {
  AV1_COMMON *const cm = &pbi->common;
  uint32_t tile_list_payload_size = 0;
  const int num_tiles = cm->tiles.cols * cm->tiles.rows;
  const int start_tile = 0;
  const int end_tile = num_tiles - 1;
  int i = 0;

  // Process the tile list info.
  pbi->output_frame_width_in_tiles_minus_1 = aom_rb_read_literal(rb, 8);
  pbi->output_frame_height_in_tiles_minus_1 = aom_rb_read_literal(rb, 8);
  pbi->tile_count_minus_1 = aom_rb_read_literal(rb, 16);

  // The output frame is used to store the decoded tile list. The decoded tile
  // list has to fit into 1 output frame.
  if ((pbi->tile_count_minus_1 + 1) >
      (pbi->output_frame_width_in_tiles_minus_1 + 1) *
          (pbi->output_frame_height_in_tiles_minus_1 + 1)) {
    pbi->error.error_code = AOM_CODEC_CORRUPT_FRAME;
    return 0;
  }

  if (pbi->tile_count_minus_1 > MAX_TILES - 1) {
    pbi->error.error_code = AOM_CODEC_CORRUPT_FRAME;
    return 0;
  }

  int tile_width, tile_height;
  if (!av1_get_uniform_tile_size(cm, &tile_width, &tile_height)) {
    pbi->error.error_code = AOM_CODEC_CORRUPT_FRAME;
    return 0;
  }
  const int tile_width_in_pixels = tile_width * MI_SIZE;
  const int tile_height_in_pixels = tile_height * MI_SIZE;

  // Allocate output frame buffer for the tile list.
  alloc_tile_list_buffer(pbi, tile_width_in_pixels, tile_height_in_pixels);

  uint32_t tile_list_info_bytes = 4;
  tile_list_payload_size += tile_list_info_bytes;
  data += tile_list_info_bytes;

  int tile_idx = 0;
  for (i = 0; i <= pbi->tile_count_minus_1; i++) {
    // Process 1 tile.
    // Reset the bit reader.
    rb->bit_offset = 0;
    rb->bit_buffer = data;

    // Read out the tile info.
    uint32_t tile_info_bytes = 5;
    // Set reference for each tile.
    int ref_idx = aom_rb_read_literal(rb, 8);
    if (ref_idx >= MAX_EXTERNAL_REFERENCES) {
      pbi->error.error_code = AOM_CODEC_CORRUPT_FRAME;
      return 0;
    }
    av1_set_reference_dec(cm, cm->remapped_ref_idx[0], 1,
                          &pbi->ext_refs.refs[ref_idx]);

    pbi->dec_tile_row = aom_rb_read_literal(rb, 8);
    pbi->dec_tile_col = aom_rb_read_literal(rb, 8);
    if (pbi->dec_tile_row < 0 || pbi->dec_tile_col < 0 ||
        pbi->dec_tile_row >= cm->tiles.rows ||
        pbi->dec_tile_col >= cm->tiles.cols) {
      pbi->error.error_code = AOM_CODEC_CORRUPT_FRAME;
      return 0;
    }

    pbi->coded_tile_data_size = aom_rb_read_literal(rb, 16) + 1;
    data += tile_info_bytes;
    if ((size_t)(data_end - data) < pbi->coded_tile_data_size) {
      pbi->error.error_code = AOM_CODEC_CORRUPT_FRAME;
      return 0;
    }

    av1_decode_tg_tiles_and_wrapup(pbi, data, data + pbi->coded_tile_data_size,
                                   p_data_end, start_tile, end_tile, 0);
    uint32_t tile_payload_size = (uint32_t)(*p_data_end - data);

    tile_list_payload_size += tile_info_bytes + tile_payload_size;

    // Update data ptr for next tile decoding.
    data = *p_data_end;
    assert(data <= data_end);

    // Copy the decoded tile to the tile list output buffer.
    copy_decoded_tile_to_tile_list_buffer(pbi, tile_idx, tile_width_in_pixels,
                                          tile_height_in_pixels);
    tile_idx++;
  }

  *frame_decoding_finished = 1;
  return tile_list_payload_size;
}

// Returns the last nonzero byte index in 'data'. If there is no nonzero byte in
// 'data', returns -1.
static int get_last_nonzero_byte_index(const uint8_t *data, size_t sz) {
  // Scan backward and return on the first nonzero byte.
  int i = (int)sz - 1;
  while (i >= 0 && data[i] == 0) {
    --i;
  }
  return i;
}

// Allocates metadata that was read and adds it to the decoders metadata array.
static void alloc_read_metadata(AV1Decoder *const pbi,
                                OBU_METADATA_TYPE metadata_type,
                                const uint8_t *data, size_t sz,
                                aom_metadata_insert_flags_t insert_flag) {
  if (!pbi->metadata) {
    pbi->metadata = aom_img_metadata_array_alloc(0);
    if (!pbi->metadata) {
      aom_internal_error(&pbi->error, AOM_CODEC_MEM_ERROR,
                         "Failed to allocate metadata array");
    }
  }
  aom_metadata_t *metadata =
      aom_img_metadata_alloc(metadata_type, data, sz, insert_flag);
  if (!metadata) {
    aom_internal_error(&pbi->error, AOM_CODEC_MEM_ERROR,
                       "Error allocating metadata");
  }
  aom_metadata_t **metadata_array =
      (aom_metadata_t **)realloc(pbi->metadata->metadata_array,
                                 (pbi->metadata->sz + 1) * sizeof(metadata));
  if (!metadata_array) {
    aom_img_metadata_free(metadata);
    aom_internal_error(&pbi->error, AOM_CODEC_MEM_ERROR,
                       "Error growing metadata array");
  }
  pbi->metadata->metadata_array = metadata_array;
  pbi->metadata->metadata_array[pbi->metadata->sz] = metadata;
  pbi->metadata->sz++;
}

// On failure, calls aom_internal_error() and does not return.
static void read_metadata_itut_t35(AV1Decoder *const pbi, const uint8_t *data,
                                   size_t sz, bool has_obu_extension_header) {
  if (sz == 0) {
    aom_internal_error(&pbi->error, AOM_CODEC_CORRUPT_FRAME,
                       "itu_t_t35_country_code is missing");
  }
  int country_code_size = 1;
  if (*data == 0xFF) {
    if (sz == 1) {
      aom_internal_error(&pbi->error, AOM_CODEC_CORRUPT_FRAME,
                         "itu_t_t35_country_code_extension_byte is missing");
    }
    ++country_code_size;
  }
  int end_index = get_last_nonzero_byte_index(data, sz);
  if (end_index < country_code_size) {
    aom_internal_error(&pbi->error, AOM_CODEC_CORRUPT_FRAME,
                       "No trailing bits found in ITU-T T.35 metadata OBU");
  }
  // itu_t_t35_payload_bytes is byte aligned. Section 6.7.2 of the spec says:
  //   itu_t_t35_payload_bytes shall be bytes containing data registered as
  //   specified in Recommendation ITU-T T.35.
  // Therefore the first trailing byte should be 0x80.
  if (data[end_index] != 0x80) {
    aom_internal_error(&pbi->error, AOM_CODEC_CORRUPT_FRAME,
                       "The last nonzero byte of the ITU-T T.35 metadata OBU "
                       "is 0x%02x, should be 0x80.",
                       data[end_index]);
  }
  alloc_read_metadata(pbi, OBU_METADATA_TYPE_ITUT_T35, data, end_index,
                      has_obu_extension_header
                          ? AOM_MIF_ANY_FRAME_LAYER_SPECIFIC
                          : AOM_MIF_ANY_FRAME);
}

// On success, returns the number of bytes read from 'data'. On failure, calls
// aom_internal_error() and does not return.
static size_t read_metadata_hdr_cll(AV1Decoder *const pbi, const uint8_t *data,
                                    size_t sz) {
  const size_t kHdrCllPayloadSize = 4;
  if (sz < kHdrCllPayloadSize) {
    aom_internal_error(&pbi->error, AOM_CODEC_CORRUPT_FRAME,
                       "Incorrect HDR CLL metadata payload size");
  }
  alloc_read_metadata(pbi, OBU_METADATA_TYPE_HDR_CLL, data, kHdrCllPayloadSize,
                      AOM_MIF_ANY_FRAME);
  return kHdrCllPayloadSize;
}

// On success, returns the number of bytes read from 'data'. On failure, calls
// aom_internal_error() and does not return.
static size_t read_metadata_hdr_mdcv(AV1Decoder *const pbi, const uint8_t *data,
                                     size_t sz) {
  const size_t kMdcvPayloadSize = 24;
  if (sz < kMdcvPayloadSize) {
    aom_internal_error(&pbi->error, AOM_CODEC_CORRUPT_FRAME,
                       "Incorrect HDR MDCV metadata payload size");
  }
  alloc_read_metadata(pbi, OBU_METADATA_TYPE_HDR_MDCV, data, kMdcvPayloadSize,
                      AOM_MIF_ANY_FRAME);
  return kMdcvPayloadSize;
}

static void scalability_structure(struct aom_read_bit_buffer *rb) {
  const int spatial_layers_cnt_minus_1 = aom_rb_read_literal(rb, 2);
  const int spatial_layer_dimensions_present_flag = aom_rb_read_bit(rb);
  const int spatial_layer_description_present_flag = aom_rb_read_bit(rb);
  const int temporal_group_description_present_flag = aom_rb_read_bit(rb);
  // scalability_structure_reserved_3bits must be set to zero and be ignored by
  // decoders.
  aom_rb_read_literal(rb, 3);

  if (spatial_layer_dimensions_present_flag) {
    for (int i = 0; i <= spatial_layers_cnt_minus_1; i++) {
      aom_rb_read_literal(rb, 16);
      aom_rb_read_literal(rb, 16);
    }
  }
  if (spatial_layer_description_present_flag) {
    for (int i = 0; i <= spatial_layers_cnt_minus_1; i++) {
      aom_rb_read_literal(rb, 8);
    }
  }
  if (temporal_group_description_present_flag) {
    const int temporal_group_size = aom_rb_read_literal(rb, 8);
    for (int i = 0; i < temporal_group_size; i++) {
      aom_rb_read_literal(rb, 3);
      aom_rb_read_bit(rb);
      aom_rb_read_bit(rb);
      const int temporal_group_ref_cnt = aom_rb_read_literal(rb, 3);
      for (int j = 0; j < temporal_group_ref_cnt; j++) {
        aom_rb_read_literal(rb, 8);
      }
    }
  }
}

static void read_metadata_scalability(struct aom_read_bit_buffer *rb) {
  const int scalability_mode_idc = aom_rb_read_literal(rb, 8);
  if (scalability_mode_idc == SCALABILITY_SS) {
    scalability_structure(rb);
  }
}

static void read_metadata_timecode(struct aom_read_bit_buffer *rb) {
  aom_rb_read_literal(rb, 5);  // counting_type f(5)
  const int full_timestamp_flag =
      aom_rb_read_bit(rb);     // full_timestamp_flag f(1)
  aom_rb_read_bit(rb);         // discontinuity_flag (f1)
  aom_rb_read_bit(rb);         // cnt_dropped_flag f(1)
  aom_rb_read_literal(rb, 9);  // n_frames f(9)
  if (full_timestamp_flag) {
    aom_rb_read_literal(rb, 6);  // seconds_value f(6)
    aom_rb_read_literal(rb, 6);  // minutes_value f(6)
    aom_rb_read_literal(rb, 5);  // hours_value f(5)
  } else {
    const int seconds_flag = aom_rb_read_bit(rb);  // seconds_flag f(1)
    if (seconds_flag) {
      aom_rb_read_literal(rb, 6);                    // seconds_value f(6)
      const int minutes_flag = aom_rb_read_bit(rb);  // minutes_flag f(1)
      if (minutes_flag) {
        aom_rb_read_literal(rb, 6);                  // minutes_value f(6)
        const int hours_flag = aom_rb_read_bit(rb);  // hours_flag f(1)
        if (hours_flag) {
          aom_rb_read_literal(rb, 5);  // hours_value f(5)
        }
      }
    }
  }
  // time_offset_length f(5)
  const int time_offset_length = aom_rb_read_literal(rb, 5);
  if (time_offset_length) {
    // time_offset_value f(time_offset_length)
    aom_rb_read_literal(rb, time_offset_length);
  }
}

// Returns the last nonzero byte in 'data'. If there is no nonzero byte in
// 'data', returns 0.
//
// Call this function to check the following requirement in the spec:
//   This implies that when any payload data is present for this OBU type, at
//   least one byte of the payload data (including the trailing bit) shall not
//   be equal to 0.
static uint8_t get_last_nonzero_byte(const uint8_t *data, size_t sz) {
  // Scan backward and return on the first nonzero byte.
  size_t i = sz;
  while (i != 0) {
    --i;
    if (data[i] != 0) return data[i];
  }
  return 0;
}

// Checks the metadata for correct syntax but ignores the parsed metadata.
//
// On success, returns the number of bytes read from 'data'. On failure, sets
// pbi->common.error.error_code and returns 0, or calls aom_internal_error()
// and does not return.
static size_t read_metadata(AV1Decoder *pbi, const uint8_t *data, size_t sz,
                            bool has_obu_extension_header) {
  size_t type_length;
  uint64_t type_value;
  if (aom_uleb_decode(data, sz, &type_value, &type_length) < 0) {
    pbi->error.error_code = AOM_CODEC_CORRUPT_FRAME;
    return 0;
  }
  const OBU_METADATA_TYPE metadata_type = (OBU_METADATA_TYPE)type_value;
  if (metadata_type == 0 || metadata_type >= 6) {
    // If metadata_type is reserved for future use or a user private value,
    // ignore the entire OBU and just check trailing bits.
    if (get_last_nonzero_byte(data + type_length, sz - type_length) == 0) {
      pbi->error.error_code = AOM_CODEC_CORRUPT_FRAME;
      return 0;
    }
    return sz;
  }
  if (metadata_type == OBU_METADATA_TYPE_ITUT_T35) {
    // read_metadata_itut_t35() checks trailing bits.
    read_metadata_itut_t35(pbi, data + type_length, sz - type_length,
                           has_obu_extension_header);
    return sz;
  } else if (metadata_type == OBU_METADATA_TYPE_HDR_CLL) {
    size_t bytes_read =
        type_length +
        read_metadata_hdr_cll(pbi, data + type_length, sz - type_length);
    if (get_last_nonzero_byte(data + bytes_read, sz - bytes_read) != 0x80) {
      pbi->error.error_code = AOM_CODEC_CORRUPT_FRAME;
      return 0;
    }
    return sz;
  } else if (metadata_type == OBU_METADATA_TYPE_HDR_MDCV) {
    size_t bytes_read =
        type_length +
        read_metadata_hdr_mdcv(pbi, data + type_length, sz - type_length);
    if (get_last_nonzero_byte(data + bytes_read, sz - bytes_read) != 0x80) {
      pbi->error.error_code = AOM_CODEC_CORRUPT_FRAME;
      return 0;
    }
    return sz;
  }

  struct aom_read_bit_buffer rb;
  av1_init_read_bit_buffer(pbi, &rb, data + type_length, data + sz);
  if (metadata_type == OBU_METADATA_TYPE_SCALABILITY) {
    read_metadata_scalability(&rb);
  } else {
    assert(metadata_type == OBU_METADATA_TYPE_TIMECODE);
    read_metadata_timecode(&rb);
  }
  if (av1_check_trailing_bits(pbi, &rb) != 0) {
    // pbi->error.error_code is already set.
    return 0;
  }
  assert((rb.bit_offset & 7) == 0);
  return type_length + (rb.bit_offset >> 3);
}

// On success, returns 'sz'. On failure, sets pbi->common.error.error_code and
// returns 0.
static size_t read_padding(AV1_COMMON *const cm, const uint8_t *data,
                           size_t sz) {
  // The spec allows a padding OBU to be header-only (i.e., obu_size = 0). So
  // check trailing bits only if sz > 0.
  if (sz > 0) {
    // The payload of a padding OBU is byte aligned. Therefore the first
    // trailing byte should be 0x80. See https://crbug.com/aomedia/2393.
    const uint8_t last_nonzero_byte = get_last_nonzero_byte(data, sz);
    if (last_nonzero_byte != 0x80) {
      cm->error->error_code = AOM_CODEC_CORRUPT_FRAME;
      return 0;
    }
  }
  return sz;
}

// On success, returns a boolean that indicates whether the decoding of the
// current frame is finished. On failure, sets pbi->error.error_code and
// returns -1.
int aom_decode_frame_from_obus(struct AV1Decoder *pbi, const uint8_t *data,
                               const uint8_t *data_end,
                               const uint8_t **p_data_end) {
  AV1_COMMON *const cm = &pbi->common;
  int frame_decoding_finished = 0;
  int is_first_tg_obu_received = 1;
  // Whenever pbi->seen_frame_header is set to 1, frame_header is set to the
  // beginning of the frame_header_obu and frame_header_size is set to its
  // size. This allows us to check if a redundant frame_header_obu is a copy
  // of the previous frame_header_obu.
  //
  // Initialize frame_header to a dummy nonnull pointer, otherwise the Clang
  // Static Analyzer in clang 7.0.1 will falsely warn that a null pointer is
  // passed as an argument to a 'nonnull' parameter of memcmp(). The initial
  // value will not be used.
  const uint8_t *frame_header = data;
  uint32_t frame_header_size = 0;
  ObuHeader obu_header;
  memset(&obu_header, 0, sizeof(obu_header));
  pbi->seen_frame_header = 0;
  pbi->next_start_tile = 0;
  pbi->num_tile_groups = 0;

  if (data_end < data) {
    pbi->error.error_code = AOM_CODEC_CORRUPT_FRAME;
    return -1;
  }

  // Reset pbi->camera_frame_header_ready to 0 if cm->tiles.large_scale = 0.
  if (!cm->tiles.large_scale) pbi->camera_frame_header_ready = 0;

  // decode frame as a series of OBUs
  while (!frame_decoding_finished && pbi->error.error_code == AOM_CODEC_OK) {
    struct aom_read_bit_buffer rb;
    size_t payload_size = 0;
    size_t decoded_payload_size = 0;
    size_t obu_payload_offset = 0;
    size_t bytes_read = 0;
    const size_t bytes_available = data_end - data;

    if (bytes_available == 0 && !pbi->seen_frame_header) {
      *p_data_end = data;
      pbi->error.error_code = AOM_CODEC_OK;
      break;
    }

    aom_codec_err_t status =
        aom_read_obu_header_and_size(data, bytes_available, pbi->is_annexb,
                                     &obu_header, &payload_size, &bytes_read);

    if (status != AOM_CODEC_OK) {
      pbi->error.error_code = status;
      return -1;
    }

    // Record obu size header information.
    pbi->obu_size_hdr.data = data + obu_header.size;
    pbi->obu_size_hdr.size = bytes_read - obu_header.size;

    // Note: aom_read_obu_header_and_size() takes care of checking that this
    // doesn't cause 'data' to advance past 'data_end'.
    data += bytes_read;

    if ((size_t)(data_end - data) < payload_size) {
      pbi->error.error_code = AOM_CODEC_CORRUPT_FRAME;
      return -1;
    }

    cm->temporal_layer_id = obu_header.temporal_layer_id;
    cm->spatial_layer_id = obu_header.spatial_layer_id;

    if (obu_header.type != OBU_TEMPORAL_DELIMITER &&
        obu_header.type != OBU_SEQUENCE_HEADER) {
      // don't decode obu if it's not in current operating mode
      if (!is_obu_in_current_operating_point(pbi, &obu_header)) {
        data += payload_size;
        continue;
      }
    }

    av1_init_read_bit_buffer(pbi, &rb, data, data + payload_size);

    switch (obu_header.type) {
      case OBU_TEMPORAL_DELIMITER:
        decoded_payload_size = read_temporal_delimiter_obu();
        if (pbi->seen_frame_header) {
          // A new temporal unit has started, but the frame in the previous
          // temporal unit is incomplete.
          pbi->error.error_code = AOM_CODEC_CORRUPT_FRAME;
          return -1;
        }
        break;
      case OBU_SEQUENCE_HEADER:
        decoded_payload_size = read_sequence_header_obu(pbi, &rb);
        if (pbi->error.error_code != AOM_CODEC_OK) return -1;
        // The sequence header should not change in the middle of a frame.
        if (pbi->sequence_header_changed && pbi->seen_frame_header) {
          pbi->error.error_code = AOM_CODEC_CORRUPT_FRAME;
          return -1;
        }
        break;
      case OBU_FRAME_HEADER:
      case OBU_REDUNDANT_FRAME_HEADER:
      case OBU_FRAME:
        if (obu_header.type == OBU_REDUNDANT_FRAME_HEADER) {
          if (!pbi->seen_frame_header) {
            pbi->error.error_code = AOM_CODEC_CORRUPT_FRAME;
            return -1;
          }
        } else {
          // OBU_FRAME_HEADER or OBU_FRAME.
          if (pbi->seen_frame_header) {
            pbi->error.error_code = AOM_CODEC_CORRUPT_FRAME;
            return -1;
          }
        }
        // Only decode first frame header received
        if (!pbi->seen_frame_header ||
            (cm->tiles.large_scale && !pbi->camera_frame_header_ready)) {
          frame_header_size = read_frame_header_obu(
              pbi, &rb, data, p_data_end, obu_header.type != OBU_FRAME);
          frame_header = data;
          pbi->seen_frame_header = 1;
          if (!pbi->ext_tile_debug && cm->tiles.large_scale)
            pbi->camera_frame_header_ready = 1;
        } else {
          // Verify that the frame_header_obu is identical to the original
          // frame_header_obu.
          if (frame_header_size > payload_size ||
              memcmp(data, frame_header, frame_header_size) != 0) {
            pbi->error.error_code = AOM_CODEC_CORRUPT_FRAME;
            return -1;
          }
          assert(rb.bit_offset == 0);
          rb.bit_offset = 8 * frame_header_size;
        }

        decoded_payload_size = frame_header_size;
        pbi->frame_header_size = frame_header_size;
        cm->cur_frame->temporal_id = obu_header.temporal_layer_id;
        cm->cur_frame->spatial_id = obu_header.spatial_layer_id;

        if (cm->show_existing_frame) {
          if (obu_header.type == OBU_FRAME) {
            pbi->error.error_code = AOM_CODEC_UNSUP_BITSTREAM;
            return -1;
          }
          frame_decoding_finished = 1;
          pbi->seen_frame_header = 0;

          if (cm->show_frame &&
              !cm->seq_params->order_hint_info.enable_order_hint) {
            ++cm->current_frame.frame_number;
          }
          break;
        }

        // In large scale tile coding, decode the common camera frame header
        // before any tile list OBU.
        if (!pbi->ext_tile_debug && pbi->camera_frame_header_ready) {
          frame_decoding_finished = 1;
          // Skip the rest of the frame data.
          decoded_payload_size = payload_size;
          // Update data_end.
          *p_data_end = data_end;
          break;
        }

        if (obu_header.type != OBU_FRAME) break;
        obu_payload_offset = frame_header_size;
        // Byte align the reader before reading the tile group.
        // byte_alignment() has set pbi->error.error_code if it returns -1.
        if (byte_alignment(cm, &rb)) return -1;
        AOM_FALLTHROUGH_INTENDED;  // fall through to read tile group.
      case OBU_TILE_GROUP:
        if (!pbi->seen_frame_header) {
          pbi->error.error_code = AOM_CODEC_CORRUPT_FRAME;
          return -1;
        }
        if (obu_payload_offset > payload_size) {
          pbi->error.error_code = AOM_CODEC_CORRUPT_FRAME;
          return -1;
        }
        decoded_payload_size += read_one_tile_group_obu(
            pbi, &rb, is_first_tg_obu_received, data + obu_payload_offset,
            data + payload_size, p_data_end, &frame_decoding_finished,
            obu_header.type == OBU_FRAME);
        if (pbi->error.error_code != AOM_CODEC_OK) return -1;
        is_first_tg_obu_received = 0;
        if (frame_decoding_finished) {
          pbi->seen_frame_header = 0;
          pbi->next_start_tile = 0;
        }
        pbi->num_tile_groups++;
        break;
      case OBU_METADATA: {
        decoded_payload_size =
            read_metadata(pbi, data, payload_size, obu_header.has_extension);
        if (pbi->error.error_code != AOM_CODEC_OK) return -1;
        break;
      }
      case OBU_TILE_LIST:
        if (CONFIG_NORMAL_TILE_MODE) {
          pbi->error.error_code = AOM_CODEC_UNSUP_BITSTREAM;
          return -1;
        }

        // This OBU type is purely for the large scale tile coding mode.
        // The common camera frame header has to be already decoded.
        if (!pbi->camera_frame_header_ready) {
          pbi->error.error_code = AOM_CODEC_CORRUPT_FRAME;
          return -1;
        }

        cm->tiles.large_scale = 1;
        av1_set_single_tile_decoding_mode(cm);
        decoded_payload_size =
            read_and_decode_one_tile_list(pbi, &rb, data, data + payload_size,
                                          p_data_end, &frame_decoding_finished);
        if (pbi->error.error_code != AOM_CODEC_OK) return -1;
        break;
      case OBU_PADDING:
        decoded_payload_size = read_padding(cm, data, payload_size);
        if (pbi->error.error_code != AOM_CODEC_OK) return -1;
        break;
      default:
        // Skip unrecognized OBUs
        if (payload_size > 0 &&
            get_last_nonzero_byte(data, payload_size) == 0) {
          pbi->error.error_code = AOM_CODEC_CORRUPT_FRAME;
          return -1;
        }
        decoded_payload_size = payload_size;
        break;
    }

    // Check that the signalled OBU size matches the actual amount of data read
    if (decoded_payload_size > payload_size) {
      pbi->error.error_code = AOM_CODEC_CORRUPT_FRAME;
      return -1;
    }

    // If there are extra padding bytes, they should all be zero
    while (decoded_payload_size < payload_size) {
      uint8_t padding_byte = data[decoded_payload_size++];
      if (padding_byte != 0) {
        pbi->error.error_code = AOM_CODEC_CORRUPT_FRAME;
        return -1;
      }
    }

    data += payload_size;
  }

  if (pbi->error.error_code != AOM_CODEC_OK) return -1;
  return frame_decoding_finished;
}
