/*
 * Copyright (c) 2021, Alliance for Open Media. All rights reserved.
 *
 * This source code is subject to the terms of the BSD 2 Clause License and
 * the Alliance for Open Media Patent License 1.0. If the BSD 2 Clause License
 * was not distributed with this source code in the LICENSE file, you can
 * obtain it at www.aomedia.org/license/software. If the Alliance for Open
 * Media Patent License 1.0 was not distributed with this source code in the
 * PATENTS file, you can obtain it at www.aomedia.org/license/patent.
 */

#include <math.h>

#include "av1/encoder/tune_butteraugli.h"

#include "aom_dsp/butteraugli.h"
#include "av1/encoder/encodeframe.h"
#include "av1/encoder/encoder_utils.h"
#include "av1/encoder/extend.h"
#include "av1/encoder/var_based_part.h"

static const int resize_factor = 2;

static void set_mb_butteraugli_rdmult_scaling(AV1_COMP *cpi,
                                              const YV12_BUFFER_CONFIG *source,
                                              const YV12_BUFFER_CONFIG *recon,
                                              const double K) {
  AV1_COMMON *const cm = &cpi->common;
  SequenceHeader *const seq_params = cm->seq_params;
  const CommonModeInfoParams *const mi_params = &cm->mi_params;
  const aom_color_range_t color_range =
      seq_params->color_range != 0 ? AOM_CR_FULL_RANGE : AOM_CR_STUDIO_RANGE;
  const int bit_depth = cpi->td.mb.e_mbd.bd;
  const int width = source->y_crop_width;
  const int height = source->y_crop_height;
  const int ss_x = source->subsampling_x;
  const int ss_y = source->subsampling_y;

  float *diffmap;
  CHECK_MEM_ERROR(cm, diffmap, aom_malloc(width * height * sizeof(*diffmap)));
  if (!aom_calc_butteraugli(source, recon, bit_depth,
                            seq_params->matrix_coefficients, color_range,
                            diffmap)) {
    aom_internal_error(cm->error, AOM_CODEC_ERROR,
                       "Failed to calculate Butteraugli distances.");
  }

  const int num_mi_w = mi_size_wide[butteraugli_rdo_bsize] / resize_factor;
  const int num_mi_h = mi_size_high[butteraugli_rdo_bsize] / resize_factor;
  const int num_cols =
      (mi_params->mi_cols / resize_factor + num_mi_w - 1) / num_mi_w;
  const int num_rows =
      (mi_params->mi_rows / resize_factor + num_mi_h - 1) / num_mi_h;
  const int block_w = num_mi_w << 2;
  const int block_h = num_mi_h << 2;
  double log_sum = 0.0;
  double blk_count = 0.0;

  // Loop through each block.
  for (int row = 0; row < num_rows; ++row) {
    for (int col = 0; col < num_cols; ++col) {
      const int index = row * num_cols + col;
      const int y_start = row * block_h;
      const int x_start = col * block_w;
      float dbutteraugli = 0.0f;
      float dmse = 0.0f;
      float px_count = 0.0f;

      // Loop through each pixel.
      for (int y = y_start; y < y_start + block_h && y < height; y++) {
        for (int x = x_start; x < x_start + block_w && x < width; x++) {
          dbutteraugli += powf(diffmap[y * width + x], 12.0f);
          float px_diff = source->y_buffer[y * source->y_stride + x] -
                          recon->y_buffer[y * recon->y_stride + x];
          dmse += px_diff * px_diff;
          px_count += 1.0f;
        }
      }
      const int y_end = AOMMIN((y_start >> ss_y) + (block_h >> ss_y),
                               (height + ss_y) >> ss_y);
      for (int y = y_start >> ss_y; y < y_end; y++) {
        const int x_end = AOMMIN((x_start >> ss_x) + (block_w >> ss_x),
                                 (width + ss_x) >> ss_x);
        for (int x = x_start >> ss_x; x < x_end; x++) {
          const int src_px_index = y * source->uv_stride + x;
          const int recon_px_index = y * recon->uv_stride + x;
          const float px_diff_u = (float)(source->u_buffer[src_px_index] -
                                          recon->u_buffer[recon_px_index]);
          const float px_diff_v = (float)(source->v_buffer[src_px_index] -
                                          recon->v_buffer[recon_px_index]);
          dmse += px_diff_u * px_diff_u + px_diff_v * px_diff_v;
          px_count += 2.0f;
        }
      }

      dbutteraugli = powf(dbutteraugli, 1.0f / 12.0f);
      dmse = dmse / px_count;
      const float eps = 0.01f;
      double weight;
      if (dbutteraugli < eps || dmse < eps) {
        weight = -1.0;
      } else {
        blk_count += 1.0;
        weight = dmse / dbutteraugli;
        weight = AOMMIN(weight, 5.0);
        weight += K;
        log_sum += log(weight);
      }
      cpi->butteraugli_info.rdmult_scaling_factors[index] = weight;
    }
  }
  // Geometric average of the weights.
  log_sum = exp(log_sum / blk_count);

  for (int row = 0; row < num_rows; ++row) {
    for (int col = 0; col < num_cols; ++col) {
      const int index = row * num_cols + col;
      double *weight = &cpi->butteraugli_info.rdmult_scaling_factors[index];
      if (*weight <= 0.0) {
        *weight = 1.0;
      } else {
        *weight /= log_sum;
      }
      *weight = AOMMIN(*weight, 2.5);
      *weight = AOMMAX(*weight, 0.4);
    }
  }

  aom_free(diffmap);
}

void av1_set_butteraugli_rdmult(const AV1_COMP *cpi, MACROBLOCK *x,
                                BLOCK_SIZE bsize, int mi_row, int mi_col,
                                int *rdmult) {
  assert(cpi->oxcf.tune_cfg.tuning == AOM_TUNE_BUTTERAUGLI);
  if (!cpi->butteraugli_info.recon_set) {
    return;
  }
  const AV1_COMMON *const cm = &cpi->common;

  const int num_mi_w = mi_size_wide[butteraugli_rdo_bsize];
  const int num_mi_h = mi_size_high[butteraugli_rdo_bsize];
  const int num_cols = (cm->mi_params.mi_cols + num_mi_w - 1) / num_mi_w;
  const int num_rows = (cm->mi_params.mi_rows + num_mi_h - 1) / num_mi_h;
  const int num_bcols = (mi_size_wide[bsize] + num_mi_w - 1) / num_mi_w;
  const int num_brows = (mi_size_high[bsize] + num_mi_h - 1) / num_mi_h;
  double num_of_mi = 0.0;
  double geom_mean_of_scale = 0.0;

  for (int row = mi_row / num_mi_w;
       row < num_rows && row < mi_row / num_mi_w + num_brows; ++row) {
    for (int col = mi_col / num_mi_h;
         col < num_cols && col < mi_col / num_mi_h + num_bcols; ++col) {
      const int index = row * num_cols + col;
      geom_mean_of_scale +=
          log(cpi->butteraugli_info.rdmult_scaling_factors[index]);
      num_of_mi += 1.0;
    }
  }
  geom_mean_of_scale = exp(geom_mean_of_scale / num_of_mi);

  *rdmult = (int)((double)(*rdmult) * geom_mean_of_scale + 0.5);
  *rdmult = AOMMAX(*rdmult, 0);
  av1_set_error_per_bit(&x->errorperbit, *rdmult);
}

static void copy_plane(const uint8_t *src, int src_stride, uint8_t *dst,
                       int dst_stride, int w, int h) {
  for (int row = 0; row < h; row++) {
    memcpy(dst, src, w);
    src += src_stride;
    dst += dst_stride;
  }
}

static void copy_img(const YV12_BUFFER_CONFIG *src, YV12_BUFFER_CONFIG *dst,
                     int width, int height) {
  copy_plane(src->y_buffer, src->y_stride, dst->y_buffer, dst->y_stride, width,
             height);
  const int width_uv = (width + src->subsampling_x) >> src->subsampling_x;
  const int height_uv = (height + src->subsampling_y) >> src->subsampling_y;
  copy_plane(src->u_buffer, src->uv_stride, dst->u_buffer, dst->uv_stride,
             width_uv, height_uv);
  copy_plane(src->v_buffer, src->uv_stride, dst->v_buffer, dst->uv_stride,
             width_uv, height_uv);
}

static void zero_plane(uint8_t *dst, int dst_stride, int h) {
  for (int row = 0; row < h; row++) {
    memset(dst, 0, dst_stride);
    dst += dst_stride;
  }
}

static void zero_img(YV12_BUFFER_CONFIG *dst) {
  zero_plane(dst->y_buffer, dst->y_stride, dst->y_height);
  zero_plane(dst->u_buffer, dst->uv_stride, dst->uv_height);
  zero_plane(dst->v_buffer, dst->uv_stride, dst->uv_height);
}

void av1_setup_butteraugli_source(AV1_COMP *cpi) {
  YV12_BUFFER_CONFIG *const dst = &cpi->butteraugli_info.source;
  AV1_COMMON *const cm = &cpi->common;
  const int width = cpi->source->y_crop_width;
  const int height = cpi->source->y_crop_height;
  const int bit_depth = cpi->td.mb.e_mbd.bd;
  const int ss_x = cpi->source->subsampling_x;
  const int ss_y = cpi->source->subsampling_y;
  if (dst->buffer_alloc_sz == 0) {
    aom_alloc_frame_buffer(
        dst, width, height, ss_x, ss_y, cm->seq_params->use_highbitdepth,
        cpi->oxcf.border_in_pixels, cm->features.byte_alignment, false, 0);
  }
  av1_copy_and_extend_frame(cpi->source, dst);

  YV12_BUFFER_CONFIG *const resized_dst = &cpi->butteraugli_info.resized_source;
  if (resized_dst->buffer_alloc_sz == 0) {
    aom_alloc_frame_buffer(
        resized_dst, width / resize_factor, height / resize_factor, ss_x, ss_y,
        cm->seq_params->use_highbitdepth, cpi->oxcf.border_in_pixels,
        cm->features.byte_alignment, false, 0);
  }
  if (!av1_resize_and_extend_frame_nonnormative(
          cpi->source, resized_dst, bit_depth, av1_num_planes(cm))) {
    aom_internal_error(cm->error, AOM_CODEC_MEM_ERROR,
                       "Error allocating buffers during resize");
  }

  zero_img(cpi->source);
  copy_img(resized_dst, cpi->source, width / resize_factor,
           height / resize_factor);
}

void av1_setup_butteraugli_rdmult_and_restore_source(AV1_COMP *cpi, double K) {
  av1_copy_and_extend_frame(&cpi->butteraugli_info.source, cpi->source);
  AV1_COMMON *const cm = &cpi->common;
  const int width = cpi->source->y_crop_width;
  const int height = cpi->source->y_crop_height;
  const int ss_x = cpi->source->subsampling_x;
  const int ss_y = cpi->source->subsampling_y;

  YV12_BUFFER_CONFIG resized_recon;
  memset(&resized_recon, 0, sizeof(resized_recon));
  aom_alloc_frame_buffer(
      &resized_recon, width / resize_factor, height / resize_factor, ss_x, ss_y,
      cm->seq_params->use_highbitdepth, cpi->oxcf.border_in_pixels,
      cm->features.byte_alignment, false, 0);
  copy_img(&cpi->common.cur_frame->buf, &resized_recon, width / resize_factor,
           height / resize_factor);

  set_mb_butteraugli_rdmult_scaling(cpi, &cpi->butteraugli_info.resized_source,
                                    &resized_recon, K);
  cpi->butteraugli_info.recon_set = true;
  aom_free_frame_buffer(&resized_recon);
}

void av1_setup_butteraugli_rdmult(AV1_COMP *cpi) {
  AV1_COMMON *const cm = &cpi->common;
  const AV1EncoderConfig *const oxcf = &cpi->oxcf;
  const QuantizationCfg *const q_cfg = &oxcf->q_cfg;
  const int q_index = 96;

  // Setup necessary params for encoding, including frame source, etc.
  if (cm->current_frame.frame_type == KEY_FRAME) copy_frame_prob_info(cpi);
  av1_set_frame_size(cpi, cm->superres_upscaled_width,
                     cm->superres_upscaled_height);

  cpi->source = av1_realloc_and_scale_if_required(
      cm, cpi->unscaled_source, &cpi->scaled_source, cm->features.interp_filter,
      0, false, false, cpi->oxcf.border_in_pixels, cpi->alloc_pyramid);
  if (cpi->unscaled_last_source != NULL) {
    cpi->last_source = av1_realloc_and_scale_if_required(
        cm, cpi->unscaled_last_source, &cpi->scaled_last_source,
        cm->features.interp_filter, 0, false, false, cpi->oxcf.border_in_pixels,
        cpi->alloc_pyramid);
  }

  av1_setup_butteraugli_source(cpi);
  av1_setup_frame(cpi);

  if (cm->seg.enabled) {
    if (!cm->seg.update_data && cm->prev_frame) {
      segfeatures_copy(&cm->seg, &cm->prev_frame->seg);
      cm->seg.enabled = cm->prev_frame->seg.enabled;
    } else {
      av1_calculate_segdata(&cm->seg);
    }
  } else {
    memset(&cm->seg, 0, sizeof(cm->seg));
  }
  segfeatures_copy(&cm->cur_frame->seg, &cm->seg);
  cm->cur_frame->seg.enabled = cm->seg.enabled;

  const PARTITION_SEARCH_TYPE partition_search_type =
      cpi->sf.part_sf.partition_search_type;
  const BLOCK_SIZE fixed_partition_size = cpi->sf.part_sf.fixed_partition_size;
  // Enable a quicker pass by uncommenting the following lines:
  // cpi->sf.part_sf.partition_search_type = FIXED_PARTITION;
  // cpi->sf.part_sf.fixed_partition_size = BLOCK_32X32;

  av1_set_quantizer(cm, q_cfg->qm_minlevel, q_cfg->qm_maxlevel, q_index,
                    q_cfg->enable_chroma_deltaq, q_cfg->enable_hdr_deltaq,
                    oxcf->mode == ALLINTRA, oxcf->tune_cfg.tuning);
  av1_set_speed_features_qindex_dependent(cpi, oxcf->speed);
  av1_init_quantizer(&cpi->enc_quant_dequant_params, &cm->quant_params,
                     cm->seq_params->bit_depth, cpi->oxcf.algo_cfg.sharpness);

  av1_set_variance_partition_thresholds(cpi, q_index, 0);
  av1_encode_frame(cpi);

  av1_setup_butteraugli_rdmult_and_restore_source(cpi, 0.3);
  cpi->sf.part_sf.partition_search_type = partition_search_type;
  cpi->sf.part_sf.fixed_partition_size = fixed_partition_size;
}
