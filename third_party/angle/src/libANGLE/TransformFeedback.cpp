//
// Copyright 2014 The ANGLE Project Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//

#include "libANGLE/TransformFeedback.h"

#include "common/mathutil.h"
#include "libANGLE/Buffer.h"
#include "libANGLE/Caps.h"
#include "libANGLE/Context.h"
#include "libANGLE/Program.h"
#include "libANGLE/State.h"
#include "libANGLE/renderer/GLImplFactory.h"
#include "libANGLE/renderer/TransformFeedbackImpl.h"

#include <limits>

namespace gl
{

angle::CheckedNumeric<GLsizeiptr> GetVerticesNeededForDraw(PrimitiveMode primitiveMode,
                                                           GLsizei count,
                                                           GLsizei primcount)
{
    if (count < 0 || primcount < 0)
    {
        return 0;
    }
    // Transform feedback only outputs complete primitives, so we need to round down to the nearest
    // complete primitive before multiplying by the number of instances.
    angle::CheckedNumeric<GLsizeiptr> checkedCount     = count;
    angle::CheckedNumeric<GLsizeiptr> checkedPrimcount = primcount;
    switch (primitiveMode)
    {
        case PrimitiveMode::Triangles:
            return checkedPrimcount * (checkedCount - checkedCount % 3);
        case PrimitiveMode::Lines:
            return checkedPrimcount * (checkedCount - checkedCount % 2);
        case PrimitiveMode::Points:
            return checkedPrimcount * checkedCount;
        default:
            UNREACHABLE();
            return checkedPrimcount * checkedCount;
    }
}

TransformFeedbackState::TransformFeedbackState(size_t maxIndexedBuffers)
    : mLabel(),
      mActive(false),
      mPrimitiveMode(PrimitiveMode::InvalidEnum),
      mPaused(false),
      mVerticesDrawn(0),
      mProgram(nullptr),
      mProgramPipeline(nullptr),
      mIndexedBuffers(maxIndexedBuffers)
{}

TransformFeedbackState::~TransformFeedbackState() {}

const OffsetBindingPointer<Buffer> &TransformFeedbackState::getIndexedBuffer(size_t idx) const
{
    return mIndexedBuffers[idx];
}

const std::vector<OffsetBindingPointer<Buffer>> &TransformFeedbackState::getIndexedBuffers() const
{
    return mIndexedBuffers;
}

GLsizeiptr TransformFeedbackState::getPrimitivesDrawn() const
{
    switch (mPrimitiveMode)
    {
        case gl::PrimitiveMode::Points:
            return mVerticesDrawn;
        case gl::PrimitiveMode::Lines:
            return mVerticesDrawn / 2;
        case gl::PrimitiveMode::Triangles:
            return mVerticesDrawn / 3;
        default:
            return 0;
    }
}

TransformFeedback::TransformFeedback(rx::GLImplFactory *implFactory,
                                     TransformFeedbackID id,
                                     const Caps &caps)
    : RefCountObject(implFactory->generateSerial(), id),
      mState(caps.maxTransformFeedbackSeparateAttributes),
      mImplementation(implFactory->createTransformFeedback(mState))
{
    ASSERT(mImplementation != nullptr);
}

void TransformFeedback::onDestroy(const Context *context)
{
    ASSERT(!context || !context->isCurrentTransformFeedback(this));
    if (mState.mProgram)
    {
        mState.mProgram->release(context);
        mState.mProgram = nullptr;
    }

    ASSERT(!mState.mProgram);
    for (OffsetBindingPointer<Buffer> &indexedBuffer : mState.mIndexedBuffers)
    {
        indexedBuffer.set(context, nullptr, 0, 0);
    }

    if (mImplementation)
    {
        mImplementation->onDestroy(context);
    }
}

TransformFeedback::~TransformFeedback()
{
    SafeDelete(mImplementation);
}

angle::Result TransformFeedback::setLabel(const Context *context, const std::string &label)
{
    mState.mLabel = label;

    if (mImplementation)
    {
        return mImplementation->onLabelUpdate(context);
    }
    return angle::Result::Continue;
}

const std::string &TransformFeedback::getLabel() const
{
    return mState.mLabel;
}

angle::Result TransformFeedback::begin(const Context *context,
                                       PrimitiveMode primitiveMode,
                                       Program *program,
                                       ProgramPipeline *programPipeline)
{
    // TODO: http://anglebug.com/42264023: This method should take in as parameter a
    // ProgramExecutable instead of a Program.

    ANGLE_TRY(mImplementation->begin(context, primitiveMode));
    mState.mActive        = true;
    mState.mPrimitiveMode = primitiveMode;
    mState.mPaused        = false;
    mState.mVerticesDrawn = 0;

    // Program and PPO are not both passed to the transform feedback object. In the event both are
    // bound, program takes precedence.
    ASSERT(program == nullptr || programPipeline == nullptr);
    bindProgram(context, program);
    bindProgramPipeline(context, programPipeline);
    bindPPOPrograms(programPipeline);

    for (auto &buffer : mState.mIndexedBuffers)
    {
        if (buffer.get())
        {
            buffer->onTFActiveChanged(context, true);
        }
    }

    return angle::Result::Continue;
}

angle::Result TransformFeedback::end(const Context *context)
{
    ANGLE_TRY(mImplementation->end(context));
    mState.mActive         = false;
    mState.mPrimitiveMode  = PrimitiveMode::InvalidEnum;
    mState.mPaused         = false;
    mState.mVerticesDrawn  = 0;
    mState.mVertexCapacity = std::nullopt;
    if (mState.mProgram != nullptr)
    {
        mState.mProgram->release(context);
        mState.mProgram = nullptr;
    }
    if (mState.mProgramPipeline != nullptr)
    {
        mState.mProgramPipeline->release(context);
        mState.mProgramPipeline = nullptr;
    }
    for (const ShaderType shaderType : gl::AllShaderTypes())
    {
        mState.mPPOPrograms[shaderType].value = 0;
    }
    for (auto &buffer : mState.mIndexedBuffers)
    {
        if (buffer.get())
        {
            buffer->onTFActiveChanged(context, false);
        }
    }
    return angle::Result::Continue;
}

angle::Result TransformFeedback::pause(const Context *context)
{
    ANGLE_TRY(mImplementation->pause(context));
    mState.mPaused = true;
    return angle::Result::Continue;
}

angle::Result TransformFeedback::resume(const Context *context)
{
    ANGLE_TRY(mImplementation->resume(context));
    mState.mPaused = false;
    return angle::Result::Continue;
}

bool TransformFeedback::isPaused() const
{
    return mState.mPaused;
}

PrimitiveMode TransformFeedback::getPrimitiveMode() const
{
    return mState.mPrimitiveMode;
}

bool TransformFeedback::checkBufferSpaceForDraw(const Context *context,
                                                const GLsizei *counts,
                                                const GLsizei *primcounts,
                                                GLsizei drawcount)
{
    auto vertices = angle::CheckedNumeric<GLsizeiptr>(mState.mVerticesDrawn);
    for (GLsizei drawID = 0; drawID < drawcount; ++drawID)
    {
        GLsizei primcount = ANGLE_UNSAFE_BUFFERS(primcounts ? primcounts[drawID] : 1);
        GLsizei count     = ANGLE_UNSAFE_BUFFERS(counts[drawID]);
        vertices += GetVerticesNeededForDraw(mState.mPrimitiveMode, count, primcount);
    }

    if (!mState.mVertexCapacity.has_value())
    {
        const ProgramExecutable *programExecutable =
            context->getState().getLinkedProgramExecutable(context);
        if (programExecutable)
        {
            // Compute the number of vertices we can draw before overflowing the bound buffers.
            auto strides = programExecutable->getTransformFeedbackStrides();
            ASSERT(strides.size() <= mState.mIndexedBuffers.size() && !strides.empty());
            GLsizeiptr minCapacity = std::numeric_limits<GLsizeiptr>::max();
            for (size_t index = 0; index < strides.size(); index++)
            {
                GLsizeiptr capacity =
                    GetBoundBufferAvailableSize(mState.mIndexedBuffers[index]) / strides[index];
                minCapacity = std::min(minCapacity, capacity);
            }
            mState.mVertexCapacity = minCapacity;
        }
        else
        {
            UNREACHABLE();
            mState.mVertexCapacity = 0;
        }
    }

    return vertices.IsValid() && vertices.ValueOrDie() <= *mState.mVertexCapacity;
}

void TransformFeedback::onVerticesDrawn(const Context *context, GLsizei count, GLsizei primcount)
{
    ASSERT(mState.mActive && !mState.mPaused);
    // All draws should be validated with checkBufferSpaceForDraw so ValueOrDie should never fail.
    mState.mVerticesDrawn =
        (mState.mVerticesDrawn + GetVerticesNeededForDraw(mState.mPrimitiveMode, count, primcount))
            .ValueOrDie();

    for (auto &buffer : mState.mIndexedBuffers)
    {
        if (buffer.get() != nullptr)
        {
            buffer->onDataChanged(context);
        }
    }
}

void TransformFeedback::bindProgram(const Context *context, Program *program)
{
    if (mState.mProgram != program)
    {
        if (mState.mProgram != nullptr)
        {
            mState.mProgram->release(context);
        }
        mState.mProgram = program;
        if (mState.mProgram != nullptr)
        {
            mState.mProgram->addRef();
        }
    }
}

void TransformFeedback::bindProgramPipeline(const Context *context,
                                            ProgramPipeline *programPipeline)
{
    if (mState.mProgramPipeline != programPipeline)
    {
        if (mState.mProgramPipeline != nullptr)
        {
            mState.mProgramPipeline->release(context);
        }
        mState.mProgramPipeline = programPipeline;
        if (mState.mProgramPipeline != nullptr)
        {
            mState.mProgramPipeline->addRef();
        }
    }
}

void TransformFeedback::bindPPOPrograms(ProgramPipeline *programPipeline)
{
    if (programPipeline == nullptr)
    {
        for (const ShaderType shaderType : gl::AllShaderTypes())
        {
            mState.mPPOPrograms[shaderType].value = 0;
        }
        return;
    }

    for (const ShaderType shaderType : gl::AllShaderTypes())
    {
        const Program *program                = programPipeline->getShaderProgram(shaderType);
        mState.mPPOPrograms[shaderType].value = program != nullptr ? program->id().value : 0;
    }
}

bool TransformFeedback::hasBoundProgram(ShaderProgramID program) const
{
    return mState.mProgram != nullptr && mState.mProgram->id().value == program.value;
}

bool TransformFeedback::hasBoundProgramPipeline(ProgramPipelineID programPipeline) const
{
    return mState.mProgramPipeline != nullptr &&
           mState.mProgramPipeline->id().value == programPipeline.value;
}

bool TransformFeedback::hasSamePPOPrograms(ProgramPipeline *programPipeline) const
{
    for (const ShaderType shaderType : gl::AllShaderTypes())
    {
        GLuint shaderProgramIDValue =
            (programPipeline != nullptr && programPipeline->getShaderProgram(shaderType) != nullptr)
                ? programPipeline->getShaderProgram(shaderType)->id().value
                : 0;
        if (mState.mPPOPrograms[shaderType].value != shaderProgramIDValue)
        {
            return false;
        }
    }

    return true;
}

angle::Result TransformFeedback::detachBuffer(const Context *context, BufferID bufferID)
{
    bool isBound = context->isCurrentTransformFeedback(this);
    for (size_t index = 0; index < mState.mIndexedBuffers.size(); index++)
    {
        if (mState.mIndexedBuffers[index].id() == bufferID)
        {
            if (isBound)
            {
                mState.mIndexedBuffers[index]->onTFBindingChanged(context, false, true);
            }
            mState.mIndexedBuffers[index].set(context, nullptr, 0, 0);
            ANGLE_TRY(
                mImplementation->bindIndexedBuffer(context, index, mState.mIndexedBuffers[index]));
        }
    }

    return angle::Result::Continue;
}

angle::Result TransformFeedback::bindIndexedBuffer(const Context *context,
                                                   size_t index,
                                                   Buffer *buffer,
                                                   size_t offset,
                                                   size_t size)
{
    ASSERT(index < mState.mIndexedBuffers.size());
    bool isBound = context && context->isCurrentTransformFeedback(this);
    if (isBound && mState.mIndexedBuffers[index].get())
    {
        mState.mIndexedBuffers[index]->onTFBindingChanged(context, false, true);
    }
    mState.mIndexedBuffers[index].set(context, buffer, offset, size);
    if (isBound && buffer)
    {
        buffer->onTFBindingChanged(context, true, true);
    }

    return mImplementation->bindIndexedBuffer(context, index, mState.mIndexedBuffers[index]);
}

const OffsetBindingPointer<Buffer> &TransformFeedback::getIndexedBuffer(size_t index) const
{
    ASSERT(index < mState.mIndexedBuffers.size());
    return mState.mIndexedBuffers[index];
}

size_t TransformFeedback::getIndexedBufferCount() const
{
    return mState.mIndexedBuffers.size();
}

bool TransformFeedback::buffersBoundForOtherUseInWebGL() const
{
    for (auto &buffer : mState.mIndexedBuffers)
    {
        if (buffer.get() && buffer->isBoundToTFAndNonTFSimultaneously())
        {
            return true;
        }
    }
    return false;
}

bool TransformFeedback::isBufferBound(BufferID bufferID) const
{
    for (const auto &buffer : mState.mIndexedBuffers)
    {
        if (buffer.id() == bufferID)
        {
            return true;
        }
    }
    return false;
}

void TransformFeedback::onBindingChanged(const Context *context, bool bound)
{
    for (auto &buffer : mState.mIndexedBuffers)
    {
        if (buffer.get())
        {
            buffer->onTFBindingChanged(context, bound, true);
        }
    }
}

const std::vector<OffsetBindingPointer<Buffer>> &TransformFeedback::getIndexedBuffers() const
{
    return mState.mIndexedBuffers;
}
}  // namespace gl
