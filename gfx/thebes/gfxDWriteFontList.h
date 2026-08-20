/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef GFX_DWRITEFONTLIST_H
#define GFX_DWRITEFONTLIST_H

#include <algorithm>

#include "cairo-win32.h"
#include "dwrite_3.h"
#include "gfxDWriteCommon.h"
#include "gfxFont.h"
#include "gfxPlatform.h"
#include "gfxPlatformFontList.h"
#include "gfxUserFontSet.h"
#include "mozilla/FontPropertyTypes.h"
#include "mozilla/MemoryReporting.h"
#include "mozilla/gfx/UnscaledFontDWrite.h"

/**
 * \brief Class representing directwrite font family.
 *
 * gfxDWriteFontFamily is a class that describes one of the font families on
 * the user's system.  It holds each gfxDWriteFontEntry (maps more directly to
 * a font face) which holds font type, charset info and character map info.
 */
class gfxDWriteFontFamily final : public gfxFontFamily {
 public:
  typedef mozilla::FontWidth FontWidth;
  typedef mozilla::FontSlantStyle FontSlantStyle;
  typedef mozilla::FontWeight FontWeight;

  /**
   * Constructs a new DWriteFont Family.
   *
   * \param aName Name identifying the family
   * \param aFamily IDWriteFontFamily object representing the directwrite
   * family object.
   */
  gfxDWriteFontFamily(const nsACString& aName, FontVisibility aVisibility,
                      IDWriteFontFamily* aFamily,
                      bool aIsSystemFontFamily = false)
      : gfxFontFamily(aName, aVisibility),
        mDWFamily(aFamily),
        mIsSystemFontFamily(aIsSystemFontFamily),
        mForceGDIClassic(false) {}
  virtual ~gfxDWriteFontFamily();

  void FindStyleVariationsLocked(FontInfoData* aFontInfoData = nullptr)
      MOZ_REQUIRES(mLock) final;

  void LocalizedName(nsACString& aLocalizedName) final;

  void ReadFaceNames(gfxPlatformFontList* aPlatformFontList,
                     bool aNeedFullnamePostscriptNames,
                     FontInfoData* aFontInfoData = nullptr) final;

  void SetForceGDIClassic(bool aForce) { mForceGDIClassic = aForce; }

  void AddSizeOfExcludingThis(mozilla::MallocSizeOf aMallocSizeOf,
                              FontListSizes* aSizes) const final;
  void AddSizeOfIncludingThis(mozilla::MallocSizeOf aMallocSizeOf,
                              FontListSizes* aSizes) const final;

  bool FilterForFontList(nsAtom* aLangGroup,
                         const nsACString& aGeneric) const final {
    return !IsSymbolFontFamily();
  }

 protected:
  // helper for FilterForFontList
  bool IsSymbolFontFamily() const;

  /** This font family's directwrite fontfamily object */
  RefPtr<IDWriteFontFamily> mDWFamily;
  bool mIsSystemFontFamily;
  bool mForceGDIClassic;
};

/**
 * \brief Class representing DirectWrite FontEntry (a unique font style/family)
 */
class gfxDWriteFontEntry final : public gfxFontEntry {
 public:
  /**
   * Constructs a font entry.
   *
   * \param aFaceName The name of the corresponding font face.
   * \param aFont DirectWrite font object
   */
  gfxDWriteFontEntry(const nsACString& aFaceName, IDWriteFont* aFont,
                     bool aIsSystemFont = false)
      : gfxFontEntry(aFaceName),
        mFont(aFont),
        mFontFile(nullptr),
        mIsSystemFont(aIsSystemFont),
        mForceGDIClassic(false),
        mHasVariations(false),
        mHasVariationsInitialized(false) {
    DWRITE_FONT_STYLE dwriteStyle = aFont->GetStyle();
    FontSlantStyle style = (dwriteStyle == DWRITE_FONT_STYLE_ITALIC
                                ? FontSlantStyle::ITALIC
                                : (dwriteStyle == DWRITE_FONT_STYLE_OBLIQUE
                                       ? FontSlantStyle::OBLIQUE
                                       : FontSlantStyle::NORMAL));
    mStyleRange = SlantStyleRange(style);

    mWidthRange = WidthRange(FontWidthFromDWriteStretch(aFont->GetStretch()));

    int weight = mozilla::RoundUpToMultiple(aFont->GetWeight() - 50, 100);
    weight = std::clamp(weight, 100, 900);
    mWeightRange = WeightRange(FontWeight::FromInt(weight));

    mIsCJK = UNINITIALIZED_VALUE;
  }

  /**
   * Constructs a font entry using a font. But with custom font values.
   * This is used for creating correct font entries for @font-face with local
   * font source.
   *
   * \param aFaceName The name of the corresponding font face.
   * \param aFont DirectWrite font object
   * \param aWeight Weight of the font
   * \param aWidth Width of the font
   * \param aStyle italic or oblique of font
   */
  gfxDWriteFontEntry(const nsACString& aFaceName, IDWriteFont* aFont,
                     WeightRange aWeight, WidthRange aWidth,
                     SlantStyleRange aStyle)
      : gfxFontEntry(aFaceName),
        mFont(aFont),
        mFontFile(nullptr),
        mIsSystemFont(false),
        mForceGDIClassic(false),
        mHasVariations(false),
        mHasVariationsInitialized(false) {
    mWeightRange = aWeight;
    mWidthRange = aWidth;
    mStyleRange = aStyle;
    mIsLocalUserFont = true;
    mIsCJK = UNINITIALIZED_VALUE;
  }

  /**
   * Constructs a font entry using a font file.
   *
   * \param aFaceName The name of the corresponding font face.
   * \param aFontFile DirectWrite fontfile object
   * \param aFontFileStream DirectWrite fontfile stream object
   * \param aWeight Weight of the font
   * \param aWidth Width of the font
   * \param aStyle italic or oblique of font
   */
  gfxDWriteFontEntry(const nsACString& aFaceName, IDWriteFontFile* aFontFile,
                     gfxDWriteFontFileStream* aFontFileStream,
                     WeightRange aWeight, WidthRange aWidth,
                     SlantStyleRange aStyle)
      : gfxFontEntry(aFaceName),
        mFont(nullptr),
        mFontFile(aFontFile),
        mFontFileStream(aFontFileStream),
        mIsSystemFont(false),
        mForceGDIClassic(false),
        mHasVariations(false),
        mHasVariationsInitialized(false) {
    mWeightRange = aWeight;
    mWidthRange = aWidth;
    mStyleRange = aStyle;
    mIsDataUserFont = true;
    mIsCJK = UNINITIALIZED_VALUE;
  }

  gfxFontEntry* Clone() const override;

  nsresult ReadCMAP(FontInfoData* aFontInfoData = nullptr) override;

  bool IsCJKFont();

  bool HasVariations() override;
  void GetVariationAxes(nsTArray<gfxFontVariationAxis>& aAxes) override;
  void GetVariationInstances(
      nsTArray<gfxFontVariationInstance>& aInstances) override;

  void SetForceGDIClassic(bool aForce) { mForceGDIClassic = aForce; }
  bool GetForceGDIClassic() { return mForceGDIClassic; }

  void AddSizeOfExcludingThis(mozilla::MallocSizeOf aMallocSizeOf,
                              FontListSizes* aSizes) const override;
  void AddSizeOfIncludingThis(mozilla::MallocSizeOf aMallocSizeOf,
                              FontListSizes* aSizes) const override;

  size_t ComputedSizeOfExcludingThis(
      mozilla::MallocSizeOf aMallocSizeOf) override;

 protected:
  friend class gfxDWriteFont;
  friend class gfxDWriteFontList;
  friend class gfxDWriteFontFamily;

  // Protected destructor, to discourage deletion outside of Release():
  virtual ~gfxDWriteFontEntry();

  hb_blob_t* GetFontTableInternal(uint32_t aTableTag) override;

  virtual nsresult CopyFontTable(uint32_t aTableTag,
                                 nsTArray<uint8_t>& aBuffer) override;

  virtual gfxFont* CreateFontInstance(const gfxFontStyle* aFontStyle);

  nsresult CreateFontFace(
      IDWriteFontFace** aFontFace, const gfxFontStyle* aFontStyle = nullptr,
      DWRITE_FONT_SIMULATIONS aSimulations = DWRITE_FONT_SIMULATIONS_NONE,
      const nsTArray<gfxFontVariation>* aVariations = nullptr);

  static bool InitLogFont(IDWriteFont* aFont, LOGFONTW* aLogFont);

  FontTableCache* GetFontTableCache(bool aCreate) override;

  /**
   * A fontentry only needs to have either of these. If it has both only
   * the IDWriteFont will be used.
   */
  RefPtr<IDWriteFont> mFont;
  RefPtr<IDWriteFontFile> mFontFile;

  // For custom fonts, we hold a reference to the IDWriteFontFileStream for
  // for the IDWriteFontFile, so that the data is available.
  RefPtr<gfxDWriteFontFileStream> mFontFileStream;

  // font face corresponding to the mFont/mFontFile *without* any DWrite
  // style simulations applied
  RefPtr<IDWriteFontFace> mFontFace;
  // Extended fontface interface if supported, else null
  RefPtr<IDWriteFontFace5> mFontFace5;

  DWRITE_FONT_FACE_TYPE mFaceType;

  mozilla::Atomic<FontTableCache*> mFontTableCache;

  int8_t mIsCJK;
  bool mIsSystemFont;
  bool mForceGDIClassic;
  bool mHasVariations;
  bool mHasVariationsInitialized;

  // Set to true only if the font belongs to a "simple" family where the
  // faces can be reliably identified via a GDI LOGFONT structure.
  bool mMayUseGDIAccess = false;

  mozilla::ThreadSafeWeakPtr<mozilla::gfx::UnscaledFontDWrite> mUnscaledFont;
  mozilla::ThreadSafeWeakPtr<mozilla::gfx::UnscaledFontDWrite>
      mUnscaledFontBold;
};

// custom text renderer used to determine the fallback font for a given char
class DWriteFontFallbackRenderer final : public IDWriteTextRenderer {
 public:
  explicit DWriteFontFallbackRenderer(IDWriteFactory* aFactory) : mRefCount(0) {
    HRESULT hr =
        aFactory->GetSystemFontCollection(getter_AddRefs(mSystemFonts));
    NS_ASSERTION(SUCCEEDED(hr), "GetSystemFontCollection failed!");
    (void)hr;
  }

  ~DWriteFontFallbackRenderer() = default;

  // If we don't have an mSystemFonts pointer, this renderer is unusable.
  bool IsValid() const { return mSystemFonts; }

  // IDWriteTextRenderer methods
  IFACEMETHOD(DrawGlyphRun)
  (void* clientDrawingContext, FLOAT baselineOriginX, FLOAT baselineOriginY,
   DWRITE_MEASURING_MODE measuringMode, DWRITE_GLYPH_RUN const* glyphRun,
   DWRITE_GLYPH_RUN_DESCRIPTION const* glyphRunDescription,
   IUnknown* clientDrawingEffect);

  IFACEMETHOD(DrawUnderline)
  (void* clientDrawingContext, FLOAT baselineOriginX, FLOAT baselineOriginY,
   DWRITE_UNDERLINE const* underline, IUnknown* clientDrawingEffect) {
    return E_NOTIMPL;
  }

  IFACEMETHOD(DrawStrikethrough)
  (void* clientDrawingContext, FLOAT baselineOriginX, FLOAT baselineOriginY,
   DWRITE_STRIKETHROUGH const* strikethrough, IUnknown* clientDrawingEffect) {
    return E_NOTIMPL;
  }

  IFACEMETHOD(DrawInlineObject)
  (void* clientDrawingContext, FLOAT originX, FLOAT originY,
   IDWriteInlineObject* inlineObject, BOOL isSideways, BOOL isRightToLeft,
   IUnknown* clientDrawingEffect) {
    return E_NOTIMPL;
  }

  // IDWritePixelSnapping methods

  IFACEMETHOD(IsPixelSnappingDisabled)
  (void* clientDrawingContext, BOOL* isDisabled) {
    *isDisabled = FALSE;
    return S_OK;
  }

  IFACEMETHOD(GetCurrentTransform)
  (void* clientDrawingContext, DWRITE_MATRIX* transform) {
    const DWRITE_MATRIX ident = {1.0, 0.0, 0.0, 1.0, 0.0, 0.0};
    *transform = ident;
    return S_OK;
  }

  IFACEMETHOD(GetPixelsPerDip)
  (void* clientDrawingContext, FLOAT* pixelsPerDip) {
    *pixelsPerDip = 1.0f;
    return S_OK;
  }

  // IUnknown methods

  IFACEMETHOD_(unsigned long, AddRef)() {
    return InterlockedIncrement(&mRefCount);
  }

  IFACEMETHOD_(unsigned long, Release)() {
    unsigned long newCount = InterlockedDecrement(&mRefCount);
    if (newCount == 0) {
      delete this;
      return 0;
    }

    return newCount;
  }

  IFACEMETHOD(QueryInterface)(IID const& riid, void** ppvObject) {
    if (__uuidof(IDWriteTextRenderer) == riid) {
      *ppvObject = this;
    } else if (__uuidof(IDWritePixelSnapping) == riid) {
      *ppvObject = this;
    } else if (__uuidof(IUnknown) == riid) {
      *ppvObject = this;
    } else {
      *ppvObject = nullptr;
      return E_FAIL;
    }

    this->AddRef();
    return S_OK;
  }

  const nsCString& FallbackFamilyName() { return mFamilyName; }

 protected:
  long mRefCount;
  RefPtr<IDWriteFontCollection> mSystemFonts;
  nsCString mFamilyName;
};

class gfxDWriteFontList final : public gfxPlatformFontList {
 public:
  gfxDWriteFontList();
  virtual ~gfxDWriteFontList() { AutoLock lock(mLock); }

  static gfxDWriteFontList* PlatformFontList() {
    return static_cast<gfxDWriteFontList*>(
        gfxPlatformFontList::PlatformFontList());
  }

  // initialize font lists
  nsresult InitFontListForPlatform() MOZ_REQUIRES(mLock) override;
  void InitSharedFontListForPlatform() MOZ_REQUIRES(mLock) override;

  FontVisibility GetVisibilityForFamily(const nsACString& aName) const;

  already_AddRefed<gfxFontFamily> CreateFontFamily(
      const nsACString& aName, FontVisibility aVisibility) const override;

  already_AddRefed<gfxFontEntry> CreateFontEntry(
      mozilla::fontlist::Face* aFace,
      const mozilla::fontlist::Family* aFamily) override;

  void ReadFaceNamesForFamily(mozilla::fontlist::Family* aFamily,
                              bool aNeedFullnamePostscriptNames)
      MOZ_REQUIRES(mLock) override;

  bool ReadFaceNames(const mozilla::fontlist::Family* aFamily,
                     const mozilla::fontlist::Face* aFace, nsCString& aPSName,
                     nsCString& aFullName) override;

  void GetFacesInitDataForFamily(
      const mozilla::fontlist::Family* aFamily,
      nsTArray<mozilla::fontlist::Face::InitData>& aFaces,
      bool aLoadCmaps) const override;

  already_AddRefed<gfxFontEntry> LookupLocalFont(
      FontVisibilityProvider* aFontVisibilityProvider,
      const nsACString& aFontName, WeightRange aWeightForEntry,
      WidthRange aWidthForEntry, SlantStyleRange aStyleForEntry) override;

  already_AddRefed<gfxFontEntry> MakePlatformFont(
      const nsACString& aFontName, WeightRange aWeightForEntry,
      WidthRange aWidthForEntry, SlantStyleRange aStyleForEntry,
      FontData* aFontData) override;

  IDWriteGdiInterop* GetGDIInterop() { return mGDIInterop; }
  bool UseGDIFontTableAccess() const;

  bool FindAndAddFamiliesLocked(
      FontVisibilityProvider* aFontVisibilityProvider,
      mozilla::StyleGenericFontFamily aGeneric, const nsACString& aFamily,
      nsTArray<FamilyAndGeneric>* aOutput, FindFamiliesFlags aFlags,
      gfxFontStyle* aStyle = nullptr, nsAtom* aLanguage = nullptr,
      gfxFloat aDevToCssSize = 1.0) MOZ_REQUIRES(mLock) override;

  gfxFloat GetForceGDIClassicMaxFontSize() {
    return mForceGDIClassicMaxFontSize;
  }

  virtual void AddSizeOfExcludingThis(mozilla::MallocSizeOf aMallocSizeOf,
                                      FontListSizes* aSizes) const;
  virtual void AddSizeOfIncludingThis(mozilla::MallocSizeOf aMallocSizeOf,
                                      FontListSizes* aSizes) const;

 protected:
  FontFamily GetDefaultFontForPlatform(
      FontVisibilityProvider* aFontVisibilityProvider,
      const gfxFontStyle* aStyle, nsAtom* aLanguage = nullptr)
      MOZ_REQUIRES(mLock) override;

  // attempt to use platform-specific fallback for the given character,
  // return null if no usable result found
  gfxFontEntry* PlatformGlobalFontFallback(
      FontVisibilityProvider* aFontVisibilityProvider, const uint32_t aCh,
      Script aRunScript, const gfxFontStyle* aMatchStyle,
      FontFamily& aMatchedFamily) MOZ_REQUIRES(mLock) override;

  nsTArray<std::pair<const char**, uint32_t>> GetFilteredPlatformFontLists()
      override;

 private:
  friend class gfxDWriteFontFamily;

  nsresult GetFontSubstitutes() MOZ_REQUIRES(mLock);

  void GetDirectWriteSubstitutes() MOZ_REQUIRES(mLock);

  virtual bool UsesSystemFallback() { return true; }

  void GetFontsFromCollection(IDWriteFontCollection* aCollection)
      MOZ_REQUIRES(mLock);

  void AppendFamiliesFromCollection(
      IDWriteFontCollection* aCollection,
      nsTArray<mozilla::fontlist::Family::InitData>& aFamilies,
      const nsTArray<nsCString>* aForceClassicFams = nullptr)
      MOZ_REQUIRES(mLock);

  void AddSubstitute(const nsCString& aSubstituteName,
                     const nsCString& aActualFontName, bool aIsHardcoded)
      MOZ_REQUIRES(mLock);

#ifdef MOZ_BUNDLED_FONTS
  already_AddRefed<IDWriteFontCollection> CreateBundledFontsCollection(
      IDWriteFactory* aFactory);
#endif

  /**
   * Fonts listed in the registry as substitutes but for which no actual
   * font family is found.
   */
  nsTArray<nsCString> mNonExistingFonts;

  /**
   * Table of font substitutes, we grab this from the registry to get
   * alternative font names.
   */
  FontFamilyTable mFontSubstitutes;
  nsClassHashtable<nsCStringHashKey, nsCString> mSubstitutions;

  /**
   * Table of hardcoded font substitutes. We use this instead of the one
   * generated from the registry when fingerprinting protections are enabled.
   */
  FontFamilyTable mHardcodedSubstitutes;
  nsClassHashtable<nsCStringHashKey, nsCString> mHardcodedSubstitutions;

  virtual already_AddRefed<FontInfoData> CreateFontInfoData();

  gfxFloat mForceGDIClassicMaxFontSize;

  // whether to use GDI font table access routines
  bool mGDIFontTableAccess;
  RefPtr<IDWriteGdiInterop> mGDIInterop;

  RefPtr<DWriteFontFallbackRenderer> mFallbackRenderer;
  RefPtr<IDWriteTextFormat> mFallbackFormat;

  RefPtr<IDWriteFontCollection> mSystemFonts;
#ifdef MOZ_BUNDLED_FONTS
  RefPtr<IDWriteFontCollection> mBundledFonts;
#endif
};

#endif /* GFX_DWRITEFONTLIST_H */
