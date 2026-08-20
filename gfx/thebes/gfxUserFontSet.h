/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef GFX_USER_FONT_SET_H
#define GFX_USER_FONT_SET_H

#include <new>

#include "PLDHashTable.h"
#include "gfxFontEntry.h"
#include "gfxFontUtils.h"
#include "mozilla/AlreadyAddRefed.h"
#include "mozilla/Atomics.h"
#include "mozilla/Attributes.h"
#include "mozilla/FontPropertyTypes.h"
#include "mozilla/MemoryReporting.h"
#include "mozilla/RecursiveMutex.h"
#include "mozilla/RefPtr.h"
#include "nsCOMPtr.h"
#include "nsHashKeys.h"
#include "nsIMemoryReporter.h"
#include "nsIObserver.h"
#include "nsIScriptError.h"
#include "nsISupports.h"
#include "nsRefPtrHashtable.h"
#include "nsString.h"
#include "nsTArray.h"
#include "nscore.h"

// Only needed for function bodies.
#include <utility>  // for move, forward

#include "MainThreadUtils.h"      // for NS_IsMainThread
#include "gfxFontFeatures.h"      // for gfxFontFeature
#include "gfxFontSrcPrincipal.h"  // for gfxFontSrcPrincipal
#include "gfxFontSrcURI.h"        // for gfxFontSrcURI
#include "mozilla/Assertions.h"  // for AssertionConditionType, MOZ_ASSERT_HELPER2, MOZ_ASSERT, MOZ_ASSERT_UNREACHABLE, MOZ_ASSER...
#include "mozilla/HashFunctions.h"      // for HashBytes, HashGeneric
#include "mozilla/TimeStamp.h"          // for TimeStamp
#include "mozilla/gfx/FontVariation.h"  // for FontVariation
#include "nsDebug.h"                    // for NS_WARNING
#include "nsIReferrerInfo.h"            // for nsIReferrerInfo

class gfxFont;
class gfxUserFontSet;
class nsIFontLoadCompleteCallback;
class nsIRunnable;
struct gfxFontStyle;
struct gfxFontVariationAxis;
struct gfxFontVariationInstance;
template <class T>
class nsMainThreadPtrHandle;

namespace mozilla {
class LogModule;
class PostTraversalTask;
enum class StyleFontDisplay : uint8_t;
}  // namespace mozilla
class nsFontFaceLoader;

// #define DEBUG_USERFONT_CACHE

class gfxFontFaceBufferSource {
  NS_INLINE_DECL_THREADSAFE_REFCOUNTING(gfxFontFaceBufferSource)
 public:
  virtual void TakeBuffer(uint8_t*& aBuffer, uint32_t& aLength) = 0;

 protected:
  virtual ~gfxFontFaceBufferSource() = default;
};

// parsed CSS @font-face rule information
// lifetime: from when @font-face rule processed until font is loaded
struct gfxFontFaceSrc {
  enum SourceType { eSourceType_Local, eSourceType_URL, eSourceType_Buffer };

  SourceType mSourceType;

  // if url, whether to use the origin principal or not
  bool mUseOriginPrincipal = false;

  // Required font technologies.
  mozilla::StyleFontFaceSourceTechFlags mTechFlags;

  // Format hint, if any was specified.
  mozilla::StyleFontFaceSourceFormatKeyword mFormatHint;

  nsCString mLocalName;                     // full font name if local
  RefPtr<gfxFontSrcURI> mURI;               // uri if url
  nsCOMPtr<nsIReferrerInfo> mReferrerInfo;  // referrer info if url
  RefPtr<gfxFontSrcPrincipal>
      mOriginPrincipal;  // principal if url and mUseOriginPrincipal

  RefPtr<gfxFontFaceBufferSource> mBuffer;

  // The principal that should be used for the load. Should only be used for
  // URL sources.
  already_AddRefed<gfxFontSrcPrincipal> LoadPrincipal(
      const gfxUserFontSet&) const;
};

inline bool operator==(const gfxFontFaceSrc& a, const gfxFontFaceSrc& b) {
  if (a.mSourceType != b.mSourceType) {
    return false;
  }
  switch (a.mSourceType) {
    case gfxFontFaceSrc::eSourceType_Local:
      return a.mLocalName == b.mLocalName;
    case gfxFontFaceSrc::eSourceType_URL: {
      if (a.mUseOriginPrincipal != b.mUseOriginPrincipal) {
        return false;
      }
      if (a.mUseOriginPrincipal) {
        if (!a.mOriginPrincipal->Equals(b.mOriginPrincipal)) {
          return false;
        }
      }
      bool equals;
      return a.mFormatHint == b.mFormatHint && a.mTechFlags == b.mTechFlags &&
             (a.mURI == b.mURI || a.mURI->Equals(b.mURI)) &&
             NS_SUCCEEDED(a.mReferrerInfo->Equals(b.mReferrerInfo, &equals)) &&
             equals;
    }
    case gfxFontFaceSrc::eSourceType_Buffer:
      return a.mBuffer == b.mBuffer;
  }
  NS_WARNING("unexpected mSourceType");
  return false;
}

// Lifetime: from when platform font is created until it is deactivated.
class gfxUserFontData final {
 public:
  gfxUserFontData()
      : mSrcIndex(0),
        mMetaOrigLen(0),
        mTechFlags(mozilla::StyleFontFaceSourceTechFlags::Empty()),
        mFormatHint(mozilla::StyleFontFaceSourceFormatKeyword::None),
        mCompression(kUnknownCompression),
        mPrivate(false),
        mIsBuffer(false) {}
  virtual ~gfxUserFontData() = default;

  size_t SizeOfIncludingThis(mozilla::MallocSizeOf aMallocSizeOf) const;

  RefPtr<FontData> mFontData;   // downloaded data buffer, if any
  nsTArray<uint8_t> mMetadata;  // woff metadata block (compressed), if any
  RefPtr<gfxFontSrcURI> mURI;   // URI of the source, if it was url()
  RefPtr<gfxFontSrcPrincipal>
      mPrincipal;         // principal for the download, if url()
  nsCString mLocalName;   // font name used for the source, if local()
  nsCString mRealName;    // original fullname from the font resource
  uint32_t mSrcIndex;     // index in the rule's source list
  uint32_t mMetaOrigLen;  // length needed to decompress metadata
  mozilla::StyleFontFaceSourceTechFlags mTechFlags;  // required font tech
  mozilla::StyleFontFaceSourceFormatKeyword
      mFormatHint;       // format hint for the source used, if any
  uint8_t mCompression;  // compression type
  bool mPrivate;         // whether font belongs to a private window
  bool mIsBuffer;        // whether the font source was a buffer

  enum {
    kUnknownCompression = 0,
    kZlibCompression = 1,
    kBrotliCompression = 2
  };
};

// initially contains a set of userfont font entry objects, replaced with
// platform/user fonts as downloaded

class gfxUserFontFamily : public gfxFontFamily {
 public:
  friend class gfxUserFontSet;

  explicit gfxUserFontFamily(const nsACString& aName)
      : gfxFontFamily(aName, FontVisibility::Webfont) {}

  virtual ~gfxUserFontFamily();

  // Add the given font entry to the end of the family's list.
  void AddFontEntry(gfxFontEntry* aFontEntry) {
    nsCString entryName = aFontEntry->FamilyName();

    mozilla::AutoWriteLock lock(mLock);
    MOZ_ASSERT(!mIsSimpleFamily, "not valid for user-font families");

    // If this entry is already the last in the list, we can bail out without
    // doing any work.
    if (!mAvailableFonts.IsEmpty()) {
      if (mAvailableFonts.LastElement() == aFontEntry) {
        return;
      }
    }

    // Append new entry: we will search faces from the end, so that the last-
    // defined font is the first one in the fontlist used for matching, as per
    // CSS Fonts spec.
    // (It is possible that the entry is already present earlier in the list,
    // but duplication is harmless and it's not worth the cost of searching for
    // an existing entry here.)
    mAvailableFonts.AppendElement(aFontEntry);

    if (entryName.IsEmpty()) {
      aFontEntry->SetFamilyName(Name());
    } else {
#ifdef DEBUG
      nsCString thisName = Name();
      ToLowerCase(thisName);
      ToLowerCase(entryName);
      MOZ_ASSERT(thisName.Equals(entryName));
#endif
    }
    ResetCharacterMap();
  }

  void RemoveFontEntry(gfxFontEntry* aFontEntry) {
    mozilla::AutoWriteLock lock(mLock);
    MOZ_ASSERT(!mIsSimpleFamily, "not valid for user-font families");
    mAvailableFonts.RemoveElement(aFontEntry);
  }

  // Remove all font entries from the family
  void DetachFontEntries() {
    mozilla::AutoWriteLock lock(mLock);
    mAvailableFonts.Clear();
  }
};

class gfxUserFontEntry;
class gfxOTSMessageContext;

struct gfxUserFontAttributes {
  using FontWidth = mozilla::FontWidth;
  using WidthRange = mozilla::WidthRange;
  using FontSlantStyle = mozilla::FontSlantStyle;
  using SlantStyleRange = mozilla::SlantStyleRange;
  using FontWeight = mozilla::FontWeight;
  using WeightRange = mozilla::WeightRange;
  using StyleFontFaceSourceListComponent =
      mozilla::StyleFontFaceSourceListComponent;
  using RangeFlags = gfxFontEntry::RangeFlags;

  WeightRange mWeight = WeightRange(FontWeight::NORMAL);
  WidthRange mWidth = WidthRange(FontWidth::NORMAL);
  SlantStyleRange mStyle = SlantStyleRange(FontSlantStyle::NORMAL);
  RangeFlags mRangeFlags = RangeFlags::eAutoWeight | RangeFlags::eAutoWidth |
                           RangeFlags::eAutoSlantStyle;
  mozilla::StyleFontDisplay mFontDisplay = mozilla::StyleFontDisplay::Auto;
  float mAscentOverride = -1.0;
  float mDescentOverride = -1.0;
  float mLineGapOverride = -1.0;
  float mSizeAdjust = 1.0;
  uint32_t mLanguageOverride = NO_FONT_LANGUAGE_OVERRIDE;
  nsTArray<gfxFontFeature> mFeatureSettings;
  nsTArray<gfxFontVariation> mVariationSettings;
  RefPtr<gfxCharacterMap> mUnicodeRanges;

  nsCString mFamilyName;
  AutoTArray<StyleFontFaceSourceListComponent, 8> mSources;
};

class gfxUserFontSet {
  friend class gfxUserFontEntry;
  friend class gfxOTSMessageContext;

 public:
  using FontWidth = mozilla::FontWidth;
  using WidthRange = mozilla::WidthRange;
  using FontSlantStyle = mozilla::FontSlantStyle;
  using SlantStyleRange = mozilla::SlantStyleRange;
  using FontWeight = mozilla::FontWeight;
  using WeightRange = mozilla::WeightRange;
  using RangeFlags = gfxFontEntry::RangeFlags;

  NS_INLINE_DECL_PURE_VIRTUAL_REFCOUNTING

  gfxUserFontSet();

  void Destroy();

  // creates a font face without adding it to a particular family
  // weight = [100, 900] (multiples of 100)
  // width = [FontWidth::UltraCondensed(), FontWidth::UltraExpanded()]
  // italic style = constants in gfxFontConstants.h, e.g. NS_FONT_STYLE_NORMAL
  // language override = result of calling
  // nsLayoutUtils::ParseFontLanguageOverride
  // TODO: support for unicode ranges not yet implemented
  virtual already_AddRefed<gfxUserFontEntry> CreateUserFontEntry(
      nsTArray<gfxFontFaceSrc>&& aFontFaceSrcList,
      gfxUserFontAttributes&& aAttr) = 0;

  // creates a font face for the specified family, or returns an existing
  // matching entry on the family if there is one
  already_AddRefed<gfxUserFontEntry> FindOrCreateUserFontEntry(
      nsTArray<gfxFontFaceSrc>&& aFontFaceSrcList,
      gfxUserFontAttributes&& aAttr);

  // add in a font face for which we have the gfxUserFontEntry already
  void AddUserFontEntry(const nsCString& aFamilyName,
                        gfxUserFontEntry* aUserFontEntry);

  // Look up and return the gfxUserFontFamily in mFontFamilies with
  // the given name
  virtual already_AddRefed<gfxUserFontFamily> LookupFamily(
      const nsACString& aName) const;

  virtual already_AddRefed<gfxFontSrcPrincipal> GetStandardFontLoadPrincipal()
      const = 0;
  virtual FontVisibilityProvider* GetFontVisibilityProvider() const = 0;

  // check whether content policies allow the given URI to load.
  virtual bool IsFontLoadAllowed(const gfxFontFaceSrc&) = 0;

  // initialize the process that loads external font data, which upon
  // completion will call FontDataDownloadComplete method
  virtual nsresult StartLoad(gfxUserFontEntry* aUserFontEntry,
                             uint32_t aSrcIndex) = 0;

  // generation - each time a face is loaded, generation is
  // incremented so that the change can be recognized
  uint64_t GetGeneration() { return mGeneration; }

  // increment the generation on font load
  void IncrementGeneration(bool aIsRebuild = false) {
    mozilla::RecursiveMutexAutoLock lock(mMutex);
    IncrementGenerationLocked(aIsRebuild);
  }
  void IncrementGenerationLocked(bool aIsRebuild = false) MOZ_REQUIRES(mMutex);

  // Generation is bumped on font loads but that doesn't affect name-style
  // mappings. Rebuilds do however affect name-style mappings so need to
  // lookup fontlists again when that happens.
  uint64_t GetRebuildGeneration() { return mRebuildGeneration; }

  // rebuild if local rules have been used
  void RebuildLocalRules();

  // Discard any font entries created for src:local(), so that they will
  // be reloaded next time they're needed. This is called when the platform
  // font list has changed, which means local font entries that were set up
  // may no longer be valid.
  virtual void ForgetLocalFaces();

  class UserFontCache {
   public:
    // Record a loaded user-font in the cache. This requires that the
    // font-entry's userFontData has been set up already, as it relies
    // on the URI and Principal recorded there.
    static void CacheFont(gfxFontEntry* aFontEntry);

    // The given gfxFontEntry is being destroyed, so remove any record that
    // refers to it.
    static void ForgetFont(gfxFontEntry* aFontEntry);

    // Return the gfxFontEntry corresponding to a given URI and principal,
    // and the features of the given userfont entry, or nullptr if none is
    // available. The aPrivate flag is set for requests coming from private
    // windows, so we can avoid leaking fonts cached in private windows mode out
    // to normal windows.
    static gfxFontEntry* GetFont(const gfxFontFaceSrc&,
                                 const gfxUserFontEntry&);

    // Clear everything so that we don't leak URIs and Principals.
    static void Shutdown();

    // Memory-reporting support.
    class MemoryReporter final : public nsIMemoryReporter {
     private:
      ~MemoryReporter() = default;

     public:
      NS_DECL_ISUPPORTS
      NS_DECL_NSIMEMORYREPORTER
    };

#ifdef DEBUG_USERFONT_CACHE
    // dump contents
    static void Dump();
#endif

   private:
    // Helper that we use to observe the empty-cache notification
    // from nsICacheService.
    class Flusher : public nsIObserver {
      virtual ~Flusher() = default;

     public:
      NS_DECL_ISUPPORTS
      NS_DECL_NSIOBSERVER
      Flusher() = default;
    };

    // Key used to look up entries in the user-font cache.
    // Note that key comparison does *not* use the mFontEntry field
    // as a whole; it only compares specific fields within the entry
    // (weight/width/style/features) that could affect font selection
    // or rendering, and that must match between a font-set's userfont
    // entry and the corresponding "real" font entry.
    struct Key {
      RefPtr<gfxFontSrcURI> mURI;
      RefPtr<gfxFontSrcPrincipal> mPrincipal;  // use nullptr with data: URLs
      // The font entry MUST notify the cache when it is destroyed
      // (by calling ForgetFont()).
      gfxFontEntry* MOZ_NON_OWNING_REF mFontEntry;
      bool mPrivate;

      Key(gfxFontSrcURI* aURI, gfxFontSrcPrincipal* aPrincipal,
          gfxFontEntry* aFontEntry, bool aPrivate)
          : mURI(aURI),
            mPrincipal(aPrincipal),
            mFontEntry(aFontEntry),
            mPrivate(aPrivate) {}
    };

    class Entry : public PLDHashEntryHdr {
     public:
      typedef const Key& KeyType;
      typedef const Key* KeyTypePointer;

      explicit Entry(KeyTypePointer aKey)
          : mURI(aKey->mURI),
            mPrincipal(aKey->mPrincipal),
            mFontEntry(aKey->mFontEntry),
            mPrivate(aKey->mPrivate) {}

      Entry(Entry&& aOther)
          : PLDHashEntryHdr(std::move(aOther)),
            mURI(std::move(aOther.mURI)),
            mPrincipal(std::move(aOther.mPrincipal)),
            mFontEntry(std::move(aOther.mFontEntry)),
            mPrivate(std::move(aOther.mPrivate)) {}

      ~Entry() = default;

      bool KeyEquals(const KeyTypePointer aKey) const;

      static KeyTypePointer KeyToPointer(KeyType aKey) { return &aKey; }

      static PLDHashNumber HashKey(const KeyTypePointer aKey) {
        PLDHashNumber principalHash =
            aKey->mPrincipal ? aKey->mPrincipal->Hash() : 0;
        return mozilla::HashGeneric(
            principalHash + int(aKey->mPrivate), aKey->mURI->Hash(),
            HashFeatures(aKey->mFontEntry->mFeatureSettings),
            HashVariations(aKey->mFontEntry->mVariationSettings),
            mozilla::HashString(aKey->mFontEntry->FamilyName()),
            aKey->mFontEntry->Weight().AsScalar(),
            aKey->mFontEntry->SlantStyle().AsScalar(),
            aKey->mFontEntry->Width().AsScalar(),
            aKey->mFontEntry->AutoRangeFlags(),
            aKey->mFontEntry->mLanguageOverride);
      }

      enum { ALLOW_MEMMOVE = false };

      gfxFontSrcURI* GetURI() const { return mURI; }
      gfxFontSrcPrincipal* GetPrincipal() const { return mPrincipal; }
      gfxFontEntry* GetFontEntry() const { return mFontEntry; }
      bool IsPrivate() const { return mPrivate; }

      void ReportMemory(nsIHandleReportCallback* aHandleReport,
                        nsISupports* aData, bool aAnonymize);

#ifdef DEBUG_USERFONT_CACHE
      void Dump();
#endif

     private:
      static uint32_t HashFeatures(const nsTArray<gfxFontFeature>& aFeatures) {
        return mozilla::HashBytes(aFeatures.Elements(),
                                  aFeatures.Length() * sizeof(gfxFontFeature));
      }

      static uint32_t HashVariations(
          const nsTArray<mozilla::gfx::FontVariation>& aVariations) {
        return mozilla::HashBytes(
            aVariations.Elements(),
            aVariations.Length() * sizeof(mozilla::gfx::FontVariation));
      }

      RefPtr<gfxFontSrcURI> mURI;
      RefPtr<gfxFontSrcPrincipal> mPrincipal;  // or nullptr for data: URLs

      // The "real" font entry corresponding to this downloaded font.
      // The font entry MUST notify the cache when it is destroyed
      // (by calling ForgetFont()).
      gfxFontEntry* MOZ_NON_OWNING_REF mFontEntry;

      // Whether this font was loaded from a private window.
      bool mPrivate;
    };

    static nsTHashtable<Entry>* sUserFonts;
  };

  void SetLocalRulesUsed() { mLocalRulesUsed = true; }

  static mozilla::LogModule* GetUserFontsLog();

  // record statistics about font completion
  virtual void RecordFontLoadDone(uint32_t aFontSize,
                                  mozilla::TimeStamp aDoneTime) {}

  void GetLoadStatistics(uint32_t& aLoadCount, uint64_t& aLoadSize) const {
    aLoadCount = mDownloadCount;
    aLoadSize = mDownloadSize;
  }

 protected:
  // Protected destructor, to discourage deletion outside of Release():
  virtual ~gfxUserFontSet();

  // Return whether the font set is associated with a private-browsing tab.
  virtual bool GetPrivateBrowsing() = 0;

  // Return whether the font set is associated with a document that was
  // shift-reloaded, for example, and thus should bypass the font cache.
  virtual bool BypassCache() = 0;

  // parse data for a data URL
  virtual nsresult SyncLoadFontData(gfxUserFontEntry* aFontToLoad,
                                    const gfxFontFaceSrc* aFontFaceSrc,
                                    uint8_t*& aBuffer,
                                    uint32_t& aBufferLength) = 0;

  // report a problem of some kind (implemented in nsUserFontSet)
  virtual nsresult LogMessage(gfxUserFontEntry* aUserFontEntry,
                              uint32_t aSrcIndex, const char* aMessage,
                              uint32_t aFlags = nsIScriptError::errorFlag,
                              nsresult aStatus = NS_OK) = 0;

  // helper method for performing the actual userfont set rebuild
  virtual void DoRebuildUserFontSet() = 0;

  // forget about a loader that has been cancelled
  virtual void RemoveLoader(nsFontFaceLoader* aLoader) = 0;

  // helper method for FindOrCreateUserFontEntry
  gfxUserFontEntry* FindExistingUserFontEntry(
      gfxUserFontFamily* aFamily,
      const nsTArray<gfxFontFaceSrc>& aFontFaceSrcList,
      const gfxUserFontAttributes& aAttr);

  // creates a new gfxUserFontFamily in mFontFamilies, or returns an existing
  // family if there is one
  virtual already_AddRefed<gfxUserFontFamily> GetFamily(
      const nsACString& aFamilyName);

  void ForgetLocalFace(gfxUserFontFamily* aFontFamily);

  // font families defined by @font-face rules
  nsRefPtrHashtable<nsCStringHashKey, gfxUserFontFamily> mFontFamilies;

  mozilla::Atomic<uint64_t> mGeneration;  // bumped on any font load change
  uint64_t mRebuildGeneration;            // only bumped on rebuilds

  // true when local names have been looked up, false otherwise
  bool mLocalRulesUsed;

  // true when rules using local names need to be redone
  bool mRebuildLocalRules;

  // performance stats
  uint32_t mDownloadCount;
  uint64_t mDownloadSize;

  mutable mozilla::RecursiveMutex mMutex;
};

// acts a placeholder until the real font is downloaded
class gfxUserFontEntry : public gfxFontEntry {
  friend class mozilla::PostTraversalTask;
  friend class gfxUserFontSet;
  friend class nsUserFontSet;
  friend class nsFontFaceLoader;
  friend class gfxOTSMessageContext;

 public:
  enum UserFontLoadState {
    STATUS_NOT_LOADED = 0,
    STATUS_LOAD_PENDING,
    STATUS_LOADING,
    STATUS_LOADED,
    STATUS_FAILED
  };

  gfxUserFontEntry(nsTArray<gfxFontFaceSrc>&& aFontFaceSrcList,
                   gfxUserFontAttributes&& aAttr);

  ~gfxUserFontEntry() override;

  // Update the attributes of the entry to the given values, without disturbing
  // the associated platform font entry or in-progress downloads.
  void UpdateAttributes(gfxUserFontAttributes&& aAttr);

  // Return whether the entry matches the given list of attributes
  bool Matches(const nsTArray<gfxFontFaceSrc>& aFontFaceSrcList,
               const gfxUserFontAttributes& aAttr);

  gfxFont* CreateFontInstance(const gfxFontStyle* aFontStyle) override;

  gfxFontEntry* GetPlatformFontEntry() const { return mPlatformFontEntry; }

  // is the font loading or loaded, or did it fail?
  UserFontLoadState LoadState() const { return mUserFontLoadState; }

  void LoadCanceled() {
    MOZ_ASSERT(NS_IsMainThread());

    mUserFontLoadState = STATUS_NOT_LOADED;
    mFontDataLoadingState = NOT_LOADING;
    mLoader = nullptr;
    // Reset mCurrentSrcIndex so that all potential sources are re-considered.
    mCurrentSrcIndex = 0;
    mSeenLocalSource = false;
  }

  // whether to wait before using fallback font or not
  bool WaitForUserFont() const {
    return (mUserFontLoadState == STATUS_LOAD_PENDING ||
            mUserFontLoadState == STATUS_LOADING) &&
           mFontDataLoadingState < LOADING_SLOWLY;
  }

  // For userfonts, cmap is used to store the unicode range data,
  // and is inert once set, so locking is not required here.
  // no cmap ==> all codepoints permitted
  bool CharacterInUnicodeRange(uint32_t ch) const {
    if (const auto* map = GetUnicodeRangeMap()) {
      return map->test(ch);
    }
    return true;
  }

  gfxCharacterMap* GetUnicodeRangeMap() const { return GetCharacterMapRaw(); }
  void SetUnicodeRangeMap(RefPtr<gfxCharacterMap>&& aCharMap) {
    auto* oldCmap = GetUnicodeRangeMap();
    if (oldCmap != aCharMap) {
      auto* newCmap = aCharMap.forget().take();
      mozilla::AutoWriteLock lock(mLock);
      if (mCharacterMap.compareExchange(oldCmap, newCmap)) {
        NS_IF_RELEASE(oldCmap);
      } else {
        NS_IF_RELEASE(newCmap);
      }
    }
  }

  mozilla::StyleFontDisplay GetFontDisplay() const { return mFontDisplay; }

  // load the font - starts the loading of sources which continues until
  // a valid font resource is found or all sources fail
  void Load();

  // Invalidates appropriately when the load finishes.
  void FontLoadComplete();

  // methods to expose some information to FontFaceSet::UserFontSet
  // since we can't make that class a friend
  void SetLoader(nsFontFaceLoader* aLoader) {
    MOZ_ASSERT(NS_IsMainThread());
    mLoader = aLoader;
  }

  nsFontFaceLoader* GetLoader() const {
    MOZ_ASSERT(NS_IsMainThread());
    return mLoader;
  }

  gfxFontSrcPrincipal* GetPrincipal() const { return mPrincipal; }
  void GetFamilyNameAndURIForLogging(uint32_t aSrcIndex,
                                     nsACString& aFamilyName, nsACString& aURI);

  gfxFontEntry* Clone() const override {
    MOZ_ASSERT_UNREACHABLE("cannot Clone user fonts");
    return nullptr;
  }

  virtual already_AddRefed<gfxUserFontSet> GetUserFontSet() const = 0;

  const nsTArray<gfxFontFaceSrc>& SourceList() const { return mSrcList; }

  // Returns a weak reference to the requested source record, which is owned
  // by the gfxUserFontEntry.
  const gfxFontFaceSrc& SourceAt(uint32_t aSrcIndex) const {
    return mSrcList[aSrcIndex];
  }

  // The variation-query APIs should not be called on placeholders.
  bool HasVariations() override {
    MOZ_ASSERT_UNREACHABLE("not meaningful for a userfont placeholder");
    return false;
  }
  void GetVariationAxes(nsTArray<gfxFontVariationAxis>&) override {
    MOZ_ASSERT_UNREACHABLE("not meaningful for a userfont placeholder");
  }
  void GetVariationInstances(nsTArray<gfxFontVariationInstance>&) override {
    MOZ_ASSERT_UNREACHABLE("not meaningful for a userfont placeholder");
  }

 protected:
  struct OTSMessage {
    nsCString mMessage;
    int mLevel;  // see OTSContext in gfx/ots/include/opentype-sanitizer.h
  };

  const uint8_t* SanitizeOpenTypeData(const uint8_t* aData, uint32_t aLength,
                                      uint32_t& aSanitaryLength,
                                      gfxUserFontType& aFontType,
                                      nsTArray<OTSMessage>& aMessages);

  // attempt to load the next resource in the src list.
  void LoadNextSrc();
  void ContinueLoad();
  void DoLoadNextSrc(bool aIsContinue);

  // change the load state
  virtual void SetLoadState(UserFontLoadState aLoadState);

  // when download has been completed, pass back data here
  // aDownloadStatus == NS_OK ==> download succeeded, error otherwise
  // Ownership of aFontData is passed in here; the font set must
  // ensure that it is eventually deleted with free().
  void FontDataDownloadComplete(uint32_t aSrcIndex, const uint8_t*&& aFontData,
                                uint32_t aLength, nsresult aDownloadStatus,
                                nsIFontLoadCompleteCallback* aCallback);

  // helper method for creating a platform font
  // returns true if platform font creation successful
  // Ownership of aFontData is passed in here; the font must
  // ensure that it is eventually deleted with free().
  bool LoadPlatformFontSync(uint32_t aSrcIndex, const uint8_t*&& aFontData,
                            uint32_t aLength);

  void LoadPlatformFontAsync(uint32_t aSrcIndex, const uint8_t*&& aFontData,
                             uint32_t aLength,
                             nsIFontLoadCompleteCallback* aCallback);

  // helper method for LoadPlatformFontAsync; runs on a background thread
  void StartPlatformFontLoadOnBackgroundThread(
      uint32_t aSrcIndex, const uint8_t*&& aFontData, uint32_t aLength,
      nsMainThreadPtrHandle<nsIFontLoadCompleteCallback> aCallback);

  // helper method for LoadPlatformFontAsync; runs on the main thread
  void ContinuePlatformFontLoadOnMainThread(
      uint32_t aSrcIndex, const uint8_t*&& aOriginalFontData,
      uint32_t aOriginalLength, gfxUserFontType aFontType,
      const uint8_t*&& aSanitizedFontData, uint32_t aSanitizedLength,
      nsTArray<OTSMessage>&& aMessages,
      nsMainThreadPtrHandle<nsIFontLoadCompleteCallback> aCallback);

  // helper method for LoadPlatformFontSync and
  // ContinuePlatformFontLoadOnMainThread; runs on the main thread.
  // This takes ownership of/consumes the original and sanitized data buffers,
  // so the caller may no longer use them.
  bool LoadPlatformFont(uint32_t aSrcIndex, const uint8_t*&& aOriginalFontData,
                        uint32_t aOriginalLength, gfxUserFontType aFontType,
                        const uint8_t*&& aSanitizedFontData,
                        uint32_t aSanitizedLength,
                        nsTArray<OTSMessage>&& aMessages);

  // helper method for FontDataDownloadComplete and
  // ContinuePlatformFontLoadOnMainThread; runs on the main thread
  void FontLoadFailed(nsIFontLoadCompleteCallback* aCallback);

  // store metadata and src details for current src into aFontEntry
  void StoreUserFontData(gfxFontEntry* aFontEntry, uint32_t aSrcIndex,
                         bool aPrivate, const nsACString& aOriginalName,
                         FallibleTArray<uint8_t>* aMetadata,
                         uint32_t aMetaOrigLen, uint8_t aCompression,
                         RefPtr<FontData>&& aFontData);

  // Clears and then adds to aResult all of the user font sets that this user
  // font entry has been added to.  This will at least include the owner of this
  // user font entry.
  virtual void GetUserFontSets(nsTArray<RefPtr<gfxUserFontSet>>& aResult);

  FontTableCache* GetFontTableCache(bool aCreate) override { return nullptr; }

  // general load state
  UserFontLoadState mUserFontLoadState;

  // detailed load state while font data is loading
  // used to determine whether to use fallback font or not
  // note that code depends on the ordering of these values!
  enum FontDataLoadingState {
    NOT_LOADING = 0,      // not started to load any font resources yet
    LOADING_STARTED,      // loading has started; hide fallback font
    LOADING_ALMOST_DONE,  // timeout happened but we're nearly done,
                          // so keep hiding fallback font
    LOADING_SLOWLY,       // timeout happened and we're not nearly done,
                          // so use the fallback font
    LOADING_TIMED_OUT,    // font load took too long
    LOADING_FAILED        // failed to load any source: use fallback
  };
  FontDataLoadingState mFontDataLoadingState;

  bool mSeenLocalSource;
  bool mUnsupportedFormat;
  mozilla::StyleFontDisplay mFontDisplay;  // timing of userfont fallback

  RefPtr<gfxFontEntry> mPlatformFontEntry;
  nsTArray<gfxFontFaceSrc> mSrcList;
  uint32_t mCurrentSrcIndex;  // index of src item to be loaded next
  // This field is managed by the nsFontFaceLoader. In the destructor and
  // Cancel() methods of nsFontFaceLoader this reference is nulled out.
  nsFontFaceLoader* MOZ_NON_OWNING_REF
      mLoader;  // current loader for this entry, if any
  RefPtr<gfxUserFontSet> mLoadingFontSet;
  RefPtr<gfxFontSrcPrincipal> mPrincipal;
};

#endif /* GFX_USER_FONT_SET_H */
