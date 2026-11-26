/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "gfxGraphiteShaper.h"

#include "ThebesRLBox.h"
#include "gfxContext.h"
#include "gfxFontConstants.h"
#include "gfxTextRun.h"
#include "graphite2/Font.h"
#include "graphite2/GraphiteExtra.h"
#include "graphite2/Segment.h"
#include "harfbuzz/hb.h"
#include "mozilla/ScopeExit.h"
#include "mozilla/Utf16.h"
#include "nsString.h"

#define FloatToFixed(f) (65536 * (f))
#define FixedToFloat(f) ((f) * (1.0 / 65536.0))
// Right shifts of negative (signed) integers are undefined, as are overflows
// when converting unsigned to negative signed integers.
// (If speed were an issue we could make some 2's complement assumptions.)
#define FixedToIntRound(f) \
  ((f) > 0 ? ((32768 + (f)) >> 16) : -((32767 - (f)) >> 16))

#define CopyAndVerifyOrFail(t, cond, failed) \
  (t).copy_and_verify([&](auto val) {        \
    if (!(cond)) {                           \
      *(failed) = true;                      \
    }                                        \
    return val;                              \
  })

using namespace mozilla;  // for AutoSwap_* types

/*
 * Creation and destruction; on deletion, release any font tables we're holding
 */

gfxGraphiteShaper::gfxGraphiteShaper(gfxFont* aFont)
    : gfxFontShaper(aFont),
      mGrFace(mFont->GetFontEntry()->GetGrFace()),
      mSandbox(mFont->GetFontEntry()->GetGrSandbox()),
      mCallback(mFont->GetFontEntry()->GetGrSandboxAdvanceCallbackHandle()),
      mFallbackToSmallCaps(false) {
  mCallbackData.mFont = aFont;
}

gfxGraphiteShaper::~gfxGraphiteShaper() {
  auto t_mGrFont = rlbox::from_opaque(mGrFont);
  if (t_mGrFont) {
    sandbox_invoke(*mSandbox, gr_font_destroy, t_mGrFont);
  }
  mFont->GetFontEntry()->ReleaseGrFace(mGrFace);
}

/*static*/
thread_local gfxGraphiteShaper::CallbackData*
    gfxGraphiteShaper::tl_GrGetAdvanceData = nullptr;

/*static*/
tainted_opaque_gr<float> gfxGraphiteShaper::GrGetAdvance(
    rlbox_sandbox_gr& sandbox,
    tainted_opaque_gr<const void*> /* appFontHandle */,
    tainted_opaque_gr<uint16_t> t_glyphid) {
  CallbackData* cb = tl_GrGetAdvanceData;
  if (!cb) {
    // GrGetAdvance callback called unexpectedly. Just return safe value.
    tainted_gr<float> ret = 0;
    return ret.to_opaque();
  }
  auto glyphid = rlbox::from_opaque(t_glyphid).unverified_safe_because(
      "Here the only use of a glyphid is for lookup to get a width. "
      "Implementations of GetGlyphWidth in this code base use a hashtable "
      "which is robust to unknown keys. So no validation is required.");
  tainted_gr<float> ret = FixedToFloat(cb->mFont->GetGlyphWidth(glyphid));
  return ret.to_opaque();
}

static inline uint32_t MakeGraphiteLangTag(uint32_t aTag) {
  uint32_t grLangTag = aTag;
  // replace trailing space-padding with NULs for graphite
  uint32_t mask = 0x000000FF;
  while ((grLangTag & mask) == ' ') {
    grLangTag &= ~mask;
    mask <<= 8;
  }
  return grLangTag;
}

struct GrFontFeatures {
  tainted_gr<gr_face*> mFace;
  tainted_gr<gr_feature_val*> mFeatures;
  rlbox_sandbox_gr* mSandbox;
};

static void AddFeature(uint32_t aTag, uint32_t aValue, void* aUserArg) {
  GrFontFeatures* f = static_cast<GrFontFeatures*>(aUserArg);

  tainted_gr<const gr_feature_ref*> fref =
      sandbox_invoke(*(f->mSandbox), gr_face_find_fref, f->mFace, aTag);
  if (fref) {
    sandbox_invoke(*(f->mSandbox), gr_fref_set_feature_value, fref, aValue,
                   f->mFeatures);
  }
}

// Count the number of Unicode characters in a UTF-16 string (i.e. surrogate
// pairs are counted as 1, although they are 2 code units).
// (Any isolated surrogates will count 1 each, because in decoding they would
// be replaced by individual U+FFFD REPLACEMENT CHARACTERs.)
static inline size_t CountUnicodes(const char16_t* aText, uint32_t aLength) {
  size_t total = 0;
  const char16_t* end = aText + aLength;
  while (aText < end) {
    if (mozilla::IsHighSurrogate(*aText) && aText + 1 < end &&
        mozilla::IsLowSurrogate(*(aText + 1))) {
      aText += 2;
    } else {
      aText++;
    }
    total++;
  }
  return total;
}

bool gfxGraphiteShaper::ShapeText(const void* aTextData, TextEncoding aEncoding,
                                  uint32_t aOffset, uint32_t aLength,
                                  const ShapingParams& aParams,
                                  gfxShapedText* aShapedText) {
  // Graphite takes UTF-16, so expand Latin-1 text.
  nsAutoString expandedText;
  const char16_t* aText;
  if (aEncoding == TextEncoding::Latin1) {
    nsDependentCSubstring latin1(static_cast<const char*>(aTextData), aLength);
    if (!AppendASCIItoUTF16(latin1, expandedText, mozilla::fallible)) {
      return false;
    }
    aText = expandedText.BeginReading();
  } else {
    aText = static_cast<const char16_t*>(aTextData);
  }

  const gfxFontStyle* style = mFont->GetStyle();
  auto t_mGrFace = rlbox::from_opaque(mGrFace);
  auto t_mGrFont = rlbox::from_opaque(mGrFont);

  if (!t_mGrFont) {
    if (!t_mGrFace) {
      return false;
    }

    if (mFont->ProvidesGlyphWidths()) {
      auto p_ops = mSandbox->malloc_in_sandbox<gr_font_ops>();
      if (!p_ops) {
        return false;
      }
      auto clean_ops = MakeScopeExit([&] { mSandbox->free_in_sandbox(p_ops); });
      p_ops->size = sizeof(*p_ops);
      p_ops->glyph_advance_x = *mCallback;
      p_ops->glyph_advance_y = nullptr;  // vertical text not yet implemented
      t_mGrFont = sandbox_invoke(
          *mSandbox, gr_make_font_with_ops, mFont->GetAdjustedSize(),
          // For security, we do not pass the callback data to this arg, and use
          // a TLS var instead. However, gr_make_font_with_ops expects this to
          // be a non null ptr, and changes its behavior if it isn't. Therefore,
          // we should pass some dummy non null pointer which will be passed to
          // the GrGetAdvance callback, but never used. Let's just pass p_ops
          // again, as this is a non-null tainted pointer.
          p_ops /* mCallbackData */, p_ops, t_mGrFace);
    } else {
      t_mGrFont = sandbox_invoke(*mSandbox, gr_make_font,
                                 mFont->GetAdjustedSize(), t_mGrFace);
    }
    mGrFont = t_mGrFont.to_opaque();

    if (!t_mGrFont) {
      return false;
    }

    // determine whether petite-caps falls back to small-caps
    if (style->variantCaps != StyleFontVariantCaps::Normal) {
      switch (style->variantCaps) {
        case StyleFontVariantCaps::AllPetiteCaps:
        case StyleFontVariantCaps::PetiteCaps:
          bool synLower, synUpper;
          mFont->SupportsVariantCaps(aParams.mScript, style->variantCaps,
                                     mFallbackToSmallCaps, synLower, synUpper);
          break;
        default:
          break;
      }
    }
  }

  gfxFontEntry* entry = mFont->GetFontEntry();
  uint32_t grLang = 0;
  if (style->languageOverride._0) {
    grLang = MakeGraphiteLangTag(style->languageOverride._0);
  } else if (entry->mLanguageOverride) {
    grLang = MakeGraphiteLangTag(entry->mLanguageOverride);
  } else if (aParams.mLanguage) {
    nsAutoCString langString;
    aParams.mLanguage->ToUTF8String(langString);
    grLang = GetGraphiteTagForLang(langString);
  }
  tainted_gr<gr_feature_val*> grFeatures =
      sandbox_invoke(*mSandbox, gr_face_featureval_for_lang, t_mGrFace, grLang);

  // insert any merged features into Graphite feature list
  GrFontFeatures f = {t_mGrFace, grFeatures, mSandbox};
  MergeFontFeatures(style, mFont->GetFontEntry()->mFeatureSettings,
                    aShapedText->DisableLigatures(),
                    mFont->GetFontEntry()->FamilyName(), mFallbackToSmallCaps,
                    AddFeature, &f);

  // Graphite shaping doesn't map U+00a0 (nbsp) to space if it is missing
  // from the font, so check for that possibility. (Most fonts double-map
  // the space glyph to both 0x20 and 0xA0, so this won't often be needed;
  // so we don't copy the text until we know it's required.)
  nsAutoString transformed;
  const char16_t NO_BREAK_SPACE = 0x00a0;
  if (!entry->HasCharacter(NO_BREAK_SPACE)) {
    nsDependentSubstring src(aText, aLength);
    if (src.FindChar(NO_BREAK_SPACE) != kNotFound) {
      transformed = src;
      transformed.ReplaceChar(NO_BREAK_SPACE, ' ');
      aText = transformed.BeginReading();
    }
  }

  size_t numChars = CountUnicodes(aText, aLength);
  gr_bidirtl grBidi = gr_bidirtl(
      aShapedText->IsRightToLeft() ? (gr_rtl | gr_nobidi) : gr_nobidi);

  tainted_gr<char16_t*> t_aText =
      mSandbox->malloc_in_sandbox<char16_t>(aLength);
  if (!t_aText) {
    return false;
  }
  auto clean_txt = MakeScopeExit([&] { mSandbox->free_in_sandbox(t_aText); });

  rlbox::memcpy(*mSandbox, t_aText, aText, aLength * sizeof(char16_t));

  tl_GrGetAdvanceData = &mCallbackData;
  auto clean_adv_data = MakeScopeExit([&] { tl_GrGetAdvanceData = nullptr; });

  tainted_gr<gr_segment*> seg =
      sandbox_invoke(*mSandbox, gr_make_seg, mGrFont, t_mGrFace, 0, grFeatures,
                     gr_utf16, t_aText, numChars, grBidi);

  sandbox_invoke(*mSandbox, gr_featureval_destroy, grFeatures);

  if (!seg) {
    return false;
  }

  nsresult rv = SetGlyphsFromSegment(aShapedText, aOffset, aLength, aText,
                                     t_aText.to_opaque(), seg.to_opaque(),
                                     aParams.mRounding);
  if (NS_FAILED(rv)) {
    aShapedText->ClearGlyphs();
  }

  sandbox_invoke(*mSandbox, gr_seg_destroy, seg);

  return NS_SUCCEEDED(rv);
}

nsresult gfxGraphiteShaper::SetGlyphsFromSegment(
    gfxShapedText* aShapedText, uint32_t aOffset, uint32_t aLength,
    const char16_t* aText, tainted_opaque_gr<char16_t*> t_aText,
    tainted_opaque_gr<gr_segment*> aSegment, RoundingFlags aRounding) {
  typedef gfxShapedText::CompressedGlyph CompressedGlyph;

  int32_t dev2appUnits = aShapedText->GetAppUnitsPerDevUnit();
  bool rtl = aShapedText->IsRightToLeft();

  // identify clusters; graphite may have reordered/expanded/ligated glyphs.
  tainted_gr<gr_glyph_to_char_association*> data =
      sandbox_invoke(*mSandbox, gr_get_glyph_to_char_association, aSegment,
                     aLength, rlbox::from_opaque(t_aText));

  if (!data) {
    return NS_ERROR_FAILURE;
  }

  tainted_gr<gr_glyph_to_char_cluster*> clusters = data->clusters;
  tainted_gr<uint16_t*> gids = data->gids;
  tainted_gr<float*> xLocs = data->xLocs;
  tainted_gr<float*> yLocs = data->yLocs;

  CompressedGlyph* charGlyphs = aShapedText->GetCharacterGlyphs() + aOffset;

  bool roundX = bool(aRounding & RoundingFlags::kRoundX);
  bool roundY = bool(aRounding & RoundingFlags::kRoundY);

  bool failedVerify = false;

  // cIndex is primarily used to index into the clusters array which has size
  // aLength below. As cIndex is not changing anymore, let's just verify it
  // and remove the tainted wrapper.
  uint32_t cIndex =
      CopyAndVerifyOrFail(data->cIndex, val < aLength, &failedVerify);
  if (failedVerify) {
    return NS_ERROR_ILLEGAL_VALUE;
  }
  // Minimum acceptable baseChar for the next cluster (to ensure monotonically
  // increasing indexes).
  uint32_t minBaseChar = 0;
  // now put glyphs into the textrun, one cluster at a time
  for (uint32_t i = 0; i <= cIndex; ++i) {
    // We makes a local copy of "clusters[i]" which is of type
    // tainted_gr<gr_glyph_to_char_cluster> below. We do this intentionally
    // rather than taking a reference. Taking a reference with the code
    //
    // tainted_volatile_gr<gr_glyph_to_char_cluster>& c = clusters[i];
    //
    // produces a tainted_volatile which means the value can change at any
    // moment allowing for possible time-of-check-time-of-use vuln. We thus
    // make a local copy to simplify the verification.
    tainted_gr<gr_glyph_to_char_cluster> c = clusters[i];

    tainted_gr<float> t_adv;  // total advance of the cluster
    if (rtl) {
      if (i == 0) {
        t_adv = sandbox_invoke(*mSandbox, gr_seg_advance_X, aSegment) -
                xLocs[c.baseGlyph];
      } else {
        t_adv = xLocs[clusters[i - 1].baseGlyph] - xLocs[c.baseGlyph];
      }
    } else {
      if (i == cIndex) {
        t_adv = sandbox_invoke(*mSandbox, gr_seg_advance_X, aSegment) -
                xLocs[c.baseGlyph];
      } else {
        t_adv = xLocs[clusters[i + 1].baseGlyph] - xLocs[c.baseGlyph];
      }
    }

    float adv = t_adv.unverified_safe_because(
        "Per Bug 1569464 - this is the advance width of a glyph or cluster of "
        "glyphs. There are no a-priori limits on what that might be. Incorrect "
        "values will tend to result in bad layout or missing text, or bad "
        "nscoord values. But, these will not result in safety issues.");

    // check unexpected offset - offs used to index into aText
    uint32_t offs = CopyAndVerifyOrFail(
        c.baseChar, val < aLength && val >= minBaseChar, &failedVerify);
    if (failedVerify) {
      return NS_ERROR_ILLEGAL_VALUE;
    }
    minBaseChar = offs + 1;

    const char gid_simple_value[] =
        "Per Bug 1569464 - these are glyph IDs that can range from 0 to the "
        "maximum glyph ID supported by the font. However, out-of-range values "
        "here should not lead to safety issues; they would simply result in "
        "blank rendering, although this depends on the platform back-end.";

    // gids[c.baseGlyph] is checked and used below. Since this is a
    // tainted_volatile, which can change at any moment, we make a local copy
    // first to prevent a time-of-check-time-of-use vuln.
    uint16_t gid_of_base_glyph =
        gids[c.baseGlyph].unverified_safe_because(gid_simple_value);

    // Check for default-ignorable char that didn't get filtered, combined,
    // etc by the shaping process, and skip it.
    auto one_glyph = c.nGlyphs == static_cast<uint32_t>(1);
    auto one_char = c.nChars == static_cast<uint32_t>(1);
    auto two_chars = c.nChars == static_cast<uint32_t>(2);

    const char ignorable_check[] =
        "using this boolean check to decide whether to ignore a character or "
        "not. The worst that can happen is a bad rendering.";
    if ((one_glyph && one_char).unverified_safe_because(ignorable_check)) {
      if (aShapedText->FilterIfIgnorable(aOffset + offs, uint32_t(aText[offs]),
                                         gid_of_base_glyph, adv)) {
        continue;
      }
    } else if ((one_glyph && two_chars)
                   .unverified_safe_because(ignorable_check) &&
               offs + 1 < aLength &&
               mozilla::IsSurrogatePair(aText[offs], aText[offs + 1])) {
      if (aShapedText->FilterIfIgnorable(
              aOffset + offs,
              mozilla::SurrogateToUCS4(aText[offs], aText[offs + 1]),
              gid_of_base_glyph, adv)) {
        continue;
      }
    }

    uint32_t appAdvance = roundX ? NSToIntRound(adv) * dev2appUnits
                                 : NSToIntRound(adv * dev2appUnits);

    const char fast_path[] =
        "Even if the number of glyphs set is an incorrect value, the else "
        "branch is a more general purpose algorithm which can handle other "
        "values of nGlyphs";

    if (one_glyph.unverified_safe_because(fast_path) &&
        CompressedGlyph::IsSimpleGlyphID(gid_of_base_glyph) &&
        CompressedGlyph::IsSimpleAdvance(appAdvance) &&
        charGlyphs[offs].IsClusterStart() &&
        (yLocs[c.baseGlyph] == 0).unverified_safe_because(fast_path)) {
      charGlyphs[offs].SetSimpleGlyph(appAdvance, gid_of_base_glyph);

    } else {
      // not a one-to-one mapping with simple metrics: use DetailedGlyph
      AutoTArray<gfxShapedText::DetailedGlyph, 8> details;
      float clusterLoc;

      uint32_t glyph_end =
          (c.baseGlyph + c.nGlyphs)
              .unverified_safe_because(
                  "This only controls the total number of glyphs set for this "
                  "particular text. Worst that can happen is a bad rendering");

      // check overflow - ensure loop start is before the end
      uint32_t glyph_start =
          CopyAndVerifyOrFail(c.baseGlyph, val <= glyph_end, &failedVerify);
      if (failedVerify) {
        return NS_ERROR_ILLEGAL_VALUE;
      }

      // The glyphCount field in a CompressedGlyph record is 16 bits;
      // check that we will not exceed this.
      if (glyph_end - glyph_start > 0xFFFF) {
        return NS_ERROR_ILLEGAL_VALUE;
      }
      if (!details.SetCapacity(glyph_end - glyph_start, fallible)) {
        return NS_ERROR_OUT_OF_MEMORY;
      }

      for (uint32_t j = glyph_start; j < glyph_end; ++j) {
        gfxShapedText::DetailedGlyph* d = details.AppendElement();
        d->mGlyphID = gids[j].unverified_safe_because(gid_simple_value);

        const char safe_coordinates[] =
            "There are no limits on coordinates. Worst case, bad values would "
            "force rendering off-screen, but there are no memory safety "
            "issues.";

        float yLocs_j = yLocs[j].unverified_safe_because(safe_coordinates);
        float xLocs_j = xLocs[j].unverified_safe_because(safe_coordinates);

        d->mOffset.y = roundY ? NSToIntRound(-yLocs_j) * dev2appUnits
                              : -yLocs_j * dev2appUnits;
        if (j == glyph_start) {
          d->mAdvance = appAdvance;
          clusterLoc = xLocs_j;
        } else {
          float dx =
              rtl ? (xLocs_j - clusterLoc) : (xLocs_j - clusterLoc - adv);
          d->mOffset.x =
              roundX ? NSToIntRound(dx) * dev2appUnits : dx * dev2appUnits;
          d->mAdvance = 0;
        }
      }
      aShapedText->SetDetailedGlyphs(aOffset + offs, details.Length(),
                                     details.Elements());
    }

    // check unexpected offset
    uint32_t char_end = CopyAndVerifyOrFail(c.baseChar + c.nChars,
                                            val <= aLength, &failedVerify);
    // check overflow - ensure loop start is before the end
    uint32_t char_start =
        CopyAndVerifyOrFail(c.baseChar + 1, val <= char_end, &failedVerify);
    if (failedVerify) {
      return NS_ERROR_ILLEGAL_VALUE;
    }

    for (uint32_t j = char_start; j < char_end; ++j) {
      CompressedGlyph& g = charGlyphs[j];
      NS_ASSERTION(!g.IsSimpleGlyph(), "overwriting a simple glyph");
      g.SetComplex(g.IsClusterStart(), false);
    }
  }

  sandbox_invoke(*mSandbox, gr_free_char_association, data);
  return NS_OK;
}

// for language tag validation - include list of tags from the IANA registry
#include "gfxLanguageTagList.cpp"

nsTHashSet<uint32_t>* gfxGraphiteShaper::sLanguageTags;

/*static*/
uint32_t gfxGraphiteShaper::GetGraphiteTagForLang(const nsCString& aLang) {
  int len = aLang.Length();
  if (len < 2) {
    return 0;
  }

  // convert primary language subtag to a left-packed, NUL-padded integer
  // for the Graphite API
  uint32_t grLang = 0;
  for (int i = 0; i < 4; ++i) {
    grLang <<= 8;
    if (i < len) {
      uint8_t ch = aLang[i];
      if (ch == '-') {
        // found end of primary language subtag, truncate here
        len = i;
        continue;
      }
      if (ch < 'a' || ch > 'z') {
        // invalid character in tag, so ignore it completely
        return 0;
      }
      grLang += ch;
    }
  }

  // valid tags must have length = 2 or 3
  if (len < 2 || len > 3) {
    return 0;
  }

  if (!sLanguageTags) {
    // store the registered IANA tags in a hash for convenient validation
    sLanguageTags = new nsTHashSet<uint32_t>(std::size(sLanguageTagList));
    for (const uint32_t* tag = sLanguageTagList; *tag != 0; ++tag) {
      sLanguageTags->Insert(*tag);
    }
  }

  // only accept tags known in the IANA registry
  if (sLanguageTags->Contains(grLang)) {
    return grLang;
  }

  return 0;
}

/*static*/
void gfxGraphiteShaper::Shutdown() {
#ifdef NS_FREE_PERMANENT_DATA
  if (sLanguageTags) {
    sLanguageTags->Clear();
    delete sLanguageTags;
    sLanguageTags = nullptr;
  }
#endif
}
