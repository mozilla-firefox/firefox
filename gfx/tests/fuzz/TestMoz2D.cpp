/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "FuzzingInterface.h"
#include "FuzzingBufferReader.h"
#include "mozilla/webrender/webrender_ffi.h"

static int testInitMoz2D(int* argc, char*** argv) { return 0; }

static int testMoz2DRenderCallback(const uint8_t* buf, size_t size) {
  FuzzingBufferReader fuzzBuf(buf, size);

  uint8_t imageFormat = MOZ_TRY(fuzzBuf.Read<uint8_t>());

  mozilla::wr::LayoutIntRect renderRect;
  renderRect.min.x = MOZ_TRY(fuzzBuf.Read<int32_t>());
  renderRect.min.y = MOZ_TRY(fuzzBuf.Read<int32_t>());
  renderRect.max.x = MOZ_TRY(fuzzBuf.Read<int32_t>());
  renderRect.max.y = MOZ_TRY(fuzzBuf.Read<int32_t>());

  mozilla::wr::DeviceIntRect visibleRect;
  visibleRect.min.x = MOZ_TRY(fuzzBuf.Read<int32_t>());
  visibleRect.min.y = MOZ_TRY(fuzzBuf.Read<int32_t>());
  visibleRect.max.x = MOZ_TRY(fuzzBuf.Read<int32_t>());
  visibleRect.max.y = MOZ_TRY(fuzzBuf.Read<int32_t>());

  uint16_t tileSize = MOZ_TRY(fuzzBuf.Read<uint16_t>());

  mozilla::wr::TileOffset tileOffset;
  if (tileSize) {
    tileOffset.x = MOZ_TRY(fuzzBuf.Read<int32_t>());
    tileOffset.y = MOZ_TRY(fuzzBuf.Read<int32_t>());
  }

  uint8_t haveDirtyRect = MOZ_TRY(fuzzBuf.Read<uint8_t>());

  mozilla::wr::LayoutIntRect dirtyRect;
  if (!!haveDirtyRect) {
    dirtyRect.min.x = MOZ_TRY(fuzzBuf.Read<int32_t>());
    dirtyRect.min.y = MOZ_TRY(fuzzBuf.Read<int32_t>());
    dirtyRect.max.x = MOZ_TRY(fuzzBuf.Read<int32_t>());
    dirtyRect.max.y = MOZ_TRY(fuzzBuf.Read<int32_t>());
  }

  uint32_t outLength = MOZ_TRY(fuzzBuf.Read<uint32_t>());
  if (outLength >= 10 * 1024 * 1024) {
    return 0;
  }

  uint32_t blobLength = fuzzBuf.Length();
  // limit buffer lengths to prevent oom
  if (blobLength >= 10 * 1024 * 1024) {
    return 0;
  }

  UniquePtr<uint8_t[]> blobBuffer(new uint8_t[blobLength]);
  memcpy(blobBuffer.get(), fuzzBuf.Pos(), blobLength);

  UniquePtr<uint8_t[]> outBuffer(new uint8_t[outLength]);

  wr_moz2d_render_cb(mozilla::wr::ByteSlice{blobBuffer.get(), blobLength},
                     mozilla::wr::IdNamespace{0},
                     static_cast<mozilla::wr::ImageFormat>(imageFormat),
                     &renderRect, &visibleRect, tileSize,
                     tileSize ? &tileOffset : nullptr,
                     !!haveDirtyRect ? &dirtyRect : nullptr,
                     mozilla::wr::MutByteSlice{outBuffer.get(), outLength});

  return 0;
}

MOZ_FUZZING_INTERFACE_RAW(testInitMoz2D, testMoz2DRenderCallback, Moz2D);
