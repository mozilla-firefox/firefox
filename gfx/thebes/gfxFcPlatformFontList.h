/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef GFXFCPLATFORMFONTLIST_H_
#define GFXFCPLATFORMFONTLIST_H_

#include <fontconfig/fontconfig.h>

#include "ft2build.h"
#include "gfxFT2FontBase.h"
#include "gfxPlatformFontList.h"
#include "mozilla/FontPropertyTypes.h"
#include "mozilla/RefPtr.h"
#include "nsClassHashtable.h"
#include FT_FREETYPE_H
#include FT_TRUETYPE_TABLES_H
#include FT_MULTIPLE_MASTERS_H

#ifdef MOZ_FONTATIONS
#  include "mozilla/MemoryMappedFile.h"
#  include "mozilla/gfx/fontations_glue_generated.h"
#endif

#if defined(MOZ_SANDBOX) && defined(XP_LINUX)
#  include "mozilla/SandboxBroker.h"
#endif

namespace mozilla {
namespace dom {
class SystemFontListEntry;
class SystemFontList;
class SystemFontOptions;
};  // namespace dom

template <>
class RefPtrTraits<FcPattern> {
 public:
  static void Release(FcPattern* ptr) { FcPatternDestroy(ptr); }
  static void AddRef(FcPattern* ptr) { FcPatternReference(ptr); }
};

template <>
class RefPtrTraits<FcConfig> {
 public:
  static void Release(FcConfig* ptr) { FcConfigDestroy(ptr); }
  static void AddRef(FcConfig* ptr) { FcConfigReference(ptr); }
};

}  // namespace mozilla

namespace std {
template <>
struct default_delete<FcFontSet> {
  void operator()(FcFontSet* aPtr) { FcFontSetDestroy(aPtr); }
};

template <>
struct default_delete<FcObjectSet> {
  void operator()(FcObjectSet* aPtr) { FcObjectSetDestroy(aPtr); }
};

}  // namespace std

// The names for the font entry and font classes should really
// the common 'Fc' abbreviation but the gfxPangoFontGroup code already
// defines versions of these, so use the verbose name for now.

class gfxFontconfigFontEntry final : public gfxFT2FontEntryBase {
  friend class gfxFcPlatformFontList;
  using FTUserFontData = mozilla::gfx::FTUserFontData;

 public:
  // used for system fonts with explicit patterns
  explicit gfxFontconfigFontEntry(const nsACString& aFaceName,
                                  FcPattern* aFontPattern,
                                  bool aIgnoreFcCharmap);

  // used for data fonts where the fontentry takes ownership
  // of the font data and the FT_Face
  explicit gfxFontconfigFontEntry(const nsACString& aFaceName,
                                  WeightRange aWeight, WidthRange aWidth,
                                  SlantStyleRange aStyle,
                                  RefPtr<mozilla::gfx::SharedFTFace>&& aFace);

  // used for @font-face local system fonts with explicit patterns
  explicit gfxFontconfigFontEntry(const nsACString& aFaceName,
                                  FcPattern* aFontPattern, WeightRange aWeight,
                                  WidthRange aWidth, SlantStyleRange aStyle);

  gfxFontEntry* Clone() const override;

  AutoHBFace GetHBFace() override;

  FcPattern* GetPattern() { return mFontPattern; }

  nsresult ReadCMAP(FontInfoData* aFontInfoData = nullptr) override;
  bool TestCharacterMap(uint32_t aCh) override;

  mozilla::gfx::SharedFTFace* GetFTFace();
  FTUserFontData* GetUserFontData() override;

  FT_MM_Var* GetMMVar() override;

  bool HasVariations() override;
  void GetVariationAxes(nsTArray<gfxFontVariationAxis>& aAxes) override;
  void GetVariationInstances(
      nsTArray<gfxFontVariationInstance>& aInstances) override;

  nsresult CopyFontTable(uint32_t aTableTag, nsTArray<uint8_t>&) override;
  FontTableCache* GetFontTableCache(bool aCreate) override {
    return mFontTableCache;
  };

  double GetAspect(uint8_t aSizeAdjustBasis);

 protected:
  virtual ~gfxFontconfigFontEntry();

  gfxFont* CreateFontInstance(const gfxFontStyle* aFontStyle) override;

  bool HasFontTableInternal(uint32_t aTableTag) override;
  hb_blob_t* GetFontTableInternal(uint32_t aTableTag) override;

  void GetUserFontFeatures(FcPattern* aPattern);

  // pattern for a single face of a family
  RefPtr<FcPattern> mFontPattern;

#ifdef MOZ_FONTATIONS
  void InitSkrifaFont(FcPattern* aPattern);
#endif

  // FTFace - initialized when needed. Once mFTFaceInitialized is true,
  // the face can be accessed without locking.
  // Note that mFTFace owns a reference to the SharedFTFace, but is not
  // a RefPtr because we need it to be an atomic.
  mozilla::Atomic<mozilla::gfx::SharedFTFace*> mFTFace;
  mozilla::Atomic<bool> mFTFaceInitialized;

  // HarfBuzz face, if the entry is backed by a disk file. Initialized on first
  // use.
  mozilla::Atomic<hb_face_t*> mHBFace;

  // Font table cache, created only if we fail to create a hb_face_t that wraps
  // the complete font data.
  mozilla::Atomic<FontTableCache*> mFontTableCache;

  // Whether TestCharacterMap should check the actual cmap rather than asking
  // fontconfig about character coverage.
  // We do this for app-bundled (rather than system) fonts, as they may
  // include color glyphs that fontconfig would overlook, and for fonts
  // loaded via @font-face.
  bool mIgnoreFcCharmap;

  // Whether the face supports variations. For system-installed fonts, we
  // query fontconfig for this (so they will only work if fontconfig is
  // recent enough to include support); for downloaded user-fonts we query
  // the FreeType face.
  enum class HasVariationsState : int8_t {
    Uninitialized = -1,
    No = 0,
    Yes = 1,
  };
  std::atomic<HasVariationsState> mHasVariations =
      HasVariationsState::Uninitialized;

  class UnscaledFontCache {
   public:
    already_AddRefed<mozilla::gfx::UnscaledFontFontconfig> Lookup(
        const std::string& aFile, uint32_t aIndex);

    void Add(const RefPtr<mozilla::gfx::UnscaledFontFontconfig>& aUnscaledFont);

   private:
    // Each mUnscaledFonts entry has a corresponding mGenerations entry. Every
    // time mUnscaledFonts is either modified or read, mLastGeneration is
    // incremented and the last modified or read font generation is set to this.
    static const size_t kNumEntries = 3;
    mozilla::ThreadSafeWeakPtr<mozilla::gfx::UnscaledFontFontconfig>
        mUnscaledFonts[kNumEntries];
    mozilla::Atomic<int32_t> mGenerations[kNumEntries];
    mozilla::Atomic<int32_t> mLastGeneration{0};
  };

  UnscaledFontCache mUnscaledFontCache;

  // Because of FreeType bug 52955, we keep the FT_MM_Var struct when it is
  // first loaded, rather than releasing it and re-fetching it as needed.
  FT_MM_Var* mMMVar = nullptr;
  bool mMMVarInitialized = false;
};

class gfxFontconfigFontFamily final : public gfxFontFamily {
 public:
  gfxFontconfigFontFamily(const nsACString& aName, FontVisibility aVisibility)
      : gfxFontFamily(aName, aVisibility),
        mContainsAppFonts(false),
        mHasNonScalableFaces(false),
        mForceScalable(false) {}

  template <typename Func>
  void AddFacesToFontList(Func aAddPatternFunc);

  void FindStyleVariationsLocked(FontInfoData* aFontInfoData = nullptr)
      MOZ_REQUIRES(mLock) override;

  // Families are constructed initially with just references to patterns.
  // When necessary, these are enumerated within FindStyleVariations.
  void AddFontPattern(FcPattern* aFontPattern, bool aSingleName);

  void SetFamilyContainsAppFonts(bool aContainsAppFonts) {
    mContainsAppFonts = aContainsAppFonts;
  }

  void FindAllFontsForStyle(const gfxFontStyle& aFontStyle,
                            nsTArray<gfxFontEntry*>& aFontEntryList,
                            bool aIgnoreSizeTolerance) override;

  bool FilterForFontList(nsAtom* aLangGroup,
                         const nsACString& aGeneric) const final {
    return SupportsLangGroup(aLangGroup);
  }

 protected:
  virtual ~gfxFontconfigFontFamily();

  // helper for FilterForFontList
  bool SupportsLangGroup(nsAtom* aLangGroup) const;

  nsTArray<RefPtr<FcPattern>> mFontPatterns;

  // Number of faces that have a single name. Faces that have multiple names are
  // sorted last.
  uint32_t mUniqueNameFaceCount = 0;
  bool mContainsAppFonts : 1;
  bool mHasNonScalableFaces : 1;
  bool mForceScalable : 1;
};

class gfxFontconfigFont final : public gfxFT2FontBase {
 public:
  gfxFontconfigFont(
      const RefPtr<mozilla::gfx::UnscaledFontFontconfig>& aUnscaledFont,
      RefPtr<mozilla::gfx::SharedFTFace>&& aFTFace, FcPattern* aPattern,
      gfxFloat aAdjustedSize, gfxFontEntry* aFontEntry,
      const gfxFontStyle* aFontStyle, int aLoadFlags, bool aEmbolden);

  FontType GetType() const override { return FONT_TYPE_FONTCONFIG; }
  FcPattern* GetPattern() const { return mPattern; }

  already_AddRefed<mozilla::gfx::ScaledFont> GetScaledFont(
      const TextRunDrawParams& aRunParams) override;

  bool ShouldHintMetrics() const override;

 private:
  ~gfxFontconfigFont() override;

  RefPtr<FcPattern> mPattern;
};

class gfxFcPlatformFontList final : public gfxPlatformFontList {
  using FontPatternListEntry = mozilla::dom::SystemFontListEntry;

 public:
  gfxFcPlatformFontList();

  static gfxFcPlatformFontList* PlatformFontList() {
    return static_cast<gfxFcPlatformFontList*>(
        gfxPlatformFontList::PlatformFontList());
  }

  // initialize font lists
  nsresult InitFontListForPlatform() MOZ_REQUIRES(mLock) override;
  void InitSharedFontListForPlatform() MOZ_REQUIRES(mLock) override;

  void GetFontList(nsAtom* aLangGroup, const nsACString& aGenericFamily,
                   nsTArray<nsString>& aListOfFonts) override;

  void ReadSystemFontList(mozilla::dom::SystemFontList*);

  already_AddRefed<gfxFontEntry> CreateFontEntry(
      mozilla::fontlist::Face* aFace,
      const mozilla::fontlist::Family* aFamily) override;

  already_AddRefed<gfxFontEntry> LookupLocalFont(
      FontVisibilityProvider* aFontVisibilityProvider,
      const nsACString& aFontName, WeightRange aWeightForEntry,
      WidthRange aWidthForEntry, SlantStyleRange aStyleForEntry) override;

  already_AddRefed<gfxFontEntry> MakePlatformFont(
      const nsACString& aFontName, WeightRange aWeightForEntry,
      WidthRange aWidthForEntry, SlantStyleRange aStyleForEntry,
      FontData* aFontData) override;

  bool FindAndAddFamiliesLocked(
      FontVisibilityProvider* aFontVisibilityProvider,
      mozilla::StyleGenericFontFamily aGeneric, const nsACString& aFamily,
      nsTArray<FamilyAndGeneric>* aOutput, FindFamiliesFlags aFlags,
      gfxFontStyle* aStyle = nullptr, nsAtom* aLanguage = nullptr,
      gfxFloat aDevToCssSize = 1.0) MOZ_REQUIRES(mLock) override;

  bool GetStandardFamilyName(const nsCString& aFontName,
                             nsACString& aFamilyName) override;

  FcConfig* GetLastConfig() const { return mLastConfig; }

  // override to use fontconfig lookup for generics
  void AddGenericFonts(FontVisibilityProvider* aFontVisibilityProvider,
                       mozilla::StyleGenericFontFamily, nsAtom* aLanguage,
                       nsTArray<FamilyAndGeneric>& aFamilyList) override;

  void ClearLangGroupPrefFontsLocked() MOZ_REQUIRES(mLock) override;

  // clear out cached generic-lang ==> family-list mappings
  void ClearGenericMappings() {
    AutoLock lock(mLock);
    ClearGenericMappingsLocked();
  }
  void ClearGenericMappingsLocked() MOZ_REQUIRES(mLock) {
    mGenericMappings.Clear();
  }

  // map lang group ==> lang string
  void GetSampleLangForGroup(nsAtom* aLanguage, nsACString& aLangStr);

 protected:
  virtual ~gfxFcPlatformFontList();

  nsTArray<std::pair<const char**, uint32_t>> GetFilteredPlatformFontLists()
      override;

#if defined(MOZ_SANDBOX) && defined(XP_LINUX)
  typedef mozilla::SandboxBroker::Policy SandboxPolicy;
#else
  // Dummy type just so we can still have a SandboxPolicy* parameter.
  struct SandboxPolicy {};
#endif

  // Add all the font families found in a font set.
  // aAppFonts indicates whether this is the system or application fontset.
  void AddFontSetFamilies(FcFontSet* aFontSet, const SandboxPolicy* aPolicy,
                          bool aAppFonts) MOZ_REQUIRES(mLock);

  // Helper for above, to add a single font pattern.
  void AddPatternToFontList(FcPattern* aFont, FcChar8*& aLastFamilyName,
                            nsACString& aFamilyName,
                            RefPtr<gfxFontconfigFontFamily>& aFontFamily,
                            bool aAppFonts) MOZ_REQUIRES(mLock);

  // figure out which families fontconfig maps a generic to
  // (aGeneric assumed already lowercase)
  PrefFontList* FindGenericFamilies(
      FontVisibilityProvider* aFontVisibilityProvider,
      const nsCString& aGeneric, nsAtom* aLanguage) MOZ_REQUIRES(mLock);

  // are all pref font settings set to use fontconfig generics?
  bool PrefFontListsUseOnlyGenerics() MOZ_REQUIRES(mLock);

  static void CheckFontUpdates(nsITimer* aTimer, void* aThis);

  FontFamily GetDefaultFontForPlatform(
      FontVisibilityProvider* aFontVisibilityProvider,
      const gfxFontStyle* aStyle, nsAtom* aLanguage = nullptr)
      MOZ_REQUIRES(mLock) override;

  FontVisibility GetVisibilityForFamily(const nsACString& aName) const;

  already_AddRefed<gfxFontFamily> CreateFontFamily(
      const nsACString& aName, FontVisibility aVisibility) const override;

  // helper method for finding an appropriate lang string
  bool TryLangForGroup(const nsACString& aOSLang, nsAtom* aLangGroup,
                       nsACString& aLang);

#ifdef MOZ_BUNDLED_FONTS
  void ActivateBundledFonts();
  nsCString mBundledFontsPath;
  bool mBundledFontsInitialized;
#endif

  // to avoid enumerating all fonts, maintain a mapping of local font
  // names to family
  nsTHashMap<nsCString, RefPtr<FcPattern>> mLocalNames;

  // caching generic/lang ==> font family list
  nsClassHashtable<nsCStringHashKey, PrefFontList> mGenericMappings;

  // Caching family lookups as found by FindAndAddFamilies after resolving
  // substitutions. The gfxFontFamily objects cached here are owned by the
  // gfxFcPlatformFontList via its mFamilies table; note that if the main
  // font list is rebuilt (e.g. due to a fontconfig configuration change),
  // these pointers will be invalidated. InitFontList() flushes the cache
  // in this case.
  nsTHashMap<nsCStringHashKey, nsTArray<FamilyAndGeneric>> mFcSubstituteCache;

  nsCOMPtr<nsITimer> mCheckFontUpdatesTimer;
  RefPtr<FcConfig> mLastConfig;

  // The current system font options in effect.
#ifdef MOZ_WIDGET_GTK
  // NOTE(emilio): This is a *system cairo* cairo_font_options_t object. As
  // such, it can't be used outside of the few functions defined here.
  cairo_font_options_t* mSystemFontOptions = nullptr;
  int32_t mFreetypeLcdSetting = -1;  // -1 for not set

  void ClearSystemFontOptions();

  // Returns whether options actually changed.
  // TODO(emilio): We could call this when gsettings change or such, but
  // historically we haven't reacted to these settings changes, so keeping it
  // simple for now.
  bool UpdateSystemFontOptions();

  void UpdateSystemFontOptionsFromIpc(const mozilla::dom::SystemFontOptions&);
  void SystemFontOptionsToIpc(mozilla::dom::SystemFontOptions&);

 public:
  void SubstituteSystemFontOptions(FcPattern*);

 private:
#endif

  // Cache for most recently used language code in FindAndAddFamiliesLocked,
  // and the result of checking whether to use lang-specific lookups.
  RefPtr<nsAtom> mPrevLanguage;
  nsCString mSampleLang;
  bool mUseCustomLookups = false;

  // By default, font prefs under Linux are set to simply lookup
  // via fontconfig the appropriate font for serif/sans-serif/monospace.
  // Rather than check each time a font pref is used, check them all at startup
  // and set a boolean to flag the case that non-default user font prefs exist
  // Note: langGroup == x-math is handled separately
  bool mAlwaysUseFontconfigGenerics;

  static FT_Library sFTLibrary;
};

#endif /* GFXPLATFORMFONTLIST_H_ */
