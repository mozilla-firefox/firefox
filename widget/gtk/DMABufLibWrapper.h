/* -*- Mode: C; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim:expandtab:shiftwidth=2:tabstop=2:
 */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef __MOZ_DMABUF_LIB_WRAPPER_H__
#define __MOZ_DMABUF_LIB_WRAPPER_H__

#include "mozilla/StaticMutex.h"
#include "mozilla/widget/DMABufFormats.h"
#include <gbm.h>
#include <mutex>

#undef LOGDMABUF
#ifdef MOZ_LOGGING
#  include "mozilla/Logging.h"
#  include "nsTArray.h"
#  include "Units.h"
extern mozilla::LazyLogModule gDmabufLog;
#  define LOGDMABUF(args) MOZ_LOG(gDmabufLog, mozilla::LogLevel::Debug, args)
#else
#  define LOGDMABUF(args)
#endif /* MOZ_LOGGING */

namespace mozilla {
namespace widget {

typedef struct gbm_device* (*CreateDeviceFunc)(int);
typedef void (*DestroyDeviceFunc)(struct gbm_device*);
typedef struct gbm_bo* (*CreateFunc)(struct gbm_device*, uint32_t, uint32_t,
                                     uint32_t, uint32_t);
typedef struct gbm_bo* (*CreateWithModifiersFunc)(struct gbm_device*, uint32_t,
                                                  uint32_t, uint32_t,
                                                  const uint64_t*,
                                                  const unsigned int);
typedef struct gbm_bo* (*CreateWithModifiers2Func)(struct gbm_device*, uint32_t,
                                                   uint32_t, uint32_t,
                                                   const uint64_t*,
                                                   const unsigned int,
                                                   const uint32_t);
typedef uint64_t (*GetModifierFunc)(struct gbm_bo*);
typedef uint32_t (*GetStrideFunc)(struct gbm_bo*);
typedef int (*GetFdFunc)(struct gbm_bo*);
typedef void (*DestroyFunc)(struct gbm_bo*);
typedef void* (*MapFunc)(struct gbm_bo*, uint32_t, uint32_t, uint32_t, uint32_t,
                         uint32_t, uint32_t*, void**);
typedef void (*UnmapFunc)(struct gbm_bo*, void*);
typedef int (*GetPlaneCountFunc)(struct gbm_bo*);
typedef union gbm_bo_handle (*GetHandleForPlaneFunc)(struct gbm_bo*, int);
typedef uint32_t (*GetStrideForPlaneFunc)(struct gbm_bo*, int);
typedef uint32_t (*GetOffsetFunc)(struct gbm_bo*, int);
typedef int (*DeviceIsFormatSupportedFunc)(struct gbm_device*, uint32_t,
                                           uint32_t);
typedef int (*DrmPrimeHandleToFDFunc)(int, uint32_t, uint32_t, int*);
typedef struct gbm_surface* (*CreateSurfaceFunc)(struct gbm_device*, uint32_t,
                                                 uint32_t, uint32_t, uint32_t);
typedef void (*DestroySurfaceFunc)(struct gbm_surface*);

class GbmLib {
 public:
  static bool IsAvailable() { return sLoaded || Load(); }
  static bool IsModifierAvailable();

  static struct gbm_device* CreateDevice(int fd) {
    StaticMutexAutoLock lockDRI(sDRILock);
    return sCreateDevice(fd);
  };
  static void DestroyDevice(struct gbm_device* gdm) {
    StaticMutexAutoLock lockDRI(sDRILock);
    return sDestroyDevice(gdm);
  };
  static struct gbm_bo* Create(struct gbm_device* gbm, uint32_t width,
                               uint32_t height, uint32_t format,
                               uint32_t flags) {
    StaticMutexAutoLock lockDRI(sDRILock);
    return sCreate(gbm, width, height, format, flags);
  }
  static void Destroy(struct gbm_bo* bo) {
    StaticMutexAutoLock lockDRI(sDRILock);
    sDestroy(bo);
  }
  static uint32_t GetStride(struct gbm_bo* bo) {
    StaticMutexAutoLock lockDRI(sDRILock);
    return sGetStride(bo);
  }
  static int GetFd(struct gbm_bo* bo) {
    StaticMutexAutoLock lockDRI(sDRILock);
    return sGetFd(bo);
  }
  static void* Map(struct gbm_bo* bo, uint32_t x, uint32_t y, uint32_t width,
                   uint32_t height, uint32_t flags, uint32_t* stride,
                   void** map_data) {
    StaticMutexAutoLock lockDRI(sDRILock);
    return sMap(bo, x, y, width, height, flags, stride, map_data);
  }
  static void Unmap(struct gbm_bo* bo, void* map_data) {
    StaticMutexAutoLock lockDRI(sDRILock);
    sUnmap(bo, map_data);
  }
  static struct gbm_bo* CreateWithModifiers(struct gbm_device* gbm,
                                            uint32_t width, uint32_t height,
                                            uint32_t format,
                                            const uint64_t* modifiers,
                                            const unsigned int count) {
    StaticMutexAutoLock lockDRI(sDRILock);
    return sCreateWithModifiers(gbm, width, height, format, modifiers, count);
  }
  static struct gbm_bo* CreateWithModifiers2(struct gbm_device* gbm,
                                             uint32_t width, uint32_t height,
                                             uint32_t format,
                                             const uint64_t* modifiers,
                                             const unsigned int count,
                                             const uint32_t flags) {
    StaticMutexAutoLock lockDRI(sDRILock);
    if (!sCreateWithModifiers2) {
      // CreateWithModifiers2 is optional and present in new GBM lib,
      // if missing just fallback to old CreateWithModifiers as
      // we want to anyway.
      return sCreateWithModifiers(gbm, width, height, format, modifiers, count);
    }
    return sCreateWithModifiers2(gbm, width, height, format, modifiers, count,
                                 flags);
  }
  static uint64_t GetModifier(struct gbm_bo* bo) {
    StaticMutexAutoLock lockDRI(sDRILock);
    return sGetModifier(bo);
  }
  static int GetPlaneCount(struct gbm_bo* bo) {
    StaticMutexAutoLock lockDRI(sDRILock);
    return sGetPlaneCount(bo);
  }
  static union gbm_bo_handle GetHandleForPlane(struct gbm_bo* bo, int plane) {
    StaticMutexAutoLock lockDRI(sDRILock);
    return sGetHandleForPlane(bo, plane);
  }
  static uint32_t GetStrideForPlane(struct gbm_bo* bo, int plane) {
    StaticMutexAutoLock lockDRI(sDRILock);
    return sGetStrideForPlane(bo, plane);
  }
  static uint32_t GetOffset(struct gbm_bo* bo, int plane) {
    StaticMutexAutoLock lockDRI(sDRILock);
    return sGetOffset(bo, plane);
  }
  static int DeviceIsFormatSupported(struct gbm_device* gbm, uint32_t format,
                                     uint32_t usage) {
    StaticMutexAutoLock lockDRI(sDRILock);
    return sDeviceIsFormatSupported(gbm, format, usage);
  }
  static int DrmPrimeHandleToFD(int fd, uint32_t handle, uint32_t flags,
                                int* prime_fd) {
    StaticMutexAutoLock lockDRI(sDRILock);
    return sDrmPrimeHandleToFD(fd, handle, flags, prime_fd);
  }
  static struct gbm_surface* CreateSurface(struct gbm_device* gbm,
                                           uint32_t width, uint32_t height,
                                           uint32_t format, uint32_t flags) {
    StaticMutexAutoLock lockDRI(sDRILock);
    return sCreateSurface(gbm, width, height, format, flags);
  }
  static void DestroySurface(struct gbm_surface* surface) {
    StaticMutexAutoLock lockDRI(sDRILock);
    return sDestroySurface(surface);
  }

 private:
  static bool Load();
  static bool IsLoaded();

  static CreateDeviceFunc sCreateDevice;
  static DestroyDeviceFunc sDestroyDevice;
  static CreateFunc sCreate;
  static CreateWithModifiersFunc sCreateWithModifiers;
  static CreateWithModifiers2Func sCreateWithModifiers2;
  static GetModifierFunc sGetModifier;
  static GetStrideFunc sGetStride;
  static GetFdFunc sGetFd;
  static DestroyFunc sDestroy;
  static MapFunc sMap;
  static UnmapFunc sUnmap;
  static GetPlaneCountFunc sGetPlaneCount;
  static GetHandleForPlaneFunc sGetHandleForPlane;
  static GetStrideForPlaneFunc sGetStrideForPlane;
  static GetOffsetFunc sGetOffset;
  static DeviceIsFormatSupportedFunc sDeviceIsFormatSupported;
  static DrmPrimeHandleToFDFunc sDrmPrimeHandleToFD;
  static CreateSurfaceFunc sCreateSurface;
  static DestroySurfaceFunc sDestroySurface;
  static bool sLoaded;

  static void* sGbmLibHandle;
  static void* sXf86DrmLibHandle;
  static mozilla::StaticMutex sDRILock MOZ_UNANNOTATED;
};

class DRMFormat;

class DMABufDevice {
 public:
  DMABufDevice();
  ~DMABufDevice();

  int OpenDRMFd();
  gbm_device* GetGbmDevice();
  int GetDmabufFD(uint32_t aGEMHandle);

  bool IsEnabled(nsACString& aFailureId);

  // Use dmabuf for WebGL content
  static bool IsDMABufWebGLEnabled();
  static void DisableDMABufWebGL();

  RefPtr<DRMFormat> GetDRMFormat(int32_t aFOURCCFormat);

 private:
  void Configure();

  void LoadFormatModifiers();
  void SetModifiersToGfxVars();
  void GetModifiersFromGfxVars();

  // Formats passed to RDD process to WebGL process
  // where we can't get formats/modifiers from Wayland display.
  // RGBA formats are mandatory, YUV ones are optional.
  RefPtr<DRMFormat> mFormatRGBA;
  RefPtr<DRMFormat> mFormatRGBX;
  RefPtr<DRMFormat> mFormatP010;
  RefPtr<DRMFormat> mFormatNV12;

  int mDRMFd = -1;
  std::once_flag mFlagGbmDevice;
  gbm_device* mGbmDevice = nullptr;
  const char* mFailureId = nullptr;
  nsAutoCString mDrmRenderNode;
};

DMABufDevice* GetDMABufDevice();

}  // namespace widget
}  // namespace mozilla

#endif  // __MOZ_DMABUF_LIB_WRAPPER_H__
