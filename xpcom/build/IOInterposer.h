/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef mozilla_IOInterposer_h
#define mozilla_IOInterposer_h

#include "mozilla/Attributes.h"
#include "mozilla/TimeStamp.h"
#include "nsString.h"

namespace mozilla {

/**
 * Interface for I/O interposer observers. This is separate from the
 * IOInterposer because we have multiple uses for these observations.
 */
class IOInterposeObserver {
 public:
  enum Operation {
    OpNone = 0,
    OpCreateOrOpen = (1 << 0),
    OpRead = (1 << 1),
    OpWrite = (1 << 2),
    OpFSync = (1 << 3),
    OpStat = (1 << 4),
    OpClose = (1 << 5),
    OpNextStage =
        (1 << 6),  // Meta - used when leaving startup, entering shutdown
    OpWriteFSync = (OpWrite | OpFSync),
    OpAll = (OpCreateOrOpen | OpRead | OpWrite | OpFSync | OpStat | OpClose),
    OpAllWithStaging = (OpAll | OpNextStage)
  };

  /** A representation of an I/O observation  */
  class Observation {
   protected:
    /**
     * This constructor is for use by subclasses that are intended to take
     * timing measurements via RAII. The |aShouldReport| parameter may be
     * used to make the measurement and reporting conditional on the
     * satisfaction of an arbitrary predicate that was evaluated
     * in the subclass. Note that IOInterposer::IsObservedOperation() is
     * always ANDed with aShouldReport, so the subclass does not need to
     * include a call to that function explicitly.
     */
    Observation(Operation aOperation, const char* aReference,
                bool aShouldReport = true);

   public:
    /**
     * Since this constructor accepts start and end times, it does *not* take
     * its own timings, nor does it report itself.
     */
    Observation(Operation aOperation, const TimeStamp& aStart,
                const TimeStamp& aEnd, const char* aReference);

    /**
     * Operation observed, this is one of the individual Operation values.
     * Combinations of these flags are only used when registering observers.
     */
    Operation ObservedOperation() const { return mOperation; }

    /**
     * Return the observed operation as a human-readable string.
     */
    const char* ObservedOperationString() const;

    /** Time at which the I/O operation was started */
    TimeStamp Start() const { return mStart; }

    /**
     * Time at which the I/O operation ended, for asynchronous methods this is
     * the time at which the call initiating the asynchronous request returned.
     */
    TimeStamp End() const { return mEnd; }

    /**
     * Duration of the operation, for asynchronous I/O methods this is the
     * duration of the call initiating the asynchronous request.
     */
    TimeDuration Duration() const { return mEnd - mStart; }

    /**
     * IO reference, function name or name of component (sqlite) that did IO
     * this is in addition the generic operation. This attribute may be platform
     * specific, but should only take a finite number of distinct values.
     * E.g. sqlite-commit, CreateFile, NtReadFile, fread, fsync, mmap, etc.
     * I.e. typically the platform specific function that did the IO.
     */
    const char* Reference() const { return mReference; }

    virtual const char* FileType() const { return "File"; }

    /** Request filename associated with the I/O operation, empty if unknown */
    virtual void Filename(nsAString& aString) { aString.Truncate(); }

    virtual ~Observation() = default;

   protected:
    void Report();

    Operation mOperation;
    TimeStamp mStart;
    TimeStamp mEnd;
    const char* mReference;  // Identifies the source of the Observation
    bool mShouldReport;      // Measure and report if true
  };

  /**
   * Invoked whenever an implementation of the IOInterposeObserver should
   * observe aObservation. Implement this and do your thing...
   * But do consider if it is wise to use IO functions in this method, they are
   * likely to cause recursion :)
   * At least, see PoisonIOInterposer.h and register your handle as a debug file
   * even, if you don't initialize the poison IO interposer, someone else might.
   *
   * Remark: Observations may occur on any thread.
   */
  virtual void Observe(Observation& aObservation) = 0;

  virtual ~IOInterposeObserver() = default;

 protected:
  /**
   * We don't use NS_IsMainThread() because we need to be able to determine the
   * main thread outside of XPCOM Initialization. IOInterposer observers should
   * call this function instead.
   */
  static bool IsMainThread();
};

/**
 * These functions are responsible for ensuring that events are routed to the
 * appropriate observers.
 */
namespace IOInterposer {

/**
 * This function must be called from the main-thread when no other threads are
 * running before any of the other methods on this class may be used.
 *
 * IO reports can however, safely assume that IsObservedOperation() will
 * return false until the IOInterposer is initialized.
 *
 * Remark, it's safe to call this method multiple times, so just call it when
 * you to utilize IO interposing.
 *
 * Using the IOInterposerInit class is preferred to calling this directly.
 */
bool Init();

/**
 * This function must be called from the main thread, and furthermore
 * it must be called when no other threads are executing. Effectively
 * restricting us to calling it only during shutdown.
 *
 * Callers should take care that no other consumers are subscribed to events,
 * as these events will stop when this function is called.
 *
 * In practice, we don't use this method as the IOInterposer is used for
 * late-write checks.
 */
void Clear();

/**
 * This function immediately disables IOInterposer functionality in a fast,
 * thread-safe manner. Primarily for use by the crash reporter.
 */
void Disable();

/**
 * This function re-enables IOInterposer functionality in a fast, thread-safe
 * manner.  Primarily for use by the crash reporter.
 */
void Enable();

/**
 * Report IO to registered observers.
 * Notice that the reported operation must be either OpRead, OpWrite or
 * OpFSync. You are not allowed to report an observation with OpWriteFSync or
 * OpAll, these are just auxiliary values for use with Register().
 *
 * If the IO call you're reporting does multiple things, write and fsync, you
 * can choose to call Report() twice once with write and once with FSync. You
 * may not call Report() with OpWriteFSync! The Observation::mOperation
 * attribute is meant to be generic, not perfect.
 *
 * Notice that there is no reason to report an observation with an operation
 * which is not being observed. Use IsObservedOperation() to check if the
 * operation you are about to report is being observed. This is especially
 * important if you are constructing expensive observations containing
 * filename and full-path.
 *
 * Remark: Init() must be called before any IO is reported. But
 * IsObservedOperation() will return false until Init() is called.
 */
void Report(IOInterposeObserver::Observation& aObservation);

/**
 * Return whether or not an operation is observed. Reporters should not
 * report operations that are not being observed by anybody. This mechanism
 * allows us to avoid reporting I/O when no observers are registered.
 */
bool IsObservedOperation(IOInterposeObserver::Operation aOp);

/**
 * Register IOInterposeObserver, the observer object will receive all
 * observations for the given operation aOp.
 *
 * Remarks:
 * - Init() must be called before observers are registered.
 * - The IOInterposeObserver object should be static, because it could still be
 *   used on another thread shortly after Unregister().
 */
void Register(IOInterposeObserver::Operation aOp,
              IOInterposeObserver* aStaticObserver);

/**
 * Unregister an IOInterposeObserver for a given operation
 * Remark: It is always safe to unregister for all operations, even if yoú
 * didn't register for them all.
 * I.e. IOInterposer::Unregister(IOInterposeObserver::OpAll, aObserver)
 *
 * Remarks:
 * - Init() must be called before observers are registered.
 * - The IOInterposeObserver object should be static, because it could still be
 *   used on another thread shortly after this Unregister() call.
 */
void Unregister(IOInterposeObserver::Operation aOp,
                IOInterposeObserver* aStaticObserver);

/**
 * Registers the current thread with the IOInterposer. This must be done to
 * ensure that per-thread data is created in an orderly fashion.
 * We could have written this to initialize that data lazily, however this
 * could have unintended consequences if a thread that is not aware of
 * IOInterposer was implicitly registered: its per-thread data would never
 * be deleted because it would not know to unregister itself.
 *
 * @param aIsMainThread true if IOInterposer should treat the current thread
 *                      as the main thread.
 */
void RegisterCurrentThread(bool aIsMainThread = false);

/**
 * Unregisters the current thread with the IOInterposer. This is important
 * to call when a thread is shutting down because it cleans up data that
 * is stored in a TLS slot.
 */
void UnregisterCurrentThread();

/**
 * Called to inform observers that the process has transitioned out of the
 * startup stage or into the shutdown stage. Main thread only.
 */
void EnteringNextStage();

}  // namespace IOInterposer

class MOZ_RAII AutoIOInterposer {
 public:
  AutoIOInterposer() = default;

  void Init() {
#if defined(NIGHTLY_BUILD)
    IOInterposer::Init();
#endif
  }

  ~AutoIOInterposer() {
#if defined(NIGHTLY_BUILD)
    IOInterposer::Clear();
#endif
  }
};

class MOZ_RAII AutoIOInterposerDisable final {
 public:
  explicit AutoIOInterposerDisable() { IOInterposer::Disable(); }
  ~AutoIOInterposerDisable() { IOInterposer::Enable(); }

 private:
};

}  // namespace mozilla

#endif  // mozilla_IOInterposer_h
