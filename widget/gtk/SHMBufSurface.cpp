/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "SHMBufSurface.h"

#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <sys/mman.h>

#include "GLContextEGL.h"
#include "GLContextProvider.h"
#include "GLUploadHelpers.h"
#include "ImageContainer.h"
#include "gfxPlatform.h"
#include "mozilla/ScopeExit.h"
#include "mozilla/ipc/SharedMemoryHandle.h"
#include "mozilla/layers/ImageDataSerializer.h"
#include "mozilla/webrender/RenderTextureHost.h"
#include "mozilla/widget/BufferSurface.h"
#include "mozilla/widget/DMABufSurface.h"
#include "nsGtkUtils.h"
#include "nsRegion.h"
#ifdef MOZ_WAYLAND
#  include "nsWaylandDisplay.h"
#endif

using namespace mozilla;
using namespace mozilla::wr;
using namespace mozilla::gfx;
using namespace mozilla::layers;
using namespace mozilla::widget;

#undef LOGDMABUF
#undef LOGDMABUFS
#ifdef MOZ_LOGGING
#  include "mozilla/Logging.h"

extern mozilla::LazyLogModule gDmabufLog;
#  define LOGDMABUF(str, ...)                     \
    MOZ_LOG(gDmabufLog, mozilla::LogLevel::Debug, \
            ("%s: " str, GetDebugTag().get(), ##__VA_ARGS__))
#  define LOGDMABUFS(str, ...) \
    MOZ_LOG(gDmabufLog, mozilla::LogLevel::Debug, (str, ##__VA_ARGS__))
#else
#  define LOGDMABUF(str, ...)
#  define LOGDMABUFS(str, ...)
#endif /* MOZ_LOGGING */

void* SHMBufSurface::GetImageData() {
  // backed by external data
  if (mBuffer) {
    return mBuffer;
  }
#ifdef MOZ_WAYLAND
  if (!mShm) {
    mShm = mShmHandle.Map();
    if (!mShm) {
      NS_WARNING("SHMBufSurface: Failed to map Shm!");
      return nullptr;
    }
  }
  return mShm.Address();
#else
  return nullptr;
#endif
}

SHMBufSurface::SHMBufSurface() {
  LOGDMABUF("SHMBufSurface::SHMBufSurface()");
  // SHM surfaces can't be recycled
  DisableRecycle();
}

SHMBufSurface::~SHMBufSurface() {
  LOGDMABUF("SHMBufSurface::~SHMBufSurface()");
  ReleaseTextures();
}

bool SHMBufSurface::CreateTexture(mozilla::gl::GLContext* aGLContext,
                                  int aPlane) {
  MOZ_DIAGNOSTIC_ASSERT(aGLContext);
  MOZ_DIAGNOSTIC_ASSERT(aPlane >= 0 && aPlane < GetTextureCount());
  MOZ_DIAGNOSTIC_ASSERT(!mGL || mGL == aGLContext);

  if (mTexture[aPlane] && !mTextureIsDirty) {
    return true;
  }

  LOGDMABUF("SHMBufSurface::CreateTexture() plane %d dirty %d", aPlane,
            mTextureIsDirty);

  if (!GetImageData()) {
    LOGDMABUF("  missing surface data!");
    return false;
  }

  if (!aGLContext->MakeCurrent()) {
    LOGDMABUF("  failed to make GL context current");
    return false;
  }

  mGL = aGLContext;
  auto releaseTextures = MakeScopeExit([&] { ReleaseTextures(); });

  const bool needInit = !mTexture[aPlane];
  if (needInit) {
    mGL->fGenTextures(1, &mTexture[aPlane]);
  }
  if (!UploadTexture(aPlane, needInit)) {
    LOGDMABUF("  failed to upload plane %d data to texture!", aPlane);
    return false;
  }
  if (needInit) {
    SetTextureFilters(mGL, mTexture[aPlane]);
  }

  releaseTextures.release();
  return true;
}

/* static */
RefPtr<SHMBufSurfaceRGBA> SHMBufSurfaceRGBA::Create(const IntSize& aSize,
                                                    int32_t aFOURCCFormat) {
  RefPtr<SHMBufSurfaceRGBA> surface = new SHMBufSurfaceRGBA();
  if (!surface->CreateImpl(aSize, aFOURCCFormat)) {
    LOGDMABUFS("SHMBufSurfaceRGBA::Create() failed size [%d x %d] format %x",
               aSize.width, aSize.height, aFOURCCFormat);
    return nullptr;
  }
  LOGDMABUFS("SHMBufSurfaceRGBA::Create() [%p] [%d x %d] format %x",
             (void*)surface, aSize.width, aSize.height, aFOURCCFormat);
  return surface;
}

/* static */
RefPtr<SHMBufSurfaceRGBA> SHMBufSurfaceRGBA::Create(
    uint8_t* aBuffer, const mozilla::layers::BufferDescriptor& aDescriptor) {
  RefPtr<SHMBufSurfaceRGBA> surface = new SHMBufSurfaceRGBA();
  if (!surface->CreateImpl(aBuffer, aDescriptor)) {
    LOGDMABUFS("SHMBufSurfaceRGBA::Create() failed size [%d x %d] format %x",
               surface->GetWidth(), surface->GetHeight(),
               surface->mFOURCCFormat);
    return nullptr;
  }
  LOGDMABUFS("SHMBufSurfaceRGBA::Create() [%p] [%d x %d] format %x",
             (void*)surface, surface->GetWidth(), surface->GetHeight(),
             surface->mFOURCCFormat);
  return surface;
}

SHMBufSurfaceRGBA::SHMBufSurfaceRGBA() {
  LOGDMABUF("SHMBufSurfaceRGBA::SHMBufSurfaceRGBA()");
  mBufferPlaneCount = 1;
}

bool SHMBufSurfaceRGBA::CreateImpl(const IntSize& aSize,
                                   int32_t aFOURCCFormat) {
#ifdef MOZ_WAYLAND
  nsWaylandDisplay* waylandDisplay = WaylandDisplayGet();
  if (!waylandDisplay->GetShm()) {
    NS_WARNING("SHMBufSurfaceRGBA: Missing Wayland shm interface!");
    return false;
  }

  mWidth[0] = aSize.width;
  mHeight[0] = aSize.height;
  mFOURCCFormat = aFOURCCFormat;
  const int size = GetWidth() * GetHeight() * GetFormatBPP();
  mShmHandle = ipc::shared_memory::Create(size);
  if (!mShmHandle) {
    NS_WARNING("SHMBufSurfaceRGBA: Unable to allocate shared memory!");
    return false;
  }
  mShmPool = WUniquePtr<wl_shm_pool>(
      wl_shm_create_pool(waylandDisplay->GetShm(),
                         mShmHandle.Clone().TakePlatformHandle().get(), size));
  if (!mShmPool) {
    NS_WARNING("SHMBufSurfaceRGBA: Unable to allocate shared memory pool!");
    return false;
  }

  LOGDMABUF("SHMBufSurfaceRGBA::Create() size [%d x %d] format %x", GetWidth(),
            GetHeight(), mFOURCCFormat);
  return true;
#else
  return false;
#endif
}

bool SHMBufSurfaceRGBA::CreateImpl(
    uint8_t* aBuffer, const layers::BufferDescriptor& aDescriptor) {
  LOGDMABUF("SHMBufSurfaceRGBA::CreateImpl() buffer [%p]", aBuffer);
  const layers::RGBDescriptor& rgb = aDescriptor.get_RGBDescriptor();

  mBuffer = aBuffer;
  mColorPrimaries = rgb.colorSpace();
  mTransferFunction = rgb.transferFunction();
  mWidth[0] = rgb.size().width;
  mHeight[0] = rgb.size().height;
  SetFormat(rgb.format());

  return true;
}

already_AddRefed<DMABufSurface> SHMBufSurfaceRGBA::UploadToDMABufSurface(
    mozilla::gl::GLContext* aGLContext) {
  if (!CreateTextures(aGLContext)) {
    LOGDMABUF("SHMBufSurfaceRGBA::UploadToDMABufSurface() no texture!");
    return nullptr;
  }
  RefPtr surface = DMABufSurfaceRGBA::CreateDMABufSurface(
      aGLContext, GetTexture(), GetSize(), GetFOURCCFormat());
  if (!surface) {
    LOGDMABUF("SHMBufSurfaceRGBA::UploadToDMABufSurface() failed!");
    return nullptr;
  }
  LOGDMABUF("SHMBufSurfaceRGBA::UploadToDMABufSurface() buffer [%p]",
            GetImageData());
  surface->SetColorPrimaries(mColorPrimaries);
  surface->SetTransferFunction(mTransferFunction);
  surface->SetColorRange(mColorRange);
  surface->DisableRecycle();
  return surface.forget();
}

bool SHMBufSurfaceRGBA::UploadTexture(int aPlane, bool aNeedInit) {
  MOZ_DIAGNOSTIC_ASSERT(!aPlane, "SHM RGBA surface has one plane only!");

  LOGDMABUF("SHMBufSurfaceRGBA::UploadTexture()");

  // SHM surface is backed by a single tightly packed plane so we upload
  // it as a whole.
  const IntSize size = GetSize();
  const nsIntRegion region(IntRect(IntPoint(), size));
  return gl::UploadImageDataToTexture(
             mGL, static_cast<unsigned char*>(GetImageData()), size, IntPoint(),
             GetFormatBPP() * size.width, GetFormat(), region, mTexture[0],
             size,
             /* aOutUploadSize */ nullptr, aNeedInit) != SurfaceFormat::UNKNOWN;
}

already_AddRefed<gfx::DrawTarget> SHMBufSurfaceRGBA::Lock() {
  LOGDMABUF("SHMBufSurfaceRGBA::Lock()");
  return gfxPlatform::CreateDrawTargetForData(
      static_cast<unsigned char*>(GetImageData()), GetSize(),
      GetFormatBPP() * GetWidth(), GetFormat());
}

#ifdef MOZ_WAYLAND
wl_buffer* SHMBufSurfaceRGBA::CreateWlBuffer() {
  // We can't create wl_buffer on borrowed SHM memory.
  if (!mShmPool) {
    return nullptr;
  }
  auto* buffer =
      wl_shm_pool_create_buffer(mShmPool.get(), 0, GetWidth(), GetHeight(),
                                GetWidth() * GetFormatBPP(), GetWLFormat());
  LOGDMABUF("SHMBufSurfaceRGBA::CreateWlBuffer() wl_buffer [%p]", buffer);
  return buffer;
}
#endif

already_AddRefed<gfx::DataSourceSurface>
SHMBufSurfaceRGBA::GetAsSourceSurface() {
  LOGDMABUF("SHMBufSurfaceRGBA::GetAsSourceSurface()");

  gfx::IntSize size(GetWidth(), GetHeight());
  const auto format = gfx::SurfaceFormat::B8G8R8A8;
  RefPtr<gfx::DataSourceSurface> source =
      gfx::Factory::CreateDataSourceSurface(size, format);
  if (NS_WARN_IF(!source)) {
    LOGDMABUF(
        "SHMBufSurfaceRGBA::GetAsSourceSurface(): CreateDataSourceSurface "
        "failed.");
    return nullptr;
  }

  gfx::DataSourceSurface::ScopedMap map(source,
                                        gfx::DataSourceSurface::READ_WRITE);
  if (NS_WARN_IF(!map.IsMapped())) {
    LOGDMABUF(
        "SHMBufSurfaceRGBA::GetAsSourceSurface(): Mapping surface failed.");
    return nullptr;
  }
  if (map.GetStride() != GetWidth() * GetFormatBPP()) {
    LOGDMABUF("SHMBufSurfaceRGBA::GetAsSourceSurface(): wrong stride %d vs. %d",
              map.GetStride(), GetWidth() * GetFormatBPP());
    return nullptr;
  }

  memcpy(map.GetData(), GetImageData(),
         GetHeight() * GetWidth() * GetFormatBPP());
  return source.forget();
}

/* static */
RefPtr<SHMBufSurfaceYUV> SHMBufSurfaceYUV::Create(
    uint8_t* aBuffer, const layers::BufferDescriptor& aDescriptor) {
  if (aDescriptor.type() != layers::BufferDescriptor::TYCbCrDescriptor) {
    LOGDMABUFS("SHMBufSurfaceYUV::Create() RGBA/SHM is not suppored yet");
    return nullptr;
  }
  RefPtr<SHMBufSurfaceYUV> surface = new SHMBufSurfaceYUV();
  if (!surface->CreateImpl(aBuffer, aDescriptor)) {
    LOGDMABUFS("SHMBufSurfaceYUV::Create() failed buffer [%p]", aBuffer);
    return nullptr;
  }
  LOGDMABUFS("SHMBufSurfaceYUV::Create() [%p] buffer [%p]", (void*)surface,
             aBuffer);
  return surface;
}

SHMBufSurfaceYUV::SHMBufSurfaceYUV() {
  LOGDMABUF("SHMBufSurfaceYUV::SHMBufSurfaceYUV()");
}

bool SHMBufSurfaceYUV::CreateImpl(uint8_t* aBuffer,
                                  const layers::BufferDescriptor& aDescriptor) {
  LOGDMABUF("SHMBufSurfaceYUV::CreateImpl() buffer [%p]", aBuffer);

  mDescriptor = aDescriptor.get_YCbCrDescriptor();
  mBuffer = aBuffer;
  mColorSpace = mDescriptor.yUVColorSpace();
  mColorPrimaries = ToColorSpace2(mDescriptor.yUVColorSpace());
  mColorRange = mDescriptor.colorRange();
  mTransferFunction = mDescriptor.transferFunction();
  if (mDescriptor.hdrMetadata()) {
    mHDRMetadata = mDescriptor.hdrMetadata().value();
  }
  mColorDepth = Some(mDescriptor.colorDepth());
  mFOURCCFormat = VA_FOURCC_I420;
  // Right now we support YUV with separated planes only produced by
  // SW decoders.
  mBufferPlaneCount = 3;

  const IntSize ySize = mDescriptor.display().Size();
  const IntSize cbCrSize = ImageDataSerializer::GetCroppedCbCrSize(mDescriptor);
  mWidth[0] = ySize.width;
  mHeight[0] = ySize.height;
  mWidth[1] = mWidth[2] = cbCrSize.width;
  mHeight[1] = mHeight[2] = cbCrSize.height;

  return true;
}

bool SHMBufSurfaceYUV::UploadTexture(int aPlane, bool aNeedInit) {
  LOGDMABUF("SHMBufSurfaceYUV::UploadTexture() plane %d", aPlane);

  auto* buffer = static_cast<uint8_t*>(GetImageData());
  uint8_t* data = nullptr;
  int32_t stride = 0;
  switch (aPlane) {
    case 0:
      data = ImageDataSerializer::GetYChannel(buffer, mDescriptor);
      stride = mDescriptor.yStride();
      break;
    case 1:
      data = ImageDataSerializer::GetCbChannel(buffer, mDescriptor);
      stride = mDescriptor.cbCrStride();
      break;
    case 2:
      data = ImageDataSerializer::GetCrChannel(buffer, mDescriptor);
      stride = mDescriptor.cbCrStride();
      break;
    default:
      MOZ_CRASH("Wrong plane");
      break;
  }

  // Every plane holds a single color channel, 8 or 16 bits per pixel.
  const auto format = GetColorDepth() == gfx::ColorDepth::COLOR_8
                          ? SurfaceFormat::A8
                          : SurfaceFormat::A16;
  const IntSize size = GetSize(aPlane);

  const nsIntRegion region(IntRect(IntPoint(), size));
  return gl::UploadImageDataToTexture(mGL, data, size, IntPoint(), stride,
                                      format, region, mTexture[aPlane], size,
                                      /* aOutUploadSize */ nullptr,
                                      aNeedInit) != SurfaceFormat::UNKNOWN;
}

already_AddRefed<DMABufSurface> SHMBufSurfaceYUV::UploadToDMABufSurface(
    mozilla::gl::GLContext* aGLContext) {
  LOGDMABUF("SHMBufSurfaceYUV::UploadToDMABufSurface() buffer [%p]",
            GetImageData());

  if (!CreateTextures(aGLContext)) {
    LOGDMABUF("  failed to create plane textures!");
    return nullptr;
  }

  return DMABufSurfaceYUV::CreateYUVSurface(aGLContext, this);
}
