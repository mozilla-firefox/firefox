/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "GdiBackBuffer.h"

#include <algorithm>

#include "mozilla/gfx/2D.h"

namespace mozilla::widget {

using namespace mozilla::gfx;

HBITMAP CreateTopDownDIBSection(HDC aDC, const IntSize& aSize, uint8_t** aBits,
                                HANDLE aSection) {
  BITMAPINFO bitmapInfo = {};
  bitmapInfo.bmiHeader.biSize = sizeof(bitmapInfo.bmiHeader);
  bitmapInfo.bmiHeader.biWidth = aSize.width;
  bitmapInfo.bmiHeader.biHeight = -aSize.height;
  bitmapInfo.bmiHeader.biPlanes = 1;
  bitmapInfo.bmiHeader.biBitCount = 32;
  bitmapInfo.bmiHeader.biCompression = BI_RGB;
  void* bits = nullptr;
  HBITMAP bitmap =
      ::CreateDIBSection(aDC, &bitmapInfo, DIB_RGB_COLORS, &bits, aSection, 0);
  if (bitmap && !bits) {
    ::DeleteObject(bitmap);
    bitmap = nullptr;
  }
  *aBits = static_cast<uint8_t*>(bits);
  return bitmap;
}

void GdiBackBuffer::Release() {
  // Deleting the DC first deselects the bitmap, which can't be deleted while
  // selected.
  if (mDC) {
    ::DeleteDC(mDC);
    mDC = nullptr;
  }
  if (mBitmap) {
    ::DeleteObject(mBitmap);
    mBitmap = nullptr;
  }
  mData = nullptr;
  mSize = IntSize();
}

bool GdiBackBuffer::Ensure(HDC aCompatibleDC, const IntSize& aSize,
                           HANDLE aSection) {
  if (mData && mSize == aSize) {
    return true;
  }
  Release();
  if (aSize.IsEmpty()) {
    return false;
  }

  mDC = ::CreateCompatibleDC(aCompatibleDC);
  if (!mDC) {
    return false;
  }
  uint8_t* data = nullptr;
  mBitmap = CreateTopDownDIBSection(mDC, aSize, &data, aSection);
  if (!mBitmap) {
    Release();
    return false;
  }
  HGDIOBJ previous = ::SelectObject(mDC, mBitmap);
  if (!previous || previous == HGDI_ERROR) {
    Release();
    return false;
  }
  mData = data;
  mSize = aSize;
  return true;
}

already_AddRefed<DrawTarget> GdiBackBuffer::CreateDrawTarget() {
  if (!mData) {
    return nullptr;
  }
  // GDI batches drawing calls, so a previous BitBlt to or from the bitmap
  // might not have executed yet. Writing to the bits directly requires
  // flushing the batch first, see the CreateDIBSection docs.
  ::GdiFlush();
  RefPtr<DrawTarget> dt = Factory::CreateDrawTargetForData(
      BackendType::SKIA, mData, mSize, mSize.width * 4,
      SurfaceFormat::B8G8R8A8);
  if (!dt || !dt->IsValid()) {
    return nullptr;
  }
  return dt.forget();
}

bool GdiBackBuffer::BlitTo(HDC aDC, const IntRect& aRect) const {
  if (aRect.IsEmpty()) {
    return true;
  }
  return ::BitBlt(aDC, aRect.x, aRect.y, aRect.width, aRect.height, mDC,
                  aRect.x, aRect.y, SRCCOPY);
}

void GdiBackBuffer::CopyPixelsFrom(const GdiBackBuffer& aOther) {
  if (!mData || !aOther.mData) {
    return;
  }
  const int32_t rowBytes = std::min(mSize.width, aOther.mSize.width) * 4;
  const int32_t rows = std::min(mSize.height, aOther.mSize.height);
  for (int32_t row = 0; row < rows; ++row) {
    memcpy(mData + row * Stride(), aOther.mData + row * aOther.Stride(),
           rowBytes);
  }
}

}  // namespace mozilla::widget
