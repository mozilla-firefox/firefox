/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "AndroidImageConsumer.h"

#include "GLContext.h"
#include "GLContextEGL.h"
#include "GLLibraryEGL.h"
#include "mozilla/layers/AndroidImageReader.h"
#include "mozilla/webrender/RenderThread.h"

namespace mozilla {
namespace layers {

/* static */
already_AddRefed<AndroidImageConsumer> AndroidImageConsumer::Create(
    AndroidImageReader* aImageReader, gl::GLContext* aGL) {
  MOZ_ASSERT(wr::RenderThread::IsInRenderThread());
  MOZ_ASSERT(aImageReader);
  MOZ_ASSERT(aGL);

  if ((!XRE_IsGPUProcess() && !XRE_IsParentProcess()) || !aImageReader ||
      !aGL) {
    MOZ_ASSERT_UNREACHABLE("unexpected to be called");
    return nullptr;
  }

  GLuint tex;
  aGL->fGenTextures(1, &tex);

  RefPtr<AndroidImageConsumer> imageConsumer =
      new AndroidImageConsumer(aImageReader, aGL, tex);
  return imageConsumer.forget();
}

AndroidImageConsumer::AndroidImageConsumer(AndroidImageReader* aImageReader,
                                           gl::GLContext* aGL,
                                           const GLuint aTextureHandle)
    : mImageReaderId(aImageReader->mImageReaderId),
      mGL(aGL),
      mTextureHandle(aTextureHandle),
      mImageReader(aImageReader) {
  MOZ_RELEASE_ASSERT(mImageReader);
  MOZ_RELEASE_ASSERT(mGL);
}

AndroidImageConsumer::~AndroidImageConsumer() {
  MOZ_ASSERT(wr::RenderThread::IsInRenderThread());

  auto imageReaderMap = layers::GpuProcessAndroidImageReaderMap::Get();
  if (imageReaderMap) {
    imageReaderMap->UnregisterImageConsumer(mImageReaderId);
  }

  mGL->fDeleteTextures(1, &mTextureHandle);
}

bool AndroidImageConsumer::PrepareTexture(AndroidMediaCodecFrameId aFrameId) {
  MOZ_ASSERT(wr::RenderThread::IsInRenderThread());

  // Release the previous image before requesting another acquisition slot.
  mCurrentImage = nullptr;
  RefPtr<AndroidImageWrapper> image = mImageReader->AcquireImage(aFrameId);
  if (!image || !mGL->MakeCurrent() ||
      !image->BindToTexture(mGL, mTextureHandle)) {
    return false;
  }

  mCurrentImage = std::move(image);
  return mCurrentImage->WaitForWriteFence(mGL);
}

gfx::SurfaceFormat AndroidImageConsumer::GetFormat() {
  if (!mCurrentImage) {
    return gfx::SurfaceFormat::UNKNOWN;
  }
  return mCurrentImage->mFormat;
}

gfx::IntSize AndroidImageConsumer::GetSize() {
  if (!mCurrentImage) {
    return gfx::IntSize();
  }
  return mCurrentImage->mSize;
}

void AndroidImageConsumer::SetReadFenceFd(UniqueFileHandle&& aFenceFd) {
  if (!mCurrentImage) {
    return;
  }
  if (!mCurrentImage->SetReadFenceFd(std::move(aFenceFd))) {
    // A failed merge leaves the other contexts' fences intact. Complete this
    // context's reads before the current acquired image can be replaced.
    mGL->fFinish();
  }
}

}  // namespace layers
}  // namespace mozilla
