/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "DMABufSurface.h"
#include "DMABufDevice.h"
#include "DMABufFormats.h"

#ifdef MOZ_WAYLAND
#  include "nsWaylandDisplay.h"
#endif

#include <gbm.h>
#include <fcntl.h>
#include <getopt.h>
#include <signal.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/time.h>
#include <dlfcn.h>
#include <sys/mman.h>
#ifdef HAVE_EVENTFD
#  include <sys/eventfd.h>
#endif
#include <poll.h>
#ifdef HAVE_SYSIOCCOM_H
#  include <sys/ioccom.h>
#endif
#include <sys/ioctl.h>

// DMABufDevice defines its own version of this which collides with the
// official version in drm_fourcc.h
#ifdef DRM_FORMAT_MOD_INVALID
#  undef DRM_FORMAT_MOD_INVALID
#endif
#include <libdrm/drm_fourcc.h>
#ifndef DRM_FORMAT_MOD_INVALID
#  define DRM_FORMAT_MOD_INVALID ((1ULL << 56) - 1)
#endif

#include "mozilla/widget/va_drmcommon.h"
#include "mozilla/gfx/2D.h"
#include "mozilla/gfx/FileHandleWrapper.h"
#include "GLContextTypes.h"  // for GLContext, etc
#include "GLContextEGL.h"
#include "GLContextProvider.h"
#include "ScopedGLHelpers.h"
#include "GLBlitHelper.h"
#include "GLReadTexImageHelper.h"
#include "nsGtkUtils.h"
#include "ImageContainer.h"
#include "mozilla/layers/LayersSurfaces.h"
#include "mozilla/ScopeExit.h"
#include "mozilla/gfx/gfxVars.h"
#include "nsIMemoryReporter.h"

/* C++ / C typecast macros for special EGL handle values */
#if defined(__cplusplus)
#  define EGL_CAST(type, value) (static_cast<type>(value))
#else
#  define EGL_CAST(type, value) ((type)(value))
#endif

using namespace mozilla;
using namespace mozilla::widget;
using namespace mozilla::gl;
using namespace mozilla::layers;
using namespace mozilla::gfx;

#undef LOGDMABUF
#undef LOGDMABUFREF
#ifdef MOZ_LOGGING
#  include "mozilla/Logging.h"
#  include "nsTArray.h"
#  include "Units.h"

extern mozilla::LazyLogModule gDmabufLog;
#  define LOGDMABUF(str, ...)                     \
    MOZ_LOG(gDmabufLog, mozilla::LogLevel::Debug, \
            ("%s: " str, GetDebugTag().get(), ##__VA_ARGS__))
#  define LOGDMAVERBOSES(str, ...) \
    MOZ_LOG(gDmabufLog, mozilla::LogLevel::Verbose, (str, ##__VA_ARGS__))
#  define LOGDMABUFS(str, ...) \
    MOZ_LOG(gDmabufLog, mozilla::LogLevel::Debug, (str, ##__VA_ARGS__))
static LazyLogModule gDmabufRefLog("DmabufRef");
#  define LOGDMABUFREF(str, ...)                     \
    MOZ_LOG(gDmabufRefLog, mozilla::LogLevel::Debug, \
            ("%s: " str, GetDebugTag().get(), ##__VA_ARGS__))
#else
#  define LOGDMABUF(str, ...)
#  define LOGDMABUFREF(str, ...)
#endif /* MOZ_LOGGING */

#define BUFFER_FLAGS 0

static const std::string FormatEGLError(EGLint err) {
  switch (err) {
    case LOCAL_EGL_NOT_INITIALIZED:
      return "EGL_NOT_INITIALIZED";
    case LOCAL_EGL_BAD_ACCESS:
      return "EGL_BAD_ACCESS";
    case LOCAL_EGL_BAD_ALLOC:
      return "EGL_BAD_ALLOC";
    case LOCAL_EGL_BAD_ATTRIBUTE:
      return "EGL_BAD_ATTRIBUTE";
    case LOCAL_EGL_BAD_CONTEXT:
      return "EGL_BAD_CONTEXT";
    case LOCAL_EGL_BAD_CONFIG:
      return "EGL_BAD_CONFIG";
    case LOCAL_EGL_BAD_CURRENT_SURFACE:
      return "EGL_BAD_CURRENT_SURFACE";
    case LOCAL_EGL_BAD_DISPLAY:
      return "EGL_BAD_DISPLAY";
    case LOCAL_EGL_BAD_SURFACE:
      return "EGL_BAD_SURFACE";
    case LOCAL_EGL_BAD_MATCH:
      return "EGL_BAD_MATCH";
    case LOCAL_EGL_BAD_PARAMETER:
      return "EGL_BAD_PARAMETER";
    case LOCAL_EGL_BAD_NATIVE_PIXMAP:
      return "EGL_BAD_NATIVE_PIXMAP";
    case LOCAL_EGL_BAD_NATIVE_WINDOW:
      return "EGL_BAD_NATIVE_WINDOW";
    case LOCAL_EGL_CONTEXT_LOST:
      return "EGL_CONTEXT_LOST";
    default:
      return "EGL error code: " + std::to_string(err);
  }
}

constinit static RefPtr<GLContext> sSnapshotContext;
static StaticMutex sSnapshotContextMutex MOZ_UNANNOTATED;
MOZ_RUNINIT static Atomic<int> gNewSurfaceUID(getpid());
/* Memory reporter stuff */
static Atomic<int64_t, Relaxed> gDMABufSurfaceMemoryRGBAUsed(0);
static Atomic<size_t, Relaxed> gDMABufSurfaceRGBAUsed(0);
static Atomic<int64_t, Relaxed> gDMABufSurfaceMemoryYUVUsed(0);
static Atomic<size_t, Relaxed> gDMABufSurfaceYUVUsed(0);

void LogMemoryAddRGBA(int aUID, size_t aUsedMem) {
  gDMABufSurfaceRGBAUsed++;
  gDMABufSurfaceMemoryRGBAUsed += aUsedMem;
  LOGDMAVERBOSES("UID %d Memory RGBA [%zu] mem %" PRId64 " KB (+ %zu KB)", aUID,
                 static_cast<size_t>(gDMABufSurfaceRGBAUsed),
                 static_cast<int64_t>(gDMABufSurfaceMemoryRGBAUsed) / 1000,
                 aUsedMem / 1000);
}

void LogMemorySubRGBA(int aUID, size_t aUsedMem) {
  gDMABufSurfaceMemoryRGBAUsed -= aUsedMem;
  gDMABufSurfaceRGBAUsed--;
  LOGDMAVERBOSES("UID %d Memory RGBA [%zu] mem %" PRId64 " KB (- %zu KB)", aUID,
                 static_cast<size_t>(gDMABufSurfaceRGBAUsed),
                 static_cast<int64_t>(gDMABufSurfaceMemoryRGBAUsed) / 1000,
                 aUsedMem / 1000);
}

void LogMemoryAddYUV(int aUID, size_t aUsedMem) {
  gDMABufSurfaceYUVUsed++;
  gDMABufSurfaceMemoryYUVUsed += aUsedMem;
  LOGDMAVERBOSES("UID %d Memory YUV [%zu] mem %" PRId64 " KB (+ %zu KB)", aUID,
                 static_cast<size_t>(gDMABufSurfaceYUVUsed),
                 static_cast<int64_t>(gDMABufSurfaceMemoryYUVUsed) / 1000,
                 aUsedMem / 1000);
}

void LogMemorySubYUV(int aUID, size_t aUsedMem) {
  gDMABufSurfaceMemoryYUVUsed -= aUsedMem;
  gDMABufSurfaceYUVUsed--;
  LOGDMAVERBOSES("UID %d Memory YUV [%zu] mem %" PRId64 " KB (- %zu KB)", aUID,
                 static_cast<size_t>(gDMABufSurfaceYUVUsed),
                 static_cast<int64_t>(gDMABufSurfaceMemoryYUVUsed) / 1000,
                 aUsedMem / 1000);
}

class DMABufSurfaceReporter final : public nsIMemoryReporter {
  ~DMABufSurfaceReporter() = default;

 public:
  NS_DECL_ISUPPORTS

  NS_IMETHOD CollectReports(nsIHandleReportCallback* aHandleReport,
                            nsISupports* aData, bool aAnonymize) override {
    MOZ_COLLECT_REPORT("dmabuf-rgba", KIND_OTHER, UNITS_BYTES,
                       gDMABufSurfaceMemoryRGBAUsed,
                       "Memory used by GPU/DMABuf for RGBA framebuffers.");
    MOZ_COLLECT_REPORT("dmabuf-rgba-num", KIND_OTHER, UNITS_COUNT,
                       gDMABufSurfaceRGBAUsed,
                       "Number of allocated GPU/DMABuf RGBA framebuffers.");
    MOZ_COLLECT_REPORT("dmabuf-yuv", KIND_OTHER, UNITS_BYTES,
                       gDMABufSurfaceMemoryYUVUsed,
                       "Memory used by GPU/DMABuf for YUV video frames.");
    MOZ_COLLECT_REPORT("dmabuf-yuv-num", KIND_OTHER, UNITS_COUNT,
                       gDMABufSurfaceYUVUsed,
                       "Number of allocated GPU/DMABuf YUV video frames.");

    return NS_OK;
  }
};

NS_IMPL_ISUPPORTS(DMABufSurfaceReporter, nsIMemoryReporter)

size_t DMABufSurfaceRGBA::GetUsedMemoryRGBA() { return mWidth * mHeight * 4; }

void DMABufSurface::InitMemoryReporting() {
  RegisterStrongMemoryReporter(MakeAndAddRef<DMABufSurfaceReporter>());
}

// We should release all resources allocated by SnapshotGLContext before
// ReturnSnapshotGLContext() call. Otherwise DMABufSurface references
// SnapshotGLContext and may colide with other SnapshotGLContext operations.
RefPtr<GLContext> DMABufSurface::ClaimSnapshotGLContext() {
  if (!sSnapshotContext) {
    nsCString discardFailureId;
    sSnapshotContext = GLContextProvider::CreateHeadless({}, &discardFailureId);
    if (!sSnapshotContext) {
      LOGDMABUFS(
          "ClaimSnapshotGLContext: Failed to create snapshot GLContext.");
      return nullptr;
    }
    sSnapshotContext->mOwningThreadId = Nothing();  // No singular owner.
  }
  if (!sSnapshotContext->MakeCurrent()) {
    gfxCriticalNote
        << "ClaimSnapshotGLContext: Failed to make GLContext current";
    return nullptr;
  }
  return sSnapshotContext;
}

void DMABufSurface::ReturnSnapshotGLContext(RefPtr<GLContext> aGLContext) {
  if (!aGLContext || !aGLContext->IsCurrent()) {
    LOGDMABUFS("ReturnSnapshotGLContext() failed, is not current!");
    return;
  }
  // direct eglMakeCurrent() call breaks current context caching so make sure
  // it's not used.
  MOZ_ASSERT(!aGLContext->mUseTLSIsCurrent);
  const auto& gle = gl::GLContextEGL::Cast(aGLContext);
  const auto& egl = gle->mEgl;
  egl->fMakeCurrent(EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
}

void DMABufSurface::ReleaseSnapshotGLContext() {
  {
    StaticMutexAutoLock lock(sSnapshotContextMutex);
    sSnapshotContext = nullptr;
  }
  gl::GLContextProvider::Shutdown();
}

bool DMABufSurface::UseDmaBufGL(GLContext* aGLContext) {
  if (!aGLContext) {
    LOGDMABUFS("DMABufSurface::UseDmaBufGL(): Missing GLContext!");
    return false;
  }

  static bool useDmabufGL = [&]() {
    if (!aGLContext->IsExtensionSupported(gl::GLContext::OES_EGL_image)) {
      gfxCriticalNote << "DMABufSurface::UseDmaBufGL(): no OES_EGL_image.";
      return false;
    }
    return true;
  }();

  return useDmabufGL;
}

bool DMABufSurface::UseDmaBufExportExtension(GLContext* aGLContext) {
  static bool useDmabufExport = [&]() {
    if (!gfx::gfxVars::UseDMABufSurfaceExport()) {
      return false;
    }

    if (!UseDmaBufGL(aGLContext)) {
      gfxCriticalNote
          << "DMABufSurface::UseDmaBufExportExtension(): dmabuf is disabled.";
      return false;
    }

    if (!aGLContext->IsAtLeast(gl::ContextProfile::OpenGLCore, 300) &&
        !aGLContext->IsAtLeast(gl::ContextProfile::OpenGLES, 300)) {
      gfxCriticalNote
          << "DMABufSurface::UseDmaBufExportExtension(): old GL version!";
      return false;
    }

    const auto& gle = gl::GLContextEGL::Cast(aGLContext);
    const auto& egl = gle->mEgl;
    bool extensionsAvailable =
        egl->IsExtensionSupported(EGLExtension::EXT_image_dma_buf_import) &&
        egl->IsExtensionSupported(
            EGLExtension::EXT_image_dma_buf_import_modifiers) &&
        egl->IsExtensionSupported(EGLExtension::MESA_image_dma_buf_export);
    if (!extensionsAvailable) {
      if (!egl->IsExtensionSupported(EGLExtension::EXT_image_dma_buf_import)) {
        gfxCriticalNote << "DMABufSurface::UseDmaBufExportExtension(): "
                           "EXT_image_dma_buf_import is missing!";
      }
      if (!egl->IsExtensionSupported(
              EGLExtension::EXT_image_dma_buf_import_modifiers)) {
        gfxCriticalNote << "DMABufSurface::UseDmaBufExportExtension(): "
                           "EXT_image_dma_buf_import_modifiers is missing!";
      }
      if (!egl->IsExtensionSupported(EGLExtension::MESA_image_dma_buf_export)) {
        gfxCriticalNote << "DMABufSurface::UseDmaBufExportExtension(): "
                           "MESA_image_dma_buf_export is missing!";
      }
      LOGDMAVERBOSES("Vendor: %s", aGLContext->VendorString().get());
      LOGDMAVERBOSES("Renderer: %s", aGLContext->RendererString().get());
      auto& strings = aGLContext->ExtensionStrings();
      for (auto& ext : strings) {
        LOGDMAVERBOSES("%s", ext.get());
      }
    }
    return extensionsAvailable;
  }();

  return aGLContext && useDmabufExport;
}

nsAutoCString DMABufSurface::GetDebugTag() const {
  nsAutoCString tag;
  tag.AppendPrintf("[%p]", this);
  return tag;
}
bool DMABufSurface::IsGlobalRefSet() {
  MutexAutoLock lock(mSurfaceLock);
  if (!mGlobalRefCountFd) {
    return false;
  }
  struct pollfd pfd;
  pfd.fd = mGlobalRefCountFd;
  pfd.events = POLLIN;
  return poll(&pfd, 1, 0) == 1;
}

void DMABufSurface::GlobalRefRelease() {
#ifdef HAVE_EVENTFD
  MutexAutoLock lock(mSurfaceLock);
  if (!mGlobalRefCountFd) {
    return;
  }
  LOGDMABUFREF("DMABufSurface::GlobalRefRelease UID %d", mUID);
  uint64_t counter;
  if (read(mGlobalRefCountFd, &counter, sizeof(counter)) != sizeof(counter)) {
    if (errno == EAGAIN) {
      LOGDMABUFREF("  GlobalRefRelease failed: already zero reference! UID %d",
                   mUID);
    }
    // EAGAIN means the refcount is already zero. It happens when we release
    // last reference to the surface.
    if (errno != EAGAIN) {
      NS_WARNING(nsPrintfCString("Failed to unref dmabuf global ref count: %s",
                                 strerror(errno))
                     .get());
    }
  }
#endif
}

void DMABufSurface::GlobalRefAddLocked(const MutexAutoLock& aProofOfLock) {
#ifdef HAVE_EVENTFD
  LOGDMABUFREF("DMABufSurface::GlobalRefAddLocked UID %d", mUID);
  MOZ_DIAGNOSTIC_ASSERT(mGlobalRefCountFd);
  uint64_t counter = 1;
  if (write(mGlobalRefCountFd, &counter, sizeof(counter)) != sizeof(counter)) {
    NS_WARNING(nsPrintfCString("Failed to ref dmabuf global ref count: %s",
                               strerror(errno))
                   .get());
  }
#endif
}

void DMABufSurface::GlobalRefAdd() {
  LOGDMABUFREF("DMABufSurface::GlobalRefAdd UID %d", mUID);
  MutexAutoLock lock(mSurfaceLock);
  GlobalRefAddLocked(lock);
}

void DMABufSurface::GlobalRefCountCreate() {
#ifdef HAVE_EVENTFD
  LOGDMABUFREF("DMABufSurface::GlobalRefCountCreate UID %d", mUID);
  MutexAutoLock lock(mSurfaceLock);
  MOZ_DIAGNOSTIC_ASSERT(!mGlobalRefCountFd);
  // Create global ref count initialized to 0,
  // i.e. is not referenced after create.
  mGlobalRefCountFd = eventfd(0, EFD_CLOEXEC | EFD_NONBLOCK | EFD_SEMAPHORE);
  if (mGlobalRefCountFd < 0) {
    NS_WARNING(nsPrintfCString("Failed to create dmabuf global ref count: %s",
                               strerror(errno))
                   .get());
    mGlobalRefCountFd = 0;
    return;
  }
#endif
}

void DMABufSurface::GlobalRefCountImport(int aFd) {
#ifdef HAVE_EVENTFD
  MutexAutoLock lock(mSurfaceLock);
  mGlobalRefCountFd = aFd;
  if (mGlobalRefCountFd) {
    LOGDMABUFREF("DMABufSurface::GlobalRefCountImport UID %d", mUID);
    GlobalRefAddLocked(lock);
  }
#endif
}

int DMABufSurface::GlobalRefCountExport() {
  MutexAutoLock lock(mSurfaceLock);
#ifdef MOZ_LOGGING
  if (mGlobalRefCountFd) {
    LOGDMABUFREF("DMABufSurface::GlobalRefCountExport UID %d", mUID);
  }
#endif
  return mGlobalRefCountFd;
}

void DMABufSurface::GlobalRefCountDelete() {
  MutexAutoLock lock(mSurfaceLock);
  if (mGlobalRefCountFd) {
    LOGDMABUFREF("DMABufSurface::GlobalRefCountDelete UID %d", mUID);
    close(mGlobalRefCountFd);
    mGlobalRefCountFd = 0;
  }
}

void DMABufSurface::ReleaseDMABuf() {
  LOGDMABUF("DMABufSurface::ReleaseDMABuf() UID %d", mUID);
#ifdef MOZ_LOGGING
  for (int i = 0; i < mBufferPlaneCount; i++) {
    Unmap(i);
  }
#endif

  CloseFileDescriptors();

  for (int i = 0; i < mBufferPlaneCount; i++) {
    if (mGbmBufferObject[i]) {
      GbmLib::Destroy(mGbmBufferObject[i]);
      mGbmBufferObject[i] = nullptr;
    }
  }
  mBufferPlaneCount = 0;
}

DMABufSurface::DMABufSurface(SurfaceType aSurfaceType)
    : mSurfaceType(aSurfaceType),
      mBufferPlaneCount(0),
      mStrides(),
      mOffsets(),
      mGbmBufferObject(),
      mGbmBufferFlags(0),
#ifdef MOZ_LOGGING
      mMappedRegion(),
      mMappedRegionStride(),
#endif
      mSync(nullptr),
      mGlobalRefCountFd(0),
      mUID(gNewSurfaceUID++),
      mPID(0),
      mCanRecycle(true),
      mSurfaceLock("DMABufSurface") {
  MOZ_COUNT_CTOR(DMABufSurface);
}

DMABufSurface::~DMABufSurface() {
  MOZ_COUNT_DTOR(DMABufSurface);
  FenceDelete();
  GlobalRefRelease();
  GlobalRefCountDelete();
}

already_AddRefed<DMABufSurface> DMABufSurface::CreateDMABufSurface(
    const mozilla::layers::SurfaceDescriptor& aDesc) {
  const SurfaceDescriptorDMABuf& desc = aDesc.get_SurfaceDescriptorDMABuf();
  RefPtr<DMABufSurface> surf;

  switch (desc.bufferType()) {
    case SURFACE_RGBA:
      surf = new DMABufSurfaceRGBA();
      break;
    case SURFACE_YUV:
      surf = new DMABufSurfaceYUV();
      break;
    default:
      return nullptr;
  }

  if (!surf->Create(desc)) {
    return nullptr;
  }
  return surf.forget();
}

void DMABufSurface::FenceDelete() {
  if (mSyncFd) {
    mSyncFd = nullptr;
  }

  if (!mGL) {
    return;
  }
  const auto& gle = gl::GLContextEGL::Cast(mGL);
  const auto& egl = gle->mEgl;

  if (mSync) {
    egl->fDestroySync(mSync);
    mSync = nullptr;
  }
}

void DMABufSurface::FenceSet() {
  // There's nothing to sync as we're missing textures/eglimages here.
  if (!HoldsTexture()) {
    return;
  }

  if (!mGL || !mGL->MakeCurrent()) {
    MOZ_DIAGNOSTIC_ASSERT(mGL,
                          "DMABufSurface::FenceSet(): missing GL context!");
    return;
  }
  const auto& gle = gl::GLContextEGL::Cast(mGL);
  const auto& egl = gle->mEgl;

  if (egl->IsExtensionSupported(EGLExtension::KHR_fence_sync) &&
      egl->IsExtensionSupported(EGLExtension::ANDROID_native_fence_sync)) {
    FenceDelete();

    mSync = egl->fCreateSync(LOCAL_EGL_SYNC_NATIVE_FENCE_ANDROID, nullptr);
    if (mSync) {
      auto rawFd = egl->fDupNativeFenceFDANDROID(mSync);
      mSyncFd = new gfx::FileHandleWrapper(UniqueFileHandle(rawFd));
      mGL->fFlush();
      return;
    }
  }

  // ANDROID_native_fence_sync may not be supported so call glFinish()
  // as a slow path.
  mGL->fFinish();
}

void DMABufSurface::FenceWait() {
  // There's nothing to sync as we're missing textures/eglimages here.
  if (!HoldsTexture()) {
    return;
  }

  if (!mGL || !mSyncFd) {
    MOZ_DIAGNOSTIC_ASSERT(mGL,
                          "DMABufSurface::FenceWait() missing GL context!");
    return;
  }

  const auto& gle = gl::GLContextEGL::Cast(mGL);
  const auto& egl = gle->mEgl;
  auto syncFd = mSyncFd->ClonePlatformHandle();
  // No need to try mSyncFd twice.
  mSyncFd = nullptr;

  const EGLint attribs[] = {LOCAL_EGL_SYNC_NATIVE_FENCE_FD_ANDROID,
                            syncFd.get(), LOCAL_EGL_NONE};
  EGLSync sync = egl->fCreateSync(LOCAL_EGL_SYNC_NATIVE_FENCE_ANDROID, attribs);
  if (!sync) {
    gfxCriticalNoteOnce << "CreateSync failed";
    int rawFd = syncFd.get();
    if (rawFd >= 0) {
      // CreateSync failed, so we cannot use eglClientWaitSync; poll(POLLIN)
      // waits for the sync_file fd to become readable when the fence signals.
      constexpr int kSyncPollTimeoutMs = 100;
      struct pollfd pfd = {rawFd, POLLIN, 0};
      int ret = poll(&pfd, 1, kSyncPollTimeoutMs);
      if (!(ret == 1 && (pfd.revents & POLLIN))) {
        gfxCriticalNoteOnce << "poll failed: " << ret;
      }
    }
    return;
  }

  // syncFd is owned by GLFence so clear local reference to avoid double.
  (void)syncFd.release();

  egl->fClientWaitSync(sync, 0, LOCAL_EGL_FOREVER);
  const EGLint waitErr = egl->mLib->fGetError();
  if (waitErr != LOCAL_EGL_SUCCESS) {
    gfxCriticalNoteOnce << "ClientWaitSync failed: " << FormatEGLError(waitErr);
    egl->fDestroySync(sync);
    return;
  }
  egl->fDestroySync(sync);
}

void DMABufSurface::SetSemaphoreFd(int aDuppedRawFd, bool aIsSyncFd) {
  mSemaphoreFd = new gfx::FileHandleWrapper(UniqueFileHandle(aDuppedRawFd));
  mSemaphoreFdIsSyncFd = aIsSyncFd;
}

void DMABufSurface::MaybeSemaphoreWait(GLuint aGlTexture) {
  MOZ_ASSERT(aGlTexture);

  if (!mSemaphoreFd) {
    return;
  }

  if (!mGL) {
    MOZ_DIAGNOSTIC_ASSERT(mGL,
                          "DMABufSurface::SemaphoreWait() missing GL context!");
    return;
  }

  auto fd = mSemaphoreFd->ClonePlatformHandle();
  // No need to try mSemaphoreFd twice.
  mSemaphoreFd = nullptr;

  if (mSemaphoreFdIsSyncFd) {
    // Vulkan semaphore can be exported as opaque (semaphore) or sync (fence)
    // fd: single GPU case: VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_OPAQUE_FD_BIT,
    // cross GPU case: VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_SYNC_FD_BIT
    const auto& gle = gl::GLContextEGL::Cast(mGL);
    const auto& egl = gle->mEgl;
    if (egl->IsExtensionSupported(EGLExtension::ANDROID_native_fence_sync)) {
      mSyncFd = new gfx::FileHandleWrapper(std::move(fd));
      FenceWait();
    }
    return;
  }

  if (!mGL->IsExtensionSupported(gl::GLContext::EXT_semaphore) ||
      !mGL->IsExtensionSupported(gl::GLContext::EXT_semaphore_fd)) {
    MOZ_ASSERT_UNREACHABLE("unexpected to be called");
    gfxCriticalNoteOnce << "EXT_semaphore_fd is not suppored";
    return;
  }

  GLuint semaphoreHandle = 0;
  mGL->fGenSemaphoresEXT(1, &semaphoreHandle);
  mGL->fImportSemaphoreFdEXT(semaphoreHandle,
                             LOCAL_GL_HANDLE_TYPE_OPAQUE_FD_EXT, fd.release());
  auto error = mGL->fGetError();
  if (error != LOCAL_GL_NO_ERROR) {
    gfxCriticalNoteOnce << "glImportSemaphoreFdEXT failed: " << error;
    return;
  }

  GLenum srcLayout = LOCAL_GL_LAYOUT_COLOR_ATTACHMENT_EXT;
  mGL->fWaitSemaphoreEXT(semaphoreHandle, 0, nullptr, 1, &aGlTexture,
                         &srcLayout);
  error = mGL->fGetError();
  if (error != LOCAL_GL_NO_ERROR) {
    gfxCriticalNoteOnce << "glWaitSemaphoreEXT failed: " << error;
    mGL->fDeleteSemaphoresEXT(1, &semaphoreHandle);
    return;
  }
  mGL->fDeleteSemaphoresEXT(1, &semaphoreHandle);
  LOGDMABUF("MaybeSemaphoreWait: done (GL semaphore wait)");
}

bool DMABufSurface::OpenFileDescriptors(
    mozilla::widget::DMABufDeviceLock* aDeviceLock) {
  for (int i = 0; i < mBufferPlaneCount; i++) {
    if (!OpenFileDescriptorForPlane(aDeviceLock, i)) {
      return false;
    }
  }
  return true;
}

void DMABufSurface::CloseFileDescriptors() {
  for (auto& mDmabufFd : mDmabufFds) {
    if (mDmabufFd) {
      mDmabufFd = nullptr;
    }
  }
}

nsresult DMABufSurface::ReadIntoBuffer(mozilla::gl::GLContext* aGLContext,
                                       uint8_t* aData, int32_t aStride,
                                       const gfx::IntSize& aSize,
                                       gfx::SurfaceFormat aFormat) {
  LOGDMABUF("DMABufSurface::ReadIntoBuffer UID %d", mUID);

  // We're empty, nothing to copy
  if (!GetTextureCount()) {
    return NS_ERROR_FAILURE;
  }

  MOZ_ASSERT(aSize.width == GetWidth());
  MOZ_ASSERT(aSize.height == GetHeight());

  for (int i = 0; i < GetTextureCount(); i++) {
    if (!GetTexture(i) && !CreateTexture(aGLContext, i)) {
      LOGDMABUF("ReadIntoBuffer: Failed to create DMABuf textures.");
      return NS_ERROR_FAILURE;
    }
  }

  ScopedTexture scopedTex(aGLContext);
  ScopedBindTexture boundTex(aGLContext, scopedTex.Texture());

  aGLContext->fTexImage2D(LOCAL_GL_TEXTURE_2D, 0, LOCAL_GL_RGBA, aSize.width,
                          aSize.height, 0, LOCAL_GL_RGBA,
                          LOCAL_GL_UNSIGNED_BYTE, nullptr);

  ScopedFramebufferForTexture autoFBForTex(aGLContext, scopedTex.Texture());
  if (!autoFBForTex.IsComplete()) {
    LOGDMABUF("ReadIntoBuffer: ScopedFramebufferForTexture failed.");
    return NS_ERROR_FAILURE;
  }

  const gl::OriginPos destOrigin = gl::OriginPos::BottomLeft;
  {
    const ScopedBindFramebuffer bindFB(aGLContext, autoFBForTex.FB());
    if (!aGLContext->BlitHelper()->Blit(
            this, gfx::IntRect(gfx::IntPoint(0, 0), aSize), destOrigin)) {
      LOGDMABUF("ReadIntoBuffer: Blit failed.");
      return NS_ERROR_FAILURE;
    }
  }

  ScopedBindFramebuffer bind(aGLContext, autoFBForTex.FB());
  ReadPixelsIntoBuffer(aGLContext, aData, aStride, aSize, aFormat);
  return NS_OK;
}

already_AddRefed<gfx::DataSourceSurface> DMABufSurface::GetAsSourceSurface() {
  LOGDMABUF("DMABufSurface::GetAsSourceSurface UID %d", mUID);

  gfx::IntSize size(GetWidth(), GetHeight());
  const auto format = gfx::SurfaceFormat::B8G8R8A8;
  RefPtr<gfx::DataSourceSurface> source =
      gfx::Factory::CreateDataSourceSurface(size, format);
  if (NS_WARN_IF(!source)) {
    LOGDMABUF("GetAsSourceSurface: CreateDataSourceSurface failed.");
    return nullptr;
  }

  gfx::DataSourceSurface::ScopedMap map(source,
                                        gfx::DataSourceSurface::READ_WRITE);
  if (NS_WARN_IF(!map.IsMapped())) {
    LOGDMABUF("GetAsSourceSurface: Mapping surface failed.");
    return nullptr;
  }

  if (mGL) {
    if (NS_WARN_IF(NS_FAILED(ReadIntoBuffer(mGL, map.GetData(), map.GetStride(),
                                            size, format)))) {
      LOGDMABUF("GetAsSourceSurface: Reading into buffer failed.");
      return nullptr;
    }
  } else {
    // We're missing active GL context - take a snapshot one.
    StaticMutexAutoLock lock(sSnapshotContextMutex);
    RefPtr<GLContext> context = ClaimSnapshotGLContext();
    auto releaseTextures = mozilla::MakeScopeExit([&] {
      ReleaseTextures();
      ReturnSnapshotGLContext(context);
    });
    if (NS_WARN_IF(NS_FAILED(ReadIntoBuffer(context, map.GetData(),
                                            map.GetStride(), size, format)))) {
      LOGDMABUF("GetAsSourceSurface: Reading into buffer failed.");
      return nullptr;
    }
  }

  return source.forget();
}

DMABufSurfaceRGBA::DMABufSurfaceRGBA()
    : DMABufSurface(SURFACE_RGBA),
      mWidth(0),
      mHeight(0),
      mEGLImage(LOCAL_EGL_NO_IMAGE),
      mTexture(0),
      mBufferModifier(DRM_FORMAT_MOD_INVALID) {}

DMABufSurfaceRGBA::~DMABufSurfaceRGBA() {
  LOGDMABUF("DMABufSurfaceRGBA::~DMABufSurfaceRGBA() UID %d", mUID);
  ReleaseSurface();
}

bool DMABufSurfaceRGBA::OpenFileDescriptorForPlane(
    DMABufDeviceLock* aDeviceLock, int aPlane) {
  if (mDmabufFds[aPlane]) {
    return true;
  }
  gbm_bo* bo = mGbmBufferObject[0];
  if (NS_WARN_IF(!bo)) {
    LOGDMABUF(
        "DMABufSurfaceRGBA::OpenFileDescriptorForPlane: Missing "
        "mGbmBufferObject object!");
    return false;
  }

  if (mBufferPlaneCount == 1) {
    MOZ_ASSERT(aPlane == 0, "DMABuf: wrong surface plane!");
    auto rawFd = GbmLib::GetFd(bo);
    if (rawFd >= 0) {
      mDmabufFds[0] = new gfx::FileHandleWrapper(UniqueFileHandle(rawFd));
    } else {
      gfxCriticalNoteOnce << "GbmLib::GetFd() failed";
      LOGDMABUF(
          "DMABufSurfaceRGBA::OpenFileDescriptorForPlane: GbmLib::GetFd() "
          "failed");
    }
  } else {
    auto rawFd = aDeviceLock->GetDMABufDevice()->GetDmabufFD(
        GbmLib::GetHandleForPlane(bo, aPlane).u32);
    if (rawFd >= 0) {
      mDmabufFds[aPlane] = new gfx::FileHandleWrapper(UniqueFileHandle(rawFd));
    } else {
      gfxCriticalNoteOnce << "DMABufDevice::GetDmabufFD() failed";
      LOGDMABUF(
          "DMABufSurfaceRGBA::OpenFileDescriptorForPlane: "
          "DMABufDevice::GetDmabufFD() failed");
    }
  }

  if (!mDmabufFds[aPlane]) {
    CloseFileDescriptors();
    return false;
  }

  return true;
}

bool DMABufSurfaceRGBA::Create(mozilla::gl::GLContext* aGLContext, int aWidth,
                               int aHeight, int aDMABufSurfaceFlags,
                               RefPtr<DRMFormat> aFormat) {
  bool useGLSnapshot = gfx::gfxVars::UseDMABufSurfaceExport() && !aGLContext;
  if (useGLSnapshot) {
    LOGDMAVERBOSES("DMABufSurfaceRGBA::Create(): use GL snapshot.");
    StaticMutexAutoLock lock(sSnapshotContextMutex);
    RefPtr<GLContext> context = ClaimSnapshotGLContext();
    auto releaseTextures = MakeScopeExit([&] {
      ReleaseTextures();
      ReturnSnapshotGLContext(context);
    });

    // If gfxVars::UseDMABufSurfaceExport() is set but we fail due to missing
    // system support, don't try GBM.
    if (!UseDmaBufExportExtension(context)) {
      LOGDMAVERBOSES(
          "DMABufSurfaceRGBA::Create(): quit, can't use "
          "UseDmaBufExportExtension!");
      return false;
    }
    return CreateExport(context, aWidth, aHeight, aDMABufSurfaceFlags);
  }

  if (gfx::gfxVars::UseDMABufSurfaceExport()) {
    if (!UseDmaBufExportExtension(aGLContext)) {
      LOGDMAVERBOSES(
          "DMABufSurfaceRGBA::Create(): quit, can't use "
          "UseDmaBufExportExtension!");
      return false;
    }
    return CreateExport(aGLContext, aWidth, aHeight, aDMABufSurfaceFlags);
  }

  if (!aFormat) {
    mFOURCCFormat = aDMABufSurfaceFlags & DMABUF_ALPHA ? GBM_FORMAT_ARGB8888
                                                       : GBM_FORMAT_XRGB8888;
    aFormat = GetGlobalDMABufFormats()->GetDRMFormat(mFOURCCFormat);
    if (!aFormat) {
      LOGDMABUF("DMABufSurfaceRGBA::Create(): Missing drm format 0x%x!",
                mFOURCCFormat);
      return false;
    }
  }
  return CreateGBM(aWidth, aHeight, aDMABufSurfaceFlags, aFormat);
}

bool DMABufSurfaceRGBA::CreateGBM(int aWidth, int aHeight,
                                  int aDMABufSurfaceFlags,
                                  RefPtr<DRMFormat> aFormat) {
  MOZ_ASSERT(mGbmBufferObject[0] == nullptr, "Already created?");

  DMABufDeviceLock device;

  mWidth = aWidth;
  mHeight = aHeight;
  mFOURCCFormat = aFormat->GetFormat();

  LOGDMABUF(
      "DMABufSurfaceRGBA::CreateGBM() UID %d size %d x %d format 0x%x "
      "modifiers %d\n",
      mUID, mWidth, mHeight, mFOURCCFormat, aFormat->UseModifiers());

  if (aDMABufSurfaceFlags & DMABUF_TEXTURE) {
    mGbmBufferFlags = GBM_BO_USE_RENDERING;
  } else if (aDMABufSurfaceFlags & DMABUF_SCANOUT) {
    mGbmBufferFlags = GBM_BO_USE_RENDERING | GBM_BO_USE_SCANOUT;
  }
  bool useModifiers =
      aFormat->UseModifiers() && (aDMABufSurfaceFlags & DMABUF_USE_MODIFIERS);
  if (useModifiers) {
    LOGDMABUF("    Creating with modifiers\n");
    uint32_t modifiersNum = 0;
    const uint64_t* modifiers = aFormat->GetModifiers(modifiersNum);
    mGbmBufferObject[0] =
        GbmLib::CreateWithModifiers2(device, mWidth, mHeight, mFOURCCFormat,
                                     modifiers, modifiersNum, mGbmBufferFlags);
    if (mGbmBufferObject[0]) {
      mBufferModifier = GbmLib::GetModifier(mGbmBufferObject[0]);
    }
  }

  if (!mGbmBufferObject[0]) {
    LOGDMABUF("    Creating without modifiers\n");
    mGbmBufferFlags = GBM_BO_USE_RENDERING | GBM_BO_USE_LINEAR;
    mGbmBufferObject[0] =
        GbmLib::Create(device, mWidth, mHeight, mFOURCCFormat, mGbmBufferFlags);
    mBufferModifier = DRM_FORMAT_MOD_INVALID;
  }

  if (!mGbmBufferObject[0]) {
    LOGDMABUF("    Failed to create GbmBufferObject\n");
    return false;
  }

  if (mBufferModifier != DRM_FORMAT_MOD_INVALID) {
    mBufferPlaneCount = GbmLib::GetPlaneCount(mGbmBufferObject[0]);
    LOGDMABUF("    Planes count %d", mBufferPlaneCount);
    if (mBufferPlaneCount > DMABUF_BUFFER_PLANES) {
      LOGDMABUF("    There's too many dmabuf planes! (%d)", mBufferPlaneCount);
      mBufferPlaneCount = DMABUF_BUFFER_PLANES;
      return false;
    }

    for (int i = 0; i < mBufferPlaneCount; i++) {
      mStrides[i] = GbmLib::GetStrideForPlane(mGbmBufferObject[0], i);
      mOffsets[i] = GbmLib::GetOffset(mGbmBufferObject[0], i);
    }
  } else {
    mBufferPlaneCount = 1;
    mStrides[0] = GbmLib::GetStride(mGbmBufferObject[0]);
  }

  if (!OpenFileDescriptors(&device)) {
    LOGDMABUF("    Failed to open Fd!");
    return false;
  }

  LOGDMABUF("    Success\n");

  LogMemoryAddRGBA(GetUID(), GetUsedMemoryRGBA());
  return true;
}

bool DMABufSurfaceRGBA::CreateExport(mozilla::gl::GLContext* aGLContext,
                                     int aWidth, int aHeight,
                                     int aDMABufSurfaceFlags) {
  LOGDMABUF("DMABufSurfaceRGBA::CreateExport() UID %d size %d x %d flags %d",
            mUID, aWidth, aHeight, aDMABufSurfaceFlags);

  MOZ_ASSERT(aGLContext);
  MOZ_DIAGNOSTIC_ASSERT(!mTexture && !mEGLImage, "Already exported??");
  MOZ_DIAGNOSTIC_ASSERT(!mGL || mGL == aGLContext);
  MOZ_DIAGNOSTIC_ASSERT(aGLContext);

  mGL = aGLContext;
  auto releaseTextures = MakeScopeExit([&] { ReleaseTextures(); });

  if (!mGL->MakeCurrent()) {
    LOGDMABUF(" failed to make GL context current");
    return false;
  }

  mWidth = aWidth;
  mHeight = aHeight;

  mGL->fGenTextures(1, &mTexture);
  const ScopedBindTexture savedTex(mGL, mTexture);

  GLContext::LocalErrorScope errorScope(*mGL);
  mGL->fTexImage2D(LOCAL_GL_TEXTURE_2D, 0, LOCAL_GL_RGBA, mWidth, mHeight, 0,
                   LOCAL_GL_RGBA, LOCAL_GL_UNSIGNED_BYTE, nullptr);
  const auto err = errorScope.GetError();
  if (err) {
    LOGDMABUF("  TexImage2D failed %x error %s", err,
              GLContext::GLErrorToString(err).c_str());
    return false;
  }

  const auto& gle = gl::GLContextEGL::Cast(mGL);
  const auto& context = gle->mContext;
  const auto& egl = gle->mEgl;
  mEGLImage =
      egl->fCreateImage(context, LOCAL_EGL_GL_TEXTURE_2D,
                        reinterpret_cast<EGLClientBuffer>(mTexture), nullptr);
  if (mEGLImage == LOCAL_EGL_NO_IMAGE) {
    LOGDMABUF("  EGLImageKHR creation failed, EGL error %s",
              FormatEGLError(egl->mLib->fGetError()).c_str());
    return false;
  }

  if (!egl->fExportDMABUFImageQuery(mEGLImage, &mFOURCCFormat,
                                    &mBufferPlaneCount, &mBufferModifier)) {
    LOGDMABUF("  ExportDMABUFImageQueryMESA failed, quit\n");
    return false;
  }
  if (mBufferPlaneCount > DMABUF_BUFFER_PLANES) {
    LOGDMABUF("  wrong plane count %d, quit\n", mBufferPlaneCount);
    mBufferPlaneCount = DMABUF_BUFFER_PLANES;
    return false;
  }
  int fds[DMABUF_BUFFER_PLANES] = {-1};
  if (!egl->fExportDMABUFImage(mEGLImage, fds, mStrides, mOffsets)) {
    LOGDMABUF("  ExportDMABUFImageMESA failed, quit\n");
    return false;
  }

  for (int i = 0; i < mBufferPlaneCount; i++) {
    if (fds[i] > 0) {
      mDmabufFds[i] = new gfx::FileHandleWrapper(UniqueFileHandle(fds[i]));
    }
  }

  // A broken driver can return dmabuf without valid file descriptors
  // which leads to fails later so quit now.
  for (int i = 0; i < mBufferPlaneCount; i++) {
    if (!mDmabufFds[i]) {
      LOGDMABUF(
          "  ExportDMABUFImageMESA failed, mDmabufFds[%d] is invalid, quit", i);
      return false;
    }
  }

  if (GetFormat() == gfx::SurfaceFormat::UNKNOWN) {
    LOGDMABUF("  failed, unsupported drm format %x", mFOURCCFormat);
    return false;
  }

  LOGDMABUF("  created size %d x %d format %x planes %d modifiers %" PRIx64
            " alpha %d",
            mWidth, mHeight, mFOURCCFormat, mBufferPlaneCount, mBufferModifier,
            HasAlpha());

  releaseTextures.release();

  LogMemoryAddRGBA(GetUID(), GetUsedMemoryRGBA());
  return true;
}

bool DMABufSurfaceRGBA::Create(
    RefPtr<mozilla::gfx::FileHandleWrapper>&& aFd,
    const mozilla::webgpu::ffi::WGPUDMABufInfo& aDMABufInfo, int aWidth,
    int aHeight) {
  LOGDMABUF("DMABufSurfaceRGBA::CreateWGPUDMABufInfo() UID %d size %d x %d\n",
            mUID, mWidth, mHeight);

  mWidth = aWidth;
  mHeight = aHeight;
  mBufferModifier = aDMABufInfo.modifier;

  // TODO: Read Vulkan modifiers from DMABufFormats?
  mFOURCCFormat = GBM_FORMAT_ARGB8888;
  mBufferPlaneCount = aDMABufInfo.plane_count;

  RefPtr<gfx::FileHandleWrapper> fd = std::move(aFd);

  for (uint32_t i = 0; i < aDMABufInfo.plane_count; i++) {
    mDmabufFds[i] = fd;
    mStrides[i] = aDMABufInfo.strides[i];
    mOffsets[i] = aDMABufInfo.offsets[i];
  }

  LogMemoryAddRGBA(GetUID(), GetUsedMemoryRGBA());
  return true;
}

bool DMABufSurfaceRGBA::ImportSurfaceDescriptor(
    const SurfaceDescriptor& aDesc) {
  const SurfaceDescriptorDMABuf& desc = aDesc.get_SurfaceDescriptorDMABuf();

  mBufferPlaneCount = desc.fds().Length();

  MOZ_RELEASE_ASSERT(mBufferPlaneCount <= DMABUF_BUFFER_PLANES);
  if (mBufferPlaneCount <= 0 || desc.width().Length() == 0 ||
      desc.height().Length() == 0 || desc.modifier().Length() == 0 ||
      desc.strides().Length() < (uint32_t)mBufferPlaneCount ||
      desc.offsets().Length() < (uint32_t)mBufferPlaneCount) {
    return false;
  }

  mFOURCCFormat = desc.fourccFormat();
  mWidth = desc.width()[0];
  mHeight = desc.height()[0];
  mGbmBufferFlags = desc.flags();
  mBufferModifier = desc.modifier()[0];
  mUID = desc.uid();
  mPID = desc.pid();

  LOGDMABUF(
      "DMABufSurfaceRGBA::ImportSurfaceDescriptor() UID %d size %d x %d\n",
      mUID, mWidth, mHeight);

  for (int i = 0; i < mBufferPlaneCount; i++) {
    mDmabufFds[i] = desc.fds()[i];
    mStrides[i] = desc.strides()[i];
    mOffsets[i] = desc.offsets()[i];
  }

  if (desc.fence().Length() > 0) {
    mSyncFd = desc.fence()[0];
  }

  if (desc.semaphoreFd()) {
    mSemaphoreFd = desc.semaphoreFd();
    mSemaphoreFdIsSyncFd = desc.semaphoreFdIsSyncFd();
  }

  if (desc.refCount().Length() > 0) {
    GlobalRefCountImport(desc.refCount()[0].ClonePlatformHandle().release());
  }

  LOGDMABUF("  imported size %d x %d format %x planes %d", mWidth, mHeight,
            mFOURCCFormat, mBufferPlaneCount);

  LogMemoryAddRGBA(GetUID(), GetUsedMemoryRGBA());
  return true;
}

bool DMABufSurfaceRGBA::Create(const SurfaceDescriptor& aDesc) {
  return ImportSurfaceDescriptor(aDesc);
}

bool DMABufSurfaceRGBA::Serialize(
    mozilla::layers::SurfaceDescriptor& aOutDescriptor) {
  AutoTArray<uint32_t, DMABUF_BUFFER_PLANES> width;
  AutoTArray<uint32_t, DMABUF_BUFFER_PLANES> height;
  AutoTArray<NotNull<RefPtr<gfx::FileHandleWrapper>>, DMABUF_BUFFER_PLANES> fds;
  AutoTArray<uint32_t, DMABUF_BUFFER_PLANES> strides;
  AutoTArray<uint32_t, DMABUF_BUFFER_PLANES> offsets;
  AutoTArray<uintptr_t, DMABUF_BUFFER_PLANES> images;
  AutoTArray<uint64_t, DMABUF_BUFFER_PLANES> modifiers;
  AutoTArray<NotNull<RefPtr<gfx::FileHandleWrapper>>, 1> fenceFDs;
  AutoTArray<ipc::FileDescriptor, 1> refCountFDs;

  LOGDMABUF("DMABufSurfaceRGBA::Serialize() UID %d\n", mUID);

  width.AppendElement(mWidth);
  height.AppendElement(mHeight);
  modifiers.AppendElement(mBufferModifier);
  for (int i = 0; i < mBufferPlaneCount; i++) {
    fds.AppendElement(WrapNotNull(mDmabufFds[i]));
    strides.AppendElement(mStrides[i]);
    offsets.AppendElement(mOffsets[i]);
  }

  if (mSync && mSyncFd) {
    fenceFDs.AppendElement(WrapNotNull(mSyncFd));
  }

  if (mGlobalRefCountFd) {
    refCountFDs.AppendElement(ipc::FileDescriptor(GlobalRefCountExport()));
  }

  // GCC needs it (Bug 1959653).
  AutoTArray<uint32_t, 1> tmp;
  aOutDescriptor = SurfaceDescriptorDMABuf(
      mSurfaceType, mFOURCCFormat, modifiers, mGbmBufferFlags, fds, width,
      height, width, height, tmp, strides, offsets, GetYUVColorSpace(),
      mColorRange, mozilla::gfx::ColorSpace2::UNKNOWN,
      mozilla::gfx::TransferFunction::Default, 0, fenceFDs, mUID,
      mCanRecycle ? getpid() : 0, refCountFDs,
      /* semaphoreFd */ nullptr, /* semaphoreFdIsSyncFd */ false);
  return true;
}

bool DMABufSurfaceRGBA::CreateTexture(GLContext* aGLContext, int aPlane) {
  if (mTexture) {
    MOZ_DIAGNOSTIC_ASSERT(mGL == aGLContext);
    return true;
  }

  LOGDMABUF("DMABufSurfaceRGBA::CreateTexture() UID %d plane %d\n", mUID,
            aPlane);

  if (!UseDmaBufGL(aGLContext)) {
    LOGDMABUF("  UseDmaBufGL() failed");
    return false;
  }

  MOZ_DIAGNOSTIC_ASSERT(aGLContext);
  mGL = aGLContext;
  auto releaseTextures = MakeScopeExit([&] { ReleaseTextures(); });

  nsTArray<EGLint> attribs;
  attribs.AppendElement(LOCAL_EGL_WIDTH);
  attribs.AppendElement(mWidth);
  attribs.AppendElement(LOCAL_EGL_HEIGHT);
  attribs.AppendElement(mHeight);
  attribs.AppendElement(LOCAL_EGL_LINUX_DRM_FOURCC_EXT);
  attribs.AppendElement(mFOURCCFormat);
#define ADD_PLANE_ATTRIBS(plane_idx)                                        \
  {                                                                         \
    attribs.AppendElement(LOCAL_EGL_DMA_BUF_PLANE##plane_idx##_FD_EXT);     \
    attribs.AppendElement(mDmabufFds[plane_idx]->GetHandle());              \
    attribs.AppendElement(LOCAL_EGL_DMA_BUF_PLANE##plane_idx##_OFFSET_EXT); \
    attribs.AppendElement((int)mOffsets[plane_idx]);                        \
    attribs.AppendElement(LOCAL_EGL_DMA_BUF_PLANE##plane_idx##_PITCH_EXT);  \
    attribs.AppendElement((int)mStrides[plane_idx]);                        \
    if (mBufferModifier != DRM_FORMAT_MOD_INVALID) {                        \
      attribs.AppendElement(                                                \
          LOCAL_EGL_DMA_BUF_PLANE##plane_idx##_MODIFIER_LO_EXT);            \
      attribs.AppendElement(mBufferModifier & 0xFFFFFFFF);                  \
      attribs.AppendElement(                                                \
          LOCAL_EGL_DMA_BUF_PLANE##plane_idx##_MODIFIER_HI_EXT);            \
      attribs.AppendElement(mBufferModifier >> 32);                         \
    }                                                                       \
  }

  ADD_PLANE_ATTRIBS(0);
  if (mBufferPlaneCount > 1) ADD_PLANE_ATTRIBS(1);
  if (mBufferPlaneCount > 2) ADD_PLANE_ATTRIBS(2);
  if (mBufferPlaneCount > 3) ADD_PLANE_ATTRIBS(3);
#undef ADD_PLANE_ATTRIBS
  attribs.AppendElement(LOCAL_EGL_NONE);

  if (!aGLContext->MakeCurrent()) {
    LOGDMABUF(
        "DMABufSurfaceRGBA::CreateTexture(): failed to make GL context "
        "current");
    return false;
  }

  const auto& gle = gl::GLContextEGL::Cast(aGLContext);
  const auto& egl = gle->mEgl;

  MOZ_ASSERT(!mEGLImage);
  mEGLImage =
      egl->fCreateImage(LOCAL_EGL_NO_CONTEXT, LOCAL_EGL_LINUX_DMA_BUF_EXT,
                        nullptr, attribs.Elements());

  if (mEGLImage == LOCAL_EGL_NO_IMAGE) {
    LOGDMABUF("  EGLImageKHR creation failed, EGL error %s",
              FormatEGLError(egl->mLib->fGetError()).c_str());
    return false;
  }

  aGLContext->fGenTextures(1, &mTexture);
  const ScopedBindTexture savedTex(aGLContext, mTexture);
  aGLContext->fTexParameteri(LOCAL_GL_TEXTURE_2D, LOCAL_GL_TEXTURE_WRAP_S,
                             LOCAL_GL_CLAMP_TO_EDGE);
  aGLContext->fTexParameteri(LOCAL_GL_TEXTURE_2D, LOCAL_GL_TEXTURE_WRAP_T,
                             LOCAL_GL_CLAMP_TO_EDGE);
  aGLContext->fTexParameteri(LOCAL_GL_TEXTURE_2D, LOCAL_GL_TEXTURE_MAG_FILTER,
                             LOCAL_GL_LINEAR);
  aGLContext->fTexParameteri(LOCAL_GL_TEXTURE_2D, LOCAL_GL_TEXTURE_MIN_FILTER,
                             LOCAL_GL_LINEAR);
  aGLContext->fEGLImageTargetTexture2D(LOCAL_GL_TEXTURE_2D, mEGLImage);

  releaseTextures.release();
  return true;
}

bool DMABufSurfaceRGBA::HoldsTexture() { return mTexture || mEGLImage; }

void DMABufSurfaceRGBA::ReleaseTextures() {
  LOGDMABUF("DMABufSurfaceRGBA::ReleaseTextures() UID %d\n", mUID);
  FenceDelete();

  if (!HoldsTexture()) {
    return;
  }

  if (!mGL) {
#ifdef NIGHTLY_BUILD
    MOZ_DIAGNOSTIC_ASSERT(mGL, "Missing GL context!");
#else
    NS_WARNING(
        "DMABufSurfaceRGBA::ReleaseTextures(): Missing GL context! We're "
        "leaking textures!");
    return;
#endif
  }

  const auto& gle = gl::GLContextEGL::Cast(mGL);
  const auto& egl = gle->mEgl;

  if (mTexture && mGL->MakeCurrent()) {
    mGL->fDeleteTextures(1, &mTexture);
    mTexture = 0;
  }

  if (mEGLImage != LOCAL_EGL_NO_IMAGE) {
    egl->fDestroyImage(mEGLImage);
    mEGLImage = LOCAL_EGL_NO_IMAGE;
  }
  mGL = nullptr;
}

void DMABufSurfaceRGBA::ReleaseSurface() {
  LOGDMABUF("DMABufSurfaceRGBA::ReleaseSurface() UID %d", mUID);
  MOZ_ASSERT(!IsMapped(), "We can't release mapped buffer!");
  ReleaseTextures();
  ReleaseDMABuf();
  LogMemorySubRGBA(GetUID(), GetUsedMemoryRGBA());
}

#ifdef MOZ_WAYLAND
wl_buffer* DMABufSurfaceRGBA::CreateWlBuffer() {
  nsWaylandDisplay* waylandDisplay = widget::WaylandDisplayGet();
  auto* dmabuf = waylandDisplay->GetDmabuf();
  if (!dmabuf) {
    gfxCriticalNoteOnce
        << "DMABufSurfaceRGBA::CreateWlBuffer(): Missing DMABuf support!";
    return nullptr;
  }

  LOGDMABUF(
      "DMABufSurfaceRGBA::CreateWlBuffer() UID %d format %s size [%d x %d]",
      mUID, GetSurfaceTypeName(), GetWidth(), GetHeight());

  struct zwp_linux_buffer_params_v1* params =
      zwp_linux_dmabuf_v1_create_params(dmabuf);

  LOGDMABUF("  layer [0] modifier %" PRIx64, mBufferModifier);
  for (int i = 0; i < mBufferPlaneCount; i++) {
    zwp_linux_buffer_params_v1_add(
        params, mDmabufFds[i]->GetHandle(), i, mOffsets[i], mStrides[i],
        mBufferModifier >> 32, mBufferModifier & 0xffffffff);
  }

  LOGDMABUF(
      "  zwp_linux_buffer_params_v1_create_immed() [%d x %d], fourcc [%x]",
      GetWidth(), GetHeight(), GetFOURCCFormat());
  wl_buffer* buffer = zwp_linux_buffer_params_v1_create_immed(
      params, GetWidth(), GetHeight(), GetFOURCCFormat(), 0);
  if (!buffer) {
    LOGDMABUF(
        "  zwp_linux_buffer_params_v1_create_immed(): failed to create "
        "wl_buffer!");
  } else {
    LOGDMABUF("  created wl_buffer [%p]", buffer);
  }
  zwp_linux_buffer_params_v1_destroy(params);

  return buffer;
}
#endif

#ifdef MOZ_LOGGING
// We should synchronize DMA Buffer object access from CPU to avoid potential
// cache incoherency and data loss.
// See
// https://01.org/linuxgraphics/gfx-docs/drm/driver-api/dma-buf.html#cpu-access-to-dma-buffer-objects
struct dma_buf_sync {
  uint64_t flags;
};
#  define DMA_BUF_SYNC_READ (1 << 0)
#  define DMA_BUF_SYNC_WRITE (2 << 0)
#  define DMA_BUF_SYNC_START (0 << 2)
#  define DMA_BUF_SYNC_END (1 << 2)
#  define DMA_BUF_BASE 'b'
#  define DMA_BUF_IOCTL_SYNC _IOW(DMA_BUF_BASE, 0, struct dma_buf_sync)

static void SyncDmaBuf(int aFd, uint64_t aFlags) {
  struct dma_buf_sync sync = {0};

  sync.flags = aFlags | DMA_BUF_SYNC_READ | DMA_BUF_SYNC_WRITE;
  while (true) {
    int ret;
    ret = ioctl(aFd, DMA_BUF_IOCTL_SYNC, &sync);
    if (ret == -1 && errno == EINTR) {
      continue;
    } else if (ret == -1) {
      LOGDMABUFS("Failed to synchronize DMA buffer: %s FD %d", strerror(errno),
                 aFd);
      break;
    } else {
      break;
    }
  }
}

void* DMABufSurface::MapInternal(uint32_t aX, uint32_t aY, uint32_t aWidth,
                                 uint32_t aHeight, uint32_t* aStride,
                                 int aGbmFlags, int aPlane) {
  NS_ASSERTION(!IsMapped(aPlane), "Already mapped!");
  if (!mGbmBufferObject[aPlane]) {
    NS_WARNING("We can't map DMABufSurface without mGbmBufferObject");
    return nullptr;
  }

  LOGDMABUF(
      "DMABufSurface::MapInternal() UID %d plane %d size %d x %d -> %d x %d\n",
      mUID, aPlane, aX, aY, aWidth, aHeight);

  mMappedRegionStride[aPlane] = 0;
  mMappedRegionData[aPlane] = nullptr;
  mMappedRegion[aPlane] =
      GbmLib::Map(mGbmBufferObject[aPlane], aX, aY, aWidth, aHeight, aGbmFlags,
                  &mMappedRegionStride[aPlane], &mMappedRegionData[aPlane]);
  if (!mMappedRegion[aPlane]) {
    LOGDMABUF("    Surface mapping failed: %s", strerror(errno));
    return nullptr;
  }
  if (aStride) {
    *aStride = mMappedRegionStride[aPlane];
  }

  SyncDmaBuf(mDmabufFds[aPlane]->GetHandle(), DMA_BUF_SYNC_START);
  return mMappedRegion[aPlane];
}

void* DMABufSurfaceRGBA::MapReadOnly(uint32_t aX, uint32_t aY, uint32_t aWidth,
                                     uint32_t aHeight, uint32_t* aStride) {
  return MapInternal(aX, aY, aWidth, aHeight, aStride, GBM_BO_TRANSFER_READ);
}

void* DMABufSurfaceRGBA::MapReadOnly(uint32_t* aStride) {
  return MapInternal(0, 0, mWidth, mHeight, aStride, GBM_BO_TRANSFER_READ);
}

void* DMABufSurfaceRGBA::Map(uint32_t aX, uint32_t aY, uint32_t aWidth,
                             uint32_t aHeight, uint32_t* aStride) {
  return MapInternal(aX, aY, aWidth, aHeight, aStride,
                     GBM_BO_TRANSFER_READ_WRITE);
}

void* DMABufSurfaceRGBA::Map(uint32_t* aStride) {
  return MapInternal(0, 0, mWidth, mHeight, aStride,
                     GBM_BO_TRANSFER_READ_WRITE);
}

void DMABufSurface::Unmap(int aPlane) {
  if (mMappedRegion[aPlane]) {
    LOGDMABUF("DMABufSurface::Unmap() UID %d plane %d\n", mUID, aPlane);
    SyncDmaBuf(mDmabufFds[aPlane]->GetHandle(), DMA_BUF_SYNC_END);
    GbmLib::Unmap(mGbmBufferObject[aPlane], mMappedRegionData[aPlane]);
    mMappedRegion[aPlane] = nullptr;
    mMappedRegionData[aPlane] = nullptr;
    mMappedRegionStride[aPlane] = 0;
  }
}
#endif  // MOZ_LOGGING

nsresult DMABufSurface::BuildSurfaceDescriptorBuffer(
    SurfaceDescriptorBuffer& aSdBuffer, Image::BuildSdbFlags aFlags,
    const std::function<MemoryOrShmem(uint32_t)>& aAllocate) {
  return NS_ERROR_NOT_IMPLEMENTED;
}

#ifdef MOZ_LOGGING
void DMABufSurfaceRGBA::DumpToFile(const char* pFile) {
  uint32_t stride;

  if (!MapReadOnly(&stride)) {
    return;
  }
  cairo_surface_t* surface = nullptr;

  auto unmap = MakeScopeExit([&] {
    if (surface) {
      cairo_surface_destroy(surface);
    }
    Unmap();
  });

  surface = cairo_image_surface_create_for_data(
      (unsigned char*)mMappedRegion[0], CAIRO_FORMAT_ARGB32, mWidth, mHeight,
      stride);
  if (cairo_surface_status(surface) == CAIRO_STATUS_SUCCESS) {
    cairo_surface_write_to_png(surface, pFile);
  }
}
#endif

#if 0
// Copy from source surface by GL
#  include "GLBlitHelper.h"

bool DMABufSurfaceRGBA::CopyFrom(class DMABufSurface* aSourceSurface,
                                 GLContext* aGLContext) {
  MOZ_ASSERT(aSourceSurface->GetTexture());
  MOZ_ASSERT(GetTexture());

  gfx::IntSize size(GetWidth(), GetHeight());
  aGLContext->BlitHelper()->BlitTextureToTexture(aSourceSurface->GetTexture(),
    GetTexture(), size, size);
  return true;
}

void DMABufSurfaceRGBA::Clear() {
  uint32_t destStride;
  void* destData = Map(&destStride);
  memset(destData, 0, GetHeight() * destStride);
  Unmap();
}
#endif

#ifdef MOZ_LOGGING
void DMABufSurfaceRGBA::Clear(unsigned int aValue) {
  uint32_t destStride;
  void* destData = Map(&destStride);

  unsigned int* data = (unsigned int*)destData;
  for (unsigned int i = 0; i < (GetHeight() * destStride) >> 2; i++) {
    *data++ = aValue;
  }

  Unmap();
}
#endif

bool DMABufSurfaceRGBA::HasAlpha() {
  return mFOURCCFormat == GBM_FORMAT_ARGB8888 ||
         mFOURCCFormat == GBM_FORMAT_ABGR8888 ||
         mFOURCCFormat == GBM_FORMAT_RGBA8888 ||
         mFOURCCFormat == GBM_FORMAT_BGRA8888;
}

gfx::SurfaceFormat DMABufSurfaceRGBA::GetFormat() {
  switch (mFOURCCFormat) {
    case GBM_FORMAT_ARGB8888:
      return gfx::SurfaceFormat::B8G8R8A8;
    case GBM_FORMAT_ABGR8888:
      return gfx::SurfaceFormat::R8G8B8A8;
    case GBM_FORMAT_BGRA8888:
      return gfx::SurfaceFormat::A8R8G8B8;
    case GBM_FORMAT_RGBA8888:
      gfxCriticalError() << "DMABufSurfaceRGBA::GetFormat(): Unsupported "
                            "format GBM_FORMAT_RGBA8888";
      return gfx::SurfaceFormat::UNKNOWN;

    case GBM_FORMAT_XRGB8888:
      return gfx::SurfaceFormat::B8G8R8X8;
    case GBM_FORMAT_XBGR8888:
      return gfx::SurfaceFormat::R8G8B8X8;
    case GBM_FORMAT_BGRX8888:
      return gfx::SurfaceFormat::X8R8G8B8;
    case GBM_FORMAT_RGBX8888:
      gfxCriticalError() << "DMABufSurfaceRGBA::GetFormat(): Unsupported "
                            "format GBM_FORMAT_RGBX8888";
      return gfx::SurfaceFormat::UNKNOWN;

    default:
      gfxCriticalError() << "DMABufSurfaceRGBA::GetFormat(): Unknown format"
                         << gfx::hexa(mFOURCCFormat);
      return gfx::SurfaceFormat::UNKNOWN;
  }
}

already_AddRefed<DMABufSurfaceRGBA> DMABufSurfaceRGBA::CreateDMABufSurface(
    mozilla::gl::GLContext* aGLContext, int aWidth, int aHeight,
    int aDMABufSurfaceFlags, RefPtr<mozilla::widget::DRMFormat> aFormat) {
  RefPtr<DMABufSurfaceRGBA> surf = new DMABufSurfaceRGBA();
  if (!surf->Create(aGLContext, aWidth, aHeight, aDMABufSurfaceFlags,
                    aFormat)) {
    return nullptr;
  }
  return surf.forget();
}

already_AddRefed<DMABufSurface> DMABufSurfaceRGBA::CreateDMABufSurface(
    RefPtr<mozilla::gfx::FileHandleWrapper>&& aFd,
    const mozilla::webgpu::ffi::WGPUDMABufInfo& aDMABufInfo, int aWidth,
    int aHeight) {
  RefPtr<DMABufSurfaceRGBA> surf = new DMABufSurfaceRGBA();
  if (!surf->Create(std::move(aFd), aDMABufInfo, aWidth, aHeight)) {
    return nullptr;
  }
  return surf.forget();
}

already_AddRefed<DMABufSurfaceYUV> DMABufSurfaceYUV::CreateYUVSurface(
    const VADRMPRIMESurfaceDescriptor& aDesc, int aWidth, int aHeight) {
  RefPtr<DMABufSurfaceYUV> surf = new DMABufSurfaceYUV();
  LOGDMABUFS("[%p] DMABufSurfaceYUV::CreateYUVSurface() UID %d from desc\n",
             surf.get(), surf->GetUID());
  if (!surf->UpdateYUVData(aDesc, aWidth, aHeight, /* aCopy */ false)) {
    return nullptr;
  }
  return surf.forget();
}

already_AddRefed<DMABufSurfaceYUV> DMABufSurfaceYUV::CopyYUVSurface(
    const VADRMPRIMESurfaceDescriptor& aDesc, int aWidth, int aHeight) {
  RefPtr<DMABufSurfaceYUV> surf = new DMABufSurfaceYUV();
  LOGDMABUFS("[%p] DMABufSurfaceYUV::CreateYUVSurfaceCopy() UID %d from desc\n",
             surf.get(), surf->GetUID());
  if (!surf->UpdateYUVData(aDesc, aWidth, aHeight, /* aCopy */ true)) {
    return nullptr;
  }
  return surf.forget();
}

size_t DMABufSurfaceYUV::GetUsedMemoryYUV(int32_t aFOURCCFormat, int aWidth,
                                          int aHeight) {
  switch (aFOURCCFormat) {
    case VA_FOURCC_P010:
    case VA_FOURCC_P016:
      // one plane 16b + two planes 16b (half sized).
      return aWidth * aHeight * 2 + (aWidth >> 1) * (aHeight >> 1) * 4;
    case VA_FOURCC_NV12:
    case VA_FOURCC_YV12:
    case VA_FOURCC_I420:
      // one plane 8b + two planes 8b (half sized).
      return aWidth * aHeight + (aWidth >> 1) * (aHeight >> 1) * 2;
    default:
      gfxCriticalError()
          << "DMABufSurfaceYUV::GetUsedMemoryYUV(): unknown format: "
          << gfx::hexa(aFOURCCFormat);
      return 0;
  }
}

DMABufSurfaceYUV::DMABufSurfaceYUV()
    : DMABufSurface(SURFACE_YUV),
      mWidth(),
      mHeight(),
      mWidthAligned(),
      mHeightAligned(),
      mDrmFormats(),
      mTexture() {
  for (int i = 0; i < DMABUF_BUFFER_PLANES; i++) {
    mEGLImage[i] = LOCAL_EGL_NO_IMAGE;
    mBufferModifiers[i] = DRM_FORMAT_MOD_INVALID;
  }
}

DMABufSurfaceYUV::~DMABufSurfaceYUV() {
  LOGDMABUF("DMABufSurfaceYUV::~DMABufSurfaceYUV() UID %d", mUID);
  ReleaseSurface();
}

bool DMABufSurfaceYUV::OpenFileDescriptorForPlane(DMABufDeviceLock* aDeviceLock,
                                                  int aPlane) {
  // The fd is already opened, no need to reopen.
  // This can happen when we import dmabuf surface from VA-API decoder,
  // mGbmBufferObject is null and we don't close
  // file descriptors for surface as they are our only reference to it.
  if (mDmabufFds[aPlane]) {
    return true;
  }

  if (mGbmBufferObject[aPlane] == nullptr) {
    LOGDMABUF(
        "DMABufSurfaceYUV::OpenFileDescriptorForPlane: Missing "
        "mGbmBufferObject object!");
    return false;
  }

  auto rawFd = GbmLib::GetFd(mGbmBufferObject[aPlane]);
  if (rawFd < 0) {
    CloseFileDescriptors();
    return false;
  }
  mDmabufFds[aPlane] = new gfx::FileHandleWrapper(UniqueFileHandle(rawFd));

  return true;
}

bool DMABufSurfaceYUV::ImportPRIMESurfaceDescriptor(
    const VADRMPRIMESurfaceDescriptor& aDesc, int aWidth, int aHeight) {
  LOGDMABUF("DMABufSurfaceYUV::ImportPRIMESurfaceDescriptor() UID %d FOURCC %x",
            mUID, aDesc.fourcc);
  // Already exists?
  MOZ_DIAGNOSTIC_ASSERT(!mDmabufFds[0]);

  if (aDesc.num_layers > DMABUF_BUFFER_PLANES ||
      aDesc.num_objects > DMABUF_BUFFER_PLANES) {
    LOGDMABUF("  Can't import, wrong layers/objects number (%d, %d)",
              aDesc.num_layers, aDesc.num_objects);
    return false;
  }
  mSurfaceType = SURFACE_YUV;
  mFOURCCFormat = aDesc.fourcc;
  mBufferPlaneCount = aDesc.num_layers;

  for (unsigned int i = 0; i < aDesc.num_layers; i++) {
    // All supported formats have 4:2:0 chroma sub-sampling.
    unsigned int subsample = i == 0 ? 0 : 1;

    unsigned int object = aDesc.layers[i].object_index[0];
    if (object >= aDesc.num_objects) {
      return false;
    }
    mBufferModifiers[i] = aDesc.objects[object].drm_format_modifier;
    mDrmFormats[i] = aDesc.layers[i].drm_format;
    mOffsets[i] = aDesc.layers[i].offset[0];
    mStrides[i] = aDesc.layers[i].pitch[0];
    mWidthAligned[i] = aDesc.width >> subsample;
    mHeightAligned[i] = aDesc.height >> subsample;
    mWidth[i] = aWidth >> subsample;
    mHeight[i] = aHeight >> subsample;
    LOGDMABUF("    plane %d size %d x %d format %x", i, mWidth[i], mHeight[i],
              mDrmFormats[i]);
  }

  LogMemoryAddYUV(GetUID(),
                  GetUsedMemoryYUV(aDesc.fourcc, aDesc.width, aDesc.height));
  return true;
}

void DMABufSurfaceYUV::ReleaseVADRMPRIMESurfaceDescriptor(
    VADRMPRIMESurfaceDescriptor& aDesc) {
  for (unsigned int i = 0; i < aDesc.num_layers; i++) {
    unsigned int object = aDesc.layers[i].object_index[0];
    if (object >= aDesc.num_objects) {
      continue;
    }
    if (aDesc.objects[object].fd != -1) {
      close(aDesc.objects[object].fd);
      aDesc.objects[object].fd = -1;
    }
  }
  LogMemorySubYUV(-1,
                  GetUsedMemoryYUV(aDesc.fourcc, aDesc.width, aDesc.height));
}

bool DMABufSurfaceYUV::MoveYUVDataImpl(const VADRMPRIMESurfaceDescriptor& aDesc,
                                       int aWidth, int aHeight) {
  if (!ImportPRIMESurfaceDescriptor(aDesc, aWidth, aHeight)) {
    return false;
  }
  for (unsigned int i = 0; i < aDesc.num_layers; i++) {
    unsigned int object = aDesc.layers[i].object_index[0];
    // Keep VADRMPRIMESurfaceDescriptor untouched and dup() dmabuf
    // file descriptors.
    auto rawFd = dup(aDesc.objects[object].fd);
    mDmabufFds[i] = new gfx::FileHandleWrapper(UniqueFileHandle(rawFd));
  }
  LogMemoryAddYUV(GetUID(),
                  GetUsedMemoryYUV(mFOURCCFormat, mWidth[0], mHeight[0]));
  return true;
}

bool DMABufSurfaceYUV::CreateYUVPlaneGBM(int aPlane, DRMFormat* aFormat) {
  LOGDMABUF(
      "DMABufSurfaceYUV::CreateYUVPlaneGBM() UID %d size %d x %d plane %d",
      mUID, mWidth[aPlane], mHeight[aPlane], aPlane);

  DMABufDeviceLock device;

  MOZ_DIAGNOSTIC_ASSERT(mGbmBufferObject[aPlane] == nullptr);

  if (aFormat && aFormat->UseModifiers()) {
    LOGDMABUF("    Creating with modifiers from DRMFormat");
    uint32_t modifiersNum = 0;
    const uint64_t* modifiers = aFormat->GetModifiers(modifiersNum);
    mGbmBufferObject[aPlane] = GbmLib::CreateWithModifiers2(
        device, mWidth[aPlane], mHeight[aPlane], mDrmFormats[aPlane], modifiers,
        modifiersNum, mGbmBufferFlags);
    if (mGbmBufferObject[aPlane]) {
      mBufferModifiers[aPlane] = GbmLib::GetModifier(mGbmBufferObject[aPlane]);
    }
  } else if (mBufferModifiers[aPlane] != DRM_FORMAT_MOD_INVALID) {
    LOGDMABUF(
        "    Creating with modifiers from DMABufSurface mBufferModifiers");
    mGbmBufferObject[aPlane] = GbmLib::CreateWithModifiers2(
        device, mWidth[aPlane], mHeight[aPlane], mDrmFormats[aPlane],
        mBufferModifiers + aPlane, 1, mGbmBufferFlags);
  }
  if (!mGbmBufferObject[aPlane]) {
    LOGDMABUF("    Creating without modifiers");
    mGbmBufferObject[aPlane] =
        GbmLib::Create(device, mWidth[aPlane], mHeight[aPlane],
                       mDrmFormats[aPlane], GBM_BO_USE_RENDERING);
    mBufferModifiers[aPlane] = DRM_FORMAT_MOD_INVALID;
  }
  if (!mGbmBufferObject[aPlane]) {
    LOGDMABUF("    Failed to create GbmBufferObject: %s", strerror(errno));
    return false;
  }

  mStrides[aPlane] = GbmLib::GetStride(mGbmBufferObject[aPlane]);
  mOffsets[aPlane] = GbmLib::GetOffset(mGbmBufferObject[aPlane], 0);
  mWidthAligned[aPlane] = mWidth[aPlane];
  mHeightAligned[aPlane] = mHeight[aPlane];

  if (!OpenFileDescriptorForPlane(&device, aPlane)) {
    return false;
  }

  return true;
}

bool DMABufSurfaceYUV::CreateYUVPlaneExport(GLContext* aGLContext, int aPlane) {
  LOGDMABUF(
      "DMABufSurfaceYUV::CreateYUVPlaneExport() UID %d size %d x %d plane %d",
      mUID, mWidth[aPlane], mHeight[aPlane], aPlane);

  MOZ_DIAGNOSTIC_ASSERT(aGLContext);
  mGL = aGLContext;
  auto releaseTextures = MakeScopeExit([&] { ReleaseTextures(); });

  MOZ_DIAGNOSTIC_ASSERT(!mEGLImage[aPlane]);
  MOZ_DIAGNOSTIC_ASSERT(!mTexture[aPlane]);

  mGL->fGenTextures(1, &mTexture[aPlane]);
  const ScopedBindTexture savedTex(mGL, mTexture[aPlane]);

  GLenum internalFormat;
  GLenum unpackFormat;
  GLenum sizeFormat;
  switch (mDrmFormats[aPlane]) {
    case GBM_FORMAT_R8:
      internalFormat = LOCAL_GL_R8;
      unpackFormat = LOCAL_GL_RED;
      sizeFormat = LOCAL_GL_UNSIGNED_BYTE;
      break;
    case GBM_FORMAT_GR88:
      internalFormat = LOCAL_GL_RG8;
      unpackFormat = LOCAL_GL_RG;
      sizeFormat = LOCAL_GL_UNSIGNED_BYTE;
      break;
    case GBM_FORMAT_R16:
      internalFormat = LOCAL_GL_R16;
      unpackFormat = LOCAL_GL_RED;
      sizeFormat = LOCAL_GL_UNSIGNED_SHORT;
      break;
    case GBM_FORMAT_GR1616:
      internalFormat = LOCAL_GL_RG16;
      unpackFormat = LOCAL_GL_RG;
      sizeFormat = LOCAL_GL_UNSIGNED_SHORT;
      break;
    default:
      gfxCriticalError()
          << "DMABufSurfaceYUV::CreateYUVPlaneExport(): Unsupported format:"
          << gfx::hexa(mDrmFormats[aPlane]);
      return false;
  }

  GLContext::LocalErrorScope errorScope(*mGL);
  mGL->fTexImage2D(LOCAL_GL_TEXTURE_2D, 0, internalFormat, mWidth[aPlane],
                   mHeight[aPlane], 0, unpackFormat, sizeFormat, nullptr);
  const auto err = errorScope.GetError();
  if (err) {
    if (err != LOCAL_GL_OUT_OF_MEMORY) {
      LOGDMABUF("  failed %x error %s", err,
                GLContext::GLErrorToString(err).c_str());
    }
    return false;
  }

  const auto buffer = reinterpret_cast<EGLClientBuffer>(mTexture[aPlane]);

  const auto& gle = gl::GLContextEGL::Cast(mGL);
  const auto& context = gle->mContext;
  const auto& egl = gle->mEgl;
  mEGLImage[aPlane] =
      egl->fCreateImage(context, LOCAL_EGL_GL_TEXTURE_2D, buffer, nullptr);
  if (mEGLImage[aPlane] == LOCAL_EGL_NO_IMAGE) {
    LOGDMABUF("  EGLImageKHR creation failed, EGL error %s",
              FormatEGLError(egl->mLib->fGetError()).c_str());
    return false;
  }

  int bufferPlaneCount = 0;
  if (!egl->fExportDMABUFImageQuery(mEGLImage[aPlane], mDrmFormats + aPlane,
                                    &bufferPlaneCount,
                                    mBufferModifiers + aPlane)) {
    LOGDMABUF("  ExportDMABUFImageQueryMESA failed, quit\n");
    return false;
  }
  if (bufferPlaneCount != 1) {
    LOGDMABUF("  wrong plane count %d, quit\n", bufferPlaneCount);
    return false;
  }
  int fds[DMABUF_BUFFER_PLANES] = {-1};
  if (!egl->fExportDMABUFImage(mEGLImage[aPlane], fds, mStrides + aPlane,
                               mOffsets + aPlane)) {
    LOGDMABUF("  ExportDMABUFImageMESA failed, quit\n");
    return false;
  }

  mDmabufFds[aPlane] = new gfx::FileHandleWrapper(UniqueFileHandle(fds[0]));
  if (!mDmabufFds[aPlane]) {
    LOGDMABUF("  ExportDMABUFImageMESA failed, mDmabufFds[%d] is invalid, quit",
              aPlane);
    return false;
  }

  LOGDMABUF("  imported size %d x %d format %x planes %d modifier %" PRIx64,
            mWidth[aPlane], mHeight[aPlane], mFOURCCFormat, mBufferPlaneCount,
            mBufferModifiers[aPlane]);

  releaseTextures.release();
  return true;
}

bool DMABufSurfaceYUV::CreateYUVPlane(GLContext* aGLContext, int aPlane,
                                      DRMFormat* aFormat) {
  if (gfx::gfxVars::UseDMABufSurfaceExport()) {
    if (!UseDmaBufExportExtension(aGLContext)) {
      LOGDMAVERBOSES(
          "DMABufSurfaceYUV::CreateYUVPlane(): quit, can't use "
          "UseDmaBufExportExtension.");
      return false;
    }
    return CreateYUVPlaneExport(aGLContext, aPlane);
  }
  return CreateYUVPlaneGBM(aPlane, aFormat);
}

bool DMABufSurfaceYUV::CopyYUVDataImpl(const VADRMPRIMESurfaceDescriptor& aDesc,
                                       int aWidth, int aHeight) {
  RefPtr<DMABufSurfaceYUV> tmpSurf = CreateYUVSurface(aDesc, aWidth, aHeight);
  if (!tmpSurf) {
    return false;
  }

  if (!ImportPRIMESurfaceDescriptor(aDesc, aWidth, aHeight)) {
    return false;
  }

  StaticMutexAutoLock lock(sSnapshotContextMutex);
  RefPtr<GLContext> context = ClaimSnapshotGLContext();
  auto releaseTextures = MakeScopeExit([&] {
    tmpSurf->ReleaseTextures();
    ReleaseTextures();
    ReturnSnapshotGLContext(context);
  });

  for (int i = 0; i < mBufferPlaneCount; i++) {
    if (!tmpSurf->CreateTexture(context, i)) {
      return false;
    }
    if (!CreateYUVPlane(context, i) || !CreateTexture(context, i)) {
      return false;
    }
    gfx::IntSize size(GetWidth(i), GetHeight(i));
    context->BlitHelper()->BlitTextureToTexture(
        tmpSurf->GetTexture(i), GetTexture(i), size, size, LOCAL_GL_TEXTURE_2D,
        LOCAL_GL_TEXTURE_2D);
  }

  LogMemoryAddYUV(GetUID(),
                  GetUsedMemoryYUV(mFOURCCFormat, mWidth[0], mHeight[0]));
  return true;
}

bool DMABufSurfaceYUV::UpdateYUVData(const VADRMPRIMESurfaceDescriptor& aDesc,
                                     int aWidth, int aHeight, bool aCopy) {
  LOGDMABUF("DMABufSurfaceYUV::UpdateYUVData() UID %d copy %d", mUID, aCopy);
  return aCopy ? CopyYUVDataImpl(aDesc, aWidth, aHeight)
               : MoveYUVDataImpl(aDesc, aWidth, aHeight);
}

bool DMABufSurfaceYUV::UpdateYUVData(
    const mozilla::layers::PlanarYCbCrData& aData,
    gfx::SurfaceFormat aImageFormat) {
  LOGDMABUF("DMABufSurfaceYUV::UpdateYUVData() PlanarYCbCrData.");

  gfx::SurfaceFormat targetFormat = GetHWFormat(aImageFormat);
  if (targetFormat == gfx::SurfaceFormat::UNKNOWN) {
    LOGDMABUF("DMABufSurfaceYUV::UpdateYUVData() wrong format!");
    return false;
  }

  StaticMutexAutoLock lock(sSnapshotContextMutex);
  RefPtr<GLContext> context = ClaimSnapshotGLContext();
  auto releaseTextures = MakeScopeExit([&] {
    ReleaseTextures();
    ReturnSnapshotGLContext(context);
  });

  gfx::IntSize size = aData.YPictureSize();

  mWidthAligned[0] = mWidth[0] = size.width;
  mHeightAligned[0] = mHeight[0] = size.height;
  mWidthAligned[1] = mWidth[1] = (size.width + 1) >> 1;
  mHeightAligned[1] = mHeight[1] = (size.height + 1) >> 1;
  mBufferPlaneCount = 2;

  // We use this YUV plane for direct rendering of YUV video as wl_buffer
  // for ask for scanout modifiers.
  mGbmBufferFlags = GBM_BO_USE_RENDERING | GBM_BO_USE_SCANOUT;

  switch (targetFormat) {
    case gfx::SurfaceFormat::P010:
      mFOURCCFormat = VA_FOURCC_P010;
      mDrmFormats[0] = GBM_FORMAT_R16;
      mDrmFormats[1] = GBM_FORMAT_GR1616;
      break;
    case gfx::SurfaceFormat::NV12:
      mFOURCCFormat = VA_FOURCC_NV12;
      mDrmFormats[0] = GBM_FORMAT_R8;
      mDrmFormats[1] = GBM_FORMAT_GR88;
      break;
    default:
      MOZ_DIAGNOSTIC_CRASH("Unsupported target format!");
      return false;
  }

  auto format = GetGlobalDMABufFormats()->GetDRMFormat(mFOURCCFormat);
  for (int i = 0; i < mBufferPlaneCount; i++) {
    if (!CreateYUVPlane(context, i, format)) {
      return false;
    }
    if (!CreateTexture(context, i)) {
      return false;
    }
  }

  LogMemoryAddYUV(GetUID(),
                  GetUsedMemoryYUV(mFOURCCFormat, mWidth[0], mHeight[0]));
  return context->BlitHelper()->BlitYCbCrImageToDMABuf(aData, this);
}

bool DMABufSurfaceYUV::Create(const SurfaceDescriptor& aDesc) {
  return ImportSurfaceDescriptor(aDesc);
}

bool DMABufSurfaceYUV::ImportSurfaceDescriptor(
    const SurfaceDescriptorDMABuf& aDesc) {
  mBufferPlaneCount = aDesc.fds().Length();
  MOZ_RELEASE_ASSERT(mBufferPlaneCount <= DMABUF_BUFFER_PLANES);
  if (mBufferPlaneCount <= 0 ||
      aDesc.width().Length() < (uint32_t)mBufferPlaneCount ||
      aDesc.height().Length() < (uint32_t)mBufferPlaneCount ||
      aDesc.widthAligned().Length() < (uint32_t)mBufferPlaneCount ||
      aDesc.heightAligned().Length() < (uint32_t)mBufferPlaneCount ||
      aDesc.format().Length() < (uint32_t)mBufferPlaneCount ||
      aDesc.modifier().Length() < (uint32_t)mBufferPlaneCount ||
      aDesc.strides().Length() < (uint32_t)mBufferPlaneCount ||
      aDesc.offsets().Length() < (uint32_t)mBufferPlaneCount) {
    return false;
  }

  mSurfaceType = SURFACE_YUV;
  mFOURCCFormat = aDesc.fourccFormat();
  mColorSpace = aDesc.yUVColorSpace();
  mColorRange = aDesc.colorRange();
  mColorPrimaries = aDesc.colorPrimaries();
  mTransferFunction = aDesc.transferFunction();
  mWPChromaLocation = aDesc.chromaLocation();
  mGbmBufferFlags = aDesc.flags();
  mUID = aDesc.uid();
  mPID = aDesc.pid();

  LOGDMABUF("DMABufSurfaceYUV::ImportSurfaceDescriptor() UID %d", mUID);

  for (int i = 0; i < mBufferPlaneCount; i++) {
    mDmabufFds[i] = aDesc.fds()[i];
    mWidth[i] = aDesc.width()[i];
    mHeight[i] = aDesc.height()[i];
    mWidthAligned[i] = aDesc.widthAligned()[i];
    mHeightAligned[i] = aDesc.heightAligned()[i];
    mDrmFormats[i] = aDesc.format()[i];
    mStrides[i] = aDesc.strides()[i];
    mOffsets[i] = aDesc.offsets()[i];
    mBufferModifiers[i] = aDesc.modifier()[i];
    LOGDMABUF("    plane %d fd %d size %d x %d format %x modifier %" PRIx64, i,
              mDmabufFds[i]->GetHandle(), mWidth[i], mHeight[i], mDrmFormats[i],
              mBufferModifiers[i]);
  }

  if (aDesc.fence().Length() > 0) {
    mSyncFd = aDesc.fence()[0];
  }

  if (aDesc.semaphoreFd()) {
    mSemaphoreFd = aDesc.semaphoreFd();
    mSemaphoreFdIsSyncFd = aDesc.semaphoreFdIsSyncFd();
  }

  if (aDesc.refCount().Length() > 0) {
    GlobalRefCountImport(aDesc.refCount()[0].ClonePlatformHandle().release());
  }

  LogMemoryAddYUV(GetUID(),
                  GetUsedMemoryYUV(mFOURCCFormat, mWidth[0], mHeight[0]));
  return true;
}

bool DMABufSurfaceYUV::Serialize(
    mozilla::layers::SurfaceDescriptor& aOutDescriptor) {
  AutoTArray<uint32_t, DMABUF_BUFFER_PLANES> width;
  AutoTArray<uint32_t, DMABUF_BUFFER_PLANES> height;
  AutoTArray<uint32_t, DMABUF_BUFFER_PLANES> widthBytes;
  AutoTArray<uint32_t, DMABUF_BUFFER_PLANES> heightBytes;
  AutoTArray<uint32_t, DMABUF_BUFFER_PLANES> format;
  AutoTArray<NotNull<RefPtr<gfx::FileHandleWrapper>>, DMABUF_BUFFER_PLANES> fds;
  AutoTArray<uint32_t, DMABUF_BUFFER_PLANES> strides;
  AutoTArray<uint32_t, DMABUF_BUFFER_PLANES> offsets;
  AutoTArray<uint64_t, DMABUF_BUFFER_PLANES> modifiers;
  AutoTArray<NotNull<RefPtr<gfx::FileHandleWrapper>>, 1> fenceFDs;
  AutoTArray<ipc::FileDescriptor, 1> refCountFDs;

  LOGDMABUF("DMABufSurfaceYUV::Serialize() UID %d", mUID);

  for (int i = 0; i < mBufferPlaneCount; i++) {
    width.AppendElement(mWidth[i]);
    height.AppendElement(mHeight[i]);
    widthBytes.AppendElement(mWidthAligned[i]);
    heightBytes.AppendElement(mHeightAligned[i]);
    format.AppendElement(mDrmFormats[i]);
    fds.AppendElement(WrapNotNull(mDmabufFds[i]));
    strides.AppendElement(mStrides[i]);
    offsets.AppendElement(mOffsets[i]);
    modifiers.AppendElement(mBufferModifiers[i]);
  }

  if (mSync && mSyncFd) {
    fenceFDs.AppendElement(WrapNotNull(mSyncFd));
  }

  if (mGlobalRefCountFd) {
    refCountFDs.AppendElement(ipc::FileDescriptor(GlobalRefCountExport()));
  }

  aOutDescriptor = SurfaceDescriptorDMABuf(
      mSurfaceType, mFOURCCFormat, modifiers, mGbmBufferFlags, fds, width,
      height, widthBytes, heightBytes, format, strides, offsets,
      GetYUVColorSpace(), mColorRange, mColorPrimaries, mTransferFunction,
      mWPChromaLocation, fenceFDs, mUID, mCanRecycle ? getpid() : 0,
      refCountFDs, mSemaphoreFd, mSemaphoreFdIsSyncFd);
  return true;
}

bool DMABufSurfaceYUV::CreateTexture(GLContext* aGLContext, int aPlane) {
  if (mTexture[aPlane]) {
    MOZ_DIAGNOSTIC_ASSERT(aGLContext);
    MOZ_DIAGNOSTIC_ASSERT(mGL == aGLContext);
    return true;
  }

  LOGDMABUF("DMABufSurfaceYUV::CreateTexture() UID %d plane %d", mUID, aPlane);

  if (!UseDmaBufGL(aGLContext)) {
    LOGDMABUF("  UseDmaBufGL() failed");
    return false;
  }

  MOZ_DIAGNOSTIC_ASSERT(aGLContext);
  MOZ_DIAGNOSTIC_ASSERT(!mGL || mGL == aGLContext);

  mGL = aGLContext;
  auto releaseTextures = MakeScopeExit([&] { ReleaseTextures(); });

  if (!aGLContext->MakeCurrent()) {
    LOGDMABUF("  Failed to make GL context current.");
    return false;
  }

  const auto& gle = gl::GLContextEGL::Cast(aGLContext);
  const auto& egl = gle->mEgl;

  if ((aPlane == 1) &&
      ((GetFOURCCFormat() == VA_FOURCC_NV12) ||
       (GetFOURCCFormat() == VA_FOURCC_P010) ||
       (GetFOURCCFormat() == VA_FOURCC_P016)) &&
      egl->IsExtensionSupported(
          EGLExtension::EXT_image_dma_buf_import_modifiers)) {
    uint32_t wasRG = (mDrmFormats[aPlane] == DRM_FORMAT_RG88 ||
                      mDrmFormats[aPlane] == DRM_FORMAT_RG1616);
    uint32_t wasGR = (mDrmFormats[aPlane] == DRM_FORMAT_GR88 ||
                      mDrmFormats[aPlane] == DRM_FORMAT_GR1616);

    EGLint modifierCount = 0;
    egl->mLib->fQueryDmaBufModifiersEXT(egl->mDisplay, mDrmFormats[aPlane], 0,
                                        nullptr, nullptr, &modifierCount);
    if (modifierCount <= 0 && (wasGR || wasRG)) {
      uint32_t swappedFormat = 0;
      if (GetFOURCCFormat() == VA_FOURCC_NV12) {
        swappedFormat = wasGR ? DRM_FORMAT_RG88 : DRM_FORMAT_GR88;
      } else if (GetFOURCCFormat() == VA_FOURCC_P010 ||
                 GetFOURCCFormat() == VA_FOURCC_P016) {
        swappedFormat = wasGR ? DRM_FORMAT_RG1616 : DRM_FORMAT_GR1616;
      }
      mDrmFormats[aPlane] = static_cast<int>(swappedFormat);

      egl->mLib->fQueryDmaBufModifiersEXT(egl->mDisplay, mDrmFormats[aPlane], 0,
                                          nullptr, nullptr, &modifierCount);
      int bits = GetFOURCCFormat() == VA_FOURCC_NV12 ? 8 : 16;
      LOGDMABUF("  EGL DMA-BUF import: swapped plane 1 to %s%d%d",
                wasGR ? "RG" : "GR", bits, bits);
    }
  }

  nsTArray<EGLint> attribs;
  attribs.AppendElement(LOCAL_EGL_WIDTH);
  attribs.AppendElement(mWidthAligned[aPlane]);
  attribs.AppendElement(LOCAL_EGL_HEIGHT);
  attribs.AppendElement(mHeightAligned[aPlane]);
  attribs.AppendElement(LOCAL_EGL_LINUX_DRM_FOURCC_EXT);
  attribs.AppendElement(mDrmFormats[aPlane]);
#define ADD_PLANE_ATTRIBS_NV12(plane_idx)                                 \
  attribs.AppendElement(LOCAL_EGL_DMA_BUF_PLANE##plane_idx##_FD_EXT);     \
  attribs.AppendElement(mDmabufFds[aPlane]->GetHandle());                 \
  attribs.AppendElement(LOCAL_EGL_DMA_BUF_PLANE##plane_idx##_OFFSET_EXT); \
  attribs.AppendElement((int)mOffsets[aPlane]);                           \
  attribs.AppendElement(LOCAL_EGL_DMA_BUF_PLANE##plane_idx##_PITCH_EXT);  \
  attribs.AppendElement((int)mStrides[aPlane]);                           \
  if (mBufferModifiers[aPlane] != DRM_FORMAT_MOD_INVALID) {               \
    attribs.AppendElement(                                                \
        LOCAL_EGL_DMA_BUF_PLANE##plane_idx##_MODIFIER_LO_EXT);            \
    attribs.AppendElement(mBufferModifiers[aPlane] & 0xFFFFFFFF);         \
    attribs.AppendElement(                                                \
        LOCAL_EGL_DMA_BUF_PLANE##plane_idx##_MODIFIER_HI_EXT);            \
    attribs.AppendElement(mBufferModifiers[aPlane] >> 32);                \
  }
  ADD_PLANE_ATTRIBS_NV12(0);
#undef ADD_PLANE_ATTRIBS_NV12
  attribs.AppendElement(LOCAL_EGL_NONE);

  LOGDMABUF(
      "Plane %d: attributes: "
      "fd=%d, offset=%u, pitch=%u, modifier=0x%llx, format=0x%x, size=%dx%d",
      aPlane, mDmabufFds[aPlane]->GetHandle(), mOffsets[aPlane],
      mStrides[aPlane], (unsigned long long)mBufferModifiers[aPlane],
      mDrmFormats[aPlane], mWidthAligned[aPlane], mHeightAligned[aPlane]);

  mEGLImage[aPlane] =
      egl->fCreateImage(LOCAL_EGL_NO_CONTEXT, LOCAL_EGL_LINUX_DMA_BUF_EXT,
                        nullptr, attribs.Elements());

  if (mEGLImage[aPlane] == LOCAL_EGL_NO_IMAGE) {
    LOGDMABUF("  EGLImageKHR creation failed, EGL error %s",
              FormatEGLError(egl->mLib->fGetError()).c_str());
    return false;
  }

  aGLContext->fGenTextures(1, &mTexture[aPlane]);
  const ScopedBindTexture savedTex(aGLContext, mTexture[aPlane]);
  aGLContext->fTexParameteri(LOCAL_GL_TEXTURE_2D, LOCAL_GL_TEXTURE_WRAP_S,
                             LOCAL_GL_CLAMP_TO_EDGE);
  aGLContext->fTexParameteri(LOCAL_GL_TEXTURE_2D, LOCAL_GL_TEXTURE_WRAP_T,
                             LOCAL_GL_CLAMP_TO_EDGE);
  aGLContext->fTexParameteri(LOCAL_GL_TEXTURE_2D, LOCAL_GL_TEXTURE_MAG_FILTER,
                             LOCAL_GL_LINEAR);
  aGLContext->fTexParameteri(LOCAL_GL_TEXTURE_2D, LOCAL_GL_TEXTURE_MIN_FILTER,
                             LOCAL_GL_LINEAR);
  aGLContext->fEGLImageTargetTexture2D(LOCAL_GL_TEXTURE_2D, mEGLImage[aPlane]);

  GLenum glErr = aGLContext->fGetError();

  if (glErr == 0) {
    LOGDMABUF("  Plane %d: zero-copy EGLImageTargetTexture2D succeeded",
              aPlane);
    releaseTextures.release();
    return true;
  }

  if (mBufferModifiers[aPlane] == DRM_FORMAT_MOD_LINEAR &&
      GetFOURCCFormat() == VA_FOURCC_NV12 &&
      CreateTextureViaCopyYUV(aGLContext, aPlane)) {
    LOGDMABUF("  Plane %d: successfully created texture via copy", aPlane);
    releaseTextures.release();
    return true;
  }

  if (mBufferModifiers[aPlane] == DRM_FORMAT_MOD_LINEAR &&
      (GetFOURCCFormat() == VA_FOURCC_P010 ||
       GetFOURCCFormat() == VA_FOURCC_P016) &&
      CreateTextureViaCopyP010(aGLContext, aPlane)) {
    LOGDMABUF("  Plane %d: successfully created P010 texture via copy", aPlane);
    releaseTextures.release();
    return true;
  }

  LOGDMABUF(
      "  CreateTexture failed: EGLImageTargetTexture2D glErr=0x%x, copy "
      "fallback not used or failed",
      glErr);
  return false;
}

bool DMABufSurfaceYUV::CreateTextureViaCopyP010(GLContext* aGLContext,
                                                int aPlane) {
  GLuint srcTex = 0;
  aGLContext->fGenTextures(1, &srcTex);
  aGLContext->fBindTexture(LOCAL_GL_TEXTURE_EXTERNAL, srcTex);
  aGLContext->fTexParameteri(LOCAL_GL_TEXTURE_EXTERNAL, LOCAL_GL_TEXTURE_WRAP_S,
                             LOCAL_GL_CLAMP_TO_EDGE);
  aGLContext->fTexParameteri(LOCAL_GL_TEXTURE_EXTERNAL, LOCAL_GL_TEXTURE_WRAP_T,
                             LOCAL_GL_CLAMP_TO_EDGE);
  aGLContext->fTexParameteri(LOCAL_GL_TEXTURE_EXTERNAL,
                             LOCAL_GL_TEXTURE_MAG_FILTER, LOCAL_GL_LINEAR);
  aGLContext->fTexParameteri(LOCAL_GL_TEXTURE_EXTERNAL,
                             LOCAL_GL_TEXTURE_MIN_FILTER, LOCAL_GL_LINEAR);
  aGLContext->fEGLImageTargetTexture2D(LOCAL_GL_TEXTURE_EXTERNAL,
                                       mEGLImage[aPlane]);
  if (aGLContext->fGetError() != 0) {
    aGLContext->fDeleteTextures(1, &srcTex);
    return false;
  }
  GLenum internalFormat = (aPlane == 0) ? LOCAL_GL_R16 : LOCAL_GL_RG16;
  aGLContext->fBindTexture(LOCAL_GL_TEXTURE_2D, mTexture[aPlane]);
  aGLContext->fTexStorage2D(LOCAL_GL_TEXTURE_2D, 1, internalFormat,
                            mWidth[aPlane], mHeight[aPlane]);
  aGLContext->fCopyImageSubData(srcTex, LOCAL_GL_TEXTURE_EXTERNAL, 0, 0, 0, 0,
                                mTexture[aPlane], LOCAL_GL_TEXTURE_2D, 0, 0, 0,
                                0, mWidth[aPlane], mHeight[aPlane], 1);
  aGLContext->fDeleteTextures(1, &srcTex);
  return aGLContext->fGetError() == 0;
}

bool DMABufSurfaceYUV::CreateTextureViaCopyYUV(GLContext* aGLContext,
                                               int aPlane) {
  GLuint srcTex = 0;
  aGLContext->fGenTextures(1, &srcTex);
  aGLContext->fBindTexture(LOCAL_GL_TEXTURE_EXTERNAL, srcTex);
  aGLContext->fTexParameteri(LOCAL_GL_TEXTURE_EXTERNAL, LOCAL_GL_TEXTURE_WRAP_S,
                             LOCAL_GL_CLAMP_TO_EDGE);
  aGLContext->fTexParameteri(LOCAL_GL_TEXTURE_EXTERNAL, LOCAL_GL_TEXTURE_WRAP_T,
                             LOCAL_GL_CLAMP_TO_EDGE);
  aGLContext->fTexParameteri(LOCAL_GL_TEXTURE_EXTERNAL,
                             LOCAL_GL_TEXTURE_MAG_FILTER, LOCAL_GL_LINEAR);
  aGLContext->fTexParameteri(LOCAL_GL_TEXTURE_EXTERNAL,
                             LOCAL_GL_TEXTURE_MIN_FILTER, LOCAL_GL_LINEAR);
  aGLContext->fEGLImageTargetTexture2D(LOCAL_GL_TEXTURE_EXTERNAL,
                                       mEGLImage[aPlane]);
  if (aGLContext->fGetError() != 0) {
    aGLContext->fDeleteTextures(1, &srcTex);
    return false;
  }
  GLenum internalFormat = (aPlane == 0) ? LOCAL_GL_R8 : LOCAL_GL_RG8;
  aGLContext->fBindTexture(LOCAL_GL_TEXTURE_2D, mTexture[aPlane]);
  aGLContext->fTexStorage2D(LOCAL_GL_TEXTURE_2D, 1, internalFormat,
                            mWidth[aPlane], mHeight[aPlane]);
  aGLContext->fCopyImageSubData(srcTex, LOCAL_GL_TEXTURE_EXTERNAL, 0, 0, 0, 0,
                                mTexture[aPlane], LOCAL_GL_TEXTURE_2D, 0, 0, 0,
                                0, mWidth[aPlane], mHeight[aPlane], 1);
  aGLContext->fDeleteTextures(1, &srcTex);
  return aGLContext->fGetError() == 0;
}

bool DMABufSurfaceYUV::HoldsTexture() {
  for (int i = 0; i < mBufferPlaneCount; i++) {
    if (mTexture[i] || mEGLImage[i]) {
      return true;
    }
  }
  return false;
}

void DMABufSurfaceYUV::ReleaseTextures() {
  LOGDMABUF("DMABufSurfaceYUV::ReleaseTextures() UID %d", mUID);

  FenceDelete();

  if (!HoldsTexture()) {
    return;
  }

  if (!mGL) {
#ifdef NIGHTLY_BUILD
    MOZ_DIAGNOSTIC_ASSERT(mGL, "Missing GL context!");
#else
    NS_WARNING(
        "DMABufSurfaceYUV::ReleaseTextures(): Missing GL context! We're "
        "leaking textures!");
    return;
#endif
  }

  if (!mGL->MakeCurrent()) {
    NS_WARNING(
        "DMABufSurfaceYUV::ReleaseTextures(): MakeCurrent failed. We're "
        "leaking textures!");
    return;
  }

  mGL->fDeleteTextures(DMABUF_BUFFER_PLANES, mTexture);
  std::fill(std::begin(mTexture), std::end(mTexture), 0);

  const auto& gle = gl::GLContextEGL::Cast(mGL);
  const auto& egl = gle->mEgl;
  for (int i = 0; i < mBufferPlaneCount; i++) {
    if (mEGLImage[i] != LOCAL_EGL_NO_IMAGE) {
      egl->fDestroyImage(mEGLImage[i]);
      mEGLImage[i] = LOCAL_EGL_NO_IMAGE;
    }
  }

  mGL = nullptr;
}

bool DMABufSurfaceYUV::VerifyTextureCreation() {
  LOGDMABUF("DMABufSurfaceYUV::VerifyTextureCreation() UID %d", mUID);

  StaticMutexAutoLock lock(sSnapshotContextMutex);
  RefPtr<GLContext> context = ClaimSnapshotGLContext();
  auto release = MakeScopeExit([&] {
    ReleaseTextures();
    ReturnSnapshotGLContext(context);
  });

  for (int i = 0; i < mBufferPlaneCount; i++) {
    if (!CreateTexture(context, i)) {
      LOGDMABUF("  failed to create EGL image!");
      return false;
    }
  }

  LOGDMABUF("  success");
  return true;
}

gfx::SurfaceFormat DMABufSurfaceYUV::GetFormat() {
  switch (mFOURCCFormat) {
    case VA_FOURCC_P010:
      return gfx::SurfaceFormat::P010;
    case VA_FOURCC_P016:
      return gfx::SurfaceFormat::P016;
    case VA_FOURCC_NV12:
      return gfx::SurfaceFormat::NV12;
    case VA_FOURCC_YV12:
    case VA_FOURCC_I420:
      return gfx::SurfaceFormat::YUV420;
    default:
      gfxCriticalNoteOnce << "DMABufSurfaceYUV::GetFormat() unknown format: "
                          << mFOURCCFormat;
      return gfx::SurfaceFormat::UNKNOWN;
  }
}

gfx::SurfaceFormat DMABufSurfaceYUV::GetHWFormat(gfx::SurfaceFormat aSWFormat) {
  switch (aSWFormat) {
    case gfx::SurfaceFormat::YUV420P10:
      return gfx::SurfaceFormat::P010;
    case gfx::SurfaceFormat::YUV420:
      return gfx::SurfaceFormat::NV12;
    default:
      return gfx::SurfaceFormat::UNKNOWN;
  }
}

int DMABufSurfaceYUV::GetTextureCount() { return mBufferPlaneCount; }

void DMABufSurfaceYUV::ReleaseSurface() {
  LOGDMABUF("DMABufSurfaceYUV::ReleaseSurface() UID %d", mUID);
  ReleaseTextures();
  ReleaseDMABuf();
  LogMemorySubYUV(GetUID(),
                  GetUsedMemoryYUV(mFOURCCFormat, mWidth[0], mHeight[0]));
}

nsresult DMABufSurfaceYUV::BuildSurfaceDescriptorBuffer(
    SurfaceDescriptorBuffer& aSdBuffer, Image::BuildSdbFlags aFlags,
    const std::function<MemoryOrShmem(uint32_t)>& aAllocate) {
  LOGDMABUF("DMABufSurfaceYUV::BuildSurfaceDescriptorBuffer UID %d", mUID);

  gfx::IntSize size(GetWidth(), GetHeight());
  const auto format = gfx::SurfaceFormat::B8G8R8A8;

  uint8_t* buffer = nullptr;
  int32_t stride = 0;
  nsresult rv = Image::AllocateSurfaceDescriptorBufferRgb(
      size, format, buffer, aSdBuffer, stride, aAllocate);
  if (NS_WARN_IF(NS_FAILED(rv))) {
    LOGDMABUF("BuildSurfaceDescriptorBuffer allocate descriptor failed");
    return rv;
  }

  if (mGL) {
    return ReadIntoBuffer(mGL, buffer, stride, size, format);
  } else {
    // We're missing active GL context - take a snapshot one.
    StaticMutexAutoLock lock(sSnapshotContextMutex);
    RefPtr<GLContext> context = ClaimSnapshotGLContext();
    auto releaseTextures = mozilla::MakeScopeExit([&] {
      ReleaseTextures();
      ReturnSnapshotGLContext(context);
    });
    return ReadIntoBuffer(context, buffer, stride, size, format);
  }
}

#if 0
// Debugging / testing only
void DMABufSurfaceYUV::ClearPlane(int aPlane, int aValue) {
  if (!MapInternal(0, 0, mWidth[aPlane], mHeight[aPlane], nullptr,
                   GBM_BO_TRANSFER_WRITE, aPlane)) {
    return;
  }
  if ((int)mMappedRegionStride[aPlane] < mWidth[aPlane]) {
    return;
  }

  unsigned short* data = (unsigned short*)mMappedRegion[aPlane];
  for (unsigned int i = 0; i < (mMappedRegionStride[aPlane] * mHeight[aPlane]) >> 1; i++) {
    *data++ = aValue;
  }

  Unmap(aPlane);
}

void DMABufSurfaceYUV::CopyPlane(int aPlane, char* aData) {
  if (!MapInternal(0, 0, mWidth[aPlane], mHeight[aPlane], nullptr,
                   GBM_BO_TRANSFER_WRITE, aPlane)) {
    return;
  }
  if ((int)mMappedRegionStride[aPlane] < mWidth[aPlane]) {
    return;
  }

  /*
    memcpy((char*)mMappedRegion[aPlane], aData,
            mMappedRegionStride[aPlane] * mHeight[aPlane]);
  */

  unsigned short* dst = (unsigned short*)mMappedRegion[aPlane];
  unsigned short* src = (unsigned short*)aData;
  for (unsigned int i = 0; i < (mMappedRegionStride[aPlane] * mHeight[aPlane]) >> 1; i++) {
    // YUV -> P010 biteshift
    *dst++ = *src++ << 6;
  }

  Unmap(aPlane);
}
#endif

#ifdef MOZ_WAYLAND
wl_buffer* DMABufSurfaceYUV::CreateWlBuffer() {
  nsWaylandDisplay* waylandDisplay = widget::WaylandDisplayGet();
  auto* dmabuf = waylandDisplay->GetDmabuf();
  if (!dmabuf) {
    gfxCriticalNoteOnce
        << "DMABufSurfaceYUV::CreateWlBuffer(): Missing DMABuf support!";
    return nullptr;
  }

  LOGDMABUF(
      "DMABufSurfaceYUV::CreateWlBuffer() UID %d format %s size [%d x %d]",
      mUID, GetSurfaceTypeName(), GetWidth(), GetHeight());

  struct zwp_linux_buffer_params_v1* params =
      zwp_linux_dmabuf_v1_create_params(dmabuf);
  for (int i = 0; i < GetTextureCount(); i++) {
    LOGDMABUF("  layer [%d] modifier %" PRIx64, i, mBufferModifiers[i]);
    zwp_linux_buffer_params_v1_add(
        params, mDmabufFds[i]->GetHandle(), i, mOffsets[i], mStrides[i],
        mBufferModifiers[i] >> 32, mBufferModifiers[i] & 0xffffffff);
  }

  // The format passed to wayland needs to be a DRM_FORMAT_* enum.  These are
  // largely the same as VA_FOURCC_* values except for I420/YUV420
  uint32_t format = GetFOURCCFormat();
  if (format == VA_FOURCC_I420) {
    format = DRM_FORMAT_YUV420;
  }

  LOGDMABUF(
      "  zwp_linux_buffer_params_v1_create_immed() [%d x %d], fourcc [%x]",
      GetWidth(), GetHeight(), format);
  wl_buffer* buffer = zwp_linux_buffer_params_v1_create_immed(
      params, GetWidth(), GetHeight(), format, 0);
  if (!buffer) {
    LOGDMABUF(
        "  zwp_linux_buffer_params_v1_create_immed(): failed to create "
        "wl_buffer!");
  } else {
    LOGDMABUF("  created wl_buffer [%p]", buffer);
  }

  return buffer;
}
#endif

#if 0
void DMABufSurfaceYUV::ClearPlane(int aPlane) {
  if (!MapInternal(0, 0, mWidth[aPlane], mHeight[aPlane], nullptr,
                   GBM_BO_TRANSFER_WRITE, aPlane)) {
    return;
  }
  if ((int)mMappedRegionStride[aPlane] < mWidth[aPlane]) {
    return;
  }
  memset((char*)mMappedRegion[aPlane], 0,
         mMappedRegionStride[aPlane] * mHeight[aPlane]);
  Unmap(aPlane);
}

#  include "gfxUtils.h"

void DMABufSurfaceYUV::DumpToFile(const char* aFile) {
  RefPtr<gfx::DataSourceSurface> surf = GetAsSourceSurface();
  gfxUtils::WriteAsPNG(surf, aFile);
}
#endif
