/* -*- Mode: C++; tab-width: 20; indent-tabs-mode: nil; c-basic-offset: 2 -*-
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef GFXPLATFORMFONTLIST_H_
#define GFXPLATFORMFONTLIST_H_

#include "nsClassHashtable.h"
#include "nsTHashMap.h"
#include "nsTHashSet.h"
#include "nsRefPtrHashtable.h"
#include "nsTHashtable.h"

#include "gfxFontUtils.h"
#include "gfxFontInfoLoader.h"
#include "gfxFont.h"
#include "gfxFontConstants.h"
#include "gfxPlatform.h"
#include "SharedFontList.h"

#include "nsIMemoryReporter.h"
#include "mozilla/Attributes.h"
#include "mozilla/EnumeratedArray.h"
#include "mozilla/FontPropertyTypes.h"
#include "mozilla/MemoryReporting.h"
#include "mozilla/RangedArray.h"
#include "mozilla/RecursiveMutex.h"
#include "nsLanguageAtomService.h"

#include "base/shared_memory.h"

namespace mozilla {
namespace fontlist {
struct AliasData;
}
}  // namespace mozilla

// Lookup key for the shared-cmap hashtable. The hash is carried separately
// from the gfxCharacterMap pointer so that MaybeRemoveCmap can look up an
// entry without dereferencing the (potentially already freed) pointer.
struct CharMapLookup {
  // The cmap to look up. In pointer-compare mode this may be dangling and is
  // used for pointer-equality only; in content-compare mode it must be alive.
  gfxCharacterMap* mCharMap;
  // The cmap's mHash, captured while the object was known to be alive.
  uint32_t mHash;
  // If true, KeyEquals compares by pointer identity (no deref of mCharMap).
  // If false, KeyEquals compares by content (derefs mCharMap; must be alive).
  bool mCompareByPointer;
};

class CharMapHashKey : public PLDHashEntryHdr {
 public:
  typedef CharMapLookup KeyType;
  typedef const CharMapLookup* KeyTypePointer;

  explicit CharMapHashKey(const CharMapLookup* aLookup)
      : mCharMap(aLookup->mCharMap) {
    // Only content-compare lookups (FindCharMap) insert; the cmap is alive.
    MOZ_ASSERT(!aLookup->mCompareByPointer);
    MOZ_COUNT_CTOR(CharMapHashKey);
  }
  CharMapHashKey(const CharMapHashKey& toCopy) : mCharMap(toCopy.mCharMap) {
    MOZ_COUNT_CTOR(CharMapHashKey);
  }
  MOZ_COUNTED_DTOR(CharMapHashKey)

  gfxCharacterMap* GetCharMap() const { return mCharMap.get(); }

  bool KeyEquals(const CharMapLookup* aLookup) const {
    if (aLookup->mCompareByPointer) {
      // Pointer-identity compare for MaybeRemoveCmap. Does not dereference
      // aLookup->mCharMap, which may be dangling.
      return mCharMap.get() == aLookup->mCharMap;
    }
    // Content compare for FindCharMap. aLookup->mCharMap must be alive.
    MOZ_ASSERT(!aLookup->mCharMap->mBuildOnTheFly && !mCharMap->mBuildOnTheFly,
               "custom cmap used in shared cmap hashtable");
    if (aLookup->mHash != mCharMap->mHash) {
      return false;
    }
    return mCharMap->Equals(aLookup->mCharMap);
  }

  static const CharMapLookup* KeyToPointer(const CharMapLookup& aLookup) {
    return &aLookup;
  }
  static PLDHashNumber HashKey(const CharMapLookup* aLookup) {
    // Use the captured hash; never dereference aLookup->mCharMap.
    return aLookup->mHash;
  }

  enum { ALLOW_MEMMOVE = true };

 protected:
  friend class gfxPlatformFontList;

  // gfxCharacterMap::Release() will notify us when the refcount of a
  // charmap drops to 1; at that point, we'll lock the cache, check if
  // the charmap is owned by the cache and this is still the only ref,
  // and if so, delete it.
  RefPtr<gfxCharacterMap> mCharMap;
};

/**
 * A helper class used to create a SharedBitSet instance in a FontList's shared
 * memory, while ensuring that we avoid bloating memory by avoiding creating
 * duplicate instances.
 */
class ShmemCharMapHashEntry final : public PLDHashEntryHdr {
 public:
  typedef const gfxSparseBitSet* KeyType;
  typedef const gfxSparseBitSet* KeyTypePointer;

  /**
   * Creation from a gfxSparseBitSet creates not only the ShmemCharMapHashEntry
   * itself, but also a SharedBitSet in shared memory.
   * Only the parent process creates and manages these entries.
   */
  explicit ShmemCharMapHashEntry(const gfxSparseBitSet* aCharMap);

  ShmemCharMapHashEntry(ShmemCharMapHashEntry&&) = default;
  ShmemCharMapHashEntry& operator=(ShmemCharMapHashEntry&&) = default;

  /**
   * Return a shared-memory Pointer that refers to the wrapped SharedBitSet.
   * This can be passed to content processes to give them access to the same
   * SharedBitSet as the parent stored.
   */
  mozilla::fontlist::Pointer GetCharMap() const { return mCharMap; }

  bool KeyEquals(KeyType aCharMap) const {
    // mHash is a 32-bit Adler checksum of the bitset; if it doesn't match we
    // can immediately reject it as non-matching, but if it is equal we still
    // need to do a full equality check below.
    if (mHash != aCharMap->GetChecksum()) {
      return false;
    }

    return mCharMap.ToPtr<const SharedBitSet>(mList)->Equals(aCharMap);
  }

  static KeyTypePointer KeyToPointer(KeyType aCharMap) { return aCharMap; }
  static PLDHashNumber HashKey(KeyType aCharMap) {
    return aCharMap->GetChecksum();
  }

  enum { ALLOW_MEMMOVE = true };

 private:
  // charMaps are stored in the shared memory that FontList objects point to,
  // and are never deleted until the FontList (all referencing font lists,
  // actually) have gone away.
  mozilla::fontlist::FontList* mList;
  mozilla::fontlist::Pointer mCharMap;
  uint32_t mHash;
};

// gfxPlatformFontList is an abstract class for the global font list on the
// system; concrete subclasses for each platform implement the actual interface
// to the system fonts. This class exists because we cannot rely on the platform
// font-finding APIs to behave in sensible/similar ways, particularly with rich,
// complex OpenType families, so we do our own font family/style management here
// instead.

// Much of this is based on the old gfxQuartzFontCache, but adapted for use on
// all platforms.

struct FontListSizes {
  uint32_t mFontListSize;  // size of the font list and dependent objects
                           // (font family and face names, etc), but NOT
                           // including the font table cache and the cmaps
  uint32_t
      mFontTableCacheSize;  // memory used for the gfxFontEntry table caches
  uint32_t mCharMapsSize;   // memory used for cmap coverage info
  uint32_t mLoaderSize;     // memory used for (platform-specific) loader
  uint32_t mSharedSize;     // shared-memory use (reported by parent only)
};

class gfxUserFontSet;

class gfxPlatformFontList : public gfxFontInfoLoader {
  friend class InitOtherFamilyNamesRunnable;

 public:
  typedef mozilla::StretchRange StretchRange;
  typedef mozilla::SlantStyleRange SlantStyleRange;
  typedef mozilla::WeightRange WeightRange;
  typedef mozilla::intl::Script Script;

  using AutoLock = mozilla::RecursiveMutexAutoLock;
  using AutoUnlock = mozilla::RecursiveMutexAutoUnlock;

  // Class used to hold cached copies of the font-name prefs, so that they can
  // be accessed from non-main-thread callers who are not allowed to touch the
  // Preferences service.
  class FontPrefs final {
   public:
    using HashMap = nsTHashMap<nsCStringHashKey, nsCString>;

    FontPrefs();
    ~FontPrefs() = default;

    FontPrefs(const FontPrefs& aOther) = delete;
    FontPrefs& operator=(const FontPrefs& aOther) = delete;

    // Lookup the font.name.<foo> or font.name-list.<foo> pref for a given
    // generic+langgroup pair.
    bool LookupName(const nsACString& aPref, nsACString& aValue) const;
    bool LookupNameList(const nsACString& aPref, nsACString& aValue) const;

    // Does the font.name-list.emoji pref have a user-set value?
    bool EmojiHasUserValue() const { return mEmojiHasUserValue; }

    // Expose iterators over all the defined prefs of each type.
    HashMap::ConstIterator NameIter() const { return mFontName.ConstIter(); }
    HashMap::ConstIterator NameListIter() const {
      return mFontNameList.ConstIter();
    }

   private:
    static constexpr char kNamePrefix[] = "font.name.";
    static constexpr char kNameListPrefix[] = "font.name-list.";

    void Init();

    HashMap mFontName;
    HashMap mFontNameList;
    bool mEmojiHasUserValue = false;
  };

  // For font family lists loaded from user preferences (prefs such as
  // font.name-list.<generic>.<langGroup>) that map CSS generics to
  // platform-specific font families.
  typedef nsTArray<FontFamily> PrefFontList;

  static gfxPlatformFontList* PlatformFontList() {
    // If there is a font-list initialization thread, we need to let it run
    // to completion before the font list can be used for anything else.
    if (sInitFontListThread) {
      // If we're currently on the initialization thread, just continue;
      // otherwise wait for it to finish.
      if (IsInitFontListThread()) {
        return sPlatformFontList;
      }
      PR_JoinThread(sInitFontListThread);
      sInitFontListThread = nullptr;
      // If font-list initialization failed, the thread will have cleared
      // the static sPlatformFontList pointer; we cannot proceed without any
      // usable fonts.
      if (!sPlatformFontList) {
        MOZ_CRASH("Could not initialize gfxPlatformFontList");
      }
    }
    if (!sPlatformFontList->IsInitialized()) {
      if (!sPlatformFontList->InitFontList()) {
        MOZ_CRASH("Could not initialize gfxPlatformFontList");
      }
    }
    return sPlatformFontList;
  }

  static bool Initialize(gfxPlatformFontList* aList);

  static void Shutdown() {
    delete sPlatformFontList;
    sPlatformFontList = nullptr;
  }

  bool IsInitialized() const { return mFontlistInitCount; }

  virtual ~gfxPlatformFontList();

  // Initialize font lists; return true on success, false if something fails.
  bool InitFontList();

  void FontListChanged();

  /**
   * Gathers (from a platform's underlying font system) the information needed
   * to initialize a fontlist::Family with its Face members.
   */
  virtual void GetFacesInitDataForFamily(
      const mozilla::fontlist::Family* aFamily,
      nsTArray<mozilla::fontlist::Face::InitData>& aFaces,
      bool aLoadCmaps) const {}

  virtual void GetFontList(nsAtom* aLangGroup, const nsACString& aGenericFamily,
                           nsTArray<nsString>& aListOfFonts);

  // Pass false to notify content that the shared font list has been modified
  // but not completely invalidated.
  void UpdateFontList(bool aFullRebuild = true);

  void ClearLangGroupPrefFonts() {
    AutoLock lock(mLock);
    ClearLangGroupPrefFontsLocked();
  }
  virtual void ClearLangGroupPrefFontsLocked() MOZ_REQUIRES(mLock);

  void GetFontFamilyList(nsTArray<RefPtr<gfxFontFamily>>& aFamilyArray);

  already_AddRefed<gfxFont> SystemFindFontForChar(
      nsPresContext* aPresContext, uint32_t aCh, uint32_t aNextCh,
      Script aRunScript, eFontPresentation aPresentation,
      const gfxFontStyle* aStyle, FontVisibility* aVisibility);

  // Flags to control optional behaviors in FindAndAddFamilies. The sense
  // of the bit flags have been chosen such that the default parameter of
  // FindFamiliesFlags(0) in FindFamily will give the most commonly-desired
  // behavior, and only a few callsites need to explicitly pass other values.
  enum class FindFamiliesFlags {
    // If set, "other" (e.g. localized) family names should be loaded
    // immediately; if clear, InitOtherFamilyNames is allowed to defer
    // loading to avoid blocking.
    eForceOtherFamilyNamesLoading = 1 << 0,

    // If set, FindAndAddFamilies should not check for legacy "styled
    // family" names to add to the font list. This is used to avoid
    // a recursive search when using FindFamily to find a potential base
    // family name for a styled variant.
    eNoSearchForLegacyFamilyNames = 1 << 1,

    // If set, FindAndAddFamilies will not add a missing entry to
    // mOtherNamesMissed
    eNoAddToNamesMissedWhenSearching = 1 << 2,

    // If set, the family name was quoted and so must not be treated as a CSS
    // generic.
    eQuotedFamilyName = 1 << 3,

    // If set, "hidden" font families (like ".SF NS Text" on macOS) are
    // searched in addition to standard user-visible families.
    eSearchHiddenFamilies = 1 << 4,
  };

  // Find family(ies) matching aFamily and append to the aOutput array
  // (there may be multiple results in the case of fontconfig aliases, etc).
  // Return true if any match was found and appended, false if none.
  bool FindAndAddFamilies(
      nsPresContext* aPresContext, mozilla::StyleGenericFontFamily aGeneric,
      const nsACString& aFamily, nsTArray<FamilyAndGeneric>* aOutput,
      FindFamiliesFlags aFlags, gfxFontStyle* aStyle = nullptr,
      nsAtom* aLanguage = nullptr, gfxFloat aDevToCssSize = 1.0) {
    AutoLock lock(mLock);
    return FindAndAddFamiliesLocked(aPresContext, aGeneric, aFamily, aOutput,
                                    aFlags, aStyle, aLanguage, aDevToCssSize);
  }
  virtual bool FindAndAddFamiliesLocked(
      nsPresContext* aPresContext, mozilla::StyleGenericFontFamily aGeneric,
      const nsACString& aFamily, nsTArray<FamilyAndGeneric>* aOutput,
      FindFamiliesFlags aFlags, gfxFontStyle* aStyle = nullptr,
      nsAtom* aLanguage = nullptr, gfxFloat aDevToCssSize = 1.0)
      MOZ_REQUIRES(mLock);

  gfxFontEntry* FindFontForFamily(nsPresContext* aPresContext,
                                  const nsACString& aFamily,
                                  const gfxFontStyle* aStyle);

  mozilla::fontlist::FontList* SharedFontList() const {
    return mSharedFontList;
  }

  // Create a handle for a single shmem block (identified by index) ready to
  // be shared to the given processId.
  void ShareFontListShmBlockToProcess(uint32_t aGeneration, uint32_t aIndex,
                                      base::ProcessId aPid,
                                      base::SharedMemoryHandle* aOut);

  // Populate the array aBlocks with the complete list of shmem handles ready
  // to be shared to the given processId.
  void ShareFontListToProcess(nsTArray<base::SharedMemoryHandle>* aBlocks,
                              base::ProcessId aPid);

  void ShmBlockAdded(uint32_t aGeneration, uint32_t aIndex,
                     base::SharedMemoryHandle aHandle);

  base::SharedMemoryHandle ShareShmBlockToProcess(uint32_t aIndex,
                                                  base::ProcessId aPid);

  void SetCharacterMap(uint32_t aGeneration, uint32_t aFamilyIndex, bool aAlias,
                       uint32_t aFaceIndex, const gfxSparseBitSet& aMap);

  void SetupFamilyCharMap(uint32_t aGeneration, uint32_t aIndex, bool aAlias);

  // Start the async cmap loading process, if not already under way, from the
  // given family index. (For use in any process that needs font lookups.)
  void StartCmapLoadingFromFamily(uint32_t aStartIndex);

  // [Parent] Handle request from content process to start cmap loading.
  void StartCmapLoading(uint32_t aGeneration, uint32_t aStartIndex);

  void CancelLoadCmapsTask() {
    if (mLoadCmapsRunnable) {
      mLoadCmapsRunnable->Cancel();
      mLoadCmapsRunnable = nullptr;
    }
  }

  // Populate aFamily with face records, and if aLoadCmaps is true, also load
  // their character maps (rather than leaving this to be done lazily).
  // Note that even when aFamily->IsInitialized() is true, it can make sense
  // to call InitializeFamily again if passing aLoadCmaps=true, in order to
  // ensure cmaps are loaded.
  [[nodiscard]] bool InitializeFamily(mozilla::fontlist::Family* aFamily,
                                      bool aLoadCmaps = false);
  void InitializeFamily(uint32_t aGeneration, uint32_t aFamilyIndex,
                        bool aLoadCmaps);

  // name lookup table methods

  void AddOtherFamilyNames(gfxFontFamily* aFamilyEntry,
                           const nsTArray<nsCString>& aOtherFamilyNames);

  void AddFullname(gfxFontEntry* aFontEntry, const nsCString& aFullname) {
    AutoLock lock(mLock);
    AddFullnameLocked(aFontEntry, aFullname);
  }
  void AddFullnameLocked(gfxFontEntry* aFontEntry, const nsCString& aFullname)
      MOZ_REQUIRES(mLock);

  void AddPostscriptName(gfxFontEntry* aFontEntry,
                         const nsCString& aPostscriptName) {
    AutoLock lock(mLock);
    AddPostscriptNameLocked(aFontEntry, aPostscriptName);
  }
  void AddPostscriptNameLocked(gfxFontEntry* aFontEntry,
                               const nsCString& aPostscriptName)
      MOZ_REQUIRES(mLock);

  bool NeedFullnamePostscriptNames() { return mExtraNames != nullptr; }

  /**
   * Read PSName and FullName of the given face, for src:local lookup,
   * returning true if actually implemented and succeeded.
   */
  virtual bool ReadFaceNames(const mozilla::fontlist::Family* aFamily,
                             const mozilla::fontlist::Face* aFace,
                             nsCString& aPSName, nsCString& aFullName) {
    return false;
  }

  // initialize localized family names
  bool InitOtherFamilyNames(bool aDeferOtherFamilyNamesLoading);
  bool InitOtherFamilyNames(uint32_t aGeneration, bool aDefer);

  // pure virtual functions, to be provided by concrete subclasses

  // get the system default font family
  FontFamily GetDefaultFont(nsPresContext* aPresContext,
                            const gfxFontStyle* aStyle);
  FontFamily GetDefaultFontLocked(nsPresContext* aPresContext,
                                  const gfxFontStyle* aStyle)
      MOZ_REQUIRES(mLock);

  // get the "ultimate" default font, for use if the font list is otherwise
  // unusable (e.g. in the middle of being updated)
  gfxFontEntry* GetDefaultFontEntry() {
    AutoLock lock(mLock);
    return mDefaultFontEntry.get();
  }

  /**
   * Look up a font by name on the host platform.
   *
   * Note that the style attributes (weight, stretch, style) are NOT used in
   * selecting the platform font, which is looked up by name only; these are
   * values to be recorded in the new font entry.
   */
  virtual gfxFontEntry* LookupLocalFont(nsPresContext* aPresContext,
                                        const nsACString& aFontName,
                                        WeightRange aWeightForEntry,
                                        StretchRange aStretchForEntry,
                                        SlantStyleRange aStyleForEntry) = 0;

  /**
   * Create a new platform font from downloaded data (@font-face).
   *
   * Note that the style attributes (weight, stretch, style) are NOT related
   * (necessarily) to any values within the font resource itself; these are
   * values to be recorded in the new font entry and used for face selection,
   * in place of whatever inherent style attributes the resource may have.
   *
   * This method takes ownership of the data block passed in as aFontData,
   * and must ensure it is free()'d when no longer required.
   */
  virtual gfxFontEntry* MakePlatformFont(const nsACString& aFontName,
                                         WeightRange aWeightForEntry,
                                         StretchRange aStretchForEntry,
                                         SlantStyleRange aStyleForEntry,
                                         const uint8_t* aFontData,
                                         uint32_t aLength) = 0;

  // get the standard family name on the platform for a given font name
  // (platforms may override, eg Mac)
  virtual bool GetStandardFamilyName(const nsCString& aFontName,
                                     nsACString& aFamilyName);

  // Get the localized family name for a given font family.
  bool GetLocalizedFamilyName(const FontFamily& aFamily,
                              nsACString& aFamilyName);

  // get the default font name which is available on the system from
  // font.name-list.*.  if there are no available fonts in the pref,
  // returns an empty FamilyAndGeneric record.
  FamilyAndGeneric GetDefaultFontFamily(const nsACString& aLangGroup,
                                        const nsACString& aGenericFamily);

  virtual void AddSizeOfExcludingThis(mozilla::MallocSizeOf aMallocSizeOf,
                                      FontListSizes* aSizes) const;
  virtual void AddSizeOfIncludingThis(mozilla::MallocSizeOf aMallocSizeOf,
                                      FontListSizes* aSizes) const;

  mozilla::fontlist::Pointer GetShmemCharMap(const gfxSparseBitSet* aCmap) {
    AutoLock lock(mLock);
    return GetShmemCharMapLocked(aCmap);
  }
  mozilla::fontlist::Pointer GetShmemCharMapLocked(const gfxSparseBitSet* aCmap)
      MOZ_REQUIRES(mLock);

  // Search for existing cmap that matches the input; return the input if no
  // match is found.
  already_AddRefed<gfxCharacterMap> FindCharMap(gfxCharacterMap* aCmap);

  // Remove the cmap from the shared cmap set if it holds the only remaining
  // reference to the object. aCharMap may be dangling; aHash is its mHash
  // captured while it was known alive.
  void MaybeRemoveCmap(gfxCharacterMap* aCharMap, uint32_t aHash);

  // Keep track of userfont sets to notify when global fontlist changes occur.
  void AddUserFontSet(gfxUserFontSet* aUserFontSet) {
    AutoLock lock(mLock);
    mUserFontSetList.Insert(aUserFontSet);
  }

  void RemoveUserFontSet(gfxUserFontSet* aUserFontSet) {
    AutoLock lock(mLock);
    mUserFontSetList.Remove(aUserFontSet);
  }

  static const gfxFontEntry::ScriptRange sComplexScriptRanges[];

  void GetFontlistInitInfo(uint32_t& aNumInits, uint32_t& aLoaderState) {
    aNumInits = mFontlistInitCount;
    aLoaderState = (uint32_t)mState;
  }

  virtual void AddGenericFonts(nsPresContext* aPresContext,
                               mozilla::StyleGenericFontFamily aGenericType,
                               nsAtom* aLanguage,
                               nsTArray<FamilyAndGeneric>& aFamilyList);

  /**
   * Given a Face from the shared font list, return a gfxFontEntry usable
   * by the current process. This returns a cached entry if available,
   * otherwise it calls the (platform-specific) CreateFontEntry method to
   * make one, and adds it to the cache.
   */
  gfxFontEntry* GetOrCreateFontEntry(mozilla::fontlist::Face* aFace,
                                     const mozilla::fontlist::Family* aFamily) {
    AutoLock lock(mLock);
    return GetOrCreateFontEntryLocked(aFace, aFamily);
  }
  gfxFontEntry* GetOrCreateFontEntryLocked(
      mozilla::fontlist::Face* aFace, const mozilla::fontlist::Family* aFamily)
      MOZ_REQUIRES(mLock);

  const FontPrefs* GetFontPrefs() const MOZ_REQUIRES(mLock) {
    return mFontPrefs.get();
  }

  bool EmojiPrefHasUserValue() const {
    AutoLock lock(mLock);
    return mFontPrefs->EmojiHasUserValue();
  }

  PrefFontList* GetPrefFontsLangGroup(
      nsPresContext* aPresContext, mozilla::StyleGenericFontFamily aGenericType,
      eFontPrefLang aPrefLang) {
    AutoLock lock(mLock);
    return GetPrefFontsLangGroupLocked(aPresContext, aGenericType, aPrefLang);
  }
  PrefFontList* GetPrefFontsLangGroupLocked(
      nsPresContext* aPresContext, mozilla::StyleGenericFontFamily aGenericType,
      eFontPrefLang aPrefLang) MOZ_REQUIRES(mLock);

  // in some situations, need to make decisions about ambiguous characters, may
  // need to look at multiple pref langs
  void GetLangPrefs(eFontPrefLang aPrefLangs[], uint32_t& aLen,
                    eFontPrefLang aCharLang, eFontPrefLang aPageLang);

  // convert a lang group to enum constant (i.e. "zh-TW" ==>
  // eFontPrefLang_ChineseTW)
  static eFontPrefLang GetFontPrefLangFor(const char* aLang);

  // convert a lang group atom to enum constant
  static eFontPrefLang GetFontPrefLangFor(nsAtom* aLang);

  // convert an enum constant to a lang group atom
  static nsAtom* GetLangGroupForPrefLang(eFontPrefLang aLang);

  // convert a enum constant to lang group string (i.e. eFontPrefLang_ChineseTW
  // ==> "zh-TW")
  static const char* GetPrefLangName(eFontPrefLang aLang);

  // map a char code to a font language for Preferences
  static eFontPrefLang GetFontPrefLangFor(uint32_t aCh);

  // returns true if a pref lang is CJK
  static bool IsLangCJK(eFontPrefLang aLang);

  // helper method to add a pref lang to an array, if not already in array
  static void AppendPrefLang(eFontPrefLang aPrefLangs[], uint32_t& aLen,
                             eFontPrefLang aAddLang);

  // default serif/sans-serif choice based on font.default.xxx prefs
  mozilla::StyleGenericFontFamily GetDefaultGeneric(eFontPrefLang aLang);

  // Returns true if the font family whitelist is not empty. In this case we
  // ignore the "CSS visibility level"; only the given fonts are present in
  // the browser's font list.
  bool IsFontFamilyWhitelistActive() const {
    return mFontFamilyWhitelistActive;
  };

  static void FontWhitelistPrefChanged(const char* aPref, void* aClosure);

  bool AddWithLegacyFamilyName(const nsACString& aLegacyName,
                               gfxFontEntry* aFontEntry,
                               FontVisibility aVisibility);

  static const char* GetGenericName(
      mozilla::StyleGenericFontFamily aGenericType);

  bool SkipFontFallbackForChar(FontVisibility aVisibility, uint32_t aCh) const {
    AutoLock lock(mLock);
    return mCodepointsWithNoFonts[aVisibility].test(aCh);
  }

  // Return whether the given font-family record should be visible to CSS,
  // in a context with the given FontVisibility setting.
  bool IsVisibleToCSS(const gfxFontFamily& aFamily,
                      FontVisibility aVisibility) const;
  bool IsVisibleToCSS(const mozilla::fontlist::Family& aFamily,
                      FontVisibility aVisibility) const;

  // (Re-)initialize the set of codepoints that we know cannot be rendered.
  void InitializeCodepointsWithNoFonts() MOZ_REQUIRES(mLock);

  // If using the shared font list, returns a generation count that is
  // incremented if/when the platform list is reinitialized (e.g. because
  // fonts are installed/removed while the browser is running), such that
  // existing references to shared font family or face objects and character
  // maps will no longer be valid.
  // (The legacy (non-shared) list just returns 0 here.)
  uint32_t GetGeneration() const { return mFontListGeneration; }

  // Sometimes we need to know if we're on the InitFontList startup thread.
  static bool IsInitFontListThread() {
    return PR_GetCurrentThread() == sInitFontListThread;
  }

  bool IsKnownIconFontFamily(const nsAtom* aFamilyName) const;
  void LoadIconFontOverrideList();

  void Lock() MOZ_CAPABILITY_ACQUIRE(mLock) { mLock.Lock(); }
  void Unlock() MOZ_CAPABILITY_RELEASE(mLock) { mLock.Unlock(); }

  // This is only public because some external callers want to be able to
  // assert about the locked status.
  mutable mozilla::RecursiveMutex mLock;

 protected:
  friend class mozilla::fontlist::FontList;
  friend class InitOtherFamilyNamesForStylo;

  template <size_t N>
  static bool FamilyInList(const nsACString& aName, const char* (&aList)[N]) {
    return FamilyInList(aName, aList, N);
  }
  static bool FamilyInList(const nsACString& aName, const char* aList[],
                           size_t aCount);

  // Check list is correctly sorted (in debug build only; no-op on release).
  template <size_t N>
  static void CheckFamilyList(const char* (&aList)[N]) {
    CheckFamilyList(aList, N);
  }
  static void CheckFamilyList(const char* aList[], size_t aCount);

  class InitOtherFamilyNamesRunnable : public mozilla::CancelableRunnable {
   public:
    InitOtherFamilyNamesRunnable()
        : CancelableRunnable(
              "gfxPlatformFontList::InitOtherFamilyNamesRunnable"),
          mIsCanceled(false) {}

    NS_IMETHOD Run() override {
      if (mIsCanceled) {
        return NS_OK;
      }

      gfxPlatformFontList* fontList = gfxPlatformFontList::PlatformFontList();
      if (!fontList) {
        return NS_OK;
      }

      fontList->InitOtherFamilyNamesInternal(true);

      return NS_OK;
    }

    nsresult Cancel() override {
      mIsCanceled = true;

      return NS_OK;
    }

   private:
    bool mIsCanceled;
  };

  class MemoryReporter final : public nsIMemoryReporter {
    ~MemoryReporter() = default;

   public:
    NS_DECL_ISUPPORTS
    NS_DECL_NSIMEMORYREPORTER
  };

  class PrefName final : public nsAutoCString {
    void Init(const nsACString& aGeneric, const nsACString& aLangGroup) {
      Assign(aGeneric);
      if (!aLangGroup.IsEmpty()) {
        Append('.');
        Append(aLangGroup);
      }
    }

   public:
    PrefName(const nsACString& aGeneric, const nsACString& aLangGroup) {
      Init(aGeneric, aLangGroup);
    }

    PrefName(const char* aGeneric, const char* aLangGroup) {
      Init(nsDependentCString(aGeneric), nsDependentCString(aLangGroup));
    }

    PrefName(const char* aGeneric, nsAtom* aLangGroup) {
      if (aLangGroup) {
        Init(nsDependentCString(aGeneric), nsAtomCString(aLangGroup));
      } else {
        Init(nsDependentCString(aGeneric), nsAutoCString());
      }
    }
  };

  explicit gfxPlatformFontList(bool aNeedFullnamePostscriptNames = true);

  static gfxPlatformFontList* sPlatformFontList;

  /**
   * Convenience method to return the first matching family (if any) as found
   * by FindAndAddFamilies(). The family will be initialized (synchronously)
   * if this has not already been done, so the returned pointer, if non-null,
   * is ready for use.
   */
  mozilla::fontlist::Family* FindSharedFamily(
      nsPresContext* aPresContext, const nsACString& aFamily,
      FindFamiliesFlags aFlags = FindFamiliesFlags(0),
      gfxFontStyle* aStyle = nullptr, nsAtom* aLanguage = nullptr,
      gfxFloat aDevToCssSize = 1.0) MOZ_REQUIRES(mLock);

  gfxFontFamily* FindUnsharedFamily(
      nsPresContext* aPresContext, const nsACString& aFamily,
      FindFamiliesFlags aFlags = FindFamiliesFlags(0),
      gfxFontStyle* aStyle = nullptr, nsAtom* aLanguage = nullptr,
      gfxFloat aDevToCssSize = 1.0) MOZ_REQUIRES(mLock) {
    if (SharedFontList()) {
      return nullptr;
    }
    AutoTArray<FamilyAndGeneric, 1> families;
    if (FindAndAddFamiliesLocked(
            aPresContext, mozilla::StyleGenericFontFamily::None, aFamily,
            &families, aFlags, aStyle, aLanguage, aDevToCssSize)) {
      return families[0].mFamily.mUnshared;
    }
    return nullptr;
  }

  FontFamily FindFamily(nsPresContext* aPresContext, const nsACString& aFamily,
                        FindFamiliesFlags aFlags = FindFamiliesFlags(0),
                        gfxFontStyle* aStyle = nullptr,
                        nsAtom* aLanguage = nullptr,
                        gfxFloat aDevToCssSize = 1.0) MOZ_REQUIRES(mLock) {
    if (SharedFontList()) {
      return FontFamily(FindSharedFamily(aPresContext, aFamily, aFlags, aStyle,
                                         aLanguage, aDevToCssSize));
    }
    return FontFamily(FindUnsharedFamily(aPresContext, aFamily, aFlags, aStyle,
                                         aLanguage, aDevToCssSize));
  }

  // Lookup family name in global family list without substitutions or
  // localized family name lookup. Used for common font fallback families.
  gfxFontFamily* FindFamilyByCanonicalName(const nsACString& aFamily)
      MOZ_REQUIRES(mLock) {
    nsAutoCString key;
    gfxFontFamily* familyEntry;
    GenerateFontListKey(aFamily, key);
    if ((familyEntry = mFontFamilies.GetWeak(key))) {
      return CheckFamily(familyEntry);
    }
    return nullptr;
  }

  // returns default font for a given character, null otherwise
  already_AddRefed<gfxFont> CommonFontFallback(nsPresContext* aPresContext,
                                               uint32_t aCh, uint32_t aNextCh,
                                               Script aRunScript,
                                               eFontPresentation aPresentation,
                                               const gfxFontStyle* aMatchStyle,
                                               FontFamily& aMatchedFamily)
      MOZ_REQUIRES(mLock);

  // Search fonts system-wide for a given character, null if not found.
  already_AddRefed<gfxFont> GlobalFontFallback(
      nsPresContext* aPresContext, uint32_t aCh, uint32_t aNextCh,
      Script aRunScript, eFontPresentation aPresentation,
      const gfxFontStyle* aMatchStyle, uint32_t& aCmapCount,
      FontFamily& aMatchedFamily) MOZ_REQUIRES(mLock);

  // Platform-specific implementation of global font fallback, if any;
  // this may return nullptr in which case the default cmap-based fallback
  // will be performed.
  virtual gfxFontEntry* PlatformGlobalFontFallback(
      nsPresContext* aPresContext, const uint32_t aCh, Script aRunScript,
      const gfxFontStyle* aMatchStyle, FontFamily& aMatchedFamily) {
    return nullptr;
  }

  // whether system-based font fallback is used or not
  // if system fallback is used, no need to load all cmaps
  virtual bool UsesSystemFallback() { return false; }

  void AppendCJKPrefLangs(eFontPrefLang aPrefLangs[], uint32_t& aLen,
                          eFontPrefLang aCharLang, eFontPrefLang aPageLang)
      MOZ_REQUIRES(mLock);

  // verifies that a family contains a non-zero font count
  gfxFontFamily* CheckFamily(gfxFontFamily* aFamily) MOZ_REQUIRES(mLock);

  // initialize localized family names
  void InitOtherFamilyNamesInternal(bool aDeferOtherFamilyNamesLoading);
  void CancelInitOtherFamilyNamesTask();

  void AddToMissedNames(const nsCString& aKey) MOZ_REQUIRES(mLock);

  // search through font families, looking for a given name, initializing
  // facename lists along the way. first checks all families with names
  // close to face name, then searchs all families if not found.
  gfxFontEntry* SearchFamiliesForFaceName(const nsACString& aFaceName)
      MOZ_REQUIRES(mLock);

  // helper method for finding fullname/postscript names in facename lists
  gfxFontEntry* FindFaceName(const nsACString& aFaceName) MOZ_REQUIRES(mLock);

  // look up a font by name, for cases where platform font list
  // maintains explicit mappings of fullname/psname ==> font
  virtual gfxFontEntry* LookupInFaceNameLists(const nsACString& aFaceName)
      MOZ_REQUIRES(mLock);

  gfxFontEntry* LookupInSharedFaceNameList(nsPresContext* aPresContext,
                                           const nsACString& aFaceName,
                                           WeightRange aWeightForEntry,
                                           StretchRange aStretchForEntry,
                                           SlantStyleRange aStyleForEntry)
      MOZ_REQUIRES(mLock);

  // load the bad underline blocklist from pref.
  void LoadBadUnderlineList();

  void GenerateFontListKey(const nsACString& aKeyName, nsACString& aResult);

  virtual void GetFontFamilyNames(nsTArray<nsCString>& aFontFamilyNames)
      MOZ_REQUIRES(mLock);

  // helper function to map lang to lang group
  nsAtom* GetLangGroup(nsAtom* aLanguage);

  // gfxFontInfoLoader overrides, used to load in font cmaps
  void InitLoader() MOZ_REQUIRES(mLock) override;
  bool LoadFontInfo() override;
  void CleanupLoader() override;

  void ForceGlobalReflowLocked(
      gfxPlatform::NeedsReframe aNeedsReframe,
      gfxPlatform::BroadcastToChildren aBroadcastToChildren =
          gfxPlatform::BroadcastToChildren::Yes) MOZ_REQUIRES(mLock);

  // read the loader initialization prefs, and start it
  void GetPrefsAndStartLoader();

  // If aForgetLocalFaces is true, all gfxFontEntries for src:local fonts must
  // be discarded (not potentially reused to satisfy the rebuilt rules),
  // because they may no longer be valid.
  void RebuildLocalFonts(bool aForgetLocalFaces = false) MOZ_REQUIRES(mLock);

  void ResolveGenericFontNames(nsPresContext* aPresContext,
                               mozilla::StyleGenericFontFamily aGenericType,
                               eFontPrefLang aPrefLang,
                               PrefFontList* aGenericFamilies)
      MOZ_REQUIRES(mLock);

  void ResolveEmojiFontNames(nsPresContext* aPresContext,
                             PrefFontList* aGenericFamilies)
      MOZ_REQUIRES(mLock);

  void GetFontFamiliesFromGenericFamilies(
      nsPresContext* aPresContext, mozilla::StyleGenericFontFamily aGenericType,
      nsTArray<nsCString>& aGenericNameFamilies, nsAtom* aLangGroup,
      PrefFontList* aFontFamilies) MOZ_REQUIRES(mLock);

  virtual nsresult InitFontListForPlatform() MOZ_REQUIRES(mLock) = 0;
  virtual void InitSharedFontListForPlatform() MOZ_REQUIRES(mLock) {}

  virtual gfxFontEntry* CreateFontEntry(
      mozilla::fontlist::Face* aFace,
      const mozilla::fontlist::Family* aFamily) {
    return nullptr;
  }

  /**
   * Methods to apply the font.system.whitelist anti-fingerprinting pref,
   * by filtering the list of installed fonts so that only whitelisted families
   * are exposed.
   * There are separate implementations of this for the per-process font list
   * and for the shared-memory font list.
   */
  void ApplyWhitelist() MOZ_REQUIRES(mLock);
  void ApplyWhitelist(nsTArray<mozilla::fontlist::Family::InitData>& aFamilies);

  // Create a new gfxFontFamily of the appropriate subclass for the platform,
  // used when AddWithLegacyFamilyName needs to create a new family.
  virtual gfxFontFamily* CreateFontFamily(const nsACString& aName,
                                          FontVisibility aVisibility) const = 0;

  /**
   * For the post-startup font info loader task.
   * Perform platform-specific work to read alternate names (if any) for a
   * font family, recording them in mAliasTable. Once alternate names have been
   * loaded for all families, the accumulated records are stored in the shared
   * font list's mAliases list.
   * Some platforms (currently Linux/fontconfig) may load alternate names as
   * part of initially populating the font list with family records, in which
   * case this method is unused.
   */
  virtual void ReadFaceNamesForFamily(mozilla::fontlist::Family* aFamily,
                                      bool aNeedFullnamePostscriptNames)
      MOZ_REQUIRES(mLock) {}

  typedef nsRefPtrHashtable<nsCStringHashKey, gfxFontFamily> FontFamilyTable;
  typedef nsRefPtrHashtable<nsCStringHashKey, gfxFontEntry> FontEntryTable;

  // used by memory reporter to accumulate sizes of family names in the table
  static size_t SizeOfFontFamilyTableExcludingThis(
      const FontFamilyTable& aTable, mozilla::MallocSizeOf aMallocSizeOf);
  static size_t SizeOfFontEntryTableExcludingThis(
      const FontEntryTable& aTable, mozilla::MallocSizeOf aMallocSizeOf);

  // Platform-specific helper for GetDefaultFont(...).
  virtual FontFamily GetDefaultFontForPlatform(nsPresContext* aPresContext,
                                               const gfxFontStyle* aStyle,
                                               nsAtom* aLanguage = nullptr)
      MOZ_REQUIRES(mLock) = 0;

  // canonical family name ==> family entry (unique, one name per family entry)
  FontFamilyTable mFontFamilies MOZ_GUARDED_BY(mLock);

  // other family name ==> family entry (not unique, can have multiple names per
  // family entry, only names *other* than the canonical names are stored here)
  FontFamilyTable mOtherFamilyNames MOZ_GUARDED_BY(mLock);

  // flag set after InitOtherFamilyNames is called upon first name lookup miss
  mozilla::Atomic<bool> mOtherFamilyNamesInitialized;

  // The pending InitOtherFamilyNames() task.
  RefPtr<mozilla::CancelableRunnable> mPendingOtherFamilyNameTask;

  // flag set after fullname and Postcript name lists are populated
  mozilla::Atomic<bool> mFaceNameListsInitialized;

  struct ExtraNames {
    ExtraNames() = default;

    // fullname ==> font entry (unique, one name per font entry)
    FontEntryTable mFullnames{64};
    // Postscript name ==> font entry (unique, one name per font entry)
    FontEntryTable mPostscriptNames{64};
  };
  // The lock is needed to guard access to the actual name tables, but does not
  // need to be held to just test whether mExtraNames is non-null as it is set
  // during initialization before other threads have a chance to see it.
  mozilla::UniquePtr<ExtraNames> mExtraNames MOZ_PT_GUARDED_BY(mLock);

  // face names missed when face name loading takes a long time
  mozilla::UniquePtr<nsTHashSet<nsCString>> mFaceNamesMissed
      MOZ_GUARDED_BY(mLock);

  // localized family names missed when face name loading takes a long time
  mozilla::UniquePtr<nsTHashSet<nsCString>> mOtherNamesMissed
      MOZ_GUARDED_BY(mLock);

  typedef mozilla::RangedArray<mozilla::UniquePtr<PrefFontList>,
                               size_t(mozilla::StyleGenericFontFamily::None),
                               size_t(
                                   mozilla::StyleGenericFontFamily::MozEmoji)>
      PrefFontsForLangGroup;
  mozilla::RangedArray<PrefFontsForLangGroup, eFontPrefLang_First,
                       eFontPrefLang_Count>
      mLangGroupPrefFonts MOZ_GUARDED_BY(mLock);
  mozilla::UniquePtr<PrefFontList> mEmojiPrefFont MOZ_GUARDED_BY(mLock);

  // When system-wide font lookup fails for a character, cache it to skip future
  // searches. This is an array of bitsets, one for each FontVisibility level.
  mozilla::EnumeratedArray<FontVisibility, FontVisibility::Count,
                           gfxSparseBitSet>
      mCodepointsWithNoFonts MOZ_GUARDED_BY(mLock);

  // the family to use for U+FFFD fallback, to avoid expensive search every time
  // on pages with lots of problems
  mozilla::EnumeratedArray<FontVisibility, FontVisibility::Count, FontFamily>
      mReplacementCharFallbackFamily MOZ_GUARDED_BY(mLock);

  // Sorted array of lowercased family names; use ContainsSorted to test
  nsTArray<nsCString> mBadUnderlineFamilyNames;

  // character map data shared across families
  // contains weak ptrs to cmaps shared by font entry objects
  nsTHashtable<CharMapHashKey> mSharedCmaps MOZ_GUARDED_BY(mLock);

  nsTHashtable<ShmemCharMapHashEntry> mShmemCharMaps MOZ_GUARDED_BY(mLock);

  // data used as part of the font cmap loading process
  nsTArray<RefPtr<gfxFontFamily>> mFontFamiliesToLoad MOZ_GUARDED_BY(mLock);
  uint32_t mStartIndex MOZ_GUARDED_BY(mLock) = 0;
  uint32_t mNumFamilies MOZ_GUARDED_BY(mLock) = 0;

  // xxx - info for diagnosing no default font aborts
  // see bugs 636957, 1070983, 1189129
  uint32_t mFontlistInitCount = 0;  // num times InitFontList called

  nsTHashSet<gfxUserFontSet*> mUserFontSetList MOZ_GUARDED_BY(mLock);

  nsLanguageAtomService* mLangService = nullptr;

  nsTArray<uint32_t> mCJKPrefLangs MOZ_GUARDED_BY(mLock);
  nsTArray<mozilla::StyleGenericFontFamily> mDefaultGenericsLangGroup
      MOZ_GUARDED_BY(mLock);

  nsTArray<nsCString> mEnabledFontsList;
  nsTHashSet<nsCString> mIconFontsSet;

  // This is an owning reference; we are responsible to delete the FontList at
  // appropriate times.
  std::atomic<mozilla::fontlist::FontList*> mSharedFontList = nullptr;

  nsClassHashtable<nsCStringHashKey, mozilla::fontlist::AliasData> mAliasTable;
  nsTHashMap<nsCStringHashKey, mozilla::fontlist::LocalFaceRec::InitData>
      mLocalNameTable;

  nsRefPtrHashtable<nsPtrHashKey<const mozilla::fontlist::Face>, gfxFontEntry>
      mFontEntries MOZ_GUARDED_BY(mLock);

  mozilla::UniquePtr<FontPrefs> mFontPrefs;

  RefPtr<gfxFontEntry> mDefaultFontEntry MOZ_GUARDED_BY(mLock);

  RefPtr<mozilla::CancelableRunnable> mLoadCmapsRunnable;
  uint32_t mStartedLoadingCmapsFrom MOZ_GUARDED_BY(mLock) = 0xffffffffu;

  // Cached value of mSharedFontList->GetGeneration(), updated by InitFontList.
  std::atomic<uint32_t> mFontListGeneration = 0;

  bool mFontFamilyWhitelistActive = false;

  static PRThread* sInitFontListThread;
};

MOZ_MAKE_ENUM_CLASS_BITWISE_OPERATORS(gfxPlatformFontList::FindFamiliesFlags)

#endif /* GFXPLATFORMFONTLIST_H_ */
