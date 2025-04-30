/*
 * Copyright (c) 2016, Alliance for Open Media. All rights reserved.
 *
 * This source code is subject to the terms of the BSD 2 Clause License and
 * the Alliance for Open Media Patent License 1.0. If the BSD 2 Clause License
 * was not distributed with this source code in the LICENSE file, you can
 * obtain it at www.aomedia.org/license/software. If the Alliance for Open
 * Media Patent License 1.0 was not distributed with this source code in the
 * PATENTS file, you can obtain it at www.aomedia.org/license/patent.
 */

#include <assert.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>

#include "aom/aom_image.h"
#include "aom/aom_integer.h"
#include "aom/internal/aom_image_internal.h"
#include "aom_mem/aom_mem.h"
#include "aom/aom_codec.h"

static inline unsigned int align_image_dimension(unsigned int d,
                                                 unsigned int subsampling,
                                                 unsigned int size_align) {
  unsigned int align;

  align = (1 << subsampling) - 1;
  align = (size_align - 1 > align) ? (size_align - 1) : align;
  return ((d + align) & ~align);
}

static aom_image_t *img_alloc_helper(
    aom_image_t *img, aom_img_fmt_t fmt, unsigned int d_w, unsigned int d_h,
    unsigned int buf_align, unsigned int stride_align, unsigned int size_align,
    unsigned int border, unsigned char *img_data,
    aom_alloc_img_data_cb_fn_t alloc_cb, void *cb_priv) {
  /* NOTE: In this function, bit_depth is either 8 or 16 (if
   * AOM_IMG_FMT_HIGHBITDEPTH is set), never 10 or 12.
   */
  unsigned int xcs, ycs, bps, bit_depth;

  if (img != NULL) memset(img, 0, sizeof(aom_image_t));

  if (fmt == AOM_IMG_FMT_NONE) goto fail;

  /* Impose maximum values on input parameters so that this function can
   * perform arithmetic operations without worrying about overflows.
   */
  if (d_w > 0x08000000 || d_h > 0x08000000 || buf_align > 65536 ||
      stride_align > 65536 || size_align > 65536 || border > 65536) {
    goto fail;
  }

  /* Treat align==0 like align==1 */
  if (!buf_align) buf_align = 1;

  /* Validate alignment (must be power of 2) */
  if (buf_align & (buf_align - 1)) goto fail;

  /* Treat align==0 like align==1 */
  if (!stride_align) stride_align = 1;

  /* Validate alignment (must be power of 2) */
  if (stride_align & (stride_align - 1)) goto fail;

  /* Treat align==0 like align==1 */
  if (!size_align) size_align = 1;

  /* Validate alignment (must be power of 2) */
  if (size_align & (size_align - 1)) goto fail;

  /* Get sample size for this format */
  switch (fmt) {
    case AOM_IMG_FMT_I420:
    case AOM_IMG_FMT_YV12:
    case AOM_IMG_FMT_NV12:
    case AOM_IMG_FMT_AOMI420:
    case AOM_IMG_FMT_AOMYV12: bps = 12; break;
    case AOM_IMG_FMT_I422: bps = 16; break;
    case AOM_IMG_FMT_I444: bps = 24; break;
    case AOM_IMG_FMT_YV1216:
    case AOM_IMG_FMT_I42016: bps = 24; break;
    case AOM_IMG_FMT_I42216: bps = 32; break;
    case AOM_IMG_FMT_I44416: bps = 48; break;
    default: bps = 16; break;
  }

  bit_depth = (fmt & AOM_IMG_FMT_HIGHBITDEPTH) ? 16 : 8;

  /* Get chroma shift values for this format */
  switch (fmt) {
    case AOM_IMG_FMT_I420:
    case AOM_IMG_FMT_YV12:
    case AOM_IMG_FMT_NV12:
    case AOM_IMG_FMT_AOMI420:
    case AOM_IMG_FMT_AOMYV12:
    case AOM_IMG_FMT_I422:
    case AOM_IMG_FMT_I42016:
    case AOM_IMG_FMT_YV1216:
    case AOM_IMG_FMT_I42216: xcs = 1; break;
    default: xcs = 0; break;
  }

  switch (fmt) {
    case AOM_IMG_FMT_I420:
    case AOM_IMG_FMT_YV12:
    case AOM_IMG_FMT_NV12:
    case AOM_IMG_FMT_AOMI420:
    case AOM_IMG_FMT_AOMYV12:
    case AOM_IMG_FMT_YV1216:
    case AOM_IMG_FMT_I42016: ycs = 1; break;
    default: ycs = 0; break;
  }

  /* Calculate storage sizes given the chroma subsampling */
  const unsigned int w = align_image_dimension(d_w, xcs, size_align);
  assert(d_w <= w);
  const unsigned int h = align_image_dimension(d_h, ycs, size_align);
  assert(d_h <= h);

  uint64_t s = (uint64_t)w + 2 * border;
  s = (fmt & AOM_IMG_FMT_PLANAR) ? s : s * bps / bit_depth;
  s = s * bit_depth / 8;
  s = (s + stride_align - 1) & ~((uint64_t)stride_align - 1);
  if (s > INT_MAX) goto fail;
  const int stride_in_bytes = (int)s;

  /* Allocate the new image */
  if (!img) {
    img = (aom_image_t *)calloc(1, sizeof(aom_image_t));

    if (!img) goto fail;

    img->self_allocd = 1;
  }

  img->img_data = img_data;

  if (!img_data) {
    const uint64_t alloc_size =
        (fmt & AOM_IMG_FMT_PLANAR)
            ? (uint64_t)(h + 2 * border) * stride_in_bytes * bps / bit_depth
            : (uint64_t)(h + 2 * border) * stride_in_bytes;

    if (alloc_size != (size_t)alloc_size) goto fail;

    if (alloc_cb) {
      const size_t padded_alloc_size = (size_t)alloc_size + buf_align - 1;
      img->img_data = (uint8_t *)alloc_cb(cb_priv, padded_alloc_size);
      if (img->img_data) {
        img->img_data = (uint8_t *)aom_align_addr(img->img_data, buf_align);
      }
      img->img_data_owner = 0;
    } else {
      img->img_data = (uint8_t *)aom_memalign(buf_align, (size_t)alloc_size);
      img->img_data_owner = 1;
    }
    img->sz = (size_t)alloc_size;
  }

  if (!img->img_data) goto fail;

  img->fmt = fmt;
  img->bit_depth = bit_depth;
  // aligned width and aligned height
  img->w = w;
  img->h = h;
  img->x_chroma_shift = xcs;
  img->y_chroma_shift = ycs;
  img->bps = bps;

  /* Calculate strides */
  img->stride[AOM_PLANE_Y] = stride_in_bytes;
  img->stride[AOM_PLANE_U] = img->stride[AOM_PLANE_V] = stride_in_bytes >> xcs;

  if (fmt == AOM_IMG_FMT_NV12) {
    // Each row is a row of U and a row of V interleaved, so the stride is twice
    // as long.
    img->stride[AOM_PLANE_U] *= 2;
    img->stride[AOM_PLANE_V] = 0;
  }

  img->cp = AOM_CICP_CP_UNSPECIFIED;
  img->tc = AOM_CICP_TC_UNSPECIFIED;
  img->mc = AOM_CICP_MC_UNSPECIFIED;

  /* Default viewport to entire image. (This aom_img_set_rect call always
   * succeeds.) */
  int ret = aom_img_set_rect(img, 0, 0, d_w, d_h, border);
  assert(ret == 0);
  (void)ret;
  return img;

fail:
  aom_img_free(img);
  return NULL;
}

aom_image_t *aom_img_alloc(aom_image_t *img, aom_img_fmt_t fmt,
                           unsigned int d_w, unsigned int d_h,
                           unsigned int align) {
  return img_alloc_helper(img, fmt, d_w, d_h, align, align, 1, 0, NULL, NULL,
                          NULL);
}

aom_image_t *aom_img_alloc_with_cb(aom_image_t *img, aom_img_fmt_t fmt,
                                   unsigned int d_w, unsigned int d_h,
                                   unsigned int align,
                                   aom_alloc_img_data_cb_fn_t alloc_cb,
                                   void *cb_priv) {
  return img_alloc_helper(img, fmt, d_w, d_h, align, align, 1, 0, NULL,
                          alloc_cb, cb_priv);
}

aom_image_t *aom_img_wrap(aom_image_t *img, aom_img_fmt_t fmt, unsigned int d_w,
                          unsigned int d_h, unsigned int stride_align,
                          unsigned char *img_data) {
  /* Set buf_align = 1. It is ignored by img_alloc_helper because img_data is
   * not NULL. */
  return img_alloc_helper(img, fmt, d_w, d_h, 1, stride_align, 1, 0, img_data,
                          NULL, NULL);
}

aom_image_t *aom_img_alloc_with_border(aom_image_t *img, aom_img_fmt_t fmt,
                                       unsigned int d_w, unsigned int d_h,
                                       unsigned int align,
                                       unsigned int size_align,
                                       unsigned int border) {
  return img_alloc_helper(img, fmt, d_w, d_h, align, align, size_align, border,
                          NULL, NULL, NULL);
}

int aom_img_set_rect(aom_image_t *img, unsigned int x, unsigned int y,
                     unsigned int w, unsigned int h, unsigned int border) {
  if (x <= UINT_MAX - w && x + w <= img->w && y <= UINT_MAX - h &&
      y + h <= img->h) {
    img->d_w = w;
    img->d_h = h;

    x += border;
    y += border;

    /* Calculate plane pointers */
    if (!(img->fmt & AOM_IMG_FMT_PLANAR)) {
      img->planes[AOM_PLANE_PACKED] =
          img->img_data + x * img->bps / 8 + y * img->stride[AOM_PLANE_PACKED];
    } else {
      const int bytes_per_sample =
          (img->fmt & AOM_IMG_FMT_HIGHBITDEPTH) ? 2 : 1;
      unsigned char *data = img->img_data;

      img->planes[AOM_PLANE_Y] =
          data + x * bytes_per_sample + y * img->stride[AOM_PLANE_Y];
      data += ((size_t)img->h + 2 * border) * img->stride[AOM_PLANE_Y];

      unsigned int uv_border_h = border >> img->y_chroma_shift;
      unsigned int uv_x = x >> img->x_chroma_shift;
      unsigned int uv_y = y >> img->y_chroma_shift;
      if (img->fmt == AOM_IMG_FMT_NV12) {
        img->planes[AOM_PLANE_U] = data + uv_x * bytes_per_sample * 2 +
                                   uv_y * img->stride[AOM_PLANE_U];
        img->planes[AOM_PLANE_V] = NULL;
      } else if (!(img->fmt & AOM_IMG_FMT_UV_FLIP)) {
        img->planes[AOM_PLANE_U] =
            data + uv_x * bytes_per_sample + uv_y * img->stride[AOM_PLANE_U];
        data += ((size_t)(img->h >> img->y_chroma_shift) + 2 * uv_border_h) *
                img->stride[AOM_PLANE_U];
        img->planes[AOM_PLANE_V] =
            data + uv_x * bytes_per_sample + uv_y * img->stride[AOM_PLANE_V];
      } else {
        img->planes[AOM_PLANE_V] =
            data + uv_x * bytes_per_sample + uv_y * img->stride[AOM_PLANE_V];
        data += ((size_t)(img->h >> img->y_chroma_shift) + 2 * uv_border_h) *
                img->stride[AOM_PLANE_V];
        img->planes[AOM_PLANE_U] =
            data + uv_x * bytes_per_sample + uv_y * img->stride[AOM_PLANE_U];
      }
    }
    return 0;
  }
  return -1;
}

void aom_img_flip(aom_image_t *img) {
  /* Note: In the calculation pointer adjustment calculation, we want the
   * rhs to be promoted to a signed type. Section 6.3.1.8 of the ISO C99
   * standard indicates that if the adjustment parameter is unsigned, the
   * stride parameter will be promoted to unsigned, causing errors when
   * the lhs is a larger type than the rhs.
   */
  img->planes[AOM_PLANE_Y] += (signed)(img->d_h - 1) * img->stride[AOM_PLANE_Y];
  img->stride[AOM_PLANE_Y] = -img->stride[AOM_PLANE_Y];

  img->planes[AOM_PLANE_U] += (signed)((img->d_h >> img->y_chroma_shift) - 1) *
                              img->stride[AOM_PLANE_U];
  img->stride[AOM_PLANE_U] = -img->stride[AOM_PLANE_U];

  img->planes[AOM_PLANE_V] += (signed)((img->d_h >> img->y_chroma_shift) - 1) *
                              img->stride[AOM_PLANE_V];
  img->stride[AOM_PLANE_V] = -img->stride[AOM_PLANE_V];
}

void aom_img_free(aom_image_t *img) {
  if (img) {
    aom_img_remove_metadata(img);
    if (img->img_data && img->img_data_owner) aom_free(img->img_data);

    if (img->self_allocd) free(img);
  }
}

int aom_img_plane_width(const aom_image_t *img, int plane) {
  if (plane > 0)
    return (img->d_w + img->x_chroma_shift) >> img->x_chroma_shift;
  else
    return img->d_w;
}

int aom_img_plane_height(const aom_image_t *img, int plane) {
  if (plane > 0)
    return (img->d_h + img->y_chroma_shift) >> img->y_chroma_shift;
  else
    return img->d_h;
}

aom_metadata_t *aom_img_metadata_alloc(
    uint32_t type, const uint8_t *data, size_t sz,
    aom_metadata_insert_flags_t insert_flag) {
  if (!data || sz == 0) return NULL;
  aom_metadata_t *metadata = (aom_metadata_t *)malloc(sizeof(aom_metadata_t));
  if (!metadata) return NULL;
  metadata->type = type;
  metadata->payload = (uint8_t *)malloc(sz);
  if (!metadata->payload) {
    free(metadata);
    return NULL;
  }
  memcpy(metadata->payload, data, sz);
  metadata->sz = sz;
  metadata->insert_flag = insert_flag;
  return metadata;
}

void aom_img_metadata_free(aom_metadata_t *metadata) {
  if (metadata) {
    if (metadata->payload) free(metadata->payload);
    free(metadata);
  }
}

aom_metadata_array_t *aom_img_metadata_array_alloc(size_t sz) {
  aom_metadata_array_t *arr =
      (aom_metadata_array_t *)calloc(1, sizeof(aom_metadata_array_t));
  if (!arr) return NULL;
  if (sz > 0) {
    arr->metadata_array =
        (aom_metadata_t **)calloc(sz, sizeof(aom_metadata_t *));
    if (!arr->metadata_array) {
      aom_img_metadata_array_free(arr);
      return NULL;
    }
    arr->sz = sz;
  }
  return arr;
}

void aom_img_metadata_array_free(aom_metadata_array_t *arr) {
  if (arr) {
    if (arr->metadata_array) {
      for (size_t i = 0; i < arr->sz; i++) {
        aom_img_metadata_free(arr->metadata_array[i]);
      }
      free(arr->metadata_array);
    }
    free(arr);
  }
}

int aom_img_add_metadata(aom_image_t *img, uint32_t type, const uint8_t *data,
                         size_t sz, aom_metadata_insert_flags_t insert_flag) {
  if (!img) return -1;
  if (!img->metadata) {
    img->metadata = aom_img_metadata_array_alloc(0);
    if (!img->metadata) return -1;
  }
  // Some metadata types are not allowed to be layer specific, according to
  // the Table in Section 6.7.1 of the AV1 specifiction.
  // Do not check for OBU_METADATA_TYPE_HDR_CLL or OBU_METADATA_TYPE_HDR_MDCV
  // because there are plans to alow them to be layer specific.
  if ((insert_flag & AOM_MIF_LAYER_SPECIFIC) &&
      (type == OBU_METADATA_TYPE_SCALABILITY ||
       type == OBU_METADATA_TYPE_TIMECODE)) {
    return -1;
  }
  aom_metadata_t *metadata =
      aom_img_metadata_alloc(type, data, sz, insert_flag);
  if (!metadata) return -1;
  aom_metadata_t **metadata_array =
      (aom_metadata_t **)realloc(img->metadata->metadata_array,
                                 (img->metadata->sz + 1) * sizeof(metadata));
  if (!metadata_array) {
    aom_img_metadata_free(metadata);
    return -1;
  }
  img->metadata->metadata_array = metadata_array;
  img->metadata->metadata_array[img->metadata->sz] = metadata;
  img->metadata->sz++;
  return 0;
}

void aom_img_remove_metadata(aom_image_t *img) {
  if (img && img->metadata) {
    aom_img_metadata_array_free(img->metadata);
    img->metadata = NULL;
  }
}

const aom_metadata_t *aom_img_get_metadata(const aom_image_t *img,
                                           size_t index) {
  if (!img) return NULL;
  const aom_metadata_array_t *array = img->metadata;
  if (array && index < array->sz) {
    return array->metadata_array[index];
  }
  return NULL;
}

size_t aom_img_num_metadata(const aom_image_t *img) {
  if (!img || !img->metadata) return 0;
  return img->metadata->sz;
}
