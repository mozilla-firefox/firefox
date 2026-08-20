/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef GFX_FT2FONTLIST_H
#define GFX_FT2FONTLIST_H

#include "gfxFT2FontBase.h"
#include "gfxPlatformFontList.h"
#include "mozilla/MemoryReporting.h"
#include "nsTHashSet.h"

namespace mozilla {
namespace dom {
class SystemFontListEntry;
};
namespace gfx {
class FTUserFontData;
};
};  // namespace mozilla

class FontNameCache;
typedef struct FT_FaceRec_* FT_Face;
class nsZipArchive;
class WillShutdownObserver;

class FT2FontEntry final : public gfxFT2FontEntryBase {
  friend class gfxFT2FontList;

  using FontListEntry = mozilla::dom::SystemFontListEntry;
  using FTUserFontData = mozilla::gfx::FTUserFontData;

 public:
  explicit FT2FontEntry(const nsACString& aFaceName)
      : gfxFT2FontEntryBase(aFaceName), mFTFontIndex(0) {}

  gfxFontEntry* Clone() const override;

  const nsCString& GetName() const { return Name(); }

  // create a font entry for a downloaded font
  static already_AddRefed<FT2FontEntry> CreateFontEntry(
      const nsACString& aFontName, WeightRange aWeight, WidthRange aWidth,
      SlantStyleRange aStyle, FontData* aFontData);

  // create a font entry representing an installed font, identified by
  // a FontListEntry; the freetype and cairo faces will not be instantiated
  // until actually needed
  // TODO(dholbert) Ideally this should return already_AddRefed like the
  // one for downloaded fonts does. See bug 2046683.
  static FT2FontEntry* CreateFontEntry(const FontListEntry& aFLE);

  // Create a font entry with the given name; if it is an installed font,
  // also record the filename and index.
  // If a non-null harfbuzz face is passed, also set style/weight/stretch
  // properties of the entry from the values in the face.
  // TODO(dholbert) Ideally this should return already_AddRefed like the
  // one for downloaded fonts does. See bug 2046683.
  static FT2FontEntry* CreateFontEntry(const nsACString& aName,
                                       const char* aFilename, uint8_t aIndex,
                                       const hb_face_t* aFace);

  gfxFont* CreateFontInstance(const gfxFontStyle* aStyle) override;

  nsresult ReadCMAP(FontInfoData* aFontInfoData = nullptr) override;

  nsresult CopyFontTable(uint32_t aTableTag, nsTArray<uint8_t>&) override;

  bool HasVariations() override;
  void GetVariationAxes(
      nsTArray<gfxFontVariationAxis>& aVariationAxes) override;
  void GetVariationInstances(
      nsTArray<gfxFontVariationInstance>& aInstances) override;

  // Check for various kinds of brokenness, and set flags on the entry
  // accordingly so that we avoid using bad font tables
  void CheckForBrokenFont(gfxFontFamily* aFamily);
  void CheckForBrokenFont(const nsACString& aFamilyKey);

  already_AddRefed<mozilla::gfx::SharedFTFace> GetFTFace(bool aCommit = false);
  FTUserFontData* GetUserFontData() override;

  FT_MM_Var* GetMMVar() override;

  // Get a harfbuzz face for this font, if possible. The caller is responsible
  // to destroy the face when no longer needed.
  // Note that a face may be cached by the font entry, and a new reference
  // returned to the caller.
  // This may be a bit expensive, so avoid calling multiple times if the same
  // face can be re-used for several purposes instead.
  hb_face_t* CreateHBFace();

  /**
   * Append this face's metadata to aFaceList for storage in the FontNameCache
   * (for faster startup).
   * The aPSName and aFullName parameters here can in principle be empty,
   * but if they are missing for a given face then src:local() lookups will
   * not be able to find it when the shared font list is in use.
   */
  void AppendToFaceList(nsCString& aFaceList, const nsACString& aFamilyName,
                        const nsACString& aPSName, const nsACString& aFullName,
                        FontVisibility aVisibility);

  void AddSizeOfExcludingThis(mozilla::MallocSizeOf aMallocSizeOf,
                              FontListSizes* aSizes) const override;
  void AddSizeOfIncludingThis(mozilla::MallocSizeOf aMallocSizeOf,
                              FontListSizes* aSizes) const override;

  nsCString mFilename;
  uint8_t mFTFontIndex;

 protected:
  // Protected destructor, to discourage deletion outside of Release():
  ~FT2FontEntry();

  hb_blob_t* GetFontTableInternal(uint32_t aTableTag) override;

  bool HasFontTableInternal(uint32_t aTableTag) override;

  FontTableCache* GetFontTableCache(bool aCreate) override;

  mozilla::Atomic<FontTableCache*> mFontTableCache;

  mozilla::Atomic<hb_face_t*> mHBFace;

  // Strong reference (addref'd), but held in an atomic ptr rather than a
  // normal RefPtr.
  mozilla::Atomic<mozilla::gfx::SharedFTFace*> mFTFace;

  FT_MM_Var* mMMVar = nullptr;

  mozilla::ThreadSafeWeakPtr<mozilla::gfx::UnscaledFontFreeType> mUnscaledFont;

  nsTHashSet<uint32_t> mAvailableTables;

  enum class HasVariationsState : int8_t {
    Uninitialized = -1,
    No = 0,
    Yes = 1,
  };
  std::atomic<HasVariationsState> mHasVariations =
      HasVariationsState::Uninitialized;

  bool mMMVarInitialized = false;
};

class FT2FontFamily final : public gfxFontFamily {
  using FontListEntry = mozilla::dom::SystemFontListEntry;

 public:
  explicit FT2FontFamily(const nsACString& aName, FontVisibility aVisibility)
      : gfxFontFamily(aName, aVisibility) {}

  // Append this family's faces to the IPC fontlist
  void AddFacesToFontList(nsTArray<FontListEntry>* aFontList);

  void FinalizeMemberList(bool aSortFaces);
};

class gfxFT2FontList final : public gfxPlatformFontList {
  using FontListEntry = mozilla::dom::SystemFontListEntry;

 public:
  gfxFT2FontList();
  virtual ~gfxFT2FontList();

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

  void WriteCache();

  void ReadSystemFontList(mozilla::dom::SystemFontList*);

  static gfxFT2FontList* PlatformFontList() {
    return static_cast<gfxFT2FontList*>(
        gfxPlatformFontList::PlatformFontList());
  }

  already_AddRefed<gfxFontFamily> CreateFontFamily(
      const nsACString& aName, FontVisibility aVisibility) const override;

  void WillShutdown();

 protected:
  typedef enum { kUnknown, kStandard } StandardFile;

  // initialize font lists
  nsresult InitFontListForPlatform() MOZ_REQUIRES(mLock) override;

  FontVisibility GetVisibilityForFamily(const nsACString& aName) const;

  void AppendFaceFromFontListEntry(const FontListEntry& aFLE,
                                   StandardFile aStdFile) MOZ_REQUIRES(mLock);

  void AppendFacesFromBlob(const nsCString& aFileName, StandardFile aStdFile,
                           hb_blob_t* aBlob, FontNameCache* aCache,
                           uint32_t aTimestamp, uint32_t aFilesize)
      MOZ_REQUIRES(mLock);

  void AppendFacesFromFontFile(const nsCString& aFileName,
                               FontNameCache* aCache, StandardFile aStdFile)
      MOZ_REQUIRES(mLock);

  void AppendFacesFromOmnijarEntry(nsZipArchive* aReader,
                                   const nsCString& aEntryName,
                                   FontNameCache* aCache, bool aJarChanged)
      MOZ_REQUIRES(mLock);

  void InitSharedFontListForPlatform() MOZ_REQUIRES(mLock) override;
  void CollectInitData(const FontListEntry& aFLE, const nsCString& aPSName,
                       const nsCString& aFullName, StandardFile aStdFile)
      MOZ_REQUIRES(mLock);

  nsTArray<std::pair<const char**, uint32_t>> GetFilteredPlatformFontLists()
      override;

  /**
   * Callback passed to AppendFacesFromCachedFaceList to collect family/face
   * information in either the unshared or shared list we're building.
   */
  typedef void (*CollectFunc)(const FontListEntry& aFLE,
                              const nsCString& aPSName,
                              const nsCString& aFullName,
                              StandardFile aStdFile);

  /**
   * Append faces from the face-list record for a specific file.
   * aCollectFace is a callback that will store the face(s) in either the
   * unshared mFontFamilies list or the mFamilyInitData/mFaceInitData tables
   * that will be used to initialize the shared list.
   * Returns true if it is able to read at least one face entry; false if no
   * usable face entry was found.
   */
  bool AppendFacesFromCachedFaceList(CollectFunc aCollectFace,
                                     const nsCString& aFileName,
                                     const nsCString& aFaceList,
                                     StandardFile aStdFile) MOZ_REQUIRES(mLock);

  void AddFaceToList(const nsCString& aEntryName, uint32_t aIndex,
                     StandardFile aStdFile, hb_face_t* aFace,
                     nsCString& aFaceList) MOZ_REQUIRES(mLock);

  void FindFonts() MOZ_REQUIRES(mLock);

  void FindFontsInOmnijar(FontNameCache* aCache) MOZ_REQUIRES(mLock);

  void FindFontsInDir(const nsCString& aDir, FontNameCache* aFNC)
      MOZ_REQUIRES(mLock);

  FontFamily GetDefaultFontForPlatform(
      FontVisibilityProvider* aFontVisibilityProvider,
      const gfxFontStyle* aStyle, nsAtom* aLanguage = nullptr)
      MOZ_REQUIRES(mLock) override;

  nsTHashSet<nsCString> mSkipSpaceLookupCheckFamilies;

 private:
  mozilla::UniquePtr<FontNameCache> mFontNameCache;
  int64_t mJarModifiedTime;
  RefPtr<WillShutdownObserver> mObserver;

  nsTArray<mozilla::fontlist::Family::InitData> mFamilyInitData;
  nsClassHashtable<nsCStringHashKey,
                   nsTArray<mozilla::fontlist::Face::InitData>>
      mFaceInitData;
};

#endif /* GFX_FT2FONTLIST_H */
