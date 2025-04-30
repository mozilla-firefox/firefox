/* clang-format off */
/* -*- Mode: Objective-C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* clang-format on */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#import "mozAccessible.h"

@interface mozHeadingAccessible : mozAccessible

// override
- (NSString*)moxTitle;

// override
- (id)moxValue;

@end

@interface mozLinkAccessible : mozAccessible

// override
- (id)moxValue;

// override
- (NSString*)moxRole;

// override
- (NSURL*)moxURL;

// override
- (NSNumber*)moxVisited;

// override
- (NSArray*)moxLinkedUIElements;

@end

@interface MOXListItemAccessible : mozAccessible

// override
- (NSString*)moxTitle;

@end

@interface MOXLabelAccessible : mozAccessible

// override
- (NSString*)moxTitle;

@end
