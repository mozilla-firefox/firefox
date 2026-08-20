/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "gfxFcPlatformFontList.h"

#include "SharedFontList-impl.h"
#include "gfxFT2Utils.h"
#include "gfxFont.h"
#include "gfxPlatform.h"
#include "mozilla/Components.h"
#include "mozilla/FileUtils.h"
#include "mozilla/Logging.h"
#include "mozilla/Preferences.h"
#include "mozilla/Sprintf.h"
#include "mozilla/StaticPrefs_gfx.h"
#include "mozilla/StaticPrefs_mathml.h"
#include "mozilla/dom/ContentChild.h"
#include "mozilla/dom/ContentParent.h"
#include "mozilla/glean/GfxMetrics.h"
#include "nsCharSeparatedTokenizer.h"
#include "nsDirectoryServiceDefs.h"
#include "nsDirectoryServiceUtils.h"
#include "nsGkAtoms.h"
#include "nsIConsoleService.h"
#include "nsIGfxInfo.h"
#include "nsPresContext.h"
#include "nsString.h"
#include "nsStringFwd.h"
#include "nsXULAppAPI.h"
#define StandardFonts
#include "StandardFonts-linux.inc"
#undef StandardFonts
#include <cairo-ft.h>
#include <dlfcn.h>
#include <fontconfig/fcfreetype.h>
#include <fontconfig/fontconfig.h>
#include <harfbuzz/hb.h>
#include <unistd.h>

#include "mozilla/intl/Locale.h"

#ifdef MOZ_WIDGET_GTK
#  include <gdk/gdk.h>
#  include <gtk/gtk.h>

#  include "gfxPlatformGtk.h"
#  include "mozilla/WidgetUtilsGtk.h"
#endif

#ifdef MOZ_X11
#  include "mozilla/X11Util.h"
#endif

#if defined(MOZ_SANDBOX) && defined(XP_LINUX)
#  include "mozilla/SandboxBrokerPolicyFactory.h"
#  include "mozilla/SandboxSettings.h"
#endif

#include FT_MULTIPLE_MASTERS_H

using namespace mozilla;
using namespace mozilla::gfx;
using namespace mozilla::intl;

#ifndef FC_POSTSCRIPT_NAME
#  define FC_POSTSCRIPT_NAME "postscriptname" /* String */
#endif
#ifndef FC_VARIABLE
#  define FC_VARIABLE "variable" /* Bool */
#endif
#ifndef FC_NAMED_INSTANCE
#  define FC_NAMED_INSTANCE "namedinstance" /* Bool */
#endif

#define PRINTING_FC_PROPERTY "gfx.printing"

#define LOG_FONTLIST(args) \
  MOZ_LOG(gfxPlatform::GetLog(eGfxLog_fontlist), LogLevel::Debug, args)
#define LOG_FONTLIST_ENABLED() \
  MOZ_LOG_TEST(gfxPlatform::GetLog(eGfxLog_fontlist), LogLevel::Debug)
#define LOG_CMAPDATA_ENABLED() \
  MOZ_LOG_TEST(gfxPlatform::GetLog(eGfxLog_cmapdata), LogLevel::Debug)

static const FcChar8* ToFcChar8Ptr(const char* aStr) {
  return reinterpret_cast<const FcChar8*>(aStr);
}

static const char* ToCharPtr(const FcChar8* aStr) {
  return reinterpret_cast<const char*>(aStr);
}

// Detect fontconfig 2.18 regression in FcNameUnparse, see bug 2051021 and
// https://gitlab.freedesktop.org/fontconfig/fontconfig/-/merge_requests/544
static bool FontconfigUnparseOmitsStringEscapes() {
  static const bool sBroken = [] {
    RefPtr<FcPattern> pat = dont_AddRef(FcPatternCreate());
    FcPatternAddString(pat, FC_FAMILY, ToFcChar8Ptr("a-b"));
    FcChar8* s = FcNameUnparse(pat);
    bool broken = s && !strchr(ToCharPtr(s), '\\');
    if (s) {
      free(s);
    }
    return broken;
  }();
  return sBroken;
}

static already_AddRefed<FcPattern> MaybeEscapeFamilyForBrokenUnparse(
    FcPattern* aPattern) {
  if (!FontconfigUnparseOmitsStringEscapes()) {
    return nullptr;
  }
  // These chars match the font family escape chars from fontconfig.
  static const char kEscapeChars[] = "\\-:,";
  AutoTArray<nsCString, 4> families;
  bool needsEscaping = false;
  FcChar8* value;
  for (int i = 0;
       FcPatternGetString(aPattern, FC_FAMILY, i, &value) == FcResultMatch;
       ++i) {
    nsAutoCString escaped;
    for (const char* p = ToCharPtr(value); *p; ++p) {
      if (strchr(kEscapeChars, *p)) {
        escaped.Append('\\');
        needsEscaping = true;
      }
      escaped.Append(*p);
    }
    families.AppendElement(escaped);
  }
  if (!needsEscaping) {
    return nullptr;
  }
  RefPtr<FcPattern> dup = dont_AddRef(FcPatternDuplicate(aPattern));
  if (!dup) {
    return nullptr;
  }
  FcPatternDel(dup, FC_FAMILY);
  for (const auto& family : families) {
    FcPatternAddString(dup, FC_FAMILY, ToFcChar8Ptr(family.get()));
  }
  return dup.forget();
}

// canonical name ==> first en name or first name if no en name
// This is the required logic for fullname lookups as per CSS3 Fonts spec.
static uint32_t FindCanonicalNameIndex(FcPattern* aFont,
                                       const char* aLangField) {
  uint32_t n = 0, en = 0;
  FcChar8* lang;
  while (FcPatternGetString(aFont, aLangField, n, &lang) == FcResultMatch) {
    // look for 'en' or variants, en-US, en-JP etc.
    uint32_t len = strlen(ToCharPtr(lang));
    bool enPrefix = (strncmp(ToCharPtr(lang), "en", 2) == 0);
    if (enPrefix && (len == 2 || (len > 2 && aLangField[2] == '-'))) {
      en = n;
      break;
    }
    n++;
  }
  return en;
}

static void GetFaceNames(FcPattern* aFont, const nsACString& aFamilyName,
                         nsACString& aPostscriptName, nsACString& aFullname) {
  // get the Postscript name
  FcChar8* psname;
  if (FcPatternGetString(aFont, FC_POSTSCRIPT_NAME, 0, &psname) ==
      FcResultMatch) {
    aPostscriptName = ToCharPtr(psname);
  }

  // get the canonical fullname (i.e. en name or first name)
  uint32_t en = FindCanonicalNameIndex(aFont, FC_FULLNAMELANG);
  FcChar8* fullname;
  if (FcPatternGetString(aFont, FC_FULLNAME, en, &fullname) == FcResultMatch) {
    aFullname = ToCharPtr(fullname);
  }

  // if have fullname, done
  if (!aFullname.IsEmpty()) {
    return;
  }

  // otherwise, set the fullname to family + style name [en] and use that
  aFullname = aFamilyName;

  // figure out the en style name
  en = FindCanonicalNameIndex(aFont, FC_STYLELANG);
  nsAutoCString style;
  FcChar8* stylename = nullptr;
  FcPatternGetString(aFont, FC_STYLE, en, &stylename);
  if (stylename) {
    style = ToCharPtr(stylename);
  }

  if (!style.IsEmpty() && !style.EqualsLiteral("Regular")) {
    aFullname.Append(' ');
    aFullname.Append(style);
  }
}

static FontWeight MapFcWeight(int aFcWeight) {
  if (aFcWeight <= (FC_WEIGHT_THIN + FC_WEIGHT_EXTRALIGHT) / 2) {
    return FontWeight::FromInt(100);
  }
  if (aFcWeight <= (FC_WEIGHT_EXTRALIGHT + FC_WEIGHT_LIGHT) / 2) {
    return FontWeight::FromInt(200);
  }
  if (aFcWeight <= (FC_WEIGHT_LIGHT + FC_WEIGHT_BOOK) / 2) {
    return FontWeight::FromInt(300);
  }
  if (aFcWeight <= (FC_WEIGHT_REGULAR + FC_WEIGHT_MEDIUM) / 2) {
    // This includes FC_WEIGHT_BOOK
    return FontWeight::FromInt(400);
  }
  if (aFcWeight <= (FC_WEIGHT_MEDIUM + FC_WEIGHT_DEMIBOLD) / 2) {
    return FontWeight::FromInt(500);
  }
  if (aFcWeight <= (FC_WEIGHT_DEMIBOLD + FC_WEIGHT_BOLD) / 2) {
    return FontWeight::FromInt(600);
  }
  if (aFcWeight <= (FC_WEIGHT_BOLD + FC_WEIGHT_EXTRABOLD) / 2) {
    return FontWeight::FromInt(700);
  }
  if (aFcWeight <= (FC_WEIGHT_EXTRABOLD + FC_WEIGHT_BLACK) / 2) {
    return FontWeight::FromInt(800);
  }
  if (aFcWeight <= FC_WEIGHT_BLACK) {
    return FontWeight::FromInt(900);
  }

  // including FC_WEIGHT_EXTRABLACK
  return FontWeight::FromInt(901);
}

// TODO(emilio, jfkthame): I think this can now be more fine-grained.
static FontWidth MapFcWidth(int aFcWidth) {
  if (aFcWidth <= (FC_WIDTH_ULTRACONDENSED + FC_WIDTH_EXTRACONDENSED) / 2) {
    return FontWidth::ULTRA_CONDENSED;
  }
  if (aFcWidth <= (FC_WIDTH_EXTRACONDENSED + FC_WIDTH_CONDENSED) / 2) {
    return FontWidth::EXTRA_CONDENSED;
  }
  if (aFcWidth <= (FC_WIDTH_CONDENSED + FC_WIDTH_SEMICONDENSED) / 2) {
    return FontWidth::CONDENSED;
  }
  if (aFcWidth <= (FC_WIDTH_SEMICONDENSED + FC_WIDTH_NORMAL) / 2) {
    return FontWidth::SEMI_CONDENSED;
  }
  if (aFcWidth <= (FC_WIDTH_NORMAL + FC_WIDTH_SEMIEXPANDED) / 2) {
    return FontWidth::NORMAL;
  }
  if (aFcWidth <= (FC_WIDTH_SEMIEXPANDED + FC_WIDTH_EXPANDED) / 2) {
    return FontWidth::SEMI_EXPANDED;
  }
  if (aFcWidth <= (FC_WIDTH_EXPANDED + FC_WIDTH_EXTRAEXPANDED) / 2) {
    return FontWidth::EXPANDED;
  }
  if (aFcWidth <= (FC_WIDTH_EXTRAEXPANDED + FC_WIDTH_ULTRAEXPANDED) / 2) {
    return FontWidth::EXTRA_EXPANDED;
  }
  return FontWidth::ULTRA_EXPANDED;
}

static void GetFontProperties(FcPattern* aFontPattern, WeightRange* aWeight,
                              WidthRange* aWidth, SlantStyleRange* aSlantStyle,
                              uint16_t* aSize = nullptr) {
  // weight
  int weight;
  if (FcPatternGetInteger(aFontPattern, FC_WEIGHT, 0, &weight) !=
      FcResultMatch) {
    weight = FC_WEIGHT_REGULAR;
  }
  *aWeight = WeightRange(MapFcWeight(weight));

  // width
  int width;
  if (FcPatternGetInteger(aFontPattern, FC_WIDTH, 0, &width) != FcResultMatch) {
    width = FC_WIDTH_NORMAL;
  }
  *aWidth = WidthRange(MapFcWidth(width));

  // italic
  int slant;
  if (FcPatternGetInteger(aFontPattern, FC_SLANT, 0, &slant) != FcResultMatch) {
    slant = FC_SLANT_ROMAN;
  }
  if (slant == FC_SLANT_OBLIQUE) {
    *aSlantStyle = SlantStyleRange(FontSlantStyle::OBLIQUE);
  } else if (slant > 0) {
    *aSlantStyle = SlantStyleRange(FontSlantStyle::ITALIC);
  }

  if (aSize) {
    // pixel size, or zero if scalable
    FcBool scalable;
    if (FcPatternGetBool(aFontPattern, FC_SCALABLE, 0, &scalable) ==
            FcResultMatch &&
        scalable) {
      *aSize = 0;
    } else {
      double size;
      if (FcPatternGetDouble(aFontPattern, FC_PIXEL_SIZE, 0, &size) ==
          FcResultMatch) {
        *aSize = uint16_t(NS_round(size));
      } else {
        *aSize = 0;
      }
    }
  }
}

void gfxFontconfigFontEntry::GetUserFontFeatures(FcPattern* aPattern) {
  int fontFeaturesNum = 0;
  char* s;
  hb_feature_t tmpFeature;
  while (FcResultMatch == FcPatternGetString(aPattern, "fontfeatures",
                                             fontFeaturesNum, (FcChar8**)&s)) {
    bool ret = hb_feature_from_string(s, -1, &tmpFeature);
    if (ret) {
      mFeatureSettings.AppendElement(
          (gfxFontFeature){tmpFeature.tag, tmpFeature.value});
    }
    fontFeaturesNum++;
  }
}

gfxFontconfigFontEntry::gfxFontconfigFontEntry(const nsACString& aFaceName,
                                               FcPattern* aFontPattern,
                                               bool aIgnoreFcCharmap)
    : gfxFT2FontEntryBase(aFaceName),
      mFontPattern(aFontPattern),
      mFTFaceInitialized(false),
      mIgnoreFcCharmap(aIgnoreFcCharmap) {
  GetFontProperties(aFontPattern, &mWeightRange, &mWidthRange, &mStyleRange);
  GetUserFontFeatures(mFontPattern);
}

gfxFontEntry* gfxFontconfigFontEntry::Clone() const {
  MOZ_ASSERT(!IsUserFont(), "we can only clone installed fonts!");
  return new gfxFontconfigFontEntry(Name(), mFontPattern, mIgnoreFcCharmap);
}

static already_AddRefed<FcPattern> CreatePatternForFace(FT_Face aFace) {
  // Use fontconfig to fill out the pattern from the FTFace.
  // The "file" argument cannot be nullptr (in fontconfig-2.6.0 at
  // least). The dummy filename passed here is removed below.
  //
  // When fontconfig scans the system fonts, FcConfigGetBlanks(nullptr)
  // is passed as the "blanks" argument, which provides that unexpectedly
  // blank glyphs are elided.  Here, however, we pass nullptr for
  // "blanks", effectively assuming that, if the font has a blank glyph,
  // then the author intends any associated character to be rendered
  // blank.
  RefPtr<FcPattern> pattern = dont_AddRef(
      FcFreeTypeQueryFace(aFace, ToFcChar8Ptr("(webfont)"), 0, nullptr));
  // given that we have a FT_Face, not really sure this is possible...
  if (!pattern) {
    pattern = dont_AddRef(FcPatternCreate());
  }
  FcPatternDel(pattern, FC_FILE);
  FcPatternDel(pattern, FC_INDEX);

  // Make a new pattern and store the face in it so that cairo uses
  // that when creating a cairo font face.
  FcPatternAddFTFace(pattern, FC_FT_FACE, aFace);

  return pattern.forget();
}

static FcChar8* GetPatternFilename(FcPattern* aPattern) {
  FcChar8* filename;
  if (FcPatternGetString(aPattern, FC_FILE, 0, &filename) != FcResultMatch) {
    filename = nullptr;
  }
  return filename;
}

static int GetPatternIndex(FcPattern* aPattern) {
  int index;
  if (FcPatternGetInteger(aPattern, FC_INDEX, 0, &index) != FcResultMatch) {
    index = 0;  // default to 0 if not found in pattern
  }
  return index;
}

static already_AddRefed<SharedFTFace> CreateFaceForPattern(
    FcPattern* aPattern) {
  FcChar8* const filename = GetPatternFilename(aPattern);
  if (!filename) {
    return nullptr;
  }
  const int index = GetPatternIndex(aPattern);
  return Factory::NewSharedFTFace(nullptr, ToCharPtr(filename), index);
}

gfxFontconfigFontEntry::gfxFontconfigFontEntry(const nsACString& aFaceName,
                                               WeightRange aWeight,
                                               WidthRange aWidth,
                                               SlantStyleRange aStyle,
                                               RefPtr<SharedFTFace>&& aFace)
    : gfxFT2FontEntryBase(aFaceName),
      mFontPattern(CreatePatternForFace(aFace->GetFace())),
      mFTFace(aFace.forget().take()),
      mFTFaceInitialized(true),
      mIgnoreFcCharmap(true) {
  mWeightRange = aWeight;
  mStyleRange = aStyle;
  mWidthRange = aWidth;
  mIsDataUserFont = true;
}

gfxFontconfigFontEntry::gfxFontconfigFontEntry(const nsACString& aFaceName,
                                               FcPattern* aFontPattern,
                                               WeightRange aWeight,
                                               WidthRange aWidth,
                                               SlantStyleRange aStyle)
    : gfxFT2FontEntryBase(aFaceName),
      mFontPattern(aFontPattern),
      mFTFaceInitialized(false) {
  mWeightRange = aWeight;
  mStyleRange = aStyle;
  mWidthRange = aWidth;
  mIsLocalUserFont = true;

  // The proper setting of mIgnoreFcCharmap is tricky for fonts loaded
  // via src:local()...
  // If the local font happens to come from the application fontset,
  // we want to set it to true so that color/svg fonts will work even
  // if the default glyphs are blank; but if the local font is a non-
  // sfnt face (e.g. legacy type 1) then we need to set it to false
  // because our cmap-reading code will fail and we depend on FT+Fc to
  // determine the coverage.
  // We set the flag here, but may flip it the first time TestCharacterMap
  // is called, at which point we'll look to see whether a 'cmap' is
  // actually present in the font.
  mIgnoreFcCharmap = true;

  GetUserFontFeatures(mFontPattern);
}

typedef FT_Error (*GetVarFunc)(FT_Face, FT_MM_Var**);
typedef FT_Error (*DoneVarFunc)(FT_Library, FT_MM_Var*);
static GetVarFunc sGetVar;
static DoneVarFunc sDoneVar;
static bool sInitializedVarFuncs = false;

static void InitializeVarFuncs() {
  if (sInitializedVarFuncs) {
    return;
  }
  sInitializedVarFuncs = true;
#if MOZ_TREE_FREETYPE
  sGetVar = &FT_Get_MM_Var;
  sDoneVar = &FT_Done_MM_Var;
#else
  sGetVar = (GetVarFunc)dlsym(RTLD_DEFAULT, "FT_Get_MM_Var");
  sDoneVar = (DoneVarFunc)dlsym(RTLD_DEFAULT, "FT_Done_MM_Var");
#endif
}

gfxFontconfigFontEntry::~gfxFontconfigFontEntry() {
  auto* cache = mFontTableCache.exchange(nullptr);
  delete cache;
  auto* face = mHBFace.exchange(nullptr);
  hb_face_destroy(face);
  if (mMMVar) {
    // Prior to freetype 2.9, there was no specific function to free the
    // FT_MM_Var record, and the docs just said to use free().
    // InitializeVarFuncs must have been called in order for mMMVar to be
    // non-null here, so we don't need to do it again.
    if (sDoneVar) {
      auto* ftFace = GetFTFace();
      MOZ_ASSERT(ftFace, "How did mMMVar get set without a face?");
      (*sDoneVar)(ftFace->GetFace()->glyph->library, mMMVar);
    } else {
      free(mMMVar);
    }
  }
  if (mFTFaceInitialized) {
    auto* face = mFTFace.exchange(nullptr);
    NS_IF_RELEASE(face);
  }
}

gfxFontconfigFontEntry::AutoHBFace gfxFontconfigFontEntry::GetHBFace() {
  hb_face_t* face = mHBFace;
  if (!face) {
    FcChar8* filename;
    FcPattern* pattern = GetPattern();
    bool useTableCache = false;
    if (FcPatternGetString(pattern, FC_FILE, 0, &filename) == FcResultMatch) {
      // Pattern has a filename: system font that we can load via
      // hb_face_create_from_file_or_fail, allowing harfbuzz to manage table
      // access internally.
      int index;
      if (FcPatternGetInteger(pattern, FC_INDEX, 0, &index) != FcResultMatch) {
        index = 0;  // default to 0 if not found in pattern
      }
      // Mask out possible variation-instance index stashed by fontconfig; we
      // just want the face index within a collection file.
      index &= 0xFFFF;
      face = hb_face_create_from_file_or_fail((const char*)filename, index);
    } else {
      // If we have an FT_Font with webfont user data attached, we can use
      // hb_face_create to wrap that.
      if (mFTFaceInitialized) {
        if (const FTUserFontData* ufd = GetUserFontData()) {
          if (const auto* data = ufd->GetData()) {
            hb_blob_t* blob =
                hb_blob_create((const char*)data, ufd->Length(),
                               HB_MEMORY_MODE_READONLY, nullptr, nullptr);
            // Currently the face index is always zero, as we don't support
            // collections as webfonts.
            face = hb_face_create(blob, 0);
            // Drop our blob reference; the face will hold on to it.
            hb_blob_destroy(blob);
          }
        }
      }
    }
    if (!face) {
      // Failed to create a face directly; fall back to gfxFontEntry::GetHBFace,
      // which will use hb_face_create_for_tables and the font table cache.
      NS_WARNING(nsPrintfCString("fallback to gfxFontEntry::GetHBFace for %s",
                                 Name().get())
                     .get());
      face = hb_face_reference(gfxFontEntry::GetHBFace());
      useTableCache = true;
    }
    AutoWriteLock lock(mLock);
    if (mHBFace.compareExchange(nullptr, face)) {
      if (useTableCache) {
        auto* cache = new FontTableCache();
        if (!mFontTableCache.compareExchange(nullptr, cache)) {
          delete cache;
        }
      }
    } else {
      // Lost a race to initialize mHBFace; discard our new one and use the
      // winner of the race.
      hb_face_destroy(face);
      face = mHBFace;
    }
  }
  // Return a new reference, owned by the AutoHBFace.
  return AutoHBFace(hb_face_reference(face));
}

nsresult gfxFontconfigFontEntry::ReadCMAP(FontInfoData* aFontInfoData) {
  // attempt this once, if errors occur leave a blank cmap
  if (HasCharacterMap()) {
    return NS_OK;
  }

  RefPtr<gfxCharacterMap> charmap;
  nsresult rv;

  uint32_t uvsOffset = 0;
  if (aFontInfoData &&
      (charmap = GetCMAPFromFontInfo(aFontInfoData, uvsOffset))) {
    rv = NS_OK;
  } else {
    uint32_t kCMAP = TRUETYPE_TAG('c', 'm', 'a', 'p');
    charmap = new gfxCharacterMap(256);
    AutoTable cmapTable(this, kCMAP);

    if (cmapTable) {
      uint32_t cmapLen;
      const uint8_t* cmapData = reinterpret_cast<const uint8_t*>(
          hb_blob_get_data(cmapTable, &cmapLen));
      rv = gfxFontUtils::ReadCMAP(cmapData, cmapLen, *charmap, uvsOffset);
    } else {
      rv = NS_ERROR_NOT_AVAILABLE;
    }
  }
  mUVSOffset.exchange(uvsOffset);

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
      charmap.get()->AddRef();
    }
  }

  LOG_FONTLIST(("(fontlist-cmap) name: %s, size: %zu hash: %8.8x%s\n",
                mName.get(), charmap->SizeOfIncludingThis(moz_malloc_size_of),
                charmap->mHash, GetCharacterMapRaw() == charmap ? " new" : ""));
  if (LOG_CMAPDATA_ENABLED()) {
    char prefix[256];
    SprintfLiteral(prefix, "(cmapdata) name: %.220s", mName.get());
    charmap->Dump(prefix, eGfxLog_cmapdata);
  }

  return rv;
}

static bool HasChar(FcPattern* aFont, FcChar32 aCh) {
  FcCharSet* charset = nullptr;
  FcPatternGetCharSet(aFont, FC_CHARSET, 0, &charset);
  return charset && FcCharSetHasChar(charset, aCh);
}

bool gfxFontconfigFontEntry::TestCharacterMap(uint32_t aCh) {
  // For user fonts, or for fonts bundled with the app (which might include
  // color/svg glyphs where the default glyphs may be blank, and thus confuse
  // fontconfig/freetype's char map checking), we instead check the cmap
  // directly for character coverage.
  if (mIgnoreFcCharmap) {
    // If it does not actually have a cmap, switch our strategy to use
    // fontconfig's charmap after all (except for data fonts, which must
    // always have a cmap to have passed OTS validation).
    if (!mIsDataUserFont && !HasFontTable(TRUETYPE_TAG('c', 'm', 'a', 'p'))) {
      mIgnoreFcCharmap = false;
      // ...and continue with HasChar() below.
    } else {
      return gfxFontEntry::TestCharacterMap(aCh);
    }
  }
  // otherwise (for system fonts), use the charmap in the pattern
  return HasChar(mFontPattern, aCh);
}

bool gfxFontconfigFontEntry::HasFontTableInternal(uint32_t aTableTag) {
  if (FTUserFontData* ufd = GetUserFontData()) {
    if (const auto* data = ufd->GetData()) {
      return !!gfxFontUtils::FindTableDirEntry(data, aTableTag);
    }
  }
  return gfxFT2FontEntryBase::FaceHasTable(GetFTFace(), aTableTag);
}

hb_blob_t* gfxFontconfigFontEntry::GetFontTableInternal(uint32_t aTableTag) {
  // for data fonts, read directly from the font data
  if (FTUserFontData* ufd = GetUserFontData()) {
    if (const auto* data = ufd->GetData()) {
      return gfxFontUtils::GetTableFromFontData(data, aTableTag);
    }
  }

  // Use the cache only if it has already been created.
  if (mFontTableCache) {
    return gfxFontEntry::GetFontTableInternal(aTableTag);
  }

  auto* table = hb_face_reference_table(GetHBFace(), aTableTag);
  return table != hb_blob_get_empty() ? table : nullptr;
}

double gfxFontconfigFontEntry::GetAspect(uint8_t aSizeAdjustBasis) {
  using FontSizeAdjust = gfxFont::FontSizeAdjust;
  if (FontSizeAdjust::Tag(aSizeAdjustBasis) == FontSizeAdjust::Tag::ExHeight ||
      FontSizeAdjust::Tag(aSizeAdjustBasis) == FontSizeAdjust::Tag::CapHeight) {
    // try to compute aspect from OS/2 metrics if available
    AutoTable os2Table(this, TRUETYPE_TAG('O', 'S', '/', '2'));
    if (os2Table) {
      uint16_t upem = UnitsPerEm();
      if (upem != kInvalidUPEM) {
        uint32_t len;
        const auto* os2 =
            reinterpret_cast<const OS2Table*>(hb_blob_get_data(os2Table, &len));
        if (uint16_t(os2->version) >= 2) {
          // XXX(jfkthame) Other implementations don't have the check for
          // values <= 0.1em; should we drop that here? Just require it to be
          // a positive number?
          if (FontSizeAdjust::Tag(aSizeAdjustBasis) ==
              FontSizeAdjust::Tag::ExHeight) {
            if (len >= offsetof(OS2Table, sxHeight) + sizeof(int16_t) &&
                int16_t(os2->sxHeight) > 0.1 * upem) {
              return double(int16_t(os2->sxHeight)) / upem;
            }
          }
          if (FontSizeAdjust::Tag(aSizeAdjustBasis) ==
              FontSizeAdjust::Tag::CapHeight) {
            if (len >= offsetof(OS2Table, sCapHeight) + sizeof(int16_t) &&
                int16_t(os2->sCapHeight) > 0.1 * upem) {
              return double(int16_t(os2->sCapHeight)) / upem;
            }
          }
        }
      }
    }
  }

  // create a font to calculate the requested aspect
  gfxFontStyle s;
  s.size = 256.0;  // pick large size to reduce hinting artifacts
  RefPtr<gfxFont> font = FindOrMakeFont(&s);
  if (font) {
    const gfxFont::Metrics& metrics =
        font->GetMetrics(nsFontMetrics::eHorizontal);
    if (metrics.emHeight == 0) {
      return 0;
    }
    switch (FontSizeAdjust::Tag(aSizeAdjustBasis)) {
      case FontSizeAdjust::Tag::ExHeight:
        return metrics.xHeight / metrics.emHeight;
      case FontSizeAdjust::Tag::CapHeight:
        return metrics.capHeight / metrics.emHeight;
      case FontSizeAdjust::Tag::ChWidth:
        return metrics.zeroWidth > 0 ? metrics.zeroWidth / metrics.emHeight
                                     : 0.5;
      case FontSizeAdjust::Tag::IcWidth:
      case FontSizeAdjust::Tag::IcHeight: {
        bool vertical = FontSizeAdjust::Tag(aSizeAdjustBasis) ==
                        FontSizeAdjust::Tag::IcHeight;
        gfxFloat advance =
            font->GetCharAdvance(gfxFont::kWaterIdeograph, vertical);
        return advance > 0 ? advance / metrics.emHeight : 1.0;
      }
      default:
        break;
    }
  }

  MOZ_ASSERT_UNREACHABLE("failed to compute size-adjust aspect");
  return 0.5;
}

static void PrepareFontOptions(FcPattern* aPattern, int* aOutLoadFlags,
                               unsigned int* aOutSynthFlags) {
  int loadFlags = FT_LOAD_DEFAULT;
  unsigned int synthFlags = 0;

  // xxx - taken from the gfxFontconfigFonts code, needs to be reviewed

  FcBool printing;
  if (FcPatternGetBool(aPattern, PRINTING_FC_PROPERTY, 0, &printing) !=
      FcResultMatch) {
    printing = FcFalse;
  }

  // Font options are set explicitly here to improve cairo's caching
  // behavior and to record the relevant parts of the pattern so that
  // the pattern can be released.
  //
  // Most font_options have already been set as defaults on the FcPattern
  // with cairo_ft_font_options_substitute(), then user and system
  // fontconfig configurations were applied.  The resulting font_options
  // have been recorded on the face during
  // cairo_ft_font_face_create_for_pattern().
  //
  // None of the settings here cause this scaled_font to behave any
  // differently from how it would behave if it were created from the same
  // face with default font_options.
  //
  // We set options explicitly so that the same scaled_font will be found in
  // the cairo_scaled_font_map when cairo loads glyphs from a context with
  // the same font_face, font_matrix, ctm, and surface font_options.
  //
  // Unfortunately, _cairo_scaled_font_keys_equal doesn't know about the
  // font_options on the cairo_ft_font_face, and doesn't consider default
  // option values to not match any explicit values.
  //
  // Even after cairo_set_scaled_font is used to set font_options for the
  // cairo context, when cairo looks for a scaled_font for the context, it
  // will look for a font with some option values from the target surface if
  // any values are left default on the context font_options.  If this
  // scaled_font is created with default font_options, cairo will not find
  // it.
  //
  // The one option not recorded in the pattern is hint_metrics, which will
  // affect glyph metrics.  The default behaves as CAIRO_HINT_METRICS_ON.
  // We should be considering the font_options of the surface on which this
  // font will be used, but currently we don't have different gfxFonts for
  // different surface font_options, so we'll create a font suitable for the
  // Screen. Image and xlib surfaces default to CAIRO_HINT_METRICS_ON.

  // The remaining options have been recorded on the pattern and the face.
  // _cairo_ft_options_merge has some logic to decide which options from the
  // scaled_font or from the cairo_ft_font_face take priority in the way the
  // font behaves.
  //
  // In the majority of cases, _cairo_ft_options_merge uses the options from
  // the cairo_ft_font_face, so sometimes it is not so important which
  // values are set here so long as they are not defaults, but we'll set
  // them to the exact values that we expect from the font, to be consistent
  // and to protect against changes in cairo.
  //
  // In some cases, _cairo_ft_options_merge uses some options from the
  // scaled_font's font_options rather than options on the
  // cairo_ft_font_face (from fontconfig).
  // https://bugs.freedesktop.org/show_bug.cgi?id=11838
  //
  // Surface font options were set on the pattern in
  // cairo_ft_font_options_substitute.  If fontconfig has changed the
  // hint_style then that is what the user (or distribution) wants, so we
  // use the setting from the FcPattern.
  //
  // Fallback values here mirror treatment of defaults in cairo-ft-font.c.
  FcBool hinting = FcFalse;
  if (FcPatternGetBool(aPattern, FC_HINTING, 0, &hinting) != FcResultMatch) {
    hinting = FcTrue;
  }

  int fc_hintstyle = FC_HINT_NONE;
  if (!printing && hinting &&
      FcPatternGetInteger(aPattern, FC_HINT_STYLE, 0, &fc_hintstyle) !=
          FcResultMatch) {
    fc_hintstyle = FC_HINT_FULL;
  }
  switch (fc_hintstyle) {
    case FC_HINT_NONE:
      loadFlags = FT_LOAD_NO_HINTING;
      break;
    case FC_HINT_SLIGHT:
      loadFlags = FT_LOAD_TARGET_LIGHT;
      break;
  }

  FcBool fc_antialias;
  if (FcPatternGetBool(aPattern, FC_ANTIALIAS, 0, &fc_antialias) !=
      FcResultMatch) {
    fc_antialias = FcTrue;
  }
  if (!fc_antialias) {
    if (fc_hintstyle != FC_HINT_NONE) {
      loadFlags = FT_LOAD_TARGET_MONO;
    }
    loadFlags |= FT_LOAD_MONOCHROME;
  } else if (fc_hintstyle == FC_HINT_FULL) {
    int fc_rgba;
    if (FcPatternGetInteger(aPattern, FC_RGBA, 0, &fc_rgba) != FcResultMatch) {
      fc_rgba = FC_RGBA_UNKNOWN;
    }
    switch (fc_rgba) {
      case FC_RGBA_RGB:
      case FC_RGBA_BGR:
        loadFlags = FT_LOAD_TARGET_LCD;
        break;
      case FC_RGBA_VRGB:
      case FC_RGBA_VBGR:
        loadFlags = FT_LOAD_TARGET_LCD_V;
        break;
    }
  }

  if (!FcPatternAllowsBitmaps(aPattern, fc_antialias != FcFalse,
                              fc_hintstyle != FC_HINT_NONE)) {
    loadFlags |= FT_LOAD_NO_BITMAP;
  }

  FcBool autohint;
  if (FcPatternGetBool(aPattern, FC_AUTOHINT, 0, &autohint) == FcResultMatch &&
      autohint) {
    loadFlags |= FT_LOAD_FORCE_AUTOHINT;
  }

  FcBool embolden;
  if (FcPatternGetBool(aPattern, FC_EMBOLDEN, 0, &embolden) == FcResultMatch &&
      embolden) {
    synthFlags |= CAIRO_FT_SYNTHESIZE_BOLD;
  }

  *aOutLoadFlags = loadFlags;
  *aOutSynthFlags = synthFlags;
}

#ifdef MOZ_X11
static bool GetXftInt(Display* aDisplay, const char* aName, int* aResult) {
  if (!aDisplay) {
    return false;
  }
  char* value = XGetDefault(aDisplay, "Xft", aName);
  if (!value) {
    return false;
  }
  if (FcNameConstant(const_cast<FcChar8*>(ToFcChar8Ptr(value)), aResult)) {
    return true;
  }
  char* end;
  *aResult = strtol(value, &end, 0);
  if (end != value) {
    return true;
  }
  return false;
}
#endif

static void PreparePattern(FcPattern* aPattern, bool aIsPrinterFont) {
  FcConfigSubstitute(nullptr, aPattern, FcMatchPattern);

  // This gets cairo_font_options_t for the Screen.  We should have
  // different font options for printing (no hinting) but we are not told
  // what we are measuring for.
  //
  // If cairo adds support for lcd_filter, gdk will not provide the default
  // setting for that option.  We could get the default setting by creating
  // an xlib surface once, recording its font_options, and then merging the
  // gdk options.
  //
  // Using an xlib surface would also be an option to get Screen font
  // options for non-GTK X11 toolkits, but less efficient than using GDK to
  // pick up dynamic changes.
  if (aIsPrinterFont) {
    cairo_font_options_t* options = cairo_font_options_create();
    cairo_font_options_set_hint_style(options, CAIRO_HINT_STYLE_NONE);
    cairo_font_options_set_antialias(options, CAIRO_ANTIALIAS_GRAY);
    cairo_ft_font_options_substitute(options, aPattern);
    cairo_font_options_destroy(options);
    FcPatternAddBool(aPattern, PRINTING_FC_PROPERTY, FcTrue);
#ifdef MOZ_WIDGET_GTK
  } else {
    gfxFcPlatformFontList::PlatformFontList()->SubstituteSystemFontOptions(
        aPattern);
#endif  // MOZ_WIDGET_GTK
  }

  FcDefaultSubstitute(aPattern);
}

void gfxFontconfigFontEntry::UnscaledFontCache::Add(
    const RefPtr<UnscaledFontFontconfig>& aUnscaledFont) {
  // Find the oldest entry and replace that with the new font.
  size_t oldestIdx = 0;
  int32_t lastGen = mLastGeneration;
  int32_t oldestAge = lastGen - mGenerations[0];
  for (size_t i = 1; i < kNumEntries; i++) {
    int32_t age = lastGen - mGenerations[i];
    if (age > oldestAge) {
      oldestIdx = i;
      oldestAge = age;
    }
  }
  mUnscaledFonts[oldestIdx] = aUnscaledFont;
  mGenerations[oldestIdx] = ++mLastGeneration;
}

already_AddRefed<UnscaledFontFontconfig>
gfxFontconfigFontEntry::UnscaledFontCache::Lookup(const std::string& aFile,
                                                  uint32_t aIndex) {
  for (size_t i = 0; i < kNumEntries; i++) {
    RefPtr<UnscaledFontFontconfig> entry(mUnscaledFonts[i]);
    if (entry && entry->GetFile() == aFile && entry->GetIndex() == aIndex) {
      mGenerations[i] = ++mLastGeneration;
      return entry.forget();
    }
  }
  return nullptr;
}

static inline gfxFloat SizeForStyle(gfxFontconfigFontEntry* aEntry,
                                    const gfxFontStyle& aStyle) {
  return StyleFontSizeAdjust::Tag(aStyle.sizeAdjustBasis) !=
                 StyleFontSizeAdjust::Tag::None
             ? aStyle.GetAdjustedSize(aEntry->GetAspect(aStyle.sizeAdjustBasis))
             : aStyle.size * aEntry->mSizeAdjust;
}

static double ChooseFontSize(gfxFontconfigFontEntry* aEntry,
                             const gfxFontStyle& aStyle) {
  double requestedSize = SizeForStyle(aEntry, aStyle);
  double bestDist = -1.0;
  double bestSize = requestedSize;
  double size;
  int v = 0;
  while (FcPatternGetDouble(aEntry->GetPattern(), FC_PIXEL_SIZE, v, &size) ==
         FcResultMatch) {
    ++v;
    double dist = fabs(size - requestedSize);
    if (bestDist < 0.0 || dist < bestDist) {
      bestDist = dist;
      bestSize = size;
    }
  }
  // If the font has bitmaps but wants to be scaled, then let it scale.
  if (bestSize >= 0.0) {
    FcBool scalable;
    if (FcPatternGetBool(aEntry->GetPattern(), FC_SCALABLE, 0, &scalable) ==
            FcResultMatch &&
        scalable) {
      return requestedSize;
    }
  }
  return bestSize;
}

gfxFont* gfxFontconfigFontEntry::CreateFontInstance(
    const gfxFontStyle* aFontStyle) {
  RefPtr<FcPattern> pattern = dont_AddRef(FcPatternCreate());
  if (!pattern) {
    NS_WARNING("Failed to create Fontconfig pattern for font instance");
    return nullptr;
  }

  double size = ChooseFontSize(this, *aFontStyle);
  FcPatternAddDouble(pattern, FC_PIXEL_SIZE, size);

  RefPtr<SharedFTFace> face = GetFTFace();
  if (!face) {
    NS_WARNING("Failed to get FreeType face for pattern");
    return nullptr;
  }
  if (HasVariations()) {
    // For variation fonts, we create a new FT_Face here so that
    // variation coordinates from the style can be applied without
    // affecting other font instances created from the same entry
    // (font resource).
    // For user fonts: create a new FT_Face from the font data, and then make
    // a pattern from that.
    // For system fonts: create a new FT_Face and store it in a copy of the
    // original mFontPattern.
    RefPtr<SharedFTFace> varFace = face->GetData()
                                       ? face->GetData()->CloneFace()
                                       : CreateFaceForPattern(mFontPattern);
    if (varFace) {
      AutoTArray<gfxFontVariation, 8> settings;
      GetVariationsForStyle(settings, *aFontStyle);
      gfxFT2FontBase::SetupVarCoords(GetMMVar(), settings, varFace->GetFace());
      face = std::move(varFace);
    }
  }

  PreparePattern(pattern, aFontStyle->printerFont);
  RefPtr<FcPattern> renderPattern =
      dont_AddRef(FcFontRenderPrepare(nullptr, pattern, mFontPattern));
  if (!renderPattern) {
    NS_WARNING("Failed to prepare Fontconfig pattern for font instance");
    return nullptr;
  }

  if (aFontStyle->NeedsSyntheticBold(this)) {
    FcPatternAddBool(renderPattern, FC_EMBOLDEN, FcTrue);
  }

  // will synthetic oblique be applied using a transform?
  if (IsUpright() && !aFontStyle->style.IsNormal() &&
      aFontStyle->synthesisStyle != StyleFontSynthesisStyle::None) {
    // disable embedded bitmaps (mimics behavior in 90-synthetic.conf)
    FcPatternDel(renderPattern, FC_EMBEDDED_BITMAP);
    FcPatternAddBool(renderPattern, FC_EMBEDDED_BITMAP, FcFalse);
  }

  int loadFlags;
  unsigned int synthFlags;
  PrepareFontOptions(renderPattern, &loadFlags, &synthFlags);

  std::string file;
  int index = 0;
  if (!face->GetData()) {
    const FcChar8* fcFile;
    if (FcPatternGetString(renderPattern, FC_FILE, 0,
                           const_cast<FcChar8**>(&fcFile)) != FcResultMatch ||
        FcPatternGetInteger(renderPattern, FC_INDEX, 0, &index) !=
            FcResultMatch) {
      NS_WARNING("No file in Fontconfig pattern for font instance");
      return nullptr;
    }
    file = ToCharPtr(fcFile);
  }

  RefPtr<UnscaledFontFontconfig> unscaledFont;
  {
    AutoReadLock lock(mLock);
    unscaledFont = mUnscaledFontCache.Lookup(file, index);
  }

  if (!unscaledFont) {
    AutoWriteLock lock(mLock);
    // Here, we use the original mFTFace, not a potential clone with variation
    // settings applied.
    auto* ftFace = GetFTFace();
    unscaledFont = ftFace->GetData() ? new UnscaledFontFontconfig(ftFace)
                                     : new UnscaledFontFontconfig(
                                           std::move(file), index, ftFace);
    mUnscaledFontCache.Add(unscaledFont);
  }

  gfxFont* newFont = new gfxFontconfigFont(
      unscaledFont, std::move(face), renderPattern, size, this, aFontStyle,
      loadFlags, (synthFlags & CAIRO_FT_SYNTHESIZE_BOLD) != 0);

  return newFont;
}

#ifdef MOZ_FONTATIONS
void gfxFontconfigFontEntry::InitSkrifaFont(FcPattern* aPattern) {
  using mozilla::MemoryMappedFile;
  using mozilla::gfx::SkrifaFontRef;

  // Try to load the file.
  const FcChar8* const filenameBytes = GetPatternFilename(aPattern);
  if (!filenameBytes) {
    return;
  }
  AutoFDClose fd(PR_Open(ToCharPtr(filenameBytes), PR_RDONLY, 0));
  MemoryMappedFile file = MemoryMappedFile::Open(fd.get());
  if (!file.IsValid()) {
    return;
  }

  const int index = GetPatternIndex(aPattern);
  const uint8_t* data = static_cast<const uint8_t*>(file.Data());
  const size_t size = file.Size();
  if (SkrifaFontRef* font = skrifa_font_new_from_index(data, size, index)) {
    SetSkrifaFont(font, std::move(file));
  }
}
#endif

SharedFTFace* gfxFontconfigFontEntry::GetFTFace() {
  if (!mFTFaceInitialized) {
#ifdef MOZ_FONTATIONS
    InitSkrifaFont(mFontPattern);
#endif
    RefPtr<SharedFTFace> face = CreateFaceForPattern(mFontPattern);

    if (face) {
      if (mFTFace.compareExchange(nullptr, face.get())) {
        face.forget().leak();  // The reference is now owned by mFTFace.
        mFTFaceInitialized = true;
      } else {
        // We lost a race to set mFTFace! Just discard our new face.
      }
    }
  }
  return mFTFace;
}

FTUserFontData* gfxFontconfigFontEntry::GetUserFontData() {
  auto* face = GetFTFace();
  if (face && face->GetData()) {
    return static_cast<FTUserFontData*>(face->GetData());
  }
  return nullptr;
}

bool gfxFontconfigFontEntry::HasVariations() {
  // If the answer is already cached, just return it.
  switch (mHasVariations) {
    case HasVariationsState::No:
      return false;
    case HasVariationsState::Yes:
      return true;
    case HasVariationsState::Uninitialized:
      break;
  }

  // Figure out whether we have variations, and record in mHasVariations.
  // (It doesn't matter if we race with another thread to set this; the result
  // will be the same.)

  if (!gfxPlatform::HasVariationFontSupport()) {
    mHasVariations = HasVariationsState::No;
    return false;
  }

  // For installed fonts, query the fontconfig pattern rather than paying
  // the cost of loading a FT_Face that we otherwise might never need.
  if (!IsUserFont() || IsLocalUserFont()) {
    FcBool variable;
    if ((FcPatternGetBool(mFontPattern, FC_VARIABLE, 0, &variable) ==
         FcResultMatch) &&
        variable) {
      mHasVariations = HasVariationsState::Yes;
      return true;
    }
  } else {
    if (auto* ftFace = GetFTFace()) {
      if (ftFace->GetFace()->face_flags & FT_FACE_FLAG_MULTIPLE_MASTERS) {
        mHasVariations = HasVariationsState::Yes;
        return true;
      }
    }
  }

  mHasVariations = HasVariationsState::No;
  return false;
}

FT_MM_Var* gfxFontconfigFontEntry::GetMMVar() {
  {
    AutoReadLock lock(mLock);
    if (mMMVarInitialized) {
      return mMMVar;
    }
  }

  AutoWriteLock lock(mLock);

  mMMVarInitialized = true;
  InitializeVarFuncs();
  if (!sGetVar) {
    return nullptr;
  }
  auto* ftFace = GetFTFace();
  if (!ftFace) {
    return nullptr;
  }
  if (FT_Err_Ok != (*sGetVar)(ftFace->GetFace(), &mMMVar)) {
    mMMVar = nullptr;
  }
  return mMMVar;
}

void gfxFontconfigFontEntry::GetVariationAxes(
    nsTArray<gfxFontVariationAxis>& aAxes) {
  if (!HasVariations()) {
    return;
  }
  gfxFT2Utils::GetVariationAxes(GetMMVar(), aAxes);
}

void gfxFontconfigFontEntry::GetVariationInstances(
    nsTArray<gfxFontVariationInstance>& aInstances) {
  if (!HasVariations()) {
    return;
  }
  gfxFT2Utils::GetVariationInstances(this, GetMMVar(), aInstances);
}

nsresult gfxFontconfigFontEntry::CopyFontTable(uint32_t aTableTag,
                                               nsTArray<uint8_t>& aBuffer) {
  NS_ASSERTION(!mIsDataUserFont,
               "data fonts should be reading tables directly from memory");
  return gfxFT2FontEntryBase::CopyFaceTable(GetFTFace(), aTableTag, aBuffer);
}

void gfxFontconfigFontFamily::FindStyleVariationsLocked(
    FontInfoData* aFontInfoData) {
  if (mHasStyles) {
    return;
  }

  // add font entries for each of the faces
  uint32_t numFonts = mFontPatterns.Length();
  NS_ASSERTION(numFonts, "font family containing no faces!!");
  uint32_t numRegularFaces = 0;
  for (uint32_t i = 0; i < numFonts; i++) {
    FcPattern* face = mFontPatterns[i];

    // figure out the psname/fullname and choose which to use as the facename
    nsAutoCString psname, fullname;
    GetFaceNames(face, mName, psname, fullname);
    const nsAutoCString& faceName = !psname.IsEmpty() ? psname : fullname;

    gfxFontconfigFontEntry* fontEntry =
        new gfxFontconfigFontEntry(faceName, face, mContainsAppFonts);

    if (gfxPlatform::HasVariationFontSupport()) {
      fontEntry->SetupVariationRanges();
    }

    AddFontEntryLocked(fontEntry);

    if (fontEntry->IsNormalStyle()) {
      numRegularFaces++;
    }

    if (LOG_FONTLIST_ENABLED()) {
      nsAutoCString weightString;
      fontEntry->Weight().ToString(weightString);
      nsAutoCString widthString;
      fontEntry->Width().ToString(widthString);
      nsAutoCString styleString;
      fontEntry->SlantStyle().ToString(styleString);
      LOG_FONTLIST((
          "(fontlist) added (%s) to family (%s)"
          " with style: %s weight: %s width: %s"
          " psname: %s fullname: %s",
          fontEntry->Name().get(), Name().get(), styleString.get(),
          weightString.get(), widthString.get(), psname.get(), fullname.get()));
    }
  }

  // somewhat arbitrary, but define a family with two or more regular
  // faces as a family for which intra-family fallback should be used
  if (numRegularFaces > 1) {
    mCheckForFallbackFaces = true;
  }
  mFaceNamesInitialized = true;
  mFontPatterns.Clear();
  SetHasStyles(true);

  CheckForSimpleFamily();
}

void gfxFontconfigFontFamily::AddFontPattern(FcPattern* aFontPattern,
                                             bool aSingleName) {
  NS_ASSERTION(
      !mHasStyles,
      "font patterns must not be added to already enumerated families");

  FcBool outline;
  if (FcPatternGetBool(aFontPattern, FC_OUTLINE, 0, &outline) !=
          FcResultMatch ||
      !outline) {
    mHasNonScalableFaces = true;

    FcBool scalable;
    if (FcPatternGetBool(aFontPattern, FC_SCALABLE, 0, &scalable) ==
            FcResultMatch &&
        scalable) {
      mForceScalable = true;
    }
  }

  if (aSingleName) {
    mFontPatterns.InsertElementAt(mUniqueNameFaceCount++, aFontPattern);
  } else {
    mFontPatterns.AppendElement(aFontPattern);
  }
}

static const double kRejectDistance = 10000.0;

// Calculate a distance score representing the size disparity between the
// requested style's size and the font entry's size.
static double SizeDistance(gfxFontconfigFontEntry* aEntry,
                           const gfxFontStyle& aStyle, bool aForceScalable) {
  double requestedSize = SizeForStyle(aEntry, aStyle);
  double bestDist = -1.0;
  double size;
  int v = 0;
  while (FcPatternGetDouble(aEntry->GetPattern(), FC_PIXEL_SIZE, v, &size) ==
         FcResultMatch) {
    ++v;
    double dist = fabs(size - requestedSize);
    if (bestDist < 0.0 || dist < bestDist) {
      bestDist = dist;
    }
  }
  if (bestDist < 0.0) {
    // No size means scalable
    return -1.0;
  } else if (aForceScalable || 5.0 * bestDist < requestedSize) {
    // fontconfig prefers a matching family or lang to pixelsize of bitmap
    // fonts. CSS suggests a tolerance of 20% on pixelsize.
    return bestDist;
  } else {
    // Reject any non-scalable fonts that are not within tolerance.
    return kRejectDistance;
  }
}

void gfxFontconfigFontFamily::FindAllFontsForStyle(
    const gfxFontStyle& aFontStyle, nsTArray<gfxFontEntry*>& aFontEntryList,
    bool aIgnoreSizeTolerance) {
  gfxFontFamily::FindAllFontsForStyle(aFontStyle, aFontEntryList,
                                      aIgnoreSizeTolerance);

  if (!mHasNonScalableFaces) {
    return;
  }

  // Iterate over the the available fonts while compacting any groups
  // of unscalable fonts with matching styles into a single entry
  // corresponding to the closest available size. If the closest
  // available size is rejected for being outside tolerance, then the
  // entire group will be skipped.
  size_t skipped = 0;
  gfxFontconfigFontEntry* bestEntry = nullptr;
  double bestDist = -1.0;
  for (size_t i = 0; i < aFontEntryList.Length(); i++) {
    gfxFontconfigFontEntry* entry =
        static_cast<gfxFontconfigFontEntry*>(aFontEntryList[i]);
    double dist =
        SizeDistance(entry, aFontStyle, mForceScalable || aIgnoreSizeTolerance);
    // If the entry is scalable or has a style that does not match
    // the group of unscalable fonts, then start a new group.
    if (dist < 0.0 || !bestEntry || bestEntry->Width() != entry->Width() ||
        bestEntry->Weight() != entry->Weight() ||
        bestEntry->SlantStyle() != entry->SlantStyle()) {
      // If the best entry in this group is still outside the tolerance,
      // then skip the entire group.
      if (bestDist >= kRejectDistance) {
        skipped++;
      }
      // Remove any compacted entries from the previous group.
      if (skipped) {
        i -= skipped;
        aFontEntryList.RemoveElementsAt(i, skipped);
        skipped = 0;
      }
      // Mark the start of the new group.
      bestEntry = entry;
      bestDist = dist;
    } else {
      // If this entry more closely matches the requested size than the
      // current best in the group, then take this entry instead.
      if (dist < bestDist) {
        aFontEntryList[i - 1 - skipped] = entry;
        bestEntry = entry;
        bestDist = dist;
      }
      skipped++;
    }
  }
  // If the best entry in this group is still outside the tolerance,
  // then skip the entire group.
  if (bestDist >= kRejectDistance) {
    skipped++;
  }
  // Remove any compacted entries from the current group.
  if (skipped) {
    aFontEntryList.TruncateLength(aFontEntryList.Length() - skipped);
  }
}

static bool PatternHasLang(const FcPattern* aPattern, const FcChar8* aLang) {
  FcLangSet* langset;

  if (FcPatternGetLangSet(aPattern, FC_LANG, 0, &langset) != FcResultMatch) {
    return false;
  }

  if (FcLangSetHasLang(langset, aLang) != FcLangDifferentLang) {
    return true;
  }
  return false;
}

bool gfxFontconfigFontFamily::SupportsLangGroup(nsAtom* aLangGroup) const {
  if (!aLangGroup || aLangGroup == nsGkAtoms::Unicode) {
    return true;
  }

  nsAutoCString fcLang;
  gfxFcPlatformFontList* pfl = gfxFcPlatformFontList::PlatformFontList();
  pfl->GetSampleLangForGroup(aLangGroup, fcLang);
  if (fcLang.IsEmpty()) {
    return true;
  }

  // Before FindStyleVariations has been called, mFontPatterns will contain
  // the font patterns.  Afterward, it'll be empty, but mAvailableFonts
  // will contain the font entries, each of which holds a reference to its
  // pattern.  We only check the first pattern in each list, because support
  // for langs is considered to be consistent across all faces in a family.
  AutoReadLock lock(mLock);
  FcPattern* fontPattern;
  if (mFontPatterns.Length()) {
    fontPattern = mFontPatterns[0];
  } else if (mAvailableFonts.Length()) {
    fontPattern = static_cast<gfxFontconfigFontEntry*>(mAvailableFonts[0].get())
                      ->GetPattern();
  } else {
    return true;
  }

  // is lang included in the underlying pattern?
  return PatternHasLang(fontPattern, ToFcChar8Ptr(fcLang.get()));
}

/* virtual */
gfxFontconfigFontFamily::~gfxFontconfigFontFamily() {
  // Should not be dropped by stylo
  MOZ_ASSERT(NS_IsMainThread());
}

template <typename Func>
void gfxFontconfigFontFamily::AddFacesToFontList(Func aAddPatternFunc) {
  AutoReadLock lock(mLock);
  if (HasStyles()) {
    for (auto& fe : mAvailableFonts) {
      if (!fe) {
        continue;
      }
      auto* fce = static_cast<gfxFontconfigFontEntry*>(fe.get());
      aAddPatternFunc(fce->GetPattern(), mContainsAppFonts);
    }
  } else {
    for (auto& pat : mFontPatterns) {
      aAddPatternFunc(pat, mContainsAppFonts);
    }
  }
}

gfxFontconfigFont::gfxFontconfigFont(
    const RefPtr<UnscaledFontFontconfig>& aUnscaledFont,
    RefPtr<SharedFTFace>&& aFTFace, FcPattern* aPattern, gfxFloat aAdjustedSize,
    gfxFontEntry* aFontEntry, const gfxFontStyle* aFontStyle, int aLoadFlags,
    bool aEmbolden)
    : gfxFT2FontBase(aUnscaledFont, std::move(aFTFace), aFontEntry, aFontStyle,
                     aLoadFlags, aEmbolden),
      mPattern(aPattern) {
  mAdjustedSize = aAdjustedSize;
  InitMetrics();
}

gfxFontconfigFont::~gfxFontconfigFont() = default;

already_AddRefed<ScaledFont> gfxFontconfigFont::GetScaledFont(
    const TextRunDrawParams& aRunParams) {
  if (ScaledFont* scaledFont = mAzureScaledFont) {
    return do_AddRef(scaledFont);
  }

  RefPtr<ScaledFont> newScaledFont = Factory::CreateScaledFontForFontconfigFont(
      GetUnscaledFont(), GetAdjustedSize(), mFTFace, GetPattern());
  if (!newScaledFont) {
    return nullptr;
  }

  InitializeScaledFont(newScaledFont);

  if (mAzureScaledFont.compareExchange(nullptr, newScaledFont.get())) {
    newScaledFont.forget().leak();
  }
  ScaledFont* scaledFont = mAzureScaledFont;
  return do_AddRef(scaledFont);
}

bool gfxFontconfigFont::ShouldHintMetrics() const {
  return !GetStyle()->printerFont;
}

gfxFcPlatformFontList::gfxFcPlatformFontList()
    : mLocalNames(64),
      mGenericMappings(32),
      mFcSubstituteCache(64),
      mLastConfig(nullptr),
      mAlwaysUseFontconfigGenerics(true) {
  CheckFamilyList(kBaseFonts_Ubuntu_22_04);
  CheckFamilyList(kLangFonts_Ubuntu_22_04);
  CheckFamilyList(kBaseFonts_Ubuntu_20_04);
  CheckFamilyList(kLangFonts_Ubuntu_20_04);
  CheckFamilyList(kBaseFonts_Fedora_39);
  CheckFamilyList(kBaseFonts_Fedora_38);
  mLastConfig = FcConfigGetCurrent();
  if (XRE_IsParentProcess()) {
    // if the rescan interval is set, start the timer
    int rescanInterval = FcConfigGetRescanInterval(nullptr);
    if (rescanInterval) {
      NS_NewTimerWithFuncCallback(
          getter_AddRefs(mCheckFontUpdatesTimer), CheckFontUpdates, this,
          (rescanInterval + 1) * 1000, nsITimer::TYPE_REPEATING_SLACK,
          "gfxFcPlatformFontList::gfxFcPlatformFontList"_ns);
      if (!mCheckFontUpdatesTimer) {
        NS_WARNING("Failure to create font updates timer");
      }
    }
  }

#ifdef MOZ_BUNDLED_FONTS
  mBundledFontsInitialized = false;
#endif
}

gfxFcPlatformFontList::~gfxFcPlatformFontList() {
  AutoLock lock(mLock);

  if (mCheckFontUpdatesTimer) {
    mCheckFontUpdatesTimer->Cancel();
    mCheckFontUpdatesTimer = nullptr;
  }
#ifdef MOZ_WIDGET_GTK
  ClearSystemFontOptions();
#endif
}

void gfxFcPlatformFontList::AddFontSetFamilies(FcFontSet* aFontSet,
                                               const SandboxPolicy* aPolicy,
                                               bool aAppFonts) {
  // This iterates over the fonts in a font set and adds in gfxFontFamily
  // objects for each family. Individual gfxFontEntry objects for each face
  // are not created here; the patterns are just stored in the family. When
  // a family is actually used, it will be populated with gfxFontEntry
  // records and the patterns moved to those.

  if (NS_WARN_IF(!aFontSet)) {
    return;
  }

  FcChar8* lastFamilyName = (FcChar8*)"";
  RefPtr<gfxFontconfigFontFamily> fontFamily;
  nsAutoCString familyName;
  for (int f = 0; f < aFontSet->nfont; f++) {
    FcPattern* pattern = aFontSet->fonts[f];

    // Skip any fonts that aren't readable for us (e.g. due to restrictive
    // file ownership/permissions).
    FcChar8* path;
    if (FcPatternGetString(pattern, FC_FILE, 0, &path) != FcResultMatch) {
      continue;
    }
    if (access(reinterpret_cast<const char*>(path), F_OK | R_OK) != 0) {
      continue;
    }

#if defined(MOZ_SANDBOX) && defined(XP_LINUX)
    // Skip any fonts that will be blocked by the content-process sandbox
    // policy.
    if (aPolicy && !(aPolicy->Lookup(reinterpret_cast<const char*>(path)) &
                     SandboxBroker::Perms::MAY_READ)) {
      continue;
    }
#endif

    AddPatternToFontList(pattern, lastFamilyName, familyName, fontFamily,
                         aAppFonts);
  }
}

// Check whether a pattern refers to a non-variable font, or a specific named
// instance of a variable font. Only such patterns are eligible for src:local()
// name lookups, as local() is defined to reference a specific face.
static bool IsNonVariableOrNamedInstance(FcPattern* aPattern) {
  FcBool value;
  if (FcPatternGetBool(aPattern, FC_VARIABLE, 0, &value) == FcResultMatch &&
      value) {
    // It's a variable font resource; check if this is a named instance.
    if (FcPatternGetBool(aPattern, FC_NAMED_INSTANCE, 0, &value) ==
            FcResultMatch &&
        value) {
      // Yes, named instance: ok to use.
      return true;
    }
    // It's a variable font; we don't want it.
    return false;
  }
  // Non-variable: no problem.
  return true;
}

void gfxFcPlatformFontList::AddPatternToFontList(
    FcPattern* aFont, FcChar8*& aLastFamilyName, nsACString& aFamilyName,
    RefPtr<gfxFontconfigFontFamily>& aFontFamily, bool aAppFonts) {
  // get canonical name
  uint32_t cIndex = FindCanonicalNameIndex(aFont, FC_FAMILYLANG);
  FcChar8* canonical = nullptr;
  FcPatternGetString(aFont, FC_FAMILY, cIndex, &canonical);
  if (!canonical) {
    return;
  }

  // same as the last one? no need to add a new family, skip
  if (FcStrCmp(canonical, aLastFamilyName) != 0) {
    aLastFamilyName = canonical;

    // add new family if one doesn't already exist
    aFamilyName.Truncate();
    aFamilyName = ToCharPtr(canonical);
    nsAutoCString keyName(aFamilyName);
    ToLowerCase(keyName);

    aFontFamily = static_cast<gfxFontconfigFontFamily*>(
        mFontFamilies
            .LookupOrInsertWith(keyName,
                                [&] {
                                  FontVisibility visibility =
                                      aAppFonts
                                          ? FontVisibility::Base
                                          : GetVisibilityForFamily(keyName);
                                  return MakeRefPtr<gfxFontconfigFontFamily>(
                                      aFamilyName, visibility);
                                })
            .get());
    // Record if the family contains fonts from the app font set
    // (in which case we won't rely on fontconfig's charmap, due to
    // bug 1276594).
    if (aAppFonts) {
      aFontFamily->SetFamilyContainsAppFonts(true);
    }
  }

  // Add pointers to other localized family names. Most fonts
  // only have a single name, so the first call to GetString
  // will usually not match
  FcChar8* otherName;
  int n = (cIndex == 0 ? 1 : 0);
  AutoTArray<nsCString, 4> otherFamilyNames;
  while (FcPatternGetString(aFont, FC_FAMILY, n, &otherName) == FcResultMatch) {
    otherFamilyNames.AppendElement(nsCString(ToCharPtr(otherName)));
    n++;
    if (n == int(cIndex)) {
      n++;  // skip over canonical name
    }
  }
  if (!otherFamilyNames.IsEmpty()) {
    AddOtherFamilyNames(aFontFamily, otherFamilyNames);
  }

  const bool singleName = n == 1;

  MOZ_ASSERT(aFontFamily, "font must belong to a font family");
  aFontFamily->AddFontPattern(aFont, singleName);

  if (IsNonVariableOrNamedInstance(aFont)) {
    // map the psname, fullname ==> font family for local font lookups
    nsAutoCString psname, fullname;
    GetFaceNames(aFont, aFamilyName, psname, fullname);
    if (!psname.IsEmpty()) {
      ToLowerCase(psname);
      mLocalNames.InsertOrUpdate(psname, RefPtr{aFont});
    }
    if (!fullname.IsEmpty()) {
      ToLowerCase(fullname);
      mLocalNames.WithEntryHandle(fullname, [&](auto&& entry) {
        if (entry && !singleName) {
          return;
        }
        entry.InsertOrUpdate(RefPtr{aFont});
      });
    }
  }
}

nsresult gfxFcPlatformFontList::InitFontListForPlatform() {
#ifdef MOZ_BUNDLED_FONTS
  if (StaticPrefs::gfx_bundled_fonts_activate_AtStartup() != 0) {
    ActivateBundledFonts();
  }
#endif

  mLocalNames.Clear();
  mFcSubstituteCache.Clear();

  ClearSystemFontOptions();

  mAlwaysUseFontconfigGenerics = PrefFontListsUseOnlyGenerics();
  mOtherFamilyNamesInitialized = true;

  mLastConfig = FcConfigGetCurrent();

  if (XRE_IsContentProcess()) {
    // Content process: use the font list passed from the chrome process,
    // because we can't rely on fontconfig in the presence of sandboxing;
    // it may report fonts that we can't actually access.

    FcChar8* lastFamilyName = (FcChar8*)"";
    RefPtr<gfxFontconfigFontFamily> fontFamily;
    nsAutoCString familyName;

    // Get font list that was passed during XPCOM startup
    // or in an UpdateFontList message.
    auto& fontList = dom::ContentChild::GetSingleton()->SystemFontList();

#ifdef MOZ_WIDGET_GTK
    UpdateSystemFontOptionsFromIpc(fontList.options());
#endif

    for (const FontPatternListEntry& fpe : fontList.entries()) {
      FcPattern* pattern = FcNameParse((const FcChar8*)fpe.pattern().get());
      AddPatternToFontList(pattern, lastFamilyName, familyName, fontFamily,
                           fpe.appFontFamily());
      FcPatternDestroy(pattern);
    }

    LOG_FONTLIST(
        ("got font list from chrome process: "
         "%u faces in %u families",
         (unsigned)fontList.entries().Length(), mFontFamilies.Count()));

    fontList.entries().Clear();
    return NS_OK;
  }

  UpdateSystemFontOptions();

  UniquePtr<SandboxPolicy> policy;

#if defined(MOZ_SANDBOX) && defined(XP_LINUX)
  // If read sandboxing is enabled, create a temporary SandboxPolicy to
  // check font paths; use a fake PID to avoid picking up any PID-specific
  // rules by accident.
  SandboxBrokerPolicyFactory policyFactory;
  if (GetEffectiveContentSandboxLevel() > 2 &&
      !PR_GetEnv("MOZ_DISABLE_CONTENT_SANDBOX")) {
    policy = policyFactory.GetContentPolicy(-1, false);
  }
#endif

#ifdef MOZ_BUNDLED_FONTS
  // https://bugzilla.mozilla.org/show_bug.cgi?id=1745715:
  // It's important to do this *before* processing the standard system fonts,
  // so that if a family is present in both font sets, we'll treat it as app-
  // bundled and therefore always visible.
  if (StaticPrefs::gfx_bundled_fonts_activate_AtStartup() != 0) {
    FcFontSet* appFonts = FcConfigGetFonts(nullptr, FcSetApplication);
    AddFontSetFamilies(appFonts, policy.get(), /* aAppFonts = */ true);
  }
#endif

  // iterate over available fonts
  FcFontSet* systemFonts = FcConfigGetFonts(nullptr, FcSetSystem);
  AddFontSetFamilies(systemFonts, policy.get(), /* aAppFonts = */ false);

  return NS_OK;
}

void gfxFcPlatformFontList::ReadSystemFontList(dom::SystemFontList* retValue) {
  AutoLock lock(mLock);

  // Fontconfig versions below 2.9 drop the FC_FILE element in FcNameUnparse
  // (see https://bugs.freedesktop.org/show_bug.cgi?id=26718), so when using
  // an older version, we manually append it to the unparsed pattern.
#ifdef MOZ_WIDGET_GTK
  SystemFontOptionsToIpc(retValue->options());
#endif

  if (FcGetVersion() < 20900) {
    for (const auto& entry : mFontFamilies) {
      auto* family = static_cast<gfxFontconfigFontFamily*>(entry.GetWeak());
      family->AddFacesToFontList([&](FcPattern* aPat, bool aAppFonts) {
        char* s = (char*)FcNameUnparse(aPat);
        nsDependentCString patternStr(s);
        char* file = nullptr;
        if (FcResultMatch ==
            FcPatternGetString(aPat, FC_FILE, 0, (FcChar8**)&file)) {
          patternStr.Append(":file=");
          patternStr.Append(file);
        }
        retValue->entries().AppendElement(
            FontPatternListEntry(patternStr, aAppFonts));
        free(s);
      });
    }
  } else {
    for (const auto& entry : mFontFamilies) {
      auto* family = static_cast<gfxFontconfigFontFamily*>(entry.GetWeak());
      family->AddFacesToFontList([&](FcPattern* aPat, bool aAppFonts) {
        char* s = (char*)FcNameUnparse(aPat);
        nsDependentCString patternStr(s);
        retValue->entries().AppendElement(
            FontPatternListEntry(patternStr, aAppFonts));
        free(s);
      });
    }
  }
}

using Device = nsIGfxInfo::FontVisibilityDeviceDetermination;
static Device sFontVisibilityDevice = Device::Unassigned;

void AssignFontVisibilityDevice() {
  if (sFontVisibilityDevice == Device::Unassigned) {
    nsCOMPtr<nsIGfxInfo> gfxInfo = components::GfxInfo::Service();
    NS_ENSURE_SUCCESS_VOID(
        gfxInfo->GetFontVisibilityDetermination(&sFontVisibilityDevice));
  }
}

// Per family array of faces.
class FacesData {
  using FaceInitArray = AutoTArray<fontlist::Face::InitData, 8>;

  FaceInitArray mFaces;

  // Number of faces that have a single name. Faces that have multiple names are
  // sorted last.
  uint32_t mUniqueNameFaceCount = 0;

 public:
  void Add(fontlist::Face::InitData&& aData, bool aSingleName) {
    if (aSingleName) {
      mFaces.InsertElementAt(mUniqueNameFaceCount++, std::move(aData));
    } else {
      mFaces.AppendElement(std::move(aData));
    }
  }

  const FaceInitArray& Get() const { return mFaces; }
};

void gfxFcPlatformFontList::InitSharedFontListForPlatform() {
  mLocalNames.Clear();
  mFcSubstituteCache.Clear();

  mAlwaysUseFontconfigGenerics = PrefFontListsUseOnlyGenerics();
  mOtherFamilyNamesInitialized = true;

  mLastConfig = FcConfigGetCurrent();

  if (!XRE_IsParentProcess()) {
#ifdef MOZ_WIDGET_GTK
    auto& fontList = dom::ContentChild::GetSingleton()->SystemFontList();
    UpdateSystemFontOptionsFromIpc(fontList.options());
#endif
    // Content processes will access the shared-memory data created by the
    // parent, so they do not need to query fontconfig for the available
    // fonts themselves.
    return;
  }

#ifdef MOZ_WIDGET_GTK
  UpdateSystemFontOptions();
#endif

#ifdef MOZ_BUNDLED_FONTS
  if (StaticPrefs::gfx_bundled_fonts_activate_AtStartup() != 0) {
    auto timerId = glean::fontlist::bundledfonts_activate.Start();
    ActivateBundledFonts();
    glean::fontlist::bundledfonts_activate.StopAndAccumulate(
        std::move(timerId));
  }
#endif

  UniquePtr<SandboxPolicy> policy;

#if defined(MOZ_CONTENT_SANDBOX) && defined(XP_LINUX)
  // If read sandboxing is enabled, create a temporary SandboxPolicy to
  // check font paths; use a fake PID to avoid picking up any PID-specific
  // rules by accident.
  SandboxBrokerPolicyFactory policyFactory;
  if (GetEffectiveContentSandboxLevel() > 2 &&
      !PR_GetEnv("MOZ_DISABLE_CONTENT_SANDBOX")) {
    policy = policyFactory.GetContentPolicy(-1, false);
  }
#endif

  nsTArray<fontlist::Family::InitData> families;

  nsClassHashtable<nsCStringHashKey, FacesData> faces;

  // Returns true if the font was added with FontVisibility::Base.
  // This enables us to count how many known Base fonts are present.
  auto addPattern = [this, &families, &faces](
                        FcPattern* aPattern, FcChar8*& aLastFamilyName,
                        nsCString& aFamilyName, bool aAppFont) -> bool {
    // get canonical name
    uint32_t cIndex = FindCanonicalNameIndex(aPattern, FC_FAMILYLANG);
    FcChar8* canonical = nullptr;
    FcPatternGetString(aPattern, FC_FAMILY, cIndex, &canonical);
    if (!canonical) {
      return false;
    }

    nsAutoCString keyName;
    keyName = ToCharPtr(canonical);
    ToLowerCase(keyName);

    aLastFamilyName = canonical;
    aFamilyName = ToCharPtr(canonical);

    const FontVisibility visibility =
        aAppFont ? FontVisibility::Base : GetVisibilityForFamily(keyName);

    // Same canonical family name as the last one? Definitely no need to add a
    // new family record.
    auto* faceList =
        faces
            .LookupOrInsertWith(
                keyName,
                [&] {
                  families.AppendElement(fontlist::Family::InitData(
                      keyName, aFamilyName, fontlist::Family::kNoIndex,
                      visibility,
                      /*bundled*/ aAppFont, /*badUnderline*/ false));
                  return MakeUnique<FacesData>();
                })
            .get();

    // Intentionally using nsCString + Assign(), rather than nsAutoCString,
    // since we copy the buffer around into an nsCString multiple times.
    nsCString descriptor;
    {
      RefPtr<FcPattern> dupToUnparse =
          MaybeEscapeFamilyForBrokenUnparse(aPattern);
      char* s =
          (char*)FcNameUnparse(dupToUnparse ? dupToUnparse.get() : aPattern);
      descriptor.Assign(s);
      free(s);
    }

    WeightRange weight(FontWeight::NORMAL);
    WidthRange width(FontWidth::NORMAL);
    SlantStyleRange style(FontSlantStyle::NORMAL);
    uint16_t size;
    GetFontProperties(aPattern, &weight, &width, &style, &size);

    auto initData = fontlist::Face::InitData{descriptor, 0,     size, false,
                                             weight,     width, style};

    // Add entries for any other localized family names. (Most fonts only have
    // a single family name, so the first call to GetString will usually fail).
    // These get the same visibility level as we looked up for the first name.
    FcChar8* otherName;
    int n = (cIndex == 0 ? 1 : 0);
    while (FcPatternGetString(aPattern, FC_FAMILY, n, &otherName) ==
           FcResultMatch) {
      nsAutoCString otherFamilyName(ToCharPtr(otherName));
      keyName = otherFamilyName;
      ToLowerCase(keyName);

      faces
          .LookupOrInsertWith(
              keyName,
              [&] {
                families.AppendElement(fontlist::Family::InitData(
                    keyName, otherFamilyName, fontlist::Family::kNoIndex,
                    visibility,
                    /*bundled*/ aAppFont, /*badUnderline*/ false));

                return MakeUnique<FacesData>();
              })
          .get()
          ->Add(fontlist::Face::InitData(initData), /* aSingleName = */ false);

      n++;
      if (n == int(cIndex)) {
        n++;  // skip over canonical name
      }
    }

    const bool singleName = n == 1;
    faceList->Add(std::move(initData), singleName);

    if (IsNonVariableOrNamedInstance(aPattern)) {
      // map the psname, fullname ==> font family for local font lookups
      nsAutoCString psname, fullname;
      GetFaceNames(aPattern, aFamilyName, psname, fullname);
      MOZ_PUSH_IGNORE_THREAD_SAFETY
      if (!psname.IsEmpty()) {
        ToLowerCase(psname);
        MaybeAddToLocalNameTable(
            psname, fontlist::LocalFaceRec::InitData(keyName, descriptor));
      }
      if (!fullname.IsEmpty()) {
        ToLowerCase(fullname);
        if (fullname != psname) {
          // We only consider overriding an existing entry if this is the only
          // way to name this family. This prevents dubious aliases from
          // clobbering the local name table.
          if (singleName || !mLocalNameTable.Contains(fullname)) {
            MaybeAddToLocalNameTable(fullname, fontlist::LocalFaceRec::InitData(
                                                   keyName, descriptor));
          }
        }
      }
      MOZ_POP_THREAD_SAFETY
    }

    return visibility == FontVisibility::Base;
  };

  // Returns the number of families with FontVisibility::Base that were found.
  auto addFontSetFamilies = [&addPattern](FcFontSet* aFontSet,
                                          SandboxPolicy* aPolicy,
                                          bool aAppFonts) -> size_t {
    size_t count = 0;
    if (NS_WARN_IF(!aFontSet)) {
      return count;
    }
    FcChar8* lastFamilyName = (FcChar8*)"";
    RefPtr<gfxFontconfigFontFamily> fontFamily;
    nsAutoCString familyName;
    for (int f = 0; f < aFontSet->nfont; f++) {
      FcPattern* pattern = aFontSet->fonts[f];

      // Skip any fonts that aren't readable for us (e.g. due to restrictive
      // file ownership/permissions).
      FcChar8* path;
      if (FcPatternGetString(pattern, FC_FILE, 0, &path) != FcResultMatch) {
        continue;
      }
      if (access(reinterpret_cast<const char*>(path), F_OK | R_OK) != 0) {
        continue;
      }

#if defined(MOZ_CONTENT_SANDBOX) && defined(XP_LINUX)
      // Skip any fonts that will be blocked by the content-process sandbox
      // policy.
      if (aPolicy && !(aPolicy->Lookup(reinterpret_cast<const char*>(path)) &
                       SandboxBroker::Perms::MAY_READ)) {
        continue;
      }
#endif

      // Clone the pattern, because we can't operate on the one belonging to
      // the FcFontSet directly.
      FcPattern* clone = FcPatternDuplicate(pattern);

      // Pick up any configuration options applicable to the font (e.g. custom
      // fontfeatures settings).
      if (!FcConfigSubstitute(nullptr, clone, FcMatchFont)) {
        // Out of memory?! We're probably doomed, but just skip this font.
        FcPatternDestroy(clone);
        continue;
      }
      // But ignore hinting settings from FcConfigSubstitute, as we don't want
      // to bake them into the pattern in the font list.
      FcPatternDel(clone, FC_HINT_STYLE);
      FcPatternDel(clone, FC_HINTING);

      // If this is a TrueType or OpenType font, discard the FC_CHARSET object
      // (which may be very large), because we'll read the 'cmap' directly.
      // This substantially reduces the pressure on shared memory (bug 1664151)
      // due to the large font descriptors (serialized patterns).
      FcChar8* fontFormat;
      MOZ_PUSH_IGNORE_THREAD_SAFETY
      if (FcPatternGetString(clone, FC_FONTFORMAT, 0, &fontFormat) ==
              FcResultMatch &&
          (!FcStrCmp(fontFormat, (const FcChar8*)"TrueType") ||
           !FcStrCmp(fontFormat, (const FcChar8*)"CFF"))) {
        FcPatternDel(clone, FC_CHARSET);
        if (addPattern(clone, lastFamilyName, familyName, aAppFonts)) {
          ++count;
        }
      } else {
        if (addPattern(clone, lastFamilyName, familyName, aAppFonts)) {
          ++count;
        }
      }
      MOZ_POP_THREAD_SAFETY

      FcPatternDestroy(clone);
    }
    return count;
  };

#ifdef MOZ_BUNDLED_FONTS
  // Add bundled fonts before system fonts, to set correct visibility status
  // for any families that appear in both.
  if (StaticPrefs::gfx_bundled_fonts_activate_AtStartup() != 0) {
    FcFontSet* appFonts = FcConfigGetFonts(nullptr, FcSetApplication);
    addFontSetFamilies(appFonts, policy.get(), /* aAppFonts = */ true);
  }
#endif

  // iterate over available fonts
  FcFontSet* systemFonts = FcConfigGetFonts(nullptr, FcSetSystem);
  auto numBaseFamilies = addFontSetFamilies(systemFonts, policy.get(),
                                            /* aAppFonts = */ false);
  AssignFontVisibilityDevice();
  if (numBaseFamilies < 3 && sFontVisibilityDevice != Device::Linux_Unknown) {
    // If we found fewer than 3 known FontVisibility::Base families in the
    // system (ignoring app-bundled fonts), we must be dealing with a very
    // non-standard configuration; disable the distro-specific font
    // fingerprinting protection by marking all fonts as Unknown.
    for (auto& f : families) {
      f.mVisibility = FontVisibility::Unknown;
    }
    // Issue a warning that we're disabling this protection.
    nsCOMPtr<nsIConsoleService> console(
        do_GetService("@mozilla.org/consoleservice;1"));
    if (console) {
      console->LogStringMessage(
          u"Font-fingerprinting protection disabled; not enough standard "
          u"distro fonts installed.");
    }
  }

  mozilla::fontlist::FontList* list = SharedFontList();
  list->SetFamilyNames(families);

  for (uint32_t i = 0; i < families.Length(); i++) {
    list->Families()[i].AddFaces(list, faces.Get(families[i].mKey)->Get());
  }
}

FontVisibility gfxFcPlatformFontList::GetVisibilityForFamily(
    const nsACString& aName) const {
  AssignFontVisibilityDevice();

  switch (sFontVisibilityDevice) {
    case Device::Linux_Ubuntu_any:
    case Device::Linux_Ubuntu_22:
      if (FamilyInList(aName, kBaseFonts_Ubuntu_22_04)) {
        return FontVisibility::Base;
      }
      if (FamilyInList(aName, kLangFonts_Ubuntu_22_04)) {
        return FontVisibility::LangPack;
      }
      if (sFontVisibilityDevice == Device::Linux_Ubuntu_22) {
        return FontVisibility::User;
      }
      // For Ubuntu_any, we fall through to also check the 20_04 lists.
      [[fallthrough]];

    case Device::Linux_Ubuntu_20:
      if (FamilyInList(aName, kBaseFonts_Ubuntu_20_04)) {
        return FontVisibility::Base;
      }
      if (FamilyInList(aName, kLangFonts_Ubuntu_20_04)) {
        return FontVisibility::LangPack;
      }
      return FontVisibility::User;

    case Device::Linux_Fedora_any:
      // We have no font list for this Fedora version
      return FontVisibility::Unknown;

    case Device::Linux_Fedora_39:
      if (FamilyInList(aName, kBaseFonts_Fedora_39)) {
        return FontVisibility::Base;
      }
      return FontVisibility::User;

    case Device::Linux_Fedora_38:
      if (FamilyInList(aName, kBaseFonts_Fedora_38)) {
        return FontVisibility::Base;
      }
      return FontVisibility::User;

    default:
      // We don't know how to categorize fonts on this system
      return FontVisibility::Unknown;
  }
}

nsTArray<std::pair<const char**, uint32_t>>
gfxFcPlatformFontList::GetFilteredPlatformFontLists() {
  AssignFontVisibilityDevice();

  nsTArray<std::pair<const char**, uint32_t>> fontLists;

  switch (sFontVisibilityDevice) {
    case Device::Linux_Ubuntu_any:
    case Device::Linux_Ubuntu_22:
      fontLists.AppendElement(std::make_pair(
          kBaseFonts_Ubuntu_22_04, std::size(kBaseFonts_Ubuntu_22_04)));
      fontLists.AppendElement(std::make_pair(
          kLangFonts_Ubuntu_22_04, std::size(kLangFonts_Ubuntu_22_04)));
      // For Ubuntu_any, we fall through to also check the 20_04 lists.
      [[fallthrough]];

    case Device::Linux_Ubuntu_20:
      fontLists.AppendElement(std::make_pair(
          kBaseFonts_Ubuntu_20_04, std::size(kBaseFonts_Ubuntu_20_04)));
      fontLists.AppendElement(std::make_pair(
          kLangFonts_Ubuntu_20_04, std::size(kLangFonts_Ubuntu_20_04)));
      break;

    case Device::Linux_Fedora_any:
      // No font list for this Fedora version; see GetVisibilityForFamily().
      break;

    case Device::Linux_Fedora_39:
      fontLists.AppendElement(std::make_pair(kBaseFonts_Fedora_39,
                                             std::size(kBaseFonts_Fedora_39)));
      break;

    case Device::Linux_Fedora_38:
      fontLists.AppendElement(std::make_pair(kBaseFonts_Fedora_38,
                                             std::size(kBaseFonts_Fedora_38)));
      break;

    default:
      // We don't know how to categorize fonts on this system
      break;
  }

  return fontLists;
}

already_AddRefed<gfxFontEntry> gfxFcPlatformFontList::CreateFontEntry(
    fontlist::Face* aFace, const fontlist::Family* aFamily) {
  nsAutoCString desc(aFace->mDescriptor.AsString(SharedFontList()));
  FcPattern* pattern = FcNameParse((const FcChar8*)desc.get());
  RefPtr fe = MakeRefPtr<gfxFontconfigFontEntry>(desc, pattern, true);
  FcPatternDestroy(pattern);
  fe->InitializeFrom(aFace, aFamily);
  return fe.forget();
}

// For displaying the fontlist in UI, use explicit call to FcFontList. Using
// FcFontList results in the list containing the localized names as dictated
// by system defaults.
static void GetSystemFontList(nsTArray<nsString>& aListOfFonts,
                              nsAtom* aLangGroup) {
  aListOfFonts.Clear();

  RefPtr<FcPattern> pat = dont_AddRef(FcPatternCreate());
  if (!pat) {
    return;
  }

  UniquePtr<FcObjectSet> os(FcObjectSetBuild(FC_FAMILY, nullptr));
  if (!os) {
    return;
  }

  // add the lang to the pattern
  nsAutoCString fcLang;
  gfxFcPlatformFontList* pfl = gfxFcPlatformFontList::PlatformFontList();
  pfl->GetSampleLangForGroup(aLangGroup, fcLang);
  if (!fcLang.IsEmpty()) {
    FcPatternAddString(pat, FC_LANG, ToFcChar8Ptr(fcLang.get()));
  }

  UniquePtr<FcFontSet> fs(FcFontList(nullptr, pat, os.get()));
  if (!fs) {
    return;
  }

  for (int i = 0; i < fs->nfont; i++) {
    char* family;

    if (FcPatternGetString(fs->fonts[i], FC_FAMILY, 0, (FcChar8**)&family) !=
        FcResultMatch) {
      continue;
    }

    // Remove duplicates...
    nsAutoString strFamily;
    AppendUTF8toUTF16(MakeStringSpan(family), strFamily);
    if (aListOfFonts.Contains(strFamily)) {
      continue;
    }

    aListOfFonts.AppendElement(strFamily);
  }

  aListOfFonts.Sort();
}

void gfxFcPlatformFontList::GetFontList(nsAtom* aLangGroup,
                                        const nsACString& aGenericFamily,
                                        nsTArray<nsString>& aListOfFonts) {
  // Get the list of font family names using fontconfig
  GetSystemFontList(aListOfFonts, aLangGroup);

  // Under Linux, the generics "serif", "sans-serif", "monospace" and "math"
  // are included in the pref fontlist. These map to whatever fontconfig
  // decides they should be for a given language, rather than one of the
  // fonts listed in the prefs font lists (e.g. font.name.*, font.name-list.*)
  bool serif = false, sansSerif = false, monospace = false, math = false;
  if (aGenericFamily.IsEmpty()) {
    serif = sansSerif = monospace = math = true;
  } else if (aGenericFamily.LowerCaseEqualsLiteral("serif")) {
    serif = true;
  } else if (aGenericFamily.LowerCaseEqualsLiteral("sans-serif")) {
    sansSerif = true;
  } else if (aGenericFamily.LowerCaseEqualsLiteral("monospace")) {
    monospace = true;
  } else if (StaticPrefs::mathml_font_family_math_enabled() &&
             aGenericFamily.LowerCaseEqualsLiteral("math")) {
    math = true;
  } else if (aGenericFamily.LowerCaseEqualsLiteral("cursive") ||
             aGenericFamily.LowerCaseEqualsLiteral("fantasy")) {
    serif = sansSerif = true;
  } else {
    MOZ_ASSERT_UNREACHABLE("unexpected CSS generic font family");
  }

  // The first in the list becomes the default in
  // FontBuilder.readFontSelection() if the preference-selected font is not
  // available, so put system configured defaults first.
  if (math) aListOfFonts.InsertElementAt(0, u"math"_ns);
  if (monospace) aListOfFonts.InsertElementAt(0, u"monospace"_ns);
  if (sansSerif) aListOfFonts.InsertElementAt(0, u"sans-serif"_ns);
  if (serif) aListOfFonts.InsertElementAt(0, u"serif"_ns);
}

FontFamily gfxFcPlatformFontList::GetDefaultFontForPlatform(
    FontVisibilityProvider* aFontVisibilityProvider, const gfxFontStyle* aStyle,
    nsAtom* aLanguage) {
  // Get the default font by using a fake name to retrieve the first
  // scalable font that fontconfig suggests for the given language.
  PrefFontList* prefFonts =
      FindGenericFamilies(aFontVisibilityProvider, "-moz-default"_ns,
                          aLanguage ? aLanguage : nsGkAtoms::x_western);
  NS_ASSERTION(prefFonts, "null list of generic fonts");
  if (prefFonts && !prefFonts->IsEmpty()) {
    return (*prefFonts)[0];
  }
  return FontFamily();
}

already_AddRefed<gfxFontEntry> gfxFcPlatformFontList::LookupLocalFont(
    FontVisibilityProvider* aFontVisibilityProvider,
    const nsACString& aFontName, WeightRange aWeightForEntry,
    WidthRange aWidthForEntry, SlantStyleRange aStyleForEntry) {
  AutoLock lock(mLock);

  nsAutoCString keyName(aFontName);
  ToLowerCase(keyName);

  if (SharedFontList()) {
    return LookupInSharedFaceNameList(aFontVisibilityProvider, aFontName,
                                      aWeightForEntry, aWidthForEntry,
                                      aStyleForEntry);
  }

  // if name is not in the global list, done
  const auto fontPattern = mLocalNames.Lookup(keyName);
  if (!fontPattern) {
    return nullptr;
  }

  return MakeAndAddRef<gfxFontconfigFontEntry>(
      aFontName, *fontPattern, aWeightForEntry, aWidthForEntry, aStyleForEntry);
}

already_AddRefed<gfxFontEntry> gfxFcPlatformFontList::MakePlatformFont(
    const nsACString& aFontName, WeightRange aWeightForEntry,
    WidthRange aWidthForEntry, SlantStyleRange aStyleForEntry,
    FontData* aFontData) {
  RefPtr<FTUserFontData> ufd = new FTUserFontData(aFontData);
  RefPtr<SharedFTFace> face = ufd->CloneFace();
  if (!face) {
    return nullptr;
  }
  return MakeAndAddRef<gfxFontconfigFontEntry>(aFontName, aWeightForEntry,
                                               aWidthForEntry, aStyleForEntry,
                                               std::move(face));
}

static bool UseCustomFontconfigLookupsForLocale(const Locale& aLocale) {
  return aLocale.Script().EqualTo("Hans") || aLocale.Script().EqualTo("Hant") ||
         aLocale.Script().EqualTo("Jpan") || aLocale.Script().EqualTo("Kore") ||
         aLocale.Script().EqualTo("Arab");
}

bool gfxFcPlatformFontList::FindAndAddFamiliesLocked(
    FontVisibilityProvider* aFontVisibilityProvider,
    StyleGenericFontFamily aGeneric, const nsACString& aFamily,
    nsTArray<FamilyAndGeneric>* aOutput, FindFamiliesFlags aFlags,
    gfxFontStyle* aStyle, nsAtom* aLanguage, gfxFloat aDevToCssSize) {
  nsAutoCString familyName(aFamily);
  ToLowerCase(familyName);

  if (!(aFlags & FindFamiliesFlags::eQuotedFamilyName)) {
    // deprecated generic names are explicitly converted to standard generics
    bool isDeprecatedGeneric = false;
    if (familyName.EqualsLiteral("sans") ||
        familyName.EqualsLiteral("sans serif")) {
      familyName.AssignLiteral("sans-serif");
      isDeprecatedGeneric = true;
    } else if (familyName.EqualsLiteral("mono")) {
      familyName.AssignLiteral("monospace");
      isDeprecatedGeneric = true;
    }

    // fontconfig generics? use fontconfig to determine the family for lang
    if (isDeprecatedGeneric ||
        mozilla::StyleSingleFontFamily::Parse(familyName).IsGeneric()) {
      PrefFontList* prefFonts =
          FindGenericFamilies(aFontVisibilityProvider, familyName, aLanguage);
      if (prefFonts && !prefFonts->IsEmpty()) {
        aOutput->AppendElements(*prefFonts);
        return true;
      }
      return false;
    }
  }

  // fontconfig allows conditional substitutions in such a way that it's
  // difficult to distinguish an explicit substitution from other suggested
  // choices. To sniff out explicit substitutions, compare the substitutions
  // for "font, -moz-sentinel" to "-moz-sentinel" to sniff out the
  // substitutions
  //
  // Example:
  //
  //   serif ==> DejaVu Serif, ...
  //   Helvetica, serif ==> Helvetica, TeX Gyre Heros, Nimbus Sans L, DejaVu
  //   Serif
  //
  // In this case fontconfig is including Tex Gyre Heros and
  // Nimbus Sans L as alternatives for Helvetica.

  // Because the FcConfigSubstitute call is quite expensive, we cache the
  // actual font families found via this process.
  nsAutoCString cacheKey;

  // For languages that use CJK or Arabic script, we include the language as
  // part of the cache key because fontconfig may have lang-specific rules that
  // specify different substitutions. (In theory, this could apply to *any*
  // language, but it's highly unlikely to matter for non-CJK/Arabic scripts,
  // and it gets really expensive to do separate lookups for 300+ distinct lang
  // tags e.g. on wikipedia.org, when they all end up mapping to the same font
  // list.)
  // We remember the most recently checked aLanguage atom so that when the same
  // language is used in many successive calls, we can avoid repeating the
  // locale code processing every time.
  if (aLanguage != mPrevLanguage) {
    GetSampleLangForGroup(aLanguage, mSampleLang);
    ToLowerCase(mSampleLang);
    Locale locale;
    mUseCustomLookups = LocaleParser::TryParse(mSampleLang, locale).isOk() &&
                        locale.AddLikelySubtags().isOk() &&
                        UseCustomFontconfigLookupsForLocale(locale);
    mPrevLanguage = aLanguage;
  }
  if (mUseCustomLookups) {
    cacheKey = mSampleLang;
    cacheKey.Append(':');
  }

  // Include the generic family in the cache key, to maintain the distinction
  // between fonts explicitly requested by name and the results of resolving
  // CSS generics.
  cacheKey.AppendInt(int(aGeneric));
  cacheKey.Append(':');

  cacheKey.Append(familyName);
  auto vis = aFontVisibilityProvider
                 ? aFontVisibilityProvider->GetFontVisibility()
                 : FontVisibility::User;
  cacheKey.Append(':');
  cacheKey.AppendInt(int(vis));
  if (const auto& cached = mFcSubstituteCache.Lookup(cacheKey)) {
    if (cached->IsEmpty()) {
      return false;
    }
    aOutput->AppendElements(*cached);
    return true;
  }

  // It wasn't in the cache, so we need to ask fontconfig...
  const FcChar8* kSentinelName = ToFcChar8Ptr("-moz-sentinel");
  const FcChar8* terminator = nullptr;
  RefPtr<FcPattern> sentinelSubst = dont_AddRef(FcPatternCreate());
  FcPatternAddString(sentinelSubst, FC_FAMILY, kSentinelName);
  if (!mSampleLang.IsEmpty()) {
    FcPatternAddString(sentinelSubst, FC_LANG, ToFcChar8Ptr(mSampleLang.get()));
  }
  FcConfigSubstitute(nullptr, sentinelSubst, FcMatchPattern);

  // If the sentinel name is still present, we'll use that as the terminator
  // for the family names we collect; this means that if fontconfig prepends
  // additional family names (e.g. an emoji font, or lang-specific preferred
  // font) to all patterns, it won't simply mask all actual requested names.
  // If the sentinel has been deleted/replaced altogether, then we'll take
  // the first substitute name as the new terminator.
  FcChar8* substName;
  for (int i = 0; FcPatternGetString(sentinelSubst, FC_FAMILY, i, &substName) ==
                  FcResultMatch;
       i++) {
    if (FcStrCmp(substName, kSentinelName) == 0) {
      terminator = kSentinelName;
      break;
    }
    if (!terminator) {
      terminator = substName;
    }
  }

  // substitutions for font, -moz-sentinel pattern
  RefPtr<FcPattern> fontWithSentinel = dont_AddRef(FcPatternCreate());
  FcPatternAddString(fontWithSentinel, FC_FAMILY,
                     ToFcChar8Ptr(familyName.get()));
  FcPatternAddString(fontWithSentinel, FC_FAMILY, kSentinelName);
  if (!mSampleLang.IsEmpty()) {
    FcPatternAddString(sentinelSubst, FC_LANG, ToFcChar8Ptr(mSampleLang.get()));
  }
  FcConfigSubstitute(nullptr, fontWithSentinel, FcMatchPattern);

  // Add all font family matches until reaching the terminator.
  AutoTArray<FamilyAndGeneric, 10> cachedFamilies;
  for (int i = 0; FcPatternGetString(fontWithSentinel, FC_FAMILY, i,
                                     &substName) == FcResultMatch;
       i++) {
    if (terminator && FcStrCmp(substName, terminator) == 0) {
      break;
    }
    gfxPlatformFontList::FindAndAddFamiliesLocked(
        aFontVisibilityProvider, aGeneric,
        nsDependentCString(ToCharPtr(substName)), &cachedFamilies, aFlags,
        aStyle, aLanguage);
  }

  const auto& insertedCachedFamilies =
      mFcSubstituteCache.InsertOrUpdate(cacheKey, std::move(cachedFamilies));

  if (insertedCachedFamilies.IsEmpty()) {
    return false;
  }
  aOutput->AppendElements(insertedCachedFamilies);
  return true;
}

bool gfxFcPlatformFontList::GetStandardFamilyName(const nsCString& aFontName,
                                                  nsACString& aFamilyName) {
  aFamilyName.Truncate();

  // The fontconfig list of fonts includes generic family names in the
  // font list. For these, just use the generic name.
  if (aFontName.EqualsLiteral("serif") ||
      aFontName.EqualsLiteral("sans-serif") ||
      aFontName.EqualsLiteral("monospace")) {
    aFamilyName.Assign(aFontName);
    return true;
  }

  RefPtr<FcPattern> pat = dont_AddRef(FcPatternCreate());
  if (!pat) {
    return true;
  }

  UniquePtr<FcObjectSet> os(FcObjectSetBuild(FC_FAMILY, nullptr));
  if (!os) {
    return true;
  }

  // add the family name to the pattern
  FcPatternAddString(pat, FC_FAMILY, ToFcChar8Ptr(aFontName.get()));

  UniquePtr<FcFontSet> givenFS(FcFontList(nullptr, pat, os.get()));
  if (!givenFS) {
    return true;
  }

  // See if there is a font face with first family equal to the given family
  // (needs to be in sync with names coming from GetFontList())
  nsTArray<nsCString> candidates;
  for (int i = 0; i < givenFS->nfont; i++) {
    char* firstFamily;

    if (FcPatternGetString(givenFS->fonts[i], FC_FAMILY, 0,
                           (FcChar8**)&firstFamily) != FcResultMatch) {
      continue;
    }

    nsDependentCString first(firstFamily);
    if (!candidates.Contains(first)) {
      candidates.AppendElement(first);

      if (aFontName.Equals(first)) {
        aFamilyName.Assign(aFontName);
        return true;
      }
    }
  }

  // Because fontconfig conflates different family name types, need to
  // double check that the candidate name is not simply a different
  // name type. For example, if a font with nameID=16 "Minion Pro" and
  // nameID=21 "Minion Pro Caption" exists, calling FcFontList with
  // family="Minion Pro" will return a set of patterns some of which
  // will have a first family of "Minion Pro Caption". Ignore these
  // patterns and use the first candidate that maps to a font set with
  // the same number of faces and an identical set of patterns.
  for (uint32_t j = 0; j < candidates.Length(); ++j) {
    FcPatternDel(pat, FC_FAMILY);
    FcPatternAddString(pat, FC_FAMILY, (FcChar8*)candidates[j].get());

    UniquePtr<FcFontSet> candidateFS(FcFontList(nullptr, pat, os.get()));
    if (!candidateFS) {
      return true;
    }

    if (candidateFS->nfont != givenFS->nfont) {
      continue;
    }

    bool equal = true;
    for (int i = 0; i < givenFS->nfont; ++i) {
      if (!FcPatternEqual(candidateFS->fonts[i], givenFS->fonts[i])) {
        equal = false;
        break;
      }
    }
    if (equal) {
      aFamilyName = candidates[j];
      return true;
    }
  }

  // didn't find localized name, leave family name blank
  return true;
}

void gfxFcPlatformFontList::AddGenericFonts(
    FontVisibilityProvider* aFontVisibilityProvider,
    StyleGenericFontFamily aGenericType, nsAtom* aLanguage,
    nsTArray<FamilyAndGeneric>& aFamilyList) {
  // TODO(eri): For now the math generic language uses the legacy
  // "serif.x-math". See `gfxPlatformFontList::AddGenericFonts`.
  if (StaticPrefs::mathml_font_family_math_enabled() &&
      aGenericType == StyleGenericFontFamily::Math) {
    aGenericType = StyleGenericFontFamily::Serif;
    aLanguage = nsGkAtoms::x_math;
  }

  const char* generic = GetGenericName(aGenericType);
  NS_ASSERTION(generic, "weird generic font type");
  if (!generic) {
    return;
  }

  // By default, most font prefs on Linux map to "use fontconfig"
  // keywords. So only need to explicitly lookup font pref if
  // non-default settings exist, or if we are the system-ui font, which we deal
  // with in the base class.
  const bool isSystemUi = aGenericType == StyleGenericFontFamily::SystemUi;
  bool usePrefFontList = isSystemUi;

  nsAutoCString genericToLookup(generic);
  if ((!mAlwaysUseFontconfigGenerics && aLanguage) ||
      aLanguage == nsGkAtoms::x_math) {
    nsAtom* langGroup = GetLangGroup(aLanguage);
    nsAutoCString fontlistValue;
    mFontPrefs->LookupName(PrefName(generic, langGroup), fontlistValue);
    if (fontlistValue.IsEmpty()) {
      // The font name list may have two or more family names as comma
      // separated list.  In such case, not matching with generic font
      // name is fine because if the list prefers specific font, we
      // should try to use the pref with complicated path.
      mFontPrefs->LookupNameList(PrefName(generic, langGroup), fontlistValue);
    }
    if (!fontlistValue.IsEmpty()) {
      if (!fontlistValue.EqualsLiteral("serif") &&
          !fontlistValue.EqualsLiteral("sans-serif") &&
          !fontlistValue.EqualsLiteral("monospace") &&
          !(StaticPrefs::mathml_font_family_math_enabled() &&
            fontlistValue.EqualsLiteral("math"))) {
        usePrefFontList = true;
      } else {
        // serif, sans-serif, monospace or math was specified
        genericToLookup = std::move(fontlistValue);
      }
    }
  }

  // when pref fonts exist, use standard pref font lookup
  if (usePrefFontList) {
    gfxPlatformFontList::AddGenericFonts(aFontVisibilityProvider, aGenericType,
                                         aLanguage, aFamilyList);
    if (!isSystemUi) {
      return;
    }
  }

  AutoLock lock(mLock);
  PrefFontList* prefFonts =
      FindGenericFamilies(aFontVisibilityProvider, genericToLookup, aLanguage);
  NS_ASSERTION(prefFonts, "null generic font list");
  aFamilyList.SetCapacity(aFamilyList.Length() + prefFonts->Length());
  for (auto& f : *prefFonts) {
    aFamilyList.AppendElement(FamilyAndGeneric(f, aGenericType));
  }
}

void gfxFcPlatformFontList::ClearLangGroupPrefFontsLocked() {
  ClearGenericMappingsLocked();
  gfxPlatformFontList::ClearLangGroupPrefFontsLocked();
  mAlwaysUseFontconfigGenerics = PrefFontListsUseOnlyGenerics();
}

gfxPlatformFontList::PrefFontList* gfxFcPlatformFontList::FindGenericFamilies(
    FontVisibilityProvider* aFontVisibilityProvider, const nsCString& aGeneric,
    nsAtom* aLanguage) {
  // set up name
  nsAutoCString fcLang;
  GetSampleLangForGroup(aLanguage, fcLang);
  ToLowerCase(fcLang);

  nsAutoCString cacheKey(aGeneric);
  if (fcLang.Length() > 0) {
    cacheKey.Append('-');
    // If the script is CJK or Arabic, we cache by lang so that different fonts
    // various locales can be supported; but otherwise, we cache by script
    // subtag, to avoid a proliferation of entries for Western & similar
    // languages.
    // In theory, this means we could fail to respect custom fontconfig rules
    // for individual (non-CJK/Arab) languages that share the same script, but
    // such setups are probably vanishingly rare.
    Locale locale;
    if (LocaleParser::TryParse(fcLang, locale).isOk() &&
        locale.AddLikelySubtags().isOk()) {
      if (UseCustomFontconfigLookupsForLocale(locale)) {
        cacheKey.Append(fcLang);
      } else {
        cacheKey.Append(locale.Script().Span());
      }
    } else {
      cacheKey.Append(fcLang);
    }
  }

  // try to get the family from the cache
  return mGenericMappings.WithEntryHandle(
      cacheKey, [&](auto&& entry) -> PrefFontList* {
        if (!entry) {
          // if not found, ask fontconfig to pick the appropriate font
          RefPtr<FcPattern> genericPattern = dont_AddRef(FcPatternCreate());
          FcPatternAddString(genericPattern, FC_FAMILY,
                             ToFcChar8Ptr(aGeneric.get()));

          // -- prefer scalable fonts
          FcPatternAddBool(genericPattern, FC_SCALABLE, FcTrue);

          // -- add the lang to the pattern
          if (!fcLang.IsEmpty()) {
            FcPatternAddString(genericPattern, FC_LANG,
                               ToFcChar8Ptr(fcLang.get()));
          }

          // -- perform substitutions
          FcConfigSubstitute(nullptr, genericPattern, FcMatchPattern);
          FcDefaultSubstitute(genericPattern);

          // -- sort to get the closest matches
          FcResult result;
          UniquePtr<FcFontSet> faces(
              FcFontSort(nullptr, genericPattern, FcFalse, nullptr, &result));

          if (!faces) {
            return nullptr;
          }

          // -- select the fonts to be used for the generic
          auto prefFonts = MakeUnique<PrefFontList>();  // can be empty but in
                                                        // practice won't happen
          uint32_t limit = StaticPrefs::
              gfx_font_rendering_fontconfig_max_generic_substitutions();
          bool foundFontWithLang = false;
          for (int i = 0; i < faces->nfont; i++) {
            FcPattern* font = faces->fonts[i];
            FcChar8* mappedGeneric = nullptr;

            FcPatternGetString(font, FC_FAMILY, 0, &mappedGeneric);
            if (mappedGeneric) {
              mLock.AssertCurrentThreadIn();
              nsAutoCString mappedGenericName(ToCharPtr(mappedGeneric));
              AutoTArray<FamilyAndGeneric, 1> genericFamilies;
              if (gfxPlatformFontList::FindAndAddFamiliesLocked(
                      aFontVisibilityProvider, StyleGenericFontFamily::None,
                      mappedGenericName, &genericFamilies,
                      FindFamiliesFlags(0))) {
                MOZ_ASSERT(genericFamilies.Length() == 1,
                           "expected a single family");
                if (!prefFonts->Contains(genericFamilies[0].mFamily)) {
                  prefFonts->AppendElement(genericFamilies[0].mFamily);
                  bool foundLang =
                      !fcLang.IsEmpty() &&
                      PatternHasLang(font, ToFcChar8Ptr(fcLang.get()));
                  foundFontWithLang = foundFontWithLang || foundLang;
                  // check to see if the list is full
                  if (prefFonts->Length() >= limit) {
                    break;
                  }
                }
              }
            }
          }

          // if no font in the list matches the lang, trim all but the first one
          if (!prefFonts->IsEmpty() && !foundFontWithLang) {
            prefFonts->TruncateLength(1);
          }

          entry.Insert(std::move(prefFonts));
        }
        return entry->get();
      });
}

bool gfxFcPlatformFontList::PrefFontListsUseOnlyGenerics() {
  for (auto iter = mFontPrefs->NameIter(); !iter.Done(); iter.Next()) {
    // Check whether all font.name prefs map to generic keywords
    // and that the pref name and keyword match.
    //   Ex: font.name.serif.ar ==> "serif" (ok)
    //   Ex: font.name.serif.ar ==> "monospace" (return false)
    //   Ex: font.name.serif.ar ==> "DejaVu Serif" (return false)
    //   Ex: font.name.serif.ar ==> "" and
    //       font.name-list.serif.ar ==> "serif" (ok)
    //   Ex: font.name.serif.ar ==> "" and
    //       font.name-list.serif.ar ==> "Something, serif"
    //                                           (return false)
    const nsACString* prefValue = &iter.Data();
    nsAutoCString listValue;
    if (iter.Data().IsEmpty()) {
      // The font name list may have two or more family names as comma
      // separated list.  In such case, not matching with generic font
      // name is fine because if the list prefers specific font, this
      // should return false.
      mFontPrefs->LookupNameList(iter.Key(), listValue);
      prefValue = &listValue;
    }

    nsCCharSeparatedTokenizer tokenizer(iter.Key(), '.');
    const nsDependentCSubstring& generic = tokenizer.nextToken();
    const nsDependentCSubstring& langGroup = tokenizer.nextToken();

    if (!langGroup.EqualsLiteral("x-math") && !generic.Equals(*prefValue)) {
      return false;
    }
  }
  return true;
}

/* static */
void gfxFcPlatformFontList::CheckFontUpdates(nsITimer* aTimer, void* aThis) {
  // A content process is not supposed to check this directly;
  // it will be notified by the parent when the font list changes.
  MOZ_ASSERT(XRE_IsParentProcess());

  // check for font updates
  FcInitBringUptoDate();

  // update fontlist if current config changed
  gfxFcPlatformFontList* pfl = static_cast<gfxFcPlatformFontList*>(aThis);
  FcConfig* current = FcConfigGetCurrent();
  if (current != pfl->GetLastConfig()) {
    pfl->UpdateFontList();
    gfxPlatform::GlobalReflowFlags flags =
        gfxPlatform::GlobalReflowFlags::NeedsReframe |
        gfxPlatform::GlobalReflowFlags::FontsChanged |
        gfxPlatform::GlobalReflowFlags::BroadcastToChildren;
    gfxPlatform::ForceGlobalReflow(flags);
    mozilla::dom::ContentParent::NotifyUpdatedFonts(true);
  }
}

already_AddRefed<gfxFontFamily> gfxFcPlatformFontList::CreateFontFamily(
    const nsACString& aName, FontVisibility aVisibility) const {
  return MakeAndAddRef<gfxFontconfigFontFamily>(aName, aVisibility);
}

// mapping of moz lang groups ==> default lang
struct MozLangGroupData {
  nsAtom* const& mozLangGroup;
  const char* defaultLang;
};

const MozLangGroupData MozLangGroups[] = {
    // clang-format off
  {nsGkAtoms::x_western, "en"},
  {nsGkAtoms::x_cyrillic, "ru"},
  {nsGkAtoms::x_devanagari, "hi"},
  {nsGkAtoms::x_tamil, "ta"},
  {nsGkAtoms::x_armn, "hy"},
  {nsGkAtoms::x_beng, "bn"},
  {nsGkAtoms::x_cans, "iu"},
  {nsGkAtoms::x_ethi, "am"},
  {nsGkAtoms::x_geor, "ka"},
  {nsGkAtoms::x_gujr, "gu"},
  {nsGkAtoms::x_guru, "pa"},
  {nsGkAtoms::x_khmr, "km"},
  {nsGkAtoms::x_knda, "kn"},
  // Zmth is a script subtag for mathematical notation. fontconfig uses
  // "und-zmth" to select math fonts:
  // https://www.iana.org/assignments/language-subtag-registry/language-subtag-registry
  // https://gitlab.freedesktop.org/fontconfig/fontconfig/-/blob/main/fc-lang/und_zmth.orth
  {nsGkAtoms::x_math, "und-zmth"},
  {nsGkAtoms::x_mlym, "ml"},
  {nsGkAtoms::x_orya, "or"},
  {nsGkAtoms::x_sinh, "si"},
  {nsGkAtoms::x_tamil, "ta"},
  {nsGkAtoms::x_telu, "te"},
  {nsGkAtoms::x_tibt, "bo"},
  {nsGkAtoms::Unicode, nullptr}
    // clang-format on
};

bool gfxFcPlatformFontList::TryLangForGroup(const nsACString& aOSLang,
                                            nsAtom* aLangGroup,
                                            nsACString& aFcLang) {
  // Truncate at '.' or '@' from aOSLang, and convert '_' to '-'.
  // aOSLang is in the form "language[_territory][.codeset][@modifier]".
  // fontconfig takes languages in the form "language-territory".
  // nsLanguageAtomService takes languages in the form language-subtag,
  // where subtag may be a territory.  fontconfig and nsLanguageAtomService
  // handle case-conversion for us.
  const char *pos, *end;
  aOSLang.BeginReading(pos);
  aOSLang.EndReading(end);
  aFcLang.Truncate();
  while (pos < end) {
    switch (*pos) {
      case '.':
      case '@':
        end = pos;
        break;
      case '_':
        aFcLang.Append('-');
        break;
      default:
        aFcLang.Append(*pos);
    }
    ++pos;
  }

  nsAtom* atom = mLangService->LookupLanguage(aFcLang);
  return atom == aLangGroup;
}

void gfxFcPlatformFontList::GetSampleLangForGroup(nsAtom* aLanguage,
                                                  nsACString& aLangStr) {
  aLangStr.Truncate();
  if (!aLanguage) {
    return;
  }

  // set up lang string
  const MozLangGroupData* mozLangGroup = nullptr;

  if (aLanguage != nsGkAtoms::x_math ||
      StaticPrefs::mathml_font_family_math_enabled()) {
    // -- look it up in the list of moz lang groups
    for (const auto& MozLangGroup : MozLangGroups) {
      if (aLanguage == MozLangGroup.mozLangGroup) {
        mozLangGroup = &MozLangGroup;
        break;
      }
    }
  }

  // -- not a mozilla lang group? Just return the BCP47 string
  //    representation of the lang group
  if (!mozLangGroup) {
    // Not a special mozilla language group.
    // Use aLanguage as a language code.
    aLanguage->ToUTF8String(aLangStr);
    return;
  }

  // -- check the environment for the user's preferred language that
  //    corresponds to this mozilla lang group.
  const char* languages = getenv("LANGUAGE");
  if (languages) {
    const char separator = ':';

    for (const char* pos = languages; true; ++pos) {
      if (*pos == '\0' || *pos == separator) {
        if (languages < pos &&
            TryLangForGroup(Substring(languages, pos), aLanguage, aLangStr)) {
          return;
        }

        if (*pos == '\0') {
          break;
        }

        languages = pos + 1;
      }
    }
  }
  const char* ctype = setlocale(LC_CTYPE, nullptr);
  if (ctype &&
      TryLangForGroup(nsDependentCString(ctype), aLanguage, aLangStr)) {
    return;
  }

  if (mozLangGroup->defaultLang) {
    aLangStr.Assign(mozLangGroup->defaultLang);
  } else {
    aLangStr.Truncate();
  }
}

#ifdef MOZ_BUNDLED_FONTS
void gfxFcPlatformFontList::ActivateBundledFonts() {
  if (!mBundledFontsInitialized) {
    mBundledFontsInitialized = true;
    nsCOMPtr<nsIFile> localDir;
    nsresult rv = NS_GetSpecialDirectory(NS_GRE_DIR, getter_AddRefs(localDir));
    if (NS_FAILED(rv)) {
      return;
    }
    if (NS_FAILED(localDir->Append(u"fonts"_ns))) {
      return;
    }
    bool isDir;
    if (NS_FAILED(localDir->IsDirectory(&isDir)) || !isDir) {
      return;
    }
    if (NS_FAILED(localDir->GetNativePath(mBundledFontsPath))) {
      return;
    }
  }
  if (!mBundledFontsPath.IsEmpty()) {
    FcConfigAppFontAddDir(nullptr, ToFcChar8Ptr(mBundledFontsPath.get()));
  }
}
#endif

#ifdef MOZ_WIDGET_GTK
/***************************************************************************
 *
 * These functions must be last in the file because it uses the system cairo
 * library.  Above this point the cairo library used is the tree cairo.
 */

// Tree cairo symbols have different names.  Disable their activation through
// preprocessor macros.
#  undef cairo_ft_font_options_substitute

#  undef cairo_font_options_create
#  undef cairo_font_options_destroy
#  undef cairo_font_options_copy
#  undef cairo_font_options_equal

#  undef cairo_font_options_get_antialias
#  undef cairo_font_options_set_antialias
#  undef cairo_font_options_get_hint_style
#  undef cairo_font_options_set_hint_style
#  undef cairo_font_options_get_lcd_filter
#  undef cairo_font_options_set_lcd_filter
#  undef cairo_font_options_get_subpixel_order
#  undef cairo_font_options_set_subpixel_order

// The system cairo functions are not declared because the include paths cause
// the gdk headers to pick up the tree cairo.h.
extern "C" {
NS_VISIBILITY_DEFAULT void cairo_ft_font_options_substitute(
    const cairo_font_options_t* options, FcPattern* pattern);

NS_VISIBILITY_DEFAULT cairo_font_options_t* cairo_font_options_copy(
    const cairo_font_options_t*);
NS_VISIBILITY_DEFAULT cairo_font_options_t* cairo_font_options_create();
NS_VISIBILITY_DEFAULT void cairo_font_options_destroy(cairo_font_options_t*);
NS_VISIBILITY_DEFAULT cairo_bool_t cairo_font_options_equal(
    const cairo_font_options_t*, const cairo_font_options_t*);

NS_VISIBILITY_DEFAULT cairo_antialias_t
cairo_font_options_get_antialias(const cairo_font_options_t*);
NS_VISIBILITY_DEFAULT void cairo_font_options_set_antialias(
    cairo_font_options_t*, cairo_antialias_t);
NS_VISIBILITY_DEFAULT cairo_hint_style_t
cairo_font_options_get_hint_style(const cairo_font_options_t*);
NS_VISIBILITY_DEFAULT void cairo_font_options_set_hint_style(
    cairo_font_options_t*, cairo_hint_style_t);
NS_VISIBILITY_DEFAULT cairo_subpixel_order_t
cairo_font_options_get_subpixel_order(const cairo_font_options_t*);
NS_VISIBILITY_DEFAULT void cairo_font_options_set_subpixel_order(
    cairo_font_options_t*, cairo_subpixel_order_t);
}

void gfxFcPlatformFontList::ClearSystemFontOptions() {
  if (mSystemFontOptions) {
    cairo_font_options_destroy(mSystemFontOptions);
    mSystemFontOptions = nullptr;
  }
  Factory::SetSubpixelOrder(SubpixelOrder::UNKNOWN);
}

static void SetSubpixelOrderFromCairo(const cairo_font_options_t* aOptions) {
  SubpixelOrder subpixelOrder = SubpixelOrder::UNKNOWN;
  switch (cairo_font_options_get_subpixel_order(aOptions)) {
    case CAIRO_SUBPIXEL_ORDER_RGB:
      subpixelOrder = SubpixelOrder::RGB;
      break;
    case CAIRO_SUBPIXEL_ORDER_BGR:
      subpixelOrder = SubpixelOrder::BGR;
      break;
    case CAIRO_SUBPIXEL_ORDER_VRGB:
      subpixelOrder = SubpixelOrder::VRGB;
      break;
    case CAIRO_SUBPIXEL_ORDER_VBGR:
      subpixelOrder = SubpixelOrder::VBGR;
      break;
    default:
      break;
  }
  Factory::SetSubpixelOrder(subpixelOrder);
}

bool gfxFcPlatformFontList::UpdateSystemFontOptions() {
  MOZ_RELEASE_ASSERT(XRE_IsParentProcess());

  if (gfxPlatform::IsHeadless()) {
    return false;
  }

#  ifdef MOZ_X11
  {
    // This one shouldn't change during the X session.
    int lcdfilter;
    GdkDisplay* dpy = gdk_display_get_default();
    if (mozilla::widget::GdkIsX11Display(dpy) &&
        GetXftInt(GDK_DISPLAY_XDISPLAY(dpy), "lcdfilter", &lcdfilter)) {
      mFreetypeLcdSetting = lcdfilter;
    }
  }
#  endif  // MOZ_X11

  const cairo_font_options_t* options =
      gdk_screen_get_font_options(gdk_screen_get_default());
  if (!options) {
    bool changed = !!mSystemFontOptions;
    ClearSystemFontOptions();
    return changed;
  }

  cairo_font_options_t* newOptions = cairo_font_options_copy(options);

  if (mSystemFontOptions &&
      cairo_font_options_equal(mSystemFontOptions, options)) {
    cairo_font_options_destroy(newOptions);
    return false;
  }

  SetSubpixelOrderFromCairo(options);

  ClearSystemFontOptions();
  mSystemFontOptions = newOptions;
  return true;
}

void gfxFcPlatformFontList::SystemFontOptionsToIpc(
    dom::SystemFontOptions& aOptions) {
  aOptions.antialias() =
      mSystemFontOptions ? cairo_font_options_get_antialias(mSystemFontOptions)
                         : CAIRO_ANTIALIAS_DEFAULT;
  aOptions.subpixelOrder() =
      mSystemFontOptions
          ? cairo_font_options_get_subpixel_order(mSystemFontOptions)
          : CAIRO_SUBPIXEL_ORDER_DEFAULT;
  aOptions.hintStyle() =
      mSystemFontOptions ? cairo_font_options_get_hint_style(mSystemFontOptions)
                         : CAIRO_HINT_STYLE_DEFAULT;
  aOptions.lcdFilter() = mFreetypeLcdSetting;
}

void gfxFcPlatformFontList::UpdateSystemFontOptionsFromIpc(
    const dom::SystemFontOptions& aOptions) {
  ClearSystemFontOptions();
  mSystemFontOptions = cairo_font_options_create();
  cairo_font_options_set_antialias(mSystemFontOptions,
                                   cairo_antialias_t(aOptions.antialias()));
  cairo_font_options_set_hint_style(mSystemFontOptions,
                                    cairo_hint_style_t(aOptions.hintStyle()));
  cairo_font_options_set_subpixel_order(
      mSystemFontOptions, cairo_subpixel_order_t(aOptions.subpixelOrder()));
  mFreetypeLcdSetting = aOptions.lcdFilter();
  SetSubpixelOrderFromCairo(mSystemFontOptions);
}

void gfxFcPlatformFontList::SubstituteSystemFontOptions(FcPattern* aPattern) {
  if (mSystemFontOptions) {
    cairo_ft_font_options_substitute(mSystemFontOptions, aPattern);
  }

  if (mFreetypeLcdSetting != -1) {
    FcValue value;
    if (FcPatternGet(aPattern, FC_LCD_FILTER, 0, &value) == FcResultNoMatch) {
      FcPatternAddInteger(aPattern, FC_LCD_FILTER, mFreetypeLcdSetting);
    }
  }
}

#endif  // MOZ_WIDGET_GTK
