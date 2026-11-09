/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "MacIOSurfaceTextureHostOGL.h"

#include "mozilla/gfx/MacIOSurface.h"
#include "mozilla/gfx/gfxVars.h"
#include "mozilla/layers/CompositeProcessFencesHolderMap.h"
#include "mozilla/webrender/RenderMacIOSurfaceTextureHost.h"
#include "mozilla/webrender/RenderThread.h"
#include "mozilla/webrender/WebRenderAPI.h"

namespace mozilla {
namespace layers {

MacIOSurfaceTextureHostOGL::MacIOSurfaceTextureHostOGL(
    TextureFlags aFlags, const SurfaceDescriptorMacIOSurface& aDescriptor)
    : TextureHost(TextureHostType::MacIOSurface, aFlags),
      mDescriptor(aDescriptor),
      mSurface(MacIOSurface::LookupSurface(
          aDescriptor.surfaceId(), aDescriptor.yUVColorSpace(),
          aDescriptor.transferFunction(),
          aDescriptor.isOpaque() ? MacIOSurface::AllowAlpha::No
                                 : MacIOSurface::AllowAlpha::Yes)) {
  MOZ_COUNT_CTOR(MacIOSurfaceTextureHostOGL);
  if (!mSurface) {
    gfxCriticalNote << "Failed to look up MacIOSurface";
  }

  if (!mDescriptor.fencesHolderId()) {
    return;
  }
  MOZ_ASSERT(mDescriptor.fencesHolderId()->IsValid());
  if (auto* fenceHolderMap = CompositeProcessFencesHolderMap::Get()) {
    fenceHolderMap->RegisterReference(mDescriptor.fencesHolderId().ref());
  } else {
    MOZ_ASSERT_UNREACHABLE("FencesHolderMap not available");
  }
}

MacIOSurfaceTextureHostOGL::~MacIOSurfaceTextureHostOGL() {
  MOZ_COUNT_DTOR(MacIOSurfaceTextureHostOGL);

  if (!mDescriptor.fencesHolderId()) {
    return;
  }
  if (auto* fenceHolderMap = CompositeProcessFencesHolderMap::Get()) {
    fenceHolderMap->Unregister(mDescriptor.fencesHolderId().ref());
  } else {
    MOZ_ASSERT_UNREACHABLE("FencesHolderMap not available");
  }
}

gfx::SurfaceFormat MacIOSurfaceTextureHostOGL::GetFormat() const {
  if (!mSurface) {
    return gfx::SurfaceFormat::UNKNOWN;
  }
  return mSurface->GetFormat();
}

gfx::SurfaceFormat MacIOSurfaceTextureHostOGL::GetReadFormat() const {
  if (!mSurface) {
    return gfx::SurfaceFormat::UNKNOWN;
  }
  return mSurface->GetReadFormat();
}

gfx::IntSize MacIOSurfaceTextureHostOGL::GetSize() const {
  if (!mSurface) {
    return gfx::IntSize();
  }
  return gfx::IntSize(mSurface->GetDevicePixelWidth(),
                      mSurface->GetDevicePixelHeight());
}

gl::GLContext* MacIOSurfaceTextureHostOGL::gl() const { return nullptr; }

gfx::YUVColorSpace MacIOSurfaceTextureHostOGL::GetYUVColorSpace() const {
  if (!mSurface) {
    return gfx::YUVColorSpace::Identity;
  }
  return mSurface->GetYUVColorSpace();
}

gfx::ColorRange MacIOSurfaceTextureHostOGL::GetColorRange() const {
  if (!mSurface) {
    return gfx::ColorRange::LIMITED;
  }
  return mSurface->IsFullRange() ? gfx::ColorRange::FULL
                                 : gfx::ColorRange::LIMITED;
}

gfx::TransferFunction MacIOSurfaceTextureHostOGL::GetTransferFunction() const {
  if (!mSurface) {
    return gfx::TransferFunction::BT709;
  }
  return mSurface->GetTransferFunction();
}

void MacIOSurfaceTextureHostOGL::CreateRenderTexture(
    const wr::ExternalImageId& aExternalImageId) {
  MOZ_ASSERT(mExternalImageId.isSome());

  RefPtr texture = MakeRefPtr<wr::RenderMacIOSurfaceTextureHost>(
      GetMacIOSurface(), mDescriptor.fencesHolderId());

  bool isDRM = (bool)(mFlags & TextureFlags::DRM_SOURCE);
  texture->SetIsFromDRMSource(isDRM);

  wr::RenderThread::Get()->RegisterExternalImage(aExternalImageId,
                                                 texture.forget());
}

uint32_t MacIOSurfaceTextureHostOGL::NumSubTextures() {
  if (!mSurface) {
    return 0;
  }

  switch (GetFormat()) {
    case gfx::SurfaceFormat::R8G8B8X8:
    case gfx::SurfaceFormat::R8G8B8A8:
    case gfx::SurfaceFormat::B8G8R8A8:
    case gfx::SurfaceFormat::B8G8R8X8:
    case gfx::SurfaceFormat::YUY2: {
      return 1;
    }
    case gfx::SurfaceFormat::NV12:
    case gfx::SurfaceFormat::P010:
    case gfx::SurfaceFormat::NV16:
    case gfx::SurfaceFormat::P210: {
      return 2;
    }
    default: {
      MOZ_ASSERT_UNREACHABLE("unexpected format");
      return 1;
    }
  }
}

void MacIOSurfaceTextureHostOGL::PushResourceUpdates(
    wr::TransactionBuilder& aResources, ResourceUpdateOp aOp,
    const Range<wr::ImageKey>& aImageKeys, const wr::ExternalImageId& aExtID) {
  MOZ_ASSERT(mSurface);

  auto method = aOp == TextureHost::ADD_IMAGE
                    ? &wr::TransactionBuilder::AddExternalImage
                    : &wr::TransactionBuilder::UpdateExternalImage;
  auto imageType = wr::ExternalImageType::TextureHandle(
      aResources.GetCapabilities().mIOSurfaceImageKind);

  switch (GetFormat()) {
    case gfx::SurfaceFormat::B8G8R8A8:
    case gfx::SurfaceFormat::B8G8R8X8: {
      if (aImageKeys.length() != 1 || mSurface->GetPlaneCount() != 0) {
        MOZ_ASSERT_UNREACHABLE("unexpected key length or plane count");
        return;
      }
      // The internal pixel format of MacIOSurface is always BGRX or BGRA
      // format.
      wr::ImageDescriptor descriptor(GetSize(), wr::ImageFormat::BGRA8,
                                     GetFormat() == gfx::SurfaceFormat::B8G8R8A8
                                         ? wr::OpacityType::HasAlphaChannel
                                         : wr::OpacityType::Opaque);
      descriptor.is_drm_source = bool(mFlags & TextureFlags::DRM_SOURCE);
      (aResources.*method)(aImageKeys[0], descriptor, aExtID, imageType, 0,
                           /* aNormalizedUvs */ false);
      break;
    }
    case gfx::SurfaceFormat::YUY2: {
      // This is the special buffer format. The buffer contents could be a
      // converted RGB interleaving data or a YCbCr interleaving data depending
      // on the different platform setting. (e.g. It will be RGB at OpenGL 2.1
      // and YCbCr at OpenGL 3.1)
      if (aImageKeys.length() != 1 || mSurface->GetPlaneCount() != 0) {
        MOZ_ASSERT_UNREACHABLE("unexpected key length or plane count");
        return;
      }
      wr::ImageDescriptor descriptor(GetSize(), wr::ImageFormat::BGRA8,
                                     wr::OpacityType::Opaque);
      descriptor.is_drm_source = bool(mFlags & TextureFlags::DRM_SOURCE);
      (aResources.*method)(aImageKeys[0], descriptor, aExtID, imageType, 0,
                           /* aNormalizedUvs */ false);
      break;
    }
    case gfx::SurfaceFormat::NV12:
    case gfx::SurfaceFormat::NV16: {
      if (aImageKeys.length() != 2 || mSurface->GetPlaneCount() != 2) {
        MOZ_ASSERT_UNREACHABLE("unexpected key length or plane count");
        return;
      }
      wr::ImageDescriptor descriptor0(
          gfx::IntSize(mSurface->GetDevicePixelWidth(0),
                       mSurface->GetDevicePixelHeight(0)),
          wr::ImageFormat::R8, wr::OpacityType::HasAlphaChannel);
      wr::ImageDescriptor descriptor1(
          gfx::IntSize(mSurface->GetDevicePixelWidth(1),
                       mSurface->GetDevicePixelHeight(1)),
          wr::ImageFormat::RG8, wr::OpacityType::Opaque);
      descriptor0.is_drm_source = bool(mFlags & TextureFlags::DRM_SOURCE);
      descriptor1.is_drm_source = bool(mFlags & TextureFlags::DRM_SOURCE);
      (aResources.*method)(aImageKeys[0], descriptor0, aExtID, imageType, 0,
                           /* aNormalizedUvs */ false);
      (aResources.*method)(aImageKeys[1], descriptor1, aExtID, imageType, 1,
                           /* aNormalizedUvs */ false);
      break;
    }
    case gfx::SurfaceFormat::P010:
    case gfx::SurfaceFormat::P016:
    case gfx::SurfaceFormat::P210: {
      if (aImageKeys.length() != 2 || mSurface->GetPlaneCount() != 2) {
        MOZ_ASSERT_UNREACHABLE("unexpected key length or plane count");
        return;
      }
      wr::ImageDescriptor descriptor0(
          gfx::IntSize(mSurface->GetDevicePixelWidth(0),
                       mSurface->GetDevicePixelHeight(0)),
          wr::ImageFormat::R16, wr::OpacityType::HasAlphaChannel);
      wr::ImageDescriptor descriptor1(
          gfx::IntSize(mSurface->GetDevicePixelWidth(1),
                       mSurface->GetDevicePixelHeight(1)),
          wr::ImageFormat::RG16, wr::OpacityType::HasAlphaChannel);
      descriptor0.is_drm_source = bool(mFlags & TextureFlags::DRM_SOURCE);
      descriptor1.is_drm_source = bool(mFlags & TextureFlags::DRM_SOURCE);
      (aResources.*method)(aImageKeys[0], descriptor0, aExtID, imageType, 0,
                           /* aNormalizedUvs */ false);
      (aResources.*method)(aImageKeys[1], descriptor1, aExtID, imageType, 1,
                           /* aNormalizedUvs */ false);
      break;
    }
    default: {
      MOZ_ASSERT_UNREACHABLE("unexpected to be called");
    }
  }
}

void MacIOSurfaceTextureHostOGL::PushDisplayItems(
    wr::DisplayListBuilder& aBuilder, const wr::LayoutRect& aBounds,
    const wr::LayoutRect& aClip, wr::ImageRendering aFilter,
    const Range<wr::ImageKey>& aImageKeys, PushDisplayItemFlagSet aFlags) {
  bool preferCompositorSurface =
      aFlags.contains(PushDisplayItemFlag::PREFER_COMPOSITOR_SURFACE) ||
      bool(mFlags & TextureFlags::DRM_SOURCE);
  switch (GetFormat()) {
    case gfx::SurfaceFormat::B8G8R8A8:
    case gfx::SurfaceFormat::B8G8R8X8: {
      if (aImageKeys.length() != 1 || mSurface->GetPlaneCount() != 0) {
        MOZ_ASSERT_UNREACHABLE("unexpected key length or plane count");
        return;
      }
      // We disable external compositing for RGB surfaces for now until
      // we've tested support more thoroughly. Bug 1667917.
      aBuilder.PushImage(aBounds, aClip, true, false, aFilter, aImageKeys[0],
                         !(mFlags & TextureFlags::NON_PREMULTIPLIED),
                         wr::ColorF{1.0f, 1.0f, 1.0f, 1.0f},
                         preferCompositorSurface,
                         /* aSupportsExternalCompositing */ true);
      break;
    }
    case gfx::SurfaceFormat::YUY2: {
      if (aImageKeys.length() != 1 || mSurface->GetPlaneCount() != 0) {
        MOZ_ASSERT_UNREACHABLE("unexpected key length or plane count");
        return;
      }
      // Those images can only be generated at present by the Apple H264 decoder
      // which only supports 8 bits color depth.
      aBuilder.PushYCbCrInterleavedImage(
          aBounds, aClip, true, aImageKeys[0], wr::ColorDepth::Color8,
          wr::ToWrYuvColorSpace(GetYUVColorSpace()),
          wr::ToWrColorRange(GetColorRange()), aFilter, preferCompositorSurface,
          /* aSupportsExternalCompositing */ true);
      break;
    }
    case gfx::SurfaceFormat::NV12: {
      if (aImageKeys.length() != 2 || mSurface->GetPlaneCount() != 2) {
        MOZ_ASSERT_UNREACHABLE("unexpected key length or plane count");
        return;
      }
      aBuilder.PushNV12Image(
          aBounds, aClip, true, aImageKeys[0], aImageKeys[1],
          wr::ColorDepth::Color8, wr::ToWrYuvColorSpace(GetYUVColorSpace()),
          wr::ToWrColorRange(GetColorRange()), aFilter, preferCompositorSurface,
          /* aSupportsExternalCompositing */ true);
      break;
    }
    case gfx::SurfaceFormat::P010:
    case gfx::SurfaceFormat::P016: {
      if (aImageKeys.length() != 2 || mSurface->GetPlaneCount() != 2) {
        MOZ_ASSERT_UNREACHABLE("unexpected key length or plane count");
        return;
      }
      aBuilder.PushP010Image(
          aBounds, aClip, true, aImageKeys[0], aImageKeys[1],
          wr::ColorDepth::Color10, wr::ToWrYuvColorSpace(GetYUVColorSpace()),
          wr::ToWrColorRange(GetColorRange()), aFilter, preferCompositorSurface,
          /* aSupportsExternalCompositing */ true);
      break;
    }
    case gfx::SurfaceFormat::NV16: {
      if (aImageKeys.length() != 2 || mSurface->GetPlaneCount() != 2) {
        MOZ_ASSERT_UNREACHABLE("unexpected key length or plane count");
        return;
      }
      aBuilder.PushNV16Image(
          aBounds, aClip, true, aImageKeys[0], aImageKeys[1],
          wr::ColorDepth::Color8, wr::ToWrYuvColorSpace(GetYUVColorSpace()),
          wr::ToWrColorRange(GetColorRange()), aFilter, preferCompositorSurface,
          /* aSupportsExternalCompositing */ true);
      break;
    }
    case gfx::SurfaceFormat::P210: {
      if (aImageKeys.length() != 2 || mSurface->GetPlaneCount() != 2) {
        MOZ_ASSERT_UNREACHABLE("unexpected key length or plane count");
        return;
      }
      aBuilder.PushP210Image(
          aBounds, aClip, true, aImageKeys[0], aImageKeys[1],
          wr::ColorDepth::Color10, wr::ToWrYuvColorSpace(GetYUVColorSpace()),
          wr::ToWrColorRange(GetColorRange()), aFilter, preferCompositorSurface,
          /* aSupportsExternalCompositing */ true);
      break;
    }
    default: {
      MOZ_ASSERT_UNREACHABLE("unexpected to be called");
    }
  }
}

SurfaceDescriptor MacIOSurfaceTextureHostOGL::GetSurfaceDescriptor() {
  return mDescriptor;
}

}  // namespace layers
}  // namespace mozilla
