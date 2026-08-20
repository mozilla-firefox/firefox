/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include <android/log.h>
#include <dirent.h>

#include "gfxAndroidPlatform.h"
#include "mozilla/Base64.h"
#include "mozilla/EndianUtils.h"
#include "mozilla/MemoryReporting.h"
#include "mozilla/Omnijar.h"
#include "mozilla/UniquePtr.h"
#include "mozilla/UniquePtrExtensions.h"
#include "mozilla/dom/ContentChild.h"
#include "nsReadableUtils.h"
#include "nsXULAppAPI.h"
#define ALOG(args...) __android_log_print(ANDROID_LOG_INFO, "Gecko", ##args)

#include "ft2build.h"
#include FT_FREETYPE_H
#include FT_TRUETYPE_TAGS_H
#include FT_TRUETYPE_TABLES_H
#include FT_MULTIPLE_MASTERS_H
#include "SharedFontList-impl.h"
#include "cairo-ft.h"
#include "gfxFT2FontList.h"
#include "gfxFT2Fonts.h"
#include "gfxFT2Utils.h"
#include "gfxFontUtils.h"
#include "gfxUserFontSet.h"
#define StandardFonts
#include "StandardFonts-android.inc"
#undef StandardFonts
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>

#include "gfxFontConstants.h"
#include "harfbuzz/hb-ot.h"  // for name ID constants
#include "mozilla/Components.h"
#include "mozilla/EndianUtils.h"
#include "mozilla/Preferences.h"
#include "mozilla/glean/GfxMetrics.h"
#include "mozilla/scache/StartupCache.h"
#include "nsAppDirectoryServiceDefs.h"
#include "nsCRT.h"
#include "nsDirectoryServiceDefs.h"
#include "nsDirectoryServiceUtils.h"
#include "nsIGfxInfo.h"
#include "nsIObserverService.h"
#include "nsMemory.h"
#include "nsPresContext.h"
#include "nsServiceManagerUtils.h"
#include "nsTArray.h"
#include "nsUnicharUtils.h"

#ifdef MOZ_WIDGET_ANDROID
#  include "AndroidBuild.h"
#  include "AndroidSystemFontIterator.h"
#  include "mozilla/jni/Utils.h"
#endif

using namespace mozilla;
using namespace mozilla::gfx;

static LazyLogModule sFontInfoLog("fontInfoLog");

#undef LOG
#define LOG(args) MOZ_LOG(sFontInfoLog, mozilla::LogLevel::Debug, args)
#define LOG_ENABLED() MOZ_LOG_TEST(sFontInfoLog, mozilla::LogLevel::Debug)

static __inline void BuildKeyNameFromFontName(nsACString& aName) {
  ToLowerCase(aName);
}

// Helper to access the FT_Face for a given FT2FontEntry,
// creating a temporary face if the entry does not have one yet.
// This allows us to read font names, tables, etc if necessary
// without permanently instantiating a freetype face and consuming
// memory long-term.
// This may fail (resulting in a null FT_Face), e.g. if it fails to
// allocate memory to uncompress a font from omnijar.
already_AddRefed<SharedFTFace> FT2FontEntry::GetFTFace(bool aCommit) {
  if (mFTFace) {
    // Create a new reference, and return it.
    RefPtr<SharedFTFace> face(mFTFace);
    return face.forget();
  }

  NS_ASSERTION(!mFilename.IsEmpty(),
               "can't use GetFTFace for fonts without a filename");

  // A relative path (no initial "/") means this is a resource in
  // omnijar, not an installed font on the device.
  // The NS_ASSERTIONs here should never fail, as the resource must have
  // been read successfully during font-list initialization or we'd never
  // have created the font entry. The only legitimate runtime failure
  // here would be memory allocation, in which case mFace remains null.
  RefPtr<SharedFTFace> face;
  if (mFilename[0] != '/') {
    RefPtr<nsZipArchive> reader = Omnijar::GetReader(Omnijar::Type::GRE);
    nsZipItem* item = reader->GetItem(mFilename);
    NS_ASSERTION(item, "failed to find zip entry");

    uint32_t bufSize = item->RealSize();
    if (uint8_t* fontDataBuf = static_cast<uint8_t*>(malloc(bufSize))) {
      nsZipCursor cursor(item, reader, fontDataBuf, bufSize);
      cursor.Copy(&bufSize);
      NS_ASSERTION(bufSize == item->RealSize(), "error reading bundled font");
      // fontData takes ownership of fontDataBuf
      auto fontData = MakeRefPtr<FontData>(std::move(fontDataBuf), bufSize);
      auto ufd = MakeRefPtr<FTUserFontData>(fontData);
      face = ufd->CloneFace(mFTFontIndex);
      if (!face) {
        NS_WARNING("failed to create freetype face");
        return nullptr;
      }
    }
  } else {
    RefPtr<FTUserFontData> fd = new FTUserFontData(mFilename.get());
    face = fd->CloneFace(mFTFontIndex);
    if (!face) {
      NS_WARNING("failed to create freetype face");
      return nullptr;
    }
    if (FT_Err_Ok != FT_Select_Charmap(face->GetFace(), FT_ENCODING_UNICODE) &&
        FT_Err_Ok !=
            FT_Select_Charmap(face->GetFace(), FT_ENCODING_MS_SYMBOL)) {
      NS_WARNING("failed to select Unicode or symbol charmap");
    }
  }

  if (aCommit) {
    if (mFTFace.compareExchange(nullptr, face.get())) {
      // The reference we created is now owned by mFTFace.
      face.forget().leak();
    } else {
      // We lost a race! Just discard our new face and use the existing one.
    }
  }

  // Create a new reference, and return it.
  face = mFTFace;
  return face.forget();
}

FTUserFontData* FT2FontEntry::GetUserFontData() {
  if (SharedFTFace* face = mFTFace) {
    return static_cast<FTUserFontData*>(face->GetData());
  }
  return nullptr;
}

/*
 * FT2FontEntry
 * gfxFontEntry subclass corresponding to a specific face that can be
 * rendered by freetype. This is associated with a face index in a
 * file (normally a .ttf/.otf file holding a single face, but in principle
 * there could be .ttc files with multiple faces).
 * The FT2FontEntry can create the necessary FT_Face on demand, and can
 * then create a Cairo font_face and scaled_font for drawing.
 */

FT2FontEntry::~FT2FontEntry() {
  auto* cache = mFontTableCache.exchange(nullptr);
  delete cache;
  auto* face = mHBFace.exchange(nullptr);
  hb_face_destroy(face);
  if (mMMVar) {
    SharedFTFace* face = mFTFace;
    FT_Done_MM_Var(face->GetFace()->glyph->library, mMMVar);
  }
  if (mFTFace) {
    auto face = mFTFace.exchange(nullptr);
    NS_IF_RELEASE(face);
  }
}

gfxFontEntry* FT2FontEntry::Clone() const {
  MOZ_ASSERT(!IsUserFont(), "we can only clone installed fonts!");
  FT2FontEntry* fe = new FT2FontEntry(Name());
  fe->mFilename = mFilename;
  fe->mFTFontIndex = mFTFontIndex;
  fe->mWeightRange = mWeightRange;
  fe->mWidthRange = mWidthRange;
  fe->mStyleRange = mStyleRange;
  return fe;
}

gfxFont* FT2FontEntry::CreateFontInstance(const gfxFontStyle* aStyle) {
  RefPtr<SharedFTFace> face = GetFTFace(true);
  if (!face) {
    return nullptr;
  }

  if (face->GetFace()->face_flags & FT_FACE_FLAG_MULTIPLE_MASTERS) {
    // Resolve variations from entry (descriptor) and style (property)
    AutoTArray<gfxFontVariation, 8> settings;
    GetVariationsForStyle(settings, aStyle ? *aStyle : gfxFontStyle());

    // If variations are present, we will not use our cached mFTFace
    // but always create a new one as it will have custom variation
    // coordinates applied.
    if (!settings.IsEmpty()) {
      // Create a separate FT_Face because we need to apply custom
      // variation settings to it.
      RefPtr<SharedFTFace> varFace;
      if (!mFilename.IsEmpty() && mFilename[0] == '/') {
        varFace =
            Factory::NewSharedFTFace(nullptr, mFilename.get(), mFTFontIndex);
      } else {
        varFace = face->GetData()->CloneFace(mFTFontIndex);
      }
      if (varFace) {
        gfxFT2FontBase::SetupVarCoords(GetMMVar(), settings,
                                       varFace->GetFace());
        face = std::move(varFace);
      }
    }
  }

  int loadFlags = gfxPlatform::GetPlatform()->FontHintingEnabled()
                      ? FT_LOAD_DEFAULT
                      : (FT_LOAD_NO_AUTOHINT | FT_LOAD_NO_HINTING);
  if (face->GetFace()->face_flags & FT_FACE_FLAG_TRICKY) {
    loadFlags &= ~FT_LOAD_NO_AUTOHINT;
  }

  RefPtr<UnscaledFontFreeType> unscaledFont;
  {
    AutoReadLock lock(mLock);
    unscaledFont = RefPtr<UnscaledFontFreeType>(mUnscaledFont);
  }
  if (!unscaledFont) {
    AutoWriteLock lock(mLock);
    RefPtr<SharedFTFace> origFace(mFTFace);
    unscaledFont = !mFilename.IsEmpty() && mFilename[0] == '/'
                       ? new UnscaledFontFreeType(mFilename.get(), mFTFontIndex,
                                                  std::move(origFace))
                       : new UnscaledFontFreeType(std::move(origFace));
    mUnscaledFont = unscaledFont;
  }

  gfxFont* font =
      new gfxFT2Font(unscaledFont, std::move(face), this, aStyle, loadFlags);
  return font;
}

/* static */
already_AddRefed<FT2FontEntry> FT2FontEntry::CreateFontEntry(
    const nsACString& aFontName, WeightRange aWeight, WidthRange aWidth,
    SlantStyleRange aStyle, FontData* aFontData) {
  // Ownership of aFontData is passed in here; the fontEntry must
  // retain it as long as the FT_Face needs it, and ensure it is
  // eventually deleted.
  RefPtr<FTUserFontData> ufd = MakeRefPtr<FTUserFontData>(aFontData);
  RefPtr<SharedFTFace> face = ufd->CloneFace();
  if (!face) {
    return nullptr;
  }
  // Create our FT2FontEntry, which inherits the name of the userfont entry
  // as it's not guaranteed that the face has valid names (bug 737315)
  RefPtr<FT2FontEntry> fe =
      FT2FontEntry::CreateFontEntry(aFontName, nullptr, 0, nullptr);
  if (fe) {
    fe->mFTFace = face.forget().take();  // mFTFace takes ownership.
    fe->mStyleRange = aStyle;
    fe->mWeightRange = aWeight;
    fe->mWidthRange = aWidth;
    fe->mIsDataUserFont = true;
  }
  return fe.forget();
}

/* static */
FT2FontEntry* FT2FontEntry::CreateFontEntry(const FontListEntry& aFLE) {
  FT2FontEntry* fe = new FT2FontEntry(aFLE.faceName());
  fe->mFilename = aFLE.filepath();
  fe->mFTFontIndex = aFLE.index();
  fe->mWeightRange = WeightRange::FromScalar(aFLE.weightRange());
  fe->mWidthRange = WidthRange::FromScalar(aFLE.widthRange());
  fe->mStyleRange = SlantStyleRange::FromScalar(aFLE.styleRange());
  return fe;
}

// Extract font entry properties from an hb_face_t
static void SetPropertiesFromFace(gfxFontEntry* aFontEntry,
                                  const hb_face_t* aFace) {
  // OS2 width class to CSS 'width' mapping from
  // https://docs.microsoft.com/en-gb/typography/opentype/spec/os2#uswidthclass
  const float kOS2WidthToWidth[] = {
      100,    // (invalid, treat as normal)
      50,     // Ultra-condensed
      62.5,   // Extra-condensed
      75,     // Condensed
      87.5,   // Semi-condensed
      100,    // Normal
      112.5,  // Semi-expanded
      125,    // Expanded
      150,    // Extra-expanded
      200     // Ultra-expanded
  };

  // Get the macStyle field from the 'head' table
  gfxFontUtils::AutoHBBlob headBlob(
      hb_face_reference_table(aFace, HB_TAG('h', 'e', 'a', 'd')));
  unsigned int len;
  const char* data = hb_blob_get_data(headBlob, &len);
  uint16_t style = 0;
  if (len >= sizeof(HeadTable)) {
    const HeadTable* head = reinterpret_cast<const HeadTable*>(data);
    style = head->macStyle;
  }

  // Get the OS/2 table for weight & width fields
  gfxFontUtils::AutoHBBlob os2blob(
      hb_face_reference_table(aFace, HB_TAG('O', 'S', '/', '2')));
  data = hb_blob_get_data(os2blob, &len);
  uint16_t os2weight = 400;
  float width = 100.0;
  if (len >= offsetof(OS2Table, fsType)) {
    const OS2Table* os2 = reinterpret_cast<const OS2Table*>(data);
    os2weight = os2->usWeightClass;
    uint16_t os2width = os2->usWidthClass;
    if (os2width < std::size(kOS2WidthToWidth)) {
      width = kOS2WidthToWidth[os2width];
    }
  }

  aFontEntry->mStyleRange = SlantStyleRange(
      (style & 2) ? FontSlantStyle::ITALIC : FontSlantStyle::NORMAL);
  aFontEntry->mWeightRange = WeightRange(FontWeight::FromInt(int(os2weight)));
  aFontEntry->mWidthRange = WidthRange(FontWidth::FromFloat(width));

  // For variable fonts, update the style/weight/width attributes if the
  // corresponding variation axes are present.
  aFontEntry->SetupVariationRanges();
}

// Used to create the font entry for installed faces on the device,
// when iterating over the fonts directories.
// We use the hb_face_t to retrieve the details needed for the font entry,
// but do *not* save a reference to it, nor create a cairo face.
/* static */
FT2FontEntry* FT2FontEntry::CreateFontEntry(const nsACString& aName,
                                            const char* aFilename,
                                            uint8_t aIndex,
                                            const hb_face_t* aFace) {
  FT2FontEntry* fe = new FT2FontEntry(aName);
  fe->mFilename = aFilename;
  fe->mFTFontIndex = aIndex;

  if (aFace) {
    SetPropertiesFromFace(fe, aFace);
  } else {
    // If nullptr is passed for aFace, the caller is intending to override
    // these attributes anyway. We just set defaults here to be safe.
    fe->mStyleRange = SlantStyleRange(FontSlantStyle::NORMAL);
    fe->mWeightRange = WeightRange(FontWeight::NORMAL);
    fe->mWidthRange = WidthRange(FontWidth::NORMAL);
  }

  return fe;
}

FT2FontEntry* gfxFT2Font::GetFontEntry() {
  return static_cast<FT2FontEntry*>(mFontEntry.get());
}

// Copied/modified from similar code in gfxMacPlatformFontList.mm:
// Complex scripts will not render correctly unless Graphite or OT
// layout tables are present.
// For OpenType, we also check that the GSUB table supports the relevant
// script tag, to avoid using things like Arial Unicode MS for Lao (it has
// the characters, but lacks OpenType support).

// TODO: consider whether we should move this to gfxFontEntry and do similar
// cmap-masking on all platforms to avoid using fonts that won't shape
// properly.

nsresult FT2FontEntry::ReadCMAP(FontInfoData* aFontInfoData) {
  if (HasCharacterMap()) {
    return NS_OK;
  }

  RefPtr<gfxCharacterMap> charmap = new gfxCharacterMap(256);

  nsresult rv = NS_ERROR_NOT_AVAILABLE;
  uint32_t uvsOffset = 0;
  gfxFontUtils::AutoHBBlob cmapBlob(GetFontTable(TTAG_cmap));
  if (cmapBlob) {
    unsigned int length;
    const char* data = hb_blob_get_data(cmapBlob, &length);
    rv = gfxFontUtils::ReadCMAP((const uint8_t*)data, length, *charmap,
                                uvsOffset);
  }
  mUVSOffset.exchange(uvsOffset);

  if (NS_SUCCEEDED(rv) && !mIsDataUserFont && !HasGraphiteTables()) {
    // For downloadable fonts, trust the author and don't
    // try to munge the cmap based on script shaping support.

    // We also assume a Graphite font knows what it's doing,
    // and provides whatever shaping is needed for the
    // characters it supports, so only check/clear the
    // complex-script ranges for non-Graphite fonts

    // for layout support, check for the presence of opentype layout tables
    bool hasGSUB = HasFontTable(TRUETYPE_TAG('G', 'S', 'U', 'B'));

    for (const ScriptRange* sr = gfxPlatformFontList::sComplexScriptRanges;
         sr->rangeStart; sr++) {
      // check to see if the cmap includes complex script codepoints
      if (charmap->TestRange(sr->rangeStart, sr->rangeEnd)) {
        // We check for GSUB here, as GPOS alone would not be ok.
        if (hasGSUB && SupportsScriptInGSUB(sr->tags, sr->numTags)) {
          continue;
        }
        charmap->ClearRange(sr->rangeStart, sr->rangeEnd);
      }
    }

    // Bug 1980258: the Cutive Mono font, widely present on Android, has
    // spurious blank glyphs for several codepoints. Mask them out, to avoid
    // the risk of font fallback using it (resulting in blank characters).
    if (FamilyName().EqualsLiteral("Cutive Mono")) {
      charmap->ClearRange(0x0080, 0x009f);  // C1 controls: not all present,
                                            // but let's just mask the block.
      charmap->clear(0x2074);               // superscript four
      charmap->clear(0xfb00);               // ff ligature
      charmap->ClearRange(0xfb03, 0xfb04);  // ffi, ffl
    }
  }

#if defined(MOZ_WIDGET_ANDROID) && !defined(NIGHTLY_BUILD)
  // Hack for the SamsungDevanagari font, bug 1012365:
  // pretend the font supports U+0972.
  if (!charmap->test(0x0972) && charmap->test(0x0905) &&
      charmap->test(0x0945)) {
    charmap->set(0x0972);
  }
#endif

  bool setCharMap = true;
  if (NS_SUCCEEDED(rv)) {
    gfxPlatformFontList* pfl = gfxPlatformFontList::PlatformFontList();
    fontlist::FontList* sharedFontList = pfl->SharedFontList();
    if (!IsUserFont() && mShmemFace) {
      mShmemFace->SetCharacterMap(sharedFontList, charmap, mShmemFamily);
      if (TrySetShmemCharacterMap()) {
        setCharMap = false;
      }
    } else {
      charmap = pfl->FindCharMap(charmap);
    }
  } else {
    // if error occurred, initialize to null cmap
    charmap = new gfxCharacterMap(0);
  }
  if (setCharMap) {
    AutoWriteLock lock(mLock);
    if (mCharacterMap.compareExchange(nullptr, charmap.get())) {
      // We forget rather than addref because we don't use the charmap below.
      charmap.forget().leak();
    }
  }

  return rv;
}

hb_face_t* FT2FontEntry::CreateHBFace() {
  if (mHBFace) {
    return hb_face_reference(mHBFace);
  }

  hb_face_t* face = nullptr;
  if (mFilename[0] == '/') {
    // An absolute path means a normal file in the filesystem, so we can use
    // hb_face_create_from_file_or_fail, and keep the face around.
    face = hb_face_create_from_file_or_fail(mFilename.get(), mFTFontIndex);
    if (face) {
      if (!mHBFace.compareExchange(nullptr, face)) {
        hb_face_destroy(face);
        face = mHBFace;
      }
    }
    return hb_face_reference(face);
  }

  // A relative path means an omnijar resource, which we may need to
  // decompress to a temporary buffer.
  RefPtr<nsZipArchive> reader = Omnijar::GetReader(Omnijar::Type::GRE);
  nsZipItem* item = reader->GetItem(mFilename);
  MOZ_ASSERT(item, "failed to find zip entry");
  if (item) {
    // TODO(jfkthame):
    // Check whether the item is compressed; if not, we could just get a
    // pointer without needing to allocate a buffer and copy the data.
    // (Currently this configuration isn't used for Gecko on Android.)
    uint32_t length = item->RealSize();
    uint8_t* buffer = static_cast<uint8_t*>(malloc(length));
    if (buffer) {
      nsZipCursor cursor(item, reader, buffer, length);
      cursor.Copy(&length);
      MOZ_ASSERT(length == item->RealSize(), "error reading font");
      if (length == item->RealSize()) {
        gfxFontUtils::AutoHBBlob blob(
            hb_blob_create((const char*)buffer, length, HB_MEMORY_MODE_READONLY,
                           buffer, free));
        // We don't retain this face; the caller will own the only reference.
        return hb_face_create(blob, mFTFontIndex);
      }
    }
  }

  return nullptr;
}

bool FT2FontEntry::HasFontTableInternal(uint32_t aTableTag) {
  // If we already have a FreeType face, we can just use that.
  if (mFTFace) {
    RefPtr<SharedFTFace> face = GetFTFace();
    return gfxFT2FontEntryBase::FaceHasTable(face, aTableTag);
  }

  {
    // If we have a cached set of tables, query that.
    AutoReadLock lock(mLock);
    if (mAvailableTables.Count() > 0) {
      return mAvailableTables.Contains(aTableTag);
    }
  }

  // If we haven't created a FreeType face already, try to avoid that by
  // reading the available table tags via harfbuzz and caching in a hashset.
  AutoWriteLock lock(mLock);
  if (!mFTFace && !mFilename.IsEmpty()) {
    hb_face_t* face = CreateHBFace();
    if (face) {
      // Read table tags in batches; 32 should be enough for most fonts in a
      // single operation.
      const unsigned TAG_BUF_LENGTH = 32;
      hb_tag_t tags[TAG_BUF_LENGTH];
      unsigned int startOffset = 0;
      unsigned int totalTables = 0;
      do {
        unsigned int count = TAG_BUF_LENGTH;
        // Updates count to the number of table tags actually retrieved
        totalTables = hb_face_get_table_tags(face, startOffset, &count, tags);
        startOffset += count;
        while (count-- > 0) {
          mAvailableTables.Insert(tags[count]);
        }
      } while (startOffset < totalTables);
      hb_face_destroy(face);
    } else {
      // Failed to create the HarfBuzz face! The font is probably broken.
      // Put a dummy entry in mAvailableTables so that we don't bother
      // re-trying here.
      mAvailableTables.Insert(uint32_t(-1));
    }
    return mAvailableTables.Contains(aTableTag);
  }

  // Last resort: we'll have to create a (temporary) FreeType face to query
  // for table presence.
  RefPtr<SharedFTFace> face = GetFTFace();
  return gfxFT2FontEntryBase::FaceHasTable(face, aTableTag);
}

nsresult FT2FontEntry::CopyFontTable(uint32_t aTableTag,
                                     nsTArray<uint8_t>& aBuffer) {
  RefPtr<SharedFTFace> face = GetFTFace();
  return gfxFT2FontEntryBase::CopyFaceTable(face, aTableTag, aBuffer);
}

hb_blob_t* FT2FontEntry::GetFontTableInternal(uint32_t aTableTag) {
  if (FTUserFontData* userFontData = GetUserFontData()) {
    // If there's a cairo font face, we may be able to return a blob
    // that just wraps a range of the attached user font data
    if (const auto* data = userFontData->GetData()) {
      return gfxFontUtils::GetTableFromFontData(data, aTableTag);
    }
  }

  // Try to read table directly via harfbuzz API, unless CreateHBFace will be
  // expensive.
  if (mHBFace || (!mFilename.IsEmpty() && mFilename[0] == '/')) {
    if (hb_face_t* face = CreateHBFace()) {
      hb_blob_t* result = hb_face_reference_table(face, aTableTag);
      hb_face_destroy(face);
      return result != hb_blob_get_empty() ? result : nullptr;
    }
  }

  // Otherwise, use the default method (which in turn will call our
  // implementation of CopyFontTable).
  return gfxFontEntry::GetFontTableInternal(aTableTag);
}

gfxFontEntry::FontTableCache* FT2FontEntry::GetFontTableCache(bool aCreate) {
  // Create the cache if it does not yet exist.
  if (!mFontTableCache && aCreate) {
    auto* cache = new FontTableCache();
    if (!mFontTableCache.compareExchange(nullptr, cache)) {
      delete cache;
    }
  }
  return mFontTableCache;
}

bool FT2FontEntry::HasVariations() {
  switch (mHasVariations) {
    case HasVariationsState::No:
      return false;
    case HasVariationsState::Yes:
      return true;
    case HasVariationsState::Uninitialized:
      break;
  }

  SharedFTFace* face = mFTFace;
  bool hasVariations =
      face ? face->GetFace()->face_flags & FT_FACE_FLAG_MULTIPLE_MASTERS
           : gfxPlatform::HasVariationFontSupport() &&
                 HasFontTable(TRUETYPE_TAG('f', 'v', 'a', 'r'));
  mHasVariations =
      hasVariations ? HasVariationsState::Yes : HasVariationsState::No;
  return hasVariations;
}

void FT2FontEntry::GetVariationAxes(nsTArray<gfxFontVariationAxis>& aAxes) {
  if (!HasVariations()) {
    return;
  }
  if (FT_MM_Var* mmVar = GetMMVar()) {
    gfxFT2Utils::GetVariationAxes(mmVar, aAxes);
  }
}

void FT2FontEntry::GetVariationInstances(
    nsTArray<gfxFontVariationInstance>& aInstances) {
  if (!HasVariations()) {
    return;
  }
  if (FT_MM_Var* mmVar = GetMMVar()) {
    gfxFT2Utils::GetVariationInstances(this, mmVar, aInstances);
  }
}

FT_MM_Var* FT2FontEntry::GetMMVar() {
  if (mMMVarInitialized) {
    return mMMVar;
  }
  AutoWriteLock lock(mLock);
  RefPtr<SharedFTFace> face = GetFTFace(true);
  if (!face) {
    return nullptr;
  }
  if (FT_Err_Ok != FT_Get_MM_Var(face->GetFace(), &mMMVar)) {
    mMMVar = nullptr;
  }
  mMMVarInitialized = true;
  return mMMVar;
}

void FT2FontEntry::AddSizeOfExcludingThis(MallocSizeOf aMallocSizeOf,
                                          FontListSizes* aSizes) const {
  gfxFontEntry::AddSizeOfExcludingThis(aMallocSizeOf, aSizes);
  aSizes->mFontListSize +=
      mFilename.SizeOfExcludingThisIfUnshared(aMallocSizeOf);
}

void FT2FontEntry::AddSizeOfIncludingThis(MallocSizeOf aMallocSizeOf,
                                          FontListSizes* aSizes) const {
  aSizes->mFontListSize += aMallocSizeOf(this);
  AddSizeOfExcludingThis(aMallocSizeOf, aSizes);
}

/*
 * FT2FontFamily
 * A standard gfxFontFamily; just adds a method used to support sending
 * the font list from chrome to content via IPC.
 */

void FT2FontFamily::AddFacesToFontList(nsTArray<FontListEntry>* aFontList) {
  AutoReadLock lock(mLock);
  for (int i = 0, n = mAvailableFonts.Length(); i < n; ++i) {
    const FT2FontEntry* fe =
        static_cast<const FT2FontEntry*>(mAvailableFonts[i].get());
    if (!fe) {
      continue;
    }

    aFontList->AppendElement(FontListEntry(
        Name(), fe->Name(), fe->mFilename, fe->Weight().AsScalar(),
        fe->Width().AsScalar(), fe->SlantStyle().AsScalar(), fe->mFTFontIndex,
        Visibility()));
  }
}

/*
 * Startup cache support for the font list:
 * We store the list of families and faces, with their style attributes and the
 * corresponding font files, in the startup cache.
 * This allows us to recreate the gfxFT2FontList collection of families and
 * faces without instantiating Freetype faces for each font file (in order to
 * find their attributes), leading to significantly quicker startup.
 */

#define CACHE_KEY "font.cached-list"

void gfxFT2FontList::CollectInitData(const FontListEntry& aFLE,
                                     const nsCString& aPSName,
                                     const nsCString& aFullName,
                                     StandardFile aStdFile) {
  nsAutoCString key(aFLE.familyName());
  BuildKeyNameFromFontName(key);
  mFaceInitData
      .LookupOrInsertWith(
          key,
          [&] {
            mFamilyInitData.AppendElement(fontlist::Family::InitData{
                key, aFLE.familyName(), fontlist::Family::kNoIndex,
                aFLE.visibility()});
            return MakeUnique<nsTArray<fontlist::Face::InitData>>();
          })
      ->AppendElement(fontlist::Face::InitData{
          aFLE.filepath(), aFLE.index(), false,
          WeightRange::FromScalar(aFLE.weightRange()),
          WidthRange::FromScalar(aFLE.widthRange()),
          SlantStyleRange::FromScalar(aFLE.styleRange())});
  nsAutoCString psname(aPSName), fullname(aFullName);
  if (!psname.IsEmpty()) {
    ToLowerCase(psname);
    MaybeAddToLocalNameTable(
        psname, fontlist::LocalFaceRec::InitData(key, aFLE.filepath()));
  }
  if (!fullname.IsEmpty()) {
    ToLowerCase(fullname);
    if (fullname != psname) {
      MaybeAddToLocalNameTable(
          fullname, fontlist::LocalFaceRec::InitData(key, aFLE.filepath()));
    }
  }
}

class FontNameCache {
 public:
  // Delimiters used in the cached font-list records we store in startupCache
  static const char kFileSep = 0x1c;
  static const char kGroupSep = 0x1d;
  static const char kRecordSep = 0x1e;
  static const char kFieldSep = 0x1f;

  // Separator for font property ranges; we only look for this within a
  // field that holds a serialized FontPropertyValue or Range, so there's no
  // risk of conflicting with printable characters in font names.
  // Note that this must be a character that will terminate strtof() parsing
  // of a number.
  static const char kRangeSep = ':';

  // Creates the object but does NOT load the cached data from the startup
  // cache; call Init() after creation to do that.
  FontNameCache() : mWriteNeeded(false) {
    MOZ_ASSERT(XRE_IsParentProcess(),
               "FontNameCache should only be used in chrome process");
    mCache = mozilla::scache::StartupCache::GetSingleton();
  }

  ~FontNameCache() { WriteCache(); }

  size_t EntryCount() const { return mMap.Count(); }

  void DropStaleEntries() {
    for (auto iter = mMap.Iter(); !iter.Done(); iter.Next()) {
      if (!iter.Data().mFileExists) {
        iter.Remove();
      }
    }
  }

  void WriteCache() {
    if (!mWriteNeeded || !mCache) {
      return;
    }

    LOG(("Writing FontNameCache:"));
    nsAutoCString buf;
    for (auto& entry : mMap) {
      MOZ_ASSERT(entry.GetData().mFileExists);
      buf.Append(entry.GetKey());
      buf.Append(kGroupSep);
      buf.Append(entry.GetData().mFaces);
      buf.Append(kGroupSep);
      buf.AppendInt(entry.GetData().mTimestamp);
      buf.Append(kGroupSep);
      buf.AppendInt(entry.GetData().mFilesize);
      buf.Append(kFileSep);
    }

    LOG(("putting FontNameCache to " CACHE_KEY ", length %zu",
         buf.Length() + 1));
    mCache->PutBuffer(CACHE_KEY, UniqueFreePtr<char[]>(ToNewCString(buf)),
                      buf.Length() + 1);
    mWriteNeeded = false;
  }

  // This may be called more than once (if we re-load the font list).
  void Init() {
    if (!mCache) {
      return;
    }

    uint32_t size;
    const char* cur;
    if (NS_FAILED(mCache->GetBuffer(CACHE_KEY, &cur, &size))) {
      LOG(("no cache of " CACHE_KEY));
      return;
    }

    LOG(("got: %u bytes from the cache " CACHE_KEY, size));

    mMap.Clear();
    mWriteNeeded = false;

    while (const char* fileEnd = strchr(cur, kFileSep)) {
      // The cached record for one file is at [cur, fileEnd].

      // Find end of field that starts at aStart, terminated by kGroupSep or
      // end of record.
      auto endOfField = [=](const char* aStart) -> const char* {
        MOZ_ASSERT(aStart <= fileEnd);
        const char* end = static_cast<const char*>(
            memchr(aStart, kGroupSep, fileEnd - aStart));
        if (end) {
          return end;
        }
        return fileEnd;
      };

      // Advance aStart and aEnd to indicate the range of the next field and
      // return true, or just return false if already at end of record.
      auto nextField = [=](const char*& aStart, const char*& aEnd) -> bool {
        if (aEnd < fileEnd) {
          aStart = aEnd + 1;
          aEnd = endOfField(aStart);
          return true;
        }
        return false;
      };

      const char* end = endOfField(cur);
      nsCString filename(cur, end - cur);
      if (!nextField(cur, end)) {
        break;
      }
      nsCString faceList(cur, end - cur);
      if (!nextField(cur, end)) {
        break;
      }
      uint32_t timestamp = strtoul(cur, nullptr, 10);
      if (!nextField(cur, end)) {
        break;
      }
      uint32_t filesize = strtoul(cur, nullptr, 10);

      auto& mapEntry = mMap.LookupOrInsert(filename);
      mapEntry.mTimestamp = timestamp;
      mapEntry.mFilesize = filesize;
      mapEntry.mFaces.Assign(faceList);
      // entries from the startupcache are marked "non-existing"
      // until we have confirmed that the file still exists
      mapEntry.mFileExists = false;

      cur = fileEnd + 1;
    }
  }

  void GetInfoForFile(const nsCString& aFileName, nsCString& aFaceList,
                      uint32_t* aTimestamp, uint32_t* aFilesize) {
    auto entry = mMap.Lookup(aFileName);
    if (entry) {
      *aTimestamp = entry->mTimestamp;
      *aFilesize = entry->mFilesize;
      aFaceList.Assign(entry->mFaces);
      // this entry does correspond to an existing file
      // (although it might not be up-to-date, in which case
      // it will get overwritten via CacheFileInfo)
      entry->mFileExists = true;
    }
  }

  void CacheFileInfo(const nsCString& aFileName, const nsCString& aFaceList,
                     uint32_t aTimestamp, uint32_t aFilesize) {
    auto& entry = mMap.LookupOrInsert(aFileName);
    entry.mTimestamp = aTimestamp;
    entry.mFilesize = aFilesize;
    entry.mFaces.Assign(aFaceList);
    entry.mFileExists = true;
    mWriteNeeded = true;
  }

 private:
  struct FNCEntry {
    uint32_t mTimestamp;
    uint32_t mFilesize;
    nsCString mFaces;
    bool mFileExists;
  };

  mozilla::scache::StartupCache* mCache;
  nsTHashMap<nsCString, FNCEntry> mMap;
  bool mWriteNeeded;
};

/***************************************************************
 *
 * gfxFT2FontList
 *
 */

// For Mobile, we use gfxFT2Fonts, and we build the font list by directly
// scanning the system's Fonts directory for OpenType and TrueType files.

#define JAR_LAST_MODIFED_TIME "jar-last-modified-time"

class WillShutdownObserver : public nsIObserver {
 public:
  NS_DECL_ISUPPORTS
  NS_DECL_NSIOBSERVER

  explicit WillShutdownObserver(gfxFT2FontList* aFontList)
      : mFontList(aFontList) {}

  void Remove() {
    nsCOMPtr<nsIObserverService> obs = services::GetObserverService();
    if (obs) {
      obs->RemoveObserver(this, NS_XPCOM_WILL_SHUTDOWN_OBSERVER_ID);
    }
    mFontList = nullptr;
  }

 protected:
  virtual ~WillShutdownObserver() = default;

  gfxFT2FontList* mFontList;
};

NS_IMPL_ISUPPORTS(WillShutdownObserver, nsIObserver)

NS_IMETHODIMP
WillShutdownObserver::Observe(nsISupports* aSubject, const char* aTopic,
                              const char16_t* aData) {
  if (!nsCRT::strcmp(aTopic, NS_XPCOM_WILL_SHUTDOWN_OBSERVER_ID)) {
    mFontList->WillShutdown();
  } else {
    MOZ_ASSERT_UNREACHABLE("unexpected notification topic");
  }
  return NS_OK;
}

gfxFT2FontList::gfxFT2FontList() : mJarModifiedTime(0) {
  CheckFamilyList(kBaseFonts_Android);
  CheckFamilyList(kBaseFonts_Android5_8);
  CheckFamilyList(kBaseFonts_Android9_Higher);
  CheckFamilyList(kBaseFonts_Android9_11);
  CheckFamilyList(kBaseFonts_Android12_Higher);
  CheckFamilyList(kLangPack_MFR_Android12_Higher);

  nsCOMPtr<nsIObserverService> obs = services::GetObserverService();
  if (obs) {
    mObserver = new WillShutdownObserver(this);
    obs->AddObserver(mObserver, NS_XPCOM_WILL_SHUTDOWN_OBSERVER_ID, false);
  }
}

gfxFT2FontList::~gfxFT2FontList() {
  AutoLock lock(mLock);
  if (mObserver) {
    mObserver->Remove();
  }
}

bool gfxFT2FontList::AppendFacesFromCachedFaceList(CollectFunc aCollectFace,
                                                   const nsCString& aFileName,
                                                   const nsCString& aFaceList,
                                                   StandardFile aStdFile) {
  const char* start = aFaceList.get();
  int count = 0;

  while (const char* recEnd = strchr(start, FontNameCache::kRecordSep)) {
    auto endOfField = [=](const char* aStart) -> const char* {
      MOZ_ASSERT(aStart <= recEnd);
      const char* end = static_cast<const char*>(
          memchr(aStart, FontNameCache::kFieldSep, recEnd - aStart));
      if (end) {
        return end;
      }
      return recEnd;
    };

    auto nextField = [=](const char*& aStart, const char*& aEnd) -> bool {
      if (aEnd < recEnd) {
        aStart = aEnd + 1;
        aEnd = endOfField(aStart);
        return true;
      }
      return false;
    };

    const char* end = endOfField(start);
    nsAutoCString familyName(start, end - start);
    nsAutoCString key(familyName);
    ToLowerCase(key);

    if (!nextField(start, end)) {
      break;
    }
    nsAutoCString faceName(start, end - start);

    if (!nextField(start, end)) {
      break;
    }
    uint32_t index = strtoul(start, nullptr, 10);

    if (!nextField(start, end)) {
      break;
    }

    auto readIntPair = [&](int32_t& aStart, int32_t& aEnd) {
      char* limit = nullptr;
      aStart = strtol(start, &limit, 10);
      if (*limit == FontNameCache::kRangeSep && limit + 1 < end) {
        aEnd = strtof(limit + 1, nullptr);
      }
    };

    int32_t minStyle, maxStyle;
    readIntPair(minStyle, maxStyle);

    if (!nextField(start, end)) {
      break;
    }

    int32_t minWeight, maxWeight;
    readIntPair(minWeight, maxWeight);

    if (!nextField(start, end)) {
      break;
    }

    int32_t minWidth, maxWidth;
    readIntPair(minWidth, maxWidth);

    if (!nextField(start, end)) {
      break;
    }
    nsAutoCString psname(start, end - start);

    if (!nextField(start, end)) {
      break;
    }
    nsAutoCString fullname(start, end - start);

    if (!nextField(start, end)) {
      break;
    }
    FontVisibility visibility = FontVisibility(strtoul(start, nullptr, 10));

    FontListEntry fle(
        familyName, faceName, aFileName,
        WeightRange(FontWeight::FromRaw(minWeight),
                    FontWeight::FromRaw(maxWeight))
            .AsScalar(),
        WidthRange(FontWidth::FromRaw(minWidth), FontWidth::FromRaw(maxWidth))
            .AsScalar(),
        SlantStyleRange(FontSlantStyle::FromRaw(minStyle),
                        FontSlantStyle::FromRaw(maxStyle))
            .AsScalar(),
        index, visibility);

    aCollectFace(fle, psname, fullname, aStdFile);
    count++;

    start = recEnd + 1;
  }

  return count > 0;
}

void FT2FontEntry::AppendToFaceList(nsCString& aFaceList,
                                    const nsACString& aFamilyName,
                                    const nsACString& aPSName,
                                    const nsACString& aFullName,
                                    FontVisibility aVisibility) {
  aFaceList.Append(aFamilyName);
  aFaceList.Append(FontNameCache::kFieldSep);
  aFaceList.Append(Name());
  aFaceList.Append(FontNameCache::kFieldSep);
  aFaceList.AppendInt(mFTFontIndex);
  aFaceList.Append(FontNameCache::kFieldSep);
  aFaceList.AppendInt(SlantStyle().Min().Raw());
  aFaceList.Append(FontNameCache::kRangeSep);
  aFaceList.AppendInt(SlantStyle().Max().Raw());
  aFaceList.Append(FontNameCache::kFieldSep);
  aFaceList.AppendInt(Weight().Min().Raw());
  aFaceList.Append(FontNameCache::kRangeSep);
  aFaceList.AppendInt(Weight().Max().Raw());
  aFaceList.Append(FontNameCache::kFieldSep);
  aFaceList.AppendInt(Width().Min().Raw());
  aFaceList.Append(FontNameCache::kRangeSep);
  aFaceList.AppendInt(Width().Max().Raw());
  aFaceList.Append(FontNameCache::kFieldSep);
  aFaceList.Append(aPSName);
  aFaceList.Append(FontNameCache::kFieldSep);
  aFaceList.Append(aFullName);
  aFaceList.Append(FontNameCache::kFieldSep);
  aFaceList.AppendInt(int(aVisibility));
  aFaceList.Append(FontNameCache::kRecordSep);
}

void FT2FontEntry::CheckForBrokenFont(gfxFontFamily* aFamily) {
  // note if the family is in the "bad underline" blocklist
  if (aFamily->IsBadUnderlineFamily()) {
    mIsBadUnderlineFont = true;
  }
  nsAutoCString familyKey(aFamily->Name());
  BuildKeyNameFromFontName(familyKey);
  CheckForBrokenFont(familyKey);
}

void FT2FontEntry::CheckForBrokenFont(const nsACString& aFamilyKey) {
  // bug 721719 - set the IgnoreGSUB flag on entries for Roboto
  // because of unwanted on-by-default "ae" ligature.
  // (See also AppendFaceFromFontListEntry.)
  if (aFamilyKey.EqualsLiteral("roboto")) {
    mIgnoreGSUB = true;
    return;
  }

  // bug 706888 - set the IgnoreGSUB flag on the broken version of
  // Droid Sans Arabic from certain phones, as identified by the
  // font checksum in the 'head' table
  if (aFamilyKey.EqualsLiteral("droid sans arabic")) {
    RefPtr<SharedFTFace> face = GetFTFace();
    if (face) {
      const TT_Header* head = static_cast<const TT_Header*>(
          FT_Get_Sfnt_Table(face->GetFace(), ft_sfnt_head));
      if (head && head->CheckSum_Adjust == 0xe445242) {
        mIgnoreGSUB = true;
      }
    }
  }
}

void gfxFT2FontList::AppendFacesFromBlob(
    const nsCString& aFileName, StandardFile aStdFile, hb_blob_t* aBlob,
    FontNameCache* aCache, uint32_t aTimestamp, uint32_t aFilesize) {
  nsCString newFaceList;
  uint32_t numFaces = 1;
  unsigned int length;
  const char* data = hb_blob_get_data(aBlob, &length);
  // Check for a possible TrueType Collection
  if (length >= sizeof(TTCHeader)) {
    const TTCHeader* ttc = reinterpret_cast<const TTCHeader*>(data);
    if (ttc->ttcTag == TRUETYPE_TAG('t', 't', 'c', 'f')) {
      numFaces = ttc->numFonts;
    }
  }
  for (unsigned int index = 0; index < numFaces; index++) {
    hb_face_t* face = hb_face_create(aBlob, index);
    if (face != hb_face_get_empty()) {
      AddFaceToList(aFileName, index, aStdFile, face, newFaceList);
    }
    hb_face_destroy(face);
  }
  if (aCache && !newFaceList.IsEmpty()) {
    aCache->CacheFileInfo(aFileName, newFaceList, aTimestamp, aFilesize);
  }
}

void gfxFT2FontList::AppendFacesFromFontFile(const nsCString& aFileName,
                                             FontNameCache* aCache,
                                             StandardFile aStdFile) {
  nsCString cachedFaceList;
  uint32_t filesize = 0, timestamp = 0;
  if (aCache) {
    aCache->GetInfoForFile(aFileName, cachedFaceList, &timestamp, &filesize);
  }

  struct stat s;
  int statRetval = stat(aFileName.get(), &s);
  if (!cachedFaceList.IsEmpty() && 0 == statRetval &&
      uint32_t(s.st_mtime) == timestamp && s.st_size == filesize) {
    CollectFunc unshared =
        [](const FontListEntry& aFLE, const nsCString& aPSName,
           const nsCString& aFullName, StandardFile aStdFile)
            MOZ_REQUIRES(PlatformFontList()->mLock) {
              auto* pfl = PlatformFontList();
              pfl->mLock.AssertCurrentThreadIn();
              pfl->AppendFaceFromFontListEntry(aFLE, aStdFile);
            };
    CollectFunc shared = [](const FontListEntry& aFLE, const nsCString& aPSName,
                            const nsCString& aFullName, StandardFile aStdFile)
                             MOZ_REQUIRES(PlatformFontList()->mLock) {
                               PlatformFontList()->CollectInitData(
                                   aFLE, aPSName, aFullName, aStdFile);
                             };
    if (AppendFacesFromCachedFaceList(SharedFontList() ? shared : unshared,
                                      aFileName, cachedFaceList, aStdFile)) {
      LOG(("using cached font info for %s", aFileName.get()));
      return;
    }
  }

  gfxFontUtils::AutoHBBlob fileBlob(hb_blob_create_from_file(aFileName.get()));
  if (hb_blob_get_length(fileBlob) > 0) {
    LOG(("reading font info via harfbuzz for %s", aFileName.get()));
    AppendFacesFromBlob(aFileName, aStdFile, fileBlob,
                        0 == statRetval ? aCache : nullptr, s.st_mtime,
                        s.st_size);
  }
}

void gfxFT2FontList::FindFontsInOmnijar(FontNameCache* aCache) {
  bool jarChanged = false;

  mozilla::scache::StartupCache* cache =
      mozilla::scache::StartupCache::GetSingleton();
  const char* cachedModifiedTimeBuf;
  uint32_t longSize;
  if (cache &&
      NS_SUCCEEDED(cache->GetBuffer(JAR_LAST_MODIFED_TIME,
                                    &cachedModifiedTimeBuf, &longSize)) &&
      longSize == sizeof(int64_t)) {
    nsCOMPtr<nsIFile> jarFile = Omnijar::GetPath(Omnijar::Type::GRE);
    jarFile->GetLastModifiedTime(&mJarModifiedTime);
    if (mJarModifiedTime > LittleEndian::readInt64(cachedModifiedTimeBuf)) {
      jarChanged = true;
    }
  }

  static const char* sJarSearchPaths[] = {
      "res/fonts/*.ttf$",
  };
  RefPtr<nsZipArchive> reader = Omnijar::GetReader(Omnijar::Type::GRE);
  for (unsigned i = 0; i < std::size(sJarSearchPaths); i++) {
    nsZipFind* find;
    if (NS_SUCCEEDED(reader->FindInit(sJarSearchPaths[i], &find))) {
      const char* path;
      uint16_t len;
      while (NS_SUCCEEDED(find->FindNext(&path, &len))) {
        nsCString entryName(path, len);
        AppendFacesFromOmnijarEntry(reader, entryName, aCache, jarChanged);
      }
      delete find;
    }
  }
}

using Device = nsIGfxInfo::FontVisibilityDeviceDetermination;
FontVisibility gfxFT2FontList::GetVisibilityForFamily(
    const nsACString& aName) const {
  static Device fontVisibilityDevice = Device::Unassigned;
  if (fontVisibilityDevice == Device::Unassigned) {
    nsCOMPtr<nsIGfxInfo> gfxInfo = components::GfxInfo::Service();
    NS_ENSURE_SUCCESS(
        gfxInfo->GetFontVisibilityDetermination(&fontVisibilityDevice),
        FontVisibility::Unknown);
  }

  if (fontVisibilityDevice == Device::Android_Unknown_Release_Version ||
      fontVisibilityDevice == Device::Android_Unknown_Peloton ||
      fontVisibilityDevice == Device::Android_Unknown_vbox ||
      fontVisibilityDevice == Device::Android_Unknown_mitv ||
      fontVisibilityDevice == Device::Android_Chromebook ||
      fontVisibilityDevice == Device::Android_Amazon) {
    return FontVisibility::Unknown;
  }

  // Sanity Check
  if (fontVisibilityDevice != Device::Android_sub_9 &&
      fontVisibilityDevice != Device::Android_9_11 &&
      fontVisibilityDevice != Device::Android_12_plus) {
    return FontVisibility::Unknown;
  }

  if (FamilyInList(aName, kBaseFonts_Android)) {
    return FontVisibility::Base;
  }

  if (fontVisibilityDevice == Device::Android_sub_9) {
    if (FamilyInList(aName, kBaseFonts_Android5_8)) {
      return FontVisibility::Base;
    }
  } else {
    if (FamilyInList(aName, kBaseFonts_Android9_Higher)) {
      return FontVisibility::Base;
    }

    if (fontVisibilityDevice == Device::Android_9_11) {
      if (FamilyInList(aName, kBaseFonts_Android9_11)) {
        return FontVisibility::Base;
      }
    } else {
      if (FamilyInList(aName, kBaseFonts_Android12_Higher)) {
        return FontVisibility::Base;
      }

      if (FamilyInList(aName, kLangPack_MFR_Android12_Higher)) {
        return FontVisibility::LangPack;
      }
    }
  }

  return FontVisibility::User;
}

nsTArray<std::pair<const char**, uint32_t>>
gfxFT2FontList::GetFilteredPlatformFontLists() {
  static Device fontVisibilityDevice = Device::Unassigned;
  if (fontVisibilityDevice == Device::Unassigned) {
    nsCOMPtr<nsIGfxInfo> gfxInfo = components::GfxInfo::Service();
    (void)gfxInfo->GetFontVisibilityDetermination(&fontVisibilityDevice);
  }

  nsTArray<std::pair<const char**, uint32_t>> fontLists;

  if (fontVisibilityDevice == Device::Android_Unknown_Release_Version ||
      fontVisibilityDevice == Device::Android_Unknown_Peloton ||
      fontVisibilityDevice == Device::Android_Unknown_vbox ||
      fontVisibilityDevice == Device::Android_Unknown_mitv ||
      fontVisibilityDevice == Device::Android_Chromebook ||
      fontVisibilityDevice == Device::Android_Amazon) {
    return fontLists;
  }

  // Sanity Check
  if (fontVisibilityDevice != Device::Android_sub_9 &&
      fontVisibilityDevice != Device::Android_9_11 &&
      fontVisibilityDevice != Device::Android_12_plus) {
    return fontLists;
  }

  fontLists.AppendElement(
      std::make_pair(kBaseFonts_Android, std::size(kBaseFonts_Android)));

  if (fontVisibilityDevice == Device::Android_sub_9) {
    fontLists.AppendElement(std::make_pair(kBaseFonts_Android5_8,
                                           std::size(kBaseFonts_Android5_8)));
  } else {
    fontLists.AppendElement(std::make_pair(
        kBaseFonts_Android9_Higher, std::size(kBaseFonts_Android9_Higher)));

    if (fontVisibilityDevice == Device::Android_9_11) {
      fontLists.AppendElement(std::make_pair(
          kBaseFonts_Android9_11, std::size(kBaseFonts_Android9_11)));
    } else {
      fontLists.AppendElement(std::make_pair(
          kBaseFonts_Android12_Higher, std::size(kBaseFonts_Android12_Higher)));
      fontLists.AppendElement(
          std::make_pair(kLangPack_MFR_Android12_Higher,
                         std::size(kLangPack_MFR_Android12_Higher)));
    }
  }

  return fontLists;
}

static void GetName(hb_face_t* aFace, hb_ot_name_id_t aNameID,
                    nsACString& aName) {
  unsigned int n = 0;
  n = hb_ot_name_get_utf8(aFace, aNameID, HB_LANGUAGE_INVALID, &n, nullptr);
  if (n) {
    aName.SetLength(n++);  // increment n to account for NUL terminator
    n = hb_ot_name_get_utf8(aFace, aNameID, HB_LANGUAGE_INVALID, &n,
                            aName.BeginWriting());
  }
}

// Given the harfbuzz face corresponding to an entryName and face index,
// add the face to the available font list and to the faceList string
void gfxFT2FontList::AddFaceToList(const nsCString& aEntryName, uint32_t aIndex,
                                   StandardFile aStdFile, hb_face_t* aFace,
                                   nsCString& aFaceList) {
  nsAutoCString familyName;
  bool preferTypographicNames = true;
  GetName(aFace, HB_OT_NAME_ID_TYPOGRAPHIC_FAMILY, familyName);
  if (familyName.IsEmpty()) {
    preferTypographicNames = false;
    GetName(aFace, HB_OT_NAME_ID_FONT_FAMILY, familyName);
  }
  if (familyName.IsEmpty()) {
    return;
  }

  nsAutoCString fullname;
  GetName(aFace, HB_OT_NAME_ID_FULL_NAME, fullname);
  if (fullname.IsEmpty()) {
    // Construct fullname from family + style
    fullname = familyName;
    nsAutoCString styleName;
    if (preferTypographicNames) {
      GetName(aFace, HB_OT_NAME_ID_TYPOGRAPHIC_SUBFAMILY, styleName);
    }
    if (styleName.IsEmpty()) {
      GetName(aFace, HB_OT_NAME_ID_FONT_SUBFAMILY, styleName);
    }
    if (!styleName.IsEmpty() && !styleName.EqualsLiteral("Regular")) {
      fullname.Append(' ');
      fullname.Append(styleName);
    }
  }

  // Build the font entry name and create an FT2FontEntry,
  // but do -not- keep a reference to the FT_Face.
  // (When using the shared font list, this entry will not be retained,
  // it is used only to call AppendToFaceList.)
  RefPtr<FT2FontEntry> fe =
      FT2FontEntry::CreateFontEntry(fullname, aEntryName.get(), aIndex, aFace);

  if (fe) {
    fe->mStandardFace = (aStdFile == kStandard);
    nsAutoCString familyKey(familyName);
    BuildKeyNameFromFontName(familyKey);

    FontVisibility visibility = GetVisibilityForFamily(familyName);

    nsAutoCString psname;
    GetName(aFace, HB_OT_NAME_ID_POSTSCRIPT_NAME, psname);

    if (SharedFontList()) {
      FontListEntry fle(familyName, fe->Name(), fe->mFilename,
                        fe->Weight().AsScalar(), fe->Width().AsScalar(),
                        fe->SlantStyle().AsScalar(), fe->mFTFontIndex,
                        visibility);
      CollectInitData(fle, psname, fullname, aStdFile);
    } else {
      RefPtr<gfxFontFamily> family =
          mFontFamilies.LookupOrInsertWith(familyKey, [&] {
            auto family = MakeRefPtr<FT2FontFamily>(familyName, visibility);
            if (mSkipSpaceLookupCheckFamilies.Contains(familyKey)) {
              family->SetSkipSpaceFeatureCheck(true);
            }
            if (mBadUnderlineFamilyNames.ContainsSorted(familyKey)) {
              family->SetBadUnderlineFamily();
            }
            return family;
          });
      family->AddFontEntry(fe);
      fe->CheckForBrokenFont(family);
    }

    fe->AppendToFaceList(aFaceList, familyName, psname, fullname, visibility);
    if (LOG_ENABLED()) {
      nsAutoCString weightString;
      fe->Weight().ToString(weightString);
      nsAutoCString widthString;
      fe->Width().ToString(widthString);
      LOG(
          ("(fontinit) added (%s) to family (%s)"
           " with style: %s weight: %s width: %s",
           fe->Name().get(), familyName.get(),
           fe->IsItalic() ? "italic" : "normal", weightString.get(),
           widthString.get()));
    }
  }
}

void gfxFT2FontList::AppendFacesFromOmnijarEntry(nsZipArchive* aArchive,
                                                 const nsCString& aEntryName,
                                                 FontNameCache* aCache,
                                                 bool aJarChanged) {
  nsCString faceList;
  if (aCache && !aJarChanged) {
    uint32_t filesize, timestamp;
    aCache->GetInfoForFile(aEntryName, faceList, &timestamp, &filesize);
    if (faceList.Length() > 0) {
      CollectFunc unshared =
          [](const FontListEntry& aFLE, const nsCString& aPSName,
             const nsCString& aFullName, StandardFile aStdFile) {
            auto* pfl = PlatformFontList();
            pfl->mLock.AssertCurrentThreadIn();
            pfl->AppendFaceFromFontListEntry(aFLE, aStdFile);
          };
      CollectFunc shared = [](const FontListEntry& aFLE,
                              const nsCString& aPSName,
                              const nsCString& aFullName, StandardFile aStdFile)
                               MOZ_REQUIRES(PlatformFontList()->mLock) {
                                 PlatformFontList()->CollectInitData(
                                     aFLE, aPSName, aFullName, aStdFile);
                               };
      if (AppendFacesFromCachedFaceList(SharedFontList() ? shared : unshared,
                                        aEntryName, faceList, kStandard)) {
        return;
      }
    }
  }

  nsZipItem* item = aArchive->GetItem(aEntryName);
  NS_ASSERTION(item, "failed to find zip entry");

  uint32_t bufSize = item->RealSize();

  // We use fallible allocation here; if there's not enough RAM, we'll simply
  // ignore the bundled fonts and fall back to the device's installed fonts.
  char* buffer = static_cast<char*>(malloc(bufSize));
  if (!buffer) {
    return;
  }

  nsZipCursor cursor(item, aArchive, (uint8_t*)buffer, bufSize);
  uint8_t* data = cursor.Copy(&bufSize);
  MOZ_ASSERT(data && bufSize == item->RealSize(), "error reading bundled font");
  if (!data) {
    return;
  }

  gfxFontUtils::AutoHBBlob blob(
      hb_blob_create(buffer, bufSize, HB_MEMORY_MODE_READONLY, buffer, free));
  AppendFacesFromBlob(aEntryName, kStandard, blob, aCache, 0, bufSize);
}

// Called on each family after all fonts are added to the list;
// if aSortFaces is true this will sort faces to give priority to "standard"
// font files.
void FT2FontFamily::FinalizeMemberList(bool aSortFaces) {
  AutoWriteLock lock(mLock);

  SetHasStyles(true);

  if (aSortFaces) {
    SortAvailableFonts();
  }
  CheckForSimpleFamily();
}

void gfxFT2FontList::FindFonts() {
  MOZ_ASSERT(XRE_IsParentProcess());

  // Chrome process: get the cached list (if any)
  if (!mFontNameCache) {
    mFontNameCache = MakeUnique<FontNameCache>();
  }
  mFontNameCache->Init();

#if defined(MOZ_WIDGET_ANDROID)
  // Android API 29+ provides system font and font matcher API for native code.

  nsAutoCString androidFontsRoot = [&] {
    // ANDROID_ROOT is the root of the android system, typically /system;
    // font files are in /$ANDROID_ROOT/fonts/
    nsAutoCString root;
    char* androidRoot = PR_GetEnv("ANDROID_ROOT");
    if (androidRoot) {
      root = androidRoot;
    } else {
      root = "/system"_ns;
    }
    root.AppendLiteral("/fonts");
    return root;
  }();

  bool useSystemFontAPI = !gfxAndroidPlatform::IsFontAPIDisabled();

  if (useSystemFontAPI) {
    bool noFontByFontAPI = true;
    if (__builtin_available(android 29, *)) {
      gfxAndroidPlatform::WaitForInitializeFontAPI();
      AndroidSystemFontIterator iter;
      while (Maybe<AndroidFont> androidFont = iter.Next()) {
        const char* _Nonnull fontPath = androidFont->GetFontFilePath();
        noFontByFontAPI = false;
        AppendFacesFromFontFile(nsDependentCString(fontPath),
                                mFontNameCache.get(), kStandard);
      }
    }

    if (noFontByFontAPI) {
      // Font API doesn't seem to work. Use legacy way.
      useSystemFontAPI = false;
    }
  }

  if (!useSystemFontAPI)
#endif
  {
    FindFontsInDir(androidFontsRoot, mFontNameCache.get());
  }

  // Look for fonts stored in omnijar, unless we're on a low-memory
  // device where we don't want to spend the RAM to decompress them.
  // (Prefs may disable this, or force-enable it even with low memory.)
  if (StaticPrefs::gfx_bundled_fonts_activate_AtStartup() > 0 ||
      (StaticPrefs::gfx_bundled_fonts_activate_AtStartup() < 0 &&
       !nsMemory::IsLowMemoryPlatform())) {
    auto timer = glean::fontlist::bundledfonts_activate.Measure();
    FindFontsInOmnijar(mFontNameCache.get());
  }

  // Look for downloaded fonts in a profile-agnostic "fonts" directory.
  nsCOMPtr<nsIProperties> dirSvc =
      do_GetService(NS_DIRECTORY_SERVICE_CONTRACTID);
  if (dirSvc) {
    nsCOMPtr<nsIFile> appDir;
    nsresult rv = dirSvc->Get(NS_XPCOM_CURRENT_PROCESS_DIR, NS_GET_IID(nsIFile),
                              getter_AddRefs(appDir));
    if (NS_SUCCEEDED(rv)) {
      appDir->AppendNative("fonts"_ns);
      nsCString localPath;
      if (NS_SUCCEEDED(appDir->GetNativePath(localPath))) {
        FindFontsInDir(localPath, mFontNameCache.get());
      }
    }
  }

  // look for locally-added fonts in a "fonts" subdir of the profile
  nsCOMPtr<nsIFile> localDir;
  nsresult rv = NS_GetSpecialDirectory(NS_APP_USER_PROFILE_LOCAL_50_DIR,
                                       getter_AddRefs(localDir));
  if (NS_SUCCEEDED(rv) && NS_SUCCEEDED(localDir->Append(u"fonts"_ns))) {
    nsCString localPath;
    rv = localDir->GetNativePath(localPath);
    if (NS_SUCCEEDED(rv)) {
      FindFontsInDir(localPath, mFontNameCache.get());
    }
  }

  mFontNameCache->DropStaleEntries();
  if (!mFontNameCache->EntryCount()) {
    // if we can't find any usable fonts, we are doomed!
    MOZ_CRASH("No font files found");
  }

  // Write out FontCache data if needed
  WriteCache();
}

void gfxFT2FontList::WriteCache() {
  if (mFontNameCache) {
    mFontNameCache->WriteCache();
  }
  mozilla::scache::StartupCache* cache =
      mozilla::scache::StartupCache::GetSingleton();
  if (cache && mJarModifiedTime > 0) {
    const size_t bufSize = sizeof(mJarModifiedTime);
    auto buf = UniqueFreePtr<char[]>(
        reinterpret_cast<char*>(malloc(sizeof(char) * bufSize)));
    LittleEndian::writeInt64(buf.get(), mJarModifiedTime);

    LOG(("WriteCache: putting Jar, length %zu", bufSize));
    cache->PutBuffer(JAR_LAST_MODIFED_TIME, std::move(buf), bufSize);
  }
  LOG(("Done with writecache"));
}

void gfxFT2FontList::FindFontsInDir(const nsCString& aDir,
                                    FontNameCache* aFNC) {
  static const char* sStandardFonts[] = {"DroidSans.ttf",
                                         "DroidSans-Bold.ttf",
                                         "DroidSerif-Regular.ttf",
                                         "DroidSerif-Bold.ttf",
                                         "DroidSerif-Italic.ttf",
                                         "DroidSerif-BoldItalic.ttf",
                                         "DroidSansMono.ttf",
                                         "DroidSansArabic.ttf",
                                         "DroidSansHebrew.ttf",
                                         "DroidSansThai.ttf",
                                         "MTLmr3m.ttf",
                                         "MTLc3m.ttf",
                                         "NanumGothic.ttf",
                                         "NotoColorEmoji.ttf",
                                         "NotoColorEmojiFlags.ttf",
                                         "NotoColorEmojiLegacy.ttf",
                                         "DroidSansJapanese.ttf",
                                         "DroidSansFallback.ttf"};

  DIR* d = opendir(aDir.get());
  if (!d) {
    return;
  }

  struct dirent* ent = nullptr;
  while ((ent = readdir(d)) != nullptr) {
    const char* ext = strrchr(ent->d_name, '.');
    if (!ext) {
      continue;
    }
    if (strcasecmp(ext, ".ttf") == 0 || strcasecmp(ext, ".otf") == 0 ||
        strcasecmp(ext, ".woff") == 0 || strcasecmp(ext, ".ttc") == 0) {
      bool isStdFont = false;
      for (unsigned int i = 0; i < std::size(sStandardFonts) && !isStdFont;
           i++) {
        isStdFont = strcmp(sStandardFonts[i], ent->d_name) == 0;
      }

      nsAutoCString s(aDir);
      s.Append('/');
      s.Append(ent->d_name);

      // Add the face(s) from this file to our font list;
      // note that if we have cached info for this file in fnc,
      // and the file is unchanged, we won't actually need to read it.
      // If the file is new/changed, this will update the FontNameCache.
      AppendFacesFromFontFile(s, aFNC, isStdFont ? kStandard : kUnknown);
    }
  }

  closedir(d);
}

void gfxFT2FontList::AppendFaceFromFontListEntry(const FontListEntry& aFLE,
                                                 StandardFile aStdFile) {
  FT2FontEntry* fe = FT2FontEntry::CreateFontEntry(aFLE);
  if (fe) {
    nsAutoCString key(aFLE.familyName());
    BuildKeyNameFromFontName(key);
    fe->mStandardFace = (aStdFile == kStandard);
    RefPtr<gfxFontFamily> family = mFontFamilies.LookupOrInsertWith(key, [&] {
      auto family =
          MakeRefPtr<FT2FontFamily>(aFLE.familyName(), aFLE.visibility());
      if (mSkipSpaceLookupCheckFamilies.Contains(key)) {
        family->SetSkipSpaceFeatureCheck(true);
      }
      if (mBadUnderlineFamilyNames.ContainsSorted(key)) {
        family->SetBadUnderlineFamily();
      }
      return family;
    });
    family->AddFontEntry(fe);

    fe->CheckForBrokenFont(family);
  }
}

void gfxFT2FontList::ReadSystemFontList(dom::SystemFontList* aList) {
  AutoLock lock(mLock);
  for (const auto& entry : mFontFamilies) {
    auto family = static_cast<FT2FontFamily*>(entry.GetData().get());
    family->AddFacesToFontList(&aList->entries());
  }
}

static void LoadSkipSpaceLookupCheck(
    nsTHashSet<nsCString>& aSkipSpaceLookupCheck) {
  AutoTArray<nsCString, 5> skiplist;
  gfxFontUtils::GetPrefsFontList(
      "font.whitelist.skip_default_features_space_check", skiplist);
  uint32_t numFonts = skiplist.Length();
  for (uint32_t i = 0; i < numFonts; i++) {
    ToLowerCase(skiplist[i]);
    aSkipSpaceLookupCheck.Insert(skiplist[i]);
  }
}

nsresult gfxFT2FontList::InitFontListForPlatform() {
  LoadSkipSpaceLookupCheck(mSkipSpaceLookupCheckFamilies);

  if (XRE_IsParentProcess()) {
    // This will populate/update mFontNameCache and store it in the
    // startupCache for future startups.
    FindFonts();

    // Finalize the families by sorting faces into standard order
    // and marking "simple" families.
    for (const auto& entry : mFontFamilies) {
      auto* family = static_cast<FT2FontFamily*>(entry.GetData().get());
      family->FinalizeMemberList(/* aSortFaces */ true);
    }

    return NS_OK;
  }

  // Content process: use font list passed from the chrome process via
  // the GetXPCOMProcessAttributes message.
  auto& fontList = dom::ContentChild::GetSingleton()->SystemFontList();
  for (FontListEntry& fle : fontList.entries()) {
    // We don't need to identify "standard" font files here,
    // as the faces are already sorted.
    AppendFaceFromFontListEntry(fle, kUnknown);
  }

  // We don't need to sort faces (because they were already sorted by the
  // chrome process, so we just maintain the existing order)
  for (const auto& entry : mFontFamilies) {
    auto* family = static_cast<FT2FontFamily*>(entry.GetData().get());
    family->FinalizeMemberList(/* aSortFaces */ false);
  }

  LOG(("got font list from chrome process: %" PRIdPTR " faces in %" PRIu32
       " families",
       fontList.entries().Length(), mFontFamilies.Count()));
  fontList.entries().Clear();

  return NS_OK;
}

void gfxFT2FontList::InitSharedFontListForPlatform() {
  if (!XRE_IsParentProcess()) {
    // Content processes will access the shared-memory data created by the
    // parent, so don't need to scan for available fonts themselves.
    return;
  }

  // This will populate mFontNameCache with entries for all the available font
  // files, and record them in mFamilies (unshared list) or mFamilyInitData and
  // mFaceInitData (shared font list).
  FindFonts();

  mozilla::fontlist::FontList* list = SharedFontList();
  list->SetFamilyNames(mFamilyInitData);

  auto families = list->Families();
  for (uint32_t i = 0; i < mFamilyInitData.Length(); i++) {
    auto faceList = mFaceInitData.Get(mFamilyInitData[i].mKey);
    MOZ_ASSERT(faceList);
    families[i].AddFaces(list, *faceList);
  }

  mFamilyInitData.Clear();
  mFaceInitData.Clear();
}

already_AddRefed<gfxFontEntry> gfxFT2FontList::CreateFontEntry(
    fontlist::Face* aFace, const fontlist::Family* aFamily) {
  fontlist::FontList* list = SharedFontList();
  nsAutoCString desc(aFace->mDescriptor.AsString(list));
  RefPtr<FT2FontEntry> fe =
      FT2FontEntry::CreateFontEntry(desc, desc.get(), aFace->mIndex, nullptr);
  fe->InitializeFrom(aFace, aFamily);
  fe->CheckForBrokenFont(aFamily->Key().AsString(list));
  return fe.forget();
}

// called for each family name, based on the assumption that the
// first part of the full name is the family name

already_AddRefed<gfxFontEntry> gfxFT2FontList::LookupLocalFont(
    FontVisibilityProvider* aFontVisibilityProvider,
    const nsACString& aFontName, WeightRange aWeightForEntry,
    WidthRange aWidthForEntry, SlantStyleRange aStyleForEntry) {
  AutoLock lock(mLock);

  if (SharedFontList()) {
    return LookupInSharedFaceNameList(aFontVisibilityProvider, aFontName,
                                      aWeightForEntry, aWidthForEntry,
                                      aStyleForEntry);
  }

  // walk over list of names
  FT2FontEntry* fontEntry = nullptr;
  FontVisibility level = aFontVisibilityProvider
                             ? aFontVisibilityProvider->GetFontVisibility()
                             : FontVisibility::User;

  for (const RefPtr<gfxFontFamily>& fontFamily : mFontFamilies.Values()) {
    if (!IsVisibleToCSS(*fontFamily, level)) {
      continue;
    }

    // Check family name, based on the assumption that the
    // first part of the full name is the family name

    // does the family name match up to the length of the family name?
    const nsCString& family = fontFamily->Name();

    const nsAutoCString fullNameFamily(
        Substring(aFontName, 0, family.Length()));

    // if so, iterate over faces in this family to see if there is a match
    if (family.Equals(fullNameFamily, nsCaseInsensitiveCStringComparator)) {
      gfxFontEntry* fe =
          fontFamily->FindFont(aFontName, nsCaseInsensitiveCStringComparator);
      if (fe) {
        fontEntry = static_cast<FT2FontEntry*>(fe);
        goto searchDone;
      }
    }
  }

searchDone:
  if (!fontEntry) {
    return nullptr;
  }

  // Clone the font entry so that we can then set its style descriptors
  // from the userfont entry rather than the actual font.

  // Ensure existence of mFTFace in the original entry
  RefPtr<SharedFTFace> face = fontEntry->GetFTFace(true);
  if (!face) {
    return nullptr;
  }

  RefPtr<gfxFontEntry> fe = FT2FontEntry::CreateFontEntry(
      fontEntry->Name(), fontEntry->mFilename.get(), fontEntry->mFTFontIndex,
      nullptr);
  if (fe) {
    fe->mStyleRange = aStyleForEntry;
    fe->mWeightRange = aWeightForEntry;
    fe->mWidthRange = aWidthForEntry;
    fe->mIsLocalUserFont = true;
  }

  return fe.forget();
}

FontFamily gfxFT2FontList::GetDefaultFontForPlatform(
    FontVisibilityProvider* aFontVisibilityProvider, const gfxFontStyle* aStyle,
    nsAtom* aLanguage) {
  FontFamily ff;
#if defined(MOZ_WIDGET_ANDROID)
  ff = FindFamily(aFontVisibilityProvider, "Roboto"_ns);
  if (ff.IsNull()) {
    ff = FindFamily(aFontVisibilityProvider, "Droid Sans"_ns);
  }
#endif
  /* TODO: what about Qt or other platforms that may use this? */
  return ff;
}

already_AddRefed<gfxFontEntry> gfxFT2FontList::MakePlatformFont(
    const nsACString& aFontName, WeightRange aWeightForEntry,
    WidthRange aWidthForEntry, SlantStyleRange aStyleForEntry,
    FontData* aFontData) {
  // The FT2 font needs the font data to persist, so the font entry will
  // hold a strong ref to aFontData for as long as it exists.
  return FT2FontEntry::CreateFontEntry(
      aFontName, aWeightForEntry, aWidthForEntry, aStyleForEntry, aFontData);
}

already_AddRefed<gfxFontFamily> gfxFT2FontList::CreateFontFamily(
    const nsACString& aName, FontVisibility aVisibility) const {
  return MakeAndAddRef<FT2FontFamily>(aName, aVisibility);
}

void gfxFT2FontList::WillShutdown() {
  LOG(("WillShutdown"));
  WriteCache();
  mFontNameCache = nullptr;
}
