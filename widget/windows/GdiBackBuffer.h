/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef widget_windows_GdiBackBuffer_h
#define widget_windows_GdiBackBuffer_h

#include <windows.h>

#include "mozilla/AlreadyAddRefed.h"
#include "mozilla/gfx/Rect.h"

namespace mozilla::gfx {
class DrawTarget;
}

namespace mozilla::widget {

// Creates a 32bpp top-down DIB section. aBits receives a pointer to its pixel
// data, which is B8G8R8A8 with a stride of 4 * width.
HBITMAP CreateTopDownDIBSection(HDC aDC, const gfx::IntSize& aSize,
                                uint8_t** aBits);

// A top-down 32bpp DIB section selected into a memory DC, which can be drawn
// to and then blitted with GDI.
class GdiBackBuffer final {
 public:
  GdiBackBuffer() = default;
  GdiBackBuffer(const GdiBackBuffer&) = delete;
  GdiBackBuffer& operator=(const GdiBackBuffer&) = delete;
  ~GdiBackBuffer() { Release(); }

  // (Re)creates the buffer unless it already has the given size.
  bool Ensure(HDC aCompatibleDC, const gfx::IntSize& aSize);
  void Release();

  HDC DC() const { return mDC; }
  const gfx::IntSize& Size() const { return mSize; }

  already_AddRefed<gfx::DrawTarget> CreateDrawTarget();

  void BlitTo(HDC aDC, const gfx::IntRect& aRect) const;

 private:
  HDC mDC = nullptr;
  HBITMAP mBitmap = nullptr;
  uint8_t* mData = nullptr;
  gfx::IntSize mSize;
};

}  // namespace mozilla::widget

#endif
