/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "gfxTextRun.h"

#include "SharedFontList-impl.h"
#include "TextDrawTarget.h"
#include "gfx2DGlue.h"
#include "gfxContext.h"
#include "gfxFontConstants.h"
#include "gfxFontMissingGlyphs.h"
#include "gfxGlyphExtents.h"
#include "gfxHarfBuzzShaper.h"
#include "gfxPlatformFontList.h"
#include "gfxUserFontSet.h"
#include "mozilla/CheckedInt.h"
#include "mozilla/ClearOnShutdown.h"
#include "mozilla/Likely.h"
#include "mozilla/MruCache.h"
#include "mozilla/ServoStyleSet.h"
#include "mozilla/Sprintf.h"
#include "mozilla/StaticPresData.h"
#include "mozilla/UniquePtr.h"
#include "mozilla/Utf16.h"
#include "mozilla/dom/WorkerCommon.h"
#include "mozilla/gfx/2D.h"
#include "mozilla/gfx/Logging.h"  // for gfxCriticalError
#include "mozilla/gfx/PathHelpers.h"
#include "mozilla/intl/Locale.h"
#include "mozilla/intl/String.h"
#include "mozilla/intl/UnicodeProperties.h"
#include "nsLayoutUtils.h"
#include "nsReadableUtils.h"
#include "nsStyleConsts.h"
#include "nsStyleUtil.h"
#include "nsUnicodeProperties.h"

#ifdef XP_WIN
#  include "gfxWindowsPlatform.h"
#endif

using namespace mozilla;
using namespace mozilla::gfx;
using namespace mozilla::intl;
using namespace mozilla::unicode;
using mozilla::services::GetObserverService;

static const char16_t kEllipsisChar[] = {0x2026, 0x0};
static const char16_t kASCIIPeriodsChar[] = {'.', '.', '.', 0x0};

#ifdef DEBUG_roc
#  define DEBUG_TEXT_RUN_STORAGE_METRICS
#endif

#ifdef DEBUG_TEXT_RUN_STORAGE_METRICS
extern uint32_t gTextRunStorageHighWaterMark;
extern uint32_t gTextRunStorage;
extern uint32_t gFontCount;
extern uint32_t gGlyphExtentsCount;
extern uint32_t gGlyphExtentsWidthsTotalSize;
extern uint32_t gGlyphExtentsSetupEagerSimple;
extern uint32_t gGlyphExtentsSetupEagerTight;
extern uint32_t gGlyphExtentsSetupLazyTight;
extern uint32_t gGlyphExtentsSetupFallBackToTight;
#endif

void gfxTextRun::GlyphRunIterator::NextRun() {
  if (mReverse) {
    if (mGlyphRun == mTextRun->mGlyphRuns.begin()) {
      mGlyphRun = nullptr;
      return;
    }
    --mGlyphRun;
  } else {
    MOZ_DIAGNOSTIC_ASSERT(mGlyphRun != mTextRun->mGlyphRuns.end());
    ++mGlyphRun;
    if (mGlyphRun == mTextRun->mGlyphRuns.end()) {
      mGlyphRun = nullptr;
      return;
    }
  }
  if (mGlyphRun->mCharacterOffset >= mEndOffset) {
    mGlyphRun = nullptr;
    return;
  }
  uint32_t glyphRunEndOffset = mGlyphRun == mTextRun->mGlyphRuns.end() - 1
                                   ? mTextRun->GetLength()
                                   : (mGlyphRun + 1)->mCharacterOffset;
  if (glyphRunEndOffset < mStartOffset) {
    mGlyphRun = nullptr;
    return;
  }
  mStringEnd = std::min(mEndOffset, glyphRunEndOffset);
  mStringStart = std::max(mStartOffset, mGlyphRun->mCharacterOffset);
}

#ifdef DEBUG_TEXT_RUN_STORAGE_METRICS
static void AccountStorageForTextRun(gfxTextRun* aTextRun, int32_t aSign) {
  // Ignores detailed glyphs... we don't know when those have been constructed
  // Also ignores gfxSkipChars dynamic storage (which won't be anything
  // for preformatted text)
  // Also ignores GlyphRun array, again because it hasn't been constructed
  // by the time this gets called. If there's only one glyphrun that's stored
  // directly in the textrun anyway so no additional overhead.
  uint32_t length = aTextRun->GetLength();
  int32_t bytes = length * sizeof(gfxTextRun::CompressedGlyph);
  bytes += sizeof(gfxTextRun);
  gTextRunStorage += bytes * aSign;
  gTextRunStorageHighWaterMark =
      std::max(gTextRunStorageHighWaterMark, gTextRunStorage);
}
#endif

bool gfxTextRun::NeedsGlyphExtents() const {
  if (GetFlags() & gfx::ShapedTextFlags::TEXT_NEED_BOUNDING_BOX) {
    return true;
  }
  for (const auto& run : mGlyphRuns) {
    if (run.mFont->GetFontEntry()->IsUserFont()) {
      return true;
    }
  }
  return false;
}

// Helper for textRun creation to preallocate storage for glyph records;
// this function returns a pointer to the newly-allocated glyph storage.
// Returns nullptr if allocation fails.
void* gfxTextRun::AllocateStorageForTextRun(size_t aSize, uint32_t aLength) {
  // Allocate the storage we need, returning nullptr on failure rather than
  // throwing an exception (because web content can create huge runs).
  CheckedInt<size_t> storageSize =
      CheckedInt<size_t>(aLength) * sizeof(CompressedGlyph) + aSize;
  void* storage = storageSize.isValid() ? malloc(storageSize.value()) : nullptr;
  if (!storage) {
    NS_WARNING("failed to allocate storage for text run!");
    return nullptr;
  }

  // Initialize the glyph storage (beyond aSize) to zero
  memset(reinterpret_cast<char*>(storage) + aSize, 0,
         storageSize.value() - aSize);

  return storage;
}

already_AddRefed<gfxTextRun> gfxTextRun::Create(
    const gfxTextRunFactory::Parameters* aParams, uint32_t aLength,
    gfxFontGroup* aFontGroup, gfx::ShapedTextFlags aFlags,
    nsTextFrameUtils::Flags aFlags2) {
  void* storage = AllocateStorageForTextRun(sizeof(gfxTextRun), aLength);
  if (!storage) {
    return nullptr;
  }

  RefPtr<gfxTextRun> result =
      new (storage) gfxTextRun(aParams, aLength, aFontGroup, aFlags, aFlags2);
  return result.forget();
}

gfxTextRun::gfxTextRun(const gfxTextRunFactory::Parameters* aParams,
                       uint32_t aLength, gfxFontGroup* aFontGroup,
                       gfx::ShapedTextFlags aFlags,
                       nsTextFrameUtils::Flags aFlags2)
    : gfxShapedText(aLength, aFlags, aParams->mAppUnitsPerDevUnit),
      mUserData(aParams->mUserData),
      mFontGroup(aFontGroup),
      mFlags2(aFlags2),
      mShapingState(eShapingState_Normal) {
  NS_ASSERTION(mAppUnitsPerDevUnit > 0, "Invalid app unit scale");
  NS_ADDREF(mFontGroup);

#ifndef RELEASE_OR_BETA
  gfxTextPerfMetrics* tp = aFontGroup->GetTextPerfMetrics();
  if (tp) {
    tp->current.textrunConst++;
  }
#endif

  mCharacterGlyphs = reinterpret_cast<CompressedGlyph*>(this + 1);

  if (aParams->mSkipChars) {
    mSkipChars.TakeFrom(aParams->mSkipChars);
  }

#ifdef DEBUG_TEXT_RUN_STORAGE_METRICS
  AccountStorageForTextRun(this, 1);
#endif

  mDontSkipDrawing =
      !!(aFlags2 & nsTextFrameUtils::Flags::DontSkipDrawingForPendingUserFonts);
}

gfxTextRun::~gfxTextRun() {
#ifdef DEBUG_TEXT_RUN_STORAGE_METRICS
  AccountStorageForTextRun(this, -1);
#endif
#ifdef DEBUG
  // Make it easy to detect a dead text run
  mFlags = ~gfx::ShapedTextFlags();
  mFlags2 = ~nsTextFrameUtils::Flags();
#endif

#ifndef RELEASE_OR_BETA
  gfxTextPerfMetrics* tp = mFontGroup->GetTextPerfMetrics();
  if (tp) {
    tp->current.textrunDestr++;
  }
#endif
  NS_RELEASE(mFontGroup);
}

bool gfxTextRun::SetPotentialLineBreaks(Range aRange,
                                        const uint8_t* aBreakBefore) {
  NS_ASSERTION(aRange.end <= GetLength(), "Overflow");

  uint32_t changed = 0;
  CompressedGlyph* cg = mCharacterGlyphs + aRange.start;
  const CompressedGlyph* const end = cg + aRange.Length();
  while (cg < end) {
    uint8_t canBreak = *aBreakBefore++;
    if (canBreak && !cg->IsClusterStart()) {
      // XXX If we replace the line-breaker with one based more closely
      // on UAX#14 (e.g. using ICU), this may not be needed any more.
      // Avoid possible breaks inside a cluster, EXCEPT when the previous
      // character was a space (compare UAX#14 rules LB9, LB10).
      if (cg == mCharacterGlyphs || !(cg - 1)->CharIsSpace()) {
        canBreak = CompressedGlyph::FLAG_BREAK_TYPE_NONE;
      }
    }
    // If a break is allowed here, set the break flag, but don't clear a
    // possible pre-existing emergency-break flag already in the run.
    if (canBreak) {
      changed |= cg->SetCanBreakBefore(canBreak);
    }
    ++cg;
  }
  return changed != 0;
}

gfxTextRun::LigatureData gfxTextRun::ComputeLigatureData(
    Range aPartRange, const PropertyProvider* aProvider) const {
  NS_ASSERTION(aPartRange.start < aPartRange.end,
               "Computing ligature data for empty range");
  NS_ASSERTION(aPartRange.end <= GetLength(), "Character length overflow");

  LigatureData result;
  const CompressedGlyph* charGlyphs = mCharacterGlyphs;

  uint32_t i = aPartRange.start;
  while (i && !charGlyphs[i].IsLigatureGroupStart()) {
    --i;
  }
  NS_ASSERTION(charGlyphs[i].IsLigatureGroupStart(), "Ligature at run start?");
  result.mRange.start = i;

  for (i = aPartRange.start + 1;
       i < GetLength() && !charGlyphs[i].IsLigatureGroupStart(); ++i) {
  }
  result.mRange.end = i;

  int32_t ligatureWidth = GetAdvanceForGlyphs(
      result.mRange, aProvider ? aProvider->LetterSpacing() : 0);
  // Count the number of started clusters we have seen
  uint32_t totalClusterCount = 0;
  uint32_t partClusterIndex = 0;
  uint32_t partClusterCount = 0;
  for (i = result.mRange.start; i < result.mRange.end; ++i) {
    // Treat the first character of the ligature as the start of a
    // cluster for our purposes of allocating ligature width to its
    // characters.
    if (i == result.mRange.start || charGlyphs[i].IsClusterStart()) {
      ++totalClusterCount;
      if (i < aPartRange.start) {
        ++partClusterIndex;
      } else if (i < aPartRange.end) {
        ++partClusterCount;
      }
    }
  }
  NS_ASSERTION(totalClusterCount > 0, "Ligature involving no clusters??");
  result.mPartAdvance = partClusterIndex * (ligatureWidth / totalClusterCount);
  result.mPartWidth = partClusterCount * (ligatureWidth / totalClusterCount);

  // Any rounding errors are apportioned to the final part of the ligature,
  // so that measuring all parts of a ligature and summing them is equal to
  // the ligature width.
  if (aPartRange.end == result.mRange.end) {
    gfxFloat allParts = totalClusterCount * (ligatureWidth / totalClusterCount);
    result.mPartWidth += ligatureWidth - allParts;
  }

  if (partClusterCount == 0) {
    // nothing to draw
    result.mClipBeforePart = result.mClipAfterPart = true;
  } else {
    // Determine whether we should clip before or after this part when
    // drawing its slice of the ligature.
    // We need to clip before the part if any cluster is drawn before
    // this part.
    result.mClipBeforePart = partClusterIndex > 0;
    // We need to clip after the part if any cluster is drawn after
    // this part.
    result.mClipAfterPart =
        partClusterIndex + partClusterCount < totalClusterCount;
  }

  if (aProvider && (mFlags & gfx::ShapedTextFlags::TEXT_ENABLE_SPACING)) {
    gfxFont::Spacing spacing;
    if (aPartRange.start == result.mRange.start) {
      if (aProvider->GetSpacing(Range(aPartRange.start, aPartRange.start + 1),
                                &spacing)) {
        result.mPartWidth += spacing.mBefore;
      }
    }
    if (aPartRange.end == result.mRange.end) {
      if (aProvider->GetSpacing(Range(aPartRange.end - 1, aPartRange.end),
                                &spacing)) {
        result.mPartWidth += spacing.mAfter;
      }
    }
  }

  return result;
}

gfxFloat gfxTextRun::ComputePartialLigatureWidth(
    Range aPartRange, const PropertyProvider* aProvider) const {
  if (aPartRange.start >= aPartRange.end) return 0;
  LigatureData data = ComputeLigatureData(aPartRange, aProvider);
  return data.mPartWidth;
}

int32_t gfxTextRun::GetAdvanceForGlyphs(Range aRange,
                                        nscoord aLetterSpacing) const {
  int32_t advance = 0;
  for (auto i = aRange.start; i < aRange.end; ++i) {
    advance += GetAdvanceForGlyph(i, aLetterSpacing);
  }
  return advance;
}

// Returns false if there is definitely no spacing to apply.
static bool GetAdjustedSpacing(
    const gfxTextRun* aTextRun, gfxTextRun::Range aRange,
    const gfxTextRun::PropertyProvider& aProvider,
    gfxTextRun::PropertyProvider::Spacing* aSpacing) {
  if (aRange.start >= aRange.end) {
    return false;
  }

  bool result = aProvider.GetSpacing(aRange, aSpacing);

#ifdef DEBUG
  // Check to see if we have spacing inside ligatures

  const gfxTextRun::CompressedGlyph* charGlyphs =
      aTextRun->GetCharacterGlyphs();
  uint32_t i;

  for (i = aRange.start; i < aRange.end; ++i) {
    if (!charGlyphs[i].IsLigatureGroupStart()) {
      NS_ASSERTION(i == aRange.start || aSpacing[i - aRange.start].mBefore == 0,
                   "Before-spacing inside a ligature!");
      NS_ASSERTION(
          i - 1 <= aRange.start || aSpacing[i - 1 - aRange.start].mAfter == 0,
          "After-spacing inside a ligature!");
    }
  }
#endif

  return result;
}

bool gfxTextRun::GetAdjustedSpacingArray(
    Range aRange, const PropertyProvider* aProvider, Range aSpacingRange,
    nsTArray<PropertyProvider::Spacing>* aSpacing) const {
  if (!aProvider || !(mFlags & gfx::ShapedTextFlags::TEXT_ENABLE_SPACING)) {
    return false;
  }
  if (!aSpacing->AppendElements(aRange.Length(), fallible)) {
    return false;
  }
  auto spacingOffset = aSpacingRange.start - aRange.start;
  memset(aSpacing->Elements(), 0, sizeof(gfxFont::Spacing) * spacingOffset);
  if (!GetAdjustedSpacing(this, aSpacingRange, *aProvider,
                          aSpacing->Elements() + spacingOffset)) {
    aSpacing->Clear();
    return false;
  }
  memset(aSpacing->Elements() + spacingOffset + aSpacingRange.Length(), 0,
         sizeof(gfxFont::Spacing) * (aRange.end - aSpacingRange.end));
  return true;
}

bool gfxTextRun::ShrinkToLigatureBoundaries(Range* aRange) const {
  if (aRange->start >= aRange->end) {
    return false;
  }

  const CompressedGlyph* charGlyphs = mCharacterGlyphs;
  bool adjusted = false;
  while (aRange->start < aRange->end &&
         !charGlyphs[aRange->start].IsLigatureGroupStart()) {
    ++aRange->start;
    adjusted = true;
  }
  if (aRange->end < GetLength()) {
    while (aRange->end > aRange->start &&
           !charGlyphs[aRange->end].IsLigatureGroupStart()) {
      --aRange->end;
      adjusted = true;
    }
  }
  return adjusted;
}

// Converts aText to UTF-8 in aUTF8, which must be empty, and fills aCharToByte
// with the byte offset in aUTF8 of the source text for each UTF-16 code unit in
// aText, followed by the length of aUTF8. The low half of a surrogate pair
// shares the offset of the high half. Returns false if the conversion fails.
static bool ConvertSourceTextToUTF8(const nsAString& aText, nsACString& aUTF8,
                                    nsTArray<uint32_t>& aCharToByte) {
  MOZ_ASSERT(aUTF8.IsEmpty());
  aCharToByte.SetCapacity(aText.Length() + 1);
  // Convert one code point at a time so we know where each one starts.
  for (uint32_t unit = 0; unit < aText.Length();) {
    uint32_t unitCount = 1;
    if (unit + 1 < aText.Length() &&
        IsSurrogatePair(aText[unit], aText[unit + 1])) {
      unitCount = 2;
    }
    aCharToByte.AppendElement(aUTF8.Length());
    if (unitCount == 2) {
      // The low surrogate shares the offset of the high surrogate.
      aCharToByte.AppendElement(aUTF8.Length());
    }
    if (!AppendUTF16toUTF8(Span<const char16_t>(aText).Subspan(unit, unitCount),
                           aUTF8, fallible)) {
      return false;
    }
    unit += unitCount;
  }
  aCharToByte.AppendElement(aUTF8.Length());
  return true;
}

void gfxTextRun::DrawGlyphs(gfxFont* aFont, Range aRange, gfx::Point* aPt,
                            const PropertyProvider* aProvider,
                            Range aProviderRange, TextRunDrawParams& aParams,
                            imgDrawingParams& aImgParams,
                            gfx::ShapedTextFlags aOrientation) const {
  AutoTArray<PropertyProvider::Spacing, 200> spacingBuffer;
  bool haveSpacing = GetAdjustedSpacingArray(aRange, aProvider, aProviderRange,
                                             &spacingBuffer);
  aParams.spacing = haveSpacing ? spacingBuffer.Elements() : nullptr;
  nsString sourceText16;
  nsCString sourceText;
  nsTArray<uint32_t> sourceCharToByte;
  bool haveSourceText = aParams.needsSourceText && aProvider &&
                        aProvider->GetSourceText(aProviderRange, sourceText16);
  if (haveSourceText) {
    MOZ_ASSERT(sourceText16.Length() == aProviderRange.Length());
    sourceCharToByte.SetCapacity(aRange.Length() + 1);
    haveSourceText =
        sourceText16.Length() == aProviderRange.Length() &&
        ConvertSourceTextToUTF8(sourceText16, sourceText, sourceCharToByte);
    if (haveSourceText) {
      // aProviderRange may be narrower than aRange. DrawPartialLigature
      // always asks to draw the glyph for the full range of a ligature, but
      // aProvider (e.g. an nsTextFrame) may only be able to supply source
      // text for part of that range if the ligature is split across
      // multiple providers. Map characters outside aProviderRange to the
      // nearest edge of the provider's source text. Otherwise, we would read
      // past the edge.
      const uint32_t startPad = aProviderRange.start - aRange.start;
      if (startPad > 0) {
        // Copy the value first because InsertElementAt takes a reference and
        // we're referencing a value in the array we're mutating.
        const uint32_t firstByte = sourceCharToByte[0];
        sourceCharToByte.InsertElementsAt(0, startPad, firstByte);
      }
      const uint32_t endPad = aRange.end - aProviderRange.end;
      if (endPad > 0) {
        const uint32_t lastByte = sourceCharToByte.LastElement();
        sourceCharToByte.InsertElementsAt(sourceCharToByte.Length(), endPad,
                                          lastByte);
      }
    }
  }
  aParams.sourceText = haveSourceText ? &sourceText : nullptr;
  aParams.sourceCharToByte =
      haveSourceText ? sourceCharToByte.Elements() : nullptr;
  aFont->Draw(this, aRange.start, aRange.end, aPt, aParams, aImgParams,
              aOrientation);
}

static void ClipPartialLigature(const gfxTextRun* aTextRun, gfxFloat* aStart,
                                gfxFloat* aEnd, gfxFloat aOrigin,
                                gfxTextRun::LigatureData* aLigature) {
  if (aLigature->mClipBeforePart) {
    if (aTextRun->IsRightToLeft()) {
      *aEnd = std::min(*aEnd, aOrigin);
    } else {
      *aStart = std::max(*aStart, aOrigin);
    }
  }
  if (aLigature->mClipAfterPart) {
    gfxFloat endEdge =
        aOrigin + aTextRun->GetDirection() * aLigature->mPartWidth;
    if (aTextRun->IsRightToLeft()) {
      *aStart = std::max(*aStart, endEdge);
    } else {
      *aEnd = std::min(*aEnd, endEdge);
    }
  }
}

void gfxTextRun::DrawPartialLigature(gfxFont* aFont, Range aRange,
                                     gfx::Point* aPt,
                                     const PropertyProvider* aProvider,
                                     TextRunDrawParams& aParams,
                                     imgDrawingParams& aImgParams,
                                     gfx::ShapedTextFlags aOrientation) const {
  if (aRange.start >= aRange.end) {
    return;
  }

  // Draw partial ligature. We hack this by clipping the ligature.
  LigatureData data = ComputeLigatureData(aRange, aProvider);
  gfxRect clipExtents = aParams.context->GetClipExtents();
  gfxFloat start, end;
  if (aParams.isVerticalRun) {
    start = clipExtents.Y() * mAppUnitsPerDevUnit;
    end = clipExtents.YMost() * mAppUnitsPerDevUnit;
    ClipPartialLigature(this, &start, &end, aPt->y, &data);
  } else {
    start = clipExtents.X() * mAppUnitsPerDevUnit;
    end = clipExtents.XMost() * mAppUnitsPerDevUnit;
    ClipPartialLigature(this, &start, &end, aPt->x, &data);
  }

  gfxClipAutoSaveRestore autoSaveClip(aParams.context);
  {
    // use division here to ensure that when the rect is aligned on multiples
    // of mAppUnitsPerDevUnit, we clip to true device unit boundaries.
    // Also, make sure we snap the rectangle to device pixels.
    Rect clipRect =
        aParams.isVerticalRun
            ? Rect(clipExtents.X(), start / mAppUnitsPerDevUnit,
                   clipExtents.Width(), (end - start) / mAppUnitsPerDevUnit)
            : Rect(start / mAppUnitsPerDevUnit, clipExtents.Y(),
                   (end - start) / mAppUnitsPerDevUnit, clipExtents.Height());
    MaybeSnapToDevicePixels(clipRect, *aParams.dt, true);

    autoSaveClip.Clip(clipRect);
  }

  gfx::Point pt;
  if (aParams.isVerticalRun) {
    pt = Point(aPt->x, aPt->y - aParams.direction * data.mPartAdvance);
  } else {
    pt = Point(aPt->x - aParams.direction * data.mPartAdvance, aPt->y);
  }

  DrawGlyphs(aFont, data.mRange, &pt, aProvider, aRange, aParams, aImgParams,
             aOrientation);

  if (aParams.isVerticalRun) {
    aPt->y += aParams.direction * data.mPartWidth;
  } else {
    aPt->x += aParams.direction * data.mPartWidth;
  }
}

// Returns true if the font has synthetic bolding enabled,
// or is a color font (COLR/SVG/sbix/CBDT), false otherwise. This is used to
// check whether the text run needs to be explicitly composited in order to
// support opacity.
static bool HasSyntheticBoldOrColor(gfxFont* aFont) {
  if (aFont->ApplySyntheticBold()) {
    return true;
  }
  gfxFontEntry* fe = aFont->GetFontEntry();
  if (fe->TryGetSVGData(aFont) || fe->TryGetColorGlyphs()) {
    return true;
  }
#if defined(XP_MACOSX)  // sbix fonts only supported via Core Text
  if (fe->HasFontTable(TRUETYPE_TAG('s', 'b', 'i', 'x'))) {
    return true;
  }
#endif
  return false;
}

// helper class for double-buffering drawing with non-opaque color
struct MOZ_STACK_CLASS BufferAlphaColor {
  explicit BufferAlphaColor(gfxContext* aContext) : mContext(aContext) {}

  ~BufferAlphaColor() = default;

  void PushSolidColor(const gfxRect& aBounds, const DeviceColor& aAlphaColor,
                      uint32_t appsPerDevUnit) {
    mContext->Save();
    mContext->SnappedClip(gfxRect(
        aBounds.X() / appsPerDevUnit, aBounds.Y() / appsPerDevUnit,
        aBounds.Width() / appsPerDevUnit, aBounds.Height() / appsPerDevUnit));
    mContext->SetDeviceColor(
        DeviceColor(aAlphaColor.r, aAlphaColor.g, aAlphaColor.b));
    mContext->PushGroupForBlendBack(gfxContentType::COLOR_ALPHA, aAlphaColor.a);
  }

  void PopAlpha() {
    // pop the text, using the color alpha as the opacity
    mContext->PopGroupAndBlend();
    mContext->Restore();
  }

  gfxContext* mContext;
};

void gfxTextRun::Draw(const Range aRange, const gfx::Point aPt,
                      const DrawParams& aParams,
                      imgDrawingParams& aImgParams) const {
  NS_ASSERTION(aRange.end <= GetLength(), "Substring out of range");
  NS_ASSERTION(aParams.drawMode == DrawMode::GLYPH_PATH ||
                   !(aParams.drawMode & DrawMode::GLYPH_PATH),
               "GLYPH_PATH cannot be used with GLYPH_FILL, GLYPH_STROKE or "
               "GLYPH_STROKE_UNDERNEATH");
  NS_ASSERTION(aParams.drawMode == DrawMode::GLYPH_PATH || !aParams.callbacks,
               "callback must not be specified unless using GLYPH_PATH");

  bool skipDrawing = !mDontSkipDrawing && mFontGroup->ShouldSkipDrawing();
  auto* textDrawer = aParams.context->GetTextDrawer();
  if (aParams.drawMode & DrawMode::GLYPH_FILL) {
    DeviceColor currentColor;
    if (aParams.context->GetDeviceColor(currentColor) && currentColor.a == 0 &&
        !textDrawer) {
      skipDrawing = true;
    }
  }

  gfxFloat direction = GetDirection();

  if (skipDrawing) {
    // We don't need to draw anything;
    // but if the caller wants advance width, we need to compute it here
    if (aParams.advanceWidth) {
      gfxTextRun::Metrics metrics =
          MeasureText(aRange, gfxFont::LOOSE_INK_EXTENTS,
                      aParams.context->GetDrawTarget(), aParams.provider);
      *aParams.advanceWidth = metrics.mAdvanceWidth * direction;
    }

    // return without drawing
    return;
  }

  // synthetic bolding draws glyphs twice ==> colors with opacity won't draw
  // correctly unless first drawn without alpha
  BufferAlphaColor syntheticBoldBuffer(aParams.context);
  DeviceColor currentColor;
  bool mayNeedBuffering =
      aParams.drawMode & DrawMode::GLYPH_FILL &&
      aParams.context->HasNonOpaqueNonTransparentColor(currentColor) &&
      !textDrawer;

  // If we need to double-buffer, we'll need to measure the text first to
  // get the bounds of the area of interest. Ideally we'd do that just for
  // the specific glyph run(s) that need buffering, but because of bug
  // 1612610 we currently use the extent of the entire range even when
  // just buffering a subrange. So we'll measure the full range once and
  // keep the metrics on hand for any subsequent subranges.
  gfxTextRun::Metrics metrics;
  bool gotMetrics = false;

  // Set up parameters that will be constant across all glyph runs we need
  // to draw, regardless of the font used.
  TextRunDrawParams params(aParams.paletteValueSet);
  params.context = aParams.context;
  params.devPerApp = 1.0 / double(GetAppUnitsPerDevUnit());
  params.isVerticalRun = IsVertical();
  params.isRTL = IsRightToLeft();
  params.direction = direction;
  params.strokeOpts = aParams.strokeOpts;
  params.textStrokeColor = aParams.textStrokeColor;
  params.fontPalette = aParams.fontPalette;
  params.textStrokePattern = aParams.textStrokePattern;
  params.drawOpts = aParams.drawOpts;
  params.drawMode = aParams.drawMode;
  params.hasTextShadow = aParams.hasTextShadow;
  params.callbacks = aParams.callbacks;
  params.runContextPaint = aParams.contextPaint;
  params.paintSVGGlyphs =
      !aParams.callbacks || aParams.callbacks->mShouldPaintSVGGlyphs;
  params.dt = aParams.context->GetDrawTarget();
  params.needsSourceText =
      aParams.provider && params.dt->SupportsGlyphSourceText();
  params.textDrawer = textDrawer;
  if (textDrawer) {
    params.clipRect = textDrawer->GeckoClipRect();
  }
  params.allowGDI = aParams.allowGDI;
  params.letterSpacing =
      aParams.provider ? aParams.provider->LetterSpacing() : 0;

  gfxFloat advance = 0.0;
  gfx::Point pt = aPt;

  for (GlyphRunIterator iter(this, aRange); !iter.AtEnd(); iter.NextRun()) {
    gfxFont* font = iter.GlyphRun()->mFont;
    Range runRange(iter.StringStart(), iter.StringEnd());

    bool needToRestore = false;
    if (mayNeedBuffering && HasSyntheticBoldOrColor(font)) {
      needToRestore = true;
      if (!gotMetrics) {
        // Measure text; use the bounding box to determine the area we need
        // to buffer. We measure the entire range, rather than just the glyph
        // run that we're actually handling, because of bug 1612610: if the
        // bounding box passed to PushSolidColor does not intersect the
        // drawTarget's current clip, the skia backend fails to clip properly.
        // This means we may use a larger buffer than actually needed, but is
        // otherwise harmless.
        metrics = MeasureText(aRange, gfxFont::LOOSE_INK_EXTENTS, params.dt,
                              aParams.provider);
        if (IsRightToLeft()) {
          metrics.mBoundingBox.MoveBy(
              gfxPoint(aPt.x - metrics.mAdvanceWidth, aPt.y));
        } else {
          metrics.mBoundingBox.MoveBy(gfxPoint(aPt.x, aPt.y));
        }
        gotMetrics = true;
      }
      syntheticBoldBuffer.PushSolidColor(metrics.mBoundingBox, currentColor,
                                         GetAppUnitsPerDevUnit());
    }

    Range ligatureRange(runRange);
    bool adjusted = ShrinkToLigatureBoundaries(&ligatureRange);

    bool drawPartial =
        adjusted &&
        ((aParams.drawMode & (DrawMode::GLYPH_FILL | DrawMode::GLYPH_STROKE)) ||
         (aParams.drawMode == DrawMode::GLYPH_PATH && aParams.callbacks));
    gfx::Point origPt = pt;

    if (drawPartial) {
      DrawPartialLigature(font, Range(runRange.start, ligatureRange.start), &pt,
                          aParams.provider, params, aImgParams,
                          iter.GlyphRun()->mOrientation);
    }

    DrawGlyphs(font, ligatureRange, &pt, aParams.provider, ligatureRange,
               params, aImgParams, iter.GlyphRun()->mOrientation);

    if (drawPartial) {
      DrawPartialLigature(font, Range(ligatureRange.end, runRange.end), &pt,
                          aParams.provider, params, aImgParams,
                          iter.GlyphRun()->mOrientation);
    }

    if (params.isVerticalRun) {
      advance += (pt.y - origPt.y) * params.direction;
    } else {
      advance += (pt.x - origPt.x) * params.direction;
    }

    // composite result when synthetic bolding used
    if (needToRestore) {
      syntheticBoldBuffer.PopAlpha();
    }
  }

  if (aParams.advanceWidth) {
    *aParams.advanceWidth = advance;
  }
}

// This method is mostly parallel to Draw().
void gfxTextRun::DrawEmphasisMarks(gfxContext* aContext, gfxTextRun* aMark,
                                   gfxFloat aMarkAdvance, gfx::Point aPt,
                                   Range aRange,
                                   const PropertyProvider* aProvider,
                                   gfx::FontPaletteValueSet* aPaletteValueSet,
                                   image::imgDrawingParams& aImgParams) const {
  MOZ_ASSERT(aRange.end <= GetLength());

  EmphasisMarkDrawParams params(aContext, aPaletteValueSet);
  params.mark = aMark;
  params.advance = aMarkAdvance;
  params.direction = GetDirection();
  params.isVertical = IsVertical();

  float& inlineCoord = params.isVertical ? aPt.y.value : aPt.x.value;
  float direction = params.direction;

  for (GlyphRunIterator iter(this, aRange); !iter.AtEnd(); iter.NextRun()) {
    gfxFont* font = iter.GlyphRun()->mFont;
    uint32_t start = iter.StringStart();
    uint32_t end = iter.StringEnd();
    Range ligatureRange(start, end);
    bool adjusted = ShrinkToLigatureBoundaries(&ligatureRange);

    if (adjusted) {
      inlineCoord +=
          direction * ComputePartialLigatureWidth(
                          Range(start, ligatureRange.start), aProvider);
    }

    AutoTArray<PropertyProvider::Spacing, 200> spacingBuffer;
    bool haveSpacing = GetAdjustedSpacingArray(ligatureRange, aProvider,
                                               ligatureRange, &spacingBuffer);
    params.spacing = haveSpacing ? spacingBuffer.Elements() : nullptr;
    font->DrawEmphasisMarks(this, &aPt, ligatureRange.start,
                            ligatureRange.Length(), params, aImgParams);

    if (adjusted) {
      inlineCoord += direction * ComputePartialLigatureWidth(
                                     Range(ligatureRange.end, end), aProvider);
    }
  }
}

void gfxTextRun::AccumulateMetricsForRun(
    gfxFont* aFont, Range aRange, gfxFont::BoundingBoxType aBoundingBoxType,
    DrawTarget* aRefDrawTarget, const PropertyProvider* aProvider,
    Range aSpacingRange, gfx::ShapedTextFlags aOrientation,
    Metrics* aMetrics) const {
  AutoTArray<PropertyProvider::Spacing, 200> spacingBuffer;
  bool haveSpacing =
      GetAdjustedSpacingArray(aRange, aProvider, aSpacingRange, &spacingBuffer);
  Metrics metrics = aFont->Measure(
      this, aRange.start, aRange.end, aBoundingBoxType, aRefDrawTarget,
      haveSpacing ? spacingBuffer.Elements() : nullptr,
      aProvider ? aProvider->LetterSpacing() : 0, aOrientation);
  aMetrics->CombineWith(metrics, IsRightToLeft());
}

void gfxTextRun::AccumulatePartialLigatureMetrics(
    gfxFont* aFont, Range aRange, gfxFont::BoundingBoxType aBoundingBoxType,
    DrawTarget* aRefDrawTarget, const PropertyProvider* aProvider,
    gfx::ShapedTextFlags aOrientation, Metrics* aMetrics) const {
  if (aRange.start >= aRange.end) return;

  // Measure partial ligature. We hack this by clipping the metrics in the
  // same way we clip the drawing.
  LigatureData data = ComputeLigatureData(aRange, aProvider);

  // First measure the complete ligature
  Metrics metrics;
  AccumulateMetricsForRun(aFont, data.mRange, aBoundingBoxType, aRefDrawTarget,
                          aProvider, aRange, aOrientation, &metrics);

  // Clip the bounding box to the ligature part
  gfxFloat bboxLeft = metrics.mBoundingBox.X();
  gfxFloat bboxRight = metrics.mBoundingBox.XMost();
  // Where we are going to start "drawing" relative to our left baseline origin
  gfxFloat origin =
      IsRightToLeft() ? metrics.mAdvanceWidth - data.mPartAdvance : 0;
  ClipPartialLigature(this, &bboxLeft, &bboxRight, origin, &data);
  metrics.mBoundingBox.SetBoxX(bboxLeft, bboxRight);

  // mBoundingBox is now relative to the left baseline origin for the entire
  // ligature. Shift it left.
  metrics.mBoundingBox.MoveByX(
      -(IsRightToLeft()
            ? metrics.mAdvanceWidth - (data.mPartAdvance + data.mPartWidth)
            : data.mPartAdvance));
  metrics.mAdvanceWidth = data.mPartWidth;

  aMetrics->CombineWith(metrics, IsRightToLeft());
}

gfxTextRun::Metrics gfxTextRun::MeasureText(
    Range aRange, gfxFont::BoundingBoxType aBoundingBoxType,
    DrawTarget* aRefDrawTarget, const PropertyProvider* aProvider) const {
  NS_ASSERTION(aRange.end <= GetLength(), "Substring out of range");

  Metrics accumulatedMetrics;
  for (GlyphRunIterator iter(this, aRange); !iter.AtEnd(); iter.NextRun()) {
    gfxFont* font = iter.GlyphRun()->mFont;
    uint32_t start = iter.StringStart();
    uint32_t end = iter.StringEnd();
    Range ligatureRange(start, end);
    bool adjusted = ShrinkToLigatureBoundaries(&ligatureRange);

    if (adjusted) {
      AccumulatePartialLigatureMetrics(font, Range(start, ligatureRange.start),
                                       aBoundingBoxType, aRefDrawTarget,
                                       aProvider, iter.GlyphRun()->mOrientation,
                                       &accumulatedMetrics);
    }

    // XXX This sucks. We have to get glyph extents just so we can detect
    // glyphs outside the font box, even when aBoundingBoxType is LOOSE,
    // even though in almost all cases we could get correct results just
    // by getting some ascent/descent from the font and using our stored
    // advance widths.
    AccumulateMetricsForRun(font, ligatureRange, aBoundingBoxType,
                            aRefDrawTarget, aProvider, ligatureRange,
                            iter.GlyphRun()->mOrientation, &accumulatedMetrics);

    if (adjusted) {
      AccumulatePartialLigatureMetrics(
          font, Range(ligatureRange.end, end), aBoundingBoxType, aRefDrawTarget,
          aProvider, iter.GlyphRun()->mOrientation, &accumulatedMetrics);
    }
  }

  return accumulatedMetrics;
}

void gfxTextRun::GetLineHeightMetrics(Range aRange, gfxFloat& aAscent,
                                      gfxFloat& aDescent) const {
  Metrics accumulatedMetrics;
  for (GlyphRunIterator iter(this, aRange); !iter.AtEnd(); iter.NextRun()) {
    gfxFont* font = iter.GlyphRun()->mFont;
    auto metrics =
        font->Measure(this, 0, 0, gfxFont::LOOSE_INK_EXTENTS, nullptr, nullptr,
                      0, iter.GlyphRun()->mOrientation);
    accumulatedMetrics.CombineWith(metrics, false);
  }
  aAscent = accumulatedMetrics.mAscent;
  aDescent = accumulatedMetrics.mDescent;
}

void gfxTextRun::ClassifyAutoHyphenations(uint32_t aStart, Range aRange,
                                          nsTArray<HyphenType>& aHyphenBuffer,
                                          HyphenationState* aWordState) {
  MOZ_ASSERT(
      aRange.end - aStart <= aHyphenBuffer.Length() && aRange.start >= aStart,
      "Range out of bounds");
  MOZ_ASSERT(aWordState->mostRecentBoundary >= aStart,
             "Unexpected aMostRecentWordBoundary!!");

  uint32_t start =
      std::min<uint32_t>(aRange.start, aWordState->mostRecentBoundary);

  for (uint32_t i = start; i < aRange.end; ++i) {
    if (aHyphenBuffer[i - aStart] == HyphenType::Explicit &&
        !aWordState->hasExplicitHyphen) {
      aWordState->hasExplicitHyphen = true;
    }
    if (!aWordState->hasManualHyphen &&
        (aHyphenBuffer[i - aStart] == HyphenType::Soft ||
         aHyphenBuffer[i - aStart] == HyphenType::Explicit)) {
      aWordState->hasManualHyphen = true;
      // This is the first manual hyphen in the current word. We can only
      // know if the current word has a manual hyphen until now. So, we need
      // to run a sub loop to update the auto hyphens between the start of
      // the current word and this manual hyphen.
      if (aWordState->hasAutoHyphen) {
        for (uint32_t j = aWordState->mostRecentBoundary; j < i; j++) {
          if (aHyphenBuffer[j - aStart] ==
              HyphenType::AutoWithoutManualInSameWord) {
            aHyphenBuffer[j - aStart] = HyphenType::AutoWithManualInSameWord;
          }
        }
      }
    }
    if (aHyphenBuffer[i - aStart] == HyphenType::AutoWithoutManualInSameWord) {
      if (!aWordState->hasAutoHyphen) {
        aWordState->hasAutoHyphen = true;
      }
      if (aWordState->hasManualHyphen) {
        aHyphenBuffer[i - aStart] = HyphenType::AutoWithManualInSameWord;
      }
    }

    // If we're at the word boundary, clear/reset couple states.
    if (mCharacterGlyphs[i].CharIsSpace() || mCharacterGlyphs[i].CharIsTab() ||
        mCharacterGlyphs[i].CharIsNewline() ||
        // Since we will not have a boundary in the end of the string, let's
        // call the end of the string a special case for word boundary.
        i == GetLength() - 1) {
      // We can only get to know whether we should raise/clear an explicit
      // manual hyphen until we get to the end of a word, because this depends
      // on whether there exists at least one auto hyphen in the same word.
      if (!aWordState->hasAutoHyphen && aWordState->hasExplicitHyphen) {
        for (uint32_t j = aWordState->mostRecentBoundary; j <= i; j++) {
          if (aHyphenBuffer[j - aStart] == HyphenType::Explicit) {
            aHyphenBuffer[j - aStart] = HyphenType::None;
          }
        }
      }
      aWordState->mostRecentBoundary = i;
      aWordState->hasManualHyphen = false;
      aWordState->hasAutoHyphen = false;
      aWordState->hasExplicitHyphen = false;
    }
  }
}

uint32_t gfxTextRun::BreakAndMeasureText(
    uint32_t aStart, uint32_t aMaxLength, bool aLineBreakBefore,
    gfxFloat aWidth, const PropertyProvider& aProvider,
    SuppressBreak aSuppressBreak, gfxFont::BoundingBoxType aBoundingBoxType,
    DrawTarget* aRefDrawTarget, bool aCanWordWrap, bool aCanWhitespaceWrap,
    bool aIsBreakSpaces,
    // output params:
    TrimmableWS* aOutTrimmableWhitespace, Metrics& aOutMetrics,
    bool& aOutUsedHyphenation, uint32_t& aOutLastBreak,
    gfxBreakPriority& aBreakPriority) {
  aMaxLength = std::min(aMaxLength, GetLength() - aStart);

  NS_ASSERTION(aStart + aMaxLength <= GetLength(), "Substring out of range");

  constexpr uint32_t kMeasurementBufferSize = 100;
  Range bufferRange(aStart,
                    aStart + std::min(aMaxLength, kMeasurementBufferSize));
  PropertyProvider::Spacing spacingBuffer[kMeasurementBufferSize];
  bool haveSpacing = !!(mFlags & gfx::ShapedTextFlags::TEXT_ENABLE_SPACING);
  if (haveSpacing) {
    GetAdjustedSpacing(this, bufferRange, aProvider, spacingBuffer);
  }
  AutoTArray<HyphenType, 4096> hyphenBuffer;
  HyphenationState wordState;
  wordState.mostRecentBoundary = aStart;
  // GetHyphensOption() is a virtual call returning a value that is constant
  // for the whole measurement, so fetch it once instead of on every use.
  const StyleHyphens hyphensOption = aProvider.GetHyphensOption();
  // Hyphenation data is only consulted in the break-detection block below,
  // which is skipped entirely when all breaks are suppressed. In that case we
  // avoid fetching and classifying hyphenation data (which can involve per-word
  // dictionary lookups) since it would never be used.
  bool haveHyphenation =
      aSuppressBreak != eSuppressAllBreaks &&
      (hyphensOption == StyleHyphens::Auto ||
       (hyphensOption == StyleHyphens::Manual &&
        !!(mFlags & gfx::ShapedTextFlags::TEXT_ENABLE_HYPHEN_BREAKS)));
  if (haveHyphenation) {
    if (hyphenBuffer.AppendElements(bufferRange.Length(), fallible)) {
      aProvider.GetHyphenationBreaks(bufferRange, hyphenBuffer.Elements());
      if (hyphensOption == StyleHyphens::Auto) {
        ClassifyAutoHyphenations(aStart, bufferRange, hyphenBuffer, &wordState);
      }
    } else {
      haveHyphenation = false;
    }
  }

  gfxFloat width = 0;
  gfxFloat advance = 0;
  // The number of space characters that can be trimmed or hang at a soft-wrap
  uint32_t trimmableChars = 0;
  // The amount of space removed by ignoring trimmableChars
  gfxFloat trimmableAdvance = 0;
  int32_t lastBreak = -1;
  int32_t lastBreakTrimmableChars = -1;
  gfxFloat lastBreakTrimmableAdvance = -1;
  // Cache the last candidate break
  int32_t lastCandidateBreak = -1;
  int32_t lastCandidateBreakTrimmableChars = -1;
  gfxFloat lastCandidateBreakTrimmableAdvance = -1;
  bool lastCandidateBreakUsedHyphenation = false;
  gfxBreakPriority lastCandidateBreakPriority = gfxBreakPriority::eNoBreak;
  bool aborted = false;
  uint32_t end = aStart + aMaxLength;
  bool lastBreakUsedHyphenation = false;
  Range ligatureRange(aStart, end);
  ShrinkToLigatureBoundaries(&ligatureRange);

  // Cache the glyph array pointer in a local so the compiler doesn't reload
  // the member on every iteration across the calls made in the loop below;
  // it does not change while we're measuring.
  const CompressedGlyph* charGlyphs = mCharacterGlyphs;

  // LetterSpacing() is a virtual call returning a value that is constant for
  // the whole measurement, so hoist it out of the per-character loop.
  const nscoord letterSpacing = aProvider.LetterSpacing();

  // We may need to move `i` backwards in the following loop, and re-scan
  // part of the textrun; we'll use `rescanLimit` so we can tell when that
  // is happening: if `i < rescanLimit` then we're rescanning.
  uint32_t rescanLimit = aStart;
  for (uint32_t i = aStart; i < end; ++i) {
    if (i >= bufferRange.end) {
      // Fetch more spacing and hyphenation data
      uint32_t oldHyphenBufferLength = hyphenBuffer.Length();
      bufferRange.start = i;
      bufferRange.end =
          std::min(aStart + aMaxLength, i + kMeasurementBufferSize);
      // For spacing, we always overwrite the old data with the newly
      // fetched one. However, for hyphenation, hyphenation data sometimes
      // depends on the context in every word (if "hyphens: auto" is set).
      // To ensure we get enough information between neighboring buffers,
      // we grow the hyphenBuffer instead of overwrite it.
      // NOTE that this means bufferRange does not correspond to the
      // entire hyphenBuffer, but only to the most recently added portion.
      // Therefore, we need to add the old length to hyphenBuffer.Elements()
      // when getting more data.
      if (haveSpacing) {
        GetAdjustedSpacing(this, bufferRange, aProvider, spacingBuffer);
      }
      if (haveHyphenation) {
        if (hyphenBuffer.AppendElements(bufferRange.Length(), fallible)) {
          aProvider.GetHyphenationBreaks(
              bufferRange, hyphenBuffer.Elements() + oldHyphenBufferLength);
          if (hyphensOption == StyleHyphens::Auto) {
            uint32_t prevMostRecentWordBoundary = wordState.mostRecentBoundary;
            ClassifyAutoHyphenations(aStart, bufferRange, hyphenBuffer,
                                     &wordState);
            // If the buffer boundary is in the middle of a word,
            // we need to go back to the start of the current word.
            // So, we can correct the wrong candidates that we set
            // in the previous runs of the loop.
            if (prevMostRecentWordBoundary < oldHyphenBufferLength) {
              rescanLimit = i;
              i = prevMostRecentWordBoundary - 1;
              continue;
            }
          }
        } else {
          haveHyphenation = false;
        }
      }
    }

    // There can't be a word-wrap break opportunity at the beginning of the
    // line: if the width is too small for even one character to fit, it
    // could be the first and last break opportunity on the line, and that
    // would trigger an infinite loop.
    if (aSuppressBreak != eSuppressAllBreaks &&
        (aSuppressBreak != eSuppressInitialBreak || i > aStart)) {
      bool atNaturalBreak = charGlyphs[i].CanBreakBefore() ==
                            CompressedGlyph::FLAG_BREAK_TYPE_NORMAL;
      // atHyphenationBreak indicates we're at a "soft" hyphen, where an extra
      // hyphen glyph will need to be painted. It is NOT set for breaks at an
      // explicit hyphen present in the text.
      //
      // NOTE(emilio): If you change this condition you also need to change
      // nsTextFrame::AddInlineMinISizeForFlow to match.
      bool atHyphenationBreak = !atNaturalBreak && haveHyphenation &&
                                (!aLineBreakBefore || i > aStart) &&
                                IsOptionalHyphenBreak(hyphenBuffer[i - aStart]);
      bool atAutoHyphenWithManualHyphenInSameWord =
          atHyphenationBreak &&
          hyphenBuffer[i - aStart] == HyphenType::AutoWithManualInSameWord;
      bool atBreak = atNaturalBreak || atHyphenationBreak;
      bool wordWrapping =
          (aCanWordWrap ||
           (aCanWhitespaceWrap &&
            charGlyphs[i].CanBreakBefore() ==
                CompressedGlyph::FLAG_BREAK_TYPE_EMERGENCY_WRAP)) &&
          charGlyphs[i].IsClusterStart() &&
          aBreakPriority <= gfxBreakPriority::eWordWrapBreak;

      bool whitespaceWrapping = false;
      if (i > aStart) {
        // The spec says the breaking opportunity is *after* whitespace.
        auto const& g = charGlyphs[i - 1];
        whitespaceWrapping =
            aIsBreakSpaces &&
            (g.CharIsSpace() || g.CharIsTab() || g.CharIsNewline());
      }

      if (atBreak || wordWrapping || whitespaceWrapping) {
        gfxFloat hyphenatedAdvance = advance;
        if (atHyphenationBreak) {
          hyphenatedAdvance += aProvider.GetHyphenWidth();
        }

        if (lastBreak < 0 ||
            width + hyphenatedAdvance - trimmableAdvance <= aWidth) {
          // We can break here.
          lastBreak = i;
          lastBreakTrimmableChars = trimmableChars;
          lastBreakTrimmableAdvance = trimmableAdvance;
          lastBreakUsedHyphenation = atHyphenationBreak;
          aBreakPriority = (atBreak || whitespaceWrapping)
                               ? gfxBreakPriority::eNormalBreak
                               : gfxBreakPriority::eWordWrapBreak;
        }

        width += advance;
        advance = 0;
        if (width - trimmableAdvance > aWidth) {
          // No more text fits. Abort
          aborted = true;
          break;
        }
        // There are various kinds of break opportunities:
        // 1. word wrap break,
        // 2. natural break,
        // 3. manual hyphenation break,
        // 4. auto hyphenation break without any manual hyphenation
        //    in the same word,
        // 5. auto hyphenation break with another manual hyphenation
        //    in the same word.
        // Allow all of them except the last one to be a candidate.
        // So, we can ensure that we don't use an automatic
        // hyphenation opportunity within a word that contains another
        // manual hyphenation, unless it is the only choice.
        if (wordWrapping || !atAutoHyphenWithManualHyphenInSameWord) {
          lastCandidateBreak = lastBreak;
          lastCandidateBreakTrimmableChars = lastBreakTrimmableChars;
          lastCandidateBreakTrimmableAdvance = lastBreakTrimmableAdvance;
          lastCandidateBreakUsedHyphenation = lastBreakUsedHyphenation;
          lastCandidateBreakPriority = aBreakPriority;
        }
      }
    }

    // If we're re-scanning part of a word (to re-process potential
    // hyphenation types) then we don't want to accumulate widths again
    // for the characters that were already added to `advance`.
    if (i < rescanLimit) {
      continue;
    }

    gfxFloat charAdvance;
    if (i >= ligatureRange.start && i < ligatureRange.end) {
      charAdvance = GetAdvanceForGlyph(i, letterSpacing);
      if (haveSpacing) {
        PropertyProvider::Spacing* space =
            &spacingBuffer[i - bufferRange.start];
        charAdvance += space->mBefore + space->mAfter;
      }
    } else {
      charAdvance = ComputePartialLigatureWidth(Range(i, i + 1), &aProvider);
    }

    advance += charAdvance;
    if (aOutTrimmableWhitespace) {
      if (charGlyphs[i].CharIsSpace()) {
        ++trimmableChars;
        trimmableAdvance += charAdvance;
      } else {
        trimmableAdvance = 0;
        trimmableChars = 0;
      }
    }
  }

  if (!aborted) {
    width += advance;
  }

  // There are three possibilities:
  // 1) all the text fit (width <= aWidth)
  // 2) some of the text fit up to a break opportunity (width > aWidth &&
  //    lastBreak >= 0)
  // 3) none of the text fits before a break opportunity (width > aWidth &&
  //    lastBreak < 0)
  uint32_t charsFit;
  aOutUsedHyphenation = false;
  if (width - trimmableAdvance <= aWidth) {
    charsFit = aMaxLength;
  } else if (lastBreak >= 0) {
    if (lastCandidateBreak >= 0 && lastCandidateBreak != lastBreak) {
      lastBreak = lastCandidateBreak;
      lastBreakTrimmableChars = lastCandidateBreakTrimmableChars;
      lastBreakTrimmableAdvance = lastCandidateBreakTrimmableAdvance;
      lastBreakUsedHyphenation = lastCandidateBreakUsedHyphenation;
      aBreakPriority = lastCandidateBreakPriority;
    }
    charsFit = lastBreak - aStart;
    trimmableChars = lastBreakTrimmableChars;
    trimmableAdvance = lastBreakTrimmableAdvance;
    aOutUsedHyphenation = lastBreakUsedHyphenation;
  } else {
    charsFit = aMaxLength;
  }

  // Get the overall metrics of the range that fit (including any potentially
  // trimmable or hanging whitespace).
  aOutMetrics = MeasureText(Range(aStart, aStart + charsFit), aBoundingBoxType,
                            aRefDrawTarget, &aProvider);

  if (aOutTrimmableWhitespace) {
    aOutTrimmableWhitespace->mAdvance = trimmableAdvance;
    aOutTrimmableWhitespace->mCount = trimmableChars;
  }

  if (charsFit == aMaxLength) {
    if (lastBreak < 0) {
      aOutLastBreak = UINT32_MAX;
    } else {
      aOutLastBreak = lastBreak - aStart;
    }
  }

  return charsFit;
}

gfxFloat gfxTextRun::GetAdvanceWidth(
    Range aRange, const PropertyProvider* aProvider,
    PropertyProvider::Spacing* aSpacing) const {
  NS_ASSERTION(aRange.end <= GetLength(), "Substring out of range");

  Range ligatureRange = aRange;
  bool adjusted = ShrinkToLigatureBoundaries(&ligatureRange);

  gfxFloat result =
      adjusted ? ComputePartialLigatureWidth(
                     Range(aRange.start, ligatureRange.start), aProvider) +
                     ComputePartialLigatureWidth(
                         Range(ligatureRange.end, aRange.end), aProvider)
               : 0.0;

  if (aSpacing) {
    aSpacing->mBefore = aSpacing->mAfter = 0;
  }

  // Account for all remaining spacing here. This is more efficient than
  // processing it along with the glyphs.
  if (aProvider && (mFlags & gfx::ShapedTextFlags::TEXT_ENABLE_SPACING)) {
    uint32_t i;
    AutoTArray<PropertyProvider::Spacing, 200> spacingBuffer;
    if (spacingBuffer.AppendElements(aRange.Length(), fallible)) {
      if (GetAdjustedSpacing(this, ligatureRange, *aProvider,
                             spacingBuffer.Elements())) {
        for (i = 0; i < ligatureRange.Length(); ++i) {
          PropertyProvider::Spacing* space = &spacingBuffer[i];
          result += space->mBefore + space->mAfter;
        }
        if (aSpacing) {
          aSpacing->mBefore = spacingBuffer[0].mBefore;
          aSpacing->mAfter = spacingBuffer.LastElement().mAfter;
        }
      }
    }
  }

  return result +
         GetAdvanceForGlyphs(ligatureRange,
                             aProvider ? aProvider->LetterSpacing() : 0);
}

gfxFloat gfxTextRun::GetMinAdvanceWidth(Range aRange,
                                        nscoord aLetterSpacing) const {
  MOZ_ASSERT(aRange.end <= GetLength(), "Substring out of range");

  Range ligatureRange = aRange;
  bool adjusted = ShrinkToLigatureBoundaries(&ligatureRange);

  gfxFloat result =
      adjusted
          ? std::max(ComputePartialLigatureWidth(
                         Range(aRange.start, ligatureRange.start), nullptr),
                     ComputePartialLigatureWidth(
                         Range(ligatureRange.end, aRange.end), nullptr))
          : 0.0;

  // Compute min advance width by assuming each grapheme cluster takes its own
  // line.
  gfxFloat clusterAdvance = 0;
  for (uint32_t i = ligatureRange.start; i < ligatureRange.end; ++i) {
    if (mCharacterGlyphs[i].CharIsSpace()) {
      // Skip space char to prevent its advance width contributing to the
      // result. That is, don't consider a space can be in its own line.
      continue;
    }
    clusterAdvance += GetAdvanceForGlyph(i, aLetterSpacing);
    if (i + 1 == ligatureRange.end || IsClusterStart(i + 1)) {
      result = std::max(result, clusterAdvance);
      clusterAdvance = 0;
    }
  }

  return result;
}

bool gfxTextRun::SetLineBreaks(Range aRange, bool aLineBreakBefore,
                               bool aLineBreakAfter,
                               gfxFloat* aAdvanceWidthDelta) {
  // Do nothing because our shaping does not currently take linebreaks into
  // account. There is no change in advance width.
  if (aAdvanceWidthDelta) {
    *aAdvanceWidthDelta = 0;
  }
  return false;
}

const gfxTextRun::GlyphRun* gfxTextRun::FindFirstGlyphRunContaining(
    uint32_t aOffset) const {
  MOZ_ASSERT(aOffset <= GetLength(), "Bad offset looking for glyphrun");
  MOZ_ASSERT(GetLength() == 0 || !mGlyphRuns.IsEmpty(),
             "non-empty text but no glyph runs present!");
  if (mGlyphRuns.Length() <= 1) {
    return mGlyphRuns.begin();
  }
  if (aOffset == GetLength()) {
    return mGlyphRuns.end() - 1;
  }
  const auto* start = mGlyphRuns.begin();
  const auto* limit = mGlyphRuns.end();
  while (limit - start > 1) {
    const auto* mid = start + (limit - start) / 2;
    if (mid->mCharacterOffset <= aOffset) {
      start = mid;
    } else {
      limit = mid;
    }
  }
  MOZ_ASSERT(start->mCharacterOffset <= aOffset,
             "Hmm, something went wrong, aOffset should have been found");
  return start;
}

void gfxTextRun::AddGlyphRun(gfxFont* aFont, FontMatchType aMatchType,
                             uint32_t aUTF16Offset, bool aForceNewRun,
                             gfx::ShapedTextFlags aOrientation, bool aIsCJK) {
  MOZ_ASSERT(aFont, "adding glyph run for null font!");
  MOZ_ASSERT(aOrientation != gfx::ShapedTextFlags::TEXT_ORIENT_VERTICAL_MIXED,
             "mixed orientation should have been resolved");
  if (!aFont) {
    return;
  }

  if (mGlyphRuns.IsEmpty()) {
    mGlyphRuns.AppendElement(
        GlyphRun{aFont, aUTF16Offset, aOrientation, aMatchType, aIsCJK});
    return;
  }

  uint32_t numGlyphRuns = mGlyphRuns.Length();
  if (!aForceNewRun) {
    GlyphRun* lastGlyphRun = &mGlyphRuns.LastElement();

    MOZ_ASSERT(lastGlyphRun->mCharacterOffset <= aUTF16Offset,
               "Glyph runs out of order (and run not forced)");

    // Don't append a run if the font is already the one we want
    if (lastGlyphRun->Matches(aFont, aOrientation, aIsCJK, aMatchType)) {
      return;
    }

    // If the offset has not changed, avoid leaving a zero-length run
    // by overwriting the last entry instead of appending...
    if (lastGlyphRun->mCharacterOffset == aUTF16Offset) {
      // ...except that if the run before the last entry had the same
      // font as the new one wants, merge with it instead of creating
      // adjacent runs with the same font
      if (numGlyphRuns > 1 && mGlyphRuns[numGlyphRuns - 2].Matches(
                                  aFont, aOrientation, aIsCJK, aMatchType)) {
        mGlyphRuns.TruncateLength(numGlyphRuns - 1);
        return;
      }

      lastGlyphRun->SetProperties(aFont, aOrientation, aIsCJK, aMatchType);
      return;
    }
  }

  MOZ_ASSERT(
      aForceNewRun || numGlyphRuns > 0 || aUTF16Offset == 0,
      "First run doesn't cover the first character (and run not forced)?");

  mGlyphRuns.AppendElement(
      GlyphRun{aFont, aUTF16Offset, aOrientation, aMatchType, aIsCJK});
}

void gfxTextRun::SanitizeGlyphRuns() {
  if (mGlyphRuns.Length() < 2) {
    return;
  }

  auto& runs = mGlyphRuns.Array();

  // The runs are almost certain to be already sorted, so it's worth avoiding
  // the Sort() call if possible.
  bool isSorted = true;
  uint32_t prevOffset = 0;
  for (const auto& r : runs) {
    if (r.mCharacterOffset < prevOffset) {
      isSorted = false;
      break;
    }
    prevOffset = r.mCharacterOffset;
  }
  if (!isSorted) {
    runs.Sort(GlyphRunOffsetComparator());
  }

  // Coalesce adjacent glyph runs that have the same properties, and eliminate
  // any empty runs.
  GlyphRun* prevRun = nullptr;
  const CompressedGlyph* charGlyphs = mCharacterGlyphs;

  runs.RemoveElementsBy([&](GlyphRun& aRun) -> bool {
    // First run is always retained.
    if (!prevRun) {
      prevRun = &aRun;
      return false;
    }

    // Merge any run whose properties match its predecessor.
    if (prevRun->Matches(aRun.mFont, aRun.mOrientation, aRun.mIsCJK,
                         aRun.mMatchType)) {
      return true;
    }

    if (prevRun->mCharacterOffset >= aRun.mCharacterOffset) {
      // Preceding run is empty (or has become so due to the adjusting for
      // ligature boundaries), so we will overwrite it with this one, which
      // will then be discarded.
      *prevRun = aRun;
      return true;
    }

    // If any glyph run starts with ligature-continuation characters, we need to
    // advance it to the first "real" character to avoid drawing partial
    // ligature glyphs from wrong font (seen with U+FEFF in reftest 474417-1, as
    // Core Text eliminates the glyph, which makes it appear as if a ligature
    // has been formed)
    while (aRun.mCharacterOffset < GetLength() &&
           charGlyphs[aRun.mCharacterOffset].IsLigatureContinuation()) {
      aRun.mCharacterOffset++;
    }

    // We're keeping another run, so update prevRun pointer to refer to it (in
    // its new position).
    ++prevRun;
    return false;
  });

  MOZ_ASSERT(prevRun == &runs.LastElement(), "lost track of prevRun!");

  // Drop any trailing empty run.
  if (runs.Length() > 1 && prevRun->mCharacterOffset == GetLength()) {
    runs.RemoveLastElement();
  }

  MOZ_ASSERT(!runs.IsEmpty());
  if (runs.Length() == 1) {
    mGlyphRuns.ConvertToElement();
  }
}

void gfxTextRun::CopyGlyphDataFrom(gfxShapedWord* aShapedWord,
                                   uint32_t aOffset) {
  uint32_t wordLen = aShapedWord->GetLength();
  MOZ_ASSERT(aOffset + wordLen <= GetLength(), "word overruns end of textrun");

  CompressedGlyph* charGlyphs = GetCharacterGlyphs();
  const CompressedGlyph* wordGlyphs = aShapedWord->GetCharacterGlyphs();
  if (aShapedWord->HasDetailedGlyphs()) {
    for (uint32_t i = 0; i < wordLen; ++i, ++aOffset) {
      const CompressedGlyph& g = wordGlyphs[i];
      if (!g.IsSimpleGlyph()) {
        uint32_t count = g.GetGlyphCount();
        const DetailedGlyph* details =
            count > 0 ? aShapedWord->GetDetailedGlyphs(i, count) : nullptr;
        SetDetailedGlyphs(aOffset, count, details);
      }
      charGlyphs[aOffset] = g;
    }
  } else {
    memcpy(charGlyphs + aOffset, wordGlyphs, wordLen * sizeof(CompressedGlyph));
  }
}

void gfxTextRun::CopyGlyphDataFrom(gfxTextRun* aSource, Range aRange,
                                   uint32_t aDest) {
  MOZ_ASSERT(aRange.end <= aSource->GetLength(),
             "Source substring out of range");
  MOZ_ASSERT(aDest + aRange.Length() <= GetLength(),
             "Destination substring out of range");

  if (aSource->mDontSkipDrawing) {
    mDontSkipDrawing = true;
  }

  // Copy base glyph data, and DetailedGlyph data where present
  const CompressedGlyph* srcGlyphs = aSource->mCharacterGlyphs + aRange.start;
  CompressedGlyph* dstGlyphs = mCharacterGlyphs + aDest;
  for (uint32_t i = 0; i < aRange.Length(); ++i) {
    CompressedGlyph g = srcGlyphs[i];
    g.SetCanBreakBefore(!g.IsClusterStart()
                            ? CompressedGlyph::FLAG_BREAK_TYPE_NONE
                            : dstGlyphs[i].CanBreakBefore());
    if (!g.IsSimpleGlyph()) {
      if (uint32_t count = g.GetGlyphCount()) {
        // DetailedGlyphs allocation is infallible, so this should never be
        // null unless the source textrun is somehow broken.
        const auto* src = aSource->GetDetailedGlyphs(i + aRange.start, count);
        MOZ_ASSERT(src, "missing DetailedGlyphs?");
        if (src) {
          DetailedGlyph* dst = AllocateDetailedGlyphs(i + aDest, count);
          ::memcpy(dst, src, count * sizeof(DetailedGlyph));
        } else {
          g.SetMissing();
        }
      }
    }
    dstGlyphs[i] = g;
  }

  // Copy glyph runs
#ifdef DEBUG
  GlyphRun* prevRun = nullptr;
#endif
  for (GlyphRunIterator iter(aSource, aRange); !iter.AtEnd(); iter.NextRun()) {
    gfxFont* font = iter.GlyphRun()->mFont;
    MOZ_ASSERT(!prevRun || !prevRun->Matches(iter.GlyphRun()->mFont,
                                             iter.GlyphRun()->mOrientation,
                                             iter.GlyphRun()->mIsCJK,
                                             FontMatchType::Kind::kUnspecified),
               "Glyphruns not coalesced?");
#ifdef DEBUG
    prevRun = const_cast<GlyphRun*>(iter.GlyphRun());
    uint32_t end = iter.StringEnd();
#endif
    uint32_t start = iter.StringStart();

    // These used to be NS_ASSERTION()s, but WARNING is more appropriate.
    // Although it's unusual (and not desirable), it's possible for us to assign
    // different fonts to a base character and a following diacritic.
    // Example on OSX 10.5/10.6 with default fonts installed:
    //     data:text/html,<p style="font-family:helvetica, arial, sans-serif;">
    //                    &%23x043E;&%23x0486;&%23x20;&%23x043E;&%23x0486;
    // This means the rendering of the cluster will probably not be very good,
    // but it's the best we can do for now if the specified font only covered
    // the initial base character and not its applied marks.
    NS_WARNING_ASSERTION(aSource->IsClusterStart(start),
                         "Started font run in the middle of a cluster");
    NS_WARNING_ASSERTION(
        end == aSource->GetLength() || aSource->IsClusterStart(end),
        "Ended font run in the middle of a cluster");

    AddGlyphRun(font, iter.GlyphRun()->mMatchType, start - aRange.start + aDest,
                false, iter.GlyphRun()->mOrientation, iter.GlyphRun()->mIsCJK);
  }
}

void gfxTextRun::ClearGlyphsAndCharacters() {
  ResetGlyphRuns();
  memset(reinterpret_cast<char*>(mCharacterGlyphs), 0,
         mLength * sizeof(CompressedGlyph));
  mDetailedGlyphs = nullptr;
}

void gfxTextRun::SetSpaceGlyph(gfxFont* aFont, DrawTarget* aDrawTarget,
                               uint32_t aCharIndex,
                               gfx::ShapedTextFlags aOrientation) {
  if (SetSpaceGlyphIfSimple(aFont, aCharIndex, ' ', aOrientation)) {
    return;
  }

  gfx::ShapedTextFlags flags =
      gfx::ShapedTextFlags::TEXT_IS_8BIT | aOrientation;
  bool vertical =
      !!(GetFlags() & gfx::ShapedTextFlags::TEXT_ORIENT_VERTICAL_UPRIGHT);
  gfxFontShaper::RoundingFlags roundingFlags =
      aFont->GetRoundOffsetsToPixels(aDrawTarget);
  aFont->ProcessSingleSpaceShapedWord(
      vertical, mAppUnitsPerDevUnit, flags, roundingFlags,
      [&](gfxShapedWord* aShapedWord) {
        const GlyphRun* prevRun = TrailingGlyphRun();
        bool isCJK = prevRun && prevRun->mFont == aFont &&
                             prevRun->mOrientation == aOrientation
                         ? prevRun->mIsCJK
                         : false;
        AddGlyphRun(aFont, FontMatchType::Kind::kUnspecified, aCharIndex, false,
                    aOrientation, isCJK);
        CopyGlyphDataFrom(aShapedWord, aCharIndex);
        GetCharacterGlyphs()[aCharIndex].SetIsSpace();
      });
}

bool gfxTextRun::SetSpaceGlyphIfSimple(gfxFont* aFont, uint32_t aCharIndex,
                                       char16_t aSpaceChar,
                                       gfx::ShapedTextFlags aOrientation) {
  uint32_t spaceGlyph = aFont->GetSpaceGlyph();
  if (!spaceGlyph || !CompressedGlyph::IsSimpleGlyphID(spaceGlyph)) {
    return false;
  }

  gfxFont::Orientation fontOrientation =
      (aOrientation & gfx::ShapedTextFlags::TEXT_ORIENT_VERTICAL_UPRIGHT)
          ? nsFontMetrics::eVertical
          : nsFontMetrics::eHorizontal;
  uint32_t spaceWidthAppUnits = NS_lroundf(
      aFont->GetMetrics(fontOrientation).spaceWidth * mAppUnitsPerDevUnit);
  if (!CompressedGlyph::IsSimpleAdvance(spaceWidthAppUnits)) {
    return false;
  }

  const GlyphRun* prevRun = TrailingGlyphRun();
  bool isCJK = prevRun && prevRun->mFont == aFont &&
                       prevRun->mOrientation == aOrientation
                   ? prevRun->mIsCJK
                   : false;
  AddGlyphRun(aFont, FontMatchType::Kind::kUnspecified, aCharIndex, false,
              aOrientation, isCJK);
  CompressedGlyph g =
      CompressedGlyph::MakeSimpleGlyph(spaceWidthAppUnits, spaceGlyph);
  if (aSpaceChar == ' ') {
    g.SetIsSpace();
  }
  GetCharacterGlyphs()[aCharIndex] = g;
  return true;
}

void gfxTextRun::FetchGlyphExtents(DrawTarget* aRefDrawTarget) const {
  bool needsGlyphExtents = NeedsGlyphExtents();
  if (!needsGlyphExtents && !mDetailedGlyphs) {
    return;
  }

  uint32_t runCount;
  const GlyphRun* glyphRuns = GetGlyphRuns(&runCount);
  CompressedGlyph* charGlyphs = mCharacterGlyphs;
  for (uint32_t i = 0; i < runCount; ++i) {
    const GlyphRun& run = glyphRuns[i];
    gfxFont* font = run.mFont;
    if (MOZ_UNLIKELY(font->GetStyle()->AdjustedSizeMustBeZero())) {
      continue;
    }

    uint32_t start = run.mCharacterOffset;
    uint32_t end =
        i + 1 < runCount ? glyphRuns[i + 1].mCharacterOffset : GetLength();
    gfxGlyphExtents* extents =
        font->GetOrCreateGlyphExtents(mAppUnitsPerDevUnit);

    AutoReadLock lock(extents->mLock);
    for (uint32_t j = start; j < end; ++j) {
      const gfxTextRun::CompressedGlyph* glyphData = &charGlyphs[j];
      if (glyphData->IsSimpleGlyph()) {
        // If we're in speed mode, don't set up glyph extents here; we'll
        // just return "optimistic" glyph bounds later
        if (needsGlyphExtents) {
          uint32_t glyphIndex = glyphData->GetSimpleGlyph();
          if (!extents->IsGlyphKnownLocked(glyphIndex)) {
#ifdef DEBUG_TEXT_RUN_STORAGE_METRICS
            ++gGlyphExtentsSetupEagerSimple;
#endif
            extents->mLock.ReadUnlock();
            font->SetupGlyphExtents(aRefDrawTarget, glyphIndex, false, extents);
            extents->mLock.ReadLock();
          }
        }
      } else if (!glyphData->IsMissing()) {
        uint32_t count = glyphData->GetGlyphCount();
        if (count == 0) {
          continue;
        }
        const auto* details = GetDetailedGlyphs(j, count);
        for (uint32_t k = 0; k < count; ++k, ++details) {
          uint32_t glyphIndex = details->mGlyphID;
          if (!extents->IsGlyphKnownWithTightExtentsLocked(glyphIndex)) {
#ifdef DEBUG_TEXT_RUN_STORAGE_METRICS
            ++gGlyphExtentsSetupEagerTight;
#endif
            extents->mLock.ReadUnlock();
            font->SetupGlyphExtents(aRefDrawTarget, glyphIndex, true, extents);
            extents->mLock.ReadLock();
          }
        }
      }
    }
  }
}

size_t gfxTextRun::SizeOfExcludingThis(MallocSizeOf aMallocSizeOf) {
  size_t total = mGlyphRuns.ShallowSizeOfExcludingThis(aMallocSizeOf);

  if (mDetailedGlyphs) {
    total += mDetailedGlyphs->SizeOfIncludingThis(aMallocSizeOf);
  }

  return total;
}

size_t gfxTextRun::SizeOfIncludingThis(MallocSizeOf aMallocSizeOf) {
  return aMallocSizeOf(this) + SizeOfExcludingThis(aMallocSizeOf);
}

#ifdef DEBUG_FRAME_DUMP
void gfxTextRun::Dump(FILE* out) {
#  define APPEND_FLAG(string_, enum_, field_, flag_)                    \
    if (field_ & enum_::flag_) {                                        \
      string_.AppendPrintf(remaining != field_ ? " %s" : "%s", #flag_); \
      remaining &= ~enum_::flag_;                                       \
    }
#  define APPEND_FLAGS(string_, enum_, field_, flags_)              \
    {                                                               \
      auto remaining = field_;                                      \
      MOZ_FOR_EACH(APPEND_FLAG, (string_, enum_, field_, ), flags_) \
      if (int(remaining)) {                                         \
        string_.AppendPrintf(" %s(0x%0x)", #enum_, int(remaining)); \
      }                                                             \
    }

  nsCString flagsString;
  ShapedTextFlags orient = mFlags & ShapedTextFlags::TEXT_ORIENT_MASK;
  ShapedTextFlags otherFlags = mFlags & ~ShapedTextFlags::TEXT_ORIENT_MASK;
  APPEND_FLAGS(flagsString, ShapedTextFlags, otherFlags,
               (TEXT_IS_RTL, TEXT_ENABLE_SPACING, TEXT_IS_8BIT,
                TEXT_ENABLE_HYPHEN_BREAKS, TEXT_NEED_BOUNDING_BOX,
                TEXT_DISABLE_OPTIONAL_LIGATURES, TEXT_OPTIMIZE_SPEED,
                TEXT_HIDE_CONTROL_CHARACTERS, TEXT_TRAILING_ARABICCHAR,
                TEXT_INCOMING_ARABICCHAR, TEXT_USE_MATH_SCRIPT))

  if (orient != ShapedTextFlags::TEXT_ORIENT_HORIZONTAL &&
      !flagsString.IsEmpty()) {
    flagsString += ' ';
  }

  switch (orient) {
    case ShapedTextFlags::TEXT_ORIENT_HORIZONTAL:
      break;
    case ShapedTextFlags::TEXT_ORIENT_VERTICAL_UPRIGHT:
      flagsString += "TEXT_ORIENT_VERTICAL_UPRIGHT";
      break;
    case ShapedTextFlags::TEXT_ORIENT_VERTICAL_SIDEWAYS_RIGHT:
      flagsString += "TEXT_ORIENT_VERTICAL_SIDEWAYS_RIGHT";
      break;
    case ShapedTextFlags::TEXT_ORIENT_VERTICAL_MIXED:
      flagsString += "TEXT_ORIENT_VERTICAL_MIXED";
      break;
    case ShapedTextFlags::TEXT_ORIENT_VERTICAL_SIDEWAYS_LEFT:
      flagsString += "TEXT_ORIENT_VERTICAL_SIDEWAYS_LEFT";
      break;
    default:
      flagsString.AppendPrintf("UNKNOWN_TEXT_ORIENT_MASK(0x%0x)", int(orient));
      break;
  }

  nsCString flags2String;
  APPEND_FLAGS(
      flags2String, nsTextFrameUtils::Flags, mFlags2,
      (HasTab, HasShy, HasNewline, DontSkipDrawingForPendingUserFonts,
       IsSimpleFlow, IncomingWhitespace, TrailingWhitespace,
       CompressedLeadingWhitespace, NoBreaks, IsTransformed, HasTrailingBreak,
       IsSingleCharMi, MightHaveGlyphChanges, RunSizeAccounted))

#  undef APPEND_FLAGS
#  undef APPEND_FLAG

  nsAutoCString lang;
  mFontGroup->Language()->ToUTF8String(lang);
  fprintf(out, "gfxTextRun@%p (length %u) [%s] [%s] [%s]\n", this, mLength,
          flagsString.get(), flags2String.get(), lang.get());

  fprintf(out, "  Glyph runs:\n");
  for (const auto& run : mGlyphRuns) {
    gfxFont* font = run.mFont;
    const gfxFontStyle* style = font->GetStyle();
    nsAutoCString styleString;
    style->style.ToString(styleString);
    fprintf(out, "    offset=%d %s %f/%g/%s\n", run.mCharacterOffset,
            font->GetName().get(), style->size, style->weight.ToFloat(),
            styleString.get());
  }

  fprintf(out, "  Glyphs:\n");
  for (uint32_t i = 0; i < mLength; ++i) {
    auto glyphData = GetCharacterGlyphs()[i];

    nsCString line;
    line.AppendPrintf("    [%d] 0x%p %s", i, GetCharacterGlyphs() + i,
                      glyphData.IsSimpleGlyph() ? "simple" : "detailed");

    if (glyphData.IsSimpleGlyph()) {
      line.AppendPrintf(" id=%d adv=%d", glyphData.GetSimpleGlyph(),
                        glyphData.GetSimpleAdvance());
    } else {
      if (uint32_t count = glyphData.GetGlyphCount()) {
        const auto* glyphs = GetDetailedGlyphs(i, count);
        line += " ids=";
        for (uint32_t j = 0; j < count; j++) {
          line.AppendPrintf(j ? ",%d" : "%d", glyphs[j].mGlyphID);
        }
        line += " advs=";
        for (uint32_t j = 0; j < count; j++) {
          line.AppendPrintf(j ? ",%d" : "%d", glyphs[j].mAdvance);
        }
        line += " offsets=";
        for (uint32_t j = 0; j < count; j++) {
          auto offset = glyphs[j].mOffset;
          line.AppendPrintf(j ? ",(%g,%g)" : "(%g,%g)", offset.x.value,
                            offset.y.value);
        }
      } else {
        line += " (no glyphs)";
      }
    }

    if (glyphData.CharIsSpace()) {
      line += " CHAR_IS_SPACE";
    }
    if (glyphData.CharIsTab()) {
      line += " CHAR_IS_TAB";
    }
    if (glyphData.CharIsNewline()) {
      line += " CHAR_IS_NEWLINE";
    }
    if (glyphData.CharIsFormattingControl()) {
      line += " CHAR_IS_FORMATTING_CONTROL";
    }
    if (glyphData.CharTypeFlags() &
        CompressedGlyph::FLAG_CHAR_NO_EMPHASIS_MARK) {
      line += " CHAR_NO_EMPHASIS_MARK";
    }

    if (!glyphData.IsSimpleGlyph()) {
      if (!glyphData.IsMissing()) {
        line += " NOT_MISSING";
      }
      if (!glyphData.IsClusterStart()) {
        line += " NOT_IS_CLUSTER_START";
      }
      if (!glyphData.IsLigatureGroupStart()) {
        line += " NOT_LIGATURE_GROUP_START";
      }
    }

    switch (glyphData.CanBreakBefore()) {
      case CompressedGlyph::FLAG_BREAK_TYPE_NORMAL:
        line += " BREAK_TYPE_NORMAL";
        break;
      case CompressedGlyph::FLAG_BREAK_TYPE_HYPHEN:
        line += " BREAK_TYPE_HYPHEN";
        break;
    }

    fprintf(out, "%s\n", line.get());
  }
}
#endif

gfxFontGroup::gfxFontGroup(FontVisibilityProvider* aFontVisibilityProvider,
                           const StyleFontFamilyList& aFontFamilyList,
                           const gfxFontStyle* aStyle, nsAtom* aLanguage,
                           bool aExplicitLanguage,
                           gfxTextPerfMetrics* aTextPerf,
                           gfxUserFontSet* aUserFontSet, gfxFloat aDevToCssSize,
                           StyleFontVariantEmoji aVariantEmoji)
    : mFontVisibilityProvider(
          aFontVisibilityProvider),  // Note that mFontVisibilityProvider may be
                                     // null!
      mFamilyList(aFontFamilyList),
      mStyle(*aStyle),
      mLanguage(aLanguage),
      mDevToCssSize(aDevToCssSize),
      mUserFontSet(aUserFontSet),
      mTextPerf(aTextPerf),
      mPageLang(gfxPlatformFontList::GetFontPrefLangFor(aLanguage)),
      mExplicitLanguage(aExplicitLanguage),
      mFontVariantEmoji(aVariantEmoji) {
  // We don't use SetUserFontSet() here, as we want to unconditionally call
  // EnsureFontList() rather than only do UpdateUserFonts() if it changed.
}

gfxFontGroup::~gfxFontGroup() {
  // Should not be dropped by stylo
  MOZ_ASSERT(!Servo_IsWorkerThread());
}

static StyleGenericFontFamily GetDefaultGeneric(nsAtom* aLanguage) {
  // If we're running on a worker thread, always return sans-serif as default
  // (matching the canvas2d default font), rather than potentially accessing
  // prefs.
  if (dom::GetCurrentThreadWorkerPrivate()) {
    return StyleGenericFontFamily::SansSerif;
  }
  return StaticPresData::Get()
      ->GetFontPrefsForLang(aLanguage)
      ->GetDefaultGeneric();
}

class DeferredClearResolvedFonts final : public nsIRunnable {
 public:
  NS_DECL_THREADSAFE_ISUPPORTS

  DeferredClearResolvedFonts() = delete;
  explicit DeferredClearResolvedFonts(
      const DeferredClearResolvedFonts& aOther) = delete;
  explicit DeferredClearResolvedFonts(
      nsTArray<gfxFontGroup::FamilyFace>&& aFontList)
      : mFontList(std::move(aFontList)) {}

 protected:
  virtual ~DeferredClearResolvedFonts() = default;

  NS_IMETHOD Run(void) override {
    mFontList.Clear();
    return NS_OK;
  }

  nsTArray<gfxFontGroup::FamilyFace> mFontList;
};

NS_IMPL_ISUPPORTS(DeferredClearResolvedFonts, nsIRunnable)

void gfxFontGroup::EnsureFontList() {
  // Ensure resolved font instances are valid; discard them if necessary.
  auto* pfl = gfxPlatformFontList::PlatformFontList();
  if (mFontListGeneration != pfl->GetGeneration()) {
    // Forget cached fonts that may no longer be valid.
    mLastPrefFamily = FontFamily();
    mLastPrefFont = nullptr;
    mDefaultFont = nullptr;
    mResolvedFonts = false;
  }

  // If we have already resolved the font list, just return.
  if (mResolvedFonts) {
    return;
  }

  // Discard existing fonts; but if we're in servo traversal, defer the actual
  // deletion.
  // XXX(jfkthame) is this really necessary, or is the assertion in
  // ~gfxUserFontFamily() obsolete?
  if (gfxFontUtils::IsInServoTraversal()) {
    NS_DispatchToMainThread(new DeferredClearResolvedFonts(std::move(mFonts)));
  } else {
    mFonts.Clear();
  }

  // (Re-)build the list of fonts.
  AutoTArray<FamilyAndGeneric, 10> fonts;

  // lookup fonts in the fontlist
  for (const StyleSingleFontFamily& name : mFamilyList.list.AsSpan()) {
    if (name.IsFamilyName()) {
      const auto& familyName = name.AsFamilyName();
      AddPlatformFont(nsAtomCString(familyName.name.AsAtom()),
                      familyName.syntax == StyleFontFamilyNameSyntax::Quoted,
                      fonts);
    } else {
      MOZ_ASSERT(name.IsGeneric());
      const StyleGenericFontFamily generic = name.AsGeneric();
      // system-ui is usually a single family, so it doesn't work great as
      // fallback. Prefer the following generic or the language default instead.
      if (mFallbackGeneric == StyleGenericFontFamily::None &&
          generic != StyleGenericFontFamily::SystemUi) {
        mFallbackGeneric = generic;
      }
      pfl->AddGenericFonts(mFontVisibilityProvider, generic, mLanguage, fonts);
      if (mTextPerf) {
        mTextPerf->current.genericLookups++;
      }
    }
  }

  // If necessary, append default language generic onto the end.
  if (mFallbackGeneric == StyleGenericFontFamily::None && !mStyle.systemFont) {
    auto defaultLanguageGeneric = GetDefaultGeneric(mLanguage);

    pfl->AddGenericFonts(mFontVisibilityProvider, defaultLanguageGeneric,
                         mLanguage, fonts);
    if (mTextPerf) {
      mTextPerf->current.genericLookups++;
    }
  }

  // build the fontlist from the specified families
  for (const auto& f : fonts) {
    if (f.mFamily.mShared) {
      AddFamilyToFontList(f.mFamily.mShared, f.mGeneric);
    } else {
      AddFamilyToFontList(f.mFamily.mUnshared, f.mGeneric);
    }
  }

  mFontListGeneration = pfl->GetGeneration();
  mResolvedFonts = true;
}

void gfxFontGroup::AddPlatformFont(const nsACString& aName, bool aQuotedName,
                                   nsTArray<FamilyAndGeneric>& aFamilyList) {
  // First, look up in the user font set...
  // If the fontSet matches the family, we must not look for a platform
  // font of the same name, even if we fail to actually get a fontEntry
  // here; we'll fall back to the next name in the CSS font-family list.
  if (mUserFontSet) {
    // Add userfonts to the fontlist whether already loaded
    // or not. Loading is initiated during font matching.
    RefPtr<gfxFontFamily> family = mUserFontSet->LookupFamily(aName);
    if (family) {
      aFamilyList.AppendElement(std::move(family));
      return;
    }
  }

  // Not known in the user font set ==> check system fonts
  gfxPlatformFontList::PlatformFontList()->FindAndAddFamilies(
      mFontVisibilityProvider, StyleGenericFontFamily::None, aName,
      &aFamilyList,
      aQuotedName ? gfxPlatformFontList::FindFamiliesFlags::eQuotedFamilyName
                  : gfxPlatformFontList::FindFamiliesFlags(0),
      &mStyle, mLanguage.get(), mDevToCssSize);
}

void gfxFontGroup::AddFamilyToFontList(gfxFontFamily* aFamily,
                                       StyleGenericFontFamily aGeneric) {
  if (!aFamily) {
    MOZ_ASSERT_UNREACHABLE("don't try to add a null font family!");
    return;
  }
  AutoTArray<gfxFontEntry*, 4> fontEntryList;
  aFamily->FindAllFontsForStyle(mStyle, fontEntryList);
  // add these to the fontlist
  for (gfxFontEntry* fe : fontEntryList) {
    if (!HasFont(fe)) {
      FamilyFace ff(aFamily, fe, aGeneric);
      if (fe->mIsUserFontContainer) {
        ff.CheckState(mSkipDrawing);
      }
      mFonts.AppendElement(ff);
    }
  }
  // for a family marked as "check fallback faces", only mark the last
  // entry so that fallbacks for a family are only checked once
  if (aFamily->CheckForFallbackFaces() && !fontEntryList.IsEmpty() &&
      !mFonts.IsEmpty()) {
    mFonts.LastElement().SetCheckForFallbackFaces();
  }
}

void gfxFontGroup::AddFamilyToFontList(fontlist::Family* aFamily,
                                       StyleGenericFontFamily aGeneric) {
  gfxPlatformFontList* pfl = gfxPlatformFontList::PlatformFontList();
  if (!aFamily->IsInitialized() && !pfl->InitializeFamily(aFamily)) {
    return;
  }
  AutoTArray<fontlist::Face*, 4> faceList;
  aFamily->FindAllFacesForStyle(pfl->SharedFontList(), mStyle, faceList);
  for (auto* face : faceList) {
    gfxFontEntry* fe = pfl->GetOrCreateFontEntry(face, aFamily);
    if (fe && !HasFont(fe)) {
      FamilyFace ff(aFamily, fe, aGeneric);
      mFonts.AppendElement(ff);
    }
  }
}

bool gfxFontGroup::HasFont(const gfxFontEntry* aFontEntry) {
  for (auto& f : mFonts) {
    if (f.FontEntry() == aFontEntry) {
      return true;
    }
  }
  return false;
}

already_AddRefed<gfxFont> gfxFontGroup::GetFontAt(uint32_t i, uint32_t aCh,
                                                  bool* aLoading) {
  if (i >= mFonts.Length()) {
    return nullptr;
  }

  FamilyFace& ff = mFonts[i];
  if (ff.IsInvalid() || ff.IsLoading()) {
    return nullptr;
  }

  RefPtr<gfxFont> font = ff.Font();
  if (!font) {
    RefPtr<gfxFontEntry> fe = ff.FontEntry();
    if (!fe) {
      return nullptr;
    }
    RefPtr<gfxCharacterMap> unicodeRangeMap;
    if (fe->mIsUserFontContainer) {
      // This raw pointer is OK because fe holds a strong ref to the object.
      gfxUserFontEntry* ufe = static_cast<gfxUserFontEntry*>(fe.get());
      if (ufe->LoadState() == gfxUserFontEntry::STATUS_NOT_LOADED &&
          ufe->CharacterInUnicodeRange(aCh) && !*aLoading) {
        ufe->Load();
        ff.CheckState(mSkipDrawing);
        *aLoading = ff.IsLoading();
      }
      unicodeRangeMap = ufe->GetUnicodeRangeMap();
      // Update fe to refer to the actual platform font entry, rather than the
      // webfont wrapper. After this, we no longer have a strong ref to ufe.
      fe = ufe->GetPlatformFontEntry();
      if (!fe) {
        return nullptr;
      }
    }
    font = fe->FindOrMakeFont(&mStyle, unicodeRangeMap);
    if (!font || !font->Valid()) {
      ff.SetInvalid();
      return nullptr;
    }
    ff.SetFont(font);
  }
  return font.forget();
}

void gfxFontGroup::FamilyFace::CheckState(bool& aSkipDrawing) {
  gfxFontEntry* fe = FontEntry();
  if (!fe) {
    return;
  }
  if (fe->mIsUserFontContainer) {
    gfxUserFontEntry* ufe = static_cast<gfxUserFontEntry*>(fe);
    gfxUserFontEntry::UserFontLoadState state = ufe->LoadState();
    switch (state) {
      case gfxUserFontEntry::STATUS_LOAD_PENDING:
      case gfxUserFontEntry::STATUS_LOADING:
        SetLoading(true);
        break;
      case gfxUserFontEntry::STATUS_FAILED:
        SetInvalid();
        // fall-thru to the default case
        [[fallthrough]];
      default:
        SetLoading(false);
    }
    if (ufe->WaitForUserFont()) {
      aSkipDrawing = true;
    }
  }
}

bool gfxFontGroup::FamilyFace::EqualsUserFont(
    const gfxUserFontEntry* aUserFont) const {
  gfxFontEntry* fe = FontEntry();
  // if there's a font, the entry is the underlying platform font
  if (mFontCreated) {
    if (aUserFont->PlatformFontEntryEquals(fe)) {
      return true;
    }
  } else if (fe == aUserFont) {
    return true;
  }
  return false;
}

static nsAutoCString FamilyListToString(
    const StyleFontFamilyList& aFamilyList) {
  return StringJoin(","_ns, aFamilyList.list.AsSpan(),
                    [](nsACString& dst, const StyleSingleFontFamily& name) {
                      name.AppendToString(dst);
                    });
}

already_AddRefed<gfxFont> gfxFontGroup::GetDefaultFont() {
  if (mDefaultFont) {
    return do_AddRef(mDefaultFont);
  }

  gfxPlatformFontList* pfl = gfxPlatformFontList::PlatformFontList();
  FontFamily family = pfl->GetDefaultFont(mFontVisibilityProvider, &mStyle);
  MOZ_ASSERT(!family.IsNull(),
             "invalid default font returned by GetDefaultFont");

  gfxFontEntry* fe = nullptr;
  if (family.mShared) {
    fontlist::Family* fam = family.mShared;
    if (!fam->IsInitialized()) {
      // If this fails, FindFaceForStyle will just safely return nullptr
      (void)pfl->InitializeFamily(fam);
    }
    fontlist::Face* face = fam->FindFaceForStyle(pfl->SharedFontList(), mStyle);
    if (face) {
      fe = pfl->GetOrCreateFontEntry(face, fam);
    }
  } else {
    fe = family.mUnshared->FindFontForStyle(mStyle);
  }
  if (fe) {
    mDefaultFont = fe->FindOrMakeFont(&mStyle);
  }

  uint32_t numInits, loaderState;
  pfl->GetFontlistInitInfo(numInits, loaderState);

  MOZ_ASSERT(numInits != 0,
             "must initialize system fontlist before getting default font!");

  uint32_t numFonts = 0;
  if (!mDefaultFont) {
    // Try for a "font of last resort...."
    // Because an empty font list would be Really Bad for later code
    // that assumes it will be able to get valid metrics for layout,
    // just look for the first usable font and put in the list.
    // (see bug 554544)
    if (pfl->SharedFontList()) {
      fontlist::FontList* list = pfl->SharedFontList();
      numFonts = list->NumFamilies();
      fontlist::Family* families = list->Families();
      for (uint32_t i = 0; i < numFonts; ++i) {
        fontlist::Family* fam = &families[i];
        if (!fam->IsInitialized()) {
          (void)pfl->InitializeFamily(fam);
        }
        fontlist::Face* face =
            fam->FindFaceForStyle(pfl->SharedFontList(), mStyle);
        if (face) {
          fe = pfl->GetOrCreateFontEntry(face, fam);
          if (fe) {
            mDefaultFont = fe->FindOrMakeFont(&mStyle);
            if (mDefaultFont) {
              break;
            }
            NS_WARNING("FindOrMakeFont failed");
          }
        }
      }
    } else {
      AutoTArray<RefPtr<gfxFontFamily>, 200> familyList;
      pfl->GetFontFamilyList(familyList);
      numFonts = familyList.Length();
      for (uint32_t i = 0; i < numFonts; ++i) {
        gfxFontEntry* fe = familyList[i]->FindFontForStyle(mStyle, true);
        if (fe) {
          mDefaultFont = fe->FindOrMakeFont(&mStyle);
          if (mDefaultFont) {
            break;
          }
        }
      }
    }
  }

  if (!mDefaultFont) {
    // We must have failed to find anything usable in our font-family list,
    // or it's badly broken. One more last-ditch effort to make a font:
    if (gfxFontEntry* fe = pfl->GetDefaultFontEntry()) {
      if (RefPtr<gfxFont> f = fe->FindOrMakeFont(&mStyle)) {
        return f.forget();
      }
    }

    // an empty font list at this point is fatal; we're not going to
    // be able to do even the most basic layout operations

    // annotate crash report with fontlist info
    nsAutoCString fontInitInfo;
    fontInitInfo.AppendPrintf("no fonts - init: %d fonts: %d loader: %d",
                              numInits, numFonts, loaderState);
#ifdef XP_WIN
    bool dwriteEnabled = gfxWindowsPlatform::GetPlatform()->DWriteEnabled();
    double upTime = (double)GetTickCount();
    fontInitInfo.AppendPrintf(" backend: %s system-uptime: %9.3f sec",
                              dwriteEnabled ? "directwrite" : "gdi",
                              upTime / 1000);
#endif
    gfxCriticalError() << fontInitInfo.get();

    char msg[256];  // CHECK buffer length if revising message below
    SprintfLiteral(msg, "unable to find a usable font (%.220s)",
                   FamilyListToString(mFamilyList).get());
    MOZ_CRASH_UNSAFE(msg);
  }

  return do_AddRef(mDefaultFont);
}

already_AddRefed<gfxFont> gfxFontGroup::GetFirstValidFont(
    uint32_t aCh, StyleGenericFontFamily* aGeneric, bool* aIsFirst) {
  EnsureFontList();

  uint32_t count = mFonts.Length();
  bool loading = false;

  // Check whether the font supports the given character, unless aCh is the
  // kCSSFirstAvailableFont constant, in which case (as per CSS Fonts spec)
  // we want the first font whose unicode-range does not exclude <space>,
  // regardless of whether it in fact supports the <space> character.
  auto isValidForChar = [](gfxFont* aFont, uint32_t aCh) -> bool {
    if (!aFont) {
      return false;
    }
    if (aCh == kCSSFirstAvailableFont) {
      if (const auto* unicodeRange = aFont->GetUnicodeRangeMap()) {
        return unicodeRange->test(' ');
      }
      return true;
    }
    return aFont->HasCharacter(aCh);
  };

  for (uint32_t i = 0; i < count; ++i) {
    FamilyFace& ff = mFonts[i];
    if (ff.IsInvalid()) {
      continue;
    }

    // already have a font?
    RefPtr<gfxFont> font = ff.Font();
    if (isValidForChar(font, aCh)) {
      if (aGeneric) {
        *aGeneric = ff.Generic();
      }
      if (aIsFirst) {
        *aIsFirst = (i == 0);
      }
      return font.forget();
    }

    // Need to build a font, loading userfont if not loaded. In
    // cases where unicode range might apply, use the character
    // provided.
    gfxFontEntry* fe = ff.FontEntry();
    if (fe && fe->mIsUserFontContainer) {
      gfxUserFontEntry* ufe = static_cast<gfxUserFontEntry*>(fe);
      bool inRange = ufe->CharacterInUnicodeRange(
          aCh == kCSSFirstAvailableFont ? ' ' : aCh);
      if (inRange) {
        if (!loading &&
            ufe->LoadState() == gfxUserFontEntry::STATUS_NOT_LOADED) {
          ufe->Load();
          ff.CheckState(mSkipDrawing);
        }
        if (ff.IsLoading()) {
          loading = true;
        }
      }
      if (ufe->LoadState() != gfxUserFontEntry::STATUS_LOADED || !inRange) {
        continue;
      }
    }

    font = GetFontAt(i, aCh, &loading);
    if (isValidForChar(font, aCh)) {
      if (aGeneric) {
        *aGeneric = ff.Generic();
      }
      if (aIsFirst) {
        *aIsFirst = (i == 0);
      }
      return font.forget();
    }
  }
  if (aGeneric) {
    *aGeneric = StyleGenericFontFamily::None;
  }
  if (aIsFirst) {
    *aIsFirst = false;
  }
  return GetDefaultFont();
}

already_AddRefed<gfxFont> gfxFontGroup::GetFirstMathFont() {
  EnsureFontList();
  uint32_t count = mFonts.Length();
  for (uint32_t i = 0; i < count; ++i) {
    RefPtr<gfxFont> font = GetFontAt(i);
    if (font && font->TryGetMathTable()) {
      return font.forget();
    }
  }
  return nullptr;
}

already_AddRefed<gfxTextRun> gfxFontGroup::MakeEmptyTextRun(
    const Parameters* aParams, gfx::ShapedTextFlags aFlags,
    nsTextFrameUtils::Flags aFlags2) {
  aFlags |= ShapedTextFlags::TEXT_IS_8BIT;
  return gfxTextRun::Create(aParams, 0, this, aFlags, aFlags2);
}

already_AddRefed<gfxTextRun> gfxFontGroup::MakeSpaceTextRun(
    const Parameters* aParams, gfx::ShapedTextFlags aFlags,
    nsTextFrameUtils::Flags aFlags2) {
  aFlags |= ShapedTextFlags::TEXT_IS_8BIT;

  RefPtr<gfxTextRun> textRun =
      gfxTextRun::Create(aParams, 1, this, aFlags, aFlags2);
  if (!textRun) {
    return nullptr;
  }

  gfx::ShapedTextFlags orientation = aFlags & ShapedTextFlags::TEXT_ORIENT_MASK;
  if (orientation == ShapedTextFlags::TEXT_ORIENT_VERTICAL_MIXED) {
    orientation = ShapedTextFlags::TEXT_ORIENT_VERTICAL_SIDEWAYS_RIGHT;
  }

  RefPtr<gfxFont> font = GetFirstValidFont();
  if (MOZ_UNLIKELY(GetStyle()->AdjustedSizeMustBeZero())) {
    // Short-circuit for size-0 fonts, as Windows and ATSUI can't handle
    // them, and always create at least size 1 fonts, i.e. they still
    // render something for size 0 fonts.
    textRun->AddGlyphRun(font, FontMatchType::Kind::kUnspecified, 0, false,
                         orientation, false);
  } else {
    if (font->GetSpaceGlyph()) {
      // Normally, the font has a cached space glyph, so we can avoid
      // the cost of calling FindFontForChar.
      textRun->SetSpaceGlyph(font, aParams->mDrawTarget, 0, orientation);
    } else {
      // In case the primary font doesn't have <space> (bug 970891),
      // find one that does.
      FontMatchType matchType;
      RefPtr<gfxFont> spaceFont =
          FindFontForChar(' ', 0, 0, Script::LATIN, nullptr, &matchType);
      if (spaceFont) {
        textRun->SetSpaceGlyph(spaceFont, aParams->mDrawTarget, 0, orientation);
      }
    }
  }

  // Note that the gfxGlyphExtents glyph bounds storage for the font will
  // always contain an entry for the font's space glyph, so we don't have
  // to call FetchGlyphExtents here.
  return textRun.forget();
}

template <typename T>
already_AddRefed<gfxTextRun> gfxFontGroup::MakeBlankTextRun(
    const T* aString, uint32_t aLength, const Parameters* aParams,
    gfx::ShapedTextFlags aFlags, nsTextFrameUtils::Flags aFlags2) {
  RefPtr<gfxTextRun> textRun =
      gfxTextRun::Create(aParams, aLength, this, aFlags, aFlags2);
  if (!textRun) {
    return nullptr;
  }

  gfx::ShapedTextFlags orientation = aFlags & ShapedTextFlags::TEXT_ORIENT_MASK;
  if (orientation == ShapedTextFlags::TEXT_ORIENT_VERTICAL_MIXED) {
    orientation = ShapedTextFlags::TEXT_ORIENT_VERTICAL_UPRIGHT;
  }
  RefPtr<gfxFont> font = GetFirstValidFont();
  textRun->AddGlyphRun(font, FontMatchType::Kind::kUnspecified, 0, false,
                       orientation, false);

  textRun->SetupClusterBoundaries(0, aString, aLength);

  for (uint32_t i = 0; i < aLength; i++) {
    if (aString[i] == '\n') {
      textRun->SetIsNewline(i);
    } else if (aString[i] == '\t') {
      textRun->SetIsTab(i);
    }
  }

  return textRun.forget();
}

already_AddRefed<gfxTextRun> gfxFontGroup::MakeHyphenTextRun(
    DrawTarget* aDrawTarget, gfx::ShapedTextFlags aFlags,
    uint32_t aAppUnitsPerDevUnit) {
  // only use U+2010 if it is supported by the first font in the group;
  // it's better to use ASCII '-' from the primary font than to fall back to
  // U+2010 from some other, possibly poorly-matching face
  static const char16_t hyphen = 0x2010;
  RefPtr<gfxFont> font = GetFirstValidFont(uint32_t(hyphen));
  if (font->HasCharacter(hyphen)) {
    return MakeTextRun(&hyphen, 1, aDrawTarget, aAppUnitsPerDevUnit, aFlags,
                       nsTextFrameUtils::Flags(), nullptr);
  }

  static const uint8_t dash = '-';
  return MakeTextRun(&dash, 1, aDrawTarget, aAppUnitsPerDevUnit, aFlags,
                     nsTextFrameUtils::Flags(), nullptr);
}

gfxFloat gfxFontGroup::GetHyphenWidth(
    const gfxTextRun::PropertyProvider* aProvider) {
  if (mHyphenWidth < 0) {
    RefPtr<DrawTarget> dt(aProvider->GetDrawTarget());
    if (dt) {
      RefPtr<gfxTextRun> hyphRun(
          MakeHyphenTextRun(dt, aProvider->GetShapedTextFlags(),
                            aProvider->GetAppUnitsPerDevUnit()));
      mHyphenWidth = hyphRun.get() ? hyphRun->GetAdvanceWidth() : 0;
    }
  }
  return mHyphenWidth;
}

template <typename T>
already_AddRefed<gfxTextRun> gfxFontGroup::MakeTextRun(
    const T* aString, uint32_t aLength, const Parameters* aParams,
    gfx::ShapedTextFlags aFlags, nsTextFrameUtils::Flags aFlags2,
    gfxMissingFontRecorder* aMFR) {
  if (aLength == 0) {
    return MakeEmptyTextRun(aParams, aFlags, aFlags2);
  }
  if (aLength == 1 && aString[0] == ' ') {
    return MakeSpaceTextRun(aParams, aFlags, aFlags2);
  }

  if constexpr (sizeof(T) == sizeof(uint8_t)) {
    aFlags |= ShapedTextFlags::TEXT_IS_8BIT;
  }

  if (MOZ_UNLIKELY(GetStyle()->AdjustedSizeMustBeZero())) {
    // Short-circuit for size-0 fonts, as Windows and ATSUI can't handle
    // them, and always create at least size 1 fonts, i.e. they still
    // render something for size 0 fonts.
    return MakeBlankTextRun(aString, aLength, aParams, aFlags, aFlags2);
  }

  RefPtr<gfxTextRun> textRun =
      gfxTextRun::Create(aParams, aLength, this, aFlags, aFlags2);
  if (!textRun) {
    return nullptr;
  }

  InitTextRun(aParams->mDrawTarget, textRun.get(), aString, aLength, aMFR);

  textRun->FetchGlyphExtents(aParams->mDrawTarget);

  return textRun.forget();
}

// MakeTextRun instantiations (needed by Linux64 base-toolchain build).
template already_AddRefed<gfxTextRun> gfxFontGroup::MakeTextRun(
    const uint8_t* aString, uint32_t aLength, const Parameters* aParams,
    gfx::ShapedTextFlags aFlags, nsTextFrameUtils::Flags aFlags2,
    gfxMissingFontRecorder* aMFR);
template already_AddRefed<gfxTextRun> gfxFontGroup::MakeTextRun(
    const char16_t* aString, uint32_t aLength, const Parameters* aParams,
    gfx::ShapedTextFlags aFlags, nsTextFrameUtils::Flags aFlags2,
    gfxMissingFontRecorder* aMFR);

// ComputeRanges instantiation (used by
// gfxPlatformFontList::ListFontsUsedForString).
template void gfxFontGroup::ComputeRanges(nsTArray<TextRange>&, const char16_t*,
                                          uint32_t, Script,
                                          gfx::ShapedTextFlags);

// Helper to get a hashtable that maps tags to Script codes, created on first
// use.
static const nsTHashMap<nsUint32HashKey, Script>* ScriptTagToCodeTable() {
  using TableT = nsTHashMap<nsUint32HashKey, Script>;

  // Initialize our static var by creating the hashtable and populating it with
  // all the valid codes.
  // According to
  // https://en.cppreference.com/w/cpp/language/storage_duration#Static_block_variables:
  // "If multiple threads attempt to initialize the same static local variable
  // concurrently, the initialization occurs exactly once."
  static UniquePtr<TableT> sScriptTagToCode = []() {
    auto tagToCode = MakeUnique<TableT>(size_t(Script::NUM_SCRIPT_CODES));
    Script scriptCount =
        Script(std::min<int>(UnicodeProperties::GetMaxNumberOfScripts() + 1,
                             int(Script::NUM_SCRIPT_CODES)));
    for (Script s = Script::ARABIC; s < scriptCount;
         s = Script(static_cast<int>(s) + 1)) {
      uint32_t tag = GetScriptTagForCode(s);
      if (tag != HB_SCRIPT_UNKNOWN) {
        tagToCode->InsertOrUpdate(tag, s);
      }
    }
    // Clearing the UniquePtr at shutdown will free the table. The call to
    // ClearOnShutdown has to be done on the main thread, even if this
    // initialization happens from a worker.
    if (NS_IsMainThread()) {
      ClearOnShutdown(&sScriptTagToCode);
    } else {
      NS_DispatchToMainThread(
          NS_NewRunnableFunction("ClearOnShutdown(sScriptTagToCode)",
                                 []() { ClearOnShutdown(&sScriptTagToCode); }));
    }
    return tagToCode;
  }();

  return sScriptTagToCode.get();
}

static Script ResolveScriptForLang(const nsAtom* aLanguage, Script aDefault) {
  // Cache for lang-to-script lookups, to avoid constantly needing to parse
  // and resolve the lang code from scratch.
  class LangScriptCache
      : public MruCache<const nsAtom*, std::pair<const nsAtom*, Script>,
                        LangScriptCache> {
   public:
    static bool IsEmpty(const std::pair<const nsAtom*, Script>& aValue) {
      return !aValue.first;
    }
    static HashNumber Hash(const nsAtom* const& aKey) { return aKey->hash(); }
    static bool Match(const nsAtom* const& aKey,
                      const std::pair<const nsAtom*, Script>& aValue) {
      return aKey == aValue.first;
    }
  };

  static LangScriptCache sCache;
  static RWLock sLock("LangScriptCache lock");

  MOZ_ASSERT(aDefault != Script::INVALID &&
             aDefault < Script::NUM_SCRIPT_CODES);

  {
    // Try to use a cached value without taking an exclusive lock.
    AutoReadLock lock(sLock);
    auto p = sCache.Lookup(aLanguage);
    if (p) {
      return p.Data().second;
    }
  }

  // Didn't find an existing entry, so lock the cache and do a full
  // lookup-and-update.
  AutoWriteLock lock(sLock);
  auto p = sCache.Lookup(aLanguage);
  if (p) {
    return p.Data().second;
  }

  Script script = aDefault;
  nsAutoCString lang;
  aLanguage->ToUTF8String(lang);
  Locale locale;
  if (LocaleParser::TryParse(lang, locale).isOk()) {
    if (locale.Script().Missing()) {
      (void)locale.AddLikelySubtags();
    }
    if (locale.Script().Present()) {
      Span span = locale.Script().Span();
      MOZ_ASSERT(span.Length() == 4);
      uint32_t tag = TRUETYPE_TAG(span[0], span[1], span[2], span[3]);
      Script localeScript;
      if (ScriptTagToCodeTable()->Get(tag, &localeScript)) {
        script = localeScript;
      }
    }
  }
  p.Set(std::pair(aLanguage, script));

  return script;
}

void gfxFontGroup::InitTextRunLog(LogModule* aLog, const uint8_t* aString,
                                  const char16_t* aTextPtr,
                                  const gfxScriptItemizer::Run& aRun) {
  nsAutoCString lang;
  mLanguage->ToUTF8String(lang);
  nsAutoCString styleString;
  mStyle.style.ToString(styleString);
  auto defaultGeneric = GetDefaultGeneric(mLanguage);
  MOZ_LOG(
      aLog, LogLevel::Warning,
      ("(%s) fontgroup: [%s] default: %s lang: %s script: %d "
       "len %d weight: %g width: %g%% style: %s size: %6.2f "
       "%d-byte TEXTRUN [%s] ENDTEXTRUN\n",
       (mStyle.systemFont ? "textrunui" : "textrun"),
       FamilyListToString(mFamilyList).get(),
       (defaultGeneric == StyleGenericFontFamily::Serif
            ? "serif"
            : (defaultGeneric == StyleGenericFontFamily::SansSerif
                   ? "sans-serif"
                   : "none")),
       lang.get(), static_cast<int>(aRun.mScript), aRun.mLength,
       mStyle.weight.ToFloat(), mStyle.width.ToFloat(), styleString.get(),
       mStyle.size, aString ? 1 : 2,
       aTextPtr
           ? NS_ConvertUTF16toUTF8(aTextPtr + aRun.mOffset, aRun.mLength).get()
           : nsPromiseFlatCString(
                 nsDependentCSubstring(
                     reinterpret_cast<const char*>(aString) + aRun.mOffset,
                     aRun.mLength))
                 .get()));
}

template <typename T>
void gfxFontGroup::InitTextRun(DrawTarget* aDrawTarget, gfxTextRun* aTextRun,
                               const T* aString, uint32_t aLength,
                               gfxMissingFontRecorder* aMFR) {
  MOZ_DIAGNOSTIC_ASSERT(aLength > 0,
                        "don't call InitTextRun for a zero-length run");

  // we need to do numeral processing even on 8-bit text,
  // in case we're converting Western to Hindi/Arabic digits
  const uint32_t numOption = gfxPlatform::GetPlatform()->GetBidiNumeralOption();
  UniquePtr<char16_t[]> transformedString;
  if (numOption != IBMBIDI_NUMERAL_NOMINAL) {
    // scan the string for numerals that may need to be transformed;
    // if we find any, we'll make a local copy here and use that for
    // font matching and glyph generation/shaping
    bool prevIsArabic =
        !!(aTextRun->GetFlags() & ShapedTextFlags::TEXT_INCOMING_ARABICCHAR);
    for (uint32_t i = 0; i < aLength; ++i) {
      char16_t origCh = aString[i];
      char16_t newCh = HandleNumberInChar(origCh, prevIsArabic, numOption);
      if (newCh != origCh) {
        if (!transformedString) {
          transformedString = MakeUnique<char16_t[]>(aLength);
          if constexpr (sizeof(T) == sizeof(char16_t)) {
            memcpy(transformedString.get(), aString, i * sizeof(char16_t));
          } else {
            for (uint32_t j = 0; j < i; ++j) {
              transformedString[j] = aString[j];
            }
          }
        }
      }
      if (transformedString) {
        transformedString[i] = newCh;
      }
      prevIsArabic = IS_ARABIC_CHAR(newCh);
    }
  }

  LogModule* log = mStyle.systemFont ? gfxPlatform::GetLog(eGfxLog_textrunui)
                                     : gfxPlatform::GetLog(eGfxLog_textrun);

  // If we have 16-bit text, either as the input string or a transformedString,
  // set textPtr to point to it. If textPtr is null, we're using the original
  // aString and it is 8-bit text.
  const char16_t* const textPtr =
      transformedString ? transformedString.get()
      : sizeof(T) == sizeof(char16_t)
          ? reinterpret_cast<const char16_t*>(aString)
          : nullptr;

  // Split into script runs so that script can potentially influence
  // the font matching process below.

  // Fast path for all-ASCII/Latin-extended text: try to skip ScriptItemizer
  // entirely. Note that even if this is the 8-bit version of InitTextRun,
  // it's possible that numeral transformation may have generated non-Latin
  // codepoints and so we have a 16-bit string to consider.
  bool allCommonOrLatin = true;
  if (textPtr) {
    for (uint32_t j = 0; j < aLength && allCommonOrLatin; j++) {
      allCommonOrLatin = textPtr[j] < gfxScriptItemizer::kFirstNonCommonOrLatin;
    }
  }

  Script script = Script::INVALID;
  if (allCommonOrLatin) {
    bool hasLetter = false;
    if (!textPtr) {
      for (uint32_t j = 0; !hasLetter && j < aLength; j++) {
        const uint8_t c = aString[j] & ~0x20;
        hasLetter = (c - 'A' <= 'Z' - 'A');
      }
    } else {
      for (uint32_t j = 0; !hasLetter && j < aLength; j++) {
        const char16_t c = textPtr[j];
        hasLetter = gfxScriptItemizer::FastGetScriptCode(c) == Script::LATIN;
      }
    }
    script = hasLetter ? Script::LATIN
                       : ResolveScriptForLang(mLanguage, Script::COMMON);
  }

  // Either we have a valid textPtr, or we must have resolved script by now.
  MOZ_DIAGNOSTIC_ASSERT(textPtr || script != Script::INVALID);

  // variant fallback handling may end up passing through this twice
  bool redo;
  do {
    redo = false;

    // If we resolved script already (for simple Latin-only runs), we don't
    // need to use gfxScriptItemizer here.
    if (script != Script::INVALID) {
      if (MOZ_UNLIKELY(MOZ_LOG_TEST(log, LogLevel::Warning))) {
        gfxScriptItemizer::Run run{0, aLength, script};
        InitTextRunLog(log,
                       sizeof(T) == sizeof(uint8_t)
                           ? reinterpret_cast<const uint8_t*>(aString)
                           : nullptr,
                       textPtr, run);
      }
      if (textPtr) {
        InitScriptRun(aDrawTarget, aTextRun, textPtr, 0, aLength, script, aMFR);
      } else {
        InitScriptRun(aDrawTarget, aTextRun, aString, 0, aLength, script, aMFR);
      }
    } else {
      gfxScriptItemizer scriptRuns(textPtr, aLength);
      MOZ_DIAGNOSTIC_ASSERT(!scriptRuns.Done(), "scriptRuns cannot be empty");

      do {
        gfxScriptItemizer::Run run = scriptRuns.Next();
        if (MOZ_UNLIKELY(MOZ_LOG_TEST(log, LogLevel::Warning))) {
          InitTextRunLog(log,
                         sizeof(T) == sizeof(uint8_t)
                             ? reinterpret_cast<const uint8_t*>(aString)
                             : nullptr,
                         textPtr, run);
        }

        // If COMMON or INHERITED was not resolved, try to use the language
        // code to guess a likely script.
        if (run.mScript <= Script::INHERITED) {
          // This assumes Script codes begin with COMMON and INHERITED,
          // preceding codes for any "real" scripts.
          MOZ_ASSERT(
              run.mScript == Script::COMMON || run.mScript == Script::INHERITED,
              "unexpected Script code!");
          run.mScript = ResolveScriptForLang(mLanguage, run.mScript);
        }

        if (textPtr) {
          InitScriptRun(aDrawTarget, aTextRun, textPtr + run.mOffset,
                        run.mOffset, run.mLength, run.mScript, aMFR);
        } else {
          InitScriptRun(aDrawTarget, aTextRun, aString + run.mOffset,
                        run.mOffset, run.mLength, run.mScript, aMFR);
        }
      } while (!scriptRuns.Done());
    }

    // if shaping was aborted due to lack of feature support, clear out
    // glyph runs and redo shaping with fallback forced on
    if (aTextRun->GetShapingState() == gfxTextRun::eShapingState_Aborted) {
      redo = true;
      aTextRun->SetShapingState(gfxTextRun::eShapingState_ForceFallbackFeature);
      aTextRun->ClearGlyphsAndCharacters();
    }

  } while (redo);

  // Guard against text that starts with a combining mark (which cannot occur
  // in 8-bit text, even after numeral processing).
  if (sizeof(T) == sizeof(char16_t)) {
    gfxTextRun::CompressedGlyph* glyph = aTextRun->GetCharacterGlyphs();
    if (!glyph->IsSimpleGlyph()) {
      glyph->SetClusterStart(true);
    }
  }

  // It's possible for CoreText to omit glyph runs if it decides they contain
  // only invisibles (e.g., U+FEFF, see reftest 474417-1). In this case, we
  // need to eliminate them from the glyph run array to avoid drawing "partial
  // ligatures" with the wrong font.
  // We don't do this during InitScriptRun (or gfxFont::InitTextRun) because
  // it will iterate back over all glyphruns in the textrun, which leads to
  // pathologically-bad perf in the case where a textrun contains many script
  // changes (see bug 680402) - we'd end up re-sanitizing all the earlier runs
  // every time a new script subrun is processed.
  aTextRun->SanitizeGlyphRuns();
}

static inline bool IsPUA(uint32_t aUSV) {
  // We could look up the General Category of the codepoint here,
  // but it's simpler to check PUA codepoint ranges.
  return (aUSV - 0xE000 <= 0xF8FF - 0xE000) || (aUSV >= 0xF0000);
}

template <typename T>
void gfxFontGroup::InitScriptRun(DrawTarget* aDrawTarget, gfxTextRun* aTextRun,
                                 const T* aString,  // text for this script run,
                                                    // not the entire textrun
                                 uint32_t aOffset,  // position of the script
                                                    // run within the textrun
                                 uint32_t aLength,  // length of the script run
                                 Script aRunScript,
                                 gfxMissingFontRecorder* aMFR) {
  NS_ASSERTION(aLength > 0, "don't call InitScriptRun for a 0-length run");
  NS_ASSERTION(aTextRun->GetShapingState() != gfxTextRun::eShapingState_Aborted,
               "don't call InitScriptRun with aborted shaping state");

  // confirm the load state of userfonts in the list
  if (mUserFontSet && mCurrGeneration != mUserFontSet->GetGeneration()) {
    UpdateUserFonts();
  }

  RefPtr<gfxFont> mainFont = GetFirstValidFont();

  ShapedTextFlags orientation =
      aTextRun->GetFlags() & ShapedTextFlags::TEXT_ORIENT_MASK;

  if (orientation != ShapedTextFlags::TEXT_ORIENT_HORIZONTAL &&
      (aRunScript == Script::MONGOLIAN || aRunScript == Script::PHAGS_PA)) {
    // Mongolian and Phags-pa text should ignore text-orientation and
    // always render in its "native" vertical mode, implemented by fonts
    // as sideways-right (i.e as if shaped horizontally, and then the
    // entire line is rotated to render vertically). Therefore, we ignore
    // the aOrientation value from the textrun's flags, and make all
    // vertical Mongolian/Phags-pa use sideways-right.
    orientation = ShapedTextFlags::TEXT_ORIENT_VERTICAL_SIDEWAYS_RIGHT;
  }

  uint32_t runStart = 0;
  AutoTArray<TextRange, 3> fontRanges;
  ComputeRanges(fontRanges, aString, aLength, aRunScript, orientation);
  uint32_t numRanges = fontRanges.Length();
  bool missingChars = false;
  bool isCJK = gfxTextRun::IsCJKScript(aRunScript);

  for (uint32_t r = 0; r < numRanges; r++) {
    const TextRange& range = fontRanges[r];
    uint32_t matchedLength = range.Length();
    RefPtr<gfxFont> matchedFont = range.font;
    // create the glyph run for this range
    if (matchedFont && mStyle.noFallbackVariantFeatures) {
      // common case - just do glyph layout and record the
      // resulting positioned glyphs
      aTextRun->AddGlyphRun(matchedFont, range.matchType, aOffset + runStart,
                            (matchedLength > 0), range.orientation, isCJK);
      if (!matchedFont->SplitAndInitTextRun(
              aDrawTarget, aTextRun, aString + runStart, aOffset + runStart,
              matchedLength, aRunScript, mLanguage, range.orientation)) {
        // glyph layout failed! treat as missing glyphs
        matchedFont = nullptr;
      }
    } else if (matchedFont) {
      // shape with some variant feature that requires fallback handling
      bool petiteToSmallCaps = false;
      bool syntheticLower = false;
      bool syntheticUpper = false;

      if (mStyle.variantSubSuper != StyleFontVariantPosition::Normal &&
          mStyle.useSyntheticPosition &&
          (aTextRun->GetShapingState() ==
               gfxTextRun::eShapingState_ForceFallbackFeature ||
           !matchedFont->SupportsSubSuperscript(mStyle.variantSubSuper, aString,
                                                aLength, aRunScript))) {
        // fallback for subscript/superscript variant glyphs

        // if the feature was already used, abort and force
        // fallback across the entire textrun
        gfxTextRun::ShapingState ss = aTextRun->GetShapingState();

        if (ss == gfxTextRun::eShapingState_Normal) {
          aTextRun->SetShapingState(
              gfxTextRun::eShapingState_ShapingWithFallback);
        } else if (ss == gfxTextRun::eShapingState_ShapingWithFeature) {
          aTextRun->SetShapingState(gfxTextRun::eShapingState_Aborted);
          return;
        }

        RefPtr<gfxFont> subSuperFont = matchedFont->GetSubSuperscriptFont(
            aTextRun->GetAppUnitsPerDevUnit());
        aTextRun->AddGlyphRun(subSuperFont, range.matchType, aOffset + runStart,
                              (matchedLength > 0), range.orientation, isCJK);
        if (!subSuperFont->SplitAndInitTextRun(
                aDrawTarget, aTextRun, aString + runStart, aOffset + runStart,
                matchedLength, aRunScript, mLanguage, range.orientation)) {
          // glyph layout failed! treat as missing glyphs
          matchedFont = nullptr;
        }
      } else if (mStyle.variantCaps != StyleFontVariantCaps::Normal &&
                 mStyle.allowSyntheticSmallCaps &&
                 !matchedFont->SupportsVariantCaps(
                     aRunScript, mStyle.variantCaps, petiteToSmallCaps,
                     syntheticLower, syntheticUpper)) {
        // fallback for small-caps variant glyphs
        if (!matchedFont->InitFakeSmallCapsRun(
                mFontVisibilityProvider, aDrawTarget, aTextRun,
                aString + runStart, aOffset + runStart, matchedLength,
                range.matchType, range.orientation, aRunScript,
                mExplicitLanguage ? mLanguage.get() : nullptr, syntheticLower,
                syntheticUpper)) {
          matchedFont = nullptr;
        }
      } else {
        // shape normally with variant feature enabled
        gfxTextRun::ShapingState ss = aTextRun->GetShapingState();

        // adjust the shaping state if necessary
        if (ss == gfxTextRun::eShapingState_Normal) {
          aTextRun->SetShapingState(
              gfxTextRun::eShapingState_ShapingWithFeature);
        } else if (ss == gfxTextRun::eShapingState_ShapingWithFallback) {
          // already have shaping results using fallback, need to redo
          aTextRun->SetShapingState(gfxTextRun::eShapingState_Aborted);
          return;
        }

        // do glyph layout and record the resulting positioned glyphs
        aTextRun->AddGlyphRun(matchedFont, range.matchType, aOffset + runStart,
                              (matchedLength > 0), range.orientation, isCJK);
        if (!matchedFont->SplitAndInitTextRun(
                aDrawTarget, aTextRun, aString + runStart, aOffset + runStart,
                matchedLength, aRunScript, mLanguage, range.orientation)) {
          // glyph layout failed! treat as missing glyphs
          matchedFont = nullptr;
        }
      }
    } else {
      aTextRun->AddGlyphRun(mainFont, FontMatchType::Kind::kFontGroup,
                            aOffset + runStart, (matchedLength > 0),
                            range.orientation, isCJK);
    }

    if (!matchedFont) {
      // We need to set cluster boundaries (and mark spaces) so that
      // surrogate pairs, combining characters, etc behave properly,
      // even if we don't have glyphs for them
      aTextRun->SetupClusterBoundaries(aOffset + runStart, aString + runStart,
                                       matchedLength);

      // various "missing" characters may need special handling,
      // so we check for them here
      uint32_t runLimit = runStart + matchedLength;
      for (uint32_t index = runStart; index < runLimit; index++) {
        T ch = aString[index];

        // tab and newline are not to be displayed as hexboxes,
        // but do need to be recorded in the textrun
        if (ch == '\n') {
          aTextRun->SetIsNewline(aOffset + index);
          continue;
        }
        if (ch == '\t') {
          aTextRun->SetIsTab(aOffset + index);
          continue;
        }

        // for 16-bit textruns only, check for surrogate pairs and
        // special Unicode spaces; omit these checks in 8-bit runs
        if constexpr (sizeof(T) == sizeof(char16_t)) {
          if (index + 1 < aLength &&
              mozilla::IsSurrogatePair(ch, aString[index + 1])) {
            uint32_t usv = mozilla::SurrogateToUCS4(ch, aString[index + 1]);
            aTextRun->SetMissingGlyph(aOffset + index, usv, mainFont);
            index++;
            if (!mSkipDrawing && !IsPUA(usv)) {
              missingChars = true;
            }
            continue;
          }

          // check if this is a known Unicode whitespace character that
          // we can render using the space glyph with a custom width
          gfxFloat wid = mainFont->SynthesizeSpaceWidth(ch);
          if (wid >= 0.0) {
            nscoord advance =
                aTextRun->GetAppUnitsPerDevUnit() * floor(wid + 0.5);
            if (gfxShapedText::CompressedGlyph::IsSimpleAdvance(advance)) {
              aTextRun->GetCharacterGlyphs()[aOffset + index].SetSimpleGlyph(
                  advance, mainFont->GetSpaceGlyph());
            } else {
              gfxTextRun::DetailedGlyph detailedGlyph;
              detailedGlyph.mGlyphID = mainFont->GetSpaceGlyph();
              detailedGlyph.mAdvance = advance;
              aTextRun->SetDetailedGlyphs(aOffset + index, 1, &detailedGlyph);
            }
            continue;
          }
        }

        if (IsInvalidChar(ch)) {
          // invalid chars are left as zero-width/invisible
          continue;
        }

        // record char code so we can draw a box with the Unicode value
        aTextRun->SetMissingGlyph(aOffset + index, ch, mainFont);
        if (!mSkipDrawing && !IsPUA(ch)) {
          missingChars = true;
        }
      }
    }

    runStart += matchedLength;
  }

  if (aMFR && missingChars) {
    aMFR->RecordScript(aRunScript);
  }
}

already_AddRefed<gfxTextRun> gfxFontGroup::MakeEllipsisTextRun(
    int32_t aAppUnitsPerDevPixel, gfx::ShapedTextFlags aFlags,
    DrawTarget* aRefDrawTarget) {
  MOZ_ASSERT(!(aFlags & ~ShapedTextFlags::TEXT_ORIENT_MASK),
             "flags here should only be used to specify orientation");
  // Use a Unicode ellipsis if the font supports it,
  // otherwise use three ASCII periods as fallback.
  RefPtr<gfxFont> firstFont = GetFirstValidFont();
  nsString ellipsis =
      firstFont->HasCharacter(kEllipsisChar[0])
          ? nsDependentString(kEllipsisChar, std::size(kEllipsisChar) - 1)
          : nsDependentString(kASCIIPeriodsChar,
                              std::size(kASCIIPeriodsChar) - 1);

  Parameters params = {aRefDrawTarget, nullptr, nullptr,
                       nullptr,        0,       aAppUnitsPerDevPixel};
  return MakeTextRun(ellipsis.BeginReading(), ellipsis.Length(), &params,
                     aFlags, nsTextFrameUtils::Flags(), nullptr);
}

already_AddRefed<gfxFont> gfxFontGroup::FindFallbackFaceForChar(
    gfxFontFamily* aFamily, uint32_t aCh, uint32_t aNextCh,
    FontPresentation aPresentation) {
  GlobalFontMatch data(aCh, aNextCh, mStyle, aPresentation);
  aFamily->SearchAllFontsForChar(&data);
  gfxFontEntry* fe = data.mBestMatch;
  if (!fe) {
    return nullptr;
  }
  return fe->FindOrMakeFont(&mStyle);
}

already_AddRefed<gfxFont> gfxFontGroup::FindFallbackFaceForChar(
    fontlist::Family* aFamily, uint32_t aCh, uint32_t aNextCh,
    FontPresentation aPresentation) {
  auto* pfl = gfxPlatformFontList::PlatformFontList();
  auto* list = pfl->SharedFontList();

  // If async fallback is enabled, and the family isn't fully initialized yet,
  // just start the async cmap loading and return.
  if (!aFamily->IsFullyInitialized() &&
      StaticPrefs::gfx_font_rendering_fallback_async() &&
      !XRE_IsParentProcess()) {
    pfl->StartCmapLoadingFromFamily(aFamily - list->Families());
    return nullptr;
  }

  GlobalFontMatch data(aCh, aNextCh, mStyle, aPresentation);
  aFamily->SearchAllFontsForChar(list, &data);
  gfxFontEntry* fe = data.mBestMatch;
  if (!fe) {
    return nullptr;
  }
  return fe->FindOrMakeFont(&mStyle);
}

already_AddRefed<gfxFont> gfxFontGroup::FindFallbackFaceForChar(
    const FamilyFace& aFamily, uint32_t aCh, uint32_t aNextCh,
    FontPresentation aPresentation) {
  if (aFamily.IsSharedFamily()) {
    return FindFallbackFaceForChar(aFamily.SharedFamily(), aCh, aNextCh,
                                   aPresentation);
  }
  return FindFallbackFaceForChar(aFamily.OwnedFamily(), aCh, aNextCh,
                                 aPresentation);
}

gfxFloat gfxFontGroup::GetUnderlineOffset() {
  if (mUnderlineOffset == UNDERLINE_OFFSET_NOT_SET) {
    // if the fontlist contains a bad underline font, make the underline
    // offset the min of the first valid font and bad font underline offsets
    uint32_t len = mFonts.Length();
    for (uint32_t i = 0; i < len; i++) {
      FamilyFace& ff = mFonts[i];
      gfxFontEntry* fe = ff.FontEntry();
      if (!fe) {
        continue;
      }
      if (!fe->mIsUserFontContainer && !fe->IsUserFont() &&
          ((ff.IsSharedFamily() && ff.SharedFamily() &&
            ff.SharedFamily()->IsBadUnderlineFamily()) ||
           (!ff.IsSharedFamily() && ff.OwnedFamily() &&
            ff.OwnedFamily()->IsBadUnderlineFamily()))) {
        RefPtr<gfxFont> font = GetFontAt(i);
        if (!font) {
          continue;
        }
        gfxFloat bad =
            font->GetMetrics(nsFontMetrics::eHorizontal).underlineOffset;
        RefPtr<gfxFont> firstValidFont = GetFirstValidFont();
        gfxFloat first = firstValidFont->GetMetrics(nsFontMetrics::eHorizontal)
                             .underlineOffset;
        mUnderlineOffset = std::min(first, bad);
        return mUnderlineOffset;
      }
    }

    // no bad underline fonts, use the first valid font's metric
    RefPtr<gfxFont> firstValidFont = GetFirstValidFont();
    mUnderlineOffset =
        firstValidFont->GetMetrics(nsFontMetrics::eHorizontal).underlineOffset;
  }

  return mUnderlineOffset;
}

#define NARROW_NO_BREAK_SPACE 0x202fu

already_AddRefed<gfxFont> gfxFontGroup::FindFontForChar(
    uint32_t aCh, uint32_t aPrevCh, uint32_t aNextCh, Script aRunScript,
    gfxFont* aPrevMatchedFont, FontMatchType* aMatchType) {
  // If the char is a cluster extender, we want to use the same font as the
  // preceding character if possible. This is preferable to using the font
  // group because it avoids breaks in shaping within a cluster.
  if (aPrevMatchedFont && IsClusterExtender(aCh)) {
    if (aPrevMatchedFont->HasCharacter(aCh) || IsDefaultIgnorable(aCh)) {
      return do_AddRef(aPrevMatchedFont);
    }
    // Check if this char and preceding char can compose; if so, is the
    // combination supported by the current font.
    uint32_t composed = intl::String::ComposePairNFC(aPrevCh, aCh);
    if (composed > 0 && aPrevMatchedFont->HasCharacter(composed)) {
      return do_AddRef(aPrevMatchedFont);
    }
  }

  // Special cases for NNBSP (as used in Mongolian):
  if (aCh == NARROW_NO_BREAK_SPACE) {
    // If there is no preceding character, try the font that we'd use
    // for the next char (unless it's just another NNBSP; we don't try
    // to look ahead through a whole run of them).
    if (!aPrevCh && aNextCh && aNextCh != NARROW_NO_BREAK_SPACE) {
      RefPtr<gfxFont> nextFont = FindFontForChar(aNextCh, 0, 0, aRunScript,
                                                 aPrevMatchedFont, aMatchType);
      if (nextFont && nextFont->HasCharacter(aCh)) {
        return nextFont.forget();
      }
    }
    // Otherwise, treat NNBSP like a cluster extender (as above) and try
    // to continue the preceding font run.
    if (aPrevMatchedFont && aPrevMatchedFont->HasCharacter(aCh)) {
      return do_AddRef(aPrevMatchedFont);
    }
  }

  // To optimize common cases, try the first font in the font-group
  // before going into the more detailed checks below
  uint32_t fontListLength = mFonts.Length();
  uint32_t nextIndex = 0;
  bool isJoinControl = gfxFontUtils::IsJoinControl(aCh);
  bool wasJoinCauser = gfxFontUtils::IsJoinCauser(aPrevCh);
  bool isVarSelector = gfxFontUtils::IsVarSelector(aCh);
  bool nextIsVarSelector = gfxFontUtils::IsVarSelector(aNextCh);

  // For Unicode hyphens, if not supported in the font then we'll try for
  // the ASCII hyphen-minus as a fallback.
  // Similarly, for NBSP we try normal <space> as a fallback.
  uint32_t fallbackChar = (aCh == 0x2010 || aCh == 0x2011) ? '-'
                          : (aCh == 0x00A0)                ? ' '
                                                           : 0;

  // Whether we've seen a font that is currently loading a resource that may
  // provide this character (so we should not start a new load).
  bool loading = false;

  // Do we need to explicitly look for a font that does or does not provide a
  // color glyph for the given character?
  // For characters with no `EMOJI` property, we'll use whatever the family
  // list calls for; but if it's a potential emoji codepoint, we need to check
  // if there's a variation selector specifically asking for Text-style or
  // Emoji-style rendering and look for a suitable font.
  FontPresentation presentation = FontPresentation::Any;
  if (EmojiPresentation emojiPresentation = GetEmojiPresentation(aCh);
      emojiPresentation != TextOnly) {
    // Default presentation from the font-variant-emoji property.
    if (mFontVariantEmoji == StyleFontVariantEmoji::Emoji) {
      presentation = FontPresentation::EmojiExplicit;
    } else if (mFontVariantEmoji == StyleFontVariantEmoji::Text) {
      presentation = FontPresentation::TextExplicit;
    }
    // If there wasn't an explicit font-variant-emoji setting, default to
    // what Unicode prefers for this character.
    if (presentation == FontPresentation::Any) {
      if (emojiPresentation == EmojiPresentation::TextDefault) {
        presentation = FontPresentation::TextDefault;
      } else {
        presentation = FontPresentation::EmojiDefault;
      }
    }
    // If the prefer-emoji selector is present, or if it's a default-emoji
    // char and the prefer-text selector is NOT present, or if there's a
    // skin-tone modifier, we specifically look for a font with a color
    // glyph.
    // If the prefer-text selector is present, we specifically look for a
    // font that will provide a monochrome glyph.
    if (aNextCh == kVariationSelector16 || IsEmojiSkinToneModifier(aNextCh) ||
        gfxFontUtils::IsEmojiFlagAndTag(aCh, aNextCh)) {
      // Emoji presentation is explicitly requested by a variation selector
      // or the presence of a skin-tone codepoint.
      presentation = FontPresentation::EmojiExplicit;
    } else if (aNextCh == kVariationSelector15) {
      // Text presentation is explicitly requested.
      presentation = FontPresentation::TextExplicit;
    }
  }

  if (!isJoinControl && !wasJoinCauser && !isVarSelector &&
      !nextIsVarSelector && presentation == FontPresentation::Any) {
    RefPtr<gfxFont> firstFont = GetFontAt(0, aCh, &loading);
    if (firstFont) {
      if (firstFont->HasCharacter(aCh) ||
          (fallbackChar && firstFont->HasCharacter(fallbackChar))) {
        *aMatchType = {FontMatchType::Kind::kFontGroup, mFonts[0].Generic()};
        return firstFont.forget();
      }

      RefPtr<gfxFont> font;
      if (mFonts[0].CheckForFallbackFaces()) {
        font = FindFallbackFaceForChar(mFonts[0], aCh, aNextCh, presentation);
      } else if (!firstFont->GetFontEntry()->IsUserFont()) {
        // For platform fonts (but not userfonts), we may need to do
        // fallback within the family to handle cases where some faces
        // such as Italic or Black have reduced character sets compared
        // to the family's Regular face.
        font = FindFallbackFaceForChar(mFonts[0], aCh, aNextCh, presentation);
      }
      if (font) {
        *aMatchType = {FontMatchType::Kind::kFontGroup, mFonts[0].Generic()};
        return font.forget();
      }
    } else {
      if (fontListLength > 0) {
        loading = loading || mFonts[0].IsLoadingFor(aCh);
      }
    }

    // we don't need to check the first font again below
    ++nextIndex;
  }

  if (aPrevMatchedFont) {
    // Don't switch fonts for control characters, regardless of
    // whether they are present in the current font, as they won't
    // actually be rendered (see bug 716229)
    if (isJoinControl ||
        GetGeneralCategory(aCh) == HB_UNICODE_GENERAL_CATEGORY_CONTROL) {
      return do_AddRef(aPrevMatchedFont);
    }

    // if previous character was a join-causer (ZWJ),
    // use the same font as the previous range if we can
    if (wasJoinCauser) {
      if (aPrevMatchedFont->HasCharacter(aCh)) {
        return do_AddRef(aPrevMatchedFont);
      }
    }
  }

  // If this character is a variation selector or default-ignorable, use the
  // previous font regardless of whether it supports the codepoint or not.
  // (We don't want to unnecessarily split glyph runs, and the character will
  // not be visibly rendered.)
  if (isVarSelector || IsDefaultIgnorable(aCh)) {
    return do_AddRef(aPrevMatchedFont);
  }

  // Used to remember the first "candidate" font that would provide a fallback
  // text-style rendering if no color glyph can be found.
  RefPtr<gfxFont> candidateFont;
  FontMatchType candidateMatchType;

  // Handle a candidate font that could support the character, returning true
  // if we should go ahead and return |f|, false to continue searching.
  auto CheckCandidate = [&](gfxFont* f, FontMatchType t) -> bool {
    // If a given character is a Private Use Area Unicode codepoint, user
    // agents must only match font families named in the font-family list that
    // are not generic families.
    if (t.generic != StyleGenericFontFamily::None && IsPUA(aCh)) {
      return false;
    }
    // If no preference, or if it's an explicitly-named family in the fontgroup
    // and font-variant-emoji is 'normal', then we accept the font.
    if (presentation == FontPresentation::Any ||
        (!IsExplicitPresentation(presentation) &&
         t.kind == FontMatchType::Kind::kFontGroup &&
         t.generic == StyleGenericFontFamily::None &&
         mFontVariantEmoji == StyleFontVariantEmoji::Normal &&
         !gfxFontUtils::IsRegionalIndicator(aCh))) {
      *aMatchType = t;
      return true;
    }
    // Does the candidate font provide a color glyph for the current character?
    bool hasColorGlyph =
        f->HasColorGlyphFor(aCh, aNextCh) ||
        (!nextIsVarSelector && f->HasColorGlyphFor(aCh, kVariationSelector16));
    // If the provided glyph matches the preference, accept the font.
    if (hasColorGlyph == PrefersColor(presentation) &&
        // Exception: if this is an emoji flag+tag letters sequence, and the
        // following codepoint (the first tag) is missing from the font, we
        // don't want to use this font as it will fail to present the cluster.
        (!hasColorGlyph || !gfxFontUtils::IsEmojiFlagAndTag(aCh, aNextCh) ||
         f->HasCharacter(aNextCh))) {
      *aMatchType = t;
      return true;
    }
    // If the character was a TextDefault char, but the next char is VS16,
    // and the font is a COLR font that supports both these codepoints, then
    // we'll assume it knows what it is doing (eg Twemoji Mozilla keycap
    // sequences).
    // TODO: reconsider all this as part of any fix for bug 543200.
    if (aNextCh == kVariationSelector16 &&
        GetEmojiPresentation(aCh) == EmojiPresentation::TextDefault &&
        f->HasCharacter(aNextCh) && f->GetFontEntry()->TryGetColorGlyphs()) {
      return true;
    }
    // Otherwise, remember the first potential fallback, but keep searching.
    if (!candidateFont) {
      candidateFont = f;
      candidateMatchType = t;
    }
    return false;
  };

  // 1. check remaining fonts in the font group
  for (uint32_t i = nextIndex; i < fontListLength; i++) {
    FamilyFace& ff = mFonts[i];
    if (ff.IsInvalid() || ff.IsLoading()) {
      if (ff.IsLoadingFor(aCh)) {
        loading = true;
      }
      continue;
    }

    RefPtr<gfxFont> font = ff.Font();
    if (font) {
      // if available, use already-made gfxFont and check for character
      if (font->HasCharacter(aCh) ||
          (fallbackChar && font->HasCharacter(fallbackChar))) {
        if (CheckCandidate(font,
                           {FontMatchType::Kind::kFontGroup, ff.Generic()})) {
          return font.forget();
        }
      }
    } else {
      // don't have a gfxFont yet, test charmap before instantiating
      gfxFontEntry* fe = ff.FontEntry();
      if (fe && fe->mIsUserFontContainer) {
        // for userfonts, need to test both the unicode range map and
        // the cmap of the platform font entry
        gfxUserFontEntry* ufe = static_cast<gfxUserFontEntry*>(fe);

        // never match a character outside the defined unicode range
        if (!ufe->CharacterInUnicodeRange(aCh)) {
          continue;
        }

        // Load if not already loaded, unless we've already seen an in-
        // progress load that is expected to satisfy this request.
        if (!loading &&
            ufe->LoadState() == gfxUserFontEntry::STATUS_NOT_LOADED) {
          ufe->Load();
          ff.CheckState(mSkipDrawing);
        }

        if (ff.IsLoading()) {
          loading = true;
        }

        RefPtr<gfxFontEntry> pfe = ufe->GetPlatformFontEntry();
        if (pfe && (pfe->HasCharacter(aCh) ||
                    (fallbackChar && pfe->HasCharacter(fallbackChar)))) {
          font = GetFontAt(i, aCh, &loading);
          if (font) {
            if (CheckCandidate(font, {FontMatchType::Kind::kFontGroup,
                                      mFonts[i].Generic()})) {
              return font.forget();
            }
          }
        }
      } else if (fe && (fe->HasCharacter(aCh) ||
                        (fallbackChar && fe->HasCharacter(fallbackChar)))) {
        // for normal platform fonts, after checking the cmap
        // build the font via GetFontAt
        font = GetFontAt(i, aCh, &loading);
        if (font) {
          if (CheckCandidate(font, {FontMatchType::Kind::kFontGroup,
                                    mFonts[i].Generic()})) {
            return font.forget();
          }
        }
      }
    }

    // check other family faces if needed
    if (ff.CheckForFallbackFaces()) {
#ifdef DEBUG
      if (i > 0) {
        fontlist::FontList* list =
            gfxPlatformFontList::PlatformFontList()->SharedFontList();
        nsCString s1 = mFonts[i - 1].IsSharedFamily()
                           ? mFonts[i - 1].SharedFamily()->Key().AsString(list)
                           : mFonts[i - 1].OwnedFamily()->Name();
        nsCString s2 = ff.IsSharedFamily()
                           ? ff.SharedFamily()->Key().AsString(list)
                           : ff.OwnedFamily()->Name();
        MOZ_ASSERT(!mFonts[i - 1].CheckForFallbackFaces() || !s1.Equals(s2),
                   "should only do fallback once per font family");
      }
#endif
      font = FindFallbackFaceForChar(ff, aCh, aNextCh, presentation);
      if (font) {
        if (CheckCandidate(font,
                           {FontMatchType::Kind::kFontGroup, ff.Generic()})) {
          return font.forget();
        }
      }
    } else {
      // For platform fonts, but not user fonts, consider intra-family
      // fallback to handle styles with reduced character sets (see
      // also above).
      gfxFontEntry* fe = ff.FontEntry();
      if (fe && !fe->mIsUserFontContainer && !fe->IsUserFont()) {
        font = FindFallbackFaceForChar(ff, aCh, aNextCh, presentation);
        if (font) {
          if (CheckCandidate(font,
                             {FontMatchType::Kind::kFontGroup, ff.Generic()})) {
            return font.forget();
          }
        }
      }
    }
  }

  if (fontListLength == 0) {
    RefPtr<gfxFont> defaultFont = GetDefaultFont();
    if (defaultFont->HasCharacter(aCh) ||
        (fallbackChar && defaultFont->HasCharacter(fallbackChar))) {
      if (CheckCandidate(defaultFont, FontMatchType::Kind::kFontGroup)) {
        return defaultFont.forget();
      }
    }
  }

  // If character is in Private Use Area, or is unassigned in Unicode, don't do
  // matching against pref or system fonts. We only support such codepoints
  // when used with an explicitly-specified font, as they have no standard/
  // interoperable meaning.
  // Also don't attempt any fallback for control characters or noncharacters,
  // where we won't be rendering a glyph anyhow, or for codepoints where global
  // fallback has already noted a failure.
  FontVisibility level = mFontVisibilityProvider
                             ? mFontVisibilityProvider->GetFontVisibility()
                             : FontVisibility::User;
  auto* pfl = gfxPlatformFontList::PlatformFontList();
  if (pfl->SkipFontFallbackForChar(level, aCh) ||
      (!StaticPrefs::gfx_font_rendering_fallback_unassigned_chars() &&
       GetGeneralCategory(aCh) == HB_UNICODE_GENERAL_CATEGORY_UNASSIGNED)) {
    if (candidateFont) {
      *aMatchType = candidateMatchType;
    }
    return candidateFont.forget();
  }

  // 2. search pref fonts
  RefPtr<gfxFont> font = WhichPrefFontSupportsChar(aCh, aNextCh, presentation);
  if (font) {
    if (PrefersColor(presentation) && pfl->EmojiPrefHasUserValue()) {
      // For emoji, always accept the font from preferences if it's explicitly
      // user-set, even if it isn't actually a color-emoji font, as some users
      // may want to set their emoji font preference to a monochrome font like
      // Symbola.
      // So a user-provided font.name-list.emoji preference takes precedence
      // over the Unicode presentation style here.
      RefPtr<gfxFont> autoRefDeref(candidateFont);
      *aMatchType = FontMatchType::Kind::kPrefsFallback;
      return font.forget();
    }
    if (CheckCandidate(font, FontMatchType::Kind::kPrefsFallback)) {
      return font.forget();
    }
  }

  // For fallback searches, we don't want to use a color-emoji font unless
  // emoji-style presentation is specifically required, so we map Any to
  // Text here.
  if (presentation == FontPresentation::Any) {
    presentation = FontPresentation::TextDefault;
  }

  // 3. use fallback fonts
  // -- before searching for something else check the font used for the
  //    previous character
  if (aPrevMatchedFont &&
      (aPrevMatchedFont->HasCharacter(aCh) ||
       (fallbackChar && aPrevMatchedFont->HasCharacter(fallbackChar)))) {
    if (CheckCandidate(aPrevMatchedFont,
                       FontMatchType::Kind::kSystemFallback)) {
      return do_AddRef(aPrevMatchedFont);
    }
  }

  // for known "space" characters, don't do a full system-fallback search;
  // we'll synthesize appropriate-width spaces instead of missing-glyph boxes
  font = GetFirstValidFont();
  if (GetGeneralCategory(aCh) == HB_UNICODE_GENERAL_CATEGORY_SPACE_SEPARATOR &&
      font->SynthesizeSpaceWidth(aCh) >= 0.0) {
    return nullptr;
  }

  // -- otherwise look for other stuff
  font = WhichSystemFontSupportsChar(aCh, aNextCh, aRunScript, presentation);
  if (font) {
    if (CheckCandidate(font, FontMatchType::Kind::kSystemFallback)) {
      return font.forget();
    }
  }
  if (candidateFont) {
    *aMatchType = candidateMatchType;
  }
  return candidateFont.forget();
}

template <typename T>
void gfxFontGroup::ComputeRanges(nsTArray<TextRange>& aRanges, const T* aString,
                                 uint32_t aLength, Script aRunScript,
                                 gfx::ShapedTextFlags aOrientation) {
  MOZ_ASSERT(aRanges.IsEmpty(), "aRanges must be initially empty");
  MOZ_ASSERT(aLength > 0, "don't call ComputeRanges for zero-length text");

  const uint32_t maxIndex = aLength - 1;  // max valid index into aString

  uint32_t prevCh = 0;
  uint32_t nextCh = aString[0];
  if constexpr (sizeof(T) == sizeof(char16_t)) {
    if (aLength > 1 && mozilla::IsSurrogatePair(nextCh, aString[1])) {
      nextCh = mozilla::SurrogateToUCS4(nextCh, aString[1]);
    }
  }

  // initialize prevFont to the group's primary font, so that this will be
  // used for string-initial control chars, etc rather than risk hitting font
  // fallback for these (bug 716229)
  StyleGenericFontFamily generic = StyleGenericFontFamily::None;
  RefPtr<gfxFont> prevFont = GetFirstValidFont(' ', &generic);

  // The absolute first font in the list (which might not be the "first valid
  // font" if it doesn't support <space>!) is the one we will consider for
  // fast-path handling.
  RefPtr<gfxFont> firstFont = GetFontAt(0);

  // if we use the initial value of prevFont, we treat this as a match from
  // the font group; fixes bug 978313
  FontMatchType matchType = {FontMatchType::Kind::kFontGroup, generic};
  TextRange* currRange = nullptr;

  for (uint32_t i = 0; i < aLength; i++) {
    // At the start of this loop, /i/ points to the beginning of a character,
    // which might be a surrogate pair in the 16-bit case.

    const uint32_t origI = i;  // save off in case we increase for surrogate

    // set up current ch
    uint32_t ch = nextCh;

    // Get next char (if any) so that FindFontForChar can look ahead
    // for a possible variation selector.
    if constexpr (sizeof(T) == sizeof(char16_t)) {
      // In 16-bit case only, check for surrogate pairs.
      if (ch > 0xffffu) {
        i++;  // increment /i/ to point to the trailing (low) surrogate of the
              // current character.
      }
      // Get the next character, if any, decoding any surrogate pair.
      if (i < maxIndex) {
        nextCh = aString[i + 1];
        if (i + 2 <= maxIndex &&
            mozilla::IsSurrogatePair(nextCh, aString[i + 2])) {
          nextCh = mozilla::SurrogateToUCS4(nextCh, aString[i + 2]);
        }
      } else {
        nextCh = 0;
      }
    } else {
      // 8-bit case is trivial.
      nextCh = i < maxIndex ? aString[i + 1] : 0;
    }

    RefPtr<gfxFont> font;

    // Find the font for this char; but try to avoid calling the expensive
    // FindFontForChar method for the most common case, where the first
    // font in the list supports the current char, and it is not one of
    // the special cases where FindFontForChar will attempt to propagate
    // the font selected for an adjacent character, and does not need to
    // consider emoji vs text presentation.
    if ((font = GetFontAt(0, ch)) != nullptr && font->HasCharacter(ch) &&
        (
            // In 8-bit text, we can unconditionally accept the first font if
            // font-variant-emoji is 'normal', or if the character does not
            // have the emoji property; there cannot be adjacent characters
            // that would affect it.
            (sizeof(T) == sizeof(uint8_t) &&
             (mFontVariantEmoji == StyleFontVariantEmoji::Normal ||
              GetEmojiPresentation(uint8_t(ch)) == TextOnly)) ||
            // For 16-bit text, we need to consider cluster extenders etc.
            (sizeof(T) == sizeof(char16_t) &&
             (!IsClusterExtender(ch) && ch != NARROW_NO_BREAK_SPACE &&
              !gfxFontUtils::IsJoinControl(ch) &&
              !gfxFontUtils::IsJoinCauser(prevCh) &&
              !gfxFontUtils::IsVarSelector(ch) &&
              (GetEmojiPresentation(ch) == TextOnly ||
               (!(IsEmojiPresentationSelector(nextCh) ||
                  IsEmojiSkinToneModifier(nextCh) ||
                  gfxFontUtils::IsEmojiFlagAndTag(ch, nextCh)) &&
                mFontVariantEmoji == StyleFontVariantEmoji::Normal &&
                mFonts[0].Generic() == StyleGenericFontFamily::None)))))) {
      matchType = {FontMatchType::Kind::kFontGroup, mFonts[0].Generic()};
    } else {
      font =
          FindFontForChar(ch, prevCh, nextCh, aRunScript, prevFont, &matchType);
    }

#ifndef RELEASE_OR_BETA
    if (MOZ_UNLIKELY(mTextPerf)) {
      if (matchType.kind == FontMatchType::Kind::kPrefsFallback) {
        mTextPerf->current.fallbackPrefs++;
      } else if (matchType.kind == FontMatchType::Kind::kSystemFallback) {
        mTextPerf->current.fallbackSystem++;
      }
    }
#endif

    // Fast path for a run of non-join-control, non-variation-selector chars
    // that are all supported by the first font.
    // On entry here, /i/ indicates the trailing code unit of the current char.
    if (font && font == firstFont && font->HasCharacter(ch) &&
        mFontVariantEmoji == StyleFontVariantEmoji::Normal &&
        (sizeof(T) == sizeof(uint8_t) || !(gfxFontUtils::IsJoinControl(ch) ||
                                           gfxFontUtils::IsVarSelector(ch))) &&
        GetEmojiPresentation(ch) == TextOnly &&
        aOrientation != ShapedTextFlags::TEXT_ORIENT_VERTICAL_MIXED) {
      uint32_t safeI = i;
      while (i < maxIndex) {
        // Look at the next character in the text.
        uint32_t c = aString[i + 1];
        uint32_t charLen = 1;
        if constexpr (sizeof(T) == sizeof(char16_t)) {
          // Decode surrogate pair.
          if (i + 2 <= maxIndex &&
              mozilla::IsSurrogatePair(c, aString[i + 2])) {
            c = mozilla::SurrogateToUCS4(c, aString[i + 2]);
            charLen = 2;
          }
          // If we've found a join control or variation selector, back up to
          // the last known /safeI/ position, because the previous character
          // needs to use the full FindFontForChar process.
          if (gfxFontUtils::IsJoinControl(c) ||
              gfxFontUtils::IsVarSelector(c)) {
            i = safeI;
            break;
          }
        }
        // Bail out if the first font doesn't support this codepoint...
        if (!font->HasCharacter(c)) {
          break;
        }
        // ...or if it might need to consider emoji presentation controls.
        if constexpr (sizeof(T) == sizeof(char16_t)) {
          if (GetEmojiPresentation(c) != TextOnly) {
            break;
          }
          // We might still need to reject this character (if followed by VS
          // or join-control), but it is safe to include everything up to here.
          safeI = i;
          i += charLen;
        } else {
          // Only the `emoji` value of StyleFontVariantEmoji could affect font
          // selection for 8-bit text.
          if (mFontVariantEmoji == StyleFontVariantEmoji::Emoji &&
              GetEmojiPresentation(uint8_t(c)) != TextOnly) {
            break;
          }
          i++;
        }
      }

      // Record that all chars from /origI/ to /i/ (inclusive) use /font/.
      if (!currRange) {
        // first char ==> make a new range
        currRange = aRanges.AppendElement(
            TextRange(origI, i + 1, font, matchType, aOrientation));
        prevFont = std::move(font);
      } else {
        // If font has changed, make a new range. (Orientation cannot have
        // changed, since we don't apply the fast-path to VERTICAL_MIXED,
        // so no need to check for cluster-extender characters.)
        if (currRange->font != font) {
          // Close out the previous range and start a new one.
          currRange->end = origI;
          currRange = aRanges.AppendElement(
              TextRange(origI, i + 1, font, matchType, aOrientation));
          prevFont = std::move(font);
        } else {
          currRange->matchType |= matchType;
        }
      }

      if (i > origI) {
        // Update prevCh and nextCh for the end of the fast-path run.
        prevCh = aString[i];
        if constexpr (sizeof(T) == sizeof(char16_t)) {
          if (i > 0 && mozilla::IsSurrogatePair(aString[i - 1], prevCh)) {
            prevCh = mozilla::SurrogateToUCS4(aString[i - 1], prevCh);
          }
          if (i < maxIndex) {
            nextCh = aString[i + 1];
            if (i + 2 <= maxIndex &&
                mozilla::IsSurrogatePair(nextCh, aString[i + 2])) {
              nextCh = mozilla::SurrogateToUCS4(nextCh, aString[i + 2]);
            }
          } else {
            nextCh = 0;
          }
        } else {
          nextCh = i < maxIndex ? aString[i + 1] : 0;
        }
      } else {
        // We didn't find a run, just a single character.
        prevCh = ch;
      }

      // Return to the beginning of the main loop.
      continue;
    }

    prevCh = ch;

    ShapedTextFlags orient = aOrientation;
    if (aOrientation == ShapedTextFlags::TEXT_ORIENT_VERTICAL_MIXED) {
      // For CSS text-orientation:mixed, we need to resolve orientation
      // on a per-character basis using the UTR50 orientation property.
      switch (GetVerticalOrientation(ch)) {
        case VERTICAL_ORIENTATION_U:
        case VERTICAL_ORIENTATION_Tu:
          orient = ShapedTextFlags::TEXT_ORIENT_VERTICAL_UPRIGHT;
          break;
        case VERTICAL_ORIENTATION_Tr: {
          // We check for a vertical presentation form first as that's
          // likely to be cheaper than inspecting lookups to see if the
          // 'vert' feature is going to handle this character, and if the
          // presentation form is available then it will be used as
          // fallback if needed, so it's OK if the feature is missing.
          //
          // Because "common" CJK punctuation characters in isolation will be
          // resolved to Bopomofo script (as the first script listed in their
          // ScriptExtensions property), but this is not always well supported
          // by fonts' OpenType tables, we also try Han script; harfbuzz will
          // apply a 'vert' feature from any available script (see
          // https://github.com/harfbuzz/harfbuzz/issues/63) when shaping,
          // so this is OK. It's not quite as general as what harfbuzz does
          // (it will find the feature in *any* script), but should be enough
          // for likely real-world examples.
          uint32_t v = gfxHarfBuzzShaper::GetVerticalPresentationForm(ch);
          const uint32_t kVert = HB_TAG('v', 'e', 'r', 't');
          orient = (!font || (v && font->HasCharacter(v)) ||
                    font->FeatureWillHandleChar(aRunScript, kVert, ch) ||
                    (aRunScript == Script::BOPOMOFO &&
                     font->FeatureWillHandleChar(Script::HAN, kVert, ch)))
                       ? ShapedTextFlags::TEXT_ORIENT_VERTICAL_UPRIGHT
                       : ShapedTextFlags::TEXT_ORIENT_VERTICAL_SIDEWAYS_RIGHT;
          break;
        }
        case VERTICAL_ORIENTATION_R:
          orient = ShapedTextFlags::TEXT_ORIENT_VERTICAL_SIDEWAYS_RIGHT;
          break;
      }
    }

    if (!currRange) {
      // first char ==> make a new range
      currRange = aRanges.AppendElement(
          TextRange(origI, i + 1, font, matchType, orient));
      prevFont = std::move(font);
    } else {
      // if font or orientation has changed, make a new range...
      // unless ch is a variation selector (bug 1248248)
      if (currRange->font != font ||
          (currRange->orientation != orient && !IsClusterExtender(ch))) {
        // close out the previous range
        currRange->end = origI;
        currRange = aRanges.AppendElement(
            TextRange(origI, i + 1, font, matchType, orient));

        // update prevFont for the next match, *unless* we switched
        // fonts on a ZWJ, in which case propagating the changed font
        // is probably not a good idea (see bug 619511)
        if (sizeof(T) == sizeof(uint8_t) || !gfxFontUtils::IsJoinCauser(ch)) {
          prevFont = std::move(font);
        }
      } else {
        currRange->matchType |= matchType;
      }
    }
  }

  MOZ_ASSERT(currRange, "no range created?");
  currRange->end = aLength;

#ifndef RELEASE_OR_BETA
  LogModule* log = mStyle.systemFont ? gfxPlatform::GetLog(eGfxLog_textrunui)
                                     : gfxPlatform::GetLog(eGfxLog_textrun);

  if (MOZ_UNLIKELY(MOZ_LOG_TEST(log, LogLevel::Debug))) {
    nsAutoCString lang;
    mLanguage->ToUTF8String(lang);
    auto defaultLanguageGeneric = GetDefaultGeneric(mLanguage);

    // collect the font matched for each range
    nsAutoCString fontMatches;
    for (size_t i = 0, i_end = aRanges.Length(); i < i_end; i++) {
      const TextRange& r = aRanges[i];
      nsAutoCString matchTypes;
      if (r.matchType.kind & FontMatchType::Kind::kFontGroup) {
        matchTypes.AppendLiteral("list");
      }
      if (r.matchType.kind & FontMatchType::Kind::kPrefsFallback) {
        if (!matchTypes.IsEmpty()) {
          matchTypes.AppendLiteral(",");
        }
        matchTypes.AppendLiteral("prefs");
      }
      if (r.matchType.kind & FontMatchType::Kind::kSystemFallback) {
        if (!matchTypes.IsEmpty()) {
          matchTypes.AppendLiteral(",");
        }
        matchTypes.AppendLiteral("sys");
      }
      fontMatches.AppendPrintf(
          " [%u:%u] %.200s (%s)", r.start, r.end,
          (r.font.get() ? r.font->GetName().get() : "<null>"),
          matchTypes.get());
    }
    MOZ_LOG(log, LogLevel::Debug,
            ("(%s-fontmatching) fontgroup: [%s] default: %s lang: %s script: %d"
             "%s\n",
             (mStyle.systemFont ? "textrunui" : "textrun"),
             FamilyListToString(mFamilyList).get(),
             (defaultLanguageGeneric == StyleGenericFontFamily::Serif
                  ? "serif"
                  : (defaultLanguageGeneric == StyleGenericFontFamily::SansSerif
                         ? "sans-serif"
                         : "none")),
             lang.get(), static_cast<int>(aRunScript), fontMatches.get()));
  }
#endif
}

gfxUserFontSet* gfxFontGroup::GetUserFontSet() { return mUserFontSet; }

void gfxFontGroup::SetUserFontSet(gfxUserFontSet* aUserFontSet) {
  if (aUserFontSet == mUserFontSet) {
    return;
  }
  mUserFontSet = aUserFontSet;
  mCurrGeneration = GetGeneration() - 1;
  UpdateUserFonts();
}

uint64_t gfxFontGroup::GetGeneration() {
  return mUserFontSet ? mUserFontSet->GetGeneration() : 0;
}

uint64_t gfxFontGroup::GetRebuildGeneration() {
  return mUserFontSet ? mUserFontSet->GetRebuildGeneration() : 0;
}

void gfxFontGroup::UpdateUserFonts() {
  if (mCurrGeneration < GetRebuildGeneration()) {
    // fonts in userfont set changed, need to redo the fontlist
    mResolvedFonts = false;
    ClearCachedData();
    mCurrGeneration = GetGeneration();
  } else if (mCurrGeneration != GetGeneration()) {
    // load state change occurred, verify load state and validity of fonts
    ClearCachedData();
    uint32_t len = mFonts.Length();
    for (uint32_t i = 0; i < len; i++) {
      FamilyFace& ff = mFonts[i];
      if (ff.Font() || !ff.IsUserFontContainer()) {
        continue;
      }
      ff.CheckState(mSkipDrawing);
    }
    mCurrGeneration = GetGeneration();
  }
}

bool gfxFontGroup::ContainsUserFont(const gfxUserFontEntry* aUserFont) {
  UpdateUserFonts();

  // If we have resolved the font list to concrete font faces, search through
  // the list for a specific user font face.
  if (mResolvedFonts) {
    uint32_t len = mFonts.Length();
    for (uint32_t i = 0; i < len; i++) {
      FamilyFace& ff = mFonts[i];
      if (ff.EqualsUserFont(aUserFont)) {
        return true;
      }
    }
    return false;
  }

  // If the font list is currently not resolved, we assume it might use the
  // given face. (This method is only called when we have already seen that
  // the family name is present in the list.)
  return true;
}

already_AddRefed<gfxFont> gfxFontGroup::WhichPrefFontSupportsChar(
    uint32_t aCh, uint32_t aNextCh, FontPresentation aPresentation) {
  eFontPrefLang charLang;
  gfxPlatformFontList* pfl = gfxPlatformFontList::PlatformFontList();

  if (PrefersColor(aPresentation)) {
    charLang = eFontPrefLang_Emoji;
  } else {
    // get the pref font list if it hasn't been set up already
    charLang = pfl->GetFontPrefLangFor(aCh);
  }

  // if the last pref font was the first family in the pref list, no need to
  // recheck through a list of families
  if (mLastPrefFont && charLang == mLastPrefLang && mLastPrefFirstFont &&
      mLastPrefFont->HasCharacter(aCh)) {
    return do_AddRef(mLastPrefFont);
  }

  // based on char lang and page lang, set up list of pref lang fonts to check
  eFontPrefLang prefLangs[kMaxLenPrefLangList];
  uint32_t i, numLangs = 0;

  pfl->GetLangPrefs(prefLangs, numLangs, charLang, mPageLang);

  for (i = 0; i < numLangs; i++) {
    eFontPrefLang currentLang = prefLangs[i];
    StyleGenericFontFamily generic =
        mFallbackGeneric != StyleGenericFontFamily::None
            ? mFallbackGeneric
            : pfl->GetDefaultGeneric(currentLang);
    gfxPlatformFontList::PrefFontList* families = pfl->GetPrefFontsLangGroup(
        mFontVisibilityProvider, generic, currentLang);
    NS_ASSERTION(families, "no pref font families found");

    // find the first pref font that includes the character
    uint32_t j, numPrefs;
    numPrefs = families->Length();
    for (j = 0; j < numPrefs; j++) {
      // look up the appropriate face
      FontFamily family = (*families)[j];
      if (family.IsNull()) {
        continue;
      }

      // if a pref font is used, it's likely to be used again in the same text
      // run. the style doesn't change so the face lookup can be cached rather
      // than calling FindOrMakeFont repeatedly.  speeds up FindFontForChar
      // lookup times for subsequent pref font lookups
      if (family == mLastPrefFamily && mLastPrefFont->HasCharacter(aCh)) {
        return do_AddRef(mLastPrefFont);
      }

      gfxFontEntry* fe = nullptr;
      if (family.mShared) {
        fontlist::Family* fam = family.mShared;
        if (!fam->IsInitialized()) {
          (void)pfl->InitializeFamily(fam);
        }
        fontlist::Face* face =
            fam->FindFaceForStyle(pfl->SharedFontList(), mStyle);
        if (face) {
          fe = pfl->GetOrCreateFontEntry(face, fam);
        }
      } else {
        fe = family.mUnshared->FindFontForStyle(mStyle);
      }
      if (!fe) {
        continue;
      }

      // if ch in cmap, create and return a gfxFont
      RefPtr<gfxFont> prefFont;
      if (fe->HasCharacter(aCh)) {
        prefFont = fe->FindOrMakeFont(&mStyle);
        if (!prefFont) {
          continue;
        }
        if (aPresentation == FontPresentation::EmojiExplicit &&
            !prefFont->HasColorGlyphFor(aCh, aNextCh)) {
          continue;
        }
      }

      // If the char was not available, see if we can fall back to an
      // alternative face in the same family.
      if (!prefFont) {
        prefFont = family.mShared
                       ? FindFallbackFaceForChar(family.mShared, aCh, aNextCh,
                                                 aPresentation)
                       : FindFallbackFaceForChar(family.mUnshared, aCh, aNextCh,
                                                 aPresentation);
      }
      if (prefFont) {
        mLastPrefFamily = family;
        mLastPrefFont = prefFont;
        mLastPrefLang = charLang;
        mLastPrefFirstFont = (i == 0 && j == 0);
        return prefFont.forget();
      }
    }
  }

  return nullptr;
}

already_AddRefed<gfxFont> gfxFontGroup::WhichSystemFontSupportsChar(
    uint32_t aCh, uint32_t aNextCh, Script aRunScript,
    FontPresentation aPresentation) {
  FontVisibility visibility;
  return gfxPlatformFontList::PlatformFontList()->SystemFindFontForChar(
      mFontVisibilityProvider, aCh, aNextCh, aRunScript, aPresentation, &mStyle,
      &visibility);
}

gfxFont::Metrics gfxFontGroup::GetMetricsForCSSUnits(
    gfxFont::Orientation aOrientation, StyleQueryFontMetricsFlags aFlags) {
  bool isFirst;
  RefPtr<gfxFont> font = GetFirstValidFont(0x20, nullptr, &isFirst);
  auto metrics = font->GetMetrics(aOrientation);

  // If the font we used to get metrics was not the first in the list,
  // or if it doesn't support the ZERO character, check for the font that
  // does support ZERO and use its metrics for the 'ch' unit.
  if ((aFlags & StyleQueryFontMetricsFlags::NEEDS_CH) &&
      (!isFirst || !font->HasCharacter('0'))) {
    RefPtr<gfxFont> zeroFont = GetFirstValidFont('0');
    if (zeroFont != font) {
      const auto& zeroMetrics = zeroFont->GetMetrics(aOrientation);
      metrics.zeroWidth = zeroMetrics.zeroWidth;
    }
  }

  // Likewise for the WATER ideograph character used as the basis for 'ic'.
  if ((aFlags & StyleQueryFontMetricsFlags::NEEDS_IC) &&
      (!isFirst || !font->HasCharacter(0x6C34))) {
    RefPtr<gfxFont> icFont = GetFirstValidFont(0x6C34);
    if (icFont != font) {
      const auto& icMetrics = icFont->GetMetrics(aOrientation);
      metrics.ideographicWidth = icMetrics.ideographicWidth;
    }
  }

  return metrics;
}

class DeferredNotifyMissingFonts final : public nsIRunnable {
 public:
  NS_DECL_THREADSAFE_ISUPPORTS

  explicit DeferredNotifyMissingFonts(nsString&& aScriptList)
      : mScriptList(std::move(aScriptList)) {}

 protected:
  virtual ~DeferredNotifyMissingFonts() = default;

  NS_IMETHOD Run(void) override {
    nsCOMPtr<nsIObserverService> service = GetObserverService();
    service->NotifyObservers(nullptr, "font-needed", mScriptList.get());
    return NS_OK;
  }

  nsString mScriptList;
};

NS_IMPL_ISUPPORTS(DeferredNotifyMissingFonts, nsIRunnable)

void gfxMissingFontRecorder::Flush() {
  static uint32_t mNotifiedFonts[gfxMissingFontRecorder::kNumScriptBitsWords];
  static StaticMutex sNotifiedFontsMutex;

  nsAutoString fontNeeded;
  sNotifiedFontsMutex.Lock();
  for (uint32_t i = 0; i < kNumScriptBitsWords; ++i) {
    mMissingFonts[i] &= ~mNotifiedFonts[i];
    if (!mMissingFonts[i]) {
      continue;
    }
    for (uint32_t j = 0; j < 32; ++j) {
      if (!(mMissingFonts[i] & (1 << j))) {
        continue;
      }
      mNotifiedFonts[i] |= (1 << j);
      if (!fontNeeded.IsEmpty()) {
        fontNeeded.Append(char16_t(','));
      }
      uint32_t sc = i * 32 + j;
      MOZ_ASSERT(sc < static_cast<uint32_t>(Script::NUM_SCRIPT_CODES),
                 "how did we set the bit for an invalid script code?");
      uint32_t tag = GetScriptTagForCode(static_cast<Script>(sc));
      fontNeeded.Append(char16_t(tag >> 24));
      fontNeeded.Append(char16_t((tag >> 16) & 0xff));
      fontNeeded.Append(char16_t((tag >> 8) & 0xff));
      fontNeeded.Append(char16_t(tag & 0xff));
    }
    mMissingFonts[i] = 0;
  }
  sNotifiedFontsMutex.Unlock();

  if (!fontNeeded.IsEmpty()) {
    if (NS_IsMainThread()) {
      nsCOMPtr<nsIObserverService> service = GetObserverService();
      service->NotifyObservers(nullptr, "font-needed", fontNeeded.get());
    } else {
      NS_DispatchToMainThread(
          new DeferredNotifyMissingFonts(std::move(fontNeeded)));
    }
  }
}
