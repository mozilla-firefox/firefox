//
// Copyright 2015 The ANGLE Project Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//

// SyncGL.h: Defines the class interface for SyncGL.

#ifndef LIBANGLE_RENDERER_GL_FENCESYNCGL_H_
#define LIBANGLE_RENDERER_GL_FENCESYNCGL_H_

#include "libANGLE/renderer/SyncImpl.h"
#include "libANGLE/renderer/gl/StateManagerGL.h"

#include <memory>

namespace rx
{
class RendererGL;

class SyncGL : public SyncImpl
{
  public:
    explicit SyncGL(const std::shared_ptr<RendererGL> &renderer);
    ~SyncGL() override;

    void onDestroy(const gl::Context *context) override;

    angle::Result set(const gl::Context *context, GLenum condition, GLbitfield flags) override;
    angle::Result clientWait(const gl::Context *context,
                             GLbitfield flags,
                             GLuint64 timeout,
                             GLenum *outResult) override;
    angle::Result serverWait(const gl::Context *context,
                             GLbitfield flags,
                             GLuint64 timeout) override;
    angle::Result getStatus(const gl::Context *context, GLint *outResult) override;

  private:
    std::shared_ptr<RendererGL> mRenderer;
    GLsync mSyncObject;
    StateManagerGL::ExecutionSerial mDeferredBufferDeletionSerial;
};
}  // namespace rx

#endif  // LIBANGLE_RENDERER_GL_FENCESYNCGL_H_
