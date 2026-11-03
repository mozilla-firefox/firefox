//
// Copyright 2015 The ANGLE Project Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//

// SyncGL.cpp: Implements the class methods for SyncGL.

#include "libANGLE/renderer/gl/SyncGL.h"

#include "common/debug.h"
#include "libANGLE/Context.h"
#include "libANGLE/renderer/gl/ContextGL.h"
#include "libANGLE/renderer/gl/FunctionsGL.h"
#include "libANGLE/renderer/gl/RendererGL.h"

namespace rx
{

SyncGL::SyncGL(const std::shared_ptr<RendererGL> &renderer)
    : SyncImpl(), mRenderer(renderer), mSyncObject(0), mDeferredBufferDeletionSerial(0)
{
    ASSERT(mRenderer);
}

SyncGL::~SyncGL()
{
    ASSERT(mSyncObject == 0);
}

void SyncGL::onDestroy(const gl::Context *context)
{
    ASSERT(mSyncObject != 0);
    mRenderer->getFunctions()->deleteSync(mSyncObject);
    mSyncObject = 0;
}

angle::Result SyncGL::set(const gl::Context *context, GLenum condition, GLbitfield flags)
{
    ASSERT(condition == GL_SYNC_GPU_COMMANDS_COMPLETE && flags == 0);
    ContextGL *contextGL = GetImplAs<ContextGL>(context);
    mSyncObject          = mRenderer->getFunctions()->fenceSync(condition, flags);
    ANGLE_CHECK(contextGL, mSyncObject != 0, "glFenceSync failed to create a GLsync object.",
                GL_OUT_OF_MEMORY);
    mDeferredBufferDeletionSerial = mRenderer->getStateManager()->onFenceSync();
    contextGL->markWorkSubmitted();
    return angle::Result::Continue;
}

angle::Result SyncGL::clientWait(const gl::Context *context,
                                 GLbitfield flags,
                                 GLuint64 timeout,
                                 GLenum *outResult)
{
    ASSERT(mSyncObject != 0);
    *outResult = mRenderer->getFunctions()->clientWaitSync(mSyncObject, flags, timeout);
    if (*outResult == GL_ALREADY_SIGNALED || *outResult == GL_CONDITION_SATISFIED)
    {
        mRenderer->getStateManager()->onSerialCompleted(mDeferredBufferDeletionSerial);
    }
    return angle::Result::Continue;
}

angle::Result SyncGL::serverWait(const gl::Context *context, GLbitfield flags, GLuint64 timeout)
{
    ASSERT(mSyncObject != 0);
    mRenderer->getFunctions()->waitSync(mSyncObject, flags, timeout);
    return angle::Result::Continue;
}

angle::Result SyncGL::getStatus(const gl::Context *context, GLint *outResult)
{
    ASSERT(mSyncObject != 0);
    mRenderer->getFunctions()->getSynciv(mSyncObject, GL_SYNC_STATUS, 1, nullptr, outResult);
    if (*outResult == GL_SIGNALED)
    {
        mRenderer->getStateManager()->onSerialCompleted(mDeferredBufferDeletionSerial);
    }
    return angle::Result::Continue;
}
}  // namespace rx
