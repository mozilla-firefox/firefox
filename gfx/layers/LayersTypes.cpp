/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "LayersTypes.h"

#include <cinttypes>

#include "mozilla/gfx/gfxVars.h"
#include "nsPrintfCString.h"

#ifdef XP_WIN
#  include "gfxConfig.h"
#  include "mozilla/StaticPrefs_gfx.h"
#endif

namespace mozilla {
namespace layers {

const char* kCompositionPayloadTypeNames[kCompositionPayloadTypeCount] = {
    "KeyPress",
    "APZScroll",
    "APZPinchZoom",
    "ContentPaint",
    "MouseUpFollowedByClick",
};

const char* GetLayersBackendName(LayersBackend aBackend) {
  switch (aBackend) {
    case LayersBackend::LAYERS_NONE:
      return "none";
    case LayersBackend::LAYERS_WR:
      if (gfx::gfxVars::UseSoftwareWebRender()) {
#ifdef XP_WIN
        if (gfx::gfxVars::AllowSoftwareWebRenderD3D11() &&
            gfx::gfxConfig::IsEnabled(gfx::Feature::D3D11_COMPOSITING)) {
          return "webrender_software_d3d11";
        }
#endif
        return "webrender_software";
      }
      return "webrender";
    default:
      MOZ_ASSERT_UNREACHABLE("unknown layers backend");
      return "unknown";
  }
}

std::ostream& operator<<(std::ostream& aStream, const LayersId& aId) {
  return aStream << nsPrintfCString("0x%" PRIx64, aId.mId).get();
}

/* static */
CompositableHandle CompositableHandle::GetNext() {
  static std::atomic<uint64_t> sCounter = 0;
  return CompositableHandle{++sCounter};
}

/* static */
RemoteTextureId RemoteTextureId::GetNext() {
  static std::atomic<uint64_t> sCounter = 0;
  return RemoteTextureId{++sCounter};
}

/* static */
RemoteTextureOwnerId RemoteTextureOwnerId::GetNext() {
  static std::atomic<uint64_t> sCounter = 0;
  return RemoteTextureOwnerId{++sCounter};
}

/* static */
SurfaceDescriptorRemoteDecoderId SurfaceDescriptorRemoteDecoderId::GetNext() {
  static std::atomic<uint64_t> sCounter = 0;
  return SurfaceDescriptorRemoteDecoderId{++sCounter};
}

/* static */
GpuProcessTextureId GpuProcessTextureId::GetNext() {
  if (!XRE_IsGPUProcess()) {
    MOZ_ASSERT_UNREACHABLE("unexpected to be called");
    return GpuProcessTextureId{};
  }

  static std::atomic<uint64_t> sCounter = 0;
  return GpuProcessTextureId{++sCounter};
}

/* static */
CompositeProcessFencesHolderId CompositeProcessFencesHolderId::GetNext() {
  if (!XRE_IsGPUProcess() && !XRE_IsParentProcess()) {
    MOZ_ASSERT_UNREACHABLE("unexpected to be called");
    return CompositeProcessFencesHolderId{};
  }

  static std::atomic<uint64_t> sCounter = 0;
  return CompositeProcessFencesHolderId{++sCounter};
}

/* static */
GpuProcessAndroidImageReaderId GpuProcessAndroidImageReaderId::GetNext() {
  if (!XRE_IsGPUProcess()) {
    MOZ_ASSERT_UNREACHABLE("unexpected to be called");
    return GpuProcessAndroidImageReaderId{};
  }

  static std::atomic<uint64_t> sCounter = 0;
  return GpuProcessAndroidImageReaderId{++sCounter};
}

/* static */
AndroidMediaCodecFrameId AndroidMediaCodecFrameId::GetNext() {
  static std::atomic<uint64_t> sCounter = 0;
  return AndroidMediaCodecFrameId{++sCounter};
}

std::ostream& operator<<(std::ostream& os, ScrollDirection aDirection) {
  switch (aDirection) {
    case ScrollDirection::eHorizontal:
      os << "horizontal";
      break;
    case ScrollDirection::eVertical:
      os << "vertical";
      break;
  }
  return os;
}

}  // namespace layers
}  // namespace mozilla
