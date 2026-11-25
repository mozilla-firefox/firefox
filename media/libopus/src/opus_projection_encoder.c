/* Copyright (c) 2017 Google Inc.
   Written by Andrew Allen */
/*
   Redistribution and use in source and binary forms, with or without
   modification, are permitted provided that the following conditions
   are met:

   - Redistributions of source code must retain the above copyright
   notice, this list of conditions and the following disclaimer.

   - Redistributions in binary form must reproduce the above copyright
   notice, this list of conditions and the following disclaimer in the
   documentation and/or other materials provided with the distribution.

   THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
   ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
   LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
   A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER
   OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
   EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
   PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
   PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
   LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
   NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
   SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "mathops.h"
#include "os_support.h"
#include "opus_private.h"
#include "opus_defines.h"
#include "opus_projection.h"
#include "opus_multistream.h"
#include "stack_alloc.h"
#include "mapping_matrix.h"

struct OpusProjectionEncoder
{
  opus_int32 mixing_matrix_size_in_bytes;
  opus_int32 demixing_matrix_size_in_bytes;
  /* Encoder states go here */
};

#if !defined(DISABLE_FLOAT_API)
static void opus_projection_copy_channel_in_float(
  opus_res *dst,
  int dst_stride,
  const void *src,
  int src_stride,
  int src_channel,
  int frame_size,
  void *user_data
)
{
  mapping_matrix_multiply_channel_in_float((const MappingMatrix*)user_data,
    (const float*)src, src_stride, dst, src_channel, dst_stride, frame_size);
}
#endif

static void opus_projection_copy_channel_in_short(
  opus_res *dst,
  int dst_stride,
  const void *src,
  int src_stride,
  int src_channel,
  int frame_size,
  void *user_data
)
{
  mapping_matrix_multiply_channel_in_short((const MappingMatrix*)user_data,
    (const opus_int16*)src, src_stride, dst, src_channel, dst_stride, frame_size);
}

static void opus_projection_copy_channel_in_int24(
  opus_res *dst,
  int dst_stride,
  const void *src,
  int src_stride,
  int src_channel,
  int frame_size,
  void *user_data
)
{
  mapping_matrix_multiply_channel_in_int24((const MappingMatrix*)user_data,
    (const opus_int32*)src, src_stride, dst, src_channel, dst_stride, frame_size);
}

static int get_order_plus_one_from_channels(int channels, int *order_plus_one)
{
  int order_plus_one_;
  int acn_channels;
  int nondiegetic_channels;

  /* Allowed numbers of channels:
   * (1 + n)^2 + 2j, for n = 0...14 and j = 0 or 1.
   */
  if (channels < 1 || channels > 227)
    return OPUS_BAD_ARG;

  order_plus_one_ = isqrt32(channels);
  acn_channels = order_plus_one_ * order_plus_one_;
  nondiegetic_channels = channels - acn_channels;
  if (nondiegetic_channels != 0 && nondiegetic_channels != 2)
    return OPUS_BAD_ARG;

  if (order_plus_one)
    *order_plus_one = order_plus_one_;
  return OPUS_OK;
}

static int get_streams_from_channels(int channels, int mapping_family,
                                     int *streams, int *coupled_streams,
                                     int *order_plus_one)
{
  if (mapping_family == 3)
  {
    if (get_order_plus_one_from_channels(channels, order_plus_one) != OPUS_OK)
      return OPUS_BAD_ARG;
    if (streams)
      *streams = (channels + 1) / 2;
    if (coupled_streams)
      *coupled_streams = channels / 2;
    return OPUS_OK;
  }
  return OPUS_BAD_ARG;
}

static MappingMatrix *get_mixing_matrix(OpusProjectionEncoder *st)
{
  /* void* cast avoids clang -Wcast-align warning */
  return (MappingMatrix *)(void*)((char*)st +
    align(sizeof(OpusProjectionEncoder)));
}

static MappingMatrix *get_enc_demixing_matrix(OpusProjectionEncoder *st)
{
  /* void* cast avoids clang -Wcast-align warning */
  return (MappingMatrix *)(void*)((char*)st +
    align(sizeof(OpusProjectionEncoder) +
    st->mixing_matrix_size_in_bytes));
}

static OpusMSEncoder *get_multistream_encoder(OpusProjectionEncoder *st)
{
  /* void* cast avoids clang -Wcast-align warning */
  return (OpusMSEncoder *)(void*)((char*)st +
    align(sizeof(OpusProjectionEncoder) +
    st->mixing_matrix_size_in_bytes +
    st->demixing_matrix_size_in_bytes));
}

static int get_encoder_size(int channels, int mapping_family,
                            opus_int32 *size)
{
  int nb_streams;
  int nb_coupled_streams;
  int order_plus_one;
  const MappingMatrix *table;
  opus_int32 mixing_matrix_size, demixing_matrix_size;
  opus_int32 encoder_size;
  int ret;

  ret = get_streams_from_channels(channels, mapping_family, &nb_streams,
                                  &nb_coupled_streams, &order_plus_one);
  if (ret != OPUS_OK)
    return ret;

  /* Use the pre-computed matrix for this order, if there is one. */
  ret = mapping_matrix_get_ambisonic(order_plus_one - 1, &table, NULL, NULL);
  if (ret != OPUS_OK)
    return ret;

  mixing_matrix_size = mapping_matrix_get_size(table->rows, table->cols);
  if (!mixing_matrix_size)
    return OPUS_BAD_ARG;

  /* The demixing matrix is the mixing matrix transposed. */
  demixing_matrix_size = mapping_matrix_get_size(table->cols, table->rows);
  if (!demixing_matrix_size)
    return OPUS_BAD_ARG;

  encoder_size =
      opus_multistream_encoder_get_size(nb_streams, nb_coupled_streams);
  if (!encoder_size)
    return OPUS_BAD_ARG;

  *size = align(sizeof(OpusProjectionEncoder)) +
    mixing_matrix_size + demixing_matrix_size + encoder_size;
  return OPUS_OK;
}

opus_int32 opus_projection_ambisonics_encoder_get_size(int channels,
                                                       int mapping_family)
{
  opus_int32 size;

  if (get_encoder_size(channels, mapping_family, &size) != OPUS_OK)
    return 0;
  return size;
}

int opus_projection_ambisonics_encoder_init(OpusProjectionEncoder *st, opus_int32 Fs,
                                            int channels, int mapping_family,
                                            int *streams, int *coupled_streams,
                                            int application)
{
  MappingMatrix *mixing_matrix;
  MappingMatrix *demixing_matrix;
  const MappingMatrix *table;
  const opus_int16 *table_data;
  opus_int32 table_data_size;
  OpusMSEncoder *ms_encoder;
  int i;
  int ret;
  int order_plus_one;
  unsigned char mapping[255];

  if (streams == NULL || coupled_streams == NULL) {
    return OPUS_BAD_ARG;
  }

  if (get_streams_from_channels(channels, mapping_family, streams,
    coupled_streams, &order_plus_one) != OPUS_OK)
    return OPUS_BAD_ARG;

  if (mapping_family == 3)
  {
    /* Assign mixing matrix based on available pre-computed matrices. */
    ret = mapping_matrix_get_ambisonic(order_plus_one - 1, &table, &table_data,
                                       &table_data_size);
    if (ret != OPUS_OK)
      return ret;

    mixing_matrix = get_mixing_matrix(st);
    mapping_matrix_init(mixing_matrix, table->rows, table->cols, table->gain,
      table_data, table_data_size);

    st->mixing_matrix_size_in_bytes = mapping_matrix_get_size(
      mixing_matrix->rows, mixing_matrix->cols);
    if (!st->mixing_matrix_size_in_bytes)
      return OPUS_BAD_ARG;

    /* The mixing matrix is orthogonal and unscaled, so the demixing matrix is its
     * transpose. A table with a gain would need its inverse instead. */
    celt_assert(table->gain == 0);
    demixing_matrix = get_enc_demixing_matrix(st);
    mapping_matrix_init_transposed(demixing_matrix, table->cols, table->rows,
      table->gain, table_data, table_data_size);

    st->demixing_matrix_size_in_bytes = mapping_matrix_get_size(
      demixing_matrix->rows, demixing_matrix->cols);
    if (!st->demixing_matrix_size_in_bytes)
      return OPUS_BAD_ARG;
  }
  else
    return OPUS_UNIMPLEMENTED;

  /* Ensure matrices are large enough for desired coding scheme. */
  if (*streams + *coupled_streams > mixing_matrix->rows ||
      channels > mixing_matrix->cols ||
      channels > demixing_matrix->rows ||
      *streams + *coupled_streams > demixing_matrix->cols)
    return OPUS_BAD_ARG;

  /* Set trivial mapping so each input channel pairs with a matrix column. */
  for (i = 0; i < channels; i++)
    mapping[i] = i;

  /* Initialize multistream encoder with provided settings. */
  ms_encoder = get_multistream_encoder(st);
  ret = opus_multistream_encoder_init(ms_encoder, Fs, channels, *streams,
                                      *coupled_streams, mapping, application);
  return ret;
}

OpusProjectionEncoder *opus_projection_ambisonics_encoder_create(
    opus_int32 Fs, int channels, int mapping_family, int *streams,
    int *coupled_streams, int application, int *error)
{
  opus_int32 size;
  int ret;
  OpusProjectionEncoder *st;

  /* Allocate space for the projection encoder. */
  ret = get_encoder_size(channels, mapping_family, &size);
  if (ret != OPUS_OK) {
    if (error)
      *error = ret;
    return NULL;
  }
  st = (OpusProjectionEncoder *)opus_alloc(size);
  if (!st)
  {
    if (error)
      *error = OPUS_ALLOC_FAIL;
    return NULL;
  }

  /* Initialize projection encoder with provided settings. */
  ret = opus_projection_ambisonics_encoder_init(st, Fs, channels,
     mapping_family, streams, coupled_streams, application);
  if (ret != OPUS_OK)
  {
    opus_free(st);
    st = NULL;
  }
  if (error)
    *error = ret;
  return st;
}

int opus_projection_encode(OpusProjectionEncoder *st, const opus_int16 *pcm,
                           int frame_size, unsigned char *data,
                           opus_int32 max_data_bytes)
{
  return opus_multistream_encode_native(get_multistream_encoder(st),
    opus_projection_copy_channel_in_short, pcm, frame_size, data,
    max_data_bytes, 16, downmix_int, 0, get_mixing_matrix(st));
}

int opus_projection_encode24(OpusProjectionEncoder *st, const opus_int32 *pcm,
                           int frame_size, unsigned char *data,
                           opus_int32 max_data_bytes)
{
  return opus_multistream_encode_native(get_multistream_encoder(st),
    opus_projection_copy_channel_in_int24, pcm, frame_size, data,
    max_data_bytes, MAX_ENCODING_DEPTH, downmix_int, 0, get_mixing_matrix(st));
}

#ifndef DISABLE_FLOAT_API
int opus_projection_encode_float(OpusProjectionEncoder *st, const float *pcm,
                                 int frame_size, unsigned char *data,
                                 opus_int32 max_data_bytes)
{
  return opus_multistream_encode_native(get_multistream_encoder(st),
    opus_projection_copy_channel_in_float, pcm, frame_size, data,
    max_data_bytes, MAX_ENCODING_DEPTH, downmix_float, 1, get_mixing_matrix(st));
}
#endif

void opus_projection_encoder_destroy(OpusProjectionEncoder *st)
{
  opus_free(st);
}

int opus_projection_encoder_ctl(OpusProjectionEncoder *st, int request, ...)
{
  va_list ap;
  MappingMatrix *demixing_matrix;
  OpusMSEncoder *ms_encoder;
  int ret = OPUS_OK;

  ms_encoder = get_multistream_encoder(st);
  demixing_matrix = get_enc_demixing_matrix(st);

  va_start(ap, request);
  switch(request)
  {
  case OPUS_PROJECTION_GET_DEMIXING_MATRIX_SIZE_REQUEST:
  {
    opus_int32 *value = va_arg(ap, opus_int32*);
    if (!value)
    {
      goto bad_arg;
    }
    *value =
      ms_encoder->layout.nb_channels * (ms_encoder->layout.nb_streams
      + ms_encoder->layout.nb_coupled_streams) * sizeof(opus_int16);
  }
  break;
  case OPUS_PROJECTION_GET_DEMIXING_MATRIX_GAIN_REQUEST:
  {
    opus_int32 *value = va_arg(ap, opus_int32*);
    if (!value)
    {
      goto bad_arg;
    }
    *value = demixing_matrix->gain;
  }
  break;
  case OPUS_PROJECTION_GET_DEMIXING_MATRIX_REQUEST:
  {
    int i, j, k, l;
    int nb_input_streams;
    int nb_output_streams;
    unsigned char *external_char;
    opus_int16 *internal_short;
    opus_int32 external_size;
    opus_int32 internal_size;

    /* (I/O is in relation to the decoder's perspective). */
    nb_input_streams = ms_encoder->layout.nb_streams +
      ms_encoder->layout.nb_coupled_streams;
    nb_output_streams = ms_encoder->layout.nb_channels;

    external_char = va_arg(ap, unsigned char *);
    external_size = va_arg(ap, opus_int32);
    if (!external_char)
    {
      goto bad_arg;
    }
    internal_short = mapping_matrix_get_data(demixing_matrix);
    internal_size = nb_input_streams * nb_output_streams * sizeof(opus_int16);
    if (external_size != internal_size)
    {
      goto bad_arg;
    }

    /* Copy demixing matrix subset to output destination. */
    l = 0;
    for (i = 0; i < nb_input_streams; i++) {
      for (j = 0; j < nb_output_streams; j++) {
        k = demixing_matrix->rows * i + j;
        external_char[2*l] = (unsigned char)internal_short[k];
        external_char[2*l+1] = (unsigned char)(internal_short[k] >> 8);
        l++;
      }
    }
  }
  break;
  default:
  {
    ret = opus_multistream_encoder_ctl_va_list(ms_encoder, request, ap);
  }
  break;
  }
  va_end(ap);
  return ret;

bad_arg:
  va_end(ap);
  return OPUS_BAD_ARG;
}
