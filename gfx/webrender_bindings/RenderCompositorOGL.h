/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef MOZILLA_GFX_RENDERCOMPOSITOR_OGL_H
#define MOZILLA_GFX_RENDERCOMPOSITOR_OGL_H

#include "GLTypes.h"
#include "mozilla/webrender/RenderCompositor.h"

namespace mozilla {
namespace wr {

class RenderCompositorOGL : public RenderCompositor {
 public:
  static UniquePtr<RenderCompositor> Create(
      const RefPtr<widget::CompositorWidget>& aWidget, nsACString& aError);

  RenderCompositorOGL(RefPtr<gl::GLContext>&& aGL,
                      const RefPtr<widget::CompositorWidget>& aWidget);
  virtual ~RenderCompositorOGL();

  bool BeginFrame() override;
  RenderedFrameId EndFrame(const nsTArray<DeviceIntRect>& aDirtyRects) final;
  void Pause() override;
  bool Resume() override;

  gl::GLContext* gl() const override { return mGL; }

  LayoutDeviceIntSize GetBufferSize() override;

  // Interface for partial present
  bool UseWindowPartialPresent() override;
  bool RequestFullRender() override;
  uint32_t GetMaxPartialPresentRects() override;
  bool ShouldUseBufferAge() override;
  size_t GetBufferAge() const override;

 protected:
  RefPtr<gl::GLContext> mGL;
  bool mIsEGL;
};

}  // namespace wr
}  // namespace mozilla

#endif
