/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */
// IWYU pragma: private, include "nsISupports.h"

#ifndef nsISupportsImpl_h__
#define nsISupportsImpl_h__

#include "nscore.h"
#include "nsISupports.h"
#include "nsISupportsUtils.h"

#if !defined(XPCOM_GLUE_AVOID_NSPR)
#  include "prthread.h" /* needed for cargo-culting headers */
#endif

#include "nsDebug.h"
#include "nsXPCOM.h"
#include <atomic>
#include <type_traits>
#include "mozilla/Attributes.h"
#include "mozilla/Assertions.h"
#include "mozilla/Atomics.h"
#include "mozilla/Compiler.h"
#include "mozilla/Likely.h"
#include "mozilla/MacroArgs.h"
#include "mozilla/MacroForEach.h"

#define MOZ_ASSERT_TYPE_OK_FOR_REFCOUNTING(X)            \
  static_assert(!std::is_destructible_v<X>,              \
                "Reference-counted class " #X            \
                " should not have a public destructor. " \
                "Make this class's destructor non-public");

inline nsISupports* ToSupports(decltype(nullptr)) { return nullptr; }

inline nsISupports* ToSupports(nsISupports* aSupports) { return aSupports; }

////////////////////////////////////////////////////////////////////////////////
// Macros to help detect thread-safety:

#ifdef MOZ_THREAD_SAFETY_OWNERSHIP_CHECKS_SUPPORTED

#  include "prthread.h" /* needed for thread-safety checks */

class nsAutoOwningThread {
 public:
  nsAutoOwningThread();

  // We move the actual assertion checks out-of-line to minimize code bloat,
  // but that means we have to pass a non-literal string to MOZ_CRASH_UNSAFE.
  // To make that more safe, the public interface requires a literal string
  // and passes that to the private interface; we can then be assured that we
  // effectively are passing a literal string to MOZ_CRASH_UNSAFE.
  template <int N>
  void AssertOwnership(const char (&aMsg)[N]) const {
    AssertCurrentThreadOwnsMe(aMsg);
  }

  bool IsCurrentThread() const;

 private:
  void AssertCurrentThreadOwnsMe(const char* aMsg) const;

  void* mThread;
};

class nsISerialEventTarget;
class nsAutoOwningEventTarget {
 public:
  nsAutoOwningEventTarget();

  nsAutoOwningEventTarget(const nsAutoOwningEventTarget& aOther);

  // Per https://en.cppreference.com/w/cpp/language/move_constructor
  // there's no implicitly-declared move constructor if there are user-declared
  // copy constructors, and we have one, immediately above.

  nsAutoOwningEventTarget& operator=(const nsAutoOwningEventTarget& aRhs);

  // Per https://en.cppreference.com/w/cpp/language/move_assignment
  // there's no implicitly-declared move assignment operator if there are
  // user-declared copy assignment operators, and we have one, immediately
  // above.

  ~nsAutoOwningEventTarget();

  // We move the actual assertion checks out-of-line to minimize code bloat,
  // but that means we have to pass a non-literal string to MOZ_CRASH_UNSAFE.
  // To make that more safe, the public interface requires a literal string
  // and passes that to the private interface; we can then be assured that we
  // effectively are passing a literal string to MOZ_CRASH_UNSAFE.
  template <int N>
  void AssertOwnership(const char (&aMsg)[N]) const {
    AssertCurrentThreadOwnsMe(aMsg);
  }

  bool IsCurrentThread() const;

 private:
  void AssertCurrentThreadOwnsMe(const char* aMsg) const;

  // A raw pointer to avoid nsCOMPtr.h dependency.
  nsISerialEventTarget* mTarget;
};

#  define NS_DECL_OWNINGTHREAD nsAutoOwningThread _mOwningThread;
#  define NS_DECL_OWNINGEVENTTARGET nsAutoOwningEventTarget _mOwningThread;
#  define NS_ASSERT_OWNINGTHREAD(_class) \
    _mOwningThread.AssertOwnership(#_class " not thread-safe")
#else  // !MOZ_THREAD_SAFETY_OWNERSHIP_CHECKS_SUPPORTED

#  define NS_DECL_OWNINGTHREAD      /* nothing */
#  define NS_DECL_OWNINGEVENTTARGET /* nothing */
#  define NS_ASSERT_OWNINGTHREAD(_class) ((void)0)

#endif  // MOZ_THREAD_SAFETY_OWNERSHIP_CHECKS_SUPPORTED

// Macros for reference-count and constructor logging

#if defined(NS_BUILD_REFCNT_LOGGING)

#  define NS_LOG_ADDREF(_p, _rc, _type, _size) \
    NS_LogAddRef((_p), (_rc), (_type), (uint32_t)(_size))

#  define NS_LOG_RELEASE(_p, _rc, _type) NS_LogRelease((_p), (_rc), (_type))

#  define MOZ_ASSERT_CLASSNAME(_type)     \
    static_assert(std::is_class_v<_type>, \
                  "Token '" #_type "' is not a class type.")

#  define MOZ_ASSERT_NOT_ISUPPORTS(_type)                                     \
    static_assert(!std::is_base_of<nsISupports, _type>::value,                \
                  "nsISupports classes don't need to call MOZ_COUNT_CTOR or " \
                  "MOZ_COUNT_DTOR");

// Note that the following constructor/destructor logging macros are redundant
// for refcounted objects that log via the NS_LOG_ADDREF/NS_LOG_RELEASE macros.
// Refcount logging is preferred.
#  define MOZ_COUNT_CTOR(_type)                       \
    do {                                              \
      MOZ_ASSERT_CLASSNAME(_type);                    \
      MOZ_ASSERT_NOT_ISUPPORTS(_type);                \
      NS_LogCtor((void*)this, #_type, sizeof(*this)); \
    } while (0)

#  define MOZ_COUNT_CTOR_INHERITED(_type, _base)                      \
    do {                                                              \
      MOZ_ASSERT_CLASSNAME(_type);                                    \
      MOZ_ASSERT_CLASSNAME(_base);                                    \
      MOZ_ASSERT_NOT_ISUPPORTS(_type);                                \
      NS_LogCtor((void*)this, #_type, sizeof(*this) - sizeof(_base)); \
    } while (0)

#  define MOZ_LOG_CTOR(_ptr, _name, _size)   \
    do {                                     \
      NS_LogCtor((void*)_ptr, _name, _size); \
    } while (0)

#  define MOZ_COUNT_DTOR(_type)                       \
    do {                                              \
      MOZ_ASSERT_CLASSNAME(_type);                    \
      MOZ_ASSERT_NOT_ISUPPORTS(_type);                \
      NS_LogDtor((void*)this, #_type, sizeof(*this)); \
    } while (0)

#  define MOZ_COUNT_DTOR_INHERITED(_type, _base)                      \
    do {                                                              \
      MOZ_ASSERT_CLASSNAME(_type);                                    \
      MOZ_ASSERT_CLASSNAME(_base);                                    \
      MOZ_ASSERT_NOT_ISUPPORTS(_type);                                \
      NS_LogDtor((void*)this, #_type, sizeof(*this) - sizeof(_base)); \
    } while (0)

#  define MOZ_LOG_DTOR(_ptr, _name, _size)   \
    do {                                     \
      NS_LogDtor((void*)_ptr, _name, _size); \
    } while (0)

#  define MOZ_COUNTED_DEFAULT_CTOR(_type) \
    _type() { MOZ_COUNT_CTOR(_type); }

#  define MOZ_COUNTED_DTOR_META(_type, _prefix, _postfix) \
    _prefix ~_type() _postfix { MOZ_COUNT_DTOR(_type); }
#  define MOZ_COUNTED_DTOR_NESTED(_type, _nestedName) \
    ~_type() { MOZ_COUNT_DTOR(_nestedName); }

/* nsCOMPtr.h allows these macros to be defined by clients
 * These logging functions require dynamic_cast<void*>, so they don't
 * do anything useful if we don't have dynamic_cast<void*>.
 * Note: The explicit comparison to nullptr is needed to avoid warnings
 *       when _p is a nullptr itself. */
#  define NSCAP_LOG_ASSIGNMENT(_c, _p) \
    if (_p != nullptr) NS_LogCOMPtrAddRef((_c), ToSupports(_p))

#  define NSCAP_LOG_RELEASE(_c, _p) \
    if (_p) NS_LogCOMPtrRelease((_c), ToSupports(_p))

#else /* !NS_BUILD_REFCNT_LOGGING */

#  define NS_LOG_ADDREF(_p, _rc, _type, _size)
#  define NS_LOG_RELEASE(_p, _rc, _type)
#  define MOZ_COUNT_CTOR(_type)
#  define MOZ_COUNT_CTOR_INHERITED(_type, _base)
#  define MOZ_LOG_CTOR(_ptr, _name, _size)
#  define MOZ_COUNT_DTOR(_type)
#  define MOZ_COUNT_DTOR_INHERITED(_type, _base)
#  define MOZ_LOG_DTOR(_ptr, _name, _size)
#  define MOZ_COUNTED_DEFAULT_CTOR(_type) _type() = default;
#  define MOZ_COUNTED_DTOR_META(_type, _prefix, _postfix) \
    _prefix ~_type() _postfix = default;
#  define MOZ_COUNTED_DTOR_NESTED(_type, _nestedName) ~_type() = default;

#endif /* NS_BUILD_REFCNT_LOGGING */

#define MOZ_COUNTED_DTOR(_type) MOZ_COUNTED_DTOR_META(_type, , )
#define MOZ_COUNTED_DTOR_OVERRIDE(_type) \
  MOZ_COUNTED_DTOR_META(_type, , override)
#define MOZ_COUNTED_DTOR_FINAL(_type) MOZ_COUNTED_DTOR_META(_type, , final)
#define MOZ_COUNTED_DTOR_VIRTUAL(_type) MOZ_COUNTED_DTOR_META(_type, virtual, )

// Support for ISupports classes which interact with cycle collector.

#ifdef HAVE_64BIT_BUILD
#  define NS_NUMBER_OF_FLAGS_IN_REFCNT 3
#  define NS_IS_ON_MAINTHREAD (1 << 2)
#else
#  define NS_NUMBER_OF_FLAGS_IN_REFCNT 2
#endif

#define NS_IN_PURPLE_BUFFER (1 << 0)
#define NS_IS_PURPLE (1 << 1)
#define NS_REFCOUNT_CHANGE (1 << NS_NUMBER_OF_FLAGS_IN_REFCNT)
#define NS_REFCOUNT_VALUE(_val) (_val >> NS_NUMBER_OF_FLAGS_IN_REFCNT)

class nsCycleCollectingAutoRefCnt {
 public:
  typedef void (*Suspect)(void* aPtr, nsCycleCollectionParticipant* aCp,
                          nsCycleCollectingAutoRefCnt* aRefCnt,
                          bool* aShouldDelete);

  constexpr nsCycleCollectingAutoRefCnt() : mRefCntAndFlags(0) {}

  constexpr explicit nsCycleCollectingAutoRefCnt(uintptr_t aValue)
      : mRefCntAndFlags(aValue << NS_NUMBER_OF_FLAGS_IN_REFCNT) {}

  nsCycleCollectingAutoRefCnt(const nsCycleCollectingAutoRefCnt&) = delete;
  void operator=(const nsCycleCollectingAutoRefCnt&) = delete;

  MOZ_ALWAYS_INLINE uintptr_t incr(nsISupports* aOwner) {
    return incr(aOwner, nullptr);
  }

  MOZ_ALWAYS_INLINE uintptr_t incr(void* aOwner,
                                   nsCycleCollectionParticipant* aCp) {
    mRefCntAndFlags += NS_REFCOUNT_CHANGE;
    mRefCntAndFlags &= ~NS_IS_PURPLE;
    // For incremental cycle collection, use the purple buffer to track objects
    // that have been AddRef'd.
    if (!IsInPurpleBuffer()) {
      mRefCntAndFlags |= NS_IN_PURPLE_BUFFER;
      // Refcount isn't zero, so Suspect won't delete anything.
      MOZ_ASSERT(get() > 0);
      NS_CycleCollectorSuspect3(aOwner, aCp, this, nullptr);
    }
    return NS_REFCOUNT_VALUE(mRefCntAndFlags);
  }

  MOZ_ALWAYS_INLINE void stabilizeForDeletion() {
    // Set refcnt to 1 and mark us to be in the purple buffer.
    // This way decr won't call suspect again.
    mRefCntAndFlags = NS_REFCOUNT_CHANGE | NS_IN_PURPLE_BUFFER;
  }

  MOZ_ALWAYS_INLINE uintptr_t decr(nsISupports* aOwner,
                                   bool* aShouldDelete = nullptr) {
    return decr(aOwner, nullptr, aShouldDelete);
  }

  MOZ_ALWAYS_INLINE uintptr_t decr(void* aOwner,
                                   nsCycleCollectionParticipant* aCp,
                                   bool* aShouldDelete = nullptr) {
    MOZ_ASSERT(get() > 0);
    if (!IsInPurpleBuffer()) {
      mRefCntAndFlags -= NS_REFCOUNT_CHANGE;
      mRefCntAndFlags |= (NS_IN_PURPLE_BUFFER | NS_IS_PURPLE);
      uintptr_t retval = NS_REFCOUNT_VALUE(mRefCntAndFlags);
      // Suspect may delete 'aOwner' and 'this'!
      NS_CycleCollectorSuspect3(aOwner, aCp, this, aShouldDelete);
      return retval;
    }
    mRefCntAndFlags -= NS_REFCOUNT_CHANGE;
    mRefCntAndFlags |= (NS_IN_PURPLE_BUFFER | NS_IS_PURPLE);
    return NS_REFCOUNT_VALUE(mRefCntAndFlags);
  }

  MOZ_ALWAYS_INLINE void RemovePurple() {
    MOZ_ASSERT(IsPurple(), "must be purple");
    mRefCntAndFlags &= ~NS_IS_PURPLE;
  }

  MOZ_ALWAYS_INLINE void RemoveFromPurpleBuffer() {
    MOZ_ASSERT(IsInPurpleBuffer());
    mRefCntAndFlags &= ~(NS_IS_PURPLE | NS_IN_PURPLE_BUFFER);
  }

  MOZ_ALWAYS_INLINE bool IsPurple() const {
    return !!(mRefCntAndFlags & NS_IS_PURPLE);
  }

  MOZ_ALWAYS_INLINE bool IsInPurpleBuffer() const {
    return !!(mRefCntAndFlags & NS_IN_PURPLE_BUFFER);
  }

  // Cycle collected objects can only be used on a single thread, so if we ever
  // see an object on the main thread, it will always be on the main thread.
  // The cycle collector then uses this information to utilize faster nursery
  // purple buffer on the main thread.
  // The method works only on 64 bit systems.
  MOZ_ALWAYS_INLINE void SetIsOnMainThread() {
#ifdef HAVE_64BIT_BUILD
    mRefCntAndFlags |= NS_IS_ON_MAINTHREAD;
#endif
  }

#ifdef HAVE_64BIT_BUILD
  // The NS_IS_ON_MAINTHREAD flag only exists on 64-bit builds.
  MOZ_ALWAYS_INLINE bool IsOnMainThread() {
    return !!(mRefCntAndFlags & NS_IS_ON_MAINTHREAD);
  }
#endif

  MOZ_ALWAYS_INLINE nsrefcnt get() const {
    return NS_REFCOUNT_VALUE(mRefCntAndFlags);
  }

  MOZ_ALWAYS_INLINE operator nsrefcnt() const { return get(); }

 private:
  uintptr_t mRefCntAndFlags;
};

class nsAutoRefCnt {
 public:
  constexpr nsAutoRefCnt() : mValue(0) {}
  constexpr explicit nsAutoRefCnt(nsrefcnt aValue) : mValue(aValue) {}

  nsAutoRefCnt(const nsAutoRefCnt&) = delete;
  void operator=(const nsAutoRefCnt&) = delete;

  // only support prefix increment/decrement
  nsrefcnt operator++() { return ++mValue; }
  nsrefcnt operator--() { return --mValue; }

  nsrefcnt operator=(nsrefcnt aValue) { return (mValue = aValue); }
  operator nsrefcnt() const { return mValue; }
  nsrefcnt get() const { return mValue; }

  static const bool isThreadSafe = false;

 private:
  nsrefcnt operator++(int) = delete;
  nsrefcnt operator--(int) = delete;
  nsrefcnt mValue;
};

namespace mozilla {
class ThreadSafeAutoRefCnt {
 public:
  constexpr ThreadSafeAutoRefCnt() : mValue(0) {}
  constexpr explicit ThreadSafeAutoRefCnt(nsrefcnt aValue) : mValue(aValue) {}

  ThreadSafeAutoRefCnt(const ThreadSafeAutoRefCnt&) = delete;
  void operator=(const ThreadSafeAutoRefCnt&) = delete;

  // only support prefix increment/decrement
  MOZ_ALWAYS_INLINE nsrefcnt operator++() {
    // Memory synchronization is not required when incrementing a
    // reference count.  The first increment of a reference count on a
    // thread is not important, since the first use of the object on a
    // thread can happen before it.  What is important is the transfer
    // of the pointer to that thread, which may happen prior to the
    // first increment on that thread.  The necessary memory
    // synchronization is done by the mechanism that transfers the
    // pointer between threads.
    return mValue.fetch_add(1, std::memory_order_relaxed) + 1;
  }
  MOZ_ALWAYS_INLINE nsrefcnt operator--() {
    // Since this may be the last release on this thread, we need
    // release semantics so that prior writes on this thread are visible
    // to the thread that destroys the object when it reads mValue with
    // acquire semantics.
    nsrefcnt result = mValue.fetch_sub(1, std::memory_order_release) - 1;
    if (result == 0) {
      // We're going to destroy the object on this thread, so we need
      // acquire semantics to synchronize with the memory released by
      // the last release on other threads, that is, to ensure that
      // writes prior to that release are now visible on this thread.
#ifdef MOZ_TSAN
      // TSan doesn't understand std::atomic_thread_fence, so in order
      // to avoid a false positive for every time a refcounted object
      // is deleted, we replace the fence with an atomic operation.
      mValue.load(std::memory_order_acquire);
#else
      std::atomic_thread_fence(std::memory_order_acquire);
#endif
    }
    return result;
  }

  MOZ_ALWAYS_INLINE nsrefcnt operator=(nsrefcnt aValue) {
    // Use release semantics since we're not sure what the caller is
    // doing.
    mValue.store(aValue, std::memory_order_release);
    return aValue;
  }
  MOZ_ALWAYS_INLINE operator nsrefcnt() const { return get(); }
  MOZ_ALWAYS_INLINE nsrefcnt get() const {
    // Use acquire semantics since we're not sure what the caller is
    // doing.
    return mValue.load(std::memory_order_acquire);
  }

  static const bool isThreadSafe = true;

 private:
  nsrefcnt operator++(int) = delete;
  nsrefcnt operator--(int) = delete;
  std::atomic<nsrefcnt> mValue;
};

namespace detail {

// Type trait indicating whether a given XPCOM interface class may only be
// implemented by types with threadsafe refcounts. This is specialized for
// classes with the `rust_sync` annotation within XPIDL-generated header files,
// and checked within macro-generated QueryInterface implementations.
template <typename T>
class InterfaceNeedsThreadSafeRefCnt : public std::false_type {};

}
}  // namespace mozilla

///////////////////////////////////////////////////////////////////////////////

/**
 * Declare the reference count variable and the implementations of the
 * AddRef and QueryInterface methods.
 */

#define NS_DECL_ISUPPORTS                                                 \
 public:                                                                  \
  NS_IMETHOD QueryInterface(REFNSIID aIID, void** aInstancePtr) override; \
  NS_IMETHOD_(MozExternalRefCountType) AddRef(void) override;             \
  NS_IMETHOD_(MozExternalRefCountType) Release(void) override;            \
  using HasThreadSafeRefCnt = std::false_type;                            \
                                                                          \
 protected:                                                               \
  nsAutoRefCnt mRefCnt;                                                   \
  NS_DECL_OWNINGTHREAD                                                    \
 public:

#define NS_DECL_ISUPPORTS_ONEVENTTARGET                                   \
 public:                                                                  \
  NS_IMETHOD QueryInterface(REFNSIID aIID, void** aInstancePtr) override; \
  NS_IMETHOD_(MozExternalRefCountType) AddRef(void) override;             \
  NS_IMETHOD_(MozExternalRefCountType) Release(void) override;            \
  using HasThreadSafeRefCnt = std::false_type;                            \
                                                                          \
 protected:                                                               \
  nsAutoRefCnt mRefCnt;                                                   \
  NS_DECL_OWNINGEVENTTARGET                                               \
 public:

#define NS_DECL_THREADSAFE_ISUPPORTS                                      \
 public:                                                                  \
  NS_IMETHOD QueryInterface(REFNSIID aIID, void** aInstancePtr) override; \
  NS_IMETHOD_(MozExternalRefCountType) AddRef(void) override;             \
  NS_IMETHOD_(MozExternalRefCountType) Release(void) override;            \
  using HasThreadSafeRefCnt = std::true_type;                             \
                                                                          \
 protected:                                                               \
  ::mozilla::ThreadSafeAutoRefCnt mRefCnt;                                \
  NS_DECL_OWNINGTHREAD                                                    \
 public:

#define NS_DECL_CYCLE_COLLECTING_ISUPPORTS          \
  NS_DECL_CYCLE_COLLECTING_ISUPPORTS_META(override) \
  NS_IMETHOD_(void) DeleteCycleCollectable(void);   \
                                                    \
 public:

#define NS_DECL_CYCLE_COLLECTING_ISUPPORTS_FINAL  \
  NS_DECL_CYCLE_COLLECTING_ISUPPORTS_META(final)  \
  NS_IMETHOD_(void) DeleteCycleCollectable(void); \
                                                  \
 public:

#define NS_DECL_CYCLE_COLLECTING_ISUPPORTS_META(...)                         \
 public:                                                                     \
  NS_IMETHOD QueryInterface(REFNSIID aIID, void** aInstancePtr) __VA_ARGS__; \
  NS_IMETHOD_(MozExternalRefCountType) AddRef(void) __VA_ARGS__;             \
  NS_IMETHOD_(MozExternalRefCountType) Release(void) __VA_ARGS__;            \
  using HasThreadSafeRefCnt = std::false_type;                               \
                                                                             \
 protected:                                                                  \
  nsCycleCollectingAutoRefCnt mRefCnt;                                       \
  NS_DECL_OWNINGTHREAD                                                       \
 public:

///////////////////////////////////////////////////////////////////////////////

/*
 * Implementation of AddRef and Release for non-nsISupports (ie "native")
 * cycle-collected classes that use the purple buffer to avoid leaks.
 */

#define NS_IMPL_CC_NATIVE_ADDREF_BODY(_class)                                 \
  MOZ_ASSERT_TYPE_OK_FOR_REFCOUNTING(_class)                                  \
  MOZ_ASSERT(int32_t(mRefCnt) >= 0, "illegal refcnt");                        \
  NS_ASSERT_OWNINGTHREAD(_class);                                             \
  nsrefcnt count =                                                            \
      mRefCnt.incr(static_cast<void*>(this),                                  \
                   _class::NS_CYCLE_COLLECTION_INNERCLASS::GetParticipant()); \
  NS_LOG_ADDREF(this, count, #_class, sizeof(*this));                         \
  return count;

#define NS_IMPL_CC_NATIVE_RELEASE_BODY(_class)                                \
  MOZ_ASSERT(int32_t(mRefCnt) > 0, "dup release");                            \
  NS_ASSERT_OWNINGTHREAD(_class);                                             \
  nsrefcnt count =                                                            \
      mRefCnt.decr(static_cast<void*>(this),                                  \
                   _class::NS_CYCLE_COLLECTION_INNERCLASS::GetParticipant()); \
  if (count == 0) {                                                           \
    NS_CycleCollectableHasRefCntZero();                                       \
  }                                                                           \
  NS_LOG_RELEASE(this, count, #_class);                                       \
  return count;

#define NS_IMPL_CYCLE_COLLECTING_NATIVE_ADDREF(_class)       \
  NS_METHOD_(MozExternalRefCountType) _class::AddRef(void) { \
    NS_IMPL_CC_NATIVE_ADDREF_BODY(_class)                    \
  }

#define NS_IMPL_CYCLE_COLLECTING_NATIVE_RELEASE_WITH_LAST_RELEASE(_class,      \
                                                                  _last)       \
  NS_METHOD_(MozExternalRefCountType) _class::Release(void) {                  \
    MOZ_ASSERT(int32_t(mRefCnt) > 0, "dup release");                           \
    NS_ASSERT_OWNINGTHREAD(_class);                                            \
    bool shouldDelete = false;                                                 \
    nsrefcnt count =                                                           \
        mRefCnt.decr(static_cast<void*>(this),                                 \
                     _class::NS_CYCLE_COLLECTION_INNERCLASS::GetParticipant(), \
                     &shouldDelete);                                           \
    NS_LOG_RELEASE(this, count, #_class);                                      \
    if (count == 0) {                                                          \
      mRefCnt.incr(static_cast<void*>(this),                                   \
                   _class::NS_CYCLE_COLLECTION_INNERCLASS::GetParticipant());  \
      _last;                                                                   \
      mRefCnt.decr(static_cast<void*>(this),                                   \
                   _class::NS_CYCLE_COLLECTION_INNERCLASS::GetParticipant());  \
      NS_CycleCollectableHasRefCntZero();                                      \
      if (shouldDelete) {                                                      \
        mRefCnt.stabilizeForDeletion();                                        \
        DeleteCycleCollectable();                                              \
      }                                                                        \
    }                                                                          \
    return count;                                                              \
  }

#define NS_IMPL_CYCLE_COLLECTING_NATIVE_RELEASE(_class)       \
  NS_METHOD_(MozExternalRefCountType) _class::Release(void) { \
    NS_IMPL_CC_NATIVE_RELEASE_BODY(_class)                    \
  }

#define NS_INLINE_DECL_CYCLE_COLLECTING_NATIVE_REFCOUNTING(_class) \
  NS_INLINE_DECL_CYCLE_COLLECTING_NATIVE_REFCOUNTING_META(_class, NS_METHOD_)

#define NS_INLINE_DECL_CYCLE_COLLECTING_NATIVE_REFCOUNTING_VIRTUAL(_class) \
  NS_INLINE_DECL_CYCLE_COLLECTING_NATIVE_REFCOUNTING_META(_class, NS_IMETHOD_)

#define NS_INLINE_DECL_CYCLE_COLLECTING_NATIVE_REFCOUNTING_INHERITED(_class)  \
  NS_INLINE_DECL_CYCLE_COLLECTING_NATIVE_REFCOUNTING_META(_class, NS_METHOD_, \
                                                          override)

#define NS_INLINE_DECL_CYCLE_COLLECTING_NATIVE_REFCOUNTING_META(_class, _decl, \
                                                                ...)           \
 public:                                                                       \
  _decl(MozExternalRefCountType) AddRef(void) __VA_ARGS__{                     \
      NS_IMPL_CC_NATIVE_ADDREF_BODY(_class)} _decl(MozExternalRefCountType)    \
      Release(void) __VA_ARGS__ {                                              \
    NS_IMPL_CC_NATIVE_RELEASE_BODY(_class)                                     \
  }                                                                            \
  using HasThreadSafeRefCnt = std::false_type;                                 \
                                                                               \
 protected:                                                                    \
  nsCycleCollectingAutoRefCnt mRefCnt;                                         \
  NS_DECL_OWNINGTHREAD                                                         \
 public:

///////////////////////////////////////////////////////////////////////////////

/**
 * Use this macro to declare and implement the AddRef & Release methods for a
 * given non-XPCOM <i>_class</i>.
 *
 * @param _class The name of the class implementing the method
 * @param _destroy A statement that is executed when the object's
 *   refcount drops to zero.
 * @param _decl Name of the macro to be used for the return type of the
 *   AddRef & Release methods (typically NS_IMETHOD_ or NS_METHOD_).
 * @param optional override Mark the AddRef & Release methods as overrides.
 */
#define NS_INLINE_DECL_REFCOUNTING_META(_class, _decl, _destroy, _owning, ...) \
 public:                                                                       \
  _decl(MozExternalRefCountType) AddRef(void) __VA_ARGS__ {                    \
    MOZ_ASSERT_TYPE_OK_FOR_REFCOUNTING(_class)                                 \
    MOZ_ASSERT(int32_t(mRefCnt) >= 0, "illegal refcnt");                       \
    NS_ASSERT_OWNINGTHREAD(_class);                                            \
    ++mRefCnt;                                                                 \
    NS_LOG_ADDREF(this, mRefCnt, #_class, sizeof(*this));                      \
    return mRefCnt;                                                            \
  }                                                                            \
  _decl(MozExternalRefCountType) Release(void) __VA_ARGS__ {                   \
    MOZ_ASSERT(int32_t(mRefCnt) > 0, "dup release");                           \
    NS_ASSERT_OWNINGTHREAD(_class);                                            \
    --mRefCnt;                                                                 \
    NS_LOG_RELEASE(this, mRefCnt, #_class);                                    \
    if (mRefCnt == 0) {                                                        \
      mRefCnt = 1; /* stabilize */                                             \
      _destroy;                                                                \
      return 0;                                                                \
    }                                                                          \
    return mRefCnt;                                                            \
  }                                                                            \
  using HasThreadSafeRefCnt = std::false_type;                                 \
                                                                               \
 protected:                                                                    \
  nsAutoRefCnt mRefCnt;                                                        \
 _owning public:

/**
 * Use this macro to declare and implement the AddRef & Release methods for a
 * given non-XPCOM <i>_class</i>.
 *
 * @param _class The name of the class implementing the method
 * @param _destroy A statement that is executed when the object's
 *   refcount drops to zero.
 * @param optional override Mark the AddRef & Release methods as overrides.
 */
#define NS_INLINE_DECL_REFCOUNTING_WITH_DESTROY(_class, _destroy, ...) \
  NS_INLINE_DECL_REFCOUNTING_META(_class, NS_METHOD_, _destroy,        \
                                  NS_DECL_OWNINGTHREAD, __VA_ARGS__)

/**
 * Like NS_INLINE_DECL_REFCOUNTING_WITH_DESTROY with AddRef & Release declared
 * virtual.
 */
#define NS_INLINE_DECL_VIRTUAL_REFCOUNTING_WITH_DESTROY(_class, _destroy, ...) \
  NS_INLINE_DECL_REFCOUNTING_META(_class, NS_IMETHOD_, _destroy,               \
                                  NS_DECL_OWNINGTHREAD, __VA_ARGS__)

/**
 * Use this macro to declare and implement the AddRef & Release methods for a
 * given non-XPCOM <i>_class</i>.
 *
 * @param _class The name of the class implementing the method
 * @param optional override Mark the AddRef & Release methods as overrides.
 */
#define NS_INLINE_DECL_REFCOUNTING(_class, ...) \
  NS_INLINE_DECL_REFCOUNTING_WITH_DESTROY(_class, delete (this), __VA_ARGS__)

/**
 * Like NS_INLINE_DECL_REFCOUNTING, however the thread safety check will work
 * with any nsISerialEventTarget. This is a workaround until bug 1648031 is
 * properly resolved. Once this is done, it will be possible to use
 * NS_INLINE_DECL_REFCOUNTING under all circumstances.
 */
#define NS_INLINE_DECL_REFCOUNTING_ONEVENTTARGET(_class, ...)        \
  NS_INLINE_DECL_REFCOUNTING_META(_class, NS_METHOD_, delete (this), \
                                  NS_DECL_OWNINGEVENTTARGET, __VA_ARGS__)

#define NS_INLINE_DECL_THREADSAFE_REFCOUNTING_META(_class, _decl, _destroy, \
                                                   ...)                     \
 public:                                                                    \
  _decl(MozExternalRefCountType) AddRef(void) __VA_ARGS__ {                 \
    MOZ_ASSERT_TYPE_OK_FOR_REFCOUNTING(_class)                              \
    MOZ_ASSERT(int32_t(mRefCnt) >= 0, "illegal refcnt");                    \
    nsrefcnt count = ++mRefCnt;                                             \
    NS_LOG_ADDREF(this, count, #_class, sizeof(*this));                     \
    return (nsrefcnt)count;                                                 \
  }                                                                         \
  _decl(MozExternalRefCountType) Release(void) __VA_ARGS__ {                \
    MOZ_ASSERT(int32_t(mRefCnt) > 0, "dup release");                        \
    nsrefcnt count = --mRefCnt;                                             \
    NS_LOG_RELEASE(this, count, #_class);                                   \
    if (count == 0) {                                                       \
      _destroy;                                                             \
      return 0;                                                             \
    }                                                                       \
    return count;                                                           \
  }                                                                         \
  using HasThreadSafeRefCnt = std::true_type;                               \
                                                                            \
 protected:                                                                 \
  ::mozilla::ThreadSafeAutoRefCnt mRefCnt;                                  \
                                                                            \
 public:

/**
 * Use this macro to declare and implement the AddRef & Release methods for a
 * given non-XPCOM <i>_class</i> in a threadsafe manner.
 *
 * DOES NOT DO REFCOUNT STABILIZATION!
 *
 * @param _class The name of the class implementing the method
 * @param _destroy A statement that is executed when the object's
 *   refcount drops to zero.
 * @param optional override Mark the AddRef & Release methods as overrides.
 */
#define NS_INLINE_DECL_THREADSAFE_REFCOUNTING_WITH_DESTROY(_class, _destroy, \
                                                           ...)              \
  NS_INLINE_DECL_THREADSAFE_REFCOUNTING_META(_class, NS_METHOD_, _destroy,   \
                                             __VA_ARGS__)

/**
 * Like NS_INLINE_DECL_THREADSAFE_REFCOUNTING_WITH_DESTROY with AddRef & Release
 * declared virtual.
 */
#define NS_INLINE_DECL_THREADSAFE_VIRTUAL_REFCOUNTING_WITH_DESTROY(         \
    _class, _destroy, ...)                                                  \
  NS_INLINE_DECL_THREADSAFE_REFCOUNTING_META(_class, NS_IMETHOD_, _destroy, \
                                             __VA_ARGS__)

/**
 * Use this macro to declare and implement the AddRef & Release methods for a
 * given non-XPCOM <i>_class</i> in a threadsafe manner.
 *
 * DOES NOT DO REFCOUNT STABILIZATION!
 *
 * @param _class The name of the class implementing the method
 * @param optional override Mark the AddRef & Release methods as overrides.
 */
#define NS_INLINE_DECL_THREADSAFE_REFCOUNTING(_class, ...)                  \
  NS_INLINE_DECL_THREADSAFE_REFCOUNTING_WITH_DESTROY(_class, delete (this), \
                                                     __VA_ARGS__)

/**
 * Like NS_INLINE_DECL_THREADSAFE_REFCOUNTING with AddRef & Release declared
 * virtual.
 */
#define NS_INLINE_DECL_THREADSAFE_VIRTUAL_REFCOUNTING(_class, ...) \
  NS_INLINE_DECL_THREADSAFE_VIRTUAL_REFCOUNTING_WITH_DESTROY(      \
      _class, delete (this), __VA_ARGS__)

#if !defined(XPCOM_GLUE_AVOID_NSPR)
class nsISerialEventTarget;
namespace mozilla {
// Forward-declare `GetMainThreadSerialEventTarget`, as `nsISupportsImpl.h`
// cannot include `nsThreadUtils.h`.
nsISerialEventTarget* GetMainThreadSerialEventTarget();

namespace detail {
using DeleteVoidFunction = void(void*);
void ProxyDeleteVoid(const char* aRunnableName,
                     nsISerialEventTarget* aEventTarget, void* aSelf,
                     DeleteVoidFunction* aDeleteFunc);
}  // namespace detail
}  // namespace mozilla

/**
 * Helper for _WITH_DELETE_ON_EVENT_TARGET threadsafe refcounting macros which
 * provides an implementation of `_destroy`
 */
#  define NS_PROXY_DELETE_TO_EVENT_TARGET(_class, _target) \
    ::mozilla::detail::ProxyDeleteVoid(                    \
        "ProxyDelete " #_class, _target, this,             \
        [](void* self) { delete static_cast<_class*>(self); })

/**
 * Use this macro to declare and implement the AddRef & Release methods for a
 * given non-XPCOM <i>_class</i> in a threadsafe manner, ensuring the
 * destructor runs on a specific nsISerialEventTarget.
 *
 * DOES NOT DO REFCOUNT STABILIZATION!
 *
 * @param _class The name of the class implementing the method
 * @param _target nsISerialEventTarget to run the class's destructor on
 * @param optional override Mark the AddRef & Release methods as overrides
 */
#  define NS_INLINE_DECL_THREADSAFE_REFCOUNTING_WITH_DELETE_ON_EVENT_TARGET( \
      _class, _target, ...)                                                  \
    NS_INLINE_DECL_THREADSAFE_REFCOUNTING_WITH_DESTROY(                      \
        _class, NS_PROXY_DELETE_TO_EVENT_TARGET(_class, _target), __VA_ARGS__)

/**
 * Use this macro to declare and implement the AddRef & Release methods for a
 * given non-XPCOM <i>_class</i> in a threadsafe manner, ensuring the
 * destructor runs on the main thread.
 *
 * DOES NOT DO REFCOUNT STABILIZATION!
 *
 * @param _class The name of the class implementing the method
 * @param optional override Mark the AddRef & Release methods as overrides
 */
#  define NS_INLINE_DECL_THREADSAFE_REFCOUNTING_WITH_DELETE_ON_MAIN_THREAD( \
      _class, ...)                                                          \
    NS_INLINE_DECL_THREADSAFE_REFCOUNTING_WITH_DELETE_ON_EVENT_TARGET(      \
        _class, ::mozilla::GetMainThreadSerialEventTarget(), __VA_ARGS__)
#endif

/**
 * Use this macro in interface classes that you want to be able to reference
 * using RefPtr, but don't want to provide a refcounting implemenation. The
 * refcounting implementation can be provided by concrete subclasses that
 * implement the interface.
 */
#define NS_INLINE_DECL_PURE_VIRTUAL_REFCOUNTING           \
 public:                                                  \
  NS_IMETHOD_(MozExternalRefCountType) AddRef(void) = 0;  \
  NS_IMETHOD_(MozExternalRefCountType) Release(void) = 0; \
                                                          \
 public:

/**
 * Use this macro to implement the AddRef method for a given <i>_class</i>
 * @param _class The name of the class implementing the method
 * @param _name The class name to be passed to XPCOM leak checking
 */
#define NS_IMPL_NAMED_ADDREF(_class, _name)                      \
  NS_IMETHODIMP_(MozExternalRefCountType) _class::AddRef(void) { \
    MOZ_ASSERT_TYPE_OK_FOR_REFCOUNTING(_class)                   \
    MOZ_ASSERT(int32_t(mRefCnt) >= 0, "illegal refcnt");         \
    MOZ_ASSERT(_name != nullptr, "Must specify a name");         \
    if (!mRefCnt.isThreadSafe) NS_ASSERT_OWNINGTHREAD(_class);   \
    nsrefcnt count = ++mRefCnt;                                  \
    NS_LOG_ADDREF(this, count, _name, sizeof(*this));            \
    return count;                                                \
  }

/**
 * Use this macro to implement the AddRef method for a given <i>_class</i>
 * @param _class The name of the class implementing the method
 */
#define NS_IMPL_ADDREF(_class) NS_IMPL_NAMED_ADDREF(_class, #_class)

/**
 * Use this macro to implement the AddRef method for a given <i>_class</i>
 * implemented as a wholly owned aggregated object intended to implement
 * interface(s) for its owner
 * @param _class The name of the class implementing the method
 * @param _aggregator the owning/containing object
 */
#define NS_IMPL_ADDREF_USING_AGGREGATOR(_class, _aggregator)     \
  NS_IMETHODIMP_(MozExternalRefCountType) _class::AddRef(void) { \
    MOZ_ASSERT_TYPE_OK_FOR_REFCOUNTING(_class)                   \
    MOZ_ASSERT(_aggregator, "null aggregator");                  \
    return (_aggregator)->AddRef();                              \
  }

// We decrement the refcnt before logging the actual release, but when logging
// named things, accessing the name may not be valid after the refcnt
// decrement, because the object may have been destroyed on a different thread.
// Use this macro to ensure that we have a local copy of the name prior to
// the refcnt decrement.  (We use a macro to make absolutely sure the name
// isn't loaded in builds where it wouldn't be used.)
#ifdef NS_BUILD_REFCNT_LOGGING
#  define NS_LOAD_NAME_BEFORE_RELEASE(localname, _name) \
    const char* const localname = _name
#else
#  define NS_LOAD_NAME_BEFORE_RELEASE(localname, _name)
#endif

/**
 * Use this macro to implement the Release method for a given
 * <i>_class</i>.
 * @param _class The name of the class implementing the method
 * @param _name The class name to be passed to XPCOM leak checking
 * @param _destroy A statement that is executed when the object's
 *   refcount drops to zero.
 *
 * For example,
 *
 *   NS_IMPL_RELEASE_WITH_DESTROY(Foo, "Foo", Destroy(this))
 *
 * will cause
 *
 *   Destroy(this);
 *
 * to be invoked when the object's refcount drops to zero. This
 * allows for arbitrary teardown activity to occur (e.g., deallocation
 * of object allocated with placement new).
 */
#define NS_IMPL_NAMED_RELEASE_WITH_DESTROY(_class, _name, _destroy) \
  NS_IMETHODIMP_(MozExternalRefCountType) _class::Release(void) {   \
    MOZ_ASSERT(int32_t(mRefCnt) > 0, "dup release");                \
    MOZ_ASSERT(_name != nullptr, "Must specify a name");            \
    if (!mRefCnt.isThreadSafe) NS_ASSERT_OWNINGTHREAD(_class);      \
    NS_LOAD_NAME_BEFORE_RELEASE(nametmp, _name);                    \
    nsrefcnt count = --mRefCnt;                                     \
    NS_LOG_RELEASE(this, count, nametmp);                           \
    if (count == 0) {                                               \
      mRefCnt = 1; /* stabilize */                                  \
      _destroy;                                                     \
      return 0;                                                     \
    }                                                               \
    return count;                                                   \
  }

#define NS_IMPL_RELEASE_WITH_DESTROY(_class, _destroy) \
  NS_IMPL_NAMED_RELEASE_WITH_DESTROY(_class, #_class, _destroy)

/**
 * Use this macro to implement the Release method for a given <i>_class</i>
 * @param _class The name of the class implementing the method
 *
 * A note on the 'stabilization' of the refcnt to one. At that point,
 * the object's refcount will have gone to zero. The object's
 * destructor may trigger code that attempts to QueryInterface() and
 * Release() 'this' again. Doing so will temporarily increment and
 * decrement the refcount. (Only a logic error would make one try to
 * keep a permanent hold on 'this'.)  To prevent re-entering the
 * destructor, we make sure that no balanced refcounting can return
 * the refcount to |0|.
 */
#define NS_IMPL_RELEASE(_class) \
  NS_IMPL_RELEASE_WITH_DESTROY(_class, delete (this))

#define NS_IMPL_NAMED_RELEASE(_class, _name) \
  NS_IMPL_NAMED_RELEASE_WITH_DESTROY(_class, _name, delete (this))

/**
 * Use this macro to implement the Release method for a given <i>_class</i>
 * implemented as a wholly owned aggregated object intended to implement
 * interface(s) for its owner
 * @param _class The name of the class implementing the method
 * @param _aggregator the owning/containing object
 */
#define NS_IMPL_RELEASE_USING_AGGREGATOR(_class, _aggregator)     \
  NS_IMETHODIMP_(MozExternalRefCountType) _class::Release(void) { \
    MOZ_ASSERT(_aggregator, "null aggregator");                   \
    return (_aggregator)->Release();                              \
  }

#define NS_IMPL_CYCLE_COLLECTING_ADDREF(_class)                              \
  NS_IMETHODIMP_(MozExternalRefCountType) _class::AddRef(void) {             \
    MOZ_ASSERT_TYPE_OK_FOR_REFCOUNTING(_class)                               \
    MOZ_ASSERT(int32_t(mRefCnt) >= 0, "illegal refcnt");                     \
    NS_ASSERT_OWNINGTHREAD(_class);                                          \
    nsISupports* base = NS_CYCLE_COLLECTION_CLASSNAME(_class)::Upcast(this); \
    nsrefcnt count = mRefCnt.incr(base);                                     \
    NS_LOG_ADDREF(this, count, #_class, sizeof(*this));                      \
    return count;                                                            \
  }

#define NS_IMPL_CYCLE_COLLECTING_RELEASE_WITH_DESTROY(_class, _destroy)      \
  NS_IMETHODIMP_(MozExternalRefCountType) _class::Release(void) {            \
    MOZ_ASSERT(int32_t(mRefCnt) > 0, "dup release");                         \
    NS_ASSERT_OWNINGTHREAD(_class);                                          \
    nsISupports* base = NS_CYCLE_COLLECTION_CLASSNAME(_class)::Upcast(this); \
    nsrefcnt count = mRefCnt.decr(base);                                     \
    if (count == 0) {                                                        \
      NS_CycleCollectableHasRefCntZero();                                    \
    }                                                                        \
    NS_LOG_RELEASE(this, count, #_class);                                    \
    return count;                                                            \
  }                                                                          \
  NS_IMETHODIMP_(void) _class::DeleteCycleCollectable(void) { _destroy; }

#define NS_IMPL_CYCLE_COLLECTING_RELEASE(_class) \
  NS_IMPL_CYCLE_COLLECTING_RELEASE_WITH_DESTROY(_class, delete (this))

// _LAST_RELEASE can be useful when certain resources should be released
// as soon as we know the object will be deleted.
#define NS_IMPL_CYCLE_COLLECTING_RELEASE_WITH_LAST_RELEASE(_class, _last)    \
  NS_IMETHODIMP_(MozExternalRefCountType) _class::Release(void) {            \
    MOZ_ASSERT(int32_t(mRefCnt) > 0, "dup release");                         \
    NS_ASSERT_OWNINGTHREAD(_class);                                          \
    bool shouldDelete = false;                                               \
    nsISupports* base = NS_CYCLE_COLLECTION_CLASSNAME(_class)::Upcast(this); \
    nsrefcnt count = mRefCnt.decr(base, &shouldDelete);                      \
    NS_LOG_RELEASE(this, count, #_class);                                    \
    if (count == 0) {                                                        \
      mRefCnt.incr(base);                                                    \
      _last;                                                                 \
      mRefCnt.decr(base);                                                    \
      NS_CycleCollectableHasRefCntZero();                                    \
      if (shouldDelete) {                                                    \
        mRefCnt.stabilizeForDeletion();                                      \
        DeleteCycleCollectable();                                            \
      }                                                                      \
    }                                                                        \
    return count;                                                            \
  }                                                                          \
  NS_IMETHODIMP_(void) _class::DeleteCycleCollectable(void) { delete this; }

// This macro is same as NS_IMPL_CYCLE_COLLECTING_RELEASE_WITH_LAST_RELEASE
// except it doesn't have DeleteCycleCollectable.
#define NS_IMPL_CYCLE_COLLECTING_RELEASE_WITH_LAST_RELEASE_AND_DESTROY(      \
    _class, _last, _destroy)                                                 \
  NS_IMETHODIMP_(MozExternalRefCountType) _class::Release(void) {            \
    MOZ_ASSERT(int32_t(mRefCnt) > 0, "dup release");                         \
    NS_ASSERT_OWNINGTHREAD(_class);                                          \
    bool shouldDelete = false;                                               \
    nsISupports* base = NS_CYCLE_COLLECTION_CLASSNAME(_class)::Upcast(this); \
    nsrefcnt count = mRefCnt.decr(base, &shouldDelete);                      \
    NS_LOG_RELEASE(this, count, #_class);                                    \
    if (count == 0) {                                                        \
      mRefCnt.incr(base);                                                    \
      _last;                                                                 \
      mRefCnt.decr(base);                                                    \
      NS_CycleCollectableHasRefCntZero();                                    \
      if (shouldDelete) {                                                    \
        mRefCnt.stabilizeForDeletion();                                      \
        DeleteCycleCollectable();                                            \
      }                                                                      \
    }                                                                        \
    return count;                                                            \
  }                                                                          \
  NS_IMETHODIMP_(void) _class::DeleteCycleCollectable(void) { _destroy; }

// _WITH_INTERRUPTABLE_LAST_RELEASE can be useful when certain resources
// should be released as soon as we know the object will be deleted and the
// instance may be cached for reuse.
// _last is performed for cleaning up its resources.  Then, _maybeInterrupt is
// tested and when it returns true, this stops deleting the instance.
// (Note that it's not allowed to grab the instance with nsCOMPtr or RefPtr
// during _last is performed.)
// Therefore, when _maybeInterrupt returns true, the instance has to be grabbed
// by nsCOMPtr or RefPtr.
#define NS_IMPL_CYCLE_COLLECTING_RELEASE_WITH_INTERRUPTABLE_LAST_RELEASE(    \
    _class, _last, _maybeInterrupt)                                          \
  NS_IMETHODIMP_(MozExternalRefCountType) _class::Release(void) {            \
    MOZ_ASSERT(int32_t(mRefCnt) > 0, "dup release");                         \
    NS_ASSERT_OWNINGTHREAD(_class);                                          \
    bool shouldDelete = false;                                               \
    nsISupports* base = NS_CYCLE_COLLECTION_CLASSNAME(_class)::Upcast(this); \
    nsrefcnt count = mRefCnt.decr(base, &shouldDelete);                      \
    NS_LOG_RELEASE(this, count, #_class);                                    \
    if (count == 0) {                                                        \
      mRefCnt.incr(base);                                                    \
      _last;                                                                 \
      mRefCnt.decr(base);                                                    \
      if (_maybeInterrupt) {                                                 \
        MOZ_ASSERT(mRefCnt.get() > 0);                                       \
        return mRefCnt.get();                                                \
      }                                                                      \
      NS_CycleCollectableHasRefCntZero();                                    \
      if (shouldDelete) {                                                    \
        mRefCnt.stabilizeForDeletion();                                      \
        DeleteCycleCollectable();                                            \
      }                                                                      \
    }                                                                        \
    return count;                                                            \
  }                                                                          \
  NS_IMETHODIMP_(void) _class::DeleteCycleCollectable(void) { delete this; }

///////////////////////////////////////////////////////////////////////////////

namespace mozilla::detail {

// Helper which is roughly equivalent to NS_GET_IID, which also performs static
// assertions that `Class` is allowed to implement the given XPCOM interface.
//
// These assertions are done like this to allow them to be used within the
// `NS_INTERFACE_TABLE_ENTRY` macro, though they are also used in
// `NS_IMPL_QUERY_BODY`.
template <typename Class, typename Interface>
constexpr const nsIID& GetImplementedIID() {
  if constexpr (mozilla::detail::InterfaceNeedsThreadSafeRefCnt<
                    Interface>::value) {
    static_assert(Class::HasThreadSafeRefCnt::value,
                  "Cannot implement a threadsafe interface with "
                  "non-threadsafe refcounting!");
  }
  return NS_GET_TEMPLATE_IID(Interface);
}

template <typename Class, typename Interface>
constexpr const nsIID& kImplementedIID = GetImplementedIID<Class, Interface>();

}

/**
 * There are two ways of implementing QueryInterface, and we use both:
 *
 * Table-driven QueryInterface uses a static table of IID->offset mappings
 * and a shared helper function. Using it tends to reduce codesize and improve
 * runtime performance (due to processor cache hits).
 *
 * Macro-driven QueryInterface generates a QueryInterface function directly
 * using common macros. This is necessary if special QueryInterface features
 * are being used (such as tearoffs and conditional interfaces).
 *
 * These methods can be combined into a table-driven function call followed
 * by custom code for tearoffs and conditionals.
 */

struct QITableEntry {
  const nsIID* iid;  // null indicates end of the QITableEntry array
  int32_t offset;
};

nsresult NS_FASTCALL NS_TableDrivenQI(void* aThis, REFNSIID aIID,
                                      void** aInstancePtr,
                                      const QITableEntry* aEntries);

/**
 * Implement table-driven queryinterface
 */

#define NS_INTERFACE_TABLE_HEAD(_class)                                      \
  NS_IMETHODIMP _class::QueryInterface(REFNSIID aIID, void** aInstancePtr) { \
    NS_ASSERTION(aInstancePtr,                                               \
                 "QueryInterface requires a non-NULL destination!");         \
    nsresult rv = NS_ERROR_FAILURE;

#define NS_INTERFACE_TABLE_BEGIN static const QITableEntry table[] = {
#define NS_INTERFACE_TABLE_ENTRY(_class, _interface)                        \
  {&mozilla::detail::kImplementedIID<_class, _interface>,                   \
   int32_t(                                                                 \
       reinterpret_cast<char*>(static_cast<_interface*>((_class*)0x1000)) - \
       reinterpret_cast<char*>((_class*)0x1000))},

#define NS_INTERFACE_TABLE_ENTRY_AMBIGUOUS(_class, _interface, _implClass) \
  {&mozilla::detail::kImplementedIID<_class, _interface>,                  \
   int32_t(reinterpret_cast<char*>(static_cast<_interface*>(               \
               static_cast<_implClass*>((_class*)0x1000))) -               \
           reinterpret_cast<char*>((_class*)0x1000))},

#define NS_INTERFACE_TABLE_END_WITH_PTR(_ptr)                       \
  { nullptr, 0 }                                                    \
  }                                                                 \
  ;                                                                 \
  static_assert(std::size(table) > 1, "need at least 1 interface"); \
  rv = NS_TableDrivenQI(static_cast<void*>(_ptr), aIID, aInstancePtr, table);

#define NS_INTERFACE_TABLE_END    \
  NS_INTERFACE_TABLE_END_WITH_PTR \
  (this)

#define NS_INTERFACE_TABLE_TAIL \
  return rv;                    \
  }

#define NS_INTERFACE_TABLE_TAIL_INHERITING(_baseclass)   \
  if (NS_SUCCEEDED(rv)) return rv;                       \
  return _baseclass::QueryInterface(aIID, aInstancePtr); \
  }

#define NS_INTERFACE_TABLE_TAIL_USING_AGGREGATOR(_aggregator) \
  if (NS_SUCCEEDED(rv)) return rv;                            \
  NS_ASSERTION(_aggregator, "null aggregator");               \
  return _aggregator->QueryInterface(aIID, aInstancePtr)      \
  }

/**
 * This implements query interface with two assumptions: First, the
 * class in question implements nsISupports and its own interface and
 * nothing else. Second, the implementation of the class's primary
 * inheritance chain leads to its own interface.
 *
 * @param _class The name of the class implementing the method
 * @param _classiiddef The name of the #define symbol that defines the IID
 * for the class (e.g. NS_ISUPPORTS_IID)
 */

#define NS_IMPL_QUERY_HEAD(_class)                                           \
  NS_IMETHODIMP _class::QueryInterface(REFNSIID aIID, void** aInstancePtr) { \
    NS_ASSERTION(aInstancePtr,                                               \
                 "QueryInterface requires a non-NULL destination!");         \
    nsISupports* foundInterface;

#define NS_IMPL_QUERY_BODY_IID(_interface)                                   \
  mozilla::detail::kImplementedIID<std::remove_reference_t<decltype(*this)>, \
                                   _interface>

#define NS_IMPL_QUERY_BODY(_interface)                 \
  if (aIID.Equals(NS_IMPL_QUERY_BODY_IID(_interface))) \
    foundInterface = static_cast<_interface*>(this);   \
  else

#define NS_IMPL_QUERY_BODY_CONDITIONAL(_interface, condition)         \
  if ((condition) && aIID.Equals(NS_IMPL_QUERY_BODY_IID(_interface))) \
    foundInterface = static_cast<_interface*>(this);                  \
  else

#define NS_IMPL_QUERY_BODY_AMBIGUOUS(_interface, _implClass)                   \
  if (aIID.Equals(NS_IMPL_QUERY_BODY_IID(_interface)))                         \
    foundInterface = static_cast<_interface*>(static_cast<_implClass*>(this)); \
  else

// Use this for querying to concrete class types which cannot be unambiguously
// cast to nsISupports. See also nsQueryObject.h.
#define NS_IMPL_QUERY_BODY_CONCRETE(_class)                       \
  if (aIID.Equals(NS_IMPL_QUERY_BODY_IID(_class))) {              \
    *aInstancePtr = do_AddRef(static_cast<_class*>(this)).take(); \
    return NS_OK;                                                 \
  } else

#define NS_IMPL_QUERY_BODY_AGGREGATED(_interface, _aggregate) \
  if (aIID.Equals(NS_IMPL_QUERY_BODY_IID(_interface)))        \
    foundInterface = static_cast<_interface*>(_aggregate);    \
  else

#define NS_IMPL_QUERY_TAIL_GUTS                                      \
  foundInterface = 0;                                                \
  nsresult status;                                                   \
  if (!foundInterface) {                                             \
    /* nsISupports should be handled by this point. If not, fail. */ \
    MOZ_ASSERT(!aIID.Equals(NS_GET_IID(nsISupports)));               \
    status = NS_NOINTERFACE;                                         \
  } else {                                                           \
    NS_ADDREF(foundInterface);                                       \
    status = NS_OK;                                                  \
  }                                                                  \
  *aInstancePtr = foundInterface;                                    \
  return status;                                                     \
  }

#define NS_IMPL_QUERY_TAIL_INHERITING(_baseclass)                       \
  foundInterface = 0;                                                   \
  nsresult status;                                                      \
  if (!foundInterface)                                                  \
    status = _baseclass::QueryInterface(aIID, (void**)&foundInterface); \
  else {                                                                \
    NS_ADDREF(foundInterface);                                          \
    status = NS_OK;                                                     \
  }                                                                     \
  *aInstancePtr = foundInterface;                                       \
  return status;                                                        \
  }

#define NS_IMPL_QUERY_TAIL_USING_AGGREGATOR(_aggregator)                 \
  foundInterface = 0;                                                    \
  nsresult status;                                                       \
  if (!foundInterface) {                                                 \
    NS_ASSERTION(_aggregator, "null aggregator");                        \
    status = _aggregator->QueryInterface(aIID, (void**)&foundInterface); \
  } else {                                                               \
    NS_ADDREF(foundInterface);                                           \
    status = NS_OK;                                                      \
  }                                                                      \
  *aInstancePtr = foundInterface;                                        \
  return status;                                                         \
  }

#define NS_IMPL_QUERY_TAIL(_supports_interface)                  \
  NS_IMPL_QUERY_BODY_AMBIGUOUS(nsISupports, _supports_interface) \
  NS_IMPL_QUERY_TAIL_GUTS

/*
  This is the new scheme.  Using this notation now will allow us to switch to
  a table driven mechanism when it's ready.  Note the difference between this
  and the (currently) underlying NS_IMPL_QUERY_INTERFACE mechanism.  You must
  explicitly mention |nsISupports| when using the interface maps.
*/
#define NS_INTERFACE_MAP_BEGIN(_implClass) NS_IMPL_QUERY_HEAD(_implClass)
#define NS_INTERFACE_MAP_ENTRY(_interface) NS_IMPL_QUERY_BODY(_interface)
#define NS_INTERFACE_MAP_ENTRY_CONDITIONAL(_interface, condition) \
  NS_IMPL_QUERY_BODY_CONDITIONAL(_interface, condition)
#define NS_INTERFACE_MAP_ENTRY_AGGREGATED(_interface, _aggregate) \
  NS_IMPL_QUERY_BODY_AGGREGATED(_interface, _aggregate)

#define NS_INTERFACE_MAP_END NS_IMPL_QUERY_TAIL_GUTS
#define NS_INTERFACE_MAP_ENTRY_AMBIGUOUS(_interface, _implClass) \
  NS_IMPL_QUERY_BODY_AMBIGUOUS(_interface, _implClass)
#define NS_INTERFACE_MAP_ENTRY_CONCRETE(_class) \
  NS_IMPL_QUERY_BODY_CONCRETE(_class)
#define NS_INTERFACE_MAP_END_INHERITING(_baseClass) \
  NS_IMPL_QUERY_TAIL_INHERITING(_baseClass)
#define NS_INTERFACE_MAP_END_AGGREGATED(_aggregator) \
  NS_IMPL_QUERY_TAIL_USING_AGGREGATOR(_aggregator)

#define NS_INTERFACE_TABLE0(_class)               \
  NS_INTERFACE_TABLE_BEGIN                        \
    NS_INTERFACE_TABLE_ENTRY(_class, nsISupports) \
  NS_INTERFACE_TABLE_END

#define NS_INTERFACE_TABLE(aClass, ...)                               \
  static_assert(MOZ_ARG_COUNT(__VA_ARGS__) > 0,                       \
                "Need more arguments to NS_INTERFACE_TABLE");         \
  NS_INTERFACE_TABLE_BEGIN                                            \
    MOZ_FOR_EACH(NS_INTERFACE_TABLE_ENTRY, (aClass, ), (__VA_ARGS__)) \
    NS_INTERFACE_TABLE_ENTRY_AMBIGUOUS(aClass, nsISupports,           \
                                       MOZ_ARG_1(__VA_ARGS__))        \
  NS_INTERFACE_TABLE_END

#define NS_IMPL_QUERY_INTERFACE0(_class) \
  NS_INTERFACE_TABLE_HEAD(_class)        \
    NS_INTERFACE_TABLE0(_class)          \
  NS_INTERFACE_TABLE_TAIL

#define NS_IMPL_QUERY_INTERFACE(aClass, ...) \
  NS_INTERFACE_TABLE_HEAD(aClass)            \
    NS_INTERFACE_TABLE(aClass, __VA_ARGS__)  \
  NS_INTERFACE_TABLE_TAIL

/**
 * Declare that you're going to inherit from something that already
 * implements nsISupports, but also implements an additional interface, thus
 * causing an ambiguity. In this case you don't need another mRefCnt, you
 * just need to forward the definitions to the appropriate superclass. E.g.
 *
 * class Bar : public Foo, public nsIBar {  // both provide nsISupports
 * public:
 *   NS_DECL_ISUPPORTS_INHERITED
 *   ...other nsIBar and Bar methods...
 * };
 */
#define NS_DECL_ISUPPORTS_INHERITED                                       \
 public:                                                                  \
  NS_IMETHOD QueryInterface(REFNSIID aIID, void** aInstancePtr) override; \
  NS_IMETHOD_(MozExternalRefCountType) AddRef(void) override;             \
  NS_IMETHOD_(MozExternalRefCountType) Release(void) override;

/**
 * These macros can be used in conjunction with NS_DECL_ISUPPORTS_INHERITED
 * to implement the nsISupports methods, forwarding the invocations to a
 * superclass that already implements nsISupports. Don't do anything for
 * subclasses of Runnable because it deals with subclass logging in its own
 * way, using the mName field.
 *
 * Note that I didn't make these inlined because they're virtual methods.
 */

namespace mozilla {
class Runnable;
namespace detail {
class SupportsThreadSafeWeakPtrBase;

// Don't NS_LOG_{ADDREF,RELEASE} when inheriting from `Runnable*` or types with
// thread safe weak references, as it will generate incorrect refcnt logs due to
// the thread-safe `Upgrade()` call's refcount modifications not calling through
// the derived class' `AddRef()` and `Release()` methods.
template <typename T>
constexpr bool ShouldLogInheritedRefcnt =
    !std::is_convertible_v<T*, Runnable*> &&
    !std::is_base_of_v<SupportsThreadSafeWeakPtrBase, T>;
}
}  // namespace mozilla

#define NS_IMPL_ADDREF_INHERITED_GUTS(Class, Super)                   \
  MOZ_ASSERT_TYPE_OK_FOR_REFCOUNTING(Class)                           \
  nsrefcnt r = Super::AddRef();                                       \
  if constexpr (::mozilla::detail::ShouldLogInheritedRefcnt<Class>) { \
    NS_LOG_ADDREF(this, r, #Class, sizeof(*this));                    \
  }                                                                   \
  return r /* Purposefully no trailing semicolon */

#define NS_IMPL_ADDREF_INHERITED(Class, Super)                  \
  NS_IMETHODIMP_(MozExternalRefCountType) Class::AddRef(void) { \
    NS_IMPL_ADDREF_INHERITED_GUTS(Class, Super);                \
  }

#define NS_IMPL_RELEASE_INHERITED_GUTS(Class, Super)                  \
  nsrefcnt r = Super::Release();                                      \
  if constexpr (::mozilla::detail::ShouldLogInheritedRefcnt<Class>) { \
    NS_LOG_RELEASE(this, r, #Class);                                  \
  }                                                                   \
  return r /* Purposefully no trailing semicolon */

#define NS_IMPL_RELEASE_INHERITED(Class, Super)                  \
  NS_IMETHODIMP_(MozExternalRefCountType) Class::Release(void) { \
    NS_IMPL_RELEASE_INHERITED_GUTS(Class, Super);                \
  }

/**
 * As above but not logging the addref/release; needed if the base
 * class might be aggregated.
 */
#define NS_IMPL_NONLOGGING_ADDREF_INHERITED(Class, Super)       \
  NS_IMETHODIMP_(MozExternalRefCountType) Class::AddRef(void) { \
    MOZ_ASSERT_TYPE_OK_FOR_REFCOUNTING(Class)                   \
    return Super::AddRef();                                     \
  }

#define NS_IMPL_NONLOGGING_RELEASE_INHERITED(Class, Super)       \
  NS_IMETHODIMP_(MozExternalRefCountType) Class::Release(void) { \
    return Super::Release();                                     \
  }

#define NS_INTERFACE_TABLE_INHERITED0(Class) /* Nothing to do here */

#define NS_INTERFACE_TABLE_INHERITED(aClass, ...)                       \
  static_assert(MOZ_ARG_COUNT(__VA_ARGS__) > 0,                         \
                "Need more arguments to NS_INTERFACE_TABLE_INHERITED"); \
  NS_INTERFACE_TABLE_BEGIN                                              \
    MOZ_FOR_EACH(NS_INTERFACE_TABLE_ENTRY, (aClass, ), (__VA_ARGS__))   \
  NS_INTERFACE_TABLE_END

#define NS_IMPL_QUERY_INTERFACE_INHERITED(aClass, aSuper, ...) \
  NS_INTERFACE_TABLE_HEAD(aClass)                              \
    NS_INTERFACE_TABLE_INHERITED(aClass, __VA_ARGS__)          \
  NS_INTERFACE_TABLE_TAIL_INHERITING(aSuper)

/**
 * Convenience macros for implementing all nsISupports methods for
 * a simple class.
 * @param _class The name of the class implementing the method
 * @param _classiiddef The name of the #define symbol that defines the IID
 * for the class (e.g. NS_ISUPPORTS_IID)
 */

#define NS_IMPL_ISUPPORTS0(_class) \
  NS_IMPL_ADDREF(_class)           \
  NS_IMPL_RELEASE(_class)          \
  NS_IMPL_QUERY_INTERFACE0(_class)

#define NS_IMPL_ISUPPORTS(aClass, ...) \
  NS_IMPL_ADDREF(aClass)               \
  NS_IMPL_RELEASE(aClass)              \
  NS_IMPL_QUERY_INTERFACE(aClass, __VA_ARGS__)

// When possible, prefer NS_INLINE_DECL_REFCOUNTING_INHERITED to
// NS_IMPL_ISUPPORTS_INHERITED0.
#define NS_IMPL_ISUPPORTS_INHERITED0(aClass, aSuper) \
  NS_INTERFACE_TABLE_HEAD(aClass)                    \
  NS_INTERFACE_TABLE_TAIL_INHERITING(aSuper)         \
  NS_IMPL_ADDREF_INHERITED(aClass, aSuper)           \
  NS_IMPL_RELEASE_INHERITED(aClass, aSuper)

#define NS_IMPL_ISUPPORTS_INHERITED(aClass, aSuper, ...)         \
  NS_IMPL_QUERY_INTERFACE_INHERITED(aClass, aSuper, __VA_ARGS__) \
  NS_IMPL_ADDREF_INHERITED(aClass, aSuper)                       \
  NS_IMPL_RELEASE_INHERITED(aClass, aSuper)

/**
 * A macro to declare and implement inherited addref/release for a class which
 * doesn't have or need to override QueryInterface from its base class.
 *
 * Note: This macro always overrides the `AddRef` and `Release` methods,
 * including when refcount logging is disabled, meaning that it will implement
 * the `AddRef` or `Release` method from another virtual base class.
 */
#define NS_INLINE_DECL_REFCOUNTING_INHERITED(Class, Super)  \
  NS_IMETHOD_(MozExternalRefCountType) AddRef() override {  \
    NS_IMPL_ADDREF_INHERITED_GUTS(Class, Super);            \
  }                                                         \
  NS_IMETHOD_(MozExternalRefCountType) Release() override { \
    NS_IMPL_RELEASE_INHERITED_GUTS(Class, Super);           \
  }

/*
 * Macro to glue together a QI that starts with an interface table
 * and segues into an interface map (e.g. it uses singleton classinfo
 * or tearoffs).
 */
#define NS_INTERFACE_TABLE_TO_MAP_SEGUE \
  if (rv == NS_OK) return rv;           \
  nsISupports* foundInterface;

///////////////////////////////////////////////////////////////////////////////

/**
 * Macro to generate nsIClassInfo methods for classes which do not have
 * corresponding nsIFactory implementations.
 */
#define NS_IMPL_THREADSAFE_CI(_class)                          \
  NS_IMETHODIMP                                                \
  _class::GetInterfaces(nsTArray<nsIID>& _array) {             \
    return NS_CI_INTERFACE_GETTER_NAME(_class)(_array);        \
  }                                                            \
                                                               \
  NS_IMETHODIMP                                                \
  _class::GetScriptableHelper(nsIXPCScriptable** _retval) {    \
    *_retval = nullptr;                                        \
    return NS_OK;                                              \
  }                                                            \
                                                               \
  NS_IMETHODIMP                                                \
  _class::GetContractID(nsACString& _contractID) {             \
    _contractID.SetIsVoid(true);                               \
    return NS_OK;                                              \
  }                                                            \
                                                               \
  NS_IMETHODIMP                                                \
  _class::GetClassDescription(nsACString& _classDescription) { \
    _classDescription.SetIsVoid(true);                         \
    return NS_OK;                                              \
  }                                                            \
                                                               \
  NS_IMETHODIMP                                                \
  _class::GetClassID(nsCID** _classID) {                       \
    *_classID = nullptr;                                       \
    return NS_OK;                                              \
  }                                                            \
                                                               \
  NS_IMETHODIMP                                                \
  _class::GetFlags(uint32_t* _flags) {                         \
    *_flags = nsIClassInfo::THREADSAFE;                        \
    return NS_OK;                                              \
  }                                                            \
                                                               \
  NS_IMETHODIMP                                                \
  _class::GetClassIDNoAlloc(nsCID* _classIDNoAlloc) {          \
    return NS_ERROR_NOT_AVAILABLE;                             \
  }

#endif
