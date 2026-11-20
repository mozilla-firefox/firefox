/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef MOZILLA_LAYERS_AndroidImageConsumer
#define MOZILLA_LAYERS_AndroidImageConsumer

#include "GLTypes.h"
#include "mozilla/RefPtr.h"
#include "mozilla/StaticPtr.h"
#include "mozilla/UniquePtrExtensions.h"
#include "mozilla/gfx/2D.h"
#include "mozilla/gfx/Types.h"
#include "mozilla/layers/LayersTypes.h"
#include "nsISupportsImpl.h"

namespace mozilla {

namespace gl {
class GLContext;
}  // namespace gl

namespace layers {

class AndroidImageReader;
class AndroidImageWrapper;

/**
 * A class represents GL texture of current frame of AndroidImageReader that is
 * used by WebRender in RenderThread.
 */
class AndroidImageConsumer final {
 public:
  NS_INLINE_DECL_THREADSAFE_REFCOUNTING(AndroidImageConsumer)

  static already_AddRefed<AndroidImageConsumer> Create(
      AndroidImageReader* aImageReader, gl::GLContext* aGL);

  AndroidImageConsumer(AndroidImageReader* aImageReader, gl::GLContext* aGL,
                       const GLuint aTextureHandle);

  bool PrepareTexture(AndroidMediaCodecFrameId aFrameId);

  void SetReadFenceFd(UniqueFileHandle&& aFenceFd);

  GLuint GetGlTextureHandle() { return mTextureHandle; }
  gfx::SurfaceFormat GetFormat();
  gfx::IntSize GetSize();

  const GpuProcessAndroidImageReaderId mImageReaderId;
  const RefPtr<gl::GLContext> mGL;
  const GLuint mTextureHandle;

 protected:
  virtual ~AndroidImageConsumer();

  RefPtr<AndroidImageReader> mImageReader;
  AndroidMediaCodecFrameId mCurrentFrameId;
  RefPtr<AndroidImageWrapper> mCurrentImage;
};

}  // namespace layers
}  // namespace mozilla

#endif
