/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "WebGLParent.h"

#include "HostWebGLContext.h"
#include "ImageContainer.h"
#include "WebGLChild.h"
#include "WebGLMethodDispatcher.h"
#include "mozilla/layers/SharedSurfacesParent.h"
#include "mozilla/layers/TextureClientSharedSurface.h"

namespace mozilla::dom {

mozilla::ipc::IPCResult WebGLParent::RecvInitialize(
    const webgl::InitContextDesc& desc, webgl::InitContextResult* const out) {
  mHost = HostWebGLContext::Create(this, desc, out);

  if (!mHost) {
    MOZ_ASSERT(!out->error->empty());
  }

  return IPC_OK();
}

WebGLParent::WebGLParent(layers::SharedSurfacesHolder* aSharedSurfacesHolder,
                         const dom::ContentParentId& aContentId)
    : mSharedSurfacesHolder(aSharedSurfacesHolder), mContentId(aContentId) {}

WebGLParent::~WebGLParent() = default;

// -

using IPCResult = mozilla::ipc::IPCResult;

IPCResult WebGLParent::RecvDispatchCommands(BigBuffer&& shmem,
                                            const uint64_t cmdsByteSize) {
  AUTO_PROFILER_LABEL("WebGLParent::RecvDispatchCommands", GRAPHICS);
  if (!mHost) {
    return IPC_FAIL(this, "HostWebGLContext is not initialized.");
  }

  const auto& gl = mHost->mContext->GL();
  const gl::GLContext::TlsScope tlsIsCurrent(gl);

  MOZ_ASSERT(cmdsByteSize);
  const auto shmemBytes = mozilla::Range<uint8_t>{shmem.AsSpan()};
  const auto byteSize = std::min<uint64_t>(shmemBytes.length(), cmdsByteSize);
  const auto cmdsBytes = mozilla::Range<const uint8_t>{
      shmemBytes.begin(), shmemBytes.begin() + byteSize};
  auto view = webgl::RangeConsumerView{cmdsBytes};

  if (kIsDebug) {
    const auto initialOffset =
        AlignmentOffset(kUniversalAlignment, cmdsBytes.begin().get());
    MOZ_ALWAYS_TRUE(!initialOffset);
  }

  std::optional<std::string> fatalError;

  while (true) {
    view.AlignTo(kUniversalAlignment);
    size_t id = 0;
    if (!view.ReadParam(&id)) break;

    // We split this up so that we don't end up in a long callstack chain of
    // WebGLMethodDispatcher<i>|i=0->N. First get the lambda for dispatch, then
    // invoke the lambda with our args.
    const auto pfn =
        WebGLMethodDispatcher<0>::DispatchCommandFuncById<HostWebGLContext>(id);
    if (!pfn) {
      const nsPrintfCString cstr(
          "MethodDispatcher<%zu> not found. Please file a bug!", id);
      fatalError = ToString(cstr);
      gfxCriticalError() << *fatalError;
      break;
    };

    const auto ok = (*pfn)(*mHost, view);
    if (!ok) {
      const nsPrintfCString cstr(
          "DispatchCommand(id: %zu) failed. Please file a bug!", id);
      fatalError = ToString(cstr);
      gfxCriticalError() << *fatalError;
      break;
    }
  }

  if (fatalError) {
    mHost->JsWarning(*fatalError);
    mHost->OnContextLoss(webgl::ContextLossReason::None);
  }

  return IPC_OK();
}

IPCResult WebGLParent::RecvTexImage(const uint32_t level,
                                    const uint32_t respecFormat,
                                    const uvec3& offset,
                                    const webgl::PackingInfo& pi,
                                    webgl::TexUnpackBlobDesc&& desc) {
  if (!mHost) {
    return IPC_FAIL(this, "HostWebGLContext is not initialized.");
  }

  mHost->TexImage(level, respecFormat, offset, pi, desc);
  return IPC_OK();
}

// -

mozilla::ipc::IPCResult WebGLParent::Recv__delete__() {
  mHost = nullptr;
  return IPC_OK();
}

void WebGLParent::ActorDestroy(ActorDestroyReason aWhy) { mHost = nullptr; }

mozilla::ipc::IPCResult WebGLParent::RecvWaitForTxn(
    layers::RemoteTextureOwnerId aOwnerId,
    layers::RemoteTextureTxnType aTxnType, layers::RemoteTextureTxnId aTxnId) {
  if (mHost) {
    mHost->WaitForTxn(aOwnerId, aTxnType, aTxnId);
  }
  return IPC_OK();
}

// -

IPCResult WebGLParent::RecvGetFrontBufferSnapshot(
    webgl::FrontBufferSnapshotIpc* const ret) {
  return GetFrontBufferSnapshot(ret);
}

IPCResult WebGLParent::GetFrontBufferSnapshot(
    webgl::FrontBufferSnapshotIpc* const ret) {
  AUTO_PROFILER_LABEL("WebGLParent::GetFrontBufferSnapshot", GRAPHICS);
  *ret = {};
  if (!mHost) {
    return IPC_FAIL(this, "HostWebGLContext is not initialized.");
  }

  const bool ok = [&]() {
    const auto maybeSize = mHost->FrontBufferSnapshotInto({});
    if (maybeSize) {
      const auto& surfSize = *maybeSize;
      const auto byteSize = 4 * surfSize.x * surfSize.y;
      const auto byteStride = 4 * surfSize.x;

      auto handle = mozilla::ipc::shared_memory::Create(byteSize);
      auto mapping = handle.Map();
      if (!handle || !mapping) {
        NS_WARNING("Failed to alloc shmem for RecvGetFrontBufferSnapshot.");
        return false;
      }
      const mozilla::Range<uint8_t> range{mapping.DataAs<uint8_t>(),
                                          mapping.Size()};
      ret->surfSize = surfSize;
      ret->byteStride = byteStride;
      ret->shmem = std::move(handle);

      if (!mHost->FrontBufferSnapshotInto(Some(range))) {
        gfxCriticalNote << "WebGLParent::RecvGetFrontBufferSnapshot: "
                           "FrontBufferSnapshotInto(some) failed after "
                           "FrontBufferSnapshotInto(none)";
        ret->shmem = {};
        return false;
      }
    }
    return true;
  }();
  if (!ok) {
    // Zero means failure, as we still need to send any shmem we alloc.
    ret->surfSize = {0, 0};
  }
  return IPC_OK();
}

IPCResult WebGLParent::RecvGetBufferSubData(const GLenum target,
                                            const uint64_t srcByteOffset,
                                            const uint64_t byteSize,
                                            Shmem* const ret) {
  AUTO_PROFILER_LABEL("WebGLParent::RecvGetBufferSubData", GRAPHICS);
  if (!mHost) {
    return IPC_FAIL(this, "HostWebGLContext is not initialized.");
  }

  if (byteSize == ~0ull) {
    NS_WARNING("Failed to alloc shmem for RecvGetBufferSubData.");
    return IPC_OK();
  }

  const auto allocSize = 1 + byteSize;
  auto shmem = webgl::RaiiShmem::Alloc(this, allocSize);
  if (!shmem) {
    NS_WARNING("Failed to alloc shmem for RecvGetBufferSubData.");
    return IPC_OK();
  }

  const auto shmemRange = shmem.ByteRange();
  const auto dataRange =
      mozilla::Range<uint8_t>{shmemRange.begin() + 1, shmemRange.end()};

  // We need to always send the shmem:
  // https://bugzilla.mozilla.org/show_bug.cgi?id=1463831#c2
  const auto ok = mHost->GetBufferSubData(target, srcByteOffset, dataRange);
  *(shmemRange.begin().get()) = ok;
  *ret = shmem.Extract();
  return IPC_OK();
}

IPCResult WebGLParent::RecvReadPixels(const webgl::ReadPixelsDesc& desc,
                                      ReadPixelsBuffer&& buffer,
                                      webgl::ReadPixelsResultIpc* const ret) {
  AUTO_PROFILER_LABEL("WebGLParent::RecvReadPixels", GRAPHICS);
  *ret = {};
  if (!mHost) {
    return IPC_FAIL(this, "HostWebGLContext is not initialized.");
  }

  if (buffer.type() == ReadPixelsBuffer::TShmem) {
    const auto& shmem = buffer.get_Shmem();
    const auto range = shmem.Range<uint8_t>();
    const auto res = mHost->ReadPixelsInto(desc, range);
    *ret = {res, {}};
    return IPC_OK();
  }

  const uint64_t byteSize = buffer.get_uint64_t();
  const auto allocSize = std::max<uint64_t>(1, byteSize);
  auto shmem = webgl::RaiiShmem::Alloc(this, allocSize);
  if (!shmem) {
    NS_WARNING("Failed to alloc shmem for RecvReadPixels.");
    return IPC_OK();
  }

  const auto range = shmem.ByteRange();

  const auto res = mHost->ReadPixelsInto(desc, range);
  *ret = {res, Some(shmem.Extract())};
  return IPC_OK();
}

IPCResult WebGLParent::RecvReadPixelsAsync(const webgl::ReadPixelsDesc& aDesc,
                                           uint64_t aByteSize,
                                           ReadPixelsAsyncResolver&& aResolve) {
  AUTO_PROFILER_LABEL("WebGLParent::RecvReadPixelsAsync", GRAPHICS);

  if (!mHost) {
    return IPC_FAIL(this, "HostWebGLContext is not initialized.");
  }

  CheckedInt<size_t> checkedSize(aByteSize);
  if (!checkedSize.isValid()) {
    return IPC_FAIL(this, "Invalid ReadPixels byte size.");
  }

  const size_t allocSize = std::max<size_t>(1, checkedSize.value());

  auto shmem = webgl::RaiiShmem::Alloc(this, allocSize);
  if (!shmem) {
    NS_WARNING("Failed to allocate shmem for RecvReadPixelsAsync.");
    aResolve(webgl::ReadPixelsResultIpc{});
    return IPC_OK();
  }

  const auto res = mHost->ReadPixelsInto(aDesc, shmem.ByteRange());

  aResolve(webgl::ReadPixelsResultIpc{
      res,
      Some(shmem.Extract()),
  });

  return IPC_OK();
}

// -

IPCResult WebGLParent::RecvCheckFramebufferStatus(GLenum target,
                                                  GLenum* const ret) {
  if (!mHost) {
    return IPC_FAIL(this, "HostWebGLContext is not initialized.");
  }

  *ret = mHost->CheckFramebufferStatus(target);
  return IPC_OK();
}

IPCResult WebGLParent::RecvClientWaitSync(ObjectId id, GLbitfield flags,
                                          GLuint64 timeout, GLenum* const ret) {
  if (!mHost) {
    return IPC_FAIL(this, "HostWebGLContext is not initialized.");
  }

  *ret = mHost->ClientWaitSync(id, flags, timeout);
  return IPC_OK();
}

IPCResult WebGLParent::RecvCreateOpaqueFramebuffer(
    const ObjectId id, const OpaqueFramebufferOptions& options,
    bool* const ret) {
  if (!mHost) {
    return IPC_FAIL(this, "HostWebGLContext is not initialized.");
  }

  *ret = mHost->CreateOpaqueFramebuffer(id, options);
  return IPC_OK();
}

IPCResult WebGLParent::RecvDrawingBufferSize(uvec2* const ret) {
  if (!mHost) {
    return IPC_FAIL(this, "HostWebGLContext is not initialized.");
  }

  *ret = mHost->DrawingBufferSize();
  return IPC_OK();
}

IPCResult WebGLParent::RecvFinish() {
  if (!mHost) {
    return IPC_FAIL(this, "HostWebGLContext is not initialized.");
  }

  mHost->Finish();
  return IPC_OK();
}

IPCResult WebGLParent::RecvGetBufferParameter(GLenum target, GLenum pname,
                                              Maybe<double>* const ret) {
  if (!mHost) {
    return IPC_FAIL(this, "HostWebGLContext is not initialized.");
  }

  *ret = mHost->GetBufferParameter(target, pname);
  return IPC_OK();
}

IPCResult WebGLParent::RecvGetCompileResult(ObjectId id,
                                            webgl::CompileResult* const ret) {
  if (!mHost) {
    return IPC_FAIL(this, "HostWebGLContext is not initialized.");
  }

  *ret = mHost->GetCompileResult(id);
  return IPC_OK();
}

IPCResult WebGLParent::RecvGetError(GLenum* const ret) {
  if (!mHost) {
    return IPC_FAIL(this, "HostWebGLContext is not initialized.");
  }

  *ret = mHost->GetError();
  return IPC_OK();
}

IPCResult WebGLParent::RecvGetFragDataLocation(ObjectId id,
                                               const std::string& name,
                                               GLint* const ret) {
  if (!mHost) {
    return IPC_FAIL(this, "HostWebGLContext is not initialized.");
  }

  *ret = mHost->GetFragDataLocation(id, name);
  return IPC_OK();
}

IPCResult WebGLParent::RecvGetFramebufferAttachmentParameter(
    ObjectId id, GLenum attachment, GLenum pname, Maybe<double>* const ret) {
  if (!mHost) {
    return IPC_FAIL(this, "HostWebGLContext is not initialized.");
  }

  *ret = mHost->GetFramebufferAttachmentParameter(id, attachment, pname);
  return IPC_OK();
}

IPCResult WebGLParent::RecvGetFrontBuffer(
    ObjectId fb, const bool vr, Maybe<layers::SurfaceDescriptor>* const ret) {
  if (!mHost) {
    return IPC_FAIL(this, "HostWebGLContext is not initialized.");
  }

  *ret = mHost->GetFrontBuffer(fb, vr);
  return IPC_OK();
}

IPCResult WebGLParent::RecvGetIndexedParameter(GLenum target, GLuint index,
                                               Maybe<double>* const ret) {
  if (!mHost) {
    return IPC_FAIL(this, "HostWebGLContext is not initialized.");
  }

  *ret = mHost->GetIndexedParameter(target, index);
  return IPC_OK();
}

IPCResult WebGLParent::RecvGetInternalformatParameter(
    const GLenum target, const GLuint format, const GLuint pname,
    Maybe<std::vector<int32_t>>* const ret) {
  if (!mHost) {
    return IPC_FAIL(this, "HostWebGLContext is not initialized.");
  }

  *ret = mHost->GetInternalformatParameter(target, format, pname);
  return IPC_OK();
}

IPCResult WebGLParent::RecvGetLinkResult(ObjectId id,
                                         webgl::LinkResult* const ret) {
  if (!mHost) {
    return IPC_FAIL(this, "HostWebGLContext is not initialized.");
  }

  *ret = mHost->GetLinkResult(id);
  return IPC_OK();
}

IPCResult WebGLParent::RecvGetNumber(GLenum pname, Maybe<double>* const ret) {
  if (!mHost) {
    return IPC_FAIL(this, "HostWebGLContext is not initialized.");
  }

  *ret = mHost->GetNumber(pname);
  return IPC_OK();
}

IPCResult WebGLParent::RecvGetQueryParameter(ObjectId id, GLenum pname,
                                             Maybe<double>* const ret) {
  if (!mHost) {
    return IPC_FAIL(this, "HostWebGLContext is not initialized.");
  }

  *ret = mHost->GetQueryParameter(id, pname);
  return IPC_OK();
}

IPCResult WebGLParent::RecvGetRenderbufferParameter(ObjectId id, GLenum pname,
                                                    Maybe<double>* const ret) {
  if (!mHost) {
    return IPC_FAIL(this, "HostWebGLContext is not initialized.");
  }

  *ret = mHost->GetRenderbufferParameter(id, pname);
  return IPC_OK();
}

IPCResult WebGLParent::RecvGetSamplerParameter(ObjectId id, GLenum pname,
                                               Maybe<double>* const ret) {
  if (!mHost) {
    return IPC_FAIL(this, "HostWebGLContext is not initialized.");
  }

  *ret = mHost->GetSamplerParameter(id, pname);
  return IPC_OK();
}

IPCResult WebGLParent::RecvGetString(GLenum pname,
                                     Maybe<std::string>* const ret) {
  if (!mHost) {
    return IPC_FAIL(this, "HostWebGLContext is not initialized.");
  }

  *ret = mHost->GetString(pname);
  return IPC_OK();
}

IPCResult WebGLParent::RecvGetTexParameter(ObjectId id, GLenum pname,
                                           Maybe<double>* const ret) {
  if (!mHost) {
    return IPC_FAIL(this, "HostWebGLContext is not initialized.");
  }

  *ret = mHost->GetTexParameter(id, pname);
  return IPC_OK();
}

IPCResult WebGLParent::RecvGetUniform(ObjectId id, uint32_t loc,
                                      webgl::GetUniformData* const ret) {
  if (!mHost) {
    return IPC_FAIL(this, "HostWebGLContext is not initialized.");
  }

  *ret = mHost->GetUniform(id, loc);
  return IPC_OK();
}

IPCResult WebGLParent::RecvGetVertexAttrib(GLuint index, GLenum pname,
                                           Maybe<double>* const ret) {
  if (!mHost) {
    return IPC_FAIL(this, "HostWebGLContext is not initialized.");
  }

  *ret = mHost->GetVertexAttrib(index, pname);
  return IPC_OK();
}

IPCResult WebGLParent::RecvOnMemoryPressure() {
  if (!mHost) {
    return IPC_FAIL(this, "HostWebGLContext is not initialized.");
  }

  mHost->OnMemoryPressure();
  return IPC_OK();
}

IPCResult WebGLParent::RecvValidateProgram(ObjectId id, bool* const ret) {
  if (!mHost) {
    return IPC_FAIL(this, "HostWebGLContext is not initialized.");
  }

  *ret = mHost->ValidateProgram(id);
  return IPC_OK();
}

}  // namespace mozilla::dom
