/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "nsTextRunTransformations.h"

#include <utility>

#include "GreekCasing.h"
#include "IrishCasing.h"
#include "MathMLTextRunFactory.h"
#include "mozilla/ComputedStyleInlines.h"
#include "mozilla/MemoryReporting.h"
#include "mozilla/StaticPrefs_layout.h"
#include "mozilla/StaticPrefs_mathml.h"
#include "mozilla/TextEditor.h"
#include "mozilla/Utf16.h"
#include "mozilla/gfx/2D.h"
#include "nsLineBreaker.h"
#include "nsSpecialCasingData.h"
#include "nsStyleConsts.h"
#include "nsStyleUtil.h"
#include "nsTextFrameUtils.h"
#include "nsUnicharUtils.h"
#include "nsUnicodeProperties.h"

using namespace mozilla;
using namespace mozilla::gfx;

// Unicode characters needing special casing treatment in tr/az languages
#define LATIN_CAPITAL_LETTER_I_WITH_DOT_ABOVE 0x0130
#define LATIN_SMALL_LETTER_DOTLESS_I 0x0131

// Greek sigma needs custom handling for the lowercase transform; for details
// see bug 740120.
#define GREEK_CAPITAL_LETTER_SIGMA 0x03A3
#define GREEK_SMALL_LETTER_FINAL_SIGMA 0x03C2
#define GREEK_SMALL_LETTER_SIGMA 0x03C3

already_AddRefed<nsTransformedTextRun> nsTransformedTextRun::Create(
    const gfxTextRunFactory::Parameters* aParams,
    nsTransformingTextRunFactory* aFactory, gfxFontGroup* aFontGroup,
    const char16_t* aString, uint32_t aLength,
    const gfx::ShapedTextFlags aFlags, const nsTextFrameUtils::Flags aFlags2,
    nsTArray<RefPtr<nsTransformedCharStyle>>&& aStyles, bool aOwnsFactory) {
  NS_ASSERTION(!(aFlags & gfx::ShapedTextFlags::TEXT_IS_8BIT),
               "didn't expect text to be marked as 8-bit here");

  void* storage =
      AllocateStorageForTextRun(sizeof(nsTransformedTextRun), aLength);
  if (!storage) {
    return nullptr;
  }

  RefPtr<nsTransformedTextRun> result = new (storage)
      nsTransformedTextRun(aParams, aFactory, aFontGroup, aString, aLength,
                           aFlags, aFlags2, std::move(aStyles), aOwnsFactory);
  return result.forget();
}

void nsTransformedTextRun::SetCapitalization(uint32_t aStart, uint32_t aLength,
                                             bool* aCapitalization) {
  if (mCapitalize.IsEmpty()) {
    // XXX(Bug 1631371) Check if this should use a fallible operation as it
    // pretended earlier.
    mCapitalize.AppendElements(GetLength());
    memset(mCapitalize.Elements(), 0, GetLength() * sizeof(bool));
  }
  memcpy(mCapitalize.Elements() + aStart, aCapitalization,
         aLength * sizeof(bool));
  mNeedsRebuild = true;
}

bool nsTransformedTextRun::SetPotentialLineBreaks(Range aRange,
                                                  const uint8_t* aBreakBefore) {
  bool changed = gfxTextRun::SetPotentialLineBreaks(aRange, aBreakBefore);
  if (changed) {
    mNeedsRebuild = true;
  }
  return changed;
}

void nsTransformedTextRun::SetEmergencyWrapPositions() {
  // This parallels part of what gfxShapedText::SetupClusterBoundaries() does
  // for normal textruns.
  bool prevWasHyphen = false;
  for (uint32_t pos : IntegerRange(mString.Length())) {
    const char16_t ch = mString[pos];
    if (prevWasHyphen) {
      if (nsContentUtils::IsAlphanumeric(ch)) {
        mCharacterGlyphs[pos].SetCanBreakBefore(
            CompressedGlyph::FLAG_BREAK_TYPE_EMERGENCY_WRAP);
      }
      prevWasHyphen = false;
    }
    if (nsContentUtils::IsHyphen(ch) && pos &&
        nsContentUtils::IsAlphanumeric(mString[pos - 1])) {
      prevWasHyphen = true;
    }
  }
}

size_t nsTransformedTextRun::SizeOfExcludingThis(
    mozilla::MallocSizeOf aMallocSizeOf) {
  size_t total = gfxTextRun::SizeOfExcludingThis(aMallocSizeOf);
  total += mStyles.ShallowSizeOfExcludingThis(aMallocSizeOf);
  total += mCapitalize.ShallowSizeOfExcludingThis(aMallocSizeOf);
  if (mOwnsFactory) {
    total += aMallocSizeOf(mFactory);
  }
  return total;
}

size_t nsTransformedTextRun::SizeOfIncludingThis(
    mozilla::MallocSizeOf aMallocSizeOf) {
  return aMallocSizeOf(this) + SizeOfExcludingThis(aMallocSizeOf);
}

already_AddRefed<nsTransformedTextRun>
nsTransformingTextRunFactory::MakeTextRun(
    const char16_t* aString, uint32_t aLength,
    const gfxTextRunFactory::Parameters* aParams, gfxFontGroup* aFontGroup,
    gfx::ShapedTextFlags aFlags, nsTextFrameUtils::Flags aFlags2,
    nsTArray<RefPtr<nsTransformedCharStyle>>&& aStyles, bool aOwnsFactory) {
  return nsTransformedTextRun::Create(aParams, this, aFontGroup, aString,
                                      aLength, aFlags, aFlags2,
                                      std::move(aStyles), aOwnsFactory);
}

already_AddRefed<nsTransformedTextRun>
nsTransformingTextRunFactory::MakeTextRun(
    const uint8_t* aString, uint32_t aLength,
    const gfxTextRunFactory::Parameters* aParams, gfxFontGroup* aFontGroup,
    gfx::ShapedTextFlags aFlags, nsTextFrameUtils::Flags aFlags2,
    nsTArray<RefPtr<nsTransformedCharStyle>>&& aStyles, bool aOwnsFactory) {
  // We'll only have a Unicode code path to minimize the amount of code needed
  // for these rarely used features
  NS_ConvertASCIItoUTF16 unicodeString(reinterpret_cast<const char*>(aString),
                                       aLength);
  return MakeTextRun(unicodeString.get(), aLength, aParams, aFontGroup,
                     aFlags & ~gfx::ShapedTextFlags::TEXT_IS_8BIT, aFlags2,
                     std::move(aStyles), aOwnsFactory);
}

void MergeCharactersInTextRun(gfxTextRun* aDest, gfxTextRun* aSrc,
                              const bool* aCharsToMerge,
                              const bool* aDeletedChars) {
  MOZ_ASSERT(!aDest->TrailingGlyphRun(), "unexpected glyphRuns in aDest!");
  uint32_t offset = 0;
  AutoTArray<gfxTextRun::DetailedGlyph, 4> glyphs;
  const gfxTextRun::CompressedGlyph continuationGlyph =
      gfxTextRun::CompressedGlyph::MakeComplex(false, false);
  const gfxTextRun::CompressedGlyph* srcGlyphs = aSrc->GetCharacterGlyphs();
  gfxTextRun::CompressedGlyph* destGlyphs = aDest->GetCharacterGlyphs();
  for (gfxTextRun::GlyphRunIterator iter(aSrc, gfxTextRun::Range(aSrc));
       !iter.AtEnd(); iter.NextRun()) {
    const gfxTextRun::GlyphRun* run = iter.GlyphRun();
    if (offset >= aDest->GetLength()) {
      MOZ_ASSERT_UNREACHABLE("bad offset in transformed textrun");
      break;
    }
    aDest->AddGlyphRun(run->mFont, run->mMatchType, offset, false,
                       run->mOrientation, run->mIsCJK);

    bool anyMissing = false;
    uint32_t mergeRunStart = iter.StringStart();
    // Initialize to a copy of the first source glyph in the merge run.
    gfxTextRun::CompressedGlyph mergedGlyph = srcGlyphs[mergeRunStart];
    uint32_t stringEnd = iter.StringEnd();
    for (uint32_t k = iter.StringStart(); k < stringEnd; ++k) {
      if (offset >= aDest->GetLength()) {
        MOZ_ASSERT_UNREACHABLE("bad offset in transformed textrun");
        break;
      }
      const gfxTextRun::CompressedGlyph g = srcGlyphs[k];
      if (g.IsSimpleGlyph()) {
        if (!anyMissing) {
          gfxTextRun::DetailedGlyph details;
          details.mGlyphID = g.GetSimpleGlyph();
          details.mAdvance = g.GetSimpleAdvance();
          glyphs.AppendElement(details);
        }
      } else {
        if (g.IsMissing()) {
          anyMissing = true;
          glyphs.Clear();
        }
        if (uint32_t count = g.GetGlyphCount()) {
          glyphs.AppendElements(aSrc->GetDetailedGlyphs(k, count), count);
        }
      }

      if (k + 1 < iter.StringEnd() && aCharsToMerge[k + 1]) {
        // next char is supposed to merge with current, so loop without
        // writing current merged glyph to the destination
        continue;
      }

      // If the start of the merge run is actually a character that should
      // have been merged with the previous character (this can happen
      // if there's a font change in the middle of a case-mapped character,
      // that decomposed into a sequence of base+diacritics, for example),
      // just discard the entire merge run. See comment at start of this
      // function.
      NS_WARNING_ASSERTION(
          !aCharsToMerge[mergeRunStart],
          "unable to merge across a glyph run boundary, glyph(s) discarded");
      if (!aCharsToMerge[mergeRunStart]) {
        // Determine if we can just copy the existing simple glyph record.
        if (mergedGlyph.IsSimpleGlyph() && glyphs.Length() == 1) {
          destGlyphs[offset] = mergedGlyph;
        } else {
          // Otherwise set up complex glyph record and store detailed glyphs.
          mergedGlyph.SetComplex(mergedGlyph.IsClusterStart(),
                                 mergedGlyph.IsLigatureGroupStart());
          // If the original character decomposed to multiple base glyphs,
          // like German es-zet being uppercased to "SS", or presentation-form
          // ligatures like U+FB01 being uppercased to "FI", then any letter-
          // spacing needs to be applied between these components. But most
          // multi-character mappings generate a base glyph and diacritic(s),
          // in which case internal letter-spacing should NOT be applied.
          // We distinguish the cases here by checking if all the component
          // glyphs have non-zero advance; if so, set the letter-spacing flag.
          if (glyphs.Length() > 1 &&
              std::all_of(glyphs.cbegin(), glyphs.cend(),
                          [](const gfxTextRun::DetailedGlyph& g) -> bool {
                            return g.mAdvance > 0;
                          })) {
            mergedGlyph.SetApplyLetterSpacingBetweenDetailedGlyphs();
          }
          destGlyphs[offset] = mergedGlyph;
          aDest->SetDetailedGlyphs(offset, glyphs.Length(), glyphs.Elements());
          if (anyMissing) {
            destGlyphs[offset].SetMissing();
          }
        }
        offset++;

        while (offset < aDest->GetLength() && aDeletedChars[offset]) {
          destGlyphs[offset++] = continuationGlyph;
        }
      }

      glyphs.Clear();
      anyMissing = false;
      mergeRunStart = k + 1;
      if (mergeRunStart < stringEnd) {
        mergedGlyph = srcGlyphs[mergeRunStart];
      }
    }
    NS_ASSERTION(glyphs.Length() == 0,
                 "Leftover glyphs, don't request merging of the last character "
                 "with its next!");
  }
  MOZ_DIAGNOSTIC_ASSERT(offset == aDest->GetLength(),
                        "Bad offset calculation in MergeCharactersInTextRun");
}

gfxTextRunFactory::Parameters GetParametersForInner(
    nsTransformedTextRun* aTextRun, gfx::ShapedTextFlags* aFlags,
    DrawTarget* aRefDrawTarget) {
  gfxTextRunFactory::Parameters params = {
      aRefDrawTarget, nullptr, nullptr,
      nullptr,        0,       aTextRun->GetAppUnitsPerDevUnit()};
  *aFlags = aTextRun->GetFlags();
  return params;
}

// Some languages have special casing conventions that differ from the
// default Unicode mappings.
// The enum values here are named for well-known exemplar languages that
// exhibit the behavior in question; multiple lang tags may map to the
// same setting here, if the behavior is shared by other languages.
enum LanguageSpecificCasingBehavior {
  eLSCB_None,       // default non-lang-specific behavior
  eLSCB_Dutch,      // treat "ij" digraph as a unit for capitalization
  eLSCB_Greek,      // strip accent when uppercasing Greek vowels
  eLSCB_Irish,      // keep prefix letters as lowercase when uppercasing Irish
  eLSCB_Turkish,    // preserve dotted/dotless-i distinction in uppercase
  eLSCB_Lithuanian  // retain dot on lowercase i/j when an accent is present
};

static LanguageSpecificCasingBehavior GetCasingFor(const nsAtom* aLang) {
  if (!aLang) {
    return eLSCB_None;
  }
  if (nsStyleUtil::MatchesLanguagePrefix(aLang, u"tr") ||
      nsStyleUtil::MatchesLanguagePrefix(aLang, u"az") ||
      nsStyleUtil::MatchesLanguagePrefix(aLang, u"ba") ||
      nsStyleUtil::MatchesLanguagePrefix(aLang, u"crh") ||
      nsStyleUtil::MatchesLanguagePrefix(aLang, u"tt")) {
    return eLSCB_Turkish;
  }
  if (nsStyleUtil::MatchesLanguagePrefix(aLang, u"nl")) {
    return eLSCB_Dutch;
  }
  if (nsStyleUtil::MatchesLanguagePrefix(aLang, u"el")) {
    return eLSCB_Greek;
  }
  if (nsStyleUtil::MatchesLanguagePrefix(aLang, u"ga")) {
    return eLSCB_Irish;
  }
  if (nsStyleUtil::MatchesLanguagePrefix(aLang, u"lt")) {
    return eLSCB_Lithuanian;
  }
  return eLSCB_None;
}

bool nsCaseTransformTextRunFactory::TransformString(
    const nsAString& aString, nsString& aConvertedString,
    const Maybe<StyleTextTransform>& aGlobalTransform, char16_t aMaskChar,
    bool aCaseTransformsOnly, bool aUseCapitalEsZet, const nsAtom* aLanguage,
    nsTArray<bool>& aCharsToMergeArray, nsTArray<bool>& aDeletedCharsArray,
    const nsTransformedTextRun* aTextRun, uint32_t aOffsetInTextRun,
    nsTArray<uint8_t>* aCanBreakBeforeArray,
    nsTArray<RefPtr<nsTransformedCharStyle>>* aStyleArray) {
  bool auxiliaryOutputArrays = aCanBreakBeforeArray && aStyleArray;
  MOZ_ASSERT(!auxiliaryOutputArrays || aTextRun,
             "text run must be provided to use aux output arrays");

  uint32_t length = aString.Length();
  const char16_t* str = aString.BeginReading();
  // If an unconditional mask character was passed, we'll use it; if not, any
  // masking called for by the textrun styles will use TextEditor's mask char.
  const char16_t mask = aMaskChar ? aMaskChar : TextEditor::PasswordMask();

  bool mergeNeeded = false;

  bool capitalizeDutchIJ = false;
  bool prevIsLetter = false;
  bool ntPrefix = false;  // true immediately after a word-initial 'n' or 't'
                          // when doing Irish lowercasing
  bool seenSoftDotted = false;  // true immediately after an I or J that is
                                // converted to lowercase in Lithuanian mode
  uint32_t sigmaIndex = uint32_t(-1);
  nsUGenCategory cat;

  StyleTextTransform style = aGlobalTransform.valueOr(StyleTextTransform::NONE);
  bool forceNonFullWidth = false;
  const nsAtom* lang = aLanguage;

  LanguageSpecificCasingBehavior languageSpecificCasing = GetCasingFor(lang);
  mozilla::GreekCasing::State greekState;
  mozilla::IrishCasing::State irishState;
  uint32_t irishMark = uint32_t(-1);  // location of possible prefix letter(s)
                                      // in the output string
  uint32_t irishMarkSrc = uint32_t(-1);  // corresponding location in source
                                         // string (may differ from output due
                                         // to expansions like eszet -> 'SS')
  uint32_t greekMark = uint32_t(-1);  // location of uppercase ETA that may need
                                      // tonos added (if it is disjunctive eta)
  const char16_t kGreekUpperEta = 0x0397;

  // If we're doing capitalization and don't have a textrun, this is the state
  // to be passed to each call to nsLineBreaker::ShouldCapitalize.
  bool capitalizeNext = true;

  for (uint32_t i = 0; i < length; ++i, ++aOffsetInTextRun) {
    uint32_t ch = str[i];

    RefPtr<nsTransformedCharStyle> charStyle;
    if (aTextRun) {
      charStyle = aTextRun->mStyles[aOffsetInTextRun];
      style = aGlobalTransform.valueOr(charStyle->mTextTransform);
      forceNonFullWidth = charStyle->mForceNonFullWidth;

      nsAtom* newLang =
          charStyle->mExplicitLanguage ? charStyle->mLanguage.get() : nullptr;
      if (lang != newLang) {
        lang = newLang;
        languageSpecificCasing = GetCasingFor(lang);
        greekState.Reset();
        irishState.Reset();
        irishMark = uint32_t(-1);
        irishMarkSrc = uint32_t(-1);
        greekMark = uint32_t(-1);
      }
    }

    // These should be mutually exclusive: mMaskPassword is set if we are
    // handling <input type=password>, where the TextEditor code controls
    // masking and we use its PasswordMask() character, in which case
    // aMaskChar (from -webkit-text-security) is not used.
    MOZ_ASSERT_IF(aMaskChar, !(charStyle && charStyle->mMaskPassword));

    bool maskPassword = (charStyle && charStyle->mMaskPassword) || aMaskChar;
    int extraChars = 0;
    const unicode::MultiCharMapping* mcm;
    bool inhibitBreakBefore = false;  // have we just deleted preceding hyphen?

    if (i < length - 1 && mozilla::IsSurrogatePair(ch, str[i + 1])) {
      ch = mozilla::SurrogateToUCS4(ch, str[i + 1]);
    }
    const uint32_t originalCh = ch;

    // Skip case transform if we're masking current character.
    if (!maskPassword) {
      switch (Servo_TextTransform_Case(style)) {
        case StyleTextTransformCase::None:
          break;
        case StyleTextTransformCase::Lowercase:
          if (languageSpecificCasing == eLSCB_Turkish) {
            if (ch == 'I') {
              ch = LATIN_SMALL_LETTER_DOTLESS_I;
              prevIsLetter = true;
              sigmaIndex = uint32_t(-1);
              break;
            }
            if (ch == LATIN_CAPITAL_LETTER_I_WITH_DOT_ABOVE) {
              ch = 'i';
              prevIsLetter = true;
              sigmaIndex = uint32_t(-1);
              break;
            }
          }

          if (languageSpecificCasing == eLSCB_Lithuanian) {
            // clang-format off
            /* From SpecialCasing.txt:
             * # Introduce an explicit dot above when lowercasing capital I's and J's
             * # whenever there are more accents above.
             * # (of the accents used in Lithuanian: grave, acute, tilde above, and ogonek)
             *
             * 0049; 0069 0307; 0049; 0049; lt More_Above; # LATIN CAPITAL LETTER I
             * 004A; 006A 0307; 004A; 004A; lt More_Above; # LATIN CAPITAL LETTER J
             * 012E; 012F 0307; 012E; 012E; lt More_Above; # LATIN CAPITAL LETTER I WITH OGONEK
             * 00CC; 0069 0307 0300; 00CC; 00CC; lt; # LATIN CAPITAL LETTER I WITH GRAVE
             * 00CD; 0069 0307 0301; 00CD; 00CD; lt; # LATIN CAPITAL LETTER I WITH ACUTE
             * 0128; 0069 0307 0303; 0128; 0128; lt; # LATIN CAPITAL LETTER I WITH TILDE
             */
            // clang-format on
            if (ch == 'I' || ch == 'J' || ch == 0x012E) {
              ch = ToLowerCase(ch);
              prevIsLetter = true;
              seenSoftDotted = true;
              sigmaIndex = uint32_t(-1);
              break;
            }
            if (ch == 0x00CC) {
              aConvertedString.Append('i');
              aConvertedString.Append(0x0307);
              extraChars += 2;
              ch = 0x0300;
              prevIsLetter = true;
              seenSoftDotted = false;
              sigmaIndex = uint32_t(-1);
              break;
            }
            if (ch == 0x00CD) {
              aConvertedString.Append('i');
              aConvertedString.Append(0x0307);
              extraChars += 2;
              ch = 0x0301;
              prevIsLetter = true;
              seenSoftDotted = false;
              sigmaIndex = uint32_t(-1);
              break;
            }
            if (ch == 0x0128) {
              aConvertedString.Append('i');
              aConvertedString.Append(0x0307);
              extraChars += 2;
              ch = 0x0303;
              prevIsLetter = true;
              seenSoftDotted = false;
              sigmaIndex = uint32_t(-1);
              break;
            }
          }

          cat = unicode::GetGenCategory(ch);

          if (languageSpecificCasing == eLSCB_Irish &&
              cat == nsUGenCategory::kLetter) {
            // See bug 1018805 for Irish lowercasing requirements
            if (!prevIsLetter && (ch == 'n' || ch == 't')) {
              ntPrefix = true;
            } else {
              if (ntPrefix && mozilla::IrishCasing::IsUpperVowel(ch)) {
                aConvertedString.Append('-');
                ++extraChars;
              }
              ntPrefix = false;
            }
          } else {
            ntPrefix = false;
          }

          if (seenSoftDotted && cat == nsUGenCategory::kMark) {
            // The seenSoftDotted flag will only be set in Lithuanian mode.
            if (ch == 0x0300 || ch == 0x0301 || ch == 0x0303) {
              aConvertedString.Append(0x0307);
              ++extraChars;
            }
          }
          seenSoftDotted = false;

          // Special lowercasing behavior for Greek Sigma: note that this is
          // listed as context-sensitive in Unicode's SpecialCasing.txt, but is
          // *not* a language-specific mapping; it applies regardless of the
          // language of the element.
          //
          // The lowercase mapping for CAPITAL SIGMA should be to SMALL SIGMA
          // (i.e. the non-final form) whenever there is a following letter, or
          // when the CAPITAL SIGMA occurs in isolation (neither preceded nor
          // followed by a LETTER); and to FINAL SIGMA when it is preceded by
          // another letter but not followed by one.
          //
          // To implement the context-sensitive nature of this mapping, we keep
          // track of whether the previous character was a letter. If not,
          // CAPITAL SIGMA will map directly to SMALL SIGMA. If the previous
          // character was a letter, CAPITAL SIGMA maps to FINAL SIGMA and we
          // record the position in the converted string; if we then encounter
          // another letter, that FINAL SIGMA is replaced with a standard
          // SMALL SIGMA.

          // If sigmaIndex is not -1, it marks where we have provisionally
          // mapped a CAPITAL SIGMA to FINAL SIGMA; if we now find another
          // letter, we need to change it to SMALL SIGMA.
          if (sigmaIndex != uint32_t(-1)) {
            if (cat == nsUGenCategory::kLetter) {
              aConvertedString.SetCharAt(GREEK_SMALL_LETTER_SIGMA, sigmaIndex);
            }
          }

          if (ch == GREEK_CAPITAL_LETTER_SIGMA) {
            // If preceding char was a letter, map to FINAL instead of SMALL,
            // and note where it occurred by setting sigmaIndex; we'll change
            // it to standard SMALL SIGMA later if another letter follows
            if (prevIsLetter) {
              ch = GREEK_SMALL_LETTER_FINAL_SIGMA;
              sigmaIndex = aConvertedString.Length();
            } else {
              // CAPITAL SIGMA not preceded by a letter is unconditionally
              // mapped to SMALL SIGMA
              ch = GREEK_SMALL_LETTER_SIGMA;
              sigmaIndex = uint32_t(-1);
            }
            prevIsLetter = true;
            break;
          }

          // ignore diacritics for the purpose of contextual sigma mapping;
          // otherwise, reset prevIsLetter appropriately and clear the
          // sigmaIndex marker
          if (cat != nsUGenCategory::kMark) {
            prevIsLetter = (cat == nsUGenCategory::kLetter);
            sigmaIndex = uint32_t(-1);
          }

          mcm = unicode::SpecialLower(ch);
          if (mcm) {
            int j = 0;
            while (j < 2 && mcm->mMappedChars[j + 1]) {
              aConvertedString.Append(mcm->mMappedChars[j]);
              ++extraChars;
              ++j;
            }
            ch = mcm->mMappedChars[j];
            break;
          }

          ch = ToLowerCase(ch);
          break;

        case StyleTextTransformCase::Uppercase:
          if (languageSpecificCasing == eLSCB_Turkish && ch == 'i') {
            ch = LATIN_CAPITAL_LETTER_I_WITH_DOT_ABOVE;
            break;
          }

          if (languageSpecificCasing == eLSCB_Greek) {
            bool markEta;
            bool updateEta;
            ch = mozilla::GreekCasing::UpperCase(ch, greekState, markEta,
                                                 updateEta);
            if (markEta) {
              greekMark = aConvertedString.Length();
            } else if (updateEta) {
              // Remove the TONOS from an uppercase ETA-TONOS that turned out
              // not to be disjunctive-eta.
              MOZ_ASSERT(aConvertedString.Length() > 0 &&
                             greekMark < aConvertedString.Length(),
                         "bad greekMark!");
              aConvertedString.SetCharAt(kGreekUpperEta, greekMark);
              greekMark = uint32_t(-1);
            }
            break;
          }

          if (languageSpecificCasing == eLSCB_Lithuanian) {
            /*
             * # Remove DOT ABOVE after "i" with upper or titlecase
             *
             * 0307; 0307; ; ; lt After_Soft_Dotted; # COMBINING DOT ABOVE
             */
            if (ch == 'i' || ch == 'j' || ch == 0x012F) {
              seenSoftDotted = true;
              ch = ToTitleCase(ch);
              break;
            }
            if (seenSoftDotted) {
              seenSoftDotted = false;
              if (ch == 0x0307) {
                ch = uint32_t(-1);
                break;
              }
            }
          }

          if (languageSpecificCasing == eLSCB_Irish) {
            bool mark;
            uint8_t action;
            ch = mozilla::IrishCasing::UpperCase(ch, irishState, mark, action);
            if (mark) {
              irishMark = aConvertedString.Length();
              irishMarkSrc = i;
              break;
            } else if (action) {
              nsString& str = aConvertedString;  // shorthand
              switch (action) {
                case 1:
                  // lowercase a single prefix letter
                  MOZ_ASSERT(str.Length() > 0 && irishMark < str.Length(),
                             "bad irishMark!");
                  str.SetCharAt(ToLowerCase(str[irishMark]), irishMark);
                  irishMark = uint32_t(-1);
                  irishMarkSrc = uint32_t(-1);
                  break;
                case 2:
                  // lowercase two prefix letters (immediately before current
                  // pos)
                  MOZ_ASSERT(str.Length() >= 2 && irishMark == str.Length() - 2,
                             "bad irishMark!");
                  str.SetCharAt(ToLowerCase(str[irishMark]), irishMark);
                  str.SetCharAt(ToLowerCase(str[irishMark + 1]), irishMark + 1);
                  irishMark = uint32_t(-1);
                  irishMarkSrc = uint32_t(-1);
                  break;
                case 3:
                  // lowercase one prefix letter, and delete following hyphen
                  // (which must be the immediately-preceding char)
                  MOZ_ASSERT(str.Length() >= 2 && irishMark == str.Length() - 2,
                             "bad irishMark!");
                  MOZ_ASSERT(
                      irishMark != uint32_t(-1) && irishMarkSrc != uint32_t(-1),
                      "failed to set irishMarks");
                  str.Replace(irishMark, 2, ToLowerCase(str[irishMark]));
                  aDeletedCharsArray[irishMarkSrc + 1] = true;
                  // Remove the trailing entries (corresponding to the deleted
                  // hyphen) from the auxiliary arrays.
                  uint32_t len = aCharsToMergeArray.Length();
                  MOZ_ASSERT(len >= 2);
                  aCharsToMergeArray.TruncateLength(len - 1);
                  if (auxiliaryOutputArrays) {
                    MOZ_ASSERT(aStyleArray->Length() == len);
                    MOZ_ASSERT(aCanBreakBeforeArray->Length() == len);
                    aStyleArray->TruncateLength(len - 1);
                    aCanBreakBeforeArray->TruncateLength(len - 1);
                    inhibitBreakBefore = true;
                  }
                  mergeNeeded = true;
                  irishMark = uint32_t(-1);
                  irishMarkSrc = uint32_t(-1);
                  break;
              }
              // ch has been set to the uppercase for current char;
              // No need to check for SpecialUpper here as none of the
              // characters that could trigger an Irish casing action have
              // special mappings.
              break;
            }
            // If we didn't have any special action to perform, fall through
            // to check for special uppercase (ß)
          }

          // Updated mapping for German eszett, not currently reflected in the
          // Unicode data files. This is behind a pref, as it may not work well
          // with many (esp. older) fonts.
          if (ch == 0x00DF && aUseCapitalEsZet) {
            ch = 0x1E9E;
            break;
          }

          mcm = unicode::SpecialUpper(ch);
          if (mcm) {
            int j = 0;
            while (j < 2 && mcm->mMappedChars[j + 1]) {
              aConvertedString.Append(mcm->mMappedChars[j]);
              ++extraChars;
              ++j;
            }
            ch = mcm->mMappedChars[j];
            break;
          }

          ch = ToUpperCase(ch);
          break;

        case StyleTextTransformCase::Capitalize: {
          if (capitalizeDutchIJ && ch == 'j') {
            ch = 'J';
            capitalizeDutchIJ = false;
            break;
          }
          capitalizeDutchIJ = false;
          // If we have a textrun, its mCapitalize array tells us which chars
          // are to be capitalized. If not, we track the state locally, and
          // assume there's no context to be considered.
          bool doCapitalize = false;
          if (aTextRun) {
            if (aOffsetInTextRun < aTextRun->mCapitalize.Length()) {
              doCapitalize = aTextRun->mCapitalize[aOffsetInTextRun];
            }
          } else {
            doCapitalize = nsLineBreaker::ShouldCapitalize(ch, capitalizeNext);
          }
          if (doCapitalize) {
            if (languageSpecificCasing == eLSCB_Turkish && ch == 'i') {
              ch = LATIN_CAPITAL_LETTER_I_WITH_DOT_ABOVE;
              break;
            }
            if (languageSpecificCasing == eLSCB_Dutch && ch == 'i') {
              ch = 'I';
              capitalizeDutchIJ = true;
              break;
            }
            if (languageSpecificCasing == eLSCB_Lithuanian) {
              /*
               * # Remove DOT ABOVE after "i" with upper or titlecase
               *
               * 0307; 0307; ; ; lt After_Soft_Dotted; # COMBINING DOT ABOVE
               */
              if (ch == 'i' || ch == 'j' || ch == 0x012F) {
                seenSoftDotted = true;
                ch = ToTitleCase(ch);
                break;
              }
              if (seenSoftDotted) {
                seenSoftDotted = false;
                if (ch == 0x0307) {
                  ch = uint32_t(-1);
                  break;
                }
              }
            }

            mcm = unicode::SpecialTitle(ch);
            if (mcm) {
              int j = 0;
              while (j < 2 && mcm->mMappedChars[j + 1]) {
                aConvertedString.Append(mcm->mMappedChars[j]);
                ++extraChars;
                ++j;
              }
              ch = mcm->mMappedChars[j];
              break;
            }

            ch = ToTitleCase(ch);
          }
          break;
        }

        case StyleTextTransformCase::MathAuto:
          // text-transform: math-auto is used for automatic italicization of
          // single-char <mi> elements. However, some legacy cases (italic style
          // fallback and <mi> with leading/trailing whitespace) are still
          // handled in MathMLTextRunFactory.
          if (length == 1) {
            uint32_t ch2 =
                MathMLTextRunFactory::MathVariant(ch, StyleMathVariant::Italic);
            if (StaticPrefs::mathml_mathvariant_styling_fallback_disabled()) {
              ch = ch2;
            } else if (ch2 != ch) {
              // Bug 930504. Some platforms do not have fonts for Mathematical
              // Alphanumeric Symbols. Hence we only perform the transform if a
              // character is actually available.
              auto* fontGroup = aTextRun->GetFontGroup();
              fontGroup->EnsureFontList();
              FontMatchType matchType;
              RefPtr<gfxFont> mathFont = fontGroup->FindFontForChar(
                  ch2, 0, 0, intl::Script::COMMON, nullptr, &matchType);
              if (mathFont) {
                ch = ch2;
              }
            }
          }
          break;
        default:
          MOZ_ASSERT_UNREACHABLE("all cases should be handled");
          break;
      }

      if (!aCaseTransformsOnly) {
        if (!forceNonFullWidth && (style & StyleTextTransform::FULL_WIDTH)) {
          ch = unicode::GetFullWidth(ch);
        }

        if (style & StyleTextTransform::FULL_SIZE_KANA) {
          // clang-format off
          static const uint32_t kSmallKanas[] = {
              // ぁ   ぃ      ぅ      ぇ      ぉ      っ      ゃ      ゅ      ょ
              0x3041, 0x3043, 0x3045, 0x3047, 0x3049, 0x3063, 0x3083, 0x3085, 0x3087,
              // ゎ   ゕ      ゖ
              0x308E, 0x3095, 0x3096,
              // ァ   ィ      ゥ      ェ      ォ      ッ      ャ      ュ      ョ
              0x30A1, 0x30A3, 0x30A5, 0x30A7, 0x30A9, 0x30C3, 0x30E3, 0x30E5, 0x30E7,
              // ヮ   ヵ      ヶ      ㇰ      ㇱ      ㇲ      ㇳ      ㇴ      ㇵ
              0x30EE, 0x30F5, 0x30F6, 0x31F0, 0x31F1, 0x31F2, 0x31F3, 0x31F4, 0x31F5,
              // ㇶ   ㇷ      ㇸ      ㇹ      ㇺ      ㇻ      ㇼ      ㇽ      ㇾ
              0x31F6, 0x31F7, 0x31F8, 0x31F9, 0x31FA, 0x31FB, 0x31FC, 0x31FD, 0x31FE,
              // ㇿ
              0x31FF,
              // ｧ    ｨ       ｩ       ｪ       ｫ       ｬ       ｭ       ｮ       ｯ
              0xFF67, 0xFF68, 0xFF69, 0xFF6A, 0xFF6B, 0xFF6C, 0xFF6D, 0xFF6E, 0xFF6F,
              // 𛄲    𛅐       𛅑       𛅒       𛅕       𛅤       𛅥       𛅦
              0x1B132, 0x1B150, 0x1B151, 0x1B152, 0x1B155, 0x1B164, 0x1B165, 0x1B166,
              // 𛅧
              0x1B167};
          static const uint16_t kFullSizeKanas[] = {
              // あ   い      う      え      お      つ      や      ゆ      よ
              0x3042, 0x3044, 0x3046, 0x3048, 0x304A, 0x3064, 0x3084, 0x3086, 0x3088,
              // わ   か      け
              0x308F, 0x304B, 0x3051,
              // ア   イ      ウ      エ      オ      ツ      ヤ      ユ      ヨ
              0x30A2, 0x30A4, 0x30A6, 0x30A8, 0x30AA, 0x30C4, 0x30E4, 0x30E6, 0x30E8,
              // ワ   カ      ケ      ク      シ      ス      ト      ヌ      ハ
              0x30EF, 0x30AB, 0x30B1, 0x30AF, 0x30B7, 0x30B9, 0x30C8, 0x30CC, 0x30CF,
              // ヒ   フ      ヘ      ホ      ム      ラ      リ      ル      レ
              0x30D2, 0x30D5, 0x30D8, 0x30DB, 0x30E0, 0x30E9, 0x30EA, 0x30EB, 0x30EC,
              // ロ
              0x30ED,
              // ｱ    ｲ       ｳ       ｴ       ｵ       ﾔ       ﾕ       ﾖ        ﾂ
              0xFF71, 0xFF72, 0xFF73, 0xFF74, 0xFF75, 0xFF94, 0xFF95, 0xFF96, 0xFF82,
              // こ   ゐ       ゑ      を      コ       ヰ      ヱ      ヲ       ン
              0x3053, 0x3090, 0x3091, 0x3092, 0x30B3, 0x30F0, 0x30F1, 0x30F2, 0x30F3};
          // clang-format on

          size_t index;
          const uint16_t len = std::size(kSmallKanas);
          if (mozilla::BinarySearch(kSmallKanas, 0, len, ch, &index)) {
            ch = kFullSizeKanas[index];
          }
        }
      }

      if (forceNonFullWidth) {
        ch = unicode::GetFullWidthInverse(ch);
      }
    }

    if (ch == uint32_t(-1)) {
      aDeletedCharsArray.AppendElement(true);
      mergeNeeded = true;
    } else {
      aDeletedCharsArray.AppendElement(false);
      aCharsToMergeArray.AppendElement(false);
      if (auxiliaryOutputArrays) {
        aStyleArray->AppendElement(charStyle);
        aCanBreakBeforeArray->AppendElement(
            inhibitBreakBefore
                ? gfxShapedText::CompressedGlyph::FLAG_BREAK_TYPE_NONE
                : aTextRun->CanBreakBefore(aOffsetInTextRun));
      }

      if (mozilla::IsInBMP(ch)) {
        aConvertedString.Append(maskPassword ? mask : ch);
      } else {
        if (maskPassword) {
          aConvertedString.Append(mask);
          // TODO: We should show a password mask for a surrogate pair later.
          aConvertedString.Append(mask);
        } else {
          aConvertedString.Append(mozilla::HighSurrogate(ch));
          aConvertedString.Append(mozilla::LowSurrogate(ch));
        }
        ++extraChars;
      }
      if (!mozilla::IsInBMP(originalCh)) {
        // Skip the trailing surrogate.
        ++aOffsetInTextRun;
        ++i;
        aDeletedCharsArray.AppendElement(true);
      }

      while (extraChars-- > 0) {
        mergeNeeded = true;
        aCharsToMergeArray.AppendElement(true);
        if (auxiliaryOutputArrays) {
          aStyleArray->AppendElement(charStyle);
          aCanBreakBeforeArray->AppendElement(
              gfxShapedText::CompressedGlyph::FLAG_BREAK_TYPE_NONE);
        }
      }
    }
  }

  // These output arrays, if present, must always have matching lengths:
  if (auxiliaryOutputArrays) {
    DebugOnly<uint32_t> len = aCharsToMergeArray.Length();
    MOZ_ASSERT(aStyleArray->Length() == len);
    MOZ_ASSERT(aCanBreakBeforeArray->Length() == len);
  }

  return mergeNeeded;
}

void nsCaseTransformTextRunFactory::RebuildTextRun(
    nsTransformedTextRun* aTextRun, DrawTarget* aRefDrawTarget,
    gfxMissingFontRecorder* aMFR) {
  nsAutoString convertedString;
  AutoTArray<bool, 50> charsToMergeArray;
  AutoTArray<bool, 50> deletedCharsArray;
  AutoTArray<uint8_t, 50> canBreakBeforeArray;
  AutoTArray<RefPtr<nsTransformedCharStyle>, 50> styleArray;

  auto globalTransform =
      mAllUppercase ? Some(StyleTextTransform::UPPERCASE) : Nothing();
  bool mergeNeeded = TransformString(
      aTextRun->mString, convertedString, globalTransform, mMaskChar,
      /* aCaseTransformsOnly = */ false, mUseCapitalEsZet, nullptr,
      charsToMergeArray, deletedCharsArray, aTextRun, 0, &canBreakBeforeArray,
      &styleArray);

  gfx::ShapedTextFlags flags;
  gfxTextRunFactory::Parameters innerParams =
      GetParametersForInner(aTextRun, &flags, aRefDrawTarget);
  gfxFontGroup* fontGroup = aTextRun->GetFontGroup();

  RefPtr<nsTransformedTextRun> transformedChild;
  RefPtr<gfxTextRun> cachedChild;
  gfxTextRun* child;

  if (mInnerTransformingTextRunFactory) {
    transformedChild = mInnerTransformingTextRunFactory->MakeTextRun(
        convertedString.BeginReading(), convertedString.Length(), &innerParams,
        fontGroup, flags, nsTextFrameUtils::Flags(), std::move(styleArray),
        false);
    child = transformedChild.get();
  } else {
    cachedChild = fontGroup->MakeTextRun(
        convertedString.BeginReading(), convertedString.Length(), &innerParams,
        flags, nsTextFrameUtils::Flags(), aMFR);
    child = cachedChild.get();
  }
  if (!child) {
    return;
  }
  // Copy potential linebreaks into child so they're preserved
  // (and also child will be shaped appropriately)
  NS_ASSERTION(convertedString.Length() == canBreakBeforeArray.Length(),
               "Dropped characters or break-before values somewhere!");
  gfxTextRun::Range range(0, uint32_t(canBreakBeforeArray.Length()));
  child->SetPotentialLineBreaks(range, canBreakBeforeArray.Elements());
  if (transformedChild) {
    transformedChild->FinishSettingProperties(aRefDrawTarget, aMFR);
  }

  aTextRun->ResetGlyphRuns();
  if (mergeNeeded) {
    // Now merge multiple characters into one multi-glyph character as required
    // and deal with skipping deleted accent chars
    NS_ASSERTION(charsToMergeArray.Length() == child->GetLength(),
                 "source length mismatch");
    NS_ASSERTION(deletedCharsArray.Length() == aTextRun->GetLength(),
                 "destination length mismatch");
    MergeCharactersInTextRun(aTextRun, child, charsToMergeArray.Elements(),
                             deletedCharsArray.Elements());
  } else {
    // No merging to do, so just copy; this produces a more optimized textrun.
    // We can't steal the data because the child may be cached and stealing
    // the data would break the cache.
    aTextRun->CopyGlyphDataFrom(child, gfxTextRun::Range(child), 0);
  }
}
