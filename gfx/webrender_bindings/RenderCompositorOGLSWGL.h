/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef MOZILLA_GFX_RENDERCOMPOSITOR_OGL_SWGL_H
#define MOZILLA_GFX_RENDERCOMPOSITOR_OGL_SWGL_H

#include "mozilla/layers/Compositor.h"
#include "mozilla/webrender/RenderCompositorLayersSWGL.h"

namespace mozilla {

namespace layers {
class TextureImageTextureSourceOGL;
}

namespace wr {

class RenderCompositorOGLSWGL : public RenderCompositorLayersSWGL {
 public:
  static UniquePtr<RenderCompositor> Create(
      const RefPtr<widget::CompositorWidget>& aWidget, nsACString& aError);

  RenderCompositorOGLSWGL(layers::Compositor* aCompositor,
                          const RefPtr<widget::CompositorWidget>& aWidget,
                          void* aContext);
  virtual ~RenderCompositorOGLSWGL();

  gl::GLContext* GetGLContext();

  bool MakeCurrent() override;

  bool BeginFrame() override;
  RenderedFrameId EndFrame(const nsTArray<DeviceIntRect>& aDirtyRects) override;

  void GetCompositorCapabilities(CompositorCapabilities* aCaps) override;

  // Returns true for requesting rendering during readback.
  // RenderCompositorOGLSWGL::MaybeReadback() requests rendering.
  // This value is not used by WebRender, since native compositor API is used
  // for sw-wr.
  bool UseWindowPartialPresent() override { return true; }
  bool RequestFullRender() override;

  void Pause() override;
  bool Resume() override;
  bool IsPaused() override;

  LayoutDeviceIntSize GetBufferSize() override;

  layers::WebRenderCompositor CompositorType() const override {
    return layers::WebRenderCompositor::OPENGL;
  }

  bool MaybeReadback(const gfx::IntSize& aReadbackSize,
                     const wr::ImageFormat& aReadbackFormat,
                     const Range<uint8_t>& aReadbackBuffer,
                     bool* aNeedsYFlip) override;
#ifdef MOZ_WIDGET_ANDROID
  bool CaptureScreenPixels(
      const gfx::IntRect& aSourceRect,
      layers::AndroidHardwareBuffer* aHardwareBuffer) override;
#endif

 private:
  void HandleExternalImage(RenderTextureHost* aExternalImage,
                           FrameSurface& aFrameSurface) override;
  UniquePtr<RenderCompositorLayersSWGL::Tile> DoCreateTile(
      Surface* aSurface) override;

  EGLSurface CreateEGLSurface();
  void DestroyEGLSurface();

  EGLSurface mEGLSurface = EGL_NO_SURFACE;
  bool mFullRender = false;

#ifdef MOZ_WIDGET_ANDROID
  // Whether we are in the process of handling a NEW_SURFACE error. On Android
  // this is used to allow the widget an opportunity to recover from the first
  // instance, before raising a WebRenderError on subsequent occurences.
  bool mHandlingNewSurfaceError = false;
#endif

  class TileOGL : public RenderCompositorLayersSWGL::Tile {
   public:
    TileOGL(RefPtr<layers::TextureImageTextureSourceOGL>&& aTexture,
            const gfx::IntSize& aSize);
    virtual ~TileOGL();

    bool Map(wr::DeviceIntRect aDirtyRect, wr::DeviceIntRect aValidRect,
             void** aData, int32_t* aStride) override;
    void Unmap(const gfx::IntRect& aDirtyRect) override;
    layers::DataTextureSource* GetTextureSource() override;
    bool IsValid() override { return true; }

   private:
    RefPtr<layers::TextureImageTextureSourceOGL> mTexture;
    RefPtr<gfx::DataSourceSurface> mSurface;
    RefPtr<gfx::DataSourceSurface> mSubSurface;
    GLuint mPBO = 0;
  };
};

}  // namespace wr
}  // namespace mozilla

#endif
