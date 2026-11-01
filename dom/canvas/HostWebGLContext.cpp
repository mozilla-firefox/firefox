/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "HostWebGLContext.h"

#include "CompositableHost.h"
#include "MozFramebuffer.h"
#include "TexUnpackBlob.h"
#include "WebGL2Context.h"
#include "WebGLBuffer.h"
#include "WebGLContext.h"
#include "WebGLFramebuffer.h"
#include "WebGLMemoryTracker.h"
#include "WebGLParent.h"
#include "WebGLProgram.h"
#include "WebGLQuery.h"
#include "WebGLRenderbuffer.h"
#include "WebGLSampler.h"
#include "WebGLShader.h"
#include "WebGLSync.h"
#include "WebGLTexture.h"
#include "WebGLTransformFeedback.h"
#include "WebGLVertexArray.h"
#include "mozilla/StaticMutex.h"
#include "mozilla/layers/LayersSurfaces.h"

namespace mozilla {

// -

static StaticMutex sContextSetLock MOZ_UNANNOTATED;

static std::unordered_set<HostWebGLContext*>& DeferredStaticContextSet() {
  static std::unordered_set<HostWebGLContext*> sContextSet;
  return sContextSet;
}

LockedOutstandingContexts::LockedOutstandingContexts()
    : contexts(DeferredStaticContextSet()) {
  sContextSetLock.Lock();
}

LockedOutstandingContexts::~LockedOutstandingContexts() {
  sContextSetLock.Unlock();
}

// -

/*static*/
std::unique_ptr<HostWebGLContext> HostWebGLContext::Create(
    dom::WebGLParent* owner, const webgl::InitContextDesc& desc,
    webgl::InitContextResult* const out) {
  auto host = std::unique_ptr<HostWebGLContext>(new HostWebGLContext(owner));
  auto webgl = WebGLContext::Create(host.get(), desc, out);
  if (!webgl) return nullptr;
  return host;
}

HostWebGLContext::HostWebGLContext(dom::WebGLParent* owner) : mOwner(owner) {
  StaticMutexAutoLock lock(sContextSetLock);
  auto& contexts = DeferredStaticContextSet();
  (void)contexts.insert(this);
}

HostWebGLContext::~HostWebGLContext() {
  StaticMutexAutoLock lock(sContextSetLock);
  auto& contexts = DeferredStaticContextSet();
  (void)contexts.erase(this);
}

// -

void HostWebGLContext::OnContextLoss(const webgl::ContextLossReason reason) {
  (void)mOwner->SendOnContextLoss(reason);
}

void HostWebGLContext::JsWarning(const std::string& text) const {
  (void)mOwner->SendJsWarning(text);
}

Maybe<layers::SurfaceDescriptor> HostWebGLContext::GetFrontBuffer(
    const ObjectId xrFb, const bool webvr) const {
  return mContext->GetFrontBuffer(AutoResolve(xrFb), webvr);
}

//////////////////////////////////////////////
// Creation

void HostWebGLContext::CreateBuffer(const ObjectId id) {
  if (mBufferMap.contains(id)) {
    MOZ_ASSERT(false, "duplicate ID");
    return;
  }
  auto buffer = mContext->CreateBuffer();
  if (!buffer) {
    return;
  }
  mBufferMap.emplace(id, WrapNotNull(std::move(buffer)));
}

void HostWebGLContext::CreateFramebuffer(const ObjectId id) {
  if (mFramebufferMap.contains(id)) {
    MOZ_ASSERT(false, "duplicate ID");
    return;
  }
  auto framebuffer = mContext->CreateFramebuffer();
  if (!framebuffer) {
    return;
  }
  mFramebufferMap.emplace(id, WrapNotNull(std::move(framebuffer)));
}

bool HostWebGLContext::CreateOpaqueFramebuffer(
    const ObjectId id, const webgl::OpaqueFramebufferOptions& options) {
  if (mFramebufferMap.contains(id)) {
    MOZ_ASSERT(false, "duplicate ID");
    return false;
  }
  auto framebuffer = mContext->CreateOpaqueFramebuffer(options);
  if (!framebuffer) {
    return false;
  }
  mFramebufferMap.emplace(id, WrapNotNull(std::move(framebuffer)));
  return true;
}

void HostWebGLContext::CreateProgram(const ObjectId id) {
  if (mProgramMap.contains(id)) {
    MOZ_ASSERT(false, "duplicate ID");
    return;
  }
  auto program = mContext->CreateProgram();
  if (!program) {
    return;
  }
  mProgramMap.emplace(id, WrapNotNull(std::move(program)));
}

void HostWebGLContext::CreateQuery(const ObjectId id) {
  if (mQueryMap.contains(id)) {
    MOZ_ASSERT(false, "duplicate ID");
    return;
  }
  auto query = mContext->CreateQuery();
  if (!query) {
    return;
  }
  mQueryMap.emplace(id, WrapNotNull(std::move(query)));
}

void HostWebGLContext::CreateRenderbuffer(const ObjectId id) {
  if (mRenderbufferMap.contains(id)) {
    MOZ_ASSERT(false, "duplicate ID");
    return;
  }
  auto renderbuffer = mContext->CreateRenderbuffer();
  if (!renderbuffer) {
    return;
  }
  mRenderbufferMap.emplace(id, WrapNotNull(std::move(renderbuffer)));
}

void HostWebGLContext::CreateSampler(const ObjectId id) {
  if (mSamplerMap.contains(id)) {
    MOZ_ASSERT(false, "duplicate ID");
    return;
  }
  auto sampler = GetWebGL2Context()->CreateSampler();
  if (!sampler) {
    return;
  }
  mSamplerMap.emplace(id, WrapNotNull(std::move(sampler)));
}

void HostWebGLContext::CreateShader(const ObjectId id, GLenum type) {
  if (mShaderMap.contains(id)) {
    MOZ_ASSERT(false, "duplicate ID");
    return;
  }
  auto shader = mContext->CreateShader(type);
  if (!shader) {
    return;
  }
  mShaderMap.emplace(id, WrapNotNull(std::move(shader)));
}

void HostWebGLContext::CreateSync(const ObjectId id) {
  if (mSyncMap.contains(id)) {
    MOZ_ASSERT(false, "duplicate ID");
    return;
  }
  auto sync =
      GetWebGL2Context()->FenceSync(LOCAL_GL_SYNC_GPU_COMMANDS_COMPLETE, 0);
  if (!sync) {
    return;
  }
  mSyncMap.emplace(id, WrapNotNull(sync));

  sync->OnCompleteTaskAdd([host = WeakPtr{this}, id]() {
    if (!host) return;
    (void)host->mOwner->SendOnSyncComplete(id);
  });
}

void HostWebGLContext::CreateTexture(const ObjectId id) {
  if (mTextureMap.contains(id)) {
    MOZ_ASSERT(false, "duplicate ID");
    return;
  }
  auto texture = mContext->CreateTexture();
  if (!texture) {
    return;
  }
  mTextureMap.emplace(id, WrapNotNull(std::move(texture)));
}

void HostWebGLContext::CreateTransformFeedback(const ObjectId id) {
  if (mTransformFeedbackMap.contains(id)) {
    MOZ_ASSERT(false, "duplicate ID");
    return;
  }
  auto transformFeedback = GetWebGL2Context()->CreateTransformFeedback();
  if (!transformFeedback) {
    return;
  }
  mTransformFeedbackMap.emplace(id, WrapNotNull(std::move(transformFeedback)));
}

void HostWebGLContext::CreateVertexArray(const ObjectId id) {
  if (mVertexArrayMap.contains(id)) {
    MOZ_ASSERT(false, "duplicate ID");
    return;
  }
  auto vertexArray = mContext->CreateVertexArray();
  if (!vertexArray) {
    return;
  }
  mVertexArrayMap.emplace(id, WrapNotNull(std::move(vertexArray)));
}

////////////////////////

#define _(X) \
  void HostWebGLContext::Delete##X(const ObjectId id) { m##X##Map.erase(id); }

_(Buffer)
_(Framebuffer)
_(Program)
_(Query)
_(Renderbuffer)
_(Sampler)
_(Shader)
_(Sync)
_(Texture)
_(TransformFeedback)
_(VertexArray)

#undef _

}  // namespace mozilla
