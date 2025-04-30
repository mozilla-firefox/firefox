/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/*
 * Code to notify things that animate before a refresh, at an appropriate
 * refresh rate.  (Perhaps temporary, until replaced by compositor.)
 */

#ifndef nsRefreshDriver_h_
#define nsRefreshDriver_h_

#include "mozilla/FlushType.h"
#include "mozilla/RenderingPhase.h"
#include "mozilla/TimeStamp.h"
#include "mozilla/UniquePtr.h"
#include "mozilla/WeakPtr.h"
#include "nsTObserverArray.h"
#include "nsTArray.h"
#include "nsTHashSet.h"
#include "nsClassHashtable.h"
#include "nsHashKeys.h"
#include "nsRefreshObservers.h"
#include "nsThreadUtils.h"
#include "mozilla/Attributes.h"
#include "mozilla/Maybe.h"
#include "mozilla/layers/TransactionIdAllocator.h"
#include "LayersTypes.h"

#include "GeckoProfiler.h"  // for ProfileChunkedBuffer

class nsPresContext;

class imgIRequest;
class nsIRunnable;

struct DocumentFrameCallbacks;

namespace mozilla {
class AnimationEventDispatcher;
class PresShell;
class RefreshDriverTimer;
class Runnable;
class Task;

namespace dom {
class Document;
}

}  // namespace mozilla

class nsRefreshDriver final : public mozilla::layers::TransactionIdAllocator,
                              public nsARefreshObserver {
  using Document = mozilla::dom::Document;
  using TransactionId = mozilla::layers::TransactionId;
  using LogPresShellObserver = mozilla::LogPresShellObserver;

 public:
  explicit nsRefreshDriver(nsPresContext* aPresContext);
  ~nsRefreshDriver();

  /**
   * Methods for testing, exposed via nsIDOMWindowUtils.  See
   * nsIDOMWindowUtils.advanceTimeAndRefresh for description.
   */
  void AdvanceTimeAndRefresh(int64_t aMilliseconds);
  void RestoreNormalRefresh();
  void DoTick();
  bool IsTestControllingRefreshesEnabled() const {
    return mTestControllingRefreshes;
  }

  /**
   * Return the time of the most recent refresh.  This is intended to be
   * used by callers who want to start an animation now and want to know
   * what time to consider the start of the animation.  (This helps
   * ensure that multiple animations started during the same event off
   * the main event loop have the same start time.)
   */
  mozilla::TimeStamp MostRecentRefresh() const { return mMostRecentRefresh; }

  /**
   * Add / remove refresh observers.
   * RemoveRefreshObserver returns true if aObserver was found.
   *
   * The flush type affects:
   *   + the order in which the observers are notified (lowest flush
   *     type to highest, in order registered)
   *   + (in the future) which observers are suppressed when the display
   *     doesn't require current position data or isn't currently
   *     painting, and, correspondingly, which get notified when there
   *     is a flush during such suppression
   * and it must be FlushType::Style, or FlushType::Display.
   *
   * The refresh driver does NOT own a reference to these observers;
   * they must remove themselves before they are destroyed.
   *
   * The observer will be called even if there is no other activity.
   */
  void AddRefreshObserver(nsARefreshObserver* aObserver,
                          mozilla::FlushType aFlushType,
                          const char* aObserverDescription);
  bool RemoveRefreshObserver(nsARefreshObserver* aObserver,
                             mozilla::FlushType aFlushType);

  MOZ_CAN_RUN_SCRIPT void FlushLayoutOnPendingDocsAndFixUpFocus();

  /**
   * Add an observer that will be called after each refresh. The caller
   * must remove the observer before it is deleted. This does not trigger
   * refresh driver ticks.
   */
  void AddPostRefreshObserver(nsAPostRefreshObserver* aObserver);
  void AddPostRefreshObserver(mozilla::ManagedPostRefreshObserver*) = delete;
  void RemovePostRefreshObserver(nsAPostRefreshObserver* aObserver);
  void RemovePostRefreshObserver(mozilla::ManagedPostRefreshObserver*) = delete;

  /**
   * Add/Remove imgIRequest versions of observers.
   *
   * These are used for hooking into the refresh driver for
   * controlling animated images.
   *
   * @note The refresh driver owns a reference to these listeners.
   *
   * @note Technically, imgIRequest objects are not nsARefreshObservers, but
   * for controlling animated image repaint events, we subscribe the
   * imgIRequests to the nsRefreshDriver for notification of paint events.
   */
  void AddImageRequest(imgIRequest* aRequest);
  void RemoveImageRequest(imgIRequest* aRequest);

  /**
   * Marks that we're currently in the middle of processing user input.
   * Called by EventDispatcher when it's handling an input event.
   */
  void EnterUserInputProcessing() { mUserInputProcessingCount++; }
  void ExitUserInputProcessing() {
    MOZ_ASSERT(mUserInputProcessingCount > 0);
    mUserInputProcessingCount--;
  }

  /**
   * "Early Runner" runnables will be called as the first step when refresh
   * driver tick is triggered. Runners shouldn't keep other objects alive,
   * since it isn't guaranteed they will ever get called.
   */
  void AddEarlyRunner(nsIRunnable* aRunnable) {
    mEarlyRunners.AppendElement(aRunnable);
    EnsureTimerStarted();
  }

  // Remember whether our presshell's view manager needs a flush
  void SchedulePaint();
  bool IsPaintPending() const {
    return mRenderingPhasesNeeded.contains(mozilla::RenderingPhase::Paint);
  }

  // Returns true if a paint actually occurred.
  MOZ_CAN_RUN_SCRIPT bool PaintIfNeeded();

  /**
   * Schedule a frame visibility update "soon", subject to the heuristics and
   * throttling we apply to visibility updates.
   */
  void ScheduleFrameVisibilityUpdate() { mNeedToRecomputeVisibility = true; }

  /**
   * Tell the refresh driver that it is done driving refreshes and
   * should stop its timer and forget about its pres context.  This may
   * be called from within a refresh.
   */
  void Disconnect();

  bool IsFrozen() const { return mFreezeCount > 0; }

  bool IsThrottled() const { return mThrottled; }

  /**
   * Freeze the refresh driver.  It should stop delivering future
   * refreshes until thawed. Note that the number of calls to Freeze() must
   * match the number of calls to Thaw() in order for the refresh driver to
   * be un-frozen.
   */
  void Freeze();

  /**
   * Thaw the refresh driver.  If the number of calls to Freeze() matches the
   * number of calls to this function, the refresh driver should start
   * delivering refreshes again.
   */
  void Thaw();

  /**
   * Throttle or unthrottle the refresh driver.  This is done if the
   * corresponding presshell is hidden or shown.
   */
  void SetActivity(bool aIsActive);

  /**
   * Return the prescontext we were initialized with
   */
  nsPresContext* GetPresContext() const;

  void CreateVsyncRefreshTimer();

#ifdef DEBUG
  /**
   * Check whether the given observer is an observer for the given flush type
   */
  bool IsRefreshObserver(nsARefreshObserver* aObserver,
                         mozilla::FlushType aFlushType);
#endif

  /**
   * Default interval the refresh driver uses, in ms.
   */
  static int32_t DefaultInterval();

  /**
   * Returns 1.0 if a recent rate wasn't smaller than
   * DefaultInterval(). Otherwise return rate / DefaultInterval();
   * So the return value is (0-1].
   *
   */
  static double HighRateMultiplier();

  bool IsInRefresh() { return mInRefresh; }

  void SetIsResizeSuppressed() { mResizeSuppressed = true; }
  bool IsResizeSuppressed() const { return mResizeSuppressed; }

  // mozilla::layers::TransactionIdAllocator
  TransactionId GetTransactionId(bool aThrottle) override;
  TransactionId LastTransactionId() const override;
  void NotifyTransactionCompleted(TransactionId aTransactionId) override;
  void RevokeTransactionId(TransactionId aTransactionId) override;
  void ClearPendingTransactions() override;
  void ResetInitialTransactionId(TransactionId aTransactionId) override;
  mozilla::TimeStamp GetTransactionStart() override;
  mozilla::VsyncId GetVsyncId() override;
  mozilla::TimeStamp GetVsyncStart() override;

  bool IsWaitingForPaint(mozilla::TimeStamp aTime);
  void ScheduleAutoFocusFlush() {
    ScheduleRenderingPhase(mozilla::RenderingPhase::FlushAutoFocusCandidates);
  }

  // nsARefreshObserver
  NS_IMETHOD_(MozExternalRefCountType) AddRef(void) override {
    return TransactionIdAllocator::AddRef();
  }
  NS_IMETHOD_(MozExternalRefCountType) Release(void) override {
    return TransactionIdAllocator::Release();
  }
  virtual void WillRefresh(mozilla::TimeStamp aTime) override;

  /**
   * Compute the time when the currently active refresh driver timer
   * will start its next tick.
   *
   * Expects a non-null default value that is the upper bound of the
   * expected deadline. If the next expected deadline is later than
   * the default value, the default value is returned.
   *
   * If we're animating and we have skipped paints a time in the past
   * is returned.
   *
   * If aCheckType is AllVsyncListeners and we're in the parent process,
   * which doesn't have a RefreshDriver ticking, but some other process does
   * have, the return value is
   * (now + refreshrate - layout.idle_period.time_limit) as an approximation
   * when something will happen.
   * This can be useful check when parent process tries to avoid having too
   * long idle periods for example when it is sending input events to an
   * active child process.
   */
  enum IdleCheck { OnlyThisProcessRefreshDriver, AllVsyncListeners };
  static mozilla::TimeStamp GetIdleDeadlineHint(mozilla::TimeStamp aDefault,
                                                IdleCheck aCheckType);

  /**
   * It returns the expected timestamp of the next tick or nothing if the next
   * tick is missed.
   */
  static mozilla::Maybe<mozilla::TimeStamp> GetNextTickHint();

  static bool IsRegularRateTimerTicking();

  static void DispatchIdleTaskAfterTickUnlessExists(mozilla::Task* aTask);
  static void CancelIdleTask(mozilla::Task* aTask);

  void InitializeTimer() {
    MOZ_ASSERT(!mActiveTimer);
    EnsureTimerStarted();
  }

  bool HasPendingTick() const { return mActiveTimer; }

  void ScheduleRenderingPhases(mozilla::RenderingPhases aPhases) {
    mRenderingPhasesNeeded += aPhases;
    EnsureTimerStarted();
  }

  void ScheduleRenderingPhase(mozilla::RenderingPhase aPhase) {
    ScheduleRenderingPhases({aPhase});
  }

  void EnsureIntersectionObservationsUpdateHappens() {
    // This is enough to make sure that UpdateIntersectionObservations runs at
    // least once. This is presumably the intent of step 5 in [1]:
    //
    //     Schedule an iteration of the event loop in the root's browsing
    //     context.
    //
    // Though the wording of it is not quite clear to me...
    //
    // [1]:
    // https://w3c.github.io/IntersectionObserver/#dom-intersectionobserver-observe
    ScheduleRenderingPhase(
        mozilla::RenderingPhase::UpdateIntersectionObservations);
  }

  void EnsureViewTransitionOperationsHappen() {
    ScheduleRenderingPhase(mozilla::RenderingPhase::ViewTransitionOperations);
  }

  void EnsureAnimationUpdate() {
    ScheduleRenderingPhase(
        mozilla::RenderingPhase::UpdateAnimationsAndSendEvents);
  }

  void ScheduleMediaQueryListenerUpdate() {
    ScheduleRenderingPhase(
        mozilla::RenderingPhase::EvaluateMediaQueriesAndReportChanges);
  }

  // Register a composition payload that will be forwarded to the layer manager
  // if the current or upcoming refresh tick does a paint.
  // If no paint happens, the payload is discarded.
  // Should only be called on root refresh drivers.
  void RegisterCompositionPayload(
      const mozilla::layers::CompositionPayload& aPayload);

  enum class TickReasons : uint32_t {
    None = 0,
    HasObservers = 1 << 0,
    HasImageRequests = 1 << 1,
    HasPendingRenderingSteps = 1 << 2,
    RootNeedsMoreTicksForUserInput = 1 << 3,
  };

  void AddForceNotifyContentfulPaintPresContext(nsPresContext* aPresContext);
  void FlushForceNotifyContentfulPaintPresContext();

  // Mark that we've just run a tick from vsync, used to throttle 'extra'
  // paints to one per vsync (see CanDoExtraTick).
  void FinishedVsyncTick() { mAttemptedExtraTickSinceLastVsync = false; }

 private:
  using RequestTable = nsTHashSet<RefPtr<imgIRequest>>;
  struct ImageStartData {
    ImageStartData() = default;

    mozilla::Maybe<mozilla::TimeStamp> mStartTime;
    RequestTable mEntries;
  };
  using ImageStartTable = nsClassHashtable<nsUint32HashKey, ImageStartData>;

  struct ObserverData {
    nsARefreshObserver* mObserver;
    const char* mDescription;
    mozilla::TimeStamp mRegisterTime;
    mozilla::MarkerInnerWindowId mInnerWindowId;
    mozilla::UniquePtr<mozilla::ProfileChunkedBuffer> mCause;
    mozilla::FlushType mFlushType;

    bool operator==(nsARefreshObserver* aObserver) const {
      return mObserver == aObserver;
    }
    operator RefPtr<nsARefreshObserver>() { return mObserver; }
  };
  using ObserverArray = nsTObserverArray<ObserverData>;
  void RunFullscreenSteps();

  MOZ_CAN_RUN_SCRIPT
  void RunVideoAndFrameRequestCallbacks(mozilla::TimeStamp aNowTime);
  MOZ_CAN_RUN_SCRIPT
  void RunVideoFrameCallbacks(const nsTArray<RefPtr<Document>>&,
                              mozilla::TimeStamp aNowTime);
  MOZ_CAN_RUN_SCRIPT
  void RunFrameRequestCallbacks(const nsTArray<RefPtr<Document>>&,
                                mozilla::TimeStamp aNowTime);
  void UpdateRemoteFrameEffects();
  void UpdateRelevancyOfContentVisibilityAutoFrames();
  MOZ_CAN_RUN_SCRIPT void PerformPendingViewTransitionOperations();
  MOZ_CAN_RUN_SCRIPT void
  DetermineProximityToViewportAndNotifyResizeObservers();
  void MaybeIncreaseMeasuredTicksSinceLoading();
  void EvaluateMediaQueriesAndReportChanges();

  enum class IsExtraTick {
    No,
    Yes,
  };

  // Helper for Tick, to call WillRefresh(aNowTime) on each entry in
  // mObservers[aIdx] and then potentially do some additional post-notification
  // work that's associated with the FlushType corresponding to aIdx.
  //
  // Returns true on success, or false if one of our calls has destroyed our
  // pres context (in which case our callsite Tick() should immediately bail).
  MOZ_CAN_RUN_SCRIPT
  bool TickObserverArray(uint32_t aIdx, mozilla::TimeStamp aNowTime);

  MOZ_CAN_RUN_SCRIPT_BOUNDARY
  void Tick(mozilla::VsyncId aId, mozilla::TimeStamp aNowTime,
            IsExtraTick aIsExtraTick = IsExtraTick::No);

  enum EnsureTimerStartedFlags {
    eNone = 0,
    eForceAdjustTimer = 1 << 0,
    eAllowTimeToGoBackwards = 1 << 1,
  };
  void EnsureTimerStarted(EnsureTimerStartedFlags aFlags = eNone);
  void StopTimer();

  void UpdateThrottledState();

  bool HasObservers() const;
  void AppendObserverDescriptionsToString(nsACString& aStr) const;
  // Note: This should only be called in the dtor of nsRefreshDriver.
  uint32_t ObserverCount() const;
  bool HasImageRequests() const;
  bool ShouldKeepTimerRunningWhileWaitingForFirstContentfulPaint();
  bool ShouldKeepTimerRunningAfterPageLoad();
  ObserverArray& ArrayFor(mozilla::FlushType aFlushType);
  // Trigger a refresh immediately, if haven't been disconnected or frozen.
  void DoRefresh();

  // Starts pending image animations, and refreshes ongoing animations.
  void UpdateAnimatedImages(mozilla::TimeStamp aPreviousRefresh,
                            mozilla::TimeStamp aNowTime);

  bool HasReasonsToTick() const {
    return GetReasonsToTick() != TickReasons::None;
  }
  TickReasons GetReasonsToTick() const;
  void AppendTickReasonsToString(TickReasons aReasons, nsACString& aStr) const;

  double GetRegularTimerInterval() const;
  static double GetThrottledTimerInterval();

  static mozilla::TimeDuration GetMinRecomputeVisibilityInterval();

  void FinishedWaitingForTransaction();

  /**
   * Returns true if we didn't tick on the most recent vsync, but we think
   * we could run one now instead in order to reduce latency.
   */
  bool CanDoCatchUpTick();
  /**
   * Returns true if we think it's possible to run an repeat tick (between
   * vsyncs) to hopefully replace the original tick's paint on the compositor.
   * We allow this sometimes for tick requests coming for user input handling
   * to reduce latency.
   */
  bool CanDoExtraTick();

  bool AtPendingTransactionLimit() {
    return mPendingTransactions.Length() == 2;
  }
  bool TooManyPendingTransactions() {
    return mPendingTransactions.Length() >= 2;
  }

  mozilla::RefreshDriverTimer* ChooseTimer();
  mozilla::RefreshDriverTimer* mActiveTimer;
  RefPtr<mozilla::RefreshDriverTimer> mOwnTimer;
  mozilla::UniquePtr<mozilla::ProfileChunkedBuffer> mRefreshTimerStartedCause;

  // nsPresContext passed in constructor and unset in Disconnect.
  mozilla::WeakPtr<nsPresContext> mPresContext;

  RefPtr<nsRefreshDriver> mRootRefresh;

  // The most recently allocated transaction id.
  TransactionId mNextTransactionId;
  AutoTArray<TransactionId, 3> mPendingTransactions;

  uint32_t mFreezeCount;
  uint32_t mUserInputProcessingCount = 0;

  // How long we wait between ticks for throttled (which generally means
  // non-visible) documents registered with a non-throttled refresh driver.
  const mozilla::TimeDuration mThrottledFrameRequestInterval;

  // How long we wait, at a minimum, before recomputing approximate frame
  // visibility information. This is a minimum because, regardless of this
  // interval, we only recompute visibility when we've seen a layout or style
  // flush since the last time we did it.
  const mozilla::TimeDuration mMinRecomputeVisibilityInterval;

  mozilla::UniquePtr<mozilla::ProfileChunkedBuffer> mPaintCause;

  bool mThrottled : 1;
  bool mNeedToRecomputeVisibility : 1;
  bool mTestControllingRefreshes : 1;
  bool mInRefresh : 1;

  // True if the refresh driver is suspended waiting for transaction
  // id's to be returned and shouldn't do any work during Tick().
  bool mWaitingForTransaction : 1;
  // True if Tick() was skipped because of mWaitingForTransaction and
  // we should schedule a new Tick immediately when resumed instead
  // of waiting until the next interval.
  bool mSkippedPaints : 1;

  // True if view managers should delay any resize request until the
  // next tick by the refresh driver. This flag will be reset at the
  // start of every tick.
  bool mResizeSuppressed : 1;

  // True if we may need to run any frame callback.
  bool mNeedToRunFrameRequestCallbacks : 1;

  // True if we're currently within the scope of Tick() handling a normal
  // (timer-driven) tick.
  bool mInNormalTick : 1;

  // True if we attempted an extra tick (see CanDoExtraTick) since the last
  // vsync and thus shouldn't allow another.
  bool mAttemptedExtraTickSinceLastVsync : 1;

  bool mHasExceededAfterLoadTickPeriod : 1;

  bool mHasStartedTimerAtLeastOnce : 1;

  mozilla::TimeStamp mMostRecentRefresh;
  mozilla::TimeStamp mTickStart;
  mozilla::VsyncId mTickVsyncId;
  mozilla::TimeStamp mTickVsyncTime;
  mozilla::TimeStamp mNextThrottledFrameRequestTick;
  mozilla::TimeStamp mNextRecomputeVisibilityTick;
  mozilla::TimeStamp mBeforeFirstContentfulPaintTimerRunningLimit;

  // The "Update the rendering" phases we need to run on our documents.
  mozilla::EnumSet<mozilla::RenderingPhase, uint16_t> mRenderingPhasesNeeded;

  // Runs a single update the rendering phase, at once, rather than filtering
  // the docs as per spec.
  //
  // TODO(emilio): This should be removed, ideally.
  template <typename Callback>
  MOZ_CAN_RUN_SCRIPT void RunRenderingPhaseLegacy(mozilla::RenderingPhase,
                                                  Callback&&);

  using DocFilter = bool (*)(const Document&);

  // Runs a single update the rendering phase with the callback called for each
  // document, as per spec.
  template <typename Callback>
  MOZ_CAN_RUN_SCRIPT void RunRenderingPhase(mozilla::RenderingPhase, Callback&&,
                                            DocFilter = nullptr);

  // separate arrays for each flush type we support
  ObserverArray mObservers[3];
  nsTArray<mozilla::layers::CompositionPayload> mCompositionPayloads;
  RequestTable mRequests;
  ImageStartTable mStartTable;
  AutoTArray<nsCOMPtr<nsIRunnable>, 16> mEarlyRunners;
  nsTObserverArray<nsAPostRefreshObserver*> mPostRefreshObservers;

  // nsPresContexts which `NotifyContentfulPaint` have been called,
  // however the corresponding paint doesn't come from a regular
  // rendering steps(aka tick).
  //
  // For these nsPresContexts, we invoke
  // `FlushForceNotifyContentfulPaintPresContext` in the next tick
  // to force notify contentful paint, regardless whether the tick paints
  // or not.
  nsTArray<mozilla::WeakPtr<nsPresContext>>
      mForceNotifyContentfulPaintPresContexts;

  void BeginRefreshingImages(RequestTable& aEntries,
                             mozilla::TimeStamp aDesired);

  friend class mozilla::RefreshDriverTimer;

  static void Shutdown();
};

#endif /* !defined(nsRefreshDriver_h_) */
