/* Any copyright is dedicated to the Public Domain.
 * https://creativecommons.org/publicdomain/zero/1.0/ */

#include "gtest/gtest.h"
#include "nsMenuUtilsX.h"

#import <Carbon/Carbon.h>

static NSString* StringFromChar(unichar aChar) {
  return [NSString stringWithCharacters:&aChar length:1];
}

static NSEvent* KeyDown(unsigned short aKeyCode, NSString* aCharacters,
                        NSEventModifierFlags aFlags) {
  return [NSEvent keyEventWithType:NSEventTypeKeyDown
                          location:NSZeroPoint
                     modifierFlags:aFlags
                         timestamp:0
                      windowNumber:0
                           context:nil
                        characters:aCharacters
       charactersIgnoringModifiers:aCharacters
                         isARepeat:NO
                           keyCode:aKeyCode];
}

TEST(CocoaMenuKeyEquivalent, FunctionKey)
{
  NSEvent* f14 = KeyDown(kVK_F14, StringFromChar(NSF14FunctionKey),
                         NSEventModifierFlagFunction);
  EXPECT_TRUE(nsMenuUtilsX::EventMatchesKeyEquivalent(
      f14, StringFromChar(NSF14FunctionKey), 0));
  EXPECT_FALSE(nsMenuUtilsX::EventMatchesKeyEquivalent(
      f14, StringFromChar(NSF13FunctionKey), 0));
  EXPECT_FALSE(nsMenuUtilsX::EventMatchesKeyEquivalent(
      f14, StringFromChar(NSF14FunctionKey), NSEventModifierFlagCommand));
}

TEST(CocoaMenuKeyEquivalent, ArrowKeyWithModifier)
{
  NSEvent* left =
      KeyDown(kVK_LeftArrow, StringFromChar(NSLeftArrowFunctionKey),
              NSEventModifierFlagCommand | NSEventModifierFlagNumericPad |
                  NSEventModifierFlagFunction);
  EXPECT_TRUE(nsMenuUtilsX::EventMatchesKeyEquivalent(
      left, StringFromChar(NSLeftArrowFunctionKey),
      NSEventModifierFlagCommand));
  EXPECT_FALSE(nsMenuUtilsX::EventMatchesKeyEquivalent(
      left, StringFromChar(NSLeftArrowFunctionKey), 0));
}

TEST(CocoaMenuKeyEquivalent, HomeAndForwardDelete)
{
  NSEvent* home = KeyDown(kVK_Home, StringFromChar(NSHomeFunctionKey),
                          NSEventModifierFlagFunction);
  EXPECT_TRUE(nsMenuUtilsX::EventMatchesKeyEquivalent(
      home, StringFromChar(NSHomeFunctionKey), 0));
  NSEvent* forwardDelete =
      KeyDown(kVK_ForwardDelete, StringFromChar(NSDeleteFunctionKey),
              NSEventModifierFlagFunction);
  EXPECT_TRUE(nsMenuUtilsX::EventMatchesKeyEquivalent(
      forwardDelete, StringFromChar(NSDeleteFunctionKey), 0));
}

TEST(CocoaMenuKeyEquivalent, Backspace)
{
  NSEvent* backspace = KeyDown(kVK_Delete, StringFromChar(NSDeleteCharacter),
                               NSEventModifierFlagOption);
  EXPECT_TRUE(nsMenuUtilsX::EventMatchesKeyEquivalent(
      backspace, StringFromChar(NSDeleteCharacter), NSEventModifierFlagOption));
}

TEST(CocoaMenuKeyEquivalent, Return)
{
  NSEvent* returnKey = KeyDown(kVK_Return, @"\r", NSEventModifierFlagCommand);
  EXPECT_TRUE(nsMenuUtilsX::EventMatchesKeyEquivalent(
      returnKey, @"\r", NSEventModifierFlagCommand));
  EXPECT_FALSE(nsMenuUtilsX::EventMatchesKeyEquivalent(returnKey, @"\r", 0));
}
