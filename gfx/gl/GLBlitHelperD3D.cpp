/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "GLBlitHelper.h"

#include <d3d11.h>
#include <d3d11_1.h>

#include "GLContextEGL.h"
#include "GLLibraryEGL.h"
#include "GPUVideoImage.h"
#include "ScopedGLHelpers.h"

#include "mozilla/layers/CompositeProcessD3D11FencesHolderMap.h"
#include "mozilla/layers/D3D11ShareHandleImage.h"
#include "mozilla/layers/D3D11ZeroCopyTextureImage.h"
#include "mozilla/layers/D3D11YCbCrImage.h"
#include "mozilla/layers/GpuProcessD3D11TextureMap.h"
#include "mozilla/layers/TextureD3D11.h"
#include "mozilla/StaticPrefs_gl.h"

namespace mozilla {
namespace gl {

#define NOTE_IF_FALSE(expr)                          \
  do {                                               \
    if (!(expr)) {                                   \
      gfxCriticalNote << "NOTE_IF_FALSE: " << #expr; \
    }                                                \
  } while (false)

static EGLStreamKHR StreamFromD3DTexture(EglDisplay* const egl,
                                         ID3D11Texture2D* const texD3D,
                                         const EGLAttrib* const postAttribs) {
  if (!egl->IsExtensionSupported(
          EGLExtension::NV_stream_consumer_gltexture_yuv) ||
      !egl->IsExtensionSupported(
          EGLExtension::ANGLE_stream_producer_d3d_texture)) {
    return 0;
  }

  const auto stream = egl->fCreateStreamKHR(nullptr);
  MOZ_ASSERT(stream);
  if (!stream) return 0;
  bool ok = true;
  NOTE_IF_FALSE(ok &= bool(egl->fStreamConsumerGLTextureExternalAttribsNV(
                    stream, nullptr)));
  NOTE_IF_FALSE(
      ok &= bool(egl->fCreateStreamProducerD3DTextureANGLE(stream, nullptr)));
  NOTE_IF_FALSE(
      ok &= bool(egl->fStreamPostD3DTextureANGLE(stream, texD3D, postAttribs)));
  if (ok) return stream;

  (void)egl->fDestroyStreamKHR(stream);
  return 0;
}

static RefPtr<ID3D11Texture2D> OpenSharedTexture(ID3D11Device* const d3d,
                                                 const WindowsHandle handle) {
  RefPtr<ID3D11Device1> device1;
  d3d->QueryInterface((ID3D11Device1**)getter_AddRefs(device1));
  if (!device1) {
    gfxCriticalNoteOnce << "Failed to get ID3D11Device1";
    return nullptr;
  }

  RefPtr<ID3D11Texture2D> tex;
  auto hr = device1->OpenSharedResource1(
      (HANDLE)handle, __uuidof(ID3D11Texture2D),
      (void**)(ID3D11Texture2D**)getter_AddRefs(tex));
  if (FAILED(hr)) {
    gfxCriticalError() << "Error code from OpenSharedResource1: "
                       << gfx::hexa(hr);
    return nullptr;
  }
  return tex;
}

// -------------------------------------

class BindAnglePlanes final {
  const GLBlitHelper& mParent;
  const uint8_t mNumPlanes;
  const ScopedSaveMultiTex mMultiTex;
  GLuint mTempTexs[3];
  EGLStreamKHR mStreams[3];
  RefPtr<IDXGIKeyedMutex> mMutexList[3];
  bool mSuccess;

 public:
  BindAnglePlanes(const GLBlitHelper* const parent, const uint8_t numPlanes,
                  const RefPtr<ID3D11Texture2D>* const texD3DList,
                  const EGLAttrib* const* postAttribsList = nullptr)
      : mParent(*parent),
        mNumPlanes(numPlanes),
        mMultiTex(mParent.mGL, mNumPlanes, LOCAL_GL_TEXTURE_EXTERNAL),
        mTempTexs{0},
        mStreams{0},
        mSuccess(true) {
    MOZ_RELEASE_ASSERT(numPlanes >= 1 && numPlanes <= 3);

    const auto& gl = mParent.mGL;
    const auto& gle = GLContextEGL::Cast(gl);
    const auto& egl = gle->mEgl;

    gl->fGenTextures(numPlanes, mTempTexs);

    for (uint8_t i = 0; i < mNumPlanes; i++) {
      gl->fActiveTexture(LOCAL_GL_TEXTURE0 + i);
      gl->fBindTexture(LOCAL_GL_TEXTURE_EXTERNAL, mTempTexs[i]);
      const EGLAttrib* postAttribs = nullptr;
      if (postAttribsList) {
        postAttribs = postAttribsList[i];
      }
      mStreams[i] = StreamFromD3DTexture(egl.get(), texD3DList[i], postAttribs);
      mSuccess &= bool(mStreams[i]);
    }

    if (mSuccess) {
      for (uint8_t i = 0; i < mNumPlanes; i++) {
        NOTE_IF_FALSE(egl->fStreamConsumerAcquireKHR(mStreams[i]));

        auto& mutex = mMutexList[i];
        texD3DList[i]->QueryInterface(IID_IDXGIKeyedMutex,
                                      (void**)getter_AddRefs(mutex));
        if (mutex) {
          const auto hr = mutex->AcquireSync(0, 100);
          if (FAILED(hr)) {
            NS_WARNING("BindAnglePlanes failed to acquire KeyedMutex.");
            mSuccess = false;
          }
        }
      }
    }
  }

  ~BindAnglePlanes() {
    const auto& gl = mParent.mGL;
    const auto& gle = GLContextEGL::Cast(gl);
    const auto& egl = gle->mEgl;

    if (mSuccess) {
      for (uint8_t i = 0; i < mNumPlanes; i++) {
        NOTE_IF_FALSE(egl->fStreamConsumerReleaseKHR(mStreams[i]));
        if (mMutexList[i]) {
          mMutexList[i]->ReleaseSync(0);
        }
      }
    }

    for (uint8_t i = 0; i < mNumPlanes; i++) {
      (void)egl->fDestroyStreamKHR(mStreams[i]);
    }

    gl->fDeleteTextures(mNumPlanes, mTempTexs);
  }

  const bool& Success() const { return mSuccess; }
};

// -------------------------------------

ID3D11Device* GLBlitHelper::GetD3D11() const {
  if (mD3D11) return mD3D11;

  if (!mGL->IsANGLE()) return nullptr;

  const auto& gle = GLContextEGL::Cast(mGL);
  const auto& egl = gle->mEgl;
  EGLDeviceEXT deviceEGL = 0;
  NOTE_IF_FALSE(egl->fQueryDisplayAttribEXT(LOCAL_EGL_DEVICE_EXT,
                                            (EGLAttrib*)&deviceEGL));
  ID3D11Device* device = nullptr;
  // ANGLE does not `AddRef` its returned pointer for `QueryDeviceAttrib`, so no
  // `getter_AddRefs`.
  if (!egl->mLib->fQueryDeviceAttribEXT(deviceEGL, LOCAL_EGL_D3D11_DEVICE_ANGLE,
                                        (EGLAttrib*)&device)) {
    MOZ_ASSERT(false, "d3d9?");
    return nullptr;
  }
  mD3D11 = device;
  return mD3D11;
}

// -------------------------------------

bool GLBlitHelper::BlitImage(layers::D3D11ShareHandleImage* const srcImage,
                             const gfx::IntSize& destSize,
                             const OriginPos destOrigin) const {
  const auto& data = srcImage->GetData();
  if (!data) return false;

  layers::SurfaceDescriptorD3D10 desc;
  if (!data->SerializeSpecific(&desc)) return false;

  return BlitDescriptor(desc, destSize, destOrigin);
}

// -------------------------------------

bool GLBlitHelper::BlitImage(layers::D3D11ZeroCopyTextureImage* const srcImage,
                             const gfx::IntSize& destSize,
                             const OriginPos destOrigin) const {
  const auto& data = srcImage->GetData();
  if (!data) return false;

  layers::SurfaceDescriptorD3D10 desc;
  if (!data->SerializeSpecific(&desc)) return false;

  return BlitDescriptor(desc, destSize, destOrigin);
}

// -------------------------------------

bool GLBlitHelper::BlitDescriptor(const layers::SurfaceDescriptorD3D10& desc,
                                  const gfx::IntSize& destSize,
                                  const OriginPos destOrigin) const {
  const auto& d3d = GetD3D11();
  if (!d3d) return false;

  const auto& gpuProcessTextureId = desc.gpuProcessTextureId();
  auto arrayIndex = desc.arrayIndex();
  const auto& format = desc.format();
  const auto& clipSize = desc.size();

  const auto srcOrigin = OriginPos::BottomLeft;
  const gfx::IntRect clipRect(0, 0, clipSize.width, clipSize.height);
  const auto colorSpace = desc.colorSpace();
  const auto fencesHolderId = desc.fencesHolderId();

  if (format != gfx::SurfaceFormat::NV12 &&
      format != gfx::SurfaceFormat::P010 &&
      format != gfx::SurfaceFormat::P016) {
    gfxCriticalError() << "Non-NV12 format for SurfaceDescriptorD3D10: "
                       << uint32_t(format);
    return false;
  }

  RefPtr<ID3D11Texture2D> tex;
  if (gpuProcessTextureId.isSome()) {
    auto* textureMap = layers::GpuProcessD3D11TextureMap::Get();
    if (textureMap) {
      Maybe<HANDLE> handle =
          textureMap->GetSharedHandle(gpuProcessTextureId.ref());
      if (handle.isSome()) {
        tex = OpenSharedTexture(d3d, (WindowsHandle)handle.ref());
        arrayIndex = 0;
      }
    }
  } else if (desc.handle()) {
    tex = OpenSharedTexture(d3d, (WindowsHandle)desc.handle()->GetHandle());
  }
  if (!tex) {
    MOZ_GL_ASSERT(mGL, false);  // Get a nullptr from OpenSharedResource1.
    return false;
  }

  auto* fencesHolderMap = layers::CompositeProcessD3D11FencesHolderMap::Get();
  MOZ_ASSERT(fencesHolderMap);
  if (fencesHolderMap && fencesHolderId.isSome()) {
    fencesHolderMap->WaitWriteFence(fencesHolderId.ref(), d3d);
  }

  const RefPtr<ID3D11Texture2D> texList[2] = {tex, tex};
  const EGLAttrib postAttribs0[] = {LOCAL_EGL_NATIVE_BUFFER_PLANE_OFFSET_IMG, 0,
                                    LOCAL_EGL_D3D_TEXTURE_SUBRESOURCE_ID_ANGLE,
                                    static_cast<EGLAttrib>(arrayIndex),
                                    LOCAL_EGL_NONE};
  const EGLAttrib postAttribs1[] = {LOCAL_EGL_NATIVE_BUFFER_PLANE_OFFSET_IMG, 1,
                                    LOCAL_EGL_D3D_TEXTURE_SUBRESOURCE_ID_ANGLE,
                                    static_cast<EGLAttrib>(arrayIndex),
                                    LOCAL_EGL_NONE};
  const EGLAttrib* const postAttribsList[2] = {postAttribs0, postAttribs1};
  // /layers/d3d11/CompositorD3D11.cpp uses bt601 for EffectTypes::NV12.
  // return BlitAngleNv12(tex, YUVColorSpace::BT601, destSize, destOrigin);

  const BindAnglePlanes bindPlanes(this, 2, texList, postAttribsList);
  if (!bindPlanes.Success()) {
    MOZ_GL_ASSERT(mGL, false);  // BindAnglePlanes failed.
    return false;
  }

  D3D11_TEXTURE2D_DESC texDesc = {0};
  tex->GetDesc(&texDesc);

  const gfx::IntSize ySize(texDesc.Width, texDesc.Height);
  const gfx::IntSize divisors(2, 2);
  MOZ_ASSERT(ySize.width % divisors.width == 0);
  MOZ_ASSERT(ySize.height % divisors.height == 0);
  const gfx::IntSize uvSize(ySize.width / divisors.width,
                            ySize.height / divisors.height);

  const auto yuvColorSpace = [&]() {
    switch (colorSpace) {
      case gfx::ColorSpace2::UNKNOWN:
      case gfx::ColorSpace2::SRGB:
      case gfx::ColorSpace2::DISPLAY_P3:
        MOZ_CRASH("Expected BT* colorspace");
      case gfx::ColorSpace2::BT601_525:
        return gfx::YUVColorSpace::BT601;
      case gfx::ColorSpace2::BT709:
        return gfx::YUVColorSpace::BT709;
      case gfx::ColorSpace2::BT2020:
        return gfx::YUVColorSpace::BT2020;
    }
    MOZ_ASSERT_UNREACHABLE();
  }();

  const bool yFlip = destOrigin != srcOrigin;
  const DrawBlitProg::BaseArgs baseArgs = {SubRectMat3(clipRect, ySize), yFlip,
                                           destSize, Nothing()};
  const DrawBlitProg::YUVArgs yuvArgs = {
      SubRectMat3(clipRect, uvSize, divisors), Some(yuvColorSpace)};

  const auto& prog = GetDrawBlitProg(
      {kFragHeader_TexExt, {kFragSample_TwoPlane, kFragConvert_ColorMatrix}});
  prog.Draw(baseArgs, &yuvArgs);
  return true;
}

bool GLBlitHelper::BlitDescriptor(
    const layers::SurfaceDescriptorDXGIYCbCr& desc,
    const gfx::IntSize& destSize, const OriginPos destOrigin) const {
  const auto& clipSize = desc.size();
  const auto& ySize = desc.sizeY();
  const auto& uvSize = desc.sizeCbCr();
  const auto& colorSpace = desc.yUVColorSpace();

  const gfx::IntRect clipRect(0, 0, clipSize.width, clipSize.height);

  auto handleY = desc.handleY() ? desc.handleY()->GetHandle() : nullptr;
  auto handleCb = desc.handleCb() ? desc.handleCb()->GetHandle() : nullptr;
  auto handleCr = desc.handleCr() ? desc.handleCr()->GetHandle() : nullptr;

  const WindowsHandle handles[3] = {
      (WindowsHandle)handleY, (WindowsHandle)handleCb, (WindowsHandle)handleCr};
  return BlitAngleYCbCr(handles, clipRect, ySize, uvSize, colorSpace, destSize,
                        destOrigin);
}

// --

bool GLBlitHelper::BlitAngleYCbCr(const WindowsHandle (&handleList)[3],
                                  const gfx::IntRect& clipRect,
                                  const gfx::IntSize& ySize,
                                  const gfx::IntSize& uvSize,
                                  const gfx::YUVColorSpace colorSpace,
                                  const gfx::IntSize& destSize,
                                  const OriginPos destOrigin) const {
  const auto& d3d = GetD3D11();
  if (!d3d) return false;

  const auto srcOrigin = OriginPos::BottomLeft;

  gfx::IntSize divisors;
  if (!GuessDivisors(ySize, uvSize, &divisors)) return false;

  const RefPtr<ID3D11Texture2D> texList[3] = {
      OpenSharedTexture(d3d, handleList[0]),
      OpenSharedTexture(d3d, handleList[1]),
      OpenSharedTexture(d3d, handleList[2])};
  const BindAnglePlanes bindPlanes(this, 3, texList);

  const bool yFlip = destOrigin != srcOrigin;
  const DrawBlitProg::BaseArgs baseArgs = {SubRectMat3(clipRect, ySize), yFlip,
                                           destSize, Nothing()};
  const DrawBlitProg::YUVArgs yuvArgs = {
      SubRectMat3(clipRect, uvSize, divisors), Some(colorSpace)};

  const auto& prog = GetDrawBlitProg(
      {kFragHeader_TexExt, {kFragSample_ThreePlane, kFragConvert_ColorMatrix}});
  prog.Draw(baseArgs, &yuvArgs);
  return true;
}

}  // namespace gl
}  // namespace mozilla
