/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef MOZILLA_MEDIAMANAGER_H
#define MOZILLA_MEDIAMANAGER_H

#include "DOMMediaStream.h"
#include "MediaEnginePrefs.h"
#include "MediaEventSource.h"
#include "PerformanceRecorder.h"
#include "PrincipalHandle.h"
#include "mozilla/Attributes.h"
#include "mozilla/Logging.h"
#include "mozilla/Preferences.h"
#include "mozilla/StaticPtr.h"
#include "mozilla/dom/GetUserMediaRequest.h"
#include "mozilla/dom/MediaStreamBinding.h"
#include "mozilla/dom/MediaStreamError.h"
#include "mozilla/dom/MediaStreamTrackBinding.h"
#include "mozilla/dom/MediaTrackCapabilitiesBinding.h"
#include "mozilla/dom/NavigatorBinding.h"
#include "mozilla/media/MediaChild.h"
#include "mozilla/media/MediaParent.h"
#include "nsClassHashtable.h"
#include "nsHashKeys.h"
#include "nsIMediaDevice.h"
#include "nsIMediaManager.h"
#include "nsIMemoryReporter.h"
#include "nsIObserver.h"
#include "nsRefPtrHashtable.h"
#include "nsXULAppAPI.h"

#ifdef MOZ_WEBRTC
#  include "transport/runnable_utils.h"
#endif

class AudioDeviceInfo;
class nsIPrefBranch;

#ifdef MOZ_WEBRTC
class WebrtcLogSinkHandle;
#endif

namespace mozilla {
class MediaEngine;
class MediaEngineSource;
class TaskQueue;
class MediaTrack;
template <typename T>
class MediaTimer;
namespace dom {
struct AudioOutputOptions;
struct MediaStreamConstraints;
struct MediaTrackConstraints;
struct MediaTrackConstraintSet;
struct MediaTrackSettings;
enum class CallerType : uint32_t;
enum class MediaDeviceKind : uint8_t;
}  // namespace dom

namespace ipc {
class PrincipalInfo;
}

class GetUserMediaTask;
class GetUserMediaWindowListener;
class MediaManager;
class DeviceListener;

/**
 * Device info that is independent of any Window.
 * MediaDevices can be shared, unlike LocalMediaDevices.
 */
class MediaDevice final {
 public:
  NS_INLINE_DECL_THREADSAFE_REFCOUNTING(MediaDevice)

  /**
   * Whether source device does end-run around cross origin restrictions.
   */
  enum class IsScary { No, Yes };

  /**
   * Whether source device can use OS level selection prompt
   */
  enum class OsPromptable { No, Yes };

  MediaDevice(MediaEngine* aEngine, dom::MediaSourceEnum aMediaSource,
              const nsString& aRawName, const nsString& aRawID,
              const nsString& aRawGroupID, IsScary aIsScary,
              const OsPromptable canRequestOsLevelPrompt);

  MediaDevice(MediaEngine* aEngine,
              const RefPtr<AudioDeviceInfo>& aAudioDeviceInfo,
              const nsString& aRawID);

  static RefPtr<MediaDevice> CopyWithNewRawGroupId(
      const RefPtr<MediaDevice>& aOther, const nsString& aRawGroupID);

  dom::MediaSourceEnum GetMediaSource() const;

 protected:
  ~MediaDevice();

 public:
  const RefPtr<MediaEngine> mEngine;
  const RefPtr<AudioDeviceInfo> mAudioDeviceInfo;
  const dom::MediaSourceEnum mMediaSource;
  const dom::MediaDeviceKind mKind;
  const bool mScary;
  const bool mCanRequestOsLevelPrompt;
  const bool mIsFake;
  const nsString mType;
  const nsString mRawID;
  const nsString mRawGroupID;
  const nsString mRawName;
};

/**
 * Device info that is specific to a particular Window.  If the device is a
 * source device, then a single corresponding MediaEngineSource is provided,
 * which can provide a maximum of one capture stream.  LocalMediaDevices are
 * not shared, but APIs returning LocalMediaDevices return a new object each
 * call.
 */
class LocalMediaDevice final : public nsIMediaDevice {
 public:
  NS_DECL_THREADSAFE_ISUPPORTS
  NS_DECL_NSIMEDIADEVICE

  LocalMediaDevice(RefPtr<const MediaDevice> aRawDevice, const nsString& aID,
                   const nsString& aGroupID, const nsString& aName);

  uint32_t GetBestFitnessDistance(
      const nsTArray<const NormalizedConstraintSet*>& aConstraintSets,
      const MediaEnginePrefs& aPrefs, dom::CallerType aCallerType);

  nsresult Allocate(const dom::MediaTrackConstraints& aConstraints,
                    const MediaEnginePrefs& aPrefs, uint64_t aWindowId,
                    const char** aOutBadConstraint);
  void SetTrack(const RefPtr<mozilla::MediaTrack>& aTrack,
                const PrincipalHandle& aPrincipalHandle);
  nsresult Start();
  nsresult Reconfigure(const dom::MediaTrackConstraints& aConstraints,
                       const MediaEnginePrefs& aPrefs,
                       const char** aOutBadConstraint);
  nsresult FocusOnSelectedSource();
  nsresult Stop();
  nsresult Deallocate();

  /**
   * Clones the LocalMediaDevice and sets a cloned source.
   */
  already_AddRefed<LocalMediaDevice> Clone() const;

  void GetSettings(dom::MediaTrackSettings& aOutSettings);
  void GetCapabilities(dom::MediaTrackCapabilities& aOutCapabilities);
  MediaEngineSource* Source();
  const TrackingId& GetTrackingId() const;
  // Returns null if not a physical audio device.
  AudioDeviceInfo* GetAudioDeviceInfo() const {
    return mRawDevice->mAudioDeviceInfo;
  }
  dom::MediaSourceEnum GetMediaSource() const {
    return mRawDevice->GetMediaSource();
  }
  dom::MediaDeviceKind Kind() const { return mRawDevice->mKind; }
  bool IsFake() const { return mRawDevice->mIsFake; }
  const nsString& RawID() { return mRawDevice->mRawID; }
  const dom::MediaTrackConstraints& Constraints() const;

 private:
  virtual ~LocalMediaDevice() = default;

  static uint32_t FitnessDistance(
      nsString aN,
      const dom::OwningStringOrStringSequenceOrConstrainDOMStringParameters&
          aConstraint);

  static bool StringsContain(const dom::OwningStringOrStringSequence& aStrings,
                             nsString aN);
  static uint32_t FitnessDistance(
      nsString aN, const dom::ConstrainDOMStringParameters& aParams);

 public:
  const RefPtr<const MediaDevice> mRawDevice;
  const nsString mName;
  const nsString mID;
  const nsString mGroupID;

 private:
  RefPtr<MediaEngineSource> mSource;
  // Currently applied constraints. Media thread only.
  dom::MediaTrackConstraints mConstraints;
};

typedef nsRefPtrHashtable<nsUint64HashKey, GetUserMediaWindowListener>
    WindowTable;

class MediaManager final : public nsIMediaManagerService,
                           public nsIMemoryReporter,
                           public nsIObserver {
  friend DeviceListener;

 public:
  static already_AddRefed<MediaManager> GetInstance();

  // NOTE: never NS_DispatchAndSpinEventLoopUntilComplete to the MediaManager
  // thread from the MainThread, as we NS_DispatchAndSpinEventLoopUntilComplete
  // to MainThread from MediaManager thread.
  static MediaManager* Get();
  static MediaManager* GetIfExists();
  static void Dispatch(already_AddRefed<Runnable> task);

  /**
   * Posts an async operation to the media manager thread.
   * FunctionType must be a function that takes a `MozPromiseHolder&`.
   *
   * The returned promise is resolved or rejected by aFunction on the media
   * manager thread.
   */
  template <typename MozPromiseType, typename FunctionType>
  static RefPtr<MozPromiseType> Dispatch(StaticString aName,
                                         FunctionType&& aFunction);

#ifdef DEBUG
  static bool IsInMediaThread();
#endif

  static bool Exists() { return !!GetIfExists(); }

  static nsresult NotifyRecordingStatusChange(nsPIDOMWindowInner* aWindow);

  NS_DECL_THREADSAFE_ISUPPORTS
  NS_DECL_NSIOBSERVER
  NS_DECL_NSIMEMORYREPORTER
  NS_DECL_NSIMEDIAMANAGERSERVICE

  media::Parent<media::NonE10s>* GetNonE10sParent();

  // If the window has not been destroyed, then return the
  // GetUserMediaWindowListener for this window.
  // If the window has been destroyed, then return null.
  RefPtr<GetUserMediaWindowListener> GetOrMakeWindowListener(
      nsPIDOMWindowInner* aWindow);
  WindowTable* GetActiveWindows() {
    MOZ_ASSERT(NS_IsMainThread());
    return &mActiveWindows;
  }
  GetUserMediaWindowListener* GetWindowListener(uint64_t aWindowId) {
    MOZ_ASSERT(NS_IsMainThread());
    return mActiveWindows.GetWeak(aWindowId);
  }
  void AddWindowID(uint64_t aWindowId,
                   RefPtr<GetUserMediaWindowListener> aListener);
  void RemoveWindowID(uint64_t aWindowId);
  void SendPendingGUMRequest();
  bool IsWindowStillActive(uint64_t aWindowId) {
    return !!GetWindowListener(aWindowId);
  }
  bool IsWindowListenerStillActive(
      const RefPtr<GetUserMediaWindowListener>& aListener);

  static bool IsOn(const dom::OwningBooleanOrMediaTrackConstraints& aUnion) {
    return !aUnion.IsBoolean() || aUnion.GetAsBoolean();
  }
  using GetUserMediaSuccessCallback = dom::NavigatorUserMediaSuccessCallback;
  using GetUserMediaErrorCallback = dom::NavigatorUserMediaErrorCallback;

  MOZ_CAN_RUN_SCRIPT
  static void CallOnError(GetUserMediaErrorCallback& aCallback,
                          dom::MediaStreamError& aError);
  MOZ_CAN_RUN_SCRIPT
  static void CallOnSuccess(GetUserMediaSuccessCallback& aCallback,
                            DOMMediaStream& aTrack);

  using MediaDeviceSet = nsTArray<RefPtr<MediaDevice>>;
  using MediaDeviceSetRefCnt = media::Refcountable<MediaDeviceSet>;
  using LocalMediaDeviceSet = nsTArray<RefPtr<LocalMediaDevice>>;
  using LocalMediaDeviceSetRefCnt = media::Refcountable<LocalMediaDeviceSet>;

  using StreamPromise =
      MozPromise<RefPtr<DOMMediaStream>, RefPtr<MediaMgrError>, true>;
  using DeviceSetPromise =
      MozPromise<RefPtr<MediaDeviceSetRefCnt>, RefPtr<MediaMgrError>, true>;
  using ConstDeviceSetPromise = MozPromise<RefPtr<const MediaDeviceSetRefCnt>,
                                           RefPtr<MediaMgrError>, true>;
  using LocalDevicePromise =
      MozPromise<RefPtr<LocalMediaDevice>, RefPtr<MediaMgrError>, true>;
  using LocalDeviceSetPromise = MozPromise<RefPtr<LocalMediaDeviceSetRefCnt>,
                                           RefPtr<MediaMgrError>, true>;
  using MgrPromise = MozPromise<bool, RefPtr<MediaMgrError>, true>;

  RefPtr<StreamPromise> GetUserMedia(
      nsPIDOMWindowInner* aWindow,
      const dom::MediaStreamConstraints& aConstraints,
      dom::CallerType aCallerType);

  RefPtr<LocalDevicePromise> SelectAudioOutput(
      nsPIDOMWindowInner* aWindow, const dom::AudioOutputOptions& aOptions,
      dom::CallerType aCallerType);

  // Return the list of microphone, camera, and speaker devices.
  // MediaDeviceSets provided on promise resolution are shared between
  // callers and so cannot be modified.
  RefPtr<ConstDeviceSetPromise> GetPhysicalDevices();

  void OnNavigation(uint64_t aWindowID);
  void OnCameraMute(bool aMute);
  void OnMicrophoneMute(bool aMute);
  bool IsActivelyCapturingOrHasAPermission(uint64_t aWindowId);

  MediaEventSource<void>& DeviceListChangeEvent() {
    return mDeviceListChangeEvent;
  }
  RefPtr<LocalDeviceSetPromise> AnonymizeDevices(
      nsPIDOMWindowInner* aWindow, RefPtr<const MediaDeviceSetRefCnt> aDevices);

  MediaEnginePrefs mPrefs;

 private:
  static nsresult GenerateUUID(nsAString& aResult);

 public:
  /**
   * This function tries to guess the group id for a video device in aDevices
   * based on the device name. If the name of only one audio device in aAudios
   * contains the name of the video device, then, this video device will take
   * the group id of the audio device. Since this is a guess we try to minimize
   * the probability of false positive. If we fail to find a correlation we
   * leave the video group id untouched. In that case the group id will be the
   * video device name.
   */
  static void GuessVideoDeviceGroupIDs(MediaDeviceSet& aDevices,
                                       const MediaDeviceSet& aAudios);

 private:
  enum class EnumerationFlag {
    AllowPermissionRequest,
    EnumerateAudioOutputs,
    ForceFakes,
  };
  using EnumerationFlags = EnumSet<EnumerationFlag>;

  enum class DeviceType { Real, Fake };

  struct DeviceEnumerationParams {
    DeviceEnumerationParams(dom::MediaSourceEnum aInputType, DeviceType aType,
                            nsAutoCString aForcedDeviceName);
    dom::MediaSourceEnum mInputType;
    DeviceType mType;
    nsAutoCString mForcedDeviceName;
  };

  struct VideoDeviceEnumerationParams : public DeviceEnumerationParams {
    VideoDeviceEnumerationParams(dom::MediaSourceEnum aInputType,
                                 DeviceType aType,
                                 nsAutoCString aForcedDeviceName,
                                 nsAutoCString aForcedMicrophoneName);

    // The by-pref forced microphone device name, used for groupId correlation
    // of camera devices.
    nsAutoCString mForcedMicrophoneName;
  };

  struct EnumerationParams {
    EnumerationParams(EnumerationFlags aFlags,
                      Maybe<VideoDeviceEnumerationParams> aVideo,
                      Maybe<DeviceEnumerationParams> aAudio);
    bool HasFakeCams() const;
    bool HasFakeMics() const;
    bool RealDeviceRequested() const;
    dom::MediaSourceEnum VideoInputType() const;
    dom::MediaSourceEnum AudioInputType() const;
    EnumerationFlags mFlags;
    Maybe<VideoDeviceEnumerationParams> mVideo;
    Maybe<DeviceEnumerationParams> mAudio;
  };

  static EnumerationParams CreateEnumerationParams(
      dom::MediaSourceEnum aVideoInputType,
      dom::MediaSourceEnum aAudioInputType, EnumerationFlags aFlags);

  RefPtr<LocalDeviceSetPromise> EnumerateDevicesImpl(
      nsPIDOMWindowInner* aWindow, EnumerationParams aParams);

  RefPtr<DeviceSetPromise> MaybeRequestPermissionAndEnumerateRawDevices(
      EnumerationParams aParams);

  static RefPtr<MediaDeviceSetRefCnt> EnumerateRawDevices(
      EnumerationParams aParams);

  RefPtr<LocalDeviceSetPromise> SelectSettings(
      const dom::MediaStreamConstraints& aConstraints,
      dom::CallerType aCallerType, RefPtr<LocalMediaDeviceSetRefCnt> aDevices);

  nsresult GetPref(nsIPrefBranch* aBranch, const char* aPref, const char* aData,
                   int32_t* aVal);
  nsresult GetPrefBool(nsIPrefBranch* aBranch, const char* aPref,
                       const char* aData, bool* aVal);
  void GetPrefs(nsIPrefBranch* aBranch, const char* aData);

  // Make private because we want only one instance of this class
  explicit MediaManager(already_AddRefed<TaskQueue> aMediaThread);

  ~MediaManager() = default;
  void Shutdown();

  void StopScreensharing(uint64_t aWindowID);

  void RemoveMediaDevicesCallback(uint64_t aWindowID);
  void DeviceListChanged();
  void EnsureNoPlaceholdersInDeviceCache();
  void InvalidateDeviceCache();
  void HandleDeviceListChanged();

  // Returns the number of incomplete tasks associated with this window,
  // including the newly added task.
  size_t AddTaskAndGetCount(uint64_t aWindowID, const nsAString& aCallID,
                            RefPtr<GetUserMediaTask> aTask);
  // Finds the task corresponding to aCallID and removes it from tracking.
  RefPtr<GetUserMediaTask> TakeGetUserMediaTask(const nsAString& aCallID);
  // Intended for use with "media.navigator.permission.disabled" to bypass the
  // permission prompt and use the first appropriate device.
  void NotifyAllowed(const nsString& aCallID,
                     const LocalMediaDeviceSet& aDevices);

  // Media thread only
  MediaEngine* GetBackend();

  MOZ_DEFINE_MALLOC_SIZE_OF(MallocSizeOf);

  // ONLY access from MainThread so we don't need to lock
  WindowTable mActiveWindows;
  nsRefPtrHashtable<nsStringHashKey, GetUserMediaTask> mActiveCallbacks;
  nsClassHashtable<nsUint64HashKey, nsTArray<nsString>> mCallIds;
  nsTArray<RefPtr<dom::GetUserMediaRequest>> mPendingGUMRequest;
#ifdef MOZ_WEBRTC
  RefPtr<WebrtcLogSinkHandle> mLogHandle;
#endif
  // non-null if a device enumeration is in progress and was started after the
  // last device-change invalidation
  RefPtr<media::Refcountable<nsTArray<MozPromiseHolder<ConstDeviceSetPromise>>>>
      mPendingDevicesPromises;
  RefPtr<MediaDeviceSetRefCnt> mPhysicalDevices;
  TimeStamp mUnhandledDeviceChangeTime;
  RefPtr<MediaTimer<TimeStamp>> mDeviceChangeTimer;
  bool mCamerasMuted = false;
  bool mMicrophonesMuted = false;

 public:
  // Always exists
  const RefPtr<TaskQueue> mMediaThread;

 private:
  nsCOMPtr<nsIAsyncShutdownBlocker> mShutdownBlocker;

  // ONLY accessed from MediaManagerThread
  RefPtr<MediaEngine> mBackend;

  // Accessed only on main thread and mMediaThread.
  // Set before mMediaThread is created, and cleared on main thread after last
  // mMediaThread task is run.
  static StaticRefPtr<MediaManager> sSingleton;

  // Connect/Disconnect on media thread only
  MediaEventListener mDeviceListChangeListener;

  MediaEventProducer<void> mDeviceListChangeEvent;

 public:
  RefPtr<media::Parent<media::NonE10s>> mNonE10sParent;
};

}  // namespace mozilla

#endif  // MOZILLA_MEDIAMANAGER_H
