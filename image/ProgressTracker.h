/*
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef mozilla_image_ProgressTracker_h
#define mozilla_image_ProgressTracker_h

#include "CopyOnWrite.h"
#include "IProgressObserver.h"
#include "mozilla/Mutex.h"
#include "mozilla/RefPtr.h"
#include "mozilla/WeakPtr.h"
#include "nsCOMPtr.h"
#include "nsRect.h"
#include "nsTHashMap.h"
#include "nsTObserverArray.h"
#include "nsThreadUtils.h"

class nsIRunnable;

namespace mozilla {
namespace image {

class AsyncNotifyRunnable;
class AsyncNotifyCurrentStateRunnable;
class Image;

/**
 * Image progress bitflags.
 *
 * See CheckProgressConsistency() for the invariants we enforce about the
 * ordering dependencies between these flags.
 */
enum {
  FLAG_SIZE_AVAILABLE = 1u << 0,    // STATUS_SIZE_AVAILABLE
  FLAG_DECODE_COMPLETE = 1u << 1,   // STATUS_DECODE_COMPLETE
  FLAG_FRAME_COMPLETE = 1u << 2,    // STATUS_FRAME_COMPLETE
  FLAG_LOAD_COMPLETE = 1u << 3,     // STATUS_LOAD_COMPLETE
  FLAG_IS_ANIMATED = 1u << 6,       // STATUS_IS_ANIMATED
  FLAG_HAS_TRANSPARENCY = 1u << 7,  // STATUS_HAS_TRANSPARENCY
  FLAG_LAST_PART_COMPLETE = 1u << 8,
  FLAG_HAS_ERROR = 1u << 9  // STATUS_ERROR
};

typedef uint32_t Progress;

const uint32_t NoProgress = 0;

inline Progress LoadCompleteProgress(bool aLastPart, bool aError,
                                     nsresult aStatus) {
  Progress progress = FLAG_LOAD_COMPLETE;
  if (aLastPart) {
    progress |= FLAG_LAST_PART_COMPLETE;
  }
  if (NS_FAILED(aStatus) || aError) {
    progress |= FLAG_HAS_ERROR;
  }
  return progress;
}

/**
 * ProgressTracker stores its observers in an ObserverTable, which is a hash
 * table mapping raw pointers to WeakPtr's to the same objects. This sounds like
 * unnecessary duplication of information, but it's necessary for stable hash
 * values since WeakPtr's lose the knowledge of which object they used to point
 * to when that object is destroyed.
 *
 * ObserverTable subclasses nsTHashMap to add reference counting
 * support and a copy constructor, both of which are needed for use with
 * CopyOnWrite<T>.
 */
class ObserverTable : public nsTHashMap<nsPtrHashKey<IProgressObserver>,
                                        WeakPtr<IProgressObserver>> {
 public:
  NS_INLINE_DECL_REFCOUNTING(ObserverTable);

  ObserverTable() = default;

  ObserverTable(const ObserverTable& aOther)
      : nsTHashMap<nsPtrHashKey<IProgressObserver>, WeakPtr<IProgressObserver>>(
            aOther.Clone()) {
    NS_WARNING("Forced to copy ObserverTable due to nested notifications");
  }

 private:
  ~ObserverTable() = default;
};

/**
 * ProgressTracker is a class that records an Image's progress through the
 * loading and decoding process, and makes it possible to send notifications to
 * IProgressObservers, both synchronously and asynchronously.
 *
 * When a new observer needs to be notified of the current progress of an image,
 * call the Notify() method on this class with the relevant observer as its
 * argument, and the notifications will be replayed to the observer
 * asynchronously.
 */
class ProgressTracker : public mozilla::SupportsWeakPtr {
  virtual ~ProgressTracker() = default;

 public:
  NS_INLINE_DECL_THREADSAFE_REFCOUNTING(ProgressTracker)

  ProgressTracker();
  ProgressTracker(const ProgressTracker& aOther) = delete;

  bool HasImage() const {
    MutexAutoLock lock(mMutex);
    return mImage;
  }
  already_AddRefed<Image> GetImage() const;

  // Get the current image status (as in imgIRequest).
  uint32_t GetImageStatus() const;

  // Get the current Progress.
  Progress GetProgress() const { return mProgress; }

  // Schedule an asynchronous "replaying" of all the notifications that would
  // have to happen to put us in the current state.
  // We will also take note of any notifications that happen between the time
  // Notify() is called and when we call SyncNotify on |aObserver|, and replay
  // them as well.
  // Should be called on the main thread only, since observers and GetURI are
  // not threadsafe.
  void Notify(IProgressObserver* aObserver);

  // Schedule an asynchronous "replaying" of all the notifications that would
  // have to happen to put us in the state we are in right now.
  // Unlike Notify(), does *not* take into account future notifications.
  // This is only useful if you do not have an imgRequest, e.g., if you are a
  // static request returned from imgIRequest::GetStaticRequest().
  // Should be called on the main thread only, since observers and GetURI are
  // not threadsafe.
  void NotifyCurrentState(IProgressObserver* aObserver);

  // "Replay" all of the notifications that would have to happen to put us in
  // the state we're currently in.
  // Only use this if you're already servicing an asynchronous call (e.g.
  // OnStartRequest).
  // Should be called on the main thread only, since observers and GetURI are
  // not threadsafe.
  void SyncNotify(IProgressObserver* aObserver);

  // Get this ProgressTracker ready for a new request. This resets all the
  // state that doesn't persist between requests.
  void ResetForNewRequest();

  // Stateless notifications. These are dispatched and immediately forgotten
  // about. All of these notifications are main thread only.
  void OnDiscard();
  void OnUnlockedDraw();
  void OnImageAvailable();

  // Compute the difference between this our progress and aProgress. This allows
  // callers to predict whether SyncNotifyProgress will send any notifications.
  Progress Difference(Progress aProgress) const {
    return ~mProgress & aProgress;
  }

  // Update our state to incorporate the changes in aProgress and synchronously
  // notify our observers.
  //
  // Because this may result in recursive notifications, no decoding locks may
  // be held.  Called on the main thread only.
  void SyncNotifyProgress(Progress aProgress,
                          const nsIntRect& aInvalidRect = nsIntRect());

  // We manage a set of observers that are using an image and thus concerned
  // with its loading progress. Weak pointers.
  void AddObserver(IProgressObserver* aObserver);
  // If aEmulateRequestFinished is false, aObserver won't be sent a load
  // complete notification even if the load hasn't completed.
  bool RemoveObserver(IProgressObserver* aObserver,
                      bool aEmulateRequestFinished = true);
  uint32_t ObserverCount() const;

  // Resets our weak reference to our image. Image subclasses should call this
  // in their destructor.
  void ResetImage();

  // Tell this progress tracker that it is for a multipart image.
  void SetIsMultipart() { mIsMultipart = true; }

 private:
  friend class AsyncNotifyRunnable;
  friend class AsyncNotifyCurrentStateRunnable;
  friend class ImageFactory;

  // Sets our weak reference to our image. Only ImageFactory should call this.
  void SetImage(Image* aImage);

  // Send some notifications that would be necessary to make |aObserver| believe
  // the request is finished downloading and decoding.  We only send
  // FLAG_LOAD_COMPLETE and FLAG_ONLOAD_UNBLOCKED, and only if necessary.
  void EmulateRequestFinished(IProgressObserver* aObserver);

  // Main thread only because it deals with the observer service.
  void FireFailureNotification();

  // Wrapper for AsyncNotifyRunnable to make it have medium high priority like
  // other imagelib runnables.
  class RenderBlockingRunnable final : public PrioritizableRunnable {
    explicit RenderBlockingRunnable(
        already_AddRefed<AsyncNotifyRunnable> aEvent);
    virtual ~RenderBlockingRunnable() = default;

   public:
    void AddObserver(IProgressObserver* aObserver);
    void RemoveObserver(IProgressObserver* aObserver);

    static already_AddRefed<RenderBlockingRunnable> Create(
        already_AddRefed<AsyncNotifyRunnable> aEvent);
  };

  // The runnable, if any, that we've scheduled to deliver async notifications.
  RefPtr<RenderBlockingRunnable> mRunnable;

  // mMutex protects access to mImage and mEventTarget.
  mutable Mutex mMutex MOZ_UNANNOTATED;

  // mImage is a weak ref; it should be set to null when the image goes out of
  // scope.
  Image* mImage;

  // Hashtable of observers attached to the image. Each observer represents a
  // consumer using the image. Main thread only.
  CopyOnWrite<ObserverTable> mObservers;

  Progress mProgress;

  // Whether this is a progress tracker for a multipart image.
  bool mIsMultipart;
};

}  // namespace image
}  // namespace mozilla

#endif  // mozilla_image_ProgressTracker_h
