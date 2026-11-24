/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef MOZILLA_GFX_VR_VRMANAGERCHILD_H
#define MOZILLA_GFX_VR_VRMANAGERCHILD_H

#include "mozilla/Attributes.h"
#include "mozilla/dom/AnimationFrameProviderBinding.h"
#include "mozilla/dom/WebXRBinding.h"
#include "mozilla/dom/XRFrame.h"
#include "mozilla/gfx/PVRManagerChild.h"
#include "mozilla/layers/ISurfaceAllocator.h"  // for ISurfaceAllocator
#include "mozilla/layers/LayersTypes.h"        // for LayersBackend
#include "nsISupportsImpl.h"
#include "nsXULAppAPI.h"

namespace mozilla {
namespace dom {
class Promise;
class GamepadManager;
class Navigator;
class VRDisplay;
class FrameRequestCallback;
}  // namespace dom
namespace gfx {
class VRLayerChild;
class VRDisplayClient;

class VRManagerEventObserver {
 public:
  NS_INLINE_DECL_PURE_VIRTUAL_REFCOUNTING
  virtual void NotifyVRDisplayMounted(uint32_t aDisplayID) = 0;
  virtual void NotifyVRDisplayUnmounted(uint32_t aDisplayID) = 0;
  virtual void NotifyVRDisplayConnect(uint32_t aDisplayID) = 0;
  virtual void NotifyVRDisplayDisconnect(uint32_t aDisplayID) = 0;
  virtual void NotifyVRDisplayPresentChange(uint32_t aDisplayID) = 0;
  virtual void NotifyPresentationGenerationChanged(uint32_t aDisplayID) = 0;
  virtual bool GetStopActivityStatus() const = 0;
  virtual void NotifyEnumerationCompleted() = 0;
  virtual void NotifyDetectRuntimesCompleted() = 0;

 protected:
  virtual ~VRManagerEventObserver() = default;
};

class VRManagerChild : public PVRManagerChild {
  friend class PVRManagerChild;

 public:
  NS_INLINE_DECL_THREADSAFE_REFCOUNTING(VRManagerChild, override);

  static VRManagerChild* Get();

  // Indicate that an observer wants to receive VR events.
  void AddListener(VRManagerEventObserver* aObserver);
  // Indicate that an observer should no longer receive VR events.
  void RemoveListener(VRManagerEventObserver* aObserver);
  void StartActivity();
  void StopActivity();
  bool RuntimeSupportsVR() const;
  bool RuntimeSupportsAR() const;
  bool RuntimeSupportsInline() const;

  void GetVRDisplays(nsTArray<RefPtr<VRDisplayClient>>& aDisplays);
  bool RefreshVRDisplaysWithCallback(uint64_t aWindowId);
  bool EnumerateVRDisplays();
  void DetectRuntimes();
  void AddPromise(const uint32_t& aID, dom::Promise* aPromise);
  gfx::VRAPIMode GetVRAPIMode(uint32_t aDisplayID) const;

  static void InitSameProcess(uint32_t aNamespace);
  static void InitWithGPUProcess(Endpoint<PVRManagerChild>&& aEndpoint,
                                 uint32_t aNamespace);
  static bool InitForContent(Endpoint<PVRManagerChild>&& aEndpoint,
                             uint32_t aNamespace);
  static void ShutDown();

  static bool IsCreated();
  static bool IsPresenting();
  static TimeStamp GetIdleDeadlineHint(TimeStamp aDefault);

  already_AddRefed<VRLayerChild> CreateVRLayer(uint32_t aDisplayID,
                                               uint32_t aGroup);

  static void IdentifyTextureHost(
      const layers::TextureFactoryIdentifier& aIdentifier);
  layers::LayersBackend GetBackendType() const;

  nsresult ScheduleFrameRequestCallback(dom::FrameRequestCallback& aCallback,
                                        int32_t* aHandle);
  void CancelFrameRequestCallback(int32_t aHandle);
  MOZ_CAN_RUN_SCRIPT
  void RunFrameRequestCallbacks();
  void NotifyPresentationGenerationChanged(uint32_t aDisplayID);

  MOZ_CAN_RUN_SCRIPT
  void UpdateDisplayInfo(const VRDisplayInfo& aDisplayInfo);
  void FireDOMVRDisplayMountedEvent(uint32_t aDisplayID);
  void FireDOMVRDisplayUnmountedEvent(uint32_t aDisplayID);
  void FireDOMVRDisplayConnectEvent(uint32_t aDisplayID);
  void FireDOMVRDisplayDisconnectEvent(uint32_t aDisplayID);
  void FireDOMVRDisplayPresentChangeEvent(uint32_t aDisplayID);
  void FireDOMVRDisplayConnectEventsForLoad(VRManagerEventObserver* aObserver);

  void HandleFatalError(const char* aMsg) override;
  void ActorDestroy(ActorDestroyReason aReason) override;

  void RunPuppet(const nsTArray<uint64_t>& aBuffer, dom::Promise* aPromise,
                 ErrorResult& aRv);
  void ResetPuppet(dom::Promise* aPromise, ErrorResult& aRv);

 protected:
  explicit VRManagerChild(uint32_t aNamespace);
  ~VRManagerChild();

  // MOZ_CAN_RUN_SCRIPT_BOUNDARY until we can mark ipdl-generated things as
  // MOZ_CAN_RUN_SCRIPT.
  MOZ_CAN_RUN_SCRIPT_BOUNDARY
  mozilla::ipc::IPCResult RecvUpdateDisplayInfo(
      const VRDisplayInfo& aDisplayInfo);
  mozilla::ipc::IPCResult RecvUpdateRuntimeCapabilities(
      const VRDisplayCapabilityFlags& aCapabilities);
  mozilla::ipc::IPCResult RecvReplyGamepadVibrateHaptic(
      const uint32_t& aPromiseID);

  MOZ_CAN_RUN_SCRIPT_BOUNDARY
  mozilla::ipc::IPCResult RecvNotifyPuppetCommandBufferCompleted(bool aSuccess);
  MOZ_CAN_RUN_SCRIPT_BOUNDARY
  mozilla::ipc::IPCResult RecvNotifyPuppetResetComplete();

  bool IsSameProcess() const { return OtherChildID() == XRE_GetChildID(); }

 private:
  void FireDOMVRDisplayMountedEventInternal(uint32_t aDisplayID);
  void FireDOMVRDisplayUnmountedEventInternal(uint32_t aDisplayID);
  void FireDOMVRDisplayConnectEventInternal(uint32_t aDisplayID);
  void FireDOMVRDisplayDisconnectEventInternal(uint32_t aDisplayID);
  void FireDOMVRDisplayPresentChangeEventInternal(uint32_t aDisplayID);
  void FireDOMVRDisplayConnectEventsForLoadInternal(
      uint32_t aDisplayID, VRManagerEventObserver* aObserver);
  void NotifyPresentationGenerationChangedInternal(uint32_t aDisplayID);
  void NotifyEnumerationCompletedInternal();
  void NotifyRuntimeCapabilitiesUpdatedInternal();

  uint32_t mNamespace;

  nsTArray<RefPtr<VRDisplayClient>> mDisplays;
  VRDisplayCapabilityFlags mRuntimeCapabilities;
  bool mDisplaysInitialized = false;
  nsTArray<uint64_t> mNavigatorCallbacks;

  struct XRFrameRequest {
    XRFrameRequest(mozilla::dom::FrameRequestCallback& aCallback,
                   int32_t aHandle)
        : mCallback(&aCallback), mHandle(aHandle) {}

    XRFrameRequest(mozilla::dom::XRFrameRequestCallback& aCallback,
                   mozilla::dom::XRFrame& aFrame, int32_t aHandle)
        : mXRCallback(&aCallback), mXRFrame(&aFrame), mHandle(aHandle) {}
    MOZ_CAN_RUN_SCRIPT
    void Call(const DOMHighResTimeStamp& aTimeStamp);

    // Comparator operators to allow RemoveElementSorted with an
    // integer argument on arrays of XRFrameRequest
    bool operator==(int32_t aHandle) const { return mHandle == aHandle; }
    bool operator<(int32_t aHandle) const { return mHandle < aHandle; }

    RefPtr<mozilla::dom::FrameRequestCallback> mCallback;
    RefPtr<mozilla::dom::XRFrameRequestCallback> mXRCallback;
    RefPtr<mozilla::dom::XRFrame> mXRFrame;
    int32_t mHandle;
  };

  nsTArray<XRFrameRequest> mFrameRequestCallbacks;
  /**
   * The current frame request callback handle
   */
  int32_t mFrameRequestCallbackCounter;
  mozilla::TimeStamp mStartTimeStamp;

  nsTArray<RefPtr<VRManagerEventObserver>> mListeners;
  bool mWaitingForEnumeration;

  layers::LayersBackend mBackend;
  nsRefPtrHashtable<nsUint32HashKey, dom::Promise> mGamepadPromiseList;
  RefPtr<dom::Promise> mRunPuppetPromise;
  nsTArray<RefPtr<dom::Promise>> mResetPuppetPromises;

  DISALLOW_COPY_AND_ASSIGN(VRManagerChild);
};

}  // namespace gfx
}  // namespace mozilla

#endif  // MOZILLA_GFX_VR_VRMANAGERCHILD_H
