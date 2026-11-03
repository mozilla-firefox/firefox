/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "VRLayerChild.h"

#include "ClientWebGLContext.h"
#include "GLContext.h"
#include "GLScreenBuffer.h"
#include "SharedSurface.h"    // for SharedSurface
#include "SharedSurfaceGL.h"  // for SharedSurface
#include "gfxPlatform.h"
#include "mozilla/StaticPrefs_dom.h"
#include "mozilla/dom/HTMLCanvasElement.h"
#include "mozilla/layers/ImageBridgeChild.h"
#include "mozilla/layers/LayersMessages.h"  // for TimedTexture
#include "mozilla/layers/SyncObject.h"      // for SyncObjectClient

namespace mozilla::gfx {

VRLayerChild::VRLayerChild() { MOZ_COUNT_CTOR(VRLayerChild); }

VRLayerChild::~VRLayerChild() {
  ClearSurfaces();

  MOZ_COUNT_DTOR(VRLayerChild);
}

void VRLayerChild::Initialize(dom::HTMLCanvasElement* aCanvasElement,
                              const gfx::Rect& aLeftEyeRect,
                              const gfx::Rect& aRightEyeRect) {
  MOZ_RELEASE_ASSERT(aCanvasElement);
  mLeftEyeRect = aLeftEyeRect;
  mRightEyeRect = aRightEyeRect;
  mCanvasElement = aCanvasElement;
}

void VRLayerChild::SetXRFramebuffer(WebGLFramebufferJS* fb) {
  mFramebuffer = fb;
}

static constexpr bool kIsAndroid =
#if defined(MOZ_WIDGET_ANDROID)
    true;
#else
    false;
#endif

void VRLayerChild::SubmitFrame(const VRDisplayInfo& aDisplayInfo) {
  uint64_t frameId = aDisplayInfo.GetFrameId();

  // aFrameId will not increment unless the previuosly submitted
  // frame was received by the VR thread and submitted to the VR
  // compositor.  We early-exit here in the event that SubmitFrame
  // was called twice for the same aFrameId.
  if (!mCanvasElement || frameId == mLastSubmittedFrameId) {
    return;
  }

  const auto& webgl = mCanvasElement->GetWebGLContext();
  if (!webgl) return;

  // Keep the SharedSurfaceTextureClient alive long enough for
  // 1 extra frame, accomodating overlapped asynchronous rendering.
  mLastFrameTextureDesc = mThisFrameTextureDesc;

  bool getNewFrame = true;
  if (kIsAndroid) {
    /**
     * Do not blit WebGL to a SurfaceTexture until the last submitted frame is
     * already processed and the new frame poses are ready. SurfaceTextures need
     * to be released in the VR render thread in order to allow to be used again
     * in the WebGLContext GLScreenBuffer producer. Not doing so causes some
     * freezes, crashes or other undefined behaviour.
     */
    getNewFrame = (!mThisFrameTextureDesc ||
                   aDisplayInfo.mDisplayState.lastSubmittedFrameId ==
                       mLastSubmittedFrameId);
  }
  if (getNewFrame) {
    const RefPtr<layers::ImageBridgeChild> imageBridge =
        layers::ImageBridgeChild::GetSingleton();

    auto texType = layers::TextureType::Unknown;
    if (imageBridge) {
      texType = layers::PreferredCanvasTextureType(imageBridge);
    }

    webgl->Present(mFramebuffer, texType, true);
    mThisFrameTextureDesc = webgl->GetFrontBuffer(mFramebuffer, true);
  }

  mLastSubmittedFrameId = frameId;

  if (!mThisFrameTextureDesc) {
    gfxCriticalError() << "ToSurfaceDescriptor failed in "
                          "VRLayerChild::SubmitFrame";
    return;
  }

  SendSubmitFrame(*mThisFrameTextureDesc, frameId, mLeftEyeRect, mRightEyeRect);
}

void VRLayerChild::ClearSurfaces() {
  mThisFrameTextureDesc = Nothing();
  mLastFrameTextureDesc = Nothing();
  const auto& webgl = mCanvasElement->GetWebGLContext();
  if (!mFramebuffer && webgl) {
    webgl->ClearVRSwapChain();
  }
}

// static
already_AddRefed<VRLayerChild> VRLayerChild::CreateIPDLActor() {
  if (!StaticPrefs::dom_vr_enabled() && !StaticPrefs::dom_vr_webxr_enabled()) {
    return nullptr;
  }

  return MakeAndAddRef<VRLayerChild>();
}

}  // namespace mozilla::gfx
