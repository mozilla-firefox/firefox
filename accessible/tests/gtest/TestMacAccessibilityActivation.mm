/* Any copyright is dedicated to the Public Domain.
 * https://creativecommons.org/publicdomain/zero/1.0/ */

#import <Cocoa/Cocoa.h>

#include "gtest/gtest.h"
#include "mozilla/Preferences.h"
#include "mozilla/ScopeExit.h"
#include "mozilla/a11y/Platform.h"

using namespace mozilla;

TEST(MacAccessibilityActivation, RoleQueryDoesNotEnableEnhancedUI)
{
  ASSERT_NE(NSApp, nil);
  @autoreleasepool {
    constexpr auto pref = "accessibility.force_disabled";
    bool hadUserValue = Preferences::HasUserValue(pref);
    int32_t originalPref = Preferences::GetInt(pref, 0);
    Preferences::SetInt(pref, 0);
    bool wasGeckoA11yEnabled = a11y::ShouldA11yBeEnabled();
    bool wasEnhancedUIEnabled = [[NSApp
        accessibilityAttributeValue:@"AXEnhancedUserInterface"] boolValue];
    auto restore = MakeScopeExit([=] {
      [NSApp accessibilitySetValue:@(wasEnhancedUIEnabled)
                      forAttribute:@"AXEnhancedUserInterface"];
      if (wasGeckoA11yEnabled && !wasEnhancedUIEnabled) {
        [NSApp accessibilityRole];
      }
      if (hadUserValue) {
        Preferences::SetInt(pref, originalPref);
      } else {
        Preferences::ClearUser(pref);
      }
    });

    [NSApp accessibilitySetValue:@NO forAttribute:@"AXEnhancedUserInterface"];
    EXPECT_FALSE(a11y::ShouldA11yBeEnabled());

    EXPECT_TRUE([[NSApp accessibilityRole]
        isEqualToString:NSAccessibilityApplicationRole]);
    EXPECT_TRUE(a11y::ShouldA11yBeEnabled());
    EXPECT_FALSE([[NSApp accessibilityAttributeValue:@"AXEnhancedUserInterface"]
        boolValue]);

    [NSApp accessibilityRole];
    EXPECT_FALSE([[NSApp accessibilityAttributeValue:@"AXEnhancedUserInterface"]
        boolValue]);

    [NSApp accessibilitySetValue:@YES forAttribute:@"AXEnhancedUserInterface"];
    EXPECT_TRUE(a11y::ShouldA11yBeEnabled());
    EXPECT_TRUE([[NSApp accessibilityAttributeValue:@"AXEnhancedUserInterface"]
        boolValue]);
    [NSApp accessibilityRole];
    EXPECT_TRUE([[NSApp accessibilityAttributeValue:@"AXEnhancedUserInterface"]
        boolValue]);

    [NSApp accessibilitySetValue:@NO forAttribute:@"AXEnhancedUserInterface"];
    EXPECT_FALSE(a11y::ShouldA11yBeEnabled());
    EXPECT_FALSE([[NSApp accessibilityAttributeValue:@"AXEnhancedUserInterface"]
        boolValue]);
  }
}
