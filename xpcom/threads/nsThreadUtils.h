/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef nsThreadUtils_h_
#define nsThreadUtils_h_

#include <tuple>
#include <type_traits>
#include <utility>

#include "MainThreadUtils.h"
#include "mozilla/AbstractThread.h"
#include "mozilla/Atomics.h"
#include "mozilla/EventQueue.h"
#include "mozilla/Likely.h"
#include "mozilla/Maybe.h"
#include "mozilla/ThreadLocal.h"
#include "mozilla/TimeStamp.h"
#include "nsCOMPtr.h"
#include "nsICancelableRunnable.h"
#include "nsIDiscardableRunnable.h"
#include "nsIIdlePeriod.h"
#include "nsIIdleRunnable.h"
#include "nsINamed.h"
#include "nsIRunnable.h"
#include "nsIThreadManager.h"
#include "nsITimer.h"
#include "nsString.h"
#include "prinrval.h"
#include "prthread.h"

class MessageLoop;
class nsIThread;

//-----------------------------------------------------------------------------
// These methods are alternatives to the methods on nsIThreadManager, provided
// for convenience.

/**
 * Create a new thread, and optionally provide an initial event for the thread.
 *
 * @param aName
 *   The name of the thread.
 * @param aResult
 *   The resulting nsIThread object.
 * @param aInitialEvent
 *   The initial event to run on this thread.  This parameter may be null.
 * @param aOptions
 *   Options used to configure thread creation.
 *   Options are documented in nsIThreadManager.idl.
 *
 * @returns NS_ERROR_INVALID_ARG
 *   Indicates that the given name is not unique.
 */

extern nsresult NS_NewNamedThread(
    const nsACString& aName, nsIThread** aResult,
    nsIRunnable* aInitialEvent = nullptr,
    nsIThreadManager::ThreadCreationOptions aOptions = {});

extern nsresult NS_NewNamedThread(
    const nsACString& aName, nsIThread** aResult,
    already_AddRefed<nsIRunnable> aInitialEvent,
    nsIThreadManager::ThreadCreationOptions aOptions = {});

template <size_t LEN>
inline nsresult NS_NewNamedThread(
    const char (&aName)[LEN], nsIThread** aResult,
    already_AddRefed<nsIRunnable> aInitialEvent,
    nsIThreadManager::ThreadCreationOptions aOptions = {}) {
  static_assert(LEN <= 16, "Thread name must be no more than 16 characters");
  return NS_NewNamedThread(nsDependentCString(aName, LEN - 1), aResult,
                           std::move(aInitialEvent), aOptions);
}

template <size_t LEN>
inline nsresult NS_NewNamedThread(
    const char (&aName)[LEN], nsIThread** aResult,
    nsIRunnable* aInitialEvent = nullptr,
    nsIThreadManager::ThreadCreationOptions aOptions = {}) {
  nsCOMPtr<nsIRunnable> event = aInitialEvent;
  static_assert(LEN <= 16, "Thread name must be no more than 16 characters");
  return NS_NewNamedThread(nsDependentCString(aName, LEN - 1), aResult,
                           event.forget(), aOptions);
}

/**
 * Get a reference to the current thread, creating it if it does not exist yet.
 *
 * @param aResult
 *   The resulting nsIThread object.
 */
extern nsresult NS_GetCurrentThread(nsIThread** aResult);

/**
 * Dispatch the given event to the current thread.
 *
 * @param aEvent
 *   The event to dispatch.
 *
 * @returns NS_ERROR_INVALID_ARG
 *   If event is null.
 */
extern nsresult NS_DispatchToCurrentThread(nsIRunnable* aEvent);
extern nsresult NS_DispatchToCurrentThread(
    already_AddRefed<nsIRunnable> aEvent);

/**
 * Dispatch the given event to the main thread.
 *
 * @param aEvent
 *   The event to dispatch.
 * @param aDispatchFlags
 *   The flags to pass to the main thread's dispatch method.
 *
 * @returns NS_ERROR_INVALID_ARG
 *   If event is null.
 */
extern nsresult NS_DispatchToMainThread(
    nsIRunnable* aEvent,
    nsIEventTarget::DispatchFlags aDispatchFlags = NS_DISPATCH_NORMAL);
extern nsresult NS_DispatchToMainThread(
    already_AddRefed<nsIRunnable> aEvent,
    nsIEventTarget::DispatchFlags aDispatchFlags = NS_DISPATCH_NORMAL);

extern nsresult NS_DelayedDispatchToCurrentThread(
    already_AddRefed<nsIRunnable> aEvent, uint32_t aDelayMs);

/**
 * Dispatch the given event to the specified queue of the current thread.
 *
 * @param aEvent The event to dispatch.
 * @param aQueue The event queue for the thread to use
 *
 * @returns NS_ERROR_INVALID_ARG
 *   If event is null.
 * @returns NS_ERROR_UNEXPECTED
 *   If the thread is shutting down.
 */
extern nsresult NS_DispatchToCurrentThreadQueue(
    already_AddRefed<nsIRunnable> aEvent, mozilla::EventQueuePriority aQueue);

/**
 * Dispatch the given event to the specified queue of the main thread.
 *
 * @param aEvent The event to dispatch.
 * @param aQueue The event queue for the thread to use
 *
 * @returns NS_ERROR_INVALID_ARG
 *   If event is null.
 * @returns NS_ERROR_UNEXPECTED
 *   If the thread is shutting down.
 */
extern nsresult NS_DispatchToMainThreadQueue(
    already_AddRefed<nsIRunnable> aEvent, mozilla::EventQueuePriority aQueue);

/**
 * Dispatch the given event to an idle queue of the current thread.
 *
 * @param aEvent The event to dispatch. If the event implements
 *   nsIIdleRunnable, it will receive a call on
 *   nsIIdleRunnable::SetTimer when dispatched, with the value of
 *   aTimeout.
 *
 * @param aTimeout The time in milliseconds until the event should be
 *   moved from an idle queue to the regular queue, if it hasn't been
 *   executed. If aEvent is also an nsIIdleRunnable, it is expected
 *   that it should handle the timeout itself, after a call to
 *   nsIIdleRunnable::SetTimer.
 *
 * @param aQueue
 *   The event queue for the thread to use.  Must be an idle queue
 *   (Idle or DeferredTimers)
 *
 * @returns NS_ERROR_INVALID_ARG
 *   If event is null.
 * @returns NS_ERROR_UNEXPECTED
 *   If the thread is shutting down.
 */
extern nsresult NS_DispatchToCurrentThreadQueue(
    already_AddRefed<nsIRunnable> aEvent, uint32_t aTimeout,
    mozilla::EventQueuePriority aQueue);

/**
 * Dispatch the given event to a queue of a thread.
 *
 * @param aEvent The event to dispatch.
 * @param aThread The target thread for the dispatch.
 * @param aQueue The event queue for the thread to use.
 *
 * @returns NS_ERROR_INVALID_ARG
 *   If event is null.
 * @returns NS_ERROR_UNEXPECTED
 *   If the thread is shutting down.
 */
extern nsresult NS_DispatchToThreadQueue(already_AddRefed<nsIRunnable> aEvent,
                                         nsIThread* aThread,
                                         mozilla::EventQueuePriority aQueue);

/**
 * Dispatch the given event to an idle queue of a thread.
 *
 * @param aEvent The event to dispatch. If the event implements
 *   nsIIdleRunnable, it will receive a call on
 *   nsIIdleRunnable::SetTimer when dispatched, with the value of
 *   aTimeout.
 *
 * @param aTimeout The time in milliseconds until the event should be
 *   moved from an idle queue to the regular queue, if it hasn't been
 *   executed. If aEvent is also an nsIIdleRunnable, it is expected
 *   that it should handle the timeout itself, after a call to
 *   nsIIdleRunnable::SetTimer.
 *
 * @param aThread The target thread for the dispatch.
 *
 * @param aQueue
 *   The event queue for the thread to use.  Must be an idle queue
 *   (Idle or DeferredTimers)
 *
 * @returns NS_ERROR_INVALID_ARG
 *   If event is null.
 * @returns NS_ERROR_UNEXPECTED
 *   If the thread is shutting down.
 */
extern nsresult NS_DispatchToThreadQueue(already_AddRefed<nsIRunnable> aEvent,
                                         uint32_t aTimeout, nsIThread* aThread,
                                         mozilla::EventQueuePriority aQueue);

#ifndef XPCOM_GLUE_AVOID_NSPR
/**
 * Process all pending events for the given thread before returning.  This
 * method simply calls ProcessNextEvent on the thread while HasPendingEvents
 * continues to return true and the time spent in NS_ProcessPendingEvents
 * does not exceed the given timeout value.
 *
 * @param aThread
 *   The thread object for which to process pending events.  If null, then
 *   events will be processed for the current thread.
 * @param aTimeout
 *   The maximum number of milliseconds to spend processing pending events.
 *   Events are not pre-empted to honor this timeout.  Rather, the timeout
 *   value is simply used to determine whether or not to process another event.
 *   Pass PR_INTERVAL_NO_TIMEOUT to specify no timeout.
 */
extern nsresult NS_ProcessPendingEvents(
    nsIThread* aThread, PRIntervalTime aTimeout = PR_INTERVAL_NO_TIMEOUT);
#endif

/**
 * Shortcut for nsIThread::HasPendingEvents.
 *
 * It is an error to call this function when the given thread is not the
 * current thread.  This function will return false if called from some
 * other thread.
 *
 * @param aThread
 *   The current thread or null.
 *
 * @returns
 *   A boolean value that if "true" indicates that there are pending events
 *   in the current thread's event queue.
 */
extern bool NS_HasPendingEvents(nsIThread* aThread = nullptr);

/**
 * Shortcut for nsIThread::ProcessNextEvent.
 *
 * It is an error to call this function when the given thread is not the
 * current thread.  This function will simply return false if called
 * from some other thread.
 *
 * @param aThread
 *   The current thread or null.
 * @param aMayWait
 *   A boolean parameter that if "true" indicates that the method may block
 *   the calling thread to wait for a pending event.
 *
 * @returns
 *   A boolean value that if "true" indicates that an event from the current
 *   thread's event queue was processed.
 */
extern bool NS_ProcessNextEvent(nsIThread* aThread = nullptr,
                                bool aMayWait = true);

/**
 * Returns true if we're in the compositor thread.
 *
 * We declare this here because the headers required to invoke
 * CompositorThreadHolder::IsInCompositorThread() also pull in a bunch of system
 * headers that #define various tokens in a way that can break the build.
 */
extern bool NS_IsInCompositorThread();

extern bool NS_IsInCanvasThread();

extern bool NS_IsInVRThread();

//-----------------------------------------------------------------------------
// Helpers that work with nsCOMPtr:

inline already_AddRefed<nsIThread> do_GetCurrentThread() {
  nsIThread* thread = nullptr;
  NS_GetCurrentThread(&thread);
  return already_AddRefed<nsIThread>(thread);
}

inline already_AddRefed<nsIThread> do_GetMainThread() {
  nsIThread* thread = nullptr;
  NS_GetMainThread(&thread);
  return already_AddRefed<nsIThread>(thread);
}

//-----------------------------------------------------------------------------

// Fast access to the current thread.  Will create an nsIThread if one does not
// exist already!  Do not release the returned pointer!  If you want to use this
// pointer from some other thread, then you will need to AddRef it.  Otherwise,
// you should only consider this pointer valid from code running on the current
// thread.
extern nsIThread* NS_GetCurrentThread();

// Exactly the same as NS_GetCurrentThread, except it will not create an
// nsThread if one does not exist yet. This is useful in cases where you have
// code that runs on threads that may or may not not be driven by an nsThread
// event loop, and wish to avoid inadvertently creating a superfluous nsThread.
extern nsIThread* NS_GetCurrentThreadNoCreate();

/**
 * Set the name of the current thread. Prefer this function over
 * PR_SetCurrentThreadName() if possible. The name will also be included in the
 * crash report.
 *
 * @param aName
 *   Name of the thread. A C language null-terminated string.
 */
extern void NS_SetCurrentThreadName(const char* aName);

//-----------------------------------------------------------------------------

#ifndef XPCOM_GLUE_AVOID_NSPR

namespace mozilla {

// This class is designed to be subclassed.
class IdlePeriod : public nsIIdlePeriod {
 public:
  NS_DECL_THREADSAFE_ISUPPORTS
  NS_DECL_NSIIDLEPERIOD

  IdlePeriod() = default;

  IdlePeriod(const IdlePeriod&) = delete;
  IdlePeriod& operator=(const IdlePeriod&) = delete;
  IdlePeriod& operator=(const IdlePeriod&&) = delete;

 protected:
  virtual ~IdlePeriod() = default;
};

// Cancelable runnable methods implement nsICancelableRunnable, and
// Idle and IdleWithTimer also nsIIdleRunnable.
enum class RunnableKind { Standard, Cancelable, Idle, IdleWithTimer };

// Implementing nsINamed on Runnable bloats vtables for the hundreds of
// Runnable subclasses that we have, so we want to avoid that overhead
// when we're not using nsINamed for anything.
#  ifndef RELEASE_OR_BETA
#    define MOZ_COLLECTING_RUNNABLE_TELEMETRY
#  endif

// This class is designed to be subclassed.
class Runnable : public nsIRunnable
#  ifdef MOZ_COLLECTING_RUNNABLE_TELEMETRY
    ,
                 public nsINamed
#  endif
{
 public:
  NS_DECL_THREADSAFE_ISUPPORTS
  NS_DECL_NSIRUNNABLE
#  ifdef MOZ_COLLECTING_RUNNABLE_TELEMETRY
  NS_DECL_NSINAMED
#  endif

  Runnable() = delete;
  Runnable(const Runnable&) = delete;
  Runnable& operator=(const Runnable&) = delete;
  Runnable& operator=(const Runnable&&) = delete;

#  ifdef MOZ_COLLECTING_RUNNABLE_TELEMETRY
  explicit Runnable(const char* aName) : mName(aName) {}
#  else
  explicit Runnable(const char* aName) {}
#  endif

 protected:
  virtual ~Runnable() = default;

#  ifdef MOZ_COLLECTING_RUNNABLE_TELEMETRY
  const char* mName = nullptr;
#  endif
};

// This is a base class for tasks that might not be run, such as those that may
// be dispatched to workers.
// The owner of an event target will call either Run() or OnDiscard()
// exactly once.
// Derived classes should override Run().  An OnDiscard() override may
// provide cleanup when Run() will not be called.
class DiscardableRunnable : public Runnable, public nsIDiscardableRunnable {
 public:
  NS_DECL_ISUPPORTS_INHERITED
  // nsIDiscardableRunnable
  void OnDiscard() override {}

  explicit DiscardableRunnable(const char* aName) : Runnable(aName) {}
  DiscardableRunnable() = delete;
  DiscardableRunnable(const DiscardableRunnable&) = delete;
  DiscardableRunnable& operator=(const DiscardableRunnable&) = delete;
  DiscardableRunnable& operator=(const DiscardableRunnable&&) = delete;

 protected:
  virtual ~DiscardableRunnable() = default;
};

// This class is designed to be subclassed.
// Derived classes should override Run() and Cancel() to provide that
// calling Run() after Cancel() is a no-op.
class CancelableRunnable : public DiscardableRunnable,
                           public nsICancelableRunnable {
 public:
  NS_DECL_ISUPPORTS_INHERITED
  // nsIDiscardableRunnable
  void OnDiscard() override;
  // nsICancelableRunnable
  virtual nsresult Cancel() override = 0;

  explicit CancelableRunnable(const char* aName) : DiscardableRunnable(aName) {}
  CancelableRunnable() = delete;
  CancelableRunnable(const CancelableRunnable&) = delete;
  CancelableRunnable& operator=(const CancelableRunnable&) = delete;
  CancelableRunnable& operator=(const CancelableRunnable&&) = delete;

 protected:
  virtual ~CancelableRunnable() = default;
};

// This class is designed to be subclassed.
class IdleRunnable : public DiscardableRunnable, public nsIIdleRunnable {
 public:
  NS_DECL_ISUPPORTS_INHERITED

  explicit IdleRunnable(const char* aName) : DiscardableRunnable(aName) {}
  IdleRunnable(const IdleRunnable&) = delete;
  IdleRunnable& operator=(const IdleRunnable&) = delete;
  IdleRunnable& operator=(const IdleRunnable&&) = delete;

 protected:
  virtual ~IdleRunnable() = default;
};

// This class is designed to be subclassed.
class CancelableIdleRunnable : public CancelableRunnable,
                               public nsIIdleRunnable {
 public:
  NS_DECL_ISUPPORTS_INHERITED

  CancelableIdleRunnable() : CancelableRunnable("CancelableIdleRunnable") {}
  explicit CancelableIdleRunnable(const char* aName)
      : CancelableRunnable(aName) {}
  CancelableIdleRunnable(const CancelableIdleRunnable&) = delete;
  CancelableIdleRunnable& operator=(const CancelableIdleRunnable&) = delete;
  CancelableIdleRunnable& operator=(const CancelableIdleRunnable&&) = delete;

 protected:
  virtual ~CancelableIdleRunnable() = default;
};

// This class is designed to be a wrapper of a real runnable to support event
// prioritizable.
class PrioritizableRunnable : public Runnable, public nsIRunnablePriority {
 public:
  PrioritizableRunnable(already_AddRefed<nsIRunnable> aRunnable,
                        uint32_t aPriority);

#  ifdef MOZ_COLLECTING_RUNNABLE_TELEMETRY
  NS_IMETHOD GetName(nsACString& aName) override;
#  endif

  NS_DECL_ISUPPORTS_INHERITED
  NS_DECL_NSIRUNNABLE
  NS_DECL_NSIRUNNABLEPRIORITY

 protected:
  virtual ~PrioritizableRunnable() = default;

  nsCOMPtr<nsIRunnable> mRunnable;
  uint32_t mPriority;
};

class PrioritizableCancelableRunnable : public CancelableRunnable,
                                        public nsIRunnablePriority {
 public:
  PrioritizableCancelableRunnable(uint32_t aPriority, const char* aName)
      : CancelableRunnable(aName), mPriority(aPriority) {}

  NS_DECL_ISUPPORTS_INHERITED
  NS_DECL_NSIRUNNABLEPRIORITY

 protected:
  virtual ~PrioritizableCancelableRunnable() = default;

  const uint32_t mPriority;
};

extern already_AddRefed<nsIRunnable> CreateRenderBlockingRunnable(
    already_AddRefed<nsIRunnable> aRunnable);

namespace detail {

// An event that can be used to call a C++11 functions or function objects,
// including lambdas. The function must have no required arguments, and must
// return void.
template <typename StoredFunction>
class RunnableFunction : public Runnable {
 public:
  template <typename F>
  explicit RunnableFunction(const char* aName, F&& aFunction)
      : Runnable(aName), mFunction(std::forward<F>(aFunction)) {}

  NS_IMETHOD Run() override {
    static_assert(std::is_void_v<decltype(mFunction())>,
                  "The lambda must return void!");
    mFunction();
    return NS_OK;
  }

 private:
  StoredFunction mFunction;
};

// Type alias for NS_NewRunnableFunction
template <typename Function>
using RunnableFunctionImpl =
    // Make sure we store a non-reference in nsRunnableFunction.
    typename detail::RunnableFunction<std::remove_reference_t<Function>>;
}  // namespace detail

namespace detail {

template <typename T>
struct RemoveSmartPointerHelper {
  using Type = T;
};

template <typename T>
struct RemoveSmartPointerHelper<RefPtr<T>> {
  using Type = T;
};

template <typename T>
struct RemoveSmartPointerHelper<nsCOMPtr<T>> {
  using Type = T;
};

template <typename T>
struct RemoveRawOrSmartPointerHelper {
  using Type = typename RemoveSmartPointerHelper<T>::Type;
};

template <typename T>
struct RemoveRawOrSmartPointerHelper<T*> {
  using Type = T;
};

}  // namespace detail

template <typename T>
using RemoveSmartPointer =
    typename detail::RemoveSmartPointerHelper<std::remove_cv_t<T>>::Type;

template <typename T>
using RemoveRawOrSmartPointer =
    typename detail::RemoveRawOrSmartPointerHelper<std::remove_cv_t<T>>::Type;

}  // namespace mozilla

inline nsISupports* ToSupports(mozilla::Runnable* p) {
  return static_cast<nsIRunnable*>(p);
}

template <typename Function>
already_AddRefed<mozilla::Runnable> NS_NewRunnableFunction(
    const char* aName, Function&& aFunction) {
  // We store a non-reference in RunnableFunction, but still forward aFunction
  // to move if possible.
  return do_AddRef(new mozilla::detail::RunnableFunctionImpl<Function>(
      aName, std::forward<Function>(aFunction)));
}

// Creates a new object implementing nsIRunnable and nsICancelableRunnable,
// which runs a given function on Run and clears the stored function object on a
// call to `Cancel` (and thus destroys all objects it holds).
template <typename Function>
already_AddRefed<mozilla::CancelableRunnable> NS_NewCancelableRunnableFunction(
    const char* aName, Function&& aFunc) {
  class FuncCancelableRunnable final : public mozilla::CancelableRunnable {
   public:
    static_assert(
        std::is_void_v<
            decltype(std::declval<std::remove_reference_t<Function>>()())>);

    NS_INLINE_DECL_REFCOUNTING_INHERITED(FuncCancelableRunnable,
                                         CancelableRunnable)

    explicit FuncCancelableRunnable(const char* aName, Function&& aFunc)
        : CancelableRunnable{aName},
          mFunc{mozilla::Some(std::forward<Function>(aFunc))} {}

    NS_IMETHOD Run() override {
      if (mFunc) {
        (*mFunc)();
      }

      return NS_OK;
    }

    nsresult Cancel() override {
      mFunc.reset();
      return NS_OK;
    }

   private:
    ~FuncCancelableRunnable() = default;

    mozilla::Maybe<std::remove_reference_t<Function>> mFunc;
  };

  return mozilla::MakeAndAddRef<FuncCancelableRunnable>(
      aName, std::forward<Function>(aFunc));
}

namespace mozilla {
namespace detail {

template <RunnableKind Kind>
class TimerBehaviour {
 public:
  nsITimer* GetTimer() { return nullptr; }
  void CancelTimer() {}

 protected:
  ~TimerBehaviour() = default;
};

template <>
class TimerBehaviour<RunnableKind::IdleWithTimer> {
 public:
  nsITimer* GetTimer() {
    if (!mTimer) {
      mTimer = NS_NewTimer();
    }

    return mTimer;
  }

  void CancelTimer() {
    if (mTimer) {
      mTimer->Cancel();
    }
  }

 protected:
  ~TimerBehaviour() { CancelTimer(); }

 private:
  nsCOMPtr<nsITimer> mTimer;
};

}  // namespace detail
}  // namespace mozilla

// An event that can be used to call a method on a class.  The class type must
// support reference counting. This event supports Revoke for use
// with nsRevocableEventPtr.
template <class ClassType, typename ReturnType = void, bool Owning = true,
          mozilla::RunnableKind Kind = mozilla::RunnableKind::Standard>
class nsRunnableMethod
    : public std::conditional_t<
          Kind == mozilla::RunnableKind::Standard, mozilla::Runnable,
          std::conditional_t<Kind == mozilla::RunnableKind::Cancelable,
                             mozilla::CancelableRunnable,
                             mozilla::CancelableIdleRunnable>>,
      protected mozilla::detail::TimerBehaviour<Kind> {
  using BaseType = std::conditional_t<
      Kind == mozilla::RunnableKind::Standard, mozilla::Runnable,
      std::conditional_t<Kind == mozilla::RunnableKind::Cancelable,
                         mozilla::CancelableRunnable,
                         mozilla::CancelableIdleRunnable>>;

 public:
  nsRunnableMethod(const char* aName) : BaseType(aName) {}

  virtual void Revoke() = 0;

  // These ReturnTypeEnforcer classes disallow return types that
  // we know are not safe. The default ReturnTypeEnforcer compiles just fine but
  // already_AddRefed will not.
  template <typename OtherReturnType>
  class ReturnTypeEnforcer {
   public:
    typedef int ReturnTypeIsSafe;
  };

  template <class T>
  class ReturnTypeEnforcer<already_AddRefed<T>> {
    // No ReturnTypeIsSafe makes this illegal!
  };

  // Make sure this return type is safe.
  typedef typename ReturnTypeEnforcer<ReturnType>::ReturnTypeIsSafe check;
};

template <class ClassType, bool Owning>
struct nsRunnableMethodReceiver {
  RefPtr<ClassType> mObj;
  explicit nsRunnableMethodReceiver(ClassType* aObj) : mObj(aObj) {}
  explicit nsRunnableMethodReceiver(RefPtr<ClassType>&& aObj)
      : mObj(std::move(aObj)) {}
  ~nsRunnableMethodReceiver() { Revoke(); }
  ClassType* Get() const { return mObj.get(); }
  void Revoke() { mObj = nullptr; }
};

template <class ClassType>
struct nsRunnableMethodReceiver<ClassType, false> {
  ClassType* MOZ_NON_OWNING_REF mObj;
  explicit nsRunnableMethodReceiver(ClassType* aObj) : mObj(aObj) {}
  ClassType* Get() const { return mObj; }
  void Revoke() { mObj = nullptr; }
};

static inline constexpr bool IsIdle(mozilla::RunnableKind aKind) {
  return aKind == mozilla::RunnableKind::Idle ||
         aKind == mozilla::RunnableKind::IdleWithTimer;
}

template <typename PtrType, typename Method, bool Owning,
          mozilla::RunnableKind Kind>
struct nsRunnableMethodTraits;

template <typename PtrType, class C, typename R, bool Owning,
          mozilla::RunnableKind Kind, typename... As>
struct nsRunnableMethodTraits<PtrType, R (C::*)(As...), Owning, Kind> {
  using class_type = mozilla::RemoveRawOrSmartPointer<PtrType>;
  static_assert(std::is_base_of_v<C, class_type>,
                "Stored class must inherit from method's class");
  using return_type = R;
  using base_type = nsRunnableMethod<C, R, Owning, Kind>;
  static const bool can_cancel = Kind == mozilla::RunnableKind::Cancelable;
};

template <typename PtrType, class C, typename R, bool Owning,
          mozilla::RunnableKind Kind, typename... As>
struct nsRunnableMethodTraits<PtrType, R (C::*)(As...) const, Owning, Kind> {
  using class_type = const mozilla::RemoveRawOrSmartPointer<PtrType>;
  static_assert(std::is_base_of_v<C, class_type>,
                "Stored class must inherit from method's class");
  using return_type = R;
  using base_type = nsRunnableMethod<C, R, Owning, Kind>;
  static const bool can_cancel = Kind == mozilla::RunnableKind::Cancelable;
};

#  ifdef NS_HAVE_STDCALL
template <typename PtrType, class C, typename R, bool Owning,
          mozilla::RunnableKind Kind, typename... As>
struct nsRunnableMethodTraits<PtrType, R (__stdcall C::*)(As...), Owning,
                              Kind> {
  using class_type = mozilla::RemoveRawOrSmartPointer<PtrType>;
  static_assert(std::is_base_of<C, class_type>::value,
                "Stored class must inherit from method's class");
  using return_type = R;
  using base_type = nsRunnableMethod<C, R, Owning, Kind>;
  static const bool can_cancel = Kind == mozilla::RunnableKind::Cancelable;
};

template <typename PtrType, class C, typename R, bool Owning,
          mozilla::RunnableKind Kind>
struct nsRunnableMethodTraits<PtrType, R (NS_STDCALL C::*)(), Owning, Kind> {
  using class_type = mozilla::RemoveRawOrSmartPointer<PtrType>;
  static_assert(std::is_base_of<C, class_type>::value,
                "Stored class must inherit from method's class");
  using return_type = R;
  using base_type = nsRunnableMethod<C, R, Owning, Kind>;
  static const bool can_cancel = Kind == mozilla::RunnableKind::Cancelable;
};

template <typename PtrType, class C, typename R, bool Owning,
          mozilla::RunnableKind Kind, typename... As>
struct nsRunnableMethodTraits<PtrType, R (__stdcall C::*)(As...) const, Owning,
                              Kind> {
  using class_type = const mozilla::RemoveRawOrSmartPointer<PtrType>;
  static_assert(std::is_base_of<C, class_type>::value,
                "Stored class must inherit from method's class");
  using return_type = R;
  using base_type = nsRunnableMethod<C, R, Owning, Kind>;
  static const bool can_cancel = Kind == mozilla::RunnableKind::Cancelable;
};

template <typename PtrType, class C, typename R, bool Owning,
          mozilla::RunnableKind Kind>
struct nsRunnableMethodTraits<PtrType, R (NS_STDCALL C::*)() const, Owning,
                              Kind> {
  using class_type = const mozilla::RemoveRawOrSmartPointer<PtrType>;
  static_assert(std::is_base_of<C, class_type>::value,
                "Stored class must inherit from method's class");
  using return_type = R;
  using base_type = nsRunnableMethod<C, R, Owning, Kind>;
  static const bool can_cancel = Kind == mozilla::RunnableKind::Cancelable;
};
#  endif

// IsParameterStorageClass<T>::value is true if T is a parameter-storage class
// that will be recognized by NS_New[NonOwning]RunnableMethodWithArg[s] to
// force a specific storage&passing strategy (instead of inferring one,
// see ParameterStorage).
// When creating a new storage class, add a specialization for it to be
// recognized.
template <typename T>
struct IsParameterStorageClass : public std::false_type {};

// StoreXPassByY structs used to inform nsRunnableMethodArguments how to
// store arguments, and how to pass them to the target method.

template <typename T>
struct StoreCopyPassByConstLRef {
  using stored_type = std::decay_t<T>;
  typedef const stored_type& passed_type;
  stored_type m;

  template <typename A>
    requires(!std::is_same_v<std::decay_t<A>, StoreCopyPassByConstLRef>)
  MOZ_IMPLICIT StoreCopyPassByConstLRef(A&& a) : m(std::forward<A>(a)) {}
  passed_type PassAsParameter() { return m; }
};
template <typename S>
struct IsParameterStorageClass<StoreCopyPassByConstLRef<S>>
    : public std::true_type {};

template <typename T>
struct StoreCopyPassByRRef {
  using stored_type = std::decay_t<T>;
  typedef stored_type&& passed_type;
  stored_type m;

  template <typename A>
    requires(!std::is_same_v<std::decay_t<A>, StoreCopyPassByRRef>)
  MOZ_IMPLICIT StoreCopyPassByRRef(A&& a) : m(std::forward<A>(a)) {}
  passed_type PassAsParameter() { return std::move(m); }
};
template <typename S>
struct IsParameterStorageClass<StoreCopyPassByRRef<S>> : public std::true_type {
};

template <typename T>
struct StoreRefPassByLRef {
  typedef T& stored_type;
  typedef T& passed_type;
  stored_type m;
  template <typename A>
  MOZ_IMPLICIT StoreRefPassByLRef(A& a) : m(a) {}
  passed_type PassAsParameter() { return m; }
};
template <typename S>
struct IsParameterStorageClass<StoreRefPassByLRef<S>> : public std::true_type {
};

template <typename T>
struct StoreConstRefPassByConstLRef {
  typedef const T& stored_type;
  typedef const T& passed_type;
  stored_type m;
  template <typename A>
  MOZ_IMPLICIT StoreConstRefPassByConstLRef(const A& a) : m(a) {}
  passed_type PassAsParameter() { return m; }
};
template <typename S>
struct IsParameterStorageClass<StoreConstRefPassByConstLRef<S>>
    : public std::true_type {};

template <typename T>
struct StoreRefPtrPassByPtr {
  typedef RefPtr<T> stored_type;
  typedef T* passed_type;
  stored_type m;

  template <typename A>
    requires(!std::is_same_v<std::decay_t<A>, StoreRefPtrPassByPtr>)
  MOZ_IMPLICIT StoreRefPtrPassByPtr(A&& a) : m(std::forward<A>(a)) {}
  passed_type PassAsParameter() { return m.get(); }
};
template <typename S>
struct IsParameterStorageClass<StoreRefPtrPassByPtr<S>>
    : public std::true_type {};

template <typename T>
struct StorePtrPassByPtr {
  typedef T* stored_type;
  typedef T* passed_type;
  stored_type m;
  template <typename A>
  MOZ_IMPLICIT StorePtrPassByPtr(A a) : m(a) {}
  passed_type PassAsParameter() { return m; }
};
template <typename S>
struct IsParameterStorageClass<StorePtrPassByPtr<S>> : public std::true_type {};

template <typename T>
struct StoreConstPtrPassByConstPtr {
  typedef const T* stored_type;
  typedef const T* passed_type;
  stored_type m;
  template <typename A>
  MOZ_IMPLICIT StoreConstPtrPassByConstPtr(A a) : m(a) {}
  passed_type PassAsParameter() { return m; }
};
template <typename S>
struct IsParameterStorageClass<StoreConstPtrPassByConstPtr<S>>
    : public std::true_type {};

namespace detail {

template <class T, typename = void>
struct HasRefCountMethodsTest : std::false_type {};

template <class T>
struct HasRefCountMethodsTest<
    T, std::void_t<decltype(std::declval<T>().AddRef(),
                            std::declval<T>().Release())>> : std::true_type {};

template <class T>
constexpr static bool HasRefCountMethods = HasRefCountMethodsTest<T>::value;

// Choose storage&passing strategy based on preferred storage type:
// - If IsParameterStorageClass<T>::value is true, use as-is.
// - RC*       -> StoreRefPtrPassByPtr<RC>       :Store RefPtr<RC>, pass RC*
//   ^^ RC quacks like a ref-counted type (i.e., has AddRef and Release methods)
// - const T*  -> StoreConstPtrPassByConstPtr<T> :Store const T*, pass const T*
// - T*        -> StorePtrPassByPtr<T>           :Store T*, pass T*.
// - const T&  -> StoreConstRefPassByConstLRef<T>:Store const T&, pass const T&.
// - T&        -> StoreRefPassByLRef<T>          :Store T&, pass T&.
// - T&&       -> StoreCopyPassByRRef<T>         :Store T, pass std::move(T).
// - RefPtr<T>, nsCOMPtr<T>
//             -> StoreRefPtrPassByPtr<T>        :Store RefPtr<T>, pass T*
// - Other T   -> StoreCopyPassByConstLRef<T>    :Store T, pass const T&.
//
// For anything less common, please use a lambda function rather than devising
// new parameter-storage classes. (In fact, consider doing that anyway.)

template <typename T>
struct OtherParameterStorage;

// The `IsParameterStorageClass` and `RC*` cases must be handled separately (see
// `ParameterStorageHelper`, below) until we can use C++20 concepts.

template <typename T>
struct OtherParameterStorage<const T*> {
  using Type = StoreConstPtrPassByConstPtr<T>;
};

template <typename T>
struct OtherParameterStorage<T*> {
  using Type = StorePtrPassByPtr<T>;
};

template <typename T>
struct OtherParameterStorage<const T&> {
  using Type = StoreConstRefPassByConstLRef<T>;
};

template <typename T>
struct OtherParameterStorage<T&> {
  using Type = StoreRefPassByLRef<T>;
};

template <typename T>
struct OtherParameterStorage<RefPtr<T>> {
  using Type = StoreRefPtrPassByPtr<T>;
};

template <typename T>
struct OtherParameterStorage<nsCOMPtr<T>> {
  using Type = StoreRefPtrPassByPtr<T>;
};

template <typename T>
struct OtherParameterStorage<T&&> {
  using Type = StoreCopyPassByRRef<T>;
};

template <typename T>
struct OtherParameterStorage<const T&&> {
  // This is good advice regardless of the types you're handling. Making it a
  // dependent type so that the check only happens when this specialization is
  // instantiated.
  static_assert(!std::is_same_v<std::void_t<T>, void>,
                "please use a lambda function");
};

// default impl.
template <typename T>
struct OtherParameterStorage {
  using Type = StoreCopyPassByConstLRef<T>;
};

template <typename T, bool A = IsParameterStorageClass<T>::value,
          bool B = std::is_pointer_v<T> &&
                   HasRefCountMethods<std::remove_pointer_t<T>>>
struct ParameterStorageHelper;

template <typename T, bool B>
struct ParameterStorageHelper<T, true, B> {
  using Type = T;
};

template <typename T>
struct ParameterStorageHelper<T, false, true> {
  using Type = StoreRefPtrPassByPtr<std::remove_pointer_t<T>>;
};

template <typename T>
struct ParameterStorageHelper<T, false, false> {
  using Type = typename OtherParameterStorage<std::remove_cv_t<T>>::Type;
};

template <typename T>
struct ParameterStorage {
  using Type = typename ParameterStorageHelper<T>::Type;
};

template <class T, typename = void>
struct HasSetDeadline : std::false_type {};

template <class T>
struct HasSetDeadline<T, std::void_t<decltype(std::declval<T>().SetDeadline(
                             std::declval<mozilla::TimeStamp>()))>>
    : std::true_type {};

template <class T>
std::enable_if_t<::detail::HasSetDeadline<T>::value> SetDeadlineImpl(
    T* aObj, mozilla::TimeStamp aTimeStamp) {
  aObj->SetDeadline(aTimeStamp);
}

template <class T>
std::enable_if_t<!::detail::HasSetDeadline<T>::value> SetDeadlineImpl(
    T* aObj, mozilla::TimeStamp aTimeStamp) {}
} /* namespace detail */

namespace mozilla {
namespace detail {

// struct used to store arguments and later apply them to a method.
template <typename... Ts>
struct RunnableMethodArguments final {
  std::tuple<typename ::detail::ParameterStorage<Ts>::Type...> mArguments;
  template <typename... As>
  explicit RunnableMethodArguments(As&&... aArguments)
      : mArguments(std::forward<As>(aArguments)...) {}
  template <class C, typename M>
  decltype(auto) apply(C* o, M m) {
    return std::apply(
        [&o, m](auto&&... args) {
          return ((*o).*m)(args.PassAsParameter()...);
        },
        mArguments);
  }
};

template <typename PtrType, typename Method, bool Owning, RunnableKind Kind,
          typename... Storages>
class RunnableMethodImpl final
    : public ::nsRunnableMethodTraits<PtrType, Method, Owning,
                                      Kind>::base_type {
  typedef typename ::nsRunnableMethodTraits<PtrType, Method, Owning, Kind>
      Traits;

  typedef typename Traits::class_type ClassType;
  typedef typename Traits::base_type BaseType;
  ::nsRunnableMethodReceiver<ClassType, Owning> mReceiver;
  Method mMethod;
  RunnableMethodArguments<Storages...> mArgs;
  using BaseType::CancelTimer;
  using BaseType::GetTimer;

 private:
  virtual ~RunnableMethodImpl() { Revoke(); };
  static void TimedOut(nsITimer* /*aTimer*/, void* aClosure) {
    static_assert(IsIdle(Kind), "Don't use me!");
    RefPtr<CancelableIdleRunnable> r =
        static_cast<CancelableIdleRunnable*>(aClosure);
    r->SetDeadline(TimeStamp());
    r->Run();
    r->Cancel();
  }

 public:
  template <typename ForwardedPtrType, typename... Args>
  explicit RunnableMethodImpl(const char* aName, ForwardedPtrType&& aObj,
                              Method aMethod, Args&&... aArgs)
      : BaseType(aName),
        mReceiver(std::forward<ForwardedPtrType>(aObj)),
        mMethod(aMethod),
        mArgs(std::forward<Args>(aArgs)...) {
    static_assert(sizeof...(Storages) == sizeof...(Args),
                  "Storages and Args should have equal sizes");
  }

  NS_IMETHOD Run() {
    CancelTimer();

    if (MOZ_LIKELY(mReceiver.Get())) {
      mArgs.apply(mReceiver.Get(), mMethod);
    }

    return NS_OK;
  }

  nsresult Cancel() {
    static_assert(Kind >= RunnableKind::Cancelable, "Don't use me!");
    Revoke();
    return NS_OK;
  }

  void Revoke() {
    CancelTimer();
    mReceiver.Revoke();
  }

  void SetDeadline(TimeStamp aDeadline) {
    if (MOZ_LIKELY(mReceiver.Get())) {
      ::detail::SetDeadlineImpl(mReceiver.Get(), aDeadline);
    }
  }

  void SetTimer(uint32_t aDelay, nsIEventTarget* aTarget) {
    MOZ_ASSERT(aTarget);

    if (nsCOMPtr<nsITimer> timer = GetTimer()) {
      timer->Cancel();
      timer->SetTarget(aTarget);
      timer->InitWithNamedFuncCallback(
          TimedOut, this, aDelay, nsITimer::TYPE_ONE_SHOT,
          "detail::RunnableMethodImpl::SetTimer"_ns);
    }
  }
};

// Type aliases for NewRunnableMethod.
template <typename PtrType, typename Method>
using OwningRunnableMethod =
    typename ::nsRunnableMethodTraits<std::remove_reference_t<PtrType>, Method,
                                      true, RunnableKind::Standard>::base_type;
template <typename PtrType, typename Method, typename... Storages>
using OwningRunnableMethodImpl =
    RunnableMethodImpl<std::remove_reference_t<PtrType>, Method, true,
                       RunnableKind::Standard, Storages...>;

// Type aliases for NewCancelableRunnableMethod.
template <typename PtrType, typename Method>
using CancelableRunnableMethod =
    typename ::nsRunnableMethodTraits<std::remove_reference_t<PtrType>, Method,
                                      true,
                                      RunnableKind::Cancelable>::base_type;
template <typename PtrType, typename Method, typename... Storages>
using CancelableRunnableMethodImpl =
    RunnableMethodImpl<std::remove_reference_t<PtrType>, Method, true,
                       RunnableKind::Cancelable, Storages...>;

// Type aliases for NewIdleRunnableMethod.
template <typename PtrType, typename Method>
using IdleRunnableMethod =
    typename ::nsRunnableMethodTraits<std::remove_reference_t<PtrType>, Method,
                                      true, RunnableKind::Idle>::base_type;
template <typename PtrType, typename Method, typename... Storages>
using IdleRunnableMethodImpl =
    RunnableMethodImpl<std::remove_reference_t<PtrType>, Method, true,
                       RunnableKind::Idle, Storages...>;

// Type aliases for NewIdleRunnableMethodWithTimer.
template <typename PtrType, typename Method>
using IdleRunnableMethodWithTimer =
    typename ::nsRunnableMethodTraits<std::remove_reference_t<PtrType>, Method,
                                      true,
                                      RunnableKind::IdleWithTimer>::base_type;
template <typename PtrType, typename Method, typename... Storages>
using IdleRunnableMethodWithTimerImpl =
    RunnableMethodImpl<std::remove_reference_t<PtrType>, Method, true,
                       RunnableKind::IdleWithTimer, Storages...>;

// Type aliases for NewNonOwningRunnableMethod.
template <typename PtrType, typename Method>
using NonOwningRunnableMethod =
    typename ::nsRunnableMethodTraits<std::remove_reference_t<PtrType>, Method,
                                      false, RunnableKind::Standard>::base_type;
template <typename PtrType, typename Method, typename... Storages>
using NonOwningRunnableMethodImpl =
    RunnableMethodImpl<std::remove_reference_t<PtrType>, Method, false,
                       RunnableKind::Standard, Storages...>;

// Type aliases for NonOwningCancelableRunnableMethod
template <typename PtrType, typename Method>
using NonOwningCancelableRunnableMethod =
    typename ::nsRunnableMethodTraits<std::remove_reference_t<PtrType>, Method,
                                      false,
                                      RunnableKind::Cancelable>::base_type;
template <typename PtrType, typename Method, typename... Storages>
using NonOwningCancelableRunnableMethodImpl =
    RunnableMethodImpl<std::remove_reference_t<PtrType>, Method, false,
                       RunnableKind::Cancelable, Storages...>;

// Type aliases for NonOwningIdleRunnableMethod
template <typename PtrType, typename Method>
using NonOwningIdleRunnableMethod =
    typename ::nsRunnableMethodTraits<std::remove_reference_t<PtrType>, Method,
                                      false, RunnableKind::Idle>::base_type;
template <typename PtrType, typename Method, typename... Storages>
using NonOwningIdleRunnableMethodImpl =
    RunnableMethodImpl<std::remove_reference_t<PtrType>, Method, false,
                       RunnableKind::Idle, Storages...>;

// Type aliases for NewIdleRunnableMethodWithTimer.
template <typename PtrType, typename Method>
using NonOwningIdleRunnableMethodWithTimer =
    typename ::nsRunnableMethodTraits<std::remove_reference_t<PtrType>, Method,
                                      false,
                                      RunnableKind::IdleWithTimer>::base_type;
template <typename PtrType, typename Method, typename... Storages>
using NonOwningIdleRunnableMethodWithTimerImpl =
    RunnableMethodImpl<std::remove_reference_t<PtrType>, Method, false,
                       RunnableKind::IdleWithTimer, Storages...>;

}  // namespace detail

// NewRunnableMethod and friends
//
// Very often in Gecko, you'll find yourself in a situation where you want
// to invoke a method (with or without arguments) asynchronously.  You
// could write a small helper class inheriting from nsRunnable to handle
// all these details, or you could let NewRunnableMethod take care of all
// those details for you.
//
// The simplest use of NewRunnableMethod looks like:
//
//   nsCOMPtr<nsIRunnable> event =
//     mozilla::NewRunnableMethod("description", myObject,
//                                &MyClass::HandleEvent);
//   NS_DispatchToCurrentThread(event);
//
// Statically enforced constraints:
//  - myObject must be of (or implicitly convertible to) type MyClass
//  - MyClass must define AddRef and Release methods
//
// The "description" string should specify a human-readable name for the
// runnable; the provided string is used by various introspection tools
// in the browser.
//
// The created runnable will take a strong reference to `myObject`.  For
// non-refcounted objects, or refcounted objects with unusual refcounting
// requirements, and if and only if you are 110% certain that `myObject`
// will live long enough, you can use NewNonOwningRunnableMethod instead,
// which will, as its name implies, take a non-owning reference.  If you
// find yourself having to use this function, you should accompany your use
// with a proof comment describing why the runnable will not lead to
// use-after-frees.
//
// (If you find yourself writing contorted code to Release() an object
// asynchronously on a different thread, you should use the
// NS_ProxyRelease function.)
//
// Invoking a method with arguments takes a little more care.  The
// natural extension of the above:
//
//   nsCOMPtr<nsIRunnable> event =
//     mozilla::NewRunnableMethod("description", myObject,
//                                &MyClass::HandleEvent,
//                                arg1, arg2, ...);
//
// can lead to security hazards (e.g. passing in raw pointers to refcounted
// objects and storing those raw pointers in the runnable).  We therefore
// require you to specify the storage types used by the runnable, just as
// you would if you were writing out the class by hand:
//
//   nsCOMPtr<nsIRunnable> event =
//     mozilla::NewRunnableMethod<RefPtr<T>, nsTArray<U>>
//         ("description", myObject, &MyClass::HandleEvent, arg1, arg2);
//
// Please note that you do not have to pass the same argument type as you
// specify in the template arguments.  For example, if you want to transfer
// ownership to a runnable, you can write:
//
//   RefPtr<T> ptr = ...;
//   nsTArray<U> array = ...;
//   nsCOMPtr<nsIRunnable> event =
//     mozilla::NewRunnableMethod<RefPtr<T>, nsTArray<U>>
//         ("description", myObject, &MyClass::DoSomething,
//          std::move(ptr), std::move(array));
//
// and there will be no extra AddRef/Release traffic, or copying of the array.
//
// Each type that you specify as a template argument to NewRunnableMethod
// comes with its own style of storage in the runnable and its own style
// of argument passing to the invoked method.  See the comment for
// ParameterStorage above for more details.
//
// If you need to customize the storage type and/or argument passing type,
// you can write your own class to use as a template argument to
// NewRunnableMethod.  If you find yourself having to do that frequently,
// please file a bug in Core::XPCOM about adding the custom type to the
// core code in this file, and/or for custom rules for ParameterStorage
// to select that strategy.
//
// For places that require you to use cancelable runnables, such as
// workers, there's also NewCancelableRunnableMethod and its non-owning
// counterpart.  The runnables returned by these methods additionally
// implement nsICancelableRunnable.
//
// Finally, all of the functions discussed above have additional overloads
// that do not take a `const char*` as their first parameter; you may see
// these in older code.  The `const char*` overload is preferred and
// should be used in new code exclusively.

template <typename PtrType, typename Method>
already_AddRefed<detail::OwningRunnableMethod<PtrType, Method>>
NewRunnableMethod(const char* aName, PtrType&& aPtr, Method aMethod) {
  return do_AddRef(new detail::OwningRunnableMethodImpl<PtrType, Method>(
      aName, std::forward<PtrType>(aPtr), aMethod));
}

template <typename PtrType, typename Method>
already_AddRefed<detail::CancelableRunnableMethod<PtrType, Method>>
NewCancelableRunnableMethod(const char* aName, PtrType&& aPtr, Method aMethod) {
  return do_AddRef(new detail::CancelableRunnableMethodImpl<PtrType, Method>(
      aName, std::forward<PtrType>(aPtr), aMethod));
}

template <typename PtrType, typename Method>
already_AddRefed<detail::IdleRunnableMethod<PtrType, Method>>
NewIdleRunnableMethod(const char* aName, PtrType&& aPtr, Method aMethod) {
  return do_AddRef(new detail::IdleRunnableMethodImpl<PtrType, Method>(
      aName, std::forward<PtrType>(aPtr), aMethod));
}

template <typename PtrType, typename Method>
already_AddRefed<detail::IdleRunnableMethodWithTimer<PtrType, Method>>
NewIdleRunnableMethodWithTimer(const char* aName, PtrType&& aPtr,
                               Method aMethod) {
  return do_AddRef(new detail::IdleRunnableMethodWithTimerImpl<PtrType, Method>(
      aName, std::forward<PtrType>(aPtr), aMethod));
}

template <typename PtrType, typename Method>
already_AddRefed<detail::NonOwningRunnableMethod<PtrType, Method>>
NewNonOwningRunnableMethod(const char* aName, PtrType&& aPtr, Method aMethod) {
  return do_AddRef(new detail::NonOwningRunnableMethodImpl<PtrType, Method>(
      aName, std::forward<PtrType>(aPtr), aMethod));
}

template <typename PtrType, typename Method>
already_AddRefed<detail::NonOwningCancelableRunnableMethod<PtrType, Method>>
NewNonOwningCancelableRunnableMethod(const char* aName, PtrType&& aPtr,
                                     Method aMethod) {
  return do_AddRef(
      new detail::NonOwningCancelableRunnableMethodImpl<PtrType, Method>(
          aName, std::forward<PtrType>(aPtr), aMethod));
}

template <typename PtrType, typename Method>
already_AddRefed<detail::NonOwningIdleRunnableMethod<PtrType, Method>>
NewNonOwningIdleRunnableMethod(const char* aName, PtrType&& aPtr,
                               Method aMethod) {
  return do_AddRef(new detail::NonOwningIdleRunnableMethodImpl<PtrType, Method>(
      aName, std::forward<PtrType>(aPtr), aMethod));
}

template <typename PtrType, typename Method>
already_AddRefed<detail::NonOwningIdleRunnableMethodWithTimer<PtrType, Method>>
NewNonOwningIdleRunnableMethodWithTimer(const char* aName, PtrType&& aPtr,
                                        Method aMethod) {
  return do_AddRef(
      new detail::NonOwningIdleRunnableMethodWithTimerImpl<PtrType, Method>(
          aName, std::forward<PtrType>(aPtr), aMethod));
}

// Similar to NewRunnableMethod. Call like so:
// nsCOMPtr<nsIRunnable> event =
//   NewRunnableMethod<Types,...>(myObject, &MyClass::HandleEvent, myArg1,...);
// 'Types' are the stored type for each argument, see ParameterStorage for
// details.
template <typename... Storages, typename PtrType, typename Method,
          typename... Args>
already_AddRefed<detail::OwningRunnableMethod<PtrType, Method>>
NewRunnableMethod(const char* aName, PtrType&& aPtr, Method aMethod,
                  Args&&... aArgs) {
  static_assert(sizeof...(Storages) == sizeof...(Args),
                "<Storages...> size should be equal to number of arguments");
  return do_AddRef(
      new detail::OwningRunnableMethodImpl<PtrType, Method, Storages...>(
          aName, std::forward<PtrType>(aPtr), aMethod,
          std::forward<Args>(aArgs)...));
}

template <typename... Storages, typename PtrType, typename Method,
          typename... Args>
already_AddRefed<detail::NonOwningRunnableMethod<PtrType, Method>>
NewNonOwningRunnableMethod(const char* aName, PtrType&& aPtr, Method aMethod,
                           Args&&... aArgs) {
  static_assert(sizeof...(Storages) == sizeof...(Args),
                "<Storages...> size should be equal to number of arguments");
  return do_AddRef(
      new detail::NonOwningRunnableMethodImpl<PtrType, Method, Storages...>(
          aName, std::forward<PtrType>(aPtr), aMethod,
          std::forward<Args>(aArgs)...));
}

template <typename... Storages, typename PtrType, typename Method,
          typename... Args>
already_AddRefed<detail::CancelableRunnableMethod<PtrType, Method>>
NewCancelableRunnableMethod(const char* aName, PtrType&& aPtr, Method aMethod,
                            Args&&... aArgs) {
  static_assert(sizeof...(Storages) == sizeof...(Args),
                "<Storages...> size should be equal to number of arguments");
  return do_AddRef(
      new detail::CancelableRunnableMethodImpl<PtrType, Method, Storages...>(
          aName, std::forward<PtrType>(aPtr), aMethod,
          std::forward<Args>(aArgs)...));
}

template <typename... Storages, typename PtrType, typename Method,
          typename... Args>
already_AddRefed<detail::NonOwningCancelableRunnableMethod<PtrType, Method>>
NewNonOwningCancelableRunnableMethod(const char* aName, PtrType&& aPtr,
                                     Method aMethod, Args&&... aArgs) {
  static_assert(sizeof...(Storages) == sizeof...(Args),
                "<Storages...> size should be equal to number of arguments");
  return do_AddRef(
      new detail::NonOwningCancelableRunnableMethodImpl<PtrType, Method,
                                                        Storages...>(
          aName, std::forward<PtrType>(aPtr), aMethod,
          std::forward<Args>(aArgs)...));
}

template <typename... Storages, typename PtrType, typename Method,
          typename... Args>
already_AddRefed<detail::IdleRunnableMethod<PtrType, Method>>
NewIdleRunnableMethod(const char* aName, PtrType&& aPtr, Method aMethod,
                      Args&&... aArgs) {
  static_assert(sizeof...(Storages) == sizeof...(Args),
                "<Storages...> size should be equal to number of arguments");
  return do_AddRef(
      new detail::IdleRunnableMethodImpl<PtrType, Method, Storages...>(
          aName, std::forward<PtrType>(aPtr), aMethod,
          std::forward<Args>(aArgs)...));
}

template <typename... Storages, typename PtrType, typename Method,
          typename... Args>
already_AddRefed<detail::NonOwningIdleRunnableMethod<PtrType, Method>>
NewNonOwningIdleRunnableMethod(const char* aName, PtrType&& aPtr,
                               Method aMethod, Args&&... aArgs) {
  static_assert(sizeof...(Storages) == sizeof...(Args),
                "<Storages...> size should be equal to number of arguments");
  return do_AddRef(
      new detail::NonOwningIdleRunnableMethodImpl<PtrType, Method, Storages...>(
          aName, std::forward<PtrType>(aPtr), aMethod,
          std::forward<Args>(aArgs)...));
}

}  // namespace mozilla

#endif  // XPCOM_GLUE_AVOID_NSPR

// This class is designed to be used when you have an event class E that has a
// pointer back to resource class R.  If R goes away while E is still pending,
// then it is important to "revoke" E so that it does not try use R after R has
// been destroyed.  nsRevocableEventPtr makes it easy for R to manage such
// situations:
//
//   class R;
//
//   class E : public mozilla::Runnable {
//   public:
//     void Revoke() {
//       mResource = nullptr;
//     }
//   private:
//     R *mResource;
//   };
//
//   class R {
//   public:
//     void EventHandled() {
//       mEvent.Forget();
//     }
//   private:
//     nsRevocableEventPtr<E> mEvent;
//   };
//
//   void R::PostEvent() {
//     // Make sure any pending event is revoked.
//     mEvent->Revoke();
//
//     nsCOMPtr<nsIRunnable> event = new E();
//     if (NS_SUCCEEDED(NS_DispatchToCurrentThread(event))) {
//       // Keep pointer to event so we can revoke it.
//       mEvent = event;
//     }
//   }
//
//   NS_IMETHODIMP E::Run() {
//     if (!mResource)
//       return NS_OK;
//     ...
//     mResource->EventHandled();
//     return NS_OK;
//   }
//
template <class T>
class nsRevocableEventPtr {
 public:
  nsRevocableEventPtr() : mEvent(nullptr) {}
  ~nsRevocableEventPtr() { Revoke(); }

  const nsRevocableEventPtr& operator=(RefPtr<T>&& aEvent) {
    if (mEvent != aEvent) {
      Revoke();
      mEvent = std::move(aEvent);
    }
    return *this;
  }

  void Revoke() {
    if (mEvent) {
      mEvent->Revoke();
      mEvent = nullptr;
    }
  }

  void Forget() { mEvent = nullptr; }
  bool IsPending() { return mEvent != nullptr; }
  T* get() { return mEvent; }

  nsRevocableEventPtr(const nsRevocableEventPtr&) = delete;
  nsRevocableEventPtr& operator=(const nsRevocableEventPtr&) = delete;

 private:
  RefPtr<T> mEvent;
};

template <class T>
inline already_AddRefed<T> do_AddRef(nsRevocableEventPtr<T>& aObj) {
  return do_AddRef(aObj.get());
}

/**
 * A simple helper to suffix thread pool name
 * with incremental numbers.
 */
class nsThreadPoolNaming {
 public:
  nsThreadPoolNaming() = default;

  /**
   * Returns a thread name as "<aPoolName> #<n>" and increments the counter.
   */
  nsCString GetNextThreadName(const nsACString& aPoolName);

  template <size_t LEN>
  nsCString GetNextThreadName(const char (&aPoolName)[LEN]) {
    return GetNextThreadName(nsDependentCString(aPoolName, LEN - 1));
  }
  nsThreadPoolNaming(const nsThreadPoolNaming&) = delete;
  void operator=(const nsThreadPoolNaming&) = delete;

 private:
  mozilla::Atomic<uint32_t> mCounter{0};
};

/**
 * Thread priority in most operating systems affect scheduling, not IO.  This
 * helper is used to set the current thread to low IO priority for the lifetime
 * of the created object.  You can only use this low priority IO setting within
 * the context of the current thread.
 */
class MOZ_STACK_CLASS nsAutoLowPriorityIO {
 public:
  nsAutoLowPriorityIO();
  ~nsAutoLowPriorityIO();

 private:
  bool lowIOPrioritySet;
#if defined(XP_MACOSX) || (defined(XP_LINUX) && !defined(ANDROID))
  int oldPriority;
#endif
};

void NS_SetMainThread();

// Used only on cooperatively scheduled "main" threads. Causes the thread to be
// considered a main thread and also causes GetCurrentVirtualThread to return
// aVirtualThread.
void NS_SetMainThread(PRThread* aVirtualThread);

// Used only on cooperatively scheduled "main" threads. Causes the thread to no
// longer be considered a main thread. Also causes GetCurrentVirtualThread() to
// return a unique value.
void NS_UnsetMainThread();

/**
 * Return the expiration time of the next timer to run on the current
 * thread.  If that expiration time is greater than aDefault, then
 * return aDefault.  aSearchBound specifies a maximum number of timers
 * to examine to find a timer on the current thread.  If no timer that
 * will run on the current thread is found after examining
 * aSearchBound timers, return the highest seen expiration time as a
 * best effort guess.
 *
 * Timers with either the type nsITimer::TYPE_ONE_SHOT_LOW_PRIORITY or
 * nsITIMER::TYPE_REPEATING_SLACK_LOW_PRIORITY will be skipped when
 * searching for the next expiration time.  This enables timers to
 * have lower priority than callbacks dispatched from
 * nsIThread::IdleDispatch.
 */
extern mozilla::TimeStamp NS_GetTimerDeadlineHintOnCurrentThread(
    mozilla::TimeStamp aDefault, uint32_t aSearchBound);

/**
 * Dispatches the given event to a background thread.  The primary benefit of
 * this API is that you do not have to manage the lifetime of your own thread
 * for running your own events; the thread manager will take care of the
 * background thread's lifetime.  Not having to manage your own thread also
 * means less resource usage, as the underlying implementation here can manage
 * spinning up and shutting down threads appropriately.
 *
 * NOTE: there is no guarantee that events dispatched via these APIs are run
 * serially, in dispatch order; several dispatched events may run in parallel.
 * If you depend on serial execution of dispatched events, you should use
 * NS_CreateBackgroundTaskQueue instead, and dispatch events to the returned
 * event target.
 */
extern nsresult NS_DispatchBackgroundTask(
    already_AddRefed<nsIRunnable> aEvent,
    nsIEventTarget::DispatchFlags aDispatchFlags = NS_DISPATCH_NORMAL);
extern "C" nsresult NS_DispatchBackgroundTask(
    nsIRunnable* aEvent,
    nsIEventTarget::DispatchFlags aDispatchFlags = NS_DISPATCH_NORMAL);

/**
 * Obtain a new serial event target that dispatches runnables to a background
 * thread.  In many cases, this is a straight replacement for creating your
 * own, private thread, and is generally preferred to creating your own,
 * private thread.
 */
extern "C" nsresult NS_CreateBackgroundTaskQueue(
    mozilla::StaticString aName, nsISerialEventTarget** aTarget);

/**
 * Dispatch the given runnable to the given event target, spinning the current
 * thread's event loop until the runnable has finished executing.
 *
 * This is roughly equivalent to the previously-supported `NS_DISPATCH_SYNC`
 * flag.
 */
extern nsresult NS_DispatchAndSpinEventLoopUntilComplete(
    const nsACString& aVeryGoodReasonToDoThis, nsIEventTarget* aEventTarget,
    already_AddRefed<nsIRunnable> aEvent);

// Predeclaration for logging function below
namespace IPC {
class Message;
class MessageReader;
class MessageWriter;
}  // namespace IPC

class nsTimerImpl;

namespace mozilla {

// RAII class that will set the TLS entry to return the currently running
// nsISerialEventTarget.
// It should be used from inner event loop implementation.
class SerialEventTargetGuard {
 public:
  explicit SerialEventTargetGuard(nsISerialEventTarget* aThread)
      : mLastCurrentThread(sCurrentThreadTLS.get()) {
    Set(aThread);
  }

  ~SerialEventTargetGuard() { sCurrentThreadTLS.set(mLastCurrentThread); }

  static void InitTLS();
  static nsISerialEventTarget* GetCurrentSerialEventTarget() {
    return sCurrentThreadTLS.get();
  }

 protected:
  friend class ::MessageLoop;
  static void Set(nsISerialEventTarget* aThread) {
    MOZ_ASSERT(aThread->IsOnCurrentThread());
    sCurrentThreadTLS.set(aThread);
  }

 private:
  static MOZ_THREAD_LOCAL(nsISerialEventTarget*) sCurrentThreadTLS;
  nsISerialEventTarget* mLastCurrentThread;
};

// Get the serial event target corresponding to the currently executing task
// queue or thread. This method will assert if called on a thread pool without
// an active task queue.
//
// This function should generally be preferred over NS_GetCurrentThread since it
// will return a more useful answer when called from a task queue running on a
// thread pool or on a non-xpcom thread which accepts runnable dispatches.
//
// NOTE: The returned nsISerialEventTarget may not accept runnable dispatches
// (e.g. if it corresponds to a non-xpcom thread), however it may still be used
// to check if you're on the given thread/queue using IsOnCurrentThread().

nsISerialEventTarget* GetCurrentSerialEventTarget();

// Get a weak reference to a serial event target which can be used to dispatch
// runnables to the main thread.
//
// NOTE: While this is currently a weak pointer to the nsIThread* returned from
// NS_GetMainThread(), this may change in the future.

nsISerialEventTarget* GetMainThreadSerialEventTarget();

// Returns the number of CPUs, like PR_GetNumberOfProcessors, except
// that it can return a cached value on platforms where sandboxing
// would prevent reading the current value (currently Linux).  CPU
// hotplugging is uncommon, so this is unlikely to make a difference
// in practice.
size_t GetNumberOfProcessors();

/**
 * A helper class to log tasks dispatch and run with "MOZ_LOG=events:1".  The
 * output is more machine readable and creates a link between dispatch and run.
 *
 * Usage example for the concrete template type nsIRunnable.
 * To log a dispatch, which means putting an event to a queue:
 *   LogRunnable::LogDispatch(event);
 *   theQueue.putEvent(event);
 *
 * To log execution (running) of the event:
 *   nsCOMPtr<nsIRunnable> event = theQueue.popEvent();
 *   {
 *     LogRunnable::Run log(event);
 *     event->Run();
 *     event = null;  // to include the destructor code in the span
 *   }
 *
 * The class is a template so that we can support various specific super-types
 * of tasks in the future.  We can't use void* because it may cast differently
 * and tracking the pointer in logs would then be impossible.
 */
template <typename T>
class LogTaskBase {
 public:
  LogTaskBase() = delete;

  // Adds a simple log about dispatch of this runnable.
  static void LogDispatch(T* aEvent);
  // The `aContext` pointer adds another uniqe identifier, nothing more
  static void LogDispatch(T* aEvent, void* aContext);

  // Logs dispatch of the message and along that also the PID of the target
  // proccess, purposed for uniquely identifying IPC messages.
  static void LogDispatchWithPid(T* aEvent, int32_t aPid);

  // This is designed to surround a call to `Run()` or any code representing
  // execution of the task body.
  // The constructor adds a simple log about start of the runnable execution and
  // the destructor adds a log about ending the execution.
  class MOZ_RAII Run {
   public:
    Run() = delete;
    explicit Run(T* aEvent, bool aWillRunAgain = false);
    explicit Run(T* aEvent, void* aContext, bool aWillRunAgain = false);
    ~Run();

    // When this is called, the log in this RAII dtor will only say
    // "interrupted" expecting that the event will run again.
    void WillRunAgain() { mWillRunAgain = true; }

   private:
    bool mWillRunAgain = false;
  };
};

class MicroTaskRunnable;
class MustConsumeMicroTask;
class Task;  // TaskController
class PresShell;
namespace dom {
class FrameRequestCallback;
class VideoFrameRequestCallback;
}  // namespace dom

// Specialized methods must be explicitly predeclared.
template <>
LogTaskBase<nsIRunnable>::Run::Run(nsIRunnable* aEvent, bool aWillRunAgain);
template <>
LogTaskBase<Task>::Run::Run(Task* aTask, bool aWillRunAgain);
template <>
void LogTaskBase<IPC::Message>::LogDispatchWithPid(IPC::Message* aEvent,
                                                   int32_t aPid);
template <>
LogTaskBase<IPC::Message>::Run::Run(IPC::Message* aMessage, bool aWillRunAgain);
template <>
LogTaskBase<nsTimerImpl>::Run::Run(nsTimerImpl* aEvent, bool aWillRunAgain);

typedef LogTaskBase<nsIRunnable> LogRunnable;
typedef LogTaskBase<MicroTaskRunnable> LogMicroTaskRunnable;
typedef LogTaskBase<MustConsumeMicroTask> LogMustConsumeMicroTask;
typedef LogTaskBase<IPC::Message> LogIPCMessage;
typedef LogTaskBase<nsTimerImpl> LogTimerEvent;
typedef LogTaskBase<Task> LogTask;
typedef LogTaskBase<PresShell> LogPresShellObserver;
typedef LogTaskBase<dom::FrameRequestCallback> LogFrameRequestCallback;
typedef LogTaskBase<dom::VideoFrameRequestCallback>
    LogVideoFrameRequestCallback;
// If you add new types don't forget to add:
// `template class LogTaskBase<YourType>;` to nsThreadUtils.cpp

}  // namespace mozilla

#endif  // nsThreadUtils_h_
