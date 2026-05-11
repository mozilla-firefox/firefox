/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "MediaManager.h"

#include "AudioCaptureTrack.h"
#include "AudioDeviceInfo.h"
#include "AudioStreamTrack.h"
#include "CubebDeviceEnumerator.h"
#include "CubebInputStream.h"
#include "MediaTimer.h"
#include "MediaTrackConstraints.h"
#include "MediaTrackGraph.h"
#include "MediaTrackListener.h"
#include "Tracing.h"
#include "VideoStreamTrack.h"
#include "VideoUtils.h"
#include "mozilla/Base64.h"
#include "mozilla/EventTargetCapability.h"
#include "mozilla/MozPromise.h"
#include "mozilla/NullPrincipal.h"
#include "mozilla/PeerIdentity.h"
#include "mozilla/PermissionDelegateHandler.h"
#include "mozilla/Sprintf.h"
#include "mozilla/StaticPrefs_media.h"
#include "mozilla/dom/BindingDeclarations.h"
#include "mozilla/dom/Document.h"
#include "mozilla/dom/Element.h"
#include "mozilla/dom/FeaturePolicyUtils.h"
#include "mozilla/dom/File.h"
#include "mozilla/dom/GetUserMediaRequestBinding.h"
#include "mozilla/dom/MediaDeviceInfo.h"
#include "mozilla/dom/MediaDevices.h"
#include "mozilla/dom/MediaDevicesBinding.h"
#include "mozilla/dom/MediaStreamBinding.h"
#include "mozilla/dom/MediaStreamTrackBinding.h"
#include "mozilla/dom/Promise.h"
#include "mozilla/dom/UserActivation.h"
#include "mozilla/dom/WindowContext.h"
#include "mozilla/dom/WindowGlobalChild.h"
#include "mozilla/glean/DomMediaWebrtcMetrics.h"
#include "mozilla/ipc/BackgroundChild.h"
#include "mozilla/ipc/PBackgroundChild.h"
#include "mozilla/media/CamerasTypes.h"
#include "mozilla/media/MediaChild.h"
#include "mozilla/media/MediaTaskUtils.h"
#include "nsAppDirectoryServiceDefs.h"
#include "nsArray.h"
#include "nsContentUtils.h"
#include "nsGlobalWindowInner.h"
#include "nsHashPropertyBag.h"
#include "nsIEventTarget.h"
#include "nsIPermissionManager.h"
#include "nsIUUIDGenerator.h"
#include "nsJSUtils.h"
#include "nsNetCID.h"
#include "nsNetUtil.h"
#include "nsProxyRelease.h"
#include "nspr.h"
#include "nss.h"
#include "pk11pub.h"

/* Using WebRTC backend on Desktops (Mac, Windows, Linux), otherwise default */
#include "MediaEngineFake.h"
#include "MediaEngineSource.h"
#if defined(MOZ_WEBRTC)
#  include "MediaEngineWebRTC.h"
#  include "MediaEngineWebRTCAudio.h"
#  include "browser_logging/WebRtcLog.h"
#  include "libwebrtcglue/WebrtcTaskQueueWrapper.h"
#  include "modules/audio_processing/include/audio_processing.h"
#endif

#if defined(XP_WIN)
#  include <objbase.h>
#endif

// A specialization of nsMainThreadPtrHolder for
// mozilla::dom::CallbackObjectHolder.  See documentation for
// nsMainThreadPtrHolder in nsProxyRelease.h.  This specialization lets us avoid
// wrapping the CallbackObjectHolder into a separate refcounted object.
template <class WebIDLCallbackT, class XPCOMCallbackT>
class nsMainThreadPtrHolder<
    mozilla::dom::CallbackObjectHolder<WebIDLCallbackT, XPCOMCallbackT>>
    final {
  typedef mozilla::dom::CallbackObjectHolder<WebIDLCallbackT, XPCOMCallbackT>
      Holder;

 public:
  nsMainThreadPtrHolder(const char* aName, Holder&& aHolder)
      : mHolder(std::move(aHolder))
#ifndef RELEASE_OR_BETA
        ,
        mName(aName)
#endif
  {
    MOZ_ASSERT(NS_IsMainThread());
  }

 private:
  // We can be released on any thread.
  ~nsMainThreadPtrHolder() {
    if (NS_IsMainThread()) {
      mHolder.Reset();
    } else if (mHolder.GetISupports()) {
      nsCOMPtr<nsIEventTarget> target = do_GetMainThread();
      MOZ_ASSERT(target);
      NS_ProxyRelease(
#ifdef RELEASE_OR_BETA
          nullptr,
#else
          mName,
#endif
          target, mHolder.Forget());
    }
  }

 public:
  Holder* get() {
    // Nobody should be touching the raw pointer off-main-thread.
    if (MOZ_UNLIKELY(!NS_IsMainThread())) {
      NS_ERROR("Can't dereference nsMainThreadPtrHolder off main thread");
      MOZ_CRASH();
    }
    return &mHolder;
  }

  bool operator!() const { return !mHolder; }

  NS_INLINE_DECL_THREADSAFE_REFCOUNTING(nsMainThreadPtrHolder<Holder>)

  // Copy constructor and operator= not implemented. Once constructed, the
  // holder is immutable.
  Holder& operator=(const nsMainThreadPtrHolder& aOther) = delete;
  nsMainThreadPtrHolder(const nsMainThreadPtrHolder& aOther) = delete;

 private:
  // Our holder.
  Holder mHolder;

#ifndef RELEASE_OR_BETA
  const char* mName = nullptr;
#endif
};

namespace mozilla {

LazyLogModule gMediaManagerLog("MediaManager");
#define LOG(...) MOZ_LOG(gMediaManagerLog, LogLevel::Debug, (__VA_ARGS__))

class GetUserMediaStreamTask;
class LocalTrackSource;
class SelectAudioOutputTask;

using camera::CamerasAccessStatus;
using dom::BFCacheStatus;
using dom::CallerType;
using dom::ConstrainDOMStringParameters;
using dom::ConstrainDoubleRange;
using dom::ConstrainLongRange;
using dom::DisplayMediaStreamConstraints;
using dom::Document;
using dom::Element;
using dom::FeaturePolicyUtils;
using dom::File;
using dom::GetUserMediaRequest;
using dom::MediaDeviceKind;
using dom::MediaDevices;
using dom::MediaSourceEnum;
using dom::MediaStreamConstraints;
using dom::MediaStreamError;
using dom::MediaStreamTrack;
using dom::MediaStreamTrackSource;
using dom::MediaTrackCapabilities;
using dom::MediaTrackConstraints;
using dom::MediaTrackConstraintSet;
using dom::MediaTrackSettings;
using dom::OwningBooleanOrMediaTrackConstraints;
using dom::OwningStringOrStringSequence;
using dom::OwningStringOrStringSequenceOrConstrainDOMStringParameters;
using dom::Promise;
using dom::Sequence;
using dom::UserActivation;
using dom::VideoResizeModeEnum;
using dom::WindowGlobalChild;
using ConstDeviceSetPromise = MediaManager::ConstDeviceSetPromise;
using DeviceSetPromise = MediaManager::DeviceSetPromise;
using LocalDevicePromise = MediaManager::LocalDevicePromise;
using LocalDeviceSetPromise = MediaManager::LocalDeviceSetPromise;
using LocalMediaDeviceSetRefCnt = MediaManager::LocalMediaDeviceSetRefCnt;
using MediaDeviceSetRefCnt = MediaManager::MediaDeviceSetRefCnt;
using media::NewRunnableFrom;
using media::NewTaskFrom;
using media::Refcountable;

// Whether main thread actions of MediaManager shutdown (except for clearing
// of sSingleton) have completed.
static bool sHasMainThreadShutdown;

struct DeviceState {
  DeviceState(RefPtr<LocalMediaDevice> aDevice,
              RefPtr<LocalTrackSource> aTrackSource, bool aOffWhileDisabled)
      : mOffWhileDisabled(aOffWhileDisabled),
        mDevice(std::move(aDevice)),
        mTrackSource(std::move(aTrackSource)) {
    MOZ_ASSERT(mDevice);
    MOZ_ASSERT(mTrackSource);
  }

  // true if we have allocated mDevice. When not allocated, we may not stop or
  // deallocate.
  // MainThread only.
  bool mAllocated = false;

  // true if we have stopped mDevice, this is a terminal state.
  // MainThread only.
  bool mStopped = false;

  // true if mDevice is currently enabled.
  // A device must be both enabled and unmuted to be turned on and capturing.
  // MainThread only.
  bool mDeviceEnabled = false;

  // true if mDevice is currently muted.
  // A device that is either muted or disabled is turned off and not capturing.
  // MainThread only.
  bool mDeviceMuted = false;

  // true if the application has currently enabled mDevice.
  // MainThread only.
  bool mTrackEnabled = false;

  // Time when the application last enabled mDevice.
  // MainThread only.
  TimeStamp mTrackEnabledTime;

  // true if an operation to Start() or Stop() mDevice has been dispatched to
  // the media thread and is not finished yet.
  // MainThread only.
  bool mOperationInProgress = false;

  // true if we are allowed to turn off the underlying source while all tracks
  // are disabled or muted.
  // MainThread only.
  bool mOffWhileDisabled = false;

  // Timer triggered by a MediaStreamTrackSource signaling that all tracks got
  // disabled. When the timer fires we initiate Stop()ing mDevice.
  // If set we allow dynamically stopping and starting mDevice.
  // Any thread.
  const RefPtr<MediaTimer<TimeStamp>> mDisableTimer =
      new MediaTimer<TimeStamp>();

  // The underlying device we keep state for. Always non-null.
  // Threadsafe access, but see method declarations for individual constraints.
  const RefPtr<LocalMediaDevice> mDevice;

  // The MediaStreamTrackSource for any tracks (original and clones) originating
  // from this device. Always non-null. Threadsafe access, but see method
  // declarations for individual constraints.
  const RefPtr<LocalTrackSource> mTrackSource;
};

/**
 * This mimics the capture state from nsIMediaManagerService.
 */
enum class CaptureState : uint16_t {
  Off = nsIMediaManagerService::STATE_NOCAPTURE,
  Enabled = nsIMediaManagerService::STATE_CAPTURE_ENABLED,
  Disabled = nsIMediaManagerService::STATE_CAPTURE_DISABLED,
};

static CaptureState CombineCaptureState(CaptureState aFirst,
                                        CaptureState aSecond) {
  if (aFirst == CaptureState::Enabled || aSecond == CaptureState::Enabled) {
    return CaptureState::Enabled;
  }
  if (aFirst == CaptureState::Disabled || aSecond == CaptureState::Disabled) {
    return CaptureState::Disabled;
  }
  MOZ_ASSERT(aFirst == CaptureState::Off);
  MOZ_ASSERT(aSecond == CaptureState::Off);
  return CaptureState::Off;
}

static uint16_t FromCaptureState(CaptureState aState) {
  MOZ_ASSERT(aState == CaptureState::Off || aState == CaptureState::Enabled ||
             aState == CaptureState::Disabled);
  return static_cast<uint16_t>(aState);
}

void MediaManager::CallOnError(GetUserMediaErrorCallback& aCallback,
                               MediaStreamError& aError) {
  aCallback.Call(aError);
}

void MediaManager::CallOnSuccess(GetUserMediaSuccessCallback& aCallback,
                                 DOMMediaStream& aStream) {
  aCallback.Call(aStream);
}

enum class PersistentPermissionState : uint32_t {
  Unknown = nsIPermissionManager::UNKNOWN_ACTION,
  Allow = nsIPermissionManager::ALLOW_ACTION,
  Deny = nsIPermissionManager::DENY_ACTION,
  Prompt = nsIPermissionManager::PROMPT_ACTION,
};

static PersistentPermissionState CheckPermission(
    PersistentPermissionState aPermission) {
  switch (aPermission) {
    case PersistentPermissionState::Unknown:
    case PersistentPermissionState::Allow:
    case PersistentPermissionState::Deny:
    case PersistentPermissionState::Prompt:
      return aPermission;
  }
  MOZ_CRASH("Unexpected permission value");
}

struct WindowPersistentPermissionState {
  PersistentPermissionState mCameraPermission;
  PersistentPermissionState mMicrophonePermission;
};

static Result<WindowPersistentPermissionState, nsresult>
GetPersistentPermissions(uint64_t aWindowId) {
  auto* window = nsGlobalWindowInner::GetInnerWindowWithId(aWindowId);
  if (NS_WARN_IF(!window) || NS_WARN_IF(!window->GetPrincipal())) {
    return Err(NS_ERROR_INVALID_ARG);
  }

  Document* doc = window->GetExtantDoc();
  if (NS_WARN_IF(!doc)) {
    return Err(NS_ERROR_INVALID_ARG);
  }

  nsIPrincipal* principal = window->GetPrincipal();
  if (NS_WARN_IF(!principal)) {
    return Err(NS_ERROR_INVALID_ARG);
  }

  nsresult rv;
  RefPtr<PermissionDelegateHandler> permDelegate =
      doc->GetPermissionDelegateHandler();
  if (NS_WARN_IF(!permDelegate)) {
    return Err(NS_ERROR_INVALID_ARG);
  }

  uint32_t audio = nsIPermissionManager::UNKNOWN_ACTION;
  uint32_t video = nsIPermissionManager::UNKNOWN_ACTION;
  {
    rv = permDelegate->GetPermission("microphone"_ns, &audio, true);
    if (NS_WARN_IF(NS_FAILED(rv))) {
      return Err(rv);
    }
    rv = permDelegate->GetPermission("camera"_ns, &video, true);
    if (NS_WARN_IF(NS_FAILED(rv))) {
      return Err(rv);
    }
  }

  return WindowPersistentPermissionState{
      CheckPermission(static_cast<PersistentPermissionState>(video)),
      CheckPermission(static_cast<PersistentPermissionState>(audio))};
}

/**
 * DeviceListener has threadsafe refcounting for use across the main, media and
 * MTG threads. But it has a non-threadsafe SupportsWeakPtr for WeakPtr usage
 * only from main thread, to ensure that garbage- and cycle-collected objects
 * don't hold a reference to it during late shutdown.
 */
class DeviceListener : public SupportsWeakPtr {
 public:
  typedef MozPromise<bool /* aIgnored */, RefPtr<MediaMgrError>, false>
      DeviceListenerPromise;

  NS_INLINE_DECL_THREADSAFE_REFCOUNTING_WITH_DELETE_ON_MAIN_THREAD(
      DeviceListener)

  DeviceListener();

  /**
   * Registers this device listener as belonging to the given window listener.
   * Stop() must be called on registered DeviceListeners before destruction.
   */
  void Register(GetUserMediaWindowListener* aListener);

  /**
   * Marks this listener as active and creates the internal device state.
   */
  void Activate(RefPtr<LocalMediaDevice> aDevice,
                RefPtr<LocalTrackSource> aTrackSource, bool aStartMuted,
                bool aIsAllocated);

  /**
   * Posts a task to initialize and start the associated device.
   */
  RefPtr<DeviceListenerPromise> InitializeAsync();

 private:
  /**
   * Initializes synchronously. Must be called on the media thread.
   */
  nsresult Initialize(PrincipalHandle aPrincipal, LocalMediaDevice* aDevice,
                      MediaTrack* aTrack, bool aStartDevice);

 public:
  /**
   * Synchronously clones this device listener, setting up the device to match
   * our current device state asynchronously. Settings, constraints and other
   * main thread state starts applying immediately.
   */
  already_AddRefed<DeviceListener> Clone() const;

  /**
   * Posts a task to stop the device associated with this DeviceListener and
   * notifies the associated window listener that a track was stopped.
   *
   * This will also clean up the weak reference to the associated window
   * listener, and tell the window listener to remove its hard reference to this
   * DeviceListener, so any caller will need to keep its own hard ref.
   */
  void Stop();

  /**
   * Gets the main thread MediaTrackSettings from the MediaEngineSource
   * associated with aTrack.
   */
  void GetSettings(MediaTrackSettings& aOutSettings) const;

  /**
   * Gets the main thread MediaTrackCapabilities from the MediaEngineSource
   * associated with aTrack.
   */
  void GetCapabilities(MediaTrackCapabilities& aOutCapabilities) const;

  /**
   * Posts a task to set the enabled state of the device associated with this
   * DeviceListener to aEnabled and notifies the associated window listener that
   * a track's state has changed.
   *
   * Turning the hardware off while the device is disabled is supported for:
   * - Camera (enabled by default, controlled by pref
   *   "media.getusermedia.camera.off_while_disabled.enabled")
   * - Microphone (disabled by default, controlled by pref
   *   "media.getusermedia.microphone.off_while_disabled.enabled")
   * Screen-, app-, or windowsharing is not supported at this time.
   *
   * The behavior is also different between disabling and enabling a device.
   * While enabling is immediate, disabling only happens after a delay.
   * This is now defaulting to 3 seconds but can be overriden by prefs:
   * - "media.getusermedia.camera.off_while_disabled.delay_ms" and
   * - "media.getusermedia.microphone.off_while_disabled.delay_ms".
   *
   * The delay is in place to prevent misuse by malicious sites. If a track is
   * re-enabled before the delay has passed, the device will not be touched
   * until another disable followed by the full delay happens.
   */
  void SetDeviceEnabled(bool aEnabled);

  /**
   * Posts a task to set the muted state of the device associated with this
   * DeviceListener to aMuted and notifies the associated window listener that a
   * track's state has changed.
   *
   * Turning the hardware off while the device is muted is supported for:
   * - Camera (enabled by default, controlled by pref
   *   "media.getusermedia.camera.off_while_disabled.enabled")
   * - Microphone (disabled by default, controlled by pref
   *   "media.getusermedia.microphone.off_while_disabled.enabled")
   * Screen-, app-, or windowsharing is not supported at this time.
   */
  void SetDeviceMuted(bool aMuted);

  /**
   * Mutes or unmutes the associated video device if it is a camera.
   */
  void MuteOrUnmuteCamera(bool aMute);
  void MuteOrUnmuteMicrophone(bool aMute);

  LocalMediaDevice* GetDevice() const {
    return mDeviceState ? mDeviceState->mDevice.get() : nullptr;
  }

  LocalTrackSource* GetTrackSource() const {
    return mDeviceState ? mDeviceState->mTrackSource.get() : nullptr;
  }

  bool Activated() const { return static_cast<bool>(mDeviceState); }

  bool Stopped() const { return mStopped; }

  bool CapturingVideo() const;

  bool CapturingAudio() const;

  CaptureState CapturingSource(MediaSourceEnum aSource) const;

  RefPtr<DeviceListenerPromise> ApplyConstraints(
      const MediaTrackConstraints& aConstraints, CallerType aCallerType);

  PrincipalHandle GetPrincipalHandle() const;

  size_t SizeOfIncludingThis(MallocSizeOf aMallocSizeOf) const {
    size_t amount = aMallocSizeOf(this);
    // Assume mPrincipalHandle refers to a principal owned elsewhere.
    // DeviceState does not have support for memory accounting.
    return amount;
  }

 private:
  virtual ~DeviceListener() {
    MOZ_ASSERT(mStopped);
    MOZ_ASSERT(!mWindowListener);
  }

  using DeviceOperationPromise =
      MozPromise<nsresult, bool, /* IsExclusive = */ true>;

  /**
   * Posts a task to start or stop the device associated with aTrack, based on
   * a passed-in boolean. Private method used by SetDeviceEnabled and
   * SetDeviceMuted.
   */
  RefPtr<DeviceOperationPromise> UpdateDevice(bool aOn);

  // true after this listener has had all devices stopped. MainThread only.
  bool mStopped;

  // never ever indirect off this; just for assertions
  PRThread* mMainThreadCheck;

  // Set in Register() on main thread, then read from any thread.
  PrincipalHandle mPrincipalHandle;

  // Weak pointer to the window listener that owns us. MainThread only.
  GetUserMediaWindowListener* mWindowListener;

  // Accessed from MediaTrackGraph thread, MediaManager thread, and MainThread
  // No locking needed as it's set on Activate() and never assigned to again.
  UniquePtr<DeviceState> mDeviceState;

  MediaEventListener mCaptureEndedListener;
};

/**
 * This class represents a WindowID and handles all MediaTrackListeners
 * (here subclassed as DeviceListeners) used to feed GetUserMedia tracks.
 * It proxies feedback from them into messages for browser chrome.
 * The DeviceListeners are used to Start() and Stop() the underlying
 * MediaEngineSource when MediaStreams are assigned and deassigned in content.
 */
class GetUserMediaWindowListener {
  friend MediaManager;

 public:
  NS_INLINE_DECL_THREADSAFE_REFCOUNTING(GetUserMediaWindowListener)

  // Create in an inactive state
  GetUserMediaWindowListener(uint64_t aWindowID,
                             const PrincipalHandle& aPrincipalHandle)
      : mWindowID(aWindowID),
        mPrincipalHandle(aPrincipalHandle),
        mChromeNotificationTaskPosted(false) {}

  /**
   * Registers an inactive gUM device listener for this WindowListener.
   */
  void Register(RefPtr<DeviceListener> aListener) {
    MOZ_ASSERT(NS_IsMainThread());
    MOZ_ASSERT(aListener);
    MOZ_ASSERT(!aListener->Activated());
    MOZ_ASSERT(!mInactiveListeners.Contains(aListener), "Already registered");
    MOZ_ASSERT(!mActiveListeners.Contains(aListener), "Already activated");

    aListener->Register(this);
    mInactiveListeners.AppendElement(std::move(aListener));
  }

  /**
   * Activates an already registered and inactive gUM device listener for this
   * WindowListener.
   */
  void Activate(RefPtr<DeviceListener> aListener,
                RefPtr<LocalMediaDevice> aDevice,
                RefPtr<LocalTrackSource> aTrackSource, bool aIsAllocated) {
    MOZ_ASSERT(NS_IsMainThread());
    MOZ_ASSERT(aListener);
    MOZ_ASSERT(!aListener->Activated());
    MOZ_ASSERT(mInactiveListeners.Contains(aListener),
               "Must be registered to activate");
    MOZ_ASSERT(!mActiveListeners.Contains(aListener), "Already activated");

    bool muted = false;
    if (aDevice->Kind() == MediaDeviceKind::Videoinput) {
      muted = mCamerasAreMuted;
    } else if (aDevice->Kind() == MediaDeviceKind::Audioinput) {
      muted = mMicrophonesAreMuted;
    } else {
      MOZ_CRASH("Unexpected device kind");
    }

    mInactiveListeners.RemoveElement(aListener);
    aListener->Activate(std::move(aDevice), std::move(aTrackSource), muted,
                        aIsAllocated);
    mActiveListeners.AppendElement(std::move(aListener));
  }

  /**
   * Removes all DeviceListeners from this window listener.
   * Removes this window listener from the list of active windows, so callers
   * need to make sure to hold a strong reference.
   */
  void RemoveAll() {
    MOZ_ASSERT(NS_IsMainThread());

    for (auto& l : mInactiveListeners.Clone()) {
      Remove(l);
    }
    for (auto& l : mActiveListeners.Clone()) {
      Remove(l);
    }
    MOZ_ASSERT(mInactiveListeners.Length() == 0);
    MOZ_ASSERT(mActiveListeners.Length() == 0);

    MediaManager* mgr = MediaManager::GetIfExists();
    if (!mgr) {
      MOZ_ASSERT(false, "MediaManager should stay until everything is removed");
      return;
    }
    GetUserMediaWindowListener* windowListener =
        mgr->GetWindowListener(mWindowID);

    if (!windowListener) {
      nsCOMPtr<nsIObserverService> obs = services::GetObserverService();
      auto* globalWindow = nsGlobalWindowInner::GetInnerWindowWithId(mWindowID);
      if (globalWindow) {
        auto req = MakeRefPtr<GetUserMediaRequest>(
            globalWindow, VoidString(), VoidString(),
            UserActivation::IsHandlingUserInput());
        obs->NotifyWhenScriptSafe(req, "recording-device-stopped", nullptr);
      }
      return;
    }

    MOZ_ASSERT(windowListener == this,
               "There should only be one window listener per window ID");

    LOG("GUMWindowListener %p removing windowID %" PRIu64, this, mWindowID);
    mgr->RemoveWindowID(mWindowID);
  }

  /**
   * Removes a listener from our lists. Safe to call without holding a hard
   * reference. That said, you'll still want to iterate on a copy of said lists,
   * if you end up calling this method (or methods that may call this method) in
   * the loop, to avoid inadvertently skipping members.
   *
   * For use only from GetUserMediaWindowListener and DeviceListener.
   */
  bool Remove(RefPtr<DeviceListener> aListener) {
    // We refcount aListener on entry since we're going to proxy-release it
    // below to prevent the refcount going to zero on callers who might be
    // inside the listener, but operating without a hard reference to self.
    MOZ_ASSERT(NS_IsMainThread());

    if (!mInactiveListeners.RemoveElement(aListener) &&
        !mActiveListeners.RemoveElement(aListener)) {
      return false;
    }
    MOZ_ASSERT(!mInactiveListeners.Contains(aListener),
               "A DeviceListener should only be once in one of "
               "mInactiveListeners and mActiveListeners");
    MOZ_ASSERT(!mActiveListeners.Contains(aListener),
               "A DeviceListener should only be once in one of "
               "mInactiveListeners and mActiveListeners");

    LOG("GUMWindowListener %p stopping DeviceListener %p.", this,
        aListener.get());
    aListener->Stop();

    if (LocalMediaDevice* removedDevice = aListener->GetDevice()) {
      bool revokePermission = true;
      nsString removedRawId;
      nsString removedSourceType;
      removedDevice->GetRawId(removedRawId);
      removedDevice->GetMediaSource(removedSourceType);

      for (const auto& l : mActiveListeners) {
        if (LocalMediaDevice* device = l->GetDevice()) {
          nsString rawId;
          device->GetRawId(rawId);
          if (removedRawId.Equals(rawId)) {
            revokePermission = false;
            break;
          }
        }
      }

      if (revokePermission) {
        nsCOMPtr<nsIObserverService> obs = services::GetObserverService();
        auto* window = nsGlobalWindowInner::GetInnerWindowWithId(mWindowID);
        auto req = MakeRefPtr<GetUserMediaRequest>(
            window, removedRawId, removedSourceType,
            UserActivation::IsHandlingUserInput());
        obs->NotifyWhenScriptSafe(req, "recording-device-stopped", nullptr);
      }
    }

    if (mInactiveListeners.Length() == 0 && mActiveListeners.Length() == 0) {
      LOG("GUMWindowListener %p Removed last DeviceListener. Cleaning up.",
          this);
      RemoveAll();
    }

    nsCOMPtr<nsIEventTarget> mainTarget = do_GetMainThread();
    // To allow being invoked by callers not holding a strong reference to self,
    // hold the listener alive until the stack has unwound, by always
    // dispatching a runnable (aAlwaysProxy = true)
    NS_ProxyRelease(__func__, mainTarget, aListener.forget(), true);
    return true;
  }

  /**
   * Stops all screen/window/audioCapture sharing, but not camera or microphone.
   */
  void StopSharing();

  void StopRawID(const nsString& removedDeviceID);

  void MuteOrUnmuteCameras(bool aMute);
  void MuteOrUnmuteMicrophones(bool aMute);

  /**
   * Called by one of our DeviceListeners when one of its tracks has changed so
   * that chrome state is affected.
   * Schedules an event for the next stable state to update chrome.
   */
  void ChromeAffectingStateChanged();

  /**
   * Called in stable state to send a notification to update chrome.
   */
  void NotifyChrome();

  bool CapturingVideo() const {
    MOZ_ASSERT(NS_IsMainThread());
    for (auto& l : mActiveListeners) {
      if (l->CapturingVideo()) {
        return true;
      }
    }
    return false;
  }

  bool CapturingAudio() const {
    MOZ_ASSERT(NS_IsMainThread());
    for (auto& l : mActiveListeners) {
      if (l->CapturingAudio()) {
        return true;
      }
    }
    return false;
  }

  CaptureState CapturingSource(MediaSourceEnum aSource) const {
    MOZ_ASSERT(NS_IsMainThread());
    CaptureState result = CaptureState::Off;
    for (auto& l : mActiveListeners) {
      result = CombineCaptureState(result, l->CapturingSource(aSource));
    }
    return result;
  }

  RefPtr<LocalMediaDeviceSetRefCnt> GetDevices() {
    RefPtr devices = new LocalMediaDeviceSetRefCnt();
    for (auto& l : mActiveListeners) {
      devices->AppendElement(l->GetDevice());
    }
    return devices;
  }

  uint64_t WindowID() const { return mWindowID; }

  PrincipalHandle GetPrincipalHandle() const { return mPrincipalHandle; }

  size_t SizeOfIncludingThis(MallocSizeOf aMallocSizeOf) const {
    size_t amount = aMallocSizeOf(this);
    // Assume mPrincipalHandle refers to a principal owned elsewhere.
    amount += mInactiveListeners.ShallowSizeOfExcludingThis(aMallocSizeOf);
    for (const RefPtr<DeviceListener>& listener : mInactiveListeners) {
      amount += listener->SizeOfIncludingThis(aMallocSizeOf);
    }
    amount += mActiveListeners.ShallowSizeOfExcludingThis(aMallocSizeOf);
    for (const RefPtr<DeviceListener>& listener : mActiveListeners) {
      amount += listener->SizeOfIncludingThis(aMallocSizeOf);
    }
    return amount;
  }

 private:
  ~GetUserMediaWindowListener() {
    MOZ_ASSERT(mInactiveListeners.Length() == 0,
               "Inactive listeners should already be removed");
    MOZ_ASSERT(mActiveListeners.Length() == 0,
               "Active listeners should already be removed");
  }

  uint64_t mWindowID;
  const PrincipalHandle mPrincipalHandle;

  // true if we have scheduled a task to notify chrome in the next stable state.
  // The task will reset this to false. MainThread only.
  bool mChromeNotificationTaskPosted;

  nsTArray<RefPtr<DeviceListener>> mInactiveListeners;
  nsTArray<RefPtr<DeviceListener>> mActiveListeners;

  // Whether camera and microphone access in this window are currently
  // User Agent (UA) muted. When true, new and cloned tracks must start
  // out muted, to avoid JS circumventing UA mute. Per-camera and
  // per-microphone UA muting is not supported.
  bool mCamerasAreMuted = false;
  bool mMicrophonesAreMuted = false;
};

class LocalTrackSource : public MediaStreamTrackSource {
 public:
  LocalTrackSource(nsIPrincipal* aPrincipal, const nsString& aLabel,
                   const RefPtr<DeviceListener>& aListener,
                   MediaSourceEnum aSource, MediaTrack* aTrack,
                   RefPtr<const PeerIdentity> aPeerIdentity,
                   TrackingId aTrackingId = TrackingId())
      : MediaStreamTrackSource(aPrincipal, aLabel, std::move(aTrackingId)),
        mSource(aSource),
        mTrack(aTrack),
        mPeerIdentity(std::move(aPeerIdentity)),
        mListener(aListener.get()) {}

  MediaSourceEnum GetMediaSource() const override { return mSource; }

  const PeerIdentity* GetPeerIdentity() const override { return mPeerIdentity; }

  RefPtr<MediaStreamTrackSource::ApplyConstraintsPromise> ApplyConstraints(
      const MediaTrackConstraints& aConstraints,
      CallerType aCallerType) override {
    MOZ_ASSERT(NS_IsMainThread());
    if (sHasMainThreadShutdown || !mListener) {
      // Track has been stopped, or we are in shutdown. In either case
      // there's no observable outcome, so pretend we succeeded.
      return MediaStreamTrackSource::ApplyConstraintsPromise::CreateAndResolve(
          false, __func__);
    }
    auto p = mListener->ApplyConstraints(aConstraints, aCallerType);
    p->Then(
        GetCurrentSerialEventTarget(), __func__,
        [aConstraints, this, self = RefPtr(this)] {
          ConstraintsChanged(aConstraints);
        },
        [] {});
    return p;
  }

  void GetSettings(MediaTrackSettings& aOutSettings) override {
    if (mListener) {
      mListener->GetSettings(aOutSettings);
    }
  }

  void GetCapabilities(MediaTrackCapabilities& aOutCapabilities) override {
    if (mListener) {
      mListener->GetCapabilities(aOutCapabilities);
    }
  }

  void Stop() override {
    if (mListener) {
      mListener->Stop();
      mListener = nullptr;
    }
    if (!mTrack->IsDestroyed()) {
      mTrack->Destroy();
    }
  }

  CloneResult Clone() override {
    if (!mListener) {
      return {};
    }
    RefPtr listener = mListener->Clone();
    MOZ_ASSERT(listener);
    if (!listener) {
      return {};
    }

    return {.mSource = listener->GetTrackSource(),
            .mInputTrack = listener->GetTrackSource()->mTrack};
  }

  void Disable() override {
    if (mListener) {
      mListener->SetDeviceEnabled(false);
    }
  }

  void Enable() override {
    if (mListener) {
      mListener->SetDeviceEnabled(true);
    }
  }

  void Mute() {
    MutedChanged(true);
    mTrack->SetDisabledTrackMode(DisabledTrackMode::SILENCE_BLACK);
  }

  void Unmute() {
    MutedChanged(false);
    mTrack->SetDisabledTrackMode(DisabledTrackMode::ENABLED);
  }

  const MediaSourceEnum mSource;
  const RefPtr<MediaTrack> mTrack;
  const RefPtr<const PeerIdentity> mPeerIdentity;

 protected:
  ~LocalTrackSource() {
    MOZ_ASSERT(NS_IsMainThread());
    MOZ_ASSERT(mTrack->IsDestroyed());
  }

  // This is a weak pointer to avoid having the DeviceListener (which may
  // have references to threads and threadpools) kept alive by DOM-objects
  // that may have ref-cycles and thus are released very late during
  // shutdown, even after xpcom-shutdown-threads. See bug 1351655 for what
  // can happen.
  WeakPtr<DeviceListener> mListener;
};

class AudioCaptureTrackSource : public LocalTrackSource {
 public:
  AudioCaptureTrackSource(nsIPrincipal* aPrincipal, nsPIDOMWindowInner* aWindow,
                          const nsString& aLabel,
                          AudioCaptureTrack* aAudioCaptureTrack,
                          RefPtr<PeerIdentity> aPeerIdentity)
      : LocalTrackSource(aPrincipal, aLabel, nullptr,
                         MediaSourceEnum::AudioCapture, aAudioCaptureTrack,
                         std::move(aPeerIdentity)),
        mWindow(aWindow),
        mAudioCaptureTrack(aAudioCaptureTrack) {
    mAudioCaptureTrack->Start();
    mAudioCaptureTrack->Graph()->RegisterCaptureTrackForWindow(
        mWindow->WindowID(), mAudioCaptureTrack);
    mWindow->SetAudioCapture(true);
  }

  void Stop() override {
    MOZ_ASSERT(NS_IsMainThread());
    if (!mAudioCaptureTrack->IsDestroyed()) {
      MOZ_ASSERT(mWindow);
      mWindow->SetAudioCapture(false);
      mAudioCaptureTrack->Graph()->UnregisterCaptureTrackForWindow(
          mWindow->WindowID());
      mWindow = nullptr;
    }
    // LocalTrackSource destroys the track.
    LocalTrackSource::Stop();
    MOZ_ASSERT(mAudioCaptureTrack->IsDestroyed());
  }

  ProcessedMediaTrack* InputTrack() const { return mAudioCaptureTrack.get(); }

 protected:
  ~AudioCaptureTrackSource() {
    MOZ_ASSERT(NS_IsMainThread());
    MOZ_ASSERT(mAudioCaptureTrack->IsDestroyed());
  }

  RefPtr<nsPIDOMWindowInner> mWindow;
  const RefPtr<AudioCaptureTrack> mAudioCaptureTrack;
};

/**
 * nsIMediaDevice implementation.
 */
NS_IMPL_ISUPPORTS(LocalMediaDevice, nsIMediaDevice)

MediaDevice::MediaDevice(MediaEngine* aEngine, MediaSourceEnum aMediaSource,
                         const nsString& aRawName, const nsString& aRawID,
                         const nsString& aRawGroupID, IsScary aIsScary,
                         const OsPromptable canRequestOsLevelPrompt)
    : mEngine(aEngine),
      mAudioDeviceInfo(nullptr),
      mMediaSource(aMediaSource),
      mKind(MediaEngineSource::IsVideo(aMediaSource)
                ? MediaDeviceKind::Videoinput
                : MediaDeviceKind::Audioinput),
      mScary(aIsScary == IsScary::Yes),
      mCanRequestOsLevelPrompt(canRequestOsLevelPrompt == OsPromptable::Yes),
      mIsFake(mEngine->IsFake()),
      mType(NS_ConvertASCIItoUTF16(dom::GetEnumString(mKind))),
      mRawID(aRawID),
      mRawGroupID(aRawGroupID),
      mRawName(aRawName) {
  MOZ_ASSERT(mEngine);
}

MediaDevice::MediaDevice(MediaEngine* aEngine,
                         const RefPtr<AudioDeviceInfo>& aAudioDeviceInfo,
                         const nsString& aRawID)
    : mEngine(aEngine),
      mAudioDeviceInfo(aAudioDeviceInfo),
      mMediaSource(mAudioDeviceInfo->Type() == AudioDeviceInfo::TYPE_INPUT
                       ? MediaSourceEnum::Microphone
                       : MediaSourceEnum::Other),
      mKind(mMediaSource == MediaSourceEnum::Microphone
                ? MediaDeviceKind::Audioinput
                : MediaDeviceKind::Audiooutput),
      mScary(false),
      mCanRequestOsLevelPrompt(false),
      mIsFake(false),
      mType(NS_ConvertASCIItoUTF16(dom::GetEnumString(mKind))),
      mRawID(aRawID),
      mRawGroupID(mAudioDeviceInfo->GroupID()),
      mRawName(mAudioDeviceInfo->Name()) {}

/* static */
RefPtr<MediaDevice> MediaDevice::CopyWithNewRawGroupId(
    const RefPtr<MediaDevice>& aOther, const nsString& aRawGroupID) {
  MOZ_ASSERT(!aOther->mAudioDeviceInfo, "device not supported");
  return new MediaDevice(aOther->mEngine, aOther->mMediaSource,
                         aOther->mRawName, aOther->mRawID, aRawGroupID,
                         IsScary(aOther->mScary),
                         OsPromptable(aOther->mCanRequestOsLevelPrompt));
}

MediaDevice::~MediaDevice() = default;

LocalMediaDevice::LocalMediaDevice(RefPtr<const MediaDevice> aRawDevice,
                                   const nsString& aID,
                                   const nsString& aGroupID,
                                   const nsString& aName)
    : mRawDevice(std::move(aRawDevice)),
      mName(aName),
      mID(aID),
      mGroupID(aGroupID) {
  MOZ_ASSERT(mRawDevice);
}

/**
 * Helper functions that implement the constraints algorithm from
 * http://dev.w3.org/2011/webrtc/editor/getusermedia.html#methods-5
 */

/* static */
bool LocalMediaDevice::StringsContain(
    const OwningStringOrStringSequence& aStrings, nsString aN) {
  return aStrings.IsString() ? aStrings.GetAsString() == aN
                             : aStrings.GetAsStringSequence().Contains(aN);
}

/* static */
uint32_t LocalMediaDevice::FitnessDistance(
    nsString aN, const ConstrainDOMStringParameters& aParams) {
  if (aParams.mExact.WasPassed() &&
      !StringsContain(aParams.mExact.Value(), aN)) {
    return UINT32_MAX;
  }
  if (aParams.mIdeal.WasPassed() &&
      !StringsContain(aParams.mIdeal.Value(), aN)) {
    return 1;
  }
  return 0;
}

// Binding code doesn't templatize well...

/* static */
uint32_t LocalMediaDevice::FitnessDistance(
    nsString aN,
    const OwningStringOrStringSequenceOrConstrainDOMStringParameters&
        aConstraint) {
  if (aConstraint.IsString()) {
    ConstrainDOMStringParameters params;
    params.mIdeal.Construct();
    params.mIdeal.Value().SetAsString() = aConstraint.GetAsString();
    return FitnessDistance(aN, params);
  } else if (aConstraint.IsStringSequence()) {
    ConstrainDOMStringParameters params;
    params.mIdeal.Construct();
    params.mIdeal.Value().SetAsStringSequence() =
        aConstraint.GetAsStringSequence();
    return FitnessDistance(aN, params);
  } else {
    return FitnessDistance(aN, aConstraint.GetAsConstrainDOMStringParameters());
  }
}

uint32_t LocalMediaDevice::GetBestFitnessDistance(
    const nsTArray<const NormalizedConstraintSet*>& aConstraintSets,
    const MediaEnginePrefs& aPrefs, CallerType aCallerType) {
  MOZ_ASSERT(MediaManager::IsInMediaThread());
  MOZ_ASSERT(GetMediaSource() != MediaSourceEnum::Other);

  bool isChrome = aCallerType == CallerType::System;
  const nsString& id = isChrome ? RawID() : mID;
  auto type = GetMediaSource();
  uint64_t distance = 0;
  if (!aConstraintSets.IsEmpty()) {
    if (isChrome /* For the screen/window sharing preview */ ||
        type == MediaSourceEnum::Camera ||
        type == MediaSourceEnum::Microphone) {
      distance += uint64_t(MediaConstraintsHelper::FitnessDistance(
                      Some(id), aConstraintSets[0]->mDeviceId)) +
                  uint64_t(MediaConstraintsHelper::FitnessDistance(
                      Some(mGroupID), aConstraintSets[0]->mGroupId));
    }
  }
  if (distance < UINT32_MAX) {
    // Forward request to underlying object to interrogate per-mode
    // capabilities.
    distance += Source()->GetBestFitnessDistance(aConstraintSets, aPrefs);
  }
  return std::min<uint64_t>(distance, UINT32_MAX);
}

NS_IMETHODIMP
LocalMediaDevice::GetRawName(nsAString& aName) {
  MOZ_ASSERT(NS_IsMainThread());
  aName.Assign(mRawDevice->mRawName);
  return NS_OK;
}

NS_IMETHODIMP
LocalMediaDevice::GetType(nsAString& aType) {
  MOZ_ASSERT(NS_IsMainThread());
  aType.Assign(mRawDevice->mType);
  return NS_OK;
}

NS_IMETHODIMP
LocalMediaDevice::GetRawId(nsAString& aID) {
  MOZ_ASSERT(NS_IsMainThread());
  aID.Assign(RawID());
  return NS_OK;
}

NS_IMETHODIMP
LocalMediaDevice::GetId(nsAString& aID) {
  MOZ_ASSERT(NS_IsMainThread());
  aID.Assign(mID);
  return NS_OK;
}

NS_IMETHODIMP
LocalMediaDevice::GetScary(bool* aScary) {
  *aScary = mRawDevice->mScary;
  return NS_OK;
}

NS_IMETHODIMP
LocalMediaDevice::GetCanRequestOsLevelPrompt(bool* aCanRequestOsLevelPrompt) {
  *aCanRequestOsLevelPrompt = mRawDevice->mCanRequestOsLevelPrompt;
  return NS_OK;
}

void LocalMediaDevice::GetSettings(MediaTrackSettings& aOutSettings) {
  MOZ_ASSERT(NS_IsMainThread());
  Source()->GetSettings(aOutSettings);
}

void LocalMediaDevice::GetCapabilities(
    MediaTrackCapabilities& aOutCapabilities) {
  MOZ_ASSERT(NS_IsMainThread());
  Source()->GetCapabilities(aOutCapabilities);
}

MediaEngineSource* LocalMediaDevice::Source() {
  if (!mSource) {
    mSource = mRawDevice->mEngine->CreateSource(mRawDevice);
  }
  return mSource;
}

const TrackingId& LocalMediaDevice::GetTrackingId() const {
  return mSource->GetTrackingId();
}

const dom::MediaTrackConstraints& LocalMediaDevice::Constraints() const {
  MOZ_ASSERT(MediaManager::IsInMediaThread());
  return mConstraints;
}

// Threadsafe since mKind and mSource are const.
NS_IMETHODIMP
LocalMediaDevice::GetMediaSource(nsAString& aMediaSource) {
  if (Kind() == MediaDeviceKind::Audiooutput) {
    aMediaSource.Truncate();
  } else {
    aMediaSource.AssignASCII(dom::GetEnumString(GetMediaSource()));
  }
  return NS_OK;
}

nsresult LocalMediaDevice::Allocate(const MediaTrackConstraints& aConstraints,
                                    const MediaEnginePrefs& aPrefs,
                                    uint64_t aWindowID,
                                    const char** aOutBadConstraint) {
  MOZ_ASSERT(MediaManager::IsInMediaThread());

  // Mock failure for automated tests.
  if (IsFake() && aConstraints.mDeviceId.WasPassed() &&
      aConstraints.mDeviceId.Value().IsString() &&
      aConstraints.mDeviceId.Value().GetAsString().EqualsASCII("bad device")) {
    return NS_ERROR_FAILURE;
  }

  nsresult rv =
      Source()->Allocate(aConstraints, aPrefs, aWindowID, aOutBadConstraint);
  if (NS_SUCCEEDED(rv)) {
    mConstraints = aConstraints;
  }
  return rv;
}

void LocalMediaDevice::SetTrack(const RefPtr<MediaTrack>& aTrack,
                                const PrincipalHandle& aPrincipalHandle) {
  MOZ_ASSERT(MediaManager::IsInMediaThread());
  Source()->SetTrack(aTrack, aPrincipalHandle);
}

nsresult LocalMediaDevice::Start() {
  MOZ_ASSERT(MediaManager::IsInMediaThread());
  MOZ_ASSERT(Source());
  return Source()->Start();
}

nsresult LocalMediaDevice::Reconfigure(
    const MediaTrackConstraints& aConstraints, const MediaEnginePrefs& aPrefs,
    const char** aOutBadConstraint) {
  MOZ_ASSERT(MediaManager::IsInMediaThread());
  using H = MediaConstraintsHelper;
  auto type = GetMediaSource();
  if (type == MediaSourceEnum::Camera || type == MediaSourceEnum::Microphone) {
    NormalizedConstraints c(aConstraints);
    if (H::FitnessDistance(Some(mID), c.mDeviceId) == UINT32_MAX) {
      *aOutBadConstraint = "deviceId";
      return NS_ERROR_INVALID_ARG;
    }
    if (H::FitnessDistance(Some(mGroupID), c.mGroupId) == UINT32_MAX) {
      *aOutBadConstraint = "groupId";
      return NS_ERROR_INVALID_ARG;
    }
    if (aPrefs.mResizeModeEnabled && type == MediaSourceEnum::Camera) {
      // Check invalid exact resizeMode constraint (not a device property)
      nsString none =
          NS_ConvertASCIItoUTF16(dom::GetEnumString(VideoResizeModeEnum::None));
      nsString crop = NS_ConvertASCIItoUTF16(
          dom::GetEnumString(VideoResizeModeEnum::Crop_and_scale));
      if (H::FitnessDistance(Some(none), c.mResizeMode) == UINT32_MAX &&
          H::FitnessDistance(Some(crop), c.mResizeMode) == UINT32_MAX) {
        *aOutBadConstraint = "resizeMode";
        return NS_ERROR_INVALID_ARG;
      }
    }
  }
  nsresult rv = Source()->Reconfigure(aConstraints, aPrefs, aOutBadConstraint);
  if (NS_SUCCEEDED(rv)) {
    mConstraints = aConstraints;
  }
  return rv;
}

nsresult LocalMediaDevice::FocusOnSelectedSource() {
  MOZ_ASSERT(MediaManager::IsInMediaThread());
  return Source()->FocusOnSelectedSource();
}

nsresult LocalMediaDevice::Stop() {
  MOZ_ASSERT(MediaManager::IsInMediaThread());
  MOZ_ASSERT(mSource);
  return mSource->Stop();
}

nsresult LocalMediaDevice::Deallocate() {
  MOZ_ASSERT(MediaManager::IsInMediaThread());
  MOZ_ASSERT(mSource);
  return mSource->Deallocate();
}

already_AddRefed<LocalMediaDevice> LocalMediaDevice::Clone() const {
  MOZ_ASSERT(NS_IsMainThread());
  auto device = MakeRefPtr<LocalMediaDevice>(mRawDevice, mID, mGroupID, mName);
  device->mSource =
      mRawDevice->mEngine->CreateSourceFrom(mSource, device->mRawDevice);
#ifdef MOZ_THREAD_SAFETY_OWNERSHIP_CHECKS_SUPPORTED
  // The source is normally created on the MediaManager thread. But for cloning,
  // it ends up being created on main thread. Make sure its owning event target
  // is set properly.
  auto* src = device->Source();
  src->_mOwningThread = mSource->_mOwningThread;
#endif
  return device.forget();
}

MediaSourceEnum MediaDevice::GetMediaSource() const { return mMediaSource; }

static const MediaTrackConstraints& GetInvariant(
    const OwningBooleanOrMediaTrackConstraints& aUnion) {
  static const MediaTrackConstraints empty;
  return aUnion.IsMediaTrackConstraints() ? aUnion.GetAsMediaTrackConstraints()
                                          : empty;
}

// Source getter returning full list

static void GetMediaDevices(MediaEngine* aEngine, MediaSourceEnum aSrcType,
                            MediaManager::MediaDeviceSet& aResult,
                            const char* aMediaDeviceName = nullptr) {
  MOZ_ASSERT(MediaManager::IsInMediaThread());

  LOG("%s: aEngine=%p, aSrcType=%" PRIu8 ", aMediaDeviceName=%s", __func__,
      aEngine, static_cast<uint8_t>(aSrcType),
      aMediaDeviceName ? aMediaDeviceName : "null");
  nsTArray<RefPtr<MediaDevice>> devices;
  aEngine->EnumerateDevices(aSrcType, MediaSinkEnum::Other, &devices);

  /*
   * We're allowing multiple tabs to access the same camera for parity
   * with Chrome.  See bug 811757 for some of the issues surrounding
   * this decision.  To disallow, we'd filter by IsAvailable() as we used
   * to.
   */
  if (aMediaDeviceName && *aMediaDeviceName) {
    for (auto& device : devices) {
      if (device->mRawName.EqualsASCII(aMediaDeviceName)) {
        aResult.AppendElement(device);
        LOG("%s: found aMediaDeviceName=%s", __func__, aMediaDeviceName);
        break;
      }
    }
  } else {
    aResult = std::move(devices);
    if (MOZ_LOG_TEST(gMediaManagerLog, mozilla::LogLevel::Debug)) {
      for (auto& device : aResult) {
        LOG("%s: appending device=%s", __func__,
            NS_ConvertUTF16toUTF8(device->mRawName).get());
      }
    }
  }
}

RefPtr<LocalDeviceSetPromise> MediaManager::SelectSettings(
    const MediaStreamConstraints& aConstraints, CallerType aCallerType,
    RefPtr<LocalMediaDeviceSetRefCnt> aDevices) {
  MOZ_ASSERT(NS_IsMainThread());

  // Algorithm accesses device capabilities code and must run on media thread.
  // Modifies passed-in aDevices.

  return MediaManager::Dispatch<LocalDeviceSetPromise>(
      __func__, [aConstraints, devices = std::move(aDevices), prefs = mPrefs,
                 aCallerType](MozPromiseHolder<LocalDeviceSetPromise>& holder) {
        auto& devicesRef = *devices;

        // Since the advanced part of the constraints algorithm needs to know
        // when a candidate set is overconstrained (zero members), we must split
        // up the list into videos and audios, and put it back together again at
        // the end.

        nsTArray<RefPtr<LocalMediaDevice>> videos;
        nsTArray<RefPtr<LocalMediaDevice>> audios;

        for (const auto& device : devicesRef) {
          MOZ_ASSERT(device->Kind() == MediaDeviceKind::Videoinput ||
                     device->Kind() == MediaDeviceKind::Audioinput);
          if (device->Kind() == MediaDeviceKind::Videoinput) {
            videos.AppendElement(device);
          } else if (device->Kind() == MediaDeviceKind::Audioinput) {
            audios.AppendElement(device);
          }
        }
        devicesRef.Clear();
        const char* badConstraint = nullptr;
        bool needVideo = IsOn(aConstraints.mVideo);
        bool needAudio = IsOn(aConstraints.mAudio);

        if (needVideo && videos.Length()) {
          badConstraint = MediaConstraintsHelper::SelectSettings(
              NormalizedConstraints(GetInvariant(aConstraints.mVideo)), prefs,
              videos, aCallerType);
        }
        if (!badConstraint && needAudio && audios.Length()) {
          badConstraint = MediaConstraintsHelper::SelectSettings(
              NormalizedConstraints(GetInvariant(aConstraints.mAudio)), prefs,
              audios, aCallerType);
        }
        if (badConstraint) {
          LOG("SelectSettings: bad constraint found! Calling error handler!");
          nsString constraint;
          constraint.AssignASCII(badConstraint);
          holder.Reject(
              new MediaMgrError(MediaMgrError::Name::OverconstrainedError, "",
                                constraint),
              __func__);
          return;
        }
        if (!needVideo == !videos.Length() && !needAudio == !audios.Length()) {
          for (auto& video : videos) {
            devicesRef.AppendElement(video);
          }
          for (auto& audio : audios) {
            devicesRef.AppendElement(audio);
          }
        }
        holder.Resolve(devices, __func__);
      });
}

/**
 * Describes a requested task that handles response from the UI and sends
 * results back to the DOM.
 */
class GetUserMediaTask {
 public:
  NS_INLINE_DECL_THREADSAFE_REFCOUNTING(GetUserMediaTask)
  GetUserMediaTask(uint64_t aWindowID, const ipc::PrincipalInfo& aPrincipalInfo,
                   CallerType aCallerType)
      : mPrincipalInfo(aPrincipalInfo),
        mWindowID(aWindowID),
        mCallerType(aCallerType) {}

  virtual void Denied(MediaMgrError::Name aName,
                      const nsCString& aMessage = ""_ns) = 0;

  virtual GetUserMediaStreamTask* AsGetUserMediaStreamTask() { return nullptr; }
  virtual SelectAudioOutputTask* AsSelectAudioOutputTask() { return nullptr; }

  uint64_t GetWindowID() const { return mWindowID; }
  enum CallerType CallerType() const { return mCallerType; }

  size_t SizeOfIncludingThis(MallocSizeOf aMallocSizeOf) const {
    size_t amount = aMallocSizeOf(this);
    // Assume mWindowListener is owned by MediaManager.
    // Assume mAudioDeviceListener and mVideoDeviceListener are owned by
    // mWindowListener.
    // Assume PrincipalInfo string buffers are shared.
    // Member types without support for accounting of pointees:
    //   MozPromiseHolder, RefPtr<LocalMediaDevice>.
    // We don't have a good way to account for lambda captures for MozPromise
    // callbacks.
    return amount;
  }

 protected:
  virtual ~GetUserMediaTask() = default;

  // Call GetPrincipalKey again, if not private browing, this time with
  // persist = true, to promote deviceIds to persistent, in case they're not
  // already. Fire'n'forget.
  void PersistPrincipalKey() {
    if (IsPrincipalInfoPrivate(mPrincipalInfo)) {
      return;
    }
    media::GetPrincipalKey(mPrincipalInfo, true)
        ->Then(
            GetCurrentSerialEventTarget(), __func__,
            [](const media::PrincipalKeyPromise::ResolveOrRejectValue& aValue) {
              if (aValue.IsReject()) {
                LOG("Failed get Principal key. Persisting of deviceIds "
                    "will be broken");
              }
            });
  }

 private:
  // Thread-safe (object) principal of Window with ID mWindowID
  const ipc::PrincipalInfo mPrincipalInfo;

 protected:
  // The ID of the not-necessarily-toplevel inner Window relevant global
  // object of the MediaDevices on which getUserMedia() was called
  const uint64_t mWindowID;
  // Whether the JS caller of getUserMedia() has system (subject) principal
  const enum CallerType mCallerType;
};

/**
 * Describes a requested task that handles response from the UI to a
 * getUserMedia() request and sends results back to content.  If the request
 * is allowed and device initialization succeeds, then the MozPromise is
 * resolved with a DOMMediaStream having a track or tracks for the approved
 * device or devices.
 */
class GetUserMediaStreamTask final : public GetUserMediaTask {
 public:
  GetUserMediaStreamTask(
      const MediaStreamConstraints& aConstraints,
      MozPromiseHolder<MediaManager::StreamPromise>&& aHolder,
      uint64_t aWindowID, RefPtr<GetUserMediaWindowListener> aWindowListener,
      RefPtr<DeviceListener> aAudioDeviceListener,
      RefPtr<DeviceListener> aVideoDeviceListener,
      const MediaEnginePrefs& aPrefs, const ipc::PrincipalInfo& aPrincipalInfo,
      enum CallerType aCallerType, bool aShouldFocusSource)
      : GetUserMediaTask(aWindowID, aPrincipalInfo, aCallerType),
        mConstraints(aConstraints),
        mHolder(std::move(aHolder)),
        mWindowListener(std::move(aWindowListener)),
        mAudioDeviceListener(std::move(aAudioDeviceListener)),
        mVideoDeviceListener(std::move(aVideoDeviceListener)),
        mPrefs(aPrefs),
        mShouldFocusSource(aShouldFocusSource),
        mManager(MediaManager::GetInstance()) {}

  void Allowed(RefPtr<LocalMediaDevice> aAudioDevice,
               RefPtr<LocalMediaDevice> aVideoDevice) {
    MOZ_ASSERT(aAudioDevice || aVideoDevice);
    mAudioDevice = std::move(aAudioDevice);
    mVideoDevice = std::move(aVideoDevice);
    // Reuse the same thread to save memory.
    MediaManager::Dispatch(
        NewRunnableMethod("GetUserMediaStreamTask::AllocateDevices", this,
                          &GetUserMediaStreamTask::AllocateDevices));
  }

  GetUserMediaStreamTask* AsGetUserMediaStreamTask() override { return this; }

 private:
  ~GetUserMediaStreamTask() override {
    if (!mHolder.IsEmpty()) {
      Fail(MediaMgrError::Name::NotAllowedError);
    }
  }

  void Fail(MediaMgrError::Name aName, const nsCString& aMessage = ""_ns,
            const nsString& aConstraint = u""_ns) {
    mHolder.Reject(MakeRefPtr<MediaMgrError>(aName, aMessage, aConstraint),
                   __func__);
    // We add a disabled listener to the StreamListeners array until accepted
    // If this was the only active MediaStream, remove the window from the list.
    NS_DispatchToMainThread(NS_NewRunnableFunction(
        "DeviceListener::Stop",
        [audio = mAudioDeviceListener, video = mVideoDeviceListener] {
          if (audio) {
            audio->Stop();
          }
          if (video) {
            video->Stop();
          }
        }));
  }

  /**
   * Runs on a separate thread and is responsible for allocating devices.
   *
   * Do not run this on the main thread.
   */
  void AllocateDevices() {
    MOZ_ASSERT(!NS_IsMainThread());
    LOG("GetUserMediaStreamTask::AllocateDevices()");

    // Allocate a video or audio device and return a MediaStream via
    // PrepareDOMStream().

    nsresult rv;
    const char* errorMsg = nullptr;
    const char* badConstraint = nullptr;

    if (mAudioDevice) {
      auto& constraints = GetInvariant(mConstraints.mAudio);
      rv = mAudioDevice->Allocate(constraints, mPrefs, mWindowID,
                                  &badConstraint);
      if (NS_FAILED(rv)) {
        errorMsg = "Failed to allocate audiosource";
        if (rv == NS_ERROR_NOT_AVAILABLE && !badConstraint) {
          nsTArray<RefPtr<LocalMediaDevice>> devices;
          devices.AppendElement(mAudioDevice);
          badConstraint = MediaConstraintsHelper::SelectSettings(
              NormalizedConstraints(constraints), mPrefs, devices, mCallerType);
        }
      }
    }
    if (!errorMsg && mVideoDevice) {
      auto& constraints = GetInvariant(mConstraints.mVideo);
      rv = mVideoDevice->Allocate(constraints, mPrefs, mWindowID,
                                  &badConstraint);
      if (NS_FAILED(rv)) {
        errorMsg = "Failed to allocate videosource";
        if (rv == NS_ERROR_NOT_AVAILABLE && !badConstraint) {
          nsTArray<RefPtr<LocalMediaDevice>> devices;
          devices.AppendElement(mVideoDevice);
          badConstraint = MediaConstraintsHelper::SelectSettings(
              NormalizedConstraints(constraints), mPrefs, devices, mCallerType);
        }
        if (mAudioDevice) {
          mAudioDevice->Deallocate();
        }
      } else {
        mVideoTrackingId.emplace(mVideoDevice->GetTrackingId());
      }
    }
    if (errorMsg) {
      LOG("%s %" PRIu32, errorMsg, static_cast<uint32_t>(rv));
      if (badConstraint) {
        Fail(MediaMgrError::Name::OverconstrainedError, ""_ns,
             NS_ConvertUTF8toUTF16(badConstraint));
      } else {
        Fail(MediaMgrError::Name::NotReadableError, nsCString(errorMsg));
      }
      NS_DispatchToMainThread(
          NS_NewRunnableFunction("MediaManager::SendPendingGUMRequest", []() {
            if (MediaManager* manager = MediaManager::GetIfExists()) {
              manager->SendPendingGUMRequest();
            }
          }));
      return;
    }
    NS_DispatchToMainThread(
        NewRunnableMethod("GetUserMediaStreamTask::PrepareDOMStream", this,
                          &GetUserMediaStreamTask::PrepareDOMStream));
  }

 public:
  void Denied(MediaMgrError::Name aName, const nsCString& aMessage) override {
    MOZ_ASSERT(NS_IsMainThread());
    Fail(aName, aMessage);
  }

  const MediaStreamConstraints& GetConstraints() { return mConstraints; }

  void PrimeVoiceProcessing() {
    mPrimingStream = MakeAndAddRef<PrimingCubebVoiceInputStream>();
    mPrimingStream->Init();
  }

 private:
  void PrepareDOMStream();

  class PrimingCubebVoiceInputStream {
    class Listener final : public CubebInputStream::Listener {
      NS_INLINE_DECL_THREADSAFE_REFCOUNTING(Listener, override);

     private:
      ~Listener() = default;

      long DataCallback(const void*, long) override {
        MOZ_CRASH("Unexpected data callback");
      }
      void StateCallback(cubeb_state) override {}
      void DeviceChangedCallback() override {}
    };

    NS_INLINE_DECL_THREADSAFE_REFCOUNTING_WITH_DELETE_ON_EVENT_TARGET(
        PrimingCubebVoiceInputStream, mCubebThread.GetEventTarget())

   public:
    void Init() {
      mCubebThread.GetEventTarget()->Dispatch(
          NS_NewRunnableFunction(__func__, [this, self = RefPtr(this)] {
            mCubebThread.AssertOnCurrentThread();
            LOG("Priming voice processing with stream %p", this);
            TRACE("PrimingCubebVoiceInputStream::Init");
            const cubeb_devid default_device = nullptr;
            const uint32_t mono = 1;
            const uint32_t rate = CubebUtils::PreferredSampleRate(false);
            const bool isVoice = true;
            mCubebStream =
                CubebInputStream::Create(default_device, mono, rate, isVoice,
                                         MakeRefPtr<Listener>().get());
          }));
    }

   private:
    ~PrimingCubebVoiceInputStream() {
      mCubebThread.AssertOnCurrentThread();
      LOG("Releasing primed voice processing stream %p", this);
      mCubebStream = nullptr;
    }

    const EventTargetCapability<nsISerialEventTarget> mCubebThread =
        EventTargetCapability<nsISerialEventTarget>(
            TaskQueue::Create(CubebUtils::GetCubebOperationThread(),
                              "PrimingCubebInputStream::mCubebThread")
                .get());
    UniquePtr<CubebInputStream> mCubebStream MOZ_GUARDED_BY(mCubebThread);
  };

  // Constraints derived from those passed to getUserMedia() but adjusted for
  // preferences, defaults, and security
  const MediaStreamConstraints mConstraints;

  MozPromiseHolder<MediaManager::StreamPromise> mHolder;
  // GetUserMediaWindowListener with which DeviceListeners are registered
  const RefPtr<GetUserMediaWindowListener> mWindowListener;
  const RefPtr<DeviceListener> mAudioDeviceListener;
  const RefPtr<DeviceListener> mVideoDeviceListener;
  // MediaDevices are set when selected and Allowed() by the UI.
  RefPtr<LocalMediaDevice> mAudioDevice;
  RefPtr<LocalMediaDevice> mVideoDevice;
  RefPtr<PrimingCubebVoiceInputStream> mPrimingStream;
  // Tracking id unique for a video frame source. Set when the corresponding
  // device has been allocated.
  Maybe<TrackingId> mVideoTrackingId;
  // Copy of MediaManager::mPrefs
  const MediaEnginePrefs mPrefs;
  // media.getusermedia.window.focus_source.enabled
  const bool mShouldFocusSource;
  // The MediaManager is referenced at construction so that it won't be
  // created after its ShutdownBlocker would run.
  const RefPtr<MediaManager> mManager;
};

/**
 * Creates a MediaTrack, attaches a listener and resolves a MozPromise to
 * provide the stream to the DOM.
 *
 * All of this must be done on the main thread!
 */
void GetUserMediaStreamTask::PrepareDOMStream() {
  MOZ_ASSERT(NS_IsMainThread());
  LOG("GetUserMediaStreamTask::PrepareDOMStream()");
  nsGlobalWindowInner* window =
      nsGlobalWindowInner::GetInnerWindowWithId(mWindowID);

  // We're on main-thread, and the windowlist can only
  // be invalidated from the main-thread (see OnNavigation)
  if (!mManager->IsWindowListenerStillActive(mWindowListener)) {
    // This window is no longer live. mListener has already been removed.
    return;
  }

  MediaTrackGraph::GraphDriverType graphDriverType =
      mAudioDevice ? MediaTrackGraph::AUDIO_THREAD_DRIVER
                   : MediaTrackGraph::SYSTEM_THREAD_DRIVER;
  MediaTrackGraph* mtg = MediaTrackGraph::GetInstance(
      graphDriverType, window, MediaTrackGraph::REQUEST_DEFAULT_SAMPLE_RATE,
      MediaTrackGraph::DEFAULT_OUTPUT_DEVICE);

  auto domStream = MakeRefPtr<DOMMediaStream>(window);
  RefPtr<LocalTrackSource> audioTrackSource;
  RefPtr<LocalTrackSource> videoTrackSource;
  nsCOMPtr<nsIPrincipal> principal;
  RefPtr<PeerIdentity> peerIdentity = nullptr;
  if (!mConstraints.mPeerIdentity.IsEmpty()) {
    peerIdentity = new PeerIdentity(mConstraints.mPeerIdentity);
    principal = NullPrincipal::CreateWithInheritedAttributes(
        window->GetExtantDoc()->NodePrincipal());
  } else {
    principal = window->GetExtantDoc()->NodePrincipal();
  }
  RefPtr<GenericNonExclusivePromise> firstFramePromise;
  if (mAudioDevice) {
    if (mAudioDevice->GetMediaSource() == MediaSourceEnum::AudioCapture) {
      // AudioCapture is a special case, here, in the sense that we're not
      // really using the audio source and the SourceMediaTrack, which acts
      // as placeholders. We re-route a number of tracks internally in the
      // MTG and mix them down instead.
      NS_WARNING(
          "MediaCaptureWindowState doesn't handle "
          "MediaSourceEnum::AudioCapture. This must be fixed with UX "
          "before shipping.");
      auto audioCaptureSource = MakeRefPtr<AudioCaptureTrackSource>(
          principal, window, u"Window audio capture"_ns,
          mtg->CreateAudioCaptureTrack(), peerIdentity);
      audioTrackSource = audioCaptureSource;
      RefPtr<MediaStreamTrack> track = new dom::AudioStreamTrack(
          window, audioCaptureSource->InputTrack(), audioCaptureSource);
      domStream->AddTrackInternal(track);
    } else {
      const nsString& audioDeviceName = mAudioDevice->mName;
      RefPtr<MediaTrack> track;
#ifdef MOZ_WEBRTC
      if (mAudioDevice->IsFake()) {
        track = mtg->CreateSourceTrack(MediaSegment::AUDIO);
      } else {
        track = AudioProcessingTrack::Create(mtg);
        track->Suspend();  // Microphone source resumes in SetTrack
      }
#else
      track = mtg->CreateSourceTrack(MediaSegment::AUDIO);
#endif
      audioTrackSource = new LocalTrackSource(
          principal, audioDeviceName, mAudioDeviceListener,
          mAudioDevice->GetMediaSource(), track, peerIdentity);
      MOZ_ASSERT(MediaManager::IsOn(mConstraints.mAudio));
      RefPtr<MediaStreamTrack> domTrack = new dom::AudioStreamTrack(
          window, track, audioTrackSource, dom::MediaStreamTrackState::Live,
          false, GetInvariant(mConstraints.mAudio));
      domStream->AddTrackInternal(domTrack);
    }
  }
  if (mVideoDevice) {
    const nsString& videoDeviceName = mVideoDevice->mName;
    RefPtr<MediaTrack> track = mtg->CreateSourceTrack(MediaSegment::VIDEO);
    videoTrackSource = new LocalTrackSource(
        principal, videoDeviceName, mVideoDeviceListener,
        mVideoDevice->GetMediaSource(), track, peerIdentity, *mVideoTrackingId);
    MOZ_ASSERT(MediaManager::IsOn(mConstraints.mVideo));
    RefPtr<MediaStreamTrack> domTrack = new dom::VideoStreamTrack(
        window, track, videoTrackSource, dom::MediaStreamTrackState::Live,
        false, GetInvariant(mConstraints.mVideo));
    domStream->AddTrackInternal(domTrack);
    switch (mVideoDevice->GetMediaSource()) {
      case MediaSourceEnum::Browser:
      case MediaSourceEnum::Screen:
      case MediaSourceEnum::Window:
        // Wait for first frame for screen-sharing devices, to ensure
        // with and height settings are available immediately, to pass wpt.
        firstFramePromise = mVideoDevice->Source()->GetFirstFramePromise();
        break;
      default:
        break;
    }
  }

  if (!domStream || (!audioTrackSource && !videoTrackSource) ||
      sHasMainThreadShutdown) {
    LOG("Returning error for getUserMedia() - no stream");

    mHolder.Reject(
        MakeRefPtr<MediaMgrError>(
            MediaMgrError::Name::AbortError,
            sHasMainThreadShutdown ? "In shutdown"_ns : "No stream."_ns),
        __func__);
    return;
  }

  // Activate our device listeners. We'll call Start() on the source when we
  // get a callback that the MediaStream has started consuming. The listener
  // is freed when the page is invalidated (on navigation or close).
  if (mAudioDeviceListener) {
    mWindowListener->Activate(mAudioDeviceListener, mAudioDevice,
                              std::move(audioTrackSource),
                              /*aIsAllocated=*/true);
  }
  if (mVideoDeviceListener) {
    mWindowListener->Activate(mVideoDeviceListener, mVideoDevice,
                              std::move(videoTrackSource),
                              /*aIsAllocated=*/true);
  }

  // Dispatch to the media thread to ask it to start the sources, because that
  // can take a while.
  typedef DeviceListener::DeviceListenerPromise PromiseType;
  AutoTArray<RefPtr<PromiseType>, 2> promises;
  if (mAudioDeviceListener) {
    promises.AppendElement(mAudioDeviceListener->InitializeAsync());
  }
  if (mVideoDeviceListener) {
    promises.AppendElement(mVideoDeviceListener->InitializeAsync());
  }
  PromiseType::All(GetMainThreadSerialEventTarget(), promises)
      ->Then(
          GetMainThreadSerialEventTarget(), __func__,
          [manager = mManager, windowListener = mWindowListener,
           firstFramePromise] {
            LOG("GetUserMediaStreamTask::PrepareDOMStream: starting success "
                "callback following InitializeAsync()");
            // Initiating and starting devices succeeded.
            windowListener->ChromeAffectingStateChanged();
            manager->SendPendingGUMRequest();
            if (!firstFramePromise) {
              return DeviceListener::DeviceListenerPromise::CreateAndResolve(
                  true, __func__);
            }
            RefPtr<DeviceListener::DeviceListenerPromise> resolvePromise =
                firstFramePromise->Then(
                    GetMainThreadSerialEventTarget(), __func__,
                    [] {
                      return DeviceListener::DeviceListenerPromise::
                          CreateAndResolve(true, __func__);
                    },
                    [](nsresult aError) {
                      MOZ_ASSERT(NS_FAILED(aError));
                      if (aError == NS_ERROR_UNEXPECTED) {
                        return DeviceListener::DeviceListenerPromise::
                            CreateAndReject(
                                MakeRefPtr<MediaMgrError>(
                                    MediaMgrError::Name::NotAllowedError),
                                __func__);
                      }
                      MOZ_ASSERT(aError == NS_ERROR_ABORT);
                      return DeviceListener::DeviceListenerPromise::
                          CreateAndReject(MakeRefPtr<MediaMgrError>(
                                              MediaMgrError::Name::AbortError,
                                              "In shutdown"),
                                          __func__);
                    });
            return resolvePromise;
          },
          [audio = mAudioDeviceListener,
           video = mVideoDeviceListener](const RefPtr<MediaMgrError>& aError) {
            LOG("GetUserMediaStreamTask::PrepareDOMStream: starting failure "
                "callback following InitializeAsync()");
            if (audio) {
              audio->Stop();
            }
            if (video) {
              video->Stop();
            }
            return DeviceListener::DeviceListenerPromise::CreateAndReject(
                aError, __func__);
          })
      ->Then(
          GetMainThreadSerialEventTarget(), __func__,
          [holder = std::move(mHolder), domStream, callerType = mCallerType,
           shouldFocus = mShouldFocusSource, videoDevice = mVideoDevice](
              const DeviceListener::DeviceListenerPromise::ResolveOrRejectValue&
                  aValue) mutable {
            if (aValue.IsResolve()) {
              if (auto* mgr = MediaManager::GetIfExists();
                  mgr && !sHasMainThreadShutdown && videoDevice &&
                  callerType == CallerType::NonSystem && shouldFocus) {
                // Device was successfully started. Attempt to focus the
                // source.
                MOZ_ALWAYS_SUCCEEDS(
                    mgr->mMediaThread->Dispatch(NS_NewRunnableFunction(
                        "GetUserMediaStreamTask::FocusOnSelectedSource",
                        [videoDevice = std::move(videoDevice)] {
                          nsresult rv = videoDevice->FocusOnSelectedSource();
                          if (NS_FAILED(rv)) {
                            LOG("FocusOnSelectedSource failed");
                          }
                        })));
              }

              holder.Resolve(domStream, __func__);
            } else {
              holder.Reject(aValue.RejectValue(), __func__);
            }
          });

  PersistPrincipalKey();
}

/**
 * Describes a requested task that handles response from the UI to a
 * selectAudioOutput() request and sends results back to content.  If the
 * request is allowed, then the MozPromise is resolved with a MediaDevice
 * for the approved device.
 */
class SelectAudioOutputTask final : public GetUserMediaTask {
 public:
  SelectAudioOutputTask(MozPromiseHolder<LocalDevicePromise>&& aHolder,
                        uint64_t aWindowID, enum CallerType aCallerType,
                        const ipc::PrincipalInfo& aPrincipalInfo)
      : GetUserMediaTask(aWindowID, aPrincipalInfo, aCallerType),
        mHolder(std::move(aHolder)) {}

  void Allowed(RefPtr<LocalMediaDevice> aAudioOutput) {
    MOZ_ASSERT(aAudioOutput);
    mHolder.Resolve(std::move(aAudioOutput), __func__);
    PersistPrincipalKey();
  }

  void Denied(MediaMgrError::Name aName, const nsCString& aMessage) override {
    MOZ_ASSERT(NS_IsMainThread());
    Fail(aName, aMessage);
  }

  SelectAudioOutputTask* AsSelectAudioOutputTask() override { return this; }

 private:
  ~SelectAudioOutputTask() override {
    if (!mHolder.IsEmpty()) {
      Fail(MediaMgrError::Name::NotAllowedError);
    }
  }

  void Fail(MediaMgrError::Name aName, const nsCString& aMessage = ""_ns) {
    mHolder.Reject(MakeRefPtr<MediaMgrError>(aName, aMessage), __func__);
  }

 private:
  MozPromiseHolder<LocalDevicePromise> mHolder;
};

/* static */
void MediaManager::GuessVideoDeviceGroupIDs(MediaDeviceSet& aDevices,
                                            const MediaDeviceSet& aAudios) {
  // Run the logic in a lambda to avoid duplication.
  auto updateGroupIdIfNeeded = [&](RefPtr<MediaDevice>& aVideo,
                                   const MediaDeviceKind aKind) -> bool {
    MOZ_ASSERT(aVideo->mKind == MediaDeviceKind::Videoinput);
    MOZ_ASSERT(aKind == MediaDeviceKind::Audioinput ||
               aKind == MediaDeviceKind::Audiooutput);
    // This will store the new group id if a match is found.
    nsString newVideoGroupID;
    // If the group id needs to be updated this will become true. It is
    // necessary when the new group id is an empty string. Without this extra
    // variable to signal the update, we would resort to test if
    // `newVideoGroupId` is empty. However,
    // that check does not work when the new group id is an empty string.
    bool updateGroupId = false;
    for (const RefPtr<MediaDevice>& dev : aAudios) {
      if (dev->mKind != aKind) {
        continue;
      }
      if (!FindInReadable(aVideo->mRawName, dev->mRawName)) {
        continue;
      }
      if (newVideoGroupID.IsEmpty()) {
        // This is only expected on first match. If that's the only match group
        // id will be updated to this one at the end of the loop.
        updateGroupId = true;
        newVideoGroupID = dev->mRawGroupID;
      } else {
        // More than one device found, it is impossible to know which group id
        // is the correct one.
        updateGroupId = false;
        newVideoGroupID = u""_ns;
        break;
      }
    }
    if (updateGroupId) {
      aVideo = MediaDevice::CopyWithNewRawGroupId(aVideo, newVideoGroupID);
      return true;
    }
    return false;
  };

  for (RefPtr<MediaDevice>& video : aDevices) {
    if (video->mKind != MediaDeviceKind::Videoinput) {
      continue;
    }
    if (updateGroupIdIfNeeded(video, MediaDeviceKind::Audioinput)) {
      // GroupId has been updated, continue to the next video device
      continue;
    }
    // GroupId has not been updated, check among the outputs
    updateGroupIdIfNeeded(video, MediaDeviceKind::Audiooutput);
  }
}

namespace {

// Class to hold the promise used to request device access and to resolve
// even if |task| does not run, either because GeckoViewPermissionProcessChild
// gets destroyed before ask-device-permission receives its
// got-device-permission reply, or because the media thread is no longer
// available.  In either case, the process is shutting down so the result is
// not important.  Reject with a dummy error so the following Then-handler can
// resolve with an empty set, so that callers do not need to handle rejection.
class DeviceAccessRequestPromiseHolderWithFallback
    : public MozPromiseHolder<MozPromise<
          CamerasAccessStatus, mozilla::ipc::ResponseRejectReason, true>> {
 public:
  DeviceAccessRequestPromiseHolderWithFallback() = default;
  DeviceAccessRequestPromiseHolderWithFallback(
      DeviceAccessRequestPromiseHolderWithFallback&&) = default;
  ~DeviceAccessRequestPromiseHolderWithFallback() {
    if (!IsEmpty()) {
      Reject(ipc::ResponseRejectReason::ChannelClosed, __func__);
    }
  }
};

}  // anonymous namespace

MediaManager::DeviceEnumerationParams::DeviceEnumerationParams(
    dom::MediaSourceEnum aInputType, DeviceType aType,
    nsAutoCString aForcedDeviceName)
    : mInputType(aInputType),
      mType(aType),
      mForcedDeviceName(std::move(aForcedDeviceName)) {
  MOZ_ASSERT(NS_IsMainThread());
  MOZ_ASSERT(mInputType != dom::MediaSourceEnum::Other);
  MOZ_ASSERT_IF(!mForcedDeviceName.IsEmpty(), mType == DeviceType::Real);
}

MediaManager::VideoDeviceEnumerationParams::VideoDeviceEnumerationParams(
    dom::MediaSourceEnum aInputType, DeviceType aType,
    nsAutoCString aForcedDeviceName, nsAutoCString aForcedMicrophoneName)
    : DeviceEnumerationParams(aInputType, aType, std::move(aForcedDeviceName)),
      mForcedMicrophoneName(std::move(aForcedMicrophoneName)) {
  MOZ_ASSERT(NS_IsMainThread());
  MOZ_ASSERT_IF(!mForcedMicrophoneName.IsEmpty(),
                mInputType == dom::MediaSourceEnum::Camera);
  MOZ_ASSERT_IF(!mForcedMicrophoneName.IsEmpty(), mType == DeviceType::Real);
}

MediaManager::EnumerationParams::EnumerationParams(
    EnumerationFlags aFlags, Maybe<VideoDeviceEnumerationParams> aVideo,
    Maybe<DeviceEnumerationParams> aAudio)
    : mFlags(aFlags), mVideo(std::move(aVideo)), mAudio(std::move(aAudio)) {
  MOZ_ASSERT(NS_IsMainThread());
  MOZ_ASSERT_IF(mVideo, MediaEngineSource::IsVideo(mVideo->mInputType));
  MOZ_ASSERT_IF(mVideo && !mVideo->mForcedDeviceName.IsEmpty(),
                mVideo->mInputType == dom::MediaSourceEnum::Camera);
  MOZ_ASSERT_IF(mVideo && mVideo->mType == DeviceType::Fake,
                mVideo->mInputType == dom::MediaSourceEnum::Camera);
  MOZ_ASSERT_IF(mAudio, MediaEngineSource::IsAudio(mAudio->mInputType));
  MOZ_ASSERT_IF(mAudio && !mAudio->mForcedDeviceName.IsEmpty(),
                mAudio->mInputType == dom::MediaSourceEnum::Microphone);
  MOZ_ASSERT_IF(mAudio && mAudio->mType == DeviceType::Fake,
                mAudio->mInputType == dom::MediaSourceEnum::Microphone);
}

bool MediaManager::EnumerationParams::HasFakeCams() const {
  return mVideo
      .map([](const auto& aDev) { return aDev.mType == DeviceType::Fake; })
      .valueOr(false);
}

bool MediaManager::EnumerationParams::HasFakeMics() const {
  return mAudio
      .map([](const auto& aDev) { return aDev.mType == DeviceType::Fake; })
      .valueOr(false);
}

bool MediaManager::EnumerationParams::RealDeviceRequested() const {
  auto isReal = [](const auto& aDev) { return aDev.mType == DeviceType::Real; };
  return mVideo.map(isReal).valueOr(false) ||
         mAudio.map(isReal).valueOr(false) ||
         mFlags.contains(EnumerationFlag::EnumerateAudioOutputs);
}

MediaSourceEnum MediaManager::EnumerationParams::VideoInputType() const {
  return mVideo.map([](const auto& aDev) { return aDev.mInputType; })
      .valueOr(MediaSourceEnum::Other);
}

MediaSourceEnum MediaManager::EnumerationParams::AudioInputType() const {
  return mAudio.map([](const auto& aDev) { return aDev.mInputType; })
      .valueOr(MediaSourceEnum::Other);
}

/* static */ MediaManager::EnumerationParams
MediaManager::CreateEnumerationParams(dom::MediaSourceEnum aVideoInputType,
                                      dom::MediaSourceEnum aAudioInputType,
                                      EnumerationFlags aFlags) {
  MOZ_ASSERT(NS_IsMainThread());
  MOZ_ASSERT_IF(!MediaEngineSource::IsVideo(aVideoInputType),
                aVideoInputType == dom::MediaSourceEnum::Other);
  MOZ_ASSERT_IF(!MediaEngineSource::IsAudio(aAudioInputType),
                aAudioInputType == dom::MediaSourceEnum::Other);
  const bool forceFakes = aFlags.contains(EnumerationFlag::ForceFakes);
  const bool fakeByPref = Preferences::GetBool("media.navigator.streams.fake");
  Maybe<VideoDeviceEnumerationParams> videoParams;
  Maybe<DeviceEnumerationParams> audioParams;
  nsAutoCString audioDev;
  bool audioDevRead = false;
  constexpr const char* VIDEO_DEV_NAME = "media.video_loopback_dev";
  constexpr const char* AUDIO_DEV_NAME = "media.audio_loopback_dev";
  const auto ensureDev = [](const char* aPref, nsAutoCString* aLoopDev,
                            bool* aPrefRead) {
    if (aPrefRead) {
      if (*aPrefRead) {
        return;
      }
      *aPrefRead = true;
    }

    if (NS_FAILED(Preferences::GetCString(aPref, *aLoopDev))) {
      // Ensure we fall back to an empty string if reading the pref failed.
      aLoopDev->SetIsVoid(true);
    }
  };
  if (MediaEngineSource::IsVideo(aVideoInputType)) {
    nsAutoCString videoDev;
    DeviceType type = DeviceType::Real;
    if (aVideoInputType == MediaSourceEnum::Camera) {
      // Fake and loopback devices are supported for only Camera.
      if (forceFakes) {
        type = DeviceType::Fake;
      } else {
        ensureDev(VIDEO_DEV_NAME, &videoDev, nullptr);
        // Loopback prefs take precedence over fake prefs
        if (fakeByPref && videoDev.IsEmpty()) {
          type = DeviceType::Fake;
        } else {
          // For groupId correlation we need the audio device name.
          ensureDev(AUDIO_DEV_NAME, &audioDev, &audioDevRead);
        }
      }
    }
    videoParams = Some(VideoDeviceEnumerationParams(
        aVideoInputType, type, std::move(videoDev), audioDev));
  }
  if (MediaEngineSource::IsAudio(aAudioInputType)) {
    nsAutoCString realAudioDev;
    DeviceType type = DeviceType::Real;
    if (aAudioInputType == MediaSourceEnum::Microphone) {
      // Fake and loopback devices are supported for only Microphone.
      if (forceFakes) {
        type = DeviceType::Fake;
      } else {
        ensureDev(AUDIO_DEV_NAME, &audioDev, &audioDevRead);
        // Loopback prefs take precedence over fake prefs
        if (fakeByPref && audioDev.IsEmpty()) {
          type = DeviceType::Fake;
        } else {
          realAudioDev = std::move(audioDev);
        }
      }
    }
    audioParams = Some(DeviceEnumerationParams(aAudioInputType, type,
                                               std::move(realAudioDev)));
  }
  return EnumerationParams(aFlags, std::move(videoParams),
                           std::move(audioParams));
}

RefPtr<DeviceSetPromise>
MediaManager::MaybeRequestPermissionAndEnumerateRawDevices(
    EnumerationParams aParams) {
  MOZ_ASSERT(NS_IsMainThread());
  MOZ_ASSERT(aParams.mVideo.isSome() || aParams.mAudio.isSome() ||
             aParams.mFlags.contains(EnumerationFlag::EnumerateAudioOutputs));

  LOG("%s: aVideoInputType=%" PRIu8 ", aAudioInputType=%" PRIu8, __func__,
      static_cast<uint8_t>(aParams.VideoInputType()),
      static_cast<uint8_t>(aParams.AudioInputType()));

  if (sHasMainThreadShutdown) {
    // The media thread is no longer available but the result will not be
    // observable.
    return DeviceSetPromise::CreateAndResolve(
        new MediaDeviceSetRefCnt(),
        "MaybeRequestPermissionAndEnumerateRawDevices: sync shutdown");
  }

  const bool hasVideo = aParams.mVideo.isSome();
  const bool hasAudio = aParams.mAudio.isSome();
  const bool hasAudioOutput =
      aParams.mFlags.contains(EnumerationFlag::EnumerateAudioOutputs);
  const bool hasFakeCams = aParams.HasFakeCams();
  const bool hasFakeMics = aParams.HasFakeMics();
  // True if at least one of video input or audio input is a real device
  // or there is audio output.
  const bool realDeviceRequested = (!hasFakeCams && hasVideo) ||
                                   (!hasFakeMics && hasAudio) || hasAudioOutput;

  using NativePromise =
      MozPromise<CamerasAccessStatus, mozilla::ipc::ResponseRejectReason,
                 /* IsExclusive = */ true>;
  RefPtr<NativePromise> deviceAccessPromise;
  if (realDeviceRequested &&
      aParams.mFlags.contains(EnumerationFlag::AllowPermissionRequest) &&
      Preferences::GetBool("media.navigator.permission.device", false)) {
    // Need to ask permission to retrieve list of all devices;
    // notify frontend observer and wait for callback notification to post
    // task.
    const char16_t* const type =
        (aParams.VideoInputType() != MediaSourceEnum::Camera)       ? u"audio"
        : (aParams.AudioInputType() != MediaSourceEnum::Microphone) ? u"video"
                                                                    : u"all";
    nsCOMPtr<nsIObserverService> obs = services::GetObserverService();
    DeviceAccessRequestPromiseHolderWithFallback deviceAccessPromiseHolder;
    deviceAccessPromise = deviceAccessPromiseHolder.Ensure(__func__);
    RefPtr task = NS_NewRunnableFunction(
        __func__, [holder = std::move(deviceAccessPromiseHolder)]() mutable {
          holder.Resolve(CamerasAccessStatus::Granted,
                         "getUserMedia:got-device-permission");
        });
    obs->NotifyObservers(static_cast<nsIRunnable*>(task),
                         "getUserMedia:ask-device-permission", type);
  } else if (realDeviceRequested && hasVideo &&
             aParams.VideoInputType() == MediaSourceEnum::Camera) {
    ipc::PBackgroundChild* backgroundChild =
        ipc::BackgroundChild::GetOrCreateForCurrentThread();
    deviceAccessPromise = backgroundChild->SendRequestCameraAccess(
        aParams.mFlags.contains(EnumerationFlag::AllowPermissionRequest));
  }

  if (!deviceAccessPromise) {
    // No device access request needed. We can proceed directly, but we still
    // need to update camera availability, because the camera engine is always
    // created together with the WebRTC backend, which is done because
    // devicechange events must work before prompting in cases where persistent
    // permission has already been given. Making a request to camera access not
    // allowing a permission request does exactly what we need in this case.
    ipc::PBackgroundChild* backgroundChild =
        ipc::BackgroundChild::GetOrCreateForCurrentThread();
    deviceAccessPromise = backgroundChild->SendRequestCameraAccess(false);
  }

  return deviceAccessPromise->Then(
      GetCurrentSerialEventTarget(), __func__,
      [this, self = RefPtr(this), aParams = std::move(aParams)](
          NativePromise::ResolveOrRejectValue&& aValue) mutable {
        if (sHasMainThreadShutdown) {
          return DeviceSetPromise::CreateAndResolve(
              new MediaDeviceSetRefCnt(),
              "MaybeRequestPermissionAndEnumerateRawDevices: async shutdown");
        }

        if (aValue.IsReject()) {
          // IPC failure probably means we're in shutdown. Resolve with
          // an empty set, so that callers do not need to handle rejection.
          return DeviceSetPromise::CreateAndResolve(
              new MediaDeviceSetRefCnt(),
              "MaybeRequestPermissionAndEnumerateRawDevices: ipc failure");
        }

        if (auto v = aValue.ResolveValue();
            v == CamerasAccessStatus::Error ||
            v == CamerasAccessStatus::Rejected) {
          LOG("Request to camera access %s",
              v == CamerasAccessStatus::Rejected ? "was rejected" : "failed");
          if (v == CamerasAccessStatus::Error) {
            NS_WARNING("Failed to request camera access");
          }
          return DeviceSetPromise::CreateAndReject(
              MakeRefPtr<MediaMgrError>(MediaMgrError::Name::NotAllowedError),
              "MaybeRequestPermissionAndEnumerateRawDevices: camera access "
              "rejected");
        }

        // We have to nest this, unfortunately, since we have no guarantees that
        // mMediaThread is alive. If we'd reject due to shutdown above, and have
        // the below async operation in a Then handler on the media thread the
        // Then handler would fail to dispatch and trip an assert on
        // destruction, for instance.
        return InvokeAsync(
            mMediaThread, __func__, [aParams = std::move(aParams)]() mutable {
              return DeviceSetPromise::CreateAndResolve(
                  EnumerateRawDevices(std::move(aParams)),
                  "MaybeRequestPermissionAndEnumerateRawDevices: success");
            });
      });
}

/**
 * EnumerateRawDevices - Enumerate a list of audio & video devices that
 * satisfy passed-in constraints. List contains raw id's.
 */

/* static */ RefPtr<MediaManager::MediaDeviceSetRefCnt>
MediaManager::EnumerateRawDevices(EnumerationParams aParams) {
  MOZ_ASSERT(IsInMediaThread());
  // Only enumerate what's asked for, and only fake cams and mics.
  RefPtr<MediaEngine> fakeBackend, realBackend;
  if (aParams.HasFakeCams() || aParams.HasFakeMics()) {
    fakeBackend = new MediaEngineFake();
  }
  if (aParams.RealDeviceRequested()) {
    MediaManager* manager = MediaManager::GetIfExists();
    MOZ_RELEASE_ASSERT(manager, "Must exist while media thread is alive");
    realBackend = manager->GetBackend();
  }

  RefPtr<MediaEngine> videoBackend;
  RefPtr<MediaEngine> audioBackend;
  Maybe<MediaDeviceSet> micsOfVideoBackend;
  Maybe<MediaDeviceSet> speakers;
  RefPtr devices = new MediaDeviceSetRefCnt();

  // Enumerate microphones first, then cameras, then speakers, since
  // the enumerateDevices() algorithm expects them listed in that order.
  if (const auto& audio = aParams.mAudio; audio.isSome()) {
    audioBackend = aParams.HasFakeMics() ? fakeBackend : realBackend;
    MediaDeviceSet audios;
    LOG("EnumerateRawDevices: Getting audio sources with %s backend",
        audioBackend == fakeBackend ? "fake" : "real");
    GetMediaDevices(audioBackend, audio->mInputType, audios,
                    audio->mForcedDeviceName.get());
    if (audio->mInputType == MediaSourceEnum::Microphone &&
        audioBackend == videoBackend) {
      micsOfVideoBackend.emplace();
      micsOfVideoBackend->AppendElements(audios);
    }
    devices->AppendElements(std::move(audios));
  }
  if (const auto& video = aParams.mVideo; video.isSome()) {
    videoBackend = aParams.HasFakeCams() ? fakeBackend : realBackend;
    MediaDeviceSet videos;
    LOG("EnumerateRawDevices: Getting video sources with %s backend",
        videoBackend == fakeBackend ? "fake" : "real");
    GetMediaDevices(videoBackend, video->mInputType, videos,
                    video->mForcedDeviceName.get());
    devices->AppendElements(std::move(videos));
  }
  if (aParams.mFlags.contains(EnumerationFlag::EnumerateAudioOutputs)) {
    MediaDeviceSet outputs;
    MOZ_ASSERT(realBackend);
    realBackend->EnumerateDevices(MediaSourceEnum::Other,
                                  MediaSinkEnum::Speaker, &outputs);
    speakers = Some(MediaDeviceSet());
    speakers->AppendElements(outputs);
    devices->AppendElements(std::move(outputs));
  }
  if (aParams.VideoInputType() == MediaSourceEnum::Camera) {
    MediaDeviceSet audios;
    LOG("EnumerateRawDevices: Getting audio sources with %s backend for "
        "groupId correlation",
        videoBackend == fakeBackend ? "fake" : "real");
    // We need to correlate cameras with audio groupIds. We use the backend of
    // the camera to always do correlation on devices in the same scope. If we
    // don't do this, video-only getUserMedia will not apply groupId constraints
    // to the same set of groupIds as gets returned by enumerateDevices.
    if (micsOfVideoBackend.isSome()) {
      // Microphones from the same backend used for the cameras have
      // already been enumerated. Avoid doing it again.
      MOZ_ASSERT(aParams.mVideo->mForcedMicrophoneName ==
                 aParams.mAudio->mForcedDeviceName);
      audios.AppendElements(micsOfVideoBackend.extract());
    } else {
      GetMediaDevices(videoBackend, MediaSourceEnum::Microphone, audios,
                      aParams.mVideo->mForcedMicrophoneName.get());
    }
    if (videoBackend == realBackend) {
      // When using the real backend for video, there could also be
      // speakers to correlate with. There are no fake speakers.
      if (speakers.isSome()) {
        // Speakers have already been enumerated. Avoid doing it again.
        audios.AppendElements(speakers.extract());
      } else {
        realBackend->EnumerateDevices(MediaSourceEnum::Other,
                                      MediaSinkEnum::Speaker, &audios);
      }
    }
    GuessVideoDeviceGroupIDs(*devices, audios);
  }

  return devices;
}

RefPtr<ConstDeviceSetPromise> MediaManager::GetPhysicalDevices() {
  MOZ_ASSERT(NS_IsMainThread());
  if (mPhysicalDevices) {
    return ConstDeviceSetPromise::CreateAndResolve(mPhysicalDevices, __func__);
  }
  if (mPendingDevicesPromises) {
    // Enumeration is already in progress.
    return mPendingDevicesPromises->AppendElement()->Ensure(__func__);
  }
  mPendingDevicesPromises =
      new Refcountable<nsTArray<MozPromiseHolder<ConstDeviceSetPromise>>>;
  MaybeRequestPermissionAndEnumerateRawDevices(
      CreateEnumerationParams(MediaSourceEnum::Camera,
                              MediaSourceEnum::Microphone,
                              EnumerationFlag::EnumerateAudioOutputs))
      ->Then(
          GetCurrentSerialEventTarget(), __func__,
          [self = RefPtr(this), this, promises = mPendingDevicesPromises](
              RefPtr<MediaDeviceSetRefCnt> aDevices) mutable {
            for (auto& promiseHolder : *promises) {
              promiseHolder.Resolve(aDevices, __func__);
            }
            // mPendingDevicesPromises may have changed if devices have changed.
            if (promises == mPendingDevicesPromises) {
              mPendingDevicesPromises = nullptr;
              mPhysicalDevices = std::move(aDevices);
            }
          },
          [](RefPtr<MediaMgrError>&& reason) {
            MOZ_ASSERT_UNREACHABLE(
                "MaybeRequestPermissionAndEnumerateRawDevices does not reject");
          });

  return mPendingDevicesPromises->AppendElement()->Ensure(__func__);
}

MediaManager::MediaManager(already_AddRefed<TaskQueue> aMediaThread)
    : mMediaThread(aMediaThread), mBackend(nullptr) {
  mPrefs.mFreq = 1000;  // 1KHz test tone
  mPrefs.mWidth = 0;    // adaptive default
  mPrefs.mHeight = 0;   // adaptive default
  mPrefs.mResizeModeEnabled = false;
  mPrefs.mResizeMode = VideoResizeModeEnum::None;
  mPrefs.mFPS = MediaEnginePrefs::DEFAULT_VIDEO_FPS;
  mPrefs.mUsePlatformProcessing = false;
  mPrefs.mAecOn = false;
  mPrefs.mAgcOn = false;
  mPrefs.mHPFOn = false;
  mPrefs.mNoiseOn = false;
  mPrefs.mTransientOn = false;
  mPrefs.mAgc2Forced = false;
  mPrefs.mExpectDrift = -1;  // auto
#ifdef MOZ_WEBRTC
  mPrefs.mAgc =
      webrtc::AudioProcessing::Config::GainController1::Mode::kAdaptiveDigital;
  mPrefs.mNoise =
      webrtc::AudioProcessing::Config::NoiseSuppression::Level::kModerate;
#else
  mPrefs.mAgc = 0;
  mPrefs.mNoise = 0;
#endif
  mPrefs.mChannels = 0;  // max channels default
  nsresult rv;
  nsCOMPtr<nsIPrefService> prefs =
      do_GetService("@mozilla.org/preferences-service;1", &rv);
  if (NS_SUCCEEDED(rv)) {
    nsCOMPtr<nsIPrefBranch> branch = do_QueryInterface(prefs);
    if (branch) {
      GetPrefs(branch, nullptr);
    }
  }
}

NS_IMPL_ISUPPORTS(MediaManager, nsIMediaManagerService, nsIMemoryReporter,
                  nsIObserver)

/* static */
StaticRefPtr<MediaManager> MediaManager::sSingleton;

#ifdef DEBUG
/* static */
bool MediaManager::IsInMediaThread() {
  return sSingleton && sSingleton->mMediaThread->IsOnCurrentThread();
}
#endif

template <typename Function>
static void ForeachObservedPref(const Function& aFunction) {
  aFunction("media.navigator.video.default_width"_ns);
  aFunction("media.navigator.video.default_height"_ns);
  aFunction("media.navigator.video.default_fps"_ns);
  aFunction("media.navigator.audio.fake_frequency"_ns);
  aFunction("media.audio_loopback_dev"_ns);
  aFunction("media.video_loopback_dev"_ns);
  aFunction("media.getusermedia.fake-camera-name"_ns);
#ifdef MOZ_WEBRTC
  aFunction("media.navigator.video.resize_mode.enabled"_ns);
  aFunction("media.navigator.video.default_resize_mode"_ns);
  aFunction("media.getusermedia.audio.processing.aec.enabled"_ns);
  aFunction("media.getusermedia.audio.processing.aec"_ns);
  aFunction("media.getusermedia.audio.processing.agc.enabled"_ns);
  aFunction("media.getusermedia.audio.processing.agc"_ns);
  aFunction("media.getusermedia.audio.processing.hpf.enabled"_ns);
  aFunction("media.getusermedia.audio.processing.noise.enabled"_ns);
  aFunction("media.getusermedia.audio.processing.noise"_ns);
  aFunction("media.getusermedia.audio.max_channels"_ns);
  aFunction("media.navigator.streams.fake"_ns);
#endif
}

// NOTE: never NS_DispatchAndSpinEventLoopUntilComplete to the MediaManager
// thread from the MainThread, as we NS_DispatchAndSpinEventLoopUntilComplete to
// MainThread from MediaManager thread.

// Guaranteed never to return nullptr.
/* static */
MediaManager* MediaManager::Get() {
  MOZ_ASSERT(NS_IsMainThread());

  if (!sSingleton) {
    static int timesCreated = 0;
    timesCreated++;
    MOZ_RELEASE_ASSERT(timesCreated == 1);

    constexpr bool kSupportsTailDispatch = false;
    RefPtr<TaskQueue> mediaThread =
#ifdef MOZ_WEBRTC
        CreateWebrtcTaskQueueWrapper(
            GetMediaThreadPool(MediaThreadType::SUPERVISOR), "MediaManager"_ns,
            kSupportsTailDispatch);
#else
        TaskQueue::Create(GetMediaThreadPool(MediaThreadType::SUPERVISOR),
                          "MediaManager", kSupportsTailDispatch);
#endif
    LOG("New Media thread for gum");

    sSingleton = new MediaManager(mediaThread.forget());

    nsCOMPtr<nsIObserverService> obs = services::GetObserverService();
    if (obs) {
      obs->AddObserver(sSingleton, "last-pb-context-exited", false);
      obs->AddObserver(sSingleton, "getUserMedia:got-device-permission", false);
      obs->AddObserver(sSingleton, "getUserMedia:privileged:allow", false);
      obs->AddObserver(sSingleton, "getUserMedia:response:allow", false);
      obs->AddObserver(sSingleton, "getUserMedia:response:deny", false);
      obs->AddObserver(sSingleton, "getUserMedia:response:noOSPermission",
                       false);
      obs->AddObserver(sSingleton, "getUserMedia:revoke", false);
      obs->AddObserver(sSingleton, "getUserMedia:muteVideo", false);
      obs->AddObserver(sSingleton, "getUserMedia:unmuteVideo", false);
      obs->AddObserver(sSingleton, "getUserMedia:muteAudio", false);
      obs->AddObserver(sSingleton, "getUserMedia:unmuteAudio", false);
      obs->AddObserver(sSingleton, "application-background", false);
      obs->AddObserver(sSingleton, "application-foreground", false);
    }
    // else MediaManager won't work properly and will leak (see bug 837874)
    nsCOMPtr<nsIPrefBranch> prefs = do_GetService(NS_PREFSERVICE_CONTRACTID);
    if (prefs) {
      ForeachObservedPref([&](const nsLiteralCString& aPrefName) {
        prefs->AddObserver(aPrefName, sSingleton, false);
      });
    }
    RegisterStrongMemoryReporter(do_AddRef(sSingleton));

    // Prepare async shutdown

    class Blocker : public media::ShutdownBlocker {
     public:
      Blocker()
          : media::ShutdownBlocker(
                u"Media shutdown: blocking on media thread"_ns) {}

      NS_IMETHOD BlockShutdown(nsIAsyncShutdownClient*) override {
        MOZ_RELEASE_ASSERT(MediaManager::GetIfExists());
        MediaManager::GetIfExists()->Shutdown();
        return NS_OK;
      }
    };

    sSingleton->mShutdownBlocker = new Blocker();
    nsresult rv = media::MustGetShutdownBarrier()->AddBlocker(
        sSingleton->mShutdownBlocker, NS_LITERAL_STRING_FROM_CSTRING(__FILE__),
        __LINE__, u""_ns);
    MOZ_RELEASE_ASSERT(NS_SUCCEEDED(rv));
  }
  return sSingleton;
}

/* static */
MediaManager* MediaManager::GetIfExists() {
  MOZ_ASSERT(NS_IsMainThread() || IsInMediaThread());
  return sSingleton;
}

/* static */
already_AddRefed<MediaManager> MediaManager::GetInstance() {
  // so we can have non-refcounted getters
  RefPtr<MediaManager> service = MediaManager::Get();
  return service.forget();
}

media::Parent<media::NonE10s>* MediaManager::GetNonE10sParent() {
  if (!mNonE10sParent) {
    mNonE10sParent = new media::Parent<media::NonE10s>();
  }
  return mNonE10sParent;
}

/* static */
void MediaManager::Dispatch(already_AddRefed<Runnable> task) {
  MOZ_ASSERT(NS_IsMainThread());
  if (sHasMainThreadShutdown) {
    // Can't safely delete task here since it may have items with specific
    // thread-release requirements.
    // XXXkhuey well then who is supposed to delete it?! We don't signal
    // that we failed ...
    MOZ_CRASH();
    return;
  }
  NS_ASSERTION(Get(), "MediaManager singleton?");
  NS_ASSERTION(Get()->mMediaThread, "No thread yet");
  MOZ_ALWAYS_SUCCEEDS(Get()->mMediaThread->Dispatch(std::move(task)));
}

template <typename MozPromiseType, typename FunctionType>
/* static */
RefPtr<MozPromiseType> MediaManager::Dispatch(StaticString aName,
                                              FunctionType&& aFunction) {
  MozPromiseHolder<MozPromiseType> holder;
  RefPtr<MozPromiseType> promise = holder.Ensure(aName);
  MediaManager::Dispatch(NS_NewRunnableFunction(
      aName, [h = std::move(holder), func = std::forward<FunctionType>(
                                         aFunction)]() mutable { func(h); }));
  return promise;
}

/* static */
nsresult MediaManager::NotifyRecordingStatusChange(
    nsPIDOMWindowInner* aWindow) {
  NS_ENSURE_ARG(aWindow);

  nsCOMPtr<nsIObserverService> obs = services::GetObserverService();
  if (!obs) {
    NS_WARNING(
        "Could not get the Observer service for GetUserMedia recording "
        "notification.");
    return NS_ERROR_FAILURE;
  }

  auto props = MakeRefPtr<nsHashPropertyBag>();

  nsCString pageURL;
  nsCOMPtr<nsIURI> docURI = aWindow->GetDocumentURI();
  NS_ENSURE_TRUE(docURI, NS_ERROR_FAILURE);

  nsresult rv = docURI->GetSpec(pageURL);
  NS_ENSURE_SUCCESS(rv, rv);

  NS_ConvertUTF8toUTF16 requestURL(pageURL);

  props->SetPropertyAsAString(u"requestURL"_ns, requestURL);
  props->SetPropertyAsInterface(u"window"_ns, aWindow);

  obs->NotifyObservers(static_cast<nsIPropertyBag2*>(props),
                       "recording-device-events", nullptr);
  LOG("Sent recording-device-events for url '%s'", pageURL.get());

  return NS_OK;
}

void MediaManager::DeviceListChanged() {
  MOZ_ASSERT(NS_IsMainThread());
  if (sHasMainThreadShutdown) {
    return;
  }
  // Invalidate immediately to provide an up-to-date device list for future
  // enumerations on platforms with sane device-list-changed events.
  InvalidateDeviceCache();

  // Wait 200 ms, because
  // A) on some Windows machines, if we call EnumerateRawDevices immediately
  //    after receiving devicechange event, we'd get an outdated devices list.
  // B) Waiting helps coalesce multiple calls on us into one, which can happen
  //    if a device with both audio input and output is attached or removed.
  //    We want to react & fire a devicechange event only once in that case.

  // The wait is extended if another hardware device-list-changed notification
  // is received to provide the full 200ms for EnumerateRawDevices().
  if (mDeviceChangeTimer) {
    mDeviceChangeTimer->Cancel();
  } else {
    mDeviceChangeTimer = MakeRefPtr<MediaTimer<TimeStamp>>();
  }
  // However, if this would cause a delay of over 1000ms in handling the
  // oldest unhandled event, then respond now and set the timer to run
  // EnumerateRawDevices() again in 200ms.
  auto now = TimeStamp::NowLoRes();
  auto enumerateDelay = TimeDuration::FromMilliseconds(200);
  auto coalescenceLimit = TimeDuration::FromMilliseconds(1000) - enumerateDelay;
  if (!mUnhandledDeviceChangeTime) {
    mUnhandledDeviceChangeTime = now;
  } else if (now - mUnhandledDeviceChangeTime > coalescenceLimit) {
    HandleDeviceListChanged();
    mUnhandledDeviceChangeTime = now;
  }
  RefPtr<MediaManager> self = this;
  mDeviceChangeTimer->WaitFor(enumerateDelay, __func__)
      ->Then(
          GetCurrentSerialEventTarget(), __func__,
          [self, this] {
            // Invalidate again for the sake of platforms with inconsistent
            // timing between device-list-changed notification and enumeration.
            InvalidateDeviceCache();

            mUnhandledDeviceChangeTime = TimeStamp();
            HandleDeviceListChanged();
          },
          [] { /* Timer was canceled by us, or we're in shutdown. */ });
}

void MediaManager::InvalidateDeviceCache() {
  MOZ_ASSERT(NS_IsMainThread());

  mPhysicalDevices = nullptr;
  // Disconnect any in-progress enumeration, which may now be out of date,
  // from updating mPhysicalDevices or resolving future device request
  // promises.
  mPendingDevicesPromises = nullptr;
}

void MediaManager::HandleDeviceListChanged() {
  mDeviceListChangeEvent.Notify();

  GetPhysicalDevices()->Then(
      GetCurrentSerialEventTarget(), __func__,
      [self = RefPtr(this), this](RefPtr<const MediaDeviceSetRefCnt> aDevices) {
        if (!MediaManager::GetIfExists()) {
          return;
        }

        nsTHashSet<nsString> deviceIDs;
        for (const auto& device : *aDevices) {
          deviceIDs.Insert(device->mRawID);
        }
        // For any real removed cameras or microphones, notify their
        // listeners cleanly that the source has stopped, so JS knows and
        // usage indicators update.
        // First collect the listeners in an array to stop them after
        // iterating the hashtable. The StopRawID() method indirectly
        // modifies the mActiveWindows and would assert-crash if the
        // iterator were active while the table is being enumerated.
        const auto windowListeners = ToArray(mActiveWindows.Values());
        for (const RefPtr<GetUserMediaWindowListener>& l : windowListeners) {
          const auto activeDevices = l->GetDevices();
          for (const RefPtr<LocalMediaDevice>& device : *activeDevices) {
            if (device->IsFake()) {
              continue;
            }
            MediaSourceEnum mediaSource = device->GetMediaSource();
            if (mediaSource != MediaSourceEnum::Microphone &&
                mediaSource != MediaSourceEnum::Camera) {
              continue;
            }
            if (!deviceIDs.Contains(device->RawID())) {
              // Device has been removed
              l->StopRawID(device->RawID());
            }
          }
        }
      },
      [](RefPtr<MediaMgrError>&& reason) {
        MOZ_ASSERT_UNREACHABLE("EnumerateRawDevices does not reject");
      });
}

size_t MediaManager::AddTaskAndGetCount(uint64_t aWindowID,
                                        const nsAString& aCallID,
                                        RefPtr<GetUserMediaTask> aTask) {
  // Store the task w/callbacks.
  mActiveCallbacks.InsertOrUpdate(aCallID, std::move(aTask));

  // Add a WindowID cross-reference so OnNavigation can tear things down
  nsTArray<nsString>* const array = mCallIds.GetOrInsertNew(aWindowID);
  array->AppendElement(aCallID);

  return array->Length();
}

RefPtr<GetUserMediaTask> MediaManager::TakeGetUserMediaTask(
    const nsAString& aCallID) {
  RefPtr<GetUserMediaTask> task;
  mActiveCallbacks.Remove(aCallID, getter_AddRefs(task));
  if (!task) {
    return nullptr;
  }
  nsTArray<nsString>* array;
  mCallIds.Get(task->GetWindowID(), &array);
  MOZ_ASSERT(array);
  array->RemoveElement(aCallID);
  return task;
}

void MediaManager::NotifyAllowed(const nsString& aCallID,
                                 const LocalMediaDeviceSet& aDevices) {
  nsCOMPtr<nsIObserverService> obs = services::GetObserverService();
  nsCOMPtr<nsIMutableArray> devicesCopy = nsArray::Create();
  for (const auto& device : aDevices) {
    nsresult rv = devicesCopy->AppendElement(device);
    if (NS_WARN_IF(NS_FAILED(rv))) {
      obs->NotifyObservers(nullptr, "getUserMedia:response:deny",
                           aCallID.get());
      return;
    }
  }
  obs->NotifyObservers(devicesCopy, "getUserMedia:privileged:allow",
                       aCallID.get());
}

nsresult MediaManager::GenerateUUID(nsAString& aResult) {
  nsresult rv;
  nsCOMPtr<nsIUUIDGenerator> uuidgen =
      do_GetService("@mozilla.org/uuid-generator;1", &rv);
  NS_ENSURE_SUCCESS(rv, rv);

  // Generate a call ID.
  nsID id;
  rv = uuidgen->GenerateUUIDInPlace(&id);
  NS_ENSURE_SUCCESS(rv, rv);

  char buffer[NSID_LENGTH];
  id.ToProvidedString(buffer);
  aResult.Assign(NS_ConvertUTF8toUTF16(buffer));
  return NS_OK;
}

enum class GetUserMediaSecurityState {
  Other = 0,
  HTTPS = 1,
  File = 2,
  App = 3,
  Localhost = 4,
  Loop = 5,
  Privileged = 6
};

/**
 * This function is used in getUserMedia when privacy.resistFingerprinting is
 * true. Only mediaSource of audio/video constraint will be kept. On mobile
 * facing mode is also kept.
 */
static void ReduceConstraint(
    OwningBooleanOrMediaTrackConstraints& aConstraint) {
  // Not requesting stream.
  if (!MediaManager::IsOn(aConstraint)) {
    return;
  }

  // It looks like {audio: true}, do nothing.
  if (!aConstraint.IsMediaTrackConstraints()) {
    return;
  }

  // Keep mediaSource.
  Maybe<nsString> mediaSource;
  if (aConstraint.GetAsMediaTrackConstraints().mMediaSource.WasPassed()) {
    mediaSource =
        Some(aConstraint.GetAsMediaTrackConstraints().mMediaSource.Value());
  }

  Maybe<OwningStringOrStringSequenceOrConstrainDOMStringParameters> facingMode;
  if (aConstraint.GetAsMediaTrackConstraints().mFacingMode.WasPassed()) {
    facingMode =
        Some(aConstraint.GetAsMediaTrackConstraints().mFacingMode.Value());
  }

  aConstraint.Uninit();
  if (mediaSource) {
    aConstraint.SetAsMediaTrackConstraints().mMediaSource.Construct(
        *mediaSource);
  } else {
    (void)aConstraint.SetAsMediaTrackConstraints();
  }

#if defined(MOZ_WIDGET_ANDROID) || defined(MOZ_WIDGET_UIKIT)
  if (facingMode) {
    aConstraint.SetAsMediaTrackConstraints().mFacingMode.Construct(*facingMode);
  } else {
    (void)aConstraint.SetAsMediaTrackConstraints();
  }
#endif
}

/**
 * The entry point for this file. A call from MediaDevices::GetUserMedia
 * will end up here. MediaManager is a singleton that is responsible
 * for handling all incoming getUserMedia calls from every window.
 */
RefPtr<MediaManager::StreamPromise> MediaManager::GetUserMedia(
    nsPIDOMWindowInner* aWindow,
    const MediaStreamConstraints& aConstraintsPassedIn,
    CallerType aCallerType) {
  MOZ_ASSERT(NS_IsMainThread());
  MOZ_ASSERT(aWindow);
  uint64_t windowID = aWindow->WindowID();

  MediaStreamConstraints c(aConstraintsPassedIn);  // use a modifiable copy

  if (sHasMainThreadShutdown) {
    return StreamPromise::CreateAndReject(
        MakeRefPtr<MediaMgrError>(MediaMgrError::Name::AbortError,
                                  "In shutdown"),
        __func__);
  }

  // Determine permissions early (while we still have a stack).

  nsIURI* docURI = aWindow->GetDocumentURI();
  if (!docURI) {
    return StreamPromise::CreateAndReject(
        MakeRefPtr<MediaMgrError>(MediaMgrError::Name::AbortError), __func__);
  }
  bool isChrome = (aCallerType == CallerType::System);
  bool privileged =
      isChrome ||
      Preferences::GetBool("media.navigator.permission.disabled", false);
  bool isSecure = aWindow->IsSecureContext();
  bool isHandlingUserInput = UserActivation::IsHandlingUserInput();
  nsCString host;
  nsresult rv = docURI->GetHost(host);

  nsCOMPtr<nsIPrincipal> principal =
      nsGlobalWindowInner::Cast(aWindow)->GetPrincipal();
  if (NS_WARN_IF(!principal)) {
    return StreamPromise::CreateAndReject(
        MakeRefPtr<MediaMgrError>(MediaMgrError::Name::SecurityError),
        __func__);
  }

  Document* doc = aWindow->GetExtantDoc();
  if (NS_WARN_IF(!doc)) {
    return StreamPromise::CreateAndReject(
        MakeRefPtr<MediaMgrError>(MediaMgrError::Name::SecurityError),
        __func__);
  }

  // Disallow access to null principal pages and http pages (unless pref)
  if (principal->GetIsNullPrincipal() ||
      !(isSecure || StaticPrefs::media_getusermedia_insecure_enabled())) {
    return StreamPromise::CreateAndReject(
        MakeRefPtr<MediaMgrError>(MediaMgrError::Name::NotAllowedError),
        __func__);
  }

  // This principal needs to be sent to different threads and so via IPC.
  // For this reason it's better to convert it to PrincipalInfo right now.
  ipc::PrincipalInfo principalInfo;
  rv = PrincipalToPrincipalInfo(principal, &principalInfo);
  if (NS_WARN_IF(NS_FAILED(rv))) {
    return StreamPromise::CreateAndReject(
        MakeRefPtr<MediaMgrError>(MediaMgrError::Name::SecurityError),
        __func__);
  }

  const bool resistFingerprinting =
      !isChrome && doc->ShouldResistFingerprinting(RFPTarget::MediaDevices);
  if (resistFingerprinting) {
    ReduceConstraint(c.mVideo);
    ReduceConstraint(c.mAudio);
  }

  if (!Preferences::GetBool("media.navigator.video.enabled", true)) {
    c.mVideo.SetAsBoolean() = false;
  }

  MediaSourceEnum videoType = MediaSourceEnum::Other;  // none
  MediaSourceEnum audioType = MediaSourceEnum::Other;  // none

  if (c.mVideo.IsMediaTrackConstraints()) {
    auto& vc = c.mVideo.GetAsMediaTrackConstraints();
    if (!vc.mMediaSource.WasPassed()) {
      vc.mMediaSource.Construct().AssignASCII(
          dom::GetEnumString(MediaSourceEnum::Camera));
    }
    videoType = dom::StringToEnum<MediaSourceEnum>(vc.mMediaSource.Value())
                    .valueOr(MediaSourceEnum::Other);
    glean::webrtc::get_user_media_type.AccumulateSingleSample(
        (uint32_t)videoType);
    switch (videoType) {
      case MediaSourceEnum::Camera:
        break;

      case MediaSourceEnum::Browser:
        // If no window id is passed in then default to the caller's window.
        // Functional defaults are helpful in tests, but also a natural outcome
        // of the constraints API's limited semantics for requiring input.
        if (!vc.mBrowserWindow.WasPassed()) {
          nsPIDOMWindowOuter* outer = aWindow->GetOuterWindow();
          vc.mBrowserWindow.Construct(outer->WindowID());
        }
        [[fallthrough]];
      case MediaSourceEnum::Screen:
      case MediaSourceEnum::Window:
        // Deny screensharing request if support is disabled, or
        // the requesting document is not from a host on the whitelist.
        if (!Preferences::GetBool(
                ((videoType == MediaSourceEnum::Browser)
                     ? "media.getusermedia.browser.enabled"
                     : "media.getusermedia.screensharing.enabled"),
                false) ||
            (!privileged && !aWindow->IsSecureContext())) {
          return StreamPromise::CreateAndReject(
              MakeRefPtr<MediaMgrError>(MediaMgrError::Name::NotAllowedError),
              __func__);
        }
        break;

      case MediaSourceEnum::Microphone:
      case MediaSourceEnum::Other:
      default: {
        return StreamPromise::CreateAndReject(
            MakeRefPtr<MediaMgrError>(MediaMgrError::Name::OverconstrainedError,
                                      "", u"mediaSource"_ns),
            __func__);
      }
    }

    if (!privileged) {
      // Only allow privileged content to explicitly pick full-screen,
      // application or tabsharing, since these modes are still available for
      // testing. All others get "Window" (*) sharing.
      //
      // *) We overload "Window" with the new default getDisplayMedia spec-
      // mandated behavior of not influencing user-choice, which we currently
      // implement as a list containing BOTH windows AND screen(s).
      //
      // Notes on why we chose "Window" as the one to overload. Two reasons:
      //
      // 1. It's the closest logically & behaviorally (multi-choice, no default)
      // 2. Screen is still useful in tests (implicit default is entire screen)
      //
      // For UX reasons we don't want "Entire Screen" to be the first/default
      // choice (in our code first=default). It's a "scary" source that comes
      // with complicated warnings on-top that would be confusing as the first
      // thing people see, and also deserves to be listed as last resort for
      // privacy reasons.

      if (videoType == MediaSourceEnum::Screen ||
          videoType == MediaSourceEnum::Browser) {
        videoType = MediaSourceEnum::Window;
        vc.mMediaSource.Value().AssignASCII(dom::GetEnumString(videoType));
      }
      // only allow privileged content to set the window id
      if (vc.mBrowserWindow.WasPassed()) {
        vc.mBrowserWindow.Value() = -1;
      }
      if (vc.mAdvanced.WasPassed()) {
        for (MediaTrackConstraintSet& cs : vc.mAdvanced.Value()) {
          if (cs.mBrowserWindow.WasPassed()) {
            cs.mBrowserWindow.Value() = -1;
          }
        }
      }
    }
  } else if (IsOn(c.mVideo)) {
    videoType = MediaSourceEnum::Camera;
    glean::webrtc::get_user_media_type.AccumulateSingleSample(
        (uint32_t)videoType);
  }

  if (c.mAudio.IsMediaTrackConstraints()) {
    auto& ac = c.mAudio.GetAsMediaTrackConstraints();
    if (!ac.mMediaSource.WasPassed()) {
      ac.mMediaSource.Construct(NS_ConvertASCIItoUTF16(
          dom::GetEnumString(MediaSourceEnum::Microphone)));
    }
    audioType = dom::StringToEnum<MediaSourceEnum>(ac.mMediaSource.Value())
                    .valueOr(MediaSourceEnum::Other);
    glean::webrtc::get_user_media_type.AccumulateSingleSample(
        (uint32_t)audioType);

    switch (audioType) {
      case MediaSourceEnum::Microphone:
        break;

      case MediaSourceEnum::AudioCapture:
        // Only enable AudioCapture if the pref is enabled. If it's not, we can
        // deny right away.
        if (!Preferences::GetBool("media.getusermedia.audio.capture.enabled")) {
          return StreamPromise::CreateAndReject(
              MakeRefPtr<MediaMgrError>(MediaMgrError::Name::NotAllowedError),
              __func__);
        }
        break;

      case MediaSourceEnum::Other:
      default: {
        return StreamPromise::CreateAndReject(
            MakeRefPtr<MediaMgrError>(MediaMgrError::Name::OverconstrainedError,
                                      "", u"mediaSource"_ns),
            __func__);
      }
    }
  } else if (IsOn(c.mAudio)) {
    audioType = MediaSourceEnum::Microphone;
    glean::webrtc::get_user_media_type.AccumulateSingleSample(
        (uint32_t)audioType);
  }

  // Create a window listener if it doesn't already exist.
  RefPtr<GetUserMediaWindowListener> windowListener =
      GetOrMakeWindowListener(aWindow);
  MOZ_ASSERT(windowListener);
  // Create an inactive DeviceListener to act as a placeholder, so the
  // window listener doesn't clean itself up until we're done.
  auto placeholderListener = MakeRefPtr<DeviceListener>();
  windowListener->Register(placeholderListener);

  {  // Check Permissions Policy.  Reject if a requested feature is disabled.
    bool disabled = !IsOn(c.mAudio) && !IsOn(c.mVideo);
    if (IsOn(c.mAudio)) {
      if (audioType == MediaSourceEnum::Microphone) {
        if (Preferences::GetBool("media.getusermedia.microphone.deny", false) ||
            !FeaturePolicyUtils::IsFeatureAllowed(doc, u"microphone"_ns)) {
          disabled = true;
        }
      } else if (!FeaturePolicyUtils::IsFeatureAllowed(doc,
                                                       u"display-capture"_ns)) {
        disabled = true;
      }
    }
    if (IsOn(c.mVideo)) {
      if (videoType == MediaSourceEnum::Camera) {
        if (Preferences::GetBool("media.getusermedia.camera.deny", false) ||
            !FeaturePolicyUtils::IsFeatureAllowed(doc, u"camera"_ns)) {
          disabled = true;
        }
      } else if (!FeaturePolicyUtils::IsFeatureAllowed(doc,
                                                       u"display-capture"_ns)) {
        disabled = true;
      }
    }

    if (disabled) {
      placeholderListener->Stop();
      return StreamPromise::CreateAndReject(
          MakeRefPtr<MediaMgrError>(MediaMgrError::Name::NotAllowedError),
          __func__);
    }
  }

  // Get list of all devices, with origin-specific device ids.

  MediaEnginePrefs prefs = mPrefs;

  nsString callID;
  rv = GenerateUUID(callID);
  MOZ_RELEASE_ASSERT(NS_SUCCEEDED(rv));

  bool hasVideo = videoType != MediaSourceEnum::Other;
  bool hasAudio = audioType != MediaSourceEnum::Other;

  // Handle fake requests from content. For gUM we don't consider resist
  // fingerprinting as users should be prompted anyway.
  bool forceFakes = c.mFake.WasPassed() && c.mFake.Value();
  // fake:true is effective only for microphone and camera devices, so
  // permission must be requested for screen capture even if fake:true is set.
  bool hasOnlyForcedFakes =
      forceFakes && (!hasVideo || videoType == MediaSourceEnum::Camera) &&
      (!hasAudio || audioType == MediaSourceEnum::Microphone);
  bool askPermission =
      (!privileged ||
       Preferences::GetBool("media.navigator.permission.force")) &&
      (!hasOnlyForcedFakes ||
       Preferences::GetBool("media.navigator.permission.fake"));

  LOG("%s: Preparing to enumerate devices. windowId=%" PRIu64
      ", videoType=%" PRIu8 ", audioType=%" PRIu8
      ", forceFakes=%s, askPermission=%s",
      __func__, windowID, static_cast<uint8_t>(videoType),
      static_cast<uint8_t>(audioType), forceFakes ? "true" : "false",
      askPermission ? "true" : "false");

  EnumerationFlags flags = EnumerationFlag::AllowPermissionRequest;
  if (forceFakes) {
    flags += EnumerationFlag::ForceFakes;
  }
  RefPtr<MediaManager> self = this;
  return EnumerateDevicesImpl(
             aWindow, CreateEnumerationParams(videoType, audioType, flags))
      ->Then(
          GetCurrentSerialEventTarget(), __func__,
          [self, windowID, c, windowListener,
           aCallerType](RefPtr<LocalMediaDeviceSetRefCnt> aDevices) {
            LOG("GetUserMedia: post enumeration promise success callback "
                "starting");
            // Ensure that our windowID is still good.
            RefPtr<nsPIDOMWindowInner> window =
                nsGlobalWindowInner::GetInnerWindowWithId(windowID);
            if (!window || !self->IsWindowListenerStillActive(windowListener)) {
              LOG("GetUserMedia: bad window (%" PRIu64
                  ") in post enumeration success callback!",
                  windowID);
              return LocalDeviceSetPromise::CreateAndReject(
                  MakeRefPtr<MediaMgrError>(MediaMgrError::Name::AbortError),
                  __func__);
            }
            // Apply any constraints. This modifies the passed-in list.
            return self->SelectSettings(c, aCallerType, std::move(aDevices));
          },
          [](RefPtr<MediaMgrError>&& aError) {
            LOG("GetUserMedia: post enumeration EnumerateDevicesImpl "
                "failure callback called!");
            return LocalDeviceSetPromise::CreateAndReject(std::move(aError),
                                                          __func__);
          })
      ->Then(
          GetCurrentSerialEventTarget(), __func__,
          [self, windowID, c, windowListener, placeholderListener, hasAudio,
           hasVideo, askPermission, prefs, isSecure, isHandlingUserInput,
           callID, principalInfo, aCallerType, resistFingerprinting, audioType,
           forceFakes](RefPtr<LocalMediaDeviceSetRefCnt> aDevices) mutable {
            LOG("GetUserMedia: starting post enumeration promise2 success "
                "callback!");

            // Ensure that the window is still good.
            RefPtr<nsPIDOMWindowInner> window =
                nsGlobalWindowInner::GetInnerWindowWithId(windowID);
            if (!window || !self->IsWindowListenerStillActive(windowListener)) {
              LOG("GetUserMedia: bad window (%" PRIu64
                  ") in post enumeration success callback 2!",
                  windowID);
              placeholderListener->Stop();
              return StreamPromise::CreateAndReject(
                  MakeRefPtr<MediaMgrError>(MediaMgrError::Name::AbortError),
                  __func__);
            }
            if (!aDevices->Length()) {
              LOG("GetUserMedia: no devices found in post enumeration promise2 "
                  "success callback! Calling error handler!");
              placeholderListener->Stop();
              // When privacy.resistFingerprinting = true, no
              // available device implies content script is requesting
              // a fake device, so report NotAllowedError.
              auto error = resistFingerprinting
                               ? MediaMgrError::Name::NotAllowedError
                               : MediaMgrError::Name::NotFoundError;
              return StreamPromise::CreateAndReject(
                  MakeRefPtr<MediaMgrError>(error), __func__);
            }

            // Time to start devices. Create the necessary device listeners and
            // remove the placeholder.
            RefPtr<DeviceListener> audioListener;
            RefPtr<DeviceListener> videoListener;
            if (hasAudio) {
              audioListener = MakeRefPtr<DeviceListener>();
              windowListener->Register(audioListener);
            }
            if (hasVideo) {
              videoListener = MakeRefPtr<DeviceListener>();
              windowListener->Register(videoListener);
            }
            placeholderListener->Stop();

            bool focusSource = mozilla::Preferences::GetBool(
                "media.getusermedia.window.focus_source.enabled", true);

            // Incremental hack to compile. To be replaced by deeper
            // refactoring. MediaManager allows
            // "neither-resolve-nor-reject" semantics, so we cannot
            // use MozPromiseHolder here.
            MozPromiseHolder<StreamPromise> holder;
            RefPtr<StreamPromise> p = holder.Ensure(__func__);

            // Pass callbacks and listeners along to GetUserMediaStreamTask.
            auto task = MakeRefPtr<GetUserMediaStreamTask>(
                c, std::move(holder), windowID, std::move(windowListener),
                std::move(audioListener), std::move(videoListener), prefs,
                principalInfo, aCallerType, focusSource);

            // It is time to ask for user permission, prime voice processing
            // now. Use a local lambda to enable a guard pattern.
            [&] {
              if (forceFakes) {
                return;
              }

              if (audioType != MediaSourceEnum::Microphone) {
                return;
              }

              if (!StaticPrefs::
                      media_getusermedia_microphone_voice_stream_priming_enabled() ||
                  !StaticPrefs::
                      media_getusermedia_microphone_prefer_voice_stream_with_processing_enabled()) {
                return;
              }

              if (const auto fc = FlattenedConstraints(
                      NormalizedConstraints(GetInvariant(c.mAudio)));
                  !fc.mEchoCancellation.Get(prefs.mAecOn) &&
                  !fc.mAutoGainControl.Get(prefs.mAgcOn && prefs.mAecOn) &&
                  !fc.mNoiseSuppression.Get(prefs.mNoiseOn && prefs.mAecOn)) {
                return;
              }

              if (GetPersistentPermissions(windowID)
                      .map([](auto&& aState) {
                        return aState.mMicrophonePermission ==
                               PersistentPermissionState::Deny;
                      })
                      .unwrapOr(true)) {
                return;
              }

              task->PrimeVoiceProcessing();
            }();

            size_t taskCount =
                self->AddTaskAndGetCount(windowID, callID, std::move(task));

            if (!askPermission) {
              self->NotifyAllowed(callID, *aDevices);
            } else {
              auto req = MakeRefPtr<GetUserMediaRequest>(
                  window, callID, std::move(aDevices), c, isSecure,
                  isHandlingUserInput);
              if (!Preferences::GetBool("media.navigator.permission.force") &&
                  taskCount > 1) {
                // there is at least 1 pending gUM request
                // For the scarySources test case, always send the
                // request
                self->mPendingGUMRequest.AppendElement(req.forget());
              } else {
                nsCOMPtr<nsIObserverService> obs =
                    services::GetObserverService();
                obs->NotifyObservers(req, "getUserMedia:request", nullptr);
              }
            }
#ifdef MOZ_WEBRTC
            self->mLogHandle = EnsureWebrtcLogging();
#endif
            return p;
          },
          [placeholderListener](RefPtr<MediaMgrError>&& aError) {
            LOG("GetUserMedia: post enumeration SelectSettings failure "
                "callback called!");
            placeholderListener->Stop();
            return StreamPromise::CreateAndReject(std::move(aError), __func__);
          });
};

RefPtr<LocalDeviceSetPromise> MediaManager::AnonymizeDevices(
    nsPIDOMWindowInner* aWindow, RefPtr<const MediaDeviceSetRefCnt> aDevices) {
  // Get an origin-key (for either regular or private browsing).
  MOZ_ASSERT(NS_IsMainThread());
  uint64_t windowId = aWindow->WindowID();
  nsCOMPtr<nsIPrincipal> principal =
      nsGlobalWindowInner::Cast(aWindow)->GetPrincipal();
  MOZ_ASSERT(principal);
  ipc::PrincipalInfo principalInfo;
  nsresult rv = PrincipalToPrincipalInfo(principal, &principalInfo);
  if (NS_WARN_IF(NS_FAILED(rv))) {
    return LocalDeviceSetPromise::CreateAndReject(
        MakeRefPtr<MediaMgrError>(MediaMgrError::Name::NotAllowedError),
        __func__);
  }
  bool resistFingerprinting =
      aWindow->AsGlobal()->ShouldResistFingerprinting(RFPTarget::MediaDevices);
  bool persist =
      IsActivelyCapturingOrHasAPermission(windowId) && !resistFingerprinting;
  return media::GetPrincipalKey(principalInfo, persist)
      ->Then(
          GetMainThreadSerialEventTarget(), __func__,
          [rawDevices = std::move(aDevices), windowId,
           resistFingerprinting](const nsCString& aOriginKey) {
            MOZ_ASSERT(!aOriginKey.IsEmpty());
            RefPtr anonymized = new LocalMediaDeviceSetRefCnt();
            for (const RefPtr<MediaDevice>& device : *rawDevices) {
              nsString name = device->mRawName;
              if (name.Find(u"AirPods"_ns) != -1) {
                name = u"AirPods"_ns;
              }

              nsString id = device->mRawID;
              if (resistFingerprinting) {
                nsRFPService::GetMediaDeviceName(name, device->mKind);
                id = name;
                id.AppendInt(windowId);
              }
              // An empty id represents a virtual default device, for which
              // the exposed deviceId is the empty string.
              if (!id.IsEmpty()) {
                nsContentUtils::AnonymizeId(id, aOriginKey);
              }

              nsString groupId = device->mRawGroupID;
              if (resistFingerprinting) {
                nsRFPService::GetMediaDeviceGroup(groupId, device->mKind);
              }
              // Use window id to salt group id in order to make it session
              // based as required by the spec. This does not provide unique
              // group ids through out a browser restart. However, this is not
              // against the spec.  Furthermore, since device ids are the same
              // after a browser restart the fingerprint is not bigger.
              groupId.AppendInt(windowId);
              nsContentUtils::AnonymizeId(groupId, aOriginKey);
              anonymized->EmplaceBack(
                  new LocalMediaDevice(device, id, groupId, name));
            }
            return LocalDeviceSetPromise::CreateAndResolve(anonymized,
                                                           __func__);
          },
          [](nsresult rs) {
            NS_WARNING("AnonymizeDevices failed to get Principal Key");
            return LocalDeviceSetPromise::CreateAndReject(
                MakeRefPtr<MediaMgrError>(MediaMgrError::Name::AbortError),
                __func__);
          });
}

RefPtr<LocalDeviceSetPromise> MediaManager::EnumerateDevicesImpl(
    nsPIDOMWindowInner* aWindow, EnumerationParams aParams) {
  MOZ_ASSERT(NS_IsMainThread());

  uint64_t windowId = aWindow->WindowID();
  LOG("%s: windowId=%" PRIu64 ", aVideoInputType=%" PRIu8
      ", aAudioInputType=%" PRIu8,
      __func__, windowId, static_cast<uint8_t>(aParams.VideoInputType()),
      static_cast<uint8_t>(aParams.AudioInputType()));

  // To get a device list anonymized for a particular origin, we must:
  // 1. Get the raw devices list
  // 2. Anonymize the raw list with an origin-key.

  // Add the window id here to check for that and abort silently if no longer
  // exists.
  RefPtr<GetUserMediaWindowListener> windowListener =
      GetOrMakeWindowListener(aWindow);
  MOZ_ASSERT(windowListener);
  // Create an inactive DeviceListener to act as a placeholder, so the
  // window listener doesn't clean itself up until we're done.
  auto placeholderListener = MakeRefPtr<DeviceListener>();
  windowListener->Register(placeholderListener);

  return MaybeRequestPermissionAndEnumerateRawDevices(std::move(aParams))
      ->Then(
          GetMainThreadSerialEventTarget(), __func__,
          [self = RefPtr(this), this, window = nsCOMPtr(aWindow),
           placeholderListener](RefPtr<MediaDeviceSetRefCnt> aDevices) mutable {
            // Only run if window is still on our active list.
            MediaManager* mgr = MediaManager::GetIfExists();
            if (!mgr || placeholderListener->Stopped()) {
              // The listener has already been removed if the window is no
              // longer active.
              return LocalDeviceSetPromise::CreateAndReject(
                  MakeRefPtr<MediaMgrError>(MediaMgrError::Name::AbortError),
                  __func__);
            }
            MOZ_ASSERT(mgr->IsWindowStillActive(window->WindowID()));
            placeholderListener->Stop();
            return AnonymizeDevices(window, aDevices);
          },
          [placeholderListener](RefPtr<MediaMgrError>&& aError) {
            // EnumerateDevicesImpl may fail if a new doc has been set, in which
            // case the OnNavigation() method should have removed all previous
            // active listeners, or if a platform device access request was not
            // granted.
            placeholderListener->Stop();
            return LocalDeviceSetPromise::CreateAndReject(std::move(aError),
                                                          __func__);
          });
}

RefPtr<LocalDevicePromise> MediaManager::SelectAudioOutput(
    nsPIDOMWindowInner* aWindow, const dom::AudioOutputOptions& aOptions,
    CallerType aCallerType) {
  bool isHandlingUserInput = UserActivation::IsHandlingUserInput();
  nsCOMPtr<nsIPrincipal> principal =
      nsGlobalWindowInner::Cast(aWindow)->GetPrincipal();
  if (!FeaturePolicyUtils::IsFeatureAllowed(aWindow->GetExtantDoc(),
                                            u"speaker-selection"_ns)) {
    return LocalDevicePromise::CreateAndReject(
        MakeRefPtr<MediaMgrError>(
            MediaMgrError::Name::NotAllowedError,
            "Document's Permissions Policy does not allow selectAudioOutput()"),
        __func__);
  }
  if (NS_WARN_IF(!principal)) {
    return LocalDevicePromise::CreateAndReject(
        MakeRefPtr<MediaMgrError>(MediaMgrError::Name::SecurityError),
        __func__);
  }
  // Disallow access to null principal.
  if (principal->GetIsNullPrincipal()) {
    return LocalDevicePromise::CreateAndReject(
        MakeRefPtr<MediaMgrError>(MediaMgrError::Name::NotAllowedError),
        __func__);
  }
  ipc::PrincipalInfo principalInfo;
  nsresult rv = PrincipalToPrincipalInfo(principal, &principalInfo);
  if (NS_WARN_IF(NS_FAILED(rv))) {
    return LocalDevicePromise::CreateAndReject(
        MakeRefPtr<MediaMgrError>(MediaMgrError::Name::SecurityError),
        __func__);
  }
  uint64_t windowID = aWindow->WindowID();
  const bool resistFingerprinting =
      aWindow->AsGlobal()->ShouldResistFingerprinting(aCallerType,
                                                      RFPTarget::MediaDevices);
  return EnumerateDevicesImpl(
             aWindow, CreateEnumerationParams(
                          MediaSourceEnum::Other, MediaSourceEnum::Other,
                          {EnumerationFlag::EnumerateAudioOutputs,
                           EnumerationFlag::AllowPermissionRequest}))
      ->Then(
          GetCurrentSerialEventTarget(), __func__,
          [self = RefPtr<MediaManager>(this), windowID, aOptions, aCallerType,
           resistFingerprinting, isHandlingUserInput,
           principalInfo](RefPtr<LocalMediaDeviceSetRefCnt> aDevices) mutable {
            // Ensure that the window is still good.
            RefPtr<nsPIDOMWindowInner> window =
                nsGlobalWindowInner::GetInnerWindowWithId(windowID);
            if (!window) {
              LOG("SelectAudioOutput: bad window (%" PRIu64
                  ") in post enumeration success callback!",
                  windowID);
              return LocalDevicePromise::CreateAndReject(
                  MakeRefPtr<MediaMgrError>(MediaMgrError::Name::AbortError),
                  __func__);
            }
            if (aDevices->IsEmpty()) {
              LOG("SelectAudioOutput: no devices found");
              auto error = resistFingerprinting
                               ? MediaMgrError::Name::NotAllowedError
                               : MediaMgrError::Name::NotFoundError;
              return LocalDevicePromise::CreateAndReject(
                  MakeRefPtr<MediaMgrError>(error), __func__);
            }
            MozPromiseHolder<LocalDevicePromise> holder;
            RefPtr<LocalDevicePromise> p = holder.Ensure(__func__);
            auto task = MakeRefPtr<SelectAudioOutputTask>(
                std::move(holder), windowID, aCallerType, principalInfo);
            nsString callID;
            nsresult rv = GenerateUUID(callID);
            MOZ_RELEASE_ASSERT(NS_SUCCEEDED(rv));
            size_t taskCount =
                self->AddTaskAndGetCount(windowID, callID, std::move(task));
            bool askPermission =
                !Preferences::GetBool("media.navigator.permission.disabled") ||
                Preferences::GetBool("media.navigator.permission.force");
            if (!askPermission) {
              self->NotifyAllowed(callID, *aDevices);
            } else {
              MOZ_ASSERT(window->IsSecureContext());
              auto req = MakeRefPtr<GetUserMediaRequest>(
                  window, callID, std::move(aDevices), aOptions, true,
                  isHandlingUserInput);
              if (taskCount > 1) {
                // there is at least 1 pending gUM request
                self->mPendingGUMRequest.AppendElement(req.forget());
              } else {
                nsCOMPtr<nsIObserverService> obs =
                    services::GetObserverService();
                obs->NotifyObservers(req, "getUserMedia:request", nullptr);
              }
            }
            return p;
          },
          [](RefPtr<MediaMgrError> aError) {
            LOG("SelectAudioOutput: EnumerateDevicesImpl "
                "failure callback called!");
            return LocalDevicePromise::CreateAndReject(std::move(aError),
                                                       __func__);
          });
}

MediaEngine* MediaManager::GetBackend() {
  MOZ_ASSERT(MediaManager::IsInMediaThread());
  // Plugin backends as appropriate. The default engine also currently
  // includes picture support for Android.
  // This IS called off main-thread.
  if (!mBackend) {
#if defined(MOZ_WEBRTC)
    mBackend = new MediaEngineWebRTC();
#else
    mBackend = new MediaEngineFake();
#endif
    mDeviceListChangeListener = mBackend->DeviceListChangeEvent().Connect(
        AbstractThread::MainThread(), this, &MediaManager::DeviceListChanged);
  }
  return mBackend;
}

void MediaManager::OnNavigation(uint64_t aWindowID) {
  MOZ_ASSERT(NS_IsMainThread());
  LOG("OnNavigation for %" PRIu64, aWindowID);

  // Stop the streams for this window. The runnables check this value before
  // making a call to content.

  nsTArray<nsString>* callIDs;
  if (mCallIds.Get(aWindowID, &callIDs)) {
    for (auto& callID : *callIDs) {
      mActiveCallbacks.Remove(callID);
      for (auto& request : mPendingGUMRequest.Clone()) {
        nsString id;
        request->GetCallID(id);
        if (id == callID) {
          mPendingGUMRequest.RemoveElement(request);
        }
      }
    }
    mCallIds.Remove(aWindowID);
  }

  if (RefPtr<GetUserMediaWindowListener> listener =
          GetWindowListener(aWindowID)) {
    listener->RemoveAll();
  }
  MOZ_ASSERT(!GetWindowListener(aWindowID));
}

void MediaManager::OnCameraMute(bool aMute) {
  MOZ_ASSERT(NS_IsMainThread());
  LOG("OnCameraMute for all windows");
  mCamerasMuted = aMute;
  // This is safe since we're on main-thread, and the windowlist can only
  // be added to from the main-thread
  for (const auto& window :
       ToTArray<AutoTArray<RefPtr<GetUserMediaWindowListener>, 2>>(
           mActiveWindows.Values())) {
    window->MuteOrUnmuteCameras(aMute);
  }
}

void MediaManager::OnMicrophoneMute(bool aMute) {
  MOZ_ASSERT(NS_IsMainThread());
  LOG("OnMicrophoneMute for all windows");
  mMicrophonesMuted = aMute;
  // This is safe since we're on main-thread, and the windowlist can only
  // be added to from the main-thread
  for (const auto& window :
       ToTArray<AutoTArray<RefPtr<GetUserMediaWindowListener>, 2>>(
           mActiveWindows.Values())) {
    window->MuteOrUnmuteMicrophones(aMute);
  }
}

RefPtr<GetUserMediaWindowListener> MediaManager::GetOrMakeWindowListener(
    nsPIDOMWindowInner* aWindow) {
  Document* doc = aWindow->GetExtantDoc();
  if (!doc) {
    // The window has been destroyed. Destroyed windows don't have listeners.
    return nullptr;
  }
  nsIPrincipal* principal = doc->NodePrincipal();
  uint64_t windowId = aWindow->WindowID();
  RefPtr<GetUserMediaWindowListener> windowListener =
      GetWindowListener(windowId);
  if (windowListener) {
    MOZ_ASSERT(PrincipalHandleMatches(windowListener->GetPrincipalHandle(),
                                      principal));
  } else {
    windowListener = new GetUserMediaWindowListener(
        windowId, MakePrincipalHandle(principal));
    AddWindowID(windowId, windowListener);
  }
  return windowListener;
}

void MediaManager::AddWindowID(uint64_t aWindowId,
                               RefPtr<GetUserMediaWindowListener> aListener) {
  MOZ_ASSERT(NS_IsMainThread());
  // Store the WindowID in a hash table and mark as active. The entry is removed
  // when this window is closed or navigated away from.
  // This is safe since we're on main-thread, and the windowlist can only
  // be invalidated from the main-thread (see OnNavigation)
  if (IsWindowStillActive(aWindowId)) {
    MOZ_ASSERT(false, "Window already added");
    return;
  }

  aListener->MuteOrUnmuteCameras(mCamerasMuted);
  aListener->MuteOrUnmuteMicrophones(mMicrophonesMuted);
  GetActiveWindows()->InsertOrUpdate(aWindowId, std::move(aListener));

  RefPtr<WindowGlobalChild> wgc =
      WindowGlobalChild::GetByInnerWindowId(aWindowId);
  if (wgc) {
    wgc->BlockBFCacheFor(BFCacheStatus::ACTIVE_GET_USER_MEDIA);
  }
}

void MediaManager::RemoveWindowID(uint64_t aWindowId) {
  RefPtr<WindowGlobalChild> wgc =
      WindowGlobalChild::GetByInnerWindowId(aWindowId);
  if (wgc) {
    wgc->UnblockBFCacheFor(BFCacheStatus::ACTIVE_GET_USER_MEDIA);
  }

  mActiveWindows.Remove(aWindowId);

  // get outer windowID
  auto* window = nsGlobalWindowInner::GetInnerWindowWithId(aWindowId);
  if (!window) {
    LOG("No inner window for %" PRIu64, aWindowId);
    return;
  }

  auto* outer = window->GetOuterWindow();
  if (!outer) {
    LOG("No outer window for inner %" PRIu64, aWindowId);
    return;
  }

  uint64_t outerID = outer->WindowID();

  // Notify the UI that this window no longer has gUM active
  char windowBuffer[32];
  SprintfLiteral(windowBuffer, "%" PRIu64, outerID);
  nsString data = NS_ConvertUTF8toUTF16(windowBuffer);

  nsCOMPtr<nsIObserverService> obs = services::GetObserverService();
  obs->NotifyWhenScriptSafe(nullptr, "recording-window-ended", data.get());
  LOG("Sent recording-window-ended for window %" PRIu64 " (outer %" PRIu64 ")",
      aWindowId, outerID);
}

bool MediaManager::IsWindowListenerStillActive(
    const RefPtr<GetUserMediaWindowListener>& aListener) {
  MOZ_DIAGNOSTIC_ASSERT(aListener);
  return aListener && aListener == GetWindowListener(aListener->WindowID());
}

nsresult MediaManager::GetPref(nsIPrefBranch* aBranch, const char* aPref,
                               const char* aData, int32_t* aVal) {
  if (aData && strcmp(aPref, aData) != 0) {
    return NS_ERROR_INVALID_ARG;
  }

  int32_t temp;
  nsresult rv = aBranch->GetIntPref(aPref, &temp);
  if (NS_SUCCEEDED(rv)) {
    *aVal = temp;
  }

  return rv;
}

nsresult MediaManager::GetPrefBool(nsIPrefBranch* aBranch, const char* aPref,
                                   const char* aData, bool* aVal) {
  if (aData && strcmp(aPref, aData) != 0) {
    return NS_ERROR_INVALID_ARG;
  }

  bool temp;
  nsresult rv = aBranch->GetBoolPref(aPref, &temp);
  if (NS_SUCCEEDED(rv)) {
    *aVal = temp;
  }

  return rv;
}

#ifdef MOZ_WEBRTC
template <class Enum, class Int>
constexpr Enum ClampEnum(Int v) {
  return std::clamp(
      static_cast<Enum>(SaturatingCast<std::underlying_type_t<Enum>>(v)),
      ContiguousEnumValues<Enum>::min, ContiguousEnumValues<Enum>::max);
}
#endif

void MediaManager::GetPrefs(nsIPrefBranch* aBranch, const char* aData) {
  GetPref(aBranch, "media.navigator.video.default_width", aData,
          &mPrefs.mWidth);
  GetPref(aBranch, "media.navigator.video.default_height", aData,
          &mPrefs.mHeight);
  GetPref(aBranch, "media.navigator.video.default_fps", aData, &mPrefs.mFPS);
  GetPref(aBranch, "media.navigator.audio.fake_frequency", aData,
          &mPrefs.mFreq);
#ifdef MOZ_WEBRTC
  GetPrefBool(aBranch, "media.navigator.video.resize_mode.enabled", aData,
              &mPrefs.mResizeModeEnabled);
  int32_t resizeMode{};
  if (NS_SUCCEEDED(GetPref(aBranch, "media.navigator.video.default_resize_mode",
                           aData, &resizeMode))) {
    mPrefs.mResizeMode = ClampEnum<VideoResizeModeEnum>(resizeMode);
  }
  GetPrefBool(aBranch, "media.getusermedia.audio.processing.platform.enabled",
              aData, &mPrefs.mUsePlatformProcessing);
  GetPrefBool(aBranch, "media.getusermedia.audio.processing.aec.enabled", aData,
              &mPrefs.mAecOn);
  GetPrefBool(aBranch, "media.getusermedia.audio.processing.agc.enabled", aData,
              &mPrefs.mAgcOn);
  GetPrefBool(aBranch, "media.getusermedia.audio.processing.hpf.enabled", aData,
              &mPrefs.mHPFOn);
  GetPrefBool(aBranch, "media.getusermedia.audio.processing.noise.enabled",
              aData, &mPrefs.mNoiseOn);
  GetPrefBool(aBranch, "media.getusermedia.audio.processing.transient.enabled",
              aData, &mPrefs.mTransientOn);
  GetPrefBool(aBranch, "media.getusermedia.audio.processing.agc2.forced", aData,
              &mPrefs.mAgc2Forced);
  // Use 0 or 1 to force to false or true
  // EchoCanceller3Config::echo_removal_control.has_clock_drift.
  // -1 is the default, which means automatically set has_clock_drift as
  // deemed appropriate.
  GetPref(aBranch, "media.getusermedia.audio.processing.aec.expect_drift",
          aData, &mPrefs.mExpectDrift);
  GetPref(aBranch, "media.getusermedia.audio.processing.agc", aData,
          &mPrefs.mAgc);
  GetPref(aBranch, "media.getusermedia.audio.processing.noise", aData,
          &mPrefs.mNoise);
  GetPref(aBranch, "media.getusermedia.audio.max_channels", aData,
          &mPrefs.mChannels);
#endif
  LOG("%s: default prefs: %dx%d @%dfps, %dHz test tones, "
      "resize mode: %s, platform processing: %s, "
      "aec: %s, agc: %s, hpf: %s, noise: %s, drift: %s, agc level: %d, agc "
      "version: "
      "%s, noise level: %d, transient: %s, channels %d",
      __FUNCTION__, mPrefs.mWidth, mPrefs.mHeight, mPrefs.mFPS, mPrefs.mFreq,
      mPrefs.mResizeModeEnabled ? dom::GetEnumString(mPrefs.mResizeMode).get()
                                : "disabled",
      mPrefs.mUsePlatformProcessing ? "on" : "off",
      mPrefs.mAecOn ? "on" : "off", mPrefs.mAgcOn ? "on" : "off",
      mPrefs.mHPFOn ? "on" : "off", mPrefs.mNoiseOn ? "on" : "off",
      mPrefs.mExpectDrift < 0 ? "auto"
      : mPrefs.mExpectDrift   ? "on"
                              : "off",
      mPrefs.mAgc, mPrefs.mAgc2Forced ? "2" : "1", mPrefs.mNoise,
      mPrefs.mTransientOn ? "on" : "off", mPrefs.mChannels);
}

void MediaManager::Shutdown() {
  MOZ_ASSERT(NS_IsMainThread());
  if (sHasMainThreadShutdown) {
    return;
  }

  nsCOMPtr<nsIObserverService> obs = services::GetObserverService();

  obs->RemoveObserver(this, "last-pb-context-exited");
  obs->RemoveObserver(this, "getUserMedia:privileged:allow");
  obs->RemoveObserver(this, "getUserMedia:response:allow");
  obs->RemoveObserver(this, "getUserMedia:response:deny");
  obs->RemoveObserver(this, "getUserMedia:response:noOSPermission");
  obs->RemoveObserver(this, "getUserMedia:revoke");
  obs->RemoveObserver(this, "getUserMedia:muteVideo");
  obs->RemoveObserver(this, "getUserMedia:unmuteVideo");
  obs->RemoveObserver(this, "getUserMedia:muteAudio");
  obs->RemoveObserver(this, "getUserMedia:unmuteAudio");
  obs->RemoveObserver(this, "application-background");
  obs->RemoveObserver(this, "application-foreground");

  nsCOMPtr<nsIPrefBranch> prefs = do_GetService(NS_PREFSERVICE_CONTRACTID);
  if (prefs) {
    ForeachObservedPref([&](const nsLiteralCString& aPrefName) {
      prefs->RemoveObserver(aPrefName, this);
    });
  }

  if (mDeviceChangeTimer) {
    mDeviceChangeTimer->Cancel();
    // Drop ref to MediaTimer early to avoid blocking SharedThreadPool shutdown
    mDeviceChangeTimer = nullptr;
  }

  {
    // Close off any remaining active windows.

    // Live capture at this point is rare but can happen. Stopping it will make
    // the window listeners attempt to remove themselves from the active windows
    // table. We cannot touch the table at point so we grab a copy of the window
    // listeners first.
    const auto listeners = ToArray(GetActiveWindows()->Values());
    for (const auto& listener : listeners) {
      listener->RemoveAll();
    }
  }
  MOZ_ASSERT(GetActiveWindows()->Count() == 0);

  GetActiveWindows()->Clear();
  mActiveCallbacks.Clear();
  mCallIds.Clear();
  mPendingGUMRequest.Clear();
#ifdef MOZ_WEBRTC
  mLogHandle = nullptr;
#endif

  // From main thread's point of view, shutdown is now done.
  // All that remains is shutting down the media thread.
  sHasMainThreadShutdown = true;

  // Release the backend (and call Shutdown()) from within mMediaThread.
  // Don't use MediaManager::Dispatch() because we're
  // sHasMainThreadShutdown == true here!
  MOZ_ALWAYS_SUCCEEDS(mMediaThread->Dispatch(
      NS_NewRunnableFunction(__func__, [self = RefPtr(this), this]() {
        LOG("MediaManager Thread Shutdown");
        MOZ_ASSERT(IsInMediaThread());
        // Must shutdown backend on MediaManager thread, since that's
        // where we started it from!
        if (mBackend) {
          mBackend->Shutdown();  // idempotent
          mDeviceListChangeListener.DisconnectIfExists();
        }
        // last reference, will invoke Shutdown() again
        mBackend = nullptr;
      })));

  // note that this == sSingleton
  MOZ_ASSERT(this == sSingleton);

  // Explicitly shut down the TaskQueue so that it releases its
  // SharedThreadPool when all tasks have completed.  SharedThreadPool blocks
  // XPCOM shutdown from proceeding beyond "xpcom-shutdown-threads" until all
  // SharedThreadPools are released, but the nsComponentManager keeps a
  // reference to the MediaManager for the nsIMediaManagerService until much
  // later in shutdown.  This also provides additional assurance that no
  // further tasks will be queued.
  mMediaThread->BeginShutdown()->Then(
      GetMainThreadSerialEventTarget(), __func__, [] {
        LOG("MediaManager shutdown lambda running, releasing MediaManager "
            "singleton");
        // Remove async shutdown blocker
        media::MustGetShutdownBarrier()->RemoveBlocker(
            sSingleton->mShutdownBlocker);

        sSingleton = nullptr;
      });
}

void MediaManager::SendPendingGUMRequest() {
  if (mPendingGUMRequest.Length() > 0) {
    nsCOMPtr<nsIObserverService> obs = services::GetObserverService();
    obs->NotifyObservers(mPendingGUMRequest[0], "getUserMedia:request",
                         nullptr);
    mPendingGUMRequest.RemoveElementAt(0);
  }
}

bool IsGUMResponseNoAccess(const char* aTopic,
                           MediaMgrError::Name& aErrorName) {
  if (!strcmp(aTopic, "getUserMedia:response:deny")) {
    aErrorName = MediaMgrError::Name::NotAllowedError;
    return true;
  }

  if (!strcmp(aTopic, "getUserMedia:response:noOSPermission")) {
    aErrorName = MediaMgrError::Name::NotFoundError;
    return true;
  }

  return false;
}

static MediaSourceEnum ParseScreenColonWindowID(const char16_t* aData,
                                                uint64_t* aWindowIDOut) {
  MOZ_ASSERT(aWindowIDOut);
  // may be windowid or screen:windowid
  const nsDependentString data(aData);
  if (Substring(data, 0, strlen("screen:")).EqualsLiteral("screen:")) {
    nsresult rv;
    *aWindowIDOut = Substring(data, strlen("screen:")).ToInteger64(&rv);
    MOZ_RELEASE_ASSERT(NS_SUCCEEDED(rv));
    return MediaSourceEnum::Screen;
  }
  nsresult rv;
  *aWindowIDOut = data.ToInteger64(&rv);
  MOZ_RELEASE_ASSERT(NS_SUCCEEDED(rv));
  return MediaSourceEnum::Camera;
}

nsresult MediaManager::Observe(nsISupports* aSubject, const char* aTopic,
                               const char16_t* aData) {
  MOZ_ASSERT(NS_IsMainThread());

  MediaMgrError::Name gumNoAccessError = MediaMgrError::Name::NotAllowedError;

  if (!strcmp(aTopic, NS_PREFBRANCH_PREFCHANGE_TOPIC_ID)) {
    nsCOMPtr<nsIPrefBranch> branch(do_QueryInterface(aSubject));
    if (branch) {
      GetPrefs(branch, NS_ConvertUTF16toUTF8(aData).get());
      DeviceListChanged();
    }
  } else if (!strcmp(aTopic, "last-pb-context-exited")) {
    // Clear memory of private-browsing-specific deviceIds. Fire and forget.
    media::SanitizeOriginKeys(0, true);
    return NS_OK;
  } else if (!strcmp(aTopic, "getUserMedia:got-device-permission")) {
    MOZ_ASSERT(aSubject);
    nsCOMPtr<nsIRunnable> task = do_QueryInterface(aSubject);
    MediaManager::Dispatch(NewTaskFrom([task] { task->Run(); }));
    return NS_OK;
  } else if (!strcmp(aTopic, "getUserMedia:privileged:allow") ||
             !strcmp(aTopic, "getUserMedia:response:allow")) {
    nsString key(aData);
    RefPtr<GetUserMediaTask> task = TakeGetUserMediaTask(key);
    if (!task) {
      return NS_OK;
    }

    if (sHasMainThreadShutdown) {
      task->Denied(MediaMgrError::Name::AbortError, "In shutdown"_ns);
      return NS_OK;
    }
    if (NS_WARN_IF(!aSubject)) {
      return NS_ERROR_FAILURE;  // ignored
    }
    // Permission has been granted.  aSubject contains the particular device
    // or devices selected and approved by the user, if any.
    nsCOMPtr<nsIArray> array(do_QueryInterface(aSubject));
    MOZ_ASSERT(array);
    uint32_t len = 0;
    array->GetLength(&len);
    RefPtr<LocalMediaDevice> audioInput;
    RefPtr<LocalMediaDevice> videoInput;
    RefPtr<LocalMediaDevice> audioOutput;
    for (uint32_t i = 0; i < len; i++) {
      nsCOMPtr<nsIMediaDevice> device;
      array->QueryElementAt(i, NS_GET_IID(nsIMediaDevice),
                            getter_AddRefs(device));
      MOZ_ASSERT(device);  // shouldn't be returning anything else...
      if (!device) {
        continue;
      }

      // Casting here is safe because a LocalMediaDevice is created
      // only in Gecko side, JS can only query for an instance.
      auto* dev = static_cast<LocalMediaDevice*>(device.get());
      switch (dev->Kind()) {
        case MediaDeviceKind::Videoinput:
          if (!videoInput) {
            videoInput = dev;
          }
          break;
        case MediaDeviceKind::Audioinput:
          if (!audioInput) {
            audioInput = dev;
          }
          break;
        case MediaDeviceKind::Audiooutput:
          if (!audioOutput) {
            audioOutput = dev;
          }
          break;
        default:
          MOZ_CRASH("Unexpected device kind");
      }
    }

    if (GetUserMediaStreamTask* streamTask = task->AsGetUserMediaStreamTask()) {
      bool needVideo = IsOn(streamTask->GetConstraints().mVideo);
      bool needAudio = IsOn(streamTask->GetConstraints().mAudio);
      MOZ_ASSERT(needVideo || needAudio);

      if ((needVideo && !videoInput) || (needAudio && !audioInput)) {
        task->Denied(MediaMgrError::Name::NotAllowedError);
        return NS_OK;
      }
      streamTask->Allowed(std::move(audioInput), std::move(videoInput));
      return NS_OK;
    }
    if (SelectAudioOutputTask* outputTask = task->AsSelectAudioOutputTask()) {
      if (!audioOutput) {
        task->Denied(MediaMgrError::Name::NotAllowedError);
        return NS_OK;
      }
      outputTask->Allowed(std::move(audioOutput));
      return NS_OK;
    }

    NS_WARNING("Unknown task type in getUserMedia");
    return NS_ERROR_FAILURE;

  } else if (IsGUMResponseNoAccess(aTopic, gumNoAccessError)) {
    nsString key(aData);
    RefPtr<GetUserMediaTask> task = TakeGetUserMediaTask(key);
    if (task) {
      task->Denied(gumNoAccessError);
      SendPendingGUMRequest();
    }
    return NS_OK;

  } else if (!strcmp(aTopic, "getUserMedia:revoke")) {
    uint64_t windowID;
    if (ParseScreenColonWindowID(aData, &windowID) == MediaSourceEnum::Screen) {
      LOG("Revoking ScreenCapture access for window %" PRIu64, windowID);
      StopScreensharing(windowID);
    } else {
      LOG("Revoking MediaCapture access for window %" PRIu64, windowID);
      OnNavigation(windowID);
    }
    return NS_OK;
  } else if (!strcmp(aTopic, "getUserMedia:muteVideo") ||
             !strcmp(aTopic, "getUserMedia:unmuteVideo")) {
    OnCameraMute(!strcmp(aTopic, "getUserMedia:muteVideo"));
    return NS_OK;
  } else if (!strcmp(aTopic, "getUserMedia:muteAudio") ||
             !strcmp(aTopic, "getUserMedia:unmuteAudio")) {
    OnMicrophoneMute(!strcmp(aTopic, "getUserMedia:muteAudio"));
    return NS_OK;
  } else if ((!strcmp(aTopic, "application-background") ||
              !strcmp(aTopic, "application-foreground")) &&
             StaticPrefs::media_getusermedia_camera_background_mute_enabled()) {
    // On mobile we turn off any cameras (but not mics) while in the background.
    // Keeping things simple for now by duplicating test-covered code above.
    //
    // NOTE: If a mobile device ever wants to implement "getUserMedia:muteVideo"
    // as well, it'd need to update this code to handle & test the combinations.
    OnCameraMute(!strcmp(aTopic, "application-background"));
  }

  return NS_OK;
}

NS_IMETHODIMP
MediaManager::CollectReports(nsIHandleReportCallback* aHandleReport,
                             nsISupports* aData, bool aAnonymize) {
  size_t amount = 0;
  amount += mActiveWindows.ShallowSizeOfExcludingThis(MallocSizeOf);
  for (const GetUserMediaWindowListener* listener : mActiveWindows.Values()) {
    amount += listener->SizeOfIncludingThis(MallocSizeOf);
  }
  amount += mActiveCallbacks.ShallowSizeOfExcludingThis(MallocSizeOf);
  for (const GetUserMediaTask* task : mActiveCallbacks.Values()) {
    // Assume nsString buffers for keys are accounted in mCallIds.
    amount += task->SizeOfIncludingThis(MallocSizeOf);
  }
  amount += mCallIds.ShallowSizeOfExcludingThis(MallocSizeOf);
  for (const auto& array : mCallIds.Values()) {
    amount += array->ShallowSizeOfExcludingThis(MallocSizeOf);
    for (const nsString& callID : *array) {
      amount += callID.SizeOfExcludingThisEvenIfShared(MallocSizeOf);
    }
  }
  amount += mPendingGUMRequest.ShallowSizeOfExcludingThis(MallocSizeOf);
  // GetUserMediaRequest pointees of mPendingGUMRequest do not have support
  // for memory accounting.  mPendingGUMRequest logic should probably be moved
  // to the front end (bug 1691625).
  MOZ_COLLECT_REPORT("explicit/media/media-manager-aggregates", KIND_HEAP,
                     UNITS_BYTES, amount,
                     "Memory used by MediaManager variable length members.");
  return NS_OK;
}

nsresult MediaManager::GetActiveMediaCaptureWindows(nsIArray** aArray) {
  MOZ_ASSERT(aArray);

  nsCOMPtr<nsIMutableArray> array = nsArray::Create();

  for (const auto& entry : mActiveWindows) {
    const uint64_t& id = entry.GetKey();
    RefPtr<GetUserMediaWindowListener> winListener = entry.GetData();
    if (!winListener) {
      continue;
    }

    auto* window = nsGlobalWindowInner::GetInnerWindowWithId(id);
    MOZ_ASSERT(window);
    // XXXkhuey ...
    if (!window) {
      continue;
    }

    if (winListener->CapturingVideo() || winListener->CapturingAudio()) {
      array->AppendElement(ToSupports(window));
    }
  }

  array.forget(aArray);
  return NS_OK;
}

struct CaptureWindowStateData {
  uint16_t* mCamera;
  uint16_t* mMicrophone;
  uint16_t* mScreenShare;
  uint16_t* mWindowShare;
  uint16_t* mAppShare;
  uint16_t* mBrowserShare;
};

NS_IMETHODIMP
MediaManager::MediaCaptureWindowState(
    nsIDOMWindow* aCapturedWindow, uint16_t* aCamera, uint16_t* aMicrophone,
    uint16_t* aScreen, uint16_t* aWindow, uint16_t* aBrowser,
    nsTArray<RefPtr<nsIMediaDevice>>& aDevices) {
  MOZ_ASSERT(NS_IsMainThread());

  CaptureState camera = CaptureState::Off;
  CaptureState microphone = CaptureState::Off;
  CaptureState screen = CaptureState::Off;
  CaptureState window = CaptureState::Off;
  CaptureState browser = CaptureState::Off;
  RefPtr<LocalMediaDeviceSetRefCnt> devices;

  nsCOMPtr<nsPIDOMWindowInner> piWin = do_QueryInterface(aCapturedWindow);
  if (piWin) {
    if (RefPtr<GetUserMediaWindowListener> listener =
            GetWindowListener(piWin->WindowID())) {
      camera = listener->CapturingSource(MediaSourceEnum::Camera);
      microphone = listener->CapturingSource(MediaSourceEnum::Microphone);
      screen = listener->CapturingSource(MediaSourceEnum::Screen);
      window = listener->CapturingSource(MediaSourceEnum::Window);
      browser = listener->CapturingSource(MediaSourceEnum::Browser);
      devices = listener->GetDevices();
    }
  }

  *aCamera = FromCaptureState(camera);
  *aMicrophone = FromCaptureState(microphone);
  *aScreen = FromCaptureState(screen);
  *aWindow = FromCaptureState(window);
  *aBrowser = FromCaptureState(browser);
  if (devices) {
    for (auto& device : *devices) {
      aDevices.AppendElement(device);
    }
  }

  LOG("%s: window %" PRIu64 " capturing %s %s %s %s %s", __FUNCTION__,
      piWin ? piWin->WindowID() : -1,
      *aCamera == nsIMediaManagerService::STATE_CAPTURE_ENABLED
          ? "camera (enabled)"
          : (*aCamera == nsIMediaManagerService::STATE_CAPTURE_DISABLED
                 ? "camera (disabled)"
                 : ""),
      *aMicrophone == nsIMediaManagerService::STATE_CAPTURE_ENABLED
          ? "microphone (enabled)"
          : (*aMicrophone == nsIMediaManagerService::STATE_CAPTURE_DISABLED
                 ? "microphone (disabled)"
                 : ""),
      *aScreen ? "screenshare" : "", *aWindow ? "windowshare" : "",
      *aBrowser ? "browsershare" : "");

  return NS_OK;
}

NS_IMETHODIMP
MediaManager::SanitizeDeviceIds(int64_t aSinceWhen) {
  MOZ_ASSERT(NS_IsMainThread());
  LOG("%s: sinceWhen = %" PRId64, __FUNCTION__, aSinceWhen);

  media::SanitizeOriginKeys(aSinceWhen, false);  // we fire and forget
  return NS_OK;
}

void MediaManager::StopScreensharing(uint64_t aWindowID) {
  // We need to stop window/screensharing for all streams in this innerwindow.

  if (RefPtr<GetUserMediaWindowListener> listener =
          GetWindowListener(aWindowID)) {
    listener->StopSharing();
  }
}

bool MediaManager::IsActivelyCapturingOrHasAPermission(uint64_t aWindowId) {
  // Does page currently have a gUM stream active?

  nsCOMPtr<nsIArray> array;
  GetActiveMediaCaptureWindows(getter_AddRefs(array));
  uint32_t len;
  array->GetLength(&len);
  for (uint32_t i = 0; i < len; i++) {
    nsCOMPtr<nsPIDOMWindowInner> win;
    array->QueryElementAt(i, NS_GET_IID(nsPIDOMWindowInner),
                          getter_AddRefs(win));
    if (win && win->WindowID() == aWindowId) {
      return true;
    }
  }

  // Or are persistent permissions (audio or video) granted?

  return GetPersistentPermissions(aWindowId)
      .map([](auto&& aState) {
        return aState.mMicrophonePermission ==
                   PersistentPermissionState::Allow ||
               aState.mCameraPermission == PersistentPermissionState::Allow;
      })
      .unwrapOr(false);
}

DeviceListener::DeviceListener()
    : mStopped(false),
      mMainThreadCheck(nullptr),
      mPrincipalHandle(PRINCIPAL_HANDLE_NONE),
      mWindowListener(nullptr) {}

void DeviceListener::Register(GetUserMediaWindowListener* aListener) {
  LOG("DeviceListener %p registering with window listener %p", this, aListener);

  MOZ_ASSERT(aListener, "No listener");
  MOZ_ASSERT(!mWindowListener, "Already registered");
  MOZ_ASSERT(!Activated(), "Already activated");

  mPrincipalHandle = aListener->GetPrincipalHandle();
  mWindowListener = aListener;
}

void DeviceListener::Activate(RefPtr<LocalMediaDevice> aDevice,
                              RefPtr<LocalTrackSource> aTrackSource,
                              bool aStartMuted, bool aIsAllocated) {
  MOZ_ASSERT(NS_IsMainThread(), "Only call on main thread");

  LOG("DeviceListener %p activating %s device %p", this,
      dom::GetEnumString(aDevice->Kind()).get(), aDevice.get());

  MOZ_ASSERT(!mStopped, "Cannot activate stopped device listener");
  MOZ_ASSERT(!Activated(), "Already activated");

  mMainThreadCheck = PR_GetCurrentThread();
  bool offWhileDisabled =
      (aDevice->GetMediaSource() == MediaSourceEnum::Microphone &&
       Preferences::GetBool(
           "media.getusermedia.microphone.off_while_disabled.enabled", true)) ||
      (aDevice->GetMediaSource() == MediaSourceEnum::Camera &&
       Preferences::GetBool(
           "media.getusermedia.camera.off_while_disabled.enabled", true));

  if (MediaEventSource<void>* event = aDevice->Source()->CaptureEndedEvent()) {
    mCaptureEndedListener = event->Connect(AbstractThread::MainThread(), this,
                                           &DeviceListener::Stop);
  }

  mDeviceState = MakeUnique<DeviceState>(
      std::move(aDevice), std::move(aTrackSource), offWhileDisabled);
  mDeviceState->mDeviceMuted = aStartMuted;
  mDeviceState->mAllocated = aIsAllocated;
  if (aStartMuted) {
    mDeviceState->mTrackSource->Mute();
  }
}

RefPtr<DeviceListener::DeviceListenerPromise>
DeviceListener::InitializeAsync() {
  MOZ_ASSERT(NS_IsMainThread(), "Only call on main thread");
  MOZ_DIAGNOSTIC_ASSERT(!mStopped);

  return InvokeAsync(
             MediaManager::Get()->mMediaThread, __func__,
             [this, self = RefPtr(this), principal = GetPrincipalHandle(),
              device = mDeviceState->mDevice,
              track = mDeviceState->mTrackSource->mTrack] {
               // Start the device if not muted, or if mOffWhileDisabled is
               // false (the device should remain on even when muted).
               bool startDevice = !mDeviceState->mDeviceMuted ||
                                  !mDeviceState->mOffWhileDisabled;
               nsresult rv = Initialize(principal, device, track, startDevice);
               if (NS_SUCCEEDED(rv)) {
                 return GenericPromise::CreateAndResolve(
                     true, "DeviceListener::InitializeAsync success");
               }
               return GenericPromise::CreateAndReject(
                   rv, "DeviceListener::InitializeAsync failure");
             })
      ->Then(
          GetMainThreadSerialEventTarget(), __func__,
          [self = RefPtr<DeviceListener>(this), this](bool) {
            if (mStopped) {
              // We were shut down during the async init
              return DeviceListenerPromise::CreateAndResolve(true, __func__);
            }

            MOZ_DIAGNOSTIC_ASSERT(!mDeviceState->mTrackEnabled);
            MOZ_DIAGNOSTIC_ASSERT(!mDeviceState->mDeviceEnabled);
            MOZ_DIAGNOSTIC_ASSERT(!mDeviceState->mStopped);

            mDeviceState->mDeviceEnabled = true;
            mDeviceState->mTrackEnabled = true;
            mDeviceState->mTrackEnabledTime = TimeStamp::Now();
            return DeviceListenerPromise::CreateAndResolve(true, __func__);
          },
          [self = RefPtr<DeviceListener>(this), this](nsresult aRv) {
            auto kind = mDeviceState->mDevice->Kind();
            RefPtr<MediaMgrError> err;
            if (aRv == NS_ERROR_NOT_AVAILABLE &&
                kind == MediaDeviceKind::Audioinput) {
              nsCString log;
              log.AssignLiteral("Concurrent mic process limit.");
              err = MakeRefPtr<MediaMgrError>(
                  MediaMgrError::Name::NotReadableError, std::move(log));
            } else if (NS_FAILED(aRv)) {
              nsCString log;
              log.AppendPrintf("Starting %s failed",
                               dom::GetEnumString(kind).get());
              err = MakeRefPtr<MediaMgrError>(MediaMgrError::Name::AbortError,
                                              std::move(log));
            }

            if (mStopped) {
              return DeviceListenerPromise::CreateAndReject(err, __func__);
            }

            MOZ_DIAGNOSTIC_ASSERT(!mDeviceState->mTrackEnabled);
            MOZ_DIAGNOSTIC_ASSERT(!mDeviceState->mDeviceEnabled);
            MOZ_DIAGNOSTIC_ASSERT(!mDeviceState->mStopped);

            Stop();

            return DeviceListenerPromise::CreateAndReject(err, __func__);
          });
}

nsresult DeviceListener::Initialize(PrincipalHandle aPrincipal,
                                    LocalMediaDevice* aDevice,
                                    MediaTrack* aTrack, bool aStartDevice) {
  MOZ_ASSERT(MediaManager::IsInMediaThread());

  auto kind = aDevice->Kind();
  aDevice->SetTrack(aTrack, aPrincipal);
  nsresult rv = aStartDevice ? aDevice->Start() : NS_OK;
  if (kind == MediaDeviceKind::Audioinput ||
      kind == MediaDeviceKind::Videoinput) {
    if ((rv == NS_ERROR_NOT_AVAILABLE && kind == MediaDeviceKind::Audioinput) ||
        (NS_FAILED(rv) && kind == MediaDeviceKind::Videoinput)) {
      PR_Sleep(200);
      rv = aDevice->Start();
    }
  }
  LOG("started %s device %p", dom::GetEnumString(kind).get(), aDevice);
  return rv;
}

already_AddRefed<DeviceListener> DeviceListener::Clone() const {
  MOZ_ASSERT(NS_IsMainThread());
  MediaManager* mgr = MediaManager::GetIfExists();
  if (!mgr) {
    return nullptr;
  }
  if (!mWindowListener) {
    return nullptr;
  }
  auto* thisDevice = GetDevice();
  if (!thisDevice) {
    return nullptr;
  }

  auto* thisTrackSource = GetTrackSource();
  if (!thisTrackSource) {
    return nullptr;
  }

  // See PrepareDOMStream for how a gUM/gDM track is created.
  RefPtr<MediaTrack> track;
  MediaTrackGraph* mtg = thisTrackSource->mTrack->Graph();
  if (const auto source = thisDevice->GetMediaSource();
      source == dom::MediaSourceEnum::Microphone) {
#ifdef MOZ_WEBRTC
    if (thisDevice->IsFake()) {
      track = mtg->CreateSourceTrack(MediaSegment::AUDIO);
    } else {
      track = AudioProcessingTrack::Create(mtg);
      track->Suspend();  // Microphone source resumes in SetTrack
    }
#else
    track = mtg->CreateSourceTrack(MediaSegment::AUDIO);
#endif
  } else if (source == dom::MediaSourceEnum::Camera ||
             source == dom::MediaSourceEnum::Screen ||
             source == dom::MediaSourceEnum::Window ||
             source == dom::MediaSourceEnum::Browser) {
    track = mtg->CreateSourceTrack(MediaSegment::VIDEO);
  }

  if (!track) {
    return nullptr;
  }

  RefPtr device = thisDevice->Clone();
  auto listener = MakeRefPtr<DeviceListener>();
  auto trackSource = MakeRefPtr<LocalTrackSource>(
      thisTrackSource->GetPrincipal(), thisTrackSource->mLabel, listener,
      thisTrackSource->mSource, track, thisTrackSource->mPeerIdentity,
      thisTrackSource->mTrackingId);

  LOG("DeviceListener %p registering clone", this);
  mWindowListener->Register(listener);
  LOG("DeviceListener %p activating clone", this);
  mWindowListener->Activate(listener, device, trackSource,
                            /*aIsAllocated=*/false);

  listener->mDeviceState->mDeviceEnabled = mDeviceState->mDeviceEnabled;
  listener->mDeviceState->mDeviceMuted = mDeviceState->mDeviceMuted;
  listener->mDeviceState->mTrackEnabled = mDeviceState->mTrackEnabled;
  listener->mDeviceState->mTrackEnabledTime = TimeStamp::Now();

  // We have to do an async operation here, even though Clone() is sync.
  // This is fine because JS will not be able to trigger any operation to run
  // async on the media thread.
  LOG("DeviceListener %p allocating clone device %p async", this, device.get());
  InvokeAsync(
      mgr->mMediaThread, __func__,
      [thisDevice = RefPtr(thisDevice), device, prefs = mgr->mPrefs,
       windowId = mWindowListener->WindowID(), listener,
       principal = GetPrincipalHandle(), track,
       startDevice = !listener->mDeviceState->mOffWhileDisabled ||
                     (!listener->mDeviceState->mDeviceMuted &&
                      listener->mDeviceState->mDeviceEnabled)] {
        const char* outBadConstraint{};
        nsresult rv = device->Source()->Allocate(
            thisDevice->Constraints(), prefs, windowId, &outBadConstraint);
        LOG("Allocated clone device %p. rv=%s", device.get(),
            GetStaticErrorName(rv));
        if (NS_FAILED(rv)) {
          return GenericPromise::CreateAndReject(
              rv, "DeviceListener::Clone failure #1");
        }
        rv = listener->Initialize(principal, device, track, startDevice);
        if (NS_SUCCEEDED(rv)) {
          return GenericPromise::CreateAndResolve(
              true, "DeviceListener::Clone success");
        }
        return GenericPromise::CreateAndReject(
            rv, "DeviceListener::Clone failure #2");
      })
      ->Then(
          GetMainThreadSerialEventTarget(), __func__,
          [listener, device,
           trackSource](GenericPromise::ResolveOrRejectValue&& aValue) {
            if (aValue.IsReject()) {
              // Allocating/initializing failed. Stopping the device listener
              // will destroy the MediaStreamTrackSource's MediaTrack, which
              // will make the MediaStreamTrack's mTrack MediaTrack auto-end
              // due to lack of inputs. This makes the MediaStreamTrack's
              // readyState transition to "ended" as expected.
              LOG("Allocating clone device %p failed. Stopping.", device.get());
              listener->Stop();
              return;
            }
            listener->mDeviceState->mAllocated = true;
            if (listener->mDeviceState->mStopped && !sHasMainThreadShutdown) {
              MediaManager::Dispatch(NS_NewRunnableFunction(
                  "DeviceListener::Clone::Stop",
                  [device = listener->mDeviceState->mDevice]() {
                    device->Stop();
                    device->Deallocate();
                  }));
            }
          });

  return listener.forget();
}

void DeviceListener::Stop() {
  MOZ_ASSERT(NS_IsMainThread(), "Only call on main thread");

  if (mStopped) {
    return;
  }
  mStopped = true;

  LOG("DeviceListener %p stopping", this);

  if (mDeviceState) {
    mDeviceState->mDisableTimer->Cancel();

    if (mDeviceState->mStopped) {
      // device already stopped.
      return;
    }
    mDeviceState->mStopped = true;

    mDeviceState->mTrackSource->Stop();

    if (mDeviceState->mAllocated) {
      MediaManager::Dispatch(NewTaskFrom([device = mDeviceState->mDevice]() {
        device->Stop();
        device->Deallocate();
      }));
    }

    mWindowListener->ChromeAffectingStateChanged();
  }

  mCaptureEndedListener.DisconnectIfExists();

  // Keep a strong ref to the removed window listener.
  RefPtr<GetUserMediaWindowListener> windowListener = mWindowListener;
  mWindowListener = nullptr;
  windowListener->Remove(this);
}

void DeviceListener::GetSettings(MediaTrackSettings& aOutSettings) const {
  MOZ_ASSERT(NS_IsMainThread(), "Only call on main thread");
  LocalMediaDevice* device = GetDevice();
  device->GetSettings(aOutSettings);

  MediaSourceEnum mediaSource = device->GetMediaSource();
  if (mediaSource == MediaSourceEnum::Camera ||
      mediaSource == MediaSourceEnum::Microphone) {
    aOutSettings.mDeviceId.Construct(device->mID);
    aOutSettings.mGroupId.Construct(device->mGroupID);
  }
}

void DeviceListener::GetCapabilities(
    MediaTrackCapabilities& aOutCapabilities) const {
  MOZ_ASSERT(NS_IsMainThread(), "Only call on main thread");
  LocalMediaDevice* device = GetDevice();
  device->GetCapabilities(aOutCapabilities);

  MediaSourceEnum mediaSource = device->GetMediaSource();
  if (mediaSource == MediaSourceEnum::Camera ||
      mediaSource == MediaSourceEnum::Microphone) {
    aOutCapabilities.mDeviceId.Construct(device->mID);
    aOutCapabilities.mGroupId.Construct(device->mGroupID);
  }
}

auto DeviceListener::UpdateDevice(bool aOn) -> RefPtr<DeviceOperationPromise> {
  MOZ_ASSERT(NS_IsMainThread());
  RefPtr<DeviceListener> self = this;
  DeviceState& state = *mDeviceState;
  return MediaManager::Dispatch<DeviceOperationPromise>(
             __func__,
             [self, device = state.mDevice,
              aOn](MozPromiseHolder<DeviceOperationPromise>& h) {
               LOG("Turning %s device (%s)", aOn ? "on" : "off",
                   NS_ConvertUTF16toUTF8(device->mName).get());
               h.Resolve(aOn ? device->Start() : device->Stop(), __func__);
             })
      ->Then(
          GetMainThreadSerialEventTarget(), __func__,
          [self, this, &state, aOn](nsresult aResult) {
            if (state.mStopped) {
              // Device was stopped on main thread during the operation. Done.
              return DeviceOperationPromise::CreateAndResolve(aResult,
                                                              __func__);
            }
            LOG("DeviceListener %p turning %s %s input device %s", this,
                aOn ? "on" : "off",
                dom::GetEnumString(GetDevice()->Kind()).get(),
                NS_SUCCEEDED(aResult) ? "succeeded" : "failed");

            if (NS_FAILED(aResult) && aResult != NS_ERROR_ABORT) {
              // This path handles errors from starting or stopping the
              // device. NS_ERROR_ABORT are for cases where *we* aborted. They
              // need graceful handling.
              if (aOn) {
                // Starting the device failed. Stopping the track here will
                // make the MediaStreamTrack end after a pass through the
                // MediaTrackGraph.
                Stop();
              } else {
                // Stopping the device failed. This is odd, but not fatal.
                MOZ_ASSERT_UNREACHABLE("The device should be stoppable");
              }
            }
            return DeviceOperationPromise::CreateAndResolve(aResult, __func__);
          },
          []() {
            MOZ_ASSERT_UNREACHABLE("Unexpected and unhandled reject");
            return DeviceOperationPromise::CreateAndReject(false, __func__);
          });
}

void DeviceListener::SetDeviceEnabled(bool aEnable) {
  MOZ_ASSERT(NS_IsMainThread(), "Only call on main thread");
  MOZ_ASSERT(Activated(), "No device to set enabled state for");

  DeviceState& state = *mDeviceState;

  LOG("DeviceListener %p %s %s device", this,
      aEnable ? "enabling" : "disabling",
      dom::GetEnumString(GetDevice()->Kind()).get());

  state.mTrackEnabled = aEnable;

  if (state.mStopped) {
    // Device terminally stopped. Updating device state is pointless.
    return;
  }

  if (state.mOperationInProgress) {
    // If a timer is in progress, it needs to be canceled now so the next
    // DisableTrack() gets a fresh start. Canceling will trigger another
    // operation.
    state.mDisableTimer->Cancel();
    return;
  }

  if (state.mDeviceEnabled == aEnable) {
    // Device is already in the desired state.
    return;
  }

  // All paths from here on must end in setting
  // `state.mOperationInProgress` to false.
  state.mOperationInProgress = true;

  RefPtr<MediaTimerPromise> timerPromise;
  if (aEnable) {
    timerPromise = MediaTimerPromise::CreateAndResolve(true, __func__);
    state.mTrackEnabledTime = TimeStamp::Now();
  } else {
    const TimeDuration maxDelay =
        TimeDuration::FromMilliseconds(Preferences::GetUint(
            GetDevice()->Kind() == MediaDeviceKind::Audioinput
                ? "media.getusermedia.microphone.off_while_disabled.delay_ms"
                : "media.getusermedia.camera.off_while_disabled.delay_ms",
            3000));
    const TimeDuration durationEnabled =
        TimeStamp::Now() - state.mTrackEnabledTime;
    const TimeDuration delay = TimeDuration::Max(
        TimeDuration::FromMilliseconds(0), maxDelay - durationEnabled);
    timerPromise = state.mDisableTimer->WaitFor(delay, __func__);
  }

  RefPtr<DeviceListener> self = this;
  timerPromise
      ->Then(
          GetMainThreadSerialEventTarget(), __func__,
          [self, this, &state, aEnable]() mutable {
            MOZ_ASSERT(state.mDeviceEnabled != aEnable,
                       "Device operation hasn't started");
            MOZ_ASSERT(state.mOperationInProgress,
                       "It's our responsibility to reset the inProgress state");

            LOG("DeviceListener %p %s %s device - starting device operation",
                this, aEnable ? "enabling" : "disabling",
                dom::GetEnumString(GetDevice()->Kind()).get());

            if (state.mStopped) {
              // Source was stopped between timer resolving and this runnable.
              return DeviceOperationPromise::CreateAndResolve(NS_ERROR_ABORT,
                                                              __func__);
            }

            state.mDeviceEnabled = aEnable;

            if (mWindowListener) {
              mWindowListener->ChromeAffectingStateChanged();
            }
            if (!state.mOffWhileDisabled || state.mDeviceMuted) {
              // If the feature to turn a device off while disabled is itself
              // disabled, or the device is currently user agent muted, then
              // we shortcut the device operation and tell the
              // ux-updating code that everything went fine.
              return DeviceOperationPromise::CreateAndResolve(NS_OK, __func__);
            }
            return UpdateDevice(aEnable);
          },
          []() {
            // Timer was canceled by us. We signal this with NS_ERROR_ABORT.
            return DeviceOperationPromise::CreateAndResolve(NS_ERROR_ABORT,
                                                            __func__);
          })
      ->Then(
          GetMainThreadSerialEventTarget(), __func__,
          [self, this, &state, aEnable](nsresult aResult) mutable {
            MOZ_ASSERT_IF(aResult != NS_ERROR_ABORT,
                          state.mDeviceEnabled == aEnable);
            MOZ_ASSERT(state.mOperationInProgress);
            state.mOperationInProgress = false;

            if (state.mStopped) {
              // Device was stopped on main thread during the operation.
              // Nothing to do.
              return;
            }

            if (NS_FAILED(aResult) && aResult != NS_ERROR_ABORT && !aEnable) {
              // To keep our internal state sane in this case, we disallow
              // future stops due to disable.
              state.mOffWhileDisabled = false;
              return;
            }

            // This path is for a device operation aResult that was success or
            // NS_ERROR_ABORT (*we* canceled the operation).
            // At this point we have to follow up on the intended state, i.e.,
            // update the device state if the track state changed in the
            // meantime.

            if (state.mTrackEnabled != state.mDeviceEnabled) {
              // Track state changed during this operation. We'll start over.
              SetDeviceEnabled(state.mTrackEnabled);
            }
          },
          []() { MOZ_ASSERT_UNREACHABLE("Unexpected and unhandled reject"); });
}

void DeviceListener::SetDeviceMuted(bool aMute) {
  MOZ_ASSERT(NS_IsMainThread(), "Only call on main thread");
  MOZ_ASSERT(Activated(), "No device to set muted state for");

  DeviceState& state = *mDeviceState;

  LOG("DeviceListener %p %s %s device", this, aMute ? "muting" : "unmuting",
      dom::GetEnumString(GetDevice()->Kind()).get());

  if (state.mStopped) {
    // Device terminally stopped. Updating device state is pointless.
    return;
  }

  if (state.mDeviceMuted == aMute) {
    // Device is already in the desired state.
    return;
  }

  LOG("DeviceListener %p %s %s device - starting device operation", this,
      aMute ? "muting" : "unmuting",
      dom::GetEnumString(GetDevice()->Kind()).get());

  state.mDeviceMuted = aMute;

  if (mWindowListener) {
    mWindowListener->ChromeAffectingStateChanged();
  }
  // Update trackSource to fire mute/unmute events on all its tracks
  if (aMute) {
    state.mTrackSource->Mute();
  } else {
    state.mTrackSource->Unmute();
  }
  if (!state.mOffWhileDisabled || !state.mDeviceEnabled) {
    // If the pref to turn the underlying device is itself off, or the device
    // is already off, it's unecessary to do anything else.
    return;
  }
  UpdateDevice(!aMute);
}

void DeviceListener::MuteOrUnmuteCamera(bool aMute) {
  MOZ_ASSERT(NS_IsMainThread());

  if (mStopped) {
    return;
  }

  MOZ_RELEASE_ASSERT(mWindowListener);
  LOG("DeviceListener %p MuteOrUnmuteCamera: %s", this,
      aMute ? "mute" : "unmute");

  if (GetDevice() &&
      (GetDevice()->GetMediaSource() == MediaSourceEnum::Camera)) {
    SetDeviceMuted(aMute);
  }
}

void DeviceListener::MuteOrUnmuteMicrophone(bool aMute) {
  MOZ_ASSERT(NS_IsMainThread());

  if (mStopped) {
    return;
  }

  MOZ_RELEASE_ASSERT(mWindowListener);
  LOG("DeviceListener %p MuteOrUnmuteMicrophone: %s", this,
      aMute ? "mute" : "unmute");

  if (GetDevice() &&
      (GetDevice()->GetMediaSource() == MediaSourceEnum::Microphone)) {
    SetDeviceMuted(aMute);
  }
}

bool DeviceListener::CapturingVideo() const {
  MOZ_ASSERT(NS_IsMainThread());
  return Activated() && mDeviceState && !mDeviceState->mStopped &&
         MediaEngineSource::IsVideo(GetDevice()->GetMediaSource()) &&
         (!GetDevice()->IsFake() ||
          Preferences::GetBool("media.navigator.permission.fake"));
}

bool DeviceListener::CapturingAudio() const {
  MOZ_ASSERT(NS_IsMainThread());
  return Activated() && mDeviceState && !mDeviceState->mStopped &&
         MediaEngineSource::IsAudio(GetDevice()->GetMediaSource()) &&
         (!GetDevice()->IsFake() ||
          Preferences::GetBool("media.navigator.permission.fake"));
}

CaptureState DeviceListener::CapturingSource(MediaSourceEnum aSource) const {
  MOZ_ASSERT(NS_IsMainThread());
  if (GetDevice()->GetMediaSource() != aSource) {
    // This DeviceListener doesn't capture a matching source
    return CaptureState::Off;
  }

  if (mDeviceState->mStopped) {
    // The source is a match but has been permanently stopped
    return CaptureState::Off;
  }

  if ((aSource == MediaSourceEnum::Camera ||
       aSource == MediaSourceEnum::Microphone) &&
      GetDevice()->IsFake() &&
      !Preferences::GetBool("media.navigator.permission.fake")) {
    // Fake Camera and Microphone only count if there is no fake permission
    return CaptureState::Off;
  }

  // Source is a match and is active and unmuted

  if (mDeviceState->mDeviceEnabled && !mDeviceState->mDeviceMuted) {
    return CaptureState::Enabled;
  }

  return CaptureState::Disabled;
}

RefPtr<DeviceListener::DeviceListenerPromise> DeviceListener::ApplyConstraints(
    const MediaTrackConstraints& aConstraints, CallerType aCallerType) {
  MOZ_ASSERT(NS_IsMainThread());

  if (mStopped || mDeviceState->mStopped) {
    LOG("DeviceListener %p %s device applyConstraints, but device is stopped",
        this, dom::GetEnumString(GetDevice()->Kind()).get());
    return DeviceListenerPromise::CreateAndResolve(false, __func__);
  }

  MediaManager* mgr = MediaManager::GetIfExists();
  if (!mgr) {
    return DeviceListenerPromise::CreateAndResolve(false, __func__);
  }

  return InvokeAsync(
      mgr->mMediaThread, __func__,
      [device = mDeviceState->mDevice, aConstraints, prefs = mgr->mPrefs,
       aCallerType]() mutable -> RefPtr<DeviceListenerPromise> {
        MOZ_ASSERT(MediaManager::IsInMediaThread());
        MediaManager* mgr = MediaManager::GetIfExists();
        MOZ_RELEASE_ASSERT(mgr);  // Must exist while media thread is alive
        const char* badConstraint = nullptr;
        nsresult rv =
            device->Reconfigure(aConstraints, mgr->mPrefs, &badConstraint);
        if (NS_FAILED(rv)) {
          if (rv == NS_ERROR_INVALID_ARG) {
            // Reconfigure failed due to constraints
            if (!badConstraint) {
              nsTArray<RefPtr<LocalMediaDevice>> devices;
              devices.AppendElement(device);
              badConstraint = MediaConstraintsHelper::SelectSettings(
                  NormalizedConstraints(aConstraints), prefs, devices,
                  aCallerType);
            }
          } else {
            // Unexpected. ApplyConstraints* cannot fail with any other error.
            badConstraint = "";
            LOG("ApplyConstraints-Task: Unexpected fail %" PRIx32,
                static_cast<uint32_t>(rv));
          }

          return DeviceListenerPromise::CreateAndReject(
              MakeRefPtr<MediaMgrError>(
                  MediaMgrError::Name::OverconstrainedError, "",
                  NS_ConvertASCIItoUTF16(badConstraint)),
              __func__);
        }
        // Reconfigure was successful
        return DeviceListenerPromise::CreateAndResolve(false, __func__);
      });
}

PrincipalHandle DeviceListener::GetPrincipalHandle() const {
  return mPrincipalHandle;
}

void GetUserMediaWindowListener::StopSharing() {
  MOZ_ASSERT(NS_IsMainThread(), "Only call on main thread");

  for (auto& l : mActiveListeners.Clone()) {
    MediaSourceEnum source = l->GetDevice()->GetMediaSource();
    if (source == MediaSourceEnum::Screen ||
        source == MediaSourceEnum::Window ||
        source == MediaSourceEnum::AudioCapture ||
        source == MediaSourceEnum::Browser) {
      l->Stop();
    }
  }
}

void GetUserMediaWindowListener::StopRawID(const nsString& removedDeviceID) {
  MOZ_ASSERT(NS_IsMainThread(), "Only call on main thread");

  for (auto& l : mActiveListeners.Clone()) {
    if (removedDeviceID.Equals(l->GetDevice()->RawID())) {
      l->Stop();
    }
  }
}

void GetUserMediaWindowListener::MuteOrUnmuteCameras(bool aMute) {
  MOZ_ASSERT(NS_IsMainThread(), "Only call on main thread");

  if (mCamerasAreMuted == aMute) {
    return;
  }
  mCamerasAreMuted = aMute;

  for (auto& l : mActiveListeners.Clone()) {
    if (l->GetDevice()->Kind() == MediaDeviceKind::Videoinput) {
      l->MuteOrUnmuteCamera(aMute);
    }
  }
}

void GetUserMediaWindowListener::MuteOrUnmuteMicrophones(bool aMute) {
  MOZ_ASSERT(NS_IsMainThread(), "Only call on main thread");

  if (mMicrophonesAreMuted == aMute) {
    return;
  }
  mMicrophonesAreMuted = aMute;

  for (auto& l : mActiveListeners.Clone()) {
    if (l->GetDevice()->Kind() == MediaDeviceKind::Audioinput) {
      l->MuteOrUnmuteMicrophone(aMute);
    }
  }
}

void GetUserMediaWindowListener::ChromeAffectingStateChanged() {
  MOZ_ASSERT(NS_IsMainThread());

  // We wait until stable state before notifying chrome so chrome only does
  // one update if more updates happen in this event loop.

  if (mChromeNotificationTaskPosted) {
    return;
  }

  nsCOMPtr<nsIRunnable> runnable =
      NewRunnableMethod("GetUserMediaWindowListener::NotifyChrome", this,
                        &GetUserMediaWindowListener::NotifyChrome);
  nsContentUtils::RunInStableState(runnable.forget());
  mChromeNotificationTaskPosted = true;
}

void GetUserMediaWindowListener::NotifyChrome() {
  MOZ_ASSERT(mChromeNotificationTaskPosted);
  mChromeNotificationTaskPosted = false;

  NS_DispatchToMainThread(NS_NewRunnableFunction(
      "MediaManager::NotifyChrome", [windowID = mWindowID]() {
        auto* window = nsGlobalWindowInner::GetInnerWindowWithId(windowID);
        if (!window) {
          MOZ_ASSERT_UNREACHABLE("Should have window");
          return;
        }

        nsresult rv = MediaManager::NotifyRecordingStatusChange(window);
        if (NS_FAILED(rv)) {
          MOZ_ASSERT_UNREACHABLE("Should be able to notify chrome");
          return;
        }
      }));
}

#undef LOG

}  // namespace mozilla
