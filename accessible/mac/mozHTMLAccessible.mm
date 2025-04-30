/* clang-format off */
/* -*- Mode: Objective-C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* clang-format on */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#import "mozHTMLAccessible.h"

#import "LocalAccessible-inl.h"
#import "HyperTextAccessible.h"

#import "nsCocoaUtils.h"

using namespace mozilla::a11y;

@implementation mozHeadingAccessible

- (NSString*)moxTitle {
  nsAutoString title;

  ENameValueFlag flag = mGeckoAccessible->Name(title);
  if (flag != eNameFromSubtree) {
    // If this is a name via relation or attribute (eg. aria-label)
    // it will be provided via AXDescription.
    return nil;
  }

  return nsCocoaUtils::ToNSString(title);
}

- (id)moxValue {
  GroupPos groupPos = mGeckoAccessible->GroupPosition();

  return [NSNumber numberWithInt:groupPos.level];
}

@end

@implementation mozLinkAccessible

- (NSString*)moxValue {
  return @"";
}

- (NSURL*)moxURL {
  nsAutoString value;
  mGeckoAccessible->Value(value);

  NSString* urlString = value.IsEmpty() ? nil : nsCocoaUtils::ToNSString(value);
  if (!urlString) return nil;

  return [NSURL URLWithString:urlString];
}

- (NSNumber*)moxVisited {
  return @([self stateWithMask:states::TRAVERSED] != 0);
}

- (NSString*)moxRole {
  // If this is not LINKED, just expose this as a generic group accessible.
  // Chrome and Safari expose this as a childless AXStaticText, but
  // the HTML Accessibility API Mappings spec says this should be an AXGroup.
  if (![self stateWithMask:states::LINKED]) {
    return NSAccessibilityGroupRole;
  }

  return [super moxRole];
}

- (NSArray*)moxLinkedUIElements {
  return [self getRelationsByType:RelationType::LINKS_TO];
}

@end

@implementation MOXListItemAccessible

- (NSString*)moxTitle {
  return @"";
}

@end

@implementation MOXLabelAccessible

- (NSString*)moxTitle {
  return @"";
}

@end
