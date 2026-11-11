/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "DMABUFTextureHostOGL.h"

#include "GLContextEGL.h"
#include "mozilla/webrender/RenderDMABUFTextureHost.h"
#include "mozilla/webrender/RenderThread.h"
#include "mozilla/webrender/WebRenderAPI.h"
#include "mozilla/widget/DMABufFormats.h"
#include "mozilla/widget/DMABufSurface.h"

namespace mozilla::layers {

DMABUFTextureHostOGL::DMABUFTextureHostOGL(TextureFlags aFlags,
                                           const SurfaceDescriptorDMABuf& aDesc)
    : TextureHost(TextureHostType::DMABUF, aFlags), mDescriptor(aDesc) {
  MOZ_COUNT_CTOR(DMABUFTextureHostOGL);

  // DMABufSurface::CreateDMABufSurface() can fail, for instance when we're run
  // out of file descriptors.
  mSurface = DMABufSurface::CreateDMABufSurface(aDesc);
}

DMABUFTextureHostOGL::~DMABUFTextureHostOGL() {
  MOZ_COUNT_DTOR(DMABUFTextureHostOGL);
}

gfx::SurfaceFormat DMABUFTextureHostOGL::GetFormat() const {
  if (!mSurface) {
    return gfx::SurfaceFormat::UNKNOWN;
  }
  return mSurface->GetFormat();
}

already_AddRefed<gfx::DataSourceSurface> DMABUFTextureHostOGL::GetAsSurface(
    gfx::DataSourceSurface* aSurface) {
  if (!mSurface) {
    return nullptr;
  }
  // DMABufSurface reads back through a blit, so it cannot reuse aSurface.
  return mSurface->GetAsSourceSurface();
}

gfx::YUVColorSpace DMABUFTextureHostOGL::GetYUVColorSpace() const {
  if (!mSurface) {
    return gfx::YUVColorSpace::Identity;
  }
  return mSurface->GetYUVColorSpace();
}

gfx::ColorRange DMABUFTextureHostOGL::GetColorRange() const {
  if (!mSurface) {
    return gfx::ColorRange::LIMITED;
  }
  return mSurface->IsFullRange() ? gfx::ColorRange::FULL
                                 : gfx::ColorRange::LIMITED;
}

gfx::TransferFunction DMABUFTextureHostOGL::GetTransferFunction() const {
  if (!mSurface) {
    return gfx::TransferFunction::BT709;
  }
  return mSurface->GetTransferFunction();
}

uint32_t DMABUFTextureHostOGL::NumSubTextures() {
  return mSurface ? mSurface->GetTextureCount() : 0;
}

gfx::IntSize DMABUFTextureHostOGL::GetSize() const {
  if (!mSurface) {
    return gfx::IntSize();
  }
  return gfx::IntSize(mSurface->GetWidth(), mSurface->GetHeight());
}

gl::GLContext* DMABUFTextureHostOGL::gl() const { return nullptr; }

void DMABUFTextureHostOGL::CreateRenderTexture(
    const wr::ExternalImageId& aExternalImageId) {
  MOZ_ASSERT(mExternalImageId.isSome());

  if (!mSurface) {
    return;
  }
  RefPtr texture = MakeRefPtr<wr::RenderDMABUFTextureHost>(mSurface);
  wr::RenderThread::Get()->RegisterExternalImage(aExternalImageId,
                                                 texture.forget());
}

void DMABUFTextureHostOGL::PushResourceUpdates(
    wr::TransactionBuilder& aResources, ResourceUpdateOp aOp,
    const Range<wr::ImageKey>& aImageKeys, const wr::ExternalImageId& aExtID) {
  if (!mSurface) {
    return;
  }

  auto method = aOp == TextureHost::ADD_IMAGE
                    ? &wr::TransactionBuilder::AddExternalImage
                    : &wr::TransactionBuilder::UpdateExternalImage;
  auto imageType =
      wr::ExternalImageType::TextureHandle(wr::ImageBufferKind::Texture2D);

  switch (mSurface->GetFormat()) {
    case gfx::SurfaceFormat::R8G8B8X8:
    case gfx::SurfaceFormat::R8G8B8A8:
    case gfx::SurfaceFormat::B8G8R8X8:
    case gfx::SurfaceFormat::B8G8R8A8: {
      if (aImageKeys.length() != 1) {
        MOZ_ASSERT_UNREACHABLE("unexpected key length");
        return;
      }
      // XXX Add RGBA handling. Temporary hack to avoid crash
      // With BGRA format setting, rendering works without problem.
      auto format = wr::SurfaceFormatToImageFormat(mSurface->GetFormat());
      if (NS_WARN_IF(!format)) {
        return;
      }
      wr::ImageDescriptor descriptor(GetSize(), *format,
                                     wr::ToOpacityType(mSurface->GetFormat()));
      (aResources.*method)(aImageKeys[0], descriptor, aExtID, imageType, 0,
                           /* aNormalizedUvs */ false);
      break;
    }
    case gfx::SurfaceFormat::NV12: {
      if (aImageKeys.length() != 2 || mSurface->GetTextureCount() != 2) {
        MOZ_ASSERT_UNREACHABLE("unexpected key length or plane count");
        return;
      }
      wr::ImageDescriptor descriptor0(
          gfx::IntSize(mSurface->GetWidth(0), mSurface->GetHeight(0)),
          wr::ImageFormat::R8, wr::OpacityType::HasAlphaChannel);
      wr::ImageDescriptor descriptor1(
          gfx::IntSize(mSurface->GetWidth(1), mSurface->GetHeight(1)),
          wr::ImageFormat::RG8, wr::OpacityType::Opaque);
      (aResources.*method)(aImageKeys[0], descriptor0, aExtID, imageType, 0,
                           /* aNormalizedUvs */ false);
      (aResources.*method)(aImageKeys[1], descriptor1, aExtID, imageType, 1,
                           /* aNormalizedUvs */ false);
      break;
    }
    case gfx::SurfaceFormat::YUV420: {
      if (aImageKeys.length() != 3 || mSurface->GetTextureCount() != 3) {
        MOZ_ASSERT_UNREACHABLE("unexpected key length or plane count");
        return;
      }
      wr::ImageDescriptor descriptor0(
          gfx::IntSize(mSurface->GetWidth(0), mSurface->GetHeight(0)),
          wr::ImageFormat::R8, wr::OpacityType::HasAlphaChannel);
      wr::ImageDescriptor descriptor1(
          gfx::IntSize(mSurface->GetWidth(1), mSurface->GetHeight(1)),
          wr::ImageFormat::R8, wr::OpacityType::HasAlphaChannel);
      (aResources.*method)(aImageKeys[0], descriptor0, aExtID, imageType, 0,
                           /* aNormalizedUvs */ false);
      (aResources.*method)(aImageKeys[1], descriptor1, aExtID, imageType, 1,
                           /* aNormalizedUvs */ false);
      (aResources.*method)(aImageKeys[2], descriptor1, aExtID, imageType, 2,
                           /* aNormalizedUvs */ false);
      break;
    }
    case gfx::SurfaceFormat::P010:
    case gfx::SurfaceFormat::P016: {
      if (aImageKeys.length() != 2 || mSurface->GetTextureCount() != 2) {
        MOZ_ASSERT_UNREACHABLE("unexpected key length or plane count");
        return;
      }
      wr::ImageDescriptor descriptor0(
          gfx::IntSize(mSurface->GetWidth(0), mSurface->GetHeight(0)),
          wr::ImageFormat::R16, wr::OpacityType::HasAlphaChannel);
      wr::ImageDescriptor descriptor1(
          gfx::IntSize(mSurface->GetWidth(1), mSurface->GetHeight(1)),
          wr::ImageFormat::RG16, wr::OpacityType::HasAlphaChannel);
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

void DMABUFTextureHostOGL::PushDisplayItems(
    wr::DisplayListBuilder& aBuilder, const wr::LayoutRect& aBounds,
    const wr::LayoutRect& aClip, wr::ImageRendering aFilter,
    const Range<wr::ImageKey>& aImageKeys, PushDisplayItemFlagSet aFlags) {
  if (!mSurface) {
    return;
  }
  bool preferCompositorSurface =
      aFlags.contains(PushDisplayItemFlag::PREFER_COMPOSITOR_SURFACE);
  bool supportsDirectComposition =
      widget::GetGlobalDMABufFormats()->SupportsDirectComposition(
          mSurface->GetFormat());

  switch (mSurface->GetFormat()) {
    case gfx::SurfaceFormat::R8G8B8X8:
    case gfx::SurfaceFormat::R8G8B8A8:
    case gfx::SurfaceFormat::B8G8R8A8:
    case gfx::SurfaceFormat::B8G8R8X8: {
      if (aImageKeys.length() != 1) {
        MOZ_ASSERT_UNREACHABLE("unexpected key length");
        return;
      }
      aBuilder.PushImage(aBounds, aClip, true, false, aFilter, aImageKeys[0],
                         !(mFlags & TextureFlags::NON_PREMULTIPLIED),
                         wr::ColorF{1.0f, 1.0f, 1.0f, 1.0f},
                         preferCompositorSurface, supportsDirectComposition);
      break;
    }
    case gfx::SurfaceFormat::NV12: {
      if (aImageKeys.length() != 2 || mSurface->GetTextureCount() != 2) {
        MOZ_ASSERT_UNREACHABLE("unexpected key length or plane count");
        return;
      }
      // Those images can only be generated at present by the VAAPI H264 decoder
      // which only supports 8 bits color depth.
      aBuilder.PushNV12Image(
          aBounds, aClip, true, aImageKeys[0], aImageKeys[1],
          wr::ColorDepth::Color8, wr::ToWrYuvColorSpace(GetYUVColorSpace()),
          wr::ToWrColorRange(GetColorRange()), aFilter, preferCompositorSurface,
          supportsDirectComposition);
      break;
    }
    case gfx::SurfaceFormat::YUV420: {
      if (aImageKeys.length() != 3 || mSurface->GetTextureCount() != 3) {
        MOZ_ASSERT_UNREACHABLE("unexpected key length or plane count");
        return;
      }
      // Those images can only be generated at present by the VAAPI vp8 decoder
      // which only supports 8 bits color depth.
      aBuilder.PushYCbCrPlanarImage(
          aBounds, aClip, true, aImageKeys[0], aImageKeys[1], aImageKeys[2],
          wr::ColorDepth::Color8, wr::ToWrYuvColorSpace(GetYUVColorSpace()),
          wr::ToWrColorRange(GetColorRange()), aFilter, preferCompositorSurface,
          supportsDirectComposition);
      break;
    }
    case gfx::SurfaceFormat::P010:
    case gfx::SurfaceFormat::P016: {
      if (aImageKeys.length() != 2 || mSurface->GetTextureCount() != 2) {
        MOZ_ASSERT_UNREACHABLE("unexpected key length or plane count");
        return;
      }
      aBuilder.PushP010Image(
          aBounds, aClip, true, aImageKeys[0], aImageKeys[1],
          wr::ColorDepth::Color10, wr::ToWrYuvColorSpace(GetYUVColorSpace()),
          wr::ToWrColorRange(GetColorRange()), aFilter, preferCompositorSurface,
          supportsDirectComposition);
      break;
    }
    default: {
      MOZ_ASSERT_UNREACHABLE("unexpected to be called");
    }
  }
}

SurfaceDescriptor DMABUFTextureHostOGL::GetSurfaceDescriptor() {
  return mDescriptor;
}

}  // namespace mozilla::layers
