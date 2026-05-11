/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "mozilla/FontPropertyTypes.h"
#include "mozilla/MemoryReporting.h"
#include "mozilla/intl/OSPreferences.h"

#include "gfxDWriteFontList.h"
#include "gfxDWriteFonts.h"
#include "gfxDWriteCommon.h"
#include "nsUnicharUtils.h"
#include "nsPresContext.h"
#include "nsServiceManagerUtils.h"
#include "nsCharSeparatedTokenizer.h"
#include "mozilla/dom/Document.h"
#include "mozilla/gfx/Logging.h"
#include "mozilla/EndianUtils.h"
#include "mozilla/LookAndFeel.h"
#include "mozilla/Preferences.h"
#include "mozilla/ProfilerLabels.h"
#include "mozilla/Sprintf.h"
#include "mozilla/StaticPrefs_gfx.h"
#include "mozilla/glean/GfxMetrics.h"
#include "mozilla/WindowsProcessMitigations.h"
#include "nsDirectoryServiceUtils.h"
#include "nsDirectoryServiceDefs.h"
#include "nsAppDirectoryServiceDefs.h"
#include "nsWindowsHelpers.h"

#include "../2d/AutoHelpersWin.h"
#include "gfxRect.h"
#include "SharedFontList-impl.h"

#include "harfbuzz/hb.h"

#define StandardFonts
#include "StandardFonts-win10.inc"
#undef StandardFonts

using namespace mozilla;
using namespace mozilla::gfx;
using mozilla::intl::OSPreferences;

#define LOG_FONTLIST(args) \
  MOZ_LOG(gfxPlatform::GetLog(eGfxLog_fontlist), LogLevel::Debug, args)
#define LOG_FONTLIST_ENABLED() \
  MOZ_LOG_TEST(gfxPlatform::GetLog(eGfxLog_fontlist), LogLevel::Debug)

#define LOG_FONTINIT(args) \
  MOZ_LOG(gfxPlatform::GetLog(eGfxLog_fontinit), LogLevel::Debug, args)
#define LOG_FONTINIT_ENABLED() \
  MOZ_LOG_TEST(gfxPlatform::GetLog(eGfxLog_fontinit), LogLevel::Debug)

#define LOG_CMAPDATA_ENABLED() \
  MOZ_LOG_TEST(gfxPlatform::GetLog(eGfxLog_cmapdata), LogLevel::Debug)

static __inline void BuildKeyNameFromFontName(nsACString& aName) {
  ToLowerCase(aName);
}

////////////////////////////////////////////////////////////////////////////////
// gfxDWriteFontFamily

gfxDWriteFontFamily::~gfxDWriteFontFamily() {}

static bool GetNameAsUtf8(nsACString& aName, IDWriteLocalizedStrings* aStrings,
                          UINT32 aIndex) {
  AutoTArray<WCHAR, 32> name;
  UINT32 length;
  HRESULT hr = aStrings->GetStringLength(aIndex, &length);
  if (FAILED(hr)) {
    return false;
  }
  if (!name.SetLength(length + 1, fallible)) {
    return false;
  }
  hr = aStrings->GetString(aIndex, name.Elements(), length + 1);
  if (FAILED(hr)) {
    return false;
  }
  aName.Truncate();
  AppendUTF16toUTF8(
      Substring(reinterpret_cast<const char16_t*>(name.Elements()),
                name.Length() - 1),
      aName);
  return true;
}

static bool GetEnglishOrFirstName(nsACString& aName,
                                  IDWriteLocalizedStrings* aStrings) {
  UINT32 englishIdx = 0;
  BOOL exists;
  HRESULT hr = aStrings->FindLocaleName(L"en-us", &englishIdx, &exists);
  if (FAILED(hr) || !exists) {
    // Use 0 index if english is not found.
    englishIdx = 0;
  }
  return GetNameAsUtf8(aName, aStrings, englishIdx);
}

static HRESULT GetDirectWriteFontName(IDWriteFont* aFont,
                                      nsACString& aFontName) {
  HRESULT hr;

  RefPtr<IDWriteLocalizedStrings> names;
  hr = aFont->GetFaceNames(getter_AddRefs(names));
  if (FAILED(hr)) {
    return hr;
  }

  if (!GetEnglishOrFirstName(aFontName, names)) {
    return E_FAIL;
  }

  return S_OK;
}

#define FULLNAME_ID DWRITE_INFORMATIONAL_STRING_FULL_NAME
#define PSNAME_ID DWRITE_INFORMATIONAL_STRING_POSTSCRIPT_NAME

// for use in reading postscript or fullname
static HRESULT GetDirectWriteFaceName(IDWriteFont* aFont,
                                      DWRITE_INFORMATIONAL_STRING_ID aWhichName,
                                      nsACString& aFontName) {
  HRESULT hr;

  BOOL exists;
  RefPtr<IDWriteLocalizedStrings> infostrings;
  hr = aFont->GetInformationalStrings(aWhichName, getter_AddRefs(infostrings),
                                      &exists);
  if (FAILED(hr) || !exists) {
    return E_FAIL;
  }

  if (!GetEnglishOrFirstName(aFontName, infostrings)) {
    return E_FAIL;
  }

  return S_OK;
}

void gfxDWriteFontFamily::FindStyleVariationsLocked(
    FontInfoData* aFontInfoData) {
  HRESULT hr;
  if (mHasStyles) {
    return;
  }

  mHasStyles = true;

  gfxPlatformFontList* fp = gfxPlatformFontList::PlatformFontList();

  bool skipFaceNames =
      mFaceNamesInitialized || !fp->NeedFullnamePostscriptNames();
  bool fontInfoShouldHaveFaceNames = !mFaceNamesInitialized &&
                                     fp->NeedFullnamePostscriptNames() &&
                                     aFontInfoData;

  for (UINT32 i = 0; i < mDWFamily->GetFontCount(); i++) {
    RefPtr<IDWriteFont> font;
    hr = mDWFamily->GetFont(i, getter_AddRefs(font));
    if (FAILED(hr)) {
      // This should never happen.
      NS_WARNING("Failed to get existing font from family.");
      continue;
    }

    if (font->GetSimulations() != DWRITE_FONT_SIMULATIONS_NONE) {
      // We don't want these in the font list; we'll apply simulations
      // on the fly when appropriate.
      continue;
    }

    // name
    nsCString fullID(mName);
    nsAutoCString faceName;
    hr = GetDirectWriteFontName(font, faceName);
    if (FAILED(hr)) {
      continue;
    }
    fullID.Append(' ');
    fullID.Append(faceName);

    // Ignore italic style's "Meiryo" because "Meiryo (Bold) Italic" has
    // non-italic style glyphs as Japanese characters.  However, using it
    // causes serious problem if web pages wants some elements to be
    // different style from others only with font-style.  For example,
    // <em> and <i> should be rendered as italic in the default style.
    if (fullID.EqualsLiteral("Meiryo Italic") ||
        fullID.EqualsLiteral("Meiryo Bold Italic")) {
      continue;
    }

    gfxDWriteFontEntry* fe =
        new gfxDWriteFontEntry(fullID, font, mIsSystemFontFamily);
    fe->SetForceGDIClassic(mForceGDIClassic);

    fe->SetupVariationRanges();

    AddFontEntryLocked(fe);

    // postscript/fullname if needed
    nsAutoCString psname, fullname;
    if (fontInfoShouldHaveFaceNames) {
      aFontInfoData->GetFaceNames(fe->Name(), fullname, psname);
      if (!fullname.IsEmpty()) {
        fp->AddFullname(fe, fullname);
      }
      if (!psname.IsEmpty()) {
        fp->AddPostscriptName(fe, psname);
      }
    } else if (!skipFaceNames) {
      hr = GetDirectWriteFaceName(font, PSNAME_ID, psname);
      if (FAILED(hr)) {
        skipFaceNames = true;
      } else if (psname.Length() > 0) {
        fp->AddPostscriptName(fe, psname);
      }

      hr = GetDirectWriteFaceName(font, FULLNAME_ID, fullname);
      if (FAILED(hr)) {
        skipFaceNames = true;
      } else if (fullname.Length() > 0) {
        fp->AddFullname(fe, fullname);
      }
    }

    if (LOG_FONTLIST_ENABLED()) {
      nsAutoCString weightString;
      fe->Weight().ToString(weightString);
      LOG_FONTLIST(
          ("(fontlist) added (%s) to family (%s)"
           " with style: %s weight: %s stretch: %d psname: %s fullname: %s",
           fe->Name().get(), Name().get(),
           (fe->IsItalic()) ? "italic"
                            : (fe->IsOblique() ? "oblique" : "normal"),
           weightString.get(), fe->Stretch().AsScalar(), psname.get(),
           fullname.get()));
    }
  }

  // assume that if no error, all postscript/fullnames were initialized
  if (!skipFaceNames) {
    mFaceNamesInitialized = true;
  }

  if (!mAvailableFonts.Length()) {
    NS_WARNING("Family with no font faces in it.");
  }

  if (mIsBadUnderlineFamily) {
    SetBadUnderlineFonts();
  }

  CheckForSimpleFamily();
  if (mIsSimpleFamily) {
    for (auto& f : mAvailableFonts) {
      if (f) {
        static_cast<gfxDWriteFontEntry*>(f.get())->mMayUseGDIAccess = true;
      }
    }
  }
}

void gfxDWriteFontFamily::ReadFaceNames(gfxPlatformFontList* aPlatformFontList,
                                        bool aNeedFullnamePostscriptNames,
                                        FontInfoData* aFontInfoData) {
  // if all needed names have already been read, skip
  if (mOtherFamilyNamesInitialized &&
      (mFaceNamesInitialized || !aNeedFullnamePostscriptNames)) {
    return;
  }

  // If we've been passed a FontInfoData, we skip the DWrite implementation
  // here and fall back to the generic code which will use that info.
  if (!aFontInfoData) {
    // DirectWrite version of this will try to read
    // postscript/fullnames via DirectWrite API
    FindStyleVariations();
  }

  // fallback to looking up via name table
  if (!mOtherFamilyNamesInitialized || !mFaceNamesInitialized) {
    gfxFontFamily::ReadFaceNames(aPlatformFontList,
                                 aNeedFullnamePostscriptNames, aFontInfoData);
  }
}

void gfxDWriteFontFamily::LocalizedName(nsACString& aLocalizedName) {
  aLocalizedName = Name();  // just return canonical name in case of failure

  if (!mDWFamily) {
    return;
  }

  HRESULT hr;
  nsAutoCString locale;
  // We use system locale here because it's what user expects to see.
  // See bug 1349454 for details.
  RefPtr<OSPreferences> osprefs = OSPreferences::GetInstanceAddRefed();
  if (!osprefs) {
    return;
  }
  osprefs->GetSystemLocale(locale);

  RefPtr<IDWriteLocalizedStrings> names;

  hr = mDWFamily->GetFamilyNames(getter_AddRefs(names));
  if (FAILED(hr)) {
    return;
  }
  UINT32 idx = 0;
  BOOL exists;
  hr =
      names->FindLocaleName(NS_ConvertUTF8toUTF16(locale).get(), &idx, &exists);
  if (FAILED(hr)) {
    return;
  }
  if (!exists) {
    // Use english is localized is not found.
    hr = names->FindLocaleName(L"en-us", &idx, &exists);
    if (FAILED(hr)) {
      return;
    }
    if (!exists) {
      // Use 0 index if english is not found.
      idx = 0;
    }
  }
  AutoTArray<WCHAR, 32> famName;
  UINT32 length;

  hr = names->GetStringLength(idx, &length);
  if (FAILED(hr)) {
    return;
  }

  if (!famName.SetLength(length + 1, fallible)) {
    // Eeep - running out of memory. Unlikely to end well.
    return;
  }

  hr = names->GetString(idx, famName.Elements(), length + 1);
  if (FAILED(hr)) {
    return;
  }

  aLocalizedName = NS_ConvertUTF16toUTF8((const char16_t*)famName.Elements(),
                                         famName.Length() - 1);
}

bool gfxDWriteFontFamily::IsSymbolFontFamily() const {
  // Just check the first font in the family
  if (mDWFamily->GetFontCount() > 0) {
    RefPtr<IDWriteFont> font;
    if (SUCCEEDED(mDWFamily->GetFont(0, getter_AddRefs(font)))) {
      return font->IsSymbolFont();
    }
  }
  return false;
}

void gfxDWriteFontFamily::AddSizeOfExcludingThis(MallocSizeOf aMallocSizeOf,
                                                 FontListSizes* aSizes) const {
  gfxFontFamily::AddSizeOfExcludingThis(aMallocSizeOf, aSizes);
  // TODO:
  // This doesn't currently account for |mDWFamily|
}

void gfxDWriteFontFamily::AddSizeOfIncludingThis(MallocSizeOf aMallocSizeOf,
                                                 FontListSizes* aSizes) const {
  aSizes->mFontListSize += aMallocSizeOf(this);
  AddSizeOfExcludingThis(aMallocSizeOf, aSizes);
}

////////////////////////////////////////////////////////////////////////////////
// gfxDWriteFontEntry

gfxFontEntry* gfxDWriteFontEntry::Clone() const {
  MOZ_ASSERT(!IsUserFont(), "we can only clone installed fonts!");
  gfxDWriteFontEntry* fe = new gfxDWriteFontEntry(Name(), mFont);
  fe->mWeightRange = mWeightRange;
  fe->mStretchRange = mStretchRange;
  fe->mStyleRange = mStyleRange;
  return fe;
}

gfxDWriteFontEntry::~gfxDWriteFontEntry() {
  auto* cache = mFontTableCache.exchange(nullptr);
  delete cache;
}

static bool UsingArabicOrHebrewScriptSystemLocale() {
  LANGID langid = PRIMARYLANGID(::GetSystemDefaultLangID());
  switch (langid) {
    case LANG_ARABIC:
    case LANG_DARI:
    case LANG_PASHTO:
    case LANG_PERSIAN:
    case LANG_SINDHI:
    case LANG_UIGHUR:
    case LANG_URDU:
    case LANG_HEBREW:
      return true;
    default:
      return false;
  }
}

nsresult gfxDWriteFontEntry::CopyFontTable(uint32_t aTableTag,
                                           nsTArray<uint8_t>& aBuffer) {
  gfxDWriteFontList* pFontList = gfxDWriteFontList::PlatformFontList();
  const uint32_t tagBE = NativeEndian::swapToBigEndian(aTableTag);

  // Don't use GDI table loading for symbol fonts or for
  // italic fonts in Arabic-script system locales because of
  // potential cmap discrepancies, see bug 629386.
  // Ditto for Hebrew, bug 837498.
  if (mFont && mMayUseGDIAccess && pFontList->UseGDIFontTableAccess() &&
      !(!IsUpright() && UsingArabicOrHebrewScriptSystemLocale()) &&
      !mFont->IsSymbolFont()) {
    LOGFONTW logfont = {0};
    if (InitLogFont(mFont, &logfont)) {
      AutoDC dc;
      AutoSelectFont font(dc.GetDC(), &logfont);
      if (font.IsValid()) {
        uint32_t tableSize = ::GetFontData(dc.GetDC(), tagBE, 0, nullptr, 0);
        if (tableSize != GDI_ERROR) {
          if (aBuffer.SetLength(tableSize, fallible)) {
            ::GetFontData(dc.GetDC(), tagBE, 0, aBuffer.Elements(),
                          aBuffer.Length());
            return NS_OK;
          }
          return NS_ERROR_OUT_OF_MEMORY;
        }
      }
    }
  }

  RefPtr<IDWriteFontFace> fontFace;
  nsresult rv = CreateFontFace(getter_AddRefs(fontFace));
  if (NS_FAILED(rv)) {
    return rv;
  }

  uint8_t* tableData;
  uint32_t len;
  void* tableContext = nullptr;
  BOOL exists;
  HRESULT hr = fontFace->TryGetFontTable(tagBE, (const void**)&tableData, &len,
                                         &tableContext, &exists);
  if (FAILED(hr) || !exists) {
    return NS_ERROR_FAILURE;
  }

  if (aBuffer.SetLength(len, fallible)) {
    memcpy(aBuffer.Elements(), tableData, len);
    rv = NS_OK;
  } else {
    rv = NS_ERROR_OUT_OF_MEMORY;
  }

  if (tableContext) {
    fontFace->ReleaseFontTable(&tableContext);
  }

  return rv;
}

// Access to font tables packaged in hb_blob_t form

// object attached to the Harfbuzz blob, used to release
// the table when the blob is destroyed
class FontTableRec {
 public:
  FontTableRec(IDWriteFontFace* aFontFace, void* aContext)
      : mFontFace(aFontFace), mContext(aContext) {
    MOZ_COUNT_CTOR(FontTableRec);
  }

  ~FontTableRec() {
    MOZ_COUNT_DTOR(FontTableRec);
    mFontFace->ReleaseFontTable(mContext);
  }

 private:
  RefPtr<IDWriteFontFace> mFontFace;
  void* mContext;
};

static void DestroyBlobFunc(void* aUserData) {
  FontTableRec* ftr = static_cast<FontTableRec*>(aUserData);
  delete ftr;
}

hb_blob_t* gfxDWriteFontEntry::GetFontTable(uint32_t aTag) {
  // try to avoid potentially expensive DWrite call if we haven't actually
  // created the font face yet, by using the gfxFontEntry method that will
  // use CopyFontTable and then cache the data
  if (!mFontFace) {
    return gfxFontEntry::GetFontTable(aTag);
  }

  const void* data;
  UINT32 size;
  void* context;
  BOOL exists;
  HRESULT hr = mFontFace->TryGetFontTable(NativeEndian::swapToBigEndian(aTag),
                                          &data, &size, &context, &exists);
  if (SUCCEEDED(hr) && exists) {
    FontTableRec* ftr = new FontTableRec(mFontFace, context);
    return hb_blob_create(static_cast<const char*>(data), size,
                          HB_MEMORY_MODE_READONLY, ftr, DestroyBlobFunc);
  }

  return nullptr;
}

gfxFontEntry::FontTableCache* gfxDWriteFontEntry::GetFontTableCache(
    bool aCreate) {
  // Create the cache if it does not yet exist.
  if (!mFontTableCache && aCreate) {
    auto* cache = new FontTableCache();
    if (!mFontTableCache.compareExchange(nullptr, cache)) {
      delete cache;
    }
  }
  return mFontTableCache;
}

nsresult gfxDWriteFontEntry::ReadCMAP(FontInfoData* aFontInfoData) {
  AUTO_PROFILER_LABEL("gfxDWriteFontEntry::ReadCMAP", GRAPHICS);

  // attempt this once, if errors occur leave a blank cmap
  if (mCharacterMap || mShmemCharacterMap) {
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
    // Bug 969504: exclude U+25B6 from Segoe UI family, because it's used
    // by sites to represent a "Play" icon, but the glyph in Segoe UI Light
    // and Semibold on Windows 7 is too thin. (Ditto for leftward U+25C0.)
    // Fallback to Segoe UI Symbol is preferred.
    if (FamilyName().EqualsLiteral("Segoe UI")) {
      charmap->clear(0x25b6);
      charmap->clear(0x25c0);
    }
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
    mHasCmapTable = true;
  } else {
    // if error occurred, initialize to null cmap
    charmap = new gfxCharacterMap(0);
    mHasCmapTable = false;
  }
  if (setCharMap) {
    // Temporarily retain charmap, until the shared version is
    // ready for use.
    if (mCharacterMap.compareExchange(nullptr, charmap.get())) {
      charmap.get()->AddRef();
    }
  }

  LOG_FONTLIST(("(fontlist-cmap) name: %s, size: %zu hash: %8.8x%s\n",
                mName.get(), charmap->SizeOfIncludingThis(moz_malloc_size_of),
                charmap->mHash, mCharacterMap == charmap ? " new" : ""));
  if (LOG_CMAPDATA_ENABLED()) {
    char prefix[256];
    SprintfLiteral(prefix, "(cmapdata) name: %.220s", mName.get());
    charmap->Dump(prefix, eGfxLog_cmapdata);
  }

  return rv;
}

bool gfxDWriteFontEntry::HasVariations() {
  if (mHasVariationsInitialized) {
    return mHasVariations;
  }
  mHasVariationsInitialized = true;
  mHasVariations = false;

  if (!gfxPlatform::HasVariationFontSupport()) {
    return mHasVariations;
  }

  if (!mFontFace) {
    // CreateFontFace will initialize the mFontFace field, and also
    // mFontFace5 if available on the current DWrite version.
    RefPtr<IDWriteFontFace> fontFace;
    if (NS_FAILED(CreateFontFace(getter_AddRefs(fontFace)))) {
      return mHasVariations;
    }
  }
  if (mFontFace5) {
    mHasVariations = mFontFace5->HasVariations();
  }
  return mHasVariations;
}

void gfxDWriteFontEntry::GetVariationAxes(
    nsTArray<gfxFontVariationAxis>& aAxes) {
  if (!HasVariations()) {
    return;
  }
  // HasVariations() will have ensured the mFontFace5 interface is available;
  // so we can get an IDWriteFontResource and ask it for the axis info.
  RefPtr<IDWriteFontResource> resource;
  HRESULT hr = mFontFace5->GetFontResource(getter_AddRefs(resource));
  if (FAILED(hr) || !resource) {
    return;
  }

  uint32_t count = resource->GetFontAxisCount();
  AutoTArray<DWRITE_FONT_AXIS_VALUE, 4> defaultValues;
  AutoTArray<DWRITE_FONT_AXIS_RANGE, 4> ranges;
  defaultValues.SetLength(count);
  ranges.SetLength(count);
  resource->GetDefaultFontAxisValues(defaultValues.Elements(), count);
  resource->GetFontAxisRanges(ranges.Elements(), count);
  for (uint32_t i = 0; i < count; ++i) {
    gfxFontVariationAxis axis;
    MOZ_ASSERT(ranges[i].axisTag == defaultValues[i].axisTag);
    DWRITE_FONT_AXIS_ATTRIBUTES attrs = resource->GetFontAxisAttributes(i);
    if (attrs & DWRITE_FONT_AXIS_ATTRIBUTES_HIDDEN) {
      continue;
    }
    if (!(attrs & DWRITE_FONT_AXIS_ATTRIBUTES_VARIABLE)) {
      continue;
    }
    // Extract the 4 chars of the tag from DWrite's packed version,
    // and reassemble them in the order we use for TRUETYPE_TAG.
    uint32_t t = defaultValues[i].axisTag;
    axis.mTag = TRUETYPE_TAG(t & 0xff, (t >> 8) & 0xff, (t >> 16) & 0xff,
                             (t >> 24) & 0xff);
    // Try to get a human-friendly name (may not be present)
    RefPtr<IDWriteLocalizedStrings> names;
    resource->GetAxisNames(i, getter_AddRefs(names));
    if (names) {
      GetEnglishOrFirstName(axis.mName, names);
    }
    axis.mMinValue = ranges[i].minValue;
    axis.mMaxValue = ranges[i].maxValue;
    axis.mDefaultValue = defaultValues[i].value;
    aAxes.AppendElement(axis);
  }
}

void gfxDWriteFontEntry::GetVariationInstances(
    nsTArray<gfxFontVariationInstance>& aInstances) {
  gfxFontUtils::GetVariationData(this, nullptr, &aInstances);
}

gfxFont* gfxDWriteFontEntry::CreateFontInstance(
    const gfxFontStyle* aFontStyle) {
  // We use the DirectWrite bold simulation for installed fonts, but NOT for
  // webfonts; those will use multi-strike synthetic bold instead.
  bool useBoldSim = false;
  if (aFontStyle->NeedsSyntheticBold(this)) {
    switch (StaticPrefs::gfx_font_rendering_directwrite_bold_simulation()) {
      case 0:  // never use the DWrite simulation
        break;
      case 1:  // use DWrite simulation for installed fonts except COLR fonts,
               // but not webfonts
        useBoldSim =
            !mIsDataUserFont && !HasFontTable(TRUETYPE_TAG('C', 'O', 'L', 'R'));
        break;
      default:  // always use DWrite bold simulation, except for COLR fonts
        useBoldSim = !HasFontTable(TRUETYPE_TAG('C', 'O', 'L', 'R'));
        break;
    }
  }
  DWRITE_FONT_SIMULATIONS sims =
      useBoldSim ? DWRITE_FONT_SIMULATIONS_BOLD : DWRITE_FONT_SIMULATIONS_NONE;
  ThreadSafeWeakPtr<UnscaledFontDWrite>& unscaledFontPtr =
      useBoldSim ? mUnscaledFontBold : mUnscaledFont;
  RefPtr<UnscaledFontDWrite> unscaledFont(unscaledFontPtr);
  if (!unscaledFont) {
    RefPtr<IDWriteFontFace> fontFace;
    nsresult rv =
        CreateFontFace(getter_AddRefs(fontFace), nullptr, sims, nullptr);
    if (NS_FAILED(rv)) {
      return nullptr;
    }
    // Only pass in the underlying IDWriteFont if the unscaled font doesn't
    // reflect a data font. This signals whether or not we can safely query
    // a descriptor to represent the font for various transport use-cases.
    unscaledFont =
        new UnscaledFontDWrite(fontFace, !mIsDataUserFont ? mFont : nullptr);
    unscaledFontPtr = unscaledFont;
  }
  RefPtr<IDWriteFontFace> fontFace;
  if (HasVariations()) {
    // Get the variation settings needed to instantiate the fontEntry
    // for a particular fontStyle.
    AutoTArray<gfxFontVariation, 4> vars;
    GetVariationsForStyle(vars, *aFontStyle);

    if (!vars.IsEmpty()) {
      nsresult rv =
          CreateFontFace(getter_AddRefs(fontFace), aFontStyle, sims, &vars);
      if (NS_FAILED(rv)) {
        return nullptr;
      }
    }
  }
  return new gfxDWriteFont(unscaledFont, this, aFontStyle, fontFace);
}

nsresult gfxDWriteFontEntry::CreateFontFace(
    IDWriteFontFace** aFontFace, const gfxFontStyle* aFontStyle,
    DWRITE_FONT_SIMULATIONS aSimulations,
    const nsTArray<gfxFontVariation>* aVariations) {
  // Convert an OpenType font tag from our uint32_t representation
  // (as constructed by TRUETYPE_TAG(...)) to the order DWrite wants.
  auto makeDWriteAxisTag = [](uint32_t aTag) {
    return DWRITE_MAKE_FONT_AXIS_TAG((aTag >> 24) & 0xff, (aTag >> 16) & 0xff,
                                     (aTag >> 8) & 0xff, aTag & 0xff);
  };

  MOZ_SEH_TRY {
    // initialize mFontFace if this hasn't been done before
    if (!mFontFace) {
      HRESULT hr;
      if (mFont) {
        hr = mFont->CreateFontFace(getter_AddRefs(mFontFace));
      } else if (mFontFile) {
        IDWriteFontFile* fontFile = mFontFile.get();
        hr = Factory::GetDWriteFactory()->CreateFontFace(
            mFaceType, 1, &fontFile, 0, DWRITE_FONT_SIMULATIONS_NONE,
            getter_AddRefs(mFontFace));
      } else {
        MOZ_ASSERT_UNREACHABLE("invalid font entry");
        return NS_ERROR_FAILURE;
      }
      if (FAILED(hr)) {
        return NS_ERROR_FAILURE;
      }
      // Also get the IDWriteFontFace5 interface if we're running on a
      // sufficiently new DWrite version where it is available.
      if (mFontFace) {
        mFontFace->QueryInterface(__uuidof(IDWriteFontFace5),
                                  (void**)getter_AddRefs(mFontFace5));
        if (!mVariationSettings.IsEmpty()) {
          // If the font entry has variations specified, mFontFace5 will
          // be a distinct face that has the variations applied.
          RefPtr<IDWriteFontResource> resource;
          HRESULT hr = mFontFace5->GetFontResource(getter_AddRefs(resource));
          if (SUCCEEDED(hr) && resource) {
            AutoTArray<DWRITE_FONT_AXIS_VALUE, 4> fontAxisValues;
            for (const auto& v : mVariationSettings) {
              DWRITE_FONT_AXIS_VALUE axisValue = {makeDWriteAxisTag(v.mTag),
                                                  v.mValue};
              fontAxisValues.AppendElement(axisValue);
            }
            resource->CreateFontFace(
                mFontFace->GetSimulations(), fontAxisValues.Elements(),
                fontAxisValues.Length(), getter_AddRefs(mFontFace5));
          }
        }
      }
    }

    // Do we need to modify DWrite simulations from what mFontFace has?
    bool needSimulations =
        (aSimulations & DWRITE_FONT_SIMULATIONS_BOLD) &&
        !(mFontFace->GetSimulations() & DWRITE_FONT_SIMULATIONS_BOLD);

    // If the IDWriteFontFace5 interface is available, we can try using
    // IDWriteFontResource to create a new modified face.
    if (mFontFace5 && (HasVariations() || needSimulations)) {
      RefPtr<IDWriteFontResource> resource;
      HRESULT hr = mFontFace5->GetFontResource(getter_AddRefs(resource));
      if (SUCCEEDED(hr) && resource) {
        AutoTArray<DWRITE_FONT_AXIS_VALUE, 4> fontAxisValues;

        // Copy variation settings to DWrite's type.
        if (aVariations) {
          for (const auto& v : *aVariations) {
            DWRITE_FONT_AXIS_VALUE axisValue = {makeDWriteAxisTag(v.mTag),
                                                v.mValue};
            fontAxisValues.AppendElement(axisValue);
          }
        }

        IDWriteFontFace5* ff5;
        resource->CreateFontFace(aSimulations, fontAxisValues.Elements(),
                                 fontAxisValues.Length(), &ff5);
        if (ff5) {
          *aFontFace = ff5;
          return NS_OK;
        }
      }
    }

    // Do we need to add DWrite simulations to the face?
    if (needSimulations) {
      // if so, we need to return not mFontFace itself but a version that
      // has the Bold simulation - unfortunately, old DWrite doesn't provide
      // a simple API for this
      UINT32 numberOfFiles = 0;
      if (FAILED(mFontFace->GetFiles(&numberOfFiles, nullptr))) {
        return NS_ERROR_FAILURE;
      }
      AutoTArray<IDWriteFontFile*, 1> files;
      files.AppendElements(numberOfFiles);
      if (FAILED(mFontFace->GetFiles(&numberOfFiles, files.Elements()))) {
        return NS_ERROR_FAILURE;
      }
      HRESULT hr = Factory::GetDWriteFactory()->CreateFontFace(
          mFontFace->GetType(), numberOfFiles, files.Elements(),
          mFontFace->GetIndex(), aSimulations, aFontFace);
      for (UINT32 i = 0; i < numberOfFiles; ++i) {
        files[i]->Release();
      }
      return FAILED(hr) ? NS_ERROR_FAILURE : NS_OK;
    }
  }
  MOZ_SEH_EXCEPT(EXCEPTION_EXECUTE_HANDLER) {
    gfxCriticalNote << "Exception occurred creating font face for "
                    << mName.get();
  }

  // no simulation: we can just add a reference to mFontFace5 (if present)
  // or mFontFace (otherwise) and return that
  if (mFontFace5) {
    *aFontFace = mFontFace5;
  } else {
    *aFontFace = mFontFace;
  }
  (*aFontFace)->AddRef();
  return NS_OK;
}

bool gfxDWriteFontEntry::InitLogFont(IDWriteFont* aFont, LOGFONTW* aLogFont) {
  HRESULT hr;

  BOOL isInSystemCollection;
  IDWriteGdiInterop* gdi =
      gfxDWriteFontList::PlatformFontList()->GetGDIInterop();
  hr = gdi->ConvertFontToLOGFONT(aFont, aLogFont, &isInSystemCollection);
  // If the font is not in the system collection, GDI will be unable to
  // select it and load its tables, so we return false here to indicate
  // failure, and let CopyFontTable fall back to DWrite native methods.
  return (SUCCEEDED(hr) && isInSystemCollection);
}

bool gfxDWriteFontEntry::IsCJKFont() {
  if (mIsCJK != UNINITIALIZED_VALUE) {
    return mIsCJK;
  }

  mIsCJK = false;

  const uint32_t kOS2Tag = TRUETYPE_TAG('O', 'S', '/', '2');
  gfxFontUtils::AutoHBBlob blob(GetFontTable(kOS2Tag));
  if (!blob) {
    return mIsCJK;
  }

  uint32_t len;
  const OS2Table* os2 =
      reinterpret_cast<const OS2Table*>(hb_blob_get_data(blob, &len));
  // ulCodePageRange bit definitions for the CJK codepages,
  // from http://www.microsoft.com/typography/otspec/os2.htm#cpr
  const uint32_t CJK_CODEPAGE_BITS =
      (1 << 17) |  // codepage 932 - JIS/Japan
      (1 << 18) |  // codepage 936 - Chinese (simplified)
      (1 << 19) |  // codepage 949 - Korean Wansung
      (1 << 20) |  // codepage 950 - Chinese (traditional)
      (1 << 21);   // codepage 1361 - Korean Johab
  if (len >= offsetof(OS2Table, sxHeight)) {
    if ((uint32_t(os2->codePageRange1) & CJK_CODEPAGE_BITS) != 0) {
      mIsCJK = true;
    }
  }

  return mIsCJK;
}

void gfxDWriteFontEntry::AddSizeOfExcludingThis(MallocSizeOf aMallocSizeOf,
                                                FontListSizes* aSizes) const {
  gfxFontEntry::AddSizeOfExcludingThis(aMallocSizeOf, aSizes);
  // TODO:
  // This doesn't currently account for the |mFont| and |mFontFile| members
}

void gfxDWriteFontEntry::AddSizeOfIncludingThis(MallocSizeOf aMallocSizeOf,
                                                FontListSizes* aSizes) const {
  aSizes->mFontListSize += aMallocSizeOf(this);
  AddSizeOfExcludingThis(aMallocSizeOf, aSizes);
}

size_t gfxDWriteFontEntry::ComputedSizeOfExcludingThis(
    mozilla::MallocSizeOf aMallocSizeOf) {
  size_t result = gfxFontEntry::ComputedSizeOfExcludingThis(aMallocSizeOf);
  if (mFontFileStream) {
    result += mFontFileStream->SizeOfExcludingThis(aMallocSizeOf);
  }
  return result;
}

////////////////////////////////////////////////////////////////////////////////
// gfxDWriteFontList

gfxDWriteFontList::gfxDWriteFontList() : mForceGDIClassicMaxFontSize(0.0) {
  CheckFamilyList(kBaseFonts);
  CheckFamilyList(kLangPackFonts);
}

// bug 602792 - CJK systems default to large CJK fonts which cause excessive
//   I/O strain during cold startup due to dwrite caching bugs.  Default to
//   Arial to avoid this.

FontFamily gfxDWriteFontList::GetDefaultFontForPlatform(
    FontVisibilityProvider* aFontVisibilityProvider, const gfxFontStyle* aStyle,
    nsAtom* aLanguage) {
  // try Arial first
  FontFamily ff;
  ff = FindFamily(aFontVisibilityProvider, "Arial"_ns);
  if (!ff.IsNull()) {
    return ff;
  }

  // otherwise, use local default
  gfxFontStyle fontStyle;
  nsAutoString systemFontName;
  if (!mozilla::LookAndFeel::GetFont(mozilla::StyleSystemFont::MessageBox,
                                     systemFontName, fontStyle)) {
    return ff;
  }

  ff = FindFamily(aFontVisibilityProvider,
                  NS_ConvertUTF16toUTF8(systemFontName));
  return ff;
}

gfxFontEntry* gfxDWriteFontList::LookupLocalFont(
    FontVisibilityProvider* aFontVisibilityProvider,
    const nsACString& aFontName, WeightRange aWeightForEntry,
    StretchRange aStretchForEntry, SlantStyleRange aStyleForEntry) {
  AutoLock lock(mLock);

  if (SharedFontList()) {
    return LookupInSharedFaceNameList(aFontVisibilityProvider, aFontName,
                                      aWeightForEntry, aStretchForEntry,
                                      aStyleForEntry);
  }

  gfxFontEntry* lookup;

  lookup = LookupInFaceNameLists(aFontName);
  if (!lookup) {
    return nullptr;
  }

  gfxDWriteFontEntry* dwriteLookup = static_cast<gfxDWriteFontEntry*>(lookup);
  gfxDWriteFontEntry* fe =
      new gfxDWriteFontEntry(lookup->Name(), dwriteLookup->mFont,
                             aWeightForEntry, aStretchForEntry, aStyleForEntry);
  fe->SetForceGDIClassic(dwriteLookup->GetForceGDIClassic());
  return fe;
}

gfxFontEntry* gfxDWriteFontList::MakePlatformFont(
    const nsACString& aFontName, WeightRange aWeightForEntry,
    StretchRange aStretchForEntry, SlantStyleRange aStyleForEntry,
    const uint8_t* aFontData, uint32_t aLength) {
  RefPtr<gfxDWriteFontFileStream> fontFileStream;
  RefPtr<IDWriteFontFile> fontFile;
  HRESULT hr = gfxDWriteFontFileLoader::CreateCustomFontFile(
      aFontData, aLength, getter_AddRefs(fontFile),
      getter_AddRefs(fontFileStream));
  free((void*)aFontData);
  NS_ASSERTION(SUCCEEDED(hr), "Failed to create font file reference");
  if (FAILED(hr)) {
    return nullptr;
  }

  nsAutoString uniqueName;
  nsresult rv = gfxFontUtils::MakeUniqueUserFontName(uniqueName);
  NS_ASSERTION(NS_SUCCEEDED(rv), "Failed to make unique user font name");
  if (NS_FAILED(rv)) {
    return nullptr;
  }

  BOOL isSupported;
  DWRITE_FONT_FILE_TYPE fileType;
  UINT32 numFaces;

  auto entry = MakeUnique<gfxDWriteFontEntry>(
      NS_ConvertUTF16toUTF8(uniqueName), fontFile, fontFileStream,
      aWeightForEntry, aStretchForEntry, aStyleForEntry);

  hr = fontFile->Analyze(&isSupported, &fileType, &entry->mFaceType, &numFaces);
  NS_ASSERTION(SUCCEEDED(hr), "IDWriteFontFile::Analyze failed");
  if (FAILED(hr)) {
    return nullptr;
  }
  NS_ASSERTION(isSupported, "Unsupported font file");
  if (!isSupported) {
    return nullptr;
  }
  NS_ASSERTION(numFaces == 1, "Font file does not contain exactly 1 face");
  if (numFaces != 1) {
    // We don't know how to deal with 0 faces either.
    return nullptr;
  }

  return entry.release();
}

bool gfxDWriteFontList::UseGDIFontTableAccess() const {
  // Using GDI font table access for DWrite is controlled by a pref, but also we
  // must be able to make win32k calls.
  return mGDIFontTableAccess && !IsWin32kLockedDown();
}

static void GetPostScriptNameFromNameTable(IDWriteFontFace* aFace,
                                           nsCString& aName) {
  const auto kNAME =
      NativeEndian::swapToBigEndian(TRUETYPE_TAG('n', 'a', 'm', 'e'));
  const char* data;
  UINT32 size;
  void* context;
  BOOL exists;
  if (SUCCEEDED(aFace->TryGetFontTable(kNAME, (const void**)&data, &size,
                                       &context, &exists)) &&
      exists) {
    if (NS_FAILED(gfxFontUtils::ReadCanonicalName(
            data, size, gfxFontUtils::NAME_ID_POSTSCRIPT, aName))) {
      aName.Truncate(0);
    }
    aFace->ReleaseFontTable(context);
  }
}

gfxFontEntry* gfxDWriteFontList::CreateFontEntry(
    fontlist::Face* aFace, const fontlist::Family* aFamily) {
  IDWriteFontCollection* collection =
#ifdef MOZ_BUNDLED_FONTS
      aFamily->IsBundled() ? mBundledFonts : mSystemFonts;
#else
      mSystemFonts;
#endif
  RefPtr<IDWriteFontFamily> family;
  bool foundExpectedFamily = false;
  const nsCString& familyName =
      aFamily->DisplayName().AsString(SharedFontList());

  // The DirectWrite calls here might throw exceptions, e.g. in case of disk
  // errors when trying to read the font file.
  MOZ_SEH_TRY {
    if (aFamily->Index() < collection->GetFontFamilyCount()) {
      HRESULT hr =
          collection->GetFontFamily(aFamily->Index(), getter_AddRefs(family));
      // Check that the family name is what we expected; if not, fall back to
      // search by name. It's sad we have to do this, but it is possible for
      // Windows to have given different versions of the system font collection
      // to the parent and child processes.
      if (SUCCEEDED(hr) && family) {
        RefPtr<IDWriteLocalizedStrings> names;
        hr = family->GetFamilyNames(getter_AddRefs(names));
        if (SUCCEEDED(hr) && names) {
          nsAutoCString name;
          if (GetEnglishOrFirstName(name, names)) {
            foundExpectedFamily = name.Equals(familyName);
          }
        }
      }
    }
    if (!foundExpectedFamily) {
      // Try to get family by name instead of index (to deal with the case of
      // collection mismatch).
      UINT32 index;
      BOOL exists;
      NS_ConvertUTF8toUTF16 name16(familyName);
      HRESULT hr = collection->FindFamilyName(name16.getW(), &index, &exists);
      if (FAILED(hr) || !exists || index == UINT_MAX ||
          FAILED(collection->GetFontFamily(index, getter_AddRefs(family))) ||
          !family) {
        return nullptr;
      }
    }

    // Retrieve the required face by index within the family.
    RefPtr<IDWriteFont> font;
    if (FAILED(family->GetFont(aFace->mIndex, getter_AddRefs(font))) || !font) {
      return nullptr;
    }

    // Retrieve the psName from the font, so we can check we've found the
    // expected face.
    nsAutoCString psName;
    if (FAILED(GetDirectWriteFaceName(font, PSNAME_ID, psName))) {
      RefPtr<IDWriteFontFace> dwFontFace;
      if (SUCCEEDED(font->CreateFontFace(getter_AddRefs(dwFontFace)))) {
        GetPostScriptNameFromNameTable(dwFontFace, psName);
      }
    }

    // If it doesn't match, DirectWrite must have shuffled the order of faces
    // returned for the family; search by name as a fallback.
    nsCString faceName = aFace->mDescriptor.AsString(SharedFontList());
    if (psName != faceName) {
      gfxWarning() << "Face name mismatch for index " << aFace->mIndex
                   << " in family " << familyName.get() << ": expected "
                   << faceName.get() << ", found " << psName.get();
      for (uint32_t i = 0; i < family->GetFontCount(); ++i) {
        if (i == aFace->mIndex) {
          continue;  // this was the face we already tried
        }
        if (FAILED(family->GetFont(i, getter_AddRefs(font))) || !font) {
          return nullptr;  // this font family is broken!
        }
        if (FAILED(GetDirectWriteFaceName(font, PSNAME_ID, psName))) {
          RefPtr<IDWriteFontFace> dwFontFace;
          if (SUCCEEDED(font->CreateFontFace(getter_AddRefs(dwFontFace)))) {
            GetPostScriptNameFromNameTable(dwFontFace, psName);
          }
        }
        if (psName == faceName) {
          break;
        }
      }
    }
    if (psName != faceName) {
      return nullptr;
    }

    auto fe = new gfxDWriteFontEntry(faceName, font, !aFamily->IsBundled());
    fe->InitializeFrom(aFace, aFamily);
    fe->mForceGDIClassic = aFamily->IsForceClassic();
    fe->mMayUseGDIAccess = aFamily->IsSimple();
    return fe;
  }
  MOZ_SEH_EXCEPT(EXCEPTION_EXECUTE_HANDLER) {
    gfxCriticalNote << "Exception occurred while creating font entry for "
                    << familyName.get();
  }
  return nullptr;
}

FontVisibility gfxDWriteFontList::GetVisibilityForFamily(
    const nsACString& aName) const {
  if (FamilyInList(aName, kBaseFonts)) {
    return FontVisibility::Base;
  }
  if (FamilyInList(aName, kLangPackFonts)) {
    return FontVisibility::LangPack;
  }
  if (nsRFPService::FontIsAllowedByLocale(aName)) {
    return FontVisibility::LangPack;
  }
  return FontVisibility::User;
}

nsTArray<std::pair<const char**, uint32_t>>
gfxDWriteFontList::GetFilteredPlatformFontLists() {
  nsTArray<std::pair<const char**, uint32_t>> fontLists;

  fontLists.AppendElement(std::make_pair(kBaseFonts, std::size(kBaseFonts)));
  fontLists.AppendElement(
      std::make_pair(kLangPackFonts, std::size(kLangPackFonts)));

  return fontLists;
}

void gfxDWriteFontList::AppendFamiliesFromCollection(
    IDWriteFontCollection* aCollection,
    nsTArray<fontlist::Family::InitData>& aFamilies,
    const nsTArray<nsCString>* aForceClassicFams) {
  auto allFacesUltraBold = [](IDWriteFontFamily* aFamily) -> bool {
    for (UINT32 i = 0; i < aFamily->GetFontCount(); i++) {
      RefPtr<IDWriteFont> font;
      HRESULT hr = aFamily->GetFont(i, getter_AddRefs(font));
      if (FAILED(hr)) {
        NS_WARNING("Failed to get existing font from family.");
        continue;
      }
      nsAutoCString faceName;
      hr = GetDirectWriteFontName(font, faceName);
      if (FAILED(hr)) {
        continue;
      }
      if (faceName.Find("Ultra Bold"_ns) == kNotFound) {
        return false;
      }
    }
    return true;
  };

  nsAutoCString locale;
  OSPreferences::GetInstance()->GetSystemLocale(locale);
  ToLowerCase(locale);
  NS_ConvertUTF8toUTF16 loc16(locale);

  for (unsigned i = 0; i < aCollection->GetFontFamilyCount(); ++i) {
    RefPtr<IDWriteFontFamily> family;
    aCollection->GetFontFamily(i, getter_AddRefs(family));
    RefPtr<IDWriteLocalizedStrings> localizedNames;
    HRESULT hr = family->GetFamilyNames(getter_AddRefs(localizedNames));
    if (FAILED(hr)) {
      gfxWarning() << "Failed to get names for font-family " << i;
      continue;
    }

    auto addFamily = [&](const nsACString& name, FontVisibility visibility,
                         bool altLocale = false) {
      nsAutoCString key;
      key = name;
      BuildKeyNameFromFontName(key);
      bool bad = mBadUnderlineFamilyNames.ContainsSorted(key);
      bool classic =
          aForceClassicFams && aForceClassicFams->ContainsSorted(key);
      aFamilies.AppendElement(fontlist::Family::InitData(
          key, name, i, visibility, aCollection != mSystemFonts, bad, classic,
          altLocale));
    };

    auto visibilityForName = [&](const nsACString& aName) -> FontVisibility {
      // Special case: hide the "Gill Sans" family that contains only UltraBold
      // faces, as this leads to breakage on sites with CSS that targeted the
      // Gill Sans family as found on macOS. (Bug 551313, bug 1632738)
      // TODO (jfkthame): the ultrabold faces from Gill Sans should be treated
      // as belonging to the Gill Sans MT family.
      if (aName.EqualsLiteral("Gill Sans") && allFacesUltraBold(family)) {
        return FontVisibility::Hidden;
      }
      // Bundled fonts are always available, so only system fonts are checked
      // against the standard font names list.
      return aCollection == mSystemFonts ? GetVisibilityForFamily(aName)
                                         : FontVisibility::Base;
    };

    unsigned count = localizedNames->GetCount();
    if (count == 1) {
      // This is the common case: the great majority of fonts only provide an
      // en-US family name.
      nsAutoCString name;
      if (!GetNameAsUtf8(name, localizedNames, 0)) {
        gfxWarning() << "GetNameAsUtf8 failed for index 0 in font-family " << i;
        continue;
      }
      addFamily(name, visibilityForName(name));
    } else {
      AutoTArray<nsCString, 4> names;
      int sysLocIndex = -1;
      FontVisibility visibility = FontVisibility::User;
      for (unsigned index = 0; index < count; ++index) {
        nsAutoCString name;
        if (!GetNameAsUtf8(name, localizedNames, index)) {
          gfxWarning() << "GetNameAsUtf8 failed for index " << index
                       << " in font-family " << i;
          continue;
        }
        if (names.Contains(name)) {
          continue;
        }
        if (sysLocIndex == -1) {
          WCHAR buf[32];
          if (SUCCEEDED(localizedNames->GetLocaleName(index, buf, 32))) {
            if (loc16.Equals(buf)) {
              sysLocIndex = names.Length();
            }
          }
        }
        names.AppendElement(name);
        // We give the family the least-restrictive visibility of all its
        // localized names, so that the used visibility will not depend on
        // locale; with the exception that if any name is explicitly Hidden,
        // this hides the family as a whole.
        if (visibility != FontVisibility::Hidden) {
          FontVisibility v = visibilityForName(name);
          if (v == FontVisibility::Hidden) {
            visibility = FontVisibility::Hidden;
          } else {
            visibility = std::min(visibility, v);
          }
        }
      }
      // If we didn't find a name that matched the system locale, use the
      // first (which is most often en-US).
      if (sysLocIndex == -1) {
        sysLocIndex = 0;
      }
      // Hack to work around EPSON fonts with bad names (tagged as MacRoman
      // but actually contain MacJapanese data): if we've chosen the first
      // name, *and* it is non-ASCII, *and* there is an alternative present,
      // use the next option instead as being more likely to be valid.
      if (sysLocIndex == 0 && names.Length() > 1 && !IsAscii(names[0])) {
        sysLocIndex = 1;
      }
      for (unsigned index = 0; index < names.Length(); ++index) {
        addFamily(names[index], visibility,
                  index != static_cast<unsigned>(sysLocIndex));
      }
    }
  }
}

void gfxDWriteFontList::GetFacesInitDataForFamily(
    const fontlist::Family* aFamily, nsTArray<fontlist::Face::InitData>& aFaces,
    bool aLoadCmaps) const {
  IDWriteFontCollection* collection =
#ifdef MOZ_BUNDLED_FONTS
      aFamily->IsBundled() ? mBundledFonts : mSystemFonts;
#else
      mSystemFonts;
#endif
  if (!collection) {
    return;
  }
  RefPtr<IDWriteFontFamily> family;
  collection->GetFontFamily(aFamily->Index(), getter_AddRefs(family));
  for (unsigned i = 0; i < family->GetFontCount(); ++i) {
    RefPtr<IDWriteFont> dwFont;
    family->GetFont(i, getter_AddRefs(dwFont));
    if (!dwFont || dwFont->GetSimulations() != DWRITE_FONT_SIMULATIONS_NONE) {
      continue;
    }
    DWRITE_FONT_STYLE dwstyle = dwFont->GetStyle();
    // Ignore italic styles of Meiryo because "Meiryo (Bold) Italic" has
    // non-italic style glyphs as Japanese characters.  However, using it
    // causes serious problem if web pages wants some elements to be
    // different style from others only with font-style.  For example,
    // <em> and <i> should be rendered as italic in the default style.
    if (dwstyle != DWRITE_FONT_STYLE_NORMAL &&
        aFamily->Key().AsString(SharedFontList()).EqualsLiteral("meiryo")) {
      continue;
    }
    WeightRange weight(FontWeight::FromInt(dwFont->GetWeight()));
    StretchRange stretch(FontStretchFromDWriteStretch(dwFont->GetStretch()));
    // Try to read PSName as a unique face identifier; if this fails we'll get
    // it directly from the 'name' table, and if that also fails we consider
    // the face unusable.
    MOZ_SEH_TRY {
      nsAutoCString name;
      RefPtr<gfxCharacterMap> charmap;
      if (FAILED(GetDirectWriteFaceName(dwFont, PSNAME_ID, name)) ||
          aLoadCmaps) {
        RefPtr<IDWriteFontFace> dwFontFace;
        if (SUCCEEDED(dwFont->CreateFontFace(getter_AddRefs(dwFontFace)))) {
          if (name.IsEmpty()) {
            GetPostScriptNameFromNameTable(dwFontFace, name);
          }
          const auto kCMAP =
              NativeEndian::swapToBigEndian(TRUETYPE_TAG('c', 'm', 'a', 'p'));
          const char* data;
          UINT32 size;
          void* context;
          BOOL exists;
          if (aLoadCmaps) {
            if (SUCCEEDED(dwFontFace->TryGetFontTable(
                    kCMAP, (const void**)&data, &size, &context, &exists)) &&
                exists) {
              charmap = new gfxCharacterMap(256);
              uint32_t offset;
              gfxFontUtils::ReadCMAP((const uint8_t*)data, size, *charmap,
                                     offset);
              dwFontFace->ReleaseFontTable(context);
            }
          }
        }
      }
      if (name.IsEmpty()) {
        gfxWarning() << "Failed to get name for face " << i << " in family "
                     << aFamily->Key().AsString(SharedFontList()).get();
        continue;
      }
      SlantStyleRange slant(
          dwstyle == DWRITE_FONT_STYLE_NORMAL   ? FontSlantStyle::NORMAL
          : dwstyle == DWRITE_FONT_STYLE_ITALIC ? FontSlantStyle::ITALIC
                                                : FontSlantStyle::OBLIQUE);
      aFaces.AppendElement(fontlist::Face::InitData{
          name, uint16_t(i), false, weight, stretch, slant, charmap});
    }
    MOZ_SEH_EXCEPT(EXCEPTION_EXECUTE_HANDLER) {
      // Exception (e.g. disk i/o error) occurred when DirectWrite tried to use
      // the font resource. We'll just skip the bad face.
      gfxCriticalNote << "Exception occurred reading faces for "
                      << aFamily->Key().AsString(SharedFontList()).get();
    }
  }
}

bool gfxDWriteFontList::ReadFaceNames(const fontlist::Family* aFamily,
                                      const fontlist::Face* aFace,
                                      nsCString& aPSName,
                                      nsCString& aFullName) {
  IDWriteFontCollection* collection =
#ifdef MOZ_BUNDLED_FONTS
      aFamily->IsBundled() ? mBundledFonts : mSystemFonts;
#else
      mSystemFonts;
#endif
  RefPtr<IDWriteFontFamily> family;
  if (FAILED(collection->GetFontFamily(aFamily->Index(),
                                       getter_AddRefs(family)))) {
    MOZ_ASSERT_UNREACHABLE("failed to get font-family");
    return false;
  }
  RefPtr<IDWriteFont> dwFont;
  if (FAILED(family->GetFont(aFace->mIndex, getter_AddRefs(dwFont)))) {
    MOZ_ASSERT_UNREACHABLE("failed to get font from family");
    return false;
  }
  HRESULT ps = GetDirectWriteFaceName(dwFont, PSNAME_ID, aPSName);
  HRESULT full = GetDirectWriteFaceName(dwFont, FULLNAME_ID, aFullName);
  if (FAILED(ps) || FAILED(full) || aPSName.IsEmpty() || aFullName.IsEmpty()) {
    // We'll return true if either name was found, false if both fail.
    // Note that on older Win7 systems, GetDirectWriteFaceName may "succeed"
    // but return an empty string, so we have to check for non-empty strings
    // to be sure we actually got a usable name.

    // Initialize result to true if either name was already found.
    bool result = (SUCCEEDED(ps) && !aPSName.IsEmpty()) ||
                  (SUCCEEDED(full) && !aFullName.IsEmpty());
    RefPtr<IDWriteFontFace> dwFontFace;
    if (FAILED(dwFont->CreateFontFace(getter_AddRefs(dwFontFace)))) {
      NS_WARNING("failed to create font face");
      return result;
    }
    void* context;
    const char* data;
    UINT32 size;
    BOOL exists;
    if (FAILED(dwFontFace->TryGetFontTable(
            NativeEndian::swapToBigEndian(TRUETYPE_TAG('n', 'a', 'm', 'e')),
            (const void**)&data, &size, &context, &exists)) ||
        !exists) {
      NS_WARNING("failed to get name table");
      return result;
    }
    MOZ_SEH_TRY {
      // Try to read the name table entries, and ensure result is true if either
      // one succeeds.
      if (FAILED(ps) || aPSName.IsEmpty()) {
        if (NS_SUCCEEDED(gfxFontUtils::ReadCanonicalName(
                data, size, gfxFontUtils::NAME_ID_POSTSCRIPT, aPSName))) {
          result = true;
        } else {
          NS_WARNING("failed to read psname");
        }
      }
      if (FAILED(full) || aFullName.IsEmpty()) {
        if (NS_SUCCEEDED(gfxFontUtils::ReadCanonicalName(
                data, size, gfxFontUtils::NAME_ID_FULL, aFullName))) {
          result = true;
        } else {
          NS_WARNING("failed to read fullname");
        }
      }
    }
    MOZ_SEH_EXCEPT(EXCEPTION_EXECUTE_HANDLER) {
      gfxCriticalNote << "Exception occurred reading face names for "
                      << aFamily->Key().AsString(SharedFontList()).get();
    }
    if (dwFontFace && context) {
      dwFontFace->ReleaseFontTable(context);
    }
    return result;
  }
  return true;
}

void gfxDWriteFontList::ReadFaceNamesForFamily(
    fontlist::Family* aFamily, bool aNeedFullnamePostscriptNames) {
  if (!aFamily->IsInitialized()) {
    if (!InitializeFamily(aFamily)) {
      return;
    }
  }
  IDWriteFontCollection* collection =
#ifdef MOZ_BUNDLED_FONTS
      aFamily->IsBundled() ? mBundledFonts : mSystemFonts;
#else
      mSystemFonts;
#endif
  RefPtr<IDWriteFontFamily> family;
  if (FAILED(collection->GetFontFamily(aFamily->Index(),
                                       getter_AddRefs(family)))) {
    return;
  }
  fontlist::FontList* list = SharedFontList();
  const fontlist::Pointer* facePtrs = aFamily->Faces(list);
  nsAutoCString familyName(aFamily->DisplayName().AsString(list));
  nsAutoCString key(aFamily->Key().AsString(list));

  MOZ_SEH_TRY {
    // Read PS-names and fullnames of the faces, and any alternate family names
    // (either localizations or legacy subfamily names)
    for (unsigned i = 0; i < aFamily->NumFaces(); ++i) {
      auto* face = facePtrs[i].ToPtr<fontlist::Face>(list);
      if (!face) {
        continue;
      }
      RefPtr<IDWriteFont> dwFont;
      if (FAILED(family->GetFont(face->mIndex, getter_AddRefs(dwFont)))) {
        continue;
      }
      RefPtr<IDWriteFontFace> dwFontFace;
      if (FAILED(dwFont->CreateFontFace(getter_AddRefs(dwFontFace)))) {
        continue;
      }

      const char* data;
      UINT32 size;
      void* context;
      BOOL exists;
      if (FAILED(dwFontFace->TryGetFontTable(
              NativeEndian::swapToBigEndian(TRUETYPE_TAG('n', 'a', 'm', 'e')),
              (const void**)&data, &size, &context, &exists)) ||
          !exists) {
        continue;
      }

      AutoTArray<nsCString, 4> otherFamilyNames;
      gfxFontUtils::ReadOtherFamilyNamesForFace(familyName, data, size,
                                                otherFamilyNames, false);
      for (const auto& alias : otherFamilyNames) {
        nsAutoCString key(alias);
        ToLowerCase(key);
        auto aliasData = mAliasTable.GetOrInsertNew(key);
        aliasData->InitFromFamily(aFamily, familyName);
        aliasData->mFaces.AppendElement(facePtrs[i]);
      }

      nsAutoCString psname, fullname;
      // Bug 1854090: don't load PSname if the family name ends with ".tmp",
      // as some PDF-related software appears to pollute the font collection
      // with spurious re-encoded versions of standard fonts like Arial, fails
      // to alter the PSname, and thus can result in garbled rendering because
      // the wrong resource may be found via src:local(...).
      if (!StringEndsWith(key, ".tmp"_ns)) {
        if (NS_SUCCEEDED(gfxFontUtils::ReadCanonicalName(
                data, size, gfxFontUtils::NAME_ID_POSTSCRIPT, psname))) {
          ToLowerCase(psname);
          MaybeAddToLocalNameTable(psname,
                                   fontlist::LocalFaceRec::InitData(key, i));
        }
      }
      if (NS_SUCCEEDED(gfxFontUtils::ReadCanonicalName(
              data, size, gfxFontUtils::NAME_ID_FULL, fullname))) {
        ToLowerCase(fullname);
        if (fullname != psname) {
          MaybeAddToLocalNameTable(fullname,
                                   fontlist::LocalFaceRec::InitData(key, i));
        }
      }

      dwFontFace->ReleaseFontTable(context);
    }
  }
  MOZ_SEH_EXCEPT(EXCEPTION_EXECUTE_HANDLER) {
    gfxCriticalNote << "Exception occurred reading names for "
                    << familyName.get();
  }
}

enum DWriteInitError {
  errGDIInterop = 1,
  errSystemFontCollection = 2,
  errNoFonts = 3
};

void gfxDWriteFontList::InitSharedFontListForPlatform() {
  mGDIFontTableAccess = Preferences::GetBool(
      "gfx.font_rendering.directwrite.use_gdi_table_loading", false);
  mForceGDIClassicMaxFontSize = Preferences::GetInt(
      "gfx.font_rendering.cleartype_params.force_gdi_classic_max_size",
      mForceGDIClassicMaxFontSize);

  mSubstitutions.Clear();
  mHardcodedSubstitutions.Clear();
  mNonExistingFonts.Clear();

  RefPtr<IDWriteFactory> factory = Factory::EnsureDWriteFactory();
  HRESULT hr = factory->GetGdiInterop(getter_AddRefs(mGDIInterop));
  if (FAILED(hr)) {
    glean::fontlist::dwritefont_init_problem.AccumulateSingleSample(
        uint32_t(errGDIInterop));
    delete mSharedFontList.exchange(nullptr);
    return;
  }

  mSystemFonts = Factory::GetDWriteSystemFonts(true);
  NS_ASSERTION(mSystemFonts != nullptr, "GetSystemFontCollection failed!");
  if (!mSystemFonts) {
    glean::fontlist::dwritefont_init_problem.AccumulateSingleSample(
        uint32_t(errSystemFontCollection));
    delete mSharedFontList.exchange(nullptr);
    return;
  }
#ifdef MOZ_BUNDLED_FONTS
  // We activate bundled fonts if the pref is > 0 (on) or < 0 (auto), only an
  // explicit value of 0 (off) will disable them.
  TimeStamp start1 = TimeStamp::Now();
  if (StaticPrefs::gfx_bundled_fonts_activate_AtStartup() != 0) {
    mBundledFonts = CreateBundledFontsCollection(factory);
  }
  TimeStamp end1 = TimeStamp::Now();
#endif

  if (XRE_IsParentProcess()) {
    nsAutoCString classicFamilies;
    AutoTArray<nsCString, 16> forceClassicFams;
    nsresult rv = Preferences::GetCString(
        "gfx.font_rendering.cleartype_params.force_gdi_classic_for_families",
        classicFamilies);
    if (NS_SUCCEEDED(rv)) {
      for (auto name :
           nsCCharSeparatedTokenizer(classicFamilies, ',').ToRange()) {
        BuildKeyNameFromFontName(name);
        forceClassicFams.AppendElement(name);
      }
      forceClassicFams.Sort();
    }
    nsTArray<fontlist::Family::InitData> families;
    AppendFamiliesFromCollection(mSystemFonts, families, &forceClassicFams);
#ifdef MOZ_BUNDLED_FONTS
    if (mBundledFonts) {
      TimeStamp start2 = TimeStamp::Now();
      AppendFamiliesFromCollection(mBundledFonts, families);
      TimeStamp end2 = TimeStamp::Now();
      glean::fontlist::bundledfonts_activate.AccumulateRawDuration(
          (end1 - start1) + (end2 - start2));
    }
#endif
    SharedFontList()->SetFamilyNames(families);
    GetPrefsAndStartLoader();
  }

  if (!SharedFontList()->Initialized()) {
    return;
  }

  GetDirectWriteSubstitutes();
  GetFontSubstitutes();
}

nsresult gfxDWriteFontList::InitFontListForPlatform() {
  LARGE_INTEGER frequency;           // ticks per second
  LARGE_INTEGER t1, t2, t3, t4, t5;  // ticks
  double elapsedTime, upTime;
  char nowTime[256], nowDate[256];

  if (LOG_FONTINIT_ENABLED()) {
    GetTimeFormatA(LOCALE_INVARIANT, TIME_FORCE24HOURFORMAT, nullptr, nullptr,
                   nowTime, 256);
    GetDateFormatA(LOCALE_INVARIANT, 0, nullptr, nullptr, nowDate, 256);
    upTime = (double)GetTickCount();
  }
  QueryPerformanceFrequency(&frequency);
  QueryPerformanceCounter(&t1);  // start

  HRESULT hr;
  mGDIFontTableAccess = Preferences::GetBool(
      "gfx.font_rendering.directwrite.use_gdi_table_loading", false);

  mFontSubstitutes.Clear();
  mHardcodedSubstitutes.Clear();
  mNonExistingFonts.Clear();

  RefPtr<IDWriteFactory> factory = Factory::GetDWriteFactory();

  hr = factory->GetGdiInterop(getter_AddRefs(mGDIInterop));
  if (FAILED(hr)) {
    glean::fontlist::dwritefont_init_problem.AccumulateSingleSample(
        uint32_t(errGDIInterop));
    return NS_ERROR_FAILURE;
  }

  QueryPerformanceCounter(&t2);  // base-class/interop initialization

  mSystemFonts = Factory::GetDWriteSystemFonts(true);
  NS_ASSERTION(mSystemFonts != nullptr, "GetSystemFontCollection failed!");

  if (!mSystemFonts) {
    glean::fontlist::dwritefont_init_problem.AccumulateSingleSample(
        uint32_t(errSystemFontCollection));
    return NS_ERROR_FAILURE;
  }

#ifdef MOZ_BUNDLED_FONTS
  // Get bundled fonts before the system collection, so that in the case of
  // duplicate names, we have recorded the family as bundled (and therefore
  // available regardless of visibility settings).
  // We activate bundled fonts if the pref is > 0 (on) or < 0 (auto), only an
  // explicit value of 0 (off) will disable them.
  if (StaticPrefs::gfx_bundled_fonts_activate_AtStartup() != 0) {
    auto timerId = glean::fontlist::bundledfonts_activate.Start();
    mBundledFonts = CreateBundledFontsCollection(factory);
    if (mBundledFonts) {
      GetFontsFromCollection(mBundledFonts);
    }
    glean::fontlist::bundledfonts_activate.StopAndAccumulate(
        std::move(timerId));
  }
#endif
  const uint32_t kBundledCount = mFontFamilies.Count();

  QueryPerformanceCounter(&t3);  // system font collection

  GetFontsFromCollection(mSystemFonts);

  // if no fonts found, something is out of whack, bail and use GDI backend
  NS_ASSERTION(mFontFamilies.Count() > kBundledCount,
               "no fonts found in the system fontlist -- holy crap batman!");
  if (mFontFamilies.Count() == kBundledCount) {
    glean::fontlist::dwritefont_init_problem.AccumulateSingleSample(
        uint32_t(errNoFonts));
    return NS_ERROR_FAILURE;
  }

  QueryPerformanceCounter(&t4);  // iterate over system fonts

  mOtherFamilyNamesInitialized = true;
  GetFontSubstitutes();

  // bug 642093 - DirectWrite does not support old bitmap (.fon)
  // font files, but a few of these such as "Courier" and "MS Sans Serif"
  // are frequently specified in shoddy CSS, without appropriate fallbacks.
  // By mapping these to TrueType equivalents, we provide better consistency
  // with both pre-DW systems and with IE9, which appears to do the same.
  GetDirectWriteSubstitutes();

  // bug 551313 - DirectWrite creates a Gill Sans family out of
  // poorly named members of the Gill Sans MT family containing
  // only Ultra Bold weights.  This causes big problems for pages
  // using Gill Sans which is usually only available on OSX

  nsAutoCString nameGillSans("Gill Sans");
  nsAutoCString nameGillSansMT("Gill Sans MT");
  BuildKeyNameFromFontName(nameGillSans);
  BuildKeyNameFromFontName(nameGillSansMT);

  gfxFontFamily* gillSansFamily = mFontFamilies.GetWeak(nameGillSans);
  gfxFontFamily* gillSansMTFamily = mFontFamilies.GetWeak(nameGillSansMT);

  if (gillSansFamily && gillSansMTFamily) {
    gillSansFamily->FindStyleVariations();

    gillSansFamily->ReadLock();
    const auto& faces = gillSansFamily->GetFontList();

    bool allUltraBold = true;
    for (const auto& face : faces) {
      // does the face have 'Ultra Bold' in the name?
      if (face->Name().Find("Ultra Bold"_ns) == -1) {
        allUltraBold = false;
        break;
      }
    }

    // if all the Gill Sans faces are Ultra Bold ==> move faces
    // for Gill Sans into Gill Sans MT family
    if (allUltraBold) {
      // add faces to Gill Sans MT
      for (const auto& face : faces) {
        // change the entry's family name to match its adoptive family
        face->mFamilyName = gillSansMTFamily->Name();
        gillSansMTFamily->AddFontEntry(face);

        if (LOG_FONTLIST_ENABLED()) {
          nsAutoCString weightString;
          face->Weight().ToString(weightString);
          LOG_FONTLIST(
              ("(fontlist) moved (%s) to family (%s)"
               " with style: %s weight: %s stretch: %d",
               face->Name().get(), gillSansMTFamily->Name().get(),
               (face->IsItalic()) ? "italic"
                                  : (face->IsOblique() ? "oblique" : "normal"),
               weightString.get(), face->Stretch().AsScalar()));
        }
      }
      gillSansFamily->ReadUnlock();

      // remove Gill Sans
      mFontFamilies.Remove(nameGillSans);
    } else {
      gillSansFamily->ReadUnlock();
    }
  }

  nsAutoCString classicFamilies;
  nsresult rv = Preferences::GetCString(
      "gfx.font_rendering.cleartype_params.force_gdi_classic_for_families",
      classicFamilies);
  if (NS_SUCCEEDED(rv)) {
    nsCCharSeparatedTokenizer tokenizer(classicFamilies, ',');
    while (tokenizer.hasMoreTokens()) {
      nsAutoCString name(tokenizer.nextToken());
      BuildKeyNameFromFontName(name);
      gfxFontFamily* family = mFontFamilies.GetWeak(name);
      if (family) {
        static_cast<gfxDWriteFontFamily*>(family)->SetForceGDIClassic(true);
      }
    }
  }
  mForceGDIClassicMaxFontSize = Preferences::GetInt(
      "gfx.font_rendering.cleartype_params.force_gdi_classic_max_size",
      mForceGDIClassicMaxFontSize);

  GetPrefsAndStartLoader();

  QueryPerformanceCounter(&t5);  // misc initialization

  if (LOG_FONTINIT_ENABLED()) {
    // determine dwrite version
    nsAutoString dwriteVers;
    gfxWindowsPlatform::GetDLLVersion(L"dwrite.dll", dwriteVers);
    LOG_FONTINIT(("(fontinit) Start: %s %s\n", nowDate, nowTime));
    LOG_FONTINIT(("(fontinit) Uptime: %9.3f s\n", upTime / 1000));
    LOG_FONTINIT(("(fontinit) dwrite version: %s\n",
                  NS_ConvertUTF16toUTF8(dwriteVers).get()));
  }

  elapsedTime = (t5.QuadPart - t1.QuadPart) * 1000.0 / frequency.QuadPart;
  glean::fontlist::dwritefont_delayedinit_total.AccumulateRawDuration(
      TimeDuration::FromMilliseconds(elapsedTime));
  glean::fontlist::dwritefont_delayedinit_count.AccumulateSingleSample(
      mSystemFonts->GetFontFamilyCount());
  LOG_FONTINIT((
      "(fontinit) Total time in InitFontList:    %9.3f ms (families: %d, %s)\n",
      elapsedTime, mSystemFonts->GetFontFamilyCount(),
      (mGDIFontTableAccess ? "gdi table access" : "dwrite table access")));

  elapsedTime = (t2.QuadPart - t1.QuadPart) * 1000.0 / frequency.QuadPart;
  LOG_FONTINIT(
      ("(fontinit)  --- base/interop obj initialization init: %9.3f ms\n",
       elapsedTime));

  elapsedTime = (t3.QuadPart - t2.QuadPart) * 1000.0 / frequency.QuadPart;
  glean::fontlist::dwritefont_delayedinit_collect.AccumulateRawDuration(
      TimeDuration::FromMilliseconds(elapsedTime));
  LOG_FONTINIT(
      ("(fontinit)  --- GetSystemFontCollection:  %9.3f ms\n", elapsedTime));

  elapsedTime = (t4.QuadPart - t3.QuadPart) * 1000.0 / frequency.QuadPart;
  LOG_FONTINIT(
      ("(fontinit)  --- iterate over families:    %9.3f ms\n", elapsedTime));

  elapsedTime = (t5.QuadPart - t4.QuadPart) * 1000.0 / frequency.QuadPart;
  LOG_FONTINIT(
      ("(fontinit)  --- misc initialization:    %9.3f ms\n", elapsedTime));

  return NS_OK;
}

void gfxDWriteFontList::GetFontsFromCollection(
    IDWriteFontCollection* aCollection) {
  for (UINT32 i = 0; i < aCollection->GetFontFamilyCount(); i++) {
    RefPtr<IDWriteFontFamily> family;
    aCollection->GetFontFamily(i, getter_AddRefs(family));

    RefPtr<IDWriteLocalizedStrings> names;
    HRESULT hr = family->GetFamilyNames(getter_AddRefs(names));
    if (FAILED(hr)) {
      continue;
    }

    nsAutoCString name;
    if (!GetEnglishOrFirstName(name, names)) {
      continue;
    }
    nsAutoCString familyName(
        name);  // keep a copy before we lowercase it as a key

    BuildKeyNameFromFontName(name);

    RefPtr<gfxFontFamily> fam;

    if (mFontFamilies.GetWeak(name)) {
      continue;
    }

    FontVisibility visibility = aCollection == mSystemFonts
                                    ? GetVisibilityForFamily(familyName)
                                    : FontVisibility::Base;

    fam = new gfxDWriteFontFamily(familyName, visibility, family,
                                  aCollection == mSystemFonts);
    if (!fam) {
      continue;
    }

    if (mBadUnderlineFamilyNames.ContainsSorted(name)) {
      fam->SetBadUnderlineFamily();
    }
    mFontFamilies.InsertOrUpdate(name, RefPtr{fam});

    // now add other family name localizations, if present
    uint32_t nameCount = names->GetCount();
    uint32_t nameIndex;

    if (nameCount > 1) {
      UINT32 englishIdx = 0;
      BOOL exists;
      // if this fails/doesn't exist, we'll have used name index 0,
      // so that's the one we'll want to skip here
      names->FindLocaleName(L"en-us", &englishIdx, &exists);
      AutoTArray<nsCString, 4> otherFamilyNames;
      for (nameIndex = 0; nameIndex < nameCount; nameIndex++) {
        UINT32 nameLen;
        AutoTArray<WCHAR, 32> localizedName;

        // only add other names
        if (nameIndex == englishIdx) {
          continue;
        }

        hr = names->GetStringLength(nameIndex, &nameLen);
        if (FAILED(hr)) {
          continue;
        }

        if (!localizedName.SetLength(nameLen + 1, fallible)) {
          continue;
        }

        hr = names->GetString(nameIndex, localizedName.Elements(), nameLen + 1);
        if (FAILED(hr)) {
          continue;
        }

        NS_ConvertUTF16toUTF8 locName(localizedName.Elements());

        if (!familyName.Equals(locName)) {
          otherFamilyNames.AppendElement(locName);
        }
      }
      if (!otherFamilyNames.IsEmpty()) {
        AddOtherFamilyNames(fam, otherFamilyNames);
      }
    }

    // at this point, all family names have been read in
    fam->SetOtherFamilyNamesInitialized();
  }
}

static void RemoveCharsetFromFontSubstitute(nsACString& aName) {
  int32_t comma = aName.FindChar(',');
  if (comma >= 0) aName.Truncate(comma);
}

#define MAX_VALUE_NAME 512
#define MAX_VALUE_DATA 512

nsresult gfxDWriteFontList::GetFontSubstitutes() {
  for (const FontSubstitute& fs : kFontSubstitutes) {
    nsAutoCString substituteName(fs.substituteName);
    nsAutoCString actualFontName(fs.actualFontName);
    BuildKeyNameFromFontName(substituteName);
    BuildKeyNameFromFontName(actualFontName);
    AddSubstitute(substituteName, actualFontName, true);
  }

  std::unique_ptr<HKEY, RegCloseKeyDeleter> hKey;
  DWORD i, rv, lenAlias, lenActual, valueType;
  WCHAR aliasName[MAX_VALUE_NAME];
  WCHAR actualName[MAX_VALUE_DATA];

  if (RegOpenKeyExW(
          HKEY_LOCAL_MACHINE,
          L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion\\FontSubstitutes",
          0, KEY_READ, getter_Transfers(hKey)) != ERROR_SUCCESS) {
    return NS_ERROR_FAILURE;
  }

  for (i = 0, rv = ERROR_SUCCESS; rv != ERROR_NO_MORE_ITEMS; i++) {
    aliasName[0] = 0;
    lenAlias = std::size(aliasName);
    actualName[0] = 0;
    lenActual = sizeof(actualName);
    rv = RegEnumValueW(hKey.get(), i, aliasName, &lenAlias, nullptr, &valueType,
                       (LPBYTE)actualName, &lenActual);

    if (rv != ERROR_SUCCESS || valueType != REG_SZ || lenAlias == 0) {
      continue;
    }

    if (aliasName[0] == WCHAR('@')) {
      continue;
    }

    NS_ConvertUTF16toUTF8 substituteName((char16_t*)aliasName);
    NS_ConvertUTF16toUTF8 actualFontName((char16_t*)actualName);
    RemoveCharsetFromFontSubstitute(substituteName);
    BuildKeyNameFromFontName(substituteName);
    RemoveCharsetFromFontSubstitute(actualFontName);
    BuildKeyNameFromFontName(actualFontName);
    AddSubstitute(substituteName, actualFontName, false);
  }

  return NS_OK;
}

void gfxDWriteFontList::AddSubstitute(const nsCString& aSubstituteName,
                                      const nsCString& aActualFontName,
                                      bool aIsHardcoded) {
  if (SharedFontList()) {
    auto& substitutions =
        aIsHardcoded ? mHardcodedSubstitutions : mSubstitutions;
    // Skip substitution if the original font is available, unless the
    // option to apply substitutions unconditionally is enabled.
    if (!StaticPrefs::gfx_windows_font_substitutes_always_AtStartup()) {
      // Font substitutions are recorded for the canonical family names;
      // we don't need FindFamily to consider localized aliases when
      // searching.
      if (SharedFontList()->FindFamily(aSubstituteName,
                                       /*aPrimaryNameOnly*/ true)) {
        return;
      }
    }
    if (SharedFontList()->FindFamily(aActualFontName,
                                     /*aPrimaryNameOnly*/ true)) {
      substitutions.InsertOrUpdate(aSubstituteName,
                                   MakeUnique<nsCString>(aActualFontName));
    } else if (substitutions.Get(aActualFontName)) {
      substitutions.InsertOrUpdate(
          aSubstituteName,
          MakeUnique<nsCString>(*substitutions.Get(aActualFontName)));
    } else {
      mNonExistingFonts.AppendElement(aSubstituteName);
    }
  } else {
    gfxFontFamily* ff;
    if (!aActualFontName.IsEmpty() &&
        (ff = mFontFamilies.GetWeak(aActualFontName))) {
      if (aIsHardcoded) {
        mHardcodedSubstitutes.InsertOrUpdate(aSubstituteName, RefPtr{ff});
      } else {
        mFontSubstitutes.InsertOrUpdate(aSubstituteName, RefPtr{ff});
      }
    } else {
      mNonExistingFonts.AppendElement(aSubstituteName);
    }
  }
}

struct FontSubstitution {
  const char* aliasName;
  const char* actualName;
};

static const FontSubstitution sDirectWriteSubs[] = {
    {"MS Sans Serif", "Microsoft Sans Serif"},
    {"MS Serif", "Times New Roman"},
    {"Courier", "Courier New"},
    {"Small Fonts", "Arial"},
    {"Roman", "Times New Roman"},
    {"Script", "Mistral"}};

void gfxDWriteFontList::GetDirectWriteSubstitutes() {
  for (uint32_t i = 0; i < std::size(sDirectWriteSubs); ++i) {
    const FontSubstitution& sub(sDirectWriteSubs[i]);
    nsAutoCString substituteName(sub.aliasName);
    BuildKeyNameFromFontName(substituteName);
    if (SharedFontList()) {
      // Skip substitution if the original font is available, unless the option
      // to apply substitutions unconditionally is enabled.
      if (!StaticPrefs::gfx_windows_font_substitutes_always_AtStartup()) {
        // We don't need FindFamily to consider localized aliases when searching
        // for the DirectWrite substitutes, we know the canonical names.
        if (SharedFontList()->FindFamily(substituteName,
                                         /*aPrimaryNameOnly*/ true)) {
          continue;
        }
      }
      nsAutoCString actualFontName(sub.actualName);
      BuildKeyNameFromFontName(actualFontName);
      if (SharedFontList()->FindFamily(actualFontName,
                                       /*aPrimaryNameOnly*/ true)) {
        mSubstitutions.InsertOrUpdate(substituteName,
                                      MakeUnique<nsCString>(actualFontName));
        mHardcodedSubstitutions.InsertOrUpdate(
            substituteName, MakeUnique<nsCString>(actualFontName));
      } else {
        mNonExistingFonts.AppendElement(substituteName);
      }
    } else {
      if (nullptr != mFontFamilies.GetWeak(substituteName)) {
        // don't do the substitution if user actually has a usable font
        // with this name installed
        continue;
      }
      nsAutoCString actualFontName(sub.actualName);
      BuildKeyNameFromFontName(actualFontName);
      gfxFontFamily* ff;
      if (nullptr != (ff = mFontFamilies.GetWeak(actualFontName))) {
        mFontSubstitutes.InsertOrUpdate(substituteName, RefPtr{ff});
        mHardcodedSubstitutes.InsertOrUpdate(substituteName, RefPtr{ff});
      } else {
        mNonExistingFonts.AppendElement(substituteName);
      }
    }
  }
}

bool gfxDWriteFontList::FindAndAddFamiliesLocked(
    FontVisibilityProvider* aFontVisibilityProvider,
    StyleGenericFontFamily aGeneric, const nsACString& aFamily,
    nsTArray<FamilyAndGeneric>* aOutput, FindFamiliesFlags aFlags,
    gfxFontStyle* aStyle, nsAtom* aLanguage, gfxFloat aDevToCssSize) {
  nsAutoCString keyName(aFamily);
  BuildKeyNameFromFontName(keyName);

  const bool useHardcodedList =
      aFontVisibilityProvider
          ? aFontVisibilityProvider->ShouldResistFingerprinting(
                RFPTarget::UseHardcodedFontSubstitutes)
          : nsContentUtils::ShouldResistFingerprinting(
                "aFontVisibilityProvider is not available",
                RFPTarget::UseHardcodedFontSubstitutes);
  if (SharedFontList()) {
    nsACString* subst = useHardcodedList ? mHardcodedSubstitutions.Get(keyName)
                                         : mSubstitutions.Get(keyName);
    if (subst) {
      keyName = *subst;
    }
  } else {
    gfxFontFamily* ff = useHardcodedList
                            ? mHardcodedSubstitutes.GetWeak(keyName)
                            : mFontSubstitutes.GetWeak(keyName);
    FontVisibility level = aFontVisibilityProvider
                               ? aFontVisibilityProvider->GetFontVisibility()
                               : FontVisibility::User;
    if (ff && IsVisibleToCSS(*ff, level)) {
      aOutput->AppendElement(FamilyAndGeneric(ff, aGeneric));
      return true;
    }
  }

  if (mNonExistingFonts.Contains(keyName)) {
    return false;
  }

  return gfxPlatformFontList::FindAndAddFamiliesLocked(
      aFontVisibilityProvider, aGeneric, keyName, aOutput, aFlags, aStyle,
      aLanguage, aDevToCssSize);
}

void gfxDWriteFontList::AddSizeOfExcludingThis(MallocSizeOf aMallocSizeOf,
                                               FontListSizes* aSizes) const {
  gfxPlatformFontList::AddSizeOfExcludingThis(aMallocSizeOf, aSizes);

  AutoLock lock(mLock);

  // We are a singleton, so include the font loader singleton's memory.
  MOZ_ASSERT(static_cast<const gfxPlatformFontList*>(this) ==
             gfxPlatformFontList::PlatformFontList());
  gfxDWriteFontFileLoader* loader = static_cast<gfxDWriteFontFileLoader*>(
      gfxDWriteFontFileLoader::Instance());
  aSizes->mLoaderSize += loader->SizeOfIncludingThis(aMallocSizeOf);

  aSizes->mFontListSize +=
      SizeOfFontFamilyTableExcludingThis(mFontSubstitutes, aMallocSizeOf);
  aSizes->mFontListSize +=
      SizeOfFontFamilyTableExcludingThis(mHardcodedSubstitutes, aMallocSizeOf);

  aSizes->mFontListSize +=
      mNonExistingFonts.ShallowSizeOfExcludingThis(aMallocSizeOf);
  for (uint32_t i = 0; i < mNonExistingFonts.Length(); ++i) {
    aSizes->mFontListSize +=
        mNonExistingFonts[i].SizeOfExcludingThisIfUnshared(aMallocSizeOf);
  }
}

void gfxDWriteFontList::AddSizeOfIncludingThis(MallocSizeOf aMallocSizeOf,
                                               FontListSizes* aSizes) const {
  aSizes->mFontListSize += aMallocSizeOf(this);
  AddSizeOfExcludingThis(aMallocSizeOf, aSizes);
}

static HRESULT GetFamilyName(IDWriteFont* aFont, nsCString& aFamilyName) {
  HRESULT hr;
  RefPtr<IDWriteFontFamily> family;

  // clean out previous value
  aFamilyName.Truncate();

  hr = aFont->GetFontFamily(getter_AddRefs(family));
  if (FAILED(hr)) {
    return hr;
  }

  RefPtr<IDWriteLocalizedStrings> familyNames;

  hr = family->GetFamilyNames(getter_AddRefs(familyNames));
  if (FAILED(hr)) {
    return hr;
  }

  if (!GetEnglishOrFirstName(aFamilyName, familyNames)) {
    return E_FAIL;
  }

  return S_OK;
}

// bug 705594 - the method below doesn't actually do any "drawing", it's only
// used to invoke the DirectWrite layout engine to determine the fallback font
// for a given character.

IFACEMETHODIMP DWriteFontFallbackRenderer::DrawGlyphRun(
    void* clientDrawingContext, FLOAT baselineOriginX, FLOAT baselineOriginY,
    DWRITE_MEASURING_MODE measuringMode, DWRITE_GLYPH_RUN const* glyphRun,
    DWRITE_GLYPH_RUN_DESCRIPTION const* glyphRunDescription,
    IUnknown* clientDrawingEffect) {
  if (!mSystemFonts) {
    return E_FAIL;
  }

  HRESULT hr = S_OK;

  RefPtr<IDWriteFont> font;
  hr = mSystemFonts->GetFontFromFontFace(glyphRun->fontFace,
                                         getter_AddRefs(font));
  if (FAILED(hr)) {
    return hr;
  }

  // copy the family name
  hr = GetFamilyName(font, mFamilyName);
  if (FAILED(hr)) {
    return hr;
  }

  // Arial is used as the default fallback font
  // so if it matches ==> no font found
  if (mFamilyName.EqualsLiteral("Arial")) {
    mFamilyName.Truncate();
    return E_FAIL;
  }
  return hr;
}

gfxFontEntry* gfxDWriteFontList::PlatformGlobalFontFallback(
    FontVisibilityProvider* aFontVisibilityProvider, const uint32_t aCh,
    Script aRunScript, const gfxFontStyle* aMatchStyle,
    FontFamily& aMatchedFamily) {
  HRESULT hr;

  RefPtr<IDWriteFactory> dwFactory = Factory::GetDWriteFactory();
  if (!dwFactory) {
    return nullptr;
  }

  // initialize fallback renderer
  if (!mFallbackRenderer) {
    mFallbackRenderer = new DWriteFontFallbackRenderer(dwFactory);
  }
  if (!mFallbackRenderer->IsValid()) {
    return nullptr;
  }

  // initialize text format
  if (!mFallbackFormat) {
    hr = dwFactory->CreateTextFormat(
        L"Arial", nullptr, DWRITE_FONT_WEIGHT_REGULAR, DWRITE_FONT_STYLE_NORMAL,
        DWRITE_FONT_STRETCH_NORMAL, 72.0f, L"en-us",
        getter_AddRefs(mFallbackFormat));
    if (FAILED(hr)) {
      return nullptr;
    }
  }

  // set up string with fallback character
  wchar_t str[16];
  uint32_t strLen;

  if (IS_IN_BMP(aCh)) {
    str[0] = static_cast<wchar_t>(aCh);
    str[1] = 0;
    strLen = 1;
  } else {
    str[0] = static_cast<wchar_t>(H_SURROGATE(aCh));
    str[1] = static_cast<wchar_t>(L_SURROGATE(aCh));
    str[2] = 0;
    strLen = 2;
  }

  // set up layout
  RefPtr<IDWriteTextLayout> fallbackLayout;

  hr = dwFactory->CreateTextLayout(str, strLen, mFallbackFormat, 200.0f, 200.0f,
                                   getter_AddRefs(fallbackLayout));
  if (FAILED(hr)) {
    return nullptr;
  }

  // call the draw method to invoke the DirectWrite layout functions
  // which determine the fallback font
  MOZ_SEH_TRY {
    hr = fallbackLayout->Draw(nullptr, mFallbackRenderer, 50.0f, 50.0f);
    if (FAILED(hr)) {
      return nullptr;
    }
  }
  MOZ_SEH_EXCEPT(EXCEPTION_EXECUTE_HANDLER) {
    gfxCriticalNote << "Exception occurred during DWrite font fallback";
    return nullptr;
  }

  FontFamily family = FindFamily(aFontVisibilityProvider,
                                 mFallbackRenderer->FallbackFamilyName());
  if (!family.IsNull()) {
    gfxFontEntry* fontEntry = nullptr;
    if (family.mShared) {
      auto face =
          family.mShared->FindFaceForStyle(SharedFontList(), *aMatchStyle);
      if (face) {
        fontEntry = GetOrCreateFontEntry(face, family.mShared);
      }
    } else {
      fontEntry = family.mUnshared->FindFontForStyle(*aMatchStyle);
    }
    if (fontEntry && fontEntry->HasCharacter(aCh)) {
      aMatchedFamily = family;
      return fontEntry;
    }
    glean::fontlist::bad_fallback_font
        .EnumGet(glean::fontlist::BadFallbackFontLabel::eTrue)
        .Add();
  }

  return nullptr;
}

// used to load system-wide font info on off-main thread
class DirectWriteFontInfo : public FontInfoData {
 public:
  DirectWriteFontInfo(bool aLoadOtherNames, bool aLoadFaceNames,
                      bool aLoadCmaps, IDWriteFontCollection* aSystemFonts
#ifdef MOZ_BUNDLED_FONTS
                      ,
                      IDWriteFontCollection* aBundledFonts
#endif
                      )
      : FontInfoData(aLoadOtherNames, aLoadFaceNames, aLoadCmaps),
        mSystemFonts(aSystemFonts)
#ifdef MOZ_BUNDLED_FONTS
        ,
        mBundledFonts(aBundledFonts)
#endif
  {
  }

  virtual ~DirectWriteFontInfo() = default;

  // loads font data for all members of a given family
  virtual void LoadFontFamilyData(const nsACString& aFamilyName);

 private:
  RefPtr<IDWriteFontCollection> mSystemFonts;
#ifdef MOZ_BUNDLED_FONTS
  RefPtr<IDWriteFontCollection> mBundledFonts;
#endif
};

void DirectWriteFontInfo::LoadFontFamilyData(const nsACString& aFamilyName) {
  // lookup the family
  NS_ConvertUTF8toUTF16 famName(aFamilyName);

  HRESULT hr;
  BOOL exists = false;

  uint32_t index;
  RefPtr<IDWriteFontFamily> family;
  hr = mSystemFonts->FindFamilyName((const wchar_t*)famName.get(), &index,
                                    &exists);
  if (SUCCEEDED(hr) && exists) {
    mSystemFonts->GetFontFamily(index, getter_AddRefs(family));
    if (!family) {
      return;
    }
  }

#ifdef MOZ_BUNDLED_FONTS
  if (!family && mBundledFonts) {
    hr = mBundledFonts->FindFamilyName((const wchar_t*)famName.get(), &index,
                                       &exists);
    if (SUCCEEDED(hr) && exists) {
      mBundledFonts->GetFontFamily(index, getter_AddRefs(family));
    }
  }
#endif

  if (!family) {
    return;
  }

  // later versions of DirectWrite support querying the fullname/psname
  bool loadFaceNamesUsingDirectWrite = mLoadFaceNames;

  for (uint32_t i = 0; i < family->GetFontCount(); i++) {
    // get the font
    RefPtr<IDWriteFont> dwFont;
    hr = family->GetFont(i, getter_AddRefs(dwFont));
    if (FAILED(hr)) {
      // This should never happen.
      NS_WARNING("Failed to get existing font from family.");
      continue;
    }

    if (dwFont->GetSimulations() != DWRITE_FONT_SIMULATIONS_NONE) {
      // We don't want these in the font list; we'll apply simulations
      // on the fly when appropriate.
      continue;
    }

    mLoadStats.fonts++;

    // get the name of the face
    nsCString fullID(aFamilyName);
    nsAutoCString fontName;
    hr = GetDirectWriteFontName(dwFont, fontName);
    if (FAILED(hr)) {
      continue;
    }
    fullID.Append(' ');
    fullID.Append(fontName);

    FontFaceData fontData;
    bool haveData = true;
    RefPtr<IDWriteFontFace> dwFontFace;

    if (mLoadFaceNames) {
      // try to load using DirectWrite first
      if (loadFaceNamesUsingDirectWrite) {
        hr =
            GetDirectWriteFaceName(dwFont, PSNAME_ID, fontData.mPostscriptName);
        if (FAILED(hr)) {
          loadFaceNamesUsingDirectWrite = false;
        }
        hr = GetDirectWriteFaceName(dwFont, FULLNAME_ID, fontData.mFullName);
        if (FAILED(hr)) {
          loadFaceNamesUsingDirectWrite = false;
        }
      }

      // if DirectWrite read fails, load directly from name table
      if (!loadFaceNamesUsingDirectWrite) {
        hr = dwFont->CreateFontFace(getter_AddRefs(dwFontFace));
        if (SUCCEEDED(hr)) {
          uint32_t kNAME =
              NativeEndian::swapToBigEndian(TRUETYPE_TAG('n', 'a', 'm', 'e'));
          const char* nameData;
          BOOL exists;
          void* ctx;
          uint32_t nameSize;

          hr = dwFontFace->TryGetFontTable(kNAME, (const void**)&nameData,
                                           &nameSize, &ctx, &exists);
          if (SUCCEEDED(hr) && nameData && nameSize > 0) {
            MOZ_SEH_TRY {
              gfxFontUtils::ReadCanonicalName(nameData, nameSize,
                                              gfxFontUtils::NAME_ID_FULL,
                                              fontData.mFullName);
              gfxFontUtils::ReadCanonicalName(nameData, nameSize,
                                              gfxFontUtils::NAME_ID_POSTSCRIPT,
                                              fontData.mPostscriptName);
            }
            MOZ_SEH_EXCEPT(EXCEPTION_EXECUTE_HANDLER) {
              gfxCriticalNote << "Exception occurred reading names for "
                              << PromiseFlatCString(aFamilyName).get();
            }
            dwFontFace->ReleaseFontTable(ctx);
          }
        }
      }

      haveData =
          !fontData.mPostscriptName.IsEmpty() || !fontData.mFullName.IsEmpty();
      if (haveData) {
        mLoadStats.facenames++;
      }
    }

    // cmaps
    if (mLoadCmaps) {
      if (!dwFontFace) {
        hr = dwFont->CreateFontFace(getter_AddRefs(dwFontFace));
        if (!SUCCEEDED(hr)) {
          continue;
        }
      }

      uint32_t kCMAP =
          NativeEndian::swapToBigEndian(TRUETYPE_TAG('c', 'm', 'a', 'p'));
      const uint8_t* cmapData;
      BOOL exists;
      void* ctx;
      uint32_t cmapSize;

      hr = dwFontFace->TryGetFontTable(kCMAP, (const void**)&cmapData,
                                       &cmapSize, &ctx, &exists);

      if (SUCCEEDED(hr) && exists) {
        bool cmapLoaded = false;
        RefPtr<gfxCharacterMap> charmap = new gfxCharacterMap(256);
        uint32_t offset;
        MOZ_SEH_TRY {
          if (cmapData && cmapSize > 0 &&
              NS_SUCCEEDED(gfxFontUtils::ReadCMAP(cmapData, cmapSize, *charmap,
                                                  offset))) {
            fontData.mCharacterMap = charmap;
            fontData.mUVSOffset = offset;
            cmapLoaded = true;
            mLoadStats.cmaps++;
          }
        }
        MOZ_SEH_EXCEPT(EXCEPTION_EXECUTE_HANDLER) {
          gfxCriticalNote << "Exception occurred reading cmaps for "
                          << PromiseFlatCString(aFamilyName).get();
        }
        dwFontFace->ReleaseFontTable(ctx);
        haveData = haveData || cmapLoaded;
      }
    }

    // if have data, load
    if (haveData) {
      mFontFaceData.InsertOrUpdate(fullID, fontData);
    }
  }
}

already_AddRefed<FontInfoData> gfxDWriteFontList::CreateFontInfoData() {
  bool loadCmaps = !UsesSystemFallback() ||
                   gfxPlatform::GetPlatform()->UseCmapsDuringSystemFallback();

  RefPtr<DirectWriteFontInfo> fi = new DirectWriteFontInfo(
      false, NeedFullnamePostscriptNames(), loadCmaps, mSystemFonts
#ifdef MOZ_BUNDLED_FONTS
      ,
      mBundledFonts
#endif
  );

  return fi.forget();
}

gfxFontFamily* gfxDWriteFontList::CreateFontFamily(
    const nsACString& aName, FontVisibility aVisibility) const {
  return new gfxDWriteFontFamily(aName, aVisibility, nullptr);
}

#ifdef MOZ_BUNDLED_FONTS

#  define IMPL_QI_FOR_DWRITE(_interface)                             \
   public:                                                           \
    IFACEMETHOD(QueryInterface)(IID const& riid, void** ppvObject) { \
      if (__uuidof(_interface) == riid) {                            \
        *ppvObject = this;                                           \
      } else if (__uuidof(IUnknown) == riid) {                       \
        *ppvObject = this;                                           \
      } else {                                                       \
        *ppvObject = nullptr;                                        \
        return E_NOINTERFACE;                                        \
      }                                                              \
      this->AddRef();                                                \
      return S_OK;                                                   \
    }

class BundledFontFileEnumerator : public IDWriteFontFileEnumerator {
  IMPL_QI_FOR_DWRITE(IDWriteFontFileEnumerator)

  NS_INLINE_DECL_REFCOUNTING(BundledFontFileEnumerator)

 public:
  BundledFontFileEnumerator(IDWriteFactory* aFactory, nsIFile* aFontDir);

  IFACEMETHODIMP MoveNext(BOOL* hasCurrentFile);

  IFACEMETHODIMP GetCurrentFontFile(IDWriteFontFile** fontFile);

 private:
  BundledFontFileEnumerator() = delete;
  BundledFontFileEnumerator(const BundledFontFileEnumerator&) = delete;
  BundledFontFileEnumerator& operator=(const BundledFontFileEnumerator&) =
      delete;
  virtual ~BundledFontFileEnumerator() = default;

  RefPtr<IDWriteFactory> mFactory;

  nsCOMPtr<nsIFile> mFontDir;
  nsCOMPtr<nsIDirectoryEnumerator> mEntries;
  nsCOMPtr<nsISupports> mCurrent;
};

BundledFontFileEnumerator::BundledFontFileEnumerator(IDWriteFactory* aFactory,
                                                     nsIFile* aFontDir)
    : mFactory(aFactory), mFontDir(aFontDir) {
  mFontDir->GetDirectoryEntries(getter_AddRefs(mEntries));
}

IFACEMETHODIMP
BundledFontFileEnumerator::MoveNext(BOOL* aHasCurrentFile) {
  bool hasMore = false;
  if (mEntries) {
    if (NS_SUCCEEDED(mEntries->HasMoreElements(&hasMore)) && hasMore) {
      if (NS_SUCCEEDED(mEntries->GetNext(getter_AddRefs(mCurrent)))) {
        hasMore = true;
      }
    }
  }
  *aHasCurrentFile = hasMore;
  return S_OK;
}

IFACEMETHODIMP
BundledFontFileEnumerator::GetCurrentFontFile(IDWriteFontFile** aFontFile) {
  nsCOMPtr<nsIFile> file = do_QueryInterface(mCurrent);
  if (!file) {
    return E_FAIL;
  }
  nsString path;
  if (NS_FAILED(file->GetPath(path))) {
    return E_FAIL;
  }
  return mFactory->CreateFontFileReference((const WCHAR*)path.get(), nullptr,
                                           aFontFile);
}

class BundledFontLoader : public IDWriteFontCollectionLoader {
  IMPL_QI_FOR_DWRITE(IDWriteFontCollectionLoader)

  NS_INLINE_DECL_REFCOUNTING(BundledFontLoader)

 public:
  BundledFontLoader() {}

  IFACEMETHODIMP CreateEnumeratorFromKey(
      IDWriteFactory* aFactory, const void* aCollectionKey,
      UINT32 aCollectionKeySize,
      IDWriteFontFileEnumerator** aFontFileEnumerator);

 private:
  BundledFontLoader(const BundledFontLoader&) = delete;
  BundledFontLoader& operator=(const BundledFontLoader&) = delete;
  virtual ~BundledFontLoader() = default;
};

IFACEMETHODIMP
BundledFontLoader::CreateEnumeratorFromKey(
    IDWriteFactory* aFactory, const void* aCollectionKey,
    UINT32 aCollectionKeySize,
    IDWriteFontFileEnumerator** aFontFileEnumerator) {
  nsIFile* fontDir = *(nsIFile**)aCollectionKey;
  *aFontFileEnumerator = new BundledFontFileEnumerator(aFactory, fontDir);
  NS_ADDREF(*aFontFileEnumerator);
  return S_OK;
}

already_AddRefed<IDWriteFontCollection>
gfxDWriteFontList::CreateBundledFontsCollection(IDWriteFactory* aFactory) {
  nsCOMPtr<nsIFile> localDir;
  nsresult rv = NS_GetSpecialDirectory(NS_GRE_DIR, getter_AddRefs(localDir));
  if (NS_FAILED(rv)) {
    return nullptr;
  }
  if (NS_FAILED(localDir->Append(u"fonts"_ns))) {
    return nullptr;
  }
  bool isDir;
  if (NS_FAILED(localDir->IsDirectory(&isDir)) || !isDir) {
    return nullptr;
  }

  RefPtr<BundledFontLoader> loader = new BundledFontLoader();
  if (FAILED(aFactory->RegisterFontCollectionLoader(loader))) {
    return nullptr;
  }

  const void* key = localDir.get();
  RefPtr<IDWriteFontCollection> collection;
  HRESULT hr = aFactory->CreateCustomFontCollection(loader, &key, sizeof(key),
                                                    getter_AddRefs(collection));

  aFactory->UnregisterFontCollectionLoader(loader);

  if (FAILED(hr)) {
    return nullptr;
  } else {
    return collection.forget();
  }
}

#endif
