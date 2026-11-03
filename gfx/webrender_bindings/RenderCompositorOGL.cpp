/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "RenderCompositorOGL.h"

#include "GLContext.h"
#include "GLContextEGL.h"
#include "GLContextProvider.h"
#include "mozilla/gfx/Logging.h"
#include "mozilla/gfx/gfxVars.h"
#include "mozilla/webrender/RenderThread.h"
#include "mozilla/widget/CompositorWidget.h"

namespace mozilla::wr {

extern LazyLogModule gRenderThreadLog;
#define LOG(...) MOZ_LOG(gRenderThreadLog, LogLevel::Debug, (__VA_ARGS__))

/* static */
UniquePtr<RenderCompositor> RenderCompositorOGL::Create(
    const RefPtr<widget::CompositorWidget>& aWidget, nsACString& aError) {
  RefPtr<gl::GLContext> gl = RenderThread::Get()->SingletonGL();
  if (!gl) {
    gl = gl::GLContextProvider::CreateForCompositorWidget(
        aWidget, /* aHardwareWebRender */ true, /* aForceAccelerated */ true);
    RenderThread::MaybeEnableGLDebugMessage(gl);
  }
  if (!gl || !gl->MakeCurrent()) {
    gfxCriticalNote << "Failed GL context creation for WebRender: "
                    << gfx::hexa(gl.get());
    return nullptr;
  }
  return std::make_unique<RenderCompositorOGL>(std::move(gl), aWidget);
}

RenderCompositorOGL::RenderCompositorOGL(
    RefPtr<gl::GLContext>&& aGL,
    const RefPtr<widget::CompositorWidget>& aWidget)
    : RenderCompositor(aWidget), mGL(aGL) {
  MOZ_ASSERT(mGL);
  LOG("RenderCompositorOGL::RenderCompositorOGL()");

  mIsEGL = aGL->GetContextType() == mozilla::gl::GLContextType::EGL;
}

RenderCompositorOGL::~RenderCompositorOGL() {
  LOG("RenderCompositorOGL::~RenderCompositorOGL()");

  if (!mGL->MakeCurrent()) {
    gfxCriticalNote
        << "Failed to make render context current during destroying.";
    // Leak resources!
    return;
  }
}

bool RenderCompositorOGL::BeginFrame() {
  if (!mGL->MakeCurrent()) {
    gfxCriticalNote << "Failed to make render context current, can't draw.";
    return false;
  }

  mGL->fBindFramebuffer(LOCAL_GL_FRAMEBUFFER, mGL->GetDefaultFramebuffer());

  return true;
}

RenderedFrameId RenderCompositorOGL::EndFrame(
    const nsTArray<DeviceIntRect>& aDirtyRects) {
  RenderedFrameId frameId = GetNextRenderFrameId();
  if (UseWindowPartialPresent() && aDirtyRects.Length() > 0) {
    gfx::IntRegion bufferInvalid;
    const auto bufferSize = GetBufferSize();
    for (const DeviceIntRect& rect : aDirtyRects) {
      const auto left = std::clamp(rect.min.x, 0, bufferSize.width);
      const auto top = std::clamp(rect.min.y, 0, bufferSize.height);

      const auto right = std::clamp(rect.max.x, 0, bufferSize.width);
      const auto bottom = std::clamp(rect.max.y, 0, bufferSize.height);

      const auto width = right - left;
      const auto height = bottom - top;

      bufferInvalid.OrWith(
          gfx::IntRect(left, (GetBufferSize().height - bottom), width, height));
    }
    gl()->SetDamage(bufferInvalid);
  }
  mGL->SwapBuffers();
  return frameId;
}

void RenderCompositorOGL::Pause() {}

bool RenderCompositorOGL::Resume() { return true; }

LayoutDeviceIntSize RenderCompositorOGL::GetBufferSize() {
  return mWidget->GetClientSize();
}

uint32_t RenderCompositorOGL::GetMaxPartialPresentRects() {
  return gfx::gfxVars::WebRenderMaxPartialPresentRects();
}

bool RenderCompositorOGL::RequestFullRender() { return false; }

bool RenderCompositorOGL::UseWindowPartialPresent() {
  return gfx::gfxVars::WebRenderMaxPartialPresentRects() > 0;
}

size_t RenderCompositorOGL::GetBufferAge() const {
  if (!StaticPrefs::
          gfx_webrender_allow_partial_present_buffer_age_AtStartup()) {
    return 0;
  }
  return gl()->GetBufferAge();
}

}  // namespace mozilla::wr
