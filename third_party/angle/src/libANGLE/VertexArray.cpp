//
// Copyright 2013 The ANGLE Project Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// Implementation of the state class for mananging GLES 3 Vertex Array Objects.
//

#include "libANGLE/VertexArray.h"

#include "common/utilities.h"
#include "libANGLE/Buffer.h"
#include "libANGLE/Context.h"
#include "libANGLE/renderer/BufferImpl.h"
#include "libANGLE/renderer/GLImplFactory.h"
#include "libANGLE/renderer/VertexArrayImpl.h"

namespace gl
{
// VertexArrayState implementation.
VertexArrayState::VertexArrayState(VertexArrayID vertexArrayID,
                                   size_t maxAttribs,
                                   size_t maxAttribBindings)
    : mId(vertexArrayID)
{
    ASSERT(maxAttribs <= maxAttribBindings);

    for (size_t i = 0; i < maxAttribs; i++)
    {
        mVertexAttributes.emplace_back(static_cast<GLuint>(i));
        mVertexBindings.emplace_back(static_cast<GLuint>(i));
        // The default vertex attribute format is R32G32B32A32_FLOAT. Keep mVertexAttributesTypeMask
        // in sync so attributes that keep their default format are treated as float.
        SetComponentTypeMask(ComponentType::Float, i, &mVertexAttributesTypeMask);
    }

    // Initially all attributes start as "client" with no buffer bound.
    mClientMemoryAttribsMask.set();
}

VertexArrayState::~VertexArrayState() {}

bool VertexArrayState::hasEnabledNullPointerClientArray() const
{
    return (mNullPointerClientMemoryAttribsMask & mEnabledAttributesMask).any();
}

AttributesMask VertexArrayState::getBindingToAttributesMask(GLuint bindingIndex) const
{
    ASSERT(bindingIndex < mVertexBindings.size());
    return mVertexBindings[bindingIndex].getBoundAttributesMask();
}

// Set an attribute using a new binding.
void VertexArrayState::setAttribBinding(size_t attribIndex, GLuint newBindingIndex)
{
    ASSERT(attribIndex < mVertexAttributes.size() && newBindingIndex < mVertexBindings.size());

    VertexAttribute &attrib = mVertexAttributes[attribIndex];

    // Update the binding-attribute map.
    const GLuint oldBindingIndex = attrib.bindingIndex;
    ASSERT(oldBindingIndex != newBindingIndex);

    VertexBinding &oldBinding = mVertexBindings[oldBindingIndex];
    VertexBinding &newBinding = mVertexBindings[newBindingIndex];

    ASSERT(oldBinding.getBoundAttributesMask().test(attribIndex) &&
           !newBinding.getBoundAttributesMask().test(attribIndex));

    oldBinding.resetBoundAttribute(attribIndex);
    newBinding.setBoundAttribute(attribIndex);

    // Set the attribute using the new binding.
    attrib.bindingIndex = newBindingIndex;

    mEnabledAttributesMask.set(attribIndex, attrib.enabled);
}

bool VertexArrayState::isDefault() const
{
    return mId.value == 0;
}

// VertexArrayPrivate implementation.
VertexArrayPrivate::VertexArrayPrivate(rx::GLImplFactory *factory,
                                       VertexArrayID id,
                                       size_t maxAttribs,
                                       size_t maxAttribBindings)
    : mId(id), mState(mId, maxAttribs, maxAttribBindings), mRobustBufferAccessEnabled(false)
{}

VertexArrayPrivate::~VertexArrayPrivate() {}

void VertexArrayPrivate::setVertexAttribBinding(size_t attribIndex, GLuint newBindingIndex)
{
    ASSERT(attribIndex < getMaxAttribs() && newBindingIndex < getMaxBindings());

    if (mState.mVertexAttributes[attribIndex].bindingIndex == newBindingIndex)
    {
        return;
    }

    mState.setAttribBinding(attribIndex, newBindingIndex);

    if (mRobustBufferAccessEnabled)
    {
        VertexAttribute &attrib = mState.mVertexAttributes[attribIndex];
        attrib.updateCachedElementLimit(mState.mVertexBindings[newBindingIndex],
                                        mCachedBufferSize[newBindingIndex]);
    }

    setDirtyAttribBit(attribIndex, DIRTY_ATTRIB_BINDING);

    // Update client attribs mask.
    mState.mClientMemoryAttribsMask.set(attribIndex, !mBufferBindingMask[newBindingIndex]);

    mCachedMappedArrayBuffers.set(attribIndex, mCachedBufferPropertyMapped.test(newBindingIndex));
    mCachedMutableOrImpersistentArrayBuffers.set(
        attribIndex, mCachedBufferPropertyMutableOrImpersistent.test(newBindingIndex));
    mCachedInvalidMappedArrayBuffer = mCachedMappedArrayBuffers & mState.mEnabledAttributesMask &
                                      mCachedMutableOrImpersistentArrayBuffers;
}

const VertexAttribute &VertexArrayPrivate::getVertexAttribute(size_t attribIndex) const
{
    ASSERT(attribIndex < getMaxAttribs());
    return mState.mVertexAttributes[attribIndex];
}

const VertexBinding &VertexArrayPrivate::getVertexBinding(size_t bindingIndex) const
{
    ASSERT(bindingIndex < getMaxBindings());
    return mState.mVertexBindings[bindingIndex];
}

void VertexArrayPrivate::setDirtyAttribBit(size_t attribIndex, DirtyAttribBitType dirtyAttribBit)
{
    mDirtyBits.set(DIRTY_BIT_ATTRIB_0 + attribIndex);
    mDirtyAttribBits[attribIndex].set(dirtyAttribBit);
}

ANGLE_INLINE void VertexArrayPrivate::clearDirtyAttribBit(size_t attribIndex,
                                                          DirtyAttribBitType dirtyAttribBit)
{
    mDirtyAttribBits[attribIndex].set(dirtyAttribBit, false);
    if (mDirtyAttribBits[attribIndex].any())
    {
        return;
    }
    mDirtyBits.set(DIRTY_BIT_ATTRIB_0 + attribIndex, false);
}

ANGLE_INLINE void VertexArrayPrivate::setDirtyBindingBit(size_t bindingIndex,
                                                         DirtyBindingBitType dirtyBindingBit)
{
    mDirtyBits.set(DIRTY_BIT_BINDING_0 + bindingIndex);
    mDirtyBindingBits[bindingIndex].set(dirtyBindingBit);
}

ANGLE_INLINE void VertexArrayPrivate::updateCachedElementLimit(const VertexBinding &binding,
                                                               GLint64 bufferSize)
{
    ASSERT(mRobustBufferAccessEnabled);
    for (size_t boundAttribute : binding.getBoundAttributesMask())
    {
        mState.mVertexAttributes[boundAttribute].updateCachedElementLimit(binding, bufferSize);
    }
}

ANGLE_INLINE void VertexArrayPrivate::updateCachedArrayBuffersMasks(
    bool isMapped,
    bool isImmutable,
    bool isPersistent,
    const AttributesMask &boundAttributesMask)
{
    if (isMapped)
    {
        mCachedMappedArrayBuffers |= boundAttributesMask;
    }
    else
    {
        mCachedMappedArrayBuffers &= ~boundAttributesMask;
    }

    if (!isImmutable || !isPersistent)
    {
        mCachedMutableOrImpersistentArrayBuffers |= boundAttributesMask;
    }
    else
    {
        mCachedMutableOrImpersistentArrayBuffers &= ~boundAttributesMask;
    }

    mCachedInvalidMappedArrayBuffer = mCachedMappedArrayBuffers & mState.mEnabledAttributesMask &
                                      mCachedMutableOrImpersistentArrayBuffers;
}

void VertexArrayPrivate::setVertexBindingDivisor(size_t bindingIndex, GLuint divisor)
{
    ASSERT(bindingIndex < getMaxBindings());

    VertexBinding &binding = mState.mVertexBindings[bindingIndex];

    if (binding.getDivisor() == divisor)
    {
        return;
    }

    binding.setDivisor(divisor);
    setDirtyBindingBit(bindingIndex, DIRTY_BINDING_DIVISOR);
}

bool VertexArrayPrivate::setVertexAttribFormatImpl(VertexAttribute *attrib,
                                                   GLint size,
                                                   VertexAttribType type,
                                                   bool normalized,
                                                   bool pureInteger,
                                                   GLuint relativeOffset)
{
    angle::FormatID formatID = GetVertexFormatID(type, normalized, size, pureInteger);

    if (formatID != attrib->format->id || attrib->relativeOffset != relativeOffset)
    {
        attrib->relativeOffset = relativeOffset;
        attrib->format         = &angle::Format::Get(formatID);
        return true;
    }

    return false;
}

void VertexArrayPrivate::setVertexAttribFormat(size_t attribIndex,
                                               GLint size,
                                               VertexAttribType type,
                                               bool normalized,
                                               bool pureInteger,
                                               GLuint relativeOffset)
{
    VertexAttribute &attrib = mState.mVertexAttributes[attribIndex];

    ComponentType componentType = GetVertexAttributeComponentType(pureInteger, type);
    SetComponentTypeMask(componentType, attribIndex, &mState.mVertexAttributesTypeMask);

    if (setVertexAttribFormatImpl(&attrib, size, type, normalized, pureInteger, relativeOffset))
    {
        setDirtyAttribBit(attribIndex, DIRTY_ATTRIB_FORMAT);
    }

    if (mRobustBufferAccessEnabled)
    {
        attrib.updateCachedElementLimit(mState.mVertexBindings[attrib.bindingIndex],
                                        mCachedBufferSize[attrib.bindingIndex]);
    }
}

void VertexArrayPrivate::setVertexAttribDivisor(size_t attribIndex, GLuint divisor)
{
    ASSERT(attribIndex < getMaxAttribs());

    setVertexAttribBinding(attribIndex, static_cast<GLuint>(attribIndex));
    setVertexBindingDivisor(attribIndex, divisor);
}

void VertexArrayPrivate::enableAttribute(size_t attribIndex, bool enabledState)
{
    ASSERT(attribIndex < getMaxAttribs());

    VertexAttribute &attrib = mState.mVertexAttributes[attribIndex];

    if (mState.mEnabledAttributesMask.test(attribIndex) == enabledState)
    {
        return;
    }

    attrib.enabled = enabledState;

    // Update state cache
    mState.mEnabledAttributesMask.set(attribIndex, enabledState);
    bool enableChanged = (mState.mEnabledAttributesMask.test(attribIndex) !=
                          mState.mLastSyncedEnabledAttributesMask.test(attribIndex));

    if (enableChanged)
    {
        setDirtyAttribBit(attribIndex, DIRTY_ATTRIB_ENABLED);
    }
    else
    {
        clearDirtyAttribBit(attribIndex, DIRTY_ATTRIB_ENABLED);
    }

    mCachedInvalidMappedArrayBuffer = mCachedMappedArrayBuffers & mState.mEnabledAttributesMask &
                                      mCachedMutableOrImpersistentArrayBuffers;
}

bool VertexArrayPrivate::hasTransformFeedbackBindingConflict(const Context *context) const
{
    // Fast check first.
    if (!mCachedBufferPropertyTransformFeedbackConflict.any())
    {
        return false;
    }

    const AttributesMask &activeAttribues = context->getActiveBufferedAttribsMask();

    // Slow check. We must ensure that the conflicting attributes are enabled/active.
    for (size_t attribIndex : activeAttribues)
    {
        const VertexAttribute &attrib = mState.mVertexAttributes[attribIndex];
        if (mCachedBufferPropertyTransformFeedbackConflict[attrib.bindingIndex])
        {
            return true;
        }
    }

    return false;
}

// VertexArray implementation.
VertexArray::VertexArray(rx::GLImplFactory *factory,
                         VertexArrayID id,
                         size_t maxAttribs,
                         size_t maxAttribBindings)
    : VertexArrayPrivate(factory, id, maxAttribs, maxAttribBindings),
      mVertexArray(factory->createVertexArray(mState, mVertexArrayBuffers))
{}

void VertexArray::onDestroy(const Context *context)
{
    bool isBound = context->isCurrentVertexArray(this);

    for (size_t bindingIndex : mBufferBindingMask)
    {
        Buffer *buffer = mVertexArrayBuffers[bindingIndex].get();
        ASSERT(buffer != nullptr);
        if (isBound)
        {
            buffer->onNonTFBindingChanged(-1);
            buffer->removeVertexArrayBinding(context, bindingIndex);
        }
        mVertexArrayBuffers[bindingIndex].set(context, nullptr);
    }

    mBufferBindingMask.reset();
    mVertexArray->destroy(context);
    SafeDelete(mVertexArray);
    delete this;
}

VertexArray::~VertexArray()
{
    ASSERT(!mVertexArray);
}

angle::Result VertexArray::setLabel(const Context *context, const std::string &label)
{
    mState.mLabel = label;

    if (mVertexArray)
    {
        return mVertexArray->onLabelUpdate(context);
    }
    return angle::Result::Continue;
}

const std::string &VertexArray::getLabel() const
{
    return mState.getLabel();
}

bool VertexArray::detachBuffer(const Context *context, BufferID bufferID)
{
    bool isBound           = context->isCurrentVertexArray(this);
    bool anyBufferDetached = false;

    for (size_t bindingIndex : mBufferBindingMask)
    {
        Buffer *buffer = mVertexArrayBuffers[bindingIndex].get();
        ASSERT(buffer != nullptr);
        if (buffer->id() == bufferID)
        {
            if (isBound)
            {
                buffer->onNonTFBindingChanged(-1);
            }

            buffer->removeVertexArrayBinding(context, bindingIndex);
            mVertexArrayBuffers[bindingIndex].set(context, nullptr);
            mBufferBindingMask.reset(bindingIndex);

            if (bindingIndex == kElementArrayBufferIndex)
            {
                mDirtyBits.set(DIRTY_BIT_ELEMENT_ARRAY_BUFFER);
            }
            else
            {
                VertexBinding &binding = mState.mVertexBindings[bindingIndex];
                if (context->getClientVersion() >= ES_3_1 && !mState.isDefault())
                {
                    setDirtyBindingBit(bindingIndex, DIRTY_BINDING_BUFFER);
                }
                else
                {
                    static_assert(MAX_VERTEX_ATTRIB_BINDINGS < 8 * sizeof(uint32_t),
                                  "Not enough bits in bindingIndex");
                    // The redundant uint32_t cast here is required to avoid a warning on MSVC.
                    ASSERT(binding.getBoundAttributesMask() ==
                           AttributesMask(static_cast<uint32_t>(1 << bindingIndex)));
                    setDirtyAttribBit(bindingIndex, DIRTY_ATTRIB_POINTER);
                }

                for (size_t attribIndex : binding.getBoundAttributesMask())
                {
                    VertexAttribute &attrib = mState.mVertexAttributes[attribIndex];
                    attrib.pointer          = nullptr;
                }

                mState.mClientMemoryAttribsMask |= binding.getBoundAttributesMask();
            }

            anyBufferDetached = true;
        }
    }

    return anyBufferDetached;
}

ANGLE_INLINE void VertexArray::updateCachedMappedArrayBuffersBinding(size_t bindingIndex)
{
    const VertexBinding &binding = mState.mVertexBindings[bindingIndex];
    const Buffer *buffer         = mVertexArrayBuffers[bindingIndex].get();
    ASSERT(mBufferBindingMask.test(bindingIndex));
    ASSERT(buffer != nullptr);

    bool isMapped     = buffer->isMapped() == GL_TRUE;
    bool isImmutable  = buffer->isImmutable() == GL_TRUE;
    bool isPersistent = (buffer->getAccessFlags() & GL_MAP_PERSISTENT_BIT_EXT) != 0;

    mCachedBufferPropertyMapped.set(bindingIndex, isMapped);
    mCachedBufferPropertyMutableOrImpersistent.set(bindingIndex, !isImmutable || !isPersistent);

    return updateCachedArrayBuffersMasks(isMapped, isImmutable, isPersistent,
                                         binding.getBoundAttributesMask());
}

void VertexArray::bindElementBuffer(const Context *context, Buffer *boundBuffer)
{
    Buffer *oldBuffer = getElementArrayBuffer();

    if (oldBuffer)
    {
        oldBuffer->removeVertexArrayBinding(context, kElementArrayBufferIndex);
        if (context->isWebGL())
        {
            oldBuffer->onNonTFBindingChanged(-1);
        }
        oldBuffer->release(context);
        mBufferBindingMask.reset(kElementArrayBufferIndex);
    }

    mVertexArrayBuffers[kElementArrayBufferIndex].assign(boundBuffer);

    if (boundBuffer)
    {
        boundBuffer->addVertexArrayBinding(context, kElementArrayBufferIndex);
        if (context->isWebGL())
        {
            boundBuffer->onNonTFBindingChanged(1);
        }
        boundBuffer->addRef();
        mBufferBindingMask.set(kElementArrayBufferIndex);
    }

    mDirtyBits.set(VertexArray::DIRTY_BIT_ELEMENT_ARRAY_BUFFER);
    mIndexRangeInlineCache = {};
}

ANGLE_INLINE VertexArray::DirtyBindingBits VertexArray::bindVertexBufferImpl(const Context *context,
                                                                             size_t bindingIndex,
                                                                             Buffer *boundBuffer,
                                                                             uintptr_t offset,
                                                                             GLsizei stride)
{
    ASSERT(bindingIndex < getMaxBindings());
    ASSERT(context->isCurrentVertexArray(this));

    VertexBinding *binding = &mState.mVertexBindings[bindingIndex];

    Buffer *oldBuffer = mVertexArrayBuffers[bindingIndex].get();

    DirtyBindingBits dirtyBindingBits;
    dirtyBindingBits.set(DIRTY_BINDING_BUFFER, oldBuffer != boundBuffer);
    dirtyBindingBits.set(DIRTY_BINDING_STRIDE, static_cast<GLuint>(stride) != binding->getStride());
    dirtyBindingBits.set(DIRTY_BINDING_OFFSET, offset != binding->getOffset());
    if (mRobustBufferAccessEnabled)
    {
        GLint64 bufferSize = boundBuffer ? boundBuffer->getSize() : 0;
        dirtyBindingBits.set(DIRTY_BINDING_SIZE, bufferSize != mCachedBufferSize[bindingIndex]);
        mCachedBufferSize[bindingIndex] = bufferSize;
    }

    if (dirtyBindingBits.none())
    {
        return dirtyBindingBits;
    }

    if (boundBuffer != oldBuffer)
    {
        // Several nullptr checks are combined here for optimization purposes.
        if (oldBuffer)
        {
            oldBuffer->onNonTFBindingChanged(-1);
            oldBuffer->removeVertexArrayBinding(context, bindingIndex);
            oldBuffer->release(context);
            mBufferBindingMask.reset(bindingIndex);
        }

        mVertexArrayBuffers[bindingIndex].assign(boundBuffer);

        // Update client memory attribute pointers. Affects all bound attributes.
        if (boundBuffer)
        {
            boundBuffer->addRef();
            boundBuffer->onNonTFBindingChanged(1);
            boundBuffer->addVertexArrayBinding(context, bindingIndex);
            if (context->isHardenedContext())
            {
                mCachedBufferPropertyTransformFeedbackConflict.set(
                    bindingIndex, boundBuffer->hasTFBBindingConflict());
            }
            mBufferBindingMask.set(bindingIndex);
            mState.mClientMemoryAttribsMask &= ~binding->getBoundAttributesMask();
            updateCachedMappedArrayBuffersBinding(bindingIndex);
        }
        else
        {
            mCachedBufferPropertyTransformFeedbackConflict.set(bindingIndex, false);
            mState.mClientMemoryAttribsMask |= binding->getBoundAttributesMask();
            mCachedBufferPropertyMapped.set(bindingIndex, false);
            mCachedBufferPropertyMutableOrImpersistent.set(bindingIndex, false);
            updateCachedArrayBuffersMasks(false, false, false, binding->getBoundAttributesMask());
        }
    }

    binding->setOffset(offset);
    binding->setStride(stride);

    return dirtyBindingBits;
}

void VertexArray::bindVertexBuffer(const Context *context,
                                   size_t bindingIndex,
                                   Buffer *boundBuffer,
                                   GLintptr offset,
                                   GLsizei stride)
{
    // |offset| must be non-negative per validation rules of glBindVertexBuffer.
    ASSERT(offset >= 0);
    const VertexArray::DirtyBindingBits dirtyBindingBits = bindVertexBufferImpl(
        context, bindingIndex, boundBuffer, static_cast<uintptr_t>(offset), stride);

    if (!dirtyBindingBits.test(DIRTY_BINDING_BUFFER) && context->isSharedContext() &&
        boundBuffer != nullptr)
    {
        ASSERT(boundBuffer == mVertexArrayBuffers[bindingIndex].get());
        VertexArrayBufferBindingMask bindingMask = boundBuffer->getVertexArrayBinding(context);
        ASSERT(!bindingMask.none());
        onSharedBufferBind(context, boundBuffer, bindingMask);
    }

    if (dirtyBindingBits.any())
    {
        mDirtyBits.set(DIRTY_BIT_BINDING_0 + bindingIndex);
        mDirtyBindingBits[bindingIndex] |= dirtyBindingBits;
        if (mRobustBufferAccessEnabled)
        {
            updateCachedElementLimit(mState.mVertexBindings[bindingIndex],
                                     mCachedBufferSize[bindingIndex]);
        }
    }
}

ANGLE_INLINE void VertexArray::setVertexAttribPointerImpl(const Context *context,
                                                          ComponentType componentType,
                                                          bool pureInteger,
                                                          size_t attribIndex,
                                                          Buffer *boundBuffer,
                                                          GLint size,
                                                          VertexAttribType type,
                                                          bool normalized,
                                                          GLsizei stride,
                                                          const void *pointer,
                                                          bool *isVertexAttribDirtyOut)
{
    ASSERT(isVertexAttribDirtyOut);
    ASSERT(attribIndex < getMaxAttribs());

    VertexAttribute &attrib = mState.mVertexAttributes[attribIndex];

    SetComponentTypeMask(componentType, attribIndex, &mState.mVertexAttributesTypeMask);

    bool attribDirty = setVertexAttribFormatImpl(&attrib, size, type, normalized, pureInteger, 0);

    if (attrib.bindingIndex != attribIndex)
    {
        setVertexAttribBinding(attribIndex, static_cast<GLuint>(attribIndex));
    }

    GLsizei effectiveStride =
        stride == 0 ? static_cast<GLsizei>(ComputeVertexAttributeTypeSize(attrib)) : stride;

    if (attrib.vertexAttribArrayStride != static_cast<GLuint>(stride))
    {
        attribDirty = true;
    }
    attrib.vertexAttribArrayStride = stride;

    // If we switch from an array buffer to a client pointer(or vice-versa), we set the whole
    // attribute dirty. This notifies the Vulkan back-end to update all its caches.
    Buffer *oldBuffer = mVertexArrayBuffers[attrib.bindingIndex].get();
    if ((boundBuffer == nullptr) != (oldBuffer == nullptr))
    {
        attribDirty = true;
    }

    // If using client arrays and the pointer changes, set the attribute as dirty
    if (boundBuffer == nullptr && attrib.pointer != pointer)
    {
        attribDirty = true;
    }

    // Change of attrib.pointer is not part of attribDirty. Pointer is actually the buffer offset
    // which is handled within bindVertexBufferImpl and reflected in bufferDirty.
    attrib.pointer  = pointer;
    uintptr_t offset = boundBuffer ? reinterpret_cast<uintptr_t>(pointer) : 0;
    const VertexArray::DirtyBindingBits dirtyBindingBits =
        bindVertexBufferImpl(context, attribIndex, boundBuffer, offset, effectiveStride);

    if (attribDirty)
    {
        setDirtyAttribBit(attribIndex, DIRTY_ATTRIB_POINTER);
        *isVertexAttribDirtyOut = true;
    }
    else if (dirtyBindingBits.any())
    {
        setDirtyAttribBit(attribIndex, DIRTY_ATTRIB_POINTER_BUFFER);
        *isVertexAttribDirtyOut = true;
    }
    if (mRobustBufferAccessEnabled && (attribDirty || dirtyBindingBits.any()))
    {
        updateCachedElementLimit(mState.mVertexBindings[attribIndex],
                                 mCachedBufferSize[attribIndex]);
    }

    mState.mNullPointerClientMemoryAttribsMask.set(attribIndex,
                                                   boundBuffer == nullptr && pointer == nullptr);
}

void VertexArray::setVertexAttribPointer(const Context *context,
                                         size_t attribIndex,
                                         Buffer *boundBuffer,
                                         GLint size,
                                         VertexAttribType type,
                                         bool normalized,
                                         GLsizei stride,
                                         const void *pointer,
                                         bool *isVertexAttribDirtyOut)
{
    setVertexAttribPointerImpl(context, ComponentType::Float, false, attribIndex, boundBuffer, size,
                               type, normalized, stride, pointer, isVertexAttribDirtyOut);
}

void VertexArray::setVertexAttribIPointer(const Context *context,
                                          size_t attribIndex,
                                          Buffer *boundBuffer,
                                          GLint size,
                                          VertexAttribType type,
                                          GLsizei stride,
                                          const void *pointer,
                                          bool *isVertexAttribDirtyOut)
{
    ComponentType componentType = GetVertexAttributeComponentType(true, type);
    setVertexAttribPointerImpl(context, componentType, true, attribIndex, boundBuffer, size, type,
                               false, stride, pointer, isVertexAttribDirtyOut);
}

angle::Result VertexArray::syncState(const Context *context)
{
    if (mDirtyBits.any())
    {
        mDirtyBitsGuard = mDirtyBits;
        ANGLE_TRY(
            mVertexArray->syncState(context, mDirtyBits, &mDirtyAttribBits, &mDirtyBindingBits));
        mDirtyBits.reset();
        mDirtyBitsGuard.reset();

        // The dirty bits should be reset in the back-end. To simplify ASSERTs only check attrib 0.
        ASSERT(mDirtyAttribBits[0].none());
        ASSERT(mDirtyBindingBits[0].none());
        mState.mLastSyncedEnabledAttributesMask = mState.mEnabledAttributesMask;
    }
    return angle::Result::Continue;
}

bool VertexArray::bufferMaskBitsPointToTheSameBuffer(
    VertexArrayBufferBindingMask bufferBindingMask) const
{
    const Buffer *buffer = nullptr;
    for (size_t bindingIndex : bufferBindingMask)
    {
        if (buffer == nullptr)
        {
            buffer = mVertexArrayBuffers[bindingIndex].get();
        }
        else if (buffer != mVertexArrayBuffers[bindingIndex].get())
        {
            return false;
        }
    }
    return true;
}

void VertexArray::updateBindingSizeIfChanged(const size_t bindingIndex, const GLint64 bufferSize)
{
    ASSERT(mRobustBufferAccessEnabled);
    if (mCachedBufferSize[bindingIndex] != bufferSize)
    {
        setDirtyBindingBit(bindingIndex, DIRTY_BINDING_SIZE);
        mCachedBufferSize[bindingIndex] = bufferSize;
    }
}

// This becomes current vertex array on the context
void VertexArray::onBind(const Context *context)
{
    VertexArrayBufferBindingMask bufferBindingMask = mBufferBindingMask;

    if (bufferBindingMask[kElementArrayBufferIndex])
    {
        Buffer *bufferGL = getElementArrayBuffer();
        ASSERT(bufferGL != nullptr);
        bufferGL->addVertexArrayBinding(context, kElementArrayBufferIndex);
        bufferBindingMask.reset(kElementArrayBufferIndex);
    }
    else
    {
        ASSERT(getElementArrayBuffer() == nullptr);
    }

    // This vertex array becoming current. Some of the bindings we may have removed from buffer's
    // observer list. We need to add it back to the buffer's observer list and update dirty bits
    // that we may have missed while we were not observing.
    for (size_t bindingIndex : bufferBindingMask)
    {
        Buffer *bufferGL = mVertexArrayBuffers[bindingIndex].get();
        ASSERT(bufferGL != nullptr);
        ASSERT(bindingIndex != kElementArrayBufferIndex);
        bufferGL->addVertexArrayBinding(context, bindingIndex);
        updateCachedMappedArrayBuffersBinding(bindingIndex);
    }

    if (mRobustBufferAccessEnabled)
    {
        for (size_t bindingIndex : bufferBindingMask)
        {
            Buffer *bufferGL                = mVertexArrayBuffers[bindingIndex].get();
            updateBindingSizeIfChanged(bindingIndex, bufferGL->getSize());
            updateCachedElementLimit(mState.mVertexBindings[bindingIndex],
                                     mCachedBufferSize[bindingIndex]);
        }
    }

    if (context->isHardenedContext())
    {
        for (size_t bindingIndex : bufferBindingMask)
        {
            bool hasConflict = mVertexArrayBuffers[bindingIndex]->hasTFBBindingConflict();
            mCachedBufferPropertyTransformFeedbackConflict.set(bindingIndex, hasConflict);
        }
    }

    // Buffers may have changed while vertex array was not current, we need to check buffer's
    // internal storage and set proper dirty bits if buffer has changed since last syncState.
    mDirtyBits |= mVertexArray->checkBufferForDirtyBits(context, mBufferBindingMask);

    // Always reset mIndexRangeInlineCache since we lost buffer observation while unbind
    mIndexRangeInlineCache = {};

    onStateChange(angle::SubjectMessage::ContentsChanged);
}

// This becomes non-current vertex array on the context
void VertexArray::onUnbind(const Context *context)
{
    // This vertex array becoming non-current. For performance reason, we remove it from the
    // buffers' observer list so that the cost of buffer sending signal to observers will not be too
    // expensive.
    for (size_t bindingIndex : mBufferBindingMask)
    {
        Buffer *bufferGL = mVertexArrayBuffers[bindingIndex].get();
        ASSERT(bufferGL != nullptr);
        bufferGL->removeVertexArrayBinding(context, bindingIndex);
    }
}

void VertexArray::onBindingChanged(const Context *context, int incr)
{
    // When vertex array gets unbound, we remove it from bound buffers' observer list so that when
    // buffer changes, it wont has to loop over all these non-current vertex arrays and set dirty
    // bit on them. To compensate for that, when we bind a vertex array, we have to check against
    // each bound buffers and see if they have changed and needs to update vertex array's dirty bits
    // accordingly
    ASSERT(incr == 1 || incr == -1);
    if (incr < 0)
    {
        onUnbind(context);
    }
    else
    {
        onBind(context);
    }

    if (context->isWebGL())
    {
        for (size_t bindingIndex : mBufferBindingMask)
        {
            ASSERT(mVertexArrayBuffers[bindingIndex].get());
            mVertexArrayBuffers[bindingIndex]->onNonTFBindingChanged(incr);
        }
    }
}

void VertexArray::setDependentDirtyBits(bool contentsChanged,
                                        VertexArrayBufferBindingMask bufferBindingMask)
{
    DirtyBits dirtyBits(contentsChanged ? (bufferBindingMask.bits() << DIRTY_BIT_BUFFER_DATA_0)
                                        : (bufferBindingMask.bits() << DIRTY_BIT_BINDING_0));
    ASSERT(!mDirtyBitsGuard.valid() || (mDirtyBitsGuard.value() & dirtyBits) == dirtyBits);
    mDirtyBits |= dirtyBits;

    if (bufferBindingMask.test(kElementArrayBufferIndex))
    {
        mIndexRangeInlineCache = {};
    }

    onStateChange(angle::SubjectMessage::ContentsChanged);
}

void VertexArray::onSharedBufferBind(const Context *context,
                                     const Buffer *buffer,
                                     VertexArrayBufferBindingMask bufferBindingMask)
{
    bufferBindingMask &= mBufferBindingMask;
    ASSERT(bufferBindingMask.any());

    // vertexBufferBindingMask is bufferBindingMask without elementBuffer.
    VertexArrayBufferBindingMask vertexBufferBindingMask = bufferBindingMask;
    vertexBufferBindingMask.reset(kElementArrayBufferIndex);

    for (size_t bindingIndex : vertexBufferBindingMask)
    {
        updateCachedMappedArrayBuffersBinding(bindingIndex);
    }

    if (mRobustBufferAccessEnabled)
    {
        for (size_t bindingIndex : vertexBufferBindingMask)
        {
            ASSERT(buffer == mVertexArrayBuffers[bindingIndex].get());
            updateBindingSizeIfChanged(bindingIndex, buffer->getSize());
            updateCachedElementLimit(mState.mVertexBindings[bindingIndex],
                                     mCachedBufferSize[bindingIndex]);
        }
    }

    if (context->isHardenedContext())
    {
        if (buffer->hasTFBBindingConflict())
        {
            mCachedBufferPropertyTransformFeedbackConflict |= vertexBufferBindingMask;
        }
        else
        {
            mCachedBufferPropertyTransformFeedbackConflict &= ~vertexBufferBindingMask;
        }
    }

    // Set proper dirty bits on VertexArray
    mDirtyBits |= mVertexArray->checkBufferForDirtyBits(context, bufferBindingMask);

    // mIndexRangeInlineCache is no longer invalid
    mIndexRangeInlineCache = {};
}

void VertexArray::onBufferChanged(const Context *context,
                                  const Buffer *buffer,
                                  angle::SubjectMessage message,
                                  VertexArrayBufferBindingMask vertexArrayBufferBindingMask)
{
    VertexArrayBufferBindingMask bufferBindingMask =
        vertexArrayBufferBindingMask & mBufferBindingMask;
    ASSERT(buffer);
    ASSERT(bufferBindingMask.any());
    ASSERT(bufferMaskBitsPointToTheSameBuffer(bufferBindingMask));

    switch (message)
    {
        case angle::SubjectMessage::SubjectChanged:
            if (mRobustBufferAccessEnabled)
            {
                VertexArrayBufferBindingMask VertexBufferBindingMask = bufferBindingMask;
                VertexBufferBindingMask.reset(kElementArrayBufferIndex);
                for (size_t bindingIndex : VertexBufferBindingMask)
                {
                    updateBindingSizeIfChanged(bindingIndex, buffer->getSize());
                    updateCachedElementLimit(mState.mVertexBindings[bindingIndex],
                                             mCachedBufferSize[bindingIndex]);
                }
            }
            // This has to be called after updateCachedElementLimit due to
            // mCachedElementLimit dependency
            setDependentDirtyBits(false, bufferBindingMask);
            break;

        case angle::SubjectMessage::BindingChanged:
            if (context->isHardenedContext())
            {
                bufferBindingMask.reset(kElementArrayBufferIndex);

                bool hasConflict = buffer->hasTFBBindingConflict();
                if (hasConflict)
                {
                    mCachedBufferPropertyTransformFeedbackConflict |= bufferBindingMask;
                }
                else
                {
                    mCachedBufferPropertyTransformFeedbackConflict &= ~bufferBindingMask;
                }
            }
            break;

        case angle::SubjectMessage::SubjectMapped:
            bufferBindingMask.reset(kElementArrayBufferIndex);
            for (size_t bindingIndex : bufferBindingMask)
            {
                updateCachedMappedArrayBuffersBinding(bindingIndex);
            }
            onStateChange(angle::SubjectMessage::SubjectMapped);
            break;

        case angle::SubjectMessage::SubjectUnmapped:
        {
            VertexArrayBufferBindingMask VertexBufferBindingMask = bufferBindingMask;
            VertexBufferBindingMask.reset(kElementArrayBufferIndex);
            for (size_t bindingIndex : VertexBufferBindingMask)
            {
                updateCachedMappedArrayBuffersBinding(bindingIndex);
            }
            setDependentDirtyBits(true, bufferBindingMask);
            onStateChange(angle::SubjectMessage::SubjectUnmapped);
        }
        break;

        case angle::SubjectMessage::InternalMemoryAllocationChanged:
            setDependentDirtyBits(false, bufferBindingMask);
            break;

        case angle::SubjectMessage::ContentsChanged:
        {
            VertexArrayBufferBindingMask bufferContentObserverBindingMask =
                vertexArrayBufferBindingMask & mVertexArray->getContentObserversBindingMask();
            if (bufferContentObserverBindingMask.any())
            {
                setDependentDirtyBits(true, bufferBindingMask);
            }
        }
        break;

        default:
            UNREACHABLE();
            break;
    }
}
}  // namespace gl
