/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

// Utility functions to help with Apple API calls.

#ifndef mozilla_AppleUtils_h
#define mozilla_AppleUtils_h

#include "mozilla/Assertions.h"
#include "mozilla/Attributes.h"
#include <CoreFoundation/CFBase.h>      // For CFRelease()
#include <CoreVideo/CVBuffer.h>         // For CVBufferRelease()
#include <VideoToolbox/VideoToolbox.h>  // For VTCompressionSessionRef

#if TARGET_OS_IPHONE
inline bool OSSupportsSVC() {
  // TODO
  return false;
}
#else
#  include "nsCocoaFeatures.h"
inline bool OSSupportsSVC() {
  return nsCocoaFeatures::IsAtLeastVersion(11, 3, 0);
}
#endif

namespace mozilla {

// Wrapper class to call CFRelease/CVBufferRelease on reference types
// when they go out of scope.
template <class T, class F, F relFunc>
class AutoObjRefRelease {
 public:
  MOZ_IMPLICIT AutoObjRefRelease(T aRef) : mRef(aRef) {}
  ~AutoObjRefRelease() {
    if (mRef) {
      relFunc(mRef);
    }
  }
  // Return the wrapped ref so it can be used as an in parameter.
  operator T() { return mRef; }
  // Return a pointer to the wrapped ref for use as an out parameter.
  T* receive() { return &mRef; }

 private:
  // Copy operator isn't supported and is not implemented.
  AutoObjRefRelease<T, F, relFunc>& operator=(
      const AutoObjRefRelease<T, F, relFunc>&);
  T mRef;
};

template <typename T>
using AutoCFRelease = AutoObjRefRelease<T, decltype(&CFRelease), &CFRelease>;
template <typename T>
using AutoCVBufferRelease =
    AutoObjRefRelease<T, decltype(&CVBufferRelease), &CVBufferRelease>;

// CFRefPtr: A CoreFoundation smart pointer.
template <class T>
class CFRefPtr {
 public:
  explicit CFRefPtr(T aRef) : mRef(aRef) {
    if (mRef) {
      CFRetain(mRef);
    }
  }
  // Copy constructor.
  CFRefPtr(const CFRefPtr<T>& aCFRefPtr) : mRef(aCFRefPtr.mRef) {
    if (mRef) {
      CFRetain(mRef);
    }
  }
  // Copy operator
  CFRefPtr<T>& operator=(const CFRefPtr<T>& aCFRefPtr) {
    if (mRef == aCFRefPtr.mRef) {
      return;
    }
    if (mRef) {
      CFRelease(mRef);
    }
    mRef = aCFRefPtr.mRef;
    if (mRef) {
      CFRetain(mRef);
    }
    return *this;
  }
  ~CFRefPtr() {
    if (mRef) {
      CFRelease(mRef);
    }
  }
  // Return the wrapped ref so it can be used as an in parameter.
  operator T() { return mRef; }

 private:
  T mRef;
};

template <typename T>
struct AutoTypeRefTraits;

enum class AutoTypePolicy { Retain, NoRetain };
template <typename T, typename Traits = AutoTypeRefTraits<T>>
class AutoTypeRef {
 public:
  explicit AutoTypeRef(T aObj = Traits::InvalidValue(),
                       AutoTypePolicy aPolicy = AutoTypePolicy::NoRetain)
      : mObj(aObj) {
    if (mObj != Traits::InvalidValue()) {
      if (aPolicy == AutoTypePolicy::Retain) {
        mObj = Traits::Retain(mObj);
      }
    }
  }

  ~AutoTypeRef() { ReleaseIfNeeded(); }

  // Copy constructor
  AutoTypeRef(const AutoTypeRef<T, Traits>& aOther) : mObj(aOther.mObj) {
    if (mObj != Traits::InvalidValue()) {
      mObj = Traits::Retain(mObj);
    }
  }

  // Copy assignment
  AutoTypeRef<T, Traits>& operator=(const AutoTypeRef<T, Traits>& aOther) {
    ReleaseIfNeeded();
    mObj = aOther.mObj;
    if (mObj != Traits::InvalidValue()) {
      mObj = Traits::Retain(mObj);
    }
    return *this;
  }

  // Move constructor
  AutoTypeRef(AutoTypeRef<T, Traits>&& aOther) : mObj(aOther.mObj) {
    aOther.mObj = Traits::InvalidValue();
  }

  // Move assignment
  AutoTypeRef<T, Traits>& operator=(const AutoTypeRef<T, Traits>&& aOther) {
    ReleaseIfNeeded();
    mObj = aOther.mObj;
    aOther.mObj = Traits::InvalidValue();
    return *this;
  }

  explicit operator bool() const { return mObj != Traits::InvalidValue(); }

  operator T() { return mObj; }

  T& Ref() { return mObj; }

  T* Receive() {
    MOZ_ASSERT(mObj == Traits::InvalidValue(),
               "Receive() should only be called for uninitialized objects");
    return &mObj;
  }

  void Reset(T aObj = Traits::InvalidValue(),
             AutoTypePolicy aPolicy = AutoTypePolicy::NoRetain) {
    ReleaseIfNeeded();
    mObj = aObj;
    if (mObj != Traits::InvalidValue()) {
      if (aPolicy == AutoTypePolicy::Retain) {
        mObj = Traits::Retain(mObj);
      } else {
        mObj = aObj;
      }
    }
  }

 private:
  void ReleaseIfNeeded() {
    if (mObj != Traits::InvalidValue()) {
      Traits::Release(mObj);
      mObj = Traits::InvalidValue();
    }
  }
  T mObj;
};

template <typename CFT>
struct CFTypeRefTraits {
  static CFT InvalidValue() { return nullptr; }
  static CFT Retain(CFT aObject) {
    CFRetain(aObject);
    return aObject;
  }
  static void Release(CFT aObject) { CFRelease(aObject); }
};

template <typename CVB>
struct CVBufferRefTraits {
  static CVB InvalidValue() { return nullptr; }
  static CVB Retain(CVB aObject) {
    CVBufferRetain(aObject);
    return aObject;
  }
  static void Release(CVB aObject) { CVBufferRelease(aObject); }
};

template <typename CFT>
using AutoCFTypeRef = AutoTypeRef<CFT, CFTypeRefTraits<CFT>>;
template <typename CVB>
using AutoCVBufferRef = AutoTypeRef<CVB, CVBufferRefTraits<CVB>>;

class MOZ_RAII SessionPropertyManager {
 public:
  explicit SessionPropertyManager(
      const AutoCFTypeRef<VTCompressionSessionRef>& aSession);
  explicit SessionPropertyManager(const VTCompressionSessionRef& aSession);
  ~SessionPropertyManager() = default;

  bool IsSupported(CFStringRef aKey);

  OSStatus Set(CFStringRef aKey, int32_t aValue);
  OSStatus Set(CFStringRef aKey, int64_t aValue);
  OSStatus Set(CFStringRef aKey, float aValue);
  OSStatus Set(CFStringRef aKey, bool aValue);
  OSStatus Set(CFStringRef aKey, CFStringRef value);
  OSStatus Copy(CFStringRef aKey, bool& aValue);

 private:
  template <typename V>
  OSStatus Set(CFStringRef aKey, V aValue, CFNumberType aType);

  AutoCFTypeRef<VTCompressionSessionRef> mSession;
  AutoCFTypeRef<CFDictionaryRef> mSupportedKeys;
};

}  // namespace mozilla

#endif  // mozilla_AppleUtils_h
