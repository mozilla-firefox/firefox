// Copyright (c) 2010 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// This webpage shows layout of YV12 and other YUV formats
// http://www.fourcc.org/yuv.php
// The actual conversion is best described here
// http://en.wikipedia.org/wiki/YUV
// An article on optimizing YUV conversion using tables instead of multiplies
// http://lestourtereaux.free.fr/papers/data/yuvrgb.pdf
//
// YV12 is a full plane of Y and a half height, half width chroma planes
// YV16 is a full plane of Y and a full height, half width chroma planes
// YV24 is a full plane of Y and a full height, full width chroma planes
// Y8   is a full plane of Y and no chroma planes (i.e., monochrome)
//
// ARGB pixel format is output, which on little endian is stored as BGRA.
// The alpha is set to 255, allowing the application to use RGBA or RGB32.

#include "yuv_convert.h"

#include "libyuv.h"
#include "mozilla/IntegerRange.h"
#include "mozilla/SSE.h"
#include "mozilla/StaticPrefs_gfx.h"
#include "scale_yuv_argb.h"
// Header for low level row functions.
#include "yuv_row.h"

namespace mozilla {

namespace gfx {

// 16.16 fixed point arithmetic
const int kFractionBits = 16;
const int kFractionMax = 1 << kFractionBits;
const int kFractionMask = ((1 << kFractionBits) - 1);

// clang-format off

nsresult ToNSResult(int aLibyuvResult) {
  // Docs for libyuv::ConvertToI420 say:
  // Returns 0 for successful; -1 for invalid parameter. Non-zero for failure.
  switch (aLibyuvResult) {
    case 0:
      return NS_OK;
    case -1:
      return NS_ERROR_INVALID_ARG;
    default:
      return NS_ERROR_FAILURE;
  }
}

libyuv::FourCC FourCCFromYUVType(YUVType aYUVType) {
  switch (aYUVType) {
    case YV24: return libyuv::FOURCC_I444;
    case YV16: return libyuv::FOURCC_I422;
    case YV12: return libyuv::FOURCC_I420;
    case   Y8: return libyuv::FOURCC_I400;
    default:   return libyuv::FOURCC_ANY;
  }
}

int GBRPlanarToARGB(const uint8_t* src_y, int y_pitch,
                     const uint8_t* src_u, int u_pitch,
                     const uint8_t* src_v, int v_pitch,
                     uint8_t* rgb_buf, int rgb_pitch,
                     int pic_width, int pic_height) {
  // libyuv has no native conversion function for this
  // fixme: replace with something less awful
  for (const auto row : IntegerRange(pic_height)) {
    for (const auto col : IntegerRange(pic_width)) {
      rgb_buf[rgb_pitch * row + col * 4 + 0] = src_u[u_pitch * row + col];
      rgb_buf[rgb_pitch * row + col * 4 + 1] = src_y[y_pitch * row + col];
      rgb_buf[rgb_pitch * row + col * 4 + 2] = src_v[v_pitch * row + col];
      rgb_buf[rgb_pitch * row + col * 4 + 3] = 255;
    }
  }
  return 0;
}

// Convert a frame of YUV to 32 bit ARGB or ABGR.
nsresult
ConvertYCbCrToRGB32(const uint8_t* y_buf,
                    const uint8_t* u_buf,
                    const uint8_t* v_buf,
                    uint8_t* rgb_buf,
                    int pic_x,
                    int pic_y,
                    int pic_width,
                    int pic_height,
                    int y_pitch,
                    int uv_pitch,
                    int rgb_pitch,
                    YUVType yuv_type,
                    YUVColorSpace yuv_color_space,
                    ColorRange color_range,
                    RGB32Type rgb32_type) {
  if (pic_x < 0 || pic_y < 0 || y_pitch < 0 || uv_pitch < 0 || rgb_pitch < 0) {
    NS_WARNING("Negative origin or pitch is unsupported");
    return  NS_ERROR_NOT_IMPLEMENTED;
  }

  // Deprecated function's conversion is accurate.
  // libyuv converion is a bit inaccurate to get performance. It dynamically
  // calculates RGB from YUV to use simd. In it, signed byte is used for
  // conversion's coefficient, but it requests 129. libyuv cut 129 to 127. And
  // only 6 bits are used for a decimal part during the dynamic calculation.
  //
  // The function is still fast on some old intel chips.
  // See Bug 1256475.
  bool use_deprecated = StaticPrefs::gfx_ycbcr_accurate_conversion() ||
                        (supports_mmx() && supports_sse() && !supports_sse3() &&
                         yuv_color_space == YUVColorSpace::BT601 &&
                         color_range == ColorRange::LIMITED);
  // The deprecated function only support BT601.
  // See Bug 1210357.
  // It also does not support Y8, which has no chroma planes.
  if (yuv_color_space != YUVColorSpace::BT601 || yuv_type == Y8) {
    use_deprecated = false;
  }
  if (use_deprecated) {
    return ConvertYCbCrToRGB32_deprecated(
        y_buf, u_buf, v_buf, rgb_buf, pic_x, pic_y, pic_width, pic_height,
        y_pitch, uv_pitch, rgb_pitch, yuv_type, rgb32_type);
  }

  decltype(libyuv::I420ToARGBMatrix)* fConvertYUVToARGB = nullptr;
  const uint8_t* src_y = nullptr;
  const uint8_t* src_u = nullptr;
  const uint8_t* src_v = nullptr;
  const libyuv::YuvConstants* yuv_constant = nullptr;
  bool swap_uv = rgb32_type == RGB32Type::ABGR;

  switch (yuv_color_space) {
    case YUVColorSpace::BT2020:
      yuv_constant = color_range == ColorRange::LIMITED
        ? swap_uv? &libyuv::kYvu2020Constants : &libyuv::kYuv2020Constants
        : swap_uv? &libyuv::kYvuV2020Constants : &libyuv::kYuvV2020Constants;
      break;
    case YUVColorSpace::BT709:
      yuv_constant = color_range == ColorRange::LIMITED
        ? swap_uv? &libyuv::kYvuH709Constants : &libyuv::kYuvH709Constants
        : swap_uv? &libyuv::kYvuF709Constants : &libyuv::kYuvF709Constants;
      break;
    case YUVColorSpace::Identity:
      if (yuv_type == YV24) {
        break;
      }
      NS_WARNING("Identity (aka RGB) with chroma subsampling is unsupported");
      return NS_ERROR_NOT_IMPLEMENTED;
      // TODO: Consider using BT601 for unsupported input?
    default:
      MOZ_FALLTHROUGH_ASSERT("Unsupported YUVColorSpace");
    case YUVColorSpace::BT601:
      yuv_constant = color_range == ColorRange::LIMITED
        ? swap_uv? &libyuv::kYvuI601Constants : &libyuv::kYuvI601Constants
        : swap_uv? &libyuv::kYvuJPEGConstants : &libyuv::kYuvJPEGConstants;
      break;
  }

  switch (yuv_type) {
    case YV24: {
      src_y = y_buf + y_pitch * pic_y + pic_x;
      src_u = u_buf + uv_pitch * pic_y + pic_x;
      src_v = v_buf + uv_pitch * pic_y + pic_x;

      if (yuv_color_space == YUVColorSpace::Identity) {
        const uint8_t* u_channel = swap_uv? src_v : src_u;
        const uint8_t* v_channel = swap_uv? src_u : src_v;
        // Special case for RGB image
        return ToNSResult(GBRPlanarToARGB(src_y, y_pitch, u_channel, uv_pitch, v_channel,
                          uv_pitch, rgb_buf, rgb_pitch, pic_width, pic_height));
      }

      fConvertYUVToARGB = libyuv::I444ToARGBMatrix;
      break;
    }
    case YV16: {
      src_y = y_buf + y_pitch * pic_y + pic_x;
      // Only used when pic_x is even; odd pic_x is handled below.
      src_u = u_buf + uv_pitch * pic_y + pic_x / 2;
      src_v = v_buf + uv_pitch * pic_y + pic_x / 2;

      fConvertYUVToARGB = libyuv::I422ToARGBMatrix;
      break;
    }
    case YV12: {
      src_y = y_buf + y_pitch * pic_y + pic_x;
      // Only used when pic_x and pic_y are both even; odd values are handled
      // below.
      src_u = u_buf + uv_pitch * (pic_y / 2) + pic_x / 2;
      src_v = v_buf + uv_pitch * (pic_y / 2) + pic_x / 2;

      fConvertYUVToARGB = libyuv::I420ToARGBMatrix;
      break;
    }
    case Y8: {
      src_y = y_buf + y_pitch * pic_y + pic_x;
      MOZ_ASSERT(u_buf == nullptr);
      MOZ_ASSERT(v_buf == nullptr);

      if (color_range == ColorRange::LIMITED) {
        return ToNSResult(libyuv::I400ToARGB(src_y, y_pitch, rgb_buf, rgb_pitch,
                                             pic_width, pic_height));
      }
      return ToNSResult(libyuv::J400ToARGB(src_y, y_pitch, rgb_buf, rgb_pitch,
                                           pic_width, pic_height));
    }
    default:
      MOZ_ASSERT_UNREACHABLE("Unsupported YUV type");
      return NS_ERROR_NOT_IMPLEMENTED;
  }

  const uint8_t* u_channel = swap_uv? src_v : src_u;
  const uint8_t* v_channel = swap_uv? src_u : src_v;

  // libyuv handles odd crop widths and heights correctly via its _Any_ row
  // variants, but cannot handle an odd pic_x or pic_y for subsampled formats.
  // For YV12/YV16 an odd pic_x means the Y plane starts at a position that
  // doesn't align with a chroma column boundary; for YV12, odd pic_y has the
  // same problem vertically. libyuv would then pair each Y sample with the
  // wrong chroma, affecting every pixel in the image.
  //
  // Fix: split the output into up to 4 non-overlapping regions, each with
  // even-aligned source coordinates. Integer division of an odd sx or sy by 2
  // floors to the correct chroma index (both luma positions in a 2x2 block
  // share one chroma sample), so all regions are correct.
  //
  // With dx = pic_x & 1 and dy = pic_y & 1 (each 0 or 1):
  //
  //   +------------+--------------------+
  //   | dx x dy    | (W-dx) x dy        |  <- only when dy > 0
  //   +------------+--------------------+
  //   | dx x (H-dy)| (W-dx) x (H-dy)    |  <- left col only when dx > 0
  //   +------------+--------------------+

  // dx/dy: 1 if pic_x/pic_y is odd and the format has chroma subsampling in
  // that axis, 0 otherwise.
  int dx = (yuv_type == YV12 || yuv_type == YV16) ? (pic_x & 1) : 0;
  int dy = (yuv_type == YV12) ? (pic_y & 1) : 0;
  if (dx | dy) {
    // Converts source sub-region (sx, sy, w, h) to the area at dst in rgb_buf.
    auto convert = [&](int sx, int sy, int w, int h, uint8_t* dst) -> nsresult {
      if (w <= 0 || h <= 0) {
        return NS_OK;
      }
      const uint8_t* py = y_buf + sy * y_pitch + sx;
      const uint8_t* pu;
      const uint8_t* pv;
      if (yuv_type == YV12) {
        pu = u_buf + (sy / 2) * uv_pitch + sx / 2;
        pv = v_buf + (sy / 2) * uv_pitch + sx / 2;
      } else {  // YV16: full-height chroma planes, only horizontally subsampled.
        pu = u_buf + sy * uv_pitch + sx / 2;
        pv = v_buf + sy * uv_pitch + sx / 2;
      }
      const uint8_t* uc = swap_uv ? pv : pu;
      const uint8_t* vc = swap_uv ? pu : pv;
      return ToNSResult(fConvertYUVToARGB(py, y_pitch, uc, uv_pitch, vc, uv_pitch,
                                          dst, rgb_pitch, yuv_constant, w, h));
    };
    if (dy) {
      // First output row (source row pic_y is odd).
      if (dx) {
        // Corner pixel: source (pic_x, pic_y) → output top-left.
        nsresult rv = convert(pic_x, pic_y, 1, 1, rgb_buf);
        if (NS_FAILED(rv)) {
          return rv;
        }
      }
      // Remaining columns of first row; source x pic_x+dx is even.
      nsresult rv = convert(pic_x + dx, pic_y, pic_width - dx, 1, rgb_buf + dx * 4);
      if (NS_FAILED(rv)) {
        return rv;
      }
    }
    int sy = pic_y + dy;  // even-aligned source y for remaining output rows
    int h = pic_height - dy;
    if (dx) {
      // Left column of remaining rows; width=1 handles the odd source x.
      nsresult rv = convert(pic_x, sy, 1, h, rgb_buf + dy * rgb_pitch);
      if (NS_FAILED(rv)) {
        return rv;
      }
    }
    // Main body: source (pic_x+dx, sy), both even-aligned.
    return convert(pic_x + dx, sy, pic_width - dx, h,
                   rgb_buf + dy * rgb_pitch + dx * 4);
  }

  return ToNSResult(fConvertYUVToARGB(src_y, y_pitch, u_channel, uv_pitch,
                                      v_channel, uv_pitch, rgb_buf, rgb_pitch,
                                      yuv_constant, pic_width, pic_height));
}

// Convert a frame of YUV to 32 bit ARGB or ABGR.
nsresult
ConvertYCbCrToRGB32_deprecated(const uint8_t* y_buf,
                               const uint8_t* u_buf,
                               const uint8_t* v_buf,
                               uint8_t* rgb_buf,
                               int pic_x,
                               int pic_y,
                               int pic_width,
                               int pic_height,
                               int y_pitch,
                               int uv_pitch,
                               int rgb_pitch,
                               YUVType yuv_type,
                               RGB32Type rgb32_type) {
  unsigned int y_shift = yuv_type == YV12 ? 1 : 0;
  unsigned int x_shift = yuv_type == YV24 ? 0 : 1;
  // Test for SSE because the optimized code uses movntq, which is not part of MMX.
  bool has_sse = supports_mmx() && supports_sse();
  // There is no optimized YV24 SSE routine so we check for this and
  // fall back to the C code.
  has_sse &= yuv_type != YV24;
  bool odd_pic_x = yuv_type != YV24 && pic_x % 2 != 0;
  int x_width = odd_pic_x ? pic_width - 1 : pic_width;
  bool swap_uv = rgb32_type == RGB32Type::ABGR;
  const uint8_t* u_channel = swap_uv? v_buf : u_buf;
  const uint8_t* v_channel = swap_uv? u_buf : v_buf;

  for (int y = pic_y; y < pic_height + pic_y; ++y) {
    uint8_t* rgb_row = rgb_buf + (y - pic_y) * rgb_pitch;
    const uint8_t* y_ptr = y_buf + y * y_pitch + pic_x;
    const uint8_t* u_ptr = u_channel + (y >> y_shift) * uv_pitch + (pic_x >> x_shift);
    const uint8_t* v_ptr = v_channel + (y >> y_shift) * uv_pitch + (pic_x >> x_shift);

    if (odd_pic_x) {
      // Handle the single odd pixel manually and use the
      // fast routines for the remaining.
      FastConvertYUVToRGB32Row_C(y_ptr++,
                                 u_ptr++,
                                 v_ptr++,
                                 rgb_row,
                                 1,
                                 x_shift);
      rgb_row += 4;
    }

    if (has_sse) {
      FastConvertYUVToRGB32Row(y_ptr,
                               u_ptr,
                               v_ptr,
                               rgb_row,
                               x_width);
    }
    else {
      FastConvertYUVToRGB32Row_C(y_ptr,
                                 u_ptr,
                                 v_ptr,
                                 rgb_row,
                                 x_width,
                                 x_shift);
    }
  }

  // MMX used for FastConvertYUVToRGB32Row requires emms instruction.
  if (has_sse)
    EMMS();

  return NS_OK;
}

// C version does 8 at a time to mimic MMX code
static void FilterRows_C(uint8_t* ybuf, const uint8_t* y0_ptr, const uint8_t* y1_ptr,
                         int source_width, int source_y_fraction) {
  int y1_fraction = source_y_fraction;
  int y0_fraction = 256 - y1_fraction;
  uint8_t* end = ybuf + source_width;
  do {
    ybuf[0] = (y0_ptr[0] * y0_fraction + y1_ptr[0] * y1_fraction) >> 8;
    ybuf[1] = (y0_ptr[1] * y0_fraction + y1_ptr[1] * y1_fraction) >> 8;
    ybuf[2] = (y0_ptr[2] * y0_fraction + y1_ptr[2] * y1_fraction) >> 8;
    ybuf[3] = (y0_ptr[3] * y0_fraction + y1_ptr[3] * y1_fraction) >> 8;
    ybuf[4] = (y0_ptr[4] * y0_fraction + y1_ptr[4] * y1_fraction) >> 8;
    ybuf[5] = (y0_ptr[5] * y0_fraction + y1_ptr[5] * y1_fraction) >> 8;
    ybuf[6] = (y0_ptr[6] * y0_fraction + y1_ptr[6] * y1_fraction) >> 8;
    ybuf[7] = (y0_ptr[7] * y0_fraction + y1_ptr[7] * y1_fraction) >> 8;
    y0_ptr += 8;
    y1_ptr += 8;
    ybuf += 8;
  } while (ybuf < end);
}

#ifdef MOZILLA_MAY_SUPPORT_MMX
void FilterRows_MMX(uint8_t* ybuf, const uint8_t* y0_ptr, const uint8_t* y1_ptr,
                    int source_width, int source_y_fraction);
#endif

#ifdef MOZILLA_MAY_SUPPORT_SSE2
void FilterRows_SSE2(uint8_t* ybuf, const uint8_t* y0_ptr, const uint8_t* y1_ptr,
                     int source_width, int source_y_fraction);
#endif

static inline void FilterRows(uint8_t* ybuf, const uint8_t* y0_ptr,
                              const uint8_t* y1_ptr, int source_width,
                              int source_y_fraction) {
#ifdef MOZILLA_MAY_SUPPORT_SSE2
  if (mozilla::supports_sse2()) {
    FilterRows_SSE2(ybuf, y0_ptr, y1_ptr, source_width, source_y_fraction);
    return;
  }
#endif

#ifdef MOZILLA_MAY_SUPPORT_MMX
  if (mozilla::supports_mmx()) {
    FilterRows_MMX(ybuf, y0_ptr, y1_ptr, source_width, source_y_fraction);
    return;
  }
#endif

  FilterRows_C(ybuf, y0_ptr, y1_ptr, source_width, source_y_fraction);
}


// Scale a frame of YUV to 32 bit ARGB.
nsresult
ScaleYCbCrToRGB32(const uint8_t* y_buf,
                  const uint8_t* u_buf,
                  const uint8_t* v_buf,
                  uint8_t* rgb_buf,
                  int source_width,
                  int source_height,
                  int width,
                  int height,
                  int y_pitch,
                  int uv_pitch,
                  int rgb_pitch,
                  YUVType yuv_type,
                  YUVColorSpace yuv_color_space,
                  ScaleFilter filter) {
  bool use_deprecated =
      StaticPrefs::gfx_ycbcr_accurate_conversion() ||
#if defined(XP_WIN) && defined(_M_X64) && !defined(__clang__)
      // libyuv does not support SIMD scaling on MSVC 64bit. See Bug 1295927.
      supports_sse3() ||
#endif
#if defined(MOZ_YCBCR_ROW_SSE)
      // Only worth taking the deprecated path where the SSE row functions
      // exist; otherwise it just falls back to LinearScaleYUVToRGB32Row_C and
      // friends, which are no faster than libyuv.
      (supports_mmx() && supports_sse() && !supports_sse3());
#else
      false;
#endif
  // The deprecated function only support BT601.
  // See Bug 1210357.
  // It also does not support Y8, which has no chroma planes.
  if (yuv_color_space != YUVColorSpace::BT601 || yuv_type == Y8) {
    use_deprecated = false;
  }
  if (use_deprecated) {
    return ScaleYCbCrToRGB32_deprecated(
        y_buf, u_buf, v_buf, rgb_buf, source_width, source_height, width,
        height, y_pitch, uv_pitch, rgb_pitch, yuv_type, ROTATE_0, filter);
  }

  return ToNSResult(YUVToARGBScale(
      y_buf, y_pitch, u_buf, uv_pitch, v_buf, uv_pitch,
      FourCCFromYUVType(yuv_type), yuv_color_space, source_width, source_height,
      rgb_buf, rgb_pitch, width, height, libyuv::kFilterBilinear));
}

// Scale a frame of YUV to 32 bit ARGB.
nsresult
ScaleYCbCrToRGB32_deprecated(const uint8_t* y_buf,
                             const uint8_t* u_buf,
                             const uint8_t* v_buf,
                             uint8_t* rgb_buf,
                             int source_width,
                             int source_height,
                             int width,
                             int height,
                             int y_pitch,
                             int uv_pitch,
                             int rgb_pitch,
                             YUVType yuv_type,
                             Rotate view_rotate,
                             ScaleFilter filter) {
  bool has_mmx = supports_mmx();

  // 4096 allows 3 buffers to fit in 12k.
  // Helps performance on CPU with 16K L1 cache.
  // Large enough for 3830x2160 and 30" displays which are 2560x1600.
  const int kFilterBufferSize = 4096;
  // Disable filtering if the screen is too big (to avoid buffer overflows).
  // This should never happen to regular users: they don't have monitors
  // wider than 4096 pixels.
  // TODO(fbarchard): Allow rotated videos to filter.
  if (source_width > kFilterBufferSize || view_rotate)
    filter = FILTER_NONE;

  unsigned int y_shift = yuv_type == YV12 ? 1 : 0;
  // Diagram showing origin and direction of source sampling.
  // ->0   4<-
  // 7       3
  //
  // 6       5
  // ->1   2<-
  // Rotations that start at right side of image.
  if ((view_rotate == ROTATE_180) ||
      (view_rotate == ROTATE_270) ||
      (view_rotate == MIRROR_ROTATE_0) ||
      (view_rotate == MIRROR_ROTATE_90)) {
    y_buf += source_width - 1;
    u_buf += source_width / 2 - 1;
    v_buf += source_width / 2 - 1;
    source_width = -source_width;
  }
  // Rotations that start at bottom of image.
  if ((view_rotate == ROTATE_90) ||
      (view_rotate == ROTATE_180) ||
      (view_rotate == MIRROR_ROTATE_90) ||
      (view_rotate == MIRROR_ROTATE_180)) {
    y_buf += (source_height - 1) * y_pitch;
    u_buf += ((source_height >> y_shift) - 1) * uv_pitch;
    v_buf += ((source_height >> y_shift) - 1) * uv_pitch;
    source_height = -source_height;
  }

  // Handle zero sized destination.
  if (width == 0 || height == 0)
    return NS_ERROR_INVALID_ARG;
  int source_dx = source_width * kFractionMax / width;
  int source_dy = source_height * kFractionMax / height;
  int source_dx_uv = source_dx;

  if ((view_rotate == ROTATE_90) ||
      (view_rotate == ROTATE_270)) {
    int tmp = height;
    height = width;
    width = tmp;
    tmp = source_height;
    source_height = source_width;
    source_width = tmp;
    int original_dx = source_dx;
    int original_dy = source_dy;
    source_dx = ((original_dy >> kFractionBits) * y_pitch) << kFractionBits;
    source_dx_uv = ((original_dy >> kFractionBits) * uv_pitch) << kFractionBits;
    source_dy = original_dx;
    if (view_rotate == ROTATE_90) {
      y_pitch = -1;
      uv_pitch = -1;
      source_height = -source_height;
    } else {
      y_pitch = 1;
      uv_pitch = 1;
    }
  }

  // Need padding because FilterRows() will write 1 to 16 extra pixels
  // after the end for SSE2 version.
  uint8_t yuvbuf[16 + kFilterBufferSize * 3 + 16];
  uint8_t* ybuf =
      reinterpret_cast<uint8_t*>(reinterpret_cast<uintptr_t>(yuvbuf + 15) & ~15);
  uint8_t* ubuf = ybuf + kFilterBufferSize;
  uint8_t* vbuf = ubuf + kFilterBufferSize;
  // TODO(fbarchard): Fixed point math is off by 1 on negatives.
  int yscale_fixed = (source_height << kFractionBits) / height;

  // TODO(fbarchard): Split this into separate function for better efficiency.
  for (int y = 0; y < height; ++y) {
    uint8_t* dest_pixel = rgb_buf + y * rgb_pitch;
    int source_y_subpixel = (y * yscale_fixed);
    if (yscale_fixed >= (kFractionMax * 2)) {
      source_y_subpixel += kFractionMax / 2;  // For 1/2 or less, center filter.
    }
    int source_y = source_y_subpixel >> kFractionBits;

    const uint8_t* y0_ptr = y_buf + source_y * y_pitch;
    const uint8_t* y1_ptr = y0_ptr + y_pitch;

    const uint8_t* u0_ptr = u_buf + (source_y >> y_shift) * uv_pitch;
    const uint8_t* u1_ptr = u0_ptr + uv_pitch;
    const uint8_t* v0_ptr = v_buf + (source_y >> y_shift) * uv_pitch;
    const uint8_t* v1_ptr = v0_ptr + uv_pitch;

    // vertical scaler uses 16.8 fixed point
    int source_y_fraction = (source_y_subpixel & kFractionMask) >> 8;
    int source_uv_fraction =
        ((source_y_subpixel >> y_shift) & kFractionMask) >> 8;

    const uint8_t* y_ptr = y0_ptr;
    const uint8_t* u_ptr = u0_ptr;
    const uint8_t* v_ptr = v0_ptr;
    // Apply vertical filtering if necessary.
    // TODO(fbarchard): Remove memcpy when not necessary.
    if (filter & mozilla::gfx::FILTER_BILINEAR_V) {
      if (yscale_fixed != kFractionMax &&
          source_y_fraction && ((source_y + 1) < source_height)) {
        FilterRows(ybuf, y0_ptr, y1_ptr, source_width, source_y_fraction);
      } else {
        memcpy(ybuf, y0_ptr, source_width);
      }
      y_ptr = ybuf;
      ybuf[source_width] = ybuf[source_width-1];
      int uv_source_width = (source_width + 1) / 2;
      if (yscale_fixed != kFractionMax &&
          source_uv_fraction &&
          (((source_y >> y_shift) + 1) < (source_height >> y_shift))) {
        FilterRows(ubuf, u0_ptr, u1_ptr, uv_source_width, source_uv_fraction);
        FilterRows(vbuf, v0_ptr, v1_ptr, uv_source_width, source_uv_fraction);
      } else {
        memcpy(ubuf, u0_ptr, uv_source_width);
        memcpy(vbuf, v0_ptr, uv_source_width);
      }
      u_ptr = ubuf;
      v_ptr = vbuf;
      ubuf[uv_source_width] = ubuf[uv_source_width - 1];
      vbuf[uv_source_width] = vbuf[uv_source_width - 1];
    }
    if (source_dx == kFractionMax) {  // Not scaled
      FastConvertYUVToRGB32Row(y_ptr, u_ptr, v_ptr,
                               dest_pixel, width);
    } else if (filter & FILTER_BILINEAR_H) {
        LinearScaleYUVToRGB32Row(y_ptr, u_ptr, v_ptr,
                                 dest_pixel, width, source_dx);
    } else {
// Specialized scalers and rotation.
#if defined(MOZILLA_MAY_SUPPORT_SSE) && defined(_MSC_VER) && defined(_M_IX86) && !defined(__clang__)
      if(mozilla::supports_sse()) {
        if (width == (source_width * 2)) {
          DoubleYUVToRGB32Row_SSE(y_ptr, u_ptr, v_ptr,
                                  dest_pixel, width);
        } else if ((source_dx & kFractionMask) == 0) {
          // Scaling by integer scale factor. ie half.
          ConvertYUVToRGB32Row_SSE(y_ptr, u_ptr, v_ptr,
                                   dest_pixel, width,
                                   source_dx >> kFractionBits);
        } else if (source_dx_uv == source_dx) {  // Not rotated.
          ScaleYUVToRGB32Row(y_ptr, u_ptr, v_ptr,
                             dest_pixel, width, source_dx);
        } else {
          RotateConvertYUVToRGB32Row_SSE(y_ptr, u_ptr, v_ptr,
                                         dest_pixel, width,
                                         source_dx >> kFractionBits,
                                         source_dx_uv >> kFractionBits);
        }
      }
      else {
        ScaleYUVToRGB32Row_C(y_ptr, u_ptr, v_ptr,
                             dest_pixel, width, source_dx);
      }
#else
      (void)source_dx_uv;
      ScaleYUVToRGB32Row(y_ptr, u_ptr, v_ptr,
                         dest_pixel, width, source_dx);
#endif
    }
  }
  // MMX used for FastConvertYUVToRGB32Row and FilterRows requires emms.
  if (has_mmx)
    EMMS();

  return NS_OK;
}

nsresult
ConvertI420AlphaToARGB32(const uint8_t* y_buf,
                         const uint8_t* u_buf,
                         const uint8_t* v_buf,
                         const uint8_t* a_buf,
                         uint8_t* argb_buf,
                         int pic_width,
                         int pic_height,
                         int ya_pitch,
                         int uv_pitch,
                         int argb_pitch) {

  // The downstream graphics stack expects an attenuated input, hence why the
  // attenuation parameter is set.
  return ToNSResult(libyuv::I420AlphaToARGB(
      y_buf, ya_pitch, u_buf, uv_pitch, v_buf, uv_pitch, a_buf, ya_pitch,
      argb_buf, argb_pitch, pic_width, pic_height, 1));
}

} // namespace gfx
} // namespace mozilla
