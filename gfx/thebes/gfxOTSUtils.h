/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef GFX_OTS_UTILS_H
#define GFX_OTS_UTILS_H

#include "gfxFontUtils.h"
#include "mozilla/StaticPrefs_gfx.h"
#include "opentype-sanitiser.h"

struct gfxOTSMozAlloc {
  void* Grow(void* aPtr, size_t aLength) { return moz_xrealloc(aPtr, aLength); }
  void* ShrinkToFit(void* aPtr, size_t aLength) {
    return moz_xrealloc(aPtr, aLength);
  }
  void Free(void* aPtr) { free(aPtr); }
};

// Based on ots::ExpandingMemoryStream from ots-memory-stream.h,
// adapted to use Mozilla allocators and to allow the final
// memory buffer to be adopted by the client.
template <typename AllocT = gfxOTSMozAlloc>
class gfxOTSExpandingMemoryStream : public ots::OTSStream {
 public:
  // limit output/expansion to 256MB by default
  enum { DEFAULT_LIMIT = 256 * 1024 * 1024 };

  explicit gfxOTSExpandingMemoryStream(size_t initial,
                                       size_t limit = DEFAULT_LIMIT)
      : mLength(initial < limit ? initial : limit), mLimit(limit), mOff(0) {
    mPtr = mAlloc.Grow(nullptr, mLength);
    std::memset(mPtr, 0, mLength);
  }

  ~gfxOTSExpandingMemoryStream() { mAlloc.Free(mPtr); }

  size_t size() override { return mLimit; }

  // Return the buffer, resized to fit its contents (as it may have been
  // over-allocated during growth), and give up ownership of it so the
  // caller becomes responsible to call free() when finished with it.
  auto forget() {
    auto p = mAlloc.ShrinkToFit(mPtr, mOff);
    mPtr = nullptr;
    return p;
  }

  bool WriteRaw(const void* data, size_t length) override {
    if ((mOff + length > mLength) ||
        (mLength > std::numeric_limits<size_t>::max() - mOff)) {
      if (mLength == mLimit) {
        return false;
      }
      size_t newLength = (mLength + 1) * 2;
      if (newLength < mLength) {
        return false;
      }
      if (newLength > mLimit) {
        newLength = mLimit;
      }
      mPtr = mAlloc.Grow(mPtr, newLength);
      std::memset(static_cast<char*>(mPtr) + mLength, 0, newLength - mLength);
      mLength = newLength;
      return WriteRaw(data, length);
    }
    std::memcpy(static_cast<char*>(mPtr) + mOff, data, length);
    mOff += length;
    return true;
  }

  bool Seek(off_t position) override {
    if (position < 0) {
      return false;
    }
    if (static_cast<size_t>(position) > mLength) {
      return false;
    }
    mOff = position;
    return true;
  }

  off_t Tell() const override { return mOff; }

 private:
  AllocT mAlloc;
  void* mPtr;
  size_t mLength;
  const size_t mLimit;
  off_t mOff;
};

class MOZ_STACK_CLASS gfxOTSContext : public ots::OTSContext {
 public:
  explicit gfxOTSContext(bool aDropShapingTables = false)
      : mDropShapingTables(aDropShapingTables) {
    using namespace mozilla;

    // Whether to apply OTS validation to OpenType Layout tables
    mCheckOTLTables = StaticPrefs::gfx_downloadable_fonts_otl_validation();
    // Whether to preserve Variation tables in downloaded fonts
    mCheckVariationTables =
        StaticPrefs::gfx_downloadable_fonts_validate_variation_tables();
    // Whether to preserve color bitmap glyphs
    mKeepColorBitmaps =
        StaticPrefs::gfx_downloadable_fonts_keep_color_bitmaps();
    // Whether to preserve SVG glyphs (which can be expensive in Core Text,
    // so better to drop them if we're not going to render them anyhow).
    mKeepSVG = StaticPrefs::gfx_font_rendering_opentype_svg_enabled();
  }

  virtual ots::TableAction GetTableAction(uint32_t aTag) override {
    auto isShapingTable = [](uint32_t aTag) -> bool {
      return  // Tables used by OT layout:
          aTag == TRUETYPE_TAG('B', 'A', 'S', 'E') ||
          aTag == TRUETYPE_TAG('G', 'D', 'E', 'F') ||
          aTag == TRUETYPE_TAG('G', 'P', 'O', 'S') ||
          aTag == TRUETYPE_TAG('G', 'S', 'U', 'B') ||
          aTag == TRUETYPE_TAG('J', 'S', 'T', 'F') ||
          aTag == TRUETYPE_TAG('k', 'e', 'r', 'n') ||
          // Tables used by AAT layout:
          aTag == TRUETYPE_TAG('a', 'n', 'k', 'r') ||
          aTag == TRUETYPE_TAG('b', 's', 'l', 'n') ||
          aTag == TRUETYPE_TAG('f', 'e', 'a', 't') ||
          aTag == TRUETYPE_TAG('j', 'u', 's', 't') ||
          aTag == TRUETYPE_TAG('k', 'e', 'r', 'x') ||
          aTag == TRUETYPE_TAG('m', 'o', 'r', 't') ||
          aTag == TRUETYPE_TAG('m', 'o', 'r', 'x') ||
          aTag == TRUETYPE_TAG('p', 'r', 'o', 'p') ||
          aTag == TRUETYPE_TAG('t', 'r', 'a', 'k') ||
          // Tables used by Graphite2:
          aTag == TRUETYPE_TAG('F', 'e', 'a', 't') ||
          aTag == TRUETYPE_TAG('G', 'l', 'a', 't') ||
          aTag == TRUETYPE_TAG('G', 'l', 'o', 'c') ||
          aTag == TRUETYPE_TAG('S', 'i', 'l', 'e') ||
          aTag == TRUETYPE_TAG('S', 'i', 'l', 'f') ||
          aTag == TRUETYPE_TAG('S', 'i', 'l', 'l');
    };
    if (mDropShapingTables && isShapingTable(aTag)) {
      return ots::TABLE_ACTION_DROP;
    }
    // Pass through or validate OTL and Variation tables, depending on prefs.
    if (aTag == TRUETYPE_TAG('G', 'D', 'E', 'F') ||
        aTag == TRUETYPE_TAG('G', 'P', 'O', 'S') ||
        aTag == TRUETYPE_TAG('G', 'S', 'U', 'B')) {
      switch (mCheckOTLTables) {
        case 0:  // no validation
          return ots::TABLE_ACTION_PASSTHRU;
        case 1:  // validate, logging errors, but do not reject font
          return ots::TABLE_ACTION_SANITIZE_SOFT;
        case 2:  // validate, and reject font if errors are found
        default:
          return ots::TABLE_ACTION_SANITIZE;
      }
    }
    auto isVariationTable = [](uint32_t aTag) -> bool {
      return aTag == TRUETYPE_TAG('a', 'v', 'a', 'r') ||
             aTag == TRUETYPE_TAG('c', 'v', 'a', 'r') ||
             aTag == TRUETYPE_TAG('f', 'v', 'a', 'r') ||
             aTag == TRUETYPE_TAG('g', 'v', 'a', 'r') ||
             aTag == TRUETYPE_TAG('H', 'V', 'A', 'R') ||
             aTag == TRUETYPE_TAG('M', 'V', 'A', 'R') ||
             aTag == TRUETYPE_TAG('S', 'T', 'A', 'T') ||
             aTag == TRUETYPE_TAG('V', 'V', 'A', 'R');
    };
    if (!mCheckVariationTables && isVariationTable(aTag)) {
      return ots::TABLE_ACTION_PASSTHRU;
    }
    if (!gfxPlatform::HasVariationFontSupport() && isVariationTable(aTag)) {
      return ots::TABLE_ACTION_DROP;
    }
    // Preserve SVG table if OpenType-SVG rendering is enabled.
    if (aTag == TRUETYPE_TAG('S', 'V', 'G', ' ')) {
      return mKeepSVG ? ots::TABLE_ACTION_PASSTHRU : ots::TABLE_ACTION_DROP;
    }
    // Preserve BASE table; harfbuzz will sanitize it before using.
    if (aTag == TRUETYPE_TAG('B', 'A', 'S', 'E')) {
      return ots::TABLE_ACTION_PASSTHRU;
    }
    if (mKeepColorBitmaps && (aTag == TRUETYPE_TAG('C', 'B', 'D', 'T') ||
                              aTag == TRUETYPE_TAG('C', 'B', 'L', 'C'))) {
      return ots::TABLE_ACTION_PASSTHRU;
    }
    return ots::TABLE_ACTION_DEFAULT;
  }

  static size_t GuessSanitizedFontSize(size_t aLength,
                                       gfxUserFontType aFontType,
                                       bool aStrict = true) {
    switch (aFontType) {
      case GFX_USERFONT_UNKNOWN:
        // If being permissive of unknown types, make a reasonable guess
        // at how much room the sanitized font may take, if it passes. Just
        // enough extra space to accomodate some growth without excessive
        // bloat in case of large fonts. 1.5x is a reasonable compromise
        // for growable vectors in general.
        return aStrict || !aLength ? 0 : (aLength * 3) / 2;
      case GFX_USERFONT_WOFF:
        return aLength * 2;
      case GFX_USERFONT_WOFF2:
        return aLength * 3;
      default:
        return aLength;
    }
  }

  static size_t GuessSanitizedFontSize(const uint8_t* aData, size_t aLength,
                                       bool aStrict = true) {
    gfxUserFontType fontType =
        gfxFontUtils::DetermineFontDataType(aData, aLength);
    return GuessSanitizedFontSize(aLength, fontType, aStrict);
  }

 private:
  uint8_t mCheckOTLTables;
  bool mCheckVariationTables;
  bool mKeepColorBitmaps;
  bool mKeepSVG;
  // Whether to discard shaping tables (OT/AAT/Graphite), leaving a resource
  // only suitable for rasterizing already-shaped text.
  const bool mDropShapingTables;
};

#endif /* GFX_OTS_UTILS_H */
