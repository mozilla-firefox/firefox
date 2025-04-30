/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "apple/AppleUtils.h"

#include <VideoToolbox/VideoToolbox.h>

namespace mozilla {

SessionPropertyManager::SessionPropertyManager(
    const AutoCFTypeRef<VTCompressionSessionRef>& aSession)
    : mSession(aSession) {
  MOZ_ASSERT(mSession, "Session must be valid");
}

SessionPropertyManager::SessionPropertyManager(
    const VTCompressionSessionRef& aSession)
    : mSession(aSession, AutoTypePolicy::Retain) {
  MOZ_ASSERT(mSession, "Session must be valid");
}

bool SessionPropertyManager::IsSupported(CFStringRef aKey) {
  MOZ_ASSERT(mSession);
  if (!mSupportedKeys) {
    CFDictionaryRef dict;
    if (VTSessionCopySupportedPropertyDictionary(mSession.Ref(), &dict) ==
        noErr) {
      mSupportedKeys.Reset(dict, AutoTypePolicy::NoRetain);
    }
  }
  return mSupportedKeys && CFDictionaryContainsKey(mSupportedKeys.Ref(), aKey);
}

OSStatus SessionPropertyManager::Set(CFStringRef aKey, int32_t aValue) {
  return Set(aKey, aValue, kCFNumberSInt32Type);
}

OSStatus SessionPropertyManager::Set(CFStringRef aKey, int64_t aValue) {
  return Set(aKey, aValue, kCFNumberSInt64Type);
}

OSStatus SessionPropertyManager::Set(CFStringRef aKey, float aValue) {
  return Set(aKey, aValue, kCFNumberFloatType);
}

OSStatus SessionPropertyManager::Set(CFStringRef aKey, bool aValue) {
  MOZ_ASSERT(mSession);
  CFBooleanRef value = aValue ? kCFBooleanTrue : kCFBooleanFalse;
  return VTSessionSetProperty(mSession.Ref(), aKey, value);
}

OSStatus SessionPropertyManager::Set(CFStringRef aKey, CFStringRef value) {
  MOZ_ASSERT(mSession);
  return VTSessionSetProperty(mSession.Ref(), aKey, value);
}

OSStatus SessionPropertyManager::Copy(CFStringRef aKey, bool& aValue) {
  MOZ_ASSERT(mSession);

  AutoCFTypeRef<CFBooleanRef> value;
  OSStatus rv = VTSessionCopyProperty(mSession.Ref(), aKey, kCFAllocatorDefault,
                                      value.Receive());
  if (rv == noErr) {
    aValue = value == kCFBooleanTrue;
  }
  return rv;
}

template <typename V>
OSStatus SessionPropertyManager::Set(CFStringRef aKey, V aValue,
                                     CFNumberType aType) {
  MOZ_ASSERT(mSession);
  AutoCFTypeRef<CFNumberRef> value(
      CFNumberCreate(kCFAllocatorDefault, aType, &aValue),
      AutoTypePolicy::NoRetain);
  return VTSessionSetProperty(mSession.Ref(), aKey, value.Ref());
}

}  // namespace mozilla
