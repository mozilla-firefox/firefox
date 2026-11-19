/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "nsMacPreferencesReader.h"
#include "CFTypeRefPtr.h"
#include "MacStringHelpers.h"
#include "nsString.h"

#include <sys/stat.h>

#include "js/JSON.h"
#include "js/RootingAPI.h"
#include "js/Value.h"
#include "mozilla/JSONStringWriteFuncs.h"

NS_IMPL_ISUPPORTS(nsMacPreferencesReader, nsIMacPreferencesReader)

using namespace mozilla;

static void EvaluateDict(JSONWriter* aWriter,
                         NSDictionary<NSString*, id>* aDict);

static void EvaluateArray(JSONWriter* aWriter, NSArray* aArray) {
  for (id elem in aArray) {
    if ([elem isKindOfClass:[NSString class]]) {
      aWriter->StringElement(MakeStringSpan([elem UTF8String]));
    } else if ([elem isKindOfClass:[NSNumber class]]) {
      aWriter->IntElement([elem longLongValue]);
    } else if ([elem isKindOfClass:[NSArray class]]) {
      aWriter->StartArrayElement();
      EvaluateArray(aWriter, elem);
      aWriter->EndArray();
    } else if ([elem isKindOfClass:[NSDictionary class]]) {
      aWriter->StartObjectElement();
      EvaluateDict(aWriter, elem);
      aWriter->EndObject();
    }
  }
}

static void EvaluateDict(JSONWriter* aWriter,
                         NSDictionary<NSString*, id>* aDict) {
  for (NSString* key in aDict) {
    id value = aDict[key];
    if ([value isKindOfClass:[NSString class]]) {
      aWriter->StringProperty(MakeStringSpan([key UTF8String]),
                              MakeStringSpan([value UTF8String]));
    } else if ([value isKindOfClass:[NSNumber class]]) {
      aWriter->IntProperty(MakeStringSpan([key UTF8String]),
                           [value longLongValue]);
    } else if ([value isKindOfClass:[NSArray class]]) {
      aWriter->StartArrayProperty(MakeStringSpan([key UTF8String]));
      EvaluateArray(aWriter, value);
      aWriter->EndArray();
    } else if ([value isKindOfClass:[NSDictionary class]]) {
      aWriter->StartObjectProperty(MakeStringSpan([key UTF8String]));
      EvaluateDict(aWriter, value);
      aWriter->EndObject();
    }
  }
}

static bool IsTrue(id aValue) {
  if ([aValue isKindOfClass:[NSNumber class]]) {
    return [aValue boolValue];
  }
  if ([aValue isKindOfClass:[NSString class]]) {
    return [@[ @"yes", @"true", @"1" ] containsObject:[aValue lowercaseString]];
  }
  return false;
}

// Values from device-scope configuration profiles. A standard user can install
// a user-scope profile, so those values are not included here even though
// macOS reports them as forced too.
static NSDictionary<NSString*, id>* DeviceManagedPreferences() {
  NSString* bundleID = [[NSBundle mainBundle] bundleIdentifier];
  if (!bundleID) {
    return @{};
  }
  NSString* path = [NSString
      stringWithFormat:@"/Library/Managed Preferences/%@.plist", bundleID];
  NSDictionary<NSFileAttributeKey, id>* attributes =
      [[NSFileManager defaultManager] attributesOfItemAtPath:path error:nil];
  if (![attributes[NSFileOwnerAccountID] isEqual:@0] ||
      ([attributes filePosixPermissions] & (S_IWGRP | S_IWOTH))) {
    return @{};
  }
  return [NSDictionary dictionaryWithContentsOfFile:path] ?: @{};
}

static NSDictionary<NSString*, id>* SystemPreferences() {
  auto prefs = CFTypeRefPtr<CFDictionaryRef>::WrapUnderCreateRule(
      CFPreferencesCopyMultiple(nullptr, kCFPreferencesCurrentApplication,
                                kCFPreferencesAnyUser, kCFPreferencesAnyHost));
  return prefs ? [[(NSDictionary*)prefs.get() copy] autorelease] : @{};
}

// The user's own preferences, host-specific values winning over the others,
// overridden by values from user-scope configuration profiles.
static NSDictionary<NSString*, id>* UserPreferences(
    NSDictionary<NSString*, id>* aDeviceManaged) {
  NSMutableDictionary<NSString*, id>* user = [NSMutableDictionary dictionary];
  for (CFStringRef host : {kCFPreferencesAnyHost, kCFPreferencesCurrentHost}) {
    auto prefs = CFTypeRefPtr<CFDictionaryRef>::WrapUnderCreateRule(
        CFPreferencesCopyMultiple(nullptr, kCFPreferencesCurrentApplication,
                                  kCFPreferencesCurrentUser, host));
    if (prefs) {
      [user addEntriesFromDictionary:(NSDictionary*)prefs.get()];
    }
  }

  NSDictionary<NSString*, id>* all =
      [[NSUserDefaults standardUserDefaults] dictionaryRepresentation];
  for (NSString* key in all) {
    if (aDeviceManaged[key] ||
        !CFPreferencesAppValueIsForced((CFStringRef)key,
                                       kCFPreferencesCurrentApplication)) {
      continue;
    }
    auto value = CFTypeRefPtr<CFPropertyListRef>::WrapUnderCreateRule(
        CFPreferencesCopyAppValue((CFStringRef)key,
                                  kCFPreferencesCurrentApplication));
    if (value) {
      user[key] = (id)value.get();
    }
  }
  return user;
}

static void WriteSource(JSONWriter* aWriter, const char* aName,
                        NSDictionary<NSString*, id>* aDict) {
  NSString* enabledKey = (NSString*)CFSTR(ENTERPRISE_POLICIES_ENABLED_KEY);
  NSMutableDictionary<NSString*, id>* dict = [[aDict mutableCopy] autorelease];
  id enabled = dict[enabledKey];
  [dict removeObjectForKey:enabledKey];

  aWriter->StartObjectProperty(MakeStringSpan(aName));
  EvaluateDict(aWriter, dict);
  if (enabled) {
    aWriter->BoolProperty(MakeStringSpan(ENTERPRISE_POLICIES_ENABLED_KEY),
                          IsTrue(enabled));
  }
  aWriter->EndObject();
}

static bool IsEnabledIn(CFStringRef aUser, CFStringRef aHost) {
  auto value = CFTypeRefPtr<CFPropertyListRef>::WrapUnderCreateRule(
      CFPreferencesCopyValue(CFSTR(ENTERPRISE_POLICIES_ENABLED_KEY),
                             kCFPreferencesCurrentApplication, aUser, aHost));
  return IsTrue((id)value.get());
}

NS_IMETHODIMP
nsMacPreferencesReader::PoliciesEnabled(bool* aPoliciesEnabled) {
  CFStringRef key = CFSTR(ENTERPRISE_POLICIES_ENABLED_KEY);
  bool forced = false;
  if (CFPreferencesAppValueIsForced(key, kCFPreferencesCurrentApplication)) {
    auto value = CFTypeRefPtr<CFPropertyListRef>::WrapUnderCreateRule(
        CFPreferencesCopyAppValue(key, kCFPreferencesCurrentApplication));
    forced = IsTrue((id)value.get());
  }
  *aPoliciesEnabled =
      forced || IsEnabledIn(kCFPreferencesAnyUser, kCFPreferencesAnyHost) ||
      IsEnabledIn(kCFPreferencesCurrentUser, kCFPreferencesAnyHost) ||
      IsEnabledIn(kCFPreferencesCurrentUser, kCFPreferencesCurrentHost);
  return NS_OK;
}

NS_IMETHODIMP
nsMacPreferencesReader::ReadPolicySources(
    JSContext* aCx, JS::MutableHandle<JS::Value> aResult) {
  NSDictionary<NSString*, id>* managed = DeviceManagedPreferences();

  JSONStringWriteFunc<nsAutoCString> jsonStr;
  JSONWriter w(jsonStr);
  w.Start();
  WriteSource(&w, "managed", managed);
  WriteSource(&w, "system", SystemPreferences());
  WriteSource(&w, "user", UserPreferences(managed));
  w.End();

  NS_ConvertUTF8toUTF16 jsonStr16(jsonStr.StringCRef());

  JS::RootedValue val(aCx);
  MOZ_ALWAYS_TRUE(JS_ParseJSON(aCx, jsonStr16.get(), jsonStr16.Length(), &val));

  aResult.set(val);
  return NS_OK;
}
