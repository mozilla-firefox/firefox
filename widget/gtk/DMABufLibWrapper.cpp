/* -*- Mode: C; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim:expandtab:shiftwidth=2:tabstop=2:
 */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "DMABufLibWrapper.h"
#include "DMABufFormats.h"
#ifdef MOZ_WAYLAND
#  include "nsWaylandDisplay.h"
#endif
#include "base/message_loop.h"    // for MessageLoop
#include "mozilla/gfx/Logging.h"  // for gfxCriticalNote
#include "mozilla/StaticPrefs_widget.h"
#include "mozilla/StaticPrefs_media.h"
#include "mozilla/gfx/gfxVars.h"
#include "WidgetUtilsGtk.h"
#include "gfxConfig.h"
#include "nsIGfxInfo.h"
#include "GfxInfo.h"
#include "mozilla/Components.h"
#include "mozilla/ClearOnShutdown.h"

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <dlfcn.h>
#include <mutex>
#include <unistd.h>

using namespace mozilla::gfx;

#ifndef GBM_FORMAT_P010
#  define GBM_FORMAT_P010 \
    __gbm_fourcc_code('P', '0', '1', '0') /* 2x2 subsampled Cr:Cb plane */
#endif

namespace mozilla {
namespace widget {

bool sUseWebGLDmabufBackend = true;

#define GBMLIB_NAME "libgbm.so.1"
#define DRMLIB_NAME "libdrm.so.2"

// Use static lock to protect dri operation as
// gbm_dri.c is not thread safe.
// https://gitlab.freedesktop.org/mesa/mesa/-/issues/4422
mozilla::StaticMutex GbmLib::sDRILock MOZ_UNANNOTATED;

bool GbmLib::sLoaded = false;
void* GbmLib::sGbmLibHandle = nullptr;
void* GbmLib::sXf86DrmLibHandle = nullptr;
CreateDeviceFunc GbmLib::sCreateDevice;
DestroyDeviceFunc GbmLib::sDestroyDevice;
CreateFunc GbmLib::sCreate;
CreateWithModifiersFunc GbmLib::sCreateWithModifiers;
CreateWithModifiers2Func GbmLib::sCreateWithModifiers2;
GetModifierFunc GbmLib::sGetModifier;
GetStrideFunc GbmLib::sGetStride;
GetFdFunc GbmLib::sGetFd;
DestroyFunc GbmLib::sDestroy;
MapFunc GbmLib::sMap;
UnmapFunc GbmLib::sUnmap;
GetPlaneCountFunc GbmLib::sGetPlaneCount;
GetHandleForPlaneFunc GbmLib::sGetHandleForPlane;
GetStrideForPlaneFunc GbmLib::sGetStrideForPlane;
GetOffsetFunc GbmLib::sGetOffset;
DeviceIsFormatSupportedFunc GbmLib::sDeviceIsFormatSupported;
DrmPrimeHandleToFDFunc GbmLib::sDrmPrimeHandleToFD;
CreateSurfaceFunc GbmLib::sCreateSurface;
DestroySurfaceFunc GbmLib::sDestroySurface;

bool GbmLib::IsLoaded() {
  return sCreateDevice != nullptr && sDestroyDevice != nullptr &&
         sCreate != nullptr && sCreateWithModifiers != nullptr &&
         sGetModifier != nullptr && sGetStride != nullptr &&
         sGetFd != nullptr && sDestroy != nullptr && sMap != nullptr &&
         sUnmap != nullptr && sGetPlaneCount != nullptr &&
         sGetHandleForPlane != nullptr && sGetStrideForPlane != nullptr &&
         sGetOffset != nullptr && sDeviceIsFormatSupported != nullptr &&
         sDrmPrimeHandleToFD != nullptr && sCreateSurface != nullptr &&
         sDestroySurface != nullptr;
}

bool GbmLib::Load() {
  static bool sTriedToLoad = false;
  if (sTriedToLoad) {
    return sLoaded;
  }

  sTriedToLoad = true;

  MOZ_ASSERT(!sGbmLibHandle);
  MOZ_ASSERT(!sLoaded);

  LOGDMABUF(("Loading DMABuf system library %s ...\n", GBMLIB_NAME));

  sGbmLibHandle = dlopen(GBMLIB_NAME, RTLD_LAZY | RTLD_LOCAL);
  if (!sGbmLibHandle) {
    LOGDMABUF(("Failed to load %s, dmabuf isn't available.\n", GBMLIB_NAME));
    return false;
  }

  sCreateDevice = (CreateDeviceFunc)dlsym(sGbmLibHandle, "gbm_create_device");
  sDestroyDevice =
      (DestroyDeviceFunc)dlsym(sGbmLibHandle, "gbm_device_destroy");
  sCreate = (CreateFunc)dlsym(sGbmLibHandle, "gbm_bo_create");
  sCreateWithModifiers = (CreateWithModifiersFunc)dlsym(
      sGbmLibHandle, "gbm_bo_create_with_modifiers");
  sCreateWithModifiers2 = (CreateWithModifiers2Func)dlsym(
      sGbmLibHandle, "gbm_bo_create_with_modifiers2");
  sGetModifier = (GetModifierFunc)dlsym(sGbmLibHandle, "gbm_bo_get_modifier");
  sGetStride = (GetStrideFunc)dlsym(sGbmLibHandle, "gbm_bo_get_stride");
  sGetFd = (GetFdFunc)dlsym(sGbmLibHandle, "gbm_bo_get_fd");
  sDestroy = (DestroyFunc)dlsym(sGbmLibHandle, "gbm_bo_destroy");
  sMap = (MapFunc)dlsym(sGbmLibHandle, "gbm_bo_map");
  sUnmap = (UnmapFunc)dlsym(sGbmLibHandle, "gbm_bo_unmap");
  sGetPlaneCount =
      (GetPlaneCountFunc)dlsym(sGbmLibHandle, "gbm_bo_get_plane_count");
  sGetHandleForPlane = (GetHandleForPlaneFunc)dlsym(
      sGbmLibHandle, "gbm_bo_get_handle_for_plane");
  sGetStrideForPlane = (GetStrideForPlaneFunc)dlsym(
      sGbmLibHandle, "gbm_bo_get_stride_for_plane");
  sGetOffset = (GetOffsetFunc)dlsym(sGbmLibHandle, "gbm_bo_get_offset");
  sDeviceIsFormatSupported = (DeviceIsFormatSupportedFunc)dlsym(
      sGbmLibHandle, "gbm_device_is_format_supported");
  sCreateSurface =
      (CreateSurfaceFunc)dlsym(sGbmLibHandle, "gbm_surface_create");
  sDestroySurface =
      (DestroySurfaceFunc)dlsym(sGbmLibHandle, "gbm_surface_destroy");

  sXf86DrmLibHandle = dlopen(DRMLIB_NAME, RTLD_LAZY | RTLD_LOCAL);
  if (!sXf86DrmLibHandle) {
    LOGDMABUF(("Failed to load %s, dmabuf isn't available.\n", DRMLIB_NAME));
    return false;
  }
  sDrmPrimeHandleToFD =
      (DrmPrimeHandleToFDFunc)dlsym(sXf86DrmLibHandle, "drmPrimeHandleToFD");
  sLoaded = IsLoaded();
  if (!sLoaded) {
    LOGDMABUF(("Failed to load all symbols from %s\n", GBMLIB_NAME));
  }
  return sLoaded;
}

int DMABufDevice::GetDmabufFD(uint32_t aGEMHandle) {
  int fd;
  return GbmLib::DrmPrimeHandleToFD(mDRMFd, aGEMHandle, 0, &fd) < 0 ? -1 : fd;
}

gbm_device* DMABufDevice::GetGbmDevice() {
  std::call_once(mFlagGbmDevice, [&] {
    mGbmDevice = (mDRMFd != -1) ? GbmLib::CreateDevice(mDRMFd) : nullptr;
  });
  return mGbmDevice;
}

int DMABufDevice::OpenDRMFd() { return open(mDrmRenderNode.get(), O_RDWR); }

bool DMABufDevice::IsEnabled(nsACString& aFailureId) {
  if (mDRMFd == -1) {
    aFailureId = mFailureId;
  }
  return mDRMFd != -1;
}

DMABufDevice::DMABufDevice() { Configure(); }

DMABufDevice::~DMABufDevice() {
  if (mGbmDevice) {
    GbmLib::DestroyDevice(mGbmDevice);
    mGbmDevice = nullptr;
  }
  if (mDRMFd != -1) {
    close(mDRMFd);
    mDRMFd = -1;
  }
}

void DMABufDevice::Configure() {
  LOGDMABUF(("DMABufDevice::Configure()"));

  LoadFormatModifiers();

  if (!GbmLib::IsAvailable()) {
    LOGDMABUF(("GbmLib is not available!"));
    mFailureId = "FEATURE_FAILURE_NO_LIBGBM";
    return;
  }

  // See Bug 1865747 for details.
  if (XRE_IsParentProcess()) {
    if (auto* gbmBackend = getenv("GBM_BACKEND")) {
      const nsCOMPtr<nsIGfxInfo> gfxInfo = components::GfxInfo::Service();
      nsAutoString vendorID;
      gfxInfo->GetAdapterVendorID(vendorID);
      if (vendorID != GfxDriverInfo::GetDeviceVendor(DeviceVendor::NVIDIA)) {
        if (strstr(gbmBackend, "nvidia")) {
          unsetenv("GBM_BACKEND");
        }
      }
    }
  }

  mDrmRenderNode = nsAutoCString(getenv("MOZ_DRM_DEVICE"));
  if (mDrmRenderNode.IsEmpty()) {
    mDrmRenderNode.Assign(gfx::gfxVars::DrmRenderDevice());
  }
  if (mDrmRenderNode.IsEmpty()) {
    LOGDMABUF(("We're missing DRM render device!\n"));
    mFailureId = "FEATURE_FAILURE_NO_DRM_DEVICE";
    return;
  }

  LOGDMABUF(("Using DRM device %s", mDrmRenderNode.get()));
  mDRMFd = open(mDrmRenderNode.get(), O_RDWR);
  if (mDRMFd < 0) {
    LOGDMABUF(("Failed to open drm render node %s error %s\n",
               mDrmRenderNode.get(), strerror(errno)));
    mFailureId = "FEATURE_FAILURE_NO_DRM_DEVICE";
    return;
  }

  LOGDMABUF(("DMABuf is enabled"));
}

bool DMABufDevice::IsDMABufWebGLEnabled() {
  LOGDMABUF(
      ("DMABufDevice::IsDMABufWebGLEnabled: UseDMABuf %d "
       "sUseWebGLDmabufBackend %d "
       "UseDMABufWebGL %d\n",
       gfx::gfxVars::UseDMABuf(), sUseWebGLDmabufBackend,
       gfx::gfxVars::UseDMABufWebGL()));
  return gfx::gfxVars::UseDMABuf() && sUseWebGLDmabufBackend &&
         gfx::gfxVars::UseDMABufWebGL();
}

void DMABufDevice::SetModifiersToGfxVars() {
  RefPtr<DMABufFormats> formats;
#ifdef MOZ_WAYLAND
  if (GdkIsWaylandDisplay()) {
    formats = WaylandDisplayGet()->GetDMABufFormats();
  }
#endif
  if (!formats) {
    formats = new DMABufFormats();
  }
  formats->EnsureBasicFormats();

  DRMFormat* format = formats->GetFormat(GBM_FORMAT_XRGB8888);
  MOZ_DIAGNOSTIC_ASSERT(format, "Missing GBM_FORMAT_XRGB8888 dmabuf format!");
  mFormatRGBX = new DRMFormat(*format);
  gfxVars::SetDMABufModifiersXRGB(*format->GetModifiers());

  format = formats->GetFormat(GBM_FORMAT_ARGB8888);
  MOZ_DIAGNOSTIC_ASSERT(format, "Missing GBM_FORMAT_ARGB8888 dmabuf format!");
  mFormatRGBA = new DRMFormat(*format);
  gfxVars::SetDMABufModifiersARGB(*format->GetModifiers());

  format = formats->GetFormat(GBM_FORMAT_P010);
  if (format) {
    mFormatP010 = new DRMFormat(*format);
    gfxVars::SetDMABufModifiersP010(*format->GetModifiers());
  }

  format = formats->GetFormat(GBM_FORMAT_NV12);
  if (format) {
    mFormatNV12 = new DRMFormat(*format);
    gfxVars::SetDMABufModifiersNV12(*format->GetModifiers());
  }
}

void DMABufDevice::GetModifiersFromGfxVars() {
  mFormatRGBX =
      new DRMFormat(GBM_FORMAT_XRGB8888, gfxVars::DMABufModifiersXRGB());
  mFormatRGBA =
      new DRMFormat(GBM_FORMAT_ARGB8888, gfxVars::DMABufModifiersARGB());
  mFormatP010 = new DRMFormat(GBM_FORMAT_P010, gfxVars::DMABufModifiersP010());
  mFormatNV12 = new DRMFormat(GBM_FORMAT_NV12, gfxVars::DMABufModifiersNV12());
}

void DMABufDevice::DisableDMABufWebGL() { sUseWebGLDmabufBackend = false; }

RefPtr<DRMFormat> DMABufDevice::GetDRMFormat(int32_t aFOURCCFormat) {
  switch (aFOURCCFormat) {
    case GBM_FORMAT_XRGB8888:
      MOZ_DIAGNOSTIC_ASSERT(mFormatRGBX, "Missing RGBX dmabuf format!");
      return mFormatRGBX;
    case GBM_FORMAT_ARGB8888:
      MOZ_DIAGNOSTIC_ASSERT(mFormatRGBA, "Missing RGBA dmabuf format!");
      return mFormatRGBA;
    case GBM_FORMAT_P010:
      return mFormatP010;
    case GBM_FORMAT_NV12:
      return mFormatNV12;
    default:
      gfxCriticalNoteOnce << "DMABufDevice::GetDRMFormat() unknow format: "
                          << aFOURCCFormat;
      return nullptr;
  }
}

void DMABufDevice::LoadFormatModifiers() {
  if (XRE_IsParentProcess()) {
    MOZ_ASSERT(NS_IsMainThread());
    SetModifiersToGfxVars();
  } else {
    GetModifiersFromGfxVars();
  }
}

DMABufDevice* GetDMABufDevice() {
  static StaticAutoPtr<DMABufDevice> sDmaBufDevice;
  static std::once_flag onceFlag;
  std::call_once(onceFlag, [] {
    sDmaBufDevice = new DMABufDevice();
    if (NS_IsMainThread()) {
      ClearOnShutdown(&sDmaBufDevice);
    } else {
      NS_DispatchToMainThread(NS_NewRunnableFunction(
          "ClearDmaBufDevice", [] { ClearOnShutdown(&sDmaBufDevice); }));
    }
  });
  return sDmaBufDevice.get();
}

}  // namespace widget
}  // namespace mozilla
