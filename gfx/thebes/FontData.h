/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef GFX_FONT_DATA_H
#define GFX_FONT_DATA_H

#include "nsCOMPtr.h"

// Refcounted wrapper for downloaded font data.
class FontData final {
 public:
  // Construct from a malloc'd buffer. This takes ownership of the given buffer;
  // caller MUST NOT delete it or assume anything further about its lifetime.
  FontData(const uint8_t*&& aData, uint32_t aLength)
      : mData(aData), mLength(aLength) {}

  FontData() = delete;
  FontData(const FontData& aOther) = delete;

#ifdef XP_MACOSX
  // This is NS_INLINE_DECL_THREADSAFE_REFCOUNTING without the leak checking,
  // because Core Graphics may hold on to font instances past the point where
  // we want to check for leaks.
  MozExternalRefCountType AddRef(void) {
    MOZ_ASSERT_TYPE_OK_FOR_REFCOUNTING(FontData);
    MOZ_ASSERT(int32_t(mRefCnt) >= 0, "illegal refcnt");
    nsrefcnt count = ++mRefCnt;
    return (nsrefcnt)count;
  }
  MozExternalRefCountType Release(void) {
    MOZ_ASSERT(int32_t(mRefCnt) > 0, "dup release");
    nsrefcnt count = --mRefCnt;
    if (count == 0) {
      delete this;
      return 0;
    }
    return count;
  }
  using HasThreadSafeRefCnt = std::true_type;

 protected:
  ::mozilla::ThreadSafeAutoRefCnt mRefCnt;

 public:
#else
  NS_INLINE_DECL_THREADSAFE_REFCOUNTING(FontData)
#endif

  const uint8_t* Data() const { return mData; }
  uint32_t Length() const { return mLength; }

 private:
  // Private destructor: this should only be destroyed by Release().
  ~FontData() { free(const_cast<uint8_t*>(mData)); }

  const uint8_t* const mData;
  uint32_t const mLength;
};

#endif  // GFX_FONT_DATA_H
