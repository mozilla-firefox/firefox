/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef WEBGLPARENT_H_
#define WEBGLPARENT_H_

#include "mozilla/GfxMessageUtils.h"
#include "mozilla/dom/ipc/IdType.h"
#include "mozilla/dom/PWebGLParent.h"
#include "mozilla/WeakPtr.h"

namespace mozilla {

class HostWebGLContext;
class WebGLChild;

namespace layers {
class SharedSurfacesHolder;
class SharedSurfaceTextureClient;
class SurfaceDescriptor;
}  // namespace layers

namespace dom {

class WebGLParent : public PWebGLParent, public SupportsWeakPtr {
  friend PWebGLParent;

 public:
  NS_INLINE_DECL_THREADSAFE_REFCOUNTING(WebGLParent, override);

  mozilla::ipc::IPCResult RecvInitialize(const webgl::InitContextDesc&,
                                         webgl::InitContextResult* out);

  WebGLParent(layers::SharedSurfacesHolder* aSharedSurfacesHolder,
              const dom::ContentParentId& aContentId);  // For IPDL

  using IPCResult = mozilla::ipc::IPCResult;

  template <class ResolveT>
  IPCResult RecvPing(const ResolveT& Resolve) {
    Resolve(void_t{});
    return IPC_OK();
  }

  IPCResult RecvSyncPing() { return IPC_OK(); }

  IPCResult RecvDispatchCommands(mozilla::ipc::BigBuffer&&, uint64_t);
  IPCResult RecvTexImage(uint32_t level, uint32_t respecFormat,
                         const uvec3& offset, const webgl::PackingInfo&,
                         webgl::TexUnpackBlobDesc&&);

  IPCResult RecvGetBufferSubData(GLenum target, uint64_t srcByteOffset,
                                 uint64_t byteSize, mozilla::ipc::Shmem* ret);
  IPCResult GetFrontBufferSnapshot(webgl::FrontBufferSnapshotIpc* ret,
                                   IProtocol* aProtocol);
  IPCResult RecvGetFrontBufferSnapshot(webgl::FrontBufferSnapshotIpc* ret);
  IPCResult RecvReadPixels(const webgl::ReadPixelsDesc&,
                           ReadPixelsBuffer&& buffer,
                           webgl::ReadPixelsResultIpc* ret);

  // -

  using ObjectId = webgl::ObjectId;

  IPCResult RecvCheckFramebufferStatus(GLenum target, GLenum* ret);
  IPCResult RecvClientWaitSync(ObjectId id, GLbitfield flags, GLuint64 timeout,
                               GLenum* ret);
  IPCResult RecvCreateOpaqueFramebuffer(ObjectId id,
                                        const OpaqueFramebufferOptions&,
                                        bool* ret);
  IPCResult RecvDrawingBufferSize(uvec2* ret);
  IPCResult RecvFinish();
  IPCResult RecvGetBufferParameter(GLenum target, GLenum pname,
                                   Maybe<double>* ret);
  IPCResult RecvGetCompileResult(ObjectId id, webgl::CompileResult* ret);
  IPCResult RecvGetError(GLenum* ret);
  IPCResult RecvGetFragDataLocation(ObjectId id, const std::string& name,
                                    GLint* ret);
  IPCResult RecvGetFramebufferAttachmentParameter(ObjectId id,
                                                  GLenum attachment,
                                                  GLenum pname,
                                                  Maybe<double>* ret);
  IPCResult RecvGetFrontBuffer(ObjectId fb, bool vr,
                               Maybe<layers::SurfaceDescriptor>* ret);
  IPCResult RecvGetIndexedParameter(GLenum target, GLuint index,
                                    Maybe<double>* ret);
  IPCResult RecvGetInternalformatParameter(GLenum target, GLuint format,
                                           GLuint pname,
                                           Maybe<std::vector<int32_t>>* ret);
  IPCResult RecvGetLinkResult(ObjectId id, webgl::LinkResult* ret);
  IPCResult RecvGetNumber(GLenum pname, Maybe<double>* ret);
  IPCResult RecvGetQueryParameter(ObjectId id, GLenum pname,
                                  Maybe<double>* ret);
  IPCResult RecvGetRenderbufferParameter(ObjectId id, GLenum pname,
                                         Maybe<double>* ret);
  IPCResult RecvGetSamplerParameter(ObjectId id, GLenum pname,
                                    Maybe<double>* ret);
  IPCResult RecvGetString(GLenum pname, Maybe<std::string>* ret);
  IPCResult RecvGetTexParameter(ObjectId id, GLenum pname, Maybe<double>* ret);
  IPCResult RecvGetUniform(ObjectId id, uint32_t loc,
                           webgl::GetUniformData* ret);
  IPCResult RecvGetVertexAttrib(GLuint index, GLenum pname, Maybe<double>* ret);
  IPCResult RecvOnMemoryPressure();
  IPCResult RecvValidateProgram(ObjectId id, bool* ret);

  // -

  const RefPtr<layers::SharedSurfacesHolder> mSharedSurfacesHolder;
  const dom::ContentParentId mContentId;

  HostWebGLContext* GetHostWebGLContext() const { return mHost.get(); }

 private:
  ~WebGLParent();

  mozilla::ipc::IPCResult Recv__delete__() override;

  void ActorDestroy(ActorDestroyReason aWhy) override;

  mozilla::ipc::IPCResult RecvWaitForTxn(layers::RemoteTextureOwnerId aOwnerId,
                                         layers::RemoteTextureTxnType aTxnType,
                                         layers::RemoteTextureTxnId aTxnId);

  std::unique_ptr<HostWebGLContext> mHost;

  // Runnable that repeatedly processes our WebGL command queue
  RefPtr<Runnable> mRunCommandsRunnable;
};

}  // namespace dom
}  // namespace mozilla

#endif  // WEBGLPARENT_H_
