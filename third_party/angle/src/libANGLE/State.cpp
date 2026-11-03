//
// Copyright 2014 The ANGLE Project Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//

// State.cpp: Implements the State class, encapsulating raw GL state.

#include "libANGLE/State.h"
#include "common/unsafe_buffers.h"

#include <string.h>
#include <limits>

#include "common/bitset_utils.h"
#include "common/mathutil.h"
#include "common/matrix_utils.h"
#include "libANGLE/Buffer.h"
#include "libANGLE/Caps.h"
#include "libANGLE/Context.h"
#include "libANGLE/Debug.h"
#include "libANGLE/Framebuffer.h"
#include "libANGLE/FramebufferAttachment.h"
#include "libANGLE/PixelLocalStorage.h"
#include "libANGLE/Query.h"
#include "libANGLE/VertexArray.h"
#include "libANGLE/formatutils.h"
#include "libANGLE/queryconversions.h"
#include "libANGLE/queryutils.h"
#include "libANGLE/renderer/ContextImpl.h"
#include "libANGLE/renderer/TextureImpl.h"

namespace gl
{

namespace
{
bool GetAlternativeQueryType(QueryType type, QueryType *alternativeType)
{
    switch (type)
    {
        case QueryType::AnySamples:
            *alternativeType = QueryType::AnySamplesConservative;
            return true;
        case QueryType::AnySamplesConservative:
            *alternativeType = QueryType::AnySamples;
            return true;
        default:
            return false;
    }
}

// Mapping from a buffer binding type to a dirty bit type.
constexpr angle::PackedEnumMap<BufferBinding, size_t> kBufferBindingDirtyBits = {{
    {BufferBinding::AtomicCounter, state::DIRTY_BIT_ATOMIC_COUNTER_BUFFER_BINDING},
    {BufferBinding::DispatchIndirect, state::DIRTY_BIT_DISPATCH_INDIRECT_BUFFER_BINDING},
    {BufferBinding::DrawIndirect, state::DIRTY_BIT_DRAW_INDIRECT_BUFFER_BINDING},
    {BufferBinding::PixelPack, state::DIRTY_BIT_PACK_BUFFER_BINDING},
    {BufferBinding::PixelUnpack, state::DIRTY_BIT_UNPACK_BUFFER_BINDING},
    {BufferBinding::ShaderStorage, state::DIRTY_BIT_SHADER_STORAGE_BUFFER_BINDING},
    {BufferBinding::Uniform, state::DIRTY_BIT_UNIFORM_BUFFER_BINDINGS},
}};

// Returns a buffer binding function depending on if a dirty bit is set.
template <BufferBinding Target>
constexpr std::pair<BufferBinding, State::BufferBindingSetter> GetBufferBindingSetter()
{
    return std::make_pair(Target, kBufferBindingDirtyBits[Target] != 0
                                      ? &State::setGenericBufferBindingWithBit<Target>
                                      : &State::setGenericBufferBinding<Target>);
}

template <typename T>
using ContextStateMember = T *(State::*);

template <typename T>
T *AllocateOrGetSharedResourceManager(const State *shareContextState,
                                      ContextStateMember<T> member,
                                      T *shareResources = nullptr)
{
    if (shareContextState)
    {
        T *resourceManager = (*shareContextState).*member;
        ASSERT(!resourceManager || resourceManager == shareResources || !shareResources);
        resourceManager->addRef();
        return resourceManager;
    }
    else if (shareResources)
    {
        shareResources->addRef();
        return shareResources;
    }
    else
    {
        return new T();
    }
}

// TODO(https://anglebug.com/42262534): Remove this helper function after blink and chromium part
// refactory done.
bool IsTextureCompatibleWithSampler(TextureType texture, TextureType sampler)
{
    return sampler == texture;
}

// While pixel local storage is active, the drawbuffers on and after 'firstPLSDrawBuffer'
// are blocked from the client and reserved for internal use by PLS.
bool HasPLSOverriddenDrawBuffers(const Caps &caps,
                                 GLuint numActivePlanes,
                                 GLint *firstPLSDrawBuffer)
{
    if (numActivePlanes != 0)
    {
        *firstPLSDrawBuffer =
            caps.maxCombinedDrawBuffersAndPixelLocalStoragePlanes - numActivePlanes;
        return *firstPLSDrawBuffer < caps.maxDrawBuffers;
    }
    return false;
}

uint32_t gIDCounter = 1;
}  // namespace

template <typename BindingT, typename... ArgsT>
ANGLE_INLINE void UpdateNonTFBufferBindingWebGL(const Context *context,
                                                BindingT *binding,
                                                Buffer *buffer,
                                                ArgsT... args)
{
    Buffer *oldBuffer = binding->get();
    if (oldBuffer)
    {
        oldBuffer->onNonTFBindingChanged(-1);
        oldBuffer->release(context);
    }
    binding->assign(buffer, args...);
    if (buffer)
    {
        buffer->addRef();
        buffer->onNonTFBindingChanged(1);
    }
}

template <typename BindingT, typename... ArgsT>
void UpdateTFBufferBindingWebGL(const Context *context,
                                BindingT *binding,
                                bool indexed,
                                ArgsT... args)
{
    if (binding->get())
        (*binding)->onTFBindingChanged(context, false, indexed);
    binding->set(context, args...);
    if (binding->get())
        (*binding)->onTFBindingChanged(context, true, indexed);
}

void UpdateBufferBinding(const Context *context,
                         BindingPointer<Buffer> *binding,
                         Buffer *buffer,
                         BufferBinding target)
{
    if (context->isWebGL())
    {
        if (target == BufferBinding::TransformFeedback)
        {
            UpdateTFBufferBindingWebGL(context, binding, false, buffer);
        }
        else
        {
            UpdateNonTFBufferBindingWebGL(context, binding, buffer);
        }
    }
    else
    {
        binding->set(context, buffer);
    }
}

bool UpdateIndexedBufferBinding(const Context *context,
                                OffsetBindingPointer<Buffer> *binding,
                                Buffer *buffer,
                                BufferBinding target,
                                GLintptr offset,
                                GLsizeiptr size)
{
    bool isBindingDirty = context->isWebGL();
    if (context->isWebGL())
    {
        if (target == BufferBinding::TransformFeedback)
        {
            UpdateTFBufferBindingWebGL(context, binding, true, buffer, offset, size);
        }
        else
        {
            UpdateNonTFBufferBindingWebGL(context, binding, buffer, offset, size);
        }
    }
    else
    {
        ASSERT(!isBindingDirty);
        isBindingDirty = binding->get() != buffer || binding->getOffset() != offset ||
                         binding->getSize() != size;
        // If buffer changed, update everything otherwise update just the offset and size
        if (binding->get() != buffer)
        {
            binding->set(context, buffer, offset, size);
        }
        else if (buffer != nullptr)
        {
            binding->assignOffsetAndSize(offset, size);
        }
    }

    return isBindingDirty;
}

// These template functions must be defined before they are instantiated in kBufferSetters.
template <BufferBinding Target>
void State::setGenericBufferBindingWithBit(const Context *context, Buffer *buffer)
{
    if (context->isWebGL())
    {
        UpdateNonTFBufferBindingWebGL(context, &mBoundBuffers[Target], buffer);
    }
    else
    {
        mBoundBuffers[Target].set(context, buffer);
    }
    mDirtyBits.set(kBufferBindingDirtyBits[Target]);
}

template <BufferBinding Target>
void State::setGenericBufferBinding(const Context *context, Buffer *buffer)
{
    if (context->isWebGL())
    {
        UpdateNonTFBufferBindingWebGL(context, &mBoundBuffers[Target], buffer);
    }
    else
    {
        mBoundBuffers[Target].set(context, buffer);
    }
}

template <>
void State::setGenericBufferBinding<BufferBinding::TransformFeedback>(const Context *context,
                                                                      Buffer *buffer)
{
    if (context->isWebGL())
    {
        UpdateTFBufferBindingWebGL(context, &mBoundBuffers[BufferBinding::TransformFeedback], false,
                                   buffer);
    }
    else
    {
        mBoundBuffers[BufferBinding::TransformFeedback].set(context, buffer);
    }
}

template <>
void State::setGenericBufferBinding<BufferBinding::ElementArray>(const Context *context,
                                                                 Buffer *buffer)
{
    mVertexArray->bindElementBuffer(context, buffer);
    mDirtyObjects.set(state::DIRTY_OBJECT_VERTEX_ARRAY);
}

const angle::PackedEnumMap<BufferBinding, State::BufferBindingSetter> State::kBufferSetters = {{
    GetBufferBindingSetter<BufferBinding::Array>(),
    GetBufferBindingSetter<BufferBinding::AtomicCounter>(),
    GetBufferBindingSetter<BufferBinding::CopyRead>(),
    GetBufferBindingSetter<BufferBinding::CopyWrite>(),
    GetBufferBindingSetter<BufferBinding::DispatchIndirect>(),
    GetBufferBindingSetter<BufferBinding::DrawIndirect>(),
    GetBufferBindingSetter<BufferBinding::ElementArray>(),
    GetBufferBindingSetter<BufferBinding::PixelPack>(),
    GetBufferBindingSetter<BufferBinding::PixelUnpack>(),
    GetBufferBindingSetter<BufferBinding::ShaderStorage>(),
    GetBufferBindingSetter<BufferBinding::Texture>(),
    GetBufferBindingSetter<BufferBinding::TransformFeedback>(),
    GetBufferBindingSetter<BufferBinding::Uniform>(),
}};

ActiveTexturesCache::ActiveTexturesCache() : mTextures{} {}

ActiveTexturesCache::~ActiveTexturesCache()
{
    ASSERT(empty());
}

void ActiveTexturesCache::clear()
{
    for (size_t textureIndex = 0; textureIndex < mTextures.size(); ++textureIndex)
    {
        reset(textureIndex);
    }
}

bool ActiveTexturesCache::empty() const
{
    for (Texture *texture : mTextures)
    {
        if (texture)
        {
            return false;
        }
    }

    return true;
}

ANGLE_INLINE void ActiveTexturesCache::reset(size_t textureIndex)
{
    if (mTextures[textureIndex])
    {
        mTextures[textureIndex] = nullptr;
    }
}

ANGLE_INLINE void ActiveTexturesCache::set(size_t textureIndex, Texture *texture)
{
    ASSERT(texture);
    mTextures[textureIndex] = texture;
}

PrivateState::PrivateState(const Version &clientVersion,
                           bool debug,
                           bool bindGeneratesResourceCHROMIUM,
                           bool clientArraysEnabled,
                           bool robustResourceInit,
                           bool programBinaryCacheEnabled,
                           bool isExternal)
    : mClientVersion(clientVersion),
      mIsExternal(isExternal),
      mDepthClearValue(0),
      mStencilClearValue(0),
      mScissorTest(false),
      mSampleAlphaToCoverage(false),
      mSampleCoverage(false),
      mSampleCoverageValue(0),
      mSampleCoverageInvert(false),
      mSampleMask(false),
      mMaxSampleMaskWords(0),
      mIsSampleShadingEnabled(false),
      mMinSampleShading(0.0f),
      mStencilRef(0),
      mStencilBackRef(0),
      mLineWidth(0),
      mGenerateMipmapHint(GL_NONE),
      mFragmentShaderDerivativeHint(GL_NONE),
      mNearZ(0),
      mFarZ(0),
      mGroupMarkerCount(0),
      mProvokingVertex(gl::ProvokingVertexConvention::LastVertexConvention),
      mActiveSampler(0),
      mPrimitiveRestart(false),
      mMultiSampling(false),
      mSampleAlphaToOne(false),
      mFramebufferSRGB(true),
      mTextureRectangleEnabled(true),
      mLogicOpEnabled(false),
      mLogicOp(LogicalOperation::Copy),
      mPatchVertices(3),
      mPixelLocalStorageActivePlanes(0),
      mNoSimultaneousConstantColorAndAlphaBlendFunc(false),
      mSetBlendIndexedInvoked(false),
      mSetBlendFactorsIndexedInvoked(false),
      mSetBlendEquationsIndexedInvoked(false),
      mBoundingBoxMinX(-1.0f),
      mBoundingBoxMinY(-1.0f),
      mBoundingBoxMinZ(-1.0f),
      mBoundingBoxMinW(1.0f),
      mBoundingBoxMaxX(1.0f),
      mBoundingBoxMaxY(1.0f),
      mBoundingBoxMaxZ(1.0f),
      mBoundingBoxMaxW(1.0f),
      mShadingRatePreserveAspectRatio(false),
      mShadingRateQCOM(ShadingRate::Undefined),
      // If the shading rate has not been set, the shading rate will be SHADING_RATE_1X1_PIXELS_EXT
      mShadingRateEXT(ShadingRate::_1x1),
      mCombinerOps{CombinerOp::Keep, CombinerOp::Keep},
      mFetchPerSample(false),
      mIsPerfMonitorActive(false),
      mTiledRendering(false),
      mBindGeneratesResource(bindGeneratesResourceCHROMIUM),
      mClientArraysEnabled(clientArraysEnabled),
      mRobustResourceInit(robustResourceInit),
      mProgramBinaryCacheEnabled(programBinaryCacheEnabled),
      mVertexArrayPrivate(nullptr),
      mDebug(debug),
      mVertexArrayHandleAllocator(IMPLEMENTATION_MAX_OBJECT_HANDLES)
{}

PrivateState::~PrivateState() = default;

void PrivateState::initialize(Context *context)
{
    mBlendStateExt = BlendStateExt(mCaps.maxDrawBuffers);

    setColorClearValue(0.0f, 0.0f, 0.0f, 0.0f);

    mDepthClearValue   = 1.0f;
    mStencilClearValue = 0;

    mScissorTest    = false;
    mScissor.x      = 0;
    mScissor.y      = 0;
    mScissor.width  = 0;
    mScissor.height = 0;

    mBlendColor.red   = 0;
    mBlendColor.green = 0;
    mBlendColor.blue  = 0;
    mBlendColor.alpha = 0;

    mStencilRef     = 0;
    mStencilBackRef = 0;

    mSampleCoverage       = false;
    mSampleCoverageValue  = 1.0f;
    mSampleCoverageInvert = false;

    mMaxSampleMaskWords = static_cast<GLuint>(mCaps.maxSampleMaskWords);
    mSampleMask         = false;
    mSampleMaskValues.fill(~GLbitfield(0));

    mGenerateMipmapHint           = GL_DONT_CARE;
    mFragmentShaderDerivativeHint = GL_DONT_CARE;

    mLineWidth = 1.0f;

    mViewport.x      = 0;
    mViewport.y      = 0;
    mViewport.width  = 0;
    mViewport.height = 0;
    mNearZ           = 0.0f;
    mFarZ            = 1.0f;

    mClipOrigin    = ClipOrigin::LowerLeft;
    mClipDepthMode = ClipDepthMode::NegativeOneToOne;

    mActiveSampler = 0;

    mVertexAttribCurrentValues.resize(mCaps.maxVertexAttributes);

    // Set all indexes in state attributes type mask to float (default)
    for (int i = 0; i < MAX_VERTEX_ATTRIBS; i++)
    {
        SetComponentTypeMask(ComponentType::Float, i, &mCurrentValuesTypeMask);
    }

    mAllAttribsMask = AttributesMask(angle::BitMask<uint32_t>(mCaps.maxVertexAttributes));

    mMultiSampling    = true;
    mSampleAlphaToOne = false;

    // This coherent blending is enabled by default, but can be enabled or disabled by calling
    // glEnable() or glDisable() with the symbolic constant GL_BLEND_ADVANCED_COHERENT_KHR.
    mBlendAdvancedCoherent = true;

    mPrimitiveRestart = false;

    mNoSimultaneousConstantColorAndAlphaBlendFunc =
        context->getLimitations().noSimultaneousConstantColorAndAlphaBlendFunc ||
        context->getExtensions().webglCompatibilityANGLE;

    mNoUnclampedBlendColor = context->getLimitations().noUnclampedBlendColor;

    // GLES1 emulation: Initialize state for GLES1 if version applies
    if (context->getClientVersion() < Version(2, 0))
    {
        mGLES1State.initialize(context, this);
    }
}

void PrivateState::initializeForCapture(const Context *context)
{
    mCaps       = context->getCaps();
    mExtensions = context->getExtensions();
}

void PrivateState::reset()
{
    mClipDistancesEnabled.reset();
    mVertexArrayMap.clear();
}

void PrivateState::setColorClearValue(float red, float green, float blue, float alpha)
{
    mColorClearValue.red   = red;
    mColorClearValue.green = green;
    mColorClearValue.blue  = blue;
    mColorClearValue.alpha = alpha;
    mDirtyBits.set(state::DIRTY_BIT_CLEAR_COLOR);
}

void PrivateState::setDepthClearValue(float depth)
{
    mDepthClearValue = depth;
    mDirtyBits.set(state::DIRTY_BIT_CLEAR_DEPTH);
}

void PrivateState::setStencilClearValue(int stencil)
{
    mStencilClearValue = stencil;
    mDirtyBits.set(state::DIRTY_BIT_CLEAR_STENCIL);
}

void PrivateState::setColorMask(bool red, bool green, bool blue, bool alpha)
{
    GLint firstPLSDrawBuffer;
    if (hasActivelyOverriddenPLSDrawBuffers(&firstPLSDrawBuffer))
    {
        // Some draw buffers are currently overridden by pixel local storage. Update only the
        // buffers that are still visible to the client and defer the remaining updates until PLS
        // ends.
        assert(firstPLSDrawBuffer == 0 || mExtensions.drawBuffersIndexedAny());
        assert(firstPLSDrawBuffer < mCaps.maxDrawBuffers);
        for (GLint i = 0; i < firstPLSDrawBuffer; ++i)
        {
            ASSERT(mExtensions.drawBuffersIndexedAny());
            setColorMaskIndexed(red, green, blue, alpha, i);
        }
        mPLSDeferredColorMasks = mBlendStateExt.expandColorMaskValue(red, green, blue, alpha);
        return;
    }

    mBlendStateExt.setColorMask(red, green, blue, alpha);
    mDirtyBits.set(state::DIRTY_BIT_COLOR_MASK);
}

void PrivateState::setColorMaskIndexed(bool red, bool green, bool blue, bool alpha, GLuint index)
{
    if (isActivelyOverriddenPLSDrawBuffer(index))
    {
        // The indexed draw buffer is currently overridden by pixel local storage. Defer this update
        // until PLS ends.
        BlendStateExt::ColorMaskStorage::SetValueIndexed(
            index, BlendStateExt::PackColorMask(red, green, blue, alpha), &mPLSDeferredColorMasks);
        return;
    }

    mBlendStateExt.setColorMaskIndexed(index, red, green, blue, alpha);
    mDirtyBits.set(state::DIRTY_BIT_COLOR_MASK);
}

void PrivateState::setDepthMask(bool mask)
{
    if (mDepthStencil.depthMask != mask)
    {
        mDepthStencil.depthMask = mask;
        mDirtyBits.set(state::DIRTY_BIT_DEPTH_MASK);
    }
}

void PrivateState::setRasterizerDiscard(bool enabled)
{
    if (mRasterizer.rasterizerDiscard != enabled)
    {
        mRasterizer.rasterizerDiscard = enabled;
        mDirtyBits.set(state::DIRTY_BIT_RASTERIZER_DISCARD_ENABLED);
    }
}

void PrivateState::setPrimitiveRestart(bool enabled)
{
    if (mPrimitiveRestart != enabled)
    {
        mPrimitiveRestart = enabled;
        mDirtyBits.set(state::DIRTY_BIT_PRIMITIVE_RESTART_ENABLED);
    }
}

void PrivateState::setCullFace(bool enabled)
{
    if (mRasterizer.cullFace != enabled)
    {
        mRasterizer.cullFace = enabled;
        mDirtyBits.set(state::DIRTY_BIT_CULL_FACE_ENABLED);
    }
}

void PrivateState::setCullMode(CullFaceMode mode)
{
    if (mRasterizer.cullMode != mode)
    {
        mRasterizer.cullMode = mode;
        mDirtyBits.set(state::DIRTY_BIT_CULL_FACE);
    }
}

void PrivateState::setFrontFace(GLenum front)
{
    if (mRasterizer.frontFace != front)
    {
        mRasterizer.frontFace = front;
        mDirtyBits.set(state::DIRTY_BIT_FRONT_FACE);
    }
}

void PrivateState::setDepthClamp(bool enabled)
{
    if (mRasterizer.depthClamp != enabled)
    {
        mRasterizer.depthClamp = enabled;
        mDirtyBits.set(state::DIRTY_BIT_EXTENDED);
        mExtendedDirtyBits.set(state::EXTENDED_DIRTY_BIT_DEPTH_CLAMP_ENABLED);
    }
}

void PrivateState::setDepthTest(bool enabled)
{
    if (mDepthStencil.depthTest != enabled)
    {
        mDepthStencil.depthTest = enabled;
        mDirtyBits.set(state::DIRTY_BIT_DEPTH_TEST_ENABLED);
    }
}

void PrivateState::setDepthFunc(GLenum depthFunc)
{
    if (mDepthStencil.depthFunc != depthFunc)
    {
        mDepthStencil.depthFunc = depthFunc;
        mDirtyBits.set(state::DIRTY_BIT_DEPTH_FUNC);
    }
}

void PrivateState::setDepthRange(float zNear, float zFar)
{
    if (mNearZ != zNear || mFarZ != zFar)
    {
        mNearZ = zNear;
        mFarZ  = zFar;
        mDirtyBits.set(state::DIRTY_BIT_DEPTH_RANGE);
    }
}

void PrivateState::setClipControl(ClipOrigin origin, ClipDepthMode depth)
{
    bool updated = false;
    if (mClipOrigin != origin)
    {
        mClipOrigin = origin;
        updated     = true;
    }

    if (mClipDepthMode != depth)
    {
        mClipDepthMode = depth;
        updated        = true;
    }

    if (updated)
    {
        mDirtyBits.set(state::DIRTY_BIT_CLIP_CONTROL);
    }
}

void PrivateState::setBlend(bool enabled)
{
    GLint firstPLSDrawBuffer;
    if (hasActivelyOverriddenPLSDrawBuffers(&firstPLSDrawBuffer))
    {
        // Some draw buffers are currently overridden by pixel local storage. Update only the
        // buffers that are still visible to the client and defer the remaining updates until PLS
        // ends.
        assert(firstPLSDrawBuffer == 0 || mExtensions.drawBuffersIndexedAny());
        assert(firstPLSDrawBuffer < mCaps.maxDrawBuffers);
        for (GLint i = 0; i < firstPLSDrawBuffer; ++i)
        {
            ASSERT(mExtensions.drawBuffersIndexedAny());
            setBlendIndexed(enabled, i);
        }
        mPLSDeferredBlendEnables =
            enabled ? mBlendStateExt.getAllEnabledMask() : DrawBufferMask::Zero();
        return;
    }

    if (mSetBlendIndexedInvoked || mBlendStateExt.getEnabledMask().test(0) != enabled)
    {
        mSetBlendIndexedInvoked = false;
        mBlendStateExt.setEnabled(enabled);
        mDirtyBits.set(state::DIRTY_BIT_BLEND_ENABLED);
    }
}

void PrivateState::setBlendIndexed(bool enabled, GLuint index)
{
    if (isActivelyOverriddenPLSDrawBuffer(index))
    {
        // The indexed draw buffer is currently overridden by pixel local storage. Defer this update
        // until PLS ends.
        mPLSDeferredBlendEnables.set(index, enabled);
        return;
    }

    mSetBlendIndexedInvoked = true;
    mBlendStateExt.setEnabledIndexed(index, enabled);
    mDirtyBits.set(state::DIRTY_BIT_BLEND_ENABLED);
}

ANGLE_INLINE bool PrivateState::hasConstantColor(GLenum sourceRGB, GLenum destRGB) const
{
    return sourceRGB == GL_CONSTANT_COLOR || sourceRGB == GL_ONE_MINUS_CONSTANT_COLOR ||
           destRGB == GL_CONSTANT_COLOR || destRGB == GL_ONE_MINUS_CONSTANT_COLOR;
}

ANGLE_INLINE bool PrivateState::hasConstantAlpha(GLenum sourceRGB, GLenum destRGB) const
{
    return sourceRGB == GL_CONSTANT_ALPHA || sourceRGB == GL_ONE_MINUS_CONSTANT_ALPHA ||
           destRGB == GL_CONSTANT_ALPHA || destRGB == GL_ONE_MINUS_CONSTANT_ALPHA;
}

void PrivateState::setBlendFactors(GLenum sourceRGB,
                                   GLenum destRGB,
                                   GLenum sourceAlpha,
                                   GLenum destAlpha)
{
    if (!mSetBlendFactorsIndexedInvoked &&
        mBlendStateExt.getSrcColorIndexed(0) == FromGLenum<BlendFactorType>(sourceRGB) &&
        mBlendStateExt.getDstColorIndexed(0) == FromGLenum<BlendFactorType>(destRGB) &&
        mBlendStateExt.getSrcAlphaIndexed(0) == FromGLenum<BlendFactorType>(sourceAlpha) &&
        mBlendStateExt.getDstAlphaIndexed(0) == FromGLenum<BlendFactorType>(destAlpha))
    {
        return;
    }

    if (mNoSimultaneousConstantColorAndAlphaBlendFunc)
    {
        if (hasConstantColor(sourceRGB, destRGB))
        {
            mBlendFuncConstantColorDrawBuffers.set();
        }
        else
        {
            mBlendFuncConstantColorDrawBuffers.reset();
        }

        if (hasConstantAlpha(sourceRGB, destRGB))
        {
            mBlendFuncConstantAlphaDrawBuffers.set();
        }
        else
        {
            mBlendFuncConstantAlphaDrawBuffers.reset();
        }
    }

    mSetBlendFactorsIndexedInvoked = false;
    mBlendStateExt.setFactors(sourceRGB, destRGB, sourceAlpha, destAlpha);
    mDirtyBits.set(state::DIRTY_BIT_BLEND_FUNCS);
}

void PrivateState::setBlendFactorsIndexed(GLenum sourceRGB,
                                          GLenum destRGB,
                                          GLenum sourceAlpha,
                                          GLenum destAlpha,
                                          GLuint index)
{
    if (mNoSimultaneousConstantColorAndAlphaBlendFunc)
    {
        mBlendFuncConstantColorDrawBuffers.set(index, hasConstantColor(sourceRGB, destRGB));
        mBlendFuncConstantAlphaDrawBuffers.set(index, hasConstantAlpha(sourceRGB, destRGB));
    }

    mSetBlendFactorsIndexedInvoked = true;
    mBlendStateExt.setFactorsIndexed(index, sourceRGB, destRGB, sourceAlpha, destAlpha);
    mDirtyBits.set(state::DIRTY_BIT_BLEND_FUNCS);
}

void PrivateState::setBlendColor(float red, float green, float blue, float alpha)
{
    // In ES2 without render-to-float extensions, BlendColor clamps to [0,1] on store.
    // On ES3+, or with render-to-float exts enabled, it does not clamp on store.
    const bool isES2 = mClientVersion == ES_2_0;
    const bool hasFloatBlending =
        mExtensions.colorBufferFloatEXT || mExtensions.colorBufferHalfFloatEXT ||
        mExtensions.colorBufferFloatRgbCHROMIUM || mExtensions.colorBufferFloatRgbaCHROMIUM;
    if ((isES2 && !hasFloatBlending) || mNoUnclampedBlendColor)
    {
        red   = clamp01(red);
        green = clamp01(green);
        blue  = clamp01(blue);
        alpha = clamp01(alpha);
    }

    if (mBlendColor.red != red || mBlendColor.green != green || mBlendColor.blue != blue ||
        mBlendColor.alpha != alpha)
    {
        mBlendColor.red   = red;
        mBlendColor.green = green;
        mBlendColor.blue  = blue;
        mBlendColor.alpha = alpha;
        mDirtyBits.set(state::DIRTY_BIT_BLEND_COLOR);
    }
}

void PrivateState::setBlendEquation(GLenum rgbEquation, GLenum alphaEquation)
{
    if (mSetBlendEquationsIndexedInvoked ||
        mBlendStateExt.getEquationColorIndexed(0) != FromGLenum<BlendEquationType>(rgbEquation) ||
        mBlendStateExt.getEquationAlphaIndexed(0) != FromGLenum<BlendEquationType>(alphaEquation))
    {
        mSetBlendEquationsIndexedInvoked = false;
        mBlendStateExt.setEquations(rgbEquation, alphaEquation);
        mDirtyBits.set(state::DIRTY_BIT_BLEND_EQUATIONS);
    }
}

void PrivateState::setBlendEquationIndexed(GLenum rgbEquation, GLenum alphaEquation, GLuint index)
{
    mSetBlendEquationsIndexedInvoked = true;
    mBlendStateExt.setEquationsIndexed(index, rgbEquation, alphaEquation);
    mDirtyBits.set(state::DIRTY_BIT_BLEND_EQUATIONS);
}

void PrivateState::setStencilTest(bool enabled)
{
    if (mDepthStencil.stencilTest != enabled)
    {
        mDepthStencil.stencilTest = enabled;
        mDirtyBits.set(state::DIRTY_BIT_STENCIL_TEST_ENABLED);
    }
}

void PrivateState::setStencilParams(GLenum stencilFunc, GLint stencilRef, GLuint stencilMask)
{
    if (mDepthStencil.stencilFunc != stencilFunc || mStencilRef != stencilRef ||
        mDepthStencil.stencilMask != stencilMask)
    {
        mDepthStencil.stencilFunc = stencilFunc;
        mStencilRef               = stencilRef;
        mDepthStencil.stencilMask = stencilMask;
        mDirtyBits.set(state::DIRTY_BIT_STENCIL_FUNCS_FRONT);
    }
}

void PrivateState::setStencilBackParams(GLenum stencilBackFunc,
                                        GLint stencilBackRef,
                                        GLuint stencilBackMask)
{
    if (mDepthStencil.stencilBackFunc != stencilBackFunc || mStencilBackRef != stencilBackRef ||
        mDepthStencil.stencilBackMask != stencilBackMask)
    {
        mDepthStencil.stencilBackFunc = stencilBackFunc;
        mStencilBackRef               = stencilBackRef;
        mDepthStencil.stencilBackMask = stencilBackMask;
        mDirtyBits.set(state::DIRTY_BIT_STENCIL_FUNCS_BACK);
    }
}

void PrivateState::setStencilWritemask(GLuint stencilWritemask)
{
    if (mDepthStencil.stencilWritemask != stencilWritemask)
    {
        mDepthStencil.stencilWritemask = stencilWritemask;
        mDirtyBits.set(state::DIRTY_BIT_STENCIL_WRITEMASK_FRONT);
    }
}

void PrivateState::setStencilBackWritemask(GLuint stencilBackWritemask)
{
    if (mDepthStencil.stencilBackWritemask != stencilBackWritemask)
    {
        mDepthStencil.stencilBackWritemask = stencilBackWritemask;
        mDirtyBits.set(state::DIRTY_BIT_STENCIL_WRITEMASK_BACK);
    }
}

void PrivateState::setStencilOperations(GLenum stencilFail,
                                        GLenum stencilPassDepthFail,
                                        GLenum stencilPassDepthPass)
{
    if (mDepthStencil.stencilFail != stencilFail ||
        mDepthStencil.stencilPassDepthFail != stencilPassDepthFail ||
        mDepthStencil.stencilPassDepthPass != stencilPassDepthPass)
    {
        mDepthStencil.stencilFail          = stencilFail;
        mDepthStencil.stencilPassDepthFail = stencilPassDepthFail;
        mDepthStencil.stencilPassDepthPass = stencilPassDepthPass;
        mDirtyBits.set(state::DIRTY_BIT_STENCIL_OPS_FRONT);
    }
}

void PrivateState::setStencilBackOperations(GLenum stencilBackFail,
                                            GLenum stencilBackPassDepthFail,
                                            GLenum stencilBackPassDepthPass)
{
    if (mDepthStencil.stencilBackFail != stencilBackFail ||
        mDepthStencil.stencilBackPassDepthFail != stencilBackPassDepthFail ||
        mDepthStencil.stencilBackPassDepthPass != stencilBackPassDepthPass)
    {
        mDepthStencil.stencilBackFail          = stencilBackFail;
        mDepthStencil.stencilBackPassDepthFail = stencilBackPassDepthFail;
        mDepthStencil.stencilBackPassDepthPass = stencilBackPassDepthPass;
        mDirtyBits.set(state::DIRTY_BIT_STENCIL_OPS_BACK);
    }
}

void PrivateState::setPolygonMode(PolygonMode mode)
{
    if (mRasterizer.polygonMode != mode)
    {
        mRasterizer.polygonMode = mode;
        mDirtyBits.set(state::DIRTY_BIT_EXTENDED);
        mExtendedDirtyBits.set(state::EXTENDED_DIRTY_BIT_POLYGON_MODE);
    }
}

void PrivateState::setPolygonOffsetPoint(bool enabled)
{
    if (mRasterizer.polygonOffsetPoint != enabled)
    {
        mRasterizer.polygonOffsetPoint = enabled;
        mDirtyBits.set(state::DIRTY_BIT_EXTENDED);
        mExtendedDirtyBits.set(state::EXTENDED_DIRTY_BIT_POLYGON_OFFSET_POINT_ENABLED);
    }
}

void PrivateState::setPolygonOffsetLine(bool enabled)
{
    if (mRasterizer.polygonOffsetLine != enabled)
    {
        mRasterizer.polygonOffsetLine = enabled;
        mDirtyBits.set(state::DIRTY_BIT_EXTENDED);
        mExtendedDirtyBits.set(state::EXTENDED_DIRTY_BIT_POLYGON_OFFSET_LINE_ENABLED);
    }
}

void PrivateState::setPolygonOffsetFill(bool enabled)
{
    if (mRasterizer.polygonOffsetFill != enabled)
    {
        mRasterizer.polygonOffsetFill = enabled;
        mDirtyBits.set(state::DIRTY_BIT_POLYGON_OFFSET_FILL_ENABLED);
    }
}

void PrivateState::setFetchPerSample(bool enabled)
{
    if (mFetchPerSample != enabled)
    {
        mFetchPerSample = enabled;
        mDirtyBits.set(state::DIRTY_BIT_EXTENDED);
        mExtendedDirtyBits.set(state::EXTENDED_DIRTY_BIT_FETCH_PER_SAMPLE_ENABLED);
    }
}

void PrivateState::setPolygonOffsetParams(GLfloat factor, GLfloat units, GLfloat clamp)
{
    // An application can pass NaN values here, so handle this gracefully
    mRasterizer.polygonOffsetFactor = factor != factor ? 0.0f : factor;
    mRasterizer.polygonOffsetUnits  = units != units ? 0.0f : units;
    mRasterizer.polygonOffsetClamp  = clamp != clamp ? 0.0f : clamp;
    mDirtyBits.set(state::DIRTY_BIT_POLYGON_OFFSET);
}

void PrivateState::setSampleAlphaToCoverage(bool enabled)
{
    if (mSampleAlphaToCoverage != enabled)
    {
        mSampleAlphaToCoverage = enabled;
        mDirtyBits.set(state::DIRTY_BIT_SAMPLE_ALPHA_TO_COVERAGE_ENABLED);
    }
}

void PrivateState::setSampleCoverage(bool enabled)
{
    if (mSampleCoverage != enabled)
    {
        mSampleCoverage = enabled;
        mDirtyBits.set(state::DIRTY_BIT_SAMPLE_COVERAGE_ENABLED);
    }
}

void PrivateState::setSampleCoverageParams(GLclampf value, bool invert)
{
    mSampleCoverageValue  = value;
    mSampleCoverageInvert = invert;
    mDirtyBits.set(state::DIRTY_BIT_SAMPLE_COVERAGE);
}

void PrivateState::setSampleMaskEnabled(bool enabled)
{
    if (mSampleMask != enabled)
    {
        mSampleMask = enabled;
        mDirtyBits.set(state::DIRTY_BIT_SAMPLE_MASK_ENABLED);
    }
}

void PrivateState::setSampleMaskParams(GLuint maskNumber, GLbitfield mask)
{
    ASSERT(maskNumber < mMaxSampleMaskWords);
    mSampleMaskValues[maskNumber] = mask;
    mDirtyBits.set(state::DIRTY_BIT_SAMPLE_MASK);
}

void PrivateState::setSampleAlphaToOne(bool enabled)
{
    if (mSampleAlphaToOne != enabled)
    {
        mSampleAlphaToOne = enabled;
        mDirtyBits.set(state::DIRTY_BIT_SAMPLE_ALPHA_TO_ONE);
    }
}

void PrivateState::setBlendAdvancedCoherent(bool enabled)
{
    if (mBlendAdvancedCoherent != enabled)
    {
        mBlendAdvancedCoherent = enabled;
        mDirtyBits.set(state::EXTENDED_DIRTY_BIT_BLEND_ADVANCED_COHERENT);
    }
}

void PrivateState::setMultisampling(bool enabled)
{
    if (mMultiSampling != enabled)
    {
        mMultiSampling = enabled;
        mDirtyBits.set(state::DIRTY_BIT_MULTISAMPLING);
    }
}

void PrivateState::setSampleShading(bool enabled)
{
    if (mIsSampleShadingEnabled != enabled)
    {
        mIsSampleShadingEnabled = enabled;
        mDirtyBits.set(state::DIRTY_BIT_SAMPLE_SHADING);
    }
}

void PrivateState::setMinSampleShading(float value)
{
    value = gl::clamp01(value);

    if (mMinSampleShading != value)
    {
        mMinSampleShading = value;
        mDirtyBits.set(state::DIRTY_BIT_SAMPLE_SHADING);
    }
}

void PrivateState::setScissorTest(bool enabled)
{
    if (mScissorTest != enabled)
    {
        mScissorTest = enabled;
        mDirtyBits.set(state::DIRTY_BIT_SCISSOR_TEST_ENABLED);
    }
}

void PrivateState::setScissorParams(GLint x, GLint y, GLsizei width, GLsizei height)
{
    // Skip if same scissor info
    if (mScissor.x != x || mScissor.y != y || mScissor.width != width || mScissor.height != height)
    {
        mScissor.x      = x;
        mScissor.y      = y;
        mScissor.width  = width;
        mScissor.height = height;
        mDirtyBits.set(state::DIRTY_BIT_SCISSOR);
    }
}

void PrivateState::setDither(bool enabled)
{
    if (mRasterizer.dither != enabled)
    {
        mRasterizer.dither = enabled;
        mDirtyBits.set(state::DIRTY_BIT_DITHER_ENABLED);
    }
}

void PrivateState::setViewportParams(GLint x, GLint y, GLsizei width, GLsizei height)
{
    // [OpenGL ES 2.0.25] section 2.12.1 page 45:
    // Viewport width and height are clamped to implementation-dependent maximums when specified.
    width  = std::min(width, mCaps.maxViewportWidth);
    height = std::min(height, mCaps.maxViewportHeight);

    // Skip if same viewport info
    if (mViewport.x != x || mViewport.y != y || mViewport.width != width ||
        mViewport.height != height)
    {
        mViewport.x      = x;
        mViewport.y      = y;
        mViewport.width  = width;
        mViewport.height = height;
        mDirtyBits.set(state::DIRTY_BIT_VIEWPORT);
    }
}

void PrivateState::setShadingRateQCOM(ShadingRate rate)
{
    mShadingRateQCOM = rate;
    mDirtyBits.set(state::DIRTY_BIT_EXTENDED);
    mExtendedDirtyBits.set(state::EXTENDED_DIRTY_BIT_SHADING_RATE_QCOM);
}

void PrivateState::setShadingRateEXT(ShadingRate rate)
{
    mShadingRateEXT = rate;
    mDirtyBits.set(state::DIRTY_BIT_EXTENDED);
    mExtendedDirtyBits.set(state::EXTENDED_DIRTY_BIT_SHADING_RATE_EXT);
}

void PrivateState::setShadingRateCombinerOps(CombinerOp combinerOp0, CombinerOp combinerOp1)
{
    mCombinerOps[0] = combinerOp0;
    mCombinerOps[1] = combinerOp1;
    mDirtyBits.set(state::DIRTY_BIT_EXTENDED);
    mExtendedDirtyBits.set(state::EXTENDED_DIRTY_BIT_SHADING_RATE_EXT);
}

void PrivateState::setPackAlignment(GLint alignment)
{
    mPack.alignment = alignment;
    mDirtyBits.set(state::DIRTY_BIT_PACK_STATE);
}

void PrivateState::setPackReverseRowOrder(bool reverseRowOrder)
{
    mPack.reverseRowOrder = reverseRowOrder;
    mDirtyBits.set(state::DIRTY_BIT_PACK_STATE);
}

void PrivateState::setPackRowLength(GLint rowLength)
{
    mPack.rowLength = rowLength;
    mDirtyBits.set(state::DIRTY_BIT_PACK_STATE);
}

void PrivateState::setPackSkipRows(GLint skipRows)
{
    mPack.skipRows = skipRows;
    mDirtyBits.set(state::DIRTY_BIT_PACK_STATE);
}

void PrivateState::setPackSkipPixels(GLint skipPixels)
{
    mPack.skipPixels = skipPixels;
    mDirtyBits.set(state::DIRTY_BIT_PACK_STATE);
}

void PrivateState::setUnpackAlignment(GLint alignment)
{
    mUnpack.alignment = alignment;
    mDirtyBits.set(state::DIRTY_BIT_UNPACK_STATE);
}

void PrivateState::setUnpackRowLength(GLint rowLength)
{
    mUnpack.rowLength = rowLength;
    mDirtyBits.set(state::DIRTY_BIT_UNPACK_STATE);
}

void PrivateState::setUnpackImageHeight(GLint imageHeight)
{
    mUnpack.imageHeight = imageHeight;
    mDirtyBits.set(state::DIRTY_BIT_UNPACK_STATE);
}

bool PrivateState::hasActivelyOverriddenPLSDrawBuffers(GLint *firstActivePLSDrawBuffer) const
{
    return HasPLSOverriddenDrawBuffers(mCaps, mPixelLocalStorageActivePlanes,
                                       firstActivePLSDrawBuffer);
}

bool PrivateState::isActivelyOverriddenPLSDrawBuffer(GLint drawbuffer) const
{
    GLint firstPLSDrawBuffer;
    return hasActivelyOverriddenPLSDrawBuffers(&firstPLSDrawBuffer) &&
           drawbuffer >= firstPLSDrawBuffer;
}

void PrivateState::setUnpackSkipImages(GLint skipImages)
{
    mUnpack.skipImages = skipImages;
    mDirtyBits.set(state::DIRTY_BIT_UNPACK_STATE);
}

void PrivateState::setUnpackSkipRows(GLint skipRows)
{
    mUnpack.skipRows = skipRows;
    mDirtyBits.set(state::DIRTY_BIT_UNPACK_STATE);
}

void PrivateState::setUnpackSkipPixels(GLint skipPixels)
{
    mUnpack.skipPixels = skipPixels;
    mDirtyBits.set(state::DIRTY_BIT_UNPACK_STATE);
}

void PrivateState::setFramebufferSRGB(bool sRGB)
{
    if (mFramebufferSRGB != sRGB)
    {
        mFramebufferSRGB = sRGB;
        mDirtyBits.set(state::DIRTY_BIT_FRAMEBUFFER_SRGB_WRITE_CONTROL_MODE);
        mDirtyObjects.set(state::DIRTY_OBJECT_DRAW_FRAMEBUFFER);
        if (isRobustResourceInitEnabled())
        {
            mDirtyObjects.set(state::DIRTY_OBJECT_DRAW_ATTACHMENTS);
        }
    }
}

void PrivateState::setPatchVertices(GLuint value)
{
    if (mPatchVertices != value)
    {
        mPatchVertices = value;
        mDirtyBits.set(state::DIRTY_BIT_PATCH_VERTICES);
    }
}

void PrivateState::setPixelLocalStorageActivePlanes(GLsizei n)
{
    if (n != 0)
    {
        // Pixel local storage is beginning.
        ASSERT(mPixelLocalStorageActivePlanes == 0);

        GLint firstPLSDrawBuffer;
        if (HasPLSOverriddenDrawBuffers(mCaps, n, &firstPLSDrawBuffer))
        {
            // Save the original blend & color mask state so we can restore it when PLS ends.
            mPLSDeferredBlendEnables = mBlendStateExt.getEnabledMask();
            mPLSDeferredColorMasks   = mBlendStateExt.getColorMaskBits();

            // Disable blend & enable color mask on the reserved PLS planes.
            if (firstPLSDrawBuffer == 0)
            {
                if (mBlendStateExt.getEnabledMask().test(0))
                {
                    setBlend(false);
                }
                if (mBlendStateExt.getColorMaskIndexed(0) != BlendStateExt::kColorMaskRGBA)
                {
                    setColorMask(true, true, true, true);
                }
            }
            else
            {
                ASSERT(mExtensions.drawBuffersIndexedAny());
                for (GLint i = firstPLSDrawBuffer; i < mCaps.maxDrawBuffers; ++i)
                {
                    if (mBlendStateExt.getEnabledMask().test(i))
                    {
                        setBlendIndexed(false, i);
                    }
                    if (mBlendStateExt.getColorMaskIndexed(i) != BlendStateExt::kColorMaskRGBA)
                    {
                        setColorMaskIndexed(true, true, true, true, i);
                    }
                }
            }
        }

        // Set mPixelLocalStorageActivePlanes last, so the setBlend()/setColorMask() calls above
        // don't bounce.
        mPixelLocalStorageActivePlanes = n;
    }
    else
    {
        // Pixel local storage is ending.
        ASSERT(mPixelLocalStorageActivePlanes != 0);

        // Set mPixelLocalStorageActivePlanes first, so the following calls to
        // setBlend()/setColorMask() don't bounce.
        GLsizei formerPLSPlaneCount    = mPixelLocalStorageActivePlanes;
        mPixelLocalStorageActivePlanes = 0;

        GLint firstPLSDrawBuffer;
        if (HasPLSOverriddenDrawBuffers(mCaps, formerPLSPlaneCount, &firstPLSDrawBuffer))
        {
            bool r, g, b, a;
            if (firstPLSDrawBuffer == 0)
            {
                if (mPLSDeferredBlendEnables.test(0))
                {
                    setBlend(true);
                }
                const uint8_t colorMask =
                    BlendStateExt::ColorMaskStorage::GetValueIndexed(0, mPLSDeferredColorMasks);
                if (colorMask != BlendStateExt::kColorMaskRGBA)
                {
                    BlendStateExt::UnpackColorMask(colorMask, &r, &g, &b, &a);
                    setColorMask(r, g, b, a);
                }
            }
            else
            {
                for (GLint i = firstPLSDrawBuffer; i < mCaps.maxDrawBuffers; ++i)
                {
                    if (mPLSDeferredBlendEnables.test(i))
                    {
                        setBlendIndexed(true, i);
                    }
                    const uint8_t colorMask =
                        BlendStateExt::ColorMaskStorage::GetValueIndexed(i, mPLSDeferredColorMasks);
                    if (colorMask != BlendStateExt::kColorMaskRGBA)
                    {
                        BlendStateExt::UnpackColorMask(colorMask, &r, &g, &b, &a);
                        setColorMaskIndexed(r, g, b, a, i);
                    }
                }
            }
        }
    }
}

void PrivateState::setLineWidth(GLfloat width)
{
    mLineWidth = width;
    mDirtyBits.set(state::DIRTY_BIT_LINE_WIDTH);
}

void PrivateState::setGenerateMipmapHint(GLenum hint)
{
    mGenerateMipmapHint = hint;
    mDirtyBits.set(state::DIRTY_BIT_EXTENDED);
    mExtendedDirtyBits.set(state::EXTENDED_DIRTY_BIT_MIPMAP_GENERATION_HINT);
}

void PrivateState::setFragmentShaderDerivativeHint(GLenum hint)
{
    mFragmentShaderDerivativeHint = hint;
    mDirtyBits.set(state::DIRTY_BIT_EXTENDED);
    mExtendedDirtyBits.set(state::EXTENDED_DIRTY_BIT_SHADER_DERIVATIVE_HINT);
    // Note: This hint could be propagated to shader translator so we can write ddx, ddx_coarse, or
    // ddx_fine depending on the hint.  Ignore for now. It is valid for implementations to ignore
    // the hint.
}

void PrivateState::setActiveSampler(unsigned int active)
{
    mActiveSampler = active;
}

AttributesMask PrivateState::getAndResetDirtyCurrentValues() const
{
    AttributesMask retVal = mDirtyCurrentValues;
    mDirtyCurrentValues.reset();
    return retVal;
}

void PrivateState::setClipDistanceEnable(int idx, bool enable)
{
    if (enable)
    {
        mClipDistancesEnabled.set(idx);
    }
    else
    {
        mClipDistancesEnabled.reset(idx);
    }

    mDirtyBits.set(state::DIRTY_BIT_EXTENDED);
    mExtendedDirtyBits.set(state::EXTENDED_DIRTY_BIT_CLIP_DISTANCES);
}

void PrivateState::setBoundingBox(GLfloat minX,
                                  GLfloat minY,
                                  GLfloat minZ,
                                  GLfloat minW,
                                  GLfloat maxX,
                                  GLfloat maxY,
                                  GLfloat maxZ,
                                  GLfloat maxW)
{
    mBoundingBoxMinX = minX;
    mBoundingBoxMinY = minY;
    mBoundingBoxMinZ = minZ;
    mBoundingBoxMinW = minW;
    mBoundingBoxMaxX = maxX;
    mBoundingBoxMaxY = maxY;
    mBoundingBoxMaxZ = maxZ;
    mBoundingBoxMaxW = maxW;
}

void PrivateState::setLogicOpEnabled(bool enabled)
{
    if (mLogicOpEnabled != enabled)
    {
        mLogicOpEnabled = enabled;
        mDirtyBits.set(state::DIRTY_BIT_EXTENDED);
        mExtendedDirtyBits.set(state::EXTENDED_DIRTY_BIT_LOGIC_OP_ENABLED);
    }
}

void PrivateState::setLogicOp(LogicalOperation opcode)
{
    if (mLogicOp != opcode)
    {
        mLogicOp = opcode;
        mDirtyBits.set(state::DIRTY_BIT_EXTENDED);
        mExtendedDirtyBits.set(state::EXTENDED_DIRTY_BIT_LOGIC_OP);
    }
}

void PrivateState::setVertexAttribf(GLuint index, const GLfloat values[4])
{
    ASSERT(static_cast<size_t>(index) < mVertexAttribCurrentValues.size());
    mVertexAttribCurrentValues[index].setFloatValues(values);
    mDirtyBits.set(state::DIRTY_BIT_CURRENT_VALUES);
    mDirtyCurrentValues.set(index);
    SetComponentTypeMask(ComponentType::Float, index, &mCurrentValuesTypeMask);
}

void PrivateState::setVertexAttribu(GLuint index, const GLuint values[4])
{
    ASSERT(static_cast<size_t>(index) < mVertexAttribCurrentValues.size());
    mVertexAttribCurrentValues[index].setUnsignedIntValues(values);
    mDirtyBits.set(state::DIRTY_BIT_CURRENT_VALUES);
    mDirtyCurrentValues.set(index);
    SetComponentTypeMask(ComponentType::UnsignedInt, index, &mCurrentValuesTypeMask);
}

void PrivateState::setVertexAttribi(GLuint index, const GLint values[4])
{
    ASSERT(static_cast<size_t>(index) < mVertexAttribCurrentValues.size());
    mVertexAttribCurrentValues[index].setIntValues(values);
    mDirtyBits.set(state::DIRTY_BIT_CURRENT_VALUES);
    mDirtyCurrentValues.set(index);
    SetComponentTypeMask(ComponentType::Int, index, &mCurrentValuesTypeMask);
}

void PrivateState::setEnableFeature(GLenum feature, bool enabled)
{
    switch (feature)
    {
        case GL_MULTISAMPLE_EXT:
            setMultisampling(enabled);
            return;
        case GL_SAMPLE_ALPHA_TO_ONE_EXT:
            setSampleAlphaToOne(enabled);
            return;
        case GL_BLEND_ADVANCED_COHERENT_KHR:
            setBlendAdvancedCoherent(enabled);
            return;
        case GL_CULL_FACE:
            setCullFace(enabled);
            return;
        case GL_POLYGON_OFFSET_POINT_NV:
            setPolygonOffsetPoint(enabled);
            return;
        case GL_POLYGON_OFFSET_LINE_NV:
            setPolygonOffsetLine(enabled);
            return;
        case GL_POLYGON_OFFSET_FILL:
            setPolygonOffsetFill(enabled);
            return;
        case GL_DEPTH_CLAMP_EXT:
            setDepthClamp(enabled);
            return;
        case GL_SAMPLE_ALPHA_TO_COVERAGE:
            setSampleAlphaToCoverage(enabled);
            return;
        case GL_SAMPLE_COVERAGE:
            setSampleCoverage(enabled);
            return;
        case GL_SCISSOR_TEST:
            setScissorTest(enabled);
            return;
        case GL_STENCIL_TEST:
            setStencilTest(enabled);
            return;
        case GL_DEPTH_TEST:
            setDepthTest(enabled);
            return;
        case GL_BLEND:
            setBlend(enabled);
            return;
        case GL_DITHER:
            setDither(enabled);
            return;
        case GL_COLOR_LOGIC_OP:
            if (mClientVersion < ES_2_0)
            {
                // Handle logicOp in GLES1 through the GLES1 state management and emulation.
                // Otherwise this state could be set as part of ANGLE_logic_op.
                break;
            }
            setLogicOpEnabled(enabled);
            return;
        case GL_PRIMITIVE_RESTART_FIXED_INDEX:
            setPrimitiveRestart(enabled);
            return;
        case GL_RASTERIZER_DISCARD:
            setRasterizerDiscard(enabled);
            return;
        case GL_SAMPLE_MASK:
            setSampleMaskEnabled(enabled);
            return;
        case GL_DEBUG_OUTPUT_SYNCHRONOUS:
            mDebug.setOutputSynchronous(enabled);
            return;
        case GL_DEBUG_OUTPUT:
            mDebug.setOutputEnabled(enabled);
            return;
        case GL_FRAMEBUFFER_SRGB_EXT:
            setFramebufferSRGB(enabled);
            return;
        case GL_TEXTURE_RECTANGLE_ANGLE:
            mTextureRectangleEnabled = enabled;
            return;
        case GL_SAMPLE_SHADING:
            setSampleShading(enabled);
            return;
        // GL_APPLE_clip_distance / GL_EXT_clip_cull_distance / GL_ANGLE_clip_cull_distance
        case GL_CLIP_DISTANCE0_EXT:
        case GL_CLIP_DISTANCE1_EXT:
        case GL_CLIP_DISTANCE2_EXT:
        case GL_CLIP_DISTANCE3_EXT:
        case GL_CLIP_DISTANCE4_EXT:
        case GL_CLIP_DISTANCE5_EXT:
        case GL_CLIP_DISTANCE6_EXT:
        case GL_CLIP_DISTANCE7_EXT:
            // NOTE(hqle): These enums are conflicted with GLES1's enums, need
            // to do additional check here:
            if (mClientVersion >= ES_2_0)
            {
                setClipDistanceEnable(feature - GL_CLIP_DISTANCE0_EXT, enabled);
                return;
            }
            break;
        case GL_SHADING_RATE_PRESERVE_ASPECT_RATIO_QCOM:
            mShadingRatePreserveAspectRatio = enabled;
            return;
        case GL_FETCH_PER_SAMPLE_ARM:
            setFetchPerSample(enabled);
            return;
        default:
            break;
    }

    ASSERT(mClientVersion < ES_2_0);

    // GLES1 emulation. Need to separate from main switch due to conflict enum between
    // GL_CLIP_DISTANCE0_EXT & GL_CLIP_PLANE0
    switch (feature)
    {
        case GL_ALPHA_TEST:
            mGLES1State.mAlphaTestEnabled = enabled;
            break;
        case GL_TEXTURE_2D:
            mGLES1State.setTextureEnabled(mActiveSampler, TextureType::_2D, enabled);
            break;
        case GL_TEXTURE_CUBE_MAP:
            mGLES1State.setTextureEnabled(mActiveSampler, TextureType::CubeMap, enabled);
            break;
        case GL_LIGHTING:
            mGLES1State.mLightingEnabled = enabled;
            break;
        case GL_LIGHT0:
        case GL_LIGHT1:
        case GL_LIGHT2:
        case GL_LIGHT3:
        case GL_LIGHT4:
        case GL_LIGHT5:
        case GL_LIGHT6:
        case GL_LIGHT7:
            mGLES1State.mLights[feature - GL_LIGHT0].enabled = enabled;
            break;
        case GL_NORMALIZE:
            mGLES1State.mNormalizeEnabled = enabled;
            break;
        case GL_RESCALE_NORMAL:
            mGLES1State.mRescaleNormalEnabled = enabled;
            break;
        case GL_COLOR_MATERIAL:
            mGLES1State.mColorMaterialEnabled = enabled;
            break;
        case GL_CLIP_PLANE0:
        case GL_CLIP_PLANE1:
        case GL_CLIP_PLANE2:
        case GL_CLIP_PLANE3:
        case GL_CLIP_PLANE4:
        case GL_CLIP_PLANE5:
            mGLES1State.mClipPlanes[feature - GL_CLIP_PLANE0].enabled = enabled;
            break;
        case GL_FOG:
            mGLES1State.mFogEnabled = enabled;
            break;
        case GL_POINT_SMOOTH:
            mGLES1State.mPointSmoothEnabled = enabled;
            break;
        case GL_LINE_SMOOTH:
            mGLES1State.mLineSmoothEnabled = enabled;
            break;
        case GL_POINT_SPRITE_OES:
            mGLES1State.mPointSpriteEnabled = enabled;
            break;
        case GL_COLOR_LOGIC_OP:
            mGLES1State.setLogicOpEnabled(enabled);
            break;
        default:
            UNREACHABLE();
    }
}

void PrivateState::setEnableFeatureIndexed(GLenum feature, bool enabled, GLuint index)
{
    switch (feature)
    {
        case GL_BLEND:
            setBlendIndexed(enabled, index);
            break;
        default:
            UNREACHABLE();
    }
}

bool PrivateState::getEnableFeature(GLenum feature) const
{
    switch (feature)
    {
        case GL_MULTISAMPLE_EXT:
            return isMultisamplingEnabled();
        case GL_SAMPLE_ALPHA_TO_ONE_EXT:
            return isSampleAlphaToOneEnabled();
        case GL_BLEND_ADVANCED_COHERENT_KHR:
            return isBlendAdvancedCoherentEnabled();
        case GL_CULL_FACE:
            return isCullFaceEnabled();
        case GL_POLYGON_OFFSET_POINT_NV:
            return isPolygonOffsetPointEnabled();
        case GL_POLYGON_OFFSET_LINE_NV:
            return isPolygonOffsetLineEnabled();
        case GL_POLYGON_OFFSET_FILL:
            return isPolygonOffsetFillEnabled();
        case GL_DEPTH_CLAMP_EXT:
            return isDepthClampEnabled();
        case GL_SAMPLE_ALPHA_TO_COVERAGE:
            return isSampleAlphaToCoverageEnabled();
        case GL_SAMPLE_COVERAGE:
            return isSampleCoverageEnabled();
        case GL_SCISSOR_TEST:
            return isScissorTestEnabled();
        case GL_STENCIL_TEST:
            return isStencilTestEnabled();
        case GL_DEPTH_TEST:
            return isDepthTestEnabled();
        case GL_BLEND:
            return isBlendEnabled();
        case GL_DITHER:
            return isDitherEnabled();
        case GL_COLOR_LOGIC_OP:
            if (mClientVersion < ES_2_0)
            {
                // Handle logicOp in GLES1 through the GLES1 state management and emulation.
                break;
            }
            return isLogicOpEnabled();
        case GL_PRIMITIVE_RESTART_FIXED_INDEX:
            return isPrimitiveRestartEnabled();
        case GL_RASTERIZER_DISCARD:
            return isRasterizerDiscardEnabled();
        case GL_SAMPLE_MASK:
            return isSampleMaskEnabled();
        case GL_DEBUG_OUTPUT_SYNCHRONOUS:
            return mDebug.isOutputSynchronous();
        case GL_DEBUG_OUTPUT:
            return mDebug.isOutputEnabled();
        case GL_BIND_GENERATES_RESOURCE_CHROMIUM:
            return isBindGeneratesResourceEnabled();
        case GL_CLIENT_ARRAYS_ANGLE:
            return areClientArraysEnabled();
        case GL_FRAMEBUFFER_SRGB_EXT:
            return getFramebufferSRGB();
        case GL_ROBUST_RESOURCE_INITIALIZATION_ANGLE:
            return mRobustResourceInit;
        case GL_PROGRAM_CACHE_ENABLED_ANGLE:
            return mProgramBinaryCacheEnabled;
        case GL_TEXTURE_RECTANGLE_ANGLE:
            return mTextureRectangleEnabled;
        case GL_SAMPLE_SHADING:
            return isSampleShadingEnabled();
        // GL_APPLE_clip_distance / GL_EXT_clip_cull_distance / GL_ANGLE_clip_cull_distance
        case GL_CLIP_DISTANCE0_EXT:
        case GL_CLIP_DISTANCE1_EXT:
        case GL_CLIP_DISTANCE2_EXT:
        case GL_CLIP_DISTANCE3_EXT:
        case GL_CLIP_DISTANCE4_EXT:
        case GL_CLIP_DISTANCE5_EXT:
        case GL_CLIP_DISTANCE6_EXT:
        case GL_CLIP_DISTANCE7_EXT:
            if (mClientVersion >= ES_2_0)
            {
                // If GLES version is 1, the GL_CLIP_DISTANCE0_EXT enum will be used as
                // GL_CLIP_PLANE0 instead.
                return mClipDistancesEnabled.test(feature - GL_CLIP_DISTANCE0_EXT);
            }
            break;
        case GL_SHADING_RATE_PRESERVE_ASPECT_RATIO_QCOM:
            return mShadingRatePreserveAspectRatio;
        case GL_FETCH_PER_SAMPLE_ARM:
            return mFetchPerSample;
    }

    ASSERT(mClientVersion < ES_2_0);

    switch (feature)
    {
        // GLES1 emulation
        case GL_ALPHA_TEST:
            return mGLES1State.mAlphaTestEnabled;
        case GL_VERTEX_ARRAY:
            return mGLES1State.mVertexArrayEnabled;
        case GL_NORMAL_ARRAY:
            return mGLES1State.mNormalArrayEnabled;
        case GL_COLOR_ARRAY:
            return mGLES1State.mColorArrayEnabled;
        case GL_POINT_SIZE_ARRAY_OES:
            return mGLES1State.mPointSizeArrayEnabled;
        case GL_TEXTURE_COORD_ARRAY:
            return mGLES1State.mTexCoordArrayEnabled[mGLES1State.mClientActiveTexture];
        case GL_TEXTURE_2D:
            return mGLES1State.isTextureTargetEnabled(getActiveSampler(), TextureType::_2D);
        case GL_TEXTURE_CUBE_MAP:
            return mGLES1State.isTextureTargetEnabled(getActiveSampler(), TextureType::CubeMap);
        case GL_LIGHTING:
            return mGLES1State.mLightingEnabled;
        case GL_LIGHT0:
        case GL_LIGHT1:
        case GL_LIGHT2:
        case GL_LIGHT3:
        case GL_LIGHT4:
        case GL_LIGHT5:
        case GL_LIGHT6:
        case GL_LIGHT7:
            return mGLES1State.mLights[feature - GL_LIGHT0].enabled;
        case GL_NORMALIZE:
            return mGLES1State.mNormalizeEnabled;
        case GL_RESCALE_NORMAL:
            return mGLES1State.mRescaleNormalEnabled;
        case GL_COLOR_MATERIAL:
            return mGLES1State.mColorMaterialEnabled;
        case GL_CLIP_PLANE0:
        case GL_CLIP_PLANE1:
        case GL_CLIP_PLANE2:
        case GL_CLIP_PLANE3:
        case GL_CLIP_PLANE4:
        case GL_CLIP_PLANE5:
            return mGLES1State.mClipPlanes[feature - GL_CLIP_PLANE0].enabled;
        case GL_FOG:
            return mGLES1State.mFogEnabled;
        case GL_POINT_SMOOTH:
            return mGLES1State.mPointSmoothEnabled;
        case GL_LINE_SMOOTH:
            return mGLES1State.mLineSmoothEnabled;
        case GL_POINT_SPRITE_OES:
            return mGLES1State.mPointSpriteEnabled;
        case GL_COLOR_LOGIC_OP:
            return mGLES1State.mLogicOpEnabled;
        default:
            UNREACHABLE();
            return false;
    }
}

bool PrivateState::getEnableFeatureIndexed(GLenum feature, GLuint index) const
{
    switch (feature)
    {
        case GL_BLEND:
            return isBlendEnabledIndexed(index);
        default:
            UNREACHABLE();
            return false;
    }
}

void PrivateState::getBooleanv(GLenum pname, GLboolean *params) const
{
    switch (pname)
    {
        case GL_SAMPLE_COVERAGE_INVERT:
            *params = mSampleCoverageInvert;
            break;
        case GL_DEPTH_WRITEMASK:
            *params = mDepthStencil.depthMask;
            break;
        case GL_COLOR_WRITEMASK:
        {
            // non-indexed get returns the state of draw buffer zero
            getBooleani_v(GL_COLOR_WRITEMASK, 0, params);
            break;
        }
        case GL_CULL_FACE:
            *params = mRasterizer.cullFace;
            break;
        case GL_POLYGON_OFFSET_POINT_NV:
            *params = mRasterizer.polygonOffsetPoint;
            break;
        case GL_POLYGON_OFFSET_LINE_NV:
            *params = mRasterizer.polygonOffsetLine;
            break;
        case GL_POLYGON_OFFSET_FILL:
            *params = mRasterizer.polygonOffsetFill;
            break;
        case GL_DEPTH_CLAMP_EXT:
            *params = mRasterizer.depthClamp;
            break;
        case GL_SAMPLE_ALPHA_TO_COVERAGE:
            *params = mSampleAlphaToCoverage;
            break;
        case GL_SAMPLE_COVERAGE:
            *params = mSampleCoverage;
            break;
        case GL_SAMPLE_MASK:
            *params = mSampleMask;
            break;
        case GL_SCISSOR_TEST:
            *params = mScissorTest;
            break;
        case GL_STENCIL_TEST:
            *params = mDepthStencil.stencilTest;
            break;
        case GL_DEPTH_TEST:
            *params = mDepthStencil.depthTest;
            break;
        case GL_BLEND:
            *params = isBlendEnabled();
            break;
        case GL_DITHER:
            *params = mRasterizer.dither;
            break;
        case GL_COLOR_LOGIC_OP:
            if (mClientVersion < ES_2_0)
            {
                // Handle logicOp in GLES1 through the GLES1 state management.
                *params = getEnableFeature(pname);
            }
            else
            {
                *params = mLogicOpEnabled;
            }
            break;
        case GL_PRIMITIVE_RESTART_FIXED_INDEX:
            *params = mPrimitiveRestart;
            break;
        case GL_RASTERIZER_DISCARD:
            *params = isRasterizerDiscardEnabled() ? GL_TRUE : GL_FALSE;
            break;
        case GL_DEBUG_OUTPUT_SYNCHRONOUS:
            *params = mDebug.isOutputSynchronous() ? GL_TRUE : GL_FALSE;
            break;
        case GL_DEBUG_OUTPUT:
            *params = mDebug.isOutputEnabled() ? GL_TRUE : GL_FALSE;
            break;
        case GL_MULTISAMPLE_EXT:
            *params = mMultiSampling;
            break;
        case GL_SAMPLE_ALPHA_TO_ONE_EXT:
            *params = mSampleAlphaToOne;
            break;
        case GL_BIND_GENERATES_RESOURCE_CHROMIUM:
            *params = isBindGeneratesResourceEnabled() ? GL_TRUE : GL_FALSE;
            break;
        case GL_CLIENT_ARRAYS_ANGLE:
            *params = areClientArraysEnabled() ? GL_TRUE : GL_FALSE;
            break;
        case GL_FRAMEBUFFER_SRGB_EXT:
            *params = getFramebufferSRGB() ? GL_TRUE : GL_FALSE;
            break;
        case GL_ROBUST_RESOURCE_INITIALIZATION_ANGLE:
            *params = mRobustResourceInit ? GL_TRUE : GL_FALSE;
            break;
        case GL_PROGRAM_CACHE_ENABLED_ANGLE:
            *params = mProgramBinaryCacheEnabled ? GL_TRUE : GL_FALSE;
            break;
        case GL_TEXTURE_RECTANGLE_ANGLE:
            *params = mTextureRectangleEnabled ? GL_TRUE : GL_FALSE;
            break;
        case GL_LIGHT_MODEL_TWO_SIDE:
            *params = IsLightModelTwoSided(&mGLES1State);
            break;
        case GL_SAMPLE_SHADING:
            *params = mIsSampleShadingEnabled;
            break;
        case GL_PRIMITIVE_RESTART_FOR_PATCHES_SUPPORTED:
            *params = mCaps.primitiveRestartForPatchesSupported ? GL_TRUE : GL_FALSE;
            break;
        case GL_ROBUST_FRAGMENT_SHADER_OUTPUT_ANGLE:
            *params = mExtensions.robustFragmentShaderOutputANGLE ? GL_TRUE : GL_FALSE;
            break;
        // GL_APPLE_clip_distance / GL_EXT_clip_cull_distance / GL_ANGLE_clip_cull_distance
        case GL_CLIP_DISTANCE0_EXT:
        case GL_CLIP_DISTANCE1_EXT:
        case GL_CLIP_DISTANCE2_EXT:
        case GL_CLIP_DISTANCE3_EXT:
        case GL_CLIP_DISTANCE4_EXT:
        case GL_CLIP_DISTANCE5_EXT:
        case GL_CLIP_DISTANCE6_EXT:
        case GL_CLIP_DISTANCE7_EXT:
            if (mClientVersion >= ES_2_0)
            {
                // If GLES version is 1, the GL_CLIP_DISTANCE0_EXT enum will be used as
                // GL_CLIP_PLANE0 instead.
                *params = mClipDistancesEnabled.test(pname - GL_CLIP_DISTANCE0_EXT);
            }
            break;
        // GL_ARM_shader_framebuffer_fetch
        case GL_FETCH_PER_SAMPLE_ARM:
            *params = mFetchPerSample;
            break;
        // GL_ARM_shader_framebuffer_fetch
        case GL_FRAGMENT_SHADER_FRAMEBUFFER_FETCH_MRT_ARM:
            *params = mCaps.fragmentShaderFramebufferFetchMRT;
            break;
        // EXT_fragment_shading_rate
        case GL_FRAGMENT_SHADING_RATE_NON_TRIVIAL_COMBINERS_SUPPORTED_EXT:
            *params =
                mCaps.fragmentShadingRateProperties.fragmentShadingRateNonTrivialCombinersSupport;
            break;
        default:
            if (mClientVersion < ES_2_0)
            {
                *params = getEnableFeature(pname);
            }
            else
            {
                UNREACHABLE();
            }
            break;
    }
}

void PrivateState::getFloatv(GLenum pname, GLfloat *params) const
{
    switch (pname)
    {
        case GL_LINE_WIDTH:
            *params = mLineWidth;
            break;
        case GL_SAMPLE_COVERAGE_VALUE:
            *params = mSampleCoverageValue;
            break;
        case GL_DEPTH_CLEAR_VALUE:
            *params = mDepthClearValue;
            break;
        case GL_POLYGON_OFFSET_FACTOR:
            *params = mRasterizer.polygonOffsetFactor;
            break;
        case GL_POLYGON_OFFSET_UNITS:
            *params = mRasterizer.polygonOffsetUnits;
            break;
        case GL_POLYGON_OFFSET_CLAMP_EXT:
            *params = mRasterizer.polygonOffsetClamp;
            break;
        case GL_DEPTH_RANGE:
            params[0] = mNearZ;
            ANGLE_UNSAFE_TODO(params[1]) = mFarZ;
            break;
        case GL_COLOR_CLEAR_VALUE:
            params[0] = mColorClearValue.red;
            ANGLE_UNSAFE_TODO(params[1]) = mColorClearValue.green;
            ANGLE_UNSAFE_TODO(params[2]) = mColorClearValue.blue;
            ANGLE_UNSAFE_TODO(params[3]) = mColorClearValue.alpha;
            break;
        case GL_BLEND_COLOR:
            params[0] = mBlendColor.red;
            ANGLE_UNSAFE_TODO(params[1]) = mBlendColor.green;
            ANGLE_UNSAFE_TODO(params[2]) = mBlendColor.blue;
            ANGLE_UNSAFE_TODO(params[3]) = mBlendColor.alpha;
            break;
        case GL_MULTISAMPLE_EXT:
            *params = static_cast<GLfloat>(mMultiSampling);
            break;
        case GL_SAMPLE_ALPHA_TO_ONE_EXT:
            *params = static_cast<GLfloat>(mSampleAlphaToOne);
            break;
        case GL_ALPHA_TEST_REF:
            *params = mGLES1State.mAlphaTestParameters.ref;
            break;
        case GL_CURRENT_COLOR:
        {
            const auto &color = mGLES1State.mCurrentColor;
            params[0]         = color.red;
            ANGLE_UNSAFE_TODO(params[1]) = color.green;
            ANGLE_UNSAFE_TODO(params[2]) = color.blue;
            ANGLE_UNSAFE_TODO(params[3]) = color.alpha;
            break;
        }
        case GL_CURRENT_NORMAL:
        {
            const auto &normal = mGLES1State.mCurrentNormal;
            params[0]          = normal[0];
            ANGLE_UNSAFE_TODO(params[1]) = normal[1];
            ANGLE_UNSAFE_TODO(params[2]) = normal[2];
            break;
        }
        case GL_CURRENT_TEXTURE_COORDS:
        {
            const auto &texcoord = mGLES1State.mCurrentTextureCoords[mActiveSampler];
            params[0]            = texcoord.s;
            ANGLE_UNSAFE_TODO(params[1]) = texcoord.t;
            ANGLE_UNSAFE_TODO(params[2]) = texcoord.r;
            ANGLE_UNSAFE_TODO(params[3]) = texcoord.q;
            break;
        }
        case GL_MODELVIEW_MATRIX:
            ANGLE_UNSAFE_TODO(memcpy(params, mGLES1State.mModelviewMatrices.back().constData(),
                                     16 * sizeof(GLfloat)));
            break;
        case GL_PROJECTION_MATRIX:
            ANGLE_UNSAFE_TODO(memcpy(params, mGLES1State.mProjectionMatrices.back().constData(),
                                     16 * sizeof(GLfloat)));
            break;
        case GL_TEXTURE_MATRIX:
            ANGLE_UNSAFE_TODO(
                memcpy(params, mGLES1State.mTextureMatrices[mActiveSampler].back().constData(),
                       16 * sizeof(GLfloat)));
            break;
        case GL_LIGHT_MODEL_AMBIENT:
            GetLightModelParameters(&mGLES1State, pname, params);
            break;
        case GL_FOG_MODE:
        case GL_FOG_DENSITY:
        case GL_FOG_START:
        case GL_FOG_END:
        case GL_FOG_COLOR:
            GetFogParameters(&mGLES1State, pname, params);
            break;
        case GL_POINT_SIZE:
            GetPointSize(&mGLES1State, params);
            break;
        case GL_POINT_SIZE_MIN:
        case GL_POINT_SIZE_MAX:
        case GL_POINT_FADE_THRESHOLD_SIZE:
        case GL_POINT_DISTANCE_ATTENUATION:
            GetPointParameter(&mGLES1State, FromGLenum<PointParameter>(pname), params);
            break;
        case GL_MIN_SAMPLE_SHADING_VALUE:
            *params = mMinSampleShading;
            break;
        // GL_ARM_shader_framebuffer_fetch
        case GL_FETCH_PER_SAMPLE_ARM:
            *params = mFetchPerSample ? 1.0f : 0.0f;
            break;
        // GL_ARM_shader_framebuffer_fetch
        case GL_FRAGMENT_SHADER_FRAMEBUFFER_FETCH_MRT_ARM:
            *params = mCaps.fragmentShaderFramebufferFetchMRT ? 1.0f : 0.0f;
            break;
        default:
            UNREACHABLE();
            break;
    }
}

void PrivateState::getIntegerv(GLenum pname, GLint *params) const
{
    // Please note: DEPTH_CLEAR_VALUE is not included in our internal getIntegerv implementation
    // because it is stored as a float, despite the fact that the GL ES 2.0 spec names
    // GetIntegerv as its native query function. As it would require conversion in any
    // case, this should make no difference to the calling application. You may find it in
    // State::getFloatv.
    switch (pname)
    {
        case GL_PACK_ALIGNMENT:
            *params = mPack.alignment;
            break;
        case GL_PACK_REVERSE_ROW_ORDER_ANGLE:
            *params = mPack.reverseRowOrder;
            break;
        case GL_PACK_ROW_LENGTH:
            *params = mPack.rowLength;
            break;
        case GL_PACK_SKIP_ROWS:
            *params = mPack.skipRows;
            break;
        case GL_PACK_SKIP_PIXELS:
            *params = mPack.skipPixels;
            break;
        case GL_UNPACK_ALIGNMENT:
            *params = mUnpack.alignment;
            break;
        case GL_UNPACK_ROW_LENGTH:
            *params = mUnpack.rowLength;
            break;
        case GL_UNPACK_IMAGE_HEIGHT:
            *params = mUnpack.imageHeight;
            break;
        case GL_UNPACK_SKIP_IMAGES:
            *params = mUnpack.skipImages;
            break;
        case GL_UNPACK_SKIP_ROWS:
            *params = mUnpack.skipRows;
            break;
        case GL_UNPACK_SKIP_PIXELS:
            *params = mUnpack.skipPixels;
            break;
        case GL_GENERATE_MIPMAP_HINT:
            *params = mGenerateMipmapHint;
            break;
        case GL_FRAGMENT_SHADER_DERIVATIVE_HINT_OES:
            *params = mFragmentShaderDerivativeHint;
            break;
        case GL_ACTIVE_TEXTURE:
            *params = (static_cast<GLint>(mActiveSampler) + GL_TEXTURE0);
            break;
        case GL_STENCIL_FUNC:
            *params = mDepthStencil.stencilFunc;
            break;
        case GL_STENCIL_REF:
            *params = mStencilRef;
            break;
        case GL_STENCIL_VALUE_MASK:
            *params = mDepthStencil.stencilMask;
            break;
        case GL_STENCIL_BACK_FUNC:
            *params = mDepthStencil.stencilBackFunc;
            break;
        case GL_STENCIL_BACK_REF:
            *params = mStencilBackRef;
            break;
        case GL_STENCIL_BACK_VALUE_MASK:
            *params = mDepthStencil.stencilBackMask;
            break;
        case GL_STENCIL_FAIL:
            *params = mDepthStencil.stencilFail;
            break;
        case GL_STENCIL_PASS_DEPTH_FAIL:
            *params = mDepthStencil.stencilPassDepthFail;
            break;
        case GL_STENCIL_PASS_DEPTH_PASS:
            *params = mDepthStencil.stencilPassDepthPass;
            break;
        case GL_STENCIL_BACK_FAIL:
            *params = mDepthStencil.stencilBackFail;
            break;
        case GL_STENCIL_BACK_PASS_DEPTH_FAIL:
            *params = mDepthStencil.stencilBackPassDepthFail;
            break;
        case GL_STENCIL_BACK_PASS_DEPTH_PASS:
            *params = mDepthStencil.stencilBackPassDepthPass;
            break;
        case GL_DEPTH_FUNC:
            *params = mDepthStencil.depthFunc;
            break;
        case GL_BLEND_SRC_RGB:
            // non-indexed get returns the state of draw buffer zero
            *params = ToGLenum(mBlendStateExt.getSrcColorIndexed(0));
            break;
        case GL_BLEND_SRC_ALPHA:
            *params = ToGLenum(mBlendStateExt.getSrcAlphaIndexed(0));
            break;
        case GL_BLEND_DST_RGB:
            *params = ToGLenum(mBlendStateExt.getDstColorIndexed(0));
            break;
        case GL_BLEND_DST_ALPHA:
            *params = ToGLenum(mBlendStateExt.getDstAlphaIndexed(0));
            break;
        case GL_BLEND_EQUATION_RGB:
            *params = ToGLenum(mBlendStateExt.getEquationColorIndexed(0));
            break;
        case GL_BLEND_EQUATION_ALPHA:
            *params = ToGLenum(mBlendStateExt.getEquationAlphaIndexed(0));
            break;
        case GL_STENCIL_WRITEMASK:
            *params = mDepthStencil.stencilWritemask;
            break;
        case GL_STENCIL_BACK_WRITEMASK:
            *params = mDepthStencil.stencilBackWritemask;
            break;
        case GL_STENCIL_CLEAR_VALUE:
            *params = mStencilClearValue;
            break;
        case GL_VIEWPORT:
            params[0] = mViewport.x;
            ANGLE_UNSAFE_TODO(params[1]) = mViewport.y;
            ANGLE_UNSAFE_TODO(params[2]) = mViewport.width;
            ANGLE_UNSAFE_TODO(params[3]) = mViewport.height;
            break;
        case GL_SCISSOR_BOX:
            params[0] = mScissor.x;
            ANGLE_UNSAFE_TODO(params[1]) = mScissor.y;
            ANGLE_UNSAFE_TODO(params[2]) = mScissor.width;
            ANGLE_UNSAFE_TODO(params[3]) = mScissor.height;
            break;
        case GL_POLYGON_MODE_NV:
            *params = ToGLenum(mRasterizer.polygonMode);
            break;
        case GL_CULL_FACE_MODE:
            *params = ToGLenum(mRasterizer.cullMode);
            break;
        case GL_FRONT_FACE:
            *params = mRasterizer.frontFace;
            break;

        case GL_MULTISAMPLE_EXT:
            *params = static_cast<GLint>(mMultiSampling);
            break;
        case GL_SAMPLE_ALPHA_TO_ONE_EXT:
            *params = static_cast<GLint>(mSampleAlphaToOne);
            break;
        case GL_ALPHA_TEST_FUNC:
            *params = ToGLenum(mGLES1State.mAlphaTestParameters.func);
            break;
        case GL_CLIENT_ACTIVE_TEXTURE:
            *params = mGLES1State.mClientActiveTexture + GL_TEXTURE0;
            break;
        case GL_MATRIX_MODE:
            *params = ToGLenum(mGLES1State.mMatrixMode);
            break;
        case GL_SHADE_MODEL:
            *params = ToGLenum(mGLES1State.mShadeModel);
            break;
        case GL_MODELVIEW_STACK_DEPTH:
        case GL_PROJECTION_STACK_DEPTH:
        case GL_TEXTURE_STACK_DEPTH:
            *params = mGLES1State.getCurrentMatrixStackDepth(pname);
            break;
        case GL_LOGIC_OP_MODE:
            *params = ToGLenum(mGLES1State.mLogicOp);
            break;
        case GL_BLEND_SRC:
            // non-indexed get returns the state of draw buffer zero
            *params = ToGLenum(mBlendStateExt.getSrcColorIndexed(0));
            break;
        case GL_BLEND_DST:
            *params = ToGLenum(mBlendStateExt.getDstColorIndexed(0));
            break;
        case GL_PERSPECTIVE_CORRECTION_HINT:
        case GL_POINT_SMOOTH_HINT:
        case GL_LINE_SMOOTH_HINT:
        case GL_FOG_HINT:
            *params = mGLES1State.getHint(pname);
            break;

        // GL_ANGLE_provoking_vertex
        case GL_PROVOKING_VERTEX_ANGLE:
            *params = ToGLenum(mProvokingVertex);
            break;

        case GL_PATCH_VERTICES:
            *params = mPatchVertices;
            break;

        // GL_EXT_clip_control
        case GL_CLIP_ORIGIN_EXT:
            *params = ToGLenum(mClipOrigin);
            break;
        case GL_CLIP_DEPTH_MODE_EXT:
            *params = ToGLenum(mClipDepthMode);
            break;

        // GL_QCOM_shading_rate
        case GL_SHADING_RATE_QCOM:
            *params = ToGLenum(mShadingRateQCOM);
            break;

        // GL_EXT_fragment_shading_rate
        case GL_SHADING_RATE_EXT:
            *params = ToGLenum(mShadingRateEXT);
            break;

        // GL_ANGLE_shader_pixel_local_storage
        case GL_PIXEL_LOCAL_STORAGE_ACTIVE_PLANES_ANGLE:
            *params = mPixelLocalStorageActivePlanes;
            break;

        // GL_ARM_shader_framebuffer_fetch
        case GL_FETCH_PER_SAMPLE_ARM:
            *params = mFetchPerSample ? 1 : 0;
            break;

        // GL_ARM_shader_framebuffer_fetch
        case GL_FRAGMENT_SHADER_FRAMEBUFFER_FETCH_MRT_ARM:
            *params = mCaps.fragmentShaderFramebufferFetchMRT ? 1 : 0;
            break;

        // GL_KHR_blend_equation_advanced_coherent
        case GL_BLEND_ADVANCED_COHERENT_KHR:
            *params = mBlendAdvancedCoherent ? 1 : 0;
            break;

        default:
            UNREACHABLE();
            break;
    }
}

void PrivateState::getIntegeri_v(GLenum target, GLuint index, GLint *data) const
{
    switch (target)
    {
        case GL_BLEND_SRC_RGB:
            ASSERT(static_cast<size_t>(index) < mBlendStateExt.getDrawBufferCount());
            *data = ToGLenum(mBlendStateExt.getSrcColorIndexed(index));
            break;
        case GL_BLEND_SRC_ALPHA:
            ASSERT(static_cast<size_t>(index) < mBlendStateExt.getDrawBufferCount());
            *data = ToGLenum(mBlendStateExt.getSrcAlphaIndexed(index));
            break;
        case GL_BLEND_DST_RGB:
            ASSERT(static_cast<size_t>(index) < mBlendStateExt.getDrawBufferCount());
            *data = ToGLenum(mBlendStateExt.getDstColorIndexed(index));
            break;
        case GL_BLEND_DST_ALPHA:
            ASSERT(static_cast<size_t>(index) < mBlendStateExt.getDrawBufferCount());
            *data = ToGLenum(mBlendStateExt.getDstAlphaIndexed(index));
            break;
        case GL_BLEND_EQUATION_RGB:
            ASSERT(static_cast<size_t>(index) < mBlendStateExt.getDrawBufferCount());
            *data = ToGLenum(mBlendStateExt.getEquationColorIndexed(index));
            break;
        case GL_BLEND_EQUATION_ALPHA:
            ASSERT(static_cast<size_t>(index) < mBlendStateExt.getDrawBufferCount());
            *data = ToGLenum(mBlendStateExt.getEquationAlphaIndexed(index));
            break;
        case GL_SAMPLE_MASK_VALUE:
            ASSERT(static_cast<size_t>(index) < mSampleMaskValues.size());
            *data = mSampleMaskValues[index];
            break;
        default:
            UNREACHABLE();
            break;
    }
}

void PrivateState::getBooleani_v(GLenum target, GLuint index, GLboolean *data) const
{
    switch (target)
    {
        case GL_COLOR_WRITEMASK:
        {
            ASSERT(static_cast<size_t>(index) < mBlendStateExt.getDrawBufferCount());
            const uint8_t colorMask = isActivelyOverriddenPLSDrawBuffer(index)
                                          ? BlendStateExt::ColorMaskStorage::GetValueIndexed(
                                                index, mPLSDeferredColorMasks)
                                          : mBlendStateExt.getColorMaskIndexed(index);
            bool r, g, b, a;
            BlendStateExt::UnpackColorMask(colorMask, &r, &g, &b, &a);
            data[0] = r;
            ANGLE_UNSAFE_TODO(data[1]) = g;
            ANGLE_UNSAFE_TODO(data[2]) = b;
            ANGLE_UNSAFE_TODO(data[3]) = a;
            break;
        }
        default:
            UNREACHABLE();
            break;
    }
}

VertexArrayID PrivateState::getVertexArrayId() const
{
    ASSERT(mVertexArrayPrivate != nullptr);
    return mVertexArrayPrivate->id();
}

void PrivateState::setVertexAttribFormat(GLuint attribIndex,
                                         GLint size,
                                         VertexAttribType type,
                                         bool normalized,
                                         bool pureInteger,
                                         GLuint relativeOffset)
{
    mVertexArrayPrivate->setVertexAttribFormat(attribIndex, size, type, normalized, pureInteger,
                                               relativeOffset);
    mDirtyObjects.set(state::DIRTY_OBJECT_VERTEX_ARRAY);
}

void PrivateState::setVertexAttribBinding(GLuint attribIndex, GLuint bindingIndex)
{
    mVertexArrayPrivate->setVertexAttribBinding(attribIndex, bindingIndex);
    mDirtyObjects.set(state::DIRTY_OBJECT_VERTEX_ARRAY);
}

void PrivateState::setVertexBindingDivisor(GLuint bindingIndex, GLuint divisor)
{
    mVertexArrayPrivate->setVertexBindingDivisor(bindingIndex, divisor);
    mDirtyObjects.set(state::DIRTY_OBJECT_VERTEX_ARRAY);
}

void PrivateState::setEnableVertexAttribArray(unsigned int attribNum, bool enabled)
{
    mVertexArrayPrivate->enableAttribute(attribNum, enabled);
    mDirtyObjects.set(state::DIRTY_OBJECT_VERTEX_ARRAY);
}

void PrivateState::setVertexAttribDivisor(GLuint index, GLuint divisor)
{
    mVertexArrayPrivate->setVertexAttribDivisor(index, divisor);
    mDirtyObjects.set(state::DIRTY_OBJECT_VERTEX_ARRAY);
}

// State implementation.
State::State(const State *shareContextState,
             egl::ShareGroup *shareGroup,
             TextureManager *shareTextures,
             SemaphoreManager *shareSemaphores,
             egl::ContextMutex *contextMutex,
             const Version &clientVersion,
             bool debug,
             bool bindGeneratesResourceCHROMIUM,
             bool clientArraysEnabled,
             bool robustResourceInit,
             bool programBinaryCacheEnabled,
             EGLenum contextPriority,
             bool hasRobustAccess,
             bool hasProtectedContent,
             bool isExternal,
             bool passthroughShaders)
    : mID({gIDCounter++}),
      mContextPriority(contextPriority),
      mHasRobustAccess(hasRobustAccess),
      mHasProtectedContent(hasProtectedContent),
      mIsDebugContext(debug),
      mPassthroughShaders(passthroughShaders),
      mShareGroup(shareGroup),
      mContextMutex(contextMutex),
      mBufferManager(AllocateOrGetSharedResourceManager(shareContextState, &State::mBufferManager)),
      mShaderProgramManager(
          AllocateOrGetSharedResourceManager(shareContextState, &State::mShaderProgramManager)),
      mTextureManager(AllocateOrGetSharedResourceManager(shareContextState,
                                                         &State::mTextureManager,
                                                         shareTextures)),
      mRenderbufferManager(
          AllocateOrGetSharedResourceManager(shareContextState, &State::mRenderbufferManager)),
      mSamplerManager(
          AllocateOrGetSharedResourceManager(shareContextState, &State::mSamplerManager)),
      mSyncManager(AllocateOrGetSharedResourceManager(shareContextState, &State::mSyncManager)),
      mFramebufferManager(new FramebufferManager()),
      mProgramPipelineManager(new ProgramPipelineManager()),
      mMemoryObjectManager(
          AllocateOrGetSharedResourceManager(shareContextState, &State::mMemoryObjectManager)),
      mSemaphoreManager(AllocateOrGetSharedResourceManager(shareContextState,
                                                           &State::mSemaphoreManager,
                                                           shareSemaphores)),
      mReadFramebuffer(nullptr),
      mDrawFramebuffer(nullptr),
      mProgram(nullptr),
      mVertexArray(nullptr),
      mDisplayTextureShareGroup(shareTextures != nullptr),
      mMaxShaderCompilerThreads(std::numeric_limits<GLuint>::max()),
      mPrivateState(clientVersion,
                    debug,
                    bindGeneratesResourceCHROMIUM,
                    clientArraysEnabled,
                    robustResourceInit,
                    programBinaryCacheEnabled,
                    isExternal)
{}

State::~State() {}

void State::initialize(Context *context)
{
    const Extensions &nativeExtensions = context->getImplementation()->getNativeExtensions();
    const Version &clientVersion       = context->getClientVersion();

    mPrivateState.initialize(context);

    mUniformBuffers.resize(getCaps().maxUniformBufferBindings);

    mSamplerTextures[TextureType::_2D].resize(getCaps().maxCombinedTextureImageUnits);
    mSamplerTextures[TextureType::CubeMap].resize(getCaps().maxCombinedTextureImageUnits);
    if (clientVersion >= Version(3, 0) || nativeExtensions.texture3DOES)
    {
        mSamplerTextures[TextureType::_3D].resize(getCaps().maxCombinedTextureImageUnits);
    }
    if (clientVersion >= Version(3, 0))
    {
        mSamplerTextures[TextureType::_2DArray].resize(getCaps().maxCombinedTextureImageUnits);
    }
    if (clientVersion >= Version(3, 1) || nativeExtensions.textureMultisampleANGLE)
    {
        mSamplerTextures[TextureType::_2DMultisample].resize(
            getCaps().maxCombinedTextureImageUnits);
    }
    if (clientVersion >= Version(3, 2) || nativeExtensions.textureStorageMultisample2dArrayOES)
    {
        mSamplerTextures[TextureType::_2DMultisampleArray].resize(
            getCaps().maxCombinedTextureImageUnits);
    }
    if (clientVersion >= Version(3, 1))
    {
        mAtomicCounterBuffers.resize(getCaps().maxAtomicCounterBufferBindings);
        mShaderStorageBuffers.resize(getCaps().maxShaderStorageBufferBindings);
    }
    if (clientVersion >= Version(3, 1) ||
        (context->getImplementation()->getNativePixelLocalStorageOptions().type ==
         ShPixelLocalStorageType::ImageLoadStore))
    {
        mImageUnits.resize(getCaps().maxImageUnits);
    }
    if (clientVersion >= Version(3, 1) || nativeExtensions.textureCubeMapArrayAny())
    {
        mSamplerTextures[TextureType::CubeMapArray].resize(getCaps().maxCombinedTextureImageUnits);
    }
    if (clientVersion >= Version(3, 1) || nativeExtensions.textureCubeMapArrayAny())
    {
        mSamplerTextures[TextureType::Buffer].resize(getCaps().maxCombinedTextureImageUnits);
    }
    if (nativeExtensions.textureRectangleANGLE)
    {
        mSamplerTextures[TextureType::Rectangle].resize(getCaps().maxCombinedTextureImageUnits);
    }
    if (nativeExtensions.EGLImageExternalOES || nativeExtensions.EGLStreamConsumerExternalNV)
    {
        mSamplerTextures[TextureType::External].resize(getCaps().maxCombinedTextureImageUnits);
    }
    mCompleteTextureBindings.reserve(getCaps().maxCombinedTextureImageUnits);
    for (int32_t textureIndex = 0; textureIndex < getCaps().maxCombinedTextureImageUnits;
         ++textureIndex)
    {
        mCompleteTextureBindings.emplace_back(context, textureIndex);
    }

    mSamplers.resize(getCaps().maxCombinedTextureImageUnits);

    for (QueryType type : angle::AllEnums<QueryType>())
    {
        mActiveQueries[type].set(context, nullptr);
    }

    mProgram = nullptr;
    UninstallExecutable(context, &mExecutable);

    mReadFramebuffer = nullptr;
    mDrawFramebuffer = nullptr;

    getDebug().setMaxLoggedMessages(getCaps().maxDebugLoggedMessages);
}

void State::reset(const Context *context)
{
    // Force a sync so clear doesn't end up dereferencing stale pointers.
    (void)syncActiveTextures(context, Command::Other);
    mActiveTexturesCache.clear();

    for (TextureBindingVector &bindingVec : mSamplerTextures)
    {
        for (BindingPointer<Texture> &texBinding : bindingVec)
        {
            texBinding.set(context, nullptr);
        }
    }
    for (BindingPointer<Sampler> &sampler : mSamplers)
    {
        sampler.set(context, nullptr);
    }

    for (ImageUnit &imageUnit : mImageUnits)
    {
        imageUnit.texture.set(context, nullptr);
        imageUnit.level   = 0;
        imageUnit.layered = false;
        imageUnit.layer   = 0;
        imageUnit.access  = GL_READ_ONLY;
        imageUnit.format  = GL_R32UI;
    }

    mRenderbuffer.set(context, nullptr);

    for (BufferBinding type : angle::AllEnums<BufferBinding>())
    {
        UpdateBufferBinding(context, &mBoundBuffers[type], nullptr, type);
    }

    UninstallExecutable(context, &mExecutable);
    if (mProgram)
    {
        mProgram->release(context);
    }
    mProgram = nullptr;
    mProgramPipeline.set(context, nullptr);

    if (mTransformFeedback.get())
    {
        mTransformFeedback->onBindingChanged(context, false);
    }
    mTransformFeedback.set(context, nullptr);

    for (QueryType type : angle::AllEnums<QueryType>())
    {
        mActiveQueries[type].set(context, nullptr);
    }

    for (OffsetBindingPointer<Buffer> &buf : mUniformBuffers)
    {
        UpdateIndexedBufferBinding(context, &buf, nullptr, BufferBinding::Uniform, 0, 0);
    }
    mBoundUniformBuffersMask.reset();

    for (OffsetBindingPointer<Buffer> &buf : mAtomicCounterBuffers)
    {
        UpdateIndexedBufferBinding(context, &buf, nullptr, BufferBinding::AtomicCounter, 0, 0);
    }
    mBoundAtomicCounterBuffersMask.reset();

    for (OffsetBindingPointer<Buffer> &buf : mShaderStorageBuffers)
    {
        UpdateIndexedBufferBinding(context, &buf, nullptr, BufferBinding::ShaderStorage, 0, 0);
    }
    mBoundShaderStorageBuffersMask.reset();

    mPrivateState.reset();

    setAllDirtyBits();
}

ANGLE_INLINE void State::unsetActiveTextures(const ActiveTextureMask &textureMask)
{
    // Unset any relevant bound textures.
    for (size_t textureIndex : textureMask)
    {
        mActiveTexturesCache.reset(textureIndex);
        mCompleteTextureBindings[textureIndex].reset();
    }
}

ANGLE_INLINE void State::updateActiveTextureStateOnSync(const Context *context,
                                                        size_t textureIndex,
                                                        const Sampler *sampler,
                                                        Texture *texture)
{
    if (!texture || !texture->isSamplerComplete(context, sampler))
    {
        mActiveTexturesCache.reset(textureIndex);
    }
    else
    {
        mActiveTexturesCache.set(textureIndex, texture);
    }

    mDirtyBits.set(state::DIRTY_BIT_TEXTURE_BINDINGS);
}

ANGLE_INLINE void State::setActiveTextureDirty(size_t textureIndex, Texture *texture)
{
    mDirtyObjects.set(state::DIRTY_OBJECT_ACTIVE_TEXTURES);
    mDirtyActiveTextures.set(textureIndex);

    if (!texture)
    {
        return;
    }

    const bool needsRobustInit =
        isRobustResourceInitEnabled() && texture->initState() == InitState::MayNeedInit;
    if (texture->hasAnyDirtyBit() || needsRobustInit)
    {
        setTextureDirty(textureIndex);
    }

    if (needsRobustInit)
    {
        mDirtyObjects.set(state::DIRTY_OBJECT_TEXTURES_INIT);
    }

    // This cache is updated immediately because we use the cache in the validation layer.
    // If we defer the update until syncState it's too late and we've already passed validation.
    if (texture && mExecutable)
    {
        // It is invalid to try to sample a non-yuv texture with a yuv sampler.
        mTexturesIncompatibleWithSamplers[textureIndex] =
            mExecutable->getActiveYUVSamplers().test(textureIndex) && !texture->isYUV();

        if (isWebGL())
        {
            const Sampler *sampler = mSamplers[textureIndex].get();
            const SamplerState &samplerState =
                sampler ? sampler->getSamplerState() : texture->getSamplerState();
            if (!texture->getTextureState().compatibleWithSamplerFormatForWebGL(
                    mExecutable->getSamplerFormatForTextureUnitIndex(textureIndex), samplerState))
            {
                mTexturesIncompatibleWithSamplers[textureIndex] = true;
            }
        }
    }
    else
    {
        mTexturesIncompatibleWithSamplers[textureIndex] = false;
    }
}

ANGLE_INLINE void State::updateTextureBinding(const Context *context,
                                              size_t textureIndex,
                                              Texture *texture)
{
    mCompleteTextureBindings[textureIndex].bind(texture);
    mActiveTexturesCache.reset(textureIndex);
    setActiveTextureDirty(textureIndex, texture);
}

bool State::allActiveDrawBufferChannelsMasked() const
{
    // Compare current color mask with all-disabled color mask, while ignoring disabled draw
    // buffers.
    return (getBlendStateExt().compareColorMask(0) & mDrawFramebuffer->getDrawBufferMask()).none();
}

bool State::anyActiveDrawBufferChannelMasked() const
{
    // Compare current color mask with all-enabled color mask, while ignoring disabled draw
    // buffers.
    return (getBlendStateExt().compareColorMask(getBlendStateExt().getAllColorMaskBits()) &
            mDrawFramebuffer->getDrawBufferMask())
        .any();
}

void State::setSamplerTexture(const Context *context, TextureType type, Texture *texture)
{
    if (mExecutable && mExecutable->getActiveSamplersMask()[getActiveSampler()] &&
        IsTextureCompatibleWithSampler(type,
                                       mExecutable->getActiveSamplerTypes()[getActiveSampler()]))
    {
        updateTextureBinding(context, getActiveSampler(), texture);
    }

    mSamplerTextures[type][getActiveSampler()].set(context, texture);

    mDirtyBits.set(state::DIRTY_BIT_TEXTURE_BINDINGS);
}

TextureID State::getSamplerTextureId(unsigned int sampler, TextureType type) const
{
    ASSERT(sampler < mSamplerTextures[type].size());
    return mSamplerTextures[type][sampler].id();
}

void State::detachTexture(Context *context, const TextureMap &zeroTextures, TextureID texture)
{
    // Textures have a detach method on State rather than a simple
    // removeBinding, because the zero/null texture objects are managed
    // separately, and don't have to go through the Context's maps or
    // the ResourceManager.

    // [OpenGL ES 2.0.24] section 3.8 page 84:
    // If a texture object is deleted, it is as if all texture units which are bound to that texture
    // object are rebound to texture object zero

    for (TextureType type : angle::AllEnums<TextureType>())
    {
        TextureBindingVector &textureVector = mSamplerTextures[type];

        for (size_t bindingIndex = 0; bindingIndex < textureVector.size(); ++bindingIndex)
        {
            BindingPointer<Texture> &binding = textureVector[bindingIndex];
            if (binding.id() == texture)
            {
                // Zero textures are the "default" textures instead of NULL
                Texture *zeroTexture = zeroTextures[type].get();
                ASSERT(zeroTexture != nullptr);
                if (mCompleteTextureBindings[bindingIndex].getSubject() == binding.get())
                {
                    updateTextureBinding(context, bindingIndex, zeroTexture);
                }
                binding.set(context, zeroTexture);
            }
        }
    }

    for (auto &bindingImageUnit : mImageUnits)
    {
        if (bindingImageUnit.texture.id() == texture)
        {
            bindingImageUnit.texture.set(context, nullptr);
            bindingImageUnit.level   = 0;
            bindingImageUnit.layered = false;
            bindingImageUnit.layer   = 0;
            bindingImageUnit.access  = GL_READ_ONLY;
            bindingImageUnit.format  = GL_R32UI;
            mDirtyBits.set(state::DIRTY_BIT_IMAGE_BINDINGS);
        }
    }

    // [OpenGL ES 2.0.24] section 4.4 page 112:
    // If a texture object is deleted while its image is attached to the currently bound
    // framebuffer, then it is as if Texture2DAttachment had been called, with a texture of 0, for
    // each attachment point to which this image was attached in the currently bound framebuffer.

    if (mReadFramebuffer && mReadFramebuffer->detachTexture(context, texture))
    {
        mDirtyObjects.set(state::DIRTY_OBJECT_READ_FRAMEBUFFER);
    }

    if (mDrawFramebuffer && mDrawFramebuffer->detachTexture(context, texture))
    {
        setDrawFramebufferDirty();
    }
}

void State::initializeZeroTextures(const Context *context, const TextureMap &zeroTextures)
{
    for (TextureType type : angle::AllEnums<TextureType>())
    {
        for (size_t textureUnit = 0; textureUnit < mSamplerTextures[type].size(); ++textureUnit)
        {
            mSamplerTextures[type][textureUnit].set(context, zeroTextures[type].get());
        }
    }
}

void State::invalidateTextureBindings(TextureType type)
{
    mDirtyBits.set(state::DIRTY_BIT_TEXTURE_BINDINGS);
}

bool State::isTextureBoundToActivePLS(TextureID textureID) const
{
    if (getPixelLocalStorageActivePlanes() == 0)
    {
        return false;
    }
    PixelLocalStorage *pls = getDrawFramebuffer()->peekPixelLocalStorage();
    if (pls == nullptr)
    {
        // Even though there is a nonzero number of active PLS planes, peekPixelLocalStorage() may
        // still return null if we are in the middle of deleting the active framebuffer.
        return false;
    }
    for (GLuint i = 0; i < getCaps().maxPixelLocalStoragePlanes; ++i)
    {
        if (pls->getPlane(i).getTextureID() == textureID)
        {
            return true;
        }
    }
    return false;
}

void State::setSamplerBinding(const Context *context, GLuint textureUnit, Sampler *sampler)
{
    if (mSamplers[textureUnit].get() == sampler)
    {
        return;
    }

    mSamplers[textureUnit].set(context, sampler);
    mDirtyBits.set(state::DIRTY_BIT_SAMPLER_BINDINGS);
    // This is overly conservative as it assumes the sampler has never been bound.
    setSamplerDirty(textureUnit);
    onActiveTextureChange(context, textureUnit);
}

void State::detachSampler(const Context *context, SamplerID sampler)
{
    // [OpenGL ES 3.0.2] section 3.8.2 pages 123-124:
    // If a sampler object that is currently bound to one or more texture units is
    // deleted, it is as though BindSampler is called once for each texture unit to
    // which the sampler is bound, with unit set to the texture unit and sampler set to zero.
    for (size_t i = 0; i < mSamplers.size(); i++)
    {
        if (mSamplers[i].id() == sampler)
        {
            setSamplerBinding(context, static_cast<GLuint>(i), nullptr);
        }
    }
}

void State::setRenderbufferBinding(const Context *context, Renderbuffer *renderbuffer)
{
    mRenderbuffer.set(context, renderbuffer);
    mDirtyBits.set(state::DIRTY_BIT_RENDERBUFFER_BINDING);
}

void State::detachRenderbuffer(Context *context, RenderbufferID renderbuffer)
{
    // [OpenGL ES 2.0.24] section 4.4 page 109:
    // If a renderbuffer that is currently bound to RENDERBUFFER is deleted, it is as though
    // BindRenderbuffer had been executed with the target RENDERBUFFER and name of zero.

    if (mRenderbuffer.id() == renderbuffer)
    {
        setRenderbufferBinding(context, nullptr);
    }

    // [OpenGL ES 2.0.24] section 4.4 page 111:
    // If a renderbuffer object is deleted while its image is attached to the currently bound
    // framebuffer, then it is as if FramebufferRenderbuffer had been called, with a renderbuffer of
    // 0, for each attachment point to which this image was attached in the currently bound
    // framebuffer.

    Framebuffer *readFramebuffer = mReadFramebuffer;
    Framebuffer *drawFramebuffer = mDrawFramebuffer;

    if (readFramebuffer && readFramebuffer->detachRenderbuffer(context, renderbuffer))
    {
        mDirtyObjects.set(state::DIRTY_OBJECT_READ_FRAMEBUFFER);
    }

    if (drawFramebuffer && drawFramebuffer != readFramebuffer)
    {
        if (drawFramebuffer->detachRenderbuffer(context, renderbuffer))
        {
            setDrawFramebufferDirty();
        }
    }
}

void State::setReadFramebufferBinding(Framebuffer *framebuffer)
{
    if (mReadFramebuffer == framebuffer)
        return;

    mReadFramebuffer = framebuffer;
    mDirtyBits.set(state::DIRTY_BIT_READ_FRAMEBUFFER_BINDING);

    if (mReadFramebuffer)
    {
        if (mReadFramebuffer->hasAnyDirtyBit())
        {
            mDirtyObjects.set(state::DIRTY_OBJECT_READ_FRAMEBUFFER);
        }

        if (isRobustResourceInitEnabled() && mReadFramebuffer->hasResourceThatNeedsInit())
        {
            mDirtyObjects.set(state::DIRTY_OBJECT_READ_ATTACHMENTS);
            mDirtyObjects.set(state::DIRTY_OBJECT_READ_FRAMEBUFFER);
        }
    }
}

void State::setDrawFramebufferBinding(Framebuffer *framebuffer)
{
    if (mDrawFramebuffer == framebuffer)
        return;

    mDrawFramebuffer = framebuffer;
    mDirtyBits.set(state::DIRTY_BIT_DRAW_FRAMEBUFFER_BINDING);

    if (mDrawFramebuffer)
    {
        mDrawFramebuffer->setWriteControlMode(getFramebufferSRGB() ? SrgbWriteControlMode::Default
                                                                   : SrgbWriteControlMode::Linear);

        if (mDrawFramebuffer->hasAnyDirtyBit())
        {
            mDirtyObjects.set(state::DIRTY_OBJECT_DRAW_FRAMEBUFFER);
        }

        if (isRobustResourceInitEnabled() && mDrawFramebuffer->hasResourceThatNeedsInit())
        {
            mDirtyObjects.set(state::DIRTY_OBJECT_DRAW_ATTACHMENTS);
            mDirtyObjects.set(state::DIRTY_OBJECT_DRAW_FRAMEBUFFER);
        }
    }
}

Framebuffer *State::getTargetFramebuffer(GLenum target) const
{
    switch (target)
    {
        case GL_READ_FRAMEBUFFER_ANGLE:
            return mReadFramebuffer;
        case GL_DRAW_FRAMEBUFFER_ANGLE:
        case GL_FRAMEBUFFER:
            return mDrawFramebuffer;
        default:
            UNREACHABLE();
            return nullptr;
    }
}

Framebuffer *State::getDefaultFramebuffer() const
{
    return mFramebufferManager->getDefaultFramebuffer();
}

bool State::removeReadFramebufferBinding(FramebufferID framebuffer)
{
    if (mReadFramebuffer != nullptr && mReadFramebuffer->id() == framebuffer)
    {
        setReadFramebufferBinding(nullptr);
        return true;
    }

    return false;
}

bool State::removeDrawFramebufferBinding(FramebufferID framebuffer)
{
    if (mDrawFramebuffer != nullptr && mDrawFramebuffer->id() == framebuffer)
    {
        setDrawFramebufferBinding(nullptr);
        return true;
    }

    return false;
}

void State::setVertexArrayBinding(const Context *context, VertexArray *vertexArray)
{
    // We have to call onBindingChanged even if we are rebinding the same vertex array, because
    // underlying buffer may have changed.
    if (mVertexArray == vertexArray)
    {
        mVertexArray->onRebind(context);
        return;
    }

    if (mVertexArray)
    {
        mVertexArray->onBindingChanged(context, -1);
    }
    if (vertexArray)
    {
        vertexArray->onBindingChanged(context, 1);
    }

    mVertexArray = vertexArray;
    mPrivateState.setVertexArrayPrivate(vertexArray);

    mDirtyBits.set(state::DIRTY_BIT_VERTEX_ARRAY_BINDING);

    if (mVertexArray && mVertexArray->hasAnyDirtyBit())
    {
        mDirtyObjects.set(state::DIRTY_OBJECT_VERTEX_ARRAY);
    }
}

bool State::removeVertexArrayBinding(const Context *context, VertexArrayID vertexArray)
{
    if (mVertexArray && mVertexArray->id().value == vertexArray.value)
    {
        mVertexArray->onBindingChanged(context, -1);
        mVertexArray = nullptr;
        mPrivateState.setVertexArrayPrivate(nullptr);
        mDirtyBits.set(state::DIRTY_BIT_VERTEX_ARRAY_BINDING);
        mDirtyObjects.set(state::DIRTY_OBJECT_VERTEX_ARRAY);
        return true;
    }

    return false;
}

VertexArrayID State::getVertexArrayId() const
{
    ASSERT(mVertexArray != nullptr);
    return mVertexArray->id();
}

void State::bindVertexBuffer(const Context *context,
                             GLuint bindingIndex,
                             Buffer *boundBuffer,
                             GLintptr offset,
                             GLsizei stride)
{
    getVertexArray()->bindVertexBuffer(context, bindingIndex, boundBuffer, offset, stride);
    mDirtyObjects.set(state::DIRTY_OBJECT_VERTEX_ARRAY);
}

angle::Result State::setProgram(const Context *context, Program *newProgram)
{
    if (newProgram && !newProgram->isLinked())
    {
        // Protect against applications that disable validation and try to use a program that was
        // not successfully linked.
        WARN() << "Attempted to use a program that was not successfully linked";
        return angle::Result::Continue;
    }

    if (mProgram != newProgram)
    {
        if (mProgram)
        {
            unsetActiveTextures(mExecutable->getActiveSamplersMask());
            mProgram->release(context);
        }

        mProgram = newProgram;

        if (mProgram)
        {
            newProgram->addRef();
            ANGLE_TRY(installProgramExecutable(context));
        }
        else if (mProgramPipeline.get() == nullptr)
        {
            UninstallExecutable(context, &mExecutable);
        }
        else if (mProgramPipeline->isLinked())
        {
            ANGLE_TRY(installProgramPipelineExecutableIfNotAlready(context));
        }

        // Note that rendering is undefined if glUseProgram(0) is called. But ANGLE will generate
        // an error if the app tries to draw in this case.

        mDirtyBits.set(state::DIRTY_BIT_PROGRAM_BINDING);
    }

    return angle::Result::Continue;
}

void State::setTransformFeedbackBinding(const Context *context,
                                        TransformFeedback *transformFeedback)
{
    if (transformFeedback == mTransformFeedback.get())
        return;
    if (mTransformFeedback.get())
        mTransformFeedback->onBindingChanged(context, false);
    mTransformFeedback.set(context, transformFeedback);
    if (mTransformFeedback.get())
        mTransformFeedback->onBindingChanged(context, true);
    mDirtyBits.set(state::DIRTY_BIT_TRANSFORM_FEEDBACK_BINDING);
}

bool State::removeTransformFeedbackBinding(const Context *context,
                                           TransformFeedbackID transformFeedback)
{
    if (mTransformFeedback.id() == transformFeedback)
    {
        if (mTransformFeedback.get())
            mTransformFeedback->onBindingChanged(context, false);
        mTransformFeedback.set(context, nullptr);
        return true;
    }

    return false;
}

angle::Result State::setProgramPipelineBinding(const Context *context, ProgramPipeline *pipeline)
{
    if (mProgramPipeline.get() == pipeline)
    {
        return angle::Result::Continue;
    }

    if (mProgramPipeline.get())
    {
        unsetActiveTextures(mProgramPipeline->getExecutable().getActiveSamplersMask());
    }

    mProgramPipeline.set(context, pipeline);
    mDirtyBits.set(state::DIRTY_BIT_PROGRAM_BINDING);

    // A bound Program always overrides the ProgramPipeline, so only update the
    // current ProgramExecutable if there isn't currently a Program bound.
    if (!mProgram)
    {
        if (mProgramPipeline.get() && mProgramPipeline->isLinked())
        {
            ANGLE_TRY(installProgramPipelineExecutableIfNotAlready(context));
        }
    }

    return angle::Result::Continue;
}

void State::detachProgramPipeline(const Context *context, ProgramPipelineID pipeline)
{
    mProgramPipeline.set(context, nullptr);

    // A bound Program always overrides the ProgramPipeline, so only update the
    // current ProgramExecutable if there isn't currently a Program bound.
    if (!mProgram)
    {
        UninstallExecutable(context, &mExecutable);
    }
}

bool State::isQueryActive(QueryType type) const
{
    const Query *query = mActiveQueries[type].get();
    if (query != nullptr)
    {
        return true;
    }

    QueryType alternativeType;
    if (GetAlternativeQueryType(type, &alternativeType))
    {
        query = mActiveQueries[alternativeType].get();
        return query != nullptr;
    }

    return false;
}

bool State::isQueryActive(Query *query) const
{
    for (auto &queryPointer : mActiveQueries)
    {
        if (queryPointer.get() == query)
        {
            return true;
        }
    }

    return false;
}

void State::setActiveQuery(const Context *context, QueryType type, Query *query)
{
    mActiveQueries[type].set(context, query);
}

QueryID State::getActiveQueryId(QueryType type) const
{
    const Query *query = getActiveQuery(type);
    if (query)
    {
        return query->id();
    }
    return {0};
}

Query *State::getActiveQuery(QueryType type) const
{
    return mActiveQueries[type].get();
}

angle::Result State::setIndexedBufferBinding(const Context *context,
                                             BufferBinding target,
                                             GLuint index,
                                             Buffer *buffer,
                                             GLintptr offset,
                                             GLsizeiptr size)
{
    if (mBoundBuffers[target].get() != buffer)
    {
        setBufferBinding(context, target, buffer);
    }

    switch (target)
    {
        case BufferBinding::TransformFeedback:
            ANGLE_TRY(mTransformFeedback->bindIndexedBuffer(context, index, buffer, offset, size));
            setBufferBinding(context, target, buffer);
            break;
        case BufferBinding::Uniform:
        {
            mBoundUniformBuffersMask.set(index, buffer != nullptr);
            BufferDirtyTypeBitMask dirtyTypeMask = {};
            if (mUniformBuffers[index].get() != buffer)
            {
                dirtyTypeMask.set();
            }
            else
            {
                dirtyTypeMask.set(BufferDirtyType::Offset,
                                  buffer && mUniformBuffers[index].getOffset() != offset);
                dirtyTypeMask.set(BufferDirtyType::Size,
                                  buffer && mUniformBuffers[index].getSize() != size);
            }
            mUniformBufferBlocksDirtyTypeMask |= dirtyTypeMask;
            if (UpdateIndexedBufferBinding(context, &mUniformBuffers[index], buffer, target, offset,
                                           size))
            {
                onUniformBufferStateChange(index, angle::SubjectMessage::SubjectChanged);
            }
        }
        break;
        case BufferBinding::AtomicCounter:
            mBoundAtomicCounterBuffersMask.set(index, buffer != nullptr);
            if (UpdateIndexedBufferBinding(context, &mAtomicCounterBuffers[index], buffer, target,
                                           offset, size))
            {
                onAtomicCounterBufferStateChange(index);
            }
            break;
        case BufferBinding::ShaderStorage:
            mBoundShaderStorageBuffersMask.set(index, buffer != nullptr);
            if (UpdateIndexedBufferBinding(context, &mShaderStorageBuffers[index], buffer, target,
                                           offset, size))
            {
                onShaderStorageBufferStateChange(index);
            }
            break;
        default:
            UNREACHABLE();
            break;
    }

    return angle::Result::Continue;
}

const OffsetBindingPointer<Buffer> &State::getIndexedUniformBuffer(size_t index) const
{
    ASSERT(index < mUniformBuffers.size());
    return mUniformBuffers[index];
}

const OffsetBindingPointer<Buffer> &State::getIndexedAtomicCounterBuffer(size_t index) const
{
    ASSERT(index < mAtomicCounterBuffers.size());
    return mAtomicCounterBuffers[index];
}

const OffsetBindingPointer<Buffer> &State::getIndexedShaderStorageBuffer(size_t index) const
{
    ASSERT(index < mShaderStorageBuffers.size());
    return mShaderStorageBuffers[index];
}

angle::Result State::detachBuffer(Context *context, const Buffer *buffer)
{
    BufferID bufferID = buffer->id();
    for (gl::BufferBinding target : angle::AllEnums<BufferBinding>())
    {
        if (mBoundBuffers[target].id() == bufferID)
        {
            UpdateBufferBinding(context, &mBoundBuffers[target], nullptr, target);
        }
    }

    TransformFeedback *curTransformFeedback = getCurrentTransformFeedback();
    if (curTransformFeedback)
    {
        ANGLE_TRY(curTransformFeedback->detachBuffer(context, bufferID));
        context->onActiveTransformFeedbackChange();
    }

    if (mVertexArray && mVertexArray->detachBuffer(context, bufferID))
    {
        mDirtyObjects.set(state::DIRTY_OBJECT_VERTEX_ARRAY);
        context->getMutablePrivateStateCache()->onVertexArrayStateChange();
    }

    for (size_t uniformBufferIndex : mBoundUniformBuffersMask)
    {
        OffsetBindingPointer<Buffer> &binding = mUniformBuffers[uniformBufferIndex];

        if (binding.id() == bufferID)
        {
            UpdateIndexedBufferBinding(context, &binding, nullptr, BufferBinding::Uniform, 0, 0);
            mBoundUniformBuffersMask.reset(uniformBufferIndex);
        }
    }

    for (size_t atomicCounterBufferIndex : mBoundAtomicCounterBuffersMask)
    {
        OffsetBindingPointer<Buffer> &binding = mAtomicCounterBuffers[atomicCounterBufferIndex];

        if (binding.id() == bufferID)
        {
            UpdateIndexedBufferBinding(context, &binding, nullptr, BufferBinding::AtomicCounter, 0,
                                       0);
            mBoundAtomicCounterBuffersMask.reset(atomicCounterBufferIndex);
        }
    }

    for (size_t shaderStorageBufferIndex : mBoundShaderStorageBuffersMask)
    {
        OffsetBindingPointer<Buffer> &binding = mShaderStorageBuffers[shaderStorageBufferIndex];

        if (binding.id() == bufferID)
        {
            UpdateIndexedBufferBinding(context, &binding, nullptr, BufferBinding::ShaderStorage, 0,
                                       0);
            mBoundShaderStorageBuffersMask.reset(shaderStorageBufferIndex);
        }
    }

    return angle::Result::Continue;
}

const void *State::getVertexAttribPointer(unsigned int attribNum) const
{
    return getVertexArray()->getVertexAttribute(attribNum).pointer;
}

void State::getBooleanv(GLenum pname, GLboolean *params) const
{
    switch (pname)
    {
        case GL_TRANSFORM_FEEDBACK_ACTIVE:
            *params = getCurrentTransformFeedback()->isActive() ? GL_TRUE : GL_FALSE;
            break;
        case GL_TRANSFORM_FEEDBACK_PAUSED:
            *params = getCurrentTransformFeedback()->isPaused() ? GL_TRUE : GL_FALSE;
            break;
        default:
            mPrivateState.getBooleanv(pname, params);
    }
}

angle::Result State::getIntegerv(const Context *context, GLenum pname, GLint *params) const
{
    if (pname >= GL_DRAW_BUFFER0_EXT && pname <= GL_DRAW_BUFFER15_EXT)
    {
        size_t drawBuffer = (pname - GL_DRAW_BUFFER0_EXT);
        ASSERT(drawBuffer < static_cast<size_t>(getCaps().maxDrawBuffers));
        Framebuffer *framebuffer = mDrawFramebuffer;
        // The default framebuffer may have fewer draw buffer states than a user-created one. The
        // user is always allowed to query up to GL_MAX_DRAWBUFFERS so just return GL_NONE here if
        // the draw buffer is out of range for this framebuffer.
        *params = drawBuffer < framebuffer->getDrawbufferStateCount()
                      ? framebuffer->getDrawBufferState(drawBuffer)
                      : GL_NONE;
        return angle::Result::Continue;
    }

    switch (pname)
    {
        case GL_ARRAY_BUFFER_BINDING:
            *params = mBoundBuffers[BufferBinding::Array].id().value;
            break;
        case GL_DRAW_INDIRECT_BUFFER_BINDING:
            *params = mBoundBuffers[BufferBinding::DrawIndirect].id().value;
            break;
        case GL_ELEMENT_ARRAY_BUFFER_BINDING:
        {
            Buffer *elementArrayBuffer = getVertexArray()->getElementArrayBuffer();
            *params                    = elementArrayBuffer ? elementArrayBuffer->id().value : 0;
            break;
        }
        case GL_DRAW_FRAMEBUFFER_BINDING:
            static_assert(GL_DRAW_FRAMEBUFFER_BINDING == GL_DRAW_FRAMEBUFFER_BINDING_ANGLE,
                          "Enum mismatch");
            *params = mDrawFramebuffer->id().value;
            break;
        case GL_READ_FRAMEBUFFER_BINDING:
            static_assert(GL_READ_FRAMEBUFFER_BINDING == GL_READ_FRAMEBUFFER_BINDING_ANGLE,
                          "Enum mismatch");
            *params = mReadFramebuffer->id().value;
            break;
        case GL_RENDERBUFFER_BINDING:
            *params = mRenderbuffer.id().value;
            break;
        case GL_VERTEX_ARRAY_BINDING:
            *params = mVertexArray->id().value;
            break;
        case GL_CURRENT_PROGRAM:
            *params = mProgram ? mProgram->id().value : 0;
            break;
        case GL_IMPLEMENTATION_COLOR_READ_TYPE:
            *params = mReadFramebuffer->getImplementationColorReadType(context);
            break;
        case GL_IMPLEMENTATION_COLOR_READ_FORMAT:
            *params = mReadFramebuffer->getImplementationColorReadFormat(context);
            break;
        case GL_SAMPLE_BUFFERS:
        case GL_SAMPLES:
        {
            Framebuffer *framebuffer = mDrawFramebuffer;
            if (framebuffer->isComplete(context))
            {
                GLint samples = framebuffer->getSamples(context);
                switch (pname)
                {
                    case GL_SAMPLE_BUFFERS:
                        if (samples != 0)
                        {
                            *params = 1;
                        }
                        else
                        {
                            *params = 0;
                        }
                        break;
                    case GL_SAMPLES:
                        *params = samples;
                        break;
                }
            }
            else
            {
                *params = 0;
            }
        }
        break;
        case GL_RED_BITS:
        case GL_GREEN_BITS:
        case GL_BLUE_BITS:
        case GL_ALPHA_BITS:
        {
            Framebuffer *framebuffer                 = getDrawFramebuffer();
            const FramebufferAttachment *colorbuffer = framebuffer->getFirstColorAttachment();

            if (colorbuffer)
            {
                switch (pname)
                {
                    case GL_RED_BITS:
                        *params = colorbuffer->getRedSize();
                        break;
                    case GL_GREEN_BITS:
                        *params = colorbuffer->getGreenSize();
                        break;
                    case GL_BLUE_BITS:
                        *params = colorbuffer->getBlueSize();
                        break;
                    case GL_ALPHA_BITS:
                        *params = colorbuffer->getAlphaSize();
                        break;
                }
            }
            else
            {
                *params = 0;
            }
        }
        break;
        case GL_DEPTH_BITS:
        {
            const Framebuffer *framebuffer           = getDrawFramebuffer();
            const FramebufferAttachment *depthbuffer = framebuffer->getDepthAttachment();

            if (depthbuffer)
            {
                *params = depthbuffer->getDepthSize();
            }
            else
            {
                *params = 0;
            }
        }
        break;
        case GL_STENCIL_BITS:
        {
            const Framebuffer *framebuffer             = getDrawFramebuffer();
            const FramebufferAttachment *stencilbuffer = framebuffer->getStencilAttachment();

            if (stencilbuffer)
            {
                *params = stencilbuffer->getStencilSize();
            }
            else
            {
                *params = 0;
            }
        }
        break;
        case GL_TEXTURE_BINDING_2D:
            ASSERT(static_cast<GLint>(getActiveSampler()) < getCaps().maxCombinedTextureImageUnits);
            *params = getSamplerTextureId(getActiveSampler(), TextureType::_2D).value;
            break;
        case GL_TEXTURE_BINDING_RECTANGLE_ANGLE:
            ASSERT(static_cast<GLint>(getActiveSampler()) < getCaps().maxCombinedTextureImageUnits);
            *params = getSamplerTextureId(getActiveSampler(), TextureType::Rectangle).value;
            break;
        case GL_TEXTURE_BINDING_CUBE_MAP:
            ASSERT(static_cast<GLint>(getActiveSampler()) < getCaps().maxCombinedTextureImageUnits);
            *params = getSamplerTextureId(getActiveSampler(), TextureType::CubeMap).value;
            break;
        case GL_TEXTURE_BINDING_3D:
            ASSERT(static_cast<GLint>(getActiveSampler()) < getCaps().maxCombinedTextureImageUnits);
            *params = getSamplerTextureId(getActiveSampler(), TextureType::_3D).value;
            break;
        case GL_TEXTURE_BINDING_2D_ARRAY:
            ASSERT(static_cast<GLint>(getActiveSampler()) < getCaps().maxCombinedTextureImageUnits);
            *params = getSamplerTextureId(getActiveSampler(), TextureType::_2DArray).value;
            break;
        case GL_TEXTURE_BINDING_2D_MULTISAMPLE:
            ASSERT(static_cast<GLint>(getActiveSampler()) < getCaps().maxCombinedTextureImageUnits);
            *params = getSamplerTextureId(getActiveSampler(), TextureType::_2DMultisample).value;
            break;
        case GL_TEXTURE_BINDING_2D_MULTISAMPLE_ARRAY:
            ASSERT(static_cast<GLint>(getActiveSampler()) < getCaps().maxCombinedTextureImageUnits);
            *params =
                getSamplerTextureId(getActiveSampler(), TextureType::_2DMultisampleArray).value;
            break;
        case GL_TEXTURE_BINDING_CUBE_MAP_ARRAY:
            ASSERT(static_cast<GLint>(getActiveSampler()) < getCaps().maxCombinedTextureImageUnits);
            *params = getSamplerTextureId(getActiveSampler(), TextureType::CubeMapArray).value;
            break;
        case GL_TEXTURE_BINDING_EXTERNAL_OES:
            ASSERT(static_cast<GLint>(getActiveSampler()) < getCaps().maxCombinedTextureImageUnits);
            *params = getSamplerTextureId(getActiveSampler(), TextureType::External).value;
            break;

        // GL_OES_texture_buffer
        case GL_TEXTURE_BINDING_BUFFER:
            ASSERT(static_cast<GLint>(getActiveSampler()) < getCaps().maxCombinedTextureImageUnits);
            *params = getSamplerTextureId(getActiveSampler(), TextureType::Buffer).value;
            break;
        case GL_TEXTURE_BUFFER_BINDING:
            *params = mBoundBuffers[BufferBinding::Texture].id().value;
            break;

        case GL_UNIFORM_BUFFER_BINDING:
            *params = mBoundBuffers[BufferBinding::Uniform].id().value;
            break;
        case GL_TRANSFORM_FEEDBACK_BINDING:
            *params = mTransformFeedback.id().value;
            break;
        case GL_TRANSFORM_FEEDBACK_BUFFER_BINDING:
            *params = mBoundBuffers[BufferBinding::TransformFeedback].id().value;
            break;
        case GL_COPY_READ_BUFFER_BINDING:
            *params = mBoundBuffers[BufferBinding::CopyRead].id().value;
            break;
        case GL_COPY_WRITE_BUFFER_BINDING:
            *params = mBoundBuffers[BufferBinding::CopyWrite].id().value;
            break;
        case GL_PIXEL_PACK_BUFFER_BINDING:
            *params = mBoundBuffers[BufferBinding::PixelPack].id().value;
            break;
        case GL_PIXEL_UNPACK_BUFFER_BINDING:
            *params = mBoundBuffers[BufferBinding::PixelUnpack].id().value;
            break;

        case GL_READ_BUFFER:
            *params = mReadFramebuffer->getReadBufferState();
            break;
        case GL_SAMPLER_BINDING:
            ASSERT(static_cast<GLint>(getActiveSampler()) < getCaps().maxCombinedTextureImageUnits);
            *params = getSamplerId(getActiveSampler()).value;
            break;
        case GL_DEBUG_LOGGED_MESSAGES:
            *params = static_cast<GLint>(getDebug().getMessageCount());
            break;
        case GL_DEBUG_NEXT_LOGGED_MESSAGE_LENGTH:
            *params = static_cast<GLint>(getDebug().getNextMessageLength());
            break;
        case GL_DEBUG_GROUP_STACK_DEPTH:
            *params = static_cast<GLint>(getDebug().getGroupStackDepth());
            break;
        case GL_ATOMIC_COUNTER_BUFFER_BINDING:
            *params = mBoundBuffers[BufferBinding::AtomicCounter].id().value;
            break;
        case GL_SHADER_STORAGE_BUFFER_BINDING:
            *params = mBoundBuffers[BufferBinding::ShaderStorage].id().value;
            break;
        case GL_DISPATCH_INDIRECT_BUFFER_BINDING:
            *params = mBoundBuffers[BufferBinding::DispatchIndirect].id().value;
            break;

        case GL_PROGRAM_PIPELINE_BINDING:
        {
            ProgramPipeline *pipeline = getProgramPipeline();
            if (pipeline)
            {
                *params = pipeline->id().value;
            }
            else
            {
                *params = 0;
            }
            break;
        }

        default:
            mPrivateState.getIntegerv(pname, params);
            break;
    }

    return angle::Result::Continue;
}

void State::getPointerv(const Context *context, GLenum pname, void **params) const
{
    switch (pname)
    {
        case GL_DEBUG_CALLBACK_FUNCTION:
            *params = reinterpret_cast<void *>(getDebug().getCallback());
            break;
        case GL_DEBUG_CALLBACK_USER_PARAM:
            *params = const_cast<void *>(getDebug().getUserParam());
            break;
        case GL_VERTEX_ARRAY_POINTER:
        case GL_NORMAL_ARRAY_POINTER:
        case GL_COLOR_ARRAY_POINTER:
        case GL_TEXTURE_COORD_ARRAY_POINTER:
        case GL_POINT_SIZE_ARRAY_POINTER_OES:
        {
            const int index = context->vertexArrayIndex(ParamToVertexArrayType(pname));
            *params = const_cast<void *>(getVertexArray()->getVertexAttribute(index).pointer);
            break;
        }
        case GL_BLOB_CACHE_GET_FUNCTION_ANGLE:
            *params = reinterpret_cast<void *>(getBlobCacheCallbacks().getFunction);
            break;
        case GL_BLOB_CACHE_SET_FUNCTION_ANGLE:
            *params = reinterpret_cast<void *>(getBlobCacheCallbacks().setFunction);
            break;
        case GL_BLOB_CACHE_USER_PARAM_ANGLE:
            *params = const_cast<void *>(getBlobCacheCallbacks().userParam);
            break;
        default:
            UNREACHABLE();
            break;
    }
}

void State::getIntegeri_v(GLenum target, GLuint index, GLint *data) const
{
    switch (target)
    {
        case GL_TRANSFORM_FEEDBACK_BUFFER_BINDING:
            ASSERT(static_cast<size_t>(index) < mTransformFeedback->getIndexedBufferCount());
            *data = mTransformFeedback->getIndexedBuffer(index).id().value;
            break;
        case GL_UNIFORM_BUFFER_BINDING:
            ASSERT(static_cast<size_t>(index) < mUniformBuffers.size());
            *data = mUniformBuffers[index].id().value;
            break;
        case GL_ATOMIC_COUNTER_BUFFER_BINDING:
            ASSERT(static_cast<size_t>(index) < mAtomicCounterBuffers.size());
            *data = mAtomicCounterBuffers[index].id().value;
            break;
        case GL_SHADER_STORAGE_BUFFER_BINDING:
            ASSERT(static_cast<size_t>(index) < mShaderStorageBuffers.size());
            *data = mShaderStorageBuffers[index].id().value;
            break;
        case GL_VERTEX_BINDING_BUFFER:
            ASSERT(static_cast<size_t>(index) < mVertexArray->getMaxBindings());
            *data = mVertexArray->getVertexArrayBufferID(index).value;
            break;
        case GL_VERTEX_BINDING_DIVISOR:
            ASSERT(static_cast<size_t>(index) < mVertexArray->getMaxBindings());
            *data = mVertexArray->getVertexBinding(index).getDivisor();
            break;
        case GL_VERTEX_BINDING_OFFSET:
            ASSERT(static_cast<size_t>(index) < mVertexArray->getMaxBindings());
            *data = static_cast<GLuint>(mVertexArray->getVertexBinding(index).getOffset());
            break;
        case GL_VERTEX_BINDING_STRIDE:
            ASSERT(static_cast<size_t>(index) < mVertexArray->getMaxBindings());
            *data = mVertexArray->getVertexBinding(index).getStride();
            break;
        case GL_IMAGE_BINDING_NAME:
            ASSERT(static_cast<size_t>(index) < mImageUnits.size());
            *data = mImageUnits[index].texture.id().value;
            break;
        case GL_IMAGE_BINDING_LEVEL:
            ASSERT(static_cast<size_t>(index) < mImageUnits.size());
            *data = mImageUnits[index].level;
            break;
        case GL_IMAGE_BINDING_LAYER:
            ASSERT(static_cast<size_t>(index) < mImageUnits.size());
            *data = mImageUnits[index].layer;
            break;
        case GL_IMAGE_BINDING_ACCESS:
            ASSERT(static_cast<size_t>(index) < mImageUnits.size());
            *data = mImageUnits[index].access;
            break;
        case GL_IMAGE_BINDING_FORMAT:
            ASSERT(static_cast<size_t>(index) < mImageUnits.size());
            *data = mImageUnits[index].format;
            break;
        default:
            mPrivateState.getIntegeri_v(target, index, data);
            break;
    }
}

void State::getInteger64i_v(GLenum target, GLuint index, GLint64 *data) const
{
    switch (target)
    {
        case GL_TRANSFORM_FEEDBACK_BUFFER_START:
            ASSERT(static_cast<size_t>(index) < mTransformFeedback->getIndexedBufferCount());
            *data = mTransformFeedback->getIndexedBuffer(index).getOffset();
            break;
        case GL_TRANSFORM_FEEDBACK_BUFFER_SIZE:
            ASSERT(static_cast<size_t>(index) < mTransformFeedback->getIndexedBufferCount());
            *data = mTransformFeedback->getIndexedBuffer(index).getSize();
            break;
        case GL_UNIFORM_BUFFER_START:
            ASSERT(static_cast<size_t>(index) < mUniformBuffers.size());
            *data = mUniformBuffers[index].getOffset();
            break;
        case GL_UNIFORM_BUFFER_SIZE:
            ASSERT(static_cast<size_t>(index) < mUniformBuffers.size());
            *data = mUniformBuffers[index].getSize();
            break;
        case GL_ATOMIC_COUNTER_BUFFER_START:
            ASSERT(static_cast<size_t>(index) < mAtomicCounterBuffers.size());
            *data = mAtomicCounterBuffers[index].getOffset();
            break;
        case GL_ATOMIC_COUNTER_BUFFER_SIZE:
            ASSERT(static_cast<size_t>(index) < mAtomicCounterBuffers.size());
            *data = mAtomicCounterBuffers[index].getSize();
            break;
        case GL_SHADER_STORAGE_BUFFER_START:
            ASSERT(static_cast<size_t>(index) < mShaderStorageBuffers.size());
            *data = mShaderStorageBuffers[index].getOffset();
            break;
        case GL_SHADER_STORAGE_BUFFER_SIZE:
            ASSERT(static_cast<size_t>(index) < mShaderStorageBuffers.size());
            *data = mShaderStorageBuffers[index].getSize();
            break;
        default:
            UNREACHABLE();
            break;
    }
}

void State::getBooleani_v(GLenum target, GLuint index, GLboolean *data) const
{
    switch (target)
    {
        case GL_IMAGE_BINDING_LAYERED:
            ASSERT(static_cast<size_t>(index) < mImageUnits.size());
            *data = mImageUnits[index].layered;
            break;
        default:
            mPrivateState.getBooleani_v(target, index, data);
            break;
    }
}

// TODO(http://anglebug.com/42262534): Remove this helper function after blink and chromium part
// refactor done.
Texture *State::getTextureForActiveSampler(TextureType type, size_t index)
{
    return mSamplerTextures[type][index].get();
}

angle::Result State::syncActiveTextures(const Context *context, Command command)
{
    if (mDirtyActiveTextures.none())
    {
        return angle::Result::Continue;
    }

    for (size_t textureUnit : mDirtyActiveTextures)
    {
        if (mExecutable)
        {
            TextureType type       = mExecutable->getActiveSamplerTypes()[textureUnit];
            Texture *activeTexture = (type != TextureType::InvalidEnum)
                                         ? getTextureForActiveSampler(type, textureUnit)
                                         : nullptr;
            const Sampler *sampler = mSamplers[textureUnit].get();

            updateActiveTextureStateOnSync(context, textureUnit, sampler, activeTexture);
        }
    }

    mDirtyActiveTextures.reset();
    return angle::Result::Continue;
}

angle::Result State::syncTexturesInit(const Context *context, Command command)
{
    ASSERT(isRobustResourceInitEnabled());

    if (!mProgram)
        return angle::Result::Continue;

    for (size_t textureUnitIndex : mExecutable->getActiveSamplersMask())
    {
        Texture *texture = mActiveTexturesCache[textureUnitIndex];
        if (texture)
        {
            ANGLE_TRY(
                texture->ensureInitialized(context, EnsureInitializedLevels::AllEnabledLevels));
        }
    }
    return angle::Result::Continue;
}

angle::Result State::syncImagesInit(const Context *context, Command command)
{
    ASSERT(isRobustResourceInitEnabled());
    ASSERT(mExecutable);
    for (size_t imageUnitIndex : mExecutable->getActiveImagesMask())
    {
        Texture *texture = mImageUnits[imageUnitIndex].texture.get();
        if (texture)
        {
            ANGLE_TRY(
                texture->ensureInitialized(context, EnsureInitializedLevels::AllEnabledLevels));
        }
    }
    return angle::Result::Continue;
}

angle::Result State::syncReadAttachments(const Context *context, Command command)
{
    ASSERT(mReadFramebuffer);
    ASSERT(isRobustResourceInitEnabled());
    return mReadFramebuffer->ensureReadAttachmentsInitialized(context);
}

angle::Result State::syncDrawAttachments(const Context *context, Command command)
{
    ASSERT(mDrawFramebuffer);
    ASSERT(isRobustResourceInitEnabled());
    return mDrawFramebuffer->ensureDrawAttachmentsInitialized(context);
}

angle::Result State::syncReadFramebuffer(const Context *context, Command command)
{
    ASSERT(mReadFramebuffer);
    return mReadFramebuffer->syncState(context, GL_READ_FRAMEBUFFER, command);
}

angle::Result State::syncDrawFramebuffer(const Context *context, Command command)
{
    ASSERT(mDrawFramebuffer);
    mDrawFramebuffer->setWriteControlMode(context->getState().getFramebufferSRGB()
                                              ? SrgbWriteControlMode::Default
                                              : SrgbWriteControlMode::Linear);
    return mDrawFramebuffer->syncState(context, GL_DRAW_FRAMEBUFFER, command);
}

angle::Result State::syncTextures(const Context *context, Command command)
{
    if (mDirtyTextures.none())
        return angle::Result::Continue;

    for (size_t textureIndex : mDirtyTextures)
    {
        Texture *texture = mActiveTexturesCache[textureIndex];
        if (texture && texture->hasAnyDirtyBit())
        {
            ANGLE_TRY(texture->syncState(context, Command::Other));
        }
    }

    mDirtyTextures.reset();
    return angle::Result::Continue;
}

angle::Result State::syncImages(const Context *context, Command command)
{
    if (mDirtyImages.none())
        return angle::Result::Continue;

    for (size_t imageUnitIndex : mDirtyImages)
    {
        Texture *texture = mImageUnits[imageUnitIndex].texture.get();
        if (texture && texture->hasAnyDirtyBit())
        {
            ANGLE_TRY(texture->syncState(context, Command::Other));
        }
    }

    mDirtyImages.reset();
    return angle::Result::Continue;
}

angle::Result State::syncSamplers(const Context *context, Command command)
{
    if (mDirtySamplers.none())
        return angle::Result::Continue;

    for (size_t samplerIndex : mDirtySamplers)
    {
        BindingPointer<Sampler> &sampler = mSamplers[samplerIndex];
        if (sampler.get() && sampler->isDirty())
        {
            ANGLE_TRY(sampler->syncState(context));
        }
    }

    mDirtySamplers.reset();

    return angle::Result::Continue;
}

angle::Result State::syncVertexArray(const Context *context, Command command)
{
    ASSERT(mVertexArray);
    return mVertexArray->syncState(context);
}

angle::Result State::syncProgramPipelineObject(const Context *context, Command command)
{
    // If a ProgramPipeline is bound, ensure it is linked.
    if (mProgramPipeline.get())
    {
        mProgramPipeline->resolveLink(context);
    }
    return angle::Result::Continue;
}

angle::Result State::syncDirtyObject(const Context *context, GLenum target, Command command)
{
    state::DirtyObjects localSet;

    switch (target)
    {
        case GL_READ_FRAMEBUFFER:
            localSet.set(state::DIRTY_OBJECT_READ_FRAMEBUFFER);
            if (mDirtyObjects.test(state::DIRTY_OBJECT_READ_ATTACHMENTS))
            {
                localSet.set(state::DIRTY_OBJECT_READ_ATTACHMENTS);
            }
            break;
        case GL_DRAW_FRAMEBUFFER:
            localSet.set(state::DIRTY_OBJECT_DRAW_FRAMEBUFFER);
            if (mDirtyObjects.test(state::DIRTY_OBJECT_DRAW_ATTACHMENTS))
            {
                localSet.set(state::DIRTY_OBJECT_DRAW_ATTACHMENTS);
            }
            break;
        default:
            UNREACHABLE();
            break;
    }

    return syncDirtyObjects(context, localSet, command);
}

void State::setObjectDirty(GLenum target)
{
    switch (target)
    {
        case GL_READ_FRAMEBUFFER:
            setReadFramebufferDirty();
            break;
        case GL_DRAW_FRAMEBUFFER:
            setDrawFramebufferDirty();
            break;
        case GL_FRAMEBUFFER:
            setReadFramebufferDirty();
            setDrawFramebufferDirty();
            break;
        case GL_VERTEX_ARRAY:
            mDirtyObjects.set(state::DIRTY_OBJECT_VERTEX_ARRAY);
            break;
        default:
            break;
    }
}

angle::Result State::installProgramExecutable(const Context *context)
{
    // OpenGL Spec:
    // "If LinkProgram or ProgramBinary successfully re-links a program object
    //  that was already in use as a result of a previous call to UseProgram, then the
    //  generated executable code will be installed as part of the current rendering state."
    ASSERT(mProgram->isLinked());

    mDirtyBits.set(state::DIRTY_BIT_PROGRAM_EXECUTABLE);

    // Make sure the program binary is cached if needed and not already.  This is automatically done
    // on program destruction, but is done here anyway to support situations like Android apps that
    // are typically killed instead of cleanly closed.
    mProgram->cacheProgramBinaryIfNecessary(context);

    // The bound Program always overrides the ProgramPipeline, so install the executable regardless
    // of whether a program pipeline is bound.
    InstallExecutable(context, mProgram->getSharedExecutable(), &mExecutable);
    return onExecutableChange(context);
}

angle::Result State::installProgramPipelineExecutable(const Context *context)
{
    ASSERT(mProgramPipeline->isLinked());

    mDirtyBits.set(state::DIRTY_BIT_PROGRAM_EXECUTABLE);

    // A bound Program always overrides the ProgramPipeline, so only update the current
    // ProgramExecutable if there isn't currently a Program bound.
    if (mProgram == nullptr)
    {
        InstallExecutable(context, mProgramPipeline->getSharedExecutable(), &mExecutable);
        return onExecutableChange(context);
    }

    return angle::Result::Continue;
}

angle::Result State::installProgramPipelineExecutableIfNotAlready(const Context *context)
{
    // If a program pipeline is bound, then unbound and bound again, its executable will still be
    // set, and there is no need to reinstall it.
    if (mExecutable.get() == mProgramPipeline->getSharedExecutable().get())
    {
        return onExecutableChange(context);
    }
    return installProgramPipelineExecutable(context);
}

angle::Result State::onExecutableChange(const Context *context)
{
    // Set any bound textures.
    const ActiveTextureTypeArray &textureTypes = mExecutable->getActiveSamplerTypes();

    for (size_t textureIndex : mExecutable->getActiveSamplersMask())
    {
        TextureType type = textureTypes[textureIndex];

        // This can happen if there is a conflicting texture type.
        if (type == TextureType::InvalidEnum)
            continue;

        Texture *texture = getTextureForActiveSampler(type, textureIndex);
        updateTextureBinding(context, textureIndex, texture);
    }

    for (size_t imageUnitIndex : mExecutable->getActiveImagesMask())
    {
        Texture *image = mImageUnits[imageUnitIndex].texture.get();
        if (!image)
            continue;

        const bool needsRobustInit =
            isRobustResourceInitEnabled() && image->initState() == InitState::MayNeedInit;
        if (image->hasAnyDirtyBit() || needsRobustInit)
        {
            ANGLE_TRY(image->syncState(context, Command::Other));
        }
    }

    // Set all active blocks dirty on executable change
    mDirtyUniformBlocks = mExecutable->getActiveUniformBufferBlocks();
    // Set all types dirty on executable change
    mUniformBufferBlocksDirtyTypeMask.set();

    return angle::Result::Continue;
}

void State::setTextureDirty(size_t textureUnitIndex)
{
    mDirtyObjects.set(state::DIRTY_OBJECT_TEXTURES);
    mDirtyTextures.set(textureUnitIndex);
}

void State::setSamplerDirty(size_t samplerIndex)
{
    mDirtyObjects.set(state::DIRTY_OBJECT_SAMPLERS);
    mDirtySamplers.set(samplerIndex);
}

void State::setImageUnit(const Context *context,
                         size_t unit,
                         Texture *texture,
                         GLint level,
                         GLboolean layered,
                         GLint layer,
                         GLenum access,
                         GLenum format)
{
    ASSERT(!mImageUnits.empty());

    ImageUnit &imageUnit = mImageUnits[unit];

    if (texture)
    {
        texture->onBindAsImageTexture();
    }
    imageUnit.texture.set(context, texture);
    imageUnit.level   = level;
    imageUnit.layered = layered;
    imageUnit.layer   = layer;
    imageUnit.access  = access;
    imageUnit.format  = format;
    mDirtyBits.set(state::DIRTY_BIT_IMAGE_BINDINGS);

    onImageStateChange(context, unit);
}

void State::setMaxShaderCompilerThreads(GLuint count)
{
    mMaxShaderCompilerThreads = count;
}

// Handle a dirty texture event.
void State::onActiveTextureChange(const Context *context, size_t textureUnit)
{
    if (mExecutable)
    {
        TextureType type       = mExecutable->getActiveSamplerTypes()[textureUnit];
        Texture *activeTexture = (type != TextureType::InvalidEnum)
                                     ? getTextureForActiveSampler(type, textureUnit)
                                     : nullptr;
        updateTextureBinding(context, textureUnit, activeTexture);

        mExecutable->onStateChange(angle::SubjectMessage::ProgramTextureOrImageBindingChanged);
    }
}

void State::onActiveTextureStateChange(const Context *context, size_t textureUnit)
{
    if (mExecutable)
    {
        TextureType type       = mExecutable->getActiveSamplerTypes()[textureUnit];
        Texture *activeTexture = (type != TextureType::InvalidEnum)
                                     ? getTextureForActiveSampler(type, textureUnit)
                                     : nullptr;
        setActiveTextureDirty(textureUnit, activeTexture);
    }
}

void State::onImageStateChange(const Context *context, size_t unit)
{
    if (mExecutable)
    {
        const ImageUnit &image = mImageUnits[unit];

        // Have nothing to do here if no texture bound
        if (!image.texture.get())
            return;

        const bool needsRobustInit =
            isRobustResourceInitEnabled() && image.texture->initState() == InitState::MayNeedInit;
        if (image.texture->hasAnyDirtyBit() || needsRobustInit)
        {
            mDirtyImages.set(unit);
            mDirtyObjects.set(state::DIRTY_OBJECT_IMAGES);
        }

        if (needsRobustInit)
        {
            mDirtyObjects.set(state::DIRTY_OBJECT_IMAGES_INIT);
        }

        mExecutable->onStateChange(angle::SubjectMessage::ProgramTextureOrImageBindingChanged);
    }
}

void State::onUniformBufferStateChange(size_t uniformBufferIndex, angle::SubjectMessage message)
{
    if (mExecutable)
    {
        // When a buffer at a given binding changes, set all blocks mapped to it dirty.
        mDirtyUniformBlocks |=
            mExecutable->getUniformBufferBlocksMappedToBinding(uniformBufferIndex);

        if (message == angle::SubjectMessage::InternalMemoryAllocationChanged)
        {
            mUniformBufferBlocksDirtyTypeMask.set(BufferDirtyType::Binding);
        }
        else
        {
            ASSERT(message == angle::SubjectMessage::SubjectChanged ||   // buffer state change
                   message == angle::SubjectMessage::SubjectMapped ||    // buffer map
                   message == angle::SubjectMessage::SubjectUnmapped ||  // buffer unmap
                   message == angle::SubjectMessage::BindingChanged);    // XFB state change
        }
    }

    // This could be represented by a different dirty bit. Using the same one keeps it simple.
    mDirtyBits.set(state::DIRTY_BIT_UNIFORM_BUFFER_BINDINGS);
}

void State::onAtomicCounterBufferStateChange(size_t atomicCounterBufferIndex)
{
    mDirtyBits.set(state::DIRTY_BIT_ATOMIC_COUNTER_BUFFER_BINDING);
}

void State::onShaderStorageBufferStateChange(size_t shaderStorageBufferIndex)
{
    mDirtyBits.set(state::DIRTY_BIT_SHADER_STORAGE_BUFFER_BINDING);
}

void State::onCurrentExecutableRelink()
{
    // Called when a program or PPO is already current but its executable is recreated.  The state
    // of the previous executable is cleaned up before the new executable is installed.
    if (mExecutable)
    {
        unsetActiveTextures(mExecutable->getActiveSamplersMask());
    }
}

void State::initializeForCapture(const Context *context)
{
    mPrivateState.initializeForCapture(context);

    // This little kludge gets around the frame capture "constness". It should be safe because
    // nothing in the context is modified in a non-compatible way during capture.
    Context *mutableContext = const_cast<Context *>(context);
    initialize(mutableContext);
}

}  // namespace gl
