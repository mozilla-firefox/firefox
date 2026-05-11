/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "mozilla/Logging.h"
#include "mozilla/gfx/Logging.h"
#include "mozilla/intl/Locale.h"
#include "mozilla/intl/LocaleService.h"
#include "mozilla/intl/OSPreferences.h"

#include "gfxPlatformFontList.h"
#include "gfxScriptItemizer.h"
#include "gfxTextRun.h"
#include "gfxUserFontSet.h"
#include "SharedFontList-impl.h"

#include "FontVisibilityProvider.h"

#include "GeckoProfiler.h"
#include "nsCRT.h"
#include "nsGkAtoms.h"
#include "nsPresContext.h"
#include "nsServiceManagerUtils.h"
#include "nsUnicharUtils.h"
#include "nsUnicodeProperties.h"
#include "nsXULAppAPI.h"

#include "mozilla/AppShutdown.h"
#include "mozilla/BinarySearch.h"
#include "mozilla/Likely.h"
#include "mozilla/MemoryReporting.h"
#include "mozilla/Mutex.h"
#include "mozilla/Preferences.h"
#include "mozilla/StaticPrefs_gfx.h"
#include "mozilla/StaticPrefs_layout.h"
#include "mozilla/StaticPrefs_mathml.h"
#include "mozilla/glean/GfxMetrics.h"
#include "mozilla/TimeStamp.h"
#include "mozilla/dom/BlobImpl.h"
#include "mozilla/dom/ContentChild.h"
#include "mozilla/dom/ContentParent.h"
#include "mozilla/dom/ContentProcessMessageManager.h"
#include "mozilla/dom/Document.h"
#include "mozilla/gfx/2D.h"
#include "mozilla/ipc/FileDescriptorUtils.h"
#include "mozilla/TextUtils.h"

#include "base/eintr_wrapper.h"

#include <numeric>

using namespace mozilla;
using mozilla::intl::Locale;
using mozilla::intl::LocaleParser;
using mozilla::intl::LocaleService;
using mozilla::intl::OSPreferences;

#define LOG_FONTLIST(args) \
  MOZ_LOG(gfxPlatform::GetLog(eGfxLog_fontlist), LogLevel::Debug, args)

#define LOG_FONTQUERY(args) \
  MOZ_LOG(gfxPlatform::GetLog(eGfxLog_fontquery), LogLevel::Debug, args)
#define LOG_FONTQUERYV(args) \
  MOZ_LOG(gfxPlatform::GetLog(eGfxLog_fontquery), LogLevel::Verbose, args)
#define LOG_FONTLIST_ENABLED() \
  MOZ_LOG_TEST(gfxPlatform::GetLog(eGfxLog_fontlist), LogLevel::Debug)
#define LOG_FONTINIT(args) \
  MOZ_LOG(gfxPlatform::GetLog(eGfxLog_fontinit), LogLevel::Debug, args)
#define LOG_FONTINIT_ENABLED() \
  MOZ_LOG_TEST(gfxPlatform::GetLog(eGfxLog_fontinit), LogLevel::Debug)

gfxPlatformFontList* gfxPlatformFontList::sPlatformFontList = nullptr;

// Character ranges that require complex-script shaping support in the font,
// and so should be masked out by ReadCMAP if the necessary layout tables
// are not present.
// Currently used by the Mac and FT2 implementations only, but probably should
// be supported on Windows as well.
const gfxFontEntry::ScriptRange gfxPlatformFontList::sComplexScriptRanges[] = {
    // Actually, now that harfbuzz supports presentation-forms shaping for
    // Arabic, we can render it without layout tables. So maybe we don't
    // want to mask the basic Arabic block here?
    // This affects the arabic-fallback-*.html reftests, which rely on
    // loading a font that *doesn't* have any GSUB table.
    {0x0600, 0x060B, 1, {TRUETYPE_TAG('a', 'r', 'a', 'b'), 0, 0}},
    // skip 060C Arabic comma, also used by N'Ko etc
    {0x060D, 0x061A, 1, {TRUETYPE_TAG('a', 'r', 'a', 'b'), 0, 0}},
    // skip 061B Arabic semicolon, also used by N'Ko etc
    {0x061C, 0x061E, 1, {TRUETYPE_TAG('a', 'r', 'a', 'b'), 0, 0}},
    // skip 061F Arabic question mark, also used by N'Ko etc
    {0x0620, 0x063F, 1, {TRUETYPE_TAG('a', 'r', 'a', 'b'), 0, 0}},
    // skip 0640 Arabic tatweel (for syriac, adlam, etc)
    {0x0641, 0x06D3, 1, {TRUETYPE_TAG('a', 'r', 'a', 'b'), 0, 0}},
    // skip 06D4 Arabic full stop (for hanifi rohingya)
    {0x06D5, 0x06FF, 1, {TRUETYPE_TAG('a', 'r', 'a', 'b'), 0, 0}},
    {0x0700, 0x074F, 1, {TRUETYPE_TAG('s', 'y', 'r', 'c'), 0, 0}},
    {0x0750, 0x077F, 1, {TRUETYPE_TAG('a', 'r', 'a', 'b'), 0, 0}},
    {0x08A0, 0x08FF, 1, {TRUETYPE_TAG('a', 'r', 'a', 'b'), 0, 0}},
    {0x0900,
     0x0963,
     2,
     {TRUETYPE_TAG('d', 'e', 'v', '2'), TRUETYPE_TAG('d', 'e', 'v', 'a'), 0}},
    // skip 0964 DEVANAGARI DANDA and 0965 DEVANAGARI DOUBLE DANDA, shared by
    // various other Indic writing systems
    {0x0966,
     0x097F,
     2,
     {TRUETYPE_TAG('d', 'e', 'v', '2'), TRUETYPE_TAG('d', 'e', 'v', 'a'), 0}},
    {0x0980,
     0x09FF,
     2,
     {TRUETYPE_TAG('b', 'n', 'g', '2'), TRUETYPE_TAG('b', 'e', 'n', 'g'), 0}},
    {0x0A00,
     0x0A7F,
     2,
     {TRUETYPE_TAG('g', 'u', 'r', '2'), TRUETYPE_TAG('g', 'u', 'r', 'u'), 0}},
    {0x0A80,
     0x0AFF,
     2,
     {TRUETYPE_TAG('g', 'j', 'r', '2'), TRUETYPE_TAG('g', 'u', 'j', 'r'), 0}},
    {0x0B00,
     0x0B7F,
     2,
     {TRUETYPE_TAG('o', 'r', 'y', '2'), TRUETYPE_TAG('o', 'r', 'y', 'a'), 0}},
    {0x0B80,
     0x0BFF,
     2,
     {TRUETYPE_TAG('t', 'm', 'l', '2'), TRUETYPE_TAG('t', 'a', 'm', 'l'), 0}},
    {0x0C00,
     0x0C7F,
     2,
     {TRUETYPE_TAG('t', 'e', 'l', '2'), TRUETYPE_TAG('t', 'e', 'l', 'u'), 0}},
    {0x0C80,
     0x0CFF,
     2,
     {TRUETYPE_TAG('k', 'n', 'd', '2'), TRUETYPE_TAG('k', 'n', 'd', 'a'), 0}},
    {0x0D00,
     0x0D7F,
     2,
     {TRUETYPE_TAG('m', 'l', 'm', '2'), TRUETYPE_TAG('m', 'l', 'y', 'm'), 0}},
    {0x0D80, 0x0DFF, 1, {TRUETYPE_TAG('s', 'i', 'n', 'h'), 0, 0}},
    {0x0E80, 0x0EFF, 1, {TRUETYPE_TAG('l', 'a', 'o', ' '), 0, 0}},
    {0x0F00, 0x0FFF, 1, {TRUETYPE_TAG('t', 'i', 'b', 't'), 0, 0}},
    {0x1000,
     0x109f,
     2,
     {TRUETYPE_TAG('m', 'y', 'm', 'r'), TRUETYPE_TAG('m', 'y', 'm', '2'), 0}},
    {0x1780, 0x17ff, 1, {TRUETYPE_TAG('k', 'h', 'm', 'r'), 0, 0}},
    // Khmer Symbols (19e0..19ff) don't seem to need any special shaping
    {0xaa60,
     0xaa7f,
     2,
     {TRUETYPE_TAG('m', 'y', 'm', 'r'), TRUETYPE_TAG('m', 'y', 'm', '2'), 0}},
    // Thai seems to be "renderable" without AAT morphing tables
    {0, 0, 0, {0, 0, 0}}  // terminator
};

static const char* kObservedPrefs[] = {
    "font.", "font.name-list.", "intl.accept_languages",  // hmmmm...
    "browser.display.use_document_fonts.icon_font_allowlist", nullptr};

static const char kFontSystemWhitelistPref[] = "font.system.whitelist";

static const char kCJKFallbackOrderPref[] = "font.cjk_pref_fallback_order";

// Pref for the list of icon font families that still get to override the
// default font from prefs, even when use_document_fonts is disabled.
// (This is to enable ligature-based icon fonts to keep working.)
static const char kIconFontsPref[] =
    "browser.display.use_document_fonts.icon_font_allowlist";

// xxx - this can probably be eliminated by reworking pref font handling code
static const char* gPrefLangNames[] = {
#define FONT_PREF_LANG(enum_id_, str_, atom_id_) str_
#include "gfxFontPrefLangList.inc"
#undef FONT_PREF_LANG
};

static_assert(std::size(gPrefLangNames) == uint32_t(eFontPrefLang_Count),
              "size of pref lang name array doesn't match pref lang enum size");

class gfxFontListPrefObserver final : public nsIObserver {
  ~gfxFontListPrefObserver() = default;

 public:
  NS_DECL_ISUPPORTS
  NS_DECL_NSIOBSERVER
};

static void FontListPrefChanged(const char* aPref, void* aData = nullptr) {
  // XXX this could be made to only clear out the cache for the prefs that were
  // changed but it probably isn't that big a deal.
  gfxPlatformFontList::PlatformFontList()->ClearLangGroupPrefFonts();
  gfxPlatformFontList::PlatformFontList()->LoadIconFontOverrideList();
  gfxFontCache::GetCache()->Flush();
}

static gfxFontListPrefObserver* gFontListPrefObserver = nullptr;

NS_IMPL_ISUPPORTS(gfxFontListPrefObserver, nsIObserver)

#define LOCALES_CHANGED_TOPIC "intl:system-locales-changed"

NS_IMETHODIMP
gfxFontListPrefObserver::Observe(nsISupports* aSubject, const char* aTopic,
                                 const char16_t* aData) {
  NS_ASSERTION(!strcmp(aTopic, LOCALES_CHANGED_TOPIC), "invalid topic");
  FontListPrefChanged(nullptr);

  if (XRE_IsParentProcess()) {
    gfxPlatform::GlobalReflowFlags flags =
        gfxPlatform::GlobalReflowFlags::BroadcastToChildren |
        gfxPlatform::GlobalReflowFlags::FontsChanged;
    gfxPlatform::ForceGlobalReflow(flags);
  }
  return NS_OK;
}

MOZ_DEFINE_MALLOC_SIZE_OF(FontListMallocSizeOf)

NS_IMPL_ISUPPORTS(gfxPlatformFontList::MemoryReporter, nsIMemoryReporter)

NS_IMETHODIMP
gfxPlatformFontList::MemoryReporter::CollectReports(
    nsIHandleReportCallback* aHandleReport, nsISupports* aData,
    bool aAnonymize) {
  FontListSizes sizes;
  sizes.mFontListSize = 0;
  sizes.mFontTableCacheSize = 0;
  sizes.mCharMapsSize = 0;
  sizes.mLoaderSize = 0;
  sizes.mSharedSize = 0;

  gfxPlatformFontList::PlatformFontList()->AddSizeOfIncludingThis(
      &FontListMallocSizeOf, &sizes);

  MOZ_COLLECT_REPORT(
      "explicit/gfx/font-list", KIND_HEAP, UNITS_BYTES, sizes.mFontListSize,
      "Memory used to manage the list of font families and faces.");

  MOZ_COLLECT_REPORT(
      "explicit/gfx/font-charmaps", KIND_HEAP, UNITS_BYTES, sizes.mCharMapsSize,
      "Memory used to record the character coverage of individual fonts.");

  if (sizes.mFontTableCacheSize) {
    MOZ_COLLECT_REPORT(
        "explicit/gfx/font-tables", KIND_HEAP, UNITS_BYTES,
        sizes.mFontTableCacheSize,
        "Memory used for cached font metrics and layout tables.");
  }

  if (sizes.mLoaderSize) {
    MOZ_COLLECT_REPORT("explicit/gfx/font-loader", KIND_HEAP, UNITS_BYTES,
                       sizes.mLoaderSize,
                       "Memory used for (platform-specific) font loader.");
  }

  if (sizes.mSharedSize) {
    MOZ_COLLECT_REPORT(
        "font-list-shmem", KIND_NONHEAP, UNITS_BYTES, sizes.mSharedSize,
        "Shared memory for system font list and character coverage data.");
  }

  return NS_OK;
}

PRThread* gfxPlatformFontList::sInitFontListThread = nullptr;

static void InitFontListCallback(void* aFontList) {
  AUTO_PROFILER_REGISTER_THREAD("InitFontList");
  PR_SetCurrentThreadName("InitFontList");

  if (!static_cast<gfxPlatformFontList*>(aFontList)->InitFontList()) {
    gfxPlatformFontList::Shutdown();
  }
}

/* static */
bool gfxPlatformFontList::Initialize(gfxPlatformFontList* aList) {
  sPlatformFontList = aList;
  if (XRE_IsParentProcess() &&
      StaticPrefs::gfx_font_list_omt_enabled_AtStartup() &&
      StaticPrefs::gfx_e10s_font_list_shared_AtStartup() &&
      !gfxPlatform::InSafeMode()) {
    // We call nsRFPService::CalculateFontLocaleAllowlist so that it reads
    // intl.accept_languages while we are still on the main thread.
    nsRFPService::CalculateFontLocaleAllowlist();
    sInitFontListThread = PR_CreateThread(
        PR_USER_THREAD, InitFontListCallback, aList, PR_PRIORITY_NORMAL,
        PR_GLOBAL_THREAD, PR_JOINABLE_THREAD, 0);
    return true;
  }
  if (aList->InitFontList()) {
    return true;
  }
  Shutdown();
  return false;
}

gfxPlatformFontList::gfxPlatformFontList(bool aNeedFullnamePostscriptNames)
    : mLock("gfxPlatformFontList lock"),
      mFontFamilies(64),
      mOtherFamilyNames(16),
      mSharedCmaps(8) {
  if (aNeedFullnamePostscriptNames) {
    mExtraNames = MakeUnique<ExtraNames>();
  }

  mLangService = nsLanguageAtomService::GetService();

  LoadBadUnderlineList();
  LoadIconFontOverrideList();

  mFontPrefs = MakeUnique<FontPrefs>();

  gfxFontUtils::GetPrefsFontList(kFontSystemWhitelistPref, mEnabledFontsList);
  mFontFamilyWhitelistActive = !mEnabledFontsList.IsEmpty();

  // pref changes notification setup
  NS_ASSERTION(!gFontListPrefObserver,
               "There has been font list pref observer already");
  gFontListPrefObserver = new gfxFontListPrefObserver();
  NS_ADDREF(gFontListPrefObserver);

  Preferences::RegisterPrefixCallbacks(FontListPrefChanged, kObservedPrefs);

  nsCOMPtr<nsIObserverService> obs = services::GetObserverService();
  if (obs) {
    obs->AddObserver(gFontListPrefObserver, LOCALES_CHANGED_TOPIC, false);
  }

  // Only the parent process listens for whitelist changes; it will then
  // notify its children to rebuild their font lists.
  if (XRE_IsParentProcess()) {
    Preferences::RegisterCallback(FontWhitelistPrefChanged,
                                  kFontSystemWhitelistPref);
  }

  RegisterStrongMemoryReporter(MakeAndAddRef<MemoryReporter>());

  // initialize lang group pref font defaults (i.e. serif/sans-serif)
  mDefaultGenericsLangGroup.AppendElements(std::size(gPrefLangNames));
  for (uint32_t i = 0; i < std::size(gPrefLangNames); i++) {
    nsAutoCString prefDefaultFontType("font.default.");
    prefDefaultFontType.Append(GetPrefLangName(eFontPrefLang(i)));
    nsAutoCString serifOrSans;
    Preferences::GetCString(prefDefaultFontType.get(), serifOrSans);
    if (serifOrSans.EqualsLiteral("sans-serif")) {
      mDefaultGenericsLangGroup[i] = StyleGenericFontFamily::SansSerif;
    } else {
      mDefaultGenericsLangGroup[i] = StyleGenericFontFamily::Serif;
    }
  }
}

gfxPlatformFontList::~gfxPlatformFontList() {
  // Note that gfxPlatformFontList::Shutdown() ensures that the init-font-list
  // thread is finished before we come here.

  AutoLock lock(mLock);

  // We can't just do mSharedCmaps.Clear() here because removing each item from
  // the table would drop its last reference, and its Release() method would
  // then call back to MaybeRemoveCmap to search for it, which we can't do
  // while in the middle of clearing the table.
  // So we first clear the "shared" flag in each entry, so Release() won't try
  // to re-find them in the table.
  for (auto iter = mSharedCmaps.ConstIter(); !iter.Done(); iter.Next()) {
    iter.Get()->mCharMap->ClearSharedFlag();
  }
  mSharedCmaps.Clear();

  ClearLangGroupPrefFontsLocked();

  NS_ASSERTION(gFontListPrefObserver, "There is no font list pref observer");

  Preferences::UnregisterPrefixCallbacks(FontListPrefChanged, kObservedPrefs);

  nsCOMPtr<nsIObserverService> obs = services::GetObserverService();
  if (obs) {
    obs->RemoveObserver(gFontListPrefObserver, LOCALES_CHANGED_TOPIC);
  }

  if (XRE_IsParentProcess()) {
    Preferences::UnregisterCallback(FontWhitelistPrefChanged,
                                    kFontSystemWhitelistPref);
  }
  NS_RELEASE(gFontListPrefObserver);

  delete mSharedFontList.exchange(nullptr);
}

FontVisibility gfxPlatformFontList::GetFontVisibility(nsCString& aFont,
                                                      bool& aFound) {
  AutoLock lock(mLock);

  GenerateFontListKey(aFont);
  if (SharedFontList()) {
    auto* font = SharedFontList()->FindFamily(aFont);
    if (font) {
      aFound = true;
      return font->Visibility();
    }
    aFound = false;
    return FontVisibility::Unknown;
  }

  {
    auto* font = mFontFamilies.GetWeak(aFont);
    if (font) {
      aFound = true;
      return font->Visibility();
    }
  }

  {
    auto* font = mOtherFamilyNames.GetWeak(aFont);
    if (font) {
      aFound = true;
      return font->Visibility();
    }
  }

  aFound = false;
  return FontVisibility::Unknown;
}

namespace {

// Minimal FontVisibilityProvider for ListFontsUsedForString that returns a
// fixed visibility level without requiring a document or pres context.
class ListFontsVisibilityProvider final : public FontVisibilityProvider {
 public:
  explicit ListFontsVisibilityProvider(FontVisibility aVisibility)
      : mVisibility(aVisibility) {}

  FontVisibility GetFontVisibility() const override { return mVisibility; }
  bool ShouldResistFingerprinting(mozilla::RFPTarget) const override {
    return false;
  }
  void ReportBlockedFontFamily(const nsCString&) const override {}
  bool IsChrome() const override { return false; }
  bool IsPrivateBrowsing() const override { return false; }
  nsICookieJarSettings* GetCookieJarSettings() const override {
    return nullptr;
  }
  mozilla::Maybe<FontVisibility> MaybeInheritFontVisibility() const override {
    return mozilla::Nothing();
  }
  void UserFontSetUpdated(gfxUserFontEntry*) override {}
  using FontVisibilityProvider::ReportBlockedFontFamily;

 private:
  FontVisibility mVisibility;
};

}  // namespace

void gfxPlatformFontList::ListFontsUsedForString(
    const nsAString& aText, const nsTArray<nsCString>& aFontList,
    nsTArray<nsCString>& aFontsUsed, FontVisibility aMaxVisibility) {
  if (aText.IsEmpty() || aFontList.IsEmpty()) {
    return;
  }

  LOG_FONTQUERY(
      ("(fontquery) ListFontsUsedForString: %zu fonts, %zu chars, "
       "maxVisibility=%d",
       aFontList.Length(), aText.Length(), static_cast<int>(aMaxVisibility)));

  // Build a StyleFontFamilyList from the provided font names.
  nsTArray<StyleSingleFontFamily> names;
  for (const auto& fontName : aFontList) {
    names.AppendElement(StyleSingleFontFamily::FamilyName(
        StyleFamilyName{StyleAtom(NS_Atomize(fontName)),
                        StyleFontFamilyNameSyntax::Identifiers}));
  }
  StyleFontFamilyList familyList =
      StyleFontFamilyList::WithNames(std::move(names));

  // Create a visibility provider and font group.
  ListFontsVisibilityProvider visProvider(aMaxVisibility);
  gfxFontStyle style;
  RefPtr<gfxFontGroup> fontGroup = new gfxFontGroup(
      &visProvider, familyList, &style, nsGkAtoms::x_western, false, nullptr,
      nullptr, 1.0, StyleFontVariantEmoji::Normal);

  // Use gfxScriptItemizer to break text into script runs, then
  // gfxFontGroup::ComputeRanges to determine which fonts are used.
  // This mirrors the browser's actual font matching, including variation
  // selector handling, emoji presentation, and global fallback.
  using TextRange = gfxFontGroup::TextRange;
  using Script = mozilla::intl::Script;

  gfxScriptItemizer scriptRuns;
  const char16_t* textData = aText.BeginReading();
  uint32_t textLen = aText.Length();
  scriptRuns.SetText(textData, textLen);

  nsTHashSet<nsCString> usedSet;

  while (gfxScriptItemizer::Run run = scriptRuns.Next()) {
    Script script = run.mScript;
    // Resolve COMMON/INHERITED to LATIN for western language contexts,
    // matching what InitTextRun does via ResolveScriptForLang.
    if (script <= Script::INHERITED) {
      script = Script::LATIN;
    }

    AutoTArray<TextRange, 3> ranges;
    fontGroup->ComputeRanges(ranges, textData + run.mOffset, run.mLength,
                             script,
                             gfx::ShapedTextFlags::TEXT_ORIENT_HORIZONTAL);

    for (const auto& range : ranges) {
      if (range.font) {
        const nsCString& familyName = range.font->GetFontEntry()->FamilyName();
        if (usedSet.EnsureInserted(familyName)) {
          aFontsUsed.AppendElement(familyName);
          LOG_FONTQUERY(
              ("(fontquery) ListFontsUsedForString: font '%s' used "
               "(matchType=%d)",
               familyName.get(), static_cast<int>(range.matchType.kind)));
        }
      }
    }
  }

  LOG_FONTQUERY(("(fontquery) ListFontsUsedForString: result - %zu fonts used",
                 aFontsUsed.Length()));
}

bool gfxPlatformFontList::GetMissingFonts(nsTArray<nsCString>& aMissingFonts) {
  AutoLock lock(mLock);

  auto fontLists = GetFilteredPlatformFontLists();

  if (!fontLists.Length()) {
    return false;
  }

  for (unsigned int i = 0; i < fontLists.Length(); i++) {
    for (unsigned int j = 0; j < fontLists[i].second; j++) {
      nsCString key(fontLists[i].first[j]);
      GenerateFontListKey(key);

      bool found = false;
      GetFontVisibility(key, found);
      if (!found) {
        aMissingFonts.AppendElement(key);
      }
    }
  }
  return true;
}

void gfxPlatformFontList::GetMissingFonts(nsCString& aMissingFonts) {
  nsTArray<nsCString> missingFonts;
  bool fontlistExists = GetMissingFonts(missingFonts);

  if (!fontlistExists) {
    aMissingFonts.AssignLiteral("No font list available for this device.");
    return;
  }

  if (missingFonts.IsEmpty()) {
    aMissingFonts.Append("All fonts are available.");
    return;
  }

  missingFonts.Sort();
  aMissingFonts.Append(StringJoin("|"_ns, missingFonts));
}

/* static */
void gfxPlatformFontList::FontWhitelistPrefChanged(const char* aPref,
                                                   void* aClosure) {
  MOZ_ASSERT(XRE_IsParentProcess());
  auto* pfl = gfxPlatformFontList::PlatformFontList();
  pfl->UpdateFontList(true);
  dom::ContentParent::NotifyUpdatedFonts(true);
}

void gfxPlatformFontList::ApplyWhitelist() {
  uint32_t numFonts = mEnabledFontsList.Length();
  if (!mFontFamilyWhitelistActive) {
    return;
  }
  nsTHashSet<nsCString> familyNamesWhitelist;
  for (uint32_t i = 0; i < numFonts; i++) {
    nsAutoCString key;
    ToLowerCase(mEnabledFontsList[i], key);
    familyNamesWhitelist.Insert(key);
  }
  AutoTArray<RefPtr<gfxFontFamily>, 128> accepted;
  bool whitelistedFontFound = false;
  for (const auto& entry : mFontFamilies) {
    nsAutoCString fontFamilyName(entry.GetKey());
    ToLowerCase(fontFamilyName);
    if (familyNamesWhitelist.Contains(fontFamilyName)) {
      accepted.AppendElement(entry.GetData());
      whitelistedFontFound = true;
    }
  }
  if (!whitelistedFontFound) {
    // No whitelisted fonts found! Ignore the whitelist.
    return;
  }
  // Replace the original full list with the accepted subset.
  mFontFamilies.Clear();
  for (auto& f : accepted) {
    nsAutoCString fontFamilyName(f->Name());
    ToLowerCase(fontFamilyName);
    mFontFamilies.InsertOrUpdate(fontFamilyName, std::move(f));
  }
}

void gfxPlatformFontList::ApplyWhitelist(
    nsTArray<fontlist::Family::InitData>& aFamilies) {
  mLock.AssertCurrentThreadIn();
  if (!mFontFamilyWhitelistActive) {
    return;
  }
  nsTHashSet<nsCString> familyNamesWhitelist;
  for (const auto& item : mEnabledFontsList) {
    nsAutoCString key;
    ToLowerCase(item, key);
    familyNamesWhitelist.Insert(key);
  }
  AutoTArray<fontlist::Family::InitData, 128> accepted;
  bool keptNonHidden = false;
  for (auto& f : aFamilies) {
    if (familyNamesWhitelist.Contains(f.mKey)) {
      accepted.AppendElement(f);
      if (f.mVisibility != FontVisibility::Hidden) {
        keptNonHidden = true;
      }
    }
  }
  if (!keptNonHidden) {
    // No (visible) families were whitelisted: ignore the whitelist
    // and just leave the fontlist unchanged.
    return;
  }
  aFamilies = std::move(accepted);
}

bool gfxPlatformFontList::FamilyInList(const nsACString& aName,
                                       const char* aList[], size_t aCount) {
  size_t result;
  return BinarySearchIf(
      aList, 0, aCount,
      [&](const char* const aVal) -> int {
        return nsCaseInsensitiveUTF8StringComparator(
            aName.BeginReading(), aVal, aName.Length(), strlen(aVal));
      },
      &result);
}

void gfxPlatformFontList::CheckFamilyList(const char* aList[], size_t aCount) {
#ifdef DEBUG
  MOZ_ASSERT(aCount > 0, "empty font family list?");
  const char* a = aList[0];
  uint32_t aLen = strlen(a);
  for (size_t i = 1; i < aCount; ++i) {
    const char* b = aList[i];
    uint32_t bLen = strlen(b);
    if (nsCaseInsensitiveUTF8StringComparator(a, b, aLen, bLen) >= 0) {
      MOZ_CRASH_UNSAFE_PRINTF("incorrectly sorted font family list: %s >= %s",
                              a, b);
    }
    a = b;
    aLen = bLen;
  }
#endif
}

bool gfxPlatformFontList::AddWithLegacyFamilyName(const nsACString& aLegacyName,
                                                  gfxFontEntry* aFontEntry,
                                                  FontVisibility aVisibility) {
  mLock.AssertCurrentThreadIn();
  bool added = false;
  nsAutoCString key;
  ToLowerCase(aLegacyName, key);
  mOtherFamilyNames
      .LookupOrInsertWith(key,
                          [&] {
                            RefPtr<gfxFontFamily> family =
                                CreateFontFamily(aLegacyName, aVisibility);
                            // We don't want the family to search for faces,
                            // we're adding them directly here.
                            family->SetHasStyles(true);
                            // And we don't want it to attempt to search for
                            // legacy names, because we've already done that
                            // (and this is the result).
                            family->SetCheckedForLegacyFamilyNames(true);
                            added = true;
                            return family;
                          })
      ->AddFontEntry(aFontEntry->Clone());
  return added;
}

bool gfxPlatformFontList::InitFontList() {
  // If the startup font-list-init thread is still running, we need to wait
  // for it to finish before trying to reinitialize here.
  if (sInitFontListThread && !IsInitFontListThread()) {
    PR_JoinThread(sInitFontListThread);
    sInitFontListThread = nullptr;
  }

  AutoLock lock(mLock);

  if (LOG_FONTINIT_ENABLED()) {
    LOG_FONTINIT(("(fontinit) system fontlist initialization\n"));
  }

  if (IsInitialized()) {
    // Font-list reinitialization always occurs on the main thread, in response
    // to a change notification; it's only the initial creation during startup
    // that may be on another thread.
    MOZ_ASSERT(NS_IsMainThread());

    // Rebuilding fontlist so clear out font/word caches.
    gfxFontCache* fontCache = gfxFontCache::GetCache();
    if (fontCache) {
      fontCache->FlushShapedWordCaches();
      fontCache->Flush();
    }

    gfxPlatform::PurgeSkiaFontCache();

    // There's no need to broadcast this reflow request to child processes, as
    // ContentParent::NotifyUpdatedFonts deals with it by re-entering into this
    // function on child processes.
    gfxPlatform::GlobalReflowFlags flags =
        gfxPlatform::GlobalReflowFlags::NeedsReframe |
        gfxPlatform::GlobalReflowFlags::FontsChanged;
    ForceGlobalReflowLocked(flags);

    mAliasTable.Clear();
    mLocalNameTable.Clear();
    mIconFontsSet.Clear();

    CancelLoadCmapsTask();
    mStartedLoadingCmapsFrom = 0xffffffffu;

    CancelInitOtherFamilyNamesTask();
    mFontFamilies.Clear();
    mOtherFamilyNames.Clear();
    mOtherFamilyNamesInitialized = false;

    if (mExtraNames) {
      mExtraNames->mFullnames.Clear();
      mExtraNames->mPostscriptNames.Clear();
    }
    mFaceNameListsInitialized = false;
    ClearLangGroupPrefFontsLocked();
    CancelLoader();

    // Clear cached family records that will no longer be valid.
    for (auto& f : mReplacementCharFallbackFamily) {
      f = FontFamily();
    }

    gfxFontUtils::GetPrefsFontList(kFontSystemWhitelistPref, mEnabledFontsList);
    mFontFamilyWhitelistActive = !mEnabledFontsList.IsEmpty();

    LoadIconFontOverrideList();
  }

  // From here, gfxPlatformFontList::IsInitialized will return true,
  // unless InitFontListForPlatform() fails and we reset it below.
  if (MOZ_UNLIKELY(!++mFontlistInitCount)) {
    ++mFontlistInitCount;  // if the counter wrapped, bump it past zero
  }

  InitializeCodepointsWithNoFonts();

  // Try to initialize the cross-process shared font list if enabled by prefs.
  if (StaticPrefs::gfx_e10s_font_list_shared_AtStartup()) {
    for (const auto& entry : mFontEntries.Values()) {
      if (!entry) {
        continue;
      }
      AutoWriteLock lock(entry->mLock);
      entry->mShmemCharacterMap = nullptr;
      entry->mShmemFace = nullptr;
      entry->mFamilyName.Truncate();
    }
    mFontEntries.Clear();
    mShmemCharMaps.Clear();
    bool oldSharedList = SharedFontList() != nullptr;
    delete mSharedFontList.exchange(new fontlist::FontList(mFontlistInitCount));
    InitSharedFontListForPlatform();
    auto* newList = SharedFontList();
    if (newList && newList->Initialized()) {
      if (mLocalNameTable.Count()) {
        newList->SetLocalNames(mLocalNameTable);
        mLocalNameTable.Clear();
      }
    } else {
      // something went wrong, fall back to in-process list
      gfxCriticalNote << "Failed to initialize shared font list, "
                         "falling back to in-process list.";
      delete mSharedFontList.exchange(nullptr);
    }
    if (oldSharedList && XRE_IsParentProcess()) {
      // notify all children of the change
      if (NS_IsMainThread()) {
        dom::ContentParent::NotifyUpdatedFonts(true);
      } else {
        NS_DispatchToMainThread(NS_NewRunnableFunction(
            "NotifyUpdatedFonts callback",
            [] { dom::ContentParent::NotifyUpdatedFonts(true); }));
      }
    }
  }

  if (SharedFontList()) {
    mFontListGeneration = SharedFontList()->GetGeneration();
  } else {
    mFontListGeneration = 0;
    if (NS_FAILED(InitFontListForPlatform())) {
      mFontlistInitCount = 0;
      return false;
    }
    ApplyWhitelist();
  }

  // Set up mDefaultFontEntry as a "last resort" default that we can use
  // to avoid crashing if the font list is otherwise unusable.
  gfxFontStyle defStyle;
  FontFamily fam = GetDefaultFontLocked(nullptr, &defStyle);
  gfxFontEntry* fe;
  if (fam.mShared) {
    auto face = fam.mShared->FindFaceForStyle(SharedFontList(), defStyle);
    fe = face ? GetOrCreateFontEntryLocked(face, fam.mShared) : nullptr;
  } else {
    fe = fam.mUnshared->FindFontForStyle(defStyle);
  }
  mDefaultFontEntry = fe;

  if (XRE_IsParentProcess() && NS_IsMainThread()) {
    if (nsCOMPtr<nsIObserverService> obsvc = services::GetObserverService()) {
      obsvc->NotifyObservers(nullptr, "font-list-initialized", nullptr);
    }
  }

  return true;
}

void gfxPlatformFontList::LoadIconFontOverrideList() {
  mIconFontsSet.Clear();
  AutoTArray<nsCString, 20> iconFontsList;
  gfxFontUtils::GetPrefsFontList(kIconFontsPref, iconFontsList);
  for (auto& name : iconFontsList) {
    ToLowerCase(name);
    mIconFontsSet.Insert(name);
  }
}

void gfxPlatformFontList::InitializeCodepointsWithNoFonts() {
  auto& first = mCodepointsWithNoFonts[FontVisibility(0)];
  for (auto& bitset : mCodepointsWithNoFonts) {
    if (&bitset == &first) {
      bitset.reset();
      bitset.SetRange(0, 0x1f);            // C0 controls
      bitset.SetRange(0x7f, 0x9f);         // C1 controls
      bitset.SetRange(0xE000, 0xF8FF);     // PUA
      bitset.SetRange(0xF0000, 0x10FFFD);  // Supplementary PUA
      bitset.SetRange(0xfdd0, 0xfdef);     // noncharacters
      for (unsigned i = 0; i <= 0x100000; i += 0x10000) {
        bitset.SetRange(i + 0xfffe, i + 0xffff);  // noncharacters
      }
      bitset.Compact();
    } else {
      bitset = first;
    }
  }
}

void gfxPlatformFontList::GenerateFontListKey(const nsACString& aKeyName,
                                              nsACString& aResult) {
  aResult = aKeyName;
  ToLowerCase(aResult);
}

void gfxPlatformFontList::GenerateFontListKey(nsACString& aKeyName) {
  ToLowerCase(aKeyName);
}

// Used if a stylo thread wants to trigger InitOtherFamilyNames in the main
// process: we can't do IPC from the stylo thread so we post this to the main
// thread instead.
class InitOtherFamilyNamesForStylo : public mozilla::Runnable {
 public:
  explicit InitOtherFamilyNamesForStylo(bool aDeferOtherFamilyNamesLoading)
      : Runnable("gfxPlatformFontList::InitOtherFamilyNamesForStylo"),
        mDefer(aDeferOtherFamilyNamesLoading) {}

  NS_IMETHOD Run() override {
    auto pfl = gfxPlatformFontList::PlatformFontList();
    auto list = pfl->SharedFontList();
    if (!list) {
      return NS_OK;
    }
    bool initialized = false;
    dom::ContentChild::GetSingleton()->SendInitOtherFamilyNames(
        pfl->GetGeneration(), mDefer, &initialized);
    pfl->mOtherFamilyNamesInitialized.compareExchange(false, initialized);
    return NS_OK;
  }

 private:
  bool mDefer;
};

#define OTHERNAMES_TIMEOUT 200

bool gfxPlatformFontList::InitOtherFamilyNames(
    bool aDeferOtherFamilyNamesLoading) {
  if (mOtherFamilyNamesInitialized) {
    return true;
  }

  if (SharedFontList() && !XRE_IsParentProcess()) {
    if (NS_IsMainThread()) {
      bool initialized;
      dom::ContentChild::GetSingleton()->SendInitOtherFamilyNames(
          GetGeneration(), aDeferOtherFamilyNamesLoading, &initialized);
      mOtherFamilyNamesInitialized.compareExchange(false, initialized);
    } else {
      NS_DispatchToMainThread(
          new InitOtherFamilyNamesForStylo(aDeferOtherFamilyNamesLoading));
    }
    return mOtherFamilyNamesInitialized;
  }

  // If the font loader delay has been set to zero, we don't defer loading
  // additional family names (regardless of the aDefer... parameter), as we
  // take this to mean availability of font info is to be prioritized over
  // potential startup perf or main-thread jank.
  // (This is used so we can reliably run reftests that depend on localized
  // font-family names being available.)
  if (aDeferOtherFamilyNamesLoading &&
      StaticPrefs::gfx_font_loader_delay() > 0) {
    if (!mPendingOtherFamilyNameTask) {
      RefPtr<mozilla::CancelableRunnable> task =
          new InitOtherFamilyNamesRunnable();
      mPendingOtherFamilyNameTask = task;
      NS_DispatchToMainThreadQueue(task.forget(), EventQueuePriority::Idle);
    }
  } else {
    InitOtherFamilyNamesInternal(false);
  }
  return mOtherFamilyNamesInitialized;
}

// time limit for loading facename lists (ms)
#define NAMELIST_TIMEOUT 200

gfxFontEntry* gfxPlatformFontList::SearchFamiliesForFaceName(
    const nsACString& aFaceName) {
  TimeStamp start = TimeStamp::Now();
  bool timedOut = false;
  // if mFirstChar is not 0, only load facenames for families
  // that start with this character
  char16_t firstChar = 0;
  gfxFontEntry* lookup = nullptr;

  // iterate over familes starting with the same letter
  firstChar = ToLowerCase(aFaceName.CharAt(0));

  for (const auto& entry : mFontFamilies) {
    nsCStringHashKey::KeyType key = entry.GetKey();
    const RefPtr<gfxFontFamily>& family = entry.GetData();

    // when filtering, skip names that don't start with the filter character
    if (firstChar && ToLowerCase(key.CharAt(0)) != firstChar) {
      continue;
    }

    family->ReadFaceNames(this, NeedFullnamePostscriptNames());

    TimeDuration elapsed = TimeStamp::Now() - start;
    if (elapsed.ToMilliseconds() > NAMELIST_TIMEOUT) {
      timedOut = true;
      break;
    }
  }

  lookup = FindFaceName(aFaceName);

  TimeDuration elapsed = TimeStamp::Now() - start;
  glean::fontlist::initfacenamelists.AccumulateRawDuration(elapsed);
  if (LOG_FONTINIT_ENABLED()) {
    LOG_FONTINIT(("(fontinit) SearchFamiliesForFaceName took %8.2f ms %s %s",
                  elapsed.ToMilliseconds(), (lookup ? "found name" : ""),
                  (timedOut ? "timeout" : "")));
  }

  return lookup;
}

gfxFontEntry* gfxPlatformFontList::FindFaceName(const nsACString& aFaceName) {
  gfxFontEntry* lookup;

  // lookup in name lookup tables, return null if not found
  if (mExtraNames &&
      ((lookup = mExtraNames->mPostscriptNames.GetWeak(aFaceName)) ||
       (lookup = mExtraNames->mFullnames.GetWeak(aFaceName)))) {
    return lookup;
  }

  return nullptr;
}

gfxFontEntry* gfxPlatformFontList::LookupInFaceNameLists(
    const nsACString& aFaceName) {
  gfxFontEntry* lookup = nullptr;

  // initialize facename lookup tables if needed
  // note: this can terminate early or time out, in which case
  //       mFaceNameListsInitialized remains false
  if (!mFaceNameListsInitialized) {
    lookup = SearchFamiliesForFaceName(aFaceName);
    if (lookup) {
      return lookup;
    }
  }

  // lookup in name lookup tables, return null if not found
  if (!(lookup = FindFaceName(aFaceName))) {
    // names not completely initialized, so keep track of lookup misses
    if (!mFaceNameListsInitialized) {
      if (!mFaceNamesMissed) {
        mFaceNamesMissed = MakeUnique<nsTHashSet<nsCString>>(2);
      }
      mFaceNamesMissed->Insert(aFaceName);
    }
  }

  return lookup;
}

gfxFontEntry* gfxPlatformFontList::LookupInSharedFaceNameList(
    FontVisibilityProvider* aFontVisibilityProvider,
    const nsACString& aFaceName, WeightRange aWeightForEntry,
    StretchRange aStretchForEntry, SlantStyleRange aStyleForEntry) {
  nsAutoCString keyName(aFaceName);
  ToLowerCase(keyName);
  fontlist::FontList* list = SharedFontList();
  fontlist::Family* family = nullptr;
  fontlist::Face* face = nullptr;
  if (list->NumLocalFaces()) {
    fontlist::LocalFaceRec* rec = list->FindLocalFace(keyName);
    if (rec) {
      auto* families = list->Families();
      if (families) {
        family = &families[rec->mFamilyIndex];
        face = family->Faces(list)[rec->mFaceIndex].ToPtr<fontlist::Face>(list);
      }
    }
  } else {
    list->SearchForLocalFace(keyName, &family, &face);
  }
  if (!face || !family) {
    return nullptr;
  }
  FontVisibility level = aFontVisibilityProvider
                             ? aFontVisibilityProvider->GetFontVisibility()
                             : FontVisibility::User;
  if (!IsVisibleToCSS(*family, level)) {
    if (aFontVisibilityProvider) {
      aFontVisibilityProvider->ReportBlockedFontFamily(*family);
    }
    return nullptr;
  }
  gfxFontEntry* fe = CreateFontEntry(face, family);
  if (fe) {
    fe->mIsLocalUserFont = true;
    fe->mWeightRange = aWeightForEntry;
    fe->mStretchRange = aStretchForEntry;
    fe->mStyleRange = aStyleForEntry;
  }
  return fe;
}

void gfxPlatformFontList::MaybeAddToLocalNameTable(
    const nsACString& aName, const fontlist::LocalFaceRec::InitData& aData) {
  // Compute a measure of the similarity between aName (which will be a PSName
  // or FullName) and aReference (a font family name).
  auto nameSimilarity = [](const nsACString& aName,
                           const nsACString& aReference) -> uint32_t {
    uint32_t nameIdx = 0, refIdx = 0, matchCount = 0;
    while (nameIdx < aName.Length() && refIdx < aReference.Length()) {
      // Ignore non-alphanumerics in the ASCII range, so that a PSname like
      // "TimesNewRomanPSMT" is a good match for family "Times New Roman".
      while (nameIdx < aName.Length() && IsAscii(aName[nameIdx]) &&
             !IsAsciiAlphanumeric(aName[nameIdx])) {
        ++nameIdx;
      }
      while (refIdx < aReference.Length() && IsAscii(aReference[refIdx]) &&
             !IsAsciiAlphanumeric(aReference[refIdx])) {
        ++refIdx;
      }
      if (nameIdx == aName.Length() || refIdx == aReference.Length() ||
          aName[nameIdx] != aReference[refIdx]) {
        break;
      }
      ++nameIdx;
      ++refIdx;
      ++matchCount;
    }
    return matchCount;
  };

  mLocalNameTable.WithEntryHandle(aName, [&](auto entry) -> void {
    if (entry) {
      if (nameSimilarity(aName, aData.mFamilyName) >
          nameSimilarity(aName, entry.Data().mFamilyName)) {
        entry.Update(aData);
      }
    } else {
      entry.OrInsert(aData);
    }
  });
}

void gfxPlatformFontList::LoadBadUnderlineList() {
  gfxFontUtils::GetPrefsFontList("font.blacklist.underline_offset",
                                 mBadUnderlineFamilyNames);
  for (auto& fam : mBadUnderlineFamilyNames) {
    ToLowerCase(fam);
  }
  mBadUnderlineFamilyNames.Compact();
  mBadUnderlineFamilyNames.Sort();
}

void gfxPlatformFontList::UpdateFontList(bool aFullRebuild) {
  MOZ_ASSERT(NS_IsMainThread());
  if (aFullRebuild) {
    InitFontList();
    AutoLock lock(mLock);
    RebuildLocalFonts();
  } else {
    // The font list isn't being fully rebuilt, we're just being notified that
    // character maps have been updated and so font fallback needs to be re-
    // done. We only care about this if we have previously encountered a
    // fallback that required cmaps that were not yet available, and so we
    // asked for the async cmap loader to run.
    AutoLock lock(mLock);
    if (mStartedLoadingCmapsFrom != 0xffffffffu) {
      InitializeCodepointsWithNoFonts();
      mStartedLoadingCmapsFrom = 0xffffffffu;
      gfxPlatform::GlobalReflowFlags flags =
          gfxPlatform::GlobalReflowFlags::FontsChanged |
          gfxPlatform::GlobalReflowFlags::BroadcastToChildren;
      ForceGlobalReflowLocked(flags);
    }
  }
}

bool gfxPlatformFontList::IsVisibleToCSS(const gfxFontFamily& aFamily,
                                         FontVisibility aVisibility) const {
  return aFamily.Visibility() <= aVisibility || IsFontFamilyWhitelistActive();
}

bool gfxPlatformFontList::IsVisibleToCSS(const fontlist::Family& aFamily,
                                         FontVisibility aVisibility) const {
  return aFamily.Visibility() <= aVisibility || IsFontFamilyWhitelistActive();
}

void gfxPlatformFontList::GetFontList(nsAtom* aLangGroup,
                                      const nsACString& aGenericFamily,
                                      nsTArray<nsString>& aListOfFonts) {
  AutoLock lock(mLock);

  if (SharedFontList()) {
    fontlist::FontList* list = SharedFontList();
    const fontlist::Family* families = list->Families();
    if (families) {
      for (uint32_t i = 0; i < list->NumFamilies(); i++) {
        auto& f = families[i];
        if (!IsVisibleToCSS(f, FontVisibility::User) || f.IsAltLocaleFamily()) {
          continue;
        }
        // XXX TODO: filter families for aGenericFamily, if supported by
        // platform
        aListOfFonts.AppendElement(
            NS_ConvertUTF8toUTF16(list->LocalizedFamilyName(&f)));
      }
    }
    return;
  }

  for (const RefPtr<gfxFontFamily>& family : mFontFamilies.Values()) {
    if (!IsVisibleToCSS(*family, FontVisibility::User)) {
      continue;
    }
    if (family->FilterForFontList(aLangGroup, aGenericFamily)) {
      nsAutoCString localizedFamilyName;
      family->LocalizedName(localizedFamilyName);
      aListOfFonts.AppendElement(NS_ConvertUTF8toUTF16(localizedFamilyName));
    }
  }

  aListOfFonts.Sort();
  aListOfFonts.Compact();
}

void gfxPlatformFontList::GetFontFamilyList(
    nsTArray<RefPtr<gfxFontFamily>>& aFamilyArray) {
  AutoLock lock(mLock);
  MOZ_ASSERT(aFamilyArray.IsEmpty());
  // This doesn't use ToArray, because the caller passes an AutoTArray.
  aFamilyArray.SetCapacity(mFontFamilies.Count());
  for (const auto& family : mFontFamilies.Values()) {
    aFamilyArray.AppendElement(family);
  }
}

already_AddRefed<gfxFont> gfxPlatformFontList::SystemFindFontForChar(
    FontVisibilityProvider* aFontVisibilityProvider, uint32_t aCh,
    uint32_t aNextCh, Script aRunScript, FontPresentation aPresentation,
    const gfxFontStyle* aStyle, FontVisibility* aVisibility) {
  AutoLock lock(mLock);
  FontVisibility level = aFontVisibilityProvider
                             ? aFontVisibilityProvider->GetFontVisibility()
                             : FontVisibility::User;
  MOZ_ASSERT(!mCodepointsWithNoFonts[level].test(aCh),
             "don't call for codepoints already known to be unsupported");

  // Try to short-circuit font fallback for U+FFFD, used to represent
  // encoding errors: just use cached family from last time U+FFFD was seen.
  // This helps speed up pages with lots of encoding errors, binary-as-text,
  // etc.
  if (aCh == 0xFFFD) {
    gfxFontEntry* fontEntry = nullptr;
    auto& fallbackFamily = mReplacementCharFallbackFamily[level];
    if (fallbackFamily.mShared) {
      fontlist::Face* face =
          fallbackFamily.mShared->FindFaceForStyle(SharedFontList(), *aStyle);
      if (face) {
        fontEntry = GetOrCreateFontEntryLocked(face, fallbackFamily.mShared);
        *aVisibility = fallbackFamily.mShared->Visibility();
      }
    } else if (fallbackFamily.mUnshared) {
      fontEntry = fallbackFamily.mUnshared->FindFontForStyle(*aStyle);
      *aVisibility = fallbackFamily.mUnshared->Visibility();
    }

    // this should never fail, as we must have found U+FFFD in order to set
    // mReplacementCharFallbackFamily[...] at all, but better play it safe
    if (fontEntry && fontEntry->HasCharacter(aCh)) {
      return fontEntry->FindOrMakeFont(aStyle);
    }
  }

  TimeStamp start = TimeStamp::Now();

  // search commonly available fonts
  bool common = true;
  FontFamily fallbackFamily;
  RefPtr<gfxFont> candidate =
      CommonFontFallback(aFontVisibilityProvider, aCh, aNextCh, aRunScript,
                         aPresentation, aStyle, fallbackFamily);
  RefPtr<gfxFont> font;
  if (candidate) {
    if (aPresentation == FontPresentation::Any) {
      font = std::move(candidate);
    } else {
      bool hasColorGlyph = candidate->HasColorGlyphFor(aCh, aNextCh);
      if (hasColorGlyph == PrefersColor(aPresentation)) {
        font = std::move(candidate);
      }
    }
  }

  // If we didn't find a common font, or it was not the preferred type (color
  // or monochrome), do system-wide fallback (except for specials).
  uint32_t cmapCount = 0;
  if (!font) {
    common = false;
    font = GlobalFontFallback(aFontVisibilityProvider, aCh, aNextCh, aRunScript,
                              aPresentation, aStyle, cmapCount, fallbackFamily);
    // If the font we found doesn't match the requested type, and we also found
    // a candidate above, prefer that one.
    if (font && aPresentation != FontPresentation::Any && candidate) {
      bool hasColorGlyph = font->HasColorGlyphFor(aCh, aNextCh);
      if (hasColorGlyph != PrefersColor(aPresentation)) {
        font = std::move(candidate);
      }
    }
  }
  TimeDuration elapsed = TimeStamp::Now() - start;

  LogModule* log = gfxPlatform::GetLog(eGfxLog_textrun);

  if (MOZ_UNLIKELY(MOZ_LOG_TEST(log, LogLevel::Warning))) {
    Script script = intl::UnicodeProperties::GetScriptCode(aCh);
    MOZ_LOG(log, LogLevel::Warning,
            ("(textrun-systemfallback-%s) char: u+%6.6x "
             "script: %d match: [%s]"
             " time: %dus cmaps: %d\n",
             (common ? "common" : "global"), aCh, static_cast<int>(script),
             (font ? font->GetFontEntry()->Name().get() : "<none>"),
             int32_t(elapsed.ToMicroseconds()), cmapCount));
  }

  // no match? add to set of non-matching codepoints
  if (!font) {
    mCodepointsWithNoFonts[level].set(aCh);
  } else {
    *aVisibility = fallbackFamily.mShared
                       ? fallbackFamily.mShared->Visibility()
                       : fallbackFamily.mUnshared->Visibility();
    if (aCh == 0xFFFD) {
      mReplacementCharFallbackFamily[level] = fallbackFamily;
    }
  }

  // track system fallback time
  static bool first = true;
  if (first)
    glean::fontlist::system_font_fallback_first.AccumulateRawDuration(elapsed);
  else
    glean::fontlist::system_font_fallback.AccumulateRawDuration(elapsed);
  first = false;

  return font.forget();
}

#define NUM_FALLBACK_FONTS 8

already_AddRefed<gfxFont> gfxPlatformFontList::CommonFontFallback(
    FontVisibilityProvider* aFontVisibilityProvider, uint32_t aCh,
    uint32_t aNextCh, Script aRunScript, FontPresentation aPresentation,
    const gfxFontStyle* aMatchStyle, FontFamily& aMatchedFamily) {
  AutoTArray<const char*, NUM_FALLBACK_FONTS> defaultFallbacks;
  gfxPlatform::GetPlatform()->GetCommonFallbackFonts(
      aCh, aRunScript, aPresentation, defaultFallbacks);
  GlobalFontMatch data(aCh, aNextCh, *aMatchStyle, aPresentation);
  FontVisibility level = aFontVisibilityProvider
                             ? aFontVisibilityProvider->GetFontVisibility()
                             : FontVisibility::User;

  // If a color-emoji presentation is requested, we will check any font found
  // to see if it can provide this; if not, we'll remember it as a possible
  // candidate but search the remainder of the list for a better choice.
  RefPtr<gfxFont> candidateFont;
  FontFamily candidateFamily;
  auto check = [&](gfxFontEntry* aFontEntry,
                   FontFamily aFamily) -> already_AddRefed<gfxFont> {
    RefPtr<gfxFont> font = aFontEntry->FindOrMakeFont(aMatchStyle);
    if (aPresentation < FontPresentation::EmojiDefault ||
        font->HasColorGlyphFor(aCh, aNextCh)) {
      aMatchedFamily = aFamily;
      return font.forget();
    }
    // We want a color glyph but this font only has monochrome; remember it
    // (unless we already have a candidate) but continue to search.
    if (!candidateFont) {
      candidateFont = std::move(font);
      candidateFamily = aFamily;
    }
    return nullptr;
  };

  if (SharedFontList()) {
    for (const auto name : defaultFallbacks) {
      fontlist::Family* family =
          FindSharedFamily(aFontVisibilityProvider, nsDependentCString(name));
      if (!family || !IsVisibleToCSS(*family, level)) {
        continue;
      }
      // XXX(jfkthame) Should we fire the async cmap-loader here, or let it
      // always do a potential sync initialization of the family?
      family->SearchAllFontsForChar(SharedFontList(), &data);
      if (data.mBestMatch) {
        RefPtr<gfxFont> font = check(data.mBestMatch, FontFamily(family));
        if (font) {
          return font.forget();
        }
      }
    }
  } else {
    for (const auto name : defaultFallbacks) {
      gfxFontFamily* fallback =
          FindFamilyByCanonicalName(nsDependentCString(name));
      if (!fallback || !IsVisibleToCSS(*fallback, level)) {
        continue;
      }
      fallback->FindFontForChar(&data);
      if (data.mBestMatch) {
        RefPtr<gfxFont> font = check(data.mBestMatch, FontFamily(fallback));
        if (font) {
          return font.forget();
        }
      }
    }
  }

  // If we had a candidate that supports the character, but doesn't have the
  // desired emoji-style glyph, we'll return it anyhow as nothing better was
  // found.
  if (candidateFont) {
    aMatchedFamily = candidateFamily;
    return candidateFont.forget();
  }

  return nullptr;
}

already_AddRefed<gfxFont> gfxPlatformFontList::GlobalFontFallback(
    FontVisibilityProvider* aFontVisibilityProvider, uint32_t aCh,
    uint32_t aNextCh, Script aRunScript, FontPresentation aPresentation,
    const gfxFontStyle* aMatchStyle, uint32_t& aCmapCount,
    FontFamily& aMatchedFamily) {
  bool useCmaps = IsFontFamilyWhitelistActive() ||
                  gfxPlatform::GetPlatform()->UseCmapsDuringSystemFallback();
  FontVisibility level = aFontVisibilityProvider
                             ? aFontVisibilityProvider->GetFontVisibility()
                             : FontVisibility::User;
  if (!useCmaps) {
    // Allow platform-specific fallback code to try and find a usable font
    gfxFontEntry* fe = PlatformGlobalFontFallback(
        aFontVisibilityProvider, aCh, aRunScript, aMatchStyle, aMatchedFamily);
    if (fe) {
      if (aMatchedFamily.mShared) {
        if (IsVisibleToCSS(*aMatchedFamily.mShared, level)) {
          RefPtr<gfxFont> font = fe->FindOrMakeFont(aMatchStyle);
          if (font) {
            if (aPresentation == FontPresentation::Any) {
              return font.forget();
            }
            bool hasColorGlyph = font->HasColorGlyphFor(aCh, aNextCh);
            if (hasColorGlyph == PrefersColor(aPresentation)) {
              return font.forget();
            }
          }
        }
      } else {
        if (IsVisibleToCSS(*aMatchedFamily.mUnshared, level)) {
          RefPtr<gfxFont> font = fe->FindOrMakeFont(aMatchStyle);
          if (font) {
            if (aPresentation == FontPresentation::Any) {
              return font.forget();
            }
            bool hasColorGlyph = font->HasColorGlyphFor(aCh, aNextCh);
            if (hasColorGlyph == PrefersColor(aPresentation)) {
              return font.forget();
            }
          }
        }
      }
    }
  }

  // otherwise, try to find it among local fonts
  GlobalFontMatch data(aCh, aNextCh, *aMatchStyle, aPresentation);
  if (SharedFontList()) {
    fontlist::Family* families = SharedFontList()->Families();
    if (families) {
      for (uint32_t i = 0; i < SharedFontList()->NumFamilies(); i++) {
        fontlist::Family& family = families[i];
        if (!IsVisibleToCSS(family, level)) {
          continue;
        }
        if (!family.IsFullyInitialized() &&
            StaticPrefs::gfx_font_rendering_fallback_async() &&
            !XRE_IsParentProcess()) {
          // Start loading all the missing charmaps; but this is async,
          // so for now we just continue, ignoring this family.
          StartCmapLoadingFromFamily(i);
        } else {
          family.SearchAllFontsForChar(SharedFontList(), &data);
          if (data.mMatchDistance == 0.0) {
            // no better style match is possible, so stop searching
            break;
          }
        }
      }
      if (data.mBestMatch) {
        aMatchedFamily = FontFamily(data.mMatchedSharedFamily);
        return data.mBestMatch->FindOrMakeFont(aMatchStyle);
      }
    }
  } else {
    // iterate over all font families to find a font that support the
    // character
    for (const RefPtr<gfxFontFamily>& family : mFontFamilies.Values()) {
      if (!IsVisibleToCSS(*family, level)) {
        continue;
      }
      // evaluate all fonts in this family for a match
      family->FindFontForChar(&data);
      if (data.mMatchDistance == 0.0) {
        // no better style match is possible, so stop searching
        break;
      }
    }

    aCmapCount = data.mCmapsTested;
    if (data.mBestMatch) {
      aMatchedFamily = FontFamily(data.mMatchedFamily);
      return data.mBestMatch->FindOrMakeFont(aMatchStyle);
    }
  }

  return nullptr;
}

class StartCmapLoadingRunnable : public mozilla::Runnable {
 public:
  explicit StartCmapLoadingRunnable(uint32_t aStartIndex)
      : Runnable("gfxPlatformFontList::StartCmapLoadingRunnable"),
        mStartIndex(aStartIndex) {}

  NS_IMETHOD Run() override {
    auto* pfl = gfxPlatformFontList::PlatformFontList();
    auto* list = pfl->SharedFontList();
    if (!list) {
      return NS_OK;
    }
    if (mStartIndex >= list->NumFamilies()) {
      return NS_OK;
    }
    if (XRE_IsParentProcess()) {
      pfl->StartCmapLoading(pfl->GetGeneration(), mStartIndex);
    } else {
      dom::ContentChild::GetSingleton()->SendStartCmapLoading(
          pfl->GetGeneration(), mStartIndex);
    }
    return NS_OK;
  }

 private:
  uint32_t mStartIndex;
};

void gfxPlatformFontList::StartCmapLoadingFromFamily(uint32_t aStartIndex) {
  AutoLock lock(mLock);
  if (aStartIndex >= mStartedLoadingCmapsFrom) {
    // We already initiated cmap-loading from here or earlier in the list;
    // no need to do it again here.
    return;
  }
  mStartedLoadingCmapsFrom = aStartIndex;

  // If we're already on the main thread, don't bother dispatching a runnable
  // here to kick off the loading process, just do it directly.
  if (NS_IsMainThread()) {
    if (XRE_IsParentProcess()) {
      StartCmapLoading(GetGeneration(), aStartIndex);
    } else {
      dom::ContentChild::GetSingleton()->SendStartCmapLoading(GetGeneration(),
                                                              aStartIndex);
    }
  } else {
    NS_DispatchToMainThread(new StartCmapLoadingRunnable(aStartIndex));
  }
}

class LoadCmapsRunnable final : public IdleRunnable,
                                public nsIObserver,
                                public nsSupportsWeakReference {
  NS_DECL_ISUPPORTS_INHERITED
  NS_DECL_NSIOBSERVER

 private:
  virtual ~LoadCmapsRunnable() {
    if (nsCOMPtr<nsIObserverService> obs = services::GetObserverService()) {
      obs->RemoveObserver(this, NS_XPCOM_WILL_SHUTDOWN_OBSERVER_ID);
    }
  }

 public:
  LoadCmapsRunnable(uint32_t aGeneration, uint32_t aFamilyIndex)
      : IdleRunnable("gfxPlatformFontList::LoadCmapsRunnable"),
        mGeneration(aGeneration),
        mStartIndex(aFamilyIndex),
        mIndex(aFamilyIndex) {}

  void SetDeadline(TimeStamp aDeadline) override { mDeadline = aDeadline; }

  // Reset the current family index, if the value passed is earlier than our
  // original starting position. We don't "reset" if it would move the current
  // position forward, or back into the already-scanned range.
  // We could optimize further by remembering the current position reached,
  // and then skipping ahead from the original start, but it doesn't seem worth
  // extra complexity for a task that usually only happens once, and already-
  // processed families will be skipped pretty quickly in Run() anyhow.
  void MaybeResetIndex(uint32_t aFamilyIndex) {
    if (aFamilyIndex < mStartIndex) {
      mStartIndex = aFamilyIndex;
      mIndex = aFamilyIndex;
    }
  }

  void Cancel() { mIsCanceled = true; }

  NS_IMETHOD Run() override {
    if (mIsCanceled) {
      return NS_OK;
    }
    auto* pfl = gfxPlatformFontList::PlatformFontList();
    auto* list = pfl->SharedFontList();
    MOZ_ASSERT(list);
    if (!list) {
      return NS_OK;
    }
    if (mGeneration != pfl->GetGeneration()) {
      return NS_OK;
    }
    uint32_t numFamilies = list->NumFamilies();
    if (mIndex >= numFamilies) {
      return NS_OK;
    }
    auto* families = list->Families();
    while (mIndex < numFamilies) {
      auto& family = families[mIndex++];
      if (family.IsFullyInitialized()) {
        // Skip any families that are already initialized.
        continue;
      }
      // Fully initialize this family.
      (void)pfl->InitializeFamily(&family, true);
      // TODO(emilio): It'd make sense to use mDeadline here to determine
      // whether we can do more work, but that is surprisingly a performance
      // regression in practice, see bug 1936489. Investigate if we can be
      // smarter about this.
      break;
    }
    // If there are more families to initialize, post ourselves back to the
    // idle queue handle the next ones; otherwise we're finished and we need
    // to notify content processes to update their rendering.
    if (mIndex < numFamilies) {
      mDeadline = TimeStamp();
      NS_DispatchToMainThreadQueue(do_AddRef(this), EventQueuePriority::Idle);
    } else {
      pfl->Lock();
      pfl->CancelLoadCmapsTask();
      pfl->InitializeCodepointsWithNoFonts();
      dom::ContentParent::NotifyUpdatedFonts(false);
      pfl->Unlock();
    }
    return NS_OK;
  }

 private:
  uint32_t mGeneration;
  uint32_t mStartIndex;
  uint32_t mIndex;
  TimeStamp mDeadline;
  bool mIsCanceled = false;
};

NS_IMPL_ISUPPORTS_INHERITED(LoadCmapsRunnable, IdleRunnable, nsIObserver,
                            nsISupportsWeakReference);

NS_IMETHODIMP
LoadCmapsRunnable::Observe(nsISupports* aSubject, const char* aTopic,
                           const char16_t* aData) {
  MOZ_ASSERT(!nsCRT::strcmp(aTopic, NS_XPCOM_WILL_SHUTDOWN_OBSERVER_ID),
             "unexpected topic");
  Cancel();
  return NS_OK;
}

void gfxPlatformFontList::CancelLoadCmapsTask() {
  if (mLoadCmapsRunnable) {
    mLoadCmapsRunnable->Cancel();
    mLoadCmapsRunnable = nullptr;
  }
}

void gfxPlatformFontList::StartCmapLoading(uint32_t aGeneration,
                                           uint32_t aStartIndex) {
  MOZ_RELEASE_ASSERT(XRE_IsParentProcess());
  if (aGeneration != GetGeneration()) {
    return;
  }
  if (AppShutdown::IsInOrBeyond(ShutdownPhase::AppShutdownConfirmed)) {
    return;
  }
  if (mLoadCmapsRunnable) {
    // We already have a runnable; just make sure it covers the full range of
    // families needed.
    mLoadCmapsRunnable->MaybeResetIndex(aStartIndex);
    return;
  }
  mLoadCmapsRunnable = new LoadCmapsRunnable(aGeneration, aStartIndex);
  if (nsCOMPtr<nsIObserverService> obs = services::GetObserverService()) {
    obs->AddObserver(mLoadCmapsRunnable, NS_XPCOM_WILL_SHUTDOWN_OBSERVER_ID,
                     /* ownsWeak = */ true);
  }
  NS_DispatchToMainThreadQueue(do_AddRef(mLoadCmapsRunnable),
                               EventQueuePriority::Idle);
}

gfxFontFamily* gfxPlatformFontList::CheckFamily(gfxFontFamily* aFamily) {
  if (aFamily && !aFamily->HasStyles()) {
    aFamily->FindStyleVariations();
  }

  if (aFamily && aFamily->FontListLength() == 0) {
    // Failed to load any faces for this family, so discard it.
    nsAutoCString key;
    GenerateFontListKey(aFamily->Name(), key);
    mFontFamilies.Remove(key);
    return nullptr;
  }

  return aFamily;
}

bool gfxPlatformFontList::FindAndAddFamilies(
    FontVisibilityProvider* aFontVisibilityProvider,
    StyleGenericFontFamily aGeneric, const nsACString& aFamily,
    nsTArray<FamilyAndGeneric>* aOutput, FindFamiliesFlags aFlags,
    gfxFontStyle* aStyle, nsAtom* aLanguage, gfxFloat aDevToCssSize) {
  AutoLock lock(mLock);

#ifdef DEBUG
  auto initialLength = aOutput->Length();
#endif

  bool didFind = FindAndAddFamiliesLocked(aFontVisibilityProvider, aGeneric,
                                          aFamily, aOutput, aFlags, aStyle,
                                          aLanguage, aDevToCssSize);
#ifdef DEBUG
  auto finalLength = aOutput->Length();
  // Validate the expectation that the output-array grows if we return true,
  // or remains the same (probably empty) if we return false.
  MOZ_ASSERT_IF(didFind, finalLength > initialLength);
  MOZ_ASSERT_IF(!didFind, finalLength == initialLength);
#endif

  return didFind;
}

bool gfxPlatformFontList::FindAndAddFamiliesLocked(
    FontVisibilityProvider* aFontVisibilityProvider,
    StyleGenericFontFamily aGeneric, const nsACString& aFamily,
    nsTArray<FamilyAndGeneric>* aOutput, FindFamiliesFlags aFlags,
    gfxFontStyle* aStyle, nsAtom* aLanguage, gfxFloat aDevToCssSize) {
  nsAutoCString key;
  GenerateFontListKey(aFamily, key);

  bool allowHidden = bool(aFlags & FindFamiliesFlags::eSearchHiddenFamilies);
  FontVisibility visibilityLevel =
      aFontVisibilityProvider ? aFontVisibilityProvider->GetFontVisibility()
                              : FontVisibility::User;

  // If this font lookup is the result of resolving a CSS generic (not a direct
  // font-family request by the page), and RFP settings allow generics to be
  // unrestricted, bump the effective visibility level applied here so as to
  // allow user-installed fonts to be used.
  if (visibilityLevel < FontVisibility::User &&
      aGeneric != StyleGenericFontFamily::None &&
      !aFontVisibilityProvider->ShouldResistFingerprinting(
          RFPTarget::FontVisibilityRestrictGenerics)) {
    visibilityLevel = FontVisibility::User;
  }

  if (SharedFontList()) {
    fontlist::Family* family = SharedFontList()->FindFamily(key);
    // If not found, and other family names have not yet been initialized,
    // initialize the rest of the list and try again. This is done lazily
    // since reading name table entries is expensive.
    // Although ASCII localized family names are possible they don't occur
    // in practice, so avoid pulling in names at startup.
    if (!family && !mOtherFamilyNamesInitialized) {
      bool triggerLoading = true;
      bool mayDefer =
          !(aFlags & FindFamiliesFlags::eForceOtherFamilyNamesLoading);
      if (IsAscii(key)) {
        // If `key` is an ASCII name, only trigger loading if it includes a
        // space, and the "base" name (up to the last space) exists as a known
        // family, so that this might be a legacy styled-family name.
        const char* data = key.BeginReading();
        int32_t index = key.Length();
        while (--index > 0) {
          if (data[index] == ' ') {
            break;
          }
        }
        if (index <= 0 ||
            !SharedFontList()->FindFamily(nsAutoCString(key.get(), index))) {
          triggerLoading = false;
        }
      }
      if (triggerLoading) {
        if (InitOtherFamilyNames(mayDefer)) {
          family = SharedFontList()->FindFamily(key);
        }
      }
      if (!family && !mOtherFamilyNamesInitialized &&
          !(aFlags & FindFamiliesFlags::eNoAddToNamesMissedWhenSearching)) {
        AddToMissedNames(key);
      }
    }
    // Check whether the family we found is actually allowed to be looked up,
    // according to current font-visibility prefs.
    if (family) {
      bool visible = IsVisibleToCSS(*family, visibilityLevel);
      if (visible || (allowHidden && family->IsHidden())) {
        aOutput->AppendElement(FamilyAndGeneric(family, aGeneric));
        return true;
      }
      if (aFontVisibilityProvider) {
        aFontVisibilityProvider->ReportBlockedFontFamily(*family);
      }
    }
    return false;
  }

  NS_ASSERTION(mFontFamilies.Count() != 0,
               "system font list was not initialized correctly");

  auto isBlockedByVisibilityLevel = [=, this](gfxFontFamily* aFamily) -> bool {
    bool visible = IsVisibleToCSS(*aFamily, visibilityLevel);
    if (visible || (allowHidden && aFamily->IsHidden())) {
      return false;
    }
    if (aFontVisibilityProvider) {
      aFontVisibilityProvider->ReportBlockedFontFamily(*aFamily);
    }
    return true;
  };

  // lookup in canonical (i.e. English) family name list
  gfxFontFamily* familyEntry = mFontFamilies.GetWeak(key);
  if (familyEntry) {
    if (isBlockedByVisibilityLevel(familyEntry)) {
      return false;
    }
  }

  // if not found, lookup in other family names list (mostly localized names)
  if (!familyEntry) {
    familyEntry = mOtherFamilyNames.GetWeak(key);
  }
  if (familyEntry) {
    if (isBlockedByVisibilityLevel(familyEntry)) {
      return false;
    }
  }

  // if still not found and other family names not yet fully initialized,
  // initialize the rest of the list and try again.  this is done lazily
  // since reading name table entries is expensive.
  // although ASCII localized family names are possible they don't occur
  // in practice so avoid pulling in names at startup
  if (!familyEntry && !mOtherFamilyNamesInitialized && !IsAscii(aFamily)) {
    InitOtherFamilyNames(
        !(aFlags & FindFamiliesFlags::eForceOtherFamilyNamesLoading));
    familyEntry = mOtherFamilyNames.GetWeak(key);
    if (!familyEntry && !mOtherFamilyNamesInitialized &&
        !(aFlags & FindFamiliesFlags::eNoAddToNamesMissedWhenSearching)) {
      // localized family names load timed out, add name to list of
      // names to check after localized names are loaded
      AddToMissedNames(key);
    }
    if (familyEntry) {
      if (isBlockedByVisibilityLevel(familyEntry)) {
        return false;
      }
    }
  }

  familyEntry = CheckFamily(familyEntry);

  // If we failed to find the requested family, check for a space in the
  // name; if found, and if the "base" name (up to the last space) exists
  // as a family, then this might be a legacy GDI-style family name for
  // an additional weight/width. Try searching the faces of the base family
  // and create any corresponding legacy families.
  if (!familyEntry &&
      !(aFlags & FindFamiliesFlags::eNoSearchForLegacyFamilyNames)) {
    // We don't have nsAString::RFindChar, so look for a space manually
    const char* data = aFamily.BeginReading();
    int32_t index = aFamily.Length();
    while (--index > 0) {
      if (data[index] == ' ') {
        break;
      }
    }
    if (index > 0) {
      gfxFontFamily* base = FindUnsharedFamily(
          aFontVisibilityProvider, Substring(aFamily, 0, index),
          FindFamiliesFlags::eNoSearchForLegacyFamilyNames);
      // If we found the "base" family name, and if it has members with
      // legacy names, this will add corresponding font-family entries to
      // the mOtherFamilyNames list; then retry the legacy-family search.
      if (base && base->CheckForLegacyFamilyNames(this)) {
        familyEntry = mOtherFamilyNames.GetWeak(key);
      }
      if (familyEntry) {
        if (isBlockedByVisibilityLevel(familyEntry)) {
          return false;
        }
      }
    }
  }

  if (familyEntry) {
    aOutput->AppendElement(FamilyAndGeneric(familyEntry, aGeneric));
    return true;
  }

  return false;
}

void gfxPlatformFontList::AddToMissedNames(const nsCString& aKey) {
  if (!mOtherNamesMissed) {
    mOtherNamesMissed = MakeUnique<nsTHashSet<nsCString>>(2);
  }
  mOtherNamesMissed->Insert(aKey);
}

fontlist::Family* gfxPlatformFontList::FindSharedFamily(
    FontVisibilityProvider* aFontVisibilityProvider, const nsACString& aFamily,
    FindFamiliesFlags aFlags, gfxFontStyle* aStyle, nsAtom* aLanguage,
    gfxFloat aDevToCss) {
  if (!SharedFontList()) {
    return nullptr;
  }
  AutoTArray<FamilyAndGeneric, 1> families;
  if (!FindAndAddFamiliesLocked(
          aFontVisibilityProvider, StyleGenericFontFamily::None, aFamily,
          &families, aFlags, aStyle, aLanguage, aDevToCss) ||
      !families[0].mFamily.mShared) {
    return nullptr;
  }
  fontlist::Family* family = families[0].mFamily.mShared;
  if (!family->IsInitialized()) {
    if (!InitializeFamily(family)) {
      return nullptr;
    }
  }
  return family;
}

class InitializeFamilyRunnable : public mozilla::Runnable {
 public:
  explicit InitializeFamilyRunnable(uint32_t aFamilyIndex, bool aLoadCmaps)
      : Runnable("gfxPlatformFontList::InitializeFamilyRunnable"),
        mIndex(aFamilyIndex),
        mLoadCmaps(aLoadCmaps) {}

  NS_IMETHOD Run() override {
    auto* pfl = gfxPlatformFontList::PlatformFontList();
    auto* list = pfl->SharedFontList();
    if (!list) {
      return NS_OK;
    }
    if (mIndex >= list->NumFamilies()) {
      // Out of range? Maybe the list got reinitialized since this request
      // was posted - just ignore it.
      return NS_OK;
    }
    auto& family = list->Families()[mIndex];
    if (mLoadCmaps ? family.IsFullyInitialized() : family.IsInitialized()) {
      // Some other runnable initialized this or somesuch.
      return NS_OK;
    }
    (void)pfl->InitializeFamily(&family, mLoadCmaps);
    return NS_OK;
  }

 private:
  uint32_t mIndex;
  bool mLoadCmaps;
};

bool gfxPlatformFontList::InitializeFamily(fontlist::Family* aFamily,
                                           bool aLoadCmaps) {
  MOZ_ASSERT(SharedFontList());
  auto list = SharedFontList();
  auto* families = list->Families();
  if (!families) {
    return false;
  }
  uint32_t index = aFamily - families;
  if (index >= list->NumFamilies()) {
    return false;
  }
  if (!NS_IsMainThread() && (!sInitFontListThread || !IsInitFontListThread())) {
    NS_DispatchToMainThread(new InitializeFamilyRunnable(index, aLoadCmaps));
    return aFamily->IsInitialized();
  }
  if (!XRE_IsParentProcess()) {
    dom::ContentChild::GetSingleton()->SendInitializeFamily(GetGeneration(),
                                                            index, aLoadCmaps);
    return aFamily->IsInitialized();
  }

  if (!aFamily->IsInitialized()) {
    // The usual case: we're being asked to populate the face list.
    AutoTArray<fontlist::Face::InitData, 16> faceList;
    GetFacesInitDataForFamily(aFamily, faceList, aLoadCmaps);
    aFamily->AddFaces(list, faceList);
  } else {
    // The family's face list was already initialized, but if aLoadCmaps is
    // true we also want to eagerly load character maps. This is used when a
    // child process is doing SearchAllFontsForChar, to have the parent load
    // all the cmaps at once and reduce IPC traffic (and content-process file
    // access overhead, which is crippling for DirectWrite on Windows).
    if (aLoadCmaps) {
      if (auto* faces = aFamily->Faces(list)) {
        for (size_t i = 0; i < aFamily->NumFaces(); i++) {
          auto* face = faces[i].ToPtr<fontlist::Face>(list);
          if (face && face->mCharacterMap.IsNull()) {
            // We don't want to cache this font entry, as the parent will most
            // likely never use it again; it's just to populate the charmap for
            // the benefit of the child process.
            RefPtr<gfxFontEntry> fe = CreateFontEntry(face, aFamily);
            if (fe) {
              fe->ReadCMAP();
            }
          }
        }
      }
    }
  }

  if (aLoadCmaps && aFamily->IsInitialized()) {
    aFamily->SetupFamilyCharMap(list);
  }

  return aFamily->IsInitialized();
}

gfxFontEntry* gfxPlatformFontList::FindFontForFamily(
    FontVisibilityProvider* aFontVisibilityProvider, const nsACString& aFamily,
    const gfxFontStyle* aStyle) {
  AutoLock lock(mLock);

  nsAutoCString key;
  GenerateFontListKey(aFamily, key);

  FontFamily family = FindFamily(aFontVisibilityProvider, key);
  if (family.IsNull()) {
    return nullptr;
  }
  if (family.mShared) {
    auto face = family.mShared->FindFaceForStyle(SharedFontList(), *aStyle);
    if (!face) {
      return nullptr;
    }
    return GetOrCreateFontEntryLocked(face, family.mShared);
  }
  return family.mUnshared->FindFontForStyle(*aStyle);
}

gfxFontEntry* gfxPlatformFontList::GetOrCreateFontEntryLocked(
    fontlist::Face* aFace, const fontlist::Family* aFamily) {
  return mFontEntries
      .LookupOrInsertWith(aFace,
                          [=, this] { return CreateFontEntry(aFace, aFamily); })
      .get();
}

void gfxPlatformFontList::AddOtherFamilyNames(
    gfxFontFamily* aFamilyEntry, const nsTArray<nsCString>& aOtherFamilyNames) {
  AutoLock lock(mLock);

  for (const auto& name : aOtherFamilyNames) {
    nsAutoCString key;
    GenerateFontListKey(name, key);

    mOtherFamilyNames.LookupOrInsertWith(key, [&] {
      LOG_FONTLIST(
          ("(fontlist-otherfamily) canonical family: %s, other family: "
           "%s\n",
           aFamilyEntry->Name().get(), name.get()));
      if (mBadUnderlineFamilyNames.ContainsSorted(key)) {
        aFamilyEntry->SetBadUnderlineFamily();
      }
      return RefPtr{aFamilyEntry};
    });
  }
}

void gfxPlatformFontList::AddFullnameLocked(gfxFontEntry* aFontEntry,
                                            const nsCString& aFullname) {
  mExtraNames->mFullnames.LookupOrInsertWith(aFullname, [&] {
    LOG_FONTLIST(("(fontlist-fullname) name: %s, fullname: %s\n",
                  aFontEntry->Name().get(), aFullname.get()));
    return RefPtr{aFontEntry};
  });
}

void gfxPlatformFontList::AddPostscriptNameLocked(
    gfxFontEntry* aFontEntry, const nsCString& aPostscriptName) {
  mExtraNames->mPostscriptNames.LookupOrInsertWith(aPostscriptName, [&] {
    LOG_FONTLIST(("(fontlist-postscript) name: %s, psname: %s\n",
                  aFontEntry->Name().get(), aPostscriptName.get()));
    return RefPtr{aFontEntry};
  });
}

bool gfxPlatformFontList::GetStandardFamilyName(const nsCString& aFontName,
                                                nsACString& aFamilyName) {
  AutoLock lock(mLock);
  FontFamily family = FindFamily(nullptr, aFontName);
  if (family.IsNull()) {
    return false;
  }
  return GetLocalizedFamilyName(family, aFamilyName);
}

bool gfxPlatformFontList::GetLocalizedFamilyName(const FontFamily& aFamily,
                                                 nsACString& aFamilyName) {
  if (aFamily.mShared) {
    aFamilyName = SharedFontList()->LocalizedFamilyName(aFamily.mShared);
    return true;
  }
  if (aFamily.mUnshared) {
    aFamily.mUnshared->LocalizedName(aFamilyName);
    return true;
  }
  return false;  // leaving the aFamilyName outparam untouched
}

FamilyAndGeneric gfxPlatformFontList::GetDefaultFontFamily(
    const nsACString& aLangGroup, const nsACString& aGenericFamily) {
  if (NS_WARN_IF(aLangGroup.IsEmpty()) ||
      NS_WARN_IF(aGenericFamily.IsEmpty())) {
    return FamilyAndGeneric();
  }

  AutoLock lock(mLock);

  nsAutoCString value;
  AutoTArray<nsCString, 4> names;
  if (mFontPrefs->LookupNameList(PrefName(aGenericFamily, aLangGroup), value)) {
    gfxFontUtils::ParseFontList(value, names);
  }

  for (const nsCString& name : names) {
    FontFamily family = FindFamily(nullptr, name);
    if (!family.IsNull()) {
      return FamilyAndGeneric(family);
    }
  }

  return FamilyAndGeneric();
}

ShmemCharMapHashEntry::ShmemCharMapHashEntry(const gfxSparseBitSet* aCharMap)
    : mList(gfxPlatformFontList::PlatformFontList()->SharedFontList()),
      mHash(aCharMap->GetChecksum()) {
  size_t len = SharedBitSet::RequiredSize(*aCharMap);
  mCharMap = mList->Alloc(len);
  SharedBitSet::Create(mCharMap.ToPtr(mList, len), len, *aCharMap);
}

fontlist::Pointer gfxPlatformFontList::GetShmemCharMapLocked(
    const gfxSparseBitSet* aCmap) {
  auto* entry = mShmemCharMaps.GetEntry(aCmap);
  if (!entry) {
    entry = mShmemCharMaps.PutEntry(aCmap);
  }
  return entry->GetCharMap();
}

// Lookup aCmap in the shared cmap set, adding if not already present.
// This is the only way for a reference to a gfxCharacterMap to be acquired
// by another thread than its original creator.
already_AddRefed<gfxCharacterMap> gfxPlatformFontList::FindCharMap(
    gfxCharacterMap* aCmap) {
  // Lock to prevent potentially racing against MaybeRemoveCmap.
  AutoLock lock(mLock);

  // Find existing entry or insert a new one (which will add a reference).
  aCmap->CalcHash();
  aCmap->mShared = true;  // Set the shared flag in preparation for adding
                          // to the global table.
  CharMapLookup lookup{aCmap, aCmap->mHash, /* mCompareByPointer */ false};
  RefPtr cmap = mSharedCmaps.PutEntry(lookup)->GetCharMap();

  // If we ended up finding a different, pre-existing entry, clear the
  // shared flag on this one so that it'll get deleted on Release().
  if (cmap.get() != aCmap) {
    aCmap->mShared = false;
  }

  return cmap.forget();
}

// Potentially remove the charmap from the shared cmap set. This is called
// when a user of the charmap drops a reference and the refcount goes to 1;
// in that case, it is possible our shared set is the only remaining user
// of the object, and we should remove it.
//
// CAUTION: aCharMap may already have been freed by the time we are called
// (another thread may have raced us here and deleted it first, or the
// destructor's teardown path may have run). We MUST NOT dereference aCharMap
// until we have established that it is still present in our table -- the
// table's strong reference then guarantees the object is alive. The hash
// lookup itself uses aHash (captured before the caller's --mRefCnt while the
// object was definitely alive) and pointer-identity comparison, so it never
// dereferences aCharMap.
void gfxPlatformFontList::MaybeRemoveCmap(gfxCharacterMap* aCharMap,
                                          uint32_t aHash) {
  // Lock so that nobody else can get a reference via FindCharMap while we're
  // checking here.
  AutoLock lock(mLock);

  // Skip lookups during teardown.
  if (!mSharedCmaps.Count()) {
    return;
  }

  // Look up by captured hash + pointer identity; this does not dereference
  // aCharMap. If a racing thread already removed and freed aCharMap, GetEntry
  // simply won't find it (the freed slot can't reappear in the table at the
  // same address while we hold mLock, since insertions also need mLock).
  CharMapLookup lookup{aCharMap, aHash, /* mCompareByPointer */ true};
  CharMapHashKey* found = mSharedCmaps.GetEntry(lookup);

  // If found, the table holds a strong ref, so aCharMap is alive and we may
  // dereference it. We must check that it still has a refcount of exactly 1
  // (the table's ref) before we remove; a FindCharMap may have raced us and
  // AddRef'd it.
  if (found && aCharMap->RefCount() == 1) {
    // Forget our reference to the object that's being deleted, without
    // calling Release() on it.
    found->mCharMap.forget().leak();

    // Do the deletion.
    delete aCharMap;

    // Log this as a "Release" to keep leak-checking correct.
    NS_LOG_RELEASE(aCharMap, 0, "gfxCharacterMap");

    mSharedCmaps.RemoveEntry(found);
  }
}

static void GetSystemUIFontFamilies(
    FontVisibilityProvider* aFontVisibilityProvider,
    [[maybe_unused]] nsAtom* aLangGroup, nsTArray<nsCString>& aFamilies) {
  // TODO: On macOS, use CTCreateUIFontForLanguage or such thing (though the
  // code below ends up using [NSFont systemFontOfSize: 0.0].
  nsFont systemFont;
  gfxFontStyle fontStyle;
  nsAutoString systemFontName;
  if (aFontVisibilityProvider
          ? aFontVisibilityProvider->ShouldResistFingerprinting(
                RFPTarget::FontVisibilityRestrictGenerics)
          : nsContentUtils::ShouldResistFingerprinting(
                "aFontVisibilityProvider not available",
                RFPTarget::FontVisibilityRestrictGenerics)) {
#if defined(XP_MACOSX) || defined(MOZ_WIDGET_UIKIT)
    *aFamilies.AppendElement() = "-apple-system"_ns;
    return;
#elif !defined(MOZ_WIDGET_ANDROID)
    *aFamilies.AppendElement() = "sans-serif"_ns;
    return;
#endif
  }
  if (!LookAndFeel::GetFont(StyleSystemFont::Menu, systemFontName, fontStyle)) {
    return;
  }
  systemFontName.Trim("\"'");
  CopyUTF16toUTF8(systemFontName, *aFamilies.AppendElement());
}

void gfxPlatformFontList::ResolveGenericFontNames(
    FontVisibilityProvider* aFontVisibilityProvider,
    StyleGenericFontFamily aGenericType, eFontPrefLang aPrefLang,
    PrefFontList* aGenericFamilies) {
  const char* langGroupStr = GetPrefLangName(aPrefLang);
  const char* generic = GetGenericName(aGenericType);

  if (!generic) {
    return;
  }

  AutoTArray<nsCString, 4> genericFamilies;

  // load family for "font.name.generic.lang"
  PrefName prefName(generic, langGroupStr);
  nsAutoCString value;
  if (mFontPrefs->LookupName(prefName, value)) {
    gfxFontUtils::ParseFontList(value, genericFamilies);
  }

  // load fonts for "font.name-list.generic.lang"
  if (mFontPrefs->LookupNameList(prefName, value)) {
    gfxFontUtils::ParseFontList(value, genericFamilies);
  }

  nsAtom* langGroup = GetLangGroupForPrefLang(aPrefLang);
  MOZ_ASSERT(langGroup, "null lang group for pref lang");

  if (aGenericType == StyleGenericFontFamily::SystemUi) {
    GetSystemUIFontFamilies(aFontVisibilityProvider, langGroup,
                            genericFamilies);
  }

  GetFontFamiliesFromGenericFamilies(aFontVisibilityProvider, aGenericType,
                                     genericFamilies, langGroup,
                                     aGenericFamilies);

#if 0  // dump out generic mappings
    printf("%s ===> ", NamePref(generic, langGroupStr).get());
    for (uint32_t k = 0; k < aGenericFamilies->Length(); k++) {
        if (k > 0) printf(", ");
        printf("%s", (*aGenericFamilies)[k].mIsShared
            ? (*aGenericFamilies)[k].mShared->DisplayName().AsString(SharedFontList()).get()
            : (*aGenericFamilies)[k].mUnshared->Name().get());
    }
    printf("\n");
#endif
}

void gfxPlatformFontList::ResolveEmojiFontNames(
    FontVisibilityProvider* aFontVisibilityProvider,
    PrefFontList* aGenericFamilies) {
  // emoji preference has no lang name
  AutoTArray<nsCString, 4> genericFamilies;

  nsAutoCString value;
  if (mFontPrefs->LookupNameList(PrefName("emoji", ""), value)) {
    gfxFontUtils::ParseFontList(value, genericFamilies);
  }

  GetFontFamiliesFromGenericFamilies(
      aFontVisibilityProvider, StyleGenericFontFamily::MozEmoji,
      genericFamilies, nullptr, aGenericFamilies);
}

void gfxPlatformFontList::GetFontFamiliesFromGenericFamilies(
    FontVisibilityProvider* aFontVisibilityProvider,
    StyleGenericFontFamily aGenericType,
    nsTArray<nsCString>& aGenericNameFamilies, nsAtom* aLangGroup,
    PrefFontList* aGenericFamilies) {
  // lookup and add platform fonts uniquely
  for (const nsCString& genericFamily : aGenericNameFamilies) {
    AutoTArray<FamilyAndGeneric, 10> families;
    FindAndAddFamiliesLocked(aFontVisibilityProvider, aGenericType,
                             genericFamily, &families, FindFamiliesFlags(0),
                             nullptr, aLangGroup);
    for (const FamilyAndGeneric& f : families) {
      if (!aGenericFamilies->Contains(f.mFamily)) {
        aGenericFamilies->AppendElement(f.mFamily);
      }
    }
  }
}

gfxPlatformFontList::PrefFontList*
gfxPlatformFontList::GetPrefFontsLangGroupLocked(
    FontVisibilityProvider* aFontVisibilityProvider,
    StyleGenericFontFamily aGenericType, eFontPrefLang aPrefLang) {
  if (aGenericType == StyleGenericFontFamily::MozEmoji ||
      aPrefLang == eFontPrefLang_Emoji) {
    // Emoji font has no lang
    PrefFontList* prefFonts = mEmojiPrefFont.get();
    if (MOZ_UNLIKELY(!prefFonts)) {
      prefFonts = new PrefFontList;
      ResolveEmojiFontNames(aFontVisibilityProvider, prefFonts);
      mEmojiPrefFont.reset(prefFonts);
    }
    return prefFonts;
  }

  auto index = static_cast<size_t>(aGenericType);
  PrefFontList* prefFonts = mLangGroupPrefFonts[aPrefLang][index].get();
  if (MOZ_UNLIKELY(!prefFonts)) {
    prefFonts = new PrefFontList;
    ResolveGenericFontNames(aFontVisibilityProvider, aGenericType, aPrefLang,
                            prefFonts);
    mLangGroupPrefFonts[aPrefLang][index].reset(prefFonts);
  }
  return prefFonts;
}

void gfxPlatformFontList::AddGenericFonts(
    FontVisibilityProvider* aFontVisibilityProvider,
    StyleGenericFontFamily aGenericType, nsAtom* aLanguage,
    nsTArray<FamilyAndGeneric>& aFamilyList) {
  AutoLock lock(mLock);

  // TODO(eri): For now the math generic language uses the legacy
  // "serif.x-math" configuration font list to avoid excesive
  // repetition until a better font preference system can be found.
  if (StaticPrefs::mathml_font_family_math_enabled() &&
      aGenericType == StyleGenericFontFamily::Math) {
    aGenericType = StyleGenericFontFamily::Serif;
    aLanguage = nsGkAtoms::x_math;
  }

  // map lang ==> langGroup
  nsAtom* langGroup = GetLangGroup(aLanguage);

  // langGroup ==> prefLang
  eFontPrefLang prefLang = GetFontPrefLangFor(langGroup);

  // lookup pref fonts
  PrefFontList* prefFonts = GetPrefFontsLangGroupLocked(aFontVisibilityProvider,
                                                        aGenericType, prefLang);

  if (!prefFonts->IsEmpty()) {
    aFamilyList.SetCapacity(aFamilyList.Length() + prefFonts->Length());
    for (auto& f : *prefFonts) {
      aFamilyList.AppendElement(FamilyAndGeneric(f, aGenericType));
    }
  }
}

static nsAtom* PrefLangToLangGroups(uint32_t aIndex) {
  // static array here avoids static constructor
  static nsAtom* gPrefLangToLangGroups[] = {
#define FONT_PREF_LANG(enum_id_, str_, atom_id_) nsGkAtoms::atom_id_
#include "gfxFontPrefLangList.inc"
#undef FONT_PREF_LANG
  };

  return aIndex < std::size(gPrefLangToLangGroups)
             ? gPrefLangToLangGroups[aIndex]
             : nsGkAtoms::Unicode;
}

eFontPrefLang gfxPlatformFontList::GetFontPrefLangFor(const char* aLang) {
  if (!aLang || !aLang[0]) {
    return eFontPrefLang_Others;
  }
  for (uint32_t i = 0; i < std::size(gPrefLangNames); ++i) {
    if (!nsCRT::strcasecmp(gPrefLangNames[i], aLang)) {
      return eFontPrefLang(i);
    }
    // If the pref-lang is a two-character lang tag, try ignoring any trailing
    // subtags in aLang and see if the base lang matches.
    if (strlen(gPrefLangNames[i]) == 2 && strlen(aLang) > 3 &&
        aLang[2] == '-' && !nsCRT::strncasecmp(gPrefLangNames[i], aLang, 2)) {
      return eFontPrefLang(i);
    }
  }
  return eFontPrefLang_Others;
}

eFontPrefLang gfxPlatformFontList::GetFontPrefLangFor(nsAtom* aLang) {
  if (!aLang) return eFontPrefLang_Others;
  nsAutoCString lang;
  aLang->ToUTF8String(lang);
  return GetFontPrefLangFor(lang.get());
}

nsAtom* gfxPlatformFontList::GetLangGroupForPrefLang(eFontPrefLang aLang) {
  // the special CJK set pref lang should be resolved into separate
  // calls to individual CJK pref langs before getting here
  NS_ASSERTION(aLang != eFontPrefLang_CJKSet, "unresolved CJK set pref lang");

  return PrefLangToLangGroups(uint32_t(aLang));
}

const char* gfxPlatformFontList::GetPrefLangName(eFontPrefLang aLang) {
  if (uint32_t(aLang) < std::size(gPrefLangNames)) {
    return gPrefLangNames[uint32_t(aLang)];
  }
  return nullptr;
}

eFontPrefLang gfxPlatformFontList::GetFontPrefLangFor(uint32_t aCh) {
  switch (ublock_getCode(aCh)) {
    case UBLOCK_BASIC_LATIN:
    case UBLOCK_LATIN_1_SUPPLEMENT:
    case UBLOCK_LATIN_EXTENDED_A:
    case UBLOCK_LATIN_EXTENDED_B:
    case UBLOCK_IPA_EXTENSIONS:
    case UBLOCK_SPACING_MODIFIER_LETTERS:
    case UBLOCK_LATIN_EXTENDED_ADDITIONAL:
    case UBLOCK_LATIN_EXTENDED_C:
    case UBLOCK_LATIN_EXTENDED_D:
    case UBLOCK_LATIN_EXTENDED_E:
    case UBLOCK_PHONETIC_EXTENSIONS:
      return eFontPrefLang_Western;
    case UBLOCK_GREEK:
    case UBLOCK_GREEK_EXTENDED:
      return eFontPrefLang_Greek;
    case UBLOCK_CYRILLIC:
    case UBLOCK_CYRILLIC_SUPPLEMENT:
    case UBLOCK_CYRILLIC_EXTENDED_A:
    case UBLOCK_CYRILLIC_EXTENDED_B:
    case UBLOCK_CYRILLIC_EXTENDED_C:
      return eFontPrefLang_Cyrillic;
    case UBLOCK_ARMENIAN:
      return eFontPrefLang_Armenian;
    case UBLOCK_HEBREW:
      return eFontPrefLang_Hebrew;
    case UBLOCK_ARABIC:
    case UBLOCK_ARABIC_PRESENTATION_FORMS_A:
    case UBLOCK_ARABIC_PRESENTATION_FORMS_B:
    case UBLOCK_ARABIC_SUPPLEMENT:
    case UBLOCK_ARABIC_EXTENDED_A:
    case UBLOCK_ARABIC_MATHEMATICAL_ALPHABETIC_SYMBOLS:
      return eFontPrefLang_Arabic;
    case UBLOCK_DEVANAGARI:
    case UBLOCK_DEVANAGARI_EXTENDED:
      return eFontPrefLang_Devanagari;
    case UBLOCK_BENGALI:
      return eFontPrefLang_Bengali;
    case UBLOCK_GURMUKHI:
      return eFontPrefLang_Gurmukhi;
    case UBLOCK_GUJARATI:
      return eFontPrefLang_Gujarati;
    case UBLOCK_ORIYA:
      return eFontPrefLang_Oriya;
    case UBLOCK_TAMIL:
      return eFontPrefLang_Tamil;
    case UBLOCK_TELUGU:
      return eFontPrefLang_Telugu;
    case UBLOCK_KANNADA:
      return eFontPrefLang_Kannada;
    case UBLOCK_MALAYALAM:
      return eFontPrefLang_Malayalam;
    case UBLOCK_SINHALA:
    case UBLOCK_SINHALA_ARCHAIC_NUMBERS:
      return eFontPrefLang_Sinhala;
    case UBLOCK_THAI:
      return eFontPrefLang_Thai;
    case UBLOCK_TIBETAN:
      return eFontPrefLang_Tibetan;
    case UBLOCK_GEORGIAN:
    case UBLOCK_GEORGIAN_SUPPLEMENT:
    case UBLOCK_GEORGIAN_EXTENDED:
      return eFontPrefLang_Georgian;
    case UBLOCK_HANGUL_JAMO:
    case UBLOCK_HANGUL_COMPATIBILITY_JAMO:
    case UBLOCK_HANGUL_SYLLABLES:
    case UBLOCK_HANGUL_JAMO_EXTENDED_A:
    case UBLOCK_HANGUL_JAMO_EXTENDED_B:
      return eFontPrefLang_Korean;
    case UBLOCK_ETHIOPIC:
    case UBLOCK_ETHIOPIC_EXTENDED:
    case UBLOCK_ETHIOPIC_SUPPLEMENT:
    case UBLOCK_ETHIOPIC_EXTENDED_A:
      return eFontPrefLang_Ethiopic;
    case UBLOCK_UNIFIED_CANADIAN_ABORIGINAL_SYLLABICS:
    case UBLOCK_UNIFIED_CANADIAN_ABORIGINAL_SYLLABICS_EXTENDED:
      return eFontPrefLang_Canadian;
    case UBLOCK_KHMER:
    case UBLOCK_KHMER_SYMBOLS:
      return eFontPrefLang_Khmer;
    case UBLOCK_CJK_RADICALS_SUPPLEMENT:
    case UBLOCK_KANGXI_RADICALS:
    case UBLOCK_IDEOGRAPHIC_DESCRIPTION_CHARACTERS:
    case UBLOCK_CJK_SYMBOLS_AND_PUNCTUATION:
    case UBLOCK_HIRAGANA:
    case UBLOCK_KATAKANA:
    case UBLOCK_BOPOMOFO:
    case UBLOCK_KANBUN:
    case UBLOCK_BOPOMOFO_EXTENDED:
    case UBLOCK_ENCLOSED_CJK_LETTERS_AND_MONTHS:
    case UBLOCK_CJK_COMPATIBILITY:
    case UBLOCK_CJK_UNIFIED_IDEOGRAPHS_EXTENSION_A:
    case UBLOCK_CJK_UNIFIED_IDEOGRAPHS:
    case UBLOCK_CJK_COMPATIBILITY_IDEOGRAPHS:
    case UBLOCK_CJK_COMPATIBILITY_FORMS:
    case UBLOCK_SMALL_FORM_VARIANTS:
    case UBLOCK_HALFWIDTH_AND_FULLWIDTH_FORMS:
    case UBLOCK_CJK_UNIFIED_IDEOGRAPHS_EXTENSION_B:
    case UBLOCK_CJK_COMPATIBILITY_IDEOGRAPHS_SUPPLEMENT:
    case UBLOCK_KATAKANA_PHONETIC_EXTENSIONS:
    case UBLOCK_CJK_STROKES:
    case UBLOCK_VERTICAL_FORMS:
    case UBLOCK_CJK_UNIFIED_IDEOGRAPHS_EXTENSION_C:
    case UBLOCK_KANA_SUPPLEMENT:
    case UBLOCK_CJK_UNIFIED_IDEOGRAPHS_EXTENSION_D:
    case UBLOCK_CJK_UNIFIED_IDEOGRAPHS_EXTENSION_E:
    case UBLOCK_IDEOGRAPHIC_SYMBOLS_AND_PUNCTUATION:
    case UBLOCK_CJK_UNIFIED_IDEOGRAPHS_EXTENSION_F:
    case UBLOCK_KANA_EXTENDED_A:
      return eFontPrefLang_CJKSet;
    case UBLOCK_MATHEMATICAL_OPERATORS:
    case UBLOCK_MATHEMATICAL_ALPHANUMERIC_SYMBOLS:
    case UBLOCK_MISCELLANEOUS_MATHEMATICAL_SYMBOLS_A:
    case UBLOCK_MISCELLANEOUS_MATHEMATICAL_SYMBOLS_B:
    case UBLOCK_SUPPLEMENTAL_MATHEMATICAL_OPERATORS:
      return eFontPrefLang_Mathematics;
    default:
      return eFontPrefLang_Others;
  }
}

bool gfxPlatformFontList::IsLangCJK(eFontPrefLang aLang) {
  switch (aLang) {
    case eFontPrefLang_Japanese:
    case eFontPrefLang_ChineseTW:
    case eFontPrefLang_ChineseCN:
    case eFontPrefLang_ChineseHK:
    case eFontPrefLang_Korean:
    case eFontPrefLang_CJKSet:
      return true;
    default:
      return false;
  }
}

void gfxPlatformFontList::GetLangPrefs(eFontPrefLang aPrefLangs[],
                                       uint32_t& aLen, eFontPrefLang aCharLang,
                                       eFontPrefLang aPageLang) {
  AutoLock lock(mLock);
  if (IsLangCJK(aCharLang)) {
    AppendCJKPrefLangs(aPrefLangs, aLen, aCharLang, aPageLang);
  } else {
    AppendPrefLang(aPrefLangs, aLen, aCharLang);
  }

  AppendPrefLang(aPrefLangs, aLen, eFontPrefLang_Others);
}

void gfxPlatformFontList::AppendCJKPrefLangs(eFontPrefLang aPrefLangs[],
                                             uint32_t& aLen,
                                             eFontPrefLang aCharLang,
                                             eFontPrefLang aPageLang) {
  // prefer the lang specified by the page *if* CJK
  if (IsLangCJK(aPageLang)) {
    AppendPrefLang(aPrefLangs, aLen, aPageLang);
  }

  // append in cached CJK langs
  for (const auto lang : GetFontPrefs()->CJKPrefLangs()) {
    AppendPrefLang(aPrefLangs, aLen, eFontPrefLang(lang));
  }
}

void gfxPlatformFontList::AppendPrefLang(eFontPrefLang aPrefLangs[],
                                         uint32_t& aLen,
                                         eFontPrefLang aAddLang) {
  if (aLen >= kMaxLenPrefLangList) {
    return;
  }

  // If the lang is already present, just ignore the addition.
  for (const auto lang : Span<eFontPrefLang>(aPrefLangs, aLen)) {
    if (lang == aAddLang) {
      return;
    }
  }

  aPrefLangs[aLen++] = aAddLang;
}

StyleGenericFontFamily gfxPlatformFontList::GetDefaultGeneric(
    eFontPrefLang aLang) {
  if (aLang == eFontPrefLang_Emoji) {
    return StyleGenericFontFamily::MozEmoji;
  }

  AutoLock lock(mLock);

  if (uint32_t(aLang) < std::size(gPrefLangNames)) {
    return mDefaultGenericsLangGroup[uint32_t(aLang)];
  }
  return StyleGenericFontFamily::Serif;
}

FontFamily gfxPlatformFontList::GetDefaultFont(
    FontVisibilityProvider* aFontVisibilityProvider,
    const gfxFontStyle* aStyle) {
  AutoLock lock(mLock);
  return GetDefaultFontLocked(aFontVisibilityProvider, aStyle);
}

FontFamily gfxPlatformFontList::GetDefaultFontLocked(
    FontVisibilityProvider* aFontVisibilityProvider,
    const gfxFontStyle* aStyle) {
  FontFamily family =
      GetDefaultFontForPlatform(aFontVisibilityProvider, aStyle);
  if (!family.IsNull()) {
    return family;
  }
  // Something has gone wrong and we were unable to retrieve a default font
  // from the platform. (Likely the whitelist has blocked all potential
  // default fonts.) As a last resort, we return the first font in our list.
  if (SharedFontList()) {
    MOZ_RELEASE_ASSERT(SharedFontList()->NumFamilies() > 0);
    return FontFamily(SharedFontList()->Families());
  }
  MOZ_RELEASE_ASSERT(mFontFamilies.Count() > 0);
  return FontFamily(mFontFamilies.ConstIter().Data());
}

void gfxPlatformFontList::GetFontFamilyNames(
    nsTArray<nsCString>& aFontFamilyNames) {
  if (SharedFontList()) {
    fontlist::FontList* list = SharedFontList();
    const fontlist::Family* families = list->Families();
    if (families) {
      for (uint32_t i = 0, n = list->NumFamilies(); i < n; i++) {
        const fontlist::Family& family = families[i];
        if (!family.IsHidden()) {
          aFontFamilyNames.AppendElement(family.DisplayName().AsString(list));
        }
      }
    }
  } else {
    for (const RefPtr<gfxFontFamily>& family : mFontFamilies.Values()) {
      if (!family->IsHidden()) {
        aFontFamilyNames.AppendElement(family->Name());
      }
    }
  }
}

nsAtom* gfxPlatformFontList::GetLangGroup(nsAtom* aLanguage) {
  // map lang ==> langGroup
  nsAtom* langGroup = nullptr;
  if (aLanguage) {
    langGroup = mLangService->GetLanguageGroup(aLanguage);
  }
  if (!langGroup) {
    langGroup = nsGkAtoms::Unicode;
  }
  return langGroup;
}

/* static */ const char* gfxPlatformFontList::GetGenericName(
    StyleGenericFontFamily aGenericType) {
  // type should be standard generic type at this point
  // map generic type to string
  switch (aGenericType) {
    case StyleGenericFontFamily::Serif:
      return "serif";
    case StyleGenericFontFamily::SansSerif:
      return "sans-serif";
    case StyleGenericFontFamily::Monospace:
      return "monospace";
    case StyleGenericFontFamily::Cursive:
      return "cursive";
    case StyleGenericFontFamily::Fantasy:
      return "fantasy";
    case StyleGenericFontFamily::Math:
      return "math";
    case StyleGenericFontFamily::SystemUi:
      return "system-ui";
    case StyleGenericFontFamily::MozEmoji:
      return "-moz-emoji";
    case StyleGenericFontFamily::None:
      break;
  }
  MOZ_ASSERT_UNREACHABLE("Unknown generic");
  return nullptr;
}

void gfxPlatformFontList::InitLoader() {
  GetFontFamilyNames(mFontInfo->mFontFamiliesToLoad);
  mStartIndex = 0;
  mNumFamilies = mFontInfo->mFontFamiliesToLoad.Length();
  memset(&(mFontInfo->mLoadStats), 0, sizeof(mFontInfo->mLoadStats));
}

#define FONT_LOADER_MAX_TIMESLICE \
  20  // max time for one pass through RunLoader = 20ms

bool gfxPlatformFontList::LoadFontInfo() {
  AutoLock lock(mLock);
  TimeStamp start = TimeStamp::Now();
  uint32_t i, endIndex = mNumFamilies;
  fontlist::FontList* list = SharedFontList();
  bool loadCmaps =
      !list && (!UsesSystemFallback() ||
                gfxPlatform::GetPlatform()->UseCmapsDuringSystemFallback());

  // for each font family, load in various font info
  for (i = mStartIndex; i < endIndex; i++) {
    nsAutoCString key;
    GenerateFontListKey(mFontInfo->mFontFamiliesToLoad[i], key);

    if (list) {
      fontlist::Family* family = list->FindFamily(key);
      if (!family) {
        continue;
      }
      ReadFaceNamesForFamily(family, NeedFullnamePostscriptNames());
    } else {
      // lookup in canonical (i.e. English) family name list
      gfxFontFamily* familyEntry = mFontFamilies.GetWeak(key);
      if (!familyEntry) {
        continue;
      }

      // read in face names
      familyEntry->ReadFaceNames(this, NeedFullnamePostscriptNames(),
                                 mFontInfo);

      // load the cmaps if needed
      if (loadCmaps) {
        familyEntry->ReadAllCMAPs(mFontInfo);
      }
    }

    // Limit the time spent reading fonts in one pass, unless the font-loader
    // delay was set to zero, in which case we run to completion even if it
    // causes some jank.
    if (StaticPrefs::gfx_font_loader_delay() > 0) {
      TimeDuration elapsed = TimeStamp::Now() - start;
      if (elapsed.ToMilliseconds() > FONT_LOADER_MAX_TIMESLICE &&
          i + 1 != endIndex) {
        endIndex = i + 1;
        break;
      }
    }
  }

  mStartIndex = endIndex;
  bool done = mStartIndex >= mNumFamilies;

  if (LOG_FONTINIT_ENABLED()) {
    TimeDuration elapsed = TimeStamp::Now() - start;
    LOG_FONTINIT(("(fontinit) fontloader load pass %8.2f ms done %s\n",
                  elapsed.ToMilliseconds(), (done ? "true" : "false")));
  }

  if (done) {
    mOtherFamilyNamesInitialized = true;
    CancelInitOtherFamilyNamesTask();
    mFaceNameListsInitialized = true;
  }

  return done;
}

void gfxPlatformFontList::CleanupLoader() {
  AutoLock lock(mLock);

  mFontFamiliesToLoad.Clear();
  mNumFamilies = 0;
  bool rebuilt = false, forceReflow = false;

  // if had missed face names that are now available, force reflow all
  if (mFaceNamesMissed) {
    rebuilt = std::any_of(mFaceNamesMissed->cbegin(), mFaceNamesMissed->cend(),
                          [&](const auto& key) {
                            mLock.AssertCurrentThreadIn();
                            return FindFaceName(key);
                          });
    if (rebuilt) {
      RebuildLocalFonts();
    }

    mFaceNamesMissed = nullptr;
  }

  if (mOtherNamesMissed) {
    forceReflow = std::any_of(
        mOtherNamesMissed->cbegin(), mOtherNamesMissed->cend(),
        [&](const auto& key) {
          mLock.AssertCurrentThreadIn();
          return FindUnsharedFamily(
              nullptr, key,
              (FindFamiliesFlags::eForceOtherFamilyNamesLoading |
               FindFamiliesFlags::eNoAddToNamesMissedWhenSearching));
        });
    if (forceReflow) {
      gfxPlatform::GlobalReflowFlags flags =
          gfxPlatform::GlobalReflowFlags::FontsChanged;
      ForceGlobalReflowLocked(flags);
    }

    mOtherNamesMissed = nullptr;
  }

  if (LOG_FONTINIT_ENABLED() && mFontInfo) {
    LOG_FONTINIT(
        ("(fontinit) fontloader load thread took %8.2f ms "
         "%d families %d fonts %d cmaps "
         "%d facenames %d othernames %s %s",
         mLoadTime.ToMilliseconds(), mFontInfo->mLoadStats.families,
         mFontInfo->mLoadStats.fonts, mFontInfo->mLoadStats.cmaps,
         mFontInfo->mLoadStats.facenames, mFontInfo->mLoadStats.othernames,
         (rebuilt ? "(userfont sets rebuilt)" : ""),
         (forceReflow ? "(global reflow)" : "")));
  }

  gfxFontInfoLoader::CleanupLoader();
}

void gfxPlatformFontList::ForceGlobalReflow(
    gfxPlatform::GlobalReflowFlags aFlags) {
  if (!NS_IsMainThread()) {
    NS_DispatchToMainThread(NS_NewRunnableFunction(
        "gfxPlatformFontList::ForceGlobalReflow",
        [this, aFlags] { this->ForceGlobalReflow(aFlags); }));
    return;
  }

  if (aFlags & gfxPlatform::GlobalReflowFlags::FontsChanged) {
    AutoLock lock(mLock);
    InitializeCodepointsWithNoFonts();
    if (SharedFontList()) {
      // If we're using a shared local face-name list, this may have changed
      // such that existing font entries held by user font sets are no longer
      // safe to use: ensure they all get flushed.
      RebuildLocalFonts(/*aForgetLocalFaces*/ true);
    }
  }

  gfxPlatform::ForceGlobalReflow(aFlags);
}

void gfxPlatformFontList::ForceGlobalReflowLocked(
    gfxPlatform::GlobalReflowFlags aFlags) {
  if (!NS_IsMainThread()) {
    NS_DispatchToMainThread(NS_NewRunnableFunction(
        "gfxPlatformFontList::ForceGlobalReflow",
        [this, aFlags] { this->ForceGlobalReflow(aFlags); }));
    return;
  }

  if (aFlags & gfxPlatform::GlobalReflowFlags::FontsChanged) {
    InitializeCodepointsWithNoFonts();
    if (SharedFontList()) {
      RebuildLocalFonts(/*aForgetLocalFaces*/ true);
    }
  }

  AutoUnlock unlock(mLock);
  gfxPlatform::ForceGlobalReflow(aFlags);
}

void gfxPlatformFontList::GetPrefsAndStartLoader() {
  // If we're already in shutdown, there's no point in starting this, and it
  // could trigger an assertion if we try to use the Thread Manager too late.
  if (AppShutdown::IsInOrBeyond(ShutdownPhase::AppShutdownConfirmed)) {
    return;
  }
  uint32_t delay = std::max(1u, StaticPrefs::gfx_font_loader_delay());
  if (NS_IsMainThread()) {
    StartLoader(delay);
  } else {
    NS_DispatchToMainThread(NS_NewRunnableFunction(
        "StartLoader callback", [delay, fontList = this] {
          fontList->Lock();
          fontList->StartLoader(delay);
          fontList->Unlock();
        }));
  }
}

void gfxPlatformFontList::RebuildLocalFonts(bool aForgetLocalFaces) {
  // Make a local copy of the list of font sets we need to process.
  AutoTArray<RefPtr<gfxUserFontSet>, 16> fontSets;
  fontSets.SetCapacity(mUserFontSetList.Count());
  for (auto* fontset : mUserFontSetList) {
    fontSets.AppendElement(fontset);
  }
  // Drop our lock before calling ForgetLocalFaces and RebuildLocalRules
  // for each set, to avoid possible deadlocks.
  AutoUnlock unlock(mLock);
  for (auto fontset : fontSets) {
    if (aForgetLocalFaces) {
      fontset->ForgetLocalFaces();
    }
    fontset->RebuildLocalRules();
  }
}

void gfxPlatformFontList::ClearLangGroupPrefFontsLocked() {
  for (uint32_t i = eFontPrefLang_First;
       i < eFontPrefLang_First + eFontPrefLang_Count; i++) {
    auto& prefFontsLangGroup = mLangGroupPrefFonts[i];
    for (auto& pref : prefFontsLangGroup) {
      pref = nullptr;
    }
  }
  mEmojiPrefFont = nullptr;

  // Create a new FontPrefs and replace the existing one.
  mFontPrefs = MakeUnique<FontPrefs>();
}

// Support for memory reporting

// this is also used by subclasses that hold additional font tables
/*static*/
size_t gfxPlatformFontList::SizeOfFontFamilyTableExcludingThis(
    const FontFamilyTable& aTable, MallocSizeOf aMallocSizeOf) {
  return std::accumulate(
      aTable.Keys().cbegin(), aTable.Keys().cend(),
      aTable.ShallowSizeOfExcludingThis(aMallocSizeOf),
      [&](size_t oldValue, const nsACString& key) {
        // We don't count the size of the family here, because this is an
        // *extra* reference to a family that will have already been counted in
        // the main list.
        return oldValue + key.SizeOfExcludingThisIfUnshared(aMallocSizeOf);
      });
}

/*static*/
size_t gfxPlatformFontList::SizeOfFontEntryTableExcludingThis(
    const FontEntryTable& aTable, MallocSizeOf aMallocSizeOf) {
  return std::accumulate(
      aTable.Keys().cbegin(), aTable.Keys().cend(),
      aTable.ShallowSizeOfExcludingThis(aMallocSizeOf),
      [&](size_t oldValue, const nsACString& key) {
        // The font itself is counted by its owning family; here we only care
        // about the names stored in the hashtable keys.

        return oldValue + key.SizeOfExcludingThisIfUnshared(aMallocSizeOf);
      });
}

void gfxPlatformFontList::AddSizeOfExcludingThis(MallocSizeOf aMallocSizeOf,
                                                 FontListSizes* aSizes) const {
  AutoLock lock(mLock);

  aSizes->mFontListSize +=
      mFontFamilies.ShallowSizeOfExcludingThis(aMallocSizeOf);
  for (const auto& entry : mFontFamilies) {
    aSizes->mFontListSize +=
        entry.GetKey().SizeOfExcludingThisIfUnshared(aMallocSizeOf);
    entry.GetData()->AddSizeOfIncludingThis(aMallocSizeOf, aSizes);
  }

  aSizes->mFontListSize +=
      SizeOfFontFamilyTableExcludingThis(mOtherFamilyNames, aMallocSizeOf);

  if (mExtraNames) {
    aSizes->mFontListSize += SizeOfFontEntryTableExcludingThis(
        mExtraNames->mFullnames, aMallocSizeOf);
    aSizes->mFontListSize += SizeOfFontEntryTableExcludingThis(
        mExtraNames->mPostscriptNames, aMallocSizeOf);
  }

  for (uint32_t i = eFontPrefLang_First;
       i < eFontPrefLang_First + eFontPrefLang_Count; i++) {
    auto& prefFontsLangGroup = mLangGroupPrefFonts[i];
    for (const UniquePtr<PrefFontList>& pf : prefFontsLangGroup) {
      if (pf) {
        aSizes->mFontListSize += pf->ShallowSizeOfExcludingThis(aMallocSizeOf);
      }
    }
  }

  for (const auto& bitset : mCodepointsWithNoFonts) {
    aSizes->mFontListSize += bitset.SizeOfExcludingThis(aMallocSizeOf);
  }
  aSizes->mFontListSize +=
      mFontFamiliesToLoad.ShallowSizeOfExcludingThis(aMallocSizeOf);

  aSizes->mFontListSize +=
      mBadUnderlineFamilyNames.ShallowSizeOfExcludingThis(aMallocSizeOf);
  for (const auto& i : mBadUnderlineFamilyNames) {
    aSizes->mFontListSize += i.SizeOfExcludingThisIfUnshared(aMallocSizeOf);
  }

  aSizes->mFontListSize +=
      mSharedCmaps.ShallowSizeOfExcludingThis(aMallocSizeOf);
  for (const auto& entry : mSharedCmaps) {
    aSizes->mCharMapsSize +=
        entry.GetCharMap()->SizeOfIncludingThis(aMallocSizeOf);
  }

  aSizes->mFontListSize +=
      mFontEntries.ShallowSizeOfExcludingThis(aMallocSizeOf);
  for (const auto& entry : mFontEntries.Values()) {
    if (entry) {
      entry->AddSizeOfIncludingThis(aMallocSizeOf, aSizes);
    }
  }

  if (SharedFontList()) {
    aSizes->mFontListSize +=
        SharedFontList()->SizeOfIncludingThis(aMallocSizeOf);
    if (XRE_IsParentProcess()) {
      aSizes->mSharedSize += SharedFontList()->AllocatedShmemSize();
    }
  }
}

void gfxPlatformFontList::AddSizeOfIncludingThis(MallocSizeOf aMallocSizeOf,
                                                 FontListSizes* aSizes) const {
  aSizes->mFontListSize += aMallocSizeOf(this);
  AddSizeOfExcludingThis(aMallocSizeOf, aSizes);
}

void gfxPlatformFontList::InitOtherFamilyNamesInternal(
    bool aDeferOtherFamilyNamesLoading) {
  if (mOtherFamilyNamesInitialized) {
    return;
  }

  AutoLock lock(mLock);

  if (aDeferOtherFamilyNamesLoading) {
    TimeStamp start = TimeStamp::Now();
    bool timedOut = false;

    auto list = SharedFontList();
    if (list) {
      // If the gfxFontInfoLoader task is not yet running, kick it off now so
      // that it will load remaining names etc as soon as idle time permits.
      if (mState == stateInitial || mState == stateTimerOnDelay) {
        StartLoader(0);
        timedOut = true;
      }
    } else {
      for (const RefPtr<gfxFontFamily>& family : mFontFamilies.Values()) {
        family->ReadOtherFamilyNames(this);
        TimeDuration elapsed = TimeStamp::Now() - start;
        if (elapsed.ToMilliseconds() > OTHERNAMES_TIMEOUT) {
          timedOut = true;
          break;
        }
      }
    }

    if (!timedOut) {
      mOtherFamilyNamesInitialized = true;
      CancelInitOtherFamilyNamesTask();
    }
    TimeDuration elapsed = TimeStamp::Now() - start;
    glean::fontlist::initotherfamilynames.AccumulateRawDuration(elapsed);

    if (LOG_FONTINIT_ENABLED()) {
      LOG_FONTINIT(("(fontinit) InitOtherFamilyNames took %8.2f ms %s",
                    elapsed.ToMilliseconds(), (timedOut ? "timeout" : "")));
    }
  } else {
    TimeStamp start = TimeStamp::Now();

    auto list = SharedFontList();
    if (list) {
      for (auto& f : mozilla::Range<fontlist::Family>(list->Families(),
                                                      list->NumFamilies())) {
        ReadFaceNamesForFamily(&f, false);
      }
    } else {
      for (const RefPtr<gfxFontFamily>& family : mFontFamilies.Values()) {
        family->ReadOtherFamilyNames(this);
      }
    }

    mOtherFamilyNamesInitialized = true;
    CancelInitOtherFamilyNamesTask();

    TimeDuration elapsed = TimeStamp::Now() - start;
    glean::fontlist::initotherfamilynames_no_deferring.AccumulateRawDuration(
        elapsed);

    if (LOG_FONTINIT_ENABLED()) {
      LOG_FONTINIT(
          ("(fontinit) InitOtherFamilyNames without deferring took %8.2f ms",
           elapsed.ToMilliseconds()));
    }
  }
}

void gfxPlatformFontList::CancelInitOtherFamilyNamesTask() {
  if (mPendingOtherFamilyNameTask) {
    mPendingOtherFamilyNameTask->Cancel();
    mPendingOtherFamilyNameTask = nullptr;
  }
  auto list = SharedFontList();
  if (list && XRE_IsParentProcess()) {
    bool forceReflow = false;
    if (!mAliasTable.IsEmpty()) {
      list->SetAliases(mAliasTable);
      mAliasTable.Clear();
      forceReflow = true;
    }
    if (mLocalNameTable.Count()) {
      list->SetLocalNames(mLocalNameTable);
      mLocalNameTable.Clear();
      forceReflow = true;
    }
    // If there's a LoadCmapsRunnable alive, we ignore forceReflow because the
    // runnable will trigger a reflow when it completes, and we don't want to
    // reflow more times than necessary.
    if (forceReflow && !mLoadCmapsRunnable) {
      gfxPlatform::GlobalReflowFlags flags =
          gfxPlatform::GlobalReflowFlags::BroadcastToChildren |
          gfxPlatform::GlobalReflowFlags::FontsChanged;
      gfxPlatform::ForceGlobalReflow(flags);
    }
  }
}

void gfxPlatformFontList::ShareFontListShmBlockToProcess(
    uint32_t aGeneration, uint32_t aIndex, base::ProcessId aPid,
    mozilla::ipc::ReadOnlySharedMemoryHandle* aOut) {
  auto list = SharedFontList();
  if (!list) {
    return;
  }
  if (!aGeneration || GetGeneration() == aGeneration) {
    list->ShareShmBlockToProcess(aIndex, aPid, aOut);
  } else {
    *aOut = nullptr;
  }
}

void gfxPlatformFontList::ShareFontListToProcess(
    nsTArray<mozilla::ipc::ReadOnlySharedMemoryHandle>* aBlocks,
    base::ProcessId aPid) {
  auto list = SharedFontList();
  if (list) {
    list->ShareBlocksToProcess(aBlocks, aPid);
  }
}

mozilla::ipc::ReadOnlySharedMemoryHandle
gfxPlatformFontList::ShareShmBlockToProcess(uint32_t aIndex,
                                            base::ProcessId aPid) {
  MOZ_RELEASE_ASSERT(SharedFontList());
  return SharedFontList()->ShareBlockToProcess(aIndex, aPid);
}

void gfxPlatformFontList::ShmBlockAdded(
    uint32_t aGeneration, uint32_t aIndex,
    mozilla::ipc::ReadOnlySharedMemoryHandle aHandle) {
  if (SharedFontList()) {
    AutoLock lock(mLock);
    SharedFontList()->ShmBlockAdded(aGeneration, aIndex, std::move(aHandle));
  }
}

void gfxPlatformFontList::InitializeFamily(uint32_t aGeneration,
                                           uint32_t aFamilyIndex,
                                           bool aLoadCmaps) {
  auto list = SharedFontList();
  MOZ_ASSERT(list);
  if (!list) {
    return;
  }
  if (GetGeneration() != aGeneration) {
    return;
  }
  if (AppShutdown::IsInOrBeyond(ShutdownPhase::AppShutdownConfirmed)) {
    return;
  }
  if (aFamilyIndex >= list->NumFamilies()) {
    return;
  }
  fontlist::Family* family = list->Families() + aFamilyIndex;
  if (!family->IsInitialized() || aLoadCmaps) {
    (void)InitializeFamily(family, aLoadCmaps);
  }
}

void gfxPlatformFontList::SetCharacterMap(uint32_t aGeneration,
                                          uint32_t aFamilyIndex, bool aAlias,
                                          uint32_t aFaceIndex,
                                          const gfxSparseBitSet& aMap) {
  MOZ_ASSERT(XRE_IsParentProcess());
  auto list = SharedFontList();
  MOZ_ASSERT(list);
  if (!list) {
    return;
  }
  if (GetGeneration() != aGeneration) {
    return;
  }
  if (AppShutdown::IsInOrBeyond(ShutdownPhase::AppShutdownConfirmed)) {
    return;
  }

  const fontlist::Family* family;
  if (aAlias) {
    if (aFamilyIndex >= list->NumAliases()) {
      MOZ_ASSERT(false, "AliasFamily index out of range");
      return;
    }
    family = list->AliasFamilies() + aFamilyIndex;
  } else {
    if (aFamilyIndex >= list->NumFamilies()) {
      MOZ_ASSERT(false, "Family index out of range");
      return;
    }
    family = list->Families() + aFamilyIndex;
  }

  if (aFaceIndex >= family->NumFaces()) {
    MOZ_ASSERT(false, "Face index out of range");
    return;
  }

  if (auto* face =
          family->Faces(list)[aFaceIndex].ToPtr<fontlist::Face>(list)) {
    face->mCharacterMap = GetShmemCharMap(&aMap);
  }
}

void gfxPlatformFontList::SetupFamilyCharMap(uint32_t aGeneration,
                                             uint32_t aIndex, bool aAlias) {
  MOZ_ASSERT(XRE_IsParentProcess());
  auto list = SharedFontList();
  MOZ_ASSERT(list);
  if (!list) {
    return;
  }
  if (GetGeneration() != aGeneration) {
    return;
  }
  if (AppShutdown::IsInOrBeyond(ShutdownPhase::AppShutdownConfirmed)) {
    return;
  }

  if (aAlias) {
    if (aIndex >= list->NumAliases()) {
      MOZ_ASSERT(false, "AliasFamily index out of range");
      return;
    }
    list->AliasFamilies()[aIndex].SetupFamilyCharMap(list);
    return;
  }

  if (aIndex >= list->NumFamilies()) {
    MOZ_ASSERT(false, "Family index out of range");
    return;
  }
  list->Families()[aIndex].SetupFamilyCharMap(list);
}

bool gfxPlatformFontList::InitOtherFamilyNames(uint32_t aGeneration,
                                               bool aDefer) {
  auto list = SharedFontList();
  MOZ_ASSERT(list);
  if (!list) {
    return false;
  }
  if (GetGeneration() != aGeneration) {
    return false;
  }
  if (AppShutdown::IsInOrBeyond(ShutdownPhase::AppShutdownConfirmed)) {
    return false;
  }
  return InitOtherFamilyNames(aDefer);
}

gfxPlatformFontList::FontPrefs::FontPrefs() {
  // This must be created on the main thread, so that we can safely use the
  // Preferences service. Once created, it can be read from any thread.
  MOZ_ASSERT(NS_IsMainThread());
  Init();
}

void gfxPlatformFontList::FontPrefs::Init() {
  if (AppShutdown::IsInOrBeyond(ShutdownPhase::XPCOMShutdownFinal)) {
    return;
  }
  nsIPrefBranch* prefRootBranch = Preferences::GetRootBranch();
  if (!prefRootBranch) {
    return;
  }
  nsTArray<nsCString> prefNames;
  if (NS_SUCCEEDED(prefRootBranch->GetChildList(kNamePrefix, prefNames))) {
    for (auto& prefName : prefNames) {
      nsAutoCString value;
      if (NS_SUCCEEDED(Preferences::GetCString(prefName.get(), value))) {
        nsAutoCString pref(Substring(prefName, sizeof(kNamePrefix) - 1));
        mFontName.InsertOrUpdate(pref, value);
      }
    }
  }
  if (NS_SUCCEEDED(prefRootBranch->GetChildList(kNameListPrefix, prefNames))) {
    for (auto& prefName : prefNames) {
      nsAutoCString value;
      if (NS_SUCCEEDED(Preferences::GetCString(prefName.get(), value))) {
        nsAutoCString pref(Substring(prefName, sizeof(kNameListPrefix) - 1));
        mFontNameList.InsertOrUpdate(pref, value);
      }
    }
  }
  mEmojiHasUserValue = Preferences::HasUserValue("font.name-list.emoji");

  // Record the default CJK order, based on accept-lang settings and locale.
  eFontPrefLang tempPrefLangs[kMaxLenPrefLangList];
  uint32_t tempLen = 0;

  // Add the CJK pref fonts from accept languages, the order should be same
  // order.
  nsAutoCString acceptLang;
  nsresult rv = LocaleService::GetInstance()->GetAcceptLanguages(acceptLang);

  // We use gfxFontUtils::ParseFontList to read the list even
  // though it's not actually a list of fonts but of locale codes;
  // the format is the same.
  AutoTArray<nsCString, 5> list;
  if (NS_SUCCEEDED(rv)) {
    gfxFontUtils::ParseFontList(acceptLang, list);
  }

  for (const auto& lang : list) {
    eFontPrefLang fpl = GetFontPrefLangFor(lang.get());
    switch (fpl) {
      case eFontPrefLang_Japanese:
      case eFontPrefLang_Korean:
      case eFontPrefLang_ChineseCN:
      case eFontPrefLang_ChineseHK:
      case eFontPrefLang_ChineseTW:
        AppendPrefLang(tempPrefLangs, tempLen, fpl);
        break;
      default:
        break;
    }
  }

  // Try using app's locale
  nsAutoCString localeStr;
  LocaleService::GetInstance()->GetAppLocaleAsBCP47(localeStr);

  {
    Locale locale;
    if (LocaleParser::TryParse(localeStr, locale).isOk() &&
        locale.Canonicalize().isOk()) {
      if (locale.Language().EqualTo("ja")) {
        AppendPrefLang(tempPrefLangs, tempLen, eFontPrefLang_Japanese);
      } else if (locale.Language().EqualTo("zh")) {
        if (locale.Region().EqualTo("CN")) {
          AppendPrefLang(tempPrefLangs, tempLen, eFontPrefLang_ChineseCN);
        } else if (locale.Region().EqualTo("TW")) {
          AppendPrefLang(tempPrefLangs, tempLen, eFontPrefLang_ChineseTW);
        } else if (locale.Region().EqualTo("HK")) {
          AppendPrefLang(tempPrefLangs, tempLen, eFontPrefLang_ChineseHK);
        }
      } else if (locale.Language().EqualTo("ko")) {
        AppendPrefLang(tempPrefLangs, tempLen, eFontPrefLang_Korean);
      }
    }
  }

  // Then add the known CJK prefs in order of system preferred locales
  AutoTArray<nsCString, 5> prefLocales;
  prefLocales.AppendElement("ja"_ns);
  prefLocales.AppendElement("zh-CN"_ns);
  prefLocales.AppendElement("zh-TW"_ns);
  prefLocales.AppendElement("zh-HK"_ns);
  prefLocales.AppendElement("ko"_ns);

  AutoTArray<nsCString, 16> sysLocales;
  AutoTArray<nsCString, 16> negLocales;
  if (NS_SUCCEEDED(
          OSPreferences::GetInstance()->GetSystemLocales(sysLocales))) {
    LocaleService::GetInstance()->NegotiateLanguages(
        sysLocales, prefLocales, ""_ns,
        LocaleService::kLangNegStrategyFiltering, negLocales);
    for (const auto& localeStr : negLocales) {
      Locale locale;
      if (LocaleParser::TryParse(localeStr, locale).isOk() &&
          locale.Canonicalize().isOk()) {
        if (locale.Language().EqualTo("ja")) {
          AppendPrefLang(tempPrefLangs, tempLen, eFontPrefLang_Japanese);
        } else if (locale.Language().EqualTo("zh")) {
          if (locale.Region().EqualTo("CN")) {
            AppendPrefLang(tempPrefLangs, tempLen, eFontPrefLang_ChineseCN);
          } else if (locale.Region().EqualTo("TW")) {
            AppendPrefLang(tempPrefLangs, tempLen, eFontPrefLang_ChineseTW);
          } else if (locale.Region().EqualTo("HK")) {
            AppendPrefLang(tempPrefLangs, tempLen, eFontPrefLang_ChineseHK);
          }
        } else if (locale.Language().EqualTo("ko")) {
          AppendPrefLang(tempPrefLangs, tempLen, eFontPrefLang_Korean);
        }
      }
    }
  }

  // Last resort... set up CJK font prefs in the order listed by the user-
  // configurable ordering pref.
  gfxFontUtils::GetPrefsFontList(kCJKFallbackOrderPref, list);
  for (const auto& item : list) {
    eFontPrefLang fpl = GetFontPrefLangFor(item.get());
    switch (fpl) {
      case eFontPrefLang_Japanese:
      case eFontPrefLang_Korean:
      case eFontPrefLang_ChineseCN:
      case eFontPrefLang_ChineseHK:
      case eFontPrefLang_ChineseTW:
        AppendPrefLang(tempPrefLangs, tempLen, fpl);
        break;
      default:
        break;
    }
  }

  // Truly-last resort... try Chinese font prefs before Japanese because
  // they tend to have more complete character coverage, and therefore less
  // risk of "ransom-note" effects.
  // (If the kCJKFallbackOrderPref was fully populated, as it is by default,
  // this will do nothing as all these values are already present.)
  AppendPrefLang(tempPrefLangs, tempLen, eFontPrefLang_ChineseCN);
  AppendPrefLang(tempPrefLangs, tempLen, eFontPrefLang_ChineseHK);
  AppendPrefLang(tempPrefLangs, tempLen, eFontPrefLang_ChineseTW);
  AppendPrefLang(tempPrefLangs, tempLen, eFontPrefLang_Japanese);
  AppendPrefLang(tempPrefLangs, tempLen, eFontPrefLang_Korean);

  // copy into the cached array
  for (const auto lang : Span<eFontPrefLang>(tempPrefLangs, tempLen)) {
    mCJKPrefLangs.AppendElement(lang);
  }
}

bool gfxPlatformFontList::FontPrefs::LookupName(const nsACString& aPref,
                                                nsACString& aValue) const {
  if (const auto& value = mFontName.Lookup(aPref)) {
    aValue = *value;
    return true;
  }
  return false;
}

bool gfxPlatformFontList::FontPrefs::LookupNameList(const nsACString& aPref,
                                                    nsACString& aValue) const {
  if (const auto& value = mFontNameList.Lookup(aPref)) {
    aValue = *value;
    return true;
  }
  return false;
}

bool gfxPlatformFontList::IsKnownIconFontFamily(
    const nsAtom* aFamilyName) const {
  nsAtomCString fam(aFamilyName);
  ToLowerCase(fam);
  return mIconFontsSet.Contains(fam);
}

#undef LOG
#undef LOG_ENABLED
