//
// Copyright (c) 2020 The ANGLE Project Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// SyncMtl:
//    Defines the class interface for SyncMtl, implementing SyncImpl.
//

#include "libANGLE/renderer/metal/SyncMtl.h"

#include <chrono>
#include <condition_variable>
#include <mutex>

#include "common/debug.h"
#include "libANGLE/Context.h"
#include "libANGLE/Display.h"
#include "libANGLE/renderer/metal/ContextMtl.h"
#include "libANGLE/renderer/metal/DisplayMtl.h"

namespace rx
{
namespace mtl
{

static uint64_t UnpackSignalValue(EGLAttrib highPart, EGLAttrib lowPart)
{
    return (static_cast<uint64_t>(highPart & 0xFFFFFFFF) << 32) |
           (static_cast<uint64_t>(lowPart & 0xFFFFFFFF));
}

static constexpr uint64_t kNanosecondsPerDay = 86400000000000;
static uint64_t SanitizeTimeout(uint64_t timeout)
{
    // Passing EGL_FOREVER_KHR overflows std::chrono::nanoseconds.
    return std::min(timeout, kNanosecondsPerDay);
}

class SyncImpl
{
  public:
    virtual ~SyncImpl() {}

    virtual angle::Result clientWait(ContextMtl *contextMtl,
                                     bool flushCommands,
                                     uint64_t timeout,
                                     GLenum *outResult)                     = 0;
    virtual angle::Result serverWait(ContextMtl *contextMtl)                = 0;
    virtual angle::Result getStatus(DisplayMtl *displayMtl, bool *signaled) = 0;
};

// SharedEvent is only available on iOS 12.0+ or mac 10.14+
class SharedEventSyncImpl : public SyncImpl
{
  public:
    SharedEventSyncImpl() : mCv(new std::condition_variable()), mLock(new std::mutex()) {}

    ~SharedEventSyncImpl() override {}

    angle::Result set(ContextMtl *contextMtl,
                      id<MTLSharedEvent> sharedEvent,
                      uint64_t signalValue,
                      bool enqueueEvent)
    {
        mMetalSharedEvent = std::move(sharedEvent);
        mSignalValue      = signalValue;

        if (enqueueEvent)
        {
            contextMtl->queueEventSignal(mMetalSharedEvent, mSignalValue);
        }

        return angle::Result::Continue;
    }

    angle::Result clientWait(ContextMtl *contextMtl,
                             bool flushCommands,
                             uint64_t timeout,
                             GLenum *outResult) override
    {
        std::unique_lock<std::mutex> lg(*mLock);
        if (mMetalSharedEvent.get().signaledValue >= mSignalValue)
        {
            *outResult = GL_ALREADY_SIGNALED;
            return angle::Result::Continue;
        }
        if (flushCommands)
        {
            contextMtl->flushCommandBuffer(mtl::WaitUntilScheduled);
        }

        if (timeout == 0)
        {
            *outResult = GL_TIMEOUT_EXPIRED;
            return angle::Result::Continue;
        }

        // Create references to mutex and condition variable since they might be released in
        // onDestroy(), but the callback might still not be fired yet.
        std::shared_ptr<std::condition_variable> cvRef = mCv;
        std::shared_ptr<std::mutex> lockRef            = mLock;
        angle::ObjCPtr<MTLSharedEventListener> eventListener =
            contextMtl->getDisplay()->getOrCreateSharedEventListener();
        [mMetalSharedEvent.get() notifyListener:eventListener
                                        atValue:mSignalValue
                                          block:^(id<MTLSharedEvent> sharedEvent, uint64_t value) {
                                            std::unique_lock<std::mutex> localLock(*lockRef);
                                            cvRef->notify_one();
                                          }];

        if (!mCv->wait_for(lg, std::chrono::nanoseconds(SanitizeTimeout(timeout)), [this] {
                return mMetalSharedEvent.get().signaledValue >= mSignalValue;
            }))
        {
            *outResult = GL_TIMEOUT_EXPIRED;
            return angle::Result::Continue;
        }

        ASSERT(mMetalSharedEvent.get().signaledValue >= mSignalValue);
        *outResult = GL_CONDITION_SATISFIED;

        return angle::Result::Continue;
    }

    angle::Result serverWait(ContextMtl *contextMtl) override
    {
        contextMtl->serverWaitEvent(mMetalSharedEvent, mSignalValue);
        return angle::Result::Continue;
    }

    angle::Result getStatus(DisplayMtl *displayMtl, bool *signaled) override
    {
        *signaled = mMetalSharedEvent.get().signaledValue >= mSignalValue;
        return angle::Result::Continue;
    }

  private:
    angle::ObjCPtr<id<MTLSharedEvent>> mMetalSharedEvent;
    uint64_t mSignalValue = 0;

    std::shared_ptr<std::condition_variable> mCv;
    std::shared_ptr<std::mutex> mLock;
};

class EventSyncImpl : public SyncImpl
{
  private:
    // MTLEvent starts with a value of 0, use 1 to signal it.
    static constexpr uint64_t kEventSignalValue = 1;

  public:
    ~EventSyncImpl() override {}

    angle::Result set(ContextMtl *contextMtl)
    {
        ANGLE_MTL_OBJC_SCOPE
        {
            mMetalEvent = contextMtl->getMetalDevice().newEvent();
        }

        mEncodedCommandBufferSerial = contextMtl->queueEventSignal(mMetalEvent, kEventSignalValue);
        return angle::Result::Continue;
    }

    angle::Result clientWait(ContextMtl *contextMtl,
                             bool flushCommands,
                             uint64_t timeout,
                             GLenum *outResult) override
    {
        DisplayMtl *display = contextMtl->getDisplay();

        if (display->cmdQueue().isSerialCompleted(mEncodedCommandBufferSerial))
        {
            *outResult = GL_ALREADY_SIGNALED;
            return angle::Result::Continue;
        }

        if (flushCommands)
        {
            contextMtl->flushCommandBuffer(mtl::WaitUntilScheduled);
        }

        if (timeout == 0 || !display->cmdQueue().waitUntilSerialCompleted(
                                mEncodedCommandBufferSerial, SanitizeTimeout(timeout)))
        {
            *outResult = GL_TIMEOUT_EXPIRED;
            return angle::Result::Continue;
        }

        *outResult = GL_CONDITION_SATISFIED;
        return angle::Result::Continue;
    }

    angle::Result serverWait(ContextMtl *contextMtl) override
    {
        contextMtl->serverWaitEvent(mMetalEvent, kEventSignalValue);
        return angle::Result::Continue;
    }

    angle::Result getStatus(DisplayMtl *displayMtl, bool *signaled) override
    {
        *signaled = displayMtl->cmdQueue().isSerialCompleted(mEncodedCommandBufferSerial);
        return angle::Result::Continue;
    }

  private:
    angle::ObjCPtr<id<MTLEvent>> mMetalEvent;
    uint64_t mEncodedCommandBufferSerial = 0;
};

class CommandsScheduledSyncImpl : public SyncImpl
{
  public:
    CommandsScheduledSyncImpl()           = default;
    ~CommandsScheduledSyncImpl() override = default;

    angle::Result set(ContextMtl *contextMtl)
    {
        contextMtl->addCommandBufferScheduledCallback([state = mState] {
            std::lock_guard<std::mutex> lock(state->mutex);
            state->signaled = true;
            state->cv.notify_one();
        });
        contextMtl->flushCommandBuffer(mtl::NoWait);
        return angle::Result::Continue;
    }

    angle::Result clientWait(ContextMtl *contextMtl,
                             bool flushCommands,
                             uint64_t timeout,
                             GLenum *outResult) override
    {
        std::unique_lock<std::mutex> lock(mState->mutex);
        if (mState->signaled)
        {
            *outResult = GL_ALREADY_SIGNALED;
            return angle::Result::Continue;
        }

        if (timeout == 0)
        {
            *outResult = GL_TIMEOUT_EXPIRED;
            return angle::Result::Continue;
        }

        if (!mState->cv.wait_for(lock, std::chrono::nanoseconds(SanitizeTimeout(timeout)),
                                 [state = mState] { return state->signaled; }))
        {
            *outResult = GL_TIMEOUT_EXPIRED;
            return angle::Result::Continue;
        }

        ASSERT(mState->signaled);
        *outResult = GL_CONDITION_SATISFIED;
        return angle::Result::Continue;
    }

    angle::Result serverWait(ContextMtl *contextMtl) override
    {
        // Not supported for this sync type.
        return angle::Result::Continue;
    }

    angle::Result getStatus(DisplayMtl *displayMtl, bool *signaled) override
    {
        std::lock_guard<std::mutex> lock(mState->mutex);
        *signaled = mState->signaled;
        return angle::Result::Continue;
    }

  private:
    struct State
    {
        std::condition_variable cv;
        std::mutex mutex;
        bool signaled = false;
    };
    std::shared_ptr<State> mState{std::make_shared<State>()};
};
}  // namespace mtl

// FenceNVMtl implementation
FenceNVMtl::FenceNVMtl() : FenceNVImpl() {}

FenceNVMtl::~FenceNVMtl() {}

void FenceNVMtl::onDestroy(const gl::Context *context)
{
    mSync.reset();
}

angle::Result FenceNVMtl::set(const gl::Context *context, GLenum condition)
{
    ASSERT(condition == GL_ALL_COMPLETED_NV);
    ContextMtl *contextMtl = mtl::GetImpl(context);

    std::unique_ptr<mtl::EventSyncImpl> impl = std::make_unique<mtl::EventSyncImpl>();
    ANGLE_TRY(impl->set(contextMtl));
    mSync = std::move(impl);

    return angle::Result::Continue;
}

angle::Result FenceNVMtl::test(const gl::Context *context, GLboolean *outFinished)
{
    ContextMtl *contextMtl = mtl::GetImpl(context);

    bool signaled = false;
    ANGLE_TRY(mSync->getStatus(contextMtl->getDisplay(), &signaled));

    *outFinished = signaled ? GL_TRUE : GL_FALSE;
    return angle::Result::Continue;
}

angle::Result FenceNVMtl::finish(const gl::Context *context)
{
    ContextMtl *contextMtl = mtl::GetImpl(context);
    GLenum result          = GL_NONE;
    ANGLE_TRY(mSync->clientWait(contextMtl, true, mtl::kNanosecondsPerDay, &result));
    ASSERT(result != GL_WAIT_FAILED);
    return angle::Result::Continue;
}

// SyncMtl implementation
SyncMtl::SyncMtl() : SyncImpl() {}

SyncMtl::~SyncMtl() {}

void SyncMtl::onDestroy(const gl::Context *context)
{
    mSync.reset();
}

angle::Result SyncMtl::set(const gl::Context *context, GLenum condition, GLbitfield flags)
{
    ASSERT(condition == GL_SYNC_GPU_COMMANDS_COMPLETE);
    ASSERT(flags == 0);

    ContextMtl *contextMtl                   = mtl::GetImpl(context);
    std::unique_ptr<mtl::EventSyncImpl> impl = std::make_unique<mtl::EventSyncImpl>();
    ANGLE_TRY(impl->set(contextMtl));
    mSync = std::move(impl);

    return angle::Result::Continue;
}

angle::Result SyncMtl::clientWait(const gl::Context *context,
                                  GLbitfield flags,
                                  GLuint64 timeout,
                                  GLenum *outResult)
{
    ASSERT((flags & ~GL_SYNC_FLUSH_COMMANDS_BIT) == 0);

    ContextMtl *contextMtl = mtl::GetImpl(context);
    bool flush             = (flags & GL_SYNC_FLUSH_COMMANDS_BIT) != 0;
    return mSync->clientWait(contextMtl, flush, timeout, outResult);
}

angle::Result SyncMtl::serverWait(const gl::Context *context, GLbitfield flags, GLuint64 timeout)
{
    ASSERT(flags == 0);
    ASSERT(timeout == GL_TIMEOUT_IGNORED);

    ContextMtl *contextMtl = mtl::GetImpl(context);
    return mSync->serverWait(contextMtl);
}

angle::Result SyncMtl::getStatus(const gl::Context *context, GLint *outResult)
{
    ContextMtl *contextMtl = mtl::GetImpl(context);

    bool signaled = false;
    ANGLE_TRY(mSync->getStatus(contextMtl->getDisplay(), &signaled));

    *outResult = signaled ? GL_SIGNALED : GL_UNSIGNALED;
    return angle::Result::Continue;
}

// EGLSyncMtl implementation
EGLSyncMtl::EGLSyncMtl() : EGLSyncImpl() {}

EGLSyncMtl::~EGLSyncMtl() {}

void EGLSyncMtl::onDestroy(const egl::ThreadSafeDisplay *display)
{
    mSync.reset();
    mSharedEvent = nil;
}

egl::Error EGLSyncMtl::initialize(const egl::ThreadSafeDisplay *display,
                                  const gl::Context *context,
                                  EGLenum type,
                                  const egl::AttributeMap &attribs)
{
    ASSERT(context != nullptr);

    ContextMtl *contextMtl = mtl::GetImpl(context);
    switch (type)
    {
        case EGL_SYNC_FENCE_KHR:
        {
            std::unique_ptr<mtl::EventSyncImpl> impl = std::make_unique<mtl::EventSyncImpl>();
            if (IsError(impl->set(contextMtl)))
            {
                return egl::Error(EGL_BAD_ALLOC, "eglCreateSyncKHR failed to create sync object");
            }
            mSync = std::move(impl);
            break;
        }

        case EGL_SYNC_METAL_SHARED_EVENT_ANGLE:
        {
            mSharedEvent = (__bridge id<MTLSharedEvent>)reinterpret_cast<void *>(
                attribs.get(EGL_SYNC_METAL_SHARED_EVENT_OBJECT_ANGLE, 0));
            if (!mSharedEvent)
            {
                mSharedEvent = contextMtl->getMetalDevice().newSharedEvent();
            }

            uint64_t signalValue = 0;
            if (attribs.contains(EGL_SYNC_METAL_SHARED_EVENT_SIGNAL_VALUE_HI_ANGLE) ||
                attribs.contains(EGL_SYNC_METAL_SHARED_EVENT_SIGNAL_VALUE_LO_ANGLE))
            {
                signalValue = mtl::UnpackSignalValue(
                    attribs.get(EGL_SYNC_METAL_SHARED_EVENT_SIGNAL_VALUE_HI_ANGLE, 0),
                    attribs.get(EGL_SYNC_METAL_SHARED_EVENT_SIGNAL_VALUE_LO_ANGLE, 0));
            }
            else
            {
                signalValue = mSharedEvent.get().signaledValue + 1;
            }

            // If the condition is anything other than EGL_SYNC_METAL_SHARED_EVENT_SIGNALED_ANGLE,
            // we enque the event created/provided.
            // TODO: Could this be changed to `mSharedEvent != nullptr`? Do we ever create an event
            // but not want to enqueue it?
            bool enqueue = attribs.getAsInt(EGL_SYNC_CONDITION, 0) !=
                           EGL_SYNC_METAL_SHARED_EVENT_SIGNALED_ANGLE;

            std::unique_ptr<mtl::SharedEventSyncImpl> impl =
                std::make_unique<mtl::SharedEventSyncImpl>();
            if (IsError(impl->set(contextMtl, mSharedEvent, signalValue, enqueue)))
            {
                return egl::Error(EGL_BAD_ALLOC, "eglCreateSyncKHR failed to create sync object");
            }
            mSync = std::move(impl);
            break;
        }

        case EGL_SYNC_METAL_COMMANDS_SCHEDULED_ANGLE:
        {
            auto impl = std::make_unique<mtl::CommandsScheduledSyncImpl>();
            if (IsError(impl->set(contextMtl)))
            {
                return egl::Error(EGL_BAD_ALLOC, "eglCreateSyncKHR failed to create sync object");
            }
            mSync = std::move(impl);
            break;
        }

        default:
            UNREACHABLE();
            return egl::Error(EGL_BAD_ALLOC);
    }

    mType = type;
    return egl::NoError();
}

egl::Error EGLSyncMtl::clientWait(const egl::ThreadSafeDisplay *display,
                                  const gl::Context *context,
                                  EGLint flags,
                                  EGLTime timeout,
                                  EGLint *outResult)
{
    ASSERT((flags & ~EGL_SYNC_FLUSH_COMMANDS_BIT_KHR) == 0);

    // Allow null context only for EGL_SYNC_METAL_COMMANDS_SCHEDULED_ANGLE.
    if (mType != EGL_SYNC_METAL_COMMANDS_SCHEDULED_ANGLE && !context)
    {
        return egl::Error(EGL_BAD_CONTEXT);
    }

    bool flush             = (flags & EGL_SYNC_FLUSH_COMMANDS_BIT_KHR) != 0;
    GLenum result          = GL_NONE;
    ContextMtl *contextMtl = context ? mtl::GetImpl(context) : nullptr;
    if (IsError(mSync->clientWait(contextMtl, flush, static_cast<uint64_t>(timeout), &result)))
    {
        return egl::Error(EGL_BAD_ALLOC);
    }

    switch (result)
    {
        case GL_ALREADY_SIGNALED:
            // fall through.  EGL doesn't differentiate between event being already set, or set
            // before timeout.
        case GL_CONDITION_SATISFIED:
            *outResult = EGL_CONDITION_SATISFIED_KHR;
            return egl::NoError();

        case GL_TIMEOUT_EXPIRED:
            *outResult = EGL_TIMEOUT_EXPIRED_KHR;
            return egl::NoError();

        default:
            UNREACHABLE();
            *outResult = EGL_FALSE;
            return egl::Error(EGL_BAD_ALLOC);
    }
}

egl::Error EGLSyncMtl::serverWait(const egl::ThreadSafeDisplay *display,
                                  const gl::Context *context,
                                  EGLint flags)
{
    // Server wait requires a valid bound context.
    ASSERT(context);

    // No flags are currently implemented.
    ASSERT(flags == 0);

    ContextMtl *contextMtl = mtl::GetImpl(context);
    if (IsError(mSync->serverWait(contextMtl)))
    {
        return egl::Error(EGL_BAD_ALLOC);
    }

    return egl::NoError();
}

egl::Error EGLSyncMtl::getStatus(const egl::ThreadSafeDisplay *display, EGLint *outStatus)
{
    // mtl::SyncImpl::getStatus() needs the full DisplayMtl for its command queue, and is shared
    // with the GL-side SyncMtl/FenceNVMtl paths that already hold one.  Only egl::Display carries
    // that implementation, so recover it here. This downcast is potentially risky and defeats
    // the purpose of ThreadSafeDisplay. Currently, the Metal backend does not claim thread-safety.
    DisplayMtl *displayMtl = mtl::GetImpl(static_cast<const egl::Display *>(display));
    bool signaled          = false;
    if (IsError(mSync->getStatus(displayMtl, &signaled)))
    {
        return egl::Error(EGL_BAD_ALLOC);
    }

    *outStatus = signaled ? EGL_SIGNALED_KHR : EGL_UNSIGNALED_KHR;
    return egl::NoError();
}

egl::Error EGLSyncMtl::copyMetalSharedEventANGLE(const egl::ThreadSafeDisplay *display,
                                                 void **result) const
{
    ASSERT(mSharedEvent != nil);

    angle::ObjCPtr<id<MTLSharedEvent>> copySharedEvent = mSharedEvent;
    *result = reinterpret_cast<void *>(copySharedEvent.leakObject());

    return egl::NoError();
}

egl::Error EGLSyncMtl::dupNativeFenceFD(const egl::ThreadSafeDisplay *display, EGLint *result) const
{
    UNREACHABLE();
    return egl::Error(EGL_BAD_DISPLAY);
}

}  // namespace rx
