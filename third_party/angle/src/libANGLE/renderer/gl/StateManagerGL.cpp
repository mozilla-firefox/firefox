//
// Copyright 2015 The ANGLE Project Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//

// StateManagerGL.h: Defines a class for caching applied OpenGL state

#include "libANGLE/renderer/gl/StateManagerGL.h"
#include "libANGLE/renderer/gl/ContextGL.h"

#include <string.h>
#include <algorithm>
#include <limits>

#include "anglebase/numerics/safe_conversions.h"
#include "common/bitset_utils.h"
#include "common/mathutil.h"
#include "common/matrix_utils.h"
#include "common/unsafe_buffers.h"
#include "libANGLE/Context.h"
#include "libANGLE/Framebuffer.h"
#include "libANGLE/Query.h"
#include "libANGLE/TransformFeedback.h"
#include "libANGLE/VertexArray.h"
#include "libANGLE/histogram_macros.h"
#include "libANGLE/queryconversions.h"
#include "libANGLE/renderer/gl/BufferGL.h"
#include "libANGLE/renderer/gl/FramebufferGL.h"
#include "libANGLE/renderer/gl/FunctionsGL.h"
#include "libANGLE/renderer/gl/ProgramGL.h"
#include "libANGLE/renderer/gl/QueryGL.h"
#include "libANGLE/renderer/gl/SamplerGL.h"
#include "libANGLE/renderer/gl/TextureGL.h"
#include "libANGLE/renderer/gl/TransformFeedbackGL.h"
#include "libANGLE/renderer/gl/VertexArrayGL.h"
#include "platform/PlatformMethods.h"

namespace rx
{

namespace
{

constexpr size_t kMaxDeferredDeletedBuffers     = 128;
constexpr size_t kMaxDeferredDeletedBuffersSize = 128 * 1024 * 1024;

inline void SetGLBoolState(const FunctionsGL *functions, GLenum name, bool value)
{
    if (value)
    {
        functions->enable(name);
    }
    else
    {
        functions->disable(name);
    }
}

inline void SetGLIndexedBoolState(const FunctionsGL *functions,
                                  GLenum name,
                                  GLuint index,
                                  bool value)
{
    if (value)
    {
        functions->enablei(name, index);
    }
    else
    {
        functions->disablei(name, index);
    }
}

inline void SetVertexAttribArrayEnabled(const FunctionsGL *functions, GLuint index, bool value)
{
    if (value)
    {
        functions->enableVertexAttribArray(index);
    }
    else
    {
        functions->disableVertexAttribArray(index);
    }
}

#if defined(ANGLE_ENABLE_ASSERTS)
#    define ANGLE_GL_CHECK_GET_HELPER(functions, getter, name, value)                              \
        do                                                                                         \
        {                                                                                          \
            ANGLE_GL_CLEAR_ERRORS(functions);                                                      \
            functions->getter(name, value);                                                        \
            GLenum error = functions->getError();                                                  \
            (error == GL_NO_ERROR) ? static_cast<void>(0)                                          \
                                   : ERR() << "Querying " << gl::FmtHex(name) << " using "         \
                                           << #getter << " generated error " << gl::FmtHex(error); \
        } while (0)

#    define ANGLE_GL_CHECK_GET_INDEXED_HELPER(functions, getter, name, index, value)               \
        do                                                                                         \
        {                                                                                          \
            ANGLE_GL_CLEAR_ERRORS(functions);                                                      \
            functions->getter(name, index, value);                                                 \
            GLenum error = functions->getError();                                                  \
            (error == GL_NO_ERROR) ? static_cast<void>(0)                                          \
                                   : ERR() << "Querying " << gl::FmtHex(name) << " at index "      \
                                           << index << " using " << #getter << " generated error " \
                                           << gl::FmtHex(error);                                   \
        } while (0)

#    define ANGLE_GL_CHECK_GET_ENABLED_HELPER(functions, getter, name, value)                      \
        do                                                                                         \
        {                                                                                          \
            ANGLE_GL_CLEAR_ERRORS(functions);                                                      \
            *value       = functions->getter(name);                                                \
            GLenum error = functions->getError();                                                  \
            (error == GL_NO_ERROR) ? static_cast<void>(0)                                          \
                                   : ERR() << "Querying " << gl::FmtHex(name) << " using "         \
                                           << #getter << " generated error " << gl::FmtHex(error); \
        } while (0)

#    define ANGLE_GL_CHECK_GET_INDEXED_ENABLED_HELPER(functions, getter, name, index, value)       \
        do                                                                                         \
        {                                                                                          \
            ANGLE_GL_CLEAR_ERRORS(functions);                                                      \
            *value       = functions->getter(name, index);                                         \
            GLenum error = functions->getError();                                                  \
            (error == GL_NO_ERROR) ? static_cast<void>(0)                                          \
                                   : ERR() << "Querying " << gl::FmtHex(name) << " at index "      \
                                           << index << " using " << #getter << " generated error " \
                                           << gl::FmtHex(error);                                   \
        } while (0)

#    define ANGLE_GL_CHECK_GET_VERTEX_HELPER(functions, getter, index, name, value)                \
        do                                                                                         \
        {                                                                                          \
            ANGLE_GL_CLEAR_ERRORS(functions);                                                      \
            functions->getter(index, name, value);                                                 \
            GLenum error = functions->getError();                                                  \
            (error == GL_NO_ERROR) ? static_cast<void>(0)                                          \
                                   : ERR() << "Querying " << gl::FmtHex(name) << " for attribute " \
                                           << index << " using " << #getter << " generated error " \
                                           << gl::FmtHex(error);                                   \
        } while (0)
#else
#    define ANGLE_GL_CHECK_GET_HELPER(functions, getter, name, value) functions->getter(name, value)
#    define ANGLE_GL_CHECK_GET_INDEXED_HELPER(functions, getter, name, index, value) \
        functions->getter(name, index, value)
#    define ANGLE_GL_CHECK_GET_ENABLED_HELPER(functions, getter, name, value) \
        *value = functions->getter(name)
#    define ANGLE_GL_CHECK_GET_INDEXED_ENABLED_HELPER(functions, getter, name, index, value) \
        *value = functions->getter(name, index)
#    define ANGLE_GL_CHECK_GET_VERTEX_HELPER(functions, getter, index, name, value) \
        functions->getter(index, name, value)
#endif

// Non-indexed GLboolean -> glIsEnabled
void GetEnabledHelper(const FunctionsGL *functions, GLenum name, GLboolean *value)
{
    ANGLE_GL_CHECK_GET_ENABLED_HELPER(functions, isEnabled, name, value);
}

// Indexed GLboolean -> glIsEnabledi
void GetEnabledHelper(const FunctionsGL *functions, GLenum name, GLuint index, GLboolean *value)
{
    ANGLE_GL_CHECK_GET_INDEXED_ENABLED_HELPER(functions, isEnabledi, name, index, value);
}
// Non-indexed bool -> Non-indexed GLboolean (for enabled checks)
void GetEnabledHelper(const FunctionsGL *functions, GLenum name, bool *value)
{
    GLboolean v = gl::ConvertToGLBoolean(*value);
    GetEnabledHelper(functions, name, &v);
    *value = gl::ConvertToBool(v);
}

// Indexed bool -> Indexed GLboolean (for enabled checks)
void GetEnabledHelper(const FunctionsGL *functions, GLenum name, GLuint index, bool *value)
{
    GLboolean v = gl::ConvertToGLBoolean(*value);
    GetEnabledHelper(functions, name, index, &v);
    *value = gl::ConvertToBool(v);
}

// Non-indexed GLint -> glGetIntegerv
void GetHelper(const FunctionsGL *functions, GLenum name, GLint *value)
{
    ANGLE_GL_CHECK_GET_HELPER(functions, getIntegerv, name, value);
}

// Indexed GLint -> glGetIntegeri_v
void GetHelper(const FunctionsGL *functions, GLenum name, GLuint index, GLint *value)
{
    ANGLE_GL_CHECK_GET_INDEXED_HELPER(functions, getIntegeri_v, name, index, value);
}

// Non-indexed GLenum -> Non-indexed GLint
void GetHelper(const FunctionsGL *functions, GLenum name, GLenum *value)
{
    GLint v = *value;
    GetHelper(functions, name, &v);
    *value = static_cast<GLenum>(v);
}

// Indexed GLenum -> Indexed GLint
void GetHelper(const FunctionsGL *functions, GLenum name, GLuint index, GLenum *value)
{
    GLint v = *value;
    GetHelper(functions, name, index, &v);
    *value = static_cast<GLenum>(v);
}

// Non-indexed bool -> Non-indexed GLint
void GetHelper(const FunctionsGL *functions, GLenum name, bool *value)
{
    GLint v = gl::ConvertToGLBoolean(*value);
    GetHelper(functions, name, &v);
    *value = gl::ConvertToBool(v);
}

// Indexed GLboolean -> Indexed GLint
void GetHelper(const FunctionsGL *functions, GLenum name, GLuint index, GLboolean *value)
{
    GLint v = gl::ConvertToGLBoolean(*value);
    GetHelper(functions, name, index, &v);
    *value = gl::ConvertToBool(v);
}

// Non-indexed std::array<bool, N> -> Non-indexed GLboolean
template <size_t N>
void GetHelper(const FunctionsGL *functions, GLenum name, std::array<bool, N> *values)
{
    std::array<GLint, N> v;
    for (size_t i = 0; i < N; i++)
    {
        v[i] = gl::ConvertToGLBoolean(values->at(i));
    }
    GetHelper(functions, name, v.data());
    for (size_t i = 0; i < N; i++)
    {
        (*values)[i] = gl::ConvertToBool(v[i]);
    }
}

// Indexed std::array<bool, N> -> Indexed GLboolean
template <size_t N>
void GetHelper(const FunctionsGL *functions, GLenum name, GLuint index, std::array<bool, N> *values)
{
    std::array<GLint, N> v;
    for (size_t i = 0; i < N; i++)
    {
        v[i] = gl::ConvertToGLBoolean(values->at(i));
    }
    GetHelper(functions, name, index, v.data());
    for (size_t i = 0; i < N; i++)
    {
        (*values)[i] = gl::ConvertToBool(v[i]);
    }
}

// Non-indexed gl::Rectangle -> Non-indexed GLint
void GetHelper(const FunctionsGL *functions, GLenum name, gl::Rectangle *rect)
{
    std::array<GLint, 4> v = {rect->x, rect->y, rect->width, rect->height};
    GetHelper(functions, name, v.data());
    *rect = gl::Rectangle(v[0], v[1], v[2], v[3]);
}

// Non-indexed GLfloat -> glGetFloatv
void GetHelper(const FunctionsGL *functions, GLenum name, GLfloat *value)
{
    ANGLE_GL_CHECK_GET_HELPER(functions, getFloatv, name, value);
}

// Non-indexed gl::ColorF -> Non-indexed GLfloat
void GetHelper(const FunctionsGL *functions, GLenum name, gl::ColorF *color)
{
    std::array<GLfloat, 4> v = {color->red, color->green, color->blue, color->alpha};
    GetHelper(functions, name, v.data());
    *color = gl::ColorF(v[0], v[1], v[2], v[3]);
}

// Non-indexed packed enum -> Non-indexed GLint
template <typename InternalEnumType, InternalEnumType MaxSize = InternalEnumType::EnumCount>
void GetHelper(const FunctionsGL *functions, GLenum name, InternalEnumType *internalEnum)
{
    GLint v = gl::ToGLenum(*internalEnum);
    GetHelper(functions, name, &v);
    *internalEnum = gl::FromGLenum<InternalEnumType>(v);
}

// Indexed packed enum -> Indexed GLint
template <typename InternalEnumType, InternalEnumType MaxSize = InternalEnumType::EnumCount>
void GetHelper(const FunctionsGL *functions,
               GLenum name,
               GLuint index,
               InternalEnumType *internalEnum)
{
    GLint v = gl::ToGLenum(*internalEnum);
    GetHelper(functions, name, index, &v);
    *internalEnum = gl::FromGLenum<InternalEnumType>(v);
}

// Non-indexed std::array<packed enum> -> Non-indexed GLint
template <typename InternalEnumType,
          size_t N,
          InternalEnumType MaxSize = InternalEnumType::EnumCount>
void GetHelper(const FunctionsGL *functions,
               GLenum name,
               std::array<InternalEnumType, N> *internalEnum)
{
    std::array<GLint, N> v;
    for (size_t i = 0; i < N; i++)
    {
        v[i] = gl::ToGLenum(internalEnum->at(i));
    }
    GetHelper(functions, name, v.data());
    for (size_t i = 0; i < N; i++)
    {
        internalEnum->at(i) = gl::FromGLenum<InternalEnumType>(v[i]);
    }
}

// Indexed GLint64 -> glGetInteger64i_v
void GetHelper(const FunctionsGL *functions, GLenum name, GLuint index, GLint64 *value)
{
    ANGLE_GL_CHECK_GET_INDEXED_HELPER(functions, getInteger64i_v, name, index, value);
}

// Indexed GLintptr -> Indexed GLint64. Enabled only if GLintptr is not the same as GLint64.
template <typename T = void>
void GetHelper(const FunctionsGL *functions, GLenum name, GLuint index, GLintptr *value)
    requires(!std::is_same_v<GLintptr, GLint64>)
{
    GLint64 v = *value;
    GetHelper(functions, name, index, &v);
    *value = static_cast<GLintptr>(v);
}

// GLint -> glGetVertexAttribiv
void GetVertexHelper(const FunctionsGL *functions, GLuint index, GLenum name, GLint *value)
{
    ANGLE_GL_CHECK_GET_VERTEX_HELPER(functions, getVertexAttribiv, index, name, value);
}

// GLenum -> GLint
void GetVertexHelper(const FunctionsGL *functions, GLuint index, GLenum name, GLenum *value)
{
    GLint v = static_cast<GLint>(*value);
    GetVertexHelper(functions, index, name, &v);
    *value = static_cast<GLint>(v);
}

// bool -> GLint
void GetVertexHelper(const FunctionsGL *functions, GLuint index, GLenum name, bool *value)
{
    GLint v = gl::ConvertToGLBoolean(*value);
    GetVertexHelper(functions, index, name, &v);
    *value = gl::ConvertToBool(v);
}

// packed enum -> GLint
template <typename InternalEnumType, InternalEnumType MaxSize = InternalEnumType::EnumCount>
void GetVertexHelper(const FunctionsGL *functions,
                     GLuint index,
                     GLenum name,
                     InternalEnumType *internalEnum)
{
    GLint v = gl::ToGLenum(*internalEnum);
    GetVertexHelper(functions, index, name, &v);
    *internalEnum = gl::FromGLenum<InternalEnumType>(v);
}

// const void* -> glGetVertexAttribPointerv
void GetVertexHelper(const FunctionsGL *functions, GLuint index, GLenum name, void **value)
{
    ANGLE_GL_CHECK_GET_VERTEX_HELPER(functions, getVertexAttribPointerv, index, name, value);
}

struct ScopedBindDrawFramebuffer
{
    ScopedBindDrawFramebuffer(const FunctionsGL *functions, GLuint prevFbo, GLuint fbo)
        : functions(functions),
          binding(nativegl::SupportsSeparateFramebufferBindings(functions) ? GL_DRAW_FRAMEBUFFER
                                                                           : GL_FRAMEBUFFER),
          prevFbo(prevFbo)
    {
        functions->bindFramebuffer(binding, fbo);
    }

    ~ScopedBindDrawFramebuffer() { functions->bindFramebuffer(binding, prevFbo); }

    const FunctionsGL *functions = nullptr;
    GLenum binding               = 0;
    GLenum prevFbo               = 0;
};

struct ScopedBindVAO
{
    ScopedBindVAO(const FunctionsGL *functions, GLuint prevVAO, GLuint newVAO)
        : functions(functions), prevVAO(prevVAO), newVAO(newVAO)
    {
        if (newVAO != prevVAO)
        {
            functions->bindVertexArray(newVAO);
        }
    }

    ~ScopedBindVAO()
    {
        if (newVAO != prevVAO)
        {
            functions->bindVertexArray(prevVAO);
        }
    }

    const FunctionsGL *functions = nullptr;
    GLuint prevVAO               = 0;
    GLuint newVAO                = 0;
};

void QueryContextStateGL(const FunctionsGL *functions,
                         GLuint framebufferWithStencilBits,
                         ContextStateGL *state)
{
    GetHelper(functions, GL_CURRENT_PROGRAM, &state->program);

    if (nativegl::SupportsVertexArrayObjects(functions))
    {
        GetHelper(functions, GL_VERTEX_ARRAY_BINDING, &state->vao);
    }

    {
        // Query the default VAO state, temporarily bind VAO 0
        ScopedBindVAO scopedVAO(functions, state->vao, 0);
        QueryVertexArrayStateGL(functions, &state->defaultVAOState);
    }

    GLint maxVertexAttribs = 0;
    GetHelper(functions, GL_MAX_VERTEX_ATTRIBS, &maxVertexAttribs);
    maxVertexAttribs = std::min(maxVertexAttribs, static_cast<GLint>(gl::MAX_VERTEX_ATTRIBS));

    for (GLint i = 0; i < maxVertexAttribs; i++)
    {
        // It is only valid to query the vertex attrib data using the same type used to set the data
        // but there is no query for the data type. Do our best and query using the type that
        // already exists in state.
        switch (state->vertexAttribCurrentValues[i].Type)
        {
            case gl::VertexAttribType::Float:
                functions->getVertexAttribfv(
                    i, GL_CURRENT_VERTEX_ATTRIB,
                    state->vertexAttribCurrentValues[i].Values.FloatValues);
                break;
            case gl::VertexAttribType::Int:
                functions->getVertexAttribIiv(i, GL_CURRENT_VERTEX_ATTRIB,
                                              state->vertexAttribCurrentValues[i].Values.IntValues);
                break;
            case gl::VertexAttribType::UnsignedInt:
                functions->getVertexAttribIuiv(
                    i, GL_CURRENT_VERTEX_ATTRIB,
                    state->vertexAttribCurrentValues[i].Values.UnsignedIntValues);
                break;
            default:
                UNREACHABLE();
        }
    }

    for (gl::BufferBinding bufferBinding : angle::AllEnums<gl::BufferBinding>())
    {
        if (!nativegl::SupportsBufferBinding(functions, bufferBinding))
        {
            continue;
        }

        // Transform feedback buffer bindings are tracked in TransformFeedbackGL
        if (bufferBinding == gl::BufferBinding::TransformFeedback)
        {
            continue;
        }

        nativegl::BufferBindingQuery queryEnums = nativegl::GetBufferBindingQuery(bufferBinding);
        GetHelper(functions, queryEnums.bindingQuery, &state->buffers[bufferBinding]);

        for (GLuint i = 0; i < state->indexedBuffers[bufferBinding].size(); i++)
        {
            IndexedBufferBindingGL &binding = state->indexedBuffers[bufferBinding][i];

            GetHelper(functions, queryEnums.bindingQuery, i, &binding.buffer);

            ASSERT(queryEnums.startQuery.has_value());
            GetHelper(functions, queryEnums.startQuery.value(), i, &binding.offset);

            ASSERT(queryEnums.sizeQuery.has_value());
            GetHelper(functions, queryEnums.sizeQuery.value(), i, &binding.size);
        }
    }

    GLuint activeTexture = 0;
    GetHelper(functions, GL_ACTIVE_TEXTURE, &activeTexture);
    state->textureUnitIndex = activeTexture - GL_TEXTURE0;

    GLint maxCombinedTextureImageUnits = 0;
    GetHelper(functions, GL_MAX_COMBINED_TEXTURE_IMAGE_UNITS, &maxCombinedTextureImageUnits);
    maxCombinedTextureImageUnits = std::min(
        maxCombinedTextureImageUnits, static_cast<GLint>(gl::IMPLEMENTATION_MAX_ACTIVE_TEXTURES));

    for (GLint i = 0; i < maxCombinedTextureImageUnits; i++)
    {
        functions->activeTexture(GL_TEXTURE0 + i);
        for (gl::TextureType textureType : angle::AllEnums<gl::TextureType>())
        {
            if (!nativegl::SupportsTextureType(functions, textureType))
            {
                continue;
            }

            GetHelper(functions, nativegl::GetTextureBindingQuery(textureType),
                      &state->textures[textureType][i]);
        }

        if (nativegl::SupportsSamplerObjects(functions))
        {
            GetHelper(functions, GL_SAMPLER_BINDING, &state->samplers[i]);
        }
    }
    // Reset the active texture
    functions->activeTexture(activeTexture);

    for (GLuint i = 0; i < state->images.size(); i++)
    {
        ImageUnitBindingGL &image = state->images[i];
        GetHelper(functions, GL_IMAGE_BINDING_NAME, i, &image.texture);
        GetHelper(functions, GL_IMAGE_BINDING_LEVEL, i, &image.level);
        GetHelper(functions, GL_IMAGE_BINDING_LAYERED, i, &image.layered);
        GetHelper(functions, GL_IMAGE_BINDING_LAYER, i, &image.layer);
        GetHelper(functions, GL_IMAGE_BINDING_ACCESS, i, &image.access);
        GetHelper(functions, GL_IMAGE_BINDING_FORMAT, i, &image.format);
    }

    if (nativegl::SupportsTransformFeedback(functions))
    {
        GetHelper(functions, GL_TRANSFORM_FEEDBACK_BINDING, &state->transformFeedback);
    }

    GetHelper(functions, GL_UNPACK_ALIGNMENT, &state->unpackState.alignment);

    if (nativegl::SupportsUnpackSubImage(functions))
    {
        GetHelper(functions, GL_UNPACK_ROW_LENGTH, &state->unpackState.rowLength);
        GetHelper(functions, GL_UNPACK_SKIP_ROWS, &state->unpackState.skipRows);
        GetHelper(functions, GL_UNPACK_SKIP_PIXELS, &state->unpackState.skipPixels);
    }

    if (nativegl::Supports3DUnpackParameters(functions))
    {
        GetHelper(functions, GL_UNPACK_IMAGE_HEIGHT, &state->unpackState.imageHeight);
        GetHelper(functions, GL_UNPACK_SKIP_IMAGES, &state->unpackState.skipImages);
    }

    GetHelper(functions, GL_PACK_ALIGNMENT, &state->packState.alignment);

    if (nativegl::SupportsPackSubImage(functions))
    {
        GetHelper(functions, GL_PACK_ROW_LENGTH, &state->packState.rowLength);
        GetHelper(functions, GL_PACK_SKIP_ROWS, &state->packState.skipRows);
        GetHelper(functions, GL_PACK_SKIP_PIXELS, &state->packState.skipPixels);
    }

    if (nativegl::SupportsSeparateFramebufferBindings(functions))
    {
        GetHelper(functions, GL_READ_FRAMEBUFFER_BINDING,
                  &state->framebuffers[angle::FramebufferBindingRead]);
        GetHelper(functions, GL_DRAW_FRAMEBUFFER_BINDING,
                  &state->framebuffers[angle::FramebufferBindingDraw]);
    }
    else
    {
        GetHelper(functions, GL_FRAMEBUFFER_BINDING,
                  &state->framebuffers[angle::FramebufferBindingDraw]);
        state->framebuffers[angle::FramebufferBindingRead] =
            state->framebuffers[angle::FramebufferBindingDraw];
    }

    // Bind a framebuffer with stencil bits for the rest of the queries. Many queries related to
    // stencil will mask with the number of bits in the current stencil buffer.
    ScopedBindDrawFramebuffer scopedFramebufferWithStencil(
        functions, state->framebuffers[angle::FramebufferBindingDraw], framebufferWithStencilBits);

    GetHelper(functions, GL_RENDERBUFFER_BINDING, &state->renderbuffer);

    GetEnabledHelper(functions, GL_SCISSOR_TEST, &state->scissorTestEnabled);
    GetHelper(functions, GL_SCISSOR_BOX, &state->scissor);
    GetHelper(functions, GL_VIEWPORT, &state->viewport);
    {
        float depthRange[2] = {state->near, state->far};
        GetHelper(functions, GL_DEPTH_RANGE, depthRange);
        state->near = depthRange[0];
        state->far  = depthRange[1];
    }

    if (nativegl::SupportsClipControl(functions))
    {
        GetHelper(functions, GL_CLIP_ORIGIN, &state->clipOrigin);
        GetHelper(functions, GL_CLIP_DEPTH_MODE, &state->clipDepthMode);
    }

    GetHelper(functions, GL_BLEND_COLOR, &state->blendColor);
    if (nativegl::SupportsDrawBuffersIndexed(functions))
    {
        GLint maxDrawBuffers = 0;
        GetHelper(functions, GL_MAX_DRAW_BUFFERS, &maxDrawBuffers);
        maxDrawBuffers =
            std::min(maxDrawBuffers, static_cast<GLint>(gl::IMPLEMENTATION_MAX_DRAW_BUFFERS));

        for (GLuint i = 0; i < static_cast<GLuint>(maxDrawBuffers); i++)
        {
            bool enabled = state->blendState.getEnabledMask().test(i);
            GetEnabledHelper(functions, GL_BLEND, i, &enabled);
            state->blendState.setEnabledIndexed(i, enabled);

            std::array<bool, 4> colorMask;
            state->blendState.getColorMaskIndexed(i, &colorMask[0], &colorMask[1], &colorMask[2],
                                                  &colorMask[3]);
            GetHelper(functions, GL_COLOR_WRITEMASK, i, &colorMask);
            state->blendState.setColorMaskIndexed(i, colorMask[0], colorMask[1], colorMask[2],
                                                  colorMask[3]);

            gl::BlendFactorType blendSrcRgb = state->blendState.getSrcColorIndexed(i);
            GetHelper(functions, GL_BLEND_SRC_RGB, i, &blendSrcRgb);
            gl::BlendFactorType blendDestRgb = state->blendState.getDstColorIndexed(i);
            GetHelper(functions, GL_BLEND_DST_RGB, i, &blendDestRgb);
            gl::BlendFactorType blendSrcAlpha = state->blendState.getSrcAlphaIndexed(i);
            GetHelper(functions, GL_BLEND_SRC_ALPHA, i, &blendSrcAlpha);
            gl::BlendFactorType blendDestAlpha = state->blendState.getDstAlphaIndexed(i);
            GetHelper(functions, GL_BLEND_DST_ALPHA, i, &blendDestAlpha);
            state->blendState.setFactorsIndexed(i, blendSrcRgb, blendDestRgb, blendSrcAlpha,
                                                blendDestAlpha);

            gl::BlendEquationType blendEquationRgb = state->blendState.getEquationColorIndexed(i);
            GetHelper(functions, GL_BLEND_EQUATION_RGB, i, &blendEquationRgb);
            gl::BlendEquationType blendEquationAlpha = state->blendState.getEquationAlphaIndexed(i);
            GetHelper(functions, GL_BLEND_EQUATION_ALPHA, i, &blendEquationAlpha);
            state->blendState.setEquationsIndexed(i, blendEquationRgb, blendEquationAlpha);
        }
    }
    else
    {
        bool enabled = state->blendState.getEnabledMask().test(0);
        GetEnabledHelper(functions, GL_BLEND, &enabled);
        state->blendState.setEnabled(enabled);

        std::array<bool, 4> colorMask;
        state->blendState.getColorMaskIndexed(0, &colorMask[0], &colorMask[1], &colorMask[2],
                                              &colorMask[3]);
        GetHelper(functions, GL_COLOR_WRITEMASK, &colorMask);
        state->blendState.setColorMask(colorMask[0], colorMask[1], colorMask[2], colorMask[3]);

        gl::BlendFactorType blendSrcRgb = state->blendState.getSrcColorIndexed(0);
        GetHelper(functions, GL_BLEND_SRC_RGB, &blendSrcRgb);
        gl::BlendFactorType blendDestRgb = state->blendState.getDstColorIndexed(0);
        GetHelper(functions, GL_BLEND_DST_RGB, &blendDestRgb);
        gl::BlendFactorType blendSrcAlpha = state->blendState.getSrcAlphaIndexed(0);
        GetHelper(functions, GL_BLEND_SRC_ALPHA, &blendSrcAlpha);
        gl::BlendFactorType blendDestAlpha = state->blendState.getDstAlphaIndexed(0);
        GetHelper(functions, GL_BLEND_DST_ALPHA, &blendDestAlpha);
        state->blendState.setFactors(blendSrcRgb, blendDestRgb, blendSrcAlpha, blendDestAlpha);

        gl::BlendEquationType blendEquationRgb = state->blendState.getEquationColorIndexed(0);
        GetHelper(functions, GL_BLEND_EQUATION_RGB, &blendEquationRgb);
        gl::BlendEquationType blendEquationAlpha = state->blendState.getEquationAlphaIndexed(0);
        GetHelper(functions, GL_BLEND_EQUATION_ALPHA, &blendEquationAlpha);
        state->blendState.setEquations(blendEquationRgb, blendEquationAlpha);
    }
    if (nativegl::SupportsBlendEquationAdvancedCoherent(functions))
    {
        GetEnabledHelper(functions, GL_BLEND_ADVANCED_COHERENT_KHR, &state->blendAdvancedCoherent);
    }

    GetEnabledHelper(functions, GL_SAMPLE_ALPHA_TO_COVERAGE, &state->sampleAlphaToCoverageEnabled);
    GetEnabledHelper(functions, GL_SAMPLE_COVERAGE, &state->sampleCoverageEnabled);
    GetHelper(functions, GL_SAMPLE_COVERAGE_VALUE, &state->sampleCoverageValue);
    GetHelper(functions, GL_SAMPLE_COVERAGE_INVERT, &state->sampleCoverageInvert);
    if (nativegl::SupportsSampleMask(functions))
    {
        GLint maxSampleMaskWords = 0;
        GetHelper(functions, GL_MAX_SAMPLE_MASK_WORDS, &maxSampleMaskWords);
        maxSampleMaskWords = std::min(maxSampleMaskWords,
                                      static_cast<GLint>(gl::IMPLEMENTATION_MAX_SAMPLE_MASK_WORDS));

        GetEnabledHelper(functions, GL_SAMPLE_MASK, &state->sampleMaskEnabled);
        for (GLuint i = 0; i < static_cast<GLuint>(maxSampleMaskWords); i++)
        {
            GetHelper(functions, GL_SAMPLE_MASK_VALUE, i, &state->sampleMaskValues[i]);
        }
    }

    GetEnabledHelper(functions, GL_DEPTH_TEST, &state->depthTestEnabled);
    GetHelper(functions, GL_DEPTH_FUNC, &state->depthFunc);
    GetHelper(functions, GL_DEPTH_WRITEMASK, &state->depthMask);

    GetEnabledHelper(functions, GL_STENCIL_TEST, &state->stencilTestEnabled);

    GetHelper(functions, GL_STENCIL_FUNC, &state->stencilFrontFunc);
    GetHelper(functions, GL_STENCIL_REF, &state->stencilFrontRef);
    GetHelper(functions, GL_STENCIL_VALUE_MASK, &state->stencilFrontValueMask);
    GetHelper(functions, GL_STENCIL_FAIL, &state->stencilFrontStencilFailOp);
    GetHelper(functions, GL_STENCIL_PASS_DEPTH_FAIL, &state->stencilFrontStencilPassDepthFailOp);
    GetHelper(functions, GL_STENCIL_PASS_DEPTH_PASS, &state->stencilFrontStencilPassDepthPassOp);
    GetHelper(functions, GL_STENCIL_WRITEMASK, &state->stencilFrontWritemask);

    GetHelper(functions, GL_STENCIL_BACK_FUNC, &state->stencilBackFunc);
    GetHelper(functions, GL_STENCIL_BACK_REF, &state->stencilBackRef);
    GetHelper(functions, GL_STENCIL_BACK_VALUE_MASK, &state->stencilBackValueMask);
    GetHelper(functions, GL_STENCIL_BACK_FAIL, &state->stencilBackStencilFailOp);
    GetHelper(functions, GL_STENCIL_BACK_PASS_DEPTH_FAIL,
              &state->stencilBackStencilPassDepthFailOp);
    GetHelper(functions, GL_STENCIL_BACK_PASS_DEPTH_PASS,
              &state->stencilBackStencilPassDepthPassOp);
    GetHelper(functions, GL_STENCIL_BACK_WRITEMASK, &state->stencilBackWritemask);

    GetEnabledHelper(functions, GL_CULL_FACE, &state->cullFaceEnabled);
    GetHelper(functions, GL_CULL_FACE_MODE, &state->cullFace);
    GetHelper(functions, GL_FRONT_FACE, &state->frontFace);
    if (nativegl::SupportsPolygonMode(functions))
    {
        // GL Core and ES contexts return one value. GL Compatibility contexts return two values.
        std::array<gl::PolygonMode, 2> polygonMode = {state->polygonMode, state->polygonMode};
        GetHelper(functions, GL_POLYGON_MODE, &polygonMode);
        // Check that either the two values are equal or the second one is unwritten.
        ASSERT(polygonMode[0] == polygonMode[1] || polygonMode[1] == state->polygonMode);
        state->polygonMode = polygonMode[0];

        GetEnabledHelper(functions, GL_POLYGON_OFFSET_POINT, &state->polygonOffsetPointEnabled);
        GetEnabledHelper(functions, GL_POLYGON_OFFSET_LINE, &state->polygonOffsetLineEnabled);
    }
    GetEnabledHelper(functions, GL_POLYGON_OFFSET_FILL, &state->polygonOffsetFillEnabled);
    GetHelper(functions, GL_POLYGON_OFFSET_FACTOR, &state->polygonOffsetFactor);
    GetHelper(functions, GL_POLYGON_OFFSET_UNITS, &state->polygonOffsetUnits);
    if (nativegl::SupportsPolygonOffsetClamp(functions))
    {
        GetHelper(functions, GL_POLYGON_OFFSET_CLAMP_EXT, &state->polygonOffsetClamp);
    }
    if (nativegl::SupportsDepthClamp(functions))
    {
        GetEnabledHelper(functions, GL_DEPTH_CLAMP_EXT, &state->depthClampEnabled);
    }
    if (nativegl::SupportsRasterizerDiscard(functions))
    {
        GetEnabledHelper(functions, GL_RASTERIZER_DISCARD, &state->rasterizerDiscardEnabled);
    }
    GetHelper(functions, GL_LINE_WIDTH, &state->lineWidth);

    if (nativegl::SupportsPrimitiveRestartFixedIndex(functions))
    {
        GetEnabledHelper(functions, GL_PRIMITIVE_RESTART_FIXED_INDEX,
                         &state->primitiveRestartFixedIndexEnabled);
    }
    if (nativegl::SupportsPrimitiveRestart(functions))
    {
        GetEnabledHelper(functions, GL_PRIMITIVE_RESTART, &state->primitiveRestartEnabled);
        GetHelper(functions, GL_PRIMITIVE_RESTART_INDEX, &state->primitiveRestartIndex);
    }

    GetHelper(functions, GL_COLOR_CLEAR_VALUE, &state->clearColor);
    GetHelper(functions, GL_DEPTH_CLEAR_VALUE, &state->clearDepth);
    GetHelper(functions, GL_STENCIL_CLEAR_VALUE, &state->clearStencil);

    if (nativegl::SupportsSRGBWriteControl(functions))
    {
        GetEnabledHelper(functions, GL_FRAMEBUFFER_SRGB, &state->framebufferSRGBEnabled);
    }

    GetHelper(functions, GL_DITHER, &state->ditherEnabled);
    if (nativegl::SupportsSettingCubemapSeamless(functions))
    {
        GetEnabledHelper(functions, GL_TEXTURE_CUBE_MAP_SEAMLESS,
                         &state->textureCubemapSeamlessEnabled);
    }

    if (nativegl::SupportsMultisampleComatibility(functions))
    {
        GetEnabledHelper(functions, GL_MULTISAMPLE, &state->multisamplingEnabled);
        GetHelper(functions, GL_SAMPLE_ALPHA_TO_ONE, &state->sampleAlphaToOneEnabled);
    }

    if (nativegl::SupportsProvokingVertex(functions))
    {
        GetHelper(functions, GL_PROVOKING_VERTEX, &state->provokingVertex);
    }

    if (nativegl::SupportsClipCullDistance(functions))
    {
        GLint maxClipDistances = 0;
        GetHelper(functions, GL_MAX_CLIP_DISTANCES, &maxClipDistances);
        maxClipDistances =
            std::min(maxClipDistances, static_cast<GLint>(gl::IMPLEMENTATION_MAX_CLIP_DISTANCES));

        for (GLuint i = 0; i < static_cast<GLuint>(maxClipDistances); i++)
        {
            bool enabled = state->enabledClipDistances.test(i);
            GetEnabledHelper(functions, GL_CLIP_DISTANCE0 + i, &enabled);
            state->enabledClipDistances[i] = enabled;
        }
    }

    if (nativegl::SupportsLogicOp(functions))
    {
        GetEnabledHelper(functions, GL_COLOR_LOGIC_OP, &state->logicOpEnabled);
        GetHelper(functions, GL_LOGIC_OP_MODE, &state->logicOp);
    }
}

bool VertexAttribCurrentValuesEqual(const gl::VertexAttribCurrentValueData &a,
                                    const gl::VertexAttribCurrentValueData &b)
{
    // When comparing vertex attribute current values, only compare the data, not the type.
    // SAFETY: this is the exact comparison done in operator== for gl::VertexAttribCurrentValueData.
    return ANGLE_UNSAFE_BUFFERS(
               memcmp(&a.Values, &b.Values, sizeof(gl::VertexAttribCurrentValueData::Values))) == 0;
}

// Compute the mask of attributes that have different current values
gl::AttributesMask ComputeVertexAttribCurrentValueDiffMask(
    const gl::AttribArray<gl::VertexAttribCurrentValueData> &a,
    const gl::AttribArray<gl::VertexAttribCurrentValueData> &b)
{
    gl::AttributesMask diffMask;
    for (size_t i = 0; i < a.size(); i++)
    {
        diffMask.set(i, !VertexAttribCurrentValuesEqual(a[i], b[i]));
    }
    return diffMask;
}

std::optional<gl::state::DirtyBitType> GetBufferBindingDirtyBit(gl::BufferBinding binding)
{
    switch (binding)
    {
        case gl::BufferBinding::Array:
            // Array buffer bindings are set before vertex attrib calls.
            return std::nullopt;
        case gl::BufferBinding::AtomicCounter:
            return gl::state::DIRTY_BIT_ATOMIC_COUNTER_BUFFER_BINDING;
        case gl::BufferBinding::CopyRead:
            // CopyRead does not affect any operations.
            return std::nullopt;
        case gl::BufferBinding::CopyWrite:
            // CopyWrite does not affect any operations.
            return std::nullopt;
        case gl::BufferBinding::DispatchIndirect:
            return gl::state::DIRTY_BIT_DISPATCH_INDIRECT_BUFFER_BINDING;
        case gl::BufferBinding::DrawIndirect:
            return gl::state::DIRTY_BIT_DRAW_INDIRECT_BUFFER_BINDING;
        case gl::BufferBinding::ElementArray:
            // Managed by the VAO
            return std::nullopt;
        case gl::BufferBinding::PixelPack:
            return gl::state::DIRTY_BIT_PACK_BUFFER_BINDING;
        case gl::BufferBinding::PixelUnpack:
            return gl::state::DIRTY_BIT_UNPACK_BUFFER_BINDING;
        case gl::BufferBinding::ShaderStorage:
            return gl::state::DIRTY_BIT_SHADER_STORAGE_BUFFER_BINDING;
        case gl::BufferBinding::Texture:
            // Not implemented in the GL backend
            return std::nullopt;
        case gl::BufferBinding::TransformFeedback:
            // Transform feedback buffer bindings are tracked in TransformFeedbackGL
            return std::nullopt;
        case gl::BufferBinding::Uniform:
            return gl::state::DIRTY_BIT_UNIFORM_BUFFER_BINDINGS;
        default:
            UNREACHABLE();
            return std::nullopt;
    }
}

template <typename T>
void SetDirtyBitIfStateDifferent(const T &a,
                                 const T &b,
                                 gl::state::DirtyBits *bits,
                                 gl::state::DirtyBitType bit)
{
    if (a != b)
    {
        bits->set(bit);
    }
}

template <typename T>
void SetDirtyBitExtIfStateDifferent(const T &a,
                                    const T &b,
                                    gl::state::DirtyBits *bits,
                                    gl::state::ExtendedDirtyBits *exBits,
                                    gl::state::ExtendedDirtyBitType exBit)
{
    if (a != b)
    {
        bits->set(gl::state::DIRTY_BIT_EXTENDED);
        exBits->set(exBit);
    }
}

// Compute the set of dirty bits needed to transition between two ContextStateGL objects.
[[maybe_unused]] void ComputeDirtyBitsBetweenStates(const ContextStateGL &a,
                                                    const ContextStateGL &b,
                                                    gl::state::DirtyBits *bits,
                                                    gl::state::ExtendedDirtyBits *exBits,
                                                    gl::AttributesMask *dirtyCurrentValues)
{
    SetDirtyBitIfStateDifferent(a.program, b.program, bits, gl::state::DIRTY_BIT_PROGRAM_BINDING);
    SetDirtyBitIfStateDifferent(a.vao, b.vao, bits, gl::state::DIRTY_BIT_VERTEX_ARRAY_BINDING);
    SetDirtyBitIfStateDifferent(a.defaultVAOState, b.defaultVAOState, bits,
                                gl::state::DIRTY_BIT_VERTEX_ARRAY_BINDING);
    {
        gl::AttributesMask currentValueDiff = ComputeVertexAttribCurrentValueDiffMask(
            a.vertexAttribCurrentValues, b.vertexAttribCurrentValues);
        if (currentValueDiff.any())
        {
            *dirtyCurrentValues |= currentValueDiff;
            bits->set(gl::state::DIRTY_BIT_CURRENT_VALUES);
        }
    }
    for (gl::BufferBinding bufferBinding : angle::AllEnums<gl::BufferBinding>())
    {
        std::optional<gl::state::DirtyBitType> bit = GetBufferBindingDirtyBit(bufferBinding);
        if (!bit.has_value())
        {
            continue;
        }

        SetDirtyBitIfStateDifferent(a.buffers[bufferBinding], b.buffers[bufferBinding], bits,
                                    bit.value());
        SetDirtyBitIfStateDifferent(a.indexedBuffers[bufferBinding],
                                    b.indexedBuffers[bufferBinding], bits, bit.value());
    }
    // No dirty bit for ContextStateGL::activeTexture
    SetDirtyBitIfStateDifferent(a.textures, b.textures, bits,
                                gl::state::DIRTY_BIT_TEXTURE_BINDINGS);
    SetDirtyBitIfStateDifferent(a.samplers, b.samplers, bits,
                                gl::state::DIRTY_BIT_SAMPLER_BINDINGS);
    SetDirtyBitIfStateDifferent(a.images, b.images, bits, gl::state::DIRTY_BIT_IMAGE_BINDINGS);
    SetDirtyBitIfStateDifferent(a.transformFeedback, b.transformFeedback, bits,
                                gl::state::DIRTY_BIT_TRANSFORM_FEEDBACK_BINDING);
    SetDirtyBitIfStateDifferent(a.unpackState, b.unpackState, bits,
                                gl::state::DIRTY_BIT_UNPACK_STATE);
    SetDirtyBitIfStateDifferent(a.packState, b.packState, bits, gl::state::DIRTY_BIT_PACK_STATE);
    SetDirtyBitIfStateDifferent(a.framebuffers[angle::FramebufferBindingRead],
                                b.framebuffers[angle::FramebufferBindingRead], bits,
                                gl::state::DIRTY_BIT_READ_FRAMEBUFFER_BINDING);
    SetDirtyBitIfStateDifferent(a.framebuffers[angle::FramebufferBindingDraw],
                                b.framebuffers[angle::FramebufferBindingDraw], bits,
                                gl::state::DIRTY_BIT_DRAW_FRAMEBUFFER_BINDING);
    SetDirtyBitIfStateDifferent(a.renderbuffer, b.renderbuffer, bits,
                                gl::state::DIRTY_BIT_RENDERBUFFER_BINDING);
    SetDirtyBitIfStateDifferent(a.scissorTestEnabled, b.scissorTestEnabled, bits,
                                gl::state::DIRTY_BIT_SCISSOR_TEST_ENABLED);
    SetDirtyBitIfStateDifferent(a.scissor, b.scissor, bits, gl::state::DIRTY_BIT_SCISSOR);
    SetDirtyBitIfStateDifferent(a.viewport, b.viewport, bits, gl::state::DIRTY_BIT_VIEWPORT);
    SetDirtyBitIfStateDifferent(a.near, b.near, bits, gl::state::DIRTY_BIT_DEPTH_RANGE);
    SetDirtyBitIfStateDifferent(a.far, b.far, bits, gl::state::DIRTY_BIT_DEPTH_RANGE);
    SetDirtyBitIfStateDifferent(a.clipOrigin, b.clipOrigin, bits,
                                gl::state::DIRTY_BIT_CLIP_CONTROL);
    SetDirtyBitIfStateDifferent(a.clipDepthMode, b.clipDepthMode, bits,
                                gl::state::DIRTY_BIT_CLIP_CONTROL);
    SetDirtyBitIfStateDifferent(a.blendColor, b.blendColor, bits, gl::state::DIRTY_BIT_BLEND_COLOR);
    SetDirtyBitIfStateDifferent(a.blendState.getAllEnabledMask(), b.blendState.getAllEnabledMask(),
                                bits, gl::state::DIRTY_BIT_COLOR_MASK);
    SetDirtyBitIfStateDifferent(a.blendState.getColorMaskBits(), b.blendState.getColorMaskBits(),
                                bits, gl::state::DIRTY_BIT_BLEND_ENABLED);
    SetDirtyBitIfStateDifferent(a.blendState.getSrcColorBits(), b.blendState.getSrcColorBits(),
                                bits, gl::state::DIRTY_BIT_BLEND_FUNCS);
    SetDirtyBitIfStateDifferent(a.blendState.getDstColorBits(), b.blendState.getDstColorBits(),
                                bits, gl::state::DIRTY_BIT_BLEND_FUNCS);
    SetDirtyBitIfStateDifferent(a.blendState.getSrcAlphaBits(), b.blendState.getSrcAlphaBits(),
                                bits, gl::state::DIRTY_BIT_BLEND_FUNCS);
    SetDirtyBitIfStateDifferent(a.blendState.getDstAlphaBits(), b.blendState.getDstAlphaBits(),
                                bits, gl::state::DIRTY_BIT_BLEND_FUNCS);
    SetDirtyBitIfStateDifferent(a.blendState.getEquationColorBits(),
                                b.blendState.getEquationColorBits(), bits,
                                gl::state::DIRTY_BIT_BLEND_EQUATIONS);
    SetDirtyBitIfStateDifferent(a.blendState.getEquationAlphaBits(),
                                b.blendState.getEquationAlphaBits(), bits,
                                gl::state::DIRTY_BIT_BLEND_EQUATIONS);
    SetDirtyBitExtIfStateDifferent(a.blendAdvancedCoherent, b.blendAdvancedCoherent, bits, exBits,
                                   gl::state::EXTENDED_DIRTY_BIT_BLEND_ADVANCED_COHERENT);
    SetDirtyBitIfStateDifferent(a.sampleAlphaToCoverageEnabled, b.sampleAlphaToCoverageEnabled,
                                bits, gl::state::DIRTY_BIT_SAMPLE_ALPHA_TO_COVERAGE_ENABLED);
    SetDirtyBitIfStateDifferent(a.sampleCoverageEnabled, b.sampleCoverageEnabled, bits,
                                gl::state::DIRTY_BIT_SAMPLE_COVERAGE_ENABLED);
    SetDirtyBitIfStateDifferent(a.sampleCoverageValue, b.sampleCoverageValue, bits,
                                gl::state::DIRTY_BIT_SAMPLE_COVERAGE);
    SetDirtyBitIfStateDifferent(a.sampleCoverageInvert, b.sampleCoverageInvert, bits,
                                gl::state::DIRTY_BIT_SAMPLE_COVERAGE);
    SetDirtyBitIfStateDifferent(a.sampleMaskEnabled, b.sampleMaskEnabled, bits,
                                gl::state::DIRTY_BIT_SAMPLE_MASK_ENABLED);
    SetDirtyBitIfStateDifferent(a.sampleMaskValues, b.sampleMaskValues, bits,
                                gl::state::DIRTY_BIT_SAMPLE_MASK);
    SetDirtyBitIfStateDifferent(a.depthTestEnabled, b.depthTestEnabled, bits,
                                gl::state::DIRTY_BIT_DEPTH_TEST_ENABLED);
    SetDirtyBitIfStateDifferent(a.depthFunc, b.depthFunc, bits, gl::state::DIRTY_BIT_DEPTH_FUNC);
    SetDirtyBitIfStateDifferent(a.depthMask, b.depthMask, bits, gl::state::DIRTY_BIT_DEPTH_MASK);
    SetDirtyBitIfStateDifferent(a.stencilTestEnabled, b.stencilTestEnabled, bits,
                                gl::state::DIRTY_BIT_STENCIL_TEST_ENABLED);
    SetDirtyBitIfStateDifferent(a.stencilFrontFunc, b.stencilFrontFunc, bits,
                                gl::state::DIRTY_BIT_STENCIL_FUNCS_FRONT);
    SetDirtyBitIfStateDifferent(a.stencilFrontRef, b.stencilFrontRef, bits,
                                gl::state::DIRTY_BIT_STENCIL_FUNCS_FRONT);
    SetDirtyBitIfStateDifferent(a.stencilFrontValueMask, b.stencilFrontValueMask, bits,
                                gl::state::DIRTY_BIT_STENCIL_FUNCS_FRONT);
    SetDirtyBitIfStateDifferent(a.stencilFrontStencilFailOp, b.stencilFrontStencilFailOp, bits,
                                gl::state::DIRTY_BIT_STENCIL_OPS_FRONT);
    SetDirtyBitIfStateDifferent(a.stencilFrontStencilPassDepthFailOp,
                                b.stencilFrontStencilPassDepthFailOp, bits,
                                gl::state::DIRTY_BIT_STENCIL_OPS_FRONT);
    SetDirtyBitIfStateDifferent(a.stencilFrontStencilPassDepthPassOp,
                                b.stencilFrontStencilPassDepthPassOp, bits,
                                gl::state::DIRTY_BIT_STENCIL_OPS_FRONT);
    SetDirtyBitIfStateDifferent(a.stencilFrontWritemask, b.stencilFrontWritemask, bits,
                                gl::state::DIRTY_BIT_STENCIL_WRITEMASK_FRONT);
    SetDirtyBitIfStateDifferent(a.stencilBackFunc, b.stencilBackFunc, bits,
                                gl::state::DIRTY_BIT_STENCIL_FUNCS_BACK);
    SetDirtyBitIfStateDifferent(a.stencilBackRef, b.stencilBackRef, bits,
                                gl::state::DIRTY_BIT_STENCIL_FUNCS_BACK);
    SetDirtyBitIfStateDifferent(a.stencilBackValueMask, b.stencilBackValueMask, bits,
                                gl::state::DIRTY_BIT_STENCIL_FUNCS_BACK);
    SetDirtyBitIfStateDifferent(a.stencilBackStencilFailOp, b.stencilBackStencilFailOp, bits,
                                gl::state::DIRTY_BIT_STENCIL_OPS_BACK);
    SetDirtyBitIfStateDifferent(a.stencilBackStencilPassDepthFailOp,
                                b.stencilBackStencilPassDepthFailOp, bits,
                                gl::state::DIRTY_BIT_STENCIL_OPS_BACK);
    SetDirtyBitIfStateDifferent(a.stencilBackStencilPassDepthPassOp,
                                b.stencilBackStencilPassDepthPassOp, bits,
                                gl::state::DIRTY_BIT_STENCIL_OPS_BACK);
    SetDirtyBitIfStateDifferent(a.stencilBackWritemask, b.stencilBackWritemask, bits,
                                gl::state::DIRTY_BIT_STENCIL_WRITEMASK_BACK);
    SetDirtyBitIfStateDifferent(a.cullFaceEnabled, b.cullFaceEnabled, bits,
                                gl::state::DIRTY_BIT_CULL_FACE_ENABLED);
    SetDirtyBitIfStateDifferent(a.cullFace, b.cullFace, bits, gl::state::DIRTY_BIT_CULL_FACE);
    SetDirtyBitIfStateDifferent(a.frontFace, b.frontFace, bits, gl::state::DIRTY_BIT_FRONT_FACE);
    SetDirtyBitExtIfStateDifferent(a.polygonMode, b.polygonMode, bits, exBits,
                                   gl::state::EXTENDED_DIRTY_BIT_POLYGON_MODE);
    SetDirtyBitExtIfStateDifferent(a.polygonOffsetPointEnabled, b.polygonOffsetPointEnabled, bits,
                                   exBits,
                                   gl::state::EXTENDED_DIRTY_BIT_POLYGON_OFFSET_POINT_ENABLED);
    SetDirtyBitExtIfStateDifferent(a.polygonOffsetLineEnabled, b.polygonOffsetLineEnabled, bits,
                                   exBits,
                                   gl::state::EXTENDED_DIRTY_BIT_POLYGON_OFFSET_LINE_ENABLED);
    SetDirtyBitIfStateDifferent(a.polygonOffsetFillEnabled, b.polygonOffsetFillEnabled, bits,
                                gl::state::DIRTY_BIT_POLYGON_OFFSET_FILL_ENABLED);
    SetDirtyBitIfStateDifferent(a.polygonOffsetFactor, b.polygonOffsetFactor, bits,
                                gl::state::DIRTY_BIT_POLYGON_OFFSET);
    SetDirtyBitIfStateDifferent(a.polygonOffsetUnits, b.polygonOffsetUnits, bits,
                                gl::state::DIRTY_BIT_POLYGON_OFFSET);
    SetDirtyBitIfStateDifferent(a.polygonOffsetClamp, b.polygonOffsetClamp, bits,
                                gl::state::DIRTY_BIT_POLYGON_OFFSET);
    SetDirtyBitExtIfStateDifferent(a.depthClampEnabled, b.depthClampEnabled, bits, exBits,
                                   gl::state::EXTENDED_DIRTY_BIT_DEPTH_CLAMP_ENABLED);
    SetDirtyBitIfStateDifferent(a.rasterizerDiscardEnabled, b.rasterizerDiscardEnabled, bits,
                                gl::state::DIRTY_BIT_RASTERIZER_DISCARD_ENABLED);
    SetDirtyBitIfStateDifferent(a.lineWidth, b.lineWidth, bits, gl::state::DIRTY_BIT_LINE_WIDTH);
    SetDirtyBitIfStateDifferent(a.primitiveRestartFixedIndexEnabled,
                                b.primitiveRestartFixedIndexEnabled, bits,
                                gl::state::DIRTY_BIT_PRIMITIVE_RESTART_ENABLED);
    SetDirtyBitIfStateDifferent(a.primitiveRestartEnabled, b.primitiveRestartEnabled, bits,
                                gl::state::DIRTY_BIT_PRIMITIVE_RESTART_ENABLED);
    SetDirtyBitIfStateDifferent(a.primitiveRestartIndex, b.primitiveRestartIndex, bits,
                                gl::state::DIRTY_BIT_PRIMITIVE_RESTART_ENABLED);
    SetDirtyBitIfStateDifferent(a.clearColor, b.clearColor, bits, gl::state::DIRTY_BIT_CLEAR_COLOR);
    SetDirtyBitIfStateDifferent(a.clearDepth, b.clearDepth, bits, gl::state::DIRTY_BIT_CLEAR_DEPTH);
    SetDirtyBitIfStateDifferent(a.clearStencil, b.clearStencil, bits,
                                gl::state::DIRTY_BIT_CLEAR_STENCIL);
    SetDirtyBitIfStateDifferent(a.framebufferSRGBEnabled, b.framebufferSRGBEnabled, bits,
                                gl::state::DIRTY_BIT_FRAMEBUFFER_SRGB_WRITE_CONTROL_MODE);
    SetDirtyBitIfStateDifferent(a.ditherEnabled, b.ditherEnabled, bits,
                                gl::state::DIRTY_BIT_DITHER_ENABLED);
    // No dirty bit for ContextStateGL::textureCubemapSeamlessEnabled
    SetDirtyBitIfStateDifferent(a.multisamplingEnabled, b.multisamplingEnabled, bits,
                                gl::state::DIRTY_BIT_MULTISAMPLING);
    SetDirtyBitIfStateDifferent(a.sampleAlphaToOneEnabled, b.sampleAlphaToOneEnabled, bits,
                                gl::state::DIRTY_BIT_SAMPLE_ALPHA_TO_ONE);
    SetDirtyBitIfStateDifferent(a.provokingVertex, b.provokingVertex, bits,
                                gl::state::DIRTY_BIT_PROVOKING_VERTEX);
    SetDirtyBitExtIfStateDifferent(a.enabledClipDistances, b.enabledClipDistances, bits, exBits,
                                   gl::state::EXTENDED_DIRTY_BIT_CLIP_DISTANCES);
    SetDirtyBitExtIfStateDifferent(a.logicOpEnabled, b.logicOpEnabled, bits, exBits,
                                   gl::state::EXTENDED_DIRTY_BIT_LOGIC_OP_ENABLED);
    SetDirtyBitExtIfStateDifferent(a.logicOp, b.logicOp, bits, exBits,
                                   gl::state::EXTENDED_DIRTY_BIT_LOGIC_OP);
}

auto TieIndexedBufferBindingGL(const IndexedBufferBindingGL &binding)
{
    return std::tie(binding.offset, binding.size, binding.buffer);
}

auto TieImageUnitBindingGL(const ImageUnitBindingGL &binding)
{
    return std::tie(binding.texture, binding.level, binding.layered, binding.layer, binding.access,
                    binding.format);
}

auto TieContextStateGL(const ContextStateGL &state)
{
    // state.vertexAttribCurrentValues is omitted and handled specially in the comparison operator
    // state.Stencil(Front|Back)(WriteMask|ValueMask) is omitted and handled specifically in the
    // comparison operator because it must be masked
    return std::tie(
        state.program, state.vao, state.defaultVAOState,
        /*state.vertexAttribCurrentValues,*/ state.buffers, state.indexedBuffers,
        state.textureUnitIndex, state.textures, state.samplers, state.images,
        state.transformFeedback, state.unpackState, state.packState, state.framebuffers,
        state.renderbuffer, state.scissorTestEnabled, state.scissor, state.viewport, state.near,
        state.far, state.clipOrigin, state.clipDepthMode, state.blendColor, state.blendState,
        state.blendAdvancedCoherent, state.sampleAlphaToCoverageEnabled,
        state.sampleCoverageEnabled, state.sampleCoverageValue, state.sampleCoverageInvert,
        state.sampleMaskEnabled, state.sampleMaskValues, state.depthTestEnabled, state.depthFunc,
        state.depthMask, state.stencilTestEnabled, state.stencilFrontFunc, state.stencilFrontRef,
        /*state.stencilFrontValueMask,*/ state.stencilFrontStencilFailOp,
        state.stencilFrontStencilPassDepthFailOp, state.stencilFrontStencilPassDepthPassOp,
        /*state.stencilFrontWritemask,*/ state.stencilBackFunc, state.stencilBackRef,
        /*state.stencilBackValueMask,*/ state.stencilBackStencilFailOp,
        state.stencilBackStencilPassDepthFailOp, state.stencilBackStencilPassDepthPassOp,
        /*state.stencilBackWritemask,*/ state.cullFaceEnabled, state.cullFace, state.frontFace,
        state.polygonMode, state.polygonOffsetPointEnabled, state.polygonOffsetLineEnabled,
        state.polygonOffsetFillEnabled, state.polygonOffsetFactor, state.polygonOffsetUnits,
        state.polygonOffsetClamp, state.depthClampEnabled, state.rasterizerDiscardEnabled,
        state.lineWidth, state.primitiveRestartFixedIndexEnabled, state.primitiveRestartEnabled,
        state.primitiveRestartIndex, state.clearColor, state.clearDepth, state.clearStencil,
        state.framebufferSRGBEnabled, state.ditherEnabled, state.textureCubemapSeamlessEnabled,
        state.multisamplingEnabled, state.sampleAlphaToOneEnabled, state.provokingVertex,
        state.enabledClipDistances, state.logicOpEnabled, state.logicOp);
}

void Indent(std::ostream &os, size_t count)
{
    for (size_t indent = 0; indent < count; indent++)
    {
        os << " ";
    }
}

template <typename T>
void PrintCompressedArray(std::ostream &os,
                          const T &values,
                          size_t indentation,
                          bool wrapValueInParens)
{
    for (size_t i = 0; i < values.size(); i++)
    {
        size_t start = i;
        while (i + 1 < values.size() && values[i + 1] == values[i])
        {
            i++;
        }

        Indent(os, indentation);
        os << "[" << start;
        if (i > start)
        {
            os << ".." << i;
        }
        os << "] = ";
        if (wrapValueInParens)
        {
            os << "(";
        }
        os << values[i];
        if (wrapValueInParens)
        {
            os << ")";
        }
        os << std::endl;
    }
}

std::string PrintIndexedBlendState(const gl::BlendStateExt &blendState, size_t index)
{
    std::ostringstream os;
    os << "enabled = " << blendState.getEnabledMask().test(index) << ", ";
    bool r, g, b, a;
    blendState.getColorMaskIndexed(index, &r, &g, &b, &a);
    os << "mask = " << (r ? "R" : "_") << (g ? "G" : "_") << (b ? "B" : "_") << (a ? "A" : "_")
       << ",";
    os << "colorMode = " << blendState.getEquationColorIndexed(index) << ", ";
    os << "alphaMode = " << blendState.getEquationAlphaIndexed(index) << ", ";
    os << "srcColor = " << blendState.getSrcColorIndexed(index) << ", ";
    os << "dstColor = " << blendState.getDstColorIndexed(index) << ", ";
    os << "srcAlpha = " << blendState.getSrcAlphaIndexed(index) << ", ";
    os << "dstAlpha = " << blendState.getDstAlphaIndexed(index);
    return os.str();
}

void PrintCompressedBlendState(std::ostream &os,
                               const gl::BlendStateExt &blendState,
                               size_t indentation)
{
    for (size_t i = 0; i < blendState.getDrawBufferCount(); i++)
    {
        std::string printed = PrintIndexedBlendState(blendState, i);

        size_t start = i;
        while (i + 1 < blendState.getDrawBufferCount() &&
               PrintIndexedBlendState(blendState, i + 1) == printed)
        {
            i++;
        }

        Indent(os, indentation);
        os << "[" << start;
        if (i > start)
        {
            os << ".." << i;
        }
        os << "] = (" << printed << ")" << std::endl;
    }
}

void PrintCompressedVertexArrayState(std::ostream &os,
                                     const VertexArrayStateGL &state,
                                     size_t indentation)
{
    Indent(os, indentation);
    os << "elementArrayBuffer = " << state.elementArrayBuffer << std::endl;
    Indent(os, indentation);
    os << "attributes =" << std::endl;
    PrintCompressedArray(os, state.attributes, indentation + 4, true);
    Indent(os, indentation);
    os << "bindings =" << std::endl;
    PrintCompressedArray(os, state.bindings, indentation + 4, true);
}
}  // anonymous namespace

std::ostream &operator<<(std::ostream &os, const VertexAttributeGL &attribute)
{
    os << "enabled = " << attribute.enabled << ", size = " << attribute.format->channelCount
       << ", type = " << attribute.format->vertexAttribType
       << ", normalized = " << attribute.format->isNorm() << ", pointer = " << attribute.pointer
       << ", relativeOffset = " << attribute.relativeOffset
       << ", bindingIndex = " << attribute.bindingIndex;
    return os;
}

std::ostream &operator<<(std::ostream &os, const VertexBindingGL &binding)
{
    os << "stride = " << binding.stride << ", divisor = " << binding.divisor
       << ", offset = " << binding.offset << ", buffer = " << binding.buffer;
    return os;
}

VertexArrayStateGL::VertexArrayStateGL(size_t maxAttribs, size_t maxBindings)
    : attributes(std::min<size_t>(maxAttribs, gl::MAX_VERTEX_ATTRIBS)),
      bindings(std::min<size_t>(maxBindings, gl::MAX_VERTEX_ATTRIBS))
{
    // Set the cached vertex attribute array and vertex attribute binding array size
    for (GLuint i = 0; i < attributes.size(); i++)
    {
        attributes[i].bindingIndex = i;
    }
}

std::ostream &operator<<(std::ostream &os, const VertexArrayStateGL &state)
{
    PrintCompressedVertexArrayState(os, state, 0);
    return os;
}

void QueryVertexArrayStateGL(const FunctionsGL *functions, VertexArrayStateGL *state)
{
    GetHelper(functions, GL_ELEMENT_ARRAY_BUFFER_BINDING, &state->elementArrayBuffer);
    GLint maxVertexAttribs = 0;
    GetHelper(functions, GL_MAX_VERTEX_ATTRIBS, &maxVertexAttribs);
    maxVertexAttribs = std::min(maxVertexAttribs, static_cast<GLint>(gl::MAX_VERTEX_ATTRIBS));

    for (GLint i = 0; i < maxVertexAttribs; i++)
    {
        VertexAttributeGL &attrib = state->attributes[i];

        GetVertexHelper(functions, i, GL_VERTEX_ATTRIB_ARRAY_ENABLED, &attrib.enabled);

        GLuint size = attrib.format->channelCount;
        GetVertexHelper(functions, i, GL_VERTEX_ATTRIB_ARRAY_SIZE, &size);

        gl::VertexAttribType type = attrib.format->vertexAttribType;
        GetVertexHelper(functions, i, GL_VERTEX_ATTRIB_ARRAY_TYPE, &type);

        bool normalized = attrib.format->isNorm();
        GetVertexHelper(functions, i, GL_VERTEX_ATTRIB_ARRAY_NORMALIZED, &normalized);

        bool integer = attrib.format->isPureInt();
        GetVertexHelper(functions, i, GL_VERTEX_ATTRIB_ARRAY_INTEGER, &integer);

        attrib.format = &angle::Format::Get(gl::GetVertexFormatID(type, normalized, size, integer));

        GetVertexHelper(functions, i, GL_VERTEX_ATTRIB_ARRAY_POINTER,
                        const_cast<void **>(&attrib.pointer));

        if (nativegl::SupportsVertexAttributeBindings(functions))
        {
            GetVertexHelper(functions, i, GL_VERTEX_ATTRIB_RELATIVE_OFFSET, &attrib.relativeOffset);
            GetVertexHelper(functions, i, GL_VERTEX_ATTRIB_BINDING, &attrib.bindingIndex);
        }
        else
        {
            ASSERT(attrib.bindingIndex == static_cast<GLuint>(i));
        }
    }

    GLint maxVertexAttribBindings = maxVertexAttribs;
    if (nativegl::SupportsVertexAttributeBindings(functions))
    {
        GetHelper(functions, GL_MAX_VERTEX_ATTRIB_BINDINGS, &maxVertexAttribBindings);
        maxVertexAttribBindings =
            std::min(maxVertexAttribBindings, static_cast<GLint>(gl::MAX_VERTEX_ATTRIB_BINDINGS));
    }

    const bool supportsInstancing = nativegl::SupportsInstancing(functions);

    for (GLint i = 0; i < maxVertexAttribBindings; i++)
    {
        VertexBindingGL &binding = state->bindings[i];

        if (nativegl::SupportsVertexAttributeBindings(functions))
        {
            GetHelper(functions, GL_VERTEX_BINDING_STRIDE, i, &binding.stride);
            ASSERT(supportsInstancing);
            GetHelper(functions, GL_VERTEX_BINDING_DIVISOR, i, &binding.divisor);
            GetHelper(functions, GL_VERTEX_BINDING_OFFSET, i, &binding.offset);
            GetHelper(functions, GL_VERTEX_BINDING_BUFFER, i, &binding.buffer);
        }
        else
        {
            const VertexAttributeGL &attrib = state->attributes[i];

            GetVertexHelper(functions, i, GL_VERTEX_ATTRIB_ARRAY_STRIDE, &binding.stride);
            if (supportsInstancing)
            {
                GetVertexHelper(functions, i, GL_VERTEX_ATTRIB_ARRAY_DIVISOR, &binding.divisor);
            }
            binding.offset = reinterpret_cast<GLintptr>(attrib.pointer);
            GetVertexHelper(functions, i, GL_VERTEX_ATTRIB_ARRAY_BUFFER_BINDING, &binding.buffer);
        }

        // Driver return inconsistent results when querying the binding offset when client data
        // is used. The spec says glVertexAttribPointer on the default VAO with no buffer bound
        // should set an offset of 0.
        if (binding.buffer == 0)
        {
            binding.offset = 0;
        }
    }
}

bool operator==(const IndexedBufferBindingGL &a, const IndexedBufferBindingGL &b)
{
    return TieIndexedBufferBindingGL(a) == TieIndexedBufferBindingGL(b);
}

std::ostream &operator<<(std::ostream &os, const IndexedBufferBindingGL &binding)
{
    os << "offset = " << binding.offset << ", size = " << binding.size
       << ", buffer = " << binding.buffer;
    return os;
}

ImageUnitBindingGL::ImageUnitBindingGL(GLenum defaultFormat) : format(defaultFormat) {}

bool operator==(const ImageUnitBindingGL &a, const ImageUnitBindingGL &b)
{
    return TieImageUnitBindingGL(a) == TieImageUnitBindingGL(b);
}

std::ostream &operator<<(std::ostream &os, const ImageUnitBindingGL &binding)
{
    os << "texture = " << binding.texture << ", level = " << binding.level
       << ", layered = " << gl::ConvertToBool(binding.layered) << ", layer = " << binding.layer
       << ", access = " << gl::FmtHex(binding.access)
       << ", format = " << gl::FmtHex(binding.format);
    return os;
}

ContextStateGLCaps::ContextStateGLCaps(const FunctionsGL *functions, const gl::Caps &caps)
    : defaultFramebufferSrgbState(functions->standard == STANDARD_GL_ES),
      defaultImageBindingFormat(functions->standard == STANDARD_GL_ES ? GL_R32UI : GL_R8),
      maxVertexAttributes(
          std::min(caps.maxVertexAttributes, static_cast<GLint>(gl::MAX_VERTEX_ATTRIBS))),
      maxVertexAttribBindings(std::min(caps.maxVertexAttribBindings,
                                       static_cast<GLint>(gl::MAX_VERTEX_ATTRIB_BINDINGS))),
      maxImageUnits(
          std::min(caps.maxImageUnits, static_cast<GLint>(gl::IMPLEMENTATION_MAX_IMAGE_UNITS))),
      maxDrawBuffers(
          std::min(caps.maxDrawBuffers, static_cast<GLint>(gl::IMPLEMENTATION_MAX_DRAW_BUFFERS))),
      maxUniformBufferBindings(caps.maxUniformBufferBindings),
      maxAtomicCounterBufferBindings(caps.maxAtomicCounterBufferBindings),
      maxShaderStorageBufferBindings(caps.maxShaderStorageBufferBindings)
{}

ContextStateGL::ContextStateGL(const ContextStateGLCaps &caps)
    : defaultVAOState(caps.maxVertexAttributes, caps.maxVertexAttribBindings),
      images(caps.maxImageUnits, ImageUnitBindingGL(caps.defaultImageBindingFormat)),
      blendState(caps.maxDrawBuffers),
      framebufferSRGBEnabled(caps.defaultFramebufferSrgbState)
{
    indexedBuffers[gl::BufferBinding::Uniform].resize(caps.maxUniformBufferBindings);
    indexedBuffers[gl::BufferBinding::AtomicCounter].resize(caps.maxAtomicCounterBufferBindings);
    indexedBuffers[gl::BufferBinding::ShaderStorage].resize(caps.maxShaderStorageBufferBindings);

    sampleMaskValues.fill(~GLbitfield(0));
}

bool operator==(const ContextStateGL &a, const ContextStateGL &b)
{
    if (TieContextStateGL(a) != TieContextStateGL(b))
    {
        return false;
    }

    if (ComputeVertexAttribCurrentValueDiffMask(a.vertexAttribCurrentValues,
                                                b.vertexAttribCurrentValues)
            .any())
    {
        return false;
    }

    auto makeStencilMaskTuple = [](const ContextStateGL &state) {
        return std::make_tuple(
            state.stencilFrontValueMask & 0xFF, state.stencilFrontWritemask & 0xFF,
            state.stencilBackValueMask & 0xFF, state.stencilBackWritemask & 0xFF);
    };
    if (makeStencilMaskTuple(a) != makeStencilMaskTuple(b))
    {
        return false;
    }

    return true;
}

bool operator!=(const ContextStateGL &a, const ContextStateGL &b)
{
    return !(a == b);
}

std::ostream &operator<<(std::ostream &os, const ContextStateGL &state)
{
    os << "program = " << state.program << std::endl;
    os << "vao = " << state.vao << std::endl;
    os << "defaultVAOState =" << std::endl;
    PrintCompressedVertexArrayState(os, state.defaultVAOState, 4);
    os << "vertexAttribCurrentValues =" << std::endl;
    PrintCompressedArray(os, state.vertexAttribCurrentValues, 4, true);
    os << "buffers =" << std::endl;
    for (gl::BufferBinding bufferBinding : angle::AllEnums<gl::BufferBinding>())
    {
        os << "    [" << bufferBinding << "] = " << state.buffers[bufferBinding] << std::endl;
    }
    os << "indexedBuffers =" << std::endl;
    for (gl::BufferBinding bufferBinding : angle::AllEnums<gl::BufferBinding>())
    {
        const std::vector<IndexedBufferBindingGL> &buffers = state.indexedBuffers[bufferBinding];
        if (buffers.empty())
        {
            continue;
        }
        os << "    [" << bufferBinding << "] =" << std::endl;
        PrintCompressedArray(os, buffers, 8, true);
    }
    os << "textureUnitIndex = " << state.textureUnitIndex << std::endl;
    os << "textures =" << std::endl;
    for (gl::TextureType textureType : angle::AllEnums<gl::TextureType>())
    {
        os << "    [" << textureType << "] =" << std::endl;
        PrintCompressedArray(os, state.textures[textureType], 8, false);
    }
    os << "samplers =" << std::endl;
    PrintCompressedArray(os, state.samplers, 4, false);
    os << "images =" << std::endl;
    PrintCompressedArray(os, state.images, 4, true);
    os << "transformFeedback = " << state.transformFeedback << std::endl;
    os << "unpackState = (" << state.unpackState << ")" << std::endl;
    os << "packState = (" << state.packState << ")" << std::endl;
    os << "framebuffers =" << std::endl;
    os << "    [GL_READ_FRAMEBUFFER] = " << state.framebuffers[angle::FramebufferBindingRead]
       << std::endl;
    os << "    [GL_DRAW_FRAMEBUFFER] = " << state.framebuffers[angle::FramebufferBindingDraw]
       << std::endl;
    os << "renderbuffer = " << state.renderbuffer << std::endl;
    os << "scissorTestEnabled = " << state.scissorTestEnabled << std::endl;
    os << "scissor = (" << state.scissor << ")" << std::endl;
    os << "viewport = (" << state.viewport << ")" << std::endl;
    os << "near = " << state.near << std::endl;
    os << "far = " << state.far << std::endl;
    os << "clipOrigin = " << state.clipOrigin << std::endl;
    os << "clipDepthMode = " << state.clipDepthMode << std::endl;
    os << "blendColor = (" << state.blendColor << ")" << std::endl;
    os << "blendState =" << std::endl;
    PrintCompressedBlendState(os, state.blendState, 4);
    os << "blendAdvancedCoherent = " << state.blendAdvancedCoherent << std::endl;
    os << "sampleAlphaToCoverageEnabled = " << state.sampleAlphaToCoverageEnabled << std::endl;
    os << "sampleCoverageEnabled = " << state.sampleCoverageEnabled << std::endl;
    os << "sampleCoverageValue = " << state.sampleCoverageValue << std::endl;
    os << "sampleCoverageInvert = " << state.sampleCoverageInvert << std::endl;
    os << "sampleMaskEnabled = " << state.sampleMaskEnabled << std::endl;
    os << "sampleMaskValues =" << std::endl;
    PrintCompressedArray(os, state.sampleMaskValues, 4, false);
    os << "depthTestEnabled = " << state.depthTestEnabled << std::endl;
    os << "depthFunc = " << gl::FmtHex(state.depthFunc) << std::endl;
    os << "depthMask = " << state.depthMask << std::endl;
    os << "stencilTestEnabled = " << state.stencilTestEnabled << std::endl;
    os << "stencilFrontFunc = " << gl::FmtHex(state.stencilFrontFunc) << std::endl;
    os << "stencilFrontRef = " << state.stencilFrontRef << std::endl;
    os << "stencilFrontValueMask = " << gl::FmtHex(state.stencilFrontValueMask) << std::endl;
    os << "stencilFrontStencilFailOp = " << gl::FmtHex(state.stencilFrontStencilFailOp)
       << std::endl;
    os << "stencilFrontStencilPassDepthFailOp = "
       << gl::FmtHex(state.stencilFrontStencilPassDepthFailOp) << std::endl;
    os << "stencilFrontStencilPassDepthPassOp = "
       << gl::FmtHex(state.stencilFrontStencilPassDepthPassOp) << std::endl;
    os << "stencilFrontWritemask = " << gl::FmtHex(state.stencilFrontWritemask) << std::endl;
    os << "stencilBackFunc = " << gl::FmtHex(state.stencilBackFunc) << std::endl;
    os << "stencilBackRef = " << state.stencilBackRef << std::endl;
    os << "stencilBackValueMask = " << gl::FmtHex(state.stencilBackValueMask) << std::endl;
    os << "stencilBackStencilFailOp = " << gl::FmtHex(state.stencilBackStencilFailOp) << std::endl;
    os << "stencilBackStencilPassDepthFailOp = "
       << gl::FmtHex(state.stencilBackStencilPassDepthFailOp) << std::endl;
    os << "stencilBackStencilPassDepthPassOp = "
       << gl::FmtHex(state.stencilBackStencilPassDepthPassOp) << std::endl;
    os << "stencilBackWritemask = " << gl::FmtHex(state.stencilBackWritemask) << std::endl;
    os << "cullFaceEnabled = " << state.cullFaceEnabled << std::endl;
    os << "cullFace = " << state.cullFace << std::endl;
    os << "frontFace = " << gl::FmtHex(state.frontFace) << std::endl;
    os << "polygonMode = " << state.polygonMode << std::endl;
    os << "polygonOffsetPointEnabled = " << state.polygonOffsetPointEnabled << std::endl;
    os << "polygonOffsetLineEnabled = " << state.polygonOffsetLineEnabled << std::endl;
    os << "polygonOffsetFillEnabled = " << state.polygonOffsetFillEnabled << std::endl;
    os << "polygonOffsetFactor = " << state.polygonOffsetFactor << std::endl;
    os << "polygonOffsetUnits = " << state.polygonOffsetUnits << std::endl;
    os << "polygonOffsetClamp = " << state.polygonOffsetClamp << std::endl;
    os << "depthClampEnabled = " << state.depthClampEnabled << std::endl;
    os << "rasterizerDiscardEnabled = " << state.rasterizerDiscardEnabled << std::endl;
    os << "lineWidth = " << state.lineWidth << std::endl;
    os << "primitiveRestartFixedIndexEnabled = " << state.primitiveRestartFixedIndexEnabled
       << std::endl;
    os << "primitiveRestartEnabled = " << state.primitiveRestartEnabled << std::endl;
    os << "primitiveRestartIndex = " << state.primitiveRestartIndex << std::endl;
    os << "clearColor = " << state.clearColor << std::endl;
    os << "clearDepth = " << state.clearDepth << std::endl;
    os << "clearStencil = " << state.clearStencil << std::endl;
    os << "framebufferSRGBEnabled = " << state.framebufferSRGBEnabled << std::endl;
    os << "ditherEnabled = " << state.ditherEnabled << std::endl;
    os << "textureCubemapSeamlessEnabled = " << state.textureCubemapSeamlessEnabled << std::endl;
    os << "multisamplingEnabled = " << state.multisamplingEnabled << std::endl;
    os << "sampleAlphaToOneEnabled = " << state.sampleAlphaToOneEnabled << std::endl;
    os << "provokingVertex = " << gl::FmtHex(state.provokingVertex) << std::endl;
    os << "enabledClipDistances =" << std::endl;
    PrintCompressedArray(os, state.enabledClipDistances, 4, false);
    os << "logicOpEnabled = " << state.logicOpEnabled << std::endl;
    os << "logicOp = " << state.logicOp << std::endl;
    return os;
}

StateManagerGL::StateManagerGL(const FunctionsGL *functions,
                               const gl::Caps &rendererCaps,
                               const gl::Extensions &extensions,
                               const angle::FeaturesGL &features)
    : mFunctions(functions),
      mFeatures(features),
      mCaps(functions, rendererCaps),
      mState(mCaps),
      mSupportsVertexArrayObjects(nativegl::SupportsVertexArrayObjects(functions)),
      mCurrentTransformFeedback(nullptr),
      mQueries(),
      mPrevDrawContext({0}),
      mIndependentBlendStates(extensions.drawBuffersIndexedAny()),
      mSampleCoverageEverChanged(false),
      mHasUnflushedQueries(false),
      mFramebufferSRGBAvailable(extensions.sRGBWriteControlEXT),
      mHasSeparateFramebufferBindings(nativegl::SupportsSeparateFramebufferBindings(functions)),
      mIsMultiviewEnabled(extensions.multiviewOVR),
      mMaxClipDistances(rendererCaps.maxClipDistances)
{
    ASSERT(mFunctions);
    ASSERT(rendererCaps.maxViews >= 1u);

    mQueries.fill(nullptr);
    mTemporaryPausedQueries.fill(nullptr);

    // Initialize point sprite state for desktop GL
    if (mFunctions->standard == STANDARD_GL_DESKTOP)
    {
        mFunctions->enable(GL_PROGRAM_POINT_SIZE);

        // GL_POINT_SPRITE was deprecated in the core profile. Point rasterization is always
        // performed
        // as though POINT_SPRITE were enabled.
        if ((mFunctions->profile & GL_CONTEXT_CORE_PROFILE_BIT) == 0)
        {
            mFunctions->enable(GL_POINT_SPRITE);
        }
    }

    if (features.emulatePrimitiveRestartFixedIndex.enabled)
    {
        // There is no consistent default value for primitive restart index. Set it to UINT -1.
        constexpr GLuint primitiveRestartIndex = gl::GetPrimitiveRestartIndexFromType<GLuint>();
        mFunctions->primitiveRestartIndex(primitiveRestartIndex);
        mState.primitiveRestartIndex = primitiveRestartIndex;
    }

    // It's possible we've enabled the emulated VAO feature for testing but we're on a core profile.
    // Use a generated VAO as the default VAO so we can still test.
    if ((features.syncAllVertexArraysToDefault.enabled ||
         features.syncDefaultVertexArraysToDefault.enabled) &&
        !nativegl::CanUseDefaultVertexArrayObject(mFunctions))
    {
        ASSERT(nativegl::SupportsVertexArrayObjects(mFunctions));
        mFunctions->genVertexArrays(1, &mDefaultVAO);
        mFunctions->bindVertexArray(mDefaultVAO);
        mState.vao = mDefaultVAO;
    }

    // By default, desktop GL clamps values read from normalized
    // color buffers to [0, 1], which does not match expected ES
    // behavior for signed normalized color buffers.
    if (mFunctions->clampColor)
    {
        mFunctions->clampColor(GL_CLAMP_READ_COLOR, GL_FALSE);
    }

    // The initial viewport and scissor state is based on the surface that this context was first
    // made current with. Query it back.
    GetHelper(mFunctions, GL_SCISSOR_BOX, &mState.scissor);
    GetHelper(mFunctions, GL_VIEWPORT, &mState.viewport);
}

StateManagerGL::~StateManagerGL()
{
    if (!mDeferredBufferDeletions.empty())
    {
        mFunctions->finish();
        onFinish();
    }

    if (mPlaceholderFbo != 0)
    {
        deleteFramebuffer(mPlaceholderFbo);
    }
    if (mPlaceholderFboColorRenderbuffer != 0)
    {
        deleteRenderbuffer(mPlaceholderFboColorRenderbuffer);
    }
    if (mPlaceholderFboDepthStencilRenderbuffer != 0)
    {
        deleteRenderbuffer(mPlaceholderFboDepthStencilRenderbuffer);
    }

    if (mDefaultVAO != 0)
    {
        mFunctions->deleteVertexArrays(1, &mDefaultVAO);
    }
}

void StateManagerGL::deleteProgram(GLuint program)
{
    if (program != 0)
    {
        if (mState.program == program)
        {
            useProgram(0);
        }

        mFunctions->deleteProgram(program);
    }
}

void StateManagerGL::deleteVertexArray(GLuint vao)
{
    if (vao != 0)
    {
        if (mState.vao == vao)
        {
            bindVertexArray(0);
        }
        mFunctions->deleteVertexArrays(1, &vao);
        mVAOStates.erase(vao);
    }
}

void StateManagerGL::deleteTexture(GLuint texture)
{
    if (texture != 0)
    {
        for (gl::TextureType type : angle::AllEnums<gl::TextureType>())
        {
            const auto &textureVector = mState.textures[type];
            for (size_t textureUnitIndex = 0; textureUnitIndex < textureVector.size();
                 textureUnitIndex++)
            {
                if (textureVector[textureUnitIndex] == texture)
                {
                    activeTexture(textureUnitIndex);
                    bindTexture(type, 0);
                }
            }
        }

        for (size_t imageUnitIndex = 0; imageUnitIndex < mState.images.size(); imageUnitIndex++)
        {
            if (mState.images[imageUnitIndex].texture == texture)
            {
                bindImageTexture(imageUnitIndex, 0, 0, false, 0, GL_READ_ONLY, GL_R32UI);
            }
        }

        mFunctions->deleteTextures(1, &texture);
    }
}

void StateManagerGL::deleteSampler(GLuint sampler)
{
    if (sampler != 0)
    {
        for (size_t unit = 0; unit < mState.samplers.size(); unit++)
        {
            if (mState.samplers[unit] == sampler)
            {
                bindSampler(unit, 0);
            }
        }

        mFunctions->deleteSamplers(1, &sampler);
    }
}

void StateManagerGL::deleteBuffer(GLuint buffer, size_t clientSpecifiedSize, bool isHardenedContext)
{
    if (buffer == 0)
    {
        return;
    }

    for (auto target : angle::AllEnums<gl::BufferBinding>())
    {
        if (mState.buffers[target] == buffer)
        {
            bindBuffer(target, 0, isHardenedContext);
        }

        auto &indexedTarget = mState.indexedBuffers[target];
        for (size_t bindIndex = 0; bindIndex < indexedTarget.size(); ++bindIndex)
        {
            if (indexedTarget[bindIndex].buffer == buffer)
            {
                bindBufferBase(target, bindIndex, 0);
            }
        }
    }

    VertexArrayStateGL *vaoState = getCurrentVAOState();
    if (vaoState)
    {
        if (vaoState->elementArrayBuffer == buffer)
        {
            vaoState->elementArrayBuffer = 0;
        }

        for (VertexBindingGL &binding : vaoState->bindings)
        {
            if (binding.buffer == buffer)
            {
                binding.buffer = 0;
                binding.offset = 0;
            }
        }
    }

    if (!isHardenedContext || !mFeatures.deferGlDeleteBuffers.enabled)
    {
        mFunctions->deleteBuffers(1, &buffer);
    }
    else
    {
        bool hasBeenBoundAsElementArrayBuffer = mElementArrayBuffers.erase(buffer) > 0;
        if (mDeferredBufferDeletions.empty() ||
            mDeferredBufferDeletions.back().serial != mCurrentSerial)
        {
            mDeferredBufferDeletions.push_back({mCurrentSerial, {}, 0});
        }
        DeferredBufferDeletions &deletions = mDeferredBufferDeletions.back();
        deletions.buffers.push_back(buffer);
        deletions.size += clientSpecifiedSize;
        ++mDeferredBufferDeletionsCount;
        mDeferredBufferDeletionsTotalSize += clientSpecifiedSize;
        if (hasBeenBoundAsElementArrayBuffer)
        {
            mDeferredBufferDeletionsNeedFinish = true;
        }
        maybeDrainDeferredBuffers();
    }
}

void StateManagerGL::deleteFramebuffer(GLuint fbo)
{
    if (fbo != 0)
    {
        bool wasBound = false;
        if (mHasSeparateFramebufferBindings)
        {
            for (size_t binding = 0; binding < mState.framebuffers.size(); ++binding)
            {
                if (mState.framebuffers[binding] == fbo)
                {
                    GLenum enumValue = angle::FramebufferBindingToEnum(
                        static_cast<angle::FramebufferBinding>(binding));
                    bindFramebuffer(enumValue, 0);
                    wasBound = true;
                }
            }
        }
        else
        {
            ASSERT(mState.framebuffers[angle::FramebufferBindingRead] ==
                   mState.framebuffers[angle::FramebufferBindingDraw]);
            if (mState.framebuffers[angle::FramebufferBindingRead] == fbo)
            {
                bindFramebuffer(GL_FRAMEBUFFER, 0);
                wasBound = true;
            }
        }
        if (!wasBound && mHasUnflushedQueries &&
            mFeatures.flushQueriesBeforeDeletingOrUnbindingFbo.enabled)
        {
            forcefullyFlush();
        }
        mFunctions->deleteFramebuffers(1, &fbo);
    }
}

void StateManagerGL::deleteRenderbuffer(GLuint rbo)
{
    if (rbo != 0)
    {
        if (mState.renderbuffer == rbo)
        {
            bindRenderbuffer(GL_RENDERBUFFER, 0);
        }

        mFunctions->deleteRenderbuffers(1, &rbo);
    }
}

void StateManagerGL::deleteTransformFeedback(GLuint transformFeedback)
{
    if (transformFeedback != 0)
    {
        if (mState.transformFeedback == transformFeedback)
        {
            bindTransformFeedback(GL_TRANSFORM_FEEDBACK, 0);
        }

        if (mCurrentTransformFeedback != nullptr &&
            mCurrentTransformFeedback->getTransformFeedbackID() == transformFeedback)
        {
            mCurrentTransformFeedback = nullptr;
        }

        mFunctions->deleteTransformFeedbacks(1, &transformFeedback);
    }
}

void StateManagerGL::useProgram(GLuint program)
{
    if (mState.program != program)
    {
        forceUseProgram(program);
    }
}

void StateManagerGL::forceUseProgram(GLuint program)
{
    mState.program = program;
    mFunctions->useProgram(program);
    mLocalDirtyBits.set(gl::state::DIRTY_BIT_PROGRAM_BINDING);
}

void StateManagerGL::bindVertexArray(GLuint vao)
{
    if (mState.vao != vao)
    {
        ASSERT(!mFeatures.syncAllVertexArraysToDefault.enabled);
        forceBindVertexArray(vao);
    }
}

void StateManagerGL::forceBindVertexArray(GLuint vao)
{
    VertexArrayStateGL *vaoState = getVAOState(vao);

    mState.vao                                      = vao;
    mState.buffers[gl::BufferBinding::ElementArray] = vaoState ? vaoState->elementArrayBuffer : 0;

    mFunctions->bindVertexArray(vao);

    mLocalDirtyBits.set(gl::state::DIRTY_BIT_VERTEX_ARRAY_BINDING);
}

void StateManagerGL::bindBuffer(gl::BufferBinding target, GLuint buffer, bool isHardenedContext)
{
    // GL drivers differ in whether the transform feedback bind point is modified when
    // glBindTransformFeedback is called. To avoid these behavior differences we shouldn't try to
    // use it.
    ASSERT(target != gl::BufferBinding::TransformFeedback);
    if (mState.buffers[target] != buffer)
    {
        mState.buffers[target] = buffer;
        mFunctions->bindBuffer(gl::ToGLenum(target), buffer);
        setBufferBindingDirty(target);

        if (target == gl::BufferBinding::ElementArray)
        {
            VertexArrayStateGL *vaoState = getCurrentVAOState();
            if (vaoState)
            {
                vaoState->elementArrayBuffer = buffer;
            }
            if (isHardenedContext && mFeatures.deferGlDeleteBuffers.enabled && buffer != 0)
            {
                mElementArrayBuffers.insert(buffer);
            }
        }
    }
}

void StateManagerGL::bindBufferBase(gl::BufferBinding target, size_t index, GLuint buffer)
{
    // Transform feedback buffer bindings are tracked in TransformFeedbackGL
    ASSERT(target != gl::BufferBinding::TransformFeedback);

    ASSERT(index < mState.indexedBuffers[target].size());
    auto &binding = mState.indexedBuffers[target][index];
    if (binding.buffer != buffer || binding.offset != 0 || binding.size != 0)
    {
        binding.buffer         = buffer;
        binding.offset         = 0;
        binding.size           = 0;
        mState.buffers[target] = buffer;
        mFunctions->bindBufferBase(gl::ToGLenum(target), static_cast<GLuint>(index), buffer);
        setBufferBindingDirty(target);
    }
}

void StateManagerGL::bindBufferRange(gl::BufferBinding target,
                                     size_t index,
                                     GLuint buffer,
                                     GLintptr offset,
                                     GLsizeiptr size)
{
    // Transform feedback buffer bindings are tracked in TransformFeedbackGL
    ASSERT(target != gl::BufferBinding::TransformFeedback);

    auto &binding = mState.indexedBuffers[target][index];
    if (binding.buffer != buffer || binding.offset != offset || binding.size != size)
    {
        binding.buffer         = buffer;
        binding.offset         = offset;
        binding.size           = size;
        mState.buffers[target] = buffer;
        mFunctions->bindBufferRange(gl::ToGLenum(target), static_cast<GLuint>(index), buffer,
                                    offset, size);
        setBufferBindingDirty(target);
    }
}

void StateManagerGL::activeTexture(size_t unit)
{
    if (mState.textureUnitIndex != unit)
    {
        mState.textureUnitIndex = unit;
        mFunctions->activeTexture(GL_TEXTURE0 + static_cast<GLenum>(unit));
    }
}

void StateManagerGL::bindTexture(gl::TextureType type, GLuint texture)
{
    if (mState.textures[type][mState.textureUnitIndex] != texture)
    {
        mState.textures[type][mState.textureUnitIndex] = texture;
        mFunctions->bindTexture(nativegl::GetTextureBindingTarget(type), texture);
        mLocalDirtyBits.set(gl::state::DIRTY_BIT_TEXTURE_BINDINGS);
    }
}

void StateManagerGL::bindTexture(gl::TextureType type, size_t unit, GLuint texture)
{
    if (mState.textures[type][unit] != texture)
    {
        activeTexture(unit);
        bindTexture(type, texture);
    }
}

void StateManagerGL::bindSampler(size_t unit, GLuint sampler)
{
    if (mState.samplers[unit] != sampler)
    {
        mState.samplers[unit] = sampler;
        mFunctions->bindSampler(static_cast<GLuint>(unit), sampler);
        mLocalDirtyBits.set(gl::state::DIRTY_BIT_SAMPLER_BINDINGS);
    }
}

void StateManagerGL::bindImageTexture(size_t unit,
                                      GLuint texture,
                                      GLint level,
                                      GLboolean layered,
                                      GLint layer,
                                      GLenum access,
                                      GLenum format)
{
    auto &binding = mState.images[unit];
    if (binding.texture != texture || binding.level != level || binding.layered != layered ||
        binding.layer != layer || binding.access != access || binding.format != format)
    {
        binding.texture = texture;
        binding.level   = level;
        binding.layered = layered;
        binding.layer   = layer;
        binding.access  = access;
        binding.format  = format;
        mFunctions->bindImageTexture(angle::base::checked_cast<GLuint>(unit), texture, level,
                                     layered, layer, access, format);
        mLocalDirtyBits.set(gl::state::DIRTY_BIT_IMAGE_BINDINGS);
    }
}

angle::Result StateManagerGL::setPixelUnpackState(const gl::Context *context,
                                                  const gl::PixelUnpackState &unpack)
{
    if (mState.unpackState == unpack)
    {
        return angle::Result::Continue;
    }

    if (mState.unpackState.alignment != unpack.alignment)
    {
        ANGLE_GL_TRY(context, mFunctions->pixelStorei(GL_UNPACK_ALIGNMENT, unpack.alignment));
    }

    if (mState.unpackState.rowLength != unpack.rowLength)
    {
        ANGLE_GL_TRY(context, mFunctions->pixelStorei(GL_UNPACK_ROW_LENGTH, unpack.rowLength));
    }

    if (mState.unpackState.skipRows != unpack.skipRows)
    {
        ANGLE_GL_TRY(context, mFunctions->pixelStorei(GL_UNPACK_SKIP_ROWS, unpack.skipRows));
    }

    if (mState.unpackState.skipPixels != unpack.skipPixels)
    {
        ANGLE_GL_TRY(context, mFunctions->pixelStorei(GL_UNPACK_SKIP_PIXELS, unpack.skipPixels));
    }

    if (mState.unpackState.imageHeight != unpack.imageHeight)
    {
        ANGLE_GL_TRY(context, mFunctions->pixelStorei(GL_UNPACK_IMAGE_HEIGHT, unpack.imageHeight));
    }

    if (mState.unpackState.skipImages != unpack.skipImages)
    {
        ANGLE_GL_TRY(context, mFunctions->pixelStorei(GL_UNPACK_SKIP_IMAGES, unpack.skipImages));
    }

    mState.unpackState = unpack;
    mLocalDirtyBits.set(gl::state::DIRTY_BIT_UNPACK_STATE);

    return angle::Result::Continue;
}

angle::Result StateManagerGL::setPixelUnpackBuffer(const gl::Context *context,
                                                   const gl::Buffer *pixelBuffer)
{
    GLuint bufferID = 0;
    if (pixelBuffer != nullptr)
    {
        bufferID = GetImplAs<BufferGL>(pixelBuffer)->getBufferID();
    }
    bindBuffer(gl::BufferBinding::PixelUnpack, bufferID, context->isHardenedContext());

    return angle::Result::Continue;
}

angle::Result StateManagerGL::setPixelPackState(const gl::Context *context,
                                                const gl::PixelPackState &pack)
{
    if (mState.packState == pack)
    {
        return angle::Result::Continue;
    }

    if (mState.packState.alignment != pack.alignment)
    {
        ANGLE_GL_TRY(context, mFunctions->pixelStorei(GL_PACK_ALIGNMENT, pack.alignment));
    }

    if (mState.packState.rowLength != pack.rowLength)
    {
        ANGLE_GL_TRY(context, mFunctions->pixelStorei(GL_PACK_ROW_LENGTH, pack.rowLength));
    }

    if (mState.packState.skipRows != pack.skipRows)
    {
        ANGLE_GL_TRY(context, mFunctions->pixelStorei(GL_PACK_SKIP_ROWS, pack.skipRows));
    }

    if (mState.packState.skipPixels != pack.skipPixels)
    {
        ANGLE_GL_TRY(context, mFunctions->pixelStorei(GL_PACK_SKIP_PIXELS, pack.skipPixels));
    }

    mState.packState = pack;
    mLocalDirtyBits.set(gl::state::DIRTY_BIT_PACK_STATE);

    return angle::Result::Continue;
}

angle::Result StateManagerGL::setPixelPackBuffer(const gl::Context *context,
                                                 const gl::Buffer *pixelBuffer)
{
    GLuint bufferID = 0;
    if (pixelBuffer != nullptr)
    {
        bufferID = GetImplAs<BufferGL>(pixelBuffer)->getBufferID();
    }
    bindBuffer(gl::BufferBinding::PixelPack, bufferID, context->isHardenedContext());

    return angle::Result::Continue;
}

void StateManagerGL::bindFramebuffer(GLenum type, GLuint framebuffer)
{
    bool framebufferChanged = false;
    switch (type)
    {
        case GL_FRAMEBUFFER:
            if (mState.framebuffers[angle::FramebufferBindingRead] != framebuffer ||
                mState.framebuffers[angle::FramebufferBindingDraw] != framebuffer)
            {
                mState.framebuffers[angle::FramebufferBindingRead] = framebuffer;
                mState.framebuffers[angle::FramebufferBindingDraw] = framebuffer;
                mFunctions->bindFramebuffer(GL_FRAMEBUFFER, framebuffer);

                mLocalDirtyBits.set(gl::state::DIRTY_BIT_READ_FRAMEBUFFER_BINDING);
                mLocalDirtyBits.set(gl::state::DIRTY_BIT_DRAW_FRAMEBUFFER_BINDING);

                framebufferChanged = true;
            }
            break;

        case GL_READ_FRAMEBUFFER:
            ASSERT(mHasSeparateFramebufferBindings);
            if (mState.framebuffers[angle::FramebufferBindingRead] != framebuffer)
            {
                mState.framebuffers[angle::FramebufferBindingRead] = framebuffer;
                mFunctions->bindFramebuffer(GL_READ_FRAMEBUFFER, framebuffer);

                mLocalDirtyBits.set(gl::state::DIRTY_BIT_READ_FRAMEBUFFER_BINDING);

                framebufferChanged = true;
            }
            break;

        case GL_DRAW_FRAMEBUFFER:
            ASSERT(mHasSeparateFramebufferBindings);
            if (mState.framebuffers[angle::FramebufferBindingDraw] != framebuffer)
            {
                mState.framebuffers[angle::FramebufferBindingDraw] = framebuffer;
                mFunctions->bindFramebuffer(GL_DRAW_FRAMEBUFFER, framebuffer);

                mLocalDirtyBits.set(gl::state::DIRTY_BIT_DRAW_FRAMEBUFFER_BINDING);

                framebufferChanged = true;
            }
            break;

        default:
            UNREACHABLE();
            break;
    }

    if (framebufferChanged)
    {
        if (mFeatures.flushOnFramebufferChange.enabled)
        {
            mFunctions->flush();
        }
        if (mHasUnflushedQueries && mFeatures.flushQueriesBeforeDeletingOrUnbindingFbo.enabled)
        {
            forcefullyFlush();
        }
    }
}

void StateManagerGL::onSyncedFlushOrFinish()
{
    mHasUnflushedQueries = false;
}

void StateManagerGL::onFinish()
{
    if (mFeatures.deferGlDeleteBuffers.enabled)
    {
        mDeferredBufferDeletionsNeedFinish = false;
        ExecutionSerial serial             = mCurrentSerial++;
        onSerialCompleted(serial);
    }
}

StateManagerGL::ExecutionSerial StateManagerGL::onFenceSync()
{
    return mCurrentSerial++;
}

StateManagerGL::ExecutionSerial StateManagerGL::onBeginQuery()
{
    return mCurrentSerial++;
}

void StateManagerGL::onSerialCompleted(ExecutionSerial serial)
{
    if (!mFeatures.deferGlDeleteBuffers.enabled)
    {
        return;
    }

    if (mDeferredBufferDeletions.empty() || mDeferredBufferDeletions.front().serial > serial)
    {
        return;
    }

    if (mDeferredBufferDeletionsNeedFinish)
    {
        mFunctions->finish();
        mDeferredBufferDeletionsNeedFinish = false;
        serial                             = mCurrentSerial++;
    }

    while (!mDeferredBufferDeletions.empty() && mDeferredBufferDeletions.front().serial <= serial)
    {
        DeferredBufferDeletions &deletions = mDeferredBufferDeletions.front();
        mFunctions->deleteBuffers(static_cast<GLsizei>(deletions.buffers.size()),
                                  deletions.buffers.data());
        ASSERT(mDeferredBufferDeletionsCount >= deletions.buffers.size());
        mDeferredBufferDeletionsCount -= deletions.buffers.size();
        ASSERT(mDeferredBufferDeletionsTotalSize >= deletions.size);
        mDeferredBufferDeletionsTotalSize -= deletions.size;
        mDeferredBufferDeletions.pop_front();
    }
}

void StateManagerGL::maybeDrainDeferredBuffers()
{
    if (mDeferredBufferDeletionsCount > kMaxDeferredDeletedBuffers ||
        mDeferredBufferDeletionsTotalSize > kMaxDeferredDeletedBuffersSize)
    {
        mFunctions->finish();
        onFinish();
    }
}

void StateManagerGL::forcefullyFlush()
{
    if (mFunctions->fenceSync != nullptr && mFunctions->clientWaitSync != nullptr &&
        mFunctions->deleteSync != nullptr)
    {
        GLsync sync = mFunctions->fenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE, 0);
        if (sync != nullptr)
        {
            mFunctions->clientWaitSync(sync, GL_SYNC_FLUSH_COMMANDS_BIT, 0);
            mFunctions->deleteSync(sync);
            onSyncedFlushOrFinish();
            return;
        }
    }

    // Sync creation not supported or failed; fall back to finish()
    mFunctions->finish();
    onSyncedFlushOrFinish();
    onFinish();
}

void StateManagerGL::bindRenderbuffer(GLenum type, GLuint renderbuffer)
{
    ASSERT(type == GL_RENDERBUFFER);
    if (mState.renderbuffer != renderbuffer)
    {
        mState.renderbuffer = renderbuffer;
        mFunctions->bindRenderbuffer(type, renderbuffer);
    }
}

void StateManagerGL::bindTransformFeedback(GLenum type, GLuint transformFeedback)
{
    ASSERT(type == GL_TRANSFORM_FEEDBACK);
    if (mState.transformFeedback != transformFeedback)
    {
        // Pause the current transform feedback if one is active.
        // To handle virtualized contexts, StateManagerGL needs to be able to bind a new transform
        // feedback at any time, even if there is one active.
        if (mCurrentTransformFeedback != nullptr &&
            mCurrentTransformFeedback->getTransformFeedbackID() != transformFeedback)
        {
            mCurrentTransformFeedback->syncPausedState(true);
            mCurrentTransformFeedback = nullptr;
        }

        mState.transformFeedback = transformFeedback;
        mFunctions->bindTransformFeedback(type, transformFeedback);
        onTransformFeedbackStateChange();
    }
}

void StateManagerGL::onTransformFeedbackStateChange()
{
    mLocalDirtyBits.set(gl::state::DIRTY_BIT_TRANSFORM_FEEDBACK_BINDING);
}

void StateManagerGL::beginQuery(gl::QueryType type, QueryGL *queryObject, GLuint queryId)
{
    // Make sure this is a valid query type and there is no current active query of this type
    ASSERT(mQueries[type] == nullptr);
    ASSERT(queryId != 0);

    GLuint oldFramebufferBindingDraw = mState.framebuffers[angle::FramebufferBindingDraw];
    if (mFeatures.bindCompleteFramebufferForTimerQueries.enabled &&
        (mState.framebuffers[angle::FramebufferBindingDraw] == 0 ||
         mFunctions->checkFramebufferStatus(GL_DRAW_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) &&
        (type == gl::QueryType::TimeElapsed || type == gl::QueryType::Timestamp))
    {
        ensurePlaceholderFramebuffer();
        bindFramebuffer(GL_DRAW_FRAMEBUFFER, mPlaceholderFbo);
    }

    mQueries[type] = queryObject;
    mFunctions->beginQuery(ToGLenum(type), queryId);
    mHasUnflushedQueries = true;

    if (oldFramebufferBindingDraw != mPlaceholderFbo)
    {
        bindFramebuffer(GL_DRAW_FRAMEBUFFER, oldFramebufferBindingDraw);
    }
}

void StateManagerGL::endQuery(gl::QueryType type, QueryGL *queryObject, GLuint queryId)
{
    ASSERT(queryObject != nullptr);
    ASSERT(mQueries[type] == queryObject);
    mQueries[type] = nullptr;
    mFunctions->endQuery(ToGLenum(type));
    mHasUnflushedQueries = true;
}

void StateManagerGL::updateDrawIndirectBufferBinding(const gl::Context *context)
{
    gl::Buffer *drawIndirectBuffer =
        context->getState().getTargetBuffer(gl::BufferBinding::DrawIndirect);
    if (drawIndirectBuffer != nullptr)
    {
        const BufferGL *bufferGL = GetImplAs<BufferGL>(drawIndirectBuffer);
        bindBuffer(gl::BufferBinding::DrawIndirect, bufferGL->getBufferID(),
                   context->isHardenedContext());
    }
}

void StateManagerGL::updateDispatchIndirectBufferBinding(const gl::Context *context)
{
    gl::Buffer *dispatchIndirectBuffer =
        context->getState().getTargetBuffer(gl::BufferBinding::DispatchIndirect);
    if (dispatchIndirectBuffer != nullptr)
    {
        const BufferGL *bufferGL = GetImplAs<BufferGL>(dispatchIndirectBuffer);
        bindBuffer(gl::BufferBinding::DispatchIndirect, bufferGL->getBufferID(),
                   context->isHardenedContext());
    }
}

void StateManagerGL::pauseTransformFeedback()
{
    if (mCurrentTransformFeedback != nullptr)
    {
        mCurrentTransformFeedback->syncPausedState(true);
        onTransformFeedbackStateChange();
    }
}

angle::Result StateManagerGL::pauseAllQueries(const gl::Context *context)
{
    for (gl::QueryType type : angle::AllEnums<gl::QueryType>())
    {
        QueryGL *previousQuery = mQueries[type];

        if (previousQuery != nullptr)
        {
            ANGLE_TRY(previousQuery->pause(context));
            mTemporaryPausedQueries[type] = previousQuery;
            mQueries[type]                = nullptr;
        }
    }

    return angle::Result::Continue;
}

angle::Result StateManagerGL::pauseQuery(const gl::Context *context, gl::QueryType type)
{
    QueryGL *previousQuery = mQueries[type];

    if (previousQuery)
    {
        ANGLE_TRY(previousQuery->pause(context));
        mTemporaryPausedQueries[type] = previousQuery;
        mQueries[type]                = nullptr;
    }

    return angle::Result::Continue;
}

angle::Result StateManagerGL::resumeAllQueries(const gl::Context *context)
{
    angle::ResultAccumulator result;

    for (gl::QueryType type : angle::AllEnums<gl::QueryType>())
    {
        QueryGL *pausedQuery          = mTemporaryPausedQueries[type];
        mTemporaryPausedQueries[type] = nullptr;

        if (pausedQuery != nullptr)
        {
            ASSERT(mQueries[type] == nullptr);
            result = pausedQuery->resume(context);
        }
    }

    return result;
}

angle::Result StateManagerGL::resumeQuery(const gl::Context *context, gl::QueryType type)
{
    QueryGL *pausedQuery          = mTemporaryPausedQueries[type];
    mTemporaryPausedQueries[type] = nullptr;

    if (pausedQuery != nullptr)
    {
        ANGLE_TRY(pausedQuery->resume(context));
    }

    return angle::Result::Continue;
}

angle::Result StateManagerGL::onMakeCurrent(const gl::Context *context)
{
    const gl::State &glState = context->getState();

#if defined(ANGLE_ENABLE_ASSERTS)
    // Temporarily pausing queries during context switch is not supported
    for (QueryGL *pausedQuery : mTemporaryPausedQueries)
    {
        ASSERT(pausedQuery == nullptr);
    }
#endif

    // If the context has changed, pause the previous context's queries
    auto contextID = context->getState().getContextID();
    if (contextID != mPrevDrawContext)
    {
        for (gl::QueryType type : angle::AllEnums<gl::QueryType>())
        {
            QueryGL *currentQuery = mQueries[type];
            // Pause any old query object
            if (currentQuery != nullptr)
            {
                ANGLE_TRY(currentQuery->pause(context));
                mQueries[type] = nullptr;
            }

            // Check if this new context needs to resume a query
            gl::Query *newQuery = glState.getActiveQuery(type);
            if (newQuery != nullptr)
            {
                QueryGL *queryGL = GetImplAs<QueryGL>(newQuery);
                ANGLE_TRY(queryGL->resume(context));
            }
        }
    }
    onTransformFeedbackStateChange();
    mPrevDrawContext = contextID;

    // Seamless cubemaps are required for ES3 and higher contexts. It should be the cheapest to set
    // this state here since MakeCurrent is expected to be called less frequently than draw calls.
    setTextureCubemapSeamlessEnabled(context->getClientVersion() >= gl::ES_3_0);

    return angle::Result::Continue;
}

void StateManagerGL::updateProgramTextureBindings(const gl::Context *context)
{
    const gl::State &glState                = context->getState();
    const gl::ProgramExecutable *executable = glState.getProgramExecutable();

    // It is possible there is no active program during a path operation.
    if (!executable)
    {
        return;
    }

    const gl::ActiveTexturesCache &textures        = glState.getActiveTexturesCache();
    const gl::ActiveTextureMask &activeTextures    = executable->getActiveSamplersMask();
    const gl::ActiveTextureTypeArray &textureTypes = executable->getActiveSamplerTypes();

    for (size_t textureUnitIndex : activeTextures)
    {
        gl::TextureType textureType = textureTypes[textureUnitIndex];
        gl::Texture *texture        = textures[textureUnitIndex];

        // A nullptr texture indicates incomplete.
        if (texture != nullptr)
        {
            const TextureGL *textureGL = GetImplAs<TextureGL>(texture);
            // The DIRTY_BIT_BOUND_AS_ATTACHMENT may get inserted when texture is attached to
            // FBO and if texture is already bound, Texture::syncState will not get called and dirty
            // bit not gets cleared. But this bit is not used by GL backend at all, so it is
            // harmless even though we expect texture is clean when reaching here. The bit will
            // still get cleared next time syncState been called.
            ASSERT(!texture->hasAnyDirtyBitExcludingBoundAsAttachmentBit());
            ASSERT(!textureGL->hasAnyDirtyBit());

            activeTexture(textureUnitIndex);
            bindTexture(textureType, textureGL->getTextureID());
        }
        else
        {
            activeTexture(textureUnitIndex);
            bindTexture(textureType, 0);
        }
    }
}

void StateManagerGL::updateProgramStorageBufferBindings(const gl::Context *context)
{
    const gl::State &glState                = context->getState();
    const gl::ProgramExecutable *executable = glState.getProgramExecutable();

    for (size_t blockIndex = 0; blockIndex < executable->getShaderStorageBlocks().size();
         blockIndex++)
    {
        GLuint binding = executable->getShaderStorageBlockBinding(static_cast<GLuint>(blockIndex));
        const auto &shaderStorageBuffer = glState.getIndexedShaderStorageBuffer(binding);

        if (shaderStorageBuffer.get() != nullptr)
        {
            BufferGL *bufferGL = GetImplAs<BufferGL>(shaderStorageBuffer.get());

            if (shaderStorageBuffer.getSize() == 0)
            {
                bindBufferBase(gl::BufferBinding::ShaderStorage, binding, bufferGL->getBufferID());
            }
            else
            {
                bindBufferRange(gl::BufferBinding::ShaderStorage, binding, bufferGL->getBufferID(),
                                shaderStorageBuffer.getOffset(), shaderStorageBuffer.getSize());
            }
        }
    }
}

void StateManagerGL::updateProgramUniformBufferBindings(const gl::Context *context)
{
    // Sync the current program executable state
    const gl::State &glState                = context->getState();
    const gl::ProgramExecutable *executable = glState.getProgramExecutable();
    ProgramExecutableGL *executableGL       = GetImplAs<ProgramExecutableGL>(executable);

    // If any calls to glUniformBlockBinding have been made, make them effective.  Note that if PPOs
    // are ever supported in this backend, this needs to look at the Program's attached to PPOs
    // instead of the PPOs own executable.  This is because glUniformBlockBinding operates on
    // programs directly.
    executableGL->syncUniformBlockBindings();

    for (size_t uniformBlockIndex = 0; uniformBlockIndex < executable->getUniformBlocks().size();
         uniformBlockIndex++)
    {
        GLuint binding = executable->getUniformBlockBinding(static_cast<GLuint>(uniformBlockIndex));
        const auto &uniformBuffer = glState.getIndexedUniformBuffer(binding);

        if (uniformBuffer.get() != nullptr)
        {
            BufferGL *bufferGL = GetImplAs<BufferGL>(uniformBuffer.get());

            if (uniformBuffer.getSize() == 0)
            {
                bindBufferBase(gl::BufferBinding::Uniform, binding, bufferGL->getBufferID());
            }
            else
            {
                bindBufferRange(gl::BufferBinding::Uniform, binding, bufferGL->getBufferID(),
                                uniformBuffer.getOffset(), uniformBuffer.getSize());
            }
        }
    }
}

void StateManagerGL::updateProgramAtomicCounterBufferBindings(const gl::Context *context)
{
    const gl::State &glState                = context->getState();
    const gl::ProgramExecutable *executable = glState.getProgramExecutable();

    const std::vector<gl::AtomicCounterBuffer> &atomicCounterBuffers =
        executable->getAtomicCounterBuffers();
    for (size_t index = 0; index < atomicCounterBuffers.size(); ++index)
    {
        const GLuint binding = executable->getAtomicCounterBufferBinding(index);
        const auto &buffer   = glState.getIndexedAtomicCounterBuffer(binding);

        if (buffer.get() != nullptr)
        {
            BufferGL *bufferGL = GetImplAs<BufferGL>(buffer.get());

            if (buffer.getSize() == 0)
            {
                bindBufferBase(gl::BufferBinding::AtomicCounter, binding, bufferGL->getBufferID());
            }
            else
            {
                bindBufferRange(gl::BufferBinding::AtomicCounter, binding, bufferGL->getBufferID(),
                                buffer.getOffset(), buffer.getSize());
            }
        }
    }
}

void StateManagerGL::updateProgramImageBindings(const gl::Context *context)
{
    const gl::State &glState                = context->getState();
    const gl::ProgramExecutable *executable = glState.getProgramExecutable();

    // It is possible there is no active program during a path operation.
    if (!executable)
    {
        return;
    }

    ASSERT(context->getClientVersion() >= gl::ES_3_1 ||
           context->getExtensions().shaderPixelLocalStorageANGLE ||
           executable->getImageBindings().empty());
    for (size_t imageUnitIndex : executable->getActiveImagesMask())
    {
        const gl::ImageUnit &imageUnit = glState.getImageUnit(imageUnitIndex);
        const TextureGL *textureGL     = SafeGetImplAs<TextureGL>(imageUnit.texture.get());
        if (textureGL)
        {
            gl::TextureTarget target =
                textureGL->getType() == gl::TextureType::CubeMap
                    ? gl::CubeFaceIndexToTextureTarget(imageUnit.layered ? 0 : imageUnit.layer)
                    : gl::NonCubeTextureTypeToTarget(textureGL->getType());
            TextureGL::NativeLevelTarget nativeTarget =
                textureGL->getNativeLevelTarget(target, imageUnit.level);
            // Do not set layer parameters for non-layered texture types (including an unmerged
            // cube map face stored in a 2D companion texture when virtualizeBaseLevelAndMipchain
            // is enabled) to avoid driver bugs.
            const bool layered = IsLayeredTextureType(gl::TextureTargetToType(nativeTarget.target));
            bindImageTexture(imageUnitIndex, nativeTarget.textureID, nativeTarget.level,
                             layered && imageUnit.layered, layered ? imageUnit.layer : 0,
                             imageUnit.access, imageUnit.format);
        }
        else
        {
            bindImageTexture(imageUnitIndex, 0, imageUnit.level, imageUnit.layered, imageUnit.layer,
                             imageUnit.access, imageUnit.format);
        }
    }
}

void StateManagerGL::setAttributeCurrentData(size_t index,
                                             const gl::VertexAttribCurrentValueData &data)
{
    if (mState.vertexAttribCurrentValues[index] != data)
    {
        mState.vertexAttribCurrentValues[index] = data;
        switch (data.Type)
        {
            case gl::VertexAttribType::Float:
                mFunctions->vertexAttrib4fv(static_cast<GLuint>(index), data.Values.FloatValues);
                break;
            case gl::VertexAttribType::Int:
                mFunctions->vertexAttribI4iv(static_cast<GLuint>(index), data.Values.IntValues);
                break;
            case gl::VertexAttribType::UnsignedInt:
                mFunctions->vertexAttribI4uiv(static_cast<GLuint>(index),
                                              data.Values.UnsignedIntValues);
                break;
            default:
                UNREACHABLE();
        }

        mLocalDirtyBits.set(gl::state::DIRTY_BIT_CURRENT_VALUES);
        mLocalDirtyCurrentValues.set(index);
    }
}

void StateManagerGL::setScissorTestEnabled(bool enabled)
{
    if (mState.scissorTestEnabled != enabled)
    {
        mState.scissorTestEnabled = enabled;
        SetGLBoolState(mFunctions, GL_SCISSOR_TEST, enabled);

        mLocalDirtyBits.set(gl::state::DIRTY_BIT_SCISSOR_TEST_ENABLED);
    }
}

void StateManagerGL::setScissor(const gl::Rectangle &scissor)
{
    if (scissor != mState.scissor)
    {
        mState.scissor = scissor;
        mFunctions->scissor(scissor.x, scissor.y, scissor.width, scissor.height);

        mLocalDirtyBits.set(gl::state::DIRTY_BIT_SCISSOR);
    }
}

void StateManagerGL::setViewport(const gl::Rectangle &viewport)
{
    if (viewport != mState.viewport)
    {
        mState.viewport = viewport;
        mFunctions->viewport(viewport.x, viewport.y, viewport.width, viewport.height);

        mLocalDirtyBits.set(gl::state::DIRTY_BIT_VIEWPORT);
    }
}

void StateManagerGL::setDepthRange(float near, float far)
{
    mState.near = near;
    mState.far  = far;

    // The glDepthRangef function isn't available until OpenGL 4.1.  Prefer it when it is
    // available because OpenGL ES only works in floats.
    if (mFunctions->depthRangef)
    {
        mFunctions->depthRangef(near, far);
    }
    else
    {
        ASSERT(mFunctions->depthRange);
        mFunctions->depthRange(near, far);
    }

    mLocalDirtyBits.set(gl::state::DIRTY_BIT_DEPTH_RANGE);
}

void StateManagerGL::setClipControl(gl::ClipOrigin origin, gl::ClipDepthMode depth)
{
    if (mState.clipOrigin == origin && mState.clipDepthMode == depth)
    {
        return;
    }

    mState.clipOrigin    = origin;
    mState.clipDepthMode = depth;

    ASSERT(mFunctions->clipControl);
    mFunctions->clipControl(ToGLenum(origin), ToGLenum(depth));

    if (mFeatures.resyncDepthRangeOnClipControl.enabled)
    {
        // Change and restore depth range to trigger internal transformation
        // state resync. This is needed to apply clip control on some drivers.
        const float near = mState.near;
        setDepthRange(near == 0.0f ? 1.0f : 0.0f, mState.far);
        setDepthRange(near, mState.far);
    }

    mLocalDirtyBits.set(gl::state::DIRTY_BIT_CLIP_CONTROL);
}

void StateManagerGL::setClipControlWithEmulatedClipOrigin(const gl::ProgramExecutable *executable,
                                                          GLenum frontFace,
                                                          gl::ClipOrigin origin,
                                                          gl::ClipDepthMode depth)
{
    ASSERT(mFeatures.emulateClipOrigin.enabled);
    if (executable)
    {
        updateEmulatedClipOriginUniform(executable, origin);
    }
    static_assert((GL_CW ^ GL_CCW) == static_cast<GLenum>(gl::ClipOrigin::UpperLeft));
    setFrontFace(frontFace ^ static_cast<GLenum>(origin));
    setClipControl(gl::ClipOrigin::LowerLeft, depth);
}

void StateManagerGL::setBlendEnabled(bool enabled)
{
    const gl::DrawBufferMask mask =
        enabled ? mState.blendState.getAllEnabledMask() : gl::DrawBufferMask::Zero();
    if (mState.blendState.getEnabledMask() == mask)
    {
        return;
    }

    SetGLBoolState(mFunctions, GL_BLEND, enabled);

    mState.blendState.setEnabled(enabled);
    mLocalDirtyBits.set(gl::state::DIRTY_BIT_BLEND_ENABLED);
}

void StateManagerGL::setBlendEnabledIndexed(const gl::DrawBufferMask enabledMask)
{
    if (mState.blendState.getEnabledMask() == enabledMask)
    {
        return;
    }

    // Get DrawBufferMask of buffers with different blend enable state
    gl::DrawBufferMask diffMask = mState.blendState.getEnabledMask() ^ enabledMask;
    const size_t diffCount      = diffMask.count();

    // Check if enabling or disabling blending for all buffers reduces the number of subsequent
    // indexed commands. Implicitly handles the case when the new blend enable state is the same for
    // all buffers.
    if (diffCount > 1)
    {
        // The number of indexed blend enable commands in case a mass disable is used.
        const size_t enabledCount = enabledMask.count();

        // The mask and the number of indexed blend disable commands in case a mass enable is used.
        const gl::DrawBufferMask disabledMask = enabledMask ^ mState.blendState.getAllEnabledMask();
        const size_t disabledCount            = disabledMask.count();

        if (enabledCount < diffCount && enabledCount <= disabledCount)
        {
            diffMask = enabledMask;
            mFunctions->disable(GL_BLEND);
        }
        else if (disabledCount < diffCount && disabledCount <= enabledCount)
        {
            diffMask = disabledMask;
            mFunctions->enable(GL_BLEND);
        }
    }

    for (size_t drawBufferIndex : diffMask)
    {
        SetGLIndexedBoolState(mFunctions, GL_BLEND, static_cast<GLuint>(drawBufferIndex),
                              enabledMask.test(drawBufferIndex));
    }

    mState.blendState.setEnabledMask(enabledMask);
    mLocalDirtyBits.set(gl::state::DIRTY_BIT_BLEND_ENABLED);
}

void StateManagerGL::setBlendColor(const gl::ColorF &blendColor)
{
    if (mState.blendColor != blendColor)
    {
        mState.blendColor = blendColor;
        mFunctions->blendColor(blendColor.red, blendColor.green, blendColor.blue, blendColor.alpha);

        mLocalDirtyBits.set(gl::state::DIRTY_BIT_BLEND_COLOR);
    }
}

void StateManagerGL::setBlendAdvancedCoherent(bool enabled)
{
    if (mState.blendAdvancedCoherent != enabled)
    {
        mState.blendAdvancedCoherent = enabled;

        SetGLBoolState(mFunctions, GL_BLEND_ADVANCED_COHERENT_KHR, enabled);

        mLocalDirtyBits.set(gl::state::DIRTY_BIT_EXTENDED);
        mLocalExtendedDirtyBits.set(gl::state::EXTENDED_DIRTY_BIT_BLEND_ADVANCED_COHERENT);
    }
}

void StateManagerGL::setBlendFuncs(const gl::BlendStateExt &blendState)
{
    if (mState.blendState.getSrcColorBits() == blendState.getSrcColorBits() &&
        mState.blendState.getDstColorBits() == blendState.getDstColorBits() &&
        mState.blendState.getSrcAlphaBits() == blendState.getSrcAlphaBits() &&
        mState.blendState.getDstAlphaBits() == blendState.getDstAlphaBits())
    {
        return;
    }

    if (!mIndependentBlendStates)
    {
        mFunctions->blendFuncSeparate(
            ToGLenum(blendState.getSrcColorIndexed(0)), ToGLenum(blendState.getDstColorIndexed(0)),
            ToGLenum(blendState.getSrcAlphaIndexed(0)), ToGLenum(blendState.getDstAlphaIndexed(0)));
    }
    else
    {
        // Get DrawBufferMask of buffers with different blend factors
        gl::DrawBufferMask diffMask = mState.blendState.compareFactors(blendState);
        size_t diffCount            = diffMask.count();

        // Check if setting all buffers to the same value reduces the number of subsequent indexed
        // commands. Implicitly handles the case when the new blend function state is the same for
        // all buffers.
        if (diffCount > 1)
        {
            bool found                                            = false;
            gl::BlendStateExt::FactorStorage::Type commonSrcColor = 0;
            gl::BlendStateExt::FactorStorage::Type commonDstColor = 0;
            gl::BlendStateExt::FactorStorage::Type commonSrcAlpha = 0;
            gl::BlendStateExt::FactorStorage::Type commonDstAlpha = 0;
            for (size_t i = 0; i < mState.blendState.getDrawBufferCount() - 1; i++)
            {
                const gl::BlendStateExt::FactorStorage::Type tempCommonSrcColor =
                    blendState.expandSrcColorIndexed(i);
                const gl::BlendStateExt::FactorStorage::Type tempCommonDstColor =
                    blendState.expandDstColorIndexed(i);
                const gl::BlendStateExt::FactorStorage::Type tempCommonSrcAlpha =
                    blendState.expandSrcAlphaIndexed(i);
                const gl::BlendStateExt::FactorStorage::Type tempCommonDstAlpha =
                    blendState.expandDstAlphaIndexed(i);

                const gl::DrawBufferMask tempDiffMask = blendState.compareFactors(
                    tempCommonSrcColor, tempCommonDstColor, tempCommonSrcAlpha, tempCommonDstAlpha);

                const size_t tempDiffCount = tempDiffMask.count();
                if (tempDiffCount < diffCount)
                {
                    found          = true;
                    diffMask       = tempDiffMask;
                    diffCount      = tempDiffCount;
                    commonSrcColor = tempCommonSrcColor;
                    commonDstColor = tempCommonDstColor;
                    commonSrcAlpha = tempCommonSrcAlpha;
                    commonDstAlpha = tempCommonDstAlpha;
                    if (tempDiffCount == 0)
                    {
                        break;  // the blend factors are the same for all buffers
                    }
                }
            }
            if (found)
            {
                mFunctions->blendFuncSeparate(
                    ToGLenum(gl::BlendStateExt::FactorStorage::GetValueIndexed(0, commonSrcColor)),
                    ToGLenum(gl::BlendStateExt::FactorStorage::GetValueIndexed(0, commonDstColor)),
                    ToGLenum(gl::BlendStateExt::FactorStorage::GetValueIndexed(0, commonSrcAlpha)),
                    ToGLenum(gl::BlendStateExt::FactorStorage::GetValueIndexed(0, commonDstAlpha)));
            }
        }

        for (size_t drawBufferIndex : diffMask)
        {
            mFunctions->blendFuncSeparatei(
                static_cast<GLuint>(drawBufferIndex),
                ToGLenum(blendState.getSrcColorIndexed(drawBufferIndex)),
                ToGLenum(blendState.getDstColorIndexed(drawBufferIndex)),
                ToGLenum(blendState.getSrcAlphaIndexed(drawBufferIndex)),
                ToGLenum(blendState.getDstAlphaIndexed(drawBufferIndex)));
        }
    }
    mState.blendState.setFactorBits(blendState.getSrcColorBits(), blendState.getDstColorBits(),
                                    blendState.getSrcAlphaBits(), blendState.getDstAlphaBits(),
                                    blendState.getUsesExtendedBlendFactorMask());
    mLocalDirtyBits.set(gl::state::DIRTY_BIT_BLEND_FUNCS);
}

void StateManagerGL::setBlendEquations(const gl::BlendStateExt &blendState)
{
    if (mState.blendState.getEquationColorBits() == blendState.getEquationColorBits() &&
        mState.blendState.getEquationAlphaBits() == blendState.getEquationAlphaBits())
    {
        return;
    }

    if (!mIndependentBlendStates)
    {
        mFunctions->blendEquationSeparate(ToGLenum(blendState.getEquationColorIndexed(0)),
                                          ToGLenum(blendState.getEquationAlphaIndexed(0)));
    }
    else
    {
        // Get DrawBufferMask of buffers with different blend equations
        gl::DrawBufferMask diffMask = mState.blendState.compareEquations(blendState);
        size_t diffCount            = diffMask.count();

        // Check if setting all buffers to the same value reduces the number of subsequent indexed
        // commands. Implicitly handles the case when the new blend equation state is the same for
        // all buffers.
        if (diffCount > 1)
        {
            bool found                                                   = false;
            gl::BlendStateExt::EquationStorage::Type commonEquationColor = 0;
            gl::BlendStateExt::EquationStorage::Type commonEquationAlpha = 0;
            for (size_t i = 0; i < mState.blendState.getDrawBufferCount() - 1; i++)
            {
                const gl::BlendStateExt::EquationStorage::Type tempCommonEquationColor =
                    blendState.expandEquationColorIndexed(i);
                const gl::BlendStateExt::EquationStorage::Type tempCommonEquationAlpha =
                    blendState.expandEquationAlphaIndexed(i);

                const gl::DrawBufferMask tempDiffMask =
                    blendState.compareEquations(tempCommonEquationColor, tempCommonEquationAlpha);

                const size_t tempDiffCount = tempDiffMask.count();
                if (tempDiffCount < diffCount)
                {
                    found               = true;
                    diffMask            = tempDiffMask;
                    diffCount           = tempDiffCount;
                    commonEquationColor = tempCommonEquationColor;
                    commonEquationAlpha = tempCommonEquationAlpha;
                    if (tempDiffCount == 0)
                    {
                        break;  // the new blend equations are the same for all buffers
                    }
                }
            }
            if (found)
            {
                if (commonEquationColor == commonEquationAlpha)
                {
                    mFunctions->blendEquation(
                        ToGLenum(gl::BlendStateExt::EquationStorage::GetValueIndexed(
                            0, commonEquationColor)));
                }
                else
                {
                    mFunctions->blendEquationSeparate(
                        ToGLenum(gl::BlendStateExt::EquationStorage::GetValueIndexed(
                            0, commonEquationColor)),
                        ToGLenum(gl::BlendStateExt::EquationStorage::GetValueIndexed(
                            0, commonEquationAlpha)));
                }
            }
        }

        for (size_t drawBufferIndex : diffMask)
        {
            gl::BlendEquationType equationColor =
                blendState.getEquationColorIndexed(drawBufferIndex);
            gl::BlendEquationType equationAlpha =
                blendState.getEquationAlphaIndexed(drawBufferIndex);
            if (equationColor == equationAlpha)
            {
                mFunctions->blendEquationi(static_cast<GLuint>(drawBufferIndex),
                                           ToGLenum(equationColor));
            }
            else
            {
                mFunctions->blendEquationSeparatei(static_cast<GLuint>(drawBufferIndex),
                                                   ToGLenum(equationColor),
                                                   ToGLenum(equationAlpha));
            }
        }
    }
    mState.blendState.setEquationColorBits(blendState.getEquationColorBits(),
                                           blendState.getUsesAdvancedBlendEquationMask());
    mState.blendState.setEquationAlphaBits(blendState.getEquationAlphaBits());
    mLocalDirtyBits.set(gl::state::DIRTY_BIT_BLEND_EQUATIONS);
}

void StateManagerGL::setColorMask(bool red, bool green, bool blue, bool alpha)
{
    const gl::BlendStateExt::ColorMaskStorage::Type mask =
        mState.blendState.expandColorMaskValue(red, green, blue, alpha);
    if (mState.blendState.getColorMaskBits() != mask)
    {
        mFunctions->colorMask(red, green, blue, alpha);
        mState.blendState.setColorMaskBits(mask);
        mLocalDirtyBits.set(gl::state::DIRTY_BIT_COLOR_MASK);
    }
}

void StateManagerGL::setSampleAlphaToCoverageEnabled(bool enabled)
{
    if (mState.sampleAlphaToCoverageEnabled != enabled)
    {
        mState.sampleAlphaToCoverageEnabled = enabled;
        SetGLBoolState(mFunctions, GL_SAMPLE_ALPHA_TO_COVERAGE, enabled);

        mLocalDirtyBits.set(gl::state::DIRTY_BIT_SAMPLE_ALPHA_TO_COVERAGE_ENABLED);
    }
}

void StateManagerGL::setSampleCoverageEnabled(bool enabled)
{
    if (mState.sampleCoverageEnabled != enabled)
    {
        mState.sampleCoverageEnabled = enabled;
        SetGLBoolState(mFunctions, GL_SAMPLE_COVERAGE, enabled);

        mLocalDirtyBits.set(gl::state::DIRTY_BIT_SAMPLE_COVERAGE_ENABLED);
    }
}

void StateManagerGL::forceSetSampleCoverage(float value, bool invert)
{
    mState.sampleCoverageValue  = value;
    mState.sampleCoverageInvert = invert;
    mSampleCoverageEverChanged  = true;
    mFunctions->sampleCoverage(value, invert);
    mLocalDirtyBits.set(gl::state::DIRTY_BIT_SAMPLE_COVERAGE);
}

void StateManagerGL::setSampleCoverage(float value, bool invert)
{
    if (mState.sampleCoverageValue != value || mState.sampleCoverageInvert != invert)
    {
        forceSetSampleCoverage(value, invert);
    }
}

void StateManagerGL::setSampleMaskEnabled(bool enabled)
{
    if (mState.sampleMaskEnabled != enabled)
    {
        mState.sampleMaskEnabled = enabled;
        SetGLBoolState(mFunctions, GL_SAMPLE_MASK, enabled);

        mLocalDirtyBits.set(gl::state::DIRTY_BIT_SAMPLE_MASK_ENABLED);
    }
}

void StateManagerGL::setSampleMask(const gl::SampleMaskArray<GLbitfield> &maskValues)
{
    for (GLuint maskNumber = 0; maskNumber < maskValues.size(); maskNumber++)
    {
        setSampleMaski(maskNumber, maskValues[maskNumber]);
    }
}

void StateManagerGL::setSampleMaski(GLuint maskNumber, GLbitfield mask)
{
    ASSERT(maskNumber < mState.sampleMaskValues.size());
    if (mState.sampleMaskValues[maskNumber] != mask)
    {
        mState.sampleMaskValues[maskNumber] = mask;
        mFunctions->sampleMaski(maskNumber, mask);

        mLocalDirtyBits.set(gl::state::DIRTY_BIT_SAMPLE_MASK);
    }
}

void StateManagerGL::setDepthTestEnabled(bool enabled)
{
    if (mState.depthTestEnabled != enabled)
    {
        mState.depthTestEnabled = enabled;
        SetGLBoolState(mFunctions, GL_DEPTH_TEST, enabled);

        mLocalDirtyBits.set(gl::state::DIRTY_BIT_DEPTH_TEST_ENABLED);
    }
}

void StateManagerGL::setDepthFunc(GLenum depthFunc)
{
    if (mState.depthFunc != depthFunc)
    {
        mState.depthFunc = depthFunc;
        mFunctions->depthFunc(depthFunc);

        mLocalDirtyBits.set(gl::state::DIRTY_BIT_DEPTH_FUNC);
    }
}

void StateManagerGL::setDepthMask(bool mask)
{
    if (mState.depthMask != mask)
    {
        mState.depthMask = mask;
        mFunctions->depthMask(mask);

        mLocalDirtyBits.set(gl::state::DIRTY_BIT_DEPTH_MASK);
    }
}

void StateManagerGL::setStencilTestEnabled(bool enabled)
{
    if (mState.stencilTestEnabled != enabled)
    {
        mState.stencilTestEnabled = enabled;
        SetGLBoolState(mFunctions, GL_STENCIL_TEST, enabled);

        mLocalDirtyBits.set(gl::state::DIRTY_BIT_STENCIL_TEST_ENABLED);
    }
}

void StateManagerGL::setStencilFrontWritemask(GLuint mask)
{
    GLuint clippedMask = mask & 0xFF;
    if (mState.stencilFrontWritemask != clippedMask)
    {
        mState.stencilFrontWritemask = clippedMask;
        mFunctions->stencilMaskSeparate(GL_FRONT, clippedMask);

        mLocalDirtyBits.set(gl::state::DIRTY_BIT_STENCIL_WRITEMASK_FRONT);
    }
}

void StateManagerGL::setStencilBackWritemask(GLuint mask)
{
    GLuint clippedMask = mask & 0xFF;
    if (mState.stencilBackWritemask != clippedMask)
    {
        mState.stencilBackWritemask = clippedMask;
        mFunctions->stencilMaskSeparate(GL_BACK, clippedMask);

        mLocalDirtyBits.set(gl::state::DIRTY_BIT_STENCIL_WRITEMASK_BACK);
    }
}

void StateManagerGL::setStencilFrontFuncs(GLenum func, GLint ref, GLuint mask)
{
    GLuint clippedMask = mask & 0xFF;
    if (mState.stencilFrontFunc != func || mState.stencilFrontRef != ref ||
        mState.stencilFrontValueMask != clippedMask)
    {
        mState.stencilFrontFunc      = func;
        mState.stencilFrontRef       = ref;
        mState.stencilFrontValueMask = clippedMask;
        mFunctions->stencilFuncSeparate(GL_FRONT, func, ref, clippedMask);

        mLocalDirtyBits.set(gl::state::DIRTY_BIT_STENCIL_FUNCS_FRONT);
    }
}

void StateManagerGL::setStencilBackFuncs(GLenum func, GLint ref, GLuint mask)
{
    GLuint clippedMask = mask & 0xFF;
    if (mState.stencilBackFunc != func || mState.stencilBackRef != ref ||
        mState.stencilBackValueMask != clippedMask)
    {
        mState.stencilBackFunc      = func;
        mState.stencilBackRef       = ref;
        mState.stencilBackValueMask = clippedMask;
        mFunctions->stencilFuncSeparate(GL_BACK, func, ref, clippedMask);

        mLocalDirtyBits.set(gl::state::DIRTY_BIT_STENCIL_FUNCS_BACK);
    }
}

void StateManagerGL::setStencilFrontOps(GLenum sfail, GLenum dpfail, GLenum dppass)
{
    if (mState.stencilFrontStencilFailOp != sfail ||
        mState.stencilFrontStencilPassDepthFailOp != dpfail ||
        mState.stencilFrontStencilPassDepthPassOp != dppass)
    {
        mState.stencilFrontStencilFailOp          = sfail;
        mState.stencilFrontStencilPassDepthFailOp = dpfail;
        mState.stencilFrontStencilPassDepthPassOp = dppass;
        mFunctions->stencilOpSeparate(GL_FRONT, sfail, dpfail, dppass);

        mLocalDirtyBits.set(gl::state::DIRTY_BIT_STENCIL_OPS_FRONT);
    }
}

void StateManagerGL::setStencilBackOps(GLenum sfail, GLenum dpfail, GLenum dppass)
{
    if (mState.stencilBackStencilFailOp != sfail ||
        mState.stencilBackStencilPassDepthFailOp != dpfail ||
        mState.stencilBackStencilPassDepthPassOp != dppass)
    {
        mState.stencilBackStencilFailOp          = sfail;
        mState.stencilBackStencilPassDepthFailOp = dpfail;
        mState.stencilBackStencilPassDepthPassOp = dppass;
        mFunctions->stencilOpSeparate(GL_BACK, sfail, dpfail, dppass);

        mLocalDirtyBits.set(gl::state::DIRTY_BIT_STENCIL_OPS_BACK);
    }
}

void StateManagerGL::setCullFaceEnabled(bool enabled)
{
    if (mState.cullFaceEnabled != enabled)
    {
        mState.cullFaceEnabled = enabled;
        SetGLBoolState(mFunctions, GL_CULL_FACE, enabled);

        mLocalDirtyBits.set(gl::state::DIRTY_BIT_CULL_FACE_ENABLED);
    }
}

void StateManagerGL::setCullFace(gl::CullFaceMode cullFace)
{
    if (mState.cullFace != cullFace)
    {
        mState.cullFace = cullFace;
        mFunctions->cullFace(ToGLenum(cullFace));

        mLocalDirtyBits.set(gl::state::DIRTY_BIT_CULL_FACE);
    }
}

void StateManagerGL::setFrontFace(GLenum frontFace)
{
    if (mState.frontFace != frontFace)
    {
        mState.frontFace = frontFace;
        mFunctions->frontFace(frontFace);

        mLocalDirtyBits.set(gl::state::DIRTY_BIT_FRONT_FACE);
    }
}

void StateManagerGL::setPolygonMode(gl::PolygonMode mode)
{
    if (mState.polygonMode != mode)
    {
        mState.polygonMode = mode;
        if (mFunctions->standard == STANDARD_GL_DESKTOP)
        {
            mFunctions->polygonMode(GL_FRONT_AND_BACK, ToGLenum(mode));
        }
        else
        {
            ASSERT(mFunctions->polygonModeNV);
            mFunctions->polygonModeNV(GL_FRONT_AND_BACK, ToGLenum(mode));
        }

        mLocalDirtyBits.set(gl::state::DIRTY_BIT_EXTENDED);
        mLocalExtendedDirtyBits.set(gl::state::EXTENDED_DIRTY_BIT_POLYGON_MODE);
    }
}

void StateManagerGL::setPolygonOffsetPointEnabled(bool enabled)
{
    if (mState.polygonOffsetPointEnabled != enabled)
    {
        mState.polygonOffsetPointEnabled = enabled;
        SetGLBoolState(mFunctions, GL_POLYGON_OFFSET_POINT_NV, enabled);

        mLocalDirtyBits.set(gl::state::DIRTY_BIT_EXTENDED);
        mLocalExtendedDirtyBits.set(gl::state::EXTENDED_DIRTY_BIT_POLYGON_OFFSET_POINT_ENABLED);
    }
}

void StateManagerGL::setPolygonOffsetLineEnabled(bool enabled)
{
    if (mState.polygonOffsetLineEnabled != enabled)
    {
        mState.polygonOffsetLineEnabled = enabled;
        SetGLBoolState(mFunctions, GL_POLYGON_OFFSET_LINE_NV, enabled);

        mLocalDirtyBits.set(gl::state::DIRTY_BIT_EXTENDED);
        mLocalExtendedDirtyBits.set(gl::state::EXTENDED_DIRTY_BIT_POLYGON_OFFSET_LINE_ENABLED);
    }
}

void StateManagerGL::setPolygonOffsetFillEnabled(bool enabled)
{
    if (mState.polygonOffsetFillEnabled != enabled)
    {
        mState.polygonOffsetFillEnabled = enabled;
        SetGLBoolState(mFunctions, GL_POLYGON_OFFSET_FILL, enabled);

        mLocalDirtyBits.set(gl::state::DIRTY_BIT_POLYGON_OFFSET_FILL_ENABLED);
    }
}

void StateManagerGL::setPolygonOffset(float factor, float units, float clamp)
{
    if (mState.polygonOffsetFactor != factor || mState.polygonOffsetUnits != units ||
        mState.polygonOffsetClamp != clamp)
    {
        mState.polygonOffsetFactor = factor;
        mState.polygonOffsetUnits  = units;
        mState.polygonOffsetClamp  = clamp;

        if (clamp == 0.0f)
        {
            mFunctions->polygonOffset(factor, units);
        }
        else
        {
            ASSERT(mFunctions->polygonOffsetClampEXT);
            mFunctions->polygonOffsetClampEXT(factor, units, clamp);
        }

        mLocalDirtyBits.set(gl::state::DIRTY_BIT_POLYGON_OFFSET);
    }
}

void StateManagerGL::setDepthClampEnabled(bool enabled)
{
    if (mState.depthClampEnabled != enabled)
    {
        mState.depthClampEnabled = enabled;
        SetGLBoolState(mFunctions, GL_DEPTH_CLAMP_EXT, enabled);

        mLocalDirtyBits.set(gl::state::DIRTY_BIT_EXTENDED);
        mLocalExtendedDirtyBits.set(gl::state::EXTENDED_DIRTY_BIT_DEPTH_CLAMP_ENABLED);
    }
}

void StateManagerGL::setRasterizerDiscardEnabled(bool enabled)
{
    if (mState.rasterizerDiscardEnabled != enabled)
    {
        mState.rasterizerDiscardEnabled = enabled;
        SetGLBoolState(mFunctions, GL_RASTERIZER_DISCARD, enabled);

        mLocalDirtyBits.set(gl::state::DIRTY_BIT_RASTERIZER_DISCARD_ENABLED);
    }
}

void StateManagerGL::setLineWidth(float width)
{
    if (mState.lineWidth != width)
    {
        mState.lineWidth = width;
        mFunctions->lineWidth(width);

        mLocalDirtyBits.set(gl::state::DIRTY_BIT_LINE_WIDTH);
    }
}

angle::Result StateManagerGL::setPrimitiveRestartFixedIndexEnabled(const gl::Context *context,
                                                                   bool enabled)
{

    if (mState.primitiveRestartFixedIndexEnabled != enabled)
    {
        SetGLBoolState(mFunctions, GL_PRIMITIVE_RESTART_FIXED_INDEX, enabled);
        mState.primitiveRestartFixedIndexEnabled = enabled;

        mLocalDirtyBits.set(gl::state::DIRTY_BIT_PRIMITIVE_RESTART_ENABLED);
    }

    return angle::Result::Continue;
}

angle::Result StateManagerGL::setPrimitiveRestartEnabled(const gl::Context *context, bool enabled)
{
    if (mState.primitiveRestartEnabled != enabled)
    {
        ASSERT(mFeatures.emulatePrimitiveRestartFixedIndex.enabled);

        SetGLBoolState(mFunctions, GL_PRIMITIVE_RESTART, enabled);
        mState.primitiveRestartEnabled = enabled;

        mLocalDirtyBits.set(gl::state::DIRTY_BIT_PRIMITIVE_RESTART_ENABLED);
    }

    return angle::Result::Continue;
}

angle::Result StateManagerGL::setPrimitiveRestartIndex(const gl::Context *context, GLuint index)
{
    if (mState.primitiveRestartIndex != index)
    {
        ANGLE_GL_TRY(context, mFunctions->primitiveRestartIndex(index));
        mState.primitiveRestartIndex = index;

        // No dirty bit for this state, it is not exposed to the frontend.
    }

    return angle::Result::Continue;
}

void StateManagerGL::setClearDepth(float clearDepth)
{
    if (mState.clearDepth != clearDepth)
    {
        mState.clearDepth = clearDepth;

        // The glClearDepthf function isn't available until OpenGL 4.1.  Prefer it when it is
        // available because OpenGL ES only works in floats.
        if (mFunctions->clearDepthf)
        {
            mFunctions->clearDepthf(clearDepth);
        }
        else
        {
            ASSERT(mFunctions->clearDepth);
            mFunctions->clearDepth(clearDepth);
        }

        mLocalDirtyBits.set(gl::state::DIRTY_BIT_CLEAR_DEPTH);
    }
}

void StateManagerGL::setClearColor(const gl::ColorF &clearColor)
{
    if (mState.clearColor != clearColor)
    {
        mState.clearColor = clearColor;
        mFunctions->clearColor(clearColor.red, clearColor.green, clearColor.blue, clearColor.alpha);

        mLocalDirtyBits.set(gl::state::DIRTY_BIT_CLEAR_COLOR);
    }
}

void StateManagerGL::setClearStencil(GLint clearStencil)
{
    // Mask the clear stencil value to 1 byte before setting it.
    // The Desktop GL spec says the driver will mask when calling glClearStencil while the GLES spec
    // says it will only be masked when doing the clear. By masking it here, the value we track will
    // always be the same as what the driver tracks.
    GLint maskedClearValue = clearStencil & 0xFF;
    if (mState.clearStencil != maskedClearValue)
    {
        mState.clearStencil = maskedClearValue;
        mFunctions->clearStencil(maskedClearValue);

        mLocalDirtyBits.set(gl::state::DIRTY_BIT_CLEAR_STENCIL);
    }
}

angle::Result StateManagerGL::syncState(const gl::Context *context,
                                        const gl::state::DirtyBits &glDirtyBits,
                                        const gl::state::DirtyBits &bitMask,
                                        const gl::state::ExtendedDirtyBits &extendedDirtyBits,
                                        const gl::state::ExtendedDirtyBits &extendedBitMask)
{
    const gl::State &state = context->getState();

    const gl::state::DirtyBits glAndLocalDirtyBits = (glDirtyBits | mLocalDirtyBits) & bitMask;
    if (!glAndLocalDirtyBits.any())
    {
        return angle::Result::Continue;
    }

    // TODO(jmadill): Investigate only syncing vertex state for active attributes
    for (auto iter = glAndLocalDirtyBits.begin(), endIter = glAndLocalDirtyBits.end();
         iter != endIter; ++iter)
    {
        switch (*iter)
        {
            case gl::state::DIRTY_BIT_SCISSOR_TEST_ENABLED:
                setScissorTestEnabled(state.isScissorTestEnabled());
                break;
            case gl::state::DIRTY_BIT_SCISSOR:
            {
                const gl::Rectangle &scissor = state.getScissor();
                setScissor(scissor);
            }
            break;
            case gl::state::DIRTY_BIT_VIEWPORT:
            {
                const gl::Rectangle &viewport = state.getViewport();
                setViewport(viewport);
            }
            break;
            case gl::state::DIRTY_BIT_DEPTH_RANGE:
                setDepthRange(state.getNearPlane(), state.getFarPlane());
                break;
            case gl::state::DIRTY_BIT_BLEND_ENABLED:
                if (mIndependentBlendStates)
                {
                    setBlendEnabledIndexed(state.getBlendEnabledDrawBufferMask());
                }
                else
                {
                    setBlendEnabled(state.isBlendEnabled());
                }
                break;
            case gl::state::DIRTY_BIT_BLEND_COLOR:
                setBlendColor(state.getBlendColor());
                break;
            case gl::state::DIRTY_BIT_BLEND_FUNCS:
            {
                setBlendFuncs(state.getBlendStateExt());
                break;
            }
            case gl::state::DIRTY_BIT_BLEND_EQUATIONS:
            {
                setBlendEquations(state.getBlendStateExt());
                break;
            }
            case gl::state::DIRTY_BIT_COLOR_MASK:
            {
                const gl::Framebuffer *framebuffer = state.getDrawFramebuffer();
                const FramebufferGL *framebufferGL = GetImplAs<FramebufferGL>(framebuffer);
                const bool disableAlphaWrite =
                    framebufferGL->hasEmulatedAlphaChannelTextureAttachment();

                setColorMaskForFramebuffer(state.getBlendStateExt(), disableAlphaWrite);
                break;
            }
            case gl::state::DIRTY_BIT_SAMPLE_ALPHA_TO_COVERAGE_ENABLED:
                setSampleAlphaToCoverageEnabled(state.isSampleAlphaToCoverageEnabled());
                break;
            case gl::state::DIRTY_BIT_SAMPLE_COVERAGE_ENABLED:
                setSampleCoverageEnabled(state.isSampleCoverageEnabled());
                break;
            case gl::state::DIRTY_BIT_SAMPLE_COVERAGE:
                setSampleCoverage(state.getSampleCoverageValue(), state.getSampleCoverageInvert());
                break;
            case gl::state::DIRTY_BIT_DEPTH_TEST_ENABLED:
                setDepthTestEnabled(state.isDepthTestEnabled());
                break;
            case gl::state::DIRTY_BIT_DEPTH_FUNC:
                setDepthFunc(state.getDepthStencilState().depthFunc);
                break;
            case gl::state::DIRTY_BIT_DEPTH_MASK:
                setDepthMask(state.getDepthStencilState().depthMask);
                break;
            case gl::state::DIRTY_BIT_STENCIL_TEST_ENABLED:
                setStencilTestEnabled(state.isStencilTestEnabled());
                break;
            case gl::state::DIRTY_BIT_STENCIL_FUNCS_FRONT:
            {
                const auto &depthStencilState = state.getDepthStencilState();
                setStencilFrontFuncs(depthStencilState.stencilFunc, state.getStencilRef(),
                                     depthStencilState.stencilMask);
                break;
            }
            case gl::state::DIRTY_BIT_STENCIL_FUNCS_BACK:
            {
                const auto &depthStencilState = state.getDepthStencilState();
                setStencilBackFuncs(depthStencilState.stencilBackFunc, state.getStencilBackRef(),
                                    depthStencilState.stencilBackMask);
                break;
            }
            case gl::state::DIRTY_BIT_STENCIL_OPS_FRONT:
            {
                const auto &depthStencilState = state.getDepthStencilState();
                setStencilFrontOps(depthStencilState.stencilFail,
                                   depthStencilState.stencilPassDepthFail,
                                   depthStencilState.stencilPassDepthPass);
                break;
            }
            case gl::state::DIRTY_BIT_STENCIL_OPS_BACK:
            {
                const auto &depthStencilState = state.getDepthStencilState();
                setStencilBackOps(depthStencilState.stencilBackFail,
                                  depthStencilState.stencilBackPassDepthFail,
                                  depthStencilState.stencilBackPassDepthPass);
                break;
            }
            case gl::state::DIRTY_BIT_STENCIL_WRITEMASK_FRONT:
                setStencilFrontWritemask(state.getDepthStencilState().stencilWritemask);
                break;
            case gl::state::DIRTY_BIT_STENCIL_WRITEMASK_BACK:
                setStencilBackWritemask(state.getDepthStencilState().stencilBackWritemask);
                break;
            case gl::state::DIRTY_BIT_CULL_FACE_ENABLED:
                setCullFaceEnabled(state.isCullFaceEnabled());
                break;
            case gl::state::DIRTY_BIT_CULL_FACE:
                setCullFace(state.getRasterizerState().cullMode);
                break;
            case gl::state::DIRTY_BIT_FRONT_FACE:
                if (mFeatures.emulateClipOrigin.enabled)
                {
                    static_assert((GL_CW ^ GL_CCW) ==
                                  static_cast<GLenum>(gl::ClipOrigin::UpperLeft));
                    setFrontFace(state.getRasterizerState().frontFace ^
                                 static_cast<GLenum>(state.getClipOrigin()));
                    break;
                }
                setFrontFace(state.getRasterizerState().frontFace);
                break;
            case gl::state::DIRTY_BIT_POLYGON_OFFSET_FILL_ENABLED:
                setPolygonOffsetFillEnabled(state.isPolygonOffsetFillEnabled());
                break;
            case gl::state::DIRTY_BIT_POLYGON_OFFSET:
            {
                const auto &rasterizerState = state.getRasterizerState();
                setPolygonOffset(rasterizerState.polygonOffsetFactor,
                                 rasterizerState.polygonOffsetUnits,
                                 rasterizerState.polygonOffsetClamp);
                break;
            }
            case gl::state::DIRTY_BIT_RASTERIZER_DISCARD_ENABLED:
                setRasterizerDiscardEnabled(state.isRasterizerDiscardEnabled());
                break;
            case gl::state::DIRTY_BIT_LINE_WIDTH:
                setLineWidth(state.getLineWidth());
                break;
            case gl::state::DIRTY_BIT_PRIMITIVE_RESTART_ENABLED:
                if (mFeatures.emulatePrimitiveRestartFixedIndex.enabled)
                {
                    ANGLE_TRY(
                        setPrimitiveRestartEnabled(context, state.isPrimitiveRestartEnabled()));
                }
                else
                {
                    ANGLE_TRY(setPrimitiveRestartFixedIndexEnabled(
                        context, state.isPrimitiveRestartEnabled()));
                }
                break;
            case gl::state::DIRTY_BIT_CLEAR_COLOR:
                setClearColor(state.getColorClearValue());
                break;
            case gl::state::DIRTY_BIT_CLEAR_DEPTH:
                setClearDepth(state.getDepthClearValue());
                break;
            case gl::state::DIRTY_BIT_CLEAR_STENCIL:
                setClearStencil(state.getStencilClearValue());
                break;
            case gl::state::DIRTY_BIT_UNPACK_STATE:
                ANGLE_TRY(setPixelUnpackState(context, state.getUnpackState()));
                break;
            case gl::state::DIRTY_BIT_UNPACK_BUFFER_BINDING:
                ANGLE_TRY(setPixelUnpackBuffer(
                    context, state.getTargetBuffer(gl::BufferBinding::PixelUnpack)));
                break;
            case gl::state::DIRTY_BIT_PACK_STATE:
                ANGLE_TRY(setPixelPackState(context, state.getPackState()));
                break;
            case gl::state::DIRTY_BIT_PACK_BUFFER_BINDING:
                ANGLE_TRY(setPixelPackBuffer(context,
                                             state.getTargetBuffer(gl::BufferBinding::PixelPack)));
                break;
            case gl::state::DIRTY_BIT_DITHER_ENABLED:
                setDitherEnabled(state.isDitherEnabled());
                break;
            case gl::state::DIRTY_BIT_READ_FRAMEBUFFER_BINDING:
            {
                gl::Framebuffer *framebuffer = state.getReadFramebuffer();

                // Necessary for an Intel TexImage workaround.
                if (!framebuffer)
                {
                    continue;
                }

                FramebufferGL *framebufferGL = GetImplAs<FramebufferGL>(framebuffer);
                bindFramebuffer(
                    mHasSeparateFramebufferBindings ? GL_READ_FRAMEBUFFER : GL_FRAMEBUFFER,
                    framebufferGL->getFramebufferID());
                GetImplAs<ContextGL>(context)->tickGC();
                break;
            }
            case gl::state::DIRTY_BIT_DRAW_FRAMEBUFFER_BINDING:
            {
                gl::Framebuffer *framebuffer = state.getDrawFramebuffer();

                // Necessary for an Intel TexImage workaround.
                if (!framebuffer)
                {
                    continue;
                }

                FramebufferGL *framebufferGL = GetImplAs<FramebufferGL>(framebuffer);
                bindFramebuffer(
                    mHasSeparateFramebufferBindings ? GL_DRAW_FRAMEBUFFER : GL_FRAMEBUFFER,
                    framebufferGL->getFramebufferID());

                GetImplAs<ContextGL>(context)->tickGC();

                if (mFeatures.resetSampleCoverageOnFBOChange.enabled && mSampleCoverageEverChanged)
                {
                    forceSetSampleCoverage(mState.sampleCoverageValue, mState.sampleCoverageInvert);
                }

                const gl::ProgramExecutable *executable = state.getProgramExecutable();
                if (executable)
                {
                    updateMultiviewBaseViewLayerIndexUniform(executable, framebufferGL->getState());
                }

                // Changing the draw framebuffer binding sometimes requires resetting srgb blending.
                iter.setLaterBit(gl::state::DIRTY_BIT_FRAMEBUFFER_SRGB_WRITE_CONTROL_MODE);

                // If the framebuffer is emulating RGB on top of RGBA, the color mask has to be
                // updated
                iter.setLaterBit(gl::state::DIRTY_BIT_COLOR_MASK);
                break;
            }
            case gl::state::DIRTY_BIT_RENDERBUFFER_BINDING:
                // TODO(jmadill): implement this
                break;
            case gl::state::DIRTY_BIT_VERTEX_ARRAY_BINDING:
            {
                VertexArrayGL *vaoGL = GetImplAs<VertexArrayGL>(state.getVertexArray());
                bindVertexArray(vaoGL->getVertexArrayID());

                ANGLE_TRY(propagateProgramToVAO(context, state.getProgramExecutable(),
                                                GetImplAs<VertexArrayGL>(state.getVertexArray())));

                if (vaoGL->syncsToSharedState())
                {
                    // Re-sync the vertex array because all frontend VAOs share the same backend
                    // state. Only sync bits that can be set in ES2.0 or 3.0
                    gl::VertexArray::DirtyBits dirtyBits;
                    gl::VertexArray::DirtyAttribBitsArray dirtyAttribBits;
                    gl::VertexArray::DirtyBindingBitsArray dirtBindingBits;

                    dirtyBits.set(gl::VertexArray::DIRTY_BIT_ELEMENT_ARRAY_BUFFER);
                    for (GLint attrib = 0; attrib < context->getCaps().maxVertexAttributes;
                         attrib++)
                    {
                        dirtyBits.set(gl::VertexArray::DIRTY_BIT_ATTRIB_0 + attrib);
                        dirtyAttribBits[attrib].set(gl::VertexArray::DIRTY_ATTRIB_ENABLED);
                        dirtyAttribBits[attrib].set(gl::VertexArray::DIRTY_ATTRIB_POINTER);
                        dirtyAttribBits[attrib].set(gl::VertexArray::DIRTY_ATTRIB_POINTER_BUFFER);
                    }
                    for (GLint binding = 0; binding < context->getCaps().maxVertexAttribBindings;
                         binding++)
                    {
                        dirtyBits.set(gl::VertexArray::DIRTY_BIT_BINDING_0 + binding);
                        dirtBindingBits[binding].set(gl::VertexArray::DIRTY_BINDING_DIVISOR);
                    }

                    ANGLE_TRY(
                        vaoGL->syncState(context, dirtyBits, &dirtyAttribBits, &dirtBindingBits));
                }
                break;
            }
            case gl::state::DIRTY_BIT_DRAW_INDIRECT_BUFFER_BINDING:
                updateDrawIndirectBufferBinding(context);
                break;
            case gl::state::DIRTY_BIT_DISPATCH_INDIRECT_BUFFER_BINDING:
                updateDispatchIndirectBufferBinding(context);
                break;
            case gl::state::DIRTY_BIT_PROGRAM_BINDING:
                syncProgramState(context);
                break;
            case gl::state::DIRTY_BIT_PROGRAM_EXECUTABLE:
            {
                const gl::ProgramExecutable *executable = state.getProgramExecutable();

                if (executable)
                {
                    iter.setLaterBit(gl::state::DIRTY_BIT_TEXTURE_BINDINGS);

                    if (executable->getActiveImagesMask().any())
                    {
                        iter.setLaterBit(gl::state::DIRTY_BIT_IMAGE_BINDINGS);
                    }

                    if (executable->getShaderStorageBlocks().size() > 0)
                    {
                        iter.setLaterBit(gl::state::DIRTY_BIT_SHADER_STORAGE_BUFFER_BINDING);
                    }

                    if (executable->getUniformBlocks().size() > 0)
                    {
                        iter.setLaterBit(gl::state::DIRTY_BIT_UNIFORM_BUFFER_BINDINGS);
                    }

                    if (executable->getAtomicCounterBuffers().size() > 0)
                    {
                        iter.setLaterBit(gl::state::DIRTY_BIT_ATOMIC_COUNTER_BUFFER_BINDING);
                    }

                    if (mIsMultiviewEnabled && executable->usesMultiview())
                    {
                        updateMultiviewBaseViewLayerIndexUniform(
                            executable,
                            state.getDrawFramebuffer()->getImplementation()->getState());
                    }

                    // If the current executable does not use clip distances, the related API
                    // state has to be disabled to avoid runtime failures on certain drivers.
                    // On other drivers, that state is always emulated via a special uniform,
                    // which needs to be updated when switching programs.
                    if (mMaxClipDistances > 0)
                    {
                        iter.setLaterBit(gl::state::DIRTY_BIT_EXTENDED);
                        mLocalExtendedDirtyBits.set(gl::state::EXTENDED_DIRTY_BIT_CLIP_DISTANCES);
                    }

                    if (mFeatures.emulateClipOrigin.enabled)
                    {
                        updateEmulatedClipOriginUniform(executable, state.getClipOrigin());
                    }
                }

                if (!executable || !executable->hasLinkedShaderStage(gl::ShaderType::Compute))
                {
                    ANGLE_TRY(propagateProgramToVAO(
                        context, executable, GetImplAs<VertexArrayGL>(state.getVertexArray())));
                }
                break;
            }
            case gl::state::DIRTY_BIT_TEXTURE_BINDINGS:
                updateProgramTextureBindings(context);
                break;
            case gl::state::DIRTY_BIT_SAMPLER_BINDINGS:
                syncSamplersState(context);
                break;
            case gl::state::DIRTY_BIT_IMAGE_BINDINGS:
                updateProgramImageBindings(context);
                break;
            case gl::state::DIRTY_BIT_TRANSFORM_FEEDBACK_BINDING:
                syncTransformFeedbackState(context);
                break;
            case gl::state::DIRTY_BIT_SHADER_STORAGE_BUFFER_BINDING:
                updateProgramStorageBufferBindings(context);
                break;
            case gl::state::DIRTY_BIT_UNIFORM_BUFFER_BINDINGS:
                updateProgramUniformBufferBindings(context);
                break;
            case gl::state::DIRTY_BIT_ATOMIC_COUNTER_BUFFER_BINDING:
                updateProgramAtomicCounterBufferBindings(context);
                break;
            case gl::state::DIRTY_BIT_MULTISAMPLING:
                setMultisamplingStateEnabled(state.isMultisamplingEnabled());
                break;
            case gl::state::DIRTY_BIT_SAMPLE_ALPHA_TO_ONE:
                setSampleAlphaToOneStateEnabled(state.isSampleAlphaToOneEnabled());
                break;
            case gl::state::DIRTY_BIT_FRAMEBUFFER_SRGB_WRITE_CONTROL_MODE:
                setFramebufferSRGBEnabledForFramebuffer(
                    context, state.getFramebufferSRGB(),
                    GetImplAs<FramebufferGL>(state.getDrawFramebuffer()));
                break;
            case gl::state::DIRTY_BIT_SAMPLE_MASK_ENABLED:
                setSampleMaskEnabled(state.isSampleMaskEnabled());
                break;
            case gl::state::DIRTY_BIT_SAMPLE_MASK:
            {
                for (GLuint maskNumber = 0; maskNumber < state.getMaxSampleMaskWords();
                     ++maskNumber)
                {
                    setSampleMaski(maskNumber, state.getSampleMaskWord(maskNumber));
                }
                break;
            }
            case gl::state::DIRTY_BIT_CURRENT_VALUES:
            {
                gl::AttributesMask combinedMask =
                    (state.getAndResetDirtyCurrentValues() | mLocalDirtyCurrentValues);

                for (auto attribIndex : combinedMask)
                {
                    setAttributeCurrentData(attribIndex,
                                            state.getVertexAttribCurrentValue(attribIndex));
                }

                mLocalDirtyCurrentValues.reset();
                break;
            }
            case gl::state::DIRTY_BIT_PROVOKING_VERTEX:
                setProvokingVertex(ToGLenum(state.getProvokingVertex()));
                break;
            case gl::state::DIRTY_BIT_CLIP_CONTROL:
                if (mFeatures.emulateClipOrigin.enabled)
                {
                    setClipControlWithEmulatedClipOrigin(
                        state.getProgramExecutable(), state.getRasterizerState().frontFace,
                        state.getClipOrigin(), state.getClipDepthMode());
                    break;
                }
                setClipControl(state.getClipOrigin(), state.getClipDepthMode());
                break;
            case gl::state::DIRTY_BIT_EXTENDED:
            {
                const gl::state::ExtendedDirtyBits glAndLocalExtendedDirtyBits =
                    (extendedDirtyBits | mLocalExtendedDirtyBits) & extendedBitMask;
                for (size_t extendedDirtyBit : glAndLocalExtendedDirtyBits)
                {
                    switch (extendedDirtyBit)
                    {
                        case gl::state::EXTENDED_DIRTY_BIT_CLIP_DISTANCES:
                        {
                            const gl::ProgramExecutable *executable = state.getProgramExecutable();
                            if (executable && executable->hasClipDistance())
                            {
                                setClipDistancesEnable(state.getEnabledClipDistances());
                                if (mFeatures.emulateClipDistanceState.enabled)
                                {
                                    updateEmulatedClipDistanceState(
                                        executable, state.getEnabledClipDistances());
                                }
                            }
                            else
                            {
                                setClipDistancesEnable(gl::ClipDistanceEnableBits());
                            }
                            break;
                        }
                        case gl::state::EXTENDED_DIRTY_BIT_DEPTH_CLAMP_ENABLED:
                            setDepthClampEnabled(state.isDepthClampEnabled());
                            break;
                        case gl::state::EXTENDED_DIRTY_BIT_LOGIC_OP_ENABLED:
                            setLogicOpEnabled(state.isLogicOpEnabled());
                            break;
                        case gl::state::EXTENDED_DIRTY_BIT_LOGIC_OP:
                            setLogicOp(state.getLogicOp());
                            break;
                        case gl::state::EXTENDED_DIRTY_BIT_MIPMAP_GENERATION_HINT:
                            break;
                        case gl::state::EXTENDED_DIRTY_BIT_POLYGON_MODE:
                            setPolygonMode(state.getPolygonMode());
                            break;
                        case gl::state::EXTENDED_DIRTY_BIT_POLYGON_OFFSET_POINT_ENABLED:
                            setPolygonOffsetPointEnabled(state.isPolygonOffsetPointEnabled());
                            break;
                        case gl::state::EXTENDED_DIRTY_BIT_POLYGON_OFFSET_LINE_ENABLED:
                            setPolygonOffsetLineEnabled(state.isPolygonOffsetLineEnabled());
                            break;
                        case gl::state::EXTENDED_DIRTY_BIT_SHADER_DERIVATIVE_HINT:
                            // These hints aren't forwarded to GL yet.
                            break;
                        case gl::state::EXTENDED_DIRTY_BIT_SHADING_RATE_QCOM:
                        case gl::state::EXTENDED_DIRTY_BIT_SHADING_RATE_EXT:
                            // Unimplemented extensions.
                            break;
                        case gl::state::EXTENDED_DIRTY_BIT_BLEND_ADVANCED_COHERENT:
                            setBlendAdvancedCoherent(state.isBlendAdvancedCoherentEnabled());
                            break;
                        case gl::state::EXTENDED_DIRTY_BIT_FETCH_PER_SAMPLE_ENABLED:
                            break;
                        default:
                            UNREACHABLE();
                            break;
                    }
                    mLocalExtendedDirtyBits &= ~extendedBitMask;
                }
                break;
            }
            case gl::state::DIRTY_BIT_SAMPLE_SHADING:
                // Nothing to do until OES_sample_shading is implemented.
                break;
            case gl::state::DIRTY_BIT_PATCH_VERTICES:
                // Nothing to do until EXT_tessellation_shader is implemented.
                break;
            default:
                UNREACHABLE();
                break;
        }
    }

    mLocalDirtyBits &= ~bitMask;

    return angle::Result::Continue;
}

void StateManagerGL::setFramebufferSRGBEnabled(const gl::Context *context, bool enabled)
{
    if (!mFramebufferSRGBAvailable)
    {
        return;
    }

    if (mState.framebufferSRGBEnabled != enabled)
    {
        mState.framebufferSRGBEnabled = enabled;
        SetGLBoolState(mFunctions, GL_FRAMEBUFFER_SRGB, enabled);
        mLocalDirtyBits.set(gl::state::DIRTY_BIT_FRAMEBUFFER_SRGB_WRITE_CONTROL_MODE);
    }
}

void StateManagerGL::setFramebufferSRGBEnabledForFramebuffer(const gl::Context *context,
                                                             bool enabled,
                                                             const FramebufferGL *framebuffer)
{
    if (framebuffer->isDefault())
    {
        // Obey the framebuffer sRGB state for blending on all framebuffers except the default
        // framebuffer.
        // When SRGB blending is enabled, only SRGB capable formats will use it but the default
        // framebuffer will always use it if it is enabled.
        // TODO(geofflang): Update this when the framebuffer binding dirty changes, when it exists.
        setFramebufferSRGBEnabled(context, false);
    }
    else
    {
        setFramebufferSRGBEnabled(context, enabled);
    }
}

void StateManagerGL::setColorMaskForFramebuffer(const gl::BlendStateExt &blendState,
                                                const bool disableAlpha)
{
    bool r, g, b, a;

    // Given that disableAlpha can be true only on macOS backbuffers and color mask is re-synced on
    // bound draw framebuffer change, switch all draw buffers color masks to avoid special case
    // later.
    if (!mIndependentBlendStates || disableAlpha)
    {
        blendState.getColorMaskIndexed(0, &r, &g, &b, &a);
        setColorMask(r, g, b, disableAlpha ? false : a);
        return;
    }

    // Check if the current mask already matches the new state
    if (mState.blendState.getColorMaskBits() == blendState.getColorMaskBits())
    {
        return;
    }

    // Get DrawBufferMask of buffers with different color masks
    gl::DrawBufferMask diffMask = mState.blendState.compareColorMask(blendState.getColorMaskBits());
    size_t diffCount            = diffMask.count();

    // Check if setting all buffers to the same value reduces the number of subsequent indexed
    // commands. Implicitly handles the case when the new mask is the same for all buffers.
    // For instance, let's say that previously synced mask is ccccff00 and the new state is
    // ffeeeeee. Instead of calling colorMaski 8 times, ANGLE can set all buffers to `e` and then
    // use colorMaski only twice. On the other hand, if the new state is cceeee00, a non-indexed
    // call will increase the total number of GL commands.
    if (diffCount > 1)
    {
        bool found                                                = false;
        gl::BlendStateExt::ColorMaskStorage::Type commonColorMask = 0;
        for (size_t i = 0; i < mState.blendState.getDrawBufferCount() - 1; i++)
        {
            const gl::BlendStateExt::ColorMaskStorage::Type tempCommonColorMask =
                blendState.expandColorMaskIndexed(i);
            const gl::DrawBufferMask tempDiffMask =
                blendState.compareColorMask(tempCommonColorMask);
            const size_t tempDiffCount = tempDiffMask.count();
            if (tempDiffCount < diffCount)
            {
                found           = true;
                diffMask        = tempDiffMask;
                diffCount       = tempDiffCount;
                commonColorMask = tempCommonColorMask;
                if (tempDiffCount == 0)
                {
                    break;  // the new mask is the same for all buffers
                }
            }
        }
        if (found)
        {
            gl::BlendStateExt::UnpackColorMask(commonColorMask, &r, &g, &b, &a);
            mFunctions->colorMask(r, g, b, a);
        }
    }

    for (size_t drawBufferIndex : diffMask)
    {
        blendState.getColorMaskIndexed(drawBufferIndex, &r, &g, &b, &a);
        mFunctions->colorMaski(static_cast<GLuint>(drawBufferIndex), r, g, b, a);
    }

    mState.blendState.setColorMaskBits(blendState.getColorMaskBits());
    mLocalDirtyBits.set(gl::state::DIRTY_BIT_COLOR_MASK);
}

void StateManagerGL::setDitherEnabled(bool enabled)
{
    if (mState.ditherEnabled != enabled)
    {
        mState.ditherEnabled = enabled;
        SetGLBoolState(mFunctions, GL_DITHER, enabled);
        mLocalDirtyBits.set(gl::state::DIRTY_BIT_DITHER_ENABLED);
    }
}

void StateManagerGL::setMultisamplingStateEnabled(bool enabled)
{
    if (mState.multisamplingEnabled != enabled)
    {
        mState.multisamplingEnabled = enabled;
        SetGLBoolState(mFunctions, GL_MULTISAMPLE, enabled);
        mLocalDirtyBits.set(gl::state::DIRTY_BIT_MULTISAMPLING);
    }
}

void StateManagerGL::setSampleAlphaToOneStateEnabled(bool enabled)
{
    if (mState.sampleAlphaToOneEnabled != enabled)
    {
        mState.sampleAlphaToOneEnabled = enabled;
        SetGLBoolState(mFunctions, GL_SAMPLE_ALPHA_TO_ONE, enabled);
        mLocalDirtyBits.set(gl::state::DIRTY_BIT_SAMPLE_ALPHA_TO_ONE);
    }
}

void StateManagerGL::setProvokingVertex(GLenum mode)
{
    if (mode != mState.provokingVertex)
    {
        mFunctions->provokingVertex(mode);
        mState.provokingVertex = mode;

        mLocalDirtyBits.set(gl::state::DIRTY_BIT_PROVOKING_VERTEX);
    }
}

void StateManagerGL::setClipDistancesEnable(const gl::ClipDistanceEnableBits &enables)
{
    if (enables == mState.enabledClipDistances)
    {
        return;
    }
    ASSERT(mMaxClipDistances <= gl::IMPLEMENTATION_MAX_CLIP_DISTANCES);

    gl::ClipDistanceEnableBits diff = enables ^ mState.enabledClipDistances;
    for (size_t i : diff)
    {
        SetGLBoolState(mFunctions, GL_CLIP_DISTANCE0_EXT + static_cast<uint32_t>(i),
                       enables.test(i));
    }

    mState.enabledClipDistances = enables;
    mLocalDirtyBits.set(gl::state::DIRTY_BIT_EXTENDED);
    mLocalExtendedDirtyBits.set(gl::state::EXTENDED_DIRTY_BIT_CLIP_DISTANCES);
}

void StateManagerGL::setLogicOpEnabled(bool enabled)
{
    if (enabled == mState.logicOpEnabled)
    {
        return;
    }
    mState.logicOpEnabled = enabled;

    SetGLBoolState(mFunctions, GL_COLOR_LOGIC_OP, enabled);

    mLocalDirtyBits.set(gl::state::DIRTY_BIT_EXTENDED);
    mLocalExtendedDirtyBits.set(gl::state::EXTENDED_DIRTY_BIT_LOGIC_OP_ENABLED);
}

void StateManagerGL::setLogicOp(gl::LogicalOperation opcode)
{
    if (opcode == mState.logicOp)
    {
        return;
    }
    mState.logicOp = opcode;

    mFunctions->logicOp(ToGLenum(opcode));

    mLocalDirtyBits.set(gl::state::DIRTY_BIT_EXTENDED);
    mLocalExtendedDirtyBits.set(gl::state::EXTENDED_DIRTY_BIT_LOGIC_OP_ENABLED);
}

void StateManagerGL::setTextureCubemapSeamlessEnabled(bool enabled)
{
    if (!nativegl::SupportsSettingCubemapSeamless(mFunctions))
    {
        return;
    }

    if (mState.textureCubemapSeamlessEnabled != enabled)
    {
        mState.textureCubemapSeamlessEnabled = enabled;
        SetGLBoolState(mFunctions, GL_TEXTURE_CUBE_MAP_SEAMLESS, enabled);
    }
}

angle::Result StateManagerGL::propagateProgramToVAO(const gl::Context *context,
                                                    const gl::ProgramExecutable *executable,
                                                    VertexArrayGL *vao)
{
    if (vao == nullptr)
    {
        return angle::Result::Continue;
    }

    // Number of views:
    if (mIsMultiviewEnabled)
    {
        int numViews = 1;
        if (executable && executable->usesMultiview())
        {
            numViews = executable->getNumViews();
        }
        ANGLE_TRY(vao->applyNumViewsToDivisor(context, numViews));
    }

    // Attribute enabled mask:
    if (executable)
    {
        ANGLE_TRY(vao->applyActiveAttribLocationsMask(context,
                                                      executable->getActiveAttribLocationsMask()));
    }

    return angle::Result::Continue;
}

void StateManagerGL::updateMultiviewBaseViewLayerIndexUniformImpl(
    const gl::ProgramExecutable *executable,
    const gl::FramebufferState &drawFramebufferState) const
{
    ASSERT(mIsMultiviewEnabled && executable && executable->usesMultiview());
    const ProgramExecutableGL *executableGL = GetImplAs<ProgramExecutableGL>(executable);
    if (drawFramebufferState.isMultiview())
    {
        executableGL->enableLayeredRenderingPath(drawFramebufferState.getBaseViewIndex());
    }
}
void StateManagerGL::setDefaultVAOState(const VertexArrayStateGL &state, bool isHardenedContext)
{
    if (mState.defaultVAOState == state)
    {
        return;
    }

    bindVertexArray(0);

    const bool supportsBindings = nativegl::SupportsVertexAttributeBindings(mFunctions);
    const bool supportsInstancing = nativegl::SupportsInstancing(mFunctions);

    bindBuffer(gl::BufferBinding::ElementArray, state.elementArrayBuffer, isHardenedContext);
    for (GLint i = 0; i < mCaps.maxVertexAttributes; i++)
    {
        VertexAttributeGL &curAttrib       = mState.defaultVAOState.attributes[i];
        const VertexAttributeGL &newAttrib = state.attributes[i];
        if (curAttrib.enabled != newAttrib.enabled)
        {
            SetVertexAttribArrayEnabled(mFunctions, i, newAttrib.enabled);
            curAttrib.enabled = newAttrib.enabled;
        }

        // If vertex bindings are supported and the binding index has been changed, set all state
        // with the binding functions. Otherwise use the old glVertexAttribPointer setting
        // functions.
        if (supportsBindings && newAttrib.bindingIndex != curAttrib.bindingIndex)
        {
            mFunctions->vertexAttribBinding(i, newAttrib.bindingIndex);
            curAttrib.bindingIndex = newAttrib.bindingIndex;

            if (curAttrib.format != newAttrib.format ||
                curAttrib.relativeOffset != newAttrib.relativeOffset)
            {
                mFunctions->vertexAttribFormat(i, newAttrib.format->channelCount,
                                               gl::ToGLenum(newAttrib.format->vertexAttribType),
                                               newAttrib.format->isNorm(),
                                               newAttrib.relativeOffset);
                curAttrib.format         = newAttrib.format;
                curAttrib.relativeOffset = newAttrib.relativeOffset;
            }
        }
        else
        {
            VertexBindingGL &curBinding = mState.defaultVAOState.bindings[curAttrib.bindingIndex];
            const VertexBindingGL &newBinding = state.bindings[newAttrib.bindingIndex];

            ASSERT(newAttrib.bindingIndex == curAttrib.bindingIndex);

            if (curAttrib.format != newAttrib.format || curAttrib.pointer != newAttrib.pointer ||
                curBinding.buffer != newBinding.buffer || curBinding.stride != newBinding.stride)
            {
                bindBuffer(gl::BufferBinding::Array, newBinding.buffer, isHardenedContext);
                mFunctions->vertexAttribPointer(i, newAttrib.format->channelCount,
                                                gl::ToGLenum(newAttrib.format->vertexAttribType),
                                                newAttrib.format->isNorm(), newBinding.stride,
                                                newAttrib.pointer);
                curAttrib.format         = newAttrib.format;
                curAttrib.pointer        = newAttrib.pointer;
                curAttrib.relativeOffset = 0;
                curBinding.buffer        = newBinding.buffer;
                curBinding.offset =
                    newBinding.buffer ? reinterpret_cast<GLintptr>(newAttrib.pointer) : 0;
                curBinding.stride = newBinding.stride;
            }

            if (curBinding.divisor != newBinding.divisor)
            {
                ASSERT(supportsInstancing);
                mFunctions->vertexAttribDivisor(i, newBinding.divisor);
                curBinding.divisor = newBinding.divisor;
            }
        }
    }

    for (GLint i = 0; i < mCaps.maxVertexAttribBindings; i++)
    {
        VertexBindingGL &curBinding       = mState.defaultVAOState.bindings[i];
        const VertexBindingGL &newBinding = state.bindings[i];

        if (curBinding.buffer != newBinding.buffer || curBinding.offset != newBinding.offset ||
            curBinding.stride != newBinding.stride)
        {
            ASSERT(supportsBindings);
            mFunctions->bindVertexBuffer(i, newBinding.buffer, newBinding.offset,
                                         newBinding.stride);
            curBinding.buffer = newBinding.buffer;
            curBinding.stride = newBinding.stride;
            curBinding.offset = newBinding.offset;
        }

        if (curBinding.divisor != newBinding.divisor)
        {
            ASSERT(supportsBindings);
            ASSERT(supportsInstancing);
            mFunctions->vertexBindingDivisor(i, newBinding.divisor);
            curBinding.divisor = newBinding.divisor;
        }
    }

#if defined(ANGLE_EANBLE_ASSERTS)
    if (mState.defaultVAOState != state)
    {
        std::ostringstream msg;
        msg << "VAO state note equal after applying!" << std::endl;
        msg << "mState.defaultVAOState:" << std::endl
            << mState.defaultVAOState << std::endl
            << std::endl;
        msg << "state:" << std::endl << state << std::endl;
        FATAL() << msg.str();
    }
#endif

    mLocalDirtyBits.set(gl::state::DIRTY_BIT_VERTEX_ARRAY_BINDING);
}

angle::Result StateManagerGL::setState(const gl::Context *context, const ContextStateGL &state)
{
    useProgram(state.program);
    setDefaultVAOState(state.defaultVAOState,
                       context->isHardenedContext());  // Set default VAO state before binding the
                                                       // target vao
    bindVertexArray(state.vao);
    for (size_t attribIndex = 0; attribIndex < state.vertexAttribCurrentValues.size();
         attribIndex++)
    {
        setAttributeCurrentData(attribIndex, state.vertexAttribCurrentValues[attribIndex]);
    }
    for (gl::BufferBinding bufferBinding : angle::AllEnums<gl::BufferBinding>())
    {
        if (bufferBinding == gl::BufferBinding::TransformFeedback)
        {
            continue;
        }

        // Apply the indexed bindings first since they change the generic binding.
        const std::vector<IndexedBufferBindingGL> &indexedBindings =
            state.indexedBuffers[bufferBinding];
        for (size_t i = 0; i < indexedBindings.size(); i++)
        {
            const IndexedBufferBindingGL &binding = indexedBindings[i];
            if (binding.size == 0)
            {
                ASSERT(binding.offset == 0);
                bindBufferBase(bufferBinding, i, binding.buffer);
            }
            else
            {
                bindBufferRange(bufferBinding, i, binding.buffer, binding.offset, binding.size);
            }
        }

        bindBuffer(bufferBinding, state.buffers[bufferBinding], context->isHardenedContext());
    }
    for (size_t textureUnit = 0; textureUnit < gl::IMPLEMENTATION_MAX_ACTIVE_TEXTURES;
         textureUnit++)
    {
        for (gl::TextureType textureType : angle::AllEnums<gl::TextureType>())
        {
            bindTexture(textureType, textureUnit, state.textures[textureType][textureUnit]);
        }

        bindSampler(textureUnit, state.samplers[textureUnit]);
    }
    // Apply the target texture type after syncing all textures
    activeTexture(state.textureUnitIndex);
    for (size_t imageUnit = 0; imageUnit < state.images.size(); imageUnit++)
    {
        const ImageUnitBindingGL &binding = state.images[imageUnit];
        bindImageTexture(imageUnit, binding.texture, binding.level, binding.layered, binding.layer,
                         binding.access, binding.format);
    }
    bindTransformFeedback(GL_TRANSFORM_FEEDBACK, state.transformFeedback);

    ANGLE_TRY(setPixelUnpackState(context, state.unpackState));
    ANGLE_TRY(setPixelPackState(context, state.packState));
    if (!mHasSeparateFramebufferBindings || state.framebuffers[angle::FramebufferBindingDraw] ==
                                                state.framebuffers[angle::FramebufferBindingRead])
    {
        bindFramebuffer(GL_FRAMEBUFFER, state.framebuffers[angle::FramebufferBindingDraw]);
    }
    else
    {
        bindFramebuffer(GL_DRAW_FRAMEBUFFER, state.framebuffers[angle::FramebufferBindingDraw]);
        bindFramebuffer(GL_READ_FRAMEBUFFER, state.framebuffers[angle::FramebufferBindingRead]);
    }
    bindRenderbuffer(GL_RENDERBUFFER, state.renderbuffer);
    setScissorTestEnabled(state.scissorTestEnabled);
    setScissor(state.scissor);
    setViewport(state.viewport);
    setDepthRange(state.near, state.far);
    setClipControl(state.clipOrigin, state.clipDepthMode);
    setBlendColor(state.blendColor);
    setBlendEnabledIndexed(state.blendState.getEnabledMask());
    setColorMaskForFramebuffer(state.blendState, false);
    setBlendFuncs(state.blendState);
    setBlendEquations(state.blendState);
    setBlendAdvancedCoherent(state.blendAdvancedCoherent);
    setSampleAlphaToCoverageEnabled(state.sampleAlphaToCoverageEnabled);
    setSampleCoverageEnabled(state.sampleCoverageEnabled);
    setSampleCoverage(state.sampleCoverageValue, state.sampleCoverageInvert);
    setSampleMaskEnabled(state.sampleMaskEnabled);
    setSampleMask(state.sampleMaskValues);
    setDepthTestEnabled(state.depthTestEnabled);
    setDepthFunc(state.depthFunc);
    setDepthMask(state.depthMask);
    setStencilTestEnabled(state.stencilTestEnabled);
    setStencilFrontFuncs(state.stencilFrontFunc, state.stencilFrontRef,
                         state.stencilFrontValueMask);
    setStencilFrontOps(state.stencilFrontStencilFailOp, state.stencilFrontStencilPassDepthFailOp,
                       state.stencilFrontStencilPassDepthPassOp);
    setStencilFrontWritemask(state.stencilFrontWritemask);
    setStencilBackFuncs(state.stencilBackFunc, state.stencilBackRef, state.stencilBackValueMask);
    setStencilBackOps(state.stencilBackStencilFailOp, state.stencilBackStencilPassDepthFailOp,
                      state.stencilBackStencilPassDepthPassOp);
    setStencilBackWritemask(state.stencilBackWritemask);
    setCullFaceEnabled(state.cullFaceEnabled);
    setCullFace(state.cullFace);
    setFrontFace(state.frontFace);
    setPolygonMode(state.polygonMode);
    setPolygonOffsetPointEnabled(state.polygonOffsetPointEnabled);
    setPolygonOffsetLineEnabled(state.polygonOffsetLineEnabled);
    setPolygonOffsetFillEnabled(state.polygonOffsetFillEnabled);
    setPolygonOffset(state.polygonOffsetFactor, state.polygonOffsetUnits, state.polygonOffsetClamp);
    setDepthClampEnabled(state.depthClampEnabled);
    setRasterizerDiscardEnabled(state.rasterizerDiscardEnabled);
    setLineWidth(state.lineWidth);
    ANGLE_TRY(
        setPrimitiveRestartFixedIndexEnabled(context, state.primitiveRestartFixedIndexEnabled));
    ANGLE_TRY(setPrimitiveRestartEnabled(context, state.primitiveRestartEnabled));
    ANGLE_TRY(setPrimitiveRestartIndex(context, state.primitiveRestartIndex));
    setClearColor(state.clearColor);
    setClearDepth(state.clearDepth);
    setClearStencil(state.clearStencil);
    setFramebufferSRGBEnabled(context, state.framebufferSRGBEnabled);
    setDitherEnabled(state.ditherEnabled);
    setTextureCubemapSeamlessEnabled(state.textureCubemapSeamlessEnabled);
    setMultisamplingStateEnabled(state.multisamplingEnabled);
    setSampleAlphaToOneStateEnabled(state.sampleAlphaToOneEnabled);
    setProvokingVertex(state.provokingVertex);
    setClipDistancesEnable(state.enabledClipDistances);
    setLogicOpEnabled(state.logicOpEnabled);
    setLogicOp(state.logicOp);

#if defined(ANGLE_ENABLE_ASSERTS)
    if (mState != state)
    {
        std::ostringstream msg;
        msg << "StateManagerGL::mState does not match new state after setState is called."
            << std::endl;
        msg << "StateManagerGL::mState:" << std::endl << mState << std::endl << std::endl;
        msg << "new state:" << std::endl << state << std::endl;
        FATAL() << msg.str();
    }
#endif

    return angle::Result::Continue;
}

void StateManagerGL::updateEmulatedClipDistanceState(const gl::ProgramExecutable *executable,
                                                     const gl::ClipDistanceEnableBits enables) const
{
    ASSERT(mFeatures.emulateClipDistanceState.enabled);
    ASSERT(executable && executable->hasClipDistance());
    const ProgramExecutableGL *executableGL = GetImplAs<ProgramExecutableGL>(executable);
    executableGL->updateEnabledClipDistances(static_cast<uint8_t>(enables.bits()));
}

void StateManagerGL::syncSamplersState(const gl::Context *context)
{
    const gl::SamplerBindingVector &samplers = context->getState().getSamplers();

    // This could be optimized by using a separate binding dirty bit per sampler.
    for (size_t samplerIndex = 0; samplerIndex < samplers.size(); ++samplerIndex)
    {
        const gl::Sampler *sampler = samplers[samplerIndex].get();
        if (sampler != nullptr)
        {
            SamplerGL *samplerGL = GetImplAs<SamplerGL>(sampler);
            bindSampler(samplerIndex, samplerGL->getSamplerID());
        }
        else
        {
            bindSampler(samplerIndex, 0);
        }
    }
}

void StateManagerGL::syncTransformFeedbackState(const gl::Context *context)
{
    // Set the current transform feedback state
    gl::TransformFeedback *transformFeedback = context->getState().getCurrentTransformFeedback();
    if (transformFeedback)
    {
        TransformFeedbackGL *transformFeedbackGL =
            GetImplAs<TransformFeedbackGL>(transformFeedback);
        bindTransformFeedback(GL_TRANSFORM_FEEDBACK, transformFeedbackGL->getTransformFeedbackID());
        transformFeedbackGL->syncActiveState(context, transformFeedback->isActive(),
                                             transformFeedback->getPrimitiveMode());
        transformFeedbackGL->syncPausedState(transformFeedback->isPaused());
        mCurrentTransformFeedback = transformFeedbackGL;
    }
    else
    {
        bindTransformFeedback(GL_TRANSFORM_FEEDBACK, 0);
        mCurrentTransformFeedback = nullptr;
    }

    syncProgramState(context);
}

void StateManagerGL::syncProgramState(const gl::Context *context)
{
    gl::Program *program = context->getState().getProgram();
    if (program != nullptr)
    {
        ProgramGL *programGL = GetImplAs<ProgramGL>(program);
        useProgram(programGL->getProgramID());
    }
}

GLuint StateManagerGL::getDefaultVAO() const
{
    return mDefaultVAO;
}

void StateManagerGL::setDefaultVAOStateDirty()
{
    mLocalDirtyBits.set(gl::state::DIRTY_BIT_VERTEX_ARRAY_BINDING);
}

VertexArrayStateGL *StateManagerGL::getVAOState(GLuint vao)
{
    // Special case: The default VAO state is stored in ContextStateGL
    if (vao == mDefaultVAO)
    {
        return &mState.defaultVAOState;
    }

    auto iter = mVAOStates.find(vao);
    return (iter != mVAOStates.end()) ? &iter->second : nullptr;
}

VertexArrayStateGL *StateManagerGL::getOrCreateVAOState(GLuint vao)
{
    VertexArrayStateGL *vaoState = getVAOState(vao);
    if (vaoState != nullptr)
    {
        return vaoState;
    }

    auto insertResult = mVAOStates.insert(std::pair{
        vao, VertexArrayStateGL(mCaps.maxVertexAttributes, mCaps.maxVertexAttribBindings)});
    ASSERT(insertResult.second);
    return &insertResult.first->second;
}

VertexArrayStateGL *StateManagerGL::getCurrentVAOState()
{
    return getVAOState(mState.vao);
}

void StateManagerGL::validateState()
{
    ensurePlaceholderFramebuffer();

    ContextStateGL queriedState(mCaps);
    QueryContextStateGL(mFunctions, mPlaceholderFbo, &queriedState);
    if (mState != queriedState)
    {
        std::ostringstream msg;
        msg << "Queried state does not match tracked state!" << std::endl;
        msg << "Tracked state:" << std::endl << mState << std::endl << std::endl;
        msg << "Queried state:" << std::endl << queriedState << std::endl;
        FATAL() << msg.str();
    }
}
void StateManagerGL::setBufferBindingDirty(gl::BufferBinding binding)
{
    std::optional<gl::state::DirtyBitType> bit = GetBufferBindingDirtyBit(binding);
    if (bit.has_value())
    {
        mLocalDirtyBits.set(bit.value());
    }
}

std::unique_ptr<ContextStateGL> StateManagerGL::createContextStateGL() const
{
    return std::make_unique<ContextStateGL>(mCaps);
}

angle::Result StateManagerGL::syncFromNativeContext(const gl::Context *context,
                                                    ContextStateGL *outNativeContextState)
{
    auto *platform   = ANGLEPlatformCurrent();
    double startTime = platform->currentTime(platform);

    ensurePlaceholderFramebuffer();

    // Query the current driver state into outNativeContextState so that it can be restored later.
    QueryContextStateGL(mFunctions, mPlaceholderFbo, outNativeContextState);

    // Compute the dirty bits between the currently tracked state and the queried state in
    // outNativeContextState. This will ensure that all frontend states will be re-applied in the
    // next syncState.
    ComputeDirtyBitsBetweenStates(mState, *outNativeContextState, &mLocalDirtyBits,
                                  &mLocalExtendedDirtyBits, &mLocalDirtyCurrentValues);

    // Set the currently tracked state to the queried state so that our understanding of the driver
    // state is correct.
    mState = *outNativeContextState;

    double delta = platform->currentTime(platform) - startTime;
    int us       = static_cast<int>(delta * 1000000.0);
    ANGLE_HISTOGRAM_COUNTS("GPU.ANGLE.SyncFromNativeContextMicroseconds", us);

    return angle::Result::Continue;
}

angle::Result StateManagerGL::restoreNativeContext(const gl::Context *context,
                                                   const ContextStateGL &nativeContextState)
{
    // Simply apply all state from the external nativeContextState
    ANGLE_TRY(setState(context, nativeContextState));
    ASSERT(mState == nativeContextState);
    return angle::Result::Continue;
}

void StateManagerGL::ensurePlaceholderFramebuffer()
{
    if (mPlaceholderFbo)
    {
        return;
    }

    GLenum framebufferBinding =
        mHasSeparateFramebufferBindings ? GL_DRAW_FRAMEBUFFER : GL_FRAMEBUFFER;
    mFunctions->genFramebuffers(1, &mPlaceholderFbo);
    mFunctions->bindFramebuffer(framebufferBinding, mPlaceholderFbo);

    mFunctions->genRenderbuffers(1, &mPlaceholderFboColorRenderbuffer);
    mFunctions->bindRenderbuffer(GL_RENDERBUFFER, mPlaceholderFboColorRenderbuffer);
    mFunctions->renderbufferStorage(GL_RENDERBUFFER, GL_RGBA8, 2, 2);
    mFunctions->framebufferRenderbuffer(framebufferBinding, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER,
                                        mPlaceholderFboColorRenderbuffer);

    mFunctions->genRenderbuffers(1, &mPlaceholderFboDepthStencilRenderbuffer);
    mFunctions->bindRenderbuffer(GL_RENDERBUFFER, mPlaceholderFboDepthStencilRenderbuffer);
    mFunctions->renderbufferStorage(GL_RENDERBUFFER, GL_DEPTH24_STENCIL8, 2, 2);
    mFunctions->framebufferRenderbuffer(framebufferBinding, GL_DEPTH_STENCIL_ATTACHMENT,
                                        GL_RENDERBUFFER, mPlaceholderFboDepthStencilRenderbuffer);

    // This ensures renderbuffer attachment is not lazy.
    mFunctions->checkFramebufferStatus(framebufferBinding);

    // Reset state
    mFunctions->bindFramebuffer(framebufferBinding,
                                mState.framebuffers[angle::FramebufferBindingDraw]);
    mFunctions->bindRenderbuffer(GL_RENDERBUFFER, mState.renderbuffer);
}

}  // namespace rx
