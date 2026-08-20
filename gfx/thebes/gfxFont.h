/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef GFX_FONT_H
#define GFX_FONT_H

#include <functional>
#include <limits>
#include <new>

#include "DrawMode.h"
#include "PLDHashTable.h"
#include "ThebesRLBoxTypes.h"
#include "gfxFontVariations.h"
#include "gfxRect.h"
#include "gfxTypes.h"
#include "harfbuzz/hb-ot.h"
#include "harfbuzz/hb.h"
#include "mozilla/AlreadyAddRefed.h"
#include "mozilla/Attributes.h"
#include "mozilla/FontPropertyTypes.h"
#include "mozilla/HashTable.h"
#include "mozilla/MemoryReporting.h"
#include "mozilla/RWLock.h"
#include "mozilla/RefPtr.h"
#include "mozilla/StaticMutex.h"
#include "mozilla/TypedEnumBits.h"
#include "mozilla/UniquePtr.h"
#include "mozilla/gfx/2D.h"
#include "mozilla/gfx/FontPaletteCache.h"
#include "mozilla/gfx/MatrixFwd.h"
#include "mozilla/gfx/Point.h"
#include "mozilla/intl/UnicodeScriptCodes.h"
#include "nsCOMPtr.h"
#include "nsColor.h"
#include "nsExpirationTracker.h"
#include "nsFontMetrics.h"
#include "nsHashKeys.h"
#include "nsIMemoryReporter.h"
#include "nsIObserver.h"
#include "nsISupports.h"
#include "nsString.h"
#include "nsTArray.h"
#include "nsTHashMap.h"
#include "nsTHashSet.h"
#include "nsTHashtable.h"
#include "nscore.h"

// Only required for function bodies
#include "gfxFontEntry.h"
#include "gfxFontFeatures.h"

class FontVisibilityProvider;
class gfxContext;
class gfxGraphiteShaper;
class gfxHarfBuzzShaper;
class gfxGlyphExtents;
class gfxMathTable;
class gfxPattern;
class gfxShapedText;
class gfxShapedWord;
class gfxSkipChars;
class gfxTextRun;
class nsIEventTarget;
class nsITimer;
struct gfxTextRunDrawCallbacks;

namespace mozilla {
class SVGContextPaint;
namespace layout {
class TextDrawTarget;
}
}  // namespace mozilla

typedef struct _cairo cairo_t;
typedef struct _cairo_scaled_font cairo_scaled_font_t;

#define FONT_MAX_SIZE 2000.0

#define SMALL_CAPS_SCALE_FACTOR 0.8

// The skew factor used for synthetic-italic [oblique] fonts;
// we use a platform-dependent value to harmonize with the platform's own APIs.
#ifdef XP_WIN
#  define OBLIQUE_SKEW_FACTOR 0.3f
#elif defined(MOZ_WIDGET_GTK)
#  define OBLIQUE_SKEW_FACTOR 0.2f
#else
#  define OBLIQUE_SKEW_FACTOR 0.25f
#endif

struct gfxFontStyle {
  using FontWidth = mozilla::FontWidth;
  using FontSlantStyle = mozilla::FontSlantStyle;
  using FontWeight = mozilla::FontWeight;
  using FontSizeAdjust = mozilla::StyleFontSizeAdjust;

  gfxFontStyle();
  gfxFontStyle(FontSlantStyle aStyle, FontWeight aWeight, FontWidth aWidth,
               gfxFloat aSize, const FontSizeAdjust& aSizeAdjust,
               bool aSystemFont, bool aPrinterFont,
#ifdef XP_WIN
               bool aAllowForceGDIClassic,
#endif
               bool aWeightSynthesis,
               mozilla::StyleFontSynthesisStyle aStyleSynthesis,
               bool aSmallCapsSynthesis, bool aPositionSynthesis,
               mozilla::StyleFontLanguageOverride aLanguageOverride);
  // Features are composed of (1) features from style rules (2) features
  // from feature settings rules and (3) family-specific features.  (1) and
  // (3) are guaranteed to be mutually exclusive

  // custom opentype feature settings
  CopyableTArray<gfxFontFeature> featureSettings;

  // Some font-variant property values require font-specific settings
  // defined via @font-feature-values rules.  These are resolved after
  // font matching occurs.

  // -- list of value tags for specific alternate features
  mozilla::StyleFontVariantAlternates variantAlternates;

  // -- object used to look these up once the font is matched
  RefPtr<gfxFontFeatureValueSet> featureValueLookup;

  // opentype variation settings
  CopyableTArray<gfxFontVariation> variationSettings;

  // The logical size of the font, in pixels
  gfxFloat size;

  // The optical size value to apply (if supported); negative means none.
  float autoOpticalSize = -1.0f;

  // The aspect-value (ie., the ratio actualsize:actualxheight) that any
  // actual physical font created from this font structure must have when
  // rendering or measuring a string. A value of -1.0 means no adjustment
  // needs to be done; otherwise the value must be nonnegative.
  float sizeAdjust;

  // baseline offset, used when simulating sub/superscript glyphs
  float baselineOffset;

  // Language system tag, to override document language;
  // an OpenType "language system" tag represented as a 32-bit integer
  // (see http://www.microsoft.com/typography/otspec/languagetags.htm).
  // Normally 0, so font rendering will use the document or element language
  // (see above) to control any language-specific rendering, but the author
  // can override this for cases where the options implemented in the font
  // do not directly match the actual language. (E.g. lang may be Macedonian,
  // but the font in use does not explicitly support this; the author can
  // use font-language-override to request the Serbian option in the font
  // in order to get correct glyph shapes.)
  mozilla::StyleFontLanguageOverride languageOverride;

  // The Font{Weight,Width,SlantStyle} fields are each a 16-bit type.

  // The weight of the font: 100, 200, ... 900.
  FontWeight weight;

  // The width of the font
  FontWidth width;

  // The style of font
  FontSlantStyle style;

  // Whether face-selection properties weight/style/width are all 'normal'
  bool IsNormalStyle() const {
    return weight.IsNormal() && style.IsNormal() && width.IsNormal();
  }

  // We pack these three small-integer fields into a single byte to avoid
  // overflowing an 8-byte boundary [in a 64-bit build] and ending up with
  // 7 bytes of padding at the end of the struct.

  // caps variant (small-caps, petite-caps, etc.)
  uint8_t variantCaps : 3;  // uses range 0..6

  // sub/superscript variant
  uint8_t variantSubSuper : 2;  // uses range 0..2

  // font metric used as basis of font-size-adjust
  uint8_t sizeAdjustBasis : 3;  // uses range 0..4

  // Say that this font is a system font and therefore does not
  // require certain fixup that we do for fonts from untrusted
  // sources.
  bool systemFont : 1;

  // Say that this font is used for print or print preview.
  bool printerFont : 1;

#ifdef XP_WIN
  bool allowForceGDIClassic : 1;
#endif

  // Used to imitate -webkit-font-smoothing: antialiased
  bool useGrayscaleAntialiasing : 1;

  // Whether synthetic styles are allowed (required, in the case of position)
  bool allowSyntheticWeight : 1;
  mozilla::StyleFontSynthesisStyle synthesisStyle : 2;
  bool allowSyntheticSmallCaps : 1;
  bool useSyntheticPosition : 1;

  // some variant features require fallback which complicates the shaping
  // code, so set up a bool to indicate when shaping with fallback is needed
  bool noFallbackVariantFeatures : 1;

  // Return the final adjusted font size for the given aspect ratio.
  // Not meant to be called when sizeAdjustBasis is NONE.
  gfxFloat GetAdjustedSize(gfxFloat aspect) const {
    MOZ_ASSERT(
        FontSizeAdjust::Tag(sizeAdjustBasis) != FontSizeAdjust::Tag::None,
        "Not meant to be called when sizeAdjustBasis is none");
    gfxFloat adjustedSize =
        std::max(NS_round(size * (sizeAdjust / aspect)), 1.0);
    return std::min(adjustedSize, FONT_MAX_SIZE);
  }

  // Some callers want to take a short-circuit path if they can be sure the
  // adjusted size will be zero.
  bool AdjustedSizeMustBeZero() const {
    return size == 0.0 ||
           (FontSizeAdjust::Tag(sizeAdjustBasis) != FontSizeAdjust::Tag::None &&
            sizeAdjust == 0.0);
  }

  PLDHashNumber Hash() const;

  // Adjust this style to simulate sub/superscript (as requested in the
  // variantSubSuper field) using size and baselineOffset instead.
  void AdjustForSubSuperscript(int32_t aAppUnitsPerDevPixel);

  // Should this style cause the given font entry to use synthetic bold?
  bool NeedsSyntheticBold(gfxFontEntry* aFontEntry) const {
    return weight.IsBold() && allowSyntheticWeight &&
           !aFontEntry->SupportsBold();
  }

  bool Equals(const gfxFontStyle& other) const {
    return mozilla::NumbersAreBitwiseIdentical(size, other.size) &&
           (style == other.style) && (weight == other.weight) &&
           (width == other.width) && (variantCaps == other.variantCaps) &&
           (variantSubSuper == other.variantSubSuper) &&
           (allowSyntheticWeight == other.allowSyntheticWeight) &&
           (synthesisStyle == other.synthesisStyle) &&
           (allowSyntheticSmallCaps == other.allowSyntheticSmallCaps) &&
           (useSyntheticPosition == other.useSyntheticPosition) &&
           (systemFont == other.systemFont) &&
           (printerFont == other.printerFont) &&
#ifdef XP_WIN
           (allowForceGDIClassic == other.allowForceGDIClassic) &&
#endif
           (useGrayscaleAntialiasing == other.useGrayscaleAntialiasing) &&
           (baselineOffset == other.baselineOffset) &&
           mozilla::NumbersAreBitwiseIdentical(sizeAdjust, other.sizeAdjust) &&
           (sizeAdjustBasis == other.sizeAdjustBasis) &&
           (featureSettings == other.featureSettings) &&
           (variantAlternates == other.variantAlternates) &&
           (featureValueLookup == other.featureValueLookup) &&
           (variationSettings == other.variationSettings) &&
           (languageOverride == other.languageOverride) &&
           mozilla::NumbersAreBitwiseIdentical(autoOpticalSize,
                                               other.autoOpticalSize);
  }
};

/**
 * Font cache design:
 *
 * The mFonts hashtable contains most fonts, indexed by (gfxFontEntry*, style).
 * It maintains a strong reference to the fonts it contains.
 * Whenever a font is accessed, it is marked as used to move it to a new
 * generation in the tracker to avoid expiration.
 * The expiration tracker will only expire fonts with a single reference, the
 * cache itself. Fonts with more than one reference are marked as used.
 *
 * We're using 3 generations with a ten-second generation interval, so
 * zero-refcount fonts will be deleted 20-30 seconds after their refcount
 * goes to zero, if timer events fire in a timely manner.
 *
 * The font cache also handles timed expiration of cached ShapedWords
 * for "persistent" fonts: it has a repeating timer, and notifies
 * each cached font to "age" its shaped words. The words will be released
 * by the fonts if they get aged three times without being re-used in the
 * meantime.
 *
 * Note that the ShapedWord timeout is much larger than the font timeout,
 * so that in the case of a short-lived font, we'll discard the gfxFont
 * completely, with all its words, and avoid the cost of aging the words
 * individually. That only happens with longer-lived fonts.
 */
struct FontCacheSizes {
  FontCacheSizes() : mFontInstances(0), mShapedWords(0) {}

  size_t mFontInstances;  // memory used by instances of gfxFont subclasses
  size_t mShapedWords;    // memory used by the per-font shapedWord caches
};

class gfxFontCache final
    : public ExpirationTrackerImpl<gfxFont, 3, mozilla::StaticMutex> {
 protected:
  // Expiration tracker implementation.
  enum { FONT_TIMEOUT_SECONDS = 10 };

  typedef mozilla::StaticMutex Lock;
  typedef mozilla::StaticMutexAutoLock AutoLock;

  // This protects the ExpirationTracker tables.
  static Lock gMutex;

  Lock& GetMutex() override { return gMutex; }

  already_AddRefed<ExpirationTrackerObserver> CreateObserver() final {
    return mozilla::MakeAndAddRef<InternalTrackerObserver>()
        .downcast<ExpirationTrackerObserver>();
  }

  class InternalTrackerObserver final : public ExpirationTrackerObserver {
   public:
    explicit InternalTrackerObserver() = default;
    void NotifyHandlerEnd() final;
  };

 public:
  explicit gfxFontCache(nsIEventTarget* aEventTarget);
  ~gfxFontCache();

  enum { SHAPED_WORD_TIMEOUT_SECONDS = 60 };

  /*
   * Get the global gfxFontCache.  You must call Init() before
   * calling this method --- the result will not be null.
   */
  static gfxFontCache* GetCache() { return gGlobalCache; }

  static nsresult Init();
  // It's OK to call this even if Init() has not been called.
  static void Shutdown();

  // Look up a font in the cache. Returns null if there's nothing matching
  // in the cache
  already_AddRefed<gfxFont> Lookup(const gfxFontEntry* aFontEntry,
                                   const gfxFontStyle* aStyle,
                                   const gfxCharacterMap* aUnicodeRangeMap);

  // We created a new font (presumably because Lookup returned null);
  // put it in the cache. The font's refcount should be nonzero. It is
  // allowable to add a new font even if there is one already in the
  // cache with the same key, as we may race with other threads to do
  // the insertion -- in that case we will return the original font,
  // and destroy the new one.
  already_AddRefed<gfxFont> MaybeInsert(gfxFont* aFont);

  bool MaybeDestroy(gfxFont* aFont);

  // Cleans out the hashtable and removes expired fonts waiting for cleanup.
  // Other gfxFont objects may be still in use but they will be pushed
  // into the expiration queues and removed.
  void Flush();

  void FlushShapedWordCaches();
  void NotifyGlyphsChanged();

  void AgeCachedWords();

  void RunWordCacheExpirationTimer() {
    if (!mTimerRunning) {
      AutoLock lock(gMutex);
      if (!mTimerRunning && mWordCacheExpirationTimer) {
        mWordCacheExpirationTimer->InitWithNamedFuncCallback(
            WordCacheExpirationTimerCallback, this,
            SHAPED_WORD_TIMEOUT_SECONDS * 1000, nsITimer::TYPE_REPEATING_SLACK,
            "gfxFontCache::WordCacheExpiration"_ns);
        mTimerRunning = true;
      }
    }
  }
  void PauseWordCacheExpirationTimer() {
    if (mTimerRunning) {
      AutoLock lock(gMutex);
      if (mTimerRunning && mWordCacheExpirationTimer) {
        mWordCacheExpirationTimer->Cancel();
        mTimerRunning = false;
      }
    }
  }

  void AddSizeOfExcludingThis(mozilla::MallocSizeOf aMallocSizeOf,
                              FontCacheSizes* aSizes) const;
  void AddSizeOfIncludingThis(mozilla::MallocSizeOf aMallocSizeOf,
                              FontCacheSizes* aSizes) const;

 protected:
  class MemoryReporter final : public nsIMemoryReporter {
    ~MemoryReporter() = default;

   public:
    NS_DECL_ISUPPORTS
    NS_DECL_NSIMEMORYREPORTER
  };

  // Observer for notifications that the font cache cares about
  class Observer final : public nsIObserver {
    ~Observer() = default;

   public:
    NS_DECL_ISUPPORTS
    NS_DECL_NSIOBSERVER
  };

  nsresult AddObject(gfxFont* aFont) {
    AutoLock lock(gMutex);
    return AddObjectLocked(aFont, lock);
  }

  // This gets called when the timeout has expired on a single-refcount
  // font; we just delete it.
  void NotifyExpiredLocked(gfxFont* aFont, const AutoLock&)
      MOZ_REQUIRES(gMutex) override;

  static void DestroyDiscard(nsTArray<gfxFont*>& aDiscard);

  static gfxFontCache* gGlobalCache;

  struct MOZ_STACK_CLASS Key {
    const gfxFontEntry* mFontEntry;
    const gfxFontStyle* mStyle;
    const gfxCharacterMap* mUnicodeRangeMap;
    Key(const gfxFontEntry* aFontEntry, const gfxFontStyle* aStyle,
        const gfxCharacterMap* aUnicodeRangeMap)
        : mFontEntry(aFontEntry),
          mStyle(aStyle),
          mUnicodeRangeMap(aUnicodeRangeMap) {}
  };

  class HashEntry : public PLDHashEntryHdr {
   public:
    typedef const Key& KeyType;
    typedef const Key* KeyTypePointer;

    // When constructing a new entry in the hashtable, we'll leave this
    // blank. The caller of Put() will fill this in.
    explicit HashEntry(KeyTypePointer aStr) {}

    bool KeyEquals(const KeyTypePointer aKey) const;
    static KeyTypePointer KeyToPointer(KeyType aKey) { return &aKey; }
    static PLDHashNumber HashKey(const KeyTypePointer aKey) {
      return mozilla::HashGeneric(aKey->mStyle->Hash(), aKey->mFontEntry,
                                  aKey->mUnicodeRangeMap);
    }
    enum { ALLOW_MEMMOVE = true };

    gfxFont* MOZ_UNSAFE_REF("tracking for deferred deletion") mFont = nullptr;
  };

  nsTHashtable<HashEntry> mFonts MOZ_GUARDED_BY(gMutex);

  nsTArray<gfxFont*> mTrackerDiscard MOZ_GUARDED_BY(gMutex);

  static void WordCacheExpirationTimerCallback(nsITimer* aTimer, void* aCache);

  nsCOMPtr<nsITimer> mWordCacheExpirationTimer MOZ_GUARDED_BY(gMutex);
  std::atomic<bool> mTimerRunning = false;
};

class gfxTextPerfMetrics {
 public:
  struct TextCounts {
    uint32_t numContentTextRuns;
    uint32_t numChromeTextRuns;
    uint32_t numChars;
    uint32_t maxTextRunLen;
    uint32_t wordCacheSpaceRules;
    uint32_t wordCacheLong;
    uint32_t wordCacheHit;
    uint32_t wordCacheMiss;
    uint32_t fallbackPrefs;
    uint32_t fallbackSystem;
    uint32_t textrunConst;
    uint32_t textrunDestr;
    uint32_t genericLookups;
  };

  uint32_t reflowCount;

  // counts per reflow operation
  TextCounts current;

  // totals for the lifetime of a document
  TextCounts cumulative;

  gfxTextPerfMetrics() { memset(this, 0, sizeof(gfxTextPerfMetrics)); }

  // add current totals to cumulative ones
  void Accumulate() {
    if (current.numChars == 0) {
      return;
    }
    cumulative.numContentTextRuns += current.numContentTextRuns;
    cumulative.numChromeTextRuns += current.numChromeTextRuns;
    cumulative.numChars += current.numChars;
    if (current.maxTextRunLen > cumulative.maxTextRunLen) {
      cumulative.maxTextRunLen = current.maxTextRunLen;
    }
    cumulative.wordCacheSpaceRules += current.wordCacheSpaceRules;
    cumulative.wordCacheLong += current.wordCacheLong;
    cumulative.wordCacheHit += current.wordCacheHit;
    cumulative.wordCacheMiss += current.wordCacheMiss;
    cumulative.fallbackPrefs += current.fallbackPrefs;
    cumulative.fallbackSystem += current.fallbackSystem;
    cumulative.textrunConst += current.textrunConst;
    cumulative.textrunDestr += current.textrunDestr;
    cumulative.genericLookups += current.genericLookups;
    memset(&current, 0, sizeof(current));
  }
};

namespace mozilla {
namespace gfx {

class UnscaledFont;

// Flags that live in the gfxShapedText::mFlags field.
// (Note that gfxTextRun has an additional mFlags2 field for use
// by textrun clients like nsTextFrame.)
//
// If you add a flag, please add support for it in gfxTextRun::Dump.
enum class ShapedTextFlags : uint16_t {
  /**
   * When set, the text is RTL.
   */
  TEXT_IS_RTL = 0x0001,
  /**
   * When set, spacing is enabled and the textrun needs to call GetSpacing
   * on the spacing provider.
   */
  TEXT_ENABLE_SPACING = 0x0002,
  /**
   * When set, the text has no characters above 255 and it is stored
   * in the textrun in 8-bit format.
   */
  TEXT_IS_8BIT = 0x0004,
  /**
   * When set, GetHyphenationBreaks may return true for some character
   * positions, otherwise it will always return false for all characters.
   */
  TEXT_ENABLE_HYPHEN_BREAKS = 0x0008,
  /**
   * When set, the RunMetrics::mBoundingBox field will be initialized
   * properly based on glyph extents, in particular, glyph extents that
   * overflow the standard font-box (the box defined by the ascent, descent
   * and advance width of the glyph). When not set, it may just be the
   * standard font-box even if glyphs overflow.
   */
  TEXT_NEED_BOUNDING_BOX = 0x0010,
  /**
   * When set, optional ligatures are disabled. Ligatures that are
   * required for legible text should still be enabled.
   */
  TEXT_DISABLE_OPTIONAL_LIGATURES = 0x0020,
  /**
   * When set, the textrun should favour speed of construction over
   * quality. This may involve disabling ligatures and/or kerning or
   * other effects.
   */
  TEXT_OPTIMIZE_SPEED = 0x0040,
  /**
   * When set, the textrun should discard control characters instead of
   * turning them into hexboxes.
   */
  TEXT_HIDE_CONTROL_CHARACTERS = 0x0080,

  /**
   * nsTextFrameThebes sets these, but they're defined here rather than
   * in nsTextFrameUtils.h because ShapedWord creation/caching also needs
   * to check the _INCOMING flag
   */
  TEXT_TRAILING_ARABICCHAR = 0x0100,
  /**
   * When set, the previous character for this textrun was an Arabic
   * character.  This is used for the context detection necessary for
   * bidi.numeral implementation.
   */
  TEXT_INCOMING_ARABICCHAR = 0x0200,

  /**
   * Set if the textrun should use the OpenType 'math' script.
   */
  TEXT_USE_MATH_SCRIPT = 0x0400,

  /*
   * Bit 0x0800 is currently unused.
   */

  /**
   * Field for orientation of the textrun and glyphs within it.
   * Possible values of the TEXT_ORIENT_MASK field:
   *   TEXT_ORIENT_HORIZONTAL
   *   TEXT_ORIENT_VERTICAL_UPRIGHT
   *   TEXT_ORIENT_VERTICAL_SIDEWAYS_RIGHT
   *   TEXT_ORIENT_VERTICAL_SIDEWAYS_LEFT
   *   TEXT_ORIENT_VERTICAL_MIXED
   * For all VERTICAL settings, the x and y coordinates of glyph
   * positions are exchanged, so that simple advances are vertical.
   *
   * The MIXED value indicates vertical textRuns for which the CSS
   * text-orientation property is 'mixed', but is never used for
   * individual glyphRuns; it will be resolved to either UPRIGHT
   * or SIDEWAYS_RIGHT according to the UTR50 properties of the
   * characters, and separate glyphRuns created for the resulting
   * glyph orientations.
   */
  TEXT_ORIENT_MASK = 0x7000,
  TEXT_ORIENT_HORIZONTAL = 0x0000,
  TEXT_ORIENT_VERTICAL_UPRIGHT = 0x1000,
  TEXT_ORIENT_VERTICAL_SIDEWAYS_RIGHT = 0x2000,
  TEXT_ORIENT_VERTICAL_MIXED = 0x3000,
  TEXT_ORIENT_VERTICAL_SIDEWAYS_LEFT = 0x4000,
};

MOZ_MAKE_ENUM_CLASS_BITWISE_OPERATORS(ShapedTextFlags)
}  // namespace gfx
}  // namespace mozilla

class gfxTextRunFactory {
  // Used by stylo
  NS_INLINE_DECL_THREADSAFE_REFCOUNTING(gfxTextRunFactory)

 public:
  typedef mozilla::gfx::DrawTarget DrawTarget;

  /**
   * This record contains all the parameters needed to initialize a textrun.
   */
  struct MOZ_STACK_CLASS Parameters {
    // Shape text params suggesting where the textrun will be rendered
    DrawTarget* mDrawTarget;
    // Pointer to arbitrary user data (which should outlive the textrun)
    void* mUserData;
    // A description of which characters have been stripped from the original
    // DOM string to produce the characters in the textrun. May be null
    // if that information is not relevant.
    gfxSkipChars* mSkipChars;
    // A list of where linebreaks are currently placed in the textrun. May
    // be null if mInitialBreakCount is zero.
    uint32_t* mInitialBreaks;
    uint32_t mInitialBreakCount;
    // The ratio to use to convert device pixels to application layout units
    int32_t mAppUnitsPerDevUnit;
  };

 protected:
  // Protected destructor, to discourage deletion outside of Release():
  virtual ~gfxTextRunFactory();
};

/**
 * gfxFontShaper
 *
 * This class implements text shaping (character to glyph mapping and
 * glyph layout). There is a gfxFontShaper subclass for each text layout
 * technology (uniscribe, core text, harfbuzz,....) we support.
 *
 * The shaper is responsible for setting up glyph data in gfxTextRuns.
 *
 * A generic, platform-independent shaper relies only on the standard
 * gfxFont interface and can work with any concrete subclass of gfxFont.
 *
 * Platform-specific implementations designed to interface to platform
 * shaping APIs such as Uniscribe or CoreText may rely on features of a
 * specific font subclass to access native font references
 * (such as CTFont, HFONT, DWriteFont, etc).
 */

class gfxFontShaper {
 public:
  typedef mozilla::gfx::DrawTarget DrawTarget;
  typedef mozilla::intl::Script Script;

  enum class RoundingFlags : uint8_t { kRoundX = 0x01, kRoundY = 0x02 };

  explicit gfxFontShaper(gfxFont* aFont) : mFont(aFont) {
    NS_ASSERTION(aFont, "shaper requires a valid font!");
  }

  virtual ~gfxFontShaper() = default;

  // Shape a piece of text and store the resulting glyph data into
  // aShapedText. Parameters aOffset/aLength indicate the range of
  // aShapedText to be updated; aLength is also the length of aText.
  virtual bool ShapeText(const char16_t* aText, uint32_t aOffset,
                         uint32_t aLength, Script aScript,
                         nsAtom* aLanguage,  // may be null, indicating no
                                             // lang-specific shaping to be
                                             // applied
                         bool aVertical, RoundingFlags aRounding,
                         gfxShapedText* aShapedText) = 0;

  gfxFont* GetFont() const { return mFont; }

  static void MergeFontFeatures(
      const gfxFontStyle* aStyle, const nsTArray<gfxFontFeature>& aFontFeatures,
      bool aDisableLigatures, const nsACString& aFamilyName, bool aAddSmallCaps,
      void (*aHandleFeature)(uint32_t, uint32_t, void*),
      void* aHandleFeatureData);

 protected:
  // the font this shaper is working with. The font owns a UniquePtr reference
  // to this object, and will destroy it before it dies. Thus, mFont will always
  // be valid.
  gfxFont* MOZ_NON_OWNING_REF mFont;
};

MOZ_MAKE_ENUM_CLASS_BITWISE_OPERATORS(gfxFontShaper::RoundingFlags)

/*
 * gfxShapedText is an abstract superclass for gfxShapedWord and gfxTextRun.
 * These are objects that store a list of zero or more glyphs for each
 * character. For each glyph we store the glyph ID, the advance, and possibly
 * x/y-offsets. The idea is that a string is rendered by a loop that draws each
 * glyph at its designated offset from the current point, then advances the
 * current point by the glyph's advance in the direction of the textrun (LTR or
 * RTL). Each glyph advance is always rounded to the nearest appunit; this
 * ensures consistent results when dividing the text in a textrun into multiple
 * text frames (frame boundaries are always aligned to appunits). We optimize
 * for the case where a character has a single glyph and zero xoffset and
 * yoffset, and the glyph ID and advance are in a reasonable range so we can
 * pack all necessary data into 32 bits.
 *
 * gfxFontShaper can shape text into either a gfxShapedWord (cached by a
 * gfxFont) or directly into a gfxTextRun (for cases where we want to shape
 * textruns in their entirety rather than using cached words, because there may
 * be layout features that depend on the inter-word spaces).
 */
class gfxShapedText {
 public:
  typedef mozilla::intl::Script Script;

  gfxShapedText(uint32_t aLength, mozilla::gfx::ShapedTextFlags aFlags,
                uint16_t aAppUnitsPerDevUnit)
      : mLength(aLength),
        mFlags(aFlags),
        mAppUnitsPerDevUnit(aAppUnitsPerDevUnit) {}

  virtual ~gfxShapedText() = default;

  /**
   * This class records the information associated with a character in the
   * input string. It's optimized for the case where there is one glyph
   * representing that character alone.
   *
   * A character can have zero or more associated glyphs. Each glyph
   * has an advance width and an x and y offset.
   * A character may be the start of a cluster.
   * A character may be the start of a ligature group.
   * A character can be "missing", indicating that the system is unable
   * to render the character.
   *
   * All characters in a ligature group conceptually share all the glyphs
   * associated with the characters in a group.
   */
  class CompressedGlyph {
   public:
    enum Flags : uint32_t {
      // Indicates that a cluster and ligature group starts at this
      // character; this character has a single glyph with a reasonable
      // advance and zero offsets. A "reasonable" advance
      // is one that fits in the available bits (currently 12) (specified
      // in appunits).
      FLAG_IS_SIMPLE_GLYPH = 0x80000000U,

      // These flags are applicable to both "simple" and "complex" records.
      COMMON_FLAGS_MASK = 0x70000000U,

      // Indicates whether a linebreak is allowed before this character;
      // this is a two-bit field that holds a FLAG_BREAK_TYPE_xxx value
      // indicating the kind of linebreak (if any) allowed here.
      FLAGS_CAN_BREAK_BEFORE = 0x60000000U,

      FLAGS_CAN_BREAK_SHIFT = 29U,
      FLAG_BREAK_TYPE_NONE = 0U,
      FLAG_BREAK_TYPE_NORMAL = 1U,
      FLAG_BREAK_TYPE_HYPHEN = 2U,
      // Allow break before this position if needed to avoid overflow:
      FLAG_BREAK_TYPE_EMERGENCY_WRAP = 3U,

      FLAG_CHAR_IS_SPACE = 0x10000000U,

      // Fields present only when FLAG_IS_SIMPLE_GLYPH is /true/.
      // The advance is stored in appunits as a 12-bit field:
      ADVANCE_MASK = 0x0FFF0000U,
      ADVANCE_SHIFT = 16,
      // and the glyph ID is stored in the low 16 bits.
      GLYPH_MASK = 0x0000FFFFU,

      // Fields present only when FLAG_IS_SIMPLE_GLYPH is /false/.
      // Non-simple glyphs may or may not have glyph data in the
      // corresponding mDetailedGlyphs entry. They have a glyph count
      // stored in the low 16 bits, and the following flag bits:
      GLYPH_COUNT_MASK = 0x0000FFFFU,

      // When NOT set, indicates that this character corresponds to a
      // missing glyph and should be skipped (or possibly, render the character
      // Unicode value in some special way). If there are glyphs,
      // the mGlyphID is actually the UTF16 character code. The bit is
      // inverted so we can memset the array to zero to indicate all missing.
      FLAG_NOT_MISSING = 0x010000U,
      FLAG_NOT_CLUSTER_START = 0x020000U,
      FLAG_NOT_LIGATURE_GROUP_START = 0x040000U,

      // This is a "merged glyph" representing multiple characters that result
      // from text-run transformation; if there is letter-spacing, it should be
      // applied between the detailed-glyph components.
      // Currently the only use of this is when es-zet is uppercased as SS.
      //
      // Note that in general, letter-spacing does not depend on this flag; it
      // is applied *between* CompressedGlyph records by textrun drawing and
      // measuring code. This flag only refers to letter-spacing between the
      // individual DetailedGlyph components of a single CompressedGlyph.
      FLAG_APPLY_LETTER_SPACING_BETWEEN_DETAILED_GLYPHS = 0x080000U,

      // Certain types of characters are marked so that they can be given
      // special treatment in rendering. This may require use of a "complex"
      // CompressedGlyph record even for a character that would otherwise be
      // treated as "simple".
      CHAR_TYPE_FLAGS_MASK = 0xF00000U,
      FLAG_CHAR_IS_TAB = 0x100000U,
      FLAG_CHAR_IS_NEWLINE = 0x200000U,
      // Per CSS Text Decoration Module Level 3, emphasis marks are not
      // drawn for any character in Unicode categories Z*, Cc, Cf, and Cn
      // which is not combined with any combining characters. This flag is
      // set for all those characters except 0x20 whitespace.
      FLAG_CHAR_NO_EMPHASIS_MARK = 0x400000U,
      // Per CSS Text, letter-spacing is not applied to formatting chars
      // (category Cf). We mark those in the textrun so as to be able to
      // skip them when setting up spacing in nsTextFrame.
      FLAG_CHAR_IS_FORMATTING_CONTROL = 0x800000U,

      // The bits 0x0F000000 are currently unused in non-simple glyphs.
    };

    // "Simple glyphs" have a simple glyph ID, simple advance and their
    // x and y offsets are zero. Also the glyph extents do not overflow
    // the font-box defined by the font ascent, descent and glyph advance width.
    // These case is optimized to avoid storing DetailedGlyphs.

    // Returns true if the glyph ID aGlyph fits into the compressed
    // representation
    static bool IsSimpleGlyphID(uint32_t aGlyph) {
      return (aGlyph & GLYPH_MASK) == aGlyph;
    }
    // Returns true if the advance aAdvance fits into the compressed
    // representation. aAdvance is in appunits.
    static bool IsSimpleAdvance(uint32_t aAdvance) {
      return (aAdvance & (ADVANCE_MASK >> ADVANCE_SHIFT)) == aAdvance;
    }

    bool IsSimpleGlyph() const { return mValue & FLAG_IS_SIMPLE_GLYPH; }
    uint32_t GetSimpleAdvance() const {
      MOZ_ASSERT(IsSimpleGlyph());
      return (mValue & ADVANCE_MASK) >> ADVANCE_SHIFT;
    }
    uint32_t GetSimpleGlyph() const {
      MOZ_ASSERT(IsSimpleGlyph());
      return mValue & GLYPH_MASK;
    }

    bool IsSimpleGlyphNoBreakBefore() const {
      return (mValue & (FLAG_IS_SIMPLE_GLYPH | FLAGS_CAN_BREAK_BEFORE)) ==
             FLAG_IS_SIMPLE_GLYPH;
    }

    bool IsMissing() const {
      return !(mValue & (FLAG_NOT_MISSING | FLAG_IS_SIMPLE_GLYPH));
    }
    bool IsClusterStart() const {
      return IsSimpleGlyph() || !(mValue & FLAG_NOT_CLUSTER_START);
    }
    bool IsLigatureGroupStart() const {
      return IsSimpleGlyph() || !(mValue & FLAG_NOT_LIGATURE_GROUP_START);
    }
    bool IsLigatureContinuation() const {
      return !IsSimpleGlyph() &&
             (mValue & (FLAG_NOT_LIGATURE_GROUP_START | FLAG_NOT_MISSING)) ==
                 (FLAG_NOT_LIGATURE_GROUP_START | FLAG_NOT_MISSING);
    }

    // Return true if the original character was a normal (breakable,
    // trimmable) space (U+0020). Not true for other characters that
    // may happen to map to the space glyph (U+00A0).
    bool CharIsSpace() const { return mValue & FLAG_CHAR_IS_SPACE; }

    bool CharIsTab() const {
      return !IsSimpleGlyph() && (mValue & FLAG_CHAR_IS_TAB);
    }
    bool CharIsNewline() const {
      return !IsSimpleGlyph() && (mValue & FLAG_CHAR_IS_NEWLINE);
    }
    bool CharMayHaveEmphasisMark() const {
      return !CharIsSpace() &&
             (IsSimpleGlyph() || !(mValue & FLAG_CHAR_NO_EMPHASIS_MARK));
    }
    bool CharIsFormattingControl() const {
      return !IsSimpleGlyph() && (mValue & FLAG_CHAR_IS_FORMATTING_CONTROL);
    }

    uint32_t CharTypeFlags() const {
      return IsSimpleGlyph() ? 0 : (mValue & CHAR_TYPE_FLAGS_MASK);
    }

    void SetClusterStart(bool aIsClusterStart) {
      MOZ_ASSERT(!IsSimpleGlyph());
      if (aIsClusterStart) {
        mValue &= ~FLAG_NOT_CLUSTER_START;
      } else {
        mValue |= FLAG_NOT_CLUSTER_START;
      }
    }

    uint8_t CanBreakBefore() const {
      return (mValue & FLAGS_CAN_BREAK_BEFORE) >> FLAGS_CAN_BREAK_SHIFT;
    }
    // Returns any break-before flags that were modifed; 0 if nothing changed.
    uint32_t SetCanBreakBefore(uint8_t aCanBreakBefore) {
      MOZ_ASSERT(aCanBreakBefore <= 3, "Bogus break-flags value!");
      // Shift the input value to the relevant bit positions.
      uint32_t breakMask = uint32_t(aCanBreakBefore) << FLAGS_CAN_BREAK_SHIFT;
      // Determine which bits in the break field are changing (if any).
      uint32_t toggle = (breakMask ^ mValue) & FLAGS_CAN_BREAK_BEFORE;
      // Update the value, and return the bits that changed.
      mValue ^= toggle;
      return toggle;
    }

    // Create a CompressedGlyph value representing a simple glyph with
    // no extra flags (line-break or is_space) set.
    static CompressedGlyph MakeSimpleGlyph(uint32_t aAdvanceAppUnits,
                                           uint32_t aGlyph) {
      MOZ_ASSERT(IsSimpleAdvance(aAdvanceAppUnits));
      MOZ_ASSERT(IsSimpleGlyphID(aGlyph));
      CompressedGlyph g;
      g.mValue =
          FLAG_IS_SIMPLE_GLYPH | (aAdvanceAppUnits << ADVANCE_SHIFT) | aGlyph;
      return g;
    }

    // Assign a simple glyph value to an existing CompressedGlyph record,
    // preserving line-break/is-space flags if present.
    CompressedGlyph& SetSimpleGlyph(uint32_t aAdvanceAppUnits,
                                    uint32_t aGlyph) {
      MOZ_ASSERT(!CharTypeFlags(), "Char type flags lost");
      mValue = (mValue & COMMON_FLAGS_MASK) |
               MakeSimpleGlyph(aAdvanceAppUnits, aGlyph).mValue;
      return *this;
    }

    // Create a CompressedGlyph value representing a complex glyph record,
    // without any line-break or char-type flags.
    static CompressedGlyph MakeComplex(bool aClusterStart,
                                       bool aLigatureStart) {
      CompressedGlyph g;
      g.mValue = FLAG_NOT_MISSING |
                 (aClusterStart ? 0 : FLAG_NOT_CLUSTER_START) |
                 (aLigatureStart ? 0 : FLAG_NOT_LIGATURE_GROUP_START);
      return g;
    }

    // Assign a complex glyph value to an existing CompressedGlyph record,
    // preserving line-break/char-type flags if present.
    // This sets the glyphCount to zero; it will be updated when we call
    // gfxShapedText::SetDetailedGlyphs.
    CompressedGlyph& SetComplex(bool aClusterStart, bool aLigatureStart) {
      mValue = (mValue & COMMON_FLAGS_MASK) | CharTypeFlags() |
               MakeComplex(aClusterStart, aLigatureStart).mValue;
      return *this;
    }

    /**
     * Mark a glyph record as being a missing-glyph.
     * Missing glyphs are treated as ligature group starts; don't mess with
     * the cluster-start flag (see bugs 618870 and 619286).
     * We also preserve the glyph count here, as this is used after any
     * required DetailedGlyphs (to store the char code for a hexbox) has been
     * set up.
     * This must be called *after* SetDetailedGlyphs is used for the relevant
     * offset in the shaped-word, because that will mark it as not-missing.
     */
    CompressedGlyph& SetMissing() {
      MOZ_ASSERT(!IsSimpleGlyph());
      mValue &= ~(FLAG_NOT_MISSING | FLAG_NOT_LIGATURE_GROUP_START);
      return *this;
    }

    uint32_t GetGlyphCount() const {
      MOZ_ASSERT(!IsSimpleGlyph());
      return mValue & GLYPH_COUNT_MASK;
    }
    void SetGlyphCount(uint32_t aGlyphCount) {
      MOZ_ASSERT(!IsSimpleGlyph());
      MOZ_ASSERT(GetGlyphCount() == 0, "Glyph count already set");
      MOZ_ASSERT(aGlyphCount <= 0xffff, "Glyph count out of range");
      mValue |= FLAG_NOT_MISSING | aGlyphCount;
    }

    void SetIsSpace() { mValue |= FLAG_CHAR_IS_SPACE; }
    void SetIsTab() {
      MOZ_ASSERT(!IsSimpleGlyph());
      mValue |= FLAG_CHAR_IS_TAB;
    }
    void SetIsNewline() {
      MOZ_ASSERT(!IsSimpleGlyph());
      mValue |= FLAG_CHAR_IS_NEWLINE;
    }
    void SetNoEmphasisMark() {
      MOZ_ASSERT(!IsSimpleGlyph());
      mValue |= FLAG_CHAR_NO_EMPHASIS_MARK;
    }
    void SetIsFormattingControl() {
      MOZ_ASSERT(!IsSimpleGlyph());
      mValue |= FLAG_CHAR_IS_FORMATTING_CONTROL;
    }

    void SetApplyLetterSpacingBetweenDetailedGlyphs() {
      MOZ_ASSERT(!IsSimpleGlyph());
      mValue |= FLAG_APPLY_LETTER_SPACING_BETWEEN_DETAILED_GLYPHS;
    }
    bool ApplyLetterSpacingBetweenDetailedGlyphs() const {
      return !IsSimpleGlyph() &&
             (mValue & FLAG_APPLY_LETTER_SPACING_BETWEEN_DETAILED_GLYPHS);
    }

    // Clear a glyph record to "missing", preserving line-break, clustering,
    // and character-type flags if present.
    void ClearGlyph() {
      if (IsSimpleGlyph()) {
        // Clear everything except the COMMON flags; this includes clearing
        // FLAG_IS_SIMPLE_GLYPH, so the record becomes "complex, missing".
        mValue &= COMMON_FLAGS_MASK;
      } else {
        // Clear the GLYPH_COUNT_MASK field and the NOT_MISSING and
        // NOT_LIGATURE_GROUP_START flags, but leave other flags (clusters,
        // line-breaks, char-type) intact.
        mValue &= ~(GLYPH_COUNT_MASK | FLAG_NOT_MISSING |
                    FLAG_NOT_LIGATURE_GROUP_START);
      }
    }

   private:
    uint32_t mValue;
  };

  // Accessor for the array of CompressedGlyph records, which will be in
  // a different place in gfxShapedWord vs gfxTextRun
  virtual const CompressedGlyph* GetCharacterGlyphs() const = 0;
  virtual CompressedGlyph* GetCharacterGlyphs() = 0;

  /**
   * When the glyphs for a character don't fit into a CompressedGlyph record
   * in SimpleGlyph format, we use an array of DetailedGlyphs instead.
   */
  struct DetailedGlyph {
    // The glyphID, or the Unicode character if this is a missing glyph
    uint32_t mGlyphID;
    // The advance of the glyph, in appunits.
    // mAdvance is in the text direction (RTL or LTR),
    // and will normally be non-negative (although this is not guaranteed)
    int32_t mAdvance;
    // The offset from the glyph's default position, in line-relative
    // coordinates (so mOffset.x is an offset in the line-right direction,
    // and mOffset.y is an offset in line-downwards direction).
    // These values are in floating-point appUnits.
    mozilla::gfx::Point mOffset;
  };

  // Store DetailedGlyph records for the given index. (This does not modify
  // the associated CompressedGlyph character-type or break flags.)
  void SetDetailedGlyphs(uint32_t aIndex, uint32_t aGlyphCount,
                         const DetailedGlyph* aGlyphs);

  void SetMissingGlyph(uint32_t aIndex, uint32_t aChar, gfxFont* aFont);

  void SetIsSpace(uint32_t aIndex) {
    GetCharacterGlyphs()[aIndex].SetIsSpace();
  }

  bool HasDetailedGlyphs() const { return mDetailedGlyphs != nullptr; }

  bool IsLigatureGroupStart(uint32_t aPos) {
    NS_ASSERTION(aPos < GetLength(), "aPos out of range");
    return GetCharacterGlyphs()[aPos].IsLigatureGroupStart();
  }

  // NOTE that this must not be called for a character offset that does
  // not have any DetailedGlyph records; callers must have verified that
  // GetCharacterGlyphs()[aCharIndex].GetGlyphCount() is greater than zero.
  DetailedGlyph* GetDetailedGlyphs(uint32_t aCharIndex, uint32_t aCount) const {
    MOZ_ASSERT(GetCharacterGlyphs() && HasDetailedGlyphs() &&
                   !GetCharacterGlyphs()[aCharIndex].IsSimpleGlyph() &&
                   GetCharacterGlyphs()[aCharIndex].GetGlyphCount() == aCount &&
                   aCount > 0,
               "invalid use of GetDetailedGlyphs; check the caller!");
    return mDetailedGlyphs->Get(aCharIndex, aCount);
  }

  void ApplyTrackingToClusters(gfxFloat aTrackingAdjustment, uint32_t aOffset,
                               uint32_t aLength);

  // Mark clusters in the CompressedGlyph records, starting at aOffset,
  // based on the Unicode properties of the text in aString.
  // This is also responsible to set the IsSpace flag for space characters.
  void SetupClusterBoundaries(uint32_t aOffset, const char16_t* aString,
                              uint32_t aLength);
  // In 8-bit text, there won't actually be any clusters, but we still need
  // the space-marking functionality.
  void SetupClusterBoundaries(uint32_t aOffset, const uint8_t* aString,
                              uint32_t aLength);

  mozilla::gfx::ShapedTextFlags GetFlags() const { return mFlags; }

  bool IsVertical() const {
    return (GetFlags() & mozilla::gfx::ShapedTextFlags::TEXT_ORIENT_MASK) !=
           mozilla::gfx::ShapedTextFlags::TEXT_ORIENT_HORIZONTAL;
  }

  bool UseCenterBaseline() const {
    mozilla::gfx::ShapedTextFlags orient =
        GetFlags() & mozilla::gfx::ShapedTextFlags::TEXT_ORIENT_MASK;
    return orient ==
               mozilla::gfx::ShapedTextFlags::TEXT_ORIENT_VERTICAL_MIXED ||
           orient ==
               mozilla::gfx::ShapedTextFlags::TEXT_ORIENT_VERTICAL_UPRIGHT;
  }

  bool IsRightToLeft() const {
    return (GetFlags() & mozilla::gfx::ShapedTextFlags::TEXT_IS_RTL) ==
           mozilla::gfx::ShapedTextFlags::TEXT_IS_RTL;
  }

  bool IsSidewaysLeft() const {
    return (GetFlags() & mozilla::gfx::ShapedTextFlags::TEXT_ORIENT_MASK) ==
           mozilla::gfx::ShapedTextFlags::TEXT_ORIENT_VERTICAL_SIDEWAYS_LEFT;
  }

  // Return true if the logical inline direction is reversed compared to
  // normal physical coordinates (i.e. if it is leftwards or upwards)
  bool IsInlineReversed() const { return IsSidewaysLeft() != IsRightToLeft(); }

  gfxFloat GetDirection() const { return IsInlineReversed() ? -1.0f : 1.0f; }

  bool DisableLigatures() const {
    return (GetFlags() &
            mozilla::gfx::ShapedTextFlags::TEXT_DISABLE_OPTIONAL_LIGATURES) ==
           mozilla::gfx::ShapedTextFlags::TEXT_DISABLE_OPTIONAL_LIGATURES;
  }

  bool TextIs8Bit() const {
    return (GetFlags() & mozilla::gfx::ShapedTextFlags::TEXT_IS_8BIT) ==
           mozilla::gfx::ShapedTextFlags::TEXT_IS_8BIT;
  }

  uint16_t GetAppUnitsPerDevUnit() const { return mAppUnitsPerDevUnit; }

  uint32_t GetLength() const { return mLength; }

  bool FilterIfIgnorable(uint32_t aIndex, uint32_t aCh);

  // Erase glyph data from the gfxShapedText, while retaining line-break and
  // cluster flags.
  void ClearGlyphs();

 protected:
  // Allocate aCount DetailedGlyphs for the given index
  DetailedGlyph* AllocateDetailedGlyphs(uint32_t aCharIndex, uint32_t aCount);

  // Ensure the glyph on the given index is complex glyph so that we can use
  // it to record specific characters that layout may need to detect.
  void EnsureComplexGlyph(uint32_t aIndex, CompressedGlyph& aGlyph) {
    MOZ_ASSERT(GetCharacterGlyphs() + aIndex == &aGlyph);
    if (aGlyph.IsSimpleGlyph()) {
      DetailedGlyph details = {aGlyph.GetSimpleGlyph(),
                               (int32_t)aGlyph.GetSimpleAdvance(),
                               mozilla::gfx::Point()};
      aGlyph.SetComplex(true, true);
      SetDetailedGlyphs(aIndex, 1, &details);
    }
  }

  // For characters whose glyph data does not fit the "simple" glyph criteria
  // in CompressedGlyph, we use a sorted array to store the association
  // between the source character offset and an index into an array
  // DetailedGlyphs. The CompressedGlyph record includes a count of
  // the number of DetailedGlyph records that belong to the character,
  // starting at the given index.
  class DetailedGlyphStore {
   public:
    DetailedGlyphStore() = default;

    // This is optimized for the most common calling patterns:
    // we rarely need random access to the records, access is most commonly
    // sequential through the textRun, so we record the last-used index
    // and check whether the caller wants the same record again, or the
    // next; if not, it's most likely we're starting over from the start
    // of the run, so we check the first entry before resorting to binary
    // search as a last resort.
    // NOTE that this must not be called for a character offset that does
    // not have any DetailedGlyph records; callers must have verified that
    // mCharacterGlyphs[aOffset].GetGlyphCount() is greater than zero
    // before calling this, otherwise the assertions here will fire (in a
    // debug build), and we'll probably crash.
    DetailedGlyph* Get(uint32_t aOffset, uint32_t aCount) {
      NS_ASSERTION(mOffsetToIndex.Length() > 0, "no detailed glyph records!");
      // Load the last-used-position hint.
      nsTArray<DGRec>::index_type lastUsed =
          mLastUsed.load(std::memory_order_relaxed);
      // Check common cases (fwd iteration, initial entry, etc) first.
      if (lastUsed < mOffsetToIndex.Length() - 1 &&
          aOffset == mOffsetToIndex[lastUsed + 1].mOffset) {
        ++lastUsed;
      } else if (aOffset == mOffsetToIndex[0].mOffset) {
        lastUsed = 0;
      } else if (aOffset == mOffsetToIndex[lastUsed].mOffset) {
        // do nothing
      } else if (lastUsed > 0 &&
                 aOffset == mOffsetToIndex[lastUsed - 1].mOffset) {
        --lastUsed;
      } else {
        // None of the fast-paths applied, so do the binary search.
        lastUsed = mOffsetToIndex.BinaryIndexOf(aOffset, CompareToOffset());
      }
      NS_ASSERTION(lastUsed != nsTArray<DGRec>::NoIndex,
                   "detailed glyph record missing!");
      uint32_t index = mOffsetToIndex[lastUsed].mIndex;
      // Remember the position, as a hint for next time.
      mLastUsed.store(lastUsed, std::memory_order_relaxed);
      // Ensure that |aCount| records are available, starting at |index|.
      MOZ_RELEASE_ASSERT(index < mDetails.Length() &&
                         aCount <= mDetails.Length() - index);
      return mDetails.Elements() + index;
    }

    DetailedGlyph* Allocate(uint32_t aOffset, uint32_t aCount) {
      uint32_t detailIndex = mDetails.Length();
      DetailedGlyph* details = mDetails.AppendElements(aCount);
      // We normally set up glyph records sequentially, so the common case
      // here is to append new records to the mOffsetToIndex array;
      // test for that before falling back to the InsertElementSorted
      // method.
      if (mOffsetToIndex.Length() == 0 ||
          aOffset > mOffsetToIndex[mOffsetToIndex.Length() - 1].mOffset) {
        mOffsetToIndex.AppendElement(DGRec(aOffset, detailIndex));
      } else {
        mOffsetToIndex.InsertElementSorted(DGRec(aOffset, detailIndex),
                                           CompareRecordOffsets());
      }
      return details;
    }

    size_t SizeOfIncludingThis(mozilla::MallocSizeOf aMallocSizeOf) {
      return aMallocSizeOf(this) +
             mDetails.ShallowSizeOfExcludingThis(aMallocSizeOf) +
             mOffsetToIndex.ShallowSizeOfExcludingThis(aMallocSizeOf);
    }

   private:
    struct DGRec {
      DGRec(const uint32_t& aOffset, const uint32_t& aIndex)
          : mOffset(aOffset), mIndex(aIndex) {}
      uint32_t mOffset;  // source character offset in the textrun
      uint32_t mIndex;   // index where this char's DetailedGlyphs begin
    };

    struct CompareToOffset {
      bool Equals(const DGRec& a, const uint32_t& b) const {
        return a.mOffset == b;
      }
      bool LessThan(const DGRec& a, const uint32_t& b) const {
        return a.mOffset < b;
      }
    };

    struct CompareRecordOffsets {
      bool Equals(const DGRec& a, const DGRec& b) const {
        return a.mOffset == b.mOffset;
      }
      bool LessThan(const DGRec& a, const DGRec& b) const {
        return a.mOffset < b.mOffset;
      }
    };

    // Concatenated array of all the DetailedGlyph records needed for the
    // textRun; individual character offsets are associated with indexes
    // into this array via the mOffsetToIndex table.
    nsTArray<DetailedGlyph> mDetails;

    // For each character offset that needs DetailedGlyphs, we record the
    // index in mDetails where the list of glyphs begins. This array is
    // sorted by mOffset.
    nsTArray<DGRec> mOffsetToIndex;

    // Records the most recently used index into mOffsetToIndex, so that
    // we can support sequential access more quickly than just doing
    // a binary search each time.
    // Atomic because multiple threads may be accessing the same shaped-
    // word or textrun; if so, they may overwrite each other's values and
    // degrade performance slightly, but this is harmless.
    std::atomic<nsTArray<DGRec>::index_type> mLastUsed = 0;
  };

  mozilla::UniquePtr<DetailedGlyphStore> mDetailedGlyphs;

  // Number of char16_t characters and CompressedGlyph glyph records
  uint32_t mLength;

  // Shaping flags (direction, ligature-suppression)
  mozilla::gfx::ShapedTextFlags mFlags;

  uint16_t mAppUnitsPerDevUnit;
};

/*
 * gfxShapedWord: an individual (space-delimited) run of text shaped with a
 * particular font, without regard to external context.
 *
 * The glyph data is copied into gfxTextRuns as needed from the cache of
 * ShapedWords associated with each gfxFont instance.
 */
class gfxShapedWord final : public gfxShapedText {
 public:
  typedef mozilla::intl::Script Script;

  // Create a ShapedWord that can hold glyphs for aLength characters,
  // with mCharacterGlyphs sized appropriately.
  //
  // Returns null on allocation failure (does NOT use infallible alloc)
  // so caller must check for success.
  //
  // This does NOT perform shaping, so the returned word contains no
  // glyph data; the caller must call gfxFont::ShapeText() with appropriate
  // parameters to set up the glyphs.
  static gfxShapedWord* Create(const uint8_t* aText, uint32_t aLength,
                               Script aRunScript, nsAtom* aLanguage,
                               uint16_t aAppUnitsPerDevUnit,
                               mozilla::gfx::ShapedTextFlags aFlags,
                               gfxFontShaper::RoundingFlags aRounding) {
    NS_ASSERTION(aLength <= gfxPlatform::GetPlatform()->WordCacheCharLimit(),
                 "excessive length for gfxShapedWord!");

    // Compute size needed including the mCharacterGlyphs array
    // and a copy of the original text
    uint32_t size = offsetof(gfxShapedWord, mCharGlyphsStorage) +
                    aLength * (sizeof(CompressedGlyph) + sizeof(uint8_t));
    void* storage = malloc(size);
    if (!storage) {
      return nullptr;
    }

    // Construct in the pre-allocated storage, using placement new
    return new (storage) gfxShapedWord(aText, aLength, aRunScript, aLanguage,
                                       aAppUnitsPerDevUnit, aFlags, aRounding);
  }

  static gfxShapedWord* Create(const char16_t* aText, uint32_t aLength,
                               Script aRunScript, nsAtom* aLanguage,
                               uint16_t aAppUnitsPerDevUnit,
                               mozilla::gfx::ShapedTextFlags aFlags,
                               gfxFontShaper::RoundingFlags aRounding) {
    NS_ASSERTION(aLength <= gfxPlatform::GetPlatform()->WordCacheCharLimit(),
                 "excessive length for gfxShapedWord!");

    // In the 16-bit version of Create, if the TEXT_IS_8BIT flag is set,
    // then we convert the text to an 8-bit version and call the 8-bit
    // Create function instead.
    if (aFlags & mozilla::gfx::ShapedTextFlags::TEXT_IS_8BIT) {
      nsAutoCString narrowText;
      LossyAppendUTF16toASCII(nsDependentSubstring(aText, aLength), narrowText);
      return Create((const uint8_t*)(narrowText.BeginReading()), aLength,
                    aRunScript, aLanguage, aAppUnitsPerDevUnit, aFlags,
                    aRounding);
    }

    uint32_t size = offsetof(gfxShapedWord, mCharGlyphsStorage) +
                    aLength * (sizeof(CompressedGlyph) + sizeof(char16_t));
    void* storage = malloc(size);
    if (!storage) {
      return nullptr;
    }

    return new (storage) gfxShapedWord(aText, aLength, aRunScript, aLanguage,
                                       aAppUnitsPerDevUnit, aFlags, aRounding);
  }

  // Override operator delete to properly free the object that was
  // allocated via malloc.
  void operator delete(void* p) { free(p); }

  const CompressedGlyph* GetCharacterGlyphs() const override {
    return &mCharGlyphsStorage[0];
  }
  CompressedGlyph* GetCharacterGlyphs() override {
    return &mCharGlyphsStorage[0];
  }

  const uint8_t* Text8Bit() const {
    NS_ASSERTION(TextIs8Bit(), "invalid use of Text8Bit()");
    return reinterpret_cast<const uint8_t*>(mCharGlyphsStorage + GetLength());
  }

  const char16_t* TextUnicode() const {
    NS_ASSERTION(!TextIs8Bit(), "invalid use of TextUnicode()");
    return reinterpret_cast<const char16_t*>(mCharGlyphsStorage + GetLength());
  }

  char16_t GetCharAt(uint32_t aOffset) const {
    NS_ASSERTION(aOffset < GetLength(), "aOffset out of range");
    return TextIs8Bit() ? char16_t(Text8Bit()[aOffset])
                        : TextUnicode()[aOffset];
  }

  Script GetScript() const { return mScript; }
  nsAtom* GetLanguage() const { return mLanguage.get(); }

  gfxFontShaper::RoundingFlags GetRounding() const { return mRounding; }

  void ResetAge() { mAgeCounter.store(0, std::memory_order_relaxed); }
  uint32_t IncrementAge() {
    return mAgeCounter.fetch_add(1, std::memory_order_relaxed) + 1;
  }

  // Helper used when hashing a word for the shaped-word caches
  static constexpr uint32_t sHashInitialValue = 0x811c9dc5;
  MOZ_ALWAYS_INLINE static constexpr uint32_t HashMix(uint32_t aHash,
                                                      char16_t aCh) {
    aHash ^= static_cast<uint32_t>(aCh);
    aHash *= 16777619u;  // FNV prime
    return aHash;
  }

  size_t SizeOfIncludingThis(mozilla::MallocSizeOf aMallocSizeOf) const;

 private:
  // so that gfxTextRun can share our DetailedGlyphStore class
  friend class gfxTextRun;

  // Construct storage for a ShapedWord, ready to receive glyph data
  gfxShapedWord(const uint8_t* aText, uint32_t aLength, Script aRunScript,
                nsAtom* aLanguage, uint16_t aAppUnitsPerDevUnit,
                mozilla::gfx::ShapedTextFlags aFlags,
                gfxFontShaper::RoundingFlags aRounding)
      : gfxShapedText(aLength,
                      aFlags | mozilla::gfx::ShapedTextFlags::TEXT_IS_8BIT,
                      aAppUnitsPerDevUnit),
        mLanguage(aLanguage),
        mScript(aRunScript),
        mRounding(aRounding),
        mAgeCounter(0) {
    memset(mCharGlyphsStorage, 0, aLength * sizeof(CompressedGlyph));
    uint8_t* text = reinterpret_cast<uint8_t*>(&mCharGlyphsStorage[aLength]);
    memcpy(text, aText, aLength * sizeof(uint8_t));
    SetupClusterBoundaries(0, aText, aLength);
  }

  gfxShapedWord(const char16_t* aText, uint32_t aLength, Script aRunScript,
                nsAtom* aLanguage, uint16_t aAppUnitsPerDevUnit,
                mozilla::gfx::ShapedTextFlags aFlags,
                gfxFontShaper::RoundingFlags aRounding)
      : gfxShapedText(aLength, aFlags, aAppUnitsPerDevUnit),
        mLanguage(aLanguage),
        mScript(aRunScript),
        mRounding(aRounding),
        mAgeCounter(0) {
    memset(mCharGlyphsStorage, 0, aLength * sizeof(CompressedGlyph));
    char16_t* text = reinterpret_cast<char16_t*>(&mCharGlyphsStorage[aLength]);
    memcpy(text, aText, aLength * sizeof(char16_t));
    SetupClusterBoundaries(0, aText, aLength);
  }

  RefPtr<nsAtom> mLanguage;
  Script mScript;

  gfxFontShaper::RoundingFlags mRounding;

  // With multithreaded shaping, this may be updated by any thread.
  std::atomic<uint32_t> mAgeCounter;

  // The mCharGlyphsStorage array is actually a variable-size member;
  // when the ShapedWord is created, its size will be increased as necessary
  // to allow the proper number of glyphs to be stored.
  // The original text, in either 8-bit or 16-bit form, will be stored
  // immediately following the CompressedGlyphs.
  CompressedGlyph mCharGlyphsStorage[1];
};

class GlyphBufferAzure;
struct TextRunDrawParams;
struct FontDrawParams;
struct EmphasisMarkDrawParams;

class gfxFont {
  friend class gfxHarfBuzzShaper;
  friend class gfxGraphiteShaper;

 protected:
  using DrawTarget = mozilla::gfx::DrawTarget;
  using Script = mozilla::intl::Script;
  using SVGContextPaint = mozilla::SVGContextPaint;

  using RoundingFlags = gfxFontShaper::RoundingFlags;
  using ShapedTextFlags = mozilla::gfx::ShapedTextFlags;
  using imgDrawingParams = mozilla::image::imgDrawingParams;

 public:
  using FontSlantStyle = mozilla::FontSlantStyle;
  using FontSizeAdjust = mozilla::StyleFontSizeAdjust;

  NS_INLINE_DECL_THREADSAFE_REFCOUNTING_WITH_DESTROY(gfxFont, MaybeDestroy())
  int32_t GetRefCount() { return int32_t(mRefCnt); }

  // options to specify the kind of AA to be used when creating a font
  typedef enum : uint8_t {
    kAntialiasDefault,
    kAntialiasNone,
    kAntialiasGrayscale,
    kAntialiasSubpixel
  } AntialiasOption;

 protected:
  gfxFont(const RefPtr<mozilla::gfx::UnscaledFont>& aUnscaledFont,
          gfxFontEntry* aFontEntry, const gfxFontStyle* aFontStyle,
          AntialiasOption anAAOption = kAntialiasDefault);

  virtual ~gfxFont();

  void MaybeDestroy() {
    bool destroy = true;
    if (gfxFontCache* fc = gfxFontCache::GetCache()) {
      destroy = fc->MaybeDestroy(this);
    }
    if (destroy) {
      Destroy();
    }
  }

 public:
  void Destroy() {
    MOZ_ASSERT(GetRefCount() == 0);
    delete this;
  }

  bool Valid() const { return mIsValid; }

  // options for the kind of bounding box to return from measurement
  typedef enum {
    LOOSE_INK_EXTENTS,
    // A box that encloses all the painted pixels, and may
    // include sidebearings and/or additional ascent/descent
    // within the glyph cell even if the ink is smaller.
    TIGHT_INK_EXTENTS,
    // A box that tightly encloses all the painted pixels
    // (although actually on Windows, at least, it may be
    // slightly larger than strictly necessary because
    // we can't get precise extents with ClearType).
    TIGHT_HINTED_OUTLINE_EXTENTS
    // A box that tightly encloses the glyph outline,
    // ignoring possible antialiasing pixels that extend
    // beyond this.
    // NOTE: The default implementation of gfxFont::Measure(),
    // which works with the glyph extents cache, does not
    // differentiate between this and TIGHT_INK_EXTENTS.
    // Whether the distinction is important depends on the
    // antialiasing behavior of the platform; currently the
    // distinction is only implemented in the gfxWindowsFont
    // subclass, because of ClearType's tendency to paint
    // outside the hinted outline.
    // Also NOTE: it is relatively expensive to request this,
    // as it does not use cached glyph extents in the font.
  } BoundingBoxType;

  const nsCString& GetName() const { return mFontEntry->Name(); }
  const gfxFontStyle* GetStyle() const { return &mStyle; }

  virtual gfxFont* CopyWithAntialiasOption(AntialiasOption anAAOption) const {
    // platforms where this actually matters should override
    return nullptr;
  }

  gfxFloat GetAdjustedSize() const {
    // mAdjustedSize is cached here if not already set to a non-zero value;
    // but it may be overridden by a value computed in metrics initialization
    // from font-size-adjust.
    if (mAdjustedSize < 0.0) {
      mAdjustedSize = mStyle.AdjustedSizeMustBeZero()
                          ? 0.0
                          : mStyle.size * mFontEntry->mSizeAdjust;
    }
    return mAdjustedSize;
  }

  float FUnitsToDevUnitsFactor() const {
    // check this was set up during font initialization
    NS_ASSERTION(mFUnitsConvFactor >= 0.0f, "mFUnitsConvFactor not valid");
    return mFUnitsConvFactor;
  }

  // check whether this is an sfnt we can potentially use with Graphite
  bool FontCanSupportGraphite() const {
    return mFontEntry->HasGraphiteTables();
  }

  // Whether this is a font that may be doing full-color rendering,
  // and therefore needs us to use a mask for text-shadow even when
  // we're not actually blurring.
  bool AlwaysNeedsMaskForShadow() const {
    return mFontEntry->AlwaysNeedsMaskForShadow();
  }

  // whether a feature is supported by the font (limited to a small set
  // of features for which some form of fallback needs to be implemented)
  bool SupportsFeature(Script aScript, uint32_t aFeatureTag);

  // whether the font supports "real" small caps, petite caps etc.
  // aFallbackToSmallCaps true when petite caps should fallback to small caps
  bool SupportsVariantCaps(Script aScript, uint32_t aVariantCaps,
                           bool& aFallbackToSmallCaps,
                           bool& aSyntheticLowerToSmallCaps,
                           bool& aSyntheticUpperToSmallCaps);

  // whether the font supports subscript/superscript feature
  // for fallback, need to verify that all characters in the run
  // have variant substitutions
  bool SupportsSubSuperscript(uint32_t aSubSuperscript, const uint8_t* aString,
                              uint32_t aLength, Script aRunScript);

  bool SupportsSubSuperscript(uint32_t aSubSuperscript, const char16_t* aString,
                              uint32_t aLength, Script aRunScript);

  // whether the specified feature will apply to the given character
  bool FeatureWillHandleChar(Script aRunScript, uint32_t aFeature,
                             uint32_t aUnicode);

  // Subclasses may choose to look up glyph ids for characters.
  // If they do not override this, gfxHarfBuzzShaper will fetch the cmap
  // table and use that.
  virtual bool ProvidesGetGlyph() const { return false; }
  // Map unicode character to glyph ID.
  // Only used if ProvidesGetGlyph() returns true.
  virtual uint32_t GetGlyph(uint32_t unicode, uint32_t variation_selector) {
    return 0;
  }

  // Return the advance of a glyph.
  gfxFloat GetGlyphAdvance(uint16_t aGID, bool aVertical = false);

  // Return the advance of a given Unicode char in isolation.
  // Returns -1.0 if the char is not supported.
  gfxFloat GetCharAdvance(uint32_t aUnicode, bool aVertical = false);

  gfxFloat SynthesizeSpaceWidth(uint32_t aCh);

  // Work out whether cairo will snap inter-glyph spacing to pixels
  // when rendering to the given drawTarget.
  RoundingFlags GetRoundOffsetsToPixels(DrawTarget* aDrawTarget);

  virtual bool ShouldHintMetrics() const { return true; }
  virtual bool ShouldRoundXOffset(cairo_t* aCairo) const { return true; }

  // Return the font's owned harfbuzz shaper, creating and initializing it if
  // necessary; returns null if shaper initialization has failed.
  gfxHarfBuzzShaper* GetHarfBuzzShaper();

  // Font metrics
  struct Metrics {
    gfxFloat capHeight = 0.0;
    gfxFloat xHeight = 0.0;
    gfxFloat strikeoutSize = 0.0;
    gfxFloat strikeoutOffset = 0.0;
    gfxFloat underlineSize = 0.0;
    gfxFloat underlineOffset = 0.0;

    gfxFloat internalLeading = 0.0;
    gfxFloat externalLeading = 0.0;

    gfxFloat emHeight = 0.0;
    gfxFloat emAscent = 0.0;
    gfxFloat emDescent = 0.0;
    gfxFloat maxHeight = 0.0;
    gfxFloat maxAscent = 0.0;
    gfxFloat maxDescent = 0.0;
    gfxFloat maxAdvance = 0.0;

    gfxFloat aveCharWidth = 0.0;
    gfxFloat spaceWidth = 0.0;
    gfxFloat zeroWidth = -1.0;         // -1 if there was no zero glyph
    gfxFloat ideographicWidth = -1.0;  // -1 if kWaterIdeograph is not supported

    gfxFloat ZeroOrAveCharWidth() const {
      return zeroWidth >= 0 ? zeroWidth : aveCharWidth;
    }
  };
  // Unicode character used as basis for 'ic' unit:
  static constexpr uint32_t kWaterIdeograph = 0x6C34;

  typedef nsFontMetrics::FontOrientation Orientation;

  const Metrics& GetMetrics(Orientation aOrientation) {
    if (aOrientation == nsFontMetrics::eHorizontal) {
      return GetHorizontalMetrics();
    }
    if (!mVerticalMetrics) {
      CreateVerticalMetrics();
    }
    return *mVerticalMetrics;
  }

  struct Baselines {
    std::atomic<gfxFloat> mAlphabetic{
        std::numeric_limits<gfxFloat>::quiet_NaN()};
    std::atomic<gfxFloat> mHanging{std::numeric_limits<gfxFloat>::quiet_NaN()};
    std::atomic<gfxFloat> mIdeographicUnder{
        std::numeric_limits<gfxFloat>::quiet_NaN()};
    std::atomic<gfxFloat> mIdeographicOver{
        std::numeric_limits<gfxFloat>::quiet_NaN()};
    std::atomic<gfxFloat> mIdeographicInkUnder{
        std::numeric_limits<gfxFloat>::quiet_NaN()};
    std::atomic<gfxFloat> mIdeographicInkOver{
        std::numeric_limits<gfxFloat>::quiet_NaN()};
    std::atomic<gfxFloat> mCentral{std::numeric_limits<gfxFloat>::quiet_NaN()};
    std::atomic<gfxFloat> mMath{std::numeric_limits<gfxFloat>::quiet_NaN()};

    Baselines() = default;
  };

  typedef std::atomic<gfxFloat> Baselines::* BaselinePtr;
  typedef std::pair<BaselinePtr, hb_ot_layout_baseline_tag_t> Baseline;

  static constexpr Baseline kAlphabetic = {&Baselines::mAlphabetic,
                                           HB_OT_LAYOUT_BASELINE_TAG_ROMAN};
  static constexpr Baseline kHanging = {&Baselines::mHanging,
                                        HB_OT_LAYOUT_BASELINE_TAG_HANGING};
  static constexpr Baseline kIdeographicUnder = {
      &Baselines::mIdeographicUnder,
      HB_OT_LAYOUT_BASELINE_TAG_IDEO_EMBOX_BOTTOM_OR_LEFT};
  static constexpr Baseline kIdeographicOver = {
      &Baselines::mIdeographicOver,
      HB_OT_LAYOUT_BASELINE_TAG_IDEO_EMBOX_TOP_OR_RIGHT};
  static constexpr Baseline kIdeographicInkUnder = {
      &Baselines::mIdeographicInkUnder,
      HB_OT_LAYOUT_BASELINE_TAG_IDEO_FACE_BOTTOM_OR_LEFT};
  static constexpr Baseline kIdeographicInkOver = {
      &Baselines::mIdeographicInkOver,
      HB_OT_LAYOUT_BASELINE_TAG_IDEO_FACE_TOP_OR_RIGHT};
  static constexpr Baseline kCentral = {
      &Baselines::mCentral, HB_OT_LAYOUT_BASELINE_TAG_IDEO_EMBOX_CENTRAL};
  static constexpr Baseline kMath = {&Baselines::mMath,
                                     HB_OT_LAYOUT_BASELINE_TAG_MATH};

  gfxFloat GetBaseline(const Baseline& aBaseline, Orientation aOrientation);

  /**
   * We let layout specify spacing on either side of any
   * character. We need to specify both before and after
   * spacing so that substring measurement can do the right things.
   * These values are in appunits.
   */
  struct Spacing {
    nscoord mBefore;
    nscoord mAfter;
  };
  /**
   * Metrics for a particular string
   */
  struct RunMetrics {
    RunMetrics() { mAdvanceWidth = mAscent = mDescent = 0.0; }

    void CombineWith(const RunMetrics& aOther, bool aOtherIsOnLeft);

    // can be negative (partly due to negative spacing).
    // Advance widths should be additive: the advance width of the
    // (offset1, length1) plus the advance width of (offset1 + length1,
    // length2) should be the advance width of (offset1, length1 + length2)
    gfxFloat mAdvanceWidth;

    // For zero-width substrings, these must be zero!
    gfxFloat mAscent;   // always non-negative
    gfxFloat mDescent;  // always non-negative

    // Bounding box that is guaranteed to include everything drawn.
    // If a tight boundingBox was requested when these metrics were
    // generated, this will tightly wrap the glyphs, otherwise it is
    // "loose" and may be larger than the true bounding box.
    // Coordinates are relative to the baseline left origin, so typically
    // mBoundingBox.y == -mAscent
    gfxRect mBoundingBox;
  };

  /**
   * Draw a series of glyphs to aContext. The direction of aTextRun must
   * be honoured.
   * @param aStart the first character to draw
   * @param aEnd draw characters up to here
   * @param aPt the baseline origin; the left end of the baseline
   * for LTR textruns, the right end for RTL textruns.
   * On return, this will be updated to the other end of the baseline.
   * In application units, really!
   * @param aRunParams record with drawing parameters, see TextRunDrawParams.
   * Particular fields of interest include
   * .spacing  spacing to insert before and after characters (for RTL
   *   glyphs, before-spacing is inserted to the right of characters). There
   *   are aEnd - aStart elements in this array, unless it's null to indicate
   *   that there is no spacing.
   * .drawMode  specifies whether the fill or stroke of the glyph should be
   *   drawn, or if it should be drawn into the current path
   * .contextPaint  information about how to construct the fill and
   *   stroke pattern. Can be nullptr if we are not stroking the text, which
   *   indicates that the current source from context should be used for fill
   * .context  the Thebes graphics context to which we're drawing
   * .dt  Moz2D DrawTarget to which we're drawing
   *
   * Callers guarantee:
   * -- aStart and aEnd are aligned to cluster and ligature boundaries
   * -- all glyphs use this font
   */
  void Draw(const gfxTextRun* aTextRun, uint32_t aStart, uint32_t aEnd,
            mozilla::gfx::Point* aPt, TextRunDrawParams& aRunParams,
            imgDrawingParams& aImgParams,
            mozilla::gfx::ShapedTextFlags aOrientation);

  /**
   * Draw the emphasis marks for the given text run. Its prerequisite
   * and output are similiar to the method Draw().
   * @param aPt the baseline origin of the emphasis marks.
   * @param aParams some drawing parameters, see EmphasisMarkDrawParams.
   */
  void DrawEmphasisMarks(const gfxTextRun* aShapedText,
                         mozilla::gfx::Point* aPt, uint32_t aOffset,
                         uint32_t aCount, const EmphasisMarkDrawParams& aParams,
                         imgDrawingParams& aImgParams);

  /**
   * Measure a run of characters. See gfxTextRun::Metrics.
   * @param aTight if false, then return the union of the glyph extents
   * with the font-box for the characters (the rectangle with x=0,width=
   * the advance width for the character run,y=-(font ascent), and height=
   * font ascent + font descent). Otherwise, we must return as tight as possible
   * an approximation to the area actually painted by glyphs.
   * @param aDrawTargetForTightBoundingBox when aTight is true, this must
   * be non-null.
   * @param aSpacing spacing to insert before and after glyphs. The bounding box
   * need not include the spacing itself, but the spacing affects the glyph
   * positions. null if there is no spacing.
   *
   * Callers guarantee:
   * -- aStart and aEnd are aligned to cluster and ligature boundaries
   * -- all glyphs use this font
   *
   * The default implementation just uses font metrics and aTextRun's
   * advances, and assumes no characters fall outside the font box. In
   * general this is insufficient, because that assumption is not always true.
   */
  virtual RunMetrics Measure(const gfxTextRun* aTextRun, uint32_t aStart,
                             uint32_t aEnd, BoundingBoxType aBoundingBoxType,
                             DrawTarget* aDrawTargetForTightBoundingBox,
                             Spacing* aSpacing, nscoord aLetterSpacing,
                             mozilla::gfx::ShapedTextFlags aOrientation);
  /**
   * Line breaks have been changed at the beginning and/or end of a substring
   * of the text. Reshaping may be required; glyph updating is permitted.
   * @return true if anything was changed, false otherwise
   */
  bool NotifyLineBreaksChanged(gfxTextRun* aTextRun, uint32_t aStart,
                               uint32_t aLength) {
    return false;
  }

  // Expiration tracking
  nsExpirationState* GetExpirationState() { return &mExpirationState; }

  // Get the glyphID of a space
  uint16_t GetSpaceGlyph() const { return mSpaceGlyph; }

  gfxGlyphExtents* GetOrCreateGlyphExtents(int32_t aAppUnitsPerDevUnit);

  void SetupGlyphExtents(DrawTarget* aDrawTarget, uint32_t aGlyphID,
                         bool aNeedTight, gfxGlyphExtents* aExtents);

  virtual bool AllowSubpixelAA() const { return true; }

  bool ApplySyntheticBold() const { return mApplySyntheticBold; }

  float AngleForSyntheticOblique() const;
  float SkewForSyntheticOblique() const;

  // Amount by which synthetic bold "fattens" the glyphs:
  // For size S up to a threshold size T, we use (0.25 + 3S / 4T),
  // so that the result ranges from 0.25 to 1.0; thereafter,
  // simply use (S / T).
  gfxFloat GetSyntheticBoldOffset() const {
    gfxFloat size = GetAdjustedSize();
    const gfxFloat threshold = 48.0;
    return size < threshold ? (0.25 + 0.75 * size / threshold)
                            : (size / threshold);
  }

  gfxFontEntry* GetFontEntry() const { return mFontEntry.get(); }
  bool HasCharacter(uint32_t ch) const {
    if (!mIsValid || (mUnicodeRangeMap && !mUnicodeRangeMap->test(ch))) {
      return false;
    }
    return mFontEntry->HasCharacter(ch);
  }

  const gfxCharacterMap* GetUnicodeRangeMap() const {
    return mUnicodeRangeMap.get();
  }

  void SetUnicodeRangeMap(gfxCharacterMap* aUnicodeRangeMap) {
    mUnicodeRangeMap = aUnicodeRangeMap;
  }

  uint16_t GetUVSGlyph(uint32_t aCh, uint32_t aVS) const {
    if (!mIsValid) {
      return 0;
    }
    return mFontEntry->GetUVSGlyph(aCh, aVS);
  }

  template <typename T>
  bool InitFakeSmallCapsRun(FontVisibilityProvider* aFontVisibilityProvider,
                            DrawTarget* aDrawTarget, gfxTextRun* aTextRun,
                            const T* aText, uint32_t aOffset, uint32_t aLength,
                            FontMatchType aMatchType,
                            mozilla::gfx::ShapedTextFlags aOrientation,
                            Script aScript, nsAtom* aLanguage,
                            bool aSyntheticLower, bool aSyntheticUpper);

  // call the (virtual) InitTextRun method to do glyph generation/shaping,
  // limiting the length of text passed by processing the run in multiple
  // segments if necessary
  template <typename T>
  bool SplitAndInitTextRun(DrawTarget* aDrawTarget, gfxTextRun* aTextRun,
                           const T* aString, uint32_t aRunStart,
                           uint32_t aRunLength, Script aRunScript,
                           nsAtom* aLanguage,
                           mozilla::gfx::ShapedTextFlags aOrientation);

  // Get a ShapedWord representing a single space for use in setting up a
  // gfxTextRun.
  bool ProcessSingleSpaceShapedWord(
      bool aVertical, uint16_t aAppUnitsPerDevUnit,
      mozilla::gfx::ShapedTextFlags aFlags, RoundingFlags aRounding,
      const std::function<void(gfxShapedWord*)>& aCallback);

  // Called by the gfxFontCache timer to increment the age of all the words,
  // so that they'll expire after a sufficient period of non-use.
  // Returns true if the cache is now empty, otherwise false.
  bool AgeCachedWords();

  // Discard all cached word records; called on memory-pressure notification.
  void ClearCachedWords() {
    mozilla::AutoWriteLock lock(mLock);
    if (mWordCache) {
      ClearCachedWordsLocked();
    }
  }
  void ClearCachedWordsLocked() MOZ_REQUIRES(mLock) {
    MOZ_ASSERT(mWordCache);
    mWordCache->clear();
  }

  // Glyph rendering/geometry has changed, so invalidate data as necessary.
  void NotifyGlyphsChanged() const;

  virtual void AddSizeOfExcludingThis(mozilla::MallocSizeOf aMallocSizeOf,
                                      FontCacheSizes* aSizes) const;
  virtual void AddSizeOfIncludingThis(mozilla::MallocSizeOf aMallocSizeOf,
                                      FontCacheSizes* aSizes) const;

  typedef enum {
    FONT_TYPE_DWRITE,
    FONT_TYPE_GDI,
    FONT_TYPE_FT2,
    FONT_TYPE_MAC,
    FONT_TYPE_OS2,
    FONT_TYPE_CAIRO,
    FONT_TYPE_FONTCONFIG
  } FontType;

  virtual FontType GetType() const = 0;

  const RefPtr<mozilla::gfx::UnscaledFont>& GetUnscaledFont() const {
    return mUnscaledFont;
  }

  virtual already_AddRefed<mozilla::gfx::ScaledFont> GetScaledFont(
      const TextRunDrawParams& aRunParams) = 0;
  already_AddRefed<mozilla::gfx::ScaledFont> GetScaledFont(
      mozilla::gfx::DrawTarget* aDrawTarget);

  // gfxFont implementations may cache ScaledFont versions other than the
  // default, so InitializeScaledFont must support explicitly specifying
  // which ScaledFonts to initialize.
  void InitializeScaledFont(
      const RefPtr<mozilla::gfx::ScaledFont>& aScaledFont);

  bool KerningDisabled() const { return mKerningSet && !mKerningEnabled; }

  /**
   * Subclass this object to be notified of glyph changes. Delete the object
   * when no longer needed.
   */
  class GlyphChangeObserver {
   public:
    virtual ~GlyphChangeObserver() {
      if (mFont) {
        mFont->RemoveGlyphChangeObserver(this);
      }
    }
    // This gets called when the gfxFont dies.
    void ForgetFont() { mFont = nullptr; }
    virtual void NotifyGlyphsChanged() = 0;

   protected:
    explicit GlyphChangeObserver(gfxFont* aFont) : mFont(aFont) {
      mFont->AddGlyphChangeObserver(this);
    }
    // This pointer is nulled by ForgetFont in the gfxFont's
    // destructor. Before the gfxFont dies.
    gfxFont* MOZ_NON_OWNING_REF mFont;
  };
  friend class GlyphChangeObserver;

  bool GlyphsMayChange() const {
    // Currently only fonts with SVG glyphs can have animated glyphs
    return mFontEntry->TryGetSVGData(this);
  }

  static void DestroySingletons() {
    delete sScriptTagToCode;
    delete sDefaultFeatures;
  }

  // Call TryGetMathTable() to try and load the Open Type MATH table.
  // If (and ONLY if) TryGetMathTable() has returned true, the MathTable()
  // method may be called to access the gfxMathTable data.
  bool TryGetMathTable();
  gfxMathTable* MathTable() const {
    MOZ_RELEASE_ASSERT(mMathTable,
                       "A successful call to TryGetMathTable() must be "
                       "performed before calling this function");
    return mMathTable;
  }

  // Return a cloned font resized and offset to simulate sub/superscript
  // glyphs. This does not add a reference to the returned font.
  already_AddRefed<gfxFont> GetSubSuperscriptFont(
      int32_t aAppUnitsPerDevPixel) const;

  bool HasColorGlyphFor(uint32_t aCh, uint32_t aNextCh);

 protected:
  virtual const Metrics& GetHorizontalMetrics() const = 0;

  void CreateVerticalMetrics();
  void CreateVerticalBaselines();

  Baselines& GetBaselines(Orientation aOrientation) {
    if (aOrientation == nsFontMetrics::eHorizontal) {
      return mHorizontalBaselines;
    }
    if (!mVerticalBaselines) {
      CreateVerticalBaselines();
    }
    return *mVerticalBaselines;
  }

  bool MeasureGlyphs(const gfxTextRun* aTextRun, uint32_t aStart, uint32_t aEnd,
                     BoundingBoxType aBoundingBoxType,
                     DrawTarget* aRefDrawTarget, Spacing* aSpacing,
                     nscoord aLetterSpacing, gfxGlyphExtents* aExtents,
                     bool aIsRTL, bool aNeedsGlyphExtents, RunMetrics& aMetrics,
                     gfxFloat* aAdvanceMin, gfxFloat* aAdvanceMax);

  bool MeasureGlyphs(const gfxTextRun* aTextRun, uint32_t aStart, uint32_t aEnd,
                     BoundingBoxType aBoundingBoxType,
                     DrawTarget* aRefDrawTarget, Spacing* aSpacing,
                     nscoord aLetterSpacing, bool aIsRTL, RunMetrics& aMetrics);

  // Template parameters for DrawGlyphs/DrawOneGlyph, used to select
  // simplified versions of the methods in the most common cases.
  enum class FontComplexityT { SimpleFont, ComplexFont };
  enum class SpacingT { NoSpacing, HasSpacing };

  // Output a run of glyphs at *aPt, which is updated to follow the last glyph
  // in the run. This method also takes account of any letter-spacing provided
  // in aRunParams.
  template <FontComplexityT FC, SpacingT S>
  bool DrawGlyphs(const gfxShapedText* aShapedText,
                  uint32_t aOffset,  // offset in the textrun
                  uint32_t aCount,   // length of run to draw
                  mozilla::gfx::Point* aPt,
                  // transform for mOffset field in DetailedGlyph records,
                  // to account for rotations (may be null)
                  const mozilla::gfx::Matrix* aOffsetMatrix,
                  GlyphBufferAzure& aBuffer);

  // Output a single glyph at *aPt.
  // Normal glyphs are simply accumulated in aBuffer until it is full and
  // gets flushed, but SVG or color-font glyphs will instead be rendered
  // directly to the destination (found from the buffer's parameters).
  template <FontComplexityT FC>
  void DrawOneGlyph(uint32_t aGlyphID, const mozilla::gfx::Point& aPt,
                    GlyphBufferAzure& aBuffer, bool* aEmittedGlyphs);

  // Helper for DrawOneGlyph to handle missing glyphs, rendering either
  // nothing (for default-ignorables) or a missing-glyph hexbox.
  bool DrawMissingGlyph(const TextRunDrawParams& aRunParams,
                        const FontDrawParams& aFontParams,
                        const gfxShapedText::DetailedGlyph* aDetails,
                        const mozilla::gfx::Point& aPt);

  // set the font size and offset used for
  // synthetic subscript/superscript glyphs
  void CalculateSubSuperSizeAndOffset(int32_t aAppUnitsPerDevPixel,
                                      gfxFloat& aSubSuperSizeRatio,
                                      float& aBaselineOffset);

  // Return a font that is a "clone" of this one, but reduced to 80% size
  // (and with variantCaps set to normal). This does not add a reference to
  // the returned font.
  already_AddRefed<gfxFont> GetSmallCapsFont() const;

  // subclasses may provide (possibly hinted) glyph widths (in font units);
  // if they do not override this, harfbuzz will use unhinted widths
  // derived from the font tables
  virtual bool ProvidesGlyphWidths() const { return false; }

  // The return value is interpreted as a horizontal advance in 16.16 fixed
  // point format.
  virtual int32_t GetGlyphWidth(uint16_t aGID) { return -1; }

  virtual bool GetGlyphBounds(uint16_t aGID, gfxRect* aBounds,
                              bool aTight = false) {
    return false;
  }

  bool IsSpaceGlyphInvisible(DrawTarget* aRefDrawTarget,
                             const gfxTextRun* aTextRun);

  void AddGlyphChangeObserver(GlyphChangeObserver* aObserver);
  void RemoveGlyphChangeObserver(GlyphChangeObserver* aObserver);

  // whether font contains substitution lookups containing spaces
  bool HasSubstitutionRulesWithSpaceLookups(Script aRunScript) const;

  // do spaces participate in shaping rules? if so, can't used word cache
  // Note that this function uses HasGraphiteSpaceContextuals, so it can only
  // return a "hint" to the correct answer. The  calling code must ensure it
  // performs safe actions independent of the value returned.
  tainted_boolean_hint SpaceMayParticipateInShaping(Script aRunScript) const;

  // For 8-bit text, expand to 16-bit and then call the following method.
  bool ShapeText(const uint8_t* aText,
                 uint32_t aOffset,  // dest offset in gfxShapedText
                 uint32_t aLength, Script aScript, nsAtom* aLanguage,
                 bool aVertical, RoundingFlags aRounding,
                 gfxShapedText* aShapedText);  // where to store the result

  // Call the appropriate shaper to generate glyphs for aText and store
  // them into aShapedText.
  virtual bool ShapeText(const char16_t* aText, uint32_t aOffset,
                         uint32_t aLength, Script aScript, nsAtom* aLanguage,
                         bool aVertical, RoundingFlags aRounding,
                         gfxShapedText* aShapedText);

  // Helper to adjust for synthetic bold and set character-type flags
  // in the shaped text; implementations of ShapeText should call this
  // after glyph shaping has been completed.
  void PostShapingFixup(const char16_t* aText,
                        uint32_t aOffset,  // position within aShapedText
                        uint32_t aLength, bool aVertical,
                        gfxShapedText* aShapedText);

  // Shape text directly into a range within a textrun, without using the
  // font's word cache. Intended for use when the font has layout features
  // that involve space, and therefore require shaping complete runs rather
  // than isolated words, or for long strings that are inefficient to cache.
  // This will split the text on "invalid" characters (tab/newline) that are
  // not handled via normal shaping, but does not otherwise divide up the
  // text.
  template <typename T>
  bool ShapeTextWithoutWordCache(const T* aText, uint32_t aOffset,
                                 uint32_t aLength, Script aScript,
                                 nsAtom* aLanguage, bool aVertical,
                                 RoundingFlags aRounding, gfxTextRun* aTextRun);

  // Shape a fragment of text (a run that is known to contain only
  // "valid" characters, no newlines/tabs/other control chars).
  // All non-wordcache shaping goes through here; this is the function
  // that will ensure we don't pass excessively long runs to the various
  // platform shapers.
  template <typename T>
  bool ShapeFragmentWithoutWordCache(const T* aText, uint32_t aOffset,
                                     uint32_t aLength, Script aScript,
                                     nsAtom* aLanguage, bool aVertical,
                                     RoundingFlags aRounding,
                                     gfxTextRun* aTextRun);

  void CheckForFeaturesInvolvingSpace() const;

  // Get a ShapedWord representing the given text (either 8- or 16-bit)
  // for use in setting up a gfxTextRun.
  template <typename T, typename Func>
  bool ProcessShapedWordInternal(const T* aText, uint8_t aLength,
                                 uint32_t aHash, Script aRunScript,
                                 nsAtom* aLanguage, bool aVertical,
                                 uint16_t aAppUnitsPerDevUnit,
                                 mozilla::gfx::ShapedTextFlags aFlags,
                                 RoundingFlags aRounding,
                                 gfxTextPerfMetrics* aTextPerf, Func aCallback);

  // whether a given feature is included in feature settings from both the
  // font and the style. aFeatureOn set if resolved feature value is non-zero
  bool HasFeatureSet(uint32_t aFeature, bool& aFeatureOn);

  // used when analyzing whether a font has space contextual lookups
  static mozilla::Atomic<nsTHashMap<nsUint32HashKey, Script>*> sScriptTagToCode;
  static mozilla::Atomic<nsTHashSet<uint32_t>*> sDefaultFeatures;

  RefPtr<gfxFontEntry> mFontEntry;
  mutable mozilla::RWLock mLock;

  // Note that WordCacheKey contains a pointer to the text of the word, which
  // must be valid for as long as the key is in use. When using for a Lookup,
  // the string may be local/temporary, but when storing in the HashMap, we
  // set the Key text pointer to reference the text in the associated
  // gfxShapedWord that is being stored.
  struct WordCacheKey {
    // Raw pointer is safe: for lookup keys, the caller holds the atom alive;
    // for keys stored in the cache, the corresponding gfxShapedWord value
    // holds a RefPtr<nsAtom> to the same atom.
    nsAtom* mLanguage;
    ShapedTextFlags mFlags;
    Script mScript;
    uint16_t mAppUnitsPerDevUnit;
    uint8_t mLength;
    RoundingFlags mRounding;

    union {
      const uint8_t* mSingle;
      const char16_t* mDouble;
    } mText;
    PLDHashNumber mHashKey;
    bool mTextIs8Bit;
    // 3 bytes of padding here

    static constexpr PLDHashNumber ComputeHash(uint32_t aStringHash,
                                               Script aScriptCode,
                                               uint32_t aLanguageHash,
                                               int16_t aAppUnitsPerDevUnit,
                                               ShapedTextFlags aFlags,
                                               RoundingFlags aRounding) {
      return (aStringHash + static_cast<int32_t>(aScriptCode) +
              aAppUnitsPerDevUnit * 0x100 + uint16_t(aFlags) * 0x10000 +
              int(aRounding) + aLanguageHash);
    }

    WordCacheKey(const uint8_t* aText, uint8_t aLength, uint32_t aStringHash,
                 Script aScriptCode, nsAtom* aLanguage,
                 uint16_t aAppUnitsPerDevUnit, ShapedTextFlags aFlags,
                 RoundingFlags aRounding)
        : mLanguage(aLanguage),
          mFlags(aFlags),
          mScript(aScriptCode),
          mAppUnitsPerDevUnit(aAppUnitsPerDevUnit),
          mLength(aLength),
          mRounding(aRounding),
          mHashKey(ComputeHash(aStringHash, aScriptCode,
                               aLanguage ? aLanguage->hash() : 0,
                               aAppUnitsPerDevUnit, aFlags, aRounding)),
          mTextIs8Bit(true) {
      NS_ASSERTION(aFlags & ShapedTextFlags::TEXT_IS_8BIT,
                   "8-bit flag should have been set");
      mText.mSingle = aText;
    }

    WordCacheKey(const char16_t* aText, uint8_t aLength, uint32_t aStringHash,
                 Script aScriptCode, nsAtom* aLanguage,
                 uint16_t aAppUnitsPerDevUnit, ShapedTextFlags aFlags,
                 RoundingFlags aRounding)
        : mLanguage(aLanguage),
          mFlags(aFlags),
          mScript(aScriptCode),
          mAppUnitsPerDevUnit(aAppUnitsPerDevUnit),
          mLength(aLength),
          mRounding(aRounding),
          mHashKey(ComputeHash(aStringHash, aScriptCode,
                               aLanguage ? aLanguage->hash() : 0,
                               aAppUnitsPerDevUnit, aFlags, aRounding)),
          mTextIs8Bit(false) {
      // We can NOT assert that TEXT_IS_8BIT is false in aFlags here,
      // because this might be an 8bit-only word from a 16-bit textrun,
      // in which case the text we're passed is still in 16-bit form,
      // and we'll have to use an 8-to-16bit comparison in KeyEquals.
      mText.mDouble = aText;
    }

    class HashPolicy {
     public:
      typedef WordCacheKey Key;
      typedef WordCacheKey Lookup;
      static mozilla::HashNumber hash(const Lookup& aLookup) {
        return aLookup.mHashKey;
      }
      static bool match(const Key& aKey, const Lookup& aLookup);
    };
  };

  mozilla::UniquePtr<
      mozilla::HashMap<WordCacheKey, mozilla::UniquePtr<gfxShapedWord>,
                       WordCacheKey::HashPolicy>>
      mWordCache MOZ_GUARDED_BY(mLock);

  static const uint32_t kShapedWordCacheMaxAge = 3;

  nsTArray<mozilla::UniquePtr<gfxGlyphExtents>> mGlyphExtentsArray
      MOZ_GUARDED_BY(mLock);
  mozilla::UniquePtr<nsTHashSet<GlyphChangeObserver*>> mGlyphChangeObservers
      MOZ_GUARDED_BY(mLock);

  // a copy of the font without antialiasing, if needed for separate
  // measurement by mathml code
  mozilla::Atomic<gfxFont*> mNonAAFont;

  // we create either or both of these shapers when needed, depending
  // whether the font has graphite tables, and whether graphite shaping
  // is actually enabled
  mozilla::Atomic<gfxHarfBuzzShaper*> mHarfBuzzShaper;
  mozilla::Atomic<gfxGraphiteShaper*> mGraphiteShaper;

  // If a userfont with unicode-range specified, contains map of *possible*
  // ranges supported by font. This is set during user-font initialization,
  // before the font is available to other threads, and thereafter is inert
  // so no guard is needed.
  RefPtr<gfxCharacterMap> mUnicodeRangeMap;

  // This is immutable once initialized by the constructor, so does not need
  // locking.
  RefPtr<mozilla::gfx::UnscaledFont> mUnscaledFont;

  mozilla::Atomic<mozilla::gfx::ScaledFont*> mAzureScaledFont;

  Baselines mHorizontalBaselines;

  // For vertical metrics, created on demand.
  mozilla::Atomic<Metrics*> mVerticalMetrics;
  mozilla::Atomic<Baselines*> mVerticalBaselines;

  // Table used for MathML layout.
  mozilla::Atomic<gfxMathTable*> mMathTable;

  gfxFontStyle mStyle;
  mutable gfxFloat mAdjustedSize;

  // Tracking adjustment to be applied for CSS px size mCachedTrackingSize.
  gfxFloat mTracking = 0.0;
  gfxFloat mCachedTrackingSize = -1.0;

  // Conversion factor from font units to dev units; note that this may be
  // zero (in the degenerate case where mAdjustedSize has become zero).
  // This is OK because we only multiply by this factor, never divide.
  float mFUnitsConvFactor;

  // This is guarded by gfxFontCache::GetCache()->GetMutex() but it is difficult
  // to annotate that fact.
  nsExpirationState mExpirationState;

  // Glyph ID of the font's <space> glyph, zero if missing
  uint16_t mSpaceGlyph = 0;

  // the AA setting requested for this font - may affect glyph bounds
  AntialiasOption mAntialiasOption;

  bool mIsValid;

  // use synthetic bolding for environments where this is not supported
  // by the platform
  bool mApplySyntheticBold;

  bool mKerningSet;      // kerning explicitly set?
  bool mKerningEnabled;  // if set, on or off?

  mozilla::Atomic<bool> mMathInitialized;  // TryGetMathTable() called?

  // Helper for subclasses that want to initialize standard metrics from the
  // tables of sfnt (TrueType/OpenType) fonts.
  // This will use mFUnitsConvFactor if it is already set, else compute it
  // from mAdjustedSize and the unitsPerEm in the font's 'head' table.
  // Returns TRUE and sets mIsValid=TRUE if successful;
  // Returns TRUE but leaves mIsValid=FALSE if the font seems to be broken.
  // Returns FALSE if the font does not appear to be an sfnt at all,
  // and should be handled (if possible) using other APIs.
  bool InitMetricsFromSfntTables(Metrics& aMetrics);

#if MOZ_FONTATIONS
  // Initialize metrics using the font entry's Skrifa font reference.
  // Returns false if unsuccessful (e.g. the entry has no Skrifa font).
  bool InitMetricsFromSkrifa(Metrics& aMetrics);
#endif

  // Helper to calculate various derived metrics from the results of
  // InitMetricsFromSfntTables or equivalent platform code
  void CalculateDerivedMetrics(Metrics& aMetrics);

  // some fonts have bad metrics, this method sanitize them.
  // if this font has bad underline offset, aIsBadUnderlineFont should be true.
  void SanitizeMetrics(Metrics* aMetrics, bool aIsBadUnderlineFont);

  bool RenderSVGGlyph(gfxContext* aContext,
                      mozilla::layout::TextDrawTarget* aTextDrawer,
                      mozilla::gfx::Point aPoint, uint32_t aGlyphId,
                      SVGContextPaint* aContextPaint,
                      imgDrawingParams& aImgParams) const;
  bool RenderSVGGlyph(gfxContext* aContext,
                      mozilla::layout::TextDrawTarget* aTextDrawer,
                      mozilla::gfx::Point aPoint, uint32_t aGlyphId,
                      SVGContextPaint* aContextPaint,
                      imgDrawingParams& aImgParams,
                      gfxTextRunDrawCallbacks* aCallbacks,
                      bool& aEmittedGlyphs) const;

  bool RenderColorGlyph(DrawTarget* aDrawTarget, gfxContext* aContext,
                        mozilla::layout::TextDrawTarget* aTextDrawer,
                        const FontDrawParams& aFontParams,
                        const mozilla::gfx::Point& aPoint, uint32_t aGlyphId);

  class ColorGlyphCache {
   public:
    ColorGlyphCache() = default;
    ~ColorGlyphCache() = default;

    void SetColors(mozilla::gfx::sRGBColor aCurrentColor,
                   mozilla::gfx::FontPalette* aPalette);

    mozilla::HashMap<uint32_t, RefPtr<mozilla::gfx::SourceSurface>> mCache;

   private:
    mozilla::gfx::sRGBColor mCurrentColor;
    RefPtr<mozilla::gfx::FontPalette> mPalette;
  };
  mozilla::UniquePtr<ColorGlyphCache> mColorGlyphCache;

  // Subclasses can override to return true if the platform is able to render
  // COLR-font glyphs directly, instead of us painting the layers explicitly.
  // (Currently used only for COLR.v0 fonts on macOS.)
  virtual bool UseNativeColrFontSupport() const { return false; }

  // Bug 674909. When synthetic bolding text by drawing twice, need to
  // render using a pixel offset in device pixels, otherwise text
  // doesn't appear bolded, it appears as if a bad text shadow exists
  // when a non-identity transform exists.  Use an offset factor so that
  // the second draw occurs at a constant offset in device pixels.
  // This helper calculates the scale factor we need to apply to the
  // synthetic-bold offset.
  static mozilla::gfx::Float CalcXScale(DrawTarget* aDrawTarget);
};

// proportion of ascent used for x-height, if unable to read value from font
#define DEFAULT_XHEIGHT_FACTOR 0.56f

// Parameters passed to gfxFont methods for drawing glyphs from a textrun.
// The TextRunDrawParams are set up once per textrun; the FontDrawParams
// are dependent on the specific font, so they are set per GlyphRun.

struct MOZ_STACK_CLASS TextRunDrawParams {
  explicit TextRunDrawParams(mozilla::gfx::PaletteCache& aPaletteCache)
      : paletteCache(aPaletteCache) {}

  mozilla::gfx::PaletteCache& paletteCache;
  RefPtr<mozilla::gfx::DrawTarget> dt;
  gfxContext* context = nullptr;
  gfxFont::Spacing* spacing = nullptr;
  gfxTextRunDrawCallbacks* callbacks = nullptr;
  mozilla::SVGContextPaint* runContextPaint = nullptr;
  mozilla::layout::TextDrawTarget* textDrawer = nullptr;
  mozilla::LayoutDeviceRect clipRect;
  mozilla::gfx::Float direction = 1.0f;
  nscoord letterSpacing = 0;
  double devPerApp = 1.0;
  nscolor textStrokeColor = 0;
  gfxPattern* textStrokePattern = nullptr;
  const mozilla::gfx::StrokeOptions* strokeOpts = nullptr;
  const mozilla::gfx::DrawOptions* drawOpts = nullptr;
  nsAtom* fontPalette = nullptr;
  DrawMode drawMode = DrawMode::GLYPH_FILL;
  bool isVerticalRun = false;
  bool isRTL = false;
  bool paintSVGGlyphs = true;
  bool allowGDI = true;
  bool hasTextShadow = false;
};

struct MOZ_STACK_CLASS FontDrawParams {
  RefPtr<mozilla::gfx::ScaledFont> scaledFont;
  mozilla::SVGContextPaint* contextPaint;
  mozilla::gfx::Float synBoldOnePixelOffset;
  mozilla::gfx::Float obliqueSkew;
  int32_t extraStrikes;
  mozilla::gfx::DrawOptions drawOptions;
  gfxFloat advanceDirection;
  mozilla::gfx::sRGBColor currentColor;
  RefPtr<mozilla::gfx::FontPalette> palette;
  mozilla::gfx::Rect fontExtents;
  bool isVerticalFont;
  bool haveSVGGlyphs;
  bool haveColorGlyphs;
  bool hasTextShadow;  // whether we're rendering with a text-shadow
};

struct MOZ_STACK_CLASS EmphasisMarkDrawParams {
  EmphasisMarkDrawParams(gfxContext* aContext,
                         mozilla::gfx::PaletteCache& aPaletteCache)
      : context(aContext), paletteCache(aPaletteCache) {}
  gfxContext* context;
  mozilla::gfx::PaletteCache& paletteCache;
  gfxFont::Spacing* spacing;
  gfxTextRun* mark;
  gfxFloat advance;
  gfxFloat direction;
  bool isVertical;
};

#endif
