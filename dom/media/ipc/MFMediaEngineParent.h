/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef DOM_MEDIA_IPC_MFMEDIAENGINEPARENT_H_
#define DOM_MEDIA_IPC_MFMEDIAENGINEPARENT_H_

#include <mfidl.h>
#include <winnt.h>
#include <wrl.h>

#include "MFMediaEngineExtra.h"
#include "MFMediaEngineNotify.h"
#include "MFMediaEngineUtils.h"
#include "MFMediaSource.h"
#include "MediaInfo.h"
#include "PlatformDecoderModule.h"
#include "mozilla/MozPromise.h"
#include "mozilla/PMFMediaEngineParent.h"

namespace mozilla {

class MFCDMProxy;
class MFContentProtectionManager;
class MFMediaEngineExtension;
class MFMediaEngineStreamWrapper;
class MFMediaSource;
class RemoteMediaManagerParent;

/**
 * MFMediaEngineParent is a wrapper class for a MediaEngine in the MF-CDM
 * process. It's responsible to create the media engine and its related classes,
 * such as a custom media source, media engine extension, media engine
 * notify...e.t.c It communicates with MFMediaEngineChild in the content process
 * to receive commands and direct them to the media engine.
 * https://docs.microsoft.com/en-us/windows/win32/api/mfmediaengine/nn-mfmediaengine-imfmediaengine
 */
class MFMediaEngineParent final : public PMFMediaEngineParent {
 public:
  NS_INLINE_DECL_THREADSAFE_REFCOUNTING(MFMediaEngineParent, final);
  MFMediaEngineParent(RemoteMediaManagerParent* aManager,
                      nsISerialEventTarget* aManagerThread);

  using TrackType = TrackInfo::TrackType;

  static already_AddRefed<MFMediaEngineParent> GetMediaEngineById(uint64_t aId);

  // Return true if aId has been allocated and its engine, if still alive, was
  // created by aManager.
  static bool IsMediaEngineIdAllowed(uint64_t aId,
                                     const RemoteMediaManagerParent* aManager);

  MFMediaEngineStreamWrapper* GetMediaEngineStream(
      TrackType aType, const CreateDecoderParams& aParam);

  uint64_t Id() const { return mMediaEngineId; }

  // Methods for PMFMediaEngineParent
  mozilla::ipc::IPCResult RecvInitMediaEngine(
      const MediaEngineInfoIPDL& aInfo, InitMediaEngineResolver&& aResolver);
  mozilla::ipc::IPCResult RecvPlay();
  mozilla::ipc::IPCResult RecvPause();
  mozilla::ipc::IPCResult RecvSeek(double aTargetTimeInSecond);
  mozilla::ipc::IPCResult RecvSetCDMProxyId(uint64_t aProxyId);
  mozilla::ipc::IPCResult RecvSetVolume(double aVolume);
  mozilla::ipc::IPCResult RecvSetPlaybackRate(double aPlaybackRate);
  mozilla::ipc::IPCResult RecvSetLooping(bool aLooping);
  mozilla::ipc::IPCResult RecvNotifyEndOfStream(TrackInfo::TrackType aType);
  mozilla::ipc::IPCResult RecvShutdown();

  void ActorDestroy(ActorDestroyReason aWhy) override;

 private:
  ~MFMediaEngineParent();

  void CreateMediaEngine();
  HRESULT SetMediaInfo(const MediaInfoIPDL& aInfo, bool aIsEncryptedCustomInit);

  void InitializeDXGIDeviceManager();

  void AssertOnManagerThread() const;

  void HandleMediaEngineEvent(MFMediaEngineEventWrapper aEvent);
  void HandleRequestSample(const SampleRequest& aRequest);

  void NotifyError(MF_MEDIA_ENGINE_ERR aError, HRESULT aResult = 0);

  static bool IsHardwareResetHRESULT(HRESULT aResult);

  void DestroyEngineIfExists(const Maybe<MediaResult>& aError = Nothing());

  void EnsureDcompSurfaceHandle();

  void UpdateStatisticsData();

  void SetMediaSourceOnEngine();

  Maybe<gfx::IntSize> DetectVideoSizeChange();
  void NotifyVideoResizing();

#ifdef MOZ_WMF_CDM
  // We will disable HWDRM when receiving error MSPR_E_NO_DECRYPTOR_AVAILABLE.
  void NotifyDisableHWDRM();
#endif

  // This generates unique id for each MFMediaEngineParent instance, and it
  // would be increased monotonically.
  static inline uint64_t sMediaEngineIdx = 0;

  const uint64_t mMediaEngineId;

  const RefPtr<RemoteMediaManagerParent> mManager;
  const RefPtr<nsISerialEventTarget> mManagerThread;

  // Required classes for working with the media engine.
  Microsoft::WRL::ComPtr<IMFMediaEngine> mMediaEngine;
  Microsoft::WRL::ComPtr<MFMediaEngineNotify> mMediaEngineNotify;
  Microsoft::WRL::ComPtr<MFMediaEngineExtension> mMediaEngineExtension;
  Microsoft::WRL::ComPtr<MFMediaSource> mMediaSource;
#ifdef MOZ_WMF_CDM
  Microsoft::WRL::ComPtr<MFContentProtectionManager> mContentProtectionManager;
  Maybe<uint64_t> mProxyId;
#endif

  MediaEventListener mMediaEngineEventListener;
  MediaEventListener mRequestSampleListener;
  bool mIsCreatedMediaEngine = false;
  // True once InitMediaEngine has run; initialization happens once per actor.
  bool mIsMediaEngineInitialized = false;
  // Set to true when EnableWindowlessSwapchainMode succeeds during media source
  // setup. Guards DComp surface handle creation in EnsureDcompSurfaceHandle:
  // if false (e.g. when a CDM incompatible with windowless swap chain is
  // active), DComp setup is skipped and we fall back to frame-server mode.
  bool mDCompModeEnabled = false;
  bool mIsFrameServerMode = false;

  Microsoft::WRL::ComPtr<IMFDXGIDeviceManager> mDXGIDeviceManager;

  // These will be always zero for audio playback.
  DWORD mDisplayWidth = 0;
  DWORD mDisplayHeight = 0;

  float mPlaybackRate = 1.0;

#ifdef MOZ_WMF_CDM
  // Set while recovering from a GPU/DRM hardware context reset. During
  // recovery the whole MFMediaEngineParent actor is torn down and a new one
  // is created; this flag suppresses spurious errors that arrive in the
  // interim and would otherwise interrupt the reinit sequence. Only
  // access/modify on the manager thread.
  bool mHardwareResetInProgress = false;
  // Pending HDCP readiness check started at hardware reset time. Shared
  // across engine instances so the new engine created after recovery can
  // chain SetMediaSourceOnEngine() on the already-running check.
  static inline RefPtr<GenericPromise> sPendingHDCPCheck;
  MozPromiseRequestHolder<GenericPromise> mHDCPRequestHolder;
#endif

  // When flush happens inside the media engine, it will reset the statistic
  // data. Therefore, whenever the statistic data gets reset, we will use
  // `mCurrentPlaybackStatisticData` to track new data and store previous data
  // to `mPrevPlaybackStatisticData`. The sum of these two data is the total
  // statistic data for playback.
  StatisticData mCurrentPlaybackStatisticData;
  StatisticData mPrevPlaybackStatisticData;
};

}  // namespace mozilla

#endif  // DOM_MEDIA_IPC_MFMEDIAENGINEPARENT_H_
