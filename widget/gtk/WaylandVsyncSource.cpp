/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifdef MOZ_WAYLAND

#  include "WaylandVsyncSource.h"

#  include <gdk/gdkwayland.h>

#  include "MainThreadUtils.h"
#  include "mozilla/StaticPrefs_layout.h"
#  include "mozilla/StaticPrefs_widget.h"
#  include "mozilla/widget/WindowOcclusionState.h"
#  include "nsGtkUtils.h"
#  include "nsISupportsImpl.h"
#  include "nsThreadUtils.h"
#  include "nsWindow.h"

#  ifdef MOZ_LOGGING
#    include "Units.h"
#    include "mozilla/Logging.h"
#    include "nsTArray.h"
extern mozilla::LazyLogModule gWidgetVsync;
#    undef LOG
#    define LOG(str, ...)                             \
      MOZ_LOG(gWidgetVsync, mozilla::LogLevel::Debug, \
              ("[%p]: " str, GetWindowForLogging(), ##__VA_ARGS__))
#    define LOGS(str, ...) \
      MOZ_LOG(gWidgetVsync, mozilla::LogLevel::Debug, (str, ##__VA_ARGS__))
#  else
#    define LOG(...)
#  endif /* MOZ_LOGGING */

using namespace mozilla::widget;

namespace mozilla {

static float GetFPS(TimeDuration aVsyncRate) {
  return 1000.0f / float(aVsyncRate.ToMilliseconds());
}

constinit static nsTArray<WaylandVsyncSource*> gWaylandVsyncSources;

Maybe<TimeDuration> WaylandVsyncSource::GetFastestVsyncRate() {
  Maybe<TimeDuration> retVal;
  for (auto* source : gWaylandVsyncSources) {
    auto rate = source->GetVsyncRateIfEnabled();
    if (!rate) {
      continue;
    }
    if (!retVal.isSome()) {
      retVal.emplace(*rate);
    } else if (*rate < *retVal) {
      retVal.ref() = *rate;
    }
  }

  return retVal;
}

// Lint forces us to take this reference outside of constructor
void WaylandVsyncSource::Init() {
  MutexAutoLock lock(mMutex);
  WaylandSurfaceLock surfaceLock(mWaylandSurface);

  // mWaylandSurface is shared and referenced by nsWindow,
  // MozContained and WaylandVsyncSource.
  //
  // All references are explicitly removed at nsWindow::Destroy()
  // by WaylandVsyncSource::Shutdown() and
  // releases mWaylandSurface / MozContainer release.
  //
  // WaylandVsyncSource can be used by layout code after
  // nsWindow::Destroy()/WaylandVsyncSource::Shutdown() but
  // only as an empty shell.
  mWaylandSurface->SetVSyncCallbackHandlerLocked(
      surfaceLock,
      [this, self = RefPtr{this}](wl_callback* aCallback, uint32_t aTime,
                                  bool aEmulated) -> void {
        {
          MutexAutoLock lock(mMutex);
          if (!mVsyncSourceEnabled || !mVsyncEnabled || !mWaylandSurface) {
            return;
          }

          // Last recorded time has the same time base so we can compare it
          // and skip redundant callbacks.
          if (mLastTimeEmulated == aEmulated && mLastTime == aTime) {
            return;
          }
          mLastTimeEmulated = aEmulated;
          mLastTime = aTime;
        }
        LOG("WaylandVsyncSource frame callback, routed %d time %d emulated %d",
            !aCallback, aTime, aEmulated);

        VisibleWindowCallback(aTime);

        // If attached WaylandSurface becomes hidden/obscured or unmapped,
        // we will not get any regular frame callback any more and we will
        // not get any notification of it.
        // So always set up hidden window callback to catch it up.
        WatchHiddenWindowVSync();
      },
      /* aEmulateFrameCallback */ true);

  // Set a condition when we should run the emulated VSync.
  mWaylandSurface->SetVSyncEmulateCheckLocked(
      surfaceLock, [surface = RefPtr{mWaylandSurface}]() -> bool {
        return !surface->IsMapped() || !surface->HasBufferAttached();
      });
}

WaylandVsyncSource::WaylandVsyncSource(nsWindow* aWindow)
    : mMutex("WaylandVsyncSource"),
      mWindow(aWindow),
      mWaylandSurface(aWindow->GetWaylandSurface()),
      mVsyncRate(TimeDuration::FromMilliseconds(1000.0 / 60.0)),
      mLastVsyncTimeStamp(TimeStamp::Now()),
      mHiddenWindowTimeout(1000 / StaticPrefs::layout_throttled_frame_rate()) {
  MOZ_ASSERT(NS_IsMainThread());
  gWaylandVsyncSources.AppendElement(this);
  LOG("WaylandVsyncSource::WaylandVsyncSource()");
}

WaylandVsyncSource::~WaylandVsyncSource() {
  LOG("WaylandVsyncSource::~WaylandVsyncSource()");
  gWaylandVsyncSources.RemoveElement(this);
}

void WaylandVsyncSource::WatchHiddenWindowVSync() {
  MOZ_DIAGNOSTIC_ASSERT(NS_IsMainThread());
  if (!mHiddenWindowTimerID) {
    LOG("WaylandVsyncSource::WatchHiddenWindowVSync()");
    mHiddenWindowTimerID = g_timeout_add(
        mHiddenWindowTimeout,
        [](void* data) -> gint {
          RefPtr vsync = static_cast<WaylandVsyncSource*>(data);
          if (vsync->HiddenWindowCallback()) {
            // We want to fire again, so don't clear mHiddenWindowTimerID
            return G_SOURCE_CONTINUE;
          }
          // No need for g_source_remove, caller does it for us.
          vsync->mHiddenWindowTimerID = 0;
          return G_SOURCE_REMOVE;
        },
        this);
  }
}

void WaylandVsyncSource::SetVSyncEventsStateLocked(
    const MutexAutoLock& aProofOfLock, bool aEnabled) {
  MOZ_DIAGNOSTIC_ASSERT(NS_IsMainThread());
  mMutex.AssertCurrentThreadOwns();
  if (aEnabled) {
    mLastVsyncTimeStamp = TimeStamp::Now();
  } else {
    MozClearHandleID(mHiddenWindowTimerID, g_source_remove);
  }
  WaylandSurfaceLock lock(mWaylandSurface);
  mWaylandSurface->SetVSyncCallbackStateLocked(lock, aEnabled);
}

void WaylandVsyncSource::EnableVsync() {
  MOZ_DIAGNOSTIC_ASSERT(NS_IsMainThread());
  MutexAutoLock lock(mMutex);
  LOG("WaylandVsyncSource::EnableVsync fps %f\n", GetFPS(mVsyncRate));
  if (mVsyncEnabled || mIsShutdown) {
    LOG("  early quit");
    return;
  }
  mVsyncEnabled = true;
  SetVSyncEventsStateLocked(lock, mVsyncEnabled && mVsyncSourceEnabled);
}

void WaylandVsyncSource::DisableVsync() {
  MOZ_DIAGNOSTIC_ASSERT(NS_IsMainThread());
  MutexAutoLock lock(mMutex);
  LOG("WaylandVsyncSource::DisableVsync fps %f\n", GetFPS(mVsyncRate));
  if (!mVsyncEnabled || mIsShutdown) {
    LOG("  early quit");
    return;
  }
  mVsyncEnabled = false;
  SetVSyncEventsStateLocked(lock, mVsyncEnabled && mVsyncSourceEnabled);
}

void WaylandVsyncSource::EnableVSyncSource() {
  MutexAutoLock lock(mMutex);
  LOG("WaylandVsyncSource::EnableVSyncSource() WaylandSurface [%p] fps %f",
      mWaylandSurface.get(), GetFPS(mVsyncRate));
  mVsyncSourceEnabled = true;

  MOZ_DIAGNOSTIC_ASSERT(NS_IsMainThread());
  MOZ_DIAGNOSTIC_ASSERT(mWaylandSurface);
  SetVSyncEventsStateLocked(lock, mVsyncEnabled && mVsyncSourceEnabled);
}

void WaylandVsyncSource::DisableVSyncSource() {
  MutexAutoLock lock(mMutex);
  LOG("WaylandVsyncSource::DisableVSyncSource() WaylandSurface [%p]",
      mWaylandSurface.get());
  mVsyncSourceEnabled = false;

  MOZ_DIAGNOSTIC_ASSERT(NS_IsMainThread());
  MOZ_DIAGNOSTIC_ASSERT(mWaylandSurface);
  SetVSyncEventsStateLocked(lock, mVsyncEnabled && mVsyncSourceEnabled);
}

bool WaylandVsyncSource::HiddenWindowCallback() {
  MOZ_DIAGNOSTIC_ASSERT(NS_IsMainThread());

  RefPtr<nsWindow> window;
  TimeStamp lastVSync;
  TimeStamp outputTimestamp;
  {
    MutexAutoLock lock(mMutex);

    if (!mVsyncSourceEnabled || !mVsyncEnabled) {
      // We are unwanted by either our creator or our consumer, so we just stop
      // here without setting up a new frame callback.
      LOG("WaylandVsyncSource::HiddenWindowCallback(): quit, mVsyncEnabled %d "
          "mWaylandSurface %p",
          mVsyncEnabled && mVsyncSourceEnabled, mWaylandSurface.get());
      return false;
    }

    const auto now = TimeStamp::Now();
    const auto timeSinceLastVSync = now - mLastVsyncTimeStamp;
    if (timeSinceLastVSync.ToMilliseconds() < mHiddenWindowTimeout) {
      // We're not hidden, keep firing the callback to monitor window state.
      // If we become hidden we want set window occlusion state from this
      // callback.
      return true;
    }

    LOG("WaylandVsyncSource::HiddenWindowCallback() we're hidden, time since "
        "last VSync %d ms",
        (int)timeSinceLastVSync.ToMilliseconds());

    CalculateVsyncRateLocked(lock, now);
    mLastVsyncTimeStamp = lastVSync = now;

    outputTimestamp = mLastVsyncTimeStamp + mVsyncRate;
    window = mWindow;
  }

  // This could disable vsync.
  window->NotifyOcclusionState(OcclusionState::OCCLUDED);

  if (window->IsDestroyed()) {
    return false;
  }

  // Make sure to fire vsync now even if we get disabled afterwards.
  // This gives an opportunity to clean up after the visibility state change.
  // FIXME: Do we really need to do this?
  NotifyVsync(lastVSync, outputTimestamp);
  return StaticPrefs::widget_wayland_vsync_keep_firing_at_idle();
}

void WaylandVsyncSource::VisibleWindowCallback(uint32_t aTime) {
#  ifdef MOZ_LOGGING
  if (!aTime) {
    LOG("WaylandVsyncSource::EmulatedWindowCallback()");
  } else {
    LOG("WaylandVsyncSource::VisibleWindowCallback() time %d", aTime);
  }
#  endif
  MOZ_DIAGNOSTIC_ASSERT(NS_IsMainThread());

  {
    // This might enable vsync.
    RefPtr window = mWindow;
    window->NotifyOcclusionState(OcclusionState::VISIBLE);
    // NotifyOcclusionState can destroy us.
    if (window->IsDestroyed()) {
      return;
    }
  }

  MutexAutoLock lock(mMutex);
  if (!mVsyncEnabled || !mVsyncSourceEnabled) {
    // We are unwanted by either our creator or our consumer, so we just stop
    // here without setting up a new frame callback.
    LOG("  quit, mVsyncEnabled %d mWaylandSurface %p",
        mVsyncEnabled && mVsyncSourceEnabled, mWaylandSurface.get());
    return;
  }

  const auto now = TimeStamp::Now();
  TimeStamp vsyncTimestamp;
  if (aTime) {
    const auto callbackTimeStamp = TimeStamp::FromSystemTime(
        BaseTimeDurationPlatformUtils::TicksFromMilliseconds(aTime));
    // If the callback timestamp is close enough to our timestamp, use it,
    // otherwise use the current time.
    vsyncTimestamp = std::abs((now - callbackTimeStamp).ToMilliseconds()) < 50.0
                         ? callbackTimeStamp
                         : now;
  } else {
    vsyncTimestamp = now;
  }

  CalculateVsyncRateLocked(lock, vsyncTimestamp);
  mLastVsyncTimeStamp = vsyncTimestamp;
  const TimeStamp outputTimestamp = mLastVsyncTimeStamp + mVsyncRate;

  {
    MutexAutoUnlock unlock(mMutex);
    NotifyVsync(vsyncTimestamp, outputTimestamp);
  }
}

TimeDuration WaylandVsyncSource::GetVsyncRate() {
  MutexAutoLock lock(mMutex);
  return mVsyncRate;
}

Maybe<TimeDuration> WaylandVsyncSource::GetVsyncRateIfEnabled() {
  MutexAutoLock lock(mMutex);
  if (!mVsyncEnabled) {
    return Nothing();
  }
  return Some(mVsyncRate);
}

void WaylandVsyncSource::CalculateVsyncRateLocked(
    const MutexAutoLock& aProofOfLock, TimeStamp aVsyncTimestamp) {
  mMutex.AssertCurrentThreadOwns();

  double duration = (aVsyncTimestamp - mLastVsyncTimeStamp).ToMilliseconds();
  double curVsyncRate = mVsyncRate.ToMilliseconds();

  LOG("WaylandVsyncSource::CalculateVsyncRateLocked start fps %f\n",
      GetFPS(mVsyncRate));

  double correction;
  if (duration > curVsyncRate) {
    correction = fmin(curVsyncRate, (duration - curVsyncRate) / 10);
    mVsyncRate += TimeDuration::FromMilliseconds(correction);
  } else {
    correction = fmin(curVsyncRate / 2, (curVsyncRate - duration) / 10);
    mVsyncRate -= TimeDuration::FromMilliseconds(correction);
  }

  LOG("  new fps %f correction %f\n", GetFPS(mVsyncRate), correction);
}

bool WaylandVsyncSource::IsVsyncEnabled() {
  MutexAutoLock lock(mMutex);
  return mVsyncEnabled && mWaylandSurface;
}

void WaylandVsyncSource::Shutdown() {
  MOZ_ASSERT(NS_IsMainThread());
  MutexAutoLock lock(mMutex);

  LOG("WaylandVsyncSource::Shutdown fps %f\n", GetFPS(mVsyncRate));

  {
    // Remove all references from callbacks
    WaylandSurfaceLock surfaceLock(mWaylandSurface);
    mWaylandSurface->ClearVSyncCallbackHandlerLocked(surfaceLock);
    mWaylandSurface->SetVSyncEmulateCheckLocked(surfaceLock, nullptr,
                                                /* aForce */ true);
  }

  mWaylandSurface = nullptr;
  mWindow = nullptr;
  mIsShutdown = true;
  mVsyncEnabled = false;
  mVsyncSourceEnabled = false;
  MozClearHandleID(mHiddenWindowTimerID, g_source_remove);
}

}  // namespace mozilla

#endif  // MOZ_WAYLAND
