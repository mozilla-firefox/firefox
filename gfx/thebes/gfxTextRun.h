/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 2 -*-
 * vim: set ts=4 et sw=2 tw=80:
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef GFX_TEXTRUN_H
#define GFX_TEXTRUN_H

#include <stdint.h>

#include "gfxTypes.h"
#include "gfxPoint.h"
#include "gfxFont.h"
#include "gfxFontConstants.h"
#include "gfxSkipChars.h"
#include "gfxPlatform.h"
#include "gfxPlatformFontList.h"
#include "gfxUserFontSet.h"
#include "gfxUtils.h"
#include "mozilla/MemoryReporting.h"
#include "mozilla/RefPtr.h"
#include "mozilla/intl/UnicodeScriptCodes.h"
#include "nsPoint.h"
#include "nsString.h"
#include "nsTArray.h"
#include "nsTHashSet.h"
#include "nsTextFrameUtils.h"
#include "DrawMode.h"
#include "harfbuzz/hb.h"
#include "nsColor.h"
#include "nsFrameList.h"
#include "X11UndefineNone.h"

#ifdef DEBUG_FRAME_DUMP
#  include <stdio.h>
#endif

class gfxContext;
class gfxFontGroup;
class nsAtom;
class nsLanguageAtomService;
class gfxMissingFontRecorder;

namespace mozilla {
class PostTraversalTask;
class SVGContextPaint;
enum class StyleHyphens : uint8_t;
};  // namespace mozilla

/**
 * Callback for Draw() to use when drawing text with mode
 * DrawMode::GLYPH_PATH.
 */
struct MOZ_STACK_CLASS gfxTextRunDrawCallbacks {
  /**
   * Constructs a new DrawCallbacks object.
   *
   * @param aShouldPaintSVGGlyphs If true, SVG glyphs will be painted.  If
   *   false, SVG glyphs will not be painted; fallback plain glyphs are not
   *   emitted either.
   */
  explicit gfxTextRunDrawCallbacks(bool aShouldPaintSVGGlyphs = false)
      : mShouldPaintSVGGlyphs(aShouldPaintSVGGlyphs) {}

  /**
   * Called when a path has been emitted to the gfxContext when
   * painting a text run.  This can be called any number of times,
   * due to partial ligatures and intervening SVG glyphs.
   */
  virtual void NotifyGlyphPathEmitted() = 0;

  bool mShouldPaintSVGGlyphs;
};

/**
 * gfxTextRun is an abstraction for drawing and measuring substrings of a run
 * of text. It stores runs of positioned glyph data, each run having a single
 * gfxFont. The glyphs are associated with a string of source text, and the
 * gfxTextRun APIs take parameters that are offsets into that source text.
 *
 * gfxTextRuns are mostly immutable. The only things that can change are
 * inter-cluster spacing and line break placement. Spacing is always obtained
 * lazily by methods that need it, it is not cached. Line breaks are stored
 * persistently (insofar as they affect the shaping of glyphs; gfxTextRun does
 * not actually do anything to explicitly account for line breaks). Initially
 * there are no line breaks. The textrun can record line breaks before or after
 * any given cluster. (Line breaks specified inside clusters are ignored.)
 *
 * It is important that zero-length substrings are handled correctly. This will
 * be on the test!
 */
class gfxTextRun : public gfxShapedText {
  NS_INLINE_DECL_REFCOUNTING(gfxTextRun);

 protected:
  // Override operator delete to properly free the object that was
  // allocated via malloc.
  void operator delete(void* p) { free(p); }

  virtual ~gfxTextRun();

 public:
  typedef gfxFont::RunMetrics Metrics;
  typedef mozilla::gfx::DrawTarget DrawTarget;

  // Public textrun API for general use

  bool IsClusterStart(uint32_t aPos) const {
    MOZ_ASSERT(aPos < GetLength());
    return mCharacterGlyphs[aPos].IsClusterStart();
  }
  bool IsLigatureGroupStart(uint32_t aPos) const {
    MOZ_ASSERT(aPos < GetLength());
    return mCharacterGlyphs[aPos].IsLigatureGroupStart();
  }
  bool CanBreakLineBefore(uint32_t aPos) const {
    return CanBreakBefore(aPos) == CompressedGlyph::FLAG_BREAK_TYPE_NORMAL;
  }
  bool CanHyphenateBefore(uint32_t aPos) const {
    return CanBreakBefore(aPos) == CompressedGlyph::FLAG_BREAK_TYPE_HYPHEN;
  }

  // Returns a gfxShapedText::CompressedGlyph::FLAG_BREAK_TYPE_* value
  // as defined in gfxFont.h (may be NONE, NORMAL, HYPHEN or EMERGENCY_WRAP).
  uint8_t CanBreakBefore(uint32_t aPos) const {
    MOZ_ASSERT(aPos < GetLength());
    return mCharacterGlyphs[aPos].CanBreakBefore();
  }

  bool CharIsSpace(uint32_t aPos) const {
    MOZ_ASSERT(aPos < GetLength());
    return mCharacterGlyphs[aPos].CharIsSpace();
  }
  bool CharIsTab(uint32_t aPos) const {
    MOZ_ASSERT(aPos < GetLength());
    return mCharacterGlyphs[aPos].CharIsTab();
  }
  bool CharIsNewline(uint32_t aPos) const {
    MOZ_ASSERT(aPos < GetLength());
    return mCharacterGlyphs[aPos].CharIsNewline();
  }
  bool CharMayHaveEmphasisMark(uint32_t aPos) const {
    MOZ_ASSERT(aPos < GetLength());
    return mCharacterGlyphs[aPos].CharMayHaveEmphasisMark();
  }
  bool CharIsFormattingControl(uint32_t aPos) const {
    MOZ_ASSERT(aPos < GetLength());
    return mCharacterGlyphs[aPos].CharIsFormattingControl();
  }

  // All offsets are in terms of the string passed into MakeTextRun.

  // Describe range [start, end) of a text run. The range is
  // restricted to grapheme cluster boundaries.
  struct Range {
    uint32_t start;
    uint32_t end;
    uint32_t Length() const { return end - start; }

    Range() : start(0), end(0) {}
    Range(uint32_t aStart, uint32_t aEnd) : start(aStart), end(aEnd) {}
    explicit Range(const gfxTextRun* aTextRun)
        : start(0), end(aTextRun->GetLength()) {}
  };

  // All coordinates are in layout/app units

  /**
   * Set the potential linebreaks for a substring of the textrun. These are
   * the "allow break before" points. Initially, there are no potential
   * linebreaks.
   *
   * This can change glyphs and/or geometry! Some textruns' shapes
   * depend on potential line breaks (e.g., title-case-converting textruns).
   * This function is virtual so that those textruns can reshape themselves.
   *
   * @return true if this changed the linebreaks, false if the new line
   * breaks are the same as the old
   */
  virtual bool SetPotentialLineBreaks(Range aRange,
                                      const uint8_t* aBreakBefore);

  enum class HyphenType : uint8_t {
    // Code in BreakAndMeasureText depends on the ordering of these values!
    None,
    Explicit,
    Soft,
    AutoWithManualInSameWord,
    AutoWithoutManualInSameWord
  };

  static bool IsOptionalHyphenBreak(HyphenType aType) {
    return aType >= HyphenType::Soft;
  }

  struct HyphenationState {
    uint32_t mostRecentBoundary = 0;
    bool hasManualHyphen = false;
    bool hasExplicitHyphen = false;
    bool hasAutoHyphen = false;
  };

  /**
   * Layout provides PropertyProvider objects. These allow detection of
   * potential line break points and computation of spacing. We pass the data
   * this way to allow lazy data acquisition; for example BreakAndMeasureText
   * will want to only ask for properties of text it's actually looking at.
   *
   * NOTE that requested spacing may not actually be applied, if the textrun
   * is unable to apply it in some context. Exception: spacing around a
   * whitespace character MUST always be applied.
   */
  class PropertyProvider {
   public:
    // Detect hyphenation break opportunities in the given range; breaks
    // not at cluster boundaries will be ignored.
    virtual void GetHyphenationBreaks(Range aRange,
                                      HyphenType* aBreakBefore) const = 0;

    // Returns the provider's hyphenation setting, so callers can decide
    // whether it is necessary to call GetHyphenationBreaks.
    // Result is an StyleHyphens value.
    virtual mozilla::StyleHyphens GetHyphensOption() const = 0;

    // Returns the extra width that will be consumed by a hyphen. This should
    // be constant for a given textrun.
    virtual gfxFloat GetHyphenWidth() const = 0;

    // Return orientation flags to be used when creating a hyphen textrun.
    virtual mozilla::gfx::ShapedTextFlags GetShapedTextFlags() const = 0;

    typedef gfxFont::Spacing Spacing;

    /**
     * Get the spacing around the indicated characters. Spacing must be zero
     * inside clusters. In other words, if character i is not
     * CLUSTER_START, then character i-1 must have zero after-spacing and
     * character i must have zero before-spacing.
     */
    virtual void GetSpacing(Range aRange, Spacing* aSpacing) const = 0;

    // Returns a gfxContext that can be used to measure the hyphen glyph.
    // Only called if the hyphen width is requested.
    virtual already_AddRefed<DrawTarget> GetDrawTarget() const = 0;

    // Return the appUnitsPerDevUnit value to be used when measuring.
    // Only called if the hyphen width is requested.
    virtual uint32_t GetAppUnitsPerDevUnit() const = 0;
  };

  struct MOZ_STACK_CLASS DrawParams {
    gfxContext* context;
    mozilla::gfx::PaletteCache& paletteCache;
    DrawMode drawMode = DrawMode::GLYPH_FILL;
    nscolor textStrokeColor = 0;
    nsAtom* fontPalette = nullptr;
    gfxPattern* textStrokePattern = nullptr;
    const mozilla::gfx::StrokeOptions* strokeOpts = nullptr;
    const mozilla::gfx::DrawOptions* drawOpts = nullptr;
    const PropertyProvider* provider = nullptr;
    // If non-null, the advance width of the substring is set.
    gfxFloat* advanceWidth = nullptr;
    mozilla::SVGContextPaint* contextPaint = nullptr;
    gfxTextRunDrawCallbacks* callbacks = nullptr;
    bool allowGDI = true;
    bool hasTextShadow = false;
    DrawParams(gfxContext* aContext, mozilla::gfx::PaletteCache& aPaletteCache)
        : context(aContext), paletteCache(aPaletteCache) {}
  };

  /**
   * Draws a substring. Uses only GetSpacing from aBreakProvider.
   * The provided point is the baseline origin on the left of the string
   * for LTR, on the right of the string for RTL.
   *
   * Drawing should respect advance widths in the sense that for LTR runs,
   *   Draw(Range(start, middle), pt, ...) followed by
   *   Draw(Range(middle, end), gfxPoint(pt.x + advance, pt.y), ...)
   * should have the same effect as
   *   Draw(Range(start, end), pt, ...)
   *
   * For RTL runs the rule is:
   *   Draw(Range(middle, end), pt, ...) followed by
   *   Draw(Range(start, middle), gfxPoint(pt.x + advance, pt.y), ...)
   * should have the same effect as
   *   Draw(Range(start, end), pt, ...)
   *
   * Glyphs should be drawn in logical content order, which can be significant
   * if they overlap (perhaps due to negative spacing).
   */
  void Draw(const Range aRange, const mozilla::gfx::Point aPt,
            const DrawParams& aParams) const;

  /**
   * Draws the emphasis marks for this text run. Uses only GetSpacing
   * from aProvider. The provided point is the baseline origin of the
   * line of emphasis marks.
   */
  void DrawEmphasisMarks(gfxContext* aContext, gfxTextRun* aMark,
                         gfxFloat aMarkAdvance, mozilla::gfx::Point aPt,
                         Range aRange, const PropertyProvider* aProvider,
                         mozilla::gfx::PaletteCache& aPaletteCache) const;

  /**
   * Computes the ReflowMetrics for a substring.
   * Uses GetSpacing from aBreakProvider.
   * @param aBoundingBoxType which kind of bounding box (loose/tight)
   */
  Metrics MeasureText(Range aRange, gfxFont::BoundingBoxType aBoundingBoxType,
                      DrawTarget* aDrawTargetForTightBoundingBox,
                      const PropertyProvider* aProvider) const;

  Metrics MeasureText(gfxFont::BoundingBoxType aBoundingBoxType,
                      DrawTarget* aDrawTargetForTightBoundingBox,
                      const PropertyProvider* aProvider = nullptr) const {
    return MeasureText(Range(this), aBoundingBoxType,
                       aDrawTargetForTightBoundingBox, aProvider);
  }

  void GetLineHeightMetrics(Range aRange, gfxFloat& aAscent,
                            gfxFloat& aDescent) const;
  void GetLineHeightMetrics(gfxFloat& aAscent, gfxFloat& aDescent) const {
    GetLineHeightMetrics(Range(this), aAscent, aDescent);
  }

  /**
   * Computes just the advance width for a substring.
   * Uses GetSpacing from aBreakProvider.
   * If aSpacing is not null, the spacing attached before and after
   * the substring would be returned in it. NOTE: the spacing is
   * included in the advance width.
   */
  gfxFloat GetAdvanceWidth(Range aRange, const PropertyProvider* aProvider,
                           PropertyProvider::Spacing* aSpacing = nullptr) const;

  gfxFloat GetAdvanceWidth() const {
    return GetAdvanceWidth(Range(this), nullptr);
  }

  /**
   * Computes the minimum advance width for a substring assuming line
   * breaking is allowed everywhere.
   */
  gfxFloat GetMinAdvanceWidth(Range aRange);

  /**
   * Clear all stored line breaks for the given range (both before and after),
   * and then set the line-break state before aRange.start to aBreakBefore and
   * after the last cluster to aBreakAfter.
   *
   * We require that before and after line breaks be consistent. For clusters
   * i and i+1, we require that if there is a break after cluster i, a break
   * will be specified before cluster i+1. This may be temporarily violated
   * (e.g. after reflowing line L and before reflowing line L+1); to handle
   * these temporary violations, we say that there is a break betwen i and i+1
   * if a break is specified after i OR a break is specified before i+1.
   *
   * This can change textrun geometry! The existence of a linebreak can affect
   * the advance width of the cluster before the break (when kerning) or the
   * geometry of one cluster before the break or any number of clusters
   * after the break. (The one-cluster-before-the-break limit is somewhat
   * arbitrary; if some scripts require breaking it, then we need to
   * alter nsTextFrame::TrimTrailingWhitespace, perhaps drastically becase
   * it could affect the layout of frames before it...)
   *
   * We return true if glyphs or geometry changed, false otherwise. This
   * function is virtual so that gfxTextRun subclasses can reshape
   * properly.
   *
   * @param aAdvanceWidthDelta if non-null, returns the change in advance
   * width of the given range.
   */
  virtual bool SetLineBreaks(Range aRange, bool aLineBreakBefore,
                             bool aLineBreakAfter,
                             gfxFloat* aAdvanceWidthDelta);

  enum SuppressBreak {
    eNoSuppressBreak,
    // Measure the range of text as if there is no break before it.
    eSuppressInitialBreak,
    // Measure the range of text as if it contains no break
    eSuppressAllBreaks
  };

  void ClassifyAutoHyphenations(uint32_t aStart, Range aRange,
                                nsTArray<HyphenType>& aHyphenBuffer,
                                HyphenationState* aWordState);

  // Struct used by BreakAndMeasureText to return the amount of trimmable
  // trailing whitespace included in the run.
  struct TrimmableWS {
    mozilla::gfx::Float mAdvance = 0;
    uint32_t mCount = 0;
  };

  /**
   * Finds the longest substring that will fit into the given width.
   * Uses GetHyphenationBreaks and GetSpacing from aProvider.
   * Guarantees the following:
   * -- 0 <= result <= aMaxLength
   * -- result is the maximal value of N such that either
   *       N < aMaxLength && line break at N &&
   *       GetAdvanceWidth(Range(aStart, N), aProvider) <= aWidth
   *   OR  N < aMaxLength && hyphen break at N &&
   *       GetAdvanceWidth(Range(aStart, N), aProvider) +
   *       GetHyphenWidth() <= aWidth
   *   OR  N == aMaxLength &&
   *       GetAdvanceWidth(Range(aStart, N), aProvider) <= aWidth
   * where GetAdvanceWidth assumes the effect of
   * SetLineBreaks(Range(aStart, N),
   *               aLineBreakBefore, N < aMaxLength, aProvider)
   * -- if no such N exists, then result is the smallest N such that
   *       N < aMaxLength && line break at N
   *   OR  N < aMaxLength && hyphen break at N
   *   OR  N == aMaxLength
   *
   * The call has the effect of
   * SetLineBreaks(Range(aStart, result), aLineBreakBefore,
   *               result < aMaxLength, aProvider)
   * and the returned metrics and the invariants above reflect this.
   *
   * @param aMaxLength this can be UINT32_MAX, in which case the length used
   * is up to the end of the string
   * @param aLineBreakBefore set to true if and only if there is an actual
   * line break at the start of this string.
   * @param aSuppressBreak what break should be suppressed.
   * @param aOutTrimmableWhitespace if non-null, returns the advance of any
   * run of trailing spaces that might be trimmed if the run ends up at
   * end-of-line.
   * Trimmable spaces are still counted in the "characters fit" result, and
   * contribute to the returned Metrics values.
   * @param aOutMetrics we fill this in for the returned substring.
   * If a hyphenation break was used, the hyphen is NOT included in the returned
   * metrics.
   * @param aBoundingBoxType whether to make the bounding box in aMetrics tight
   * @param aRefDrawTarget a reference DrawTarget to get the tight bounding box,
   * if requested
   * @param aOutUsedHyphenation records if we selected a hyphenation break
   * @param aOutLastBreak if result is aMaxLength, we set this to
   * the maximal N such that
   *       N < aMaxLength && line break at N &&
   *       GetAdvanceWidth(Range(aStart, N), aProvider) <= aWidth
   *   OR  N < aMaxLength && hyphen break at N &&
   *       GetAdvanceWidth(Range(aStart, N), aProvider) +
   *         GetHyphenWidth() <= aWidth
   * or UINT32_MAX if no such N exists, where GetAdvanceWidth assumes
   * the effect of
   * SetLineBreaks(Range(aStart, N), aLineBreakBefore,
   *               N < aMaxLength, aProvider)
   *
   * @param aCanWordWrap true if we can break between any two grapheme
   * clusters. This is set by overflow-wrap|word-wrap: break-word
   *
   * @param aBreakPriority in/out the priority of the break opportunity
   * saved in the line. If we are prioritizing break opportunities, we will
   * not set a break with a lower priority. @see gfxBreakPriority.
   *
   * Note that negative advance widths are possible especially if negative
   * spacing is provided.
   */
  uint32_t BreakAndMeasureText(
      uint32_t aStart, uint32_t aMaxLength, bool aLineBreakBefore,
      gfxFloat aWidth, const PropertyProvider& aProvider,
      SuppressBreak aSuppressBreak, gfxFont::BoundingBoxType aBoundingBoxType,
      DrawTarget* aRefDrawTarget, bool aCanWordWrap, bool aCanWhitespaceWrap,
      bool aIsBreakSpaces,
      // Output parameters:
      TrimmableWS* aOutTrimmableWhitespace,  // may be null
      Metrics& aOutMetrics, bool& aOutUsedHyphenation, uint32_t& aOutLastBreak,
      // In/out:
      gfxBreakPriority& aBreakPriority);

  // Utility getters

  void* GetUserData() const { return mUserData; }
  void SetUserData(void* aUserData) { mUserData = aUserData; }

  void SetFlagBits(nsTextFrameUtils::Flags aFlags) { mFlags2 |= aFlags; }
  void ClearFlagBits(nsTextFrameUtils::Flags aFlags) { mFlags2 &= ~aFlags; }
  const gfxSkipChars& GetSkipChars() const { return mSkipChars; }
  gfxFontGroup* GetFontGroup() const { return mFontGroup; }

  // Call this, don't call "new gfxTextRun" directly. This does custom
  // allocation and initialization
  static already_AddRefed<gfxTextRun> Create(
      const gfxTextRunFactory::Parameters* aParams, uint32_t aLength,
      gfxFontGroup* aFontGroup, mozilla::gfx::ShapedTextFlags aFlags,
      nsTextFrameUtils::Flags aFlags2);

  // The text is divided into GlyphRuns as necessary. (In the vast majority
  // of cases, a gfxTextRun contains just a single GlyphRun.)
  struct GlyphRun {
    RefPtr<gfxFont> mFont;      // never null in a valid GlyphRun
    uint32_t mCharacterOffset;  // into original UTF16 string
    mozilla::gfx::ShapedTextFlags
        mOrientation;  // gfxTextRunFactory::TEXT_ORIENT_* value
    FontMatchType mMatchType;
    bool mIsCJK;  // Whether the text was a CJK script run (used to decide if
                  // text-decoration-skip-ink should not be applied)

    // Set up the properties (but NOT offset) of the GlyphRun.
    void SetProperties(gfxFont* aFont,
                       mozilla::gfx::ShapedTextFlags aOrientation, bool aIsCJK,
                       FontMatchType aMatchType) {
      mFont = aFont;
      mOrientation = aOrientation;
      mIsCJK = aIsCJK;
      mMatchType = aMatchType;
    }

    // Return whether the GlyphRun matches the given properties;
    // the given FontMatchType will be added to the run if not present.
    bool Matches(gfxFont* aFont, mozilla::gfx::ShapedTextFlags aOrientation,
                 bool aIsCJK, FontMatchType aMatchType) {
      if (mFont == aFont && mOrientation == aOrientation && mIsCJK == aIsCJK) {
        mMatchType.kind |= aMatchType.kind;
        if (mMatchType.generic == mozilla::StyleGenericFontFamily::None) {
          mMatchType.generic = aMatchType.generic;
        }
        return true;
      }
      return false;
    }

    bool IsSidewaysLeft() const {
      return (mOrientation & mozilla::gfx::ShapedTextFlags::TEXT_ORIENT_MASK) ==
             mozilla::gfx::ShapedTextFlags::TEXT_ORIENT_VERTICAL_SIDEWAYS_LEFT;
    }

    bool IsSidewaysRight() const {
      return (mOrientation & mozilla::gfx::ShapedTextFlags::TEXT_ORIENT_MASK) ==
             mozilla::gfx::ShapedTextFlags::TEXT_ORIENT_VERTICAL_SIDEWAYS_RIGHT;
    }
  };

  // Script run codes that we will mark as CJK to suppress skip-ink behavior.
  static inline bool IsCJKScript(Script aScript) {
    switch (aScript) {
      case Script::BOPOMOFO:
      case Script::HAN:
      case Script::HANGUL:
      case Script::HIRAGANA:
      case Script::KATAKANA:
      case Script::KATAKANA_OR_HIRAGANA:
      case Script::SIMPLIFIED_HAN:
      case Script::TRADITIONAL_HAN:
      case Script::JAPANESE:
      case Script::KOREAN:
      case Script::HAN_WITH_BOPOMOFO:
      case Script::JAMO:
        return true;
      default:
        return false;
    }
  }

  class MOZ_STACK_CLASS GlyphRunIterator {
   public:
    GlyphRunIterator(const gfxTextRun* aTextRun, Range aRange,
                     bool aReverse = false)
        : mTextRun(aTextRun),
          mStartOffset(aRange.start),
          mEndOffset(aRange.end),
          mReverse(aReverse) {
      mGlyphRun = mTextRun->FindFirstGlyphRunContaining(
          aReverse ? aRange.end - 1 : aRange.start);
      if (!mGlyphRun) {
        mStringEnd = mStringStart = mStartOffset;
        return;
      }
      uint32_t glyphRunEndOffset = mGlyphRun == mTextRun->mGlyphRuns.end() - 1
                                       ? mTextRun->GetLength()
                                       : (mGlyphRun + 1)->mCharacterOffset;
      mStringEnd = std::min(mEndOffset, glyphRunEndOffset);
      mStringStart = std::max(mStartOffset, mGlyphRun->mCharacterOffset);
    }
    void NextRun();
    bool AtEnd() const { return mGlyphRun == nullptr; }
    const struct GlyphRun* GlyphRun() const { return mGlyphRun; }
    uint32_t StringStart() const { return mStringStart; }
    uint32_t StringEnd() const { return mStringEnd; }

   private:
    const gfxTextRun* mTextRun;
    const struct GlyphRun* mGlyphRun;
    uint32_t mStringStart;
    uint32_t mStringEnd;
    uint32_t mStartOffset;
    uint32_t mEndOffset;
    bool mReverse;
  };

  class GlyphRunOffsetComparator {
   public:
    bool Equals(const GlyphRun& a, const GlyphRun& b) const {
      return a.mCharacterOffset == b.mCharacterOffset;
    }

    bool LessThan(const GlyphRun& a, const GlyphRun& b) const {
      return a.mCharacterOffset < b.mCharacterOffset;
    }
  };

  // API for setting up the textrun glyphs. Should only be called by
  // things that construct textruns.
  /**
   * We've found a run of text that should use a particular font. Call this
   * only during initialization when font substitution has been computed.
   * Call it before setting up the glyphs for the characters in this run;
   * SetMissingGlyph requires that the correct glyphrun be installed.
   *
   * If aForceNewRun, a new glyph run will be added, even if the
   * previously added run uses the same font.  If glyph runs are
   * added out of strictly increasing aStartCharIndex order (via
   * force), then SortGlyphRuns must be called after all glyph runs
   * are added before any further operations are performed with this
   * TextRun.
   */
  void AddGlyphRun(gfxFont* aFont, FontMatchType aMatchType,
                   uint32_t aUTF16Offset, bool aForceNewRun,
                   mozilla::gfx::ShapedTextFlags aOrientation, bool aIsCJK);
  void ResetGlyphRuns() { mGlyphRuns.Clear(); }
  void SanitizeGlyphRuns();

  const CompressedGlyph* GetCharacterGlyphs() const final {
    MOZ_ASSERT(mCharacterGlyphs, "failed to initialize mCharacterGlyphs");
    return mCharacterGlyphs;
  }
  CompressedGlyph* GetCharacterGlyphs() final {
    MOZ_ASSERT(mCharacterGlyphs, "failed to initialize mCharacterGlyphs");
    return mCharacterGlyphs;
  }

  // clean out results from shaping in progress, used for fallback scenarios
  void ClearGlyphsAndCharacters();

  void SetSpaceGlyph(gfxFont* aFont, DrawTarget* aDrawTarget,
                     uint32_t aCharIndex,
                     mozilla::gfx::ShapedTextFlags aOrientation);

  // Set the glyph data for the given character index to the font's
  // space glyph, IF this can be done as a "simple" glyph record
  // (not requiring a DetailedGlyph entry). This avoids the need to call
  // the font shaper and go through the shaped-word cache for most spaces.
  //
  // The parameter aSpaceChar is the original character code for which
  // this space glyph is being used; if this is U+0020, we need to record
  // that it could be trimmed at a run edge, whereas other kinds of space
  // (currently just U+00A0) would not be trimmable/breakable.
  //
  // Returns true if it was able to set simple glyph data for the space;
  // if it returns false, the caller needs to fall back to some other
  // means to create the necessary (detailed) glyph data.
  bool SetSpaceGlyphIfSimple(gfxFont* aFont, uint32_t aCharIndex,
                             char16_t aSpaceChar,
                             mozilla::gfx::ShapedTextFlags aOrientation);

  // Record the positions of specific characters that layout may need to
  // detect in the textrun, even though it doesn't have an explicit copy
  // of the original text. These are recorded using flag bits in the
  // CompressedGlyph record; if necessary, we convert "simple" glyph records
  // to "complex" ones as the Tab and Newline flags are not present in
  // simple CompressedGlyph records.
  void SetIsTab(uint32_t aIndex) { EnsureComplexGlyph(aIndex).SetIsTab(); }
  void SetIsNewline(uint32_t aIndex) {
    EnsureComplexGlyph(aIndex).SetIsNewline();
  }
  void SetNoEmphasisMark(uint32_t aIndex) {
    EnsureComplexGlyph(aIndex).SetNoEmphasisMark();
  }
  void SetIsFormattingControl(uint32_t aIndex) {
    EnsureComplexGlyph(aIndex).SetIsFormattingControl();
  }

  /**
   * Prefetch all the glyph extents needed to ensure that Measure calls
   * on this textrun not requesting tight boundingBoxes will succeed. Note
   * that some glyph extents might not be fetched due to OOM or other
   * errors.
   */
  void FetchGlyphExtents(DrawTarget* aRefDrawTarget) const;

  const GlyphRun* GetGlyphRuns(uint32_t* aNumGlyphRuns) const {
    *aNumGlyphRuns = mGlyphRuns.Length();
    return mGlyphRuns.begin();
  }

  uint32_t GlyphRunCount() const { return mGlyphRuns.Length(); }

  const GlyphRun* TrailingGlyphRun() const {
    return mGlyphRuns.IsEmpty() ? nullptr : mGlyphRuns.end() - 1;
  }

  // Returns the GlyphRun containing the given offset.
  // (Returns mGlyphRuns.end()-1 when aOffset is mCharacterCount; returns
  // nullptr if textrun is empty and no glyph runs are present.)
  const GlyphRun* FindFirstGlyphRunContaining(uint32_t aOffset) const;

  // Copy glyph data from a ShapedWord into this textrun.
  void CopyGlyphDataFrom(gfxShapedWord* aSource, uint32_t aStart);

  // Copy glyph data for a range of characters from aSource to this
  // textrun.
  void CopyGlyphDataFrom(gfxTextRun* aSource, Range aRange, uint32_t aDest);

  // Tell the textrun to release its reference to its creating gfxFontGroup
  // immediately, rather than on destruction. This is used for textruns
  // that are actually owned by a gfxFontGroup, so that they don't keep it
  // permanently alive due to a circular reference. (The caller of this is
  // taking responsibility for ensuring the textrun will not outlive its
  // mFontGroup.)
  void ReleaseFontGroup();

  struct LigatureData {
    // textrun range of the containing ligature
    Range mRange;
    // appunits advance to the start of the ligature part within the ligature;
    // never includes any spacing
    gfxFloat mPartAdvance;
    // appunits width of the ligature part; includes before-spacing
    // when the part is at the start of the ligature, and after-spacing
    // when the part is as the end of the ligature
    gfxFloat mPartWidth;

    bool mClipBeforePart;
    bool mClipAfterPart;
  };

  // return storage used by this run, for memory reporter;
  // nsTransformedTextRun needs to override this as it holds additional data
  virtual size_t SizeOfExcludingThis(mozilla::MallocSizeOf aMallocSizeOf)
      MOZ_MUST_OVERRIDE;
  virtual size_t SizeOfIncludingThis(mozilla::MallocSizeOf aMallocSizeOf)
      MOZ_MUST_OVERRIDE;

  nsTextFrameUtils::Flags GetFlags2() const { return mFlags2; }

  // Get the size, if it hasn't already been gotten, marking as it goes.
  size_t MaybeSizeOfIncludingThis(mozilla::MallocSizeOf aMallocSizeOf) {
    if (mFlags2 & nsTextFrameUtils::Flags::RunSizeAccounted) {
      return 0;
    }
    mFlags2 |= nsTextFrameUtils::Flags::RunSizeAccounted;
    return SizeOfIncludingThis(aMallocSizeOf);
  }
  void ResetSizeOfAccountingFlags() {
    mFlags2 &= ~nsTextFrameUtils::Flags::RunSizeAccounted;
  }

  // shaping state - for some font features, fallback is required that
  // affects the entire run. for example, fallback for one script/font
  // portion of a textrun requires fallback to be applied to the entire run

  enum ShapingState : uint8_t {
    eShapingState_Normal,               // default state
    eShapingState_ShapingWithFeature,   // have shaped with feature
    eShapingState_ShapingWithFallback,  // have shaped with fallback
    eShapingState_Aborted,              // abort initial iteration
    eShapingState_ForceFallbackFeature  // redo with fallback forced on
  };

  ShapingState GetShapingState() const { return mShapingState; }
  void SetShapingState(ShapingState aShapingState) {
    mShapingState = aShapingState;
  }

  int32_t GetAdvanceForGlyph(uint32_t aIndex) const {
    const CompressedGlyph& glyphData = mCharacterGlyphs[aIndex];
    if (glyphData.IsSimpleGlyph()) {
      return glyphData.GetSimpleAdvance();
    }
    uint32_t glyphCount = glyphData.GetGlyphCount();
    if (!glyphCount) {
      return 0;
    }
    const DetailedGlyph* details = GetDetailedGlyphs(aIndex);
    int32_t advance = 0;
    for (uint32_t j = 0; j < glyphCount; ++j, ++details) {
      advance += details->mAdvance;
    }
    return advance;
  }

#ifdef DEBUG_FRAME_DUMP
  void Dump(FILE* aOutput = stderr);
#endif

 protected:
  /**
   * Create a textrun, and set its mCharacterGlyphs to point immediately
   * after the base object; this is ONLY used in conjunction with placement
   * new, after allocating a block large enough for the glyph records to
   * follow the base textrun object.
   */
  gfxTextRun(const gfxTextRunFactory::Parameters* aParams, uint32_t aLength,
             gfxFontGroup* aFontGroup, mozilla::gfx::ShapedTextFlags aFlags,
             nsTextFrameUtils::Flags aFlags2);

  // Whether we need to fetch actual glyph extents from the fonts.
  bool NeedsGlyphExtents() const;

  /**
   * Helper for the Create() factory method to allocate the required
   * glyph storage for a textrun object with the basic size aSize,
   * plus room for aLength glyph records.
   */
  static void* AllocateStorageForTextRun(size_t aSize, uint32_t aLength);

  // Pointer to the array of CompressedGlyph records; must be initialized
  // when the object is constructed.
  CompressedGlyph* mCharacterGlyphs;

 private:
  // **** general helpers ****

  // Get the total advance for a range of glyphs.
  int32_t GetAdvanceForGlyphs(Range aRange) const;

  // Spacing for characters outside the range aSpacingStart/aSpacingEnd
  // is assumed to be zero; such characters are not passed to aProvider.
  // This is useful to protect aProvider from being passed character indices
  // it is not currently able to handle.
  bool GetAdjustedSpacingArray(
      Range aRange, const PropertyProvider* aProvider, Range aSpacingRange,
      nsTArray<PropertyProvider::Spacing>* aSpacing) const;

  CompressedGlyph& EnsureComplexGlyph(uint32_t aIndex) {
    gfxShapedText::EnsureComplexGlyph(aIndex, mCharacterGlyphs[aIndex]);
    return mCharacterGlyphs[aIndex];
  }

  //  **** ligature helpers ****
  // (Platforms do the actual ligaturization, but we need to do a bunch of stuff
  // to handle requests that begin or end inside a ligature)

  // if aProvider is null then mBeforeSpacing and mAfterSpacing are set to zero
  LigatureData ComputeLigatureData(Range aPartRange,
                                   const PropertyProvider* aProvider) const;
  gfxFloat ComputePartialLigatureWidth(Range aPartRange,
                                       const PropertyProvider* aProvider) const;
  void DrawPartialLigature(gfxFont* aFont, Range aRange,
                           mozilla::gfx::Point* aPt,
                           const PropertyProvider* aProvider,
                           TextRunDrawParams& aParams,
                           mozilla::gfx::ShapedTextFlags aOrientation) const;
  // Advance aRange.start to the start of the nearest ligature, back
  // up aRange.end to the nearest ligature end; may result in
  // aRange->start == aRange->end.
  // Returns whether any adjustment was made.
  bool ShrinkToLigatureBoundaries(Range* aRange) const;
  // result in appunits
  gfxFloat GetPartialLigatureWidth(Range aRange,
                                   const PropertyProvider* aProvider) const;
  void AccumulatePartialLigatureMetrics(
      gfxFont* aFont, Range aRange, gfxFont::BoundingBoxType aBoundingBoxType,
      DrawTarget* aRefDrawTarget, const PropertyProvider* aProvider,
      mozilla::gfx::ShapedTextFlags aOrientation, Metrics* aMetrics) const;

  // **** measurement helper ****
  void AccumulateMetricsForRun(gfxFont* aFont, Range aRange,
                               gfxFont::BoundingBoxType aBoundingBoxType,
                               DrawTarget* aRefDrawTarget,
                               const PropertyProvider* aProvider,
                               Range aSpacingRange,
                               mozilla::gfx::ShapedTextFlags aOrientation,
                               Metrics* aMetrics) const;

  // **** drawing helper ****
  void DrawGlyphs(gfxFont* aFont, Range aRange, mozilla::gfx::Point* aPt,
                  const PropertyProvider* aProvider, Range aSpacingRange,
                  TextRunDrawParams& aParams,
                  mozilla::gfx::ShapedTextFlags aOrientation) const;

  // The textrun holds either a single GlyphRun -or- an array.
  mozilla::ElementOrArray<GlyphRun> mGlyphRuns;

  void* mUserData;

  // mFontGroup is usually a strong reference, but refcounting is managed
  // manually because it may be explicitly released by ReleaseFontGroup()
  // in the case where the font group actually owns the textrun.
  gfxFontGroup* MOZ_OWNING_REF mFontGroup;

  gfxSkipChars mSkipChars;

  nsTextFrameUtils::Flags
      mFlags2;  // additional flags (see also gfxShapedText::mFlags)

  bool mDontSkipDrawing;  // true if the text run must not skip drawing, even if
                          // waiting for a user font download, e.g. because we
                          // are using it to draw canvas text
  bool mReleasedFontGroup;                // we already called NS_RELEASE on
                                          // mFontGroup, so don't do it again
  bool mReleasedFontGroupSkippedDrawing;  // whether our old mFontGroup value
                                          // was set to skip drawing

  // shaping state for handling variant fallback features
  // such as subscript/superscript variant glyphs
  ShapingState mShapingState;
};

class gfxFontGroup final : public gfxTextRunFactory {
 public:
  typedef mozilla::intl::Script Script;
  typedef gfxShapedText::CompressedGlyph CompressedGlyph;
  friend class MathMLTextRunFactory;
  friend class nsCaseTransformTextRunFactory;

  static void
  Shutdown();  // platform must call this to release the languageAtomService

  gfxFontGroup(nsPresContext* aPresContext,
               const mozilla::StyleFontFamilyList& aFontFamilyList,
               const gfxFontStyle* aStyle, nsAtom* aLanguage,
               bool aExplicitLanguage, gfxTextPerfMetrics* aTextPerf,
               gfxUserFontSet* aUserFontSet, gfxFloat aDevToCssSize,
               StyleFontVariantEmoji aVariantEmoji);

  virtual ~gfxFontGroup();

  gfxFontGroup(const gfxFontGroup& aOther) = delete;

  // Returns first valid font in the fontlist or default font.
  // Initiates userfont loads if userfont not loaded.
  // aCh: character to look for, or kCSSFirstAvailableFont for default "first
  //      available font" as defined by CSS Fonts (i.e. the first font whose
  //      unicode-range includes <space>, but does not require space to
  //      actually be present)
  // aGeneric: if non-null, returns the CSS generic type that was mapped to
  //           this font
  // aIsFirst: if non-null, returns whether the font was first in the list
  static constexpr uint32_t kCSSFirstAvailableFont = UINT32_MAX;
  already_AddRefed<gfxFont> GetFirstValidFont(
      uint32_t aCh = kCSSFirstAvailableFont,
      mozilla::StyleGenericFontFamily* aGeneric = nullptr,
      bool* aIsFirst = nullptr);

  // Returns the first font in the font-group that has an OpenType MATH table,
  // or null if no such font is available. The GetMathConstant methods may be
  // called on the returned font.
  already_AddRefed<gfxFont> GetFirstMathFont();

  const gfxFontStyle* GetStyle() const { return &mStyle; }

  // Get the presContext for which this fontGroup was constructed. This may be
  // null! (In the case of canvas not connected to a document.)
  nsPresContext* GetPresContext() const { return mPresContext; }

  /**
   * The listed characters should be treated as invisible and zero-width
   * when creating textruns.
   */
  static bool IsInvalidChar(uint8_t ch);
  static bool IsInvalidChar(char16_t ch);

  /**
   * Make a textrun for a given string.
   * If aText is not persistent (aFlags & TEXT_IS_PERSISTENT), the
   * textrun will copy it.
   * This calls FetchGlyphExtents on the textrun.
   */
  template <typename T>
  already_AddRefed<gfxTextRun> MakeTextRun(const T* aString, uint32_t aLength,
                                           const Parameters* aParams,
                                           mozilla::gfx::ShapedTextFlags aFlags,
                                           nsTextFrameUtils::Flags aFlags2,
                                           gfxMissingFontRecorder* aMFR);

  /**
   * Textrun creation helper for clients that don't want to pass
   * a full Parameters record.
   */
  template <typename T>
  already_AddRefed<gfxTextRun> MakeTextRun(const T* aString, uint32_t aLength,
                                           DrawTarget* aRefDrawTarget,
                                           int32_t aAppUnitsPerDevUnit,
                                           mozilla::gfx::ShapedTextFlags aFlags,
                                           nsTextFrameUtils::Flags aFlags2,
                                           gfxMissingFontRecorder* aMFR) {
    gfxTextRunFactory::Parameters params = {
        aRefDrawTarget, nullptr, nullptr, nullptr, 0, aAppUnitsPerDevUnit};
    return MakeTextRun(aString, aLength, &params, aFlags, aFlags2, aMFR);
  }

  // Get the (possibly-cached) width of the hyphen character.
  gfxFloat GetHyphenWidth(const gfxTextRun::PropertyProvider* aProvider);

  /**
   * Make a text run representing a single hyphen character.
   * This will use U+2010 HYPHEN if available in the first font,
   * otherwise fall back to U+002D HYPHEN-MINUS.
   * The caller is responsible for deleting the returned text run
   * when no longer required.
   */
  already_AddRefed<gfxTextRun> MakeHyphenTextRun(
      DrawTarget* aDrawTarget, mozilla::gfx::ShapedTextFlags aFlags,
      uint32_t aAppUnitsPerDevUnit);

  /**
   * Check whether a given font (specified by its gfxFontEntry)
   * is already in the fontgroup's list of actual fonts
   */
  bool HasFont(const gfxFontEntry* aFontEntry);

  // This returns the preferred underline for this font group.
  // Some CJK fonts have wrong underline offset in its metrics.
  // If this group has such "bad" font, each platform's gfxFontGroup
  // initialized mUnderlineOffset. The value should be lower value of
  // first font's metrics and the bad font's metrics. Otherwise, this
  // returns from first font's metrics.
  static constexpr gfxFloat UNDERLINE_OFFSET_NOT_SET = INT16_MAX;
  gfxFloat GetUnderlineOffset();

  already_AddRefed<gfxFont> FindFontForChar(uint32_t ch, uint32_t prevCh,
                                            uint32_t aNextCh, Script aRunScript,
                                            gfxFont* aPrevMatchedFont,
                                            FontMatchType* aMatchType);

  gfxUserFontSet* GetUserFontSet();

  // With downloadable fonts, the composition of the font group can change as
  // fonts are downloaded for each change in state of the user font set, the
  // generation value is bumped to avoid picking up previously created text runs
  // in the text run word cache.  For font groups based on stylesheets with no
  // @font-face rule, this always returns 0.
  uint64_t GetGeneration();

  // generation of the latest fontset rebuild, 0 when no fontset present
  uint64_t GetRebuildGeneration();

  // used when logging text performance
  gfxTextPerfMetrics* GetTextPerfMetrics() const { return mTextPerf; }

  // This will call UpdateUserFonts() if the user font set is changed.
  void SetUserFontSet(gfxUserFontSet* aUserFontSet);

  void ClearCachedData() {
    mUnderlineOffset = UNDERLINE_OFFSET_NOT_SET;
    mSkipDrawing = false;
    mHyphenWidth = -1;
    mCachedEllipsisTextRun = nullptr;
  }

  // If there is a user font set, check to see whether the font list or any
  // caches need updating.
  void UpdateUserFonts();

  // search for a specific userfont in the list of fonts
  bool ContainsUserFont(const gfxUserFontEntry* aUserFont);

  bool ShouldSkipDrawing() const { return mSkipDrawing; }

  class LazyReferenceDrawTargetGetter {
   public:
    virtual already_AddRefed<DrawTarget> GetRefDrawTarget() = 0;
  };
  // The gfxFontGroup keeps ownership of this textrun.
  // It is only guaranteed to exist until the next call to GetEllipsisTextRun
  // (which might use a different appUnitsPerDev value or flags) for the font
  // group, or until UpdateUserFonts is called, or the fontgroup is destroyed.
  // Get it/use it/forget it :) - don't keep a reference that might go stale.
  gfxTextRun* GetEllipsisTextRun(
      int32_t aAppUnitsPerDevPixel, mozilla::gfx::ShapedTextFlags aFlags,
      LazyReferenceDrawTargetGetter& aRefDrawTargetGetter);

  nsAtom* Language() const { return mLanguage.get(); }

  // Get font metrics to be used as the basis for CSS font-relative units.
  // Note that these may be a "composite" of metrics from multiple fonts,
  // because the 'ch' and 'ic' units depend on the font that would be used
  // to render specific characters, not simply the "first available" font.
  // https://drafts.csswg.org/css-values-4/#ch
  // https://drafts.csswg.org/css-values-4/#ic
  // Whether extra font resources may be loaded to resolve 'ch' and 'ic'
  // depends on the corresponding flags passed by the caller.
  gfxFont::Metrics GetMetricsForCSSUnits(
      gfxFont::Orientation aOrientation,
      mozilla::StyleQueryFontMetricsFlags aFlags);

 protected:
  friend class mozilla::PostTraversalTask;
  friend class DeferredClearResolvedFonts;

  struct TextRange {
    TextRange(uint32_t aStart, uint32_t aEnd, gfxFont* aFont,
              FontMatchType aMatchType,
              mozilla::gfx::ShapedTextFlags aOrientation)
        : start(aStart),
          end(aEnd),
          font(aFont),
          matchType(aMatchType),
          orientation(aOrientation) {}
    uint32_t Length() const { return end - start; }
    uint32_t start, end;
    RefPtr<gfxFont> font;
    FontMatchType matchType;
    mozilla::gfx::ShapedTextFlags orientation;
  };

  // search through pref fonts for a character, return nullptr if no matching
  // pref font
  already_AddRefed<gfxFont> WhichPrefFontSupportsChar(
      uint32_t aCh, uint32_t aNextCh, FontPresentation aPresentation);

  already_AddRefed<gfxFont> WhichSystemFontSupportsChar(
      uint32_t aCh, uint32_t aNextCh, Script aRunScript,
      FontPresentation aPresentation);

  template <typename T>
  void ComputeRanges(nsTArray<TextRange>& aRanges, const T* aString,
                     uint32_t aLength, Script aRunScript,
                     mozilla::gfx::ShapedTextFlags aOrientation);

  class FamilyFace {
   public:
    FamilyFace()
        : mOwnedFamily(nullptr),
          mFontEntry(nullptr),
          mGeneric(mozilla::StyleGenericFontFamily::None),
          mFontCreated(false),
          mLoading(false),
          mInvalid(false),
          mCheckForFallbackFaces(false),
          mIsSharedFamily(false),
          mHasFontEntry(false) {}

    FamilyFace(gfxFontFamily* aFamily, gfxFont* aFont,
               mozilla::StyleGenericFontFamily aGeneric)
        : mOwnedFamily(aFamily),
          mGeneric(aGeneric),
          mFontCreated(true),
          mLoading(false),
          mInvalid(false),
          mCheckForFallbackFaces(false),
          mIsSharedFamily(false),
          mHasFontEntry(false) {
      NS_ASSERTION(aFont, "font pointer must not be null");
      NS_ASSERTION(!aFamily || aFamily->ContainsFace(aFont->GetFontEntry()),
                   "font is not a member of the given family");
      NS_IF_ADDREF(aFamily);
      mFont = aFont;
      NS_ADDREF(aFont);
    }

    FamilyFace(gfxFontFamily* aFamily, gfxFontEntry* aFontEntry,
               mozilla::StyleGenericFontFamily aGeneric)
        : mOwnedFamily(aFamily),
          mGeneric(aGeneric),
          mFontCreated(false),
          mLoading(false),
          mInvalid(false),
          mCheckForFallbackFaces(false),
          mIsSharedFamily(false),
          mHasFontEntry(true) {
      NS_ASSERTION(aFontEntry, "font entry pointer must not be null");
      NS_ASSERTION(!aFamily || aFamily->ContainsFace(aFontEntry),
                   "font is not a member of the given family");
      NS_IF_ADDREF(aFamily);
      mFontEntry = aFontEntry;
      NS_ADDREF(aFontEntry);
    }

    FamilyFace(mozilla::fontlist::Family* aFamily, gfxFontEntry* aFontEntry,
               mozilla::StyleGenericFontFamily aGeneric)
        : mSharedFamily(aFamily),
          mGeneric(aGeneric),
          mFontCreated(false),
          mLoading(false),
          mInvalid(false),
          mCheckForFallbackFaces(false),
          mIsSharedFamily(true),
          mHasFontEntry(true) {
      MOZ_ASSERT(aFamily && aFontEntry && aFontEntry->mShmemFace);
      mFontEntry = aFontEntry;
      NS_ADDREF(aFontEntry);
    }

    FamilyFace(const FamilyFace& aOtherFamilyFace)
        : mGeneric(aOtherFamilyFace.mGeneric),
          mFontCreated(aOtherFamilyFace.mFontCreated),
          mLoading(aOtherFamilyFace.mLoading),
          mInvalid(aOtherFamilyFace.mInvalid),
          mCheckForFallbackFaces(aOtherFamilyFace.mCheckForFallbackFaces),
          mIsSharedFamily(aOtherFamilyFace.mIsSharedFamily),
          mHasFontEntry(aOtherFamilyFace.mHasFontEntry) {
      if (mIsSharedFamily) {
        mSharedFamily = aOtherFamilyFace.mSharedFamily;
        if (mFontCreated) {
          mFont = aOtherFamilyFace.mFont;
          NS_ADDREF(mFont);
        } else if (mHasFontEntry) {
          mFontEntry = aOtherFamilyFace.mFontEntry;
          NS_ADDREF(mFontEntry);
        } else {
          mSharedFace = aOtherFamilyFace.mSharedFace;
        }
      } else {
        mOwnedFamily = aOtherFamilyFace.mOwnedFamily;
        NS_IF_ADDREF(mOwnedFamily);
        if (mFontCreated) {
          mFont = aOtherFamilyFace.mFont;
          NS_ADDREF(mFont);
        } else {
          mFontEntry = aOtherFamilyFace.mFontEntry;
          NS_IF_ADDREF(mFontEntry);
        }
      }
    }

    ~FamilyFace() {
      if (mFontCreated) {
        NS_RELEASE(mFont);
      }
      if (!mIsSharedFamily) {
        NS_IF_RELEASE(mOwnedFamily);
      }
      if (mHasFontEntry) {
        NS_RELEASE(mFontEntry);
      }
    }

    FamilyFace& operator=(const FamilyFace& aOther) {
      if (mFontCreated) {
        NS_RELEASE(mFont);
      }
      if (!mIsSharedFamily) {
        NS_IF_RELEASE(mOwnedFamily);
      }
      if (mHasFontEntry) {
        NS_RELEASE(mFontEntry);
      }

      mGeneric = aOther.mGeneric;
      mFontCreated = aOther.mFontCreated;
      mLoading = aOther.mLoading;
      mInvalid = aOther.mInvalid;
      mIsSharedFamily = aOther.mIsSharedFamily;
      mHasFontEntry = aOther.mHasFontEntry;

      if (mIsSharedFamily) {
        mSharedFamily = aOther.mSharedFamily;
        if (mFontCreated) {
          mFont = aOther.mFont;
          NS_ADDREF(mFont);
        } else if (mHasFontEntry) {
          mFontEntry = aOther.mFontEntry;
          NS_ADDREF(mFontEntry);
        } else {
          mSharedFace = aOther.mSharedFace;
        }
      } else {
        mOwnedFamily = aOther.mOwnedFamily;
        NS_IF_ADDREF(mOwnedFamily);
        if (mFontCreated) {
          mFont = aOther.mFont;
          NS_ADDREF(mFont);
        } else {
          mFontEntry = aOther.mFontEntry;
          NS_IF_ADDREF(mFontEntry);
        }
      }

      return *this;
    }

    gfxFontFamily* OwnedFamily() const {
      MOZ_ASSERT(!mIsSharedFamily);
      return mOwnedFamily;
    }
    mozilla::fontlist::Family* SharedFamily() const {
      MOZ_ASSERT(mIsSharedFamily);
      return mSharedFamily;
    }
    gfxFont* Font() const { return mFontCreated ? mFont : nullptr; }

    gfxFontEntry* FontEntry() const {
      if (mFontCreated) {
        return mFont->GetFontEntry();
      }
      if (mHasFontEntry) {
        return mFontEntry;
      }
      if (mIsSharedFamily) {
        return gfxPlatformFontList::PlatformFontList()->GetOrCreateFontEntry(
            mSharedFace, SharedFamily());
      }
      return nullptr;
    }

    mozilla::StyleGenericFontFamily Generic() const { return mGeneric; }

    bool IsSharedFamily() const { return mIsSharedFamily; }
    bool IsUserFontContainer() const {
      gfxFontEntry* fe = FontEntry();
      return fe && fe->mIsUserFontContainer;
    }
    bool IsLoading() const { return mLoading; }
    bool IsInvalid() const { return mInvalid; }
    void CheckState(bool& aSkipDrawing);
    void SetLoading(bool aIsLoading) { mLoading = aIsLoading; }
    void SetInvalid() { mInvalid = true; }
    bool CheckForFallbackFaces() const { return mCheckForFallbackFaces; }
    void SetCheckForFallbackFaces() { mCheckForFallbackFaces = true; }

    // Return true if we're currently loading (or waiting for) a resource that
    // may support the given character.
    bool IsLoadingFor(uint32_t aCh) {
      if (!IsLoading()) {
        return false;
      }
      MOZ_ASSERT(IsUserFontContainer());
      auto* ufe = static_cast<gfxUserFontEntry*>(FontEntry());
      return ufe && ufe->CharacterInUnicodeRange(aCh);
    }

    void SetFont(gfxFont* aFont) {
      NS_ASSERTION(aFont, "font pointer must not be null");
      NS_ADDREF(aFont);
      if (mFontCreated) {
        NS_RELEASE(mFont);
      } else if (mHasFontEntry) {
        NS_RELEASE(mFontEntry);
        mHasFontEntry = false;
      }
      mFont = aFont;
      mFontCreated = true;
      mLoading = false;
    }

    bool EqualsUserFont(const gfxUserFontEntry* aUserFont) const;

   private:
    union {
      gfxFontFamily* MOZ_OWNING_REF mOwnedFamily;
      mozilla::fontlist::Family* MOZ_NON_OWNING_REF mSharedFamily;
    };
    // either a font or a font entry exists
    union {
      // Whichever of these fields is actually present will be a strong
      // reference, with refcounting handled manually.
      gfxFont* MOZ_OWNING_REF mFont;
      gfxFontEntry* MOZ_OWNING_REF mFontEntry;
      mozilla::fontlist::Face* MOZ_NON_OWNING_REF mSharedFace;
    };
    mozilla::StyleGenericFontFamily mGeneric;
    bool mFontCreated : 1;
    bool mLoading : 1;
    bool mInvalid : 1;
    bool mCheckForFallbackFaces : 1;
    bool mIsSharedFamily : 1;
    bool mHasFontEntry : 1;
  };

  nsPresContext* mPresContext = nullptr;

  // List of font families, either named or generic.
  // Generic names map to system pref fonts based on language.
  mozilla::StyleFontFamilyList mFamilyList;

  // Fontlist containing a font entry for each family found. gfxFont objects
  // are created as needed and userfont loads are initiated when needed.
  // Code should be careful about addressing this array directly.
  nsTArray<FamilyFace> mFonts;

  RefPtr<gfxFont> mDefaultFont;
  gfxFontStyle mStyle;

  RefPtr<nsAtom> mLanguage;

  gfxFloat mUnderlineOffset;
  gfxFloat mHyphenWidth;
  gfxFloat mDevToCssSize;

  RefPtr<gfxUserFontSet> mUserFontSet;
  uint64_t mCurrGeneration;  // track the current user font set generation,
                             // rebuild font list if needed

  gfxTextPerfMetrics* mTextPerf;

  // Cache a textrun representing an ellipsis (useful for CSS text-overflow)
  // at a specific appUnitsPerDevPixel size and orientation
  RefPtr<gfxTextRun> mCachedEllipsisTextRun;

  // cache the most recent pref font to avoid general pref font lookup
  FontFamily mLastPrefFamily;
  RefPtr<gfxFont> mLastPrefFont;
  eFontPrefLang mLastPrefLang;  // lang group for last pref font
  eFontPrefLang mPageLang;
  bool mLastPrefFirstFont;  // is this the first font in the list of pref fonts
                            // for this lang group?

  bool mSkipDrawing;  // hide text while waiting for a font
                      // download to complete (or fallback
                      // timer to fire)

  bool mExplicitLanguage;  // Does mLanguage come from an explicit attribute?

  bool mResolvedFonts = false;  // Whether the mFonts array has been set up.

  FontPresentation mEmojiPresentation = FontPresentation::Any;

  // Generic font family used to select among font prefs during fallback.
  mozilla::StyleGenericFontFamily mFallbackGeneric =
      mozilla::StyleGenericFontFamily::None;

  uint32_t mFontListGeneration = 0;  // platform font list generation for this
                                     // fontgroup

  /**
   * Textrun creation short-cuts for special cases where we don't need to
   * call a font shaper to generate glyphs.
   */
  already_AddRefed<gfxTextRun> MakeEmptyTextRun(
      const Parameters* aParams, mozilla::gfx::ShapedTextFlags aFlags,
      nsTextFrameUtils::Flags aFlags2);

  already_AddRefed<gfxTextRun> MakeSpaceTextRun(
      const Parameters* aParams, mozilla::gfx::ShapedTextFlags aFlags,
      nsTextFrameUtils::Flags aFlags2);

  template <typename T>
  already_AddRefed<gfxTextRun> MakeBlankTextRun(
      const T* aString, uint32_t aLength, const Parameters* aParams,
      mozilla::gfx::ShapedTextFlags aFlags, nsTextFrameUtils::Flags aFlags2);

  // Ensure the font-family list & style properties from CSS/prefs/defaults is
  // resolved to the array of available font faces we'll actually use.
  void EnsureFontList();

  // Get the font at index i within the fontlist, for character aCh (in case
  // of fonts with multiple resources and unicode-range partitioning).
  // Will initiate userfont load if not already loaded.
  // May return null if userfont not loaded or if font invalid.
  // If *aLoading is true, a relevant resource is already being loaded so no
  // new download will be initiated; if a download is started, *aLoading will
  // be set to true on return.
  already_AddRefed<gfxFont> GetFontAt(uint32_t i, uint32_t aCh, bool* aLoading);

  // Simplified version of GetFontAt() for use where we just need a font for
  // metrics, math layout tables, etc.
  already_AddRefed<gfxFont> GetFontAt(uint32_t i, uint32_t aCh = 0x20) {
    bool loading = false;
    return GetFontAt(i, aCh, &loading);
  }

  // will always return a font or force a shutdown
  already_AddRefed<gfxFont> GetDefaultFont();

  // Init this font group's font metrics. If there no bad fonts, you don't need
  // to call this. But if there are one or more bad fonts which have bad
  // underline offset, you should call this with the *first* bad font.
  void InitMetricsForBadFont(gfxFont* aBadFont);

  // Set up the textrun glyphs for an entire text run:
  // find script runs, and then call InitScriptRun for each
  template <typename T>
  void InitTextRun(DrawTarget* aDrawTarget, gfxTextRun* aTextRun,
                   const T* aString, uint32_t aLength,
                   gfxMissingFontRecorder* aMFR);

  // InitTextRun helper to handle a single script run, by finding font ranges
  // and calling each font's InitTextRun() as appropriate
  template <typename T>
  void InitScriptRun(DrawTarget* aDrawTarget, gfxTextRun* aTextRun,
                     const T* aString, uint32_t aScriptRunStart,
                     uint32_t aScriptRunEnd, Script aRunScript,
                     gfxMissingFontRecorder* aMFR);

  // Helper for font-matching:
  // search all faces in a family for a fallback in cases where it's unclear
  // whether the family might have a font for a given character
  already_AddRefed<gfxFont> FindFallbackFaceForChar(
      const FamilyFace& aFamily, uint32_t aCh, uint32_t aNextCh,
      FontPresentation aPresentation);

  already_AddRefed<gfxFont> FindFallbackFaceForChar(
      mozilla::fontlist::Family* aFamily, uint32_t aCh, uint32_t aNextCh,
      FontPresentation aPresentation);

  already_AddRefed<gfxFont> FindFallbackFaceForChar(
      gfxFontFamily* aFamily, uint32_t aCh, uint32_t aNextCh,
      FontPresentation aPresentation);

  // helper methods for looking up fonts

  // lookup and add a font with a given name (i.e. *not* a generic!)
  void AddPlatformFont(const nsACString& aName, bool aQuotedName,
                       nsTArray<FamilyAndGeneric>& aFamilyList);

  // do style selection and add entries to list
  void AddFamilyToFontList(gfxFontFamily* aFamily,
                           mozilla::StyleGenericFontFamily aGeneric);
  void AddFamilyToFontList(mozilla::fontlist::Family* aFamily,
                           mozilla::StyleGenericFontFamily aGeneric);
};

// A "missing font recorder" is to be used during text-run creation to keep
// a record of any scripts encountered for which font coverage was lacking;
// when Flush() is called, it sends a notification that front-end code can use
// to download fonts on demand (or whatever else it wants to do).

#define GFX_MISSING_FONTS_NOTIFY_PREF "gfx.missing_fonts.notify"

class gfxMissingFontRecorder {
 public:
  gfxMissingFontRecorder() {
    MOZ_COUNT_CTOR(gfxMissingFontRecorder);
    memset(&mMissingFonts, 0, sizeof(mMissingFonts));
  }

  ~gfxMissingFontRecorder() {
#ifdef DEBUG
    for (uint32_t i = 0; i < kNumScriptBitsWords; i++) {
      NS_ASSERTION(mMissingFonts[i] == 0,
                   "failed to flush the missing-font recorder");
    }
#endif
    MOZ_COUNT_DTOR(gfxMissingFontRecorder);
  }

  // record this script code in our mMissingFonts bitset
  void RecordScript(mozilla::intl::Script aScriptCode) {
    mMissingFonts[static_cast<uint32_t>(aScriptCode) >> 5] |=
        (1 << (static_cast<uint32_t>(aScriptCode) & 0x1f));
  }

  // send a notification of any missing-scripts that have been
  // recorded, and clear the mMissingFonts set for re-use
  void Flush();

  // forget any missing-scripts that have been recorded up to now;
  // called before discarding a recorder we no longer care about
  void Clear() { memset(&mMissingFonts, 0, sizeof(mMissingFonts)); }

 private:
  // Number of 32-bit words needed for the missing-script flags
  static const uint32_t kNumScriptBitsWords =
      ((static_cast<int>(mozilla::intl::Script::NUM_SCRIPT_CODES) + 31) / 32);
  uint32_t mMissingFonts[kNumScriptBitsWords];
};

#endif
