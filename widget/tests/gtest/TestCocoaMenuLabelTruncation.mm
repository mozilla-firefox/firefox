/* Any copyright is dedicated to the Public Domain.
 * https://creativecommons.org/publicdomain/zero/1.0/ */

#include "gtest/gtest.h"
#include "nsMenuUtilsX.h"

static CGFloat WidthOf(NSString* aString, NSFont* aFont) {
  return [aString sizeWithAttributes:@{NSFontAttributeName : aFont}].width;
}

TEST(CocoaMenuLabelTruncation, FittingLabelIsUnchanged)
{
  NSFont* font = [NSFont menuFontOfSize:13];
  NSString* label = @"Short label";
  NSString* result =
      nsMenuUtilsX::TruncateLabelToWidth(label, font, WidthOf(label, font));
  EXPECT_TRUE([result isEqualToString:label]);
}

TEST(CocoaMenuLabelTruncation, LongLabelIsCutWithEllipsis)
{
  NSFont* font = [NSFont menuFontOfSize:13];
  NSString* label =
      @"A tab group with a very long name that does not fit in the menu";
  const CGFloat maxWidth = 100;
  NSString* result = nsMenuUtilsX::TruncateLabelToWidth(label, font, maxWidth);

  EXPECT_TRUE([result hasSuffix:@"…"]);
  EXPECT_LE(WidthOf(result, font), maxWidth);
  NSString* prefix = [result substringToIndex:result.length - 1];
  EXPECT_GT(prefix.length, 0u);
  EXPECT_TRUE([label hasPrefix:prefix]);

  // The prefix is the longest one that fits.
  NSString* longer =
      [[label substringToIndex:prefix.length + 1] stringByAppendingString:@"…"];
  EXPECT_GT(WidthOf(longer, font), maxWidth);
}

TEST(CocoaMenuLabelTruncation, TinyWidthGivesOnlyEllipsis)
{
  NSFont* font = [NSFont menuFontOfSize:13];
  NSString* result =
      nsMenuUtilsX::TruncateLabelToWidth(@"Anything at all", font, 1);
  EXPECT_TRUE([result isEqualToString:@"…"]);
}

TEST(CocoaMenuLabelTruncation, DoesNotSplitComposedCharacters)
{
  NSFont* font = [NSFont menuFontOfSize:13];
  // A family emoji is several UTF-16 code units joined by zero width
  // joiners, so most cut points would land inside one.
  static const char16_t kFamily[] =
      u"\U0001F468\u200D\U0001F469\u200D\U0001F467\u200D\U0001F466";
  NSString* family =
      [NSString stringWithCharacters:reinterpret_cast<const unichar*>(kFamily)
                              length:std::size(kFamily) - 1];
  NSString* label = [@"" stringByPaddingToLength:family.length * 8
                                      withString:family
                                 startingAtIndex:0];
  const CGFloat fullWidth = WidthOf(label, font);
  for (CGFloat maxWidth = 1; maxWidth < fullWidth; maxWidth += 3) {
    NSString* result =
        nsMenuUtilsX::TruncateLabelToWidth(label, font, maxWidth);
    ASSERT_TRUE([result hasSuffix:@"…"])
    << "max width " << maxWidth;
    NSUInteger prefixLength = result.length - 1;
    EXPECT_EQ(prefixLength % family.length, 0u) << "max width " << maxWidth;
  }
}
