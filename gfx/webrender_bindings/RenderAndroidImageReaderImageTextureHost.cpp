/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "RenderAndroidImageReaderImageTextureHost.h"

#include "GLContext.h"
#include "GLReadTexImageHelper.h"
#include "OGLShaderConfig.h"
#include "mozilla/gfx/2D.h"
#include "mozilla/layers/AndroidHardwareBuffer.h"
#include "mozilla/layers/AndroidImageConsumer.h"
#include "mozilla/layers/AndroidImageReader.h"
#include "mozilla/layers/TextureHostOGL.h"
#include "mozilla/webrender/RenderThread.h"

namespace mozilla {
namespace wr {

RenderAndroidImageReaderImageTextureHost::
    RenderAndroidImageReaderImageTextureHost(
        const layers::GpuProcessAndroidImageReaderId aImageReaderId,
        const layers::AndroidMediaCodecFrameId aFrameId,
        const gfx::IntSize aSize, const gfx::SurfaceFormat aFormat)
    : mImageReaderId(aImageReaderId),
      mFrameId(aFrameId),
      mSize(aSize),
      mFormat(aFormat) {
  MOZ_COUNT_CTOR_INHERITED(RenderAndroidImageReaderImageTextureHost,
                           RenderTextureHost);
}

RenderAndroidImageReaderImageTextureHost::
    ~RenderAndroidImageReaderImageTextureHost() {
  MOZ_COUNT_DTOR_INHERITED(RenderAndroidImageReaderImageTextureHost,
                           RenderTextureHost);
}

gfx::IntSize RenderAndroidImageReaderImageTextureHost::GetSize() const {
  return mSize;
}

void RenderAndroidImageReaderImageTextureHost::SetReadFenceFd(
    UniqueFileHandle&& aFenceFd) {
  if (!mAndroidImageConsumer) {
    return;
  }
  mAndroidImageConsumer->SetReadFenceFd(std::move(aFenceFd));
}

bool RenderAndroidImageReaderImageTextureHost::EnsureLockable() {
  MOZ_RELEASE_ASSERT(mGL);

  if (!mAndroidImageConsumer) {
    auto imageReaderMap = layers::GpuProcessAndroidImageReaderMap::Get();
    if (!imageReaderMap) {
      return false;
    }

    mAndroidImageConsumer =
        imageReaderMap->GetImageConsumer(mImageReaderId, mGL);
    if (!mAndroidImageConsumer) {
      return false;
    }
  }

  MOZ_ASSERT(mAndroidImageConsumer);

  if (!mAndroidImageConsumer->PrepareTexture(mFrameId)) {
    return false;
  }

  if (mAndroidImageConsumer->GetSize() != mSize) {
    gfxCriticalNoteOnce << "AndroidImageReader size mismatch: actual="
                        << mAndroidImageConsumer->GetSize()
                        << ", expected=" << mSize;
  }
  MOZ_ASSERT(mAndroidImageConsumer->GetSize() == mSize);
  MOZ_RELEASE_ASSERT(mAndroidImageConsumer->GetFormat() == mFormat);

  const auto handle = mAndroidImageConsumer->GetGlTextureHandle();
  ActivateBindAndTexParameteri(mGL, LOCAL_GL_TEXTURE0,
                               LOCAL_GL_TEXTURE_EXTERNAL_OES, handle);
  return true;
}

wr::WrExternalImage RenderAndroidImageReaderImageTextureHost::Lock(
    uint8_t aChannelIndex, gl::GLContext* aGL) {
  MOZ_ASSERT(aChannelIndex == 0);

  if (mGL.get() != aGL) {
    if (mGL) {
      // This should not happen.
      MOZ_ASSERT_UNREACHABLE("Unexpected GL context");
      return InvalidToWrExternalImage();
    }
    mGL = aGL;
  }

  if (!mGL || !mGL->MakeCurrent()) {
    return InvalidToWrExternalImage();
  }

  if (!EnsureLockable()) {
    return InvalidToWrExternalImage();
  }

  const auto handle = mAndroidImageConsumer->GetGlTextureHandle();
  const gfx::IntSize size = GetSize();
  return NativeTextureToWrExternalImage(handle, 0.0, 0.0,
                                        static_cast<float>(size.width),
                                        static_cast<float>(size.height));
}

void RenderAndroidImageReaderImageTextureHost::Unlock() {}

size_t RenderAndroidImageReaderImageTextureHost::Bytes() {
  return GetSize().width * GetSize().height * BytesPerPixel(GetFormat());
}

gfx::SurfaceFormat RenderAndroidImageReaderImageTextureHost::GetFormat() const {
  MOZ_ASSERT(mFormat == gfx::SurfaceFormat::R8G8B8A8 ||
             mFormat == gfx::SurfaceFormat::R8G8B8X8);

  if (mFormat == gfx::SurfaceFormat::R8G8B8A8) {
    return gfx::SurfaceFormat::B8G8R8A8;
  }

  if (mFormat == gfx::SurfaceFormat::R8G8B8X8) {
    return gfx::SurfaceFormat::B8G8R8X8;
  }

  gfxCriticalNoteOnce
      << "Unexpected color format of RenderAndroidImageReaderImageTextureHost";

  return gfx::SurfaceFormat::UNKNOWN;
}

already_AddRefed<gfx::DataSourceSurface>
RenderAndroidImageReaderImageTextureHost::ReadTexImage() {
  if (!mGL) {
    mGL = RenderThread::Get()->SingletonGL();
    if (!mGL) {
      return nullptr;
    }
  }

  if (!EnsureLockable()) {
    return nullptr;
  }

  /* Allocate resulting image surface */
  int32_t stride = GetSize().width * BytesPerPixel(GetFormat());
  RefPtr<gfx::DataSourceSurface> surf =
      gfx::Factory::CreateDataSourceSurfaceWithStride(GetSize(), GetFormat(),
                                                      stride);
  if (!surf) {
    return nullptr;
  }

  layers::ShaderConfigOGL config = layers::ShaderConfigFromTargetAndFormat(
      LOCAL_GL_TEXTURE_EXTERNAL, GetFormat());
  int shaderConfig = config.mFeatures;

  const auto handle = mAndroidImageConsumer->GetGlTextureHandle();
  bool ret = mGL->ReadTexImageHelper()->ReadTexImage(
      surf, handle, LOCAL_GL_TEXTURE_EXTERNAL, GetSize(), gfx::Matrix4x4(),
      shaderConfig, /* aYInvert */ false);
  if (!ret) {
    return nullptr;
  }

  return surf.forget();
}

bool RenderAndroidImageReaderImageTextureHost::MapPlane(
    RenderCompositor* aCompositor, uint8_t aChannelIndex,
    PlaneInfo& aPlaneInfo) {
  RefPtr<gfx::DataSourceSurface> readback = ReadTexImage();
  if (!readback) {
    return false;
  }

  gfx::DataSourceSurface::MappedSurface map;
  if (!readback->Map(gfx::DataSourceSurface::MapType::READ, &map)) {
    return false;
  }

  mReadback = readback;
  aPlaneInfo.mSize = GetSize();
  aPlaneInfo.mStride = map.mStride;
  aPlaneInfo.mData = map.mData;
  return true;
}

void RenderAndroidImageReaderImageTextureHost::UnmapPlanes() {
  if (mReadback) {
    mReadback->Unmap();
    mReadback = nullptr;
  }
}

RefPtr<layers::TextureSource>
RenderAndroidImageReaderImageTextureHost::CreateTextureSource(
    layers::TextureSourceProvider* aProvider) {
  if (!mGL) {
    mGL = RenderThread::Get()->SingletonGL();
    if (!mGL) {
      return nullptr;
    }
  }
  if (!EnsureLockable()) {
    return nullptr;
  }
  if (!mAndroidImageConsumer) {
    return nullptr;
  }
  return new layers::AndroidImageReaderImageTextureSource(
      aProvider, mAndroidImageConsumer, GetFormat(), LOCAL_GL_TEXTURE_EXTERNAL,
      LOCAL_GL_CLAMP_TO_EDGE, GetSize());
}

}  // namespace wr
}  // namespace mozilla
