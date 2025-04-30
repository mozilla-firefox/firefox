/* -*- Mode: C++; tab-width: 40; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef nsICanvasRenderingContextInternal_h___
#define nsICanvasRenderingContextInternal_h___

#include "gfxRect.h"
#include "mozilla/gfx/2D.h"
#include "nsISupports.h"
#include "nsIInputStream.h"
#include "nsIDocShell.h"
#include "nsRefreshObservers.h"
#include "nsRFPService.h"
#include "mozilla/dom/HTMLCanvasElement.h"
#include "mozilla/dom/OffscreenCanvas.h"
#include "mozilla/EventForwards.h"
#include "mozilla/Maybe.h"
#include "mozilla/RefPtr.h"
#include "mozilla/StateWatching.h"
#include "mozilla/UniquePtr.h"
#include "mozilla/NotNull.h"
#include "mozilla/WeakPtr.h"
#include "mozilla/layers/LayersSurfaces.h"

#define NS_ICANVASRENDERINGCONTEXTINTERNAL_IID       \
  {                                                  \
    0xb84f2fed, 0x9d4b, 0x430b, {                    \
      0xbd, 0xfb, 0x85, 0x57, 0x8a, 0xc2, 0xb4, 0x4b \
    }                                                \
  }

class nsICookieJarSettings;
class nsIDocShell;
class nsIPrincipal;
class nsRefreshDriver;

namespace mozilla {
class nsDisplayListBuilder;
class ClientWebGLContext;
class PresShell;
class WebGLFramebufferJS;
namespace ipc {
class IProtocol;
}  // namespace ipc
namespace layers {
class CanvasRenderer;
class CompositableForwarder;
class FwdTransactionTracker;
class Layer;
class Image;
class LayerManager;
class LayerTransactionChild;
class PersistentBufferProvider;
class WebRenderCanvasData;
}  // namespace layers
namespace gfx {
class DrawTarget;
class SourceSurface;
}  // namespace gfx
}  // namespace mozilla

enum class FrameCaptureState : uint8_t { CLEAN, DIRTY };

class nsICanvasRenderingContextInternal : public nsISupports,
                                          public mozilla::SupportsWeakPtr,
                                          public nsAPostRefreshObserver {
 public:
  using CanvasRenderer = mozilla::layers::CanvasRenderer;
  using WebRenderCanvasData = mozilla::layers::WebRenderCanvasData;

  NS_DECLARE_STATIC_IID_ACCESSOR(NS_ICANVASRENDERINGCONTEXTINTERNAL_IID)

  nsICanvasRenderingContextInternal();

  ~nsICanvasRenderingContextInternal();

  void SetCanvasElement(mozilla::dom::HTMLCanvasElement* parentCanvas) {
    RemovePostRefreshObserver();
    mCanvasElement = parentCanvas;
    AddPostRefreshObserverIfNecessary();
  }

  virtual mozilla::PresShell* GetPresShell();

  void RemovePostRefreshObserver();

  void AddPostRefreshObserverIfNecessary();

  nsIGlobalObject* GetParentObject() const;

  nsICookieJarSettings* GetCookieJarSettings() const;

  nsIPrincipal* PrincipalOrNull() const;

  void SetOffscreenCanvas(mozilla::dom::OffscreenCanvas* aOffscreenCanvas) {
    mOffscreenCanvas = aOffscreenCanvas;
  }

  // Dimensions of the canvas, in pixels.
  virtual int32_t GetWidth() = 0;
  virtual int32_t GetHeight() = 0;

  // Sets the dimensions of the canvas, in pixels.  Called
  // whenever the size of the element changes.
  NS_IMETHOD SetDimensions(int32_t width, int32_t height) = 0;

  // Initializes the canvas after the object is constructed.
  virtual nsresult Initialize() { return NS_OK; }

  // Initializes with an nsIDocShell and DrawTarget. The size is taken from the
  // DrawTarget.
  NS_IMETHOD InitializeWithDrawTarget(
      nsIDocShell* aDocShell,
      mozilla::NotNull<mozilla::gfx::DrawTarget*> aTarget) = 0;

  // Creates an image buffer. Returns null on failure.
  virtual mozilla::UniquePtr<uint8_t[]> GetImageBuffer(
      int32_t* out_format, mozilla::gfx::IntSize* out_imageSize) = 0;

  // Gives you a stream containing the image represented by this context.
  // The format is given in mimeTime, for example "image/png".
  //
  // If the image format does not support transparency or includeTransparency
  // is false, alpha will be discarded and the result will be the image
  // composited on black.
  NS_IMETHOD GetInputStream(const char* mimeType,
                            const nsAString& encoderOptions,
                            nsIInputStream** stream) = 0;

  // This gets an Azure SourceSurface for the canvas, this will be a snapshot
  // of the canvas at the time it was called.
  // If premultAlpha is provided, then it assumed the callee can handle
  // un-premultiplied surfaces, and *premultAlpha will be set to false
  // if one is returned.
  virtual already_AddRefed<mozilla::gfx::SourceSurface> GetSurfaceSnapshot(
      gfxAlphaType* out_alphaType = nullptr) = 0;

  // Like GetSurfaceSnapshot, but will attempt to optimize the snapshot for the
  // provided DrawTarget, which may be nullptr. By default, this will defer to
  // GetSurfaceSnapshot and ignore target-dependent optimization.
  virtual already_AddRefed<mozilla::gfx::SourceSurface> GetOptimizedSnapshot(
      mozilla::gfx::DrawTarget* aTarget, gfxAlphaType* out_alphaType = nullptr);

  virtual mozilla::ipc::IProtocol* SupportsSnapshotExternalCanvas() const {
    return nullptr;
  }

  virtual void SyncSnapshot() {}

  virtual RefPtr<mozilla::gfx::SourceSurface> GetFrontBufferSnapshot(bool) {
    return GetSurfaceSnapshot();
  }

  // If this is called with true, the backing store of the canvas should
  // be created as opaque; all compositing operators should assume the
  // dst alpha is always 1.0.  If this is never called, the context's
  // opaqueness is determined by the context attributes that it's initialized
  // with.
  virtual void SetOpaqueValueFromOpaqueAttr(bool aOpaqueAttrValue) = 0;

  // Returns whether the context is opaque. This value can be based both on
  // the value of the moz-opaque attribute and on the context's initialization
  // attributes.
  virtual bool GetIsOpaque() = 0;

  // Clear and/or release backing bitmaps, such as for transferToImageBitmap.
  virtual void ResetBitmap() = 0;

  virtual already_AddRefed<mozilla::layers::Image> GetAsImage() {
    return nullptr;
  }

  virtual bool UpdateWebRenderCanvasData(
      mozilla::nsDisplayListBuilder* aBuilder,
      WebRenderCanvasData* aCanvasData) {
    return false;
  }

  virtual bool InitializeCanvasRenderer(mozilla::nsDisplayListBuilder* aBuilder,
                                        CanvasRenderer* aRenderer) {
    return false;
  }

  virtual void MarkContextClean() = 0;

  // Called when a frame is captured.
  virtual void MarkContextCleanForFrameCapture() = 0;

  // Whether the context is clean or has been invalidated (dirty) since the last
  // frame was captured. The Watchable allows the caller to get notified of
  // state changes.
  virtual mozilla::Watchable<FrameCaptureState>* GetFrameCaptureState() = 0;

  // Redraw the dirty rectangle of this canvas.
  NS_IMETHOD Redraw(const gfxRect& dirty) = 0;

  NS_IMETHOD SetContextOptions(JSContext* cx, JS::Handle<JS::Value> options,
                               mozilla::ErrorResult& aRvForDictionaryInit) {
    return NS_OK;
  }

  virtual void OnMemoryPressure() {}

  virtual void OnBeforePaintTransaction() {}
  virtual void OnDidPaintTransaction() {}

  virtual mozilla::layers::PersistentBufferProvider* GetBufferProvider() {
    return nullptr;
  }

  virtual mozilla::Maybe<mozilla::layers::SurfaceDescriptor> GetFrontBuffer(
      mozilla::WebGLFramebufferJS*, const bool webvr = false) {
    return mozilla::Nothing();
  }

  virtual mozilla::Maybe<mozilla::layers::SurfaceDescriptor> PresentFrontBuffer(
      mozilla::WebGLFramebufferJS* fb, const bool webvr = false) {
    return GetFrontBuffer(fb, webvr);
  }

  virtual already_AddRefed<mozilla::layers::FwdTransactionTracker>
  UseCompositableForwarder(mozilla::layers::CompositableForwarder* aForwarder) {
    return nullptr;
  }

  void DoSecurityCheck(nsIPrincipal* aPrincipal, bool forceWriteOnly,
                       bool CORSUsed);

  // Checking if fingerprinting protection is enable for the given target.
  bool ShouldResistFingerprinting(mozilla::RFPTarget aTarget) const;

  bool DispatchEvent(const nsAString& eventName, mozilla::CanBubble aCanBubble,
                     mozilla::Cancelable aIsCancelable) const;

 protected:
  RefPtr<mozilla::dom::HTMLCanvasElement> mCanvasElement;
  RefPtr<mozilla::dom::OffscreenCanvas> mOffscreenCanvas;
  RefPtr<nsRefreshDriver> mRefreshDriver;
};

NS_DEFINE_STATIC_IID_ACCESSOR(nsICanvasRenderingContextInternal,
                              NS_ICANVASRENDERINGCONTEXTINTERNAL_IID)

#endif /* nsICanvasRenderingContextInternal_h___ */
