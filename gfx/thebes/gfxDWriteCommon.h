/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef GFX_DWRITECOMMON_H
#define GFX_DWRITECOMMON_H

// Mozilla includes
#include "mozilla/MemoryReporting.h"
#include "mozilla/FontPropertyTypes.h"
#include "nscore.h"
#include "nsCOMPtr.h"
#include "gfxFontConstants.h"
#include "nsTArray.h"
#include "gfxWindowsPlatform.h"

#include <windows.h>
#include <dwrite.h>

class FontData;

#define GFX_CLEARTYPE_PARAMS "gfx.font_rendering.cleartype_params."
#define GFX_CLEARTYPE_PARAMS_GAMMA "gfx.font_rendering.cleartype_params.gamma"
#define GFX_CLEARTYPE_PARAMS_CONTRAST \
  "gfx.font_rendering.cleartype_params.enhanced_contrast"
#define GFX_CLEARTYPE_PARAMS_LEVEL \
  "gfx.font_rendering.cleartype_params.cleartype_level"
#define GFX_CLEARTYPE_PARAMS_STRUCTURE \
  "gfx.font_rendering.cleartype_params.pixel_structure"
#define GFX_CLEARTYPE_PARAMS_MODE \
  "gfx.font_rendering.cleartype_params.rendering_mode"

#define DISPLAY1_REGISTRY_KEY \
  HKEY_CURRENT_USER, L"Software\\Microsoft\\Avalon.Graphics\\DISPLAY1"

#define ENHANCED_CONTRAST_VALUE_NAME L"EnhancedContrastLevel"

// FIXME: This shouldn't look at constants probably.
static inline DWRITE_FONT_STRETCH DWriteFontStretchFromStretch(
    mozilla::FontStretch aStretch) {
  if (aStretch == mozilla::FontStretch::ULTRA_CONDENSED) {
    return DWRITE_FONT_STRETCH_ULTRA_CONDENSED;
  }
  if (aStretch == mozilla::FontStretch::EXTRA_CONDENSED) {
    return DWRITE_FONT_STRETCH_EXTRA_CONDENSED;
  }
  if (aStretch == mozilla::FontStretch::CONDENSED) {
    return DWRITE_FONT_STRETCH_CONDENSED;
  }
  if (aStretch == mozilla::FontStretch::SEMI_CONDENSED) {
    return DWRITE_FONT_STRETCH_SEMI_CONDENSED;
  }
  if (aStretch == mozilla::FontStretch::NORMAL) {
    return DWRITE_FONT_STRETCH_NORMAL;
  }
  if (aStretch == mozilla::FontStretch::SEMI_EXPANDED) {
    return DWRITE_FONT_STRETCH_SEMI_EXPANDED;
  }
  if (aStretch == mozilla::FontStretch::EXPANDED) {
    return DWRITE_FONT_STRETCH_EXPANDED;
  }
  if (aStretch == mozilla::FontStretch::EXTRA_EXPANDED) {
    return DWRITE_FONT_STRETCH_EXTRA_EXPANDED;
  }
  if (aStretch == mozilla::FontStretch::ULTRA_EXPANDED) {
    return DWRITE_FONT_STRETCH_ULTRA_EXPANDED;
  }
  return DWRITE_FONT_STRETCH_UNDEFINED;
}

static inline mozilla::FontStretch FontStretchFromDWriteStretch(
    DWRITE_FONT_STRETCH aStretch) {
  switch (aStretch) {
    case DWRITE_FONT_STRETCH_ULTRA_CONDENSED:
      return mozilla::FontStretch::ULTRA_CONDENSED;
    case DWRITE_FONT_STRETCH_EXTRA_CONDENSED:
      return mozilla::FontStretch::EXTRA_CONDENSED;
    case DWRITE_FONT_STRETCH_CONDENSED:
      return mozilla::FontStretch::CONDENSED;
    case DWRITE_FONT_STRETCH_SEMI_CONDENSED:
      return mozilla::FontStretch::SEMI_CONDENSED;
    case DWRITE_FONT_STRETCH_NORMAL:
      return mozilla::FontStretch::NORMAL;
    case DWRITE_FONT_STRETCH_SEMI_EXPANDED:
      return mozilla::FontStretch::SEMI_EXPANDED;
    case DWRITE_FONT_STRETCH_EXPANDED:
      return mozilla::FontStretch::EXPANDED;
    case DWRITE_FONT_STRETCH_EXTRA_EXPANDED:
      return mozilla::FontStretch::EXTRA_EXPANDED;
    case DWRITE_FONT_STRETCH_ULTRA_EXPANDED:
      return mozilla::FontStretch::ULTRA_EXPANDED;
    default:
      return mozilla::FontStretch::NORMAL;
  }
}

class gfxDWriteFontFileStream final : public IDWriteFontFileStream {
 public:
  /**
   * Used by the FontFileLoader to create a new font stream,
   * this font stream is created from data in memory.
   *
   * The FontData struct is refcounted; the stream will hold a reference
   * to it as long as needed, to guaranteed the data remains alive.
   *
   * @param aData Font data
   */
  gfxDWriteFontFileStream(FontData* aData, uint64_t aFontFileKey);
  ~gfxDWriteFontFileStream();

  // IUnknown interface
  IFACEMETHOD(QueryInterface)(IID const& iid, OUT void** ppObject) {
    if (iid == __uuidof(IDWriteFontFileStream)) {
      *ppObject = static_cast<IDWriteFontFileStream*>(this);
      return S_OK;
    } else if (iid == __uuidof(IUnknown)) {
      *ppObject = static_cast<IUnknown*>(this);
      return S_OK;
    } else {
      return E_NOINTERFACE;
    }
  }

  IFACEMETHOD_(ULONG, AddRef)() {
    MOZ_ASSERT(int32_t(mRefCnt) >= 0, "illegal refcnt");
    return ++mRefCnt;
  }

  IFACEMETHOD_(ULONG, Release)();

  // IDWriteFontFileStream methods
  virtual HRESULT STDMETHODCALLTYPE
  ReadFileFragment(void const** fragmentStart, UINT64 fileOffset,
                   UINT64 fragmentSize, OUT void** fragmentContext);

  virtual void STDMETHODCALLTYPE ReleaseFileFragment(void* fragmentContext);

  virtual HRESULT STDMETHODCALLTYPE GetFileSize(OUT UINT64* fileSize);

  virtual HRESULT STDMETHODCALLTYPE GetLastWriteTime(OUT UINT64* lastWriteTime);

  size_t SizeOfExcludingThis(mozilla::MallocSizeOf mallocSizeOf) const {
    // We don't report the size of mData, because the original user font entry
    // will account for that.
    return 0;
  }

  size_t SizeOfIncludingThis(mozilla::MallocSizeOf mallocSizeOf) const {
    return mallocSizeOf(this) + SizeOfExcludingThis(mallocSizeOf);
  }

 private:
  RefPtr<FontData> mData;
  mozilla::Atomic<uint32_t> mRefCnt;
  uint64_t mFontFileKey;
};

class gfxDWriteFontFileLoader : public IDWriteFontFileLoader {
 public:
  gfxDWriteFontFileLoader() {}

  // IUnknown interface
  IFACEMETHOD(QueryInterface)(IID const& iid, OUT void** ppObject) {
    if (iid == __uuidof(IDWriteFontFileLoader)) {
      *ppObject = static_cast<IDWriteFontFileLoader*>(this);
      return S_OK;
    } else if (iid == __uuidof(IUnknown)) {
      *ppObject = static_cast<IUnknown*>(this);
      return S_OK;
    } else {
      return E_NOINTERFACE;
    }
  }

  IFACEMETHOD_(ULONG, AddRef)() { return 1; }

  IFACEMETHOD_(ULONG, Release)() { return 1; }

  // IDWriteFontFileLoader methods
  /**
   * Important! Note the key here -has- to be a pointer to a uint64_t.
   */
  virtual HRESULT STDMETHODCALLTYPE CreateStreamFromKey(
      void const* fontFileReferenceKey, UINT32 fontFileReferenceKeySize,
      OUT IDWriteFontFileStream** fontFileStream);

  /**
   * Gets the singleton loader instance. Note that when using this font
   * loader, the key must be a pointer to a unint64_t.
   */
  static IDWriteFontFileLoader* Instance() {
    if (!mInstance) {
      mInstance = new gfxDWriteFontFileLoader();
      mozilla::gfx::Factory::GetDWriteFactory()->RegisterFontFileLoader(
          mInstance);
    }
    return mInstance;
  }

  /**
   * Creates a IDWriteFontFile and IDWriteFontFileStream from aFontData.
   * The data from aFontData must remain valid as long as the DWrite
   * font file is alive.
   *
   * @param aFontData the font data for the custom font file
   * @param aLength length of the font data
   * @param aFontFile out param for the created font file
   * @param aFontFileStream out param for the corresponding stream
   * @return HRESULT of internal calls
   */
  static HRESULT CreateCustomFontFile(
      FontData* aFontData, IDWriteFontFile** aFontFile,
      gfxDWriteFontFileStream** aFontFileStream);

  size_t SizeOfIncludingThis(mozilla::MallocSizeOf mallocSizeOf) const;

 private:
  static IDWriteFontFileLoader* mInstance;
};

#endif /* GFX_DWRITECOMMON_H */
