//
// Copyright 2023 The ANGLE Project Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//

// ShareGroup.h: Defines the egl::ShareGroup class, representing the collection of contexts in a
// share group.

#ifndef LIBANGLE_SHAREGROUP_H_
#define LIBANGLE_SHAREGROUP_H_

#include <mutex>
#include <type_traits>
#include <vector>

#include "common/FastVector.h"
#include "common/SimpleMutex.h"
#include "libANGLE/Context.h"
#include "libANGLE/ObjectMap.h"

namespace gl
{
class Context;
}  // namespace gl

namespace rx
{
class EGLImplFactory;
class ShareGroupImpl;
}  // namespace rx

namespace egl
{
class ContextMap final : public priv::ObjectMap<gl::Context, angle::SimpleMutex>
{
  public:
    void pruneUnreferenced(ContextMap *invalidContextMap)
    {
        std::lock_guard<angle::SimpleMutex> lock(mMutex);
        std::lock_guard<angle::SimpleMutex> invalidLock(invalidContextMap->mMutex);

        // Cache total number of contexts before invalidation. This is used as a check to verify
        // that no context is "lost" while being moved between the various sets.
        size_t contextSetSizeBeforeInvalidation =
            this->mObjects.size() + invalidContextMap->mObjects.size();

        // If app called eglTerminate and no active threads remain,
        // force release any context that is still current.
        angle::HashMap<GLuint, gl::Context *> contextsStillCurrent = {};
        for (auto context : this->mObjects)
        {
            if (context.second->isReferenced())
            {
                contextsStillCurrent.emplace(context);
                continue;
            }

            // Add context that is not current to mInvalidContextSet for cleanup.
            invalidContextMap->mObjects.emplace(context);
        }

        // There are many methods that require contexts that are still current to be present in
        // display's contextSet like during context release or to notify of state changes in a
        // subject. So as to not interrupt this flow, do not remove contexts that are still
        // current on some thread from display's contextSet even though eglTerminate marks such
        // contexts as invalid.
        //
        // "mState.contextSet" will now contain only those contexts that are still current on
        // some thread.
        this->mObjects = std::move(contextsStillCurrent);

        // Assert that the total number of contexts is the same before and after context
        // invalidation.
        ASSERT(contextSetSizeBeforeInvalidation ==
               this->mObjects.size() + invalidContextMap->mObjects.size());
    }

    template <typename ConditionType>
    void notifyDeviceLost(ConditionType *deviceLost) const
    {
        std::lock_guard<angle::SimpleMutex> lock(mMutex);
        if (*deviceLost)
        {
            return;
        }
        for (const auto &pair : this->mObjects)
        {
            pair.second->markContextLost(gl::GraphicsResetStatus::UnknownContextReset);
        }
        *deviceLost = true;
    }
};

class UnlockedContextMap final : public priv::ObjectMap<gl::Context, angle::NoOpMutex>
{
  public:
    size_t size() const { return mObjects.size(); }
};

using SharedContextMap = UnlockedContextMap;

class ShareGroupState final : angle::NonCopyable
{
  public:
    ShareGroupState();
    ~ShareGroupState();

    const SharedContextMap &getContexts() const { return mContexts; }
    void addSharedContext(gl::Context *context);
    void removeSharedContext(gl::Context *context);

    bool hasAnyContextWithRobustness() const { return mAnyContextWithRobustness; }
    bool hasAnyContextWithDisplayTextureShareGroup() const
    {
        return mAnyContextWithDisplayTextureShareGroup;
    }

  private:
    // The list of contexts within the share group
    SharedContextMap mContexts;

    // Whether any context in the share group has robustness enabled.  If any context in the share
    // group is robust, any program created in any context of the share group must have robustness
    // enabled.  This is because programs are shared between the share group contexts.
    bool mAnyContextWithRobustness;

    // Whether any context in the share group uses display shared textures.  This functionality is
    // provided by ANGLE_display_texture_share_group and allows textures to be shared between
    // contexts that are not in the same share group.
    bool mAnyContextWithDisplayTextureShareGroup;
};

class ShareGroup final : angle::NonCopyable
{
  public:
    ShareGroup(rx::EGLImplFactory *factory);

    void addRef();

    void release(const egl::ThreadSafeDisplay *display);

    rx::ShareGroupImpl *getImplementation() const { return mImplementation; }

    rx::UniqueSerial generateFramebufferSerial() { return mFramebufferSerialFactory.generate(); }

    angle::FrameCaptureShared *getFrameCaptureShared() { return mFrameCaptureShared.get(); }

    void finishAllContexts();

    const SharedContextMap &getContexts() const { return mState.getContexts(); }
    void addSharedContext(gl::Context *context);
    void removeSharedContext(gl::Context *context);

  protected:
    ~ShareGroup();

  private:
    size_t mRefCount;
    rx::ShareGroupImpl *mImplementation;
    rx::UniqueSerialFactory mFramebufferSerialFactory;

    // Note: we use a raw pointer here so we can exclude frame capture sources from the build.
    std::unique_ptr<angle::FrameCaptureShared> mFrameCaptureShared;

    ShareGroupState mState;
};

}  // namespace egl

#endif  // LIBANGLE_SHAREGROUP_H_
