/* -*- Mode: C++; tab-width: 20; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "gfxFont.h"

#include "mozilla/BinarySearch.h"
#include "mozilla/DebugOnly.h"
#include "mozilla/FontPropertyTypes.h"
#include "mozilla/gfx/2D.h"
#include "mozilla/IntegerRange.h"
#include "mozilla/intl/Segmenter.h"
#include "mozilla/MathAlgorithms.h"
#include "mozilla/StaticPrefs_gfx.h"
#include "mozilla/ScopeExit.h"
#include "mozilla/SVGContextPaint.h"

#include "mozilla/Logging.h"

#include "nsITimer.h"

#include "gfxGlyphExtents.h"
#include "gfxPlatform.h"
#include "gfxTextRun.h"
#include "nsGkAtoms.h"

#include "gfxTypes.h"
#include "gfxContext.h"
#include "gfxFontMissingGlyphs.h"
#include "gfxGraphiteShaper.h"
#include "gfxHarfBuzzShaper.h"
#include "gfxUserFontSet.h"
#include "nsCRT.h"
#include "nsContentUtils.h"
#include "nsSpecialCasingData.h"
#include "nsTextRunTransformations.h"
#include "nsUGenCategory.h"
#include "nsUnicodeProperties.h"
#include "nsStyleConsts.h"
#include "mozilla/AppUnits.h"
#include "mozilla/HashTable.h"
#include "mozilla/Likely.h"
#include "mozilla/MemoryReporting.h"
#include "mozilla/Preferences.h"
#include "mozilla/Services.h"
#include "mozilla/glean/GfxMetrics.h"
#include "gfxMathTable.h"
#include "gfxSVGGlyphs.h"
#include "gfx2DGlue.h"
#include "TextDrawTarget.h"

#include "ThebesRLBox.h"

#include "GreekCasing.h"

#include "cairo.h"
#ifdef XP_WIN
#  include "cairo-win32.h"
#  include "gfxWindowsPlatform.h"
#endif

#include "harfbuzz/hb.h"
#include "harfbuzz/hb-ot.h"

#include <algorithm>
#include <limits>
#include <cmath>

using namespace mozilla;
using namespace mozilla::gfx;
using namespace mozilla::unicode;
using mozilla::services::GetObserverService;

gfxFontCache* gfxFontCache::gGlobalCache = nullptr;

#ifdef DEBUG_roc
#  define DEBUG_TEXT_RUN_STORAGE_METRICS
#endif

#ifdef DEBUG_TEXT_RUN_STORAGE_METRICS
uint32_t gTextRunStorageHighWaterMark = 0;
uint32_t gTextRunStorage = 0;
uint32_t gFontCount = 0;
uint32_t gGlyphExtentsCount = 0;
uint32_t gGlyphExtentsWidthsTotalSize = 0;
uint32_t gGlyphExtentsSetupEagerSimple = 0;
uint32_t gGlyphExtentsSetupEagerTight = 0;
uint32_t gGlyphExtentsSetupLazyTight = 0;
uint32_t gGlyphExtentsSetupFallBackToTight = 0;
#endif

#define LOG_FONTINIT(args) \
  MOZ_LOG(gfxPlatform::GetLog(eGfxLog_fontinit), LogLevel::Debug, args)
#define LOG_FONTINIT_ENABLED() \
  MOZ_LOG_TEST(gfxPlatform::GetLog(eGfxLog_fontinit), LogLevel::Debug)

/*
 * gfxFontCache - global cache of gfxFont instances.
 * Expires unused fonts after a short interval;
 * notifies fonts to age their cached shaped-word records;
 * observes memory-pressure notification and tells fonts to clear their
 * shaped-word caches to free up memory.
 */

MOZ_DEFINE_MALLOC_SIZE_OF(FontCacheMallocSizeOf)

NS_IMPL_ISUPPORTS(gfxFontCache::MemoryReporter, nsIMemoryReporter)

/*virtual*/
gfxTextRunFactory::~gfxTextRunFactory() {
  // Should not be dropped by stylo
  MOZ_ASSERT(!Servo_IsWorkerThread());
}

NS_IMETHODIMP
gfxFontCache::MemoryReporter::CollectReports(
    nsIHandleReportCallback* aHandleReport, nsISupports* aData,
    bool aAnonymize) {
  FontCacheSizes sizes;

  gfxFontCache::GetCache()->AddSizeOfIncludingThis(&FontCacheMallocSizeOf,
                                                   &sizes);

  MOZ_COLLECT_REPORT("explicit/gfx/font-cache", KIND_HEAP, UNITS_BYTES,
                     sizes.mFontInstances,
                     "Memory used for active font instances.");

  MOZ_COLLECT_REPORT("explicit/gfx/font-shaped-words", KIND_HEAP, UNITS_BYTES,
                     sizes.mShapedWords,
                     "Memory used to cache shaped glyph data.");

  return NS_OK;
}

NS_IMPL_ISUPPORTS(gfxFontCache::Observer, nsIObserver)

NS_IMETHODIMP
gfxFontCache::Observer::Observe(nsISupports* aSubject, const char* aTopic,
                                const char16_t* someData) {
  if (!nsCRT::strcmp(aTopic, "memory-pressure")) {
    gfxFontCache* fontCache = gfxFontCache::GetCache();
    if (fontCache) {
      fontCache->FlushShapedWordCaches();
    }
  } else {
    MOZ_ASSERT_UNREACHABLE("unexpected notification topic");
  }
  return NS_OK;
}

nsresult gfxFontCache::Init() {
  NS_ASSERTION(!gGlobalCache, "Where did this come from?");
  gGlobalCache = new gfxFontCache(GetMainThreadSerialEventTarget());
  if (!gGlobalCache) {
    return NS_ERROR_OUT_OF_MEMORY;
  }
  RegisterStrongMemoryReporter(new MemoryReporter());
  return NS_OK;
}

void gfxFontCache::Shutdown() {
  delete gGlobalCache;
  gGlobalCache = nullptr;

#ifdef DEBUG_TEXT_RUN_STORAGE_METRICS
  printf("Textrun storage high water mark=%d\n", gTextRunStorageHighWaterMark);
  printf("Total number of fonts=%d\n", gFontCount);
  printf("Total glyph extents allocated=%d (size %d)\n", gGlyphExtentsCount,
         int(gGlyphExtentsCount * sizeof(gfxGlyphExtents)));
  printf("Total glyph extents width-storage size allocated=%d\n",
         gGlyphExtentsWidthsTotalSize);
  printf("Number of simple glyph extents eagerly requested=%d\n",
         gGlyphExtentsSetupEagerSimple);
  printf("Number of tight glyph extents eagerly requested=%d\n",
         gGlyphExtentsSetupEagerTight);
  printf("Number of tight glyph extents lazily requested=%d\n",
         gGlyphExtentsSetupLazyTight);
  printf("Number of simple glyph extent setups that fell back to tight=%d\n",
         gGlyphExtentsSetupFallBackToTight);
#endif
}

gfxFontCache::gfxFontCache(nsIEventTarget* aEventTarget)
    : ExpirationTrackerImpl<gfxFont, 3, Lock, AutoLock>(
          FONT_TIMEOUT_SECONDS * 1000, "gfxFontCache", aEventTarget) {
  nsCOMPtr<nsIObserverService> obs = GetObserverService();
  if (obs) {
    obs->AddObserver(new Observer, "memory-pressure", false);
  }

  nsIEventTarget* target = nullptr;
  if (XRE_IsContentProcess() && NS_IsMainThread()) {
    target = aEventTarget;
  }

  // Create the timer used to expire shaped-word records from each font's
  // cache after a short period of non-use. We have a single timer in
  // gfxFontCache that loops over all fonts known to the cache, to avoid
  // the overhead of individual timers in each font instance.
  // The timer will be started any time shaped word records are cached
  // (and pauses itself when all caches become empty).
  mWordCacheExpirationTimer = NS_NewTimer(target);
}

gfxFontCache::~gfxFontCache() {
  // Ensure the user font cache releases its references to font entries,
  // so they aren't kept alive after the font instances and font-list
  // have been shut down.
  gfxUserFontSet::UserFontCache::Shutdown();

  if (mWordCacheExpirationTimer) {
    mWordCacheExpirationTimer->Cancel();
    mWordCacheExpirationTimer = nullptr;
  }

  // Expire everything manually so we don't leak them.
  Flush();
}

bool gfxFontCache::HashEntry::KeyEquals(const KeyTypePointer aKey) const {
  const gfxCharacterMap* fontUnicodeRangeMap = mFont->GetUnicodeRangeMap();
  return aKey->mFontEntry == mFont->GetFontEntry() &&
         aKey->mStyle->Equals(*mFont->GetStyle()) &&
         ((!aKey->mUnicodeRangeMap && !fontUnicodeRangeMap) ||
          (aKey->mUnicodeRangeMap && fontUnicodeRangeMap &&
           aKey->mUnicodeRangeMap->Equals(fontUnicodeRangeMap)));
}

already_AddRefed<gfxFont> gfxFontCache::Lookup(
    const gfxFontEntry* aFontEntry, const gfxFontStyle* aStyle,
    const gfxCharacterMap* aUnicodeRangeMap) {
  MutexAutoLock lock(mMutex);

  Key key(aFontEntry, aStyle, aUnicodeRangeMap);
  HashEntry* entry = mFonts.GetEntry(key);

  glean::fontlist::font_cache_hit
      .EnumGet(
          static_cast<glean::fontlist::FontCacheHitLabel>(entry != nullptr))
      .Add();

  if (!entry) {
    return nullptr;
  }

  RefPtr<gfxFont> font = entry->mFont;
  if (font->GetExpirationState()->IsTracked()) {
    RemoveObjectLocked(font, lock);
  }
  return font.forget();
}

already_AddRefed<gfxFont> gfxFontCache::MaybeInsert(gfxFont* aFont) {
  MOZ_ASSERT(aFont);
  MutexAutoLock lock(mMutex);

  Key key(aFont->GetFontEntry(), aFont->GetStyle(),
          aFont->GetUnicodeRangeMap());
  HashEntry* entry = mFonts.PutEntry(key);
  if (!entry) {
    return do_AddRef(aFont);
  }

  // If it is null, then we are inserting a new entry. Otherwise we are
  // attempting to replace an existing font, probably due to a thread race, in
  // which case stick with the original font.
  if (!entry->mFont) {
    entry->mFont = aFont;
    // Assert that we can find the entry we just put in (this fails if the key
    // has a NaN float value in it, e.g. 'sizeAdjust').
    MOZ_ASSERT(entry == mFonts.GetEntry(key));
  } else {
    MOZ_ASSERT(entry->mFont != aFont);
    aFont->Destroy();
    if (entry->mFont->GetExpirationState()->IsTracked()) {
      RemoveObjectLocked(entry->mFont, lock);
    }
  }

  return do_AddRef(entry->mFont);
}

bool gfxFontCache::MaybeDestroy(gfxFont* aFont) {
  MOZ_ASSERT(aFont);
  MutexAutoLock lock(mMutex);

  // If the font has a non-zero refcount, then we must have lost the race with
  // gfxFontCache::Lookup and the same font was reacquired.
  if (aFont->GetRefCount() > 0) {
    return false;
  }

  Key key(aFont->GetFontEntry(), aFont->GetStyle(),
          aFont->GetUnicodeRangeMap());
  HashEntry* entry = mFonts.GetEntry(key);
  if (!entry || entry->mFont != aFont) {
    MOZ_ASSERT(!aFont->GetExpirationState()->IsTracked());
    return true;
  }

  // If the font is being tracked, we must have then also lost another race with
  // gfxFontCache::MaybeDestroy which re-added it to the tracker.
  if (aFont->GetExpirationState()->IsTracked()) {
    return false;
  }

  // Typically this won't fail, but it may during startup/shutdown if the timer
  // service is not available.
  nsresult rv = AddObjectLocked(aFont, lock);
  if (NS_SUCCEEDED(rv)) {
    return false;
  }

  mFonts.RemoveEntry(entry);
  return true;
}

void gfxFontCache::NotifyExpiredLocked(gfxFont* aFont, const AutoLock& aLock) {
  MOZ_ASSERT(aFont->GetRefCount() == 0);

  RemoveObjectLocked(aFont, aLock);
  mTrackerDiscard.AppendElement(aFont);

  Key key(aFont->GetFontEntry(), aFont->GetStyle(),
          aFont->GetUnicodeRangeMap());
  HashEntry* entry = mFonts.GetEntry(key);
  if (!entry || entry->mFont != aFont) {
    MOZ_ASSERT_UNREACHABLE("Invalid font?");
    return;
  }

  mFonts.RemoveEntry(entry);
}

void gfxFontCache::NotifyHandlerEnd() {
  nsTArray<gfxFont*> discard;
  {
    MutexAutoLock lock(mMutex);
    discard = std::move(mTrackerDiscard);
  }
  DestroyDiscard(discard);
}

void gfxFontCache::DestroyDiscard(nsTArray<gfxFont*>& aDiscard) {
  for (auto& font : aDiscard) {
    NS_ASSERTION(font->GetRefCount() == 0,
                 "Destroying with refs outside cache!");
    font->ClearCachedWords();
    font->Destroy();
  }
  aDiscard.Clear();
}

void gfxFontCache::Flush() {
  nsTArray<gfxFont*> discard;
  {
    MutexAutoLock lock(mMutex);
    discard.SetCapacity(mFonts.Count());
    for (auto iter = mFonts.Iter(); !iter.Done(); iter.Next()) {
      HashEntry* entry = static_cast<HashEntry*>(iter.Get());
      if (!entry || !entry->mFont) {
        MOZ_ASSERT_UNREACHABLE("Invalid font?");
        continue;
      }

      if (entry->mFont->GetRefCount() == 0) {
        // If we are not tracked, then we must have won the race with
        // gfxFont::MaybeDestroy and it is waiting on the mutex. To avoid a
        // double free, we let gfxFont::MaybeDestroy handle the freeing when it
        // acquires the mutex and discovers there is no matching entry in the
        // hashtable.
        if (entry->mFont->GetExpirationState()->IsTracked()) {
          RemoveObjectLocked(entry->mFont, lock);
          discard.AppendElement(entry->mFont);
        }
      } else {
        MOZ_ASSERT(!entry->mFont->GetExpirationState()->IsTracked());
      }
    }
    MOZ_ASSERT(IsEmptyLocked(lock),
               "Cache tracker still has fonts after flush!");
    mFonts.Clear();
  }
  DestroyDiscard(discard);
}

/*static*/
void gfxFontCache::WordCacheExpirationTimerCallback(nsITimer* aTimer,
                                                    void* aCache) {
  gfxFontCache* cache = static_cast<gfxFontCache*>(aCache);
  cache->AgeCachedWords();
}

void gfxFontCache::AgeCachedWords() {
  bool allEmpty = true;
  {
    MutexAutoLock lock(mMutex);
    for (const auto& entry : mFonts) {
      allEmpty = entry.mFont->AgeCachedWords() && allEmpty;
    }
  }
  if (allEmpty) {
    PauseWordCacheExpirationTimer();
  }
}

void gfxFontCache::FlushShapedWordCaches() {
  {
    MutexAutoLock lock(mMutex);
    for (const auto& entry : mFonts) {
      entry.mFont->ClearCachedWords();
    }
  }
  PauseWordCacheExpirationTimer();
}

void gfxFontCache::NotifyGlyphsChanged() {
  MutexAutoLock lock(mMutex);
  for (const auto& entry : mFonts) {
    entry.mFont->NotifyGlyphsChanged();
  }
}

void gfxFontCache::AddSizeOfExcludingThis(MallocSizeOf aMallocSizeOf,
                                          FontCacheSizes* aSizes) const {
  // TODO: add the overhead of the expiration tracker (generation arrays)

  MutexAutoLock lock(*const_cast<Mutex*>(&mMutex));
  aSizes->mFontInstances += mFonts.ShallowSizeOfExcludingThis(aMallocSizeOf);
  for (const auto& entry : mFonts) {
    entry.mFont->AddSizeOfExcludingThis(aMallocSizeOf, aSizes);
  }
}

void gfxFontCache::AddSizeOfIncludingThis(MallocSizeOf aMallocSizeOf,
                                          FontCacheSizes* aSizes) const {
  aSizes->mFontInstances += aMallocSizeOf(this);
  AddSizeOfExcludingThis(aMallocSizeOf, aSizes);
}

#define MAX_SSXX_VALUE 99
#define MAX_CVXX_VALUE 99

static void LookupAlternateValues(const gfxFontFeatureValueSet& aFeatureLookup,
                                  const nsACString& aFamily,
                                  const StyleVariantAlternates& aAlternates,
                                  nsTArray<gfxFontFeature>& aFontFeatures) {
  using Tag = StyleVariantAlternates::Tag;

  // historical-forms gets handled in nsFont::AddFontFeaturesToStyle.
  if (aAlternates.IsHistoricalForms()) {
    return;
  }

  gfxFontFeature feature;
  if (aAlternates.IsCharacterVariant()) {
    for (auto& ident : aAlternates.AsCharacterVariant().AsSpan()) {
      Span<const uint32_t> values = aFeatureLookup.GetFontFeatureValuesFor(
          aFamily, NS_FONT_VARIANT_ALTERNATES_CHARACTER_VARIANT,
          ident.AsAtom());
      // nothing defined, skip
      if (values.IsEmpty()) {
        continue;
      }
      NS_ASSERTION(values.Length() <= 2,
                   "too many values allowed for character-variant");
      // character-variant(12 3) ==> 'cv12' = 3
      uint32_t nn = values[0];
      // ignore values greater than 99
      if (nn == 0 || nn > MAX_CVXX_VALUE) {
        continue;
      }
      feature.mValue = values.Length() > 1 ? values[1] : 1;
      feature.mTag = HB_TAG('c', 'v', ('0' + nn / 10), ('0' + nn % 10));
      aFontFeatures.AppendElement(feature);
    }
    return;
  }

  if (aAlternates.IsStyleset()) {
    for (auto& ident : aAlternates.AsStyleset().AsSpan()) {
      Span<const uint32_t> values = aFeatureLookup.GetFontFeatureValuesFor(
          aFamily, NS_FONT_VARIANT_ALTERNATES_STYLESET, ident.AsAtom());

      // styleset(1 2 7) ==> 'ss01' = 1, 'ss02' = 1, 'ss07' = 1
      feature.mValue = 1;
      for (uint32_t nn : values) {
        if (nn == 0 || nn > MAX_SSXX_VALUE) {
          continue;
        }
        feature.mTag = HB_TAG('s', 's', ('0' + nn / 10), ('0' + nn % 10));
        aFontFeatures.AppendElement(feature);
      }
    }
    return;
  }

  uint32_t constant = 0;
  nsAtom* name = nullptr;
  switch (aAlternates.tag) {
    case Tag::Swash:
      constant = NS_FONT_VARIANT_ALTERNATES_SWASH;
      name = aAlternates.AsSwash().AsAtom();
      break;
    case Tag::Stylistic:
      constant = NS_FONT_VARIANT_ALTERNATES_STYLISTIC;
      name = aAlternates.AsStylistic().AsAtom();
      break;
    case Tag::Ornaments:
      constant = NS_FONT_VARIANT_ALTERNATES_ORNAMENTS;
      name = aAlternates.AsOrnaments().AsAtom();
      break;
    case Tag::Annotation:
      constant = NS_FONT_VARIANT_ALTERNATES_ANNOTATION;
      name = aAlternates.AsAnnotation().AsAtom();
      break;
    default:
      MOZ_ASSERT_UNREACHABLE("Unknown font-variant-alternates value!");
      return;
  }

  Span<const uint32_t> values =
      aFeatureLookup.GetFontFeatureValuesFor(aFamily, constant, name);
  if (values.IsEmpty()) {
    return;
  }
  MOZ_ASSERT(values.Length() == 1,
             "too many values for font-specific font-variant-alternates");

  feature.mValue = values[0];
  switch (aAlternates.tag) {
    case Tag::Swash:  // swsh, cswh
      feature.mTag = HB_TAG('s', 'w', 's', 'h');
      aFontFeatures.AppendElement(feature);
      feature.mTag = HB_TAG('c', 's', 'w', 'h');
      break;
    case Tag::Stylistic:  // salt
      feature.mTag = HB_TAG('s', 'a', 'l', 't');
      break;
    case Tag::Ornaments:  // ornm
      feature.mTag = HB_TAG('o', 'r', 'n', 'm');
      break;
    case Tag::Annotation:  // nalt
      feature.mTag = HB_TAG('n', 'a', 'l', 't');
      break;
    default:
      MOZ_ASSERT_UNREACHABLE("how?");
      return;
  }
  aFontFeatures.AppendElement(feature);
}

/* static */
void gfxFontShaper::MergeFontFeatures(
    const gfxFontStyle* aStyle, const nsTArray<gfxFontFeature>& aFontFeatures,
    bool aDisableLigatures, const nsACString& aFamilyName, bool aAddSmallCaps,
    void (*aHandleFeature)(uint32_t, uint32_t, void*),
    void* aHandleFeatureData) {
  const nsTArray<gfxFontFeature>& styleRuleFeatures = aStyle->featureSettings;

  // Bail immediately if nothing to do, which is the common case.
  if (styleRuleFeatures.IsEmpty() && aFontFeatures.IsEmpty() &&
      !aDisableLigatures &&
      aStyle->variantCaps == NS_FONT_VARIANT_CAPS_NORMAL &&
      aStyle->variantSubSuper == NS_FONT_VARIANT_POSITION_NORMAL &&
      aStyle->variantAlternates.IsEmpty()) {
    return;
  }

  AutoTArray<gfxFontFeature, 32> mergedFeatures;

  struct FeatureTagCmp {
    bool Equals(const gfxFontFeature& a, const gfxFontFeature& b) const {
      return a.mTag == b.mTag;
    }
    bool LessThan(const gfxFontFeature& a, const gfxFontFeature& b) const {
      return a.mTag < b.mTag;
    }
  } cmp;

  auto addOrReplace = [&](const gfxFontFeature& aFeature) {
    auto index = mergedFeatures.BinaryIndexOf(aFeature, cmp);
    if (index == nsTArray<gfxFontFeature>::NoIndex) {
      mergedFeatures.InsertElementSorted(aFeature, cmp);
    } else {
      mergedFeatures[index].mValue = aFeature.mValue;
    }
  };

  // add feature values from font
  for (const gfxFontFeature& feature : aFontFeatures) {
    addOrReplace(feature);
  }

  // font-variant-caps - handled here due to the need for fallback handling
  // petite caps cases can fallback to appropriate smallcaps
  uint32_t variantCaps = aStyle->variantCaps;
  switch (variantCaps) {
    case NS_FONT_VARIANT_CAPS_NORMAL:
      break;

    case NS_FONT_VARIANT_CAPS_ALLSMALL:
      addOrReplace(gfxFontFeature{HB_TAG('c', '2', 's', 'c'), 1});
      // fall through to the small-caps case
      [[fallthrough]];

    case NS_FONT_VARIANT_CAPS_SMALLCAPS:
      addOrReplace(gfxFontFeature{HB_TAG('s', 'm', 'c', 'p'), 1});
      break;

    case NS_FONT_VARIANT_CAPS_ALLPETITE:
      addOrReplace(gfxFontFeature{aAddSmallCaps ? HB_TAG('c', '2', 's', 'c')
                                                : HB_TAG('c', '2', 'p', 'c'),
                                  1});
      // fall through to the petite-caps case
      [[fallthrough]];

    case NS_FONT_VARIANT_CAPS_PETITECAPS:
      addOrReplace(gfxFontFeature{aAddSmallCaps ? HB_TAG('s', 'm', 'c', 'p')
                                                : HB_TAG('p', 'c', 'a', 'p'),
                                  1});
      break;

    case NS_FONT_VARIANT_CAPS_TITLING:
      addOrReplace(gfxFontFeature{HB_TAG('t', 'i', 't', 'l'), 1});
      break;

    case NS_FONT_VARIANT_CAPS_UNICASE:
      addOrReplace(gfxFontFeature{HB_TAG('u', 'n', 'i', 'c'), 1});
      break;

    default:
      MOZ_ASSERT_UNREACHABLE("Unexpected variantCaps");
      break;
  }

  // font-variant-position - handled here due to the need for fallback
  switch (aStyle->variantSubSuper) {
    case NS_FONT_VARIANT_POSITION_NORMAL:
      break;
    case NS_FONT_VARIANT_POSITION_SUPER:
      addOrReplace(gfxFontFeature{HB_TAG('s', 'u', 'p', 's'), 1});
      break;
    case NS_FONT_VARIANT_POSITION_SUB:
      addOrReplace(gfxFontFeature{HB_TAG('s', 'u', 'b', 's'), 1});
      break;
    default:
      MOZ_ASSERT_UNREACHABLE("Unexpected variantSubSuper");
      break;
  }

  // add font-specific feature values from style rules
  if (aStyle->featureValueLookup && !aStyle->variantAlternates.IsEmpty()) {
    AutoTArray<gfxFontFeature, 4> featureList;

    // insert list of alternate feature settings
    for (auto& alternate : aStyle->variantAlternates.AsSpan()) {
      LookupAlternateValues(*aStyle->featureValueLookup, aFamilyName, alternate,
                            featureList);
    }

    for (const gfxFontFeature& feature : featureList) {
      addOrReplace(gfxFontFeature{feature.mTag, feature.mValue});
    }
  }

  auto disableOptionalLigatures = [&]() -> void {
    addOrReplace(gfxFontFeature{HB_TAG('l', 'i', 'g', 'a'), 0});
    addOrReplace(gfxFontFeature{HB_TAG('c', 'l', 'i', 'g'), 0});
    addOrReplace(gfxFontFeature{HB_TAG('d', 'l', 'i', 'g'), 0});
    addOrReplace(gfxFontFeature{HB_TAG('h', 'l', 'i', 'g'), 0});
  };

  // Add features that are already resolved to tags & values in the style.
  if (styleRuleFeatures.IsEmpty()) {
    // Disable optional ligatures if non-zero letter-spacing is in effect.
    if (aDisableLigatures) {
      disableOptionalLigatures();
    }
  } else {
    for (const gfxFontFeature& feature : styleRuleFeatures) {
      // A dummy feature (0,0) is used as a sentinel to separate features
      // originating from font-variant-* or other high-level properties from
      // those directly specified as font-feature-settings. The high-level
      // features may be overridden by aDisableLigatures, while low-level
      // features specified directly as tags will come last and therefore
      // take precedence over everything else.
      if (feature.mTag) {
        addOrReplace(gfxFontFeature{feature.mTag, feature.mValue});
      } else if (aDisableLigatures) {
        // Handle ligature-disabling setting at the boundary between high-
        // and low-level features.
        disableOptionalLigatures();
      }
    }
  }

  for (const auto& f : mergedFeatures) {
    aHandleFeature(f.mTag, f.mValue, aHandleFeatureData);
  }
}

void gfxShapedText::SetupClusterBoundaries(uint32_t aOffset,
                                           const char16_t* aString,
                                           uint32_t aLength) {
  if (aLength == 0) {
    return;
  }

  CompressedGlyph* const glyphs = GetCharacterGlyphs() + aOffset;
  CompressedGlyph extendCluster = CompressedGlyph::MakeComplex(false, true);

  // GraphemeClusterBreakIteratorUtf16 won't be able to tell us if the string
  // _begins_ with a cluster-extender, so we handle that here
  uint32_t ch = aString[0];
  if (aLength > 1 && NS_IS_SURROGATE_PAIR(ch, aString[1])) {
    ch = SURROGATE_TO_UCS4(ch, aString[1]);
  }
  if (IsClusterExtender(ch)) {
    glyphs[0] = extendCluster;
  }

  intl::GraphemeClusterBreakIteratorUtf16 iter(
      Span<const char16_t>(aString, aLength));
  uint32_t pos = 0;

  const char16_t kIdeographicSpace = 0x3000;
  // Special case for Bengali: although Virama normally clusters with the
  // preceding letter, we *also* want to cluster it with a following Ya
  // so that when the Virama+Ya form ya-phala, this is not separated from the
  // preceding letter by any letter-spacing or justification.
  const char16_t kBengaliVirama = 0x09CD;
  const char16_t kBengaliYa = 0x09AF;
  bool prevWasHyphen = false;
  while (pos < aLength) {
    const char16_t ch = aString[pos];
    if (prevWasHyphen) {
      if (nsContentUtils::IsAlphanumeric(ch)) {
        glyphs[pos].SetCanBreakBefore(
            CompressedGlyph::FLAG_BREAK_TYPE_EMERGENCY_WRAP);
      }
      prevWasHyphen = false;
    }
    if (ch == char16_t(' ') || ch == kIdeographicSpace) {
      glyphs[pos].SetIsSpace();
    } else if (nsContentUtils::IsHyphen(ch) && pos &&
               nsContentUtils::IsAlphanumeric(aString[pos - 1])) {
      prevWasHyphen = true;
    } else if (ch == kBengaliYa) {
      // Unless we're at the start, check for a preceding virama.
      if (pos > 0 && aString[pos - 1] == kBengaliVirama) {
        glyphs[pos] = extendCluster;
      }
    }
    // advance iter to the next cluster-start (or end of text)
    const uint32_t nextPos = *iter.Next();
    // step past the first char of the cluster
    ++pos;
    // mark all the rest as cluster-continuations
    for (; pos < nextPos; ++pos) {
      glyphs[pos] = extendCluster;
    }
  }
}

void gfxShapedText::SetupClusterBoundaries(uint32_t aOffset,
                                           const uint8_t* aString,
                                           uint32_t aLength) {
  CompressedGlyph* glyphs = GetCharacterGlyphs() + aOffset;
  uint32_t pos = 0;
  bool prevWasHyphen = false;
  while (pos < aLength) {
    uint8_t ch = aString[pos];
    if (prevWasHyphen) {
      if (nsContentUtils::IsAlphanumeric(ch)) {
        glyphs->SetCanBreakBefore(
            CompressedGlyph::FLAG_BREAK_TYPE_EMERGENCY_WRAP);
      }
      prevWasHyphen = false;
    }
    if (ch == uint8_t(' ')) {
      glyphs->SetIsSpace();
    } else if (ch == uint8_t('-') && pos &&
               nsContentUtils::IsAlphanumeric(aString[pos - 1])) {
      prevWasHyphen = true;
    }
    ++pos;
    ++glyphs;
  }
}

gfxShapedText::DetailedGlyph* gfxShapedText::AllocateDetailedGlyphs(
    uint32_t aIndex, uint32_t aCount) {
  NS_ASSERTION(aIndex < GetLength(), "Index out of range");

  if (!mDetailedGlyphs) {
    mDetailedGlyphs = MakeUnique<DetailedGlyphStore>();
  }

  return mDetailedGlyphs->Allocate(aIndex, aCount);
}

void gfxShapedText::SetDetailedGlyphs(uint32_t aIndex, uint32_t aGlyphCount,
                                      const DetailedGlyph* aGlyphs) {
  CompressedGlyph& g = GetCharacterGlyphs()[aIndex];

  MOZ_ASSERT(aIndex > 0 || g.IsLigatureGroupStart(),
             "First character can't be a ligature continuation!");

  if (aGlyphCount > 0) {
    DetailedGlyph* details = AllocateDetailedGlyphs(aIndex, aGlyphCount);
    memcpy(details, aGlyphs, sizeof(DetailedGlyph) * aGlyphCount);
  }

  g.SetGlyphCount(aGlyphCount);
}

#define ZWNJ 0x200C
#define ZWJ 0x200D
static inline bool IsIgnorable(uint32_t aChar) {
  return (IsDefaultIgnorable(aChar)) || aChar == ZWNJ || aChar == ZWJ;
}

void gfxShapedText::SetMissingGlyph(uint32_t aIndex, uint32_t aChar,
                                    gfxFont* aFont) {
  CompressedGlyph& g = GetCharacterGlyphs()[aIndex];
  uint8_t category = GetGeneralCategory(aChar);
  if (category >= HB_UNICODE_GENERAL_CATEGORY_SPACING_MARK &&
      category <= HB_UNICODE_GENERAL_CATEGORY_NON_SPACING_MARK) {
    g.SetComplex(false, true);
  }

  // Leaving advance as zero will prevent drawing the hexbox for ignorables.
  int32_t advance = 0;
  if (!IsIgnorable(aChar)) {
    gfxFloat width =
        std::max(aFont->GetMetrics(nsFontMetrics::eHorizontal).aveCharWidth,
                 gfxFloat(gfxFontMissingGlyphs::GetDesiredMinWidth(
                     aChar, mAppUnitsPerDevUnit)));
    advance = int32_t(width * mAppUnitsPerDevUnit);
  }
  DetailedGlyph detail = {aChar, advance, gfx::Point()};
  SetDetailedGlyphs(aIndex, 1, &detail);
  g.SetMissing();
}

bool gfxShapedText::FilterIfIgnorable(uint32_t aIndex, uint32_t aCh) {
  if (IsIgnorable(aCh)) {
    // There are a few default-ignorables of Letter category (currently,
    // just the Hangul filler characters) that we'd better not discard
    // if they're followed by additional characters in the same cluster.
    // Some fonts use them to carry the width of a whole cluster of
    // combining jamos; see bug 1238243.
    auto* charGlyphs = GetCharacterGlyphs();
    if (GetGenCategory(aCh) == nsUGenCategory::kLetter &&
        aIndex + 1 < GetLength() && !charGlyphs[aIndex + 1].IsClusterStart()) {
      return false;
    }
    // A compressedGlyph that is set to MISSING but has no DetailedGlyphs list
    // will be zero-width/invisible, which is what we want here.
    CompressedGlyph& g = charGlyphs[aIndex];
    g.SetComplex(g.IsClusterStart(), g.IsLigatureGroupStart()).SetMissing();
    return true;
  }
  return false;
}

void gfxShapedText::ApplyTrackingToClusters(gfxFloat aTrackingAdjustment,
                                            uint32_t aOffset,
                                            uint32_t aLength) {
  int32_t appUnitAdjustment =
      NS_round(aTrackingAdjustment * gfxFloat(mAppUnitsPerDevUnit));
  CompressedGlyph* charGlyphs = GetCharacterGlyphs();
  for (uint32_t i = aOffset; i < aOffset + aLength; ++i) {
    CompressedGlyph* glyphData = charGlyphs + i;
    if (glyphData->IsSimpleGlyph()) {
      // simple glyphs ==> just add the advance
      int32_t advance = glyphData->GetSimpleAdvance();
      if (advance > 0) {
        advance = std::max(0, advance + appUnitAdjustment);
        if (CompressedGlyph::IsSimpleAdvance(advance)) {
          glyphData->SetSimpleGlyph(advance, glyphData->GetSimpleGlyph());
        } else {
          // rare case, tested by making this the default
          uint32_t glyphIndex = glyphData->GetSimpleGlyph();
          // convert the simple CompressedGlyph to an empty complex record
          glyphData->SetComplex(true, true);
          // then set its details (glyph ID with its new advance)
          DetailedGlyph detail = {glyphIndex, advance, gfx::Point()};
          SetDetailedGlyphs(i, 1, &detail);
        }
      }
    } else {
      // complex glyphs ==> add offset at cluster/ligature boundaries
      uint32_t detailedLength = glyphData->GetGlyphCount();
      if (detailedLength) {
        DetailedGlyph* details = GetDetailedGlyphs(i);
        if (!details) {
          continue;
        }
        auto& advance = IsRightToLeft() ? details[0].mAdvance
                                        : details[detailedLength - 1].mAdvance;
        if (advance > 0) {
          advance = std::max(0, advance + appUnitAdjustment);
        }
      }
    }
  }
}

size_t gfxShapedWord::SizeOfIncludingThis(MallocSizeOf aMallocSizeOf) const {
  size_t total = aMallocSizeOf(this);
  if (mDetailedGlyphs) {
    total += mDetailedGlyphs->SizeOfIncludingThis(aMallocSizeOf);
  }
  return total;
}

float gfxFont::AngleForSyntheticOblique() const {
  // First check conditions that mean no synthetic slant should be used:
  if (mStyle.style == FontSlantStyle::NORMAL) {
    return 0.0f;  // Requested style is 'normal'.
  }
  if (mStyle.synthesisStyle == StyleFontSynthesisStyle::None) {
    return 0.0f;  // Synthetic obliquing is disabled.
  }
  if (!mFontEntry->MayUseSyntheticSlant()) {
    return 0.0f;  // The resource supports "real" slant, so don't synthesize.
  }

  // If style calls for italic, and face doesn't support it, use default
  // oblique angle as a simulation, but only if synthesis setting allows it.
  if (mStyle.style.IsItalic()) {
    return mFontEntry->SupportsItalic() ? 0.0f
           : mStyle.synthesisStyle == StyleFontSynthesisStyle::Auto
               ? FontSlantStyle::DEFAULT_OBLIQUE_DEGREES
               : 0.0f;
  }

  // OK, we're going to use synthetic oblique: return the requested angle.
  return mStyle.style.ObliqueAngle();
}

float gfxFont::SkewForSyntheticOblique() const {
  // Precomputed value of tan(kDefaultAngle), the default italic/oblique slant;
  // avoids calling tan() at runtime except for custom oblique values.
  static const float kTanDefaultAngle =
      tan(FontSlantStyle::DEFAULT_OBLIQUE_DEGREES * (M_PI / 180.0));

  float angle = AngleForSyntheticOblique();
  if (angle == 0.0f) {
    return 0.0f;
  } else if (angle == FontSlantStyle::DEFAULT_OBLIQUE_DEGREES) {
    return kTanDefaultAngle;
  } else {
    return tan(angle * (M_PI / 180.0));
  }
}

void gfxFont::RunMetrics::CombineWith(const RunMetrics& aOther,
                                      bool aOtherIsOnLeft) {
  mAscent = std::max(mAscent, aOther.mAscent);
  mDescent = std::max(mDescent, aOther.mDescent);
  if (aOtherIsOnLeft) {
    mBoundingBox = (mBoundingBox + gfxPoint(aOther.mAdvanceWidth, 0))
                       .Union(aOther.mBoundingBox);
  } else {
    mBoundingBox =
        mBoundingBox.Union(aOther.mBoundingBox + gfxPoint(mAdvanceWidth, 0));
  }
  mAdvanceWidth += aOther.mAdvanceWidth;
}

gfxFont::gfxFont(const RefPtr<UnscaledFont>& aUnscaledFont,
                 gfxFontEntry* aFontEntry, const gfxFontStyle* aFontStyle,
                 AntialiasOption anAAOption)
    : mFontEntry(aFontEntry),
      mLock("gfxFont lock"),
      mUnscaledFont(aUnscaledFont),
      mStyle(*aFontStyle),
      mAdjustedSize(-1.0),       // negative to indicate "not yet initialized"
      mFUnitsConvFactor(-1.0f),  // negative to indicate "not yet initialized"
      mAntialiasOption(anAAOption),
      mIsValid(true),
      mApplySyntheticBold(false),
      mKerningEnabled(false),
      mMathInitialized(false) {
#ifdef DEBUG_TEXT_RUN_STORAGE_METRICS
  ++gFontCount;
#endif

  if (MOZ_UNLIKELY(StaticPrefs::gfx_text_disable_aa_AtStartup())) {
    mAntialiasOption = kAntialiasNone;
  }

  // Turn off AA for Ahem for testing purposes when requested.
  if (MOZ_UNLIKELY(StaticPrefs::gfx_font_rendering_ahem_antialias_none() &&
                   mFontEntry->FamilyName().EqualsLiteral("Ahem"))) {
    mAntialiasOption = kAntialiasNone;
  }

  mKerningSet = HasFeatureSet(HB_TAG('k', 'e', 'r', 'n'), mKerningEnabled);

  // Ensure the gfxFontEntry's unitsPerEm and extents fields are initialized,
  // so that GetFontExtents can use them without risk of races.
  Unused << mFontEntry->UnitsPerEm();
}

gfxFont::~gfxFont() {
  mFontEntry->NotifyFontDestroyed(this);

  // Delete objects owned through atomic pointers. (Some of these may be null,
  // but that's OK.)
  delete mVerticalMetrics.exchange(nullptr);
  delete mHarfBuzzShaper.exchange(nullptr);
  delete mGraphiteShaper.exchange(nullptr);
  delete mMathTable.exchange(nullptr);
  delete mNonAAFont.exchange(nullptr);

  if (auto* scaledFont = mAzureScaledFont.exchange(nullptr)) {
    scaledFont->Release();
  }

  if (mGlyphChangeObservers) {
    for (const auto& key : *mGlyphChangeObservers) {
      key->ForgetFont();
    }
  }
}

// Work out whether cairo will snap inter-glyph spacing to pixels.
//
// Layout does not align text to pixel boundaries, so, with font drawing
// backends that snap glyph positions to pixels, it is important that
// inter-glyph spacing within words is always an integer number of pixels.
// This ensures that the drawing backend snaps all of the word's glyphs in the
// same direction and so inter-glyph spacing remains the same.
//
gfxFont::RoundingFlags gfxFont::GetRoundOffsetsToPixels(
    DrawTarget* aDrawTarget) {
  // Could do something fancy here for ScaleFactors of
  // AxisAlignedTransforms, but we leave things simple.
  // Not much point rounding if a matrix will mess things up anyway.
  // Also check if the font already knows hint metrics is off...
  if (aDrawTarget->GetTransform().HasNonTranslation() || !ShouldHintMetrics()) {
    return RoundingFlags(0);
  }

  cairo_t* cr = static_cast<cairo_t*>(
      aDrawTarget->GetNativeSurface(NativeSurfaceType::CAIRO_CONTEXT));
  if (cr) {
    cairo_surface_t* target = cairo_get_target(cr);

    // Check whether the cairo surface's font options hint metrics.
    cairo_font_options_t* fontOptions = cairo_font_options_create();
    cairo_surface_get_font_options(target, fontOptions);
    cairo_hint_metrics_t hintMetrics =
        cairo_font_options_get_hint_metrics(fontOptions);
    cairo_font_options_destroy(fontOptions);

    switch (hintMetrics) {
      case CAIRO_HINT_METRICS_OFF:
        return RoundingFlags(0);
      case CAIRO_HINT_METRICS_ON:
        return RoundingFlags::kRoundX | RoundingFlags::kRoundY;
      default:
        break;
    }
  }

  if (ShouldRoundXOffset(cr)) {
    return RoundingFlags::kRoundX | RoundingFlags::kRoundY;
  } else {
    return RoundingFlags::kRoundY;
  }
}

gfxHarfBuzzShaper* gfxFont::GetHarfBuzzShaper() {
  if (!mHarfBuzzShaper) {
    auto* shaper = new gfxHarfBuzzShaper(this);
    shaper->Initialize();
    if (!mHarfBuzzShaper.compareExchange(nullptr, shaper)) {
      delete shaper;
    }
  }
  gfxHarfBuzzShaper* shaper = mHarfBuzzShaper;
  return shaper->IsInitialized() ? shaper : nullptr;
}

gfxFloat gfxFont::GetGlyphAdvance(uint16_t aGID, bool aVertical) {
  if (!aVertical && ProvidesGlyphWidths()) {
    return GetGlyphWidth(aGID) / 65536.0;
  }
  if (mFUnitsConvFactor < 0.0f) {
    // Metrics haven't been initialized; lock while we do that.
    AutoWriteLock lock(mLock);
    if (mFUnitsConvFactor < 0.0f) {
      GetMetrics(nsFontMetrics::eHorizontal);
    }
  }
  NS_ASSERTION(mFUnitsConvFactor >= 0.0f,
               "missing font unit conversion factor");
  if (gfxHarfBuzzShaper* shaper = GetHarfBuzzShaper()) {
    if (aVertical) {
      // Note that GetGlyphVAdvance may return -1 to indicate it was unable
      // to retrieve vertical metrics; in that case we fall back to the
      // aveCharWidth value as a default advance.
      int32_t advance = shaper->GetGlyphVAdvance(aGID);
      if (advance < 0) {
        return GetMetrics(nsFontMetrics::eVertical).aveCharWidth;
      }
      return advance / 65536.0;
    }
    return shaper->GetGlyphHAdvance(aGID) / 65536.0;
  }
  return 0.0;
}

gfxFloat gfxFont::GetCharAdvance(uint32_t aUnicode, bool aVertical) {
  uint32_t gid = 0;
  if (ProvidesGetGlyph()) {
    gid = GetGlyph(aUnicode, 0);
  } else {
    if (gfxHarfBuzzShaper* shaper = GetHarfBuzzShaper()) {
      gid = shaper->GetNominalGlyph(aUnicode);
    }
  }
  if (!gid) {
    return -1.0;
  }
  return GetGlyphAdvance(gid, aVertical);
}

static void CollectLookupsByFeature(hb_face_t* aFace, hb_tag_t aTableTag,
                                    uint32_t aFeatureIndex,
                                    hb_set_t* aLookups) {
  uint32_t lookups[32];
  uint32_t i, len, offset;

  offset = 0;
  do {
    len = std::size(lookups);
    hb_ot_layout_feature_get_lookups(aFace, aTableTag, aFeatureIndex, offset,
                                     &len, lookups);
    for (i = 0; i < len; i++) {
      hb_set_add(aLookups, lookups[i]);
    }
    offset += len;
  } while (len == std::size(lookups));
}

static void CollectLookupsByLanguage(
    hb_face_t* aFace, hb_tag_t aTableTag,
    const nsTHashSet<uint32_t>& aSpecificFeatures, hb_set_t* aOtherLookups,
    hb_set_t* aSpecificFeatureLookups, uint32_t aScriptIndex,
    uint32_t aLangIndex) {
  uint32_t reqFeatureIndex;
  if (hb_ot_layout_language_get_required_feature_index(
          aFace, aTableTag, aScriptIndex, aLangIndex, &reqFeatureIndex)) {
    CollectLookupsByFeature(aFace, aTableTag, reqFeatureIndex, aOtherLookups);
  }

  uint32_t featureIndexes[32];
  uint32_t i, len, offset;

  offset = 0;
  do {
    len = std::size(featureIndexes);
    hb_ot_layout_language_get_feature_indexes(aFace, aTableTag, aScriptIndex,
                                              aLangIndex, offset, &len,
                                              featureIndexes);

    for (i = 0; i < len; i++) {
      uint32_t featureIndex = featureIndexes[i];

      // get the feature tag
      hb_tag_t featureTag;
      uint32_t tagLen = 1;
      hb_ot_layout_language_get_feature_tags(aFace, aTableTag, aScriptIndex,
                                             aLangIndex, offset + i, &tagLen,
                                             &featureTag);

      // collect lookups
      hb_set_t* lookups = aSpecificFeatures.Contains(featureTag)
                              ? aSpecificFeatureLookups
                              : aOtherLookups;
      CollectLookupsByFeature(aFace, aTableTag, featureIndex, lookups);
    }
    offset += len;
  } while (len == std::size(featureIndexes));
}

static bool HasLookupRuleWithGlyphByScript(
    hb_face_t* aFace, hb_tag_t aTableTag, hb_tag_t aScriptTag,
    uint32_t aScriptIndex, uint16_t aGlyph,
    const nsTHashSet<uint32_t>& aDefaultFeatures,
    bool& aHasDefaultFeatureWithGlyph) {
  uint32_t numLangs, lang;
  hb_set_t* defaultFeatureLookups = hb_set_create();
  hb_set_t* nonDefaultFeatureLookups = hb_set_create();

  // default lang
  CollectLookupsByLanguage(aFace, aTableTag, aDefaultFeatures,
                           nonDefaultFeatureLookups, defaultFeatureLookups,
                           aScriptIndex, HB_OT_LAYOUT_DEFAULT_LANGUAGE_INDEX);

  // iterate over langs
  numLangs = hb_ot_layout_script_get_language_tags(
      aFace, aTableTag, aScriptIndex, 0, nullptr, nullptr);
  for (lang = 0; lang < numLangs; lang++) {
    CollectLookupsByLanguage(aFace, aTableTag, aDefaultFeatures,
                             nonDefaultFeatureLookups, defaultFeatureLookups,
                             aScriptIndex, lang);
  }

  // look for the glyph among default feature lookups
  aHasDefaultFeatureWithGlyph = false;
  hb_set_t* glyphs = hb_set_create();
  hb_codepoint_t index = -1;
  while (hb_set_next(defaultFeatureLookups, &index)) {
    hb_ot_layout_lookup_collect_glyphs(aFace, aTableTag, index, glyphs, glyphs,
                                       glyphs, nullptr);
    if (hb_set_has(glyphs, aGlyph)) {
      aHasDefaultFeatureWithGlyph = true;
      break;
    }
  }

  // look for the glyph among non-default feature lookups
  // if no default feature lookups contained spaces
  bool hasNonDefaultFeatureWithGlyph = false;
  if (!aHasDefaultFeatureWithGlyph) {
    hb_set_clear(glyphs);
    index = -1;
    while (hb_set_next(nonDefaultFeatureLookups, &index)) {
      hb_ot_layout_lookup_collect_glyphs(aFace, aTableTag, index, glyphs,
                                         glyphs, glyphs, nullptr);
      if (hb_set_has(glyphs, aGlyph)) {
        hasNonDefaultFeatureWithGlyph = true;
        break;
      }
    }
  }

  hb_set_destroy(glyphs);
  hb_set_destroy(defaultFeatureLookups);
  hb_set_destroy(nonDefaultFeatureLookups);

  return aHasDefaultFeatureWithGlyph || hasNonDefaultFeatureWithGlyph;
}

static void HasLookupRuleWithGlyph(hb_face_t* aFace, hb_tag_t aTableTag,
                                   bool& aHasGlyph, hb_tag_t aSpecificFeature,
                                   bool& aHasGlyphSpecific, uint16_t aGlyph) {
  // iterate over the scripts in the font
  uint32_t numScripts, numLangs, script, lang;
  hb_set_t* otherLookups = hb_set_create();
  hb_set_t* specificFeatureLookups = hb_set_create();
  nsTHashSet<uint32_t> specificFeature(1);

  specificFeature.Insert(aSpecificFeature);

  numScripts =
      hb_ot_layout_table_get_script_tags(aFace, aTableTag, 0, nullptr, nullptr);

  for (script = 0; script < numScripts; script++) {
    // default lang
    CollectLookupsByLanguage(aFace, aTableTag, specificFeature, otherLookups,
                             specificFeatureLookups, script,
                             HB_OT_LAYOUT_DEFAULT_LANGUAGE_INDEX);

    // iterate over langs
    numLangs = hb_ot_layout_script_get_language_tags(
        aFace, HB_OT_TAG_GPOS, script, 0, nullptr, nullptr);
    for (lang = 0; lang < numLangs; lang++) {
      CollectLookupsByLanguage(aFace, aTableTag, specificFeature, otherLookups,
                               specificFeatureLookups, script, lang);
    }
  }

  // look for the glyph among non-specific feature lookups
  hb_set_t* glyphs = hb_set_create();
  hb_codepoint_t index = -1;
  while (hb_set_next(otherLookups, &index)) {
    hb_ot_layout_lookup_collect_glyphs(aFace, aTableTag, index, glyphs, glyphs,
                                       glyphs, nullptr);
    if (hb_set_has(glyphs, aGlyph)) {
      aHasGlyph = true;
      break;
    }
  }

  // look for the glyph among specific feature lookups
  hb_set_clear(glyphs);
  index = -1;
  while (hb_set_next(specificFeatureLookups, &index)) {
    hb_ot_layout_lookup_collect_glyphs(aFace, aTableTag, index, glyphs, glyphs,
                                       glyphs, nullptr);
    if (hb_set_has(glyphs, aGlyph)) {
      aHasGlyphSpecific = true;
      break;
    }
  }

  hb_set_destroy(glyphs);
  hb_set_destroy(specificFeatureLookups);
  hb_set_destroy(otherLookups);
}

Atomic<nsTHashMap<nsUint32HashKey, intl::Script>*> gfxFont::sScriptTagToCode;
Atomic<nsTHashSet<uint32_t>*> gfxFont::sDefaultFeatures;

static inline bool HasSubstitution(uint32_t* aBitVector, intl::Script aScript) {
  return (aBitVector[static_cast<uint32_t>(aScript) >> 5] &
          (1 << (static_cast<uint32_t>(aScript) & 0x1f))) != 0;
}

// union of all default substitution features across scripts
static const hb_tag_t defaultFeatures[] = {
    HB_TAG('a', 'b', 'v', 'f'), HB_TAG('a', 'b', 'v', 's'),
    HB_TAG('a', 'k', 'h', 'n'), HB_TAG('b', 'l', 'w', 'f'),
    HB_TAG('b', 'l', 'w', 's'), HB_TAG('c', 'a', 'l', 't'),
    HB_TAG('c', 'c', 'm', 'p'), HB_TAG('c', 'f', 'a', 'r'),
    HB_TAG('c', 'j', 'c', 't'), HB_TAG('c', 'l', 'i', 'g'),
    HB_TAG('f', 'i', 'n', '2'), HB_TAG('f', 'i', 'n', '3'),
    HB_TAG('f', 'i', 'n', 'a'), HB_TAG('h', 'a', 'l', 'f'),
    HB_TAG('h', 'a', 'l', 'n'), HB_TAG('i', 'n', 'i', 't'),
    HB_TAG('i', 's', 'o', 'l'), HB_TAG('l', 'i', 'g', 'a'),
    HB_TAG('l', 'j', 'm', 'o'), HB_TAG('l', 'o', 'c', 'l'),
    HB_TAG('l', 't', 'r', 'a'), HB_TAG('l', 't', 'r', 'm'),
    HB_TAG('m', 'e', 'd', '2'), HB_TAG('m', 'e', 'd', 'i'),
    HB_TAG('m', 's', 'e', 't'), HB_TAG('n', 'u', 'k', 't'),
    HB_TAG('p', 'r', 'e', 'f'), HB_TAG('p', 'r', 'e', 's'),
    HB_TAG('p', 's', 't', 'f'), HB_TAG('p', 's', 't', 's'),
    HB_TAG('r', 'c', 'l', 't'), HB_TAG('r', 'l', 'i', 'g'),
    HB_TAG('r', 'k', 'r', 'f'), HB_TAG('r', 'p', 'h', 'f'),
    HB_TAG('r', 't', 'l', 'a'), HB_TAG('r', 't', 'l', 'm'),
    HB_TAG('t', 'j', 'm', 'o'), HB_TAG('v', 'a', 't', 'u'),
    HB_TAG('v', 'e', 'r', 't'), HB_TAG('v', 'j', 'm', 'o')};

void gfxFont::CheckForFeaturesInvolvingSpace() const {
  gfxFontEntry::SpaceFeatures flags = gfxFontEntry::SpaceFeatures::None;

  // mFontEntry->mHasSpaceFeatures is a std::atomic<>, so we set it with
  // `exchange` to avoid a potential data race. It's ok if two threads both
  // try to set it; they'll end up with the same value, so it doesn't matter
  // that one will overwrite the other.
  auto setFlags =
      MakeScopeExit([&]() { mFontEntry->mHasSpaceFeatures.exchange(flags); });

  bool log = LOG_FONTINIT_ENABLED();
  TimeStamp start;
  if (MOZ_UNLIKELY(log)) {
    start = TimeStamp::Now();
  }

  uint32_t spaceGlyph = GetSpaceGlyph();
  if (!spaceGlyph) {
    return;
  }

  auto face(GetFontEntry()->GetHBFace());

  // GSUB lookups - examine per script
  if (hb_ot_layout_has_substitution(face)) {
    // Get the script ==> code hashtable, creating it on first use.
    nsTHashMap<nsUint32HashKey, Script>* tagToCode = sScriptTagToCode;
    if (!tagToCode) {
      tagToCode = new nsTHashMap<nsUint32HashKey, Script>(
          size_t(Script::NUM_SCRIPT_CODES));
      tagToCode->InsertOrUpdate(HB_TAG('D', 'F', 'L', 'T'), Script::COMMON);
      // Ensure that we don't try to look at script codes beyond what the
      // current version of ICU (at runtime -- in case of system ICU)
      // knows about.
      Script scriptCount = Script(
          std::min<int>(intl::UnicodeProperties::GetMaxNumberOfScripts() + 1,
                        int(Script::NUM_SCRIPT_CODES)));
      for (Script s = Script::ARABIC; s < scriptCount;
           s = Script(static_cast<int>(s) + 1)) {
        hb_script_t script = hb_script_t(GetScriptTagForCode(s));
        unsigned int scriptCount = 4;
        hb_tag_t scriptTags[4];
        hb_ot_tags_from_script_and_language(script, HB_LANGUAGE_INVALID,
                                            &scriptCount, scriptTags, nullptr,
                                            nullptr);
        for (unsigned int i = 0; i < scriptCount; i++) {
          tagToCode->InsertOrUpdate(scriptTags[i], s);
        }
      }
      if (!sScriptTagToCode.compareExchange(nullptr, tagToCode)) {
        // We lost a race! Discard our new table and use the winner.
        delete tagToCode;
        tagToCode = sScriptTagToCode;
      }
    }

    // Set up the default-features hashset on first use.
    if (!sDefaultFeatures) {
      uint32_t numDefaultFeatures = std::size(defaultFeatures);
      auto* set = new nsTHashSet<uint32_t>(numDefaultFeatures);
      for (uint32_t i = 0; i < numDefaultFeatures; i++) {
        set->Insert(defaultFeatures[i]);
      }
      if (!sDefaultFeatures.compareExchange(nullptr, set)) {
        delete set;
      }
    }

    // iterate over the scripts in the font
    hb_tag_t scriptTags[8];

    uint32_t len, offset = 0;
    do {
      len = std::size(scriptTags);
      hb_ot_layout_table_get_script_tags(face, HB_OT_TAG_GSUB, offset, &len,
                                         scriptTags);
      for (uint32_t i = 0; i < len; i++) {
        bool isDefaultFeature = false;
        Script s;
        if (!HasLookupRuleWithGlyphByScript(
                face, HB_OT_TAG_GSUB, scriptTags[i], offset + i, spaceGlyph,
                *sDefaultFeatures, isDefaultFeature) ||
            !tagToCode->Get(scriptTags[i], &s)) {
          continue;
        }
        flags = flags | gfxFontEntry::SpaceFeatures::HasFeatures;
        uint32_t index = static_cast<uint32_t>(s) >> 5;
        uint32_t bit = static_cast<uint32_t>(s) & 0x1f;
        if (isDefaultFeature) {
          mFontEntry->mDefaultSubSpaceFeatures[index] |= (1 << bit);
        } else {
          mFontEntry->mNonDefaultSubSpaceFeatures[index] |= (1 << bit);
        }
      }
      offset += len;
    } while (len == std::size(scriptTags));
  }

  // spaces in default features of default script?
  // ==> can't use word cache, skip GPOS analysis
  bool canUseWordCache = true;
  if (HasSubstitution(mFontEntry->mDefaultSubSpaceFeatures, Script::COMMON)) {
    canUseWordCache = false;
  }

  // GPOS lookups - distinguish kerning from non-kerning features
  if (canUseWordCache && hb_ot_layout_has_positioning(face)) {
    bool hasKerning = false, hasNonKerning = false;
    HasLookupRuleWithGlyph(face, HB_OT_TAG_GPOS, hasNonKerning,
                           HB_TAG('k', 'e', 'r', 'n'), hasKerning, spaceGlyph);
    if (hasKerning) {
      flags |= gfxFontEntry::SpaceFeatures::HasFeatures |
               gfxFontEntry::SpaceFeatures::Kerning;
    }
    if (hasNonKerning) {
      flags |= gfxFontEntry::SpaceFeatures::HasFeatures |
               gfxFontEntry::SpaceFeatures::NonKerning;
    }
  }

  if (MOZ_UNLIKELY(log)) {
    TimeDuration elapsed = TimeStamp::Now() - start;
    LOG_FONTINIT((
        "(fontinit-spacelookups) font: %s - "
        "subst default: %8.8x %8.8x %8.8x %8.8x "
        "subst non-default: %8.8x %8.8x %8.8x %8.8x "
        "kerning: %s non-kerning: %s time: %6.3f\n",
        mFontEntry->Name().get(), mFontEntry->mDefaultSubSpaceFeatures[3],
        mFontEntry->mDefaultSubSpaceFeatures[2],
        mFontEntry->mDefaultSubSpaceFeatures[1],
        mFontEntry->mDefaultSubSpaceFeatures[0],
        mFontEntry->mNonDefaultSubSpaceFeatures[3],
        mFontEntry->mNonDefaultSubSpaceFeatures[2],
        mFontEntry->mNonDefaultSubSpaceFeatures[1],
        mFontEntry->mNonDefaultSubSpaceFeatures[0],
        (mFontEntry->mHasSpaceFeatures & gfxFontEntry::SpaceFeatures::Kerning
             ? "true"
             : "false"),
        (mFontEntry->mHasSpaceFeatures & gfxFontEntry::SpaceFeatures::NonKerning
             ? "true"
             : "false"),
        elapsed.ToMilliseconds()));
  }
}

bool gfxFont::HasSubstitutionRulesWithSpaceLookups(Script aRunScript) const {
  NS_ASSERTION(GetFontEntry()->mHasSpaceFeatures !=
                   gfxFontEntry::SpaceFeatures::Uninitialized,
               "need to initialize space lookup flags");
  NS_ASSERTION(aRunScript < Script::NUM_SCRIPT_CODES, "weird script code");
  if (aRunScript == Script::INVALID || aRunScript >= Script::NUM_SCRIPT_CODES) {
    return false;
  }

  // default features have space lookups ==> true
  if (HasSubstitution(mFontEntry->mDefaultSubSpaceFeatures, Script::COMMON) ||
      HasSubstitution(mFontEntry->mDefaultSubSpaceFeatures, aRunScript)) {
    return true;
  }

  // non-default features have space lookups and some type of
  // font feature, in font or style is specified ==> true
  if ((HasSubstitution(mFontEntry->mNonDefaultSubSpaceFeatures,
                       Script::COMMON) ||
       HasSubstitution(mFontEntry->mNonDefaultSubSpaceFeatures, aRunScript)) &&
      (!mStyle.featureSettings.IsEmpty() ||
       !mFontEntry->mFeatureSettings.IsEmpty())) {
    return true;
  }

  return false;
}

tainted_boolean_hint gfxFont::SpaceMayParticipateInShaping(
    Script aRunScript) const {
  // avoid checking fonts known not to include default space-dependent features
  if (MOZ_UNLIKELY(mFontEntry->mSkipDefaultFeatureSpaceCheck)) {
    if (!mKerningSet && mStyle.featureSettings.IsEmpty() &&
        mFontEntry->mFeatureSettings.IsEmpty()) {
      return false;
    }
  }

  if (FontCanSupportGraphite()) {
    if (gfxPlatform::GetPlatform()->UseGraphiteShaping()) {
      return mFontEntry->HasGraphiteSpaceContextuals();
    }
  }

  // We record the presence of space-dependent features in the font entry
  // so that subsequent instantiations for the same font face won't
  // require us to re-check the tables; however, the actual check is done
  // by gfxFont because not all font entry subclasses know how to create
  // a harfbuzz face for introspection.
  gfxFontEntry::SpaceFeatures flags = mFontEntry->mHasSpaceFeatures;
  if (flags == gfxFontEntry::SpaceFeatures::Uninitialized) {
    CheckForFeaturesInvolvingSpace();
    flags = mFontEntry->mHasSpaceFeatures;
  }

  if (!(flags & gfxFontEntry::SpaceFeatures::HasFeatures)) {
    return false;
  }

  // if font has substitution rules or non-kerning positioning rules
  // that involve spaces, bypass
  if (HasSubstitutionRulesWithSpaceLookups(aRunScript) ||
      (flags & gfxFontEntry::SpaceFeatures::NonKerning)) {
    return true;
  }

  // if kerning explicitly enabled/disabled via font-feature-settings or
  // font-kerning and kerning rules use spaces, only bypass when enabled
  if (mKerningSet && (flags & gfxFontEntry::SpaceFeatures::Kerning)) {
    return mKerningEnabled;
  }

  return false;
}

bool gfxFont::SupportsFeature(Script aScript, uint32_t aFeatureTag) {
  if (mGraphiteShaper && gfxPlatform::GetPlatform()->UseGraphiteShaping()) {
    return GetFontEntry()->SupportsGraphiteFeature(aFeatureTag);
  }
  return GetFontEntry()->SupportsOpenTypeFeature(aScript, aFeatureTag);
}

bool gfxFont::SupportsVariantCaps(Script aScript, uint32_t aVariantCaps,
                                  bool& aFallbackToSmallCaps,
                                  bool& aSyntheticLowerToSmallCaps,
                                  bool& aSyntheticUpperToSmallCaps) {
  bool ok = true;  // cases without fallback are fine
  aFallbackToSmallCaps = false;
  aSyntheticLowerToSmallCaps = false;
  aSyntheticUpperToSmallCaps = false;
  switch (aVariantCaps) {
    case NS_FONT_VARIANT_CAPS_SMALLCAPS:
      ok = SupportsFeature(aScript, HB_TAG('s', 'm', 'c', 'p'));
      if (!ok) {
        aSyntheticLowerToSmallCaps = true;
      }
      break;
    case NS_FONT_VARIANT_CAPS_ALLSMALL:
      ok = SupportsFeature(aScript, HB_TAG('s', 'm', 'c', 'p')) &&
           SupportsFeature(aScript, HB_TAG('c', '2', 's', 'c'));
      if (!ok) {
        aSyntheticLowerToSmallCaps = true;
        aSyntheticUpperToSmallCaps = true;
      }
      break;
    case NS_FONT_VARIANT_CAPS_PETITECAPS:
      ok = SupportsFeature(aScript, HB_TAG('p', 'c', 'a', 'p'));
      if (!ok) {
        ok = SupportsFeature(aScript, HB_TAG('s', 'm', 'c', 'p'));
        aFallbackToSmallCaps = ok;
      }
      if (!ok) {
        aSyntheticLowerToSmallCaps = true;
      }
      break;
    case NS_FONT_VARIANT_CAPS_ALLPETITE:
      ok = SupportsFeature(aScript, HB_TAG('p', 'c', 'a', 'p')) &&
           SupportsFeature(aScript, HB_TAG('c', '2', 'p', 'c'));
      if (!ok) {
        ok = SupportsFeature(aScript, HB_TAG('s', 'm', 'c', 'p')) &&
             SupportsFeature(aScript, HB_TAG('c', '2', 's', 'c'));
        aFallbackToSmallCaps = ok;
      }
      if (!ok) {
        aSyntheticLowerToSmallCaps = true;
        aSyntheticUpperToSmallCaps = true;
      }
      break;
    default:
      break;
  }

  NS_ASSERTION(
      !(ok && (aSyntheticLowerToSmallCaps || aSyntheticUpperToSmallCaps)),
      "shouldn't use synthetic features if we found real ones");

  NS_ASSERTION(!(!ok && aFallbackToSmallCaps),
               "if we found a usable fallback, that counts as ok");

  return ok;
}

bool gfxFont::SupportsSubSuperscript(uint32_t aSubSuperscript,
                                     const uint8_t* aString, uint32_t aLength,
                                     Script aRunScript) {
  NS_ConvertASCIItoUTF16 unicodeString(reinterpret_cast<const char*>(aString),
                                       aLength);
  return SupportsSubSuperscript(aSubSuperscript, unicodeString.get(), aLength,
                                aRunScript);
}

bool gfxFont::SupportsSubSuperscript(uint32_t aSubSuperscript,
                                     const char16_t* aString, uint32_t aLength,
                                     Script aRunScript) {
  NS_ASSERTION(aSubSuperscript == NS_FONT_VARIANT_POSITION_SUPER ||
                   aSubSuperscript == NS_FONT_VARIANT_POSITION_SUB,
               "unknown value of font-variant-position");

  uint32_t feature = aSubSuperscript == NS_FONT_VARIANT_POSITION_SUPER
                         ? HB_TAG('s', 'u', 'p', 's')
                         : HB_TAG('s', 'u', 'b', 's');

  if (!SupportsFeature(aRunScript, feature)) {
    return false;
  }

  // xxx - for graphite, don't really know how to sniff lookups so bail
  if (mGraphiteShaper && gfxPlatform::GetPlatform()->UseGraphiteShaping()) {
    return true;
  }

  gfxHarfBuzzShaper* shaper = GetHarfBuzzShaper();
  if (!shaper) {
    return false;
  }

  // get the hbset containing input glyphs for the feature
  const hb_set_t* inputGlyphs =
      mFontEntry->InputsForOpenTypeFeature(aRunScript, feature);

  // create an hbset containing default glyphs for the script run
  hb_set_t* defaultGlyphsInRun = hb_set_create();

  // for each character, get the glyph id
  for (uint32_t i = 0; i < aLength; i++) {
    uint32_t ch = aString[i];

    if (i + 1 < aLength && NS_IS_SURROGATE_PAIR(ch, aString[i + 1])) {
      i++;
      ch = SURROGATE_TO_UCS4(ch, aString[i]);
    }

    hb_codepoint_t gid = shaper->GetNominalGlyph(ch);
    hb_set_add(defaultGlyphsInRun, gid);
  }

  // intersect with input glyphs, if size is not the same ==> fallback
  uint32_t origSize = hb_set_get_population(defaultGlyphsInRun);
  hb_set_intersect(defaultGlyphsInRun, inputGlyphs);
  uint32_t intersectionSize = hb_set_get_population(defaultGlyphsInRun);
  hb_set_destroy(defaultGlyphsInRun);

  return origSize == intersectionSize;
}

bool gfxFont::FeatureWillHandleChar(Script aRunScript, uint32_t aFeature,
                                    uint32_t aUnicode) {
  if (!SupportsFeature(aRunScript, aFeature)) {
    return false;
  }

  // xxx - for graphite, don't really know how to sniff lookups so bail
  if (mGraphiteShaper && gfxPlatform::GetPlatform()->UseGraphiteShaping()) {
    return true;
  }

  if (gfxHarfBuzzShaper* shaper = GetHarfBuzzShaper()) {
    // get the hbset containing input glyphs for the feature
    const hb_set_t* inputGlyphs =
        mFontEntry->InputsForOpenTypeFeature(aRunScript, aFeature);

    hb_codepoint_t gid = shaper->GetNominalGlyph(aUnicode);
    return hb_set_has(inputGlyphs, gid);
  }

  return false;
}

bool gfxFont::HasFeatureSet(uint32_t aFeature, bool& aFeatureOn) {
  aFeatureOn = false;

  if (mStyle.featureSettings.IsEmpty() &&
      GetFontEntry()->mFeatureSettings.IsEmpty()) {
    return false;
  }

  // add feature values from font
  bool featureSet = false;
  uint32_t i, count;

  nsTArray<gfxFontFeature>& fontFeatures = GetFontEntry()->mFeatureSettings;
  count = fontFeatures.Length();
  for (i = 0; i < count; i++) {
    const gfxFontFeature& feature = fontFeatures.ElementAt(i);
    if (feature.mTag == aFeature) {
      featureSet = true;
      aFeatureOn = (feature.mValue != 0);
    }
  }

  // add feature values from style rules
  nsTArray<gfxFontFeature>& styleFeatures = mStyle.featureSettings;
  count = styleFeatures.Length();
  for (i = 0; i < count; i++) {
    const gfxFontFeature& feature = styleFeatures.ElementAt(i);
    if (feature.mTag == aFeature) {
      featureSet = true;
      aFeatureOn = (feature.mValue != 0);
    }
  }

  return featureSet;
}

already_AddRefed<mozilla::gfx::ScaledFont> gfxFont::GetScaledFont(
    mozilla::gfx::DrawTarget* aDrawTarget) {
  mozilla::gfx::PaletteCache dummy;
  TextRunDrawParams params(dummy);
  return GetScaledFont(params);
}

void gfxFont::InitializeScaledFont(
    const RefPtr<mozilla::gfx::ScaledFont>& aScaledFont) {
  if (!aScaledFont) {
    return;
  }

  float angle = AngleForSyntheticOblique();
  if (angle != 0.0f) {
    aScaledFont->SetSyntheticObliqueAngle(angle);
  }
}

/**
 * A helper function in case we need to do any rounding or other
 * processing here.
 */
#define ToDeviceUnits(aAppUnits, aDevUnitsPerAppUnit) \
  (double(aAppUnits) * double(aDevUnitsPerAppUnit))

static AntialiasMode Get2DAAMode(gfxFont::AntialiasOption aAAOption) {
  switch (aAAOption) {
    case gfxFont::kAntialiasSubpixel:
      return AntialiasMode::SUBPIXEL;
    case gfxFont::kAntialiasGrayscale:
      return AntialiasMode::GRAY;
    case gfxFont::kAntialiasNone:
      return AntialiasMode::NONE;
    default:
      return AntialiasMode::DEFAULT;
  }
}

class GlyphBufferAzure {
#define AUTO_BUFFER_SIZE (2048 / sizeof(Glyph))

  typedef mozilla::image::imgDrawingParams imgDrawingParams;

 public:
  GlyphBufferAzure(const TextRunDrawParams& aRunParams,
                   const FontDrawParams& aFontParams)
      : mRunParams(aRunParams),
        mFontParams(aFontParams),
        mBuffer(*mAutoBuffer.addr()),
        mBufSize(AUTO_BUFFER_SIZE),
        mCapacity(0),
        mNumGlyphs(0) {}

  ~GlyphBufferAzure() {
    if (mNumGlyphs > 0) {
      FlushGlyphs();
    }

    if (mBuffer != *mAutoBuffer.addr()) {
      free(mBuffer);
    }
  }

  // Ensure the buffer has enough space for aGlyphCount glyphs to be added,
  // considering the supplied strike multipler aStrikeCount.
  // This MUST be called before OutputGlyph is used to actually store glyph
  // records in the buffer. It may be called repeated to add further capacity
  // in case we don't know up-front exactly what will be needed.
  void AddCapacity(uint32_t aGlyphCount, uint32_t aStrikeCount) {
    // Calculate the new capacity and ensure it will fit within the maximum
    // allowed capacity.
    static const uint64_t kMaxCapacity = 64 * 1024;
    mCapacity = uint32_t(std::min(
        kMaxCapacity,
        uint64_t(mCapacity) + uint64_t(aGlyphCount) * uint64_t(aStrikeCount)));
    // See if the required capacity fits within the already-allocated space
    if (mCapacity <= mBufSize) {
      return;
    }
    // We need to grow the buffer: determine a new size, allocate, and
    // copy the existing data over if we didn't use realloc (which would
    // do it automatically).
    mBufSize = std::max(mCapacity, mBufSize * 2);
    if (mBuffer == *mAutoBuffer.addr()) {
      // switching from autobuffer to malloc, so we need to copy
      mBuffer = reinterpret_cast<Glyph*>(moz_xmalloc(mBufSize * sizeof(Glyph)));
      std::memcpy(mBuffer, *mAutoBuffer.addr(), mNumGlyphs * sizeof(Glyph));
    } else {
      mBuffer = reinterpret_cast<Glyph*>(
          moz_xrealloc(mBuffer, mBufSize * sizeof(Glyph)));
    }
  }

  void OutputGlyph(uint32_t aGlyphID, const gfx::Point& aPt) {
    // If the buffer is full, flush to make room for the new glyph.
    if (mNumGlyphs >= mCapacity) {
      // Check that AddCapacity has been used appropriately!
      MOZ_ASSERT(mCapacity > 0 && mNumGlyphs == mCapacity);
      Flush();
    }
    Glyph* glyph = mBuffer + mNumGlyphs++;
    glyph->mIndex = aGlyphID;
    glyph->mPosition = aPt;
  }

  void Flush() {
    if (mNumGlyphs > 0) {
      FlushGlyphs();
      mNumGlyphs = 0;
    }
  }

  const TextRunDrawParams& mRunParams;
  const FontDrawParams& mFontParams;

 private:
  static DrawMode GetStrokeMode(DrawMode aMode) {
    return aMode & (DrawMode::GLYPH_STROKE | DrawMode::GLYPH_STROKE_UNDERNEATH);
  }

  // Render the buffered glyphs to the draw target.
  void FlushGlyphs() {
    gfx::GlyphBuffer buf;
    buf.mGlyphs = mBuffer;
    buf.mNumGlyphs = mNumGlyphs;

    const gfxContext::AzureState& state = mRunParams.context->CurrentState();

    // Draw stroke first if the UNDERNEATH flag is set in drawMode.
    if (mRunParams.strokeOpts &&
        GetStrokeMode(mRunParams.drawMode) ==
            (DrawMode::GLYPH_STROKE | DrawMode::GLYPH_STROKE_UNDERNEATH)) {
      DrawStroke(state, buf);
    }

    if (mRunParams.drawMode & DrawMode::GLYPH_FILL) {
      if (state.pattern || mFontParams.contextPaint) {
        Pattern* pat;

        RefPtr<gfxPattern> fillPattern;
        if (mFontParams.contextPaint) {
          imgDrawingParams imgParams;
          fillPattern = mFontParams.contextPaint->GetFillPattern(
              mRunParams.context->GetDrawTarget(),
              mRunParams.context->CurrentMatrixDouble(), imgParams);
        }
        if (!fillPattern) {
          if (state.pattern) {
            RefPtr<gfxPattern> statePattern =
                mRunParams.context->CurrentState().pattern;
            pat = statePattern->GetPattern(mRunParams.dt,
                                           state.patternTransformChanged
                                               ? &state.patternTransform
                                               : nullptr);
          } else {
            pat = nullptr;
          }
        } else {
          pat = fillPattern->GetPattern(mRunParams.dt);
        }

        if (pat) {
          mRunParams.dt->FillGlyphs(mFontParams.scaledFont, buf, *pat,
                                    mFontParams.drawOptions);
        }
      } else {
        mRunParams.dt->FillGlyphs(mFontParams.scaledFont, buf,
                                  ColorPattern(state.color),
                                  mFontParams.drawOptions);
      }
    }

    // Draw stroke if the UNDERNEATH flag is not set.
    if (mRunParams.strokeOpts &&
        GetStrokeMode(mRunParams.drawMode) == DrawMode::GLYPH_STROKE) {
      DrawStroke(state, buf);
    }

    if (mRunParams.drawMode & DrawMode::GLYPH_PATH) {
      mRunParams.context->EnsurePathBuilder();
      Matrix mat = mRunParams.dt->GetTransform();
      mFontParams.scaledFont->CopyGlyphsToBuilder(
          buf, mRunParams.context->mPathBuilder, &mat);
    }
  }

  void DrawStroke(const gfxContext::AzureState& aState,
                  gfx::GlyphBuffer& aBuffer) {
    if (mRunParams.textStrokePattern) {
      Pattern* pat = mRunParams.textStrokePattern->GetPattern(
          mRunParams.dt,
          aState.patternTransformChanged ? &aState.patternTransform : nullptr);

      if (pat) {
        FlushStroke(aBuffer, *pat);
      }
    } else {
      FlushStroke(aBuffer,
                  ColorPattern(ToDeviceColor(mRunParams.textStrokeColor)));
    }
  }

  void FlushStroke(gfx::GlyphBuffer& aBuf, const Pattern& aPattern) {
    mRunParams.dt->StrokeGlyphs(mFontParams.scaledFont, aBuf, aPattern,
                                *mRunParams.strokeOpts,
                                mFontParams.drawOptions);
  }

  // We use an "inline" buffer automatically allocated (on the stack) as part
  // of the GlyphBufferAzure object to hold the glyphs in most cases, falling
  // back to a separately-allocated heap buffer if the count of buffered
  // glyphs gets too big.
  //
  // This is basically a rudimentary AutoTArray; so why not use AutoTArray
  // itself?
  //
  // If we used an AutoTArray, we'd want to avoid using SetLength or
  // AppendElements to allocate the space we actually need, because those
  // methods would default-construct the new elements.
  //
  // Could we use SetCapacity to reserve the necessary buffer space without
  // default-constructing all the Glyph records? No, because of a failure
  // that could occur when we need to grow the buffer, which happens when we
  // encounter a DetailedGlyph in the textrun that refers to a sequence of
  // several real glyphs. At that point, we need to add some extra capacity
  // to the buffer we initially allocated based on the length of the textrun
  // range we're rendering.
  //
  // This buffer growth would work fine as long as it still fits within the
  // array's inline buffer (we just use a bit more of it), or if the buffer
  // was already heap-allocated (in which case AutoTArray will use realloc(),
  // preserving its contents). But a problem will arise when the initial
  // capacity we allocated (based on the length of the run) fits within the
  // array's inline buffer, but subsequently we need to extend the buffer
  // beyond the inline buffer size, so we reallocate to the heap. Because we
  // haven't "officially" filled the array with SetLength or AppendElements,
  // its mLength is still zero; as far as it's concerned the buffer is just
  // uninitialized space, and when it switches to use a malloc'd buffer it
  // won't copy the existing contents.

  // Allocate space for a buffer of Glyph records, without initializing them.
  AlignedStorage2<Glyph[AUTO_BUFFER_SIZE]> mAutoBuffer;

  // Pointer to the buffer we're currently using -- initially mAutoBuffer,
  // but may be changed to a malloc'd buffer, in which case that buffer must
  // be free'd on destruction.
  Glyph* mBuffer;

  uint32_t mBufSize;    // size of allocated buffer; capacity can grow to
                        // this before reallocation is needed
  uint32_t mCapacity;   // amount of buffer size reserved
  uint32_t mNumGlyphs;  // number of glyphs actually present in the buffer

#undef AUTO_BUFFER_SIZE
};

// Bug 674909. When synthetic bolding text by drawing twice, need to
// render using a pixel offset in device pixels, otherwise text
// doesn't appear bolded, it appears as if a bad text shadow exists
// when a non-identity transform exists.  Use an offset factor so that
// the second draw occurs at a constant offset in device pixels.

gfx::Float gfxFont::CalcXScale(DrawTarget* aDrawTarget) {
  // determine magnitude of a 1px x offset in device space
  Size t = aDrawTarget->GetTransform().TransformSize(Size(1.0, 0.0));
  if (t.width == 1.0 && t.height == 0.0) {
    // short-circuit the most common case to avoid sqrt() and division
    return 1.0;
  }

  gfx::Float m = sqrtf(t.width * t.width + t.height * t.height);

  NS_ASSERTION(m != 0.0, "degenerate transform while synthetic bolding");
  if (m == 0.0) {
    return 0.0;  // effectively disables offset
  }

  // scale factor so that offsets are 1px in device pixels
  return 1.0 / m;
}

// Draw a run of CharacterGlyph records from the given offset in aShapedText.
// Returns true if glyph paths were actually emitted.
template <gfxFont::FontComplexityT FC, gfxFont::SpacingT S>
bool gfxFont::DrawGlyphs(const gfxShapedText* aShapedText,
                         uint32_t aOffset,  // offset in the textrun
                         uint32_t aCount,   // length of run to draw
                         gfx::Point* aPt,
                         const gfx::Matrix* aOffsetMatrix,  // may be null
                         GlyphBufferAzure& aBuffer) {
  float& inlineCoord =
      aBuffer.mFontParams.isVerticalFont ? aPt->y.value : aPt->x.value;

  const gfxShapedText::CompressedGlyph* glyphData =
      &aShapedText->GetCharacterGlyphs()[aOffset];

  if (S == SpacingT::HasSpacing) {
    float space = aBuffer.mRunParams.spacing[0].mBefore *
                  aBuffer.mFontParams.advanceDirection;
    inlineCoord += space;
  }

  // Allocate buffer space for the run, assuming all simple glyphs.
  uint32_t capacityMult = 1 + aBuffer.mFontParams.extraStrikes;
  aBuffer.AddCapacity(aCount, capacityMult);

  bool emittedGlyphs = false;

  for (uint32_t i = 0; i < aCount; ++i, ++glyphData) {
    if (glyphData->IsSimpleGlyph()) {
      float advance =
          glyphData->GetSimpleAdvance() * aBuffer.mFontParams.advanceDirection;
      if (aBuffer.mRunParams.isRTL) {
        inlineCoord += advance;
      }
      DrawOneGlyph<FC>(glyphData->GetSimpleGlyph(), *aPt, aBuffer,
                       &emittedGlyphs);
      if (!aBuffer.mRunParams.isRTL) {
        inlineCoord += advance;
      }
    } else {
      uint32_t glyphCount = glyphData->GetGlyphCount();
      if (glyphCount > 0) {
        // Add extra buffer capacity to allow for multiple-glyph entry.
        aBuffer.AddCapacity(glyphCount - 1, capacityMult);
        const gfxShapedText::DetailedGlyph* details =
            aShapedText->GetDetailedGlyphs(aOffset + i);
        MOZ_ASSERT(details, "missing DetailedGlyph!");
        for (uint32_t j = 0; j < glyphCount; ++j, ++details) {
          float advance =
              details->mAdvance * aBuffer.mFontParams.advanceDirection;
          if (aBuffer.mRunParams.isRTL) {
            inlineCoord += advance;
          }
          if (glyphData->IsMissing()) {
            if (!DrawMissingGlyph(aBuffer.mRunParams, aBuffer.mFontParams,
                                  details, *aPt)) {
              return false;
            }
          } else {
            gfx::Point glyphPt(
                *aPt + (aOffsetMatrix
                            ? aOffsetMatrix->TransformPoint(details->mOffset)
                            : details->mOffset));
            DrawOneGlyph<FC>(details->mGlyphID, glyphPt, aBuffer,
                             &emittedGlyphs);
          }
          if (!aBuffer.mRunParams.isRTL) {
            inlineCoord += advance;
          }
        }
      }
    }

    if (S == SpacingT::HasSpacing) {
      float space = aBuffer.mRunParams.spacing[i].mAfter;
      if (i + 1 < aCount) {
        space += aBuffer.mRunParams.spacing[i + 1].mBefore;
      }
      space *= aBuffer.mFontParams.advanceDirection;
      inlineCoord += space;
    }
  }

  return emittedGlyphs;
}

// Draw an individual glyph at a specific location.
// *aPt is the glyph position in appUnits; it is converted to device
// coordinates (devPt) here.
template <gfxFont::FontComplexityT FC>
void gfxFont::DrawOneGlyph(uint32_t aGlyphID, const gfx::Point& aPt,
                           GlyphBufferAzure& aBuffer, bool* aEmittedGlyphs) {
  const TextRunDrawParams& runParams(aBuffer.mRunParams);

  gfx::Point devPt(ToDeviceUnits(aPt.x, runParams.devPerApp),
                   ToDeviceUnits(aPt.y, runParams.devPerApp));

  auto* textDrawer = runParams.textDrawer;
  if (textDrawer) {
    // If the glyph is entirely outside the clip rect, we don't need to draw it
    // at all. (We check the font extents here rather than the individual glyph
    // bounds because that's cheaper to look up, and provides a conservative
    // "worst case" for where this glyph might want to draw.)
    LayoutDeviceRect extents =
        LayoutDeviceRect::FromUnknownRect(aBuffer.mFontParams.fontExtents);
    extents.MoveBy(LayoutDevicePoint::FromUnknownPoint(devPt));
    if (!extents.Intersects(runParams.clipRect)) {
      return;
    }
  }

  if (FC == FontComplexityT::ComplexFont) {
    const FontDrawParams& fontParams(aBuffer.mFontParams);

    gfxContextMatrixAutoSaveRestore matrixRestore;

    if (fontParams.obliqueSkew != 0.0f && fontParams.isVerticalFont &&
        !textDrawer) {
      // We have to flush each glyph individually when doing
      // synthetic-oblique for vertical-upright text, because
      // the skew transform needs to be applied to a separate
      // origin for each glyph, not once for the whole run.
      aBuffer.Flush();
      matrixRestore.SetContext(runParams.context);
      gfx::Point skewPt(
          devPt.x + GetMetrics(nsFontMetrics::eVertical).emHeight / 2, devPt.y);
      gfx::Matrix mat =
          runParams.context->CurrentMatrix()
              .PreTranslate(skewPt)
              .PreMultiply(gfx::Matrix(1, fontParams.obliqueSkew, 0, 1, 0, 0))
              .PreTranslate(-skewPt);
      runParams.context->SetMatrix(mat);
    }

    if (fontParams.haveSVGGlyphs) {
      if (!runParams.paintSVGGlyphs) {
        return;
      }
      NS_WARNING_ASSERTION(
          runParams.drawMode != DrawMode::GLYPH_PATH,
          "Rendering SVG glyph despite request for glyph path");
      if (RenderSVGGlyph(runParams.context, textDrawer, devPt, aGlyphID,
                         fontParams.contextPaint, runParams.callbacks,
                         *aEmittedGlyphs)) {
        return;
      }
    }

    if (fontParams.haveColorGlyphs && !UseNativeColrFontSupport() &&
        RenderColorGlyph(runParams.dt, runParams.context, textDrawer,
                         fontParams, devPt, aGlyphID)) {
      return;
    }

    aBuffer.OutputGlyph(aGlyphID, devPt);

    // Synthetic bolding (if required) by multi-striking.
    for (int32_t i = 0; i < fontParams.extraStrikes; ++i) {
      if (fontParams.isVerticalFont) {
        devPt.y += fontParams.synBoldOnePixelOffset;
      } else {
        devPt.x += fontParams.synBoldOnePixelOffset;
      }
      aBuffer.OutputGlyph(aGlyphID, devPt);
    }

    if (fontParams.obliqueSkew != 0.0f && fontParams.isVerticalFont &&
        !textDrawer) {
      aBuffer.Flush();
    }
  } else {
    aBuffer.OutputGlyph(aGlyphID, devPt);
  }

  *aEmittedGlyphs = true;
}

bool gfxFont::DrawMissingGlyph(const TextRunDrawParams& aRunParams,
                               const FontDrawParams& aFontParams,
                               const gfxShapedText::DetailedGlyph* aDetails,
                               const gfx::Point& aPt) {
  // Default-ignorable chars will have zero advance width;
  // we don't have to draw the hexbox for them.
  float advance = aDetails->mAdvance;
  if (aRunParams.drawMode != DrawMode::GLYPH_PATH && advance > 0) {
    auto* textDrawer = aRunParams.textDrawer;
    const Matrix* matPtr = nullptr;
    Matrix mat;
    if (textDrawer) {
      // Generate an orientation matrix for the current writing mode
      wr::FontInstanceFlags flags = textDrawer->GetWRGlyphFlags();
      if (flags & wr::FontInstanceFlags::TRANSPOSE) {
        std::swap(mat._11, mat._12);
        std::swap(mat._21, mat._22);
      }
      mat.PostScale(flags & wr::FontInstanceFlags::FLIP_X ? -1.0f : 1.0f,
                    flags & wr::FontInstanceFlags::FLIP_Y ? -1.0f : 1.0f);
      matPtr = &mat;
    }

    Point pt(Float(ToDeviceUnits(aPt.x, aRunParams.devPerApp)),
             Float(ToDeviceUnits(aPt.y, aRunParams.devPerApp)));
    Float advanceDevUnits = Float(ToDeviceUnits(advance, aRunParams.devPerApp));
    Float height = GetMetrics(nsFontMetrics::eHorizontal).maxAscent;
    // Horizontally center if drawing vertically upright with no sideways
    // transform.
    Rect glyphRect =
        aFontParams.isVerticalFont && !mat.HasNonAxisAlignedTransform()
            ? Rect(pt.x - height / 2, pt.y, height, advanceDevUnits)
            : Rect(pt.x, pt.y - height, advanceDevUnits, height);

    // If there's a fake-italic skew in effect as part
    // of the drawTarget's transform, we need to undo
    // this before drawing the hexbox. (Bug 983985)
    gfxContextMatrixAutoSaveRestore matrixRestore;
    if (aFontParams.obliqueSkew != 0.0f && !aFontParams.isVerticalFont &&
        !textDrawer) {
      matrixRestore.SetContext(aRunParams.context);
      gfx::Matrix mat =
          aRunParams.context->CurrentMatrix()
              .PreTranslate(pt)
              .PreMultiply(gfx::Matrix(1, 0, aFontParams.obliqueSkew, 1, 0, 0))
              .PreTranslate(-pt);
      aRunParams.context->SetMatrix(mat);
    }

    gfxFontMissingGlyphs::DrawMissingGlyph(
        aDetails->mGlyphID, glyphRect, *aRunParams.dt,
        PatternFromState(aRunParams.context), matPtr);
  }
  return true;
}

// This method is mostly parallel to DrawGlyphs.
void gfxFont::DrawEmphasisMarks(const gfxTextRun* aShapedText, gfx::Point* aPt,
                                uint32_t aOffset, uint32_t aCount,
                                const EmphasisMarkDrawParams& aParams) {
  float& inlineCoord = aParams.isVertical ? aPt->y.value : aPt->x.value;
  gfxTextRun::Range markRange(aParams.mark);
  gfxTextRun::DrawParams params(aParams.context, aParams.paletteCache);

  float clusterStart = -std::numeric_limits<float>::infinity();
  bool shouldDrawEmphasisMark = false;
  for (uint32_t i = 0, idx = aOffset; i < aCount; ++i, ++idx) {
    if (aParams.spacing) {
      inlineCoord += aParams.direction * aParams.spacing[i].mBefore;
    }
    if (aShapedText->IsClusterStart(idx) ||
        clusterStart == -std::numeric_limits<float>::infinity()) {
      clusterStart = inlineCoord;
    }
    if (aShapedText->CharMayHaveEmphasisMark(idx)) {
      shouldDrawEmphasisMark = true;
    }
    inlineCoord += aParams.direction * aShapedText->GetAdvanceForGlyph(idx);
    if (shouldDrawEmphasisMark &&
        (i + 1 == aCount || aShapedText->IsClusterStart(idx + 1))) {
      float clusterAdvance = inlineCoord - clusterStart;
      // Move the coord backward to get the needed start point.
      float delta = (clusterAdvance + aParams.advance) / 2;
      inlineCoord -= delta;
      aParams.mark->Draw(markRange, *aPt, params);
      inlineCoord += delta;
      shouldDrawEmphasisMark = false;
    }
    if (aParams.spacing) {
      inlineCoord += aParams.direction * aParams.spacing[i].mAfter;
    }
  }
}

void gfxFont::Draw(const gfxTextRun* aTextRun, uint32_t aStart, uint32_t aEnd,
                   gfx::Point* aPt, TextRunDrawParams& aRunParams,
                   gfx::ShapedTextFlags aOrientation) {
  NS_ASSERTION(aRunParams.drawMode == DrawMode::GLYPH_PATH ||
                   !(int(aRunParams.drawMode) & int(DrawMode::GLYPH_PATH)),
               "GLYPH_PATH cannot be used with GLYPH_FILL, GLYPH_STROKE or "
               "GLYPH_STROKE_UNDERNEATH");

  if (aStart >= aEnd) {
    return;
  }

  FontDrawParams fontParams;

  if (aRunParams.drawOpts) {
    fontParams.drawOptions = *aRunParams.drawOpts;
  }

  fontParams.scaledFont = GetScaledFont(aRunParams);
  if (!fontParams.scaledFont) {
    return;
  }
  auto* textDrawer = aRunParams.textDrawer;

  fontParams.obliqueSkew = SkewForSyntheticOblique();
  fontParams.haveSVGGlyphs = GetFontEntry()->TryGetSVGData(this);
  fontParams.haveColorGlyphs = GetFontEntry()->TryGetColorGlyphs();
  fontParams.hasTextShadow = aRunParams.hasTextShadow;
  fontParams.contextPaint = aRunParams.runContextPaint;

  if (fontParams.haveColorGlyphs && !UseNativeColrFontSupport()) {
    DeviceColor ctxColor;
    fontParams.currentColor = aRunParams.context->GetDeviceColor(ctxColor)
                                  ? sRGBColor::FromABGR(ctxColor.ToABGR())
                                  : sRGBColor::OpaqueBlack();
    fontParams.palette = aRunParams.paletteCache.GetPaletteFor(
        GetFontEntry(), aRunParams.fontPalette);
  }

  if (textDrawer) {
    fontParams.isVerticalFont = aRunParams.isVerticalRun;
  } else {
    fontParams.isVerticalFont =
        aOrientation == gfx::ShapedTextFlags::TEXT_ORIENT_VERTICAL_UPRIGHT;
  }

  gfxContextMatrixAutoSaveRestore matrixRestore;
  layout::TextDrawTarget::AutoRestoreWRGlyphFlags glyphFlagsRestore;

  // Save the current baseline offset for restoring later, in case it is
  // modified.
  float& baseline = fontParams.isVerticalFont ? aPt->x.value : aPt->y.value;
  float origBaseline = baseline;

  // The point may be advanced in local-space, while the resulting point on
  // return must be advanced in transformed space. So save the original point so
  // we can properly transform the advance later.
  gfx::Point origPt = *aPt;
  const gfx::Matrix* offsetMatrix = nullptr;

  // Default to advancing along the +X direction (-X if RTL).
  fontParams.advanceDirection = aRunParams.isRTL ? -1.0f : 1.0f;
  // Default to offsetting baseline downward along the +Y direction.
  float baselineDir = 1.0f;
  // The direction of sideways rotation, if applicable.
  // -1 for rotating left/counter-clockwise
  // 1 for rotating right/clockwise
  // 0 for no rotation
  float sidewaysDir =
      (aOrientation == gfx::ShapedTextFlags::TEXT_ORIENT_VERTICAL_SIDEWAYS_LEFT
           ? -1.0f
           : (aOrientation ==
                      gfx::ShapedTextFlags::TEXT_ORIENT_VERTICAL_SIDEWAYS_RIGHT
                  ? 1.0f
                  : 0.0f));
  // If we're rendering a sideways run, we need to push a rotation transform to
  // the context.
  if (sidewaysDir != 0.0f) {
    if (textDrawer) {
      // For WebRender, we can't use a DrawTarget transform and must instead use
      // flags that locally transform the glyph, without affecting the glyph
      // origin. The glyph origins must thus be offset in the transformed
      // directions (instead of local-space directions). Modify the advance and
      // baseline directions to account for the indicated transform.

      // The default text orientation is down being +Y and right being +X.
      // Rotating 90 degrees left/CCW makes down be +X and right be -Y.
      // Rotating 90 degrees right/CW makes down be -X and right be +Y.
      // Thus the advance direction (moving right) is just sidewaysDir,
      // i.e. negative along Y axis if rotated left and positive if
      // rotated right.
      fontParams.advanceDirection *= sidewaysDir;
      // The baseline direction (moving down) is negated relative to the
      // advance direction for sideways transforms.
      baselineDir *= -sidewaysDir;

      glyphFlagsRestore.Save(textDrawer);
      // Set the transform flags accordingly. Both sideways rotations transpose
      // X and Y, while left rotation flips the resulting Y axis, and right
      // rotation flips the resulting X axis.
      textDrawer->SetWRGlyphFlags(
          textDrawer->GetWRGlyphFlags() | wr::FontInstanceFlags::TRANSPOSE |
          (aOrientation ==
                   gfx::ShapedTextFlags::TEXT_ORIENT_VERTICAL_SIDEWAYS_LEFT
               ? wr::FontInstanceFlags::FLIP_Y
               : wr::FontInstanceFlags::FLIP_X));
      // We also need to set up a transform for the glyph offset vector that
      // may be present in DetailedGlyph records.
      static const gfx::Matrix kSidewaysLeft = {0, -1, 1, 0, 0, 0};
      static const gfx::Matrix kSidewaysRight = {0, 1, -1, 0, 0, 0};
      offsetMatrix =
          (aOrientation == ShapedTextFlags::TEXT_ORIENT_VERTICAL_SIDEWAYS_LEFT)
              ? &kSidewaysLeft
              : &kSidewaysRight;
    } else {
      // For non-WebRender targets, just push a rotation transform.
      matrixRestore.SetContext(aRunParams.context);
      gfxPoint p(aPt->x * aRunParams.devPerApp, aPt->y * aRunParams.devPerApp);
      // Get a matrix we can use to draw the (horizontally-shaped) textrun
      // with 90-degree CW rotation.
      const gfxFloat rotation = sidewaysDir * M_PI / 2.0f;
      gfxMatrix mat = aRunParams.context->CurrentMatrixDouble()
                          .PreTranslate(p)
                          .  // translate origin for rotation
                      PreRotate(rotation)
                          .  // turn 90deg CCW (sideways-left) or CW (*-right)
                      PreTranslate(-p);  // undo the translation

      aRunParams.context->SetMatrixDouble(mat);
    }

    // If we're drawing rotated horizontal text for an element styled
    // text-orientation:mixed, the dominant baseline will be vertical-
    // centered. So in this case, we need to adjust the position so that
    // the rotated horizontal text (which uses an alphabetic baseline) will
    // look OK when juxtaposed with upright glyphs (rendered on a centered
    // vertical baseline). The adjustment here is somewhat ad hoc; we
    // should eventually look for baseline tables[1] in the fonts and use
    // those if available.
    // [1] See http://www.microsoft.com/typography/otspec/base.htm
    if (aTextRun->UseCenterBaseline()) {
      const Metrics& metrics = GetMetrics(nsFontMetrics::eHorizontal);
      float baseAdj = (metrics.emAscent - metrics.emDescent) / 2;
      baseline += baseAdj * aTextRun->GetAppUnitsPerDevUnit() * baselineDir;
    }
  } else if (textDrawer &&
             aOrientation == ShapedTextFlags::TEXT_ORIENT_VERTICAL_UPRIGHT) {
    glyphFlagsRestore.Save(textDrawer);
    textDrawer->SetWRGlyphFlags(textDrawer->GetWRGlyphFlags() |
                                wr::FontInstanceFlags::VERTICAL);
  }

  if (fontParams.obliqueSkew != 0.0f && !fontParams.isVerticalFont &&
      !textDrawer) {
    // Adjust matrix for synthetic-oblique, except if we're doing vertical-
    // upright text, in which case this will be handled for each glyph
    // individually in DrawOneGlyph.
    if (!matrixRestore.HasMatrix()) {
      matrixRestore.SetContext(aRunParams.context);
    }
    gfx::Point p(aPt->x * aRunParams.devPerApp, aPt->y * aRunParams.devPerApp);
    gfx::Matrix mat =
        aRunParams.context->CurrentMatrix()
            .PreTranslate(p)
            .PreMultiply(gfx::Matrix(1, 0, -fontParams.obliqueSkew, 1, 0, 0))
            .PreTranslate(-p);
    aRunParams.context->SetMatrix(mat);
  }

  RefPtr<SVGContextPaint> contextPaint;
  if (fontParams.haveSVGGlyphs && !fontParams.contextPaint) {
    // If no pattern is specified for fill, use the current pattern
    NS_ASSERTION((int(aRunParams.drawMode) & int(DrawMode::GLYPH_STROKE)) == 0,
                 "no pattern supplied for stroking text");
    RefPtr<gfxPattern> fillPattern = aRunParams.context->GetPattern();
    contextPaint = new SimpleTextContextPaint(
        fillPattern, nullptr, aRunParams.context->CurrentMatrixDouble());
    fontParams.contextPaint = contextPaint.get();
  }

  // Synthetic-bold strikes are each offset one device pixel in run direction
  // (these values are only needed if ApplySyntheticBold() is true).
  // If drawing via webrender, it will do multistrike internally so we don't
  // need to handle it here.
  bool doMultistrikeBold = ApplySyntheticBold() && !textDrawer;
  if (doMultistrikeBold) {
    // For screen display, we want to try and repeat strikes with an offset of
    // one device pixel, accounting for zoom or other transforms that may be
    // in effect, so compute x-axis scale factor from the drawtarget.
    // However, when generating PDF output the drawtarget's transform does not
    // really bear any relation to "device pixels", and may result in an
    // excessively large offset relative to the font size (bug 1823888), so
    // we limit it based on the used font size to avoid this.
    // The constant 48.0 reflects the threshold where the calculation in
    // gfxFont::GetSyntheticBoldOffset() switches to a simple origin-based
    // slope, though the exact value is somewhat arbitrary; it's selected to
    // allow a visible amount of boldness while preventing the offset from
    // becoming "large" in relation to the glyphs.
    Float xscale =
        std::min<Float>(GetAdjustedSize() / 48.0,
                        CalcXScale(aRunParams.context->GetDrawTarget()));
    fontParams.synBoldOnePixelOffset = aRunParams.direction * xscale;
    if (xscale != 0.0) {
      static const int32_t kMaxExtraStrikes = 128;
      gfxFloat extraStrikes = GetSyntheticBoldOffset() / xscale;
      if (extraStrikes > kMaxExtraStrikes) {
        // if too many strikes are required, limit them and increase the step
        // size to compensate
        fontParams.extraStrikes = kMaxExtraStrikes;
        fontParams.synBoldOnePixelOffset = aRunParams.direction *
                                           GetSyntheticBoldOffset() /
                                           fontParams.extraStrikes;
      } else {
        // use as many strikes as needed for the increased advance
        fontParams.extraStrikes = NS_lroundf(std::max(1.0, extraStrikes));
      }
    } else {
      // Degenerate transform?!
      fontParams.extraStrikes = 0;
    }
  } else {
    fontParams.synBoldOnePixelOffset = 0;
    fontParams.extraStrikes = 0;
  }

  // Figure out the maximum extents for the font, accounting for synthetic
  // oblique and bold.
  if (mFUnitsConvFactor > 0.0) {
    fontParams.fontExtents = GetFontEntry()->GetFontExtents(mFUnitsConvFactor);
  } else {
    // Was it not an sfnt? Maybe on Linux... use arbitrary huge extents, so we
    // don't inadvertently clip stuff. A bit less efficient than true extents,
    // but this should be extremely rare.
    auto size = GetAdjustedSize();
    fontParams.fontExtents = Rect(-2 * size, -2 * size, 5 * size, 5 * size);
  }
  if (fontParams.obliqueSkew != 0.0f) {
    gfx::Point p(fontParams.fontExtents.x, fontParams.fontExtents.y);
    gfx::Matrix skew(1, 0, fontParams.obliqueSkew, 1, 0, 0);
    fontParams.fontExtents = skew.TransformBounds(fontParams.fontExtents);
  }
  if (fontParams.extraStrikes) {
    if (fontParams.isVerticalFont) {
      fontParams.fontExtents.height +=
          float(fontParams.extraStrikes) * fontParams.synBoldOnePixelOffset;
    } else {
      fontParams.fontExtents.width +=
          float(fontParams.extraStrikes) * fontParams.synBoldOnePixelOffset;
    }
  }

  bool oldSubpixelAA = aRunParams.dt->GetPermitSubpixelAA();
  if (!AllowSubpixelAA()) {
    aRunParams.dt->SetPermitSubpixelAA(false);
  }

  Matrix mat;
  Matrix oldMat = aRunParams.dt->GetTransform();

  fontParams.drawOptions.mAntialiasMode = Get2DAAMode(mAntialiasOption);

  if (mStyle.baselineOffset != 0.0) {
    baseline +=
        mStyle.baselineOffset * aTextRun->GetAppUnitsPerDevUnit() * baselineDir;
  }

  bool emittedGlyphs;
  {
    // Select appropriate version of the templated DrawGlyphs method
    // to output glyphs to the buffer, depending on complexity needed
    // for the type of font, and whether added inter-glyph spacing
    // is specified.
    GlyphBufferAzure buffer(aRunParams, fontParams);
    if (fontParams.haveSVGGlyphs || fontParams.haveColorGlyphs ||
        fontParams.extraStrikes ||
        (fontParams.obliqueSkew != 0.0f && fontParams.isVerticalFont &&
         !textDrawer)) {
      if (aRunParams.spacing) {
        emittedGlyphs =
            DrawGlyphs<FontComplexityT::ComplexFont, SpacingT::HasSpacing>(
                aTextRun, aStart, aEnd - aStart, aPt, offsetMatrix, buffer);
      } else {
        emittedGlyphs =
            DrawGlyphs<FontComplexityT::ComplexFont, SpacingT::NoSpacing>(
                aTextRun, aStart, aEnd - aStart, aPt, offsetMatrix, buffer);
      }
    } else {
      if (aRunParams.spacing) {
        emittedGlyphs =
            DrawGlyphs<FontComplexityT::SimpleFont, SpacingT::HasSpacing>(
                aTextRun, aStart, aEnd - aStart, aPt, offsetMatrix, buffer);
      } else {
        emittedGlyphs =
            DrawGlyphs<FontComplexityT::SimpleFont, SpacingT::NoSpacing>(
                aTextRun, aStart, aEnd - aStart, aPt, offsetMatrix, buffer);
      }
    }
  }

  baseline = origBaseline;

  if (aRunParams.callbacks && emittedGlyphs) {
    aRunParams.callbacks->NotifyGlyphPathEmitted();
  }

  aRunParams.dt->SetTransform(oldMat);
  aRunParams.dt->SetPermitSubpixelAA(oldSubpixelAA);

  if (sidewaysDir != 0.0f && !textDrawer) {
    // Adjust updated aPt to account for the transform we were using.
    // The advance happened horizontally in local-space, but the transformed
    // sideways advance is actually vertical, with sign depending on the
    // direction of rotation.
    float advance = aPt->x - origPt.x;
    *aPt = gfx::Point(origPt.x, origPt.y + advance * sidewaysDir);
  }
}

bool gfxFont::RenderSVGGlyph(gfxContext* aContext,
                             layout::TextDrawTarget* aTextDrawer,
                             gfx::Point aPoint, uint32_t aGlyphId,
                             SVGContextPaint* aContextPaint) const {
  if (!GetFontEntry()->HasSVGGlyph(aGlyphId)) {
    return false;
  }

  if (aTextDrawer) {
    // WebRender doesn't support SVG Glyphs.
    // (pretend to succeed, output doesn't matter, we will emit a blob)
    aTextDrawer->FoundUnsupportedFeature();
    return true;
  }

  const gfxFloat devUnitsPerSVGUnit =
      GetAdjustedSize() / GetFontEntry()->UnitsPerEm();
  gfxContextMatrixAutoSaveRestore matrixRestore(aContext);

  aContext->SetMatrix(aContext->CurrentMatrix()
                          .PreTranslate(aPoint.x, aPoint.y)
                          .PreScale(devUnitsPerSVGUnit, devUnitsPerSVGUnit));

  aContextPaint->InitStrokeGeometry(aContext, devUnitsPerSVGUnit);

  GetFontEntry()->RenderSVGGlyph(aContext, aGlyphId, aContextPaint);
  aContext->NewPath();
  return true;
}

bool gfxFont::RenderSVGGlyph(gfxContext* aContext,
                             layout::TextDrawTarget* aTextDrawer,
                             gfx::Point aPoint, uint32_t aGlyphId,
                             SVGContextPaint* aContextPaint,
                             gfxTextRunDrawCallbacks* aCallbacks,
                             bool& aEmittedGlyphs) const {
  if (aCallbacks && aEmittedGlyphs) {
    aCallbacks->NotifyGlyphPathEmitted();
    aEmittedGlyphs = false;
  }
  return RenderSVGGlyph(aContext, aTextDrawer, aPoint, aGlyphId, aContextPaint);
}

bool gfxFont::RenderColorGlyph(DrawTarget* aDrawTarget, gfxContext* aContext,
                               layout::TextDrawTarget* aTextDrawer,
                               const FontDrawParams& aFontParams,
                               const Point& aPoint, uint32_t aGlyphId) {
  if (aTextDrawer && aFontParams.hasTextShadow) {
    aTextDrawer->FoundUnsupportedFeature();
    return true;
  }

  auto* colr = GetFontEntry()->GetCOLR();
  const auto* paintGraph = COLRFonts::GetGlyphPaintGraph(colr, aGlyphId);
  const gfxHarfBuzzShaper* hbShaper = nullptr;
  if (paintGraph) {
    // We need the hbShaper to get color glyph bounds, so check that it's
    // usable.
    hbShaper = GetHarfBuzzShaper();
    if (!hbShaper && !hbShaper->IsInitialized()) {
      return false;
    }
    if (aTextDrawer) {
      aTextDrawer->FoundUnsupportedFeature();
      return true;
    }
  }
  const auto* layers =
      paintGraph ? nullptr : COLRFonts::GetGlyphLayers(colr, aGlyphId);

  if (!paintGraph && !layers) {
    return false;
  }

  // For reasonable font sizes, use a cache of rasterized glyphs.
  bool useCache = GetAdjustedSize() <= 256.0;

  // If the composition op is not OVER, rasterize to a temporary surface
  // and then composite to the destination, even if we're not caching.
  // But we can't do this if the target is a TextDrawTarget, as it doesn't
  // support DrawSurface.
  RefPtr<SourceSurface> snapshot;
  if ((useCache ||
       aFontParams.drawOptions.mCompositionOp != CompositionOp::OP_OVER) &&
      aDrawTarget->GetBackendType() != BackendType::WEBRENDER_TEXT) {
    AutoWriteLock lock(mLock);
    if (!mColorGlyphCache && useCache) {
      mColorGlyphCache = MakeUnique<ColorGlyphCache>();
    }

    Rect bounds;
    if (paintGraph) {
      bounds = COLRFonts::GetColorGlyphBounds(
          colr, hbShaper->GetHBFont(), aGlyphId, aDrawTarget,
          aFontParams.scaledFont, mFUnitsConvFactor);
    } else {
      bounds = GetFontEntry()->GetFontExtents(mFUnitsConvFactor);
    }
    bounds.RoundOut();

    // Tell the cache what colors we're using; if they have changed, it
    // will discard any currently-cached entries.
    HashMap<uint32_t, RefPtr<SourceSurface>>::AddPtr cached;
    if (useCache) {
      mColorGlyphCache->SetColors(aFontParams.currentColor,
                                  aFontParams.palette);
      cached = mColorGlyphCache->mCache.lookupForAdd(aGlyphId);
      if (cached) {
        snapshot = cached->value();
      }
    }

    const int kScale = 2;
    if (!snapshot) {
      // Create a temporary DrawTarget and render the glyph to it.
      IntSize size(int(bounds.width), int(bounds.height));
      SurfaceFormat format = SurfaceFormat::B8G8R8A8;
      RefPtr target =
          Factory::CreateDrawTarget(BackendType::SKIA, size * kScale, format);
      if (target) {
        // Use OP_OVER and opaque alpha to create the glyph snapshot.
        Matrix m;
        m.PreScale(kScale, kScale);
        target->SetTransform(m);
        DrawOptions drawOptions(aFontParams.drawOptions);
        drawOptions.mCompositionOp = CompositionOp::OP_OVER;
        drawOptions.mAlpha = 1.0f;
        bool ok = false;
        if (paintGraph) {
          ok = COLRFonts::PaintGlyphGraph(
              colr, hbShaper->GetHBFont(), paintGraph, target, nullptr,
              aFontParams.scaledFont, drawOptions, -bounds.TopLeft(),
              aFontParams.currentColor, aFontParams.palette->Colors(), aGlyphId,
              mFUnitsConvFactor);
        } else {
          auto face(GetFontEntry()->GetHBFace());
          ok = COLRFonts::PaintGlyphLayers(
              colr, face, layers, target, nullptr, aFontParams.scaledFont,
              drawOptions, -bounds.TopLeft(), aFontParams.currentColor,
              aFontParams.palette->Colors());
        }
        if (ok) {
          snapshot = target->Snapshot();
          if (useCache) {
            // Save a snapshot of the rendering in the cache.
            // (We ignore potential failure here, and just paint the snapshot
            // without caching it.)
            Unused << mColorGlyphCache->mCache.add(cached, aGlyphId, snapshot);
          }
        }
      }
    }
    if (snapshot) {
      // Paint the snapshot using the appropriate composition op.
      Point snappedPoint = Point(roundf(aPoint.x), roundf(aPoint.y));
      aDrawTarget->DrawSurface(
          snapshot, Rect(snappedPoint + bounds.TopLeft(), bounds.Size()),
          Rect(Point(), bounds.Size() * kScale), DrawSurfaceOptions(),
          aFontParams.drawOptions);
      return true;
    }
  }

  // If we didn't paint from a cached or temporary snapshot, just render
  // directly to the destination drawTarget.
  if (paintGraph) {
    return COLRFonts::PaintGlyphGraph(
        colr, hbShaper->GetHBFont(), paintGraph, aDrawTarget, aTextDrawer,
        aFontParams.scaledFont, aFontParams.drawOptions, aPoint,
        aFontParams.currentColor, aFontParams.palette->Colors(), aGlyphId,
        mFUnitsConvFactor);
  }

  if (layers) {
    auto face(GetFontEntry()->GetHBFace());
    return COLRFonts::PaintGlyphLayers(
        colr, face, layers, aDrawTarget, aTextDrawer, aFontParams.scaledFont,
        aFontParams.drawOptions, aPoint, aFontParams.currentColor,
        aFontParams.palette->Colors());
  }

  return false;
}

void gfxFont::ColorGlyphCache::SetColors(sRGBColor aCurrentColor,
                                         FontPalette* aPalette) {
  if (aCurrentColor != mCurrentColor || aPalette != mPalette) {
    mCache.clear();
    mCurrentColor = aCurrentColor;
    mPalette = aPalette;
  }
}

bool gfxFont::HasColorGlyphFor(uint32_t aCh, uint32_t aNextCh) {
  // Bitmap fonts are assumed to provide "color" glyphs for all supported chars.
  gfxFontEntry* fe = GetFontEntry();
  if (fe->HasColorBitmapTable()) {
    return true;
  }
  // Use harfbuzz shaper to look up the default glyph ID for the character.
  auto* shaper = GetHarfBuzzShaper();
  if (!shaper) {
    return false;
  }
  uint32_t gid = 0;
  if (gfxFontUtils::IsVarSelector(aNextCh)) {
    gid = shaper->GetVariationGlyph(aCh, aNextCh);
  }
  if (!gid) {
    gid = shaper->GetNominalGlyph(aCh);
  }
  if (!gid) {
    return false;
  }

  // See https://bugzilla.mozilla.org/show_bug.cgi?id=1801521:
  // Emoji special-case: flag sequences NOT based on Regional Indicator pairs
  // use the BLACK FLAG character plus a series of plane-14 TAG LETTERs, e.g.
  //   England = <black-flag, tag-G, tag-B, tag-E, tag-N, tag-G, tag-cancel>
  // Here, we don't check for support of the entire sequence (too much
  // expensive lookahead), but we check that the font at least supports the
  // first of the tag letter codes, because if it doesn't, we're at risk of
  // just getting an undifferentiated black flag glyph.
  if (gfxFontUtils::IsEmojiFlagAndTag(aCh, aNextCh)) {
    if (!shaper->GetNominalGlyph(aNextCh)) {
      return false;
    }
  }

  // Check if there is a COLR/CPAL or SVG glyph for this ID.
  if (fe->TryGetColorGlyphs() &&
      (COLRFonts::GetGlyphPaintGraph(fe->GetCOLR(), gid) ||
       COLRFonts::GetGlyphLayers(fe->GetCOLR(), gid))) {
    return true;
  }
  if (fe->TryGetSVGData(this) && fe->HasSVGGlyph(gid)) {
    return true;
  }
  return false;
}

static void UnionRange(gfxFloat aX, gfxFloat* aDestMin, gfxFloat* aDestMax) {
  *aDestMin = std::min(*aDestMin, aX);
  *aDestMax = std::max(*aDestMax, aX);
}

// We get precise glyph extents if the textrun creator requested them, or
// if the font is a user font --- in which case the author may be relying
// on overflowing glyphs.
static bool NeedsGlyphExtents(gfxFont* aFont, const gfxTextRun* aTextRun) {
  return (aTextRun->GetFlags() &
          gfx::ShapedTextFlags::TEXT_NEED_BOUNDING_BOX) ||
         aFont->GetFontEntry()->IsUserFont();
}

bool gfxFont::IsSpaceGlyphInvisible(DrawTarget* aRefDrawTarget,
                                    const gfxTextRun* aTextRun) {
  gfxFontEntry::LazyFlag flag = mFontEntry->mSpaceGlyphIsInvisible;
  if (flag == gfxFontEntry::LazyFlag::Uninitialized &&
      GetAdjustedSize() >= 1.0) {
    gfxGlyphExtents* extents =
        GetOrCreateGlyphExtents(aTextRun->GetAppUnitsPerDevUnit());
    gfxRect glyphExtents;
    flag = extents->GetTightGlyphExtentsAppUnits(
               this, aRefDrawTarget, GetSpaceGlyph(), &glyphExtents) &&
                   glyphExtents.IsEmpty()
               ? gfxFontEntry::LazyFlag::Yes
               : gfxFontEntry::LazyFlag::No;
    mFontEntry->mSpaceGlyphIsInvisible = flag;
  }
  return flag == gfxFontEntry::LazyFlag::Yes;
}

bool gfxFont::MeasureGlyphs(const gfxTextRun* aTextRun, uint32_t aStart,
                            uint32_t aEnd, BoundingBoxType aBoundingBoxType,
                            DrawTarget* aRefDrawTarget, Spacing* aSpacing,
                            gfxGlyphExtents* aExtents, bool aIsRTL,
                            bool aNeedsGlyphExtents, RunMetrics& aMetrics,
                            gfxFloat* aAdvanceMin, gfxFloat* aAdvanceMax) {
  const gfxTextRun::CompressedGlyph* charGlyphs =
      aTextRun->GetCharacterGlyphs();
  double x = 0;
  if (aSpacing) {
    x += aSpacing[0].mBefore;
  }
  uint32_t spaceGlyph = GetSpaceGlyph();
  bool allGlyphsInvisible = true;

  AutoReadLock lock(aExtents->mLock);

  for (uint32_t i = aStart; i < aEnd; ++i) {
    const gfxTextRun::CompressedGlyph* glyphData = &charGlyphs[i];
    if (glyphData->IsSimpleGlyph()) {
      double advance = glyphData->GetSimpleAdvance();
      uint32_t glyphIndex = glyphData->GetSimpleGlyph();
      if (allGlyphsInvisible) {
        if (glyphIndex != spaceGlyph) {
          allGlyphsInvisible = false;
        } else {
          gfxFontEntry::LazyFlag flag = mFontEntry->mSpaceGlyphIsInvisible;
          if (flag == gfxFontEntry::LazyFlag::Uninitialized &&
              GetAdjustedSize() >= 1.0) {
            gfxRect glyphExtents;
            flag = aExtents->GetTightGlyphExtentsAppUnitsLocked(
                       this, aRefDrawTarget, spaceGlyph, &glyphExtents) &&
                           glyphExtents.IsEmpty()
                       ? gfxFontEntry::LazyFlag::Yes
                       : gfxFontEntry::LazyFlag::No;
            mFontEntry->mSpaceGlyphIsInvisible = flag;
          }
          if (flag == gfxFontEntry::LazyFlag::No) {
            allGlyphsInvisible = false;
          }
        }
      }
      // Only get the real glyph horizontal extent if we were asked
      // for the tight bounding box or we're in quality mode
      if (aBoundingBoxType != LOOSE_INK_EXTENTS || aNeedsGlyphExtents) {
        uint16_t extentsWidth =
            aExtents->GetContainedGlyphWidthAppUnitsLocked(glyphIndex);
        if (extentsWidth != gfxGlyphExtents::INVALID_WIDTH &&
            aBoundingBoxType == LOOSE_INK_EXTENTS) {
          UnionRange(x, aAdvanceMin, aAdvanceMax);
          UnionRange(x + extentsWidth, aAdvanceMin, aAdvanceMax);
        } else {
          gfxRect glyphRect;
          if (!aExtents->GetTightGlyphExtentsAppUnitsLocked(
                  this, aRefDrawTarget, glyphIndex, &glyphRect)) {
            glyphRect = gfxRect(0, aMetrics.mBoundingBox.Y(), advance,
                                aMetrics.mBoundingBox.Height());
          }
          if (aIsRTL) {
            // In effect, swap left and right sidebearings of the glyph, for
            // proper accumulation of potentially-overlapping glyph rects.
            glyphRect.MoveToX(advance - glyphRect.XMost());
          }
          glyphRect.MoveByX(x);
          aMetrics.mBoundingBox = aMetrics.mBoundingBox.Union(glyphRect);
        }
      }
      x += advance;
    } else {
      allGlyphsInvisible = false;
      uint32_t glyphCount = glyphData->GetGlyphCount();
      if (glyphCount > 0) {
        const gfxTextRun::DetailedGlyph* details =
            aTextRun->GetDetailedGlyphs(i);
        NS_ASSERTION(details != nullptr,
                     "detailedGlyph record should not be missing!");
        uint32_t j;
        for (j = 0; j < glyphCount; ++j, ++details) {
          uint32_t glyphIndex = details->mGlyphID;
          double advance = details->mAdvance;
          gfxRect glyphRect;
          if (glyphData->IsMissing() ||
              !aExtents->GetTightGlyphExtentsAppUnitsLocked(
                  this, aRefDrawTarget, glyphIndex, &glyphRect)) {
            // We might have failed to get glyph extents due to
            // OOM or something
            glyphRect = gfxRect(0, -aMetrics.mAscent, advance,
                                aMetrics.mAscent + aMetrics.mDescent);
          }
          if (aIsRTL) {
            // Swap left/right sidebearings of the glyph, because we're doing
            // mirrored measurement.
            glyphRect.MoveToX(advance - glyphRect.XMost());
            // Move to current x position, mirroring any x-offset amount.
            glyphRect.MoveByX(x - details->mOffset.x);
          } else {
            glyphRect.MoveByX(x + details->mOffset.x);
          }
          glyphRect.MoveByY(details->mOffset.y);
          aMetrics.mBoundingBox = aMetrics.mBoundingBox.Union(glyphRect);
          x += advance;
        }
      }
    }
    if (aSpacing) {
      double space = aSpacing[i - aStart].mAfter;
      if (i + 1 < aEnd) {
        space += aSpacing[i + 1 - aStart].mBefore;
      }
      x += space;
    }
  }

  aMetrics.mAdvanceWidth = x;
  return allGlyphsInvisible;
}

bool gfxFont::MeasureGlyphs(const gfxTextRun* aTextRun, uint32_t aStart,
                            uint32_t aEnd, BoundingBoxType aBoundingBoxType,
                            DrawTarget* aRefDrawTarget, Spacing* aSpacing,
                            bool aIsRTL, RunMetrics& aMetrics) {
  const gfxTextRun::CompressedGlyph* charGlyphs =
      aTextRun->GetCharacterGlyphs();
  double x = 0;
  if (aSpacing) {
    x += aSpacing[0].mBefore;
  }
  uint32_t spaceGlyph = GetSpaceGlyph();
  bool allGlyphsInvisible = true;

  for (uint32_t i = aStart; i < aEnd; ++i) {
    const gfxTextRun::CompressedGlyph* glyphData = &charGlyphs[i];
    if (glyphData->IsSimpleGlyph()) {
      double advance = glyphData->GetSimpleAdvance();
      uint32_t glyphIndex = glyphData->GetSimpleGlyph();
      if (allGlyphsInvisible &&
          (glyphIndex != spaceGlyph ||
           !IsSpaceGlyphInvisible(aRefDrawTarget, aTextRun))) {
        allGlyphsInvisible = false;
      }
      x += advance;
    } else {
      allGlyphsInvisible = false;
      uint32_t glyphCount = glyphData->GetGlyphCount();
      if (glyphCount > 0) {
        const gfxTextRun::DetailedGlyph* details =
            aTextRun->GetDetailedGlyphs(i);
        NS_ASSERTION(details != nullptr,
                     "detailedGlyph record should not be missing!");
        uint32_t j;
        for (j = 0; j < glyphCount; ++j, ++details) {
          double advance = details->mAdvance;
          gfxRect glyphRect(0, -aMetrics.mAscent, advance,
                            aMetrics.mAscent + aMetrics.mDescent);
          if (aIsRTL) {
            // Swap left/right sidebearings of the glyph, because we're doing
            // mirrored measurement.
            glyphRect.MoveToX(advance - glyphRect.XMost());
            // Move to current x position, mirroring any x-offset amount.
            glyphRect.MoveByX(x - details->mOffset.x);
          } else {
            glyphRect.MoveByX(x + details->mOffset.x);
          }
          glyphRect.MoveByY(details->mOffset.y);
          aMetrics.mBoundingBox = aMetrics.mBoundingBox.Union(glyphRect);
          x += advance;
        }
      }
    }
    if (aSpacing) {
      double space = aSpacing[i - aStart].mAfter;
      if (i + 1 < aEnd) {
        space += aSpacing[i + 1 - aStart].mBefore;
      }
      x += space;
    }
  }

  aMetrics.mAdvanceWidth = x;
  return allGlyphsInvisible;
}

gfxFont::RunMetrics gfxFont::Measure(const gfxTextRun* aTextRun,
                                     uint32_t aStart, uint32_t aEnd,
                                     BoundingBoxType aBoundingBoxType,
                                     DrawTarget* aRefDrawTarget,
                                     Spacing* aSpacing,
                                     gfx::ShapedTextFlags aOrientation) {
  // If aBoundingBoxType is TIGHT_HINTED_OUTLINE_EXTENTS
  // and the underlying cairo font may be antialiased,
  // we need to create a copy in order to avoid getting cached extents.
  // This is only used by MathML layout at present.
  if (aBoundingBoxType == TIGHT_HINTED_OUTLINE_EXTENTS &&
      mAntialiasOption != kAntialiasNone) {
    gfxFont* nonAA = mNonAAFont;
    if (!nonAA) {
      nonAA = CopyWithAntialiasOption(kAntialiasNone);
      if (nonAA) {
        if (!mNonAAFont.compareExchange(nullptr, nonAA)) {
          delete nonAA;
          nonAA = mNonAAFont;
        }
      }
    }
    // if font subclass doesn't implement CopyWithAntialiasOption(),
    // it will return null and we'll proceed to use the existing font
    if (nonAA) {
      return nonAA->Measure(aTextRun, aStart, aEnd,
                            TIGHT_HINTED_OUTLINE_EXTENTS, aRefDrawTarget,
                            aSpacing, aOrientation);
    }
  }

  const int32_t appUnitsPerDevUnit = aTextRun->GetAppUnitsPerDevUnit();
  // Current position in appunits
  Orientation orientation =
      aOrientation == gfx::ShapedTextFlags::TEXT_ORIENT_VERTICAL_UPRIGHT
          ? nsFontMetrics::eVertical
          : nsFontMetrics::eHorizontal;
  const gfxFont::Metrics& fontMetrics = GetMetrics(orientation);

  gfxFloat baselineOffset = 0;
  if (aTextRun->UseCenterBaseline() &&
      orientation == nsFontMetrics::eHorizontal) {
    // For a horizontal font being used in vertical writing mode with
    // text-orientation:mixed, the overall metrics we're accumulating
    // will be aimed at a center baseline. But this font's metrics were
    // based on the alphabetic baseline. So we compute a baseline offset
    // that will be applied to ascent/descent values and glyph rects
    // to effectively shift them relative to the baseline.
    // XXX Eventually we should probably use the BASE table, if present.
    // But it usually isn't, so we need an ad hoc adjustment for now.
    baselineOffset =
        appUnitsPerDevUnit * (fontMetrics.emAscent - fontMetrics.emDescent) / 2;
  }

  RunMetrics metrics;
  metrics.mAscent = fontMetrics.maxAscent * appUnitsPerDevUnit;
  metrics.mDescent = fontMetrics.maxDescent * appUnitsPerDevUnit;

  if (aStart == aEnd) {
    // exit now before we look at aSpacing[0], which is undefined
    metrics.mAscent -= baselineOffset;
    metrics.mDescent += baselineOffset;
    metrics.mBoundingBox =
        gfxRect(0, -metrics.mAscent, 0, metrics.mAscent + metrics.mDescent);
    return metrics;
  }

  gfxFloat advanceMin = 0, advanceMax = 0;
  bool isRTL = aTextRun->IsRightToLeft();
  bool needsGlyphExtents = NeedsGlyphExtents(this, aTextRun);
  gfxGlyphExtents* extents =
      ((aBoundingBoxType == LOOSE_INK_EXTENTS && !needsGlyphExtents &&
        !aTextRun->HasDetailedGlyphs()) ||
       MOZ_UNLIKELY(GetStyle()->AdjustedSizeMustBeZero()))
          ? nullptr
          : GetOrCreateGlyphExtents(aTextRun->GetAppUnitsPerDevUnit());

  bool allGlyphsInvisible;
  if (extents) {
    allGlyphsInvisible = MeasureGlyphs(
        aTextRun, aStart, aEnd, aBoundingBoxType, aRefDrawTarget, aSpacing,
        extents, isRTL, needsGlyphExtents, metrics, &advanceMin, &advanceMax);
  } else {
    allGlyphsInvisible =
        MeasureGlyphs(aTextRun, aStart, aEnd, aBoundingBoxType, aRefDrawTarget,
                      aSpacing, isRTL, metrics);
  }

  if (allGlyphsInvisible) {
    metrics.mBoundingBox.SetEmpty();
  } else if (aBoundingBoxType == LOOSE_INK_EXTENTS) {
    UnionRange(metrics.mAdvanceWidth, &advanceMin, &advanceMax);
    gfxRect fontBox(advanceMin, -metrics.mAscent, advanceMax - advanceMin,
                    metrics.mAscent + metrics.mDescent);
    metrics.mBoundingBox = metrics.mBoundingBox.Union(fontBox);
  }

  if (isRTL) {
    // Reverse the effect of having swapped each glyph's sidebearings, to get
    // the correct sidebearings of the merged bounding box.
    metrics.mBoundingBox.MoveToX(metrics.mAdvanceWidth -
                                 metrics.mBoundingBox.XMost());
  }

  // If the font may be rendered with a fake-italic effect, we need to allow
  // for the top-right of the glyphs being skewed to the right, and the
  // bottom-left being skewed further left.
  gfxFloat skew = SkewForSyntheticOblique();
  if (skew != 0.0) {
    gfxFloat extendLeftEdge, extendRightEdge;
    if (orientation == nsFontMetrics::eVertical) {
      // The glyph will actually be skewed vertically, but "left" and "right"
      // here refer to line-left (physical top) and -right (bottom), so these
      // are still the directions in which we need to extend the box.
      extendLeftEdge = skew < 0.0 ? ceil(-skew * metrics.mBoundingBox.XMost())
                                  : ceil(skew * -metrics.mBoundingBox.X());
      extendRightEdge = skew < 0.0 ? ceil(-skew * -metrics.mBoundingBox.X())
                                   : ceil(skew * metrics.mBoundingBox.XMost());
    } else {
      extendLeftEdge = skew < 0.0 ? ceil(-skew * -metrics.mBoundingBox.Y())
                                  : ceil(skew * metrics.mBoundingBox.YMost());
      extendRightEdge = skew < 0.0 ? ceil(-skew * metrics.mBoundingBox.YMost())
                                   : ceil(skew * -metrics.mBoundingBox.Y());
    }
    metrics.mBoundingBox.SetWidth(metrics.mBoundingBox.Width() +
                                  extendLeftEdge + extendRightEdge);
    metrics.mBoundingBox.MoveByX(-extendLeftEdge);
  }

  if (baselineOffset != 0) {
    metrics.mAscent -= baselineOffset;
    metrics.mDescent += baselineOffset;
    metrics.mBoundingBox.MoveByY(baselineOffset);
  }

  return metrics;
}

bool gfxFont::AgeCachedWords() {
  mozilla::AutoWriteLock lock(mLock);
  if (mWordCache) {
    for (auto it = mWordCache->modIter(); !it.done(); it.next()) {
      auto& entry = it.get().value();
      if (!entry) {
        NS_ASSERTION(entry, "cache entry has no gfxShapedWord!");
        it.remove();
      } else if (entry->IncrementAge() == kShapedWordCacheMaxAge) {
        it.remove();
      }
    }
    return mWordCache->empty();
  }
  return true;
}

void gfxFont::NotifyGlyphsChanged() const {
  AutoReadLock lock(mLock);
  uint32_t i, count = mGlyphExtentsArray.Length();
  for (i = 0; i < count; ++i) {
    // Flush cached extents array
    mGlyphExtentsArray[i]->NotifyGlyphsChanged();
  }

  if (mGlyphChangeObservers) {
    for (const auto& key : *mGlyphChangeObservers) {
      key->NotifyGlyphsChanged();
    }
  }
}

// If aChar is a "word boundary" for shaped-word caching purposes, return it;
// else return 0.
static char16_t IsBoundarySpace(char16_t aChar, char16_t aNextChar) {
  if ((aChar == ' ' || aChar == 0x00A0) && !IsClusterExtender(aNextChar)) {
    return aChar;
  }
  return 0;
}

// In 8-bit text, there cannot be any cluster-extenders.
static uint8_t IsBoundarySpace(uint8_t aChar, uint8_t aNextChar) {
  if (aChar == ' ' || aChar == 0x00A0) {
    return aChar;
  }
  return 0;
}

#ifdef __GNUC__
#  define GFX_MAYBE_UNUSED __attribute__((unused))
#else
#  define GFX_MAYBE_UNUSED
#endif

template <typename T, typename Func>
bool gfxFont::ProcessShapedWordInternal(
    DrawTarget* aDrawTarget, const T* aText, uint32_t aLength, uint32_t aHash,
    Script aRunScript, nsAtom* aLanguage, bool aVertical,
    int32_t aAppUnitsPerDevUnit, gfx::ShapedTextFlags aFlags,
    RoundingFlags aRounding, gfxTextPerfMetrics* aTextPerf GFX_MAYBE_UNUSED,
    Func aCallback) {
  WordCacheKey key(aText, aLength, aHash, aRunScript, aLanguage,
                   aAppUnitsPerDevUnit, aFlags, aRounding);
  {
    // If we have a word cache, attempt to look up the word in it.
    AutoReadLock lock(mLock);
    if (mWordCache) {
      // if there's a cached entry for this word, just return it
      if (auto entry = mWordCache->lookup(key)) {
        entry->value()->ResetAge();
#ifndef RELEASE_OR_BETA
        if (aTextPerf) {
          // XXX we should make sure this is atomic
          aTextPerf->current.wordCacheHit++;
        }
#endif
        aCallback(entry->value().get());
        return true;
      }
    }
  }

  // We didn't find a cached word (or don't even have a cache yet), so create
  // a new gfxShapedWord and cache it. We don't have to lock during shaping,
  // only when it comes time to cache the new entry.

  UniquePtr<gfxShapedWord> newShapedWord(
      gfxShapedWord::Create(aText, aLength, aRunScript, aLanguage,
                            aAppUnitsPerDevUnit, aFlags, aRounding));
  if (!newShapedWord) {
    NS_WARNING("failed to create gfxShapedWord - expect missing text");
    return false;
  }
  DebugOnly<bool> ok =
      ShapeText(aDrawTarget, aText, 0, aLength, aRunScript, aLanguage,
                aVertical, aRounding, newShapedWord.get());
  NS_WARNING_ASSERTION(ok, "failed to shape word - expect garbled text");

  {
    // We're going to cache the new shaped word, so lock for writing now.
    AutoWriteLock lock(mLock);
    if (!mWordCache) {
      mWordCache = MakeUnique<HashMap<WordCacheKey, UniquePtr<gfxShapedWord>,
                                      WordCacheKey::HashPolicy>>();
    } else {
      // If the cache is getting too big, flush it and start over.
      uint32_t wordCacheMaxEntries =
          gfxPlatform::GetPlatform()->WordCacheMaxEntries();
      if (mWordCache->count() > wordCacheMaxEntries) {
        // Flush the cache if it is getting overly big.
        NS_WARNING("flushing shaped-word cache");
        ClearCachedWordsLocked();
      }
    }

    // Update key so that it references the text stored in the newShapedWord,
    // which is guaranteed to live as long as the hashtable entry.
    if ((key.mTextIs8Bit = newShapedWord->TextIs8Bit())) {
      key.mText.mSingle = newShapedWord->Text8Bit();
    } else {
      key.mText.mDouble = newShapedWord->TextUnicode();
    }
    auto entry = mWordCache->lookupForAdd(key);

    // It's unlikely, but maybe another thread got there before us...
    if (entry) {
      // Use the existing entry; the newShapedWord will be discarded.
      entry->value()->ResetAge();
#ifndef RELEASE_OR_BETA
      if (aTextPerf) {
        aTextPerf->current.wordCacheHit++;
      }
#endif
      aCallback(entry->value().get());
      return true;
    }

    if (!mWordCache->add(entry, key, std::move(newShapedWord))) {
      NS_WARNING("failed to cache gfxShapedWord - expect missing text");
      return false;
    }

#ifndef RELEASE_OR_BETA
    if (aTextPerf) {
      aTextPerf->current.wordCacheMiss++;
    }
#endif
    aCallback(entry->value().get());
  }

  gfxFontCache::GetCache()->RunWordCacheExpirationTimer();
  return true;
}

bool gfxFont::WordCacheKey::HashPolicy::match(const Key& aKey,
                                              const Lookup& aLookup) {
  if (aKey.mLength != aLookup.mLength || aKey.mFlags != aLookup.mFlags ||
      aKey.mRounding != aLookup.mRounding ||
      aKey.mAppUnitsPerDevUnit != aLookup.mAppUnitsPerDevUnit ||
      aKey.mScript != aLookup.mScript || aKey.mLanguage != aLookup.mLanguage) {
    return false;
  }

  if (aKey.mTextIs8Bit) {
    if (aLookup.mTextIs8Bit) {
      return (0 == memcmp(aKey.mText.mSingle, aLookup.mText.mSingle,
                          aKey.mLength * sizeof(uint8_t)));
    }
    // The lookup key has 16-bit text, even though all the characters are < 256,
    // so the TEXT_IS_8BIT flag was set and the cached ShapedWord we're
    // comparing with will have 8-bit text.
    const uint8_t* s1 = aKey.mText.mSingle;
    const char16_t* s2 = aLookup.mText.mDouble;
    const char16_t* s2end = s2 + aKey.mLength;
    while (s2 < s2end) {
      if (*s1++ != *s2++) {
        return false;
      }
    }
    return true;
  }
  NS_ASSERTION(!(aLookup.mFlags & gfx::ShapedTextFlags::TEXT_IS_8BIT) &&
                   !aLookup.mTextIs8Bit,
               "didn't expect 8-bit text here");
  return (0 == memcmp(aKey.mText.mDouble, aLookup.mText.mDouble,
                      aKey.mLength * sizeof(char16_t)));
}

bool gfxFont::ProcessSingleSpaceShapedWord(
    DrawTarget* aDrawTarget, bool aVertical, int32_t aAppUnitsPerDevUnit,
    gfx::ShapedTextFlags aFlags, RoundingFlags aRounding,
    const std::function<void(gfxShapedWord*)>& aCallback) {
  static const uint8_t space = ' ';
  return ProcessShapedWordInternal(
      aDrawTarget, &space, 1, gfxShapedWord::HashMix(0, ' '), Script::LATIN,
      /* aLanguage = */ nullptr, aVertical, aAppUnitsPerDevUnit, aFlags,
      aRounding, nullptr, aCallback);
}

bool gfxFont::ShapeText(DrawTarget* aDrawTarget, const uint8_t* aText,
                        uint32_t aOffset, uint32_t aLength, Script aScript,
                        nsAtom* aLanguage, bool aVertical,
                        RoundingFlags aRounding, gfxShapedText* aShapedText) {
  nsDependentCSubstring ascii((const char*)aText, aLength);
  nsAutoString utf16;
  AppendASCIItoUTF16(ascii, utf16);
  if (utf16.Length() != aLength) {
    return false;
  }
  return ShapeText(aDrawTarget, utf16.BeginReading(), aOffset, aLength, aScript,
                   aLanguage, aVertical, aRounding, aShapedText);
}

bool gfxFont::ShapeText(DrawTarget* aDrawTarget, const char16_t* aText,
                        uint32_t aOffset, uint32_t aLength, Script aScript,
                        nsAtom* aLanguage, bool aVertical,
                        RoundingFlags aRounding, gfxShapedText* aShapedText) {
  // XXX Currently, we do all vertical shaping through harfbuzz.
  // Vertical graphite support may be wanted as a future enhancement.
  // XXX Graphite shaping currently only supported on the main thread!
  // Worker-thread shaping (offscreen canvas) will always go via harfbuzz.
  if (FontCanSupportGraphite() && !aVertical && NS_IsMainThread()) {
    if (gfxPlatform::GetPlatform()->UseGraphiteShaping()) {
      gfxGraphiteShaper* shaper = mGraphiteShaper;
      if (!shaper) {
        shaper = new gfxGraphiteShaper(this);
        if (!mGraphiteShaper.compareExchange(nullptr, shaper)) {
          delete shaper;
          shaper = mGraphiteShaper;
        }
      }
      if (shaper->ShapeText(aDrawTarget, aText, aOffset, aLength, aScript,
                            aLanguage, aVertical, aRounding, aShapedText)) {
        PostShapingFixup(aDrawTarget, aText, aOffset, aLength, aVertical,
                         aShapedText);
        return true;
      }
    }
  }

  gfxHarfBuzzShaper* shaper = GetHarfBuzzShaper();
  if (shaper &&
      shaper->ShapeText(aDrawTarget, aText, aOffset, aLength, aScript,
                        aLanguage, aVertical, aRounding, aShapedText)) {
    PostShapingFixup(aDrawTarget, aText, aOffset, aLength, aVertical,
                     aShapedText);
    if (GetFontEntry()->HasTrackingTable()) {
      // Convert font size from device pixels back to CSS px
      // to use in selecting tracking value
      gfxFloat trackSize = GetAdjustedSize() *
                           aShapedText->GetAppUnitsPerDevUnit() /
                           AppUnitsPerCSSPixel();
      // Usually, a given font will be used with the same appunit scale, so we
      // can cache the tracking value rather than recompute it every time.
      {
        AutoReadLock lock(mLock);
        if (trackSize == mCachedTrackingSize) {
          // Applying tracking is a lot like the adjustment we do for
          // synthetic bold: we want to apply between clusters, not to
          // non-spacing glyphs within a cluster. So we can reuse that
          // helper here.
          aShapedText->ApplyTrackingToClusters(mTracking, aOffset, aLength);
          return true;
        }
      }
      // We didn't have the appropriate tracking value cached yet.
      AutoWriteLock lock(mLock);
      if (trackSize != mCachedTrackingSize) {
        mCachedTrackingSize = trackSize;
        mTracking =
            GetFontEntry()->TrackingForCSSPx(trackSize) * mFUnitsConvFactor;
      }
      aShapedText->ApplyTrackingToClusters(mTracking, aOffset, aLength);
    }
    return true;
  }

  NS_WARNING_ASSERTION(false, "shaper failed, expect scrambled/missing text");
  return false;
}

void gfxFont::PostShapingFixup(DrawTarget* aDrawTarget, const char16_t* aText,
                               uint32_t aOffset, uint32_t aLength,
                               bool aVertical, gfxShapedText* aShapedText) {
  if (ApplySyntheticBold()) {
    const Metrics& metrics = GetMetrics(aVertical ? nsFontMetrics::eVertical
                                                  : nsFontMetrics::eHorizontal);
    if (metrics.maxAdvance > metrics.aveCharWidth) {
      aShapedText->ApplyTrackingToClusters(GetSyntheticBoldOffset(), aOffset,
                                           aLength);
    }
  }
}

#define MAX_SHAPING_LENGTH \
  32760  // slightly less than 32K, trying to avoid
         // over-stressing platform shapers
#define BACKTRACK_LIMIT \
  16  // backtrack this far looking for a good place
      // to split into fragments for separate shaping

template <typename T>
bool gfxFont::ShapeFragmentWithoutWordCache(DrawTarget* aDrawTarget,
                                            const T* aText, uint32_t aOffset,
                                            uint32_t aLength, Script aScript,
                                            nsAtom* aLanguage, bool aVertical,
                                            RoundingFlags aRounding,
                                            gfxTextRun* aTextRun) {
  aTextRun->SetupClusterBoundaries(aOffset, aText, aLength);

  bool ok = true;

  while (ok && aLength > 0) {
    uint32_t fragLen = aLength;

    // limit the length of text we pass to shapers in a single call
    if (fragLen > MAX_SHAPING_LENGTH) {
      fragLen = MAX_SHAPING_LENGTH;

      // in the 8-bit case, there are no multi-char clusters,
      // so we don't need to do this check
      if constexpr (sizeof(T) == sizeof(char16_t)) {
        uint32_t i;
        for (i = 0; i < BACKTRACK_LIMIT; ++i) {
          if (aTextRun->IsClusterStart(aOffset + fragLen - i)) {
            fragLen -= i;
            break;
          }
        }
        if (i == BACKTRACK_LIMIT) {
          // if we didn't find any cluster start while backtracking,
          // just check that we're not in the middle of a surrogate
          // pair; back up by one code unit if we are.
          if (NS_IS_SURROGATE_PAIR(aText[fragLen - 1], aText[fragLen])) {
            --fragLen;
          }
        }
      }
    }

    ok = ShapeText(aDrawTarget, aText, aOffset, fragLen, aScript, aLanguage,
                   aVertical, aRounding, aTextRun);

    aText += fragLen;
    aOffset += fragLen;
    aLength -= fragLen;
  }

  return ok;
}

// Check if aCh is an unhandled control character that should be displayed
// as a hexbox rather than rendered by some random font on the system.
// We exclude \r as stray &#13;s are rather common (bug 941940).
// Note that \n and \t don't come through here, as they have specific
// meanings that have already been handled.
static bool IsInvalidControlChar(uint32_t aCh) {
  return aCh != '\r' && ((aCh & 0x7f) < 0x20 || aCh == 0x7f);
}

template <typename T>
bool gfxFont::ShapeTextWithoutWordCache(DrawTarget* aDrawTarget, const T* aText,
                                        uint32_t aOffset, uint32_t aLength,
                                        Script aScript, nsAtom* aLanguage,
                                        bool aVertical, RoundingFlags aRounding,
                                        gfxTextRun* aTextRun) {
  uint32_t fragStart = 0;
  bool ok = true;

  for (uint32_t i = 0; i <= aLength && ok; ++i) {
    T ch = (i < aLength) ? aText[i] : '\n';
    bool invalid = gfxFontGroup::IsInvalidChar(ch);
    uint32_t length = i - fragStart;

    // break into separate fragments when we hit an invalid char
    if (!invalid) {
      continue;
    }

    if (length > 0) {
      ok = ShapeFragmentWithoutWordCache(
          aDrawTarget, aText + fragStart, aOffset + fragStart, length, aScript,
          aLanguage, aVertical, aRounding, aTextRun);
    }

    if (i == aLength) {
      break;
    }

    // fragment was terminated by an invalid char: skip it,
    // unless it's a control char that we want to show as a hexbox,
    // but record where TAB or NEWLINE occur
    if (ch == '\t') {
      aTextRun->SetIsTab(aOffset + i);
    } else if (ch == '\n') {
      aTextRun->SetIsNewline(aOffset + i);
    } else if (GetGeneralCategory(ch) == HB_UNICODE_GENERAL_CATEGORY_FORMAT) {
      aTextRun->SetIsFormattingControl(aOffset + i);
    } else if (IsInvalidControlChar(ch) &&
               !(aTextRun->GetFlags() &
                 gfx::ShapedTextFlags::TEXT_HIDE_CONTROL_CHARACTERS)) {
      if (GetFontEntry()->IsUserFont() && HasCharacter(ch)) {
        ShapeFragmentWithoutWordCache(aDrawTarget, aText + i, aOffset + i, 1,
                                      aScript, aLanguage, aVertical, aRounding,
                                      aTextRun);
      } else {
        aTextRun->SetMissingGlyph(aOffset + i, ch, this);
      }
    }
    fragStart = i + 1;
  }

  NS_WARNING_ASSERTION(ok, "failed to shape text - expect garbled text");
  return ok;
}

#ifndef RELEASE_OR_BETA
#  define TEXT_PERF_INCR(tp, m) (tp ? (tp)->current.m++ : 0)
#else
#  define TEXT_PERF_INCR(tp, m)
#endif

inline static bool IsChar8Bit(uint8_t /*aCh*/) { return true; }
inline static bool IsChar8Bit(char16_t aCh) { return aCh < 0x100; }

inline static bool HasSpaces(const uint8_t* aString, uint32_t aLen) {
  return memchr(aString, 0x20, aLen) != nullptr;
}

inline static bool HasSpaces(const char16_t* aString, uint32_t aLen) {
  for (const char16_t* ch = aString; ch < aString + aLen; ch++) {
    if (*ch == 0x20) {
      return true;
    }
  }
  return false;
}

template <typename T>
bool gfxFont::SplitAndInitTextRun(
    DrawTarget* aDrawTarget, gfxTextRun* aTextRun,
    const T* aString,    // text for this font run
    uint32_t aRunStart,  // position in the textrun
    uint32_t aRunLength, Script aRunScript, nsAtom* aLanguage,
    ShapedTextFlags aOrientation) {
  if (aRunLength == 0) {
    return true;
  }

  gfxTextPerfMetrics* tp = nullptr;
  RoundingFlags rounding = GetRoundOffsetsToPixels(aDrawTarget);

#ifndef RELEASE_OR_BETA
  tp = aTextRun->GetFontGroup()->GetTextPerfMetrics();
  if (tp) {
    if (mStyle.systemFont) {
      tp->current.numChromeTextRuns++;
    } else {
      tp->current.numContentTextRuns++;
    }
    tp->current.numChars += aRunLength;
    if (aRunLength > tp->current.maxTextRunLen) {
      tp->current.maxTextRunLen = aRunLength;
    }
  }
#endif

  uint32_t wordCacheCharLimit =
      gfxPlatform::GetPlatform()->WordCacheCharLimit();

  bool vertical = aOrientation == ShapedTextFlags::TEXT_ORIENT_VERTICAL_UPRIGHT;

  // If spaces can participate in shaping (e.g. within lookups for automatic
  // fractions), need to shape without using the word cache which segments
  // textruns on space boundaries. Word cache can be used if the textrun
  // is short enough to fit in the word cache and it lacks spaces.
  tainted_boolean_hint t_canParticipate =
      SpaceMayParticipateInShaping(aRunScript);
  bool canParticipate = t_canParticipate.unverified_safe_because(
      "We need to ensure that this function operates safely independent of "
      "t_canParticipate. The worst that can happen here is that the decision "
      "to use the cache is incorrectly made, resulting in a bad "
      "rendering/slowness. However, this  would not compromise the memory "
      "safety of Firefox in any way, and can thus be permitted");

  if (canParticipate) {
    if (aRunLength > wordCacheCharLimit || HasSpaces(aString, aRunLength)) {
      TEXT_PERF_INCR(tp, wordCacheSpaceRules);
      return ShapeTextWithoutWordCache(aDrawTarget, aString, aRunStart,
                                       aRunLength, aRunScript, aLanguage,
                                       vertical, rounding, aTextRun);
    }
  }

  // the only flags we care about for ShapedWord construction/caching
  gfx::ShapedTextFlags flags = aTextRun->GetFlags();
  flags &= (gfx::ShapedTextFlags::TEXT_IS_RTL |
            gfx::ShapedTextFlags::TEXT_DISABLE_OPTIONAL_LIGATURES |
            gfx::ShapedTextFlags::TEXT_USE_MATH_SCRIPT |
            gfx::ShapedTextFlags::TEXT_ORIENT_MASK);
  if constexpr (sizeof(T) == sizeof(uint8_t)) {
    flags |= gfx::ShapedTextFlags::TEXT_IS_8BIT;
  }

  uint32_t wordStart = 0;
  uint32_t hash = 0;
  bool wordIs8Bit = true;
  int32_t appUnitsPerDevUnit = aTextRun->GetAppUnitsPerDevUnit();

  T nextCh = aString[0];
  for (uint32_t i = 0; i <= aRunLength; ++i) {
    T ch = nextCh;
    nextCh = (i < aRunLength - 1) ? aString[i + 1] : '\n';
    T boundary = IsBoundarySpace(ch, nextCh);
    bool invalid = !boundary && gfxFontGroup::IsInvalidChar(ch);
    uint32_t length = i - wordStart;

    // break into separate ShapedWords when we hit an invalid char,
    // or a boundary space (always handled individually),
    // or the first non-space after a space
    if (!boundary && !invalid) {
      if (!IsChar8Bit(ch)) {
        wordIs8Bit = false;
      }
      // include this character in the hash, and move on to next
      hash = gfxShapedWord::HashMix(hash, ch);
      continue;
    }

    // We've decided to break here (i.e. we're at the end of a "word");
    // shape the word and add it to the textrun.
    // For words longer than the limit, we don't use the
    // font's word cache but just shape directly into the textrun.
    if (length > wordCacheCharLimit) {
      TEXT_PERF_INCR(tp, wordCacheLong);
      bool ok = ShapeFragmentWithoutWordCache(
          aDrawTarget, aString + wordStart, aRunStart + wordStart, length,
          aRunScript, aLanguage, vertical, rounding, aTextRun);
      if (!ok) {
        return false;
      }
    } else if (length > 0) {
      gfx::ShapedTextFlags wordFlags = flags;
      // in the 8-bit version of this method, TEXT_IS_8BIT was
      // already set as part of |flags|, so no need for a per-word
      // adjustment here
      if (sizeof(T) == sizeof(char16_t)) {
        if (wordIs8Bit) {
          wordFlags |= gfx::ShapedTextFlags::TEXT_IS_8BIT;
        }
      }
      bool processed = ProcessShapedWordInternal(
          aDrawTarget, aString + wordStart, length, hash, aRunScript, aLanguage,
          vertical, appUnitsPerDevUnit, wordFlags, rounding, tp,
          [&](gfxShapedWord* aShapedWord) {
            aTextRun->CopyGlyphDataFrom(aShapedWord, aRunStart + wordStart);
          });
      if (!processed) {
        return false;  // failed, presumably out of memory?
      }
    }

    if (boundary) {
      // word was terminated by a space: add that to the textrun
      MOZ_ASSERT(aOrientation != ShapedTextFlags::TEXT_ORIENT_VERTICAL_MIXED,
                 "text-orientation:mixed should be resolved earlier");
      if (boundary != ' ' || !aTextRun->SetSpaceGlyphIfSimple(
                                 this, aRunStart + i, ch, aOrientation)) {
        // Currently, the only "boundary" characters we recognize are
        // space and no-break space, which are both 8-bit, so we force
        // that flag (below). If we ever change IsBoundarySpace, we
        // may need to revise this.
        // Avoid tautological-constant-out-of-range-compare in 8-bit:
        DebugOnly<char16_t> boundary16 = boundary;
        NS_ASSERTION(boundary16 < 256, "unexpected boundary!");
        bool processed = ProcessShapedWordInternal(
            aDrawTarget, &boundary, 1, gfxShapedWord::HashMix(0, boundary),
            aRunScript, aLanguage, vertical, appUnitsPerDevUnit,
            flags | gfx::ShapedTextFlags::TEXT_IS_8BIT, rounding, tp,
            [&](gfxShapedWord* aShapedWord) {
              aTextRun->CopyGlyphDataFrom(aShapedWord, aRunStart + i);
              if (boundary == ' ') {
                aTextRun->GetCharacterGlyphs()[aRunStart + i].SetIsSpace();
              }
            });
        if (!processed) {
          return false;
        }
      }
      hash = 0;
      wordStart = i + 1;
      wordIs8Bit = true;
      continue;
    }

    if (i == aRunLength) {
      break;
    }

    NS_ASSERTION(invalid, "how did we get here except via an invalid char?");

    // word was terminated by an invalid char: skip it,
    // unless it's a control char that we want to show as a hexbox,
    // but record where TAB or NEWLINE occur
    if (ch == '\t') {
      aTextRun->SetIsTab(aRunStart + i);
    } else if (ch == '\n') {
      aTextRun->SetIsNewline(aRunStart + i);
    } else if (GetGeneralCategory(ch) == HB_UNICODE_GENERAL_CATEGORY_FORMAT) {
      aTextRun->SetIsFormattingControl(aRunStart + i);
    } else if (IsInvalidControlChar(ch) &&
               !(aTextRun->GetFlags() &
                 gfx::ShapedTextFlags::TEXT_HIDE_CONTROL_CHARACTERS)) {
      if (GetFontEntry()->IsUserFont() && HasCharacter(ch)) {
        ShapeFragmentWithoutWordCache(aDrawTarget, aString + i, aRunStart + i,
                                      1, aRunScript, aLanguage, vertical,
                                      rounding, aTextRun);
      } else {
        aTextRun->SetMissingGlyph(aRunStart + i, ch, this);
      }
    }

    hash = 0;
    wordStart = i + 1;
    wordIs8Bit = true;
  }

  return true;
}

// Explicit instantiations of SplitAndInitTextRun, to avoid libxul link failure
template bool gfxFont::SplitAndInitTextRun(
    DrawTarget* aDrawTarget, gfxTextRun* aTextRun, const uint8_t* aString,
    uint32_t aRunStart, uint32_t aRunLength, Script aRunScript,
    nsAtom* aLanguage, ShapedTextFlags aOrientation);
template bool gfxFont::SplitAndInitTextRun(
    DrawTarget* aDrawTarget, gfxTextRun* aTextRun, const char16_t* aString,
    uint32_t aRunStart, uint32_t aRunLength, Script aRunScript,
    nsAtom* aLanguage, ShapedTextFlags aOrientation);

template <>
bool gfxFont::InitFakeSmallCapsRun(
    nsPresContext* aPresContext, DrawTarget* aDrawTarget, gfxTextRun* aTextRun,
    const char16_t* aText, uint32_t aOffset, uint32_t aLength,
    FontMatchType aMatchType, gfx::ShapedTextFlags aOrientation, Script aScript,
    nsAtom* aLanguage, bool aSyntheticLower, bool aSyntheticUpper) {
  bool ok = true;

  RefPtr<gfxFont> smallCapsFont = GetSmallCapsFont();
  if (!smallCapsFont) {
    NS_WARNING("failed to get reduced-size font for smallcaps!");
    smallCapsFont = this;
  }

  bool isCJK = gfxTextRun::IsCJKScript(aScript);

  enum RunCaseAction { kNoChange, kUppercaseReduce, kUppercase };

  RunCaseAction runAction = kNoChange;
  uint32_t runStart = 0;

  for (uint32_t i = 0; i <= aLength; ++i) {
    uint32_t extraCodeUnits = 0;  // Will be set to 1 if we need to consume
                                  // a trailing surrogate as well as the
                                  // current code unit.
    RunCaseAction chAction = kNoChange;
    // Unless we're at the end, figure out what treatment the current
    // character will need.
    if (i < aLength) {
      uint32_t ch = aText[i];
      if (i < aLength - 1 && NS_IS_SURROGATE_PAIR(ch, aText[i + 1])) {
        ch = SURROGATE_TO_UCS4(ch, aText[i + 1]);
        extraCodeUnits = 1;
      }
      // Characters that aren't the start of a cluster are ignored here.
      // They get added to whatever lowercase/non-lowercase run we're in.
      if (IsClusterExtender(ch)) {
        chAction = runAction;
      } else {
        if (ch != ToUpperCase(ch) || SpecialUpper(ch)) {
          // ch is lower case
          chAction = (aSyntheticLower ? kUppercaseReduce : kNoChange);
        } else if (ch != ToLowerCase(ch)) {
          // ch is upper case
          chAction = (aSyntheticUpper ? kUppercaseReduce : kNoChange);
          if (aLanguage == nsGkAtoms::el) {
            // In Greek, check for characters that will be modified by
            // the GreekUpperCase mapping - this catches accented
            // capitals where the accent is to be removed (bug 307039).
            // These are handled by using the full-size font with the
            // uppercasing transform.
            mozilla::GreekCasing::State state;
            bool markEta, updateEta;
            uint32_t ch2 =
                mozilla::GreekCasing::UpperCase(ch, state, markEta, updateEta);
            if ((ch != ch2 || markEta) && !aSyntheticUpper) {
              chAction = kUppercase;
            }
          }
        }
      }
    }

    // At the end of the text or when the current character needs different
    // casing treatment from the current run, finish the run-in-progress
    // and prepare to accumulate a new run.
    // Note that we do not look at any source data for offset [i] here,
    // as that would be invalid in the case where i==length.
    if ((i == aLength || runAction != chAction) && runStart < i) {
      uint32_t runLength = i - runStart;
      gfxFont* f = this;
      switch (runAction) {
        case kNoChange:
          // just use the current font and the existing string
          aTextRun->AddGlyphRun(f, aMatchType, aOffset + runStart, true,
                                aOrientation, isCJK);
          if (!f->SplitAndInitTextRun(aDrawTarget, aTextRun, aText + runStart,
                                      aOffset + runStart, runLength, aScript,
                                      aLanguage, aOrientation)) {
            ok = false;
          }
          break;

        case kUppercaseReduce:
          // use reduced-size font, then fall through to uppercase the text
          f = smallCapsFont;
          [[fallthrough]];

        case kUppercase:
          // apply uppercase transform to the string
          nsDependentSubstring origString(aText + runStart, runLength);
          nsAutoString convertedString;
          AutoTArray<bool, 50> charsToMergeArray;
          AutoTArray<bool, 50> deletedCharsArray;

          const auto globalTransform = StyleTextTransform::UPPERCASE;
          // No mask needed; we're doing case conversion, not password-hiding.
          const char16_t maskChar = 0;
          bool mergeNeeded = nsCaseTransformTextRunFactory::TransformString(
              origString, convertedString, Some(globalTransform), maskChar,
              /* aCaseTransformsOnly = */ false, aLanguage, charsToMergeArray,
              deletedCharsArray);

          // Check whether the font supports the uppercased characters needed;
          // if not, we're not going to be able to simulate small-caps.
          bool failed = false;
          char16_t highSurrogate = 0;
          for (const char16_t* cp = convertedString.BeginReading();
               cp != convertedString.EndReading(); ++cp) {
            if (NS_IS_HIGH_SURROGATE(*cp)) {
              highSurrogate = *cp;
              continue;
            }
            uint32_t ch = *cp;
            if (NS_IS_LOW_SURROGATE(*cp) && highSurrogate) {
              ch = SURROGATE_TO_UCS4(highSurrogate, *cp);
            }
            highSurrogate = 0;
            if (!f->HasCharacter(ch)) {
              if (IsDefaultIgnorable(ch)) {
                continue;
              }
              failed = true;
              break;
            }
          }
          // Required uppercase letter(s) missing from the font. Just use the
          // original text with the original font, no fake small caps!
          if (failed) {
            convertedString = origString;
            mergeNeeded = false;
            f = this;
          }

          if (mergeNeeded) {
            // This is the hard case: the transformation caused chars
            // to be inserted or deleted, so we can't shape directly
            // into the destination textrun but have to handle the
            // mismatch of character positions.
            gfxTextRunFactory::Parameters params = {
                aDrawTarget, nullptr, nullptr,
                nullptr,     0,       aTextRun->GetAppUnitsPerDevUnit()};
            RefPtr<gfxTextRun> tempRun(gfxTextRun::Create(
                &params, convertedString.Length(), aTextRun->GetFontGroup(),
                gfx::ShapedTextFlags(), nsTextFrameUtils::Flags()));
            tempRun->AddGlyphRun(f, aMatchType, 0, true, aOrientation, isCJK);
            if (!f->SplitAndInitTextRun(aDrawTarget, tempRun.get(),
                                        convertedString.BeginReading(), 0,
                                        convertedString.Length(), aScript,
                                        aLanguage, aOrientation)) {
              ok = false;
            } else {
              RefPtr<gfxTextRun> mergedRun(gfxTextRun::Create(
                  &params, runLength, aTextRun->GetFontGroup(),
                  gfx::ShapedTextFlags(), nsTextFrameUtils::Flags()));
              MergeCharactersInTextRun(mergedRun.get(), tempRun.get(),
                                       charsToMergeArray.Elements(),
                                       deletedCharsArray.Elements());
              gfxTextRun::Range runRange(0, runLength);
              aTextRun->CopyGlyphDataFrom(mergedRun.get(), runRange,
                                          aOffset + runStart);
            }
          } else {
            aTextRun->AddGlyphRun(f, aMatchType, aOffset + runStart, true,
                                  aOrientation, isCJK);
            if (!f->SplitAndInitTextRun(aDrawTarget, aTextRun,
                                        convertedString.BeginReading(),
                                        aOffset + runStart, runLength, aScript,
                                        aLanguage, aOrientation)) {
              ok = false;
            }
          }
          break;
      }

      runStart = i;
    }

    i += extraCodeUnits;
    if (i < aLength) {
      runAction = chAction;
    }
  }

  return ok;
}

template <>
bool gfxFont::InitFakeSmallCapsRun(
    nsPresContext* aPresContext, DrawTarget* aDrawTarget, gfxTextRun* aTextRun,
    const uint8_t* aText, uint32_t aOffset, uint32_t aLength,
    FontMatchType aMatchType, gfx::ShapedTextFlags aOrientation, Script aScript,
    nsAtom* aLanguage, bool aSyntheticLower, bool aSyntheticUpper) {
  NS_ConvertASCIItoUTF16 unicodeString(reinterpret_cast<const char*>(aText),
                                       aLength);
  return InitFakeSmallCapsRun(aPresContext, aDrawTarget, aTextRun,
                              static_cast<const char16_t*>(unicodeString.get()),
                              aOffset, aLength, aMatchType, aOrientation,
                              aScript, aLanguage, aSyntheticLower,
                              aSyntheticUpper);
}

already_AddRefed<gfxFont> gfxFont::GetSmallCapsFont() const {
  gfxFontStyle style(*GetStyle());
  style.size *= SMALL_CAPS_SCALE_FACTOR;
  style.variantCaps = NS_FONT_VARIANT_CAPS_NORMAL;
  gfxFontEntry* fe = GetFontEntry();
  return fe->FindOrMakeFont(&style, mUnicodeRangeMap);
}

already_AddRefed<gfxFont> gfxFont::GetSubSuperscriptFont(
    int32_t aAppUnitsPerDevPixel) const {
  gfxFontStyle style(*GetStyle());
  style.AdjustForSubSuperscript(aAppUnitsPerDevPixel);
  gfxFontEntry* fe = GetFontEntry();
  return fe->FindOrMakeFont(&style, mUnicodeRangeMap);
}

gfxGlyphExtents* gfxFont::GetOrCreateGlyphExtents(int32_t aAppUnitsPerDevUnit) {
  uint32_t readCount;
  {
    AutoReadLock lock(mLock);
    readCount = mGlyphExtentsArray.Length();
    for (uint32_t i = 0; i < readCount; ++i) {
      if (mGlyphExtentsArray[i]->GetAppUnitsPerDevUnit() == aAppUnitsPerDevUnit)
        return mGlyphExtentsArray[i].get();
    }
  }
  AutoWriteLock lock(mLock);
  // Re-check in case of race.
  uint32_t count = mGlyphExtentsArray.Length();
  for (uint32_t i = readCount; i < count; ++i) {
    if (mGlyphExtentsArray[i]->GetAppUnitsPerDevUnit() == aAppUnitsPerDevUnit)
      return mGlyphExtentsArray[i].get();
  }
  gfxGlyphExtents* glyphExtents = new gfxGlyphExtents(aAppUnitsPerDevUnit);
  if (glyphExtents) {
    mGlyphExtentsArray.AppendElement(glyphExtents);
    // Initialize the extents of a space glyph, assuming that spaces don't
    // render anything!
    glyphExtents->SetContainedGlyphWidthAppUnits(GetSpaceGlyph(), 0);
  }
  return glyphExtents;
}

void gfxFont::SetupGlyphExtents(DrawTarget* aDrawTarget, uint32_t aGlyphID,
                                bool aNeedTight, gfxGlyphExtents* aExtents) {
  gfxRect svgBounds;
  if (mFontEntry->TryGetSVGData(this) && mFontEntry->HasSVGGlyph(aGlyphID) &&
      mFontEntry->GetSVGGlyphExtents(aDrawTarget, aGlyphID, GetAdjustedSize(),
                                     &svgBounds)) {
    gfxFloat d2a = aExtents->GetAppUnitsPerDevUnit();
    aExtents->SetTightGlyphExtents(
        aGlyphID, gfxRect(svgBounds.X() * d2a, svgBounds.Y() * d2a,
                          svgBounds.Width() * d2a, svgBounds.Height() * d2a));
    return;
  }

  if (mFontEntry->TryGetColorGlyphs() && mFontEntry->mCOLR &&
      COLRFonts::GetColrTableVersion(mFontEntry->mCOLR) == 1) {
    auto* shaper = GetHarfBuzzShaper();
    if (shaper && shaper->IsInitialized()) {
      RefPtr scaledFont = GetScaledFont(aDrawTarget);
      Rect r = COLRFonts::GetColorGlyphBounds(
          mFontEntry->mCOLR, shaper->GetHBFont(), aGlyphID, aDrawTarget,
          scaledFont, mFUnitsConvFactor);
      if (!r.IsEmpty()) {
        gfxFloat d2a = aExtents->GetAppUnitsPerDevUnit();
        aExtents->SetTightGlyphExtents(
            aGlyphID, gfxRect(r.X() * d2a, r.Y() * d2a, r.Width() * d2a,
                              r.Height() * d2a));
        return;
      }
    }
  }

  gfxRect bounds;
  GetGlyphBounds(aGlyphID, &bounds, mAntialiasOption == kAntialiasNone);

  const Metrics& fontMetrics = GetMetrics(nsFontMetrics::eHorizontal);
  int32_t appUnitsPerDevUnit = aExtents->GetAppUnitsPerDevUnit();
  if (!aNeedTight && bounds.x >= 0.0 && bounds.y >= -fontMetrics.maxAscent &&
      bounds.height + bounds.y <= fontMetrics.maxDescent) {
    uint32_t appUnitsWidth =
        uint32_t(ceil((bounds.x + bounds.width) * appUnitsPerDevUnit));
    if (appUnitsWidth < gfxGlyphExtents::INVALID_WIDTH) {
      aExtents->SetContainedGlyphWidthAppUnits(aGlyphID,
                                               uint16_t(appUnitsWidth));
      return;
    }
  }
#ifdef DEBUG_TEXT_RUN_STORAGE_METRICS
  if (!aNeedTight) {
    ++gGlyphExtentsSetupFallBackToTight;
  }
#endif

  gfxFloat d2a = appUnitsPerDevUnit;
  aExtents->SetTightGlyphExtents(
      aGlyphID, gfxRect(bounds.x * d2a, bounds.y * d2a, bounds.width * d2a,
                        bounds.height * d2a));
}

// Try to initialize font metrics by reading sfnt tables directly;
// set mIsValid=TRUE and return TRUE on success.
// Return FALSE if the gfxFontEntry subclass does not
// implement GetFontTable(), or for non-sfnt fonts where tables are
// not available.
// If this returns TRUE without setting the mIsValid flag, then we -did-
// apparently find an sfnt, but it was too broken to be used.
bool gfxFont::InitMetricsFromSfntTables(Metrics& aMetrics) {
  mIsValid = false;  // font is NOT valid in case of early return

  const uint32_t kHheaTableTag = TRUETYPE_TAG('h', 'h', 'e', 'a');
  const uint32_t kOS_2TableTag = TRUETYPE_TAG('O', 'S', '/', '2');

  uint32_t len;

  if (mFUnitsConvFactor < 0.0) {
    // If the conversion factor from FUnits is not yet set,
    // get the unitsPerEm from the 'head' table via the font entry
    uint16_t unitsPerEm = GetFontEntry()->UnitsPerEm();
    if (unitsPerEm == gfxFontEntry::kInvalidUPEM) {
      return false;
    }
    mFUnitsConvFactor = GetAdjustedSize() / unitsPerEm;
  }

  // 'hhea' table is required for the advanceWidthMax field
  gfxFontEntry::AutoTable hheaTable(mFontEntry, kHheaTableTag);
  if (!hheaTable) {
    return false;  // no 'hhea' table -> not an sfnt
  }
  const MetricsHeader* hhea =
      reinterpret_cast<const MetricsHeader*>(hb_blob_get_data(hheaTable, &len));
  if (len < sizeof(MetricsHeader)) {
    return false;
  }

#define SET_UNSIGNED(field, src) \
  aMetrics.field = uint16_t(src) * mFUnitsConvFactor
#define SET_SIGNED(field, src) aMetrics.field = int16_t(src) * mFUnitsConvFactor

  SET_UNSIGNED(maxAdvance, hhea->advanceWidthMax);

  // 'OS/2' table is optional, if not found we'll estimate xHeight
  // and aveCharWidth by measuring glyphs
  gfxFontEntry::AutoTable os2Table(mFontEntry, kOS_2TableTag);
  if (os2Table) {
    const OS2Table* os2 =
        reinterpret_cast<const OS2Table*>(hb_blob_get_data(os2Table, &len));
    // this should always be present in any valid OS/2 of any version
    if (len >= offsetof(OS2Table, xAvgCharWidth) + sizeof(int16_t)) {
      SET_SIGNED(aveCharWidth, os2->xAvgCharWidth);
    }
  }

#undef SET_SIGNED
#undef SET_UNSIGNED

  hb_font_t* hbFont = gfxHarfBuzzShaper::CreateHBFont(this);
  hb_position_t position;

  auto FixedToFloat = [](hb_position_t f) -> gfxFloat { return f / 65536.0; };

  if (hb_ot_metrics_get_position(hbFont, HB_OT_METRICS_TAG_HORIZONTAL_ASCENDER,
                                 &position)) {
    aMetrics.maxAscent = FixedToFloat(position);
  }
  if (hb_ot_metrics_get_position(hbFont, HB_OT_METRICS_TAG_HORIZONTAL_DESCENDER,
                                 &position)) {
    aMetrics.maxDescent = -FixedToFloat(position);
  }
  if (hb_ot_metrics_get_position(hbFont, HB_OT_METRICS_TAG_HORIZONTAL_LINE_GAP,
                                 &position)) {
    aMetrics.externalLeading = FixedToFloat(position);
  }

  if (hb_ot_metrics_get_position(hbFont, HB_OT_METRICS_TAG_UNDERLINE_OFFSET,
                                 &position)) {
    aMetrics.underlineOffset = FixedToFloat(position);
  }
  if (hb_ot_metrics_get_position(hbFont, HB_OT_METRICS_TAG_UNDERLINE_SIZE,
                                 &position)) {
    aMetrics.underlineSize = FixedToFloat(position);
  }
  if (hb_ot_metrics_get_position(hbFont, HB_OT_METRICS_TAG_STRIKEOUT_OFFSET,
                                 &position)) {
    aMetrics.strikeoutOffset = FixedToFloat(position);
  }
  if (hb_ot_metrics_get_position(hbFont, HB_OT_METRICS_TAG_STRIKEOUT_SIZE,
                                 &position)) {
    aMetrics.strikeoutSize = FixedToFloat(position);
  }

  // Although sxHeight and sCapHeight are signed fields, we consider
  // zero/negative values to be erroneous and just ignore them.
  if (hb_ot_metrics_get_position(hbFont, HB_OT_METRICS_TAG_X_HEIGHT,
                                 &position) &&
      position > 0) {
    aMetrics.xHeight = FixedToFloat(position);
  }
  if (hb_ot_metrics_get_position(hbFont, HB_OT_METRICS_TAG_CAP_HEIGHT,
                                 &position) &&
      position > 0) {
    aMetrics.capHeight = FixedToFloat(position);
  }
  hb_font_destroy(hbFont);

  mIsValid = true;

  return true;
}

static double RoundToNearestMultiple(double aValue, double aFraction) {
  return floor(aValue / aFraction + 0.5) * aFraction;
}

void gfxFont::CalculateDerivedMetrics(Metrics& aMetrics) {
  aMetrics.maxAscent =
      ceil(RoundToNearestMultiple(aMetrics.maxAscent, 1 / 1024.0));
  aMetrics.maxDescent =
      ceil(RoundToNearestMultiple(aMetrics.maxDescent, 1 / 1024.0));

  if (aMetrics.xHeight <= 0) {
    // only happens if we couldn't find either font metrics
    // or a char to measure;
    // pick an arbitrary value that's better than zero
    aMetrics.xHeight = aMetrics.maxAscent * DEFAULT_XHEIGHT_FACTOR;
  }

  // If we have a font that doesn't provide a capHeight value, use maxAscent
  // as a reasonable fallback.
  if (aMetrics.capHeight <= 0) {
    aMetrics.capHeight = aMetrics.maxAscent;
  }

  aMetrics.maxHeight = aMetrics.maxAscent + aMetrics.maxDescent;

  if (aMetrics.maxHeight - aMetrics.emHeight > 0.0) {
    aMetrics.internalLeading = aMetrics.maxHeight - aMetrics.emHeight;
  } else {
    aMetrics.internalLeading = 0.0;
  }

  aMetrics.emAscent =
      aMetrics.maxAscent * aMetrics.emHeight / aMetrics.maxHeight;
  aMetrics.emDescent = aMetrics.emHeight - aMetrics.emAscent;

  if (GetFontEntry()->IsFixedPitch()) {
    // Some Quartz fonts are fixed pitch, but there's some glyph with a bigger
    // advance than the average character width... this forces
    // those fonts to be recognized like fixed pitch fonts by layout.
    aMetrics.maxAdvance = aMetrics.aveCharWidth;
  }

  if (!aMetrics.strikeoutOffset) {
    aMetrics.strikeoutOffset = aMetrics.xHeight * 0.5;
  }
  if (!aMetrics.strikeoutSize) {
    aMetrics.strikeoutSize = aMetrics.underlineSize;
  }
}

void gfxFont::SanitizeMetrics(gfxFont::Metrics* aMetrics,
                              bool aIsBadUnderlineFont) {
  // Even if this font size is zero, this font is created with non-zero size.
  // However, for layout and others, we should return the metrics of zero size
  // font.
  if (mStyle.AdjustedSizeMustBeZero()) {
    memset(aMetrics, 0, sizeof(gfxFont::Metrics));
    return;
  }

  // If the font entry has ascent/descent/lineGap-override values,
  // replace the metrics from the font with the overrides.
  gfxFloat adjustedSize = GetAdjustedSize();
  if (mFontEntry->mAscentOverride >= 0.0) {
    aMetrics->maxAscent = mFontEntry->mAscentOverride * adjustedSize;
    aMetrics->maxHeight = aMetrics->maxAscent + aMetrics->maxDescent;
    aMetrics->internalLeading =
        std::max(0.0, aMetrics->maxHeight - aMetrics->emHeight);
  }
  if (mFontEntry->mDescentOverride >= 0.0) {
    aMetrics->maxDescent = mFontEntry->mDescentOverride * adjustedSize;
    aMetrics->maxHeight = aMetrics->maxAscent + aMetrics->maxDescent;
    aMetrics->internalLeading =
        std::max(0.0, aMetrics->maxHeight - aMetrics->emHeight);
  }
  if (mFontEntry->mLineGapOverride >= 0.0) {
    aMetrics->externalLeading = mFontEntry->mLineGapOverride * adjustedSize;
  }

  aMetrics->underlineSize = std::max(1.0, aMetrics->underlineSize);
  aMetrics->strikeoutSize = std::max(1.0, aMetrics->strikeoutSize);

  aMetrics->underlineOffset = std::min(aMetrics->underlineOffset, -1.0);

  if (aMetrics->maxAscent < 1.0) {
    // We cannot draw strikeout line and overline in the ascent...
    aMetrics->underlineSize = 0;
    aMetrics->underlineOffset = 0;
    aMetrics->strikeoutSize = 0;
    aMetrics->strikeoutOffset = 0;
    return;
  }

  /**
   * Some CJK fonts have bad underline offset. Therefore, if this is such font,
   * we need to lower the underline offset to bottom of *em* descent.
   * However, if this is system font, we should not do this for the rendering
   * compatibility with another application's UI on the platform.
   * XXX Should not use this hack if the font size is too small?
   *     Such text cannot be read, this might be used for tight CSS
   *     rendering? (E.g., Acid2)
   */
  if (!mStyle.systemFont && aIsBadUnderlineFont) {
    // First, we need 2 pixels between baseline and underline at least. Because
    // many CJK characters put their glyphs on the baseline, so, 1 pixel is too
    // close for CJK characters.
    aMetrics->underlineOffset = std::min(aMetrics->underlineOffset, -2.0);

    // Next, we put the underline to bottom of below of the descent space.
    if (aMetrics->internalLeading + aMetrics->externalLeading >
        aMetrics->underlineSize) {
      aMetrics->underlineOffset =
          std::min(aMetrics->underlineOffset, -aMetrics->emDescent);
    } else {
      aMetrics->underlineOffset =
          std::min(aMetrics->underlineOffset,
                   aMetrics->underlineSize - aMetrics->emDescent);
    }
  }
  // If underline positioned is too far from the text, descent position is
  // preferred so that underline will stay within the boundary.
  else if (aMetrics->underlineSize - aMetrics->underlineOffset >
           aMetrics->maxDescent) {
    if (aMetrics->underlineSize > aMetrics->maxDescent)
      aMetrics->underlineSize = std::max(aMetrics->maxDescent, 1.0);
    // The max underlineOffset is 1px (the min underlineSize is 1px, and min
    // maxDescent is 0px.)
    aMetrics->underlineOffset = aMetrics->underlineSize - aMetrics->maxDescent;
  }

  // If strikeout line is overflowed from the ascent, the line should be resized
  // and moved for that being in the ascent space. Note that the strikeoutOffset
  // is *middle* of the strikeout line position.
  gfxFloat halfOfStrikeoutSize = floor(aMetrics->strikeoutSize / 2.0 + 0.5);
  if (halfOfStrikeoutSize + aMetrics->strikeoutOffset > aMetrics->maxAscent) {
    if (aMetrics->strikeoutSize > aMetrics->maxAscent) {
      aMetrics->strikeoutSize = std::max(aMetrics->maxAscent, 1.0);
      halfOfStrikeoutSize = floor(aMetrics->strikeoutSize / 2.0 + 0.5);
    }
    gfxFloat ascent = floor(aMetrics->maxAscent + 0.5);
    aMetrics->strikeoutOffset = std::max(halfOfStrikeoutSize, ascent / 2.0);
  }

  // If overline is larger than the ascent, the line should be resized.
  if (aMetrics->underlineSize > aMetrics->maxAscent) {
    aMetrics->underlineSize = aMetrics->maxAscent;
  }
}

gfxFont::Baselines gfxFont::GetBaselines(Orientation aOrientation) {
  // Approximated baselines for fonts lacking actual baseline data. These are
  // fractions of the em ascent/descent from the alphabetic baseline.
  const double kHangingBaselineDefault = 0.8;       // fraction of ascent
  const double kIdeographicBaselineDefault = -0.5;  // fraction of descent

  // If no BASE table is present, just return synthetic values immediately.
  if (!mFontEntry->HasFontTable(TRUETYPE_TAG('B', 'A', 'S', 'E'))) {
    // No baseline table; just synthesize them immediately.
    const Metrics& metrics = GetMetrics(aOrientation);
    return Baselines{
        0.0,                                             // alphabetic
        kHangingBaselineDefault * metrics.emAscent,      // hanging
        kIdeographicBaselineDefault * metrics.emDescent  // ideographic
    };
  }

  // Use harfbuzz to try to read the font's baseline metrics.
  Baselines result{NAN, NAN, NAN};
  hb_font_t* hbFont = gfxHarfBuzzShaper::CreateHBFont(this);
  hb_direction_t hbDir = aOrientation == nsFontMetrics::eHorizontal
                             ? HB_DIRECTION_LTR
                             : HB_DIRECTION_TTB;
  hb_position_t position;
  unsigned count = 0;
  auto Fix2Float = [](hb_position_t f) -> gfxFloat { return f / 65536.0; };
  if (hb_ot_layout_get_baseline(hbFont, HB_OT_LAYOUT_BASELINE_TAG_ROMAN, hbDir,
                                HB_OT_TAG_DEFAULT_SCRIPT,
                                HB_OT_TAG_DEFAULT_LANGUAGE, &position)) {
    result.mAlphabetic = Fix2Float(position);
    count++;
  }
  if (hb_ot_layout_get_baseline(hbFont, HB_OT_LAYOUT_BASELINE_TAG_HANGING,
                                hbDir, HB_OT_TAG_DEFAULT_SCRIPT,
                                HB_OT_TAG_DEFAULT_LANGUAGE, &position)) {
    result.mHanging = Fix2Float(position);
    count++;
  }
  if (hb_ot_layout_get_baseline(
          hbFont, HB_OT_LAYOUT_BASELINE_TAG_IDEO_EMBOX_BOTTOM_OR_LEFT, hbDir,
          HB_OT_TAG_DEFAULT_SCRIPT, HB_OT_TAG_DEFAULT_LANGUAGE, &position)) {
    result.mIdeographic = Fix2Float(position);
    count++;
  }
  hb_font_destroy(hbFont);
  // If we successfully read all three, we can return now.
  if (count == 3) {
    return result;
  }

  // Synthesize the baselines that we didn't find in the font.
  const Metrics& metrics = GetMetrics(aOrientation);
  if (std::isnan(result.mAlphabetic)) {
    result.mAlphabetic = 0.0;
  }
  if (std::isnan(result.mHanging)) {
    result.mHanging = kHangingBaselineDefault * metrics.emAscent;
  }
  if (std::isnan(result.mIdeographic)) {
    result.mIdeographic = kIdeographicBaselineDefault * metrics.emDescent;
  }

  return result;
}

// Create a Metrics record to be used for vertical layout. This should never
// fail, as we've already decided this is a valid font. We do not have the
// option of marking it invalid (as can happen if we're unable to read
// horizontal metrics), because that could break a font that we're already
// using for horizontal text.
// So we will synthesize *something* usable here even if there aren't any of the
// usual font tables (which can happen in the case of a legacy bitmap or Type1
// font for which the platform-specific backend used platform APIs instead of
// sfnt tables to create the horizontal metrics).
void gfxFont::CreateVerticalMetrics() {
  const uint32_t kHheaTableTag = TRUETYPE_TAG('h', 'h', 'e', 'a');
  const uint32_t kVheaTableTag = TRUETYPE_TAG('v', 'h', 'e', 'a');
  const uint32_t kPostTableTag = TRUETYPE_TAG('p', 'o', 's', 't');
  const uint32_t kOS_2TableTag = TRUETYPE_TAG('O', 'S', '/', '2');
  uint32_t len;

  auto* metrics = new Metrics();
  ::memset(metrics, 0, sizeof(Metrics));

  // Some basic defaults, in case the font lacks any real metrics tables.
  // TODO: consider what rounding (if any) we should apply to these.
  metrics->emHeight = GetAdjustedSize();
  metrics->emAscent = metrics->emHeight / 2;
  metrics->emDescent = metrics->emHeight - metrics->emAscent;

  metrics->maxAscent = metrics->emAscent;
  metrics->maxDescent = metrics->emDescent;

  const float UNINITIALIZED_LEADING = -10000.0f;
  metrics->externalLeading = UNINITIALIZED_LEADING;

  if (mFUnitsConvFactor < 0.0) {
    uint16_t upem = GetFontEntry()->UnitsPerEm();
    if (upem != gfxFontEntry::kInvalidUPEM) {
      AutoWriteLock lock(mLock);
      mFUnitsConvFactor = GetAdjustedSize() / upem;
    }
  }

#define SET_UNSIGNED(field, src) \
  metrics->field = uint16_t(src) * mFUnitsConvFactor
#define SET_SIGNED(field, src) metrics->field = int16_t(src) * mFUnitsConvFactor

  gfxFontEntry::AutoTable os2Table(mFontEntry, kOS_2TableTag);
  if (os2Table && mFUnitsConvFactor >= 0.0) {
    const OS2Table* os2 =
        reinterpret_cast<const OS2Table*>(hb_blob_get_data(os2Table, &len));
    // These fields should always be present in any valid OS/2 table
    if (len >= offsetof(OS2Table, sTypoLineGap) + sizeof(int16_t)) {
      SET_SIGNED(strikeoutSize, os2->yStrikeoutSize);
      // Use ascent+descent from the horizontal metrics as the default
      // advance (aveCharWidth) in vertical mode
      gfxFloat ascentDescent =
          gfxFloat(mFUnitsConvFactor) *
          (int16_t(os2->sTypoAscender) - int16_t(os2->sTypoDescender));
      metrics->aveCharWidth = std::max(metrics->emHeight, ascentDescent);
      // Use xAvgCharWidth from horizontal metrics as minimum font extent
      // for vertical layout, applying half of it to ascent and half to
      // descent (to work with a default centered baseline).
      gfxFloat halfCharWidth =
          int16_t(os2->xAvgCharWidth) * gfxFloat(mFUnitsConvFactor) / 2;
      metrics->maxAscent = std::max(metrics->maxAscent, halfCharWidth);
      metrics->maxDescent = std::max(metrics->maxDescent, halfCharWidth);
    }
  }

  // If we didn't set aveCharWidth from OS/2, try to read 'hhea' metrics
  // and use the line height from its ascent/descent.
  if (!metrics->aveCharWidth) {
    gfxFontEntry::AutoTable hheaTable(mFontEntry, kHheaTableTag);
    if (hheaTable && mFUnitsConvFactor >= 0.0) {
      const MetricsHeader* hhea = reinterpret_cast<const MetricsHeader*>(
          hb_blob_get_data(hheaTable, &len));
      if (len >= sizeof(MetricsHeader)) {
        SET_SIGNED(aveCharWidth,
                   int16_t(hhea->ascender) - int16_t(hhea->descender));
        metrics->maxAscent = metrics->aveCharWidth / 2;
        metrics->maxDescent = metrics->aveCharWidth - metrics->maxAscent;
      }
    }
  }

  // Read real vertical metrics if available.
  metrics->ideographicWidth = -1.0;
  metrics->zeroWidth = -1.0;
  gfxFontEntry::AutoTable vheaTable(mFontEntry, kVheaTableTag);
  if (vheaTable && mFUnitsConvFactor >= 0.0) {
    const MetricsHeader* vhea = reinterpret_cast<const MetricsHeader*>(
        hb_blob_get_data(vheaTable, &len));
    if (len >= sizeof(MetricsHeader)) {
      SET_UNSIGNED(maxAdvance, vhea->advanceWidthMax);
      // Redistribute space between ascent/descent because we want a
      // centered vertical baseline by default.
      gfxFloat halfExtent =
          0.5 * gfxFloat(mFUnitsConvFactor) *
          (int16_t(vhea->ascender) + std::abs(int16_t(vhea->descender)));
      // Some bogus fonts have ascent and descent set to zero in 'vhea'.
      // In that case we just ignore them and keep our synthetic values
      // from above.
      if (halfExtent > 0) {
        metrics->maxAscent = halfExtent;
        metrics->maxDescent = halfExtent;
        SET_SIGNED(externalLeading, vhea->lineGap);
      }
      // Call gfxHarfBuzzShaper::GetGlyphVAdvance directly, as GetCharAdvance
      // would potentially recurse if no v-advance is available and it attempts
      // to fall back to a value from mVerticalMetrics.
      if (gfxHarfBuzzShaper* shaper = GetHarfBuzzShaper()) {
        uint32_t gid = ProvidesGetGlyph()
                           ? GetGlyph(kWaterIdeograph, 0)
                           : shaper->GetNominalGlyph(kWaterIdeograph);
        if (gid) {
          int32_t advance = shaper->GetGlyphVAdvance(gid);
          // Convert 16.16 fixed-point advance from the shaper to a float.
          metrics->ideographicWidth =
              advance < 0 ? metrics->aveCharWidth : advance / 65536.0;
        }
        gid = ProvidesGetGlyph() ? GetGlyph('0', 0)
                                 : shaper->GetNominalGlyph('0');
        if (gid) {
          int32_t advance = shaper->GetGlyphVAdvance(gid);
          metrics->zeroWidth =
              advance < 0 ? metrics->aveCharWidth : advance / 65536.0;
        }
      }
    }
  }

  // If we didn't set aveCharWidth above, we must be dealing with a non-sfnt
  // font of some kind (Type1, bitmap, vector, ...), so fall back to using
  // whatever the platform backend figured out for horizontal layout.
  // And if we haven't set externalLeading yet, then copy that from the
  // horizontal metrics as well, to help consistency of CSS line-height.
  if (!metrics->aveCharWidth ||
      metrics->externalLeading == UNINITIALIZED_LEADING) {
    const Metrics& horizMetrics = GetHorizontalMetrics();
    if (!metrics->aveCharWidth) {
      metrics->aveCharWidth = horizMetrics.maxAscent + horizMetrics.maxDescent;
    }
    if (metrics->externalLeading == UNINITIALIZED_LEADING) {
      metrics->externalLeading = horizMetrics.externalLeading;
    }
  }

  // Get underline thickness from the 'post' table if available.
  // We also read the underline position, although in vertical-upright mode
  // this will not be appropriate to use directly (see nsTextFrame.cpp).
  gfxFontEntry::AutoTable postTable(mFontEntry, kPostTableTag);
  if (postTable) {
    const PostTable* post =
        reinterpret_cast<const PostTable*>(hb_blob_get_data(postTable, &len));
    if (len >= offsetof(PostTable, underlineThickness) + sizeof(uint16_t)) {
      static_assert(offsetof(PostTable, underlinePosition) <
                        offsetof(PostTable, underlineThickness),
                    "broken PostTable struct?");
      SET_SIGNED(underlineOffset, post->underlinePosition);
      SET_UNSIGNED(underlineSize, post->underlineThickness);
      // Also use for strikeout if we didn't find that in OS/2 above.
      if (!metrics->strikeoutSize) {
        metrics->strikeoutSize = metrics->underlineSize;
      }
    }
  }

#undef SET_UNSIGNED
#undef SET_SIGNED

  // If we didn't read this from a vhea table, it will still be zero.
  // In any case, let's make sure it is not less than the value we've
  // come up with for aveCharWidth.
  metrics->maxAdvance = std::max(metrics->maxAdvance, metrics->aveCharWidth);

  // Thickness of underline and strikeout may have been read from tables,
  // but in case they were not present, ensure a minimum of 1 pixel.
  metrics->underlineSize = std::max(1.0, metrics->underlineSize);

  metrics->strikeoutSize = std::max(1.0, metrics->strikeoutSize);
  metrics->strikeoutOffset = -0.5 * metrics->strikeoutSize;

  // Somewhat arbitrary values for now, subject to future refinement...
  metrics->spaceWidth = metrics->aveCharWidth;
  metrics->maxHeight = metrics->maxAscent + metrics->maxDescent;
  metrics->xHeight = metrics->emHeight / 2;
  metrics->capHeight = metrics->maxAscent;

  if (metrics->zeroWidth < 0.0) {
    metrics->zeroWidth = metrics->aveCharWidth;
  }

  if (!mVerticalMetrics.compareExchange(nullptr, metrics)) {
    delete metrics;
  }
}

gfxFloat gfxFont::SynthesizeSpaceWidth(uint32_t aCh) {
  // return an appropriate width for various Unicode space characters
  // that we "fake" if they're not actually present in the font;
  // returns negative value if the char is not a known space.
  switch (aCh) {
    case 0x2000:  // en quad
    case 0x2002:
      return GetAdjustedSize() / 2;  // en space
    case 0x2001:                     // em quad
    case 0x2003:
      return GetAdjustedSize();  // em space
    case 0x2004:
      return GetAdjustedSize() / 3;  // three-per-em space
    case 0x2005:
      return GetAdjustedSize() / 4;  // four-per-em space
    case 0x2006:
      return GetAdjustedSize() / 6;  // six-per-em space
    case 0x2007:
      return GetMetrics(nsFontMetrics::eHorizontal)
          .ZeroOrAveCharWidth();  // figure space
    case 0x2008:
      return GetMetrics(nsFontMetrics::eHorizontal)
          .spaceWidth;  // punctuation space
    case 0x2009:
      return GetAdjustedSize() / 5;  // thin space
    case 0x200a:
      return GetAdjustedSize() / 10;  // hair space
    case 0x202f:
      return GetAdjustedSize() / 5;  // narrow no-break space
    case 0x3000:
      return GetAdjustedSize();  // ideographic space
    default:
      return -1.0;
  }
}

void gfxFont::AddSizeOfExcludingThis(MallocSizeOf aMallocSizeOf,
                                     FontCacheSizes* aSizes) const {
  AutoReadLock lock(mLock);
  for (uint32_t i = 0; i < mGlyphExtentsArray.Length(); ++i) {
    aSizes->mFontInstances +=
        mGlyphExtentsArray[i]->SizeOfIncludingThis(aMallocSizeOf);
  }
  if (mWordCache) {
    aSizes->mShapedWords +=
        mWordCache->shallowSizeOfIncludingThis(aMallocSizeOf);
    for (auto it = mWordCache->iter(); !it.done(); it.next()) {
      aSizes->mShapedWords +=
          it.get().value()->SizeOfIncludingThis(aMallocSizeOf);
    }
  }
}

void gfxFont::AddSizeOfIncludingThis(MallocSizeOf aMallocSizeOf,
                                     FontCacheSizes* aSizes) const {
  aSizes->mFontInstances += aMallocSizeOf(this);
  AddSizeOfExcludingThis(aMallocSizeOf, aSizes);
}

void gfxFont::AddGlyphChangeObserver(GlyphChangeObserver* aObserver) {
  AutoWriteLock lock(mLock);
  if (!mGlyphChangeObservers) {
    mGlyphChangeObservers = MakeUnique<nsTHashSet<GlyphChangeObserver*>>();
  }
  mGlyphChangeObservers->Insert(aObserver);
}

void gfxFont::RemoveGlyphChangeObserver(GlyphChangeObserver* aObserver) {
  AutoWriteLock lock(mLock);
  NS_ASSERTION(mGlyphChangeObservers, "No observers registered");
  NS_ASSERTION(mGlyphChangeObservers->Contains(aObserver),
               "Observer not registered");
  mGlyphChangeObservers->Remove(aObserver);
}

#define DEFAULT_PIXEL_FONT_SIZE 16.0f

gfxFontStyle::gfxFontStyle()
    : size(DEFAULT_PIXEL_FONT_SIZE),
      sizeAdjust(0.0f),
      baselineOffset(0.0f),
      languageOverride{0},
      weight(FontWeight::NORMAL),
      stretch(FontStretch::NORMAL),
      style(FontSlantStyle::NORMAL),
      variantCaps(NS_FONT_VARIANT_CAPS_NORMAL),
      variantSubSuper(NS_FONT_VARIANT_POSITION_NORMAL),
      sizeAdjustBasis(uint8_t(FontSizeAdjust::Tag::None)),
      systemFont(true),
      printerFont(false),
#ifdef XP_WIN
      allowForceGDIClassic(true),
#endif
      useGrayscaleAntialiasing(false),
      allowSyntheticWeight(true),
      synthesisStyle(StyleFontSynthesisStyle::Auto),
      allowSyntheticSmallCaps(true),
      useSyntheticPosition(true),
      noFallbackVariantFeatures(true) {
}

gfxFontStyle::gfxFontStyle(FontSlantStyle aStyle, FontWeight aWeight,
                           FontStretch aStretch, gfxFloat aSize,
                           const FontSizeAdjust& aSizeAdjust, bool aSystemFont,
                           bool aPrinterFont,
#ifdef XP_WIN
                           bool aAllowForceGDIClassic,
#endif
                           bool aAllowWeightSynthesis,
                           StyleFontSynthesisStyle aStyleSynthesis,
                           bool aAllowSmallCapsSynthesis,
                           bool aUsePositionSynthesis,
                           StyleFontLanguageOverride aLanguageOverride)
    : size(aSize),
      baselineOffset(0.0f),
      languageOverride(aLanguageOverride),
      weight(aWeight),
      stretch(aStretch),
      style(aStyle),
      variantCaps(NS_FONT_VARIANT_CAPS_NORMAL),
      variantSubSuper(NS_FONT_VARIANT_POSITION_NORMAL),
      systemFont(aSystemFont),
      printerFont(aPrinterFont),
#ifdef XP_WIN
      allowForceGDIClassic(aAllowForceGDIClassic),
#endif
      useGrayscaleAntialiasing(false),
      allowSyntheticWeight(aAllowWeightSynthesis),
      synthesisStyle(aStyleSynthesis),
      allowSyntheticSmallCaps(aAllowSmallCapsSynthesis),
      useSyntheticPosition(aUsePositionSynthesis),
      noFallbackVariantFeatures(true) {
  MOZ_ASSERT(!std::isnan(size));

  sizeAdjustBasis = uint8_t(aSizeAdjust.tag);
  // sizeAdjustBasis is currently a small bitfield, so let's assert that the
  // tag value was not truncated.
  MOZ_ASSERT(FontSizeAdjust::Tag(sizeAdjustBasis) == aSizeAdjust.tag,
             "gfxFontStyle.sizeAdjustBasis too small?");

#define HANDLE_TAG(TAG)                 \
  case FontSizeAdjust::Tag::TAG:        \
    sizeAdjust = aSizeAdjust.As##TAG(); \
    break;

  switch (aSizeAdjust.tag) {
    case FontSizeAdjust::Tag::None:
      sizeAdjust = 0.0f;
      break;
      HANDLE_TAG(ExHeight)
      HANDLE_TAG(CapHeight)
      HANDLE_TAG(ChWidth)
      HANDLE_TAG(IcWidth)
      HANDLE_TAG(IcHeight)
  }

#undef HANDLE_TAG

  MOZ_ASSERT(!std::isnan(sizeAdjust));

  if (weight > FontWeight::FromInt(1000)) {
    weight = FontWeight::FromInt(1000);
  }
  if (weight < FontWeight::FromInt(1)) {
    weight = FontWeight::FromInt(1);
  }

  if (size >= FONT_MAX_SIZE) {
    size = FONT_MAX_SIZE;
    sizeAdjust = 0.0f;
    sizeAdjustBasis = uint8_t(FontSizeAdjust::Tag::None);
  } else if (size < 0.0) {
    NS_WARNING("negative font size");
    size = 0.0;
  }
}

PLDHashNumber gfxFontStyle::Hash() const {
  uint32_t hash = variationSettings.IsEmpty()
                      ? 0
                      : mozilla::HashBytes(variationSettings.Elements(),
                                           variationSettings.Length() *
                                               sizeof(gfxFontVariation));
  return mozilla::AddToHash(hash, systemFont, style.Raw(), stretch.Raw(),
                            weight.Raw(), size, int32_t(sizeAdjust * 1000.0f));
}

void gfxFontStyle::AdjustForSubSuperscript(int32_t aAppUnitsPerDevPixel) {
  MOZ_ASSERT(
      variantSubSuper != NS_FONT_VARIANT_POSITION_NORMAL && baselineOffset == 0,
      "can't adjust this style for sub/superscript");

  // calculate the baseline offset (before changing the size)
  if (variantSubSuper == NS_FONT_VARIANT_POSITION_SUPER) {
    baselineOffset = size * -NS_FONT_SUPERSCRIPT_OFFSET_RATIO;
  } else {
    baselineOffset = size * NS_FONT_SUBSCRIPT_OFFSET_RATIO;
  }

  // calculate reduced size, roughly mimicing behavior of font-size: smaller
  float cssSize = size * aAppUnitsPerDevPixel / AppUnitsPerCSSPixel();
  if (cssSize < NS_FONT_SUB_SUPER_SMALL_SIZE) {
    size *= NS_FONT_SUB_SUPER_SIZE_RATIO_SMALL;
  } else if (cssSize >= NS_FONT_SUB_SUPER_LARGE_SIZE) {
    size *= NS_FONT_SUB_SUPER_SIZE_RATIO_LARGE;
  } else {
    gfxFloat t = (cssSize - NS_FONT_SUB_SUPER_SMALL_SIZE) /
                 (NS_FONT_SUB_SUPER_LARGE_SIZE - NS_FONT_SUB_SUPER_SMALL_SIZE);
    size *= (1.0 - t) * NS_FONT_SUB_SUPER_SIZE_RATIO_SMALL +
            t * NS_FONT_SUB_SUPER_SIZE_RATIO_LARGE;
  }

  // clear the variant field
  variantSubSuper = NS_FONT_VARIANT_POSITION_NORMAL;
}

bool gfxFont::TryGetMathTable() {
  if (mMathInitialized) {
    return !!mMathTable;
  }

  auto face(GetFontEntry()->GetHBFace());
  if (hb_ot_math_has_data(face)) {
    auto* mathTable = new gfxMathTable(face, GetAdjustedSize());
    if (!mMathTable.compareExchange(nullptr, mathTable)) {
      delete mathTable;
    }
  }
  mMathInitialized = true;

  return !!mMathTable;
}
