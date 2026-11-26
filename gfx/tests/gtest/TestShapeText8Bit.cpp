/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include <string>

#include "gfxFont.h"
#include "gfxHarfBuzzShaper.h"
#include "gfxPlatform.h"
#include "gfxTextRun.h"
#include "gtest/gtest.h"
#include "mozilla/ServoStyleConsts.h"
#include "mozilla/Sprintf.h"
#include "nsGkAtoms.h"
#include "nsTArray.h"
#include "nsUnicodeProperties.h"

using namespace mozilla;

namespace {

already_AddRefed<gfxFontGroup> MakeSerifFontGroup() {
  nsTArray<StyleSingleFontFamily> names;
  names.AppendElement(
      StyleSingleFontFamily::Generic(StyleGenericFontFamily::Serif));
  gfxFontStyle style;
  RefPtr<gfxFontGroup> fontGroup = new gfxFontGroup(
      nullptr, StyleFontFamilyList::WithNames(std::move(names)), &style,
      nsGkAtoms::x_western, false, nullptr, nullptr, 1.0,
      StyleFontVariantEmoji::Normal);
  return fontGroup.forget();
}

void ExpectSameGlyphs(const gfxShapedWord* aA, const gfxShapedWord* aB,
                      uint32_t aLength, const std::string& aLabel) {
  for (uint32_t i = 0; i < aLength; ++i) {
    const auto& a = aA->GetCharacterGlyphs()[i];
    const auto& b = aB->GetCharacterGlyphs()[i];
    ASSERT_EQ(a.IsSimpleGlyph(), b.IsSimpleGlyph()) << aLabel << " @" << i;
    EXPECT_EQ(a.IsClusterStart(), b.IsClusterStart()) << aLabel << " @" << i;
    EXPECT_EQ(a.IsLigatureGroupStart(), b.IsLigatureGroupStart())
        << aLabel << " @" << i;
    EXPECT_EQ(a.IsMissing(), b.IsMissing()) << aLabel << " @" << i;
    if (a.IsSimpleGlyph()) {
      EXPECT_EQ(a.GetSimpleGlyph(), b.GetSimpleGlyph()) << aLabel << " @" << i;
      EXPECT_EQ(a.GetSimpleAdvance(), b.GetSimpleAdvance())
          << aLabel << " @" << i;
      continue;
    }
    ASSERT_EQ(a.GetGlyphCount(), b.GetGlyphCount()) << aLabel << " @" << i;
    uint32_t count = a.GetGlyphCount();
    if (!count) {
      continue;
    }
    const auto* da = aA->GetDetailedGlyphs(i, count);
    const auto* db = aB->GetDetailedGlyphs(i, count);
    ASSERT_TRUE(da && db)
    << aLabel << " @" << i;
    for (uint32_t j = 0; j < count; ++j) {
      EXPECT_EQ(da[j].mGlyphID, db[j].mGlyphID) << aLabel << " @" << i;
      EXPECT_EQ(da[j].mAdvance, db[j].mAdvance) << aLabel << " @" << i;
      EXPECT_EQ(da[j].mOffset.x, db[j].mOffset.x) << aLabel << " @" << i;
      EXPECT_EQ(da[j].mOffset.y, db[j].mOffset.y) << aLabel << " @" << i;
    }
  }
}

}  // namespace

// Shaping 8-bit text directly with harfbuzz must give the same glyphs as
// shaping the same text expanded to UTF-16.
TEST(gfxFont, ShapeText8BitMatches16Bit)
{
  ASSERT_TRUE(gfxPlatform::GetPlatform());
  RefPtr<gfxFontGroup> fontGroup = MakeSerifFontGroup();
  RefPtr<gfxFont> font = fontGroup->GetFirstValidFont();
  ASSERT_TRUE(font);
  gfxHarfBuzzShaper* shaper = font->GetHarfBuzzShaper();
  ASSERT_TRUE(shaper);

  const char16_t* const texts[] = {
      u"The",
      u"office",
      u"affine fjord",
      u"caf\u00E9 na\u00EFve fa\u00E7ade",
      u"\u00FF\u00DF\u00F1\u00C6\u00F8\u00B5\u00A9",
      u"non\u00A0breaking",
      u"soft\u00ADhyphen",
      u"\u00C0\u00C1\u00C2\u00C3 \u00D7 \u00F7",
  };

  for (const char16_t* text : texts) {
    const uint32_t length = std::char_traits<char16_t>::length(text);
    nsTArray<uint8_t> text8;
    for (uint32_t i = 0; i < length; ++i) {
      ASSERT_LE(text[i], 0xffu);
      text8.AppendElement(uint8_t(text[i]));
    }
    // Keep failure messages ASCII, whatever the text.
    std::string label;
    for (uint8_t ch : text8) {
      if (ch < 0x80) {
        label += char(ch);
      } else {
        char buf[8];
        SprintfLiteral(buf, "\\x%02x", ch);
        label += buf;
      }
    }
    for (bool rtl : {false, true}) {
      gfx::ShapedTextFlags flags = gfx::ShapedTextFlags();
      if (rtl) {
        flags |= gfx::ShapedTextFlags::TEXT_IS_RTL;
      }
      constexpr auto rounding = gfxFontShaper::RoundingFlags::kRoundX;
      UniquePtr<gfxShapedWord> word8(
          gfxShapedWord::Create(text8.Elements(), length, intl::Script::LATIN,
                                nullptr, 60, flags, rounding));
      UniquePtr<gfxShapedWord> word16(gfxShapedWord::Create(
          text, length, intl::Script::LATIN, nullptr, 60, flags, rounding));
      ASSERT_TRUE(word8 && word16);
      const gfxFontShaper::ShapingParams params{nullptr, intl::Script::LATIN,
                                                false, rounding};
      ASSERT_TRUE(shaper->ShapeText(text8.Elements(),
                                    gfxFontShaper::TextEncoding::Latin1, 0,
                                    length, params, word8.get()));
      ASSERT_TRUE(shaper->ShapeText(text, gfxFontShaper::TextEncoding::Utf16, 0,
                                    length, params, word16.get()));
      ExpectSameGlyphs(word8.get(), word16.get(), length,
                       label + (rtl ? " (rtl)" : " (ltr)"));
    }
  }
}

// The uint8_t overload of FilterIfIgnorable relies on U+00AD being the only
// default-ignorable character in Latin-1.
TEST(gfxFont, SoftHyphenIsOnlyIgnorableLatin1)
{
  for (uint32_t ch = 0; ch <= 0xff; ++ch) {
    EXPECT_EQ(unicode::IsDefaultIgnorable(ch), ch == 0x00AD) << ch;
  }
}

// A default-ignorable character outside the BMP must be recognized, and its
// trailing surrogate marked as a ligature continuation.
TEST(gfxFont, FilterIfIgnorableSupplementary)
{
  const char16_t text[] = {'a', 0xDB40, 0xDD00, 'b'};  // a U+E0100 b
  UniquePtr<gfxShapedWord> word(gfxShapedWord::Create(
      text, 4, intl::Script::LATIN, nullptr, 60, gfx::ShapedTextFlags(),
      gfxFontShaper::RoundingFlags::kRoundX));
  ASSERT_TRUE(word);
  EXPECT_FALSE(word->FilterIfIgnorable(0, uint32_t('a'), 1, 10.0f));
  EXPECT_TRUE(word->FilterIfIgnorable(1, uint32_t(0xE0100), 1, 10.0f));
  const auto* glyphs = word->GetCharacterGlyphs();
  EXPECT_TRUE(glyphs[1].IsMissing());
  EXPECT_FALSE(glyphs[2].IsMissing());
  EXPECT_FALSE(glyphs[2].IsSimpleGlyph());
  EXPECT_EQ(glyphs[2].GetGlyphCount(), 0u);
  EXPECT_FALSE(word->FilterIfIgnorable(1, uint32_t(0x1F600), 1, 10.0f));
}

// A Format character is kept only if the font gave it a real glyph with a
// positive advance.
TEST(gfxFont, FilterIfIgnorableFormatControl)
{
  const char16_t text[] = {0x200B, 0x200B, 0x200B, 0x00AD};
  UniquePtr<gfxShapedWord> word(gfxShapedWord::Create(
      text, 4, intl::Script::LATIN, nullptr, 60, gfx::ShapedTextFlags(),
      gfxFontShaper::RoundingFlags::kRoundX));
  ASSERT_TRUE(word);
  EXPECT_TRUE(word->FilterIfIgnorable(0, uint32_t(0x200B), 0, 10.0f));
  EXPECT_TRUE(word->FilterIfIgnorable(1, uint32_t(0x200B), 5, 0.0f));
  EXPECT_FALSE(word->FilterIfIgnorable(2, uint32_t(0x200B), 5, 10.0f));
  EXPECT_FALSE(word->FilterIfIgnorable(3, uint8_t(0xAD), 5, 10.0f));
  EXPECT_TRUE(word->FilterIfIgnorable(3, uint8_t(0xAD), 0, 0.0f));
  EXPECT_TRUE(word->GetCharacterGlyphs()[0].IsMissing());
}
