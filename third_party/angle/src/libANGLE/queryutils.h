//
// Copyright 2016 The ANGLE Project Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//

// queryutils.h: Utilities for querying values from GL objects

#ifndef LIBANGLE_QUERYUTILS_H_
#define LIBANGLE_QUERYUTILS_H_

#include "angle_gl.h"
#include "common/PackedEnums.h"
#include "common/angleutils.h"
#include "libANGLE/Error.h"

#include <EGL/egl.h>

namespace gl
{
class Buffer;
class Context;
class Sync;
class Framebuffer;
class GLES1State;
class Program;
class Renderbuffer;
class Sampler;
class Shader;
class State;
class Texture;
struct TextureCaps;
struct UniformBlock;
struct VertexAttribute;
class VertexBinding;
struct VertexAttribCurrentValueData;

void QueryFramebufferAttachmentParameteriv(const Context *context,
                                           const Framebuffer *framebuffer,
                                           GLenum attachment,
                                           GLenum pname,
                                           GLint *params);
void QueryBufferParameteriv(const Buffer *buffer, BufferParam pnamePacked, GLint *params);
void QueryBufferParameteri64v(const Buffer *buffer, BufferParam pnamePacked, GLint64 *params);
void QueryProgramiv(Context *context, Program *program, GLenum pname, GLint *params);
void QueryRenderbufferiv(const Context *context,
                         const Renderbuffer *renderbuffer,
                         GLenum pname,
                         GLint *params);
void QueryShaderiv(const Context *context,
                   Shader *shader,
                   ShaderParameter pnamePacked,
                   GLint *params);
void QueryTexLevelParameterfv(const Texture *texture,
                              TextureTarget targetPacked,
                              GLint level,
                              TextureImageParameter pnamePacked,
                              GLfloat *params);
void QueryTexLevelParameteriv(const Texture *texture,
                              TextureTarget targetPacked,
                              GLint level,
                              TextureImageParameter pnamePacked,
                              GLint *params);
void QueryTexParameterfv(const Context *context,
                         const Texture *texture,
                         GLenum pname,
                         GLfloat *params);
void QueryTexParameterxv(const Context *context,
                         const Texture *texture,
                         GLenum pname,
                         GLfixed *params);
void QueryTexParameteriv(const Context *context,
                         const Texture *texture,
                         GLenum pname,
                         GLint *params);
void QueryTexParameterIiv(const Context *context,
                          const Texture *texture,
                          GLenum pname,
                          GLint *params);
void QueryTexParameterIuiv(const Context *context,
                           const Texture *texture,
                           GLenum pname,
                           GLuint *params);
void QuerySamplerParameterfv(const Sampler *sampler, SamplerParameter pnamePacked, GLfloat *params);
void QuerySamplerParameteriv(const Sampler *sampler, SamplerParameter pnamePacked, GLint *params);
void QuerySamplerParameterIiv(const Sampler *sampler, SamplerParameter pnamePacked, GLint *params);
void QuerySamplerParameterIuiv(const Sampler *sampler,
                               SamplerParameter pnamePacked,
                               GLuint *params);

// Warning: you should ensure binding really matches attrib.bindingIndex before using the following
// functions.
void QueryVertexAttribfv(const VertexAttribute &attrib,
                         const VertexBinding &binding,
                         const Buffer *buffer,
                         const VertexAttribCurrentValueData &currentValueData,
                         GLenum pname,
                         GLfloat *params);

void QueryVertexAttribiv(const VertexAttribute &attrib,
                         const VertexBinding &binding,
                         const Buffer *buffer,
                         const VertexAttribCurrentValueData &currentValueData,
                         GLenum pname,
                         GLint *params);

void QueryVertexAttribIiv(const VertexAttribute &attrib,
                          const VertexBinding &binding,
                          const Buffer *buffer,
                          const VertexAttribCurrentValueData &currentValueData,
                          GLenum pname,
                          GLint *params);

void QueryVertexAttribIuiv(const VertexAttribute &attrib,
                           const VertexBinding &binding,
                           const Buffer *buffer,
                           const VertexAttribCurrentValueData &currentValueData,
                           GLenum pname,
                           GLuint *params);

void QueryActiveUniformBlockiv(const Program *program,
                               UniformBlockIndex uniformBlockIndexPacked,
                               UniformBlockParameter pnamePacked,
                               GLsizei *length,
                               GLint *params);

void QueryInternalFormativ(const Context *context,
                           const Texture *texture,
                           GLenum internalformat,
                           const TextureCaps &format,
                           GLenum pname,
                           GLsizei count,
                           GLint *params);

void QueryFramebufferParameteriv(const Framebuffer *framebuffer,
                                 FramebufferParameter pnamePacked,
                                 GLint *params);

void QueryFramebufferPixelLocalStorageParameterfv(Context *context,
                                                  GLint plane,
                                                  PlaneParameter pnamePacked,
                                                  GLsizei *length,
                                                  GLfloat *params);
void QueryFramebufferPixelLocalStorageParameteriv(Context *context,
                                                  GLint plane,
                                                  PlaneParameter pnamePacked,
                                                  GLsizei *length,
                                                  GLint *params);
void QueryFramebufferPixelLocalStorageParameteruiv(Context *context,
                                                   GLint plane,
                                                   PlaneParameter pnamePacked,
                                                   GLsizei *length,
                                                   GLuint *params);

angle::Result QuerySynciv(const Context *context,
                          const Sync *sync,
                          GLenum pname,
                          GLsizei count,
                          GLsizei *length,
                          GLint *values);

void SetTexParameterf(Context *context, Texture *texture, GLenum pname, GLfloat param);
void SetTexParameterfv(Context *context, Texture *texture, GLenum pname, const GLfloat *params);
void SetTexParameteri(Context *context, Texture *texture, GLenum pname, GLint param);
void SetTexParameteriv(Context *context, Texture *texture, GLenum pname, const GLint *params);
void SetTexParameterIiv(Context *context, Texture *texture, GLenum pname, const GLint *params);
void SetTexParameterIuiv(Context *context, Texture *texture, GLenum pname, const GLuint *params);
void SetTexParameterx(Context *context, Texture *texture, GLenum pname, GLfixed param);
void SetTexParameterxv(Context *context, Texture *texture, GLenum pname, const GLfixed *params);

void SetSamplerParameterfv(Context *context,
                           Sampler *sampler,
                           SamplerParameter pnamePacked,
                           const GLfloat *params);
void SetSamplerParameteriv(Context *context,
                           Sampler *sampler,
                           SamplerParameter pnamePacked,
                           const GLint *params);
void SetSamplerParameterIiv(Context *context,
                            Sampler *sampler,
                            SamplerParameter pnamePacked,
                            const GLint *params);
void SetSamplerParameterIuiv(Context *context,
                             Sampler *sampler,
                             SamplerParameter pnamePacked,
                             const GLuint *params);

void SetFramebufferParameteri(const Context *context,
                              Framebuffer *framebuffer,
                              FramebufferParameter pnamePacked,
                              GLint param);

void SetProgramParameteri(const Context *context, Program *program, GLenum pname, GLint value);

GLint GetUniformResourceProperty(const Program *program, GLuint index, const GLenum prop);

GLuint QueryProgramResourceIndex(const Program *program,
                                 GLenum programInterface,
                                 const GLchar *name);

void QueryProgramResourceName(const Context *context,
                              const Program *program,
                              GLenum programInterface,
                              GLuint index,
                              GLsizei bufSize,
                              GLsizei *length,
                              GLchar *name);

GLint QueryProgramResourceLocation(const Program *program,
                                   GLenum programInterface,
                                   const GLchar *name);
void QueryProgramResourceiv(const Program *program,
                            GLenum programInterface,
                            UniformBlockIndex index,
                            GLsizei propCount,
                            const GLenum *props,
                            GLsizei count,
                            GLsizei *length,
                            GLint *params);

void QueryProgramInterfaceiv(const Program *program,
                             GLenum programInterface,
                             GLenum pname,
                             GLint *params);

angle::Result SetMemoryObjectParameteriv(const Context *context,
                                         MemoryObject *memoryObject,
                                         GLenum pname,
                                         const GLint *params);
void QueryMemoryObjectParameteriv(const MemoryObject *memoryObject, GLenum pname, GLint *params);

// GLES1 emulation

ClientVertexArrayType ParamToVertexArrayType(GLenum param);

void SetLightParameters(GLES1State *state,
                        GLenum light,
                        LightParameter pname,
                        const GLfloat *params);
void GetLightParameters(const GLES1State *state,
                        GLenum light,
                        LightParameter pname,
                        GLfloat *params);

void SetLightModelParameters(GLES1State *state, GLenum pname, const GLfloat *params);
void GetLightModelParameters(const GLES1State *state, GLenum pname, GLfloat *params);
bool IsLightModelTwoSided(const GLES1State *state);

void SetMaterialParameters(GLES1State *state,
                           GLenum face,
                           MaterialParameter pname,
                           const GLfloat *params);
void GetMaterialParameters(const GLES1State *state,
                           GLenum face,
                           MaterialParameter pname,
                           GLfloat *params);

unsigned int GetLightModelParameterCount(GLenum pname);
unsigned int GetLightParameterCount(LightParameter pname);
unsigned int GetMaterialParameterCount(MaterialParameter pname);

void SetFogParameters(GLES1State *state, GLenum pname, const GLfloat *params);
void GetFogParameters(const GLES1State *state, GLenum pname, GLfloat *params);
unsigned int GetFogParameterCount(GLenum pname);

unsigned int GetTextureEnvParameterCount(TextureEnvParameter pname);

void ConvertTextureEnvFromInt(TextureEnvParameter pname, const GLint *input, GLfloat *output);
void ConvertTextureEnvFromFixed(TextureEnvParameter pname, const GLfixed *input, GLfloat *output);
void ConvertTextureEnvToInt(TextureEnvParameter pname, const GLfloat *input, GLint *output);
void ConvertTextureEnvToFixed(TextureEnvParameter pname, const GLfloat *input, GLfixed *output);

void SetTextureEnv(unsigned int unit,
                   GLES1State *state,
                   TextureEnvTarget target,
                   TextureEnvParameter pname,
                   const GLfloat *params);
void GetTextureEnv(unsigned int unit,
                   const GLES1State *state,
                   TextureEnvTarget target,
                   TextureEnvParameter pname,
                   GLfloat *params);

unsigned int GetPointParameterCount(PointParameter pname);

void SetPointParameter(GLES1State *state, PointParameter pname, const GLfloat *params);
void GetPointParameter(const GLES1State *state, PointParameter pname, GLfloat *params);

void SetPointSize(GLES1State *state, GLfloat size);
void GetPointSize(const GLES1State *state, GLfloat *sizeOut);

bool GetQueryParameterInfo(const State &glState,
                           GLenum pname,
                           GLenum *type,
                           unsigned int *numParams);

bool GetIndexedQueryParameterInfo(const State &glState,
                                  GLenum target,
                                  GLenum *type,
                                  unsigned int *numParams);

void QueryProgramPipelineiv(const Context *context,
                            ProgramPipeline *programPipeline,
                            GLenum pname,
                            GLint *params);
}  // namespace gl

namespace egl
{
struct Config;
class Display;
class ThreadSafeDisplay;
class Surface;
class Sync;

void QueryConfigAttrib(const Config *config, EGLint attribute, EGLint *value);

void QueryContextAttrib(const gl::Context *context, EGLint attribute, EGLint *value);

egl::Error QuerySurfaceAttrib(const ThreadSafeDisplay *display,
                              const gl::Context *context,
                              Surface *surface,
                              EGLint attribute,
                              EGLint *value);
egl::Error SetSurfaceAttrib(Surface *surface, EGLint attribute, EGLint value);
Error GetSyncAttrib(const ThreadSafeDisplay *display,
                    const Sync *syncObject,
                    EGLint attribute,
                    EGLint *value);
egl::Error QuerySurfaceAttrib64KHR(const ThreadSafeDisplay *display,
                                   const gl::Context *context,
                                   Surface *surface,
                                   EGLint attribute,
                                   EGLAttribKHR *value);

}  // namespace egl

#endif  // LIBANGLE_QUERYUTILS_H_
