/* Any copyright is dedicated to the Public Domain.
 * https://creativecommons.org/publicdomain/zero/1.0/ */

#include <gdk-pixbuf/gdk-pixbuf.h>

#include "gtest/gtest.h"
#include "mozilla/GRefPtr.h"
#include "mozilla/RefPtr.h"
#include "mozilla/gfx/2D.h"
#include "nsImageToPixbuf.h"

using namespace mozilla;
using mozilla::gfx::DataSourceSurface;
using mozilla::gfx::Factory;
using mozilla::gfx::IntSize;
using mozilla::gfx::SourceSurface;
using mozilla::gfx::SurfaceFormat;

static already_AddRefed<DataSourceSurface> MakeOpaqueSurface(int32_t aSize) {
  RefPtr<DataSourceSurface> surface = Factory::CreateDataSourceSurface(
      IntSize(aSize, aSize), SurfaceFormat::B8G8R8A8);
  if (!surface) {
    return nullptr;
  }

  DataSourceSurface::MappedSurface map;
  if (!surface->Map(DataSourceSurface::MapType::WRITE, &map)) {
    return nullptr;
  }
  for (int32_t row = 0; row < aSize; ++row) {
    uint32_t* dst = reinterpret_cast<uint32_t*>(map.mData + row * map.mStride);
    for (int32_t col = 0; col < aSize; ++col) {
      dst[col] = 0xFFFFFFFF;
    }
  }
  surface->Unmap();
  return surface.forget();
}

TEST(SourceSurfaceToPixbuf, RequestLargerThanSurface)
{
  const int32_t kSrcSize = 8;
  const int32_t kReqSize = 2000;

  RefPtr<DataSourceSurface> surface = MakeOpaqueSurface(kSrcSize);
  ASSERT_TRUE(surface);

  RefPtr<GdkPixbuf> pixbuf =
      nsImageToPixbuf::SourceSurfaceToPixbuf(surface, kReqSize, kReqSize);
  ASSERT_TRUE(pixbuf);

  EXPECT_EQ(gdk_pixbuf_get_width(pixbuf), kReqSize);
  EXPECT_EQ(gdk_pixbuf_get_height(pixbuf), kReqSize);
  ASSERT_EQ(gdk_pixbuf_get_n_channels(pixbuf), 4);

  const guchar* pixels = gdk_pixbuf_get_pixels(pixbuf);
  const int32_t stride = gdk_pixbuf_get_rowstride(pixbuf);

  // A pixel covered by the source is opaque white.
  const guchar* covered = pixels;
  EXPECT_EQ(covered[0], 0xFF);
  EXPECT_EQ(covered[1], 0xFF);
  EXPECT_EQ(covered[2], 0xFF);
  EXPECT_EQ(covered[3], 0xFF);

  const guchar* uncovered =
      pixels + (kReqSize - 1) * stride + (kReqSize - 1) * 4;
  EXPECT_EQ(uncovered[0], 0x00);
  EXPECT_EQ(uncovered[1], 0x00);
  EXPECT_EQ(uncovered[2], 0x00);
  EXPECT_EQ(uncovered[3], 0x00);
}

// The exact-fit case still copies every pixel.
TEST(SourceSurfaceToPixbuf, RequestMatchesSurface)
{
  const int32_t kSize = 16;

  RefPtr<DataSourceSurface> surface = MakeOpaqueSurface(kSize);
  ASSERT_TRUE(surface);

  RefPtr<GdkPixbuf> pixbuf =
      nsImageToPixbuf::SourceSurfaceToPixbuf(surface, kSize, kSize);
  ASSERT_TRUE(pixbuf);

  EXPECT_EQ(gdk_pixbuf_get_width(pixbuf), kSize);
  EXPECT_EQ(gdk_pixbuf_get_height(pixbuf), kSize);

  const guchar* pixels = gdk_pixbuf_get_pixels(pixbuf);
  const int32_t stride = gdk_pixbuf_get_rowstride(pixbuf);
  const guchar* corner = pixels + (kSize - 1) * stride + (kSize - 1) * 4;
  EXPECT_EQ(corner[0], 0xFF);
  EXPECT_EQ(corner[3], 0xFF);
}
