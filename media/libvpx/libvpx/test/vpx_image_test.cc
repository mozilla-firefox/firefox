/*
 *  Copyright (c) 2024 The WebM project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#include <climits>
#include <cstdint>
#include <cstdio>
#include <initializer_list>

#include "vpx/vpx_image.h"
#include "gtest/gtest.h"

TEST(VpxImageTest, VpxImgWrapInvalidAlign) {
  const int kWidth = 128;
  const int kHeight = 128;
  unsigned char buf[kWidth * kHeight * 3];

  vpx_image_t img;
  // Set img_data and img_data_owner to junk values. vpx_img_wrap() should
  // not read these values on failure.
  unsigned char empty[] = "";
  img.img_data = empty;
  img.img_data_owner = 1;

  vpx_img_fmt_t format = VPX_IMG_FMT_I444;
  // 'align' must be a power of 2 but is not. This causes the vpx_img_wrap()
  // call to fail. The test verifies we do not read the junk values in 'img'.
  unsigned int align = 31;
  EXPECT_EQ(vpx_img_wrap(&img, format, kWidth, kHeight, align, buf), nullptr);
}

TEST(VpxImageTest, VpxImgWrapI42xOddWidth) {
  vpx_image_t img;
  unsigned char buf[1];  // Should not be read
  char str[32];
  for (const auto format : { VPX_IMG_FMT_I420, VPX_IMG_FMT_I422,
                             VPX_IMG_FMT_I42016, VPX_IMG_FMT_I42216 }) {
    for (const int width : { 1, 711, 4913 }) {
      snprintf(str, sizeof(str), "format: %d width: %d", format, width);
      SCOPED_TRACE(str);
      const int stride_in_bytes =
          (format & VPX_IMG_FMT_HIGHBITDEPTH) ? width * 2 : width;
      const int uv_width = (width + 1) / 2;
      const int uv_stride_in_bytes =
          (format & VPX_IMG_FMT_HIGHBITDEPTH) ? uv_width * 2 : uv_width;
      EXPECT_EQ(vpx_img_wrap(&img, format, width, 32, /*stride_align=*/1, buf),
                &img);
      EXPECT_EQ(img.stride[VPX_PLANE_Y], stride_in_bytes);
      EXPECT_EQ(img.stride[VPX_PLANE_U], uv_stride_in_bytes);
      EXPECT_EQ(img.stride[VPX_PLANE_V], uv_stride_in_bytes);
    }
  }
}

TEST(VpxImageTest, VpxImgWrapNV12OddWidth) {
  vpx_image_t img;
  unsigned char buf[1];  // Should not be read

  for (const int width : { 1, 711, 4913 }) {
    const int uv_stride_in_bytes = 2 * ((width + 1) / 2);

    EXPECT_EQ(vpx_img_wrap(&img, VPX_IMG_FMT_NV12, width, 32,
                           /*stride_align=*/1, buf),
              &img);
    EXPECT_EQ(img.stride[VPX_PLANE_Y], width);
    EXPECT_EQ(img.stride[VPX_PLANE_U], uv_stride_in_bytes);
    EXPECT_EQ(img.stride[VPX_PLANE_V], uv_stride_in_bytes);
    EXPECT_EQ(img.planes[VPX_PLANE_V], img.planes[VPX_PLANE_U] + 1);

    const int aligned_stride = (width + 15) & ~15;
    EXPECT_EQ(vpx_img_wrap(&img, VPX_IMG_FMT_NV12, width, 32,
                           /*stride_align=*/16, buf),
              &img);
    EXPECT_EQ(img.stride[VPX_PLANE_Y], aligned_stride);
    EXPECT_EQ(img.stride[VPX_PLANE_U], aligned_stride);
    EXPECT_EQ(img.stride[VPX_PLANE_V], aligned_stride);
  }
}

TEST(VpxImageTest, VpxImgWrapOddHeightSeparatesChromaPlanes) {
  static constexpr vpx_img_fmt_t kFormats[] = {
    VPX_IMG_FMT_YV12,   VPX_IMG_FMT_I420,   VPX_IMG_FMT_I440,
    VPX_IMG_FMT_I42016, VPX_IMG_FMT_I44016,
  };
  static constexpr int kWidth = 16;
  static constexpr int kHeight = 3;
  unsigned char buf[256];

  for (const vpx_img_fmt_t format : kFormats) {
    vpx_image_t img;
    ASSERT_EQ(vpx_img_wrap(&img, format, kWidth, kHeight,
                           /*stride_align=*/1, buf),
              &img);

    const ptrdiff_t chroma_plane_size =
        ((kHeight + 1) / 2) * img.stride[VPX_PLANE_U];
    if (format & VPX_IMG_FMT_UV_FLIP) {
      EXPECT_EQ(img.planes[VPX_PLANE_U] - img.planes[VPX_PLANE_V],
                chroma_plane_size);
    } else {
      EXPECT_EQ(img.planes[VPX_PLANE_V] - img.planes[VPX_PLANE_U],
                chroma_plane_size);
    }
  }
}

TEST(VpxImageTest, VpxImgSetRectOverflow) {
  const int kWidth = 128;
  const int kHeight = 128;
  unsigned char buf[kWidth * kHeight * 3];

  vpx_image_t img;
  vpx_img_fmt_t format = VPX_IMG_FMT_I444;
  unsigned int align = 32;
  EXPECT_EQ(vpx_img_wrap(&img, format, kWidth, kHeight, align, buf), &img);

  EXPECT_EQ(vpx_img_set_rect(&img, 0, 0, kWidth, kHeight), 0);
  // This would result in overflow because -1 is cast to UINT_MAX.
  EXPECT_NE(vpx_img_set_rect(&img, static_cast<unsigned int>(-1),
                             static_cast<unsigned int>(-1), kWidth, kHeight),
            0);
}

TEST(VpxImageTest, VpxImgSetRectRejectsFlipped) {
  static constexpr vpx_img_fmt_t kFormats[] = {
    VPX_IMG_FMT_YV12,   VPX_IMG_FMT_I420,   VPX_IMG_FMT_I422,
    VPX_IMG_FMT_I444,   VPX_IMG_FMT_I440,   VPX_IMG_FMT_NV12,
    VPX_IMG_FMT_I42016, VPX_IMG_FMT_I42216, VPX_IMG_FMT_I44416,
    VPX_IMG_FMT_I44016,
  };

  for (const vpx_img_fmt_t format : kFormats) {
    SCOPED_TRACE(format);
    vpx_image_t *img = vpx_img_alloc(nullptr, format, 4, 4, 1);
    ASSERT_NE(img, nullptr);

    // An ordinary crop is valid before flipping.
    ASSERT_EQ(vpx_img_set_rect(img, 2, 2, 2, 2), 0);
    EXPECT_EQ(img->d_w, 2u);
    EXPECT_EQ(img->d_h, 2u);
    ASSERT_EQ(vpx_img_set_rect(img, 0, 0, 4, 4), 0);

    vpx_img_flip(img);
    const unsigned char *const flipped_planes[] = { img->planes[VPX_PLANE_Y],
                                                    img->planes[VPX_PLANE_U],
                                                    img->planes[VPX_PLANE_V] };
    EXPECT_EQ(vpx_img_set_rect(img, 2, 2, 2, 2), -1);
    EXPECT_EQ(img->d_w, 4u);
    EXPECT_EQ(img->d_h, 4u);
    for (int plane = VPX_PLANE_Y; plane <= VPX_PLANE_V; ++plane) {
      EXPECT_EQ(img->planes[plane], flipped_planes[plane]);
    }

    // Flipping back permits changing the viewport again.
    vpx_img_flip(img);
    EXPECT_GT(img->stride[VPX_PLANE_Y], 0);
    const unsigned char *const unflipped_y_plane = img->planes[VPX_PLANE_Y];
    EXPECT_EQ(vpx_img_set_rect(img, 2, 2, 2, 2), 0);
    EXPECT_NE(img->planes[VPX_PLANE_Y], unflipped_y_plane);
    EXPECT_EQ(img->d_w, 2u);
    EXPECT_EQ(img->d_h, 2u);
    vpx_img_free(img);
  }
}

TEST(VpxImageTest, VpxImgAllocNone) {
  const int kWidth = 128;
  const int kHeight = 128;

  vpx_image_t img;
  vpx_img_fmt_t format = VPX_IMG_FMT_NONE;
  unsigned int align = 32;
  ASSERT_EQ(vpx_img_alloc(&img, format, kWidth, kHeight, align), nullptr);
}

TEST(VpxImageTest, VpxImgAllocNv12) {
  const int kWidth = 128;
  const int kHeight = 128;

  vpx_image_t img;
  vpx_img_fmt_t format = VPX_IMG_FMT_NV12;
  unsigned int align = 32;
  EXPECT_EQ(vpx_img_alloc(&img, format, kWidth, kHeight, align), &img);
  EXPECT_EQ(img.x_chroma_shift, 1u);
  EXPECT_EQ(img.stride[VPX_PLANE_U], img.stride[VPX_PLANE_Y]);
  EXPECT_EQ(img.stride[VPX_PLANE_V], img.stride[VPX_PLANE_U]);
  EXPECT_EQ(img.planes[VPX_PLANE_V], img.planes[VPX_PLANE_U] + 1);
  vpx_img_free(&img);
}

TEST(VpxImageTest, VpxImgAllocHugeWidth) {
  // The stride (0x80000000 * 2) would overflow unsigned int.
  vpx_image_t *image =
      vpx_img_alloc(nullptr, VPX_IMG_FMT_I42016, 0x80000000, 1, 1);
  ASSERT_EQ(image, nullptr);

  // The stride (0x80000000) would overflow int.
  image = vpx_img_alloc(nullptr, VPX_IMG_FMT_I420, 0x80000000, 1, 1);
  ASSERT_EQ(image, nullptr);

  // The aligned width (UINT_MAX + 1) would overflow unsigned int.
  image = vpx_img_alloc(nullptr, VPX_IMG_FMT_I420, UINT_MAX, 1, 1);
  ASSERT_EQ(image, nullptr);

  image = vpx_img_alloc(nullptr, VPX_IMG_FMT_I420, 0x7ffffffe, 1, 1);
  if (image) {
    vpx_img_free(image);
  }

  image = vpx_img_alloc(nullptr, VPX_IMG_FMT_I420, 285245883, 64, 1);
  if (image) {
    vpx_img_free(image);
  }

  image = vpx_img_alloc(nullptr, VPX_IMG_FMT_NV12, 285245883, 64, 1);
  if (image) {
    vpx_img_free(image);
  }

  image = vpx_img_alloc(nullptr, VPX_IMG_FMT_YV12, 285245883, 64, 1);
  if (image) {
    vpx_img_free(image);
  }

  image = vpx_img_alloc(nullptr, VPX_IMG_FMT_I42016, 65536, 2, 1);
  if (image) {
    uint16_t *y_plane =
        reinterpret_cast<uint16_t *>(image->planes[VPX_PLANE_Y]);
    y_plane[0] = 0;
    y_plane[image->d_w - 1] = 0;
    vpx_img_free(image);
  }

  image = vpx_img_alloc(nullptr, VPX_IMG_FMT_I42016, 285245883, 2, 1);
  if (image) {
    uint16_t *y_plane =
        reinterpret_cast<uint16_t *>(image->planes[VPX_PLANE_Y]);
    y_plane[0] = 0;
    y_plane[image->d_w - 1] = 0;
    vpx_img_free(image);
  }
}

TEST(VpxImageTest, VpxImgFlipNoAlpha) {
  vpx_image_t *img = vpx_img_alloc(nullptr, VPX_IMG_FMT_I420, 64, 64, 16);
  ASSERT_NE(img, nullptr);
  vpx_img_flip(img);
  vpx_img_free(img);
}

TEST(VpxImageTest, VpxImgFlipOneRow) {
  vpx_image_t *img = vpx_img_alloc(nullptr, VPX_IMG_FMT_I420, 16, 1, 1);
  ASSERT_NE(img, nullptr);
  unsigned char *const y_plane = img->planes[VPX_PLANE_Y];
  unsigned char *const u_plane = img->planes[VPX_PLANE_U];
  unsigned char *const v_plane = img->planes[VPX_PLANE_V];
  const int y_stride = img->stride[VPX_PLANE_Y];
  const int u_stride = img->stride[VPX_PLANE_U];
  const int v_stride = img->stride[VPX_PLANE_V];

  vpx_img_flip(img);

  EXPECT_EQ(img->planes[VPX_PLANE_Y], y_plane);
  EXPECT_EQ(img->planes[VPX_PLANE_U], u_plane);
  EXPECT_EQ(img->planes[VPX_PLANE_V], v_plane);
  EXPECT_EQ(img->stride[VPX_PLANE_Y], -y_stride);
  EXPECT_EQ(img->stride[VPX_PLANE_U], -u_stride);
  EXPECT_EQ(img->stride[VPX_PLANE_V], -v_stride);

  vpx_img_flip(img);

  EXPECT_EQ(img->planes[VPX_PLANE_Y], y_plane);
  EXPECT_EQ(img->planes[VPX_PLANE_U], u_plane);
  EXPECT_EQ(img->planes[VPX_PLANE_V], v_plane);
  EXPECT_EQ(img->stride[VPX_PLANE_Y], y_stride);
  EXPECT_EQ(img->stride[VPX_PLANE_U], u_stride);
  EXPECT_EQ(img->stride[VPX_PLANE_V], v_stride);

  vpx_img_free(img);
}

TEST(VpxImageTest, VpxImgFlipEmpty) {
  vpx_image_t *img = vpx_img_alloc(nullptr, VPX_IMG_FMT_I420, 16, 16, 1);
  ASSERT_NE(img, nullptr);
  ASSERT_EQ(vpx_img_set_rect(img, 0, 0, 16, 0), 0);
  const unsigned char *const planes[] = { img->planes[VPX_PLANE_Y],
                                          img->planes[VPX_PLANE_U],
                                          img->planes[VPX_PLANE_V] };

  vpx_img_flip(img);

  for (int plane = VPX_PLANE_Y; plane <= VPX_PLANE_V; ++plane) {
    EXPECT_EQ(img->planes[plane], planes[plane]);
    EXPECT_GT(img->stride[plane], 0);
  }
  vpx_img_free(img);
}

TEST(VpxImageTest, VpxImgFlipOddHeight) {
  static constexpr vpx_img_fmt_t kFormats[] = {
    VPX_IMG_FMT_YV12, VPX_IMG_FMT_I420,   VPX_IMG_FMT_I440,
    VPX_IMG_FMT_NV12, VPX_IMG_FMT_I42016, VPX_IMG_FMT_I44016,
  };

  for (const vpx_img_fmt_t format : kFormats) {
    vpx_image_t *img = vpx_img_alloc(nullptr, format, 16, 3, 1);
    ASSERT_NE(img, nullptr);
    unsigned char *const y_plane = img->planes[VPX_PLANE_Y];
    unsigned char *const u_plane = img->planes[VPX_PLANE_U];
    unsigned char *const v_plane = img->planes[VPX_PLANE_V];
    const int y_stride = img->stride[VPX_PLANE_Y];
    const int u_stride = img->stride[VPX_PLANE_U];
    const int v_stride = img->stride[VPX_PLANE_V];

    vpx_img_flip(img);

    EXPECT_EQ(img->planes[VPX_PLANE_Y], y_plane + 2 * y_stride);
    EXPECT_EQ(img->planes[VPX_PLANE_U], u_plane + u_stride);
    EXPECT_EQ(img->planes[VPX_PLANE_V], v_plane + v_stride);
    EXPECT_EQ(img->stride[VPX_PLANE_Y], -y_stride);
    EXPECT_EQ(img->stride[VPX_PLANE_U], -u_stride);
    EXPECT_EQ(img->stride[VPX_PLANE_V], -v_stride);

    vpx_img_flip(img);

    EXPECT_EQ(img->planes[VPX_PLANE_Y], y_plane);
    EXPECT_EQ(img->planes[VPX_PLANE_U], u_plane);
    EXPECT_EQ(img->planes[VPX_PLANE_V], v_plane);
    EXPECT_EQ(img->stride[VPX_PLANE_Y], y_stride);
    EXPECT_EQ(img->stride[VPX_PLANE_U], u_stride);
    EXPECT_EQ(img->stride[VPX_PLANE_V], v_stride);

    vpx_img_free(img);
  }
}
