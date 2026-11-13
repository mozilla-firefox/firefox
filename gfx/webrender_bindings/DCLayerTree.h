/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef MOZILLA_GFX_DCLAYER_TREE_H
#define MOZILLA_GFX_DCLAYER_TREE_H

// clang-format off
#include <windows.h>
#include <dxgicommon.h>
#include <dxgiformat.h>
// clang-format on

#include <deque>
#include <unordered_map>
#include <vector>

#include "Colorspaces.h"
#include "GLTypes.h"
#include "mozilla/HashFunctions.h"
#include "mozilla/Maybe.h"
#include "mozilla/RefPtr.h"
#include "mozilla/StaticPtr.h"
#include "mozilla/UniquePtr.h"
#include "mozilla/layers/OverlayInfo.h"
#include "mozilla/webrender/WebRenderTypes.h"

struct ID3D11Device;
struct ID3D11DeviceContext;
struct ID3D11Texture2D;
struct ID3D11VideoDevice;
struct ID3D11VideoContext;
struct ID3D11VideoProcessor;
struct ID3D11VideoProcessorEnumerator;
struct ID3D11VideoProcessorOutputView;
struct IDCompositionColorMatrixEffect;
struct IDCompositionDynamicTexture;
struct IDCompositionFilterEffect;
struct IDCompositionTableTransferEffect;
struct IDCompositionTexture;
struct IDCompositionDevice2;
struct IDCompositionDevice3;
struct IDCompositionSurface;
struct IDCompositionTarget;
struct IDCompositionVisual2;
struct IDXGIDecodeSwapChain;
struct IDXGIResource;
struct IDXGISwapChain1;
struct IDCompositionVirtualSurface;
struct IDCompositionRectangleClip;
struct ID3D11VertexShader;
struct ID3D11PixelShader;
struct ID3D10Blob;
typedef ID3D10Blob ID3DBlob;
struct D3D11_SHADER_RESOURCE_VIEW_DESC;
struct ID3D11Buffer;
struct ID3D11InputLayout;
struct ID3D11ShaderResourceView;
struct ID3D11RasterizerState;
struct ID3D11BlendState;
struct ID3D11SamplerState;

namespace mozilla {

namespace gfx {
color::ColorProfileDesc QueryOutputColorProfile();
}

namespace gl {
class GLContext;
}

namespace wr {

class DCLayerSurface;
class DCLayerDCompositionTexture;
class DCSurface;
class DCSwapChain;
class DCSurfaceDCompositionTextureOverlay;
class DCSurfaceVideo;
class DCSurfaceHandle;
class RenderTextureHost;
class RenderTextureHostUsageInfo;
class RenderDcompSurfaceTextureHost;

struct GpuOverlayInfo {
  bool mSupportsOverlays = false;
  bool mSupportsHardwareOverlays = false;
  bool mSupportsHardwareOverlayRGB10A2 = false;
  bool mSupportsHardwareOverlayRGBA16F = false;
  DXGI_FORMAT mOverlayFormatUsed = DXGI_FORMAT_B8G8R8A8_UNORM;
  DXGI_FORMAT mOverlayFormatUsedHdr = DXGI_FORMAT_R16G16B16A16_FLOAT;
  UINT mNv12OverlaySupportFlags = 0;
  UINT mYuy2OverlaySupportFlags = 0;
  UINT mBgra8OverlaySupportFlags = 0;
  UINT mRgb10a2OverlaySupportFlags = 0;
  UINT mRgba16fOverlaySupportFlags = 0;

  bool mSupportsVpSuperResolution = false;
  bool mSupportsVpAutoHDR = false;
  bool mSupportsHDR = false;
};

// -

struct ColorManagementChain {
  RefPtr<IDCompositionColorMatrixEffect> srcRgbFromSrcYuv;
  RefPtr<IDCompositionTableTransferEffect> srcLinearFromSrcTf;
  RefPtr<IDCompositionColorMatrixEffect> dstLinearFromSrcLinear;
  RefPtr<IDCompositionTableTransferEffect> dstTfFromDstLinear;
  RefPtr<IDCompositionFilterEffect> last;

  static ColorManagementChain From(IDCompositionDevice3& dcomp,
                                   const color::ColorProfileConversionDesc&);

  ~ColorManagementChain();
};

// -

enum class DCompOverlayTypes : uint8_t {
  NO_OVERLAY = 0,
  HARDWARE_DECODED_VIDEO = 1 << 0,
  SOFTWARE_DECODED_VIDEO = 1 << 1,
};
MOZ_MAKE_ENUM_CLASS_BITWISE_OPERATORS(DCompOverlayTypes)

// -

/**
 * DCLayerTree manages direct composition layers.
 * It does not manage gecko's layers::Layer.
 */
class DCLayerTree {
 public:
  static UniquePtr<DCLayerTree> Create(gl::GLContext* aGL, EGLConfig aEGLConfig,
                                       ID3D11Device* aDevice,
                                       ID3D11DeviceContext* aCtx, HWND aHwnd,
                                       nsACString& aError);

  static void Shutdown();

  explicit DCLayerTree(gl::GLContext* aGL, EGLConfig aEGLConfig,
                       ID3D11Device* aDevice, ID3D11DeviceContext* aCtx,
                       HWND aHwnd, IDCompositionDevice2* aCompositionDevice);
  ~DCLayerTree();

  void SetDefaultSwapChain(IDXGISwapChain1* aSwapChain);
  void MaybeUpdateDebug();
  void MaybeCommit();
  void WaitForCommitCompletion();

  bool UseLayerCompositor() const;
  void DisableNativeCompositor();
  bool EnableAsyncScreenshot();
  bool GetAsyncScreenshotEnabled() const { return mEnableAsyncScreenshot; }

  // Interface for wr::Compositor
  void CompositorBeginFrame();
  void CompositorEndFrame();
  void CreateSwapChainSurface(wr::NativeSurfaceId aId, wr::DeviceIntSize aSize,
                              bool aIsOpaque, bool aNeedsSyncDcompCommit);
  void ResizeSwapChainSurface(wr::NativeSurfaceId aId, wr::DeviceIntSize aSize);
  void CreateExternalSurface(wr::NativeSurfaceId aId, bool aIsOpaque);
  void DestroySurface(NativeSurfaceId aId);
  void AttachExternalImage(wr::NativeSurfaceId aId,
                           wr::ExternalImageId aExternalImage);
  void AddSurface(wr::NativeSurfaceId aId,
                  const wr::CompositorSurfaceTransform& aTransform,
                  wr::DeviceIntRect aClipRect,
                  wr::ImageRendering aImageRendering,
                  wr::DeviceIntRect aRoundedClipRect,
                  wr::ClipRadius aClipRadius);
  void BindSwapChain(wr::NativeSurfaceId aId,
                     const wr::DeviceIntRect* aDirtyRects,
                     size_t aNumDirtyRects);
  void PresentSwapChain(wr::NativeSurfaceId aId,
                        const wr::DeviceIntRect* aDirtyRects,
                        size_t aNumDirtyRects);

  gl::GLContext* GetGLContext() const { return mGL; }
  EGLConfig GetEGLConfig() const { return mEGLConfig; }
  ID3D11Device* GetDevice() const { return mDevice; }
  ID3D11DeviceContext* GetDeviceContext() const { return mCtx; }
  IDCompositionDevice2* GetCompositionDevice() const {
    return mCompositionDevice;
  }
  ID3D11VideoDevice* GetVideoDevice() const { return mVideoDevice; }
  ID3D11VideoContext* GetVideoContext() const { return mVideoContext; }
  ID3D11VideoProcessor* GetVideoProcessor() const { return mVideoProcessor; }
  ID3D11VideoProcessorEnumerator* GetVideoProcessorEnumerator() const {
    return mVideoProcessorEnumerator;
  }
  bool EnsureVideoProcessor(const gfx::IntSize& aInputSize,
                            const gfx::IntSize& aOutputSize);

  DCSurface* GetSurface(wr::NativeSurfaceId aId) const;

  HWND GetHwnd() const { return mHwnd; }

  // Get or create an FBO with depth buffer suitable for specified dimensions
  GLuint GetOrCreateFbo(int aWidth, int aHeight);

  bool SetVideoProcessorAutoHDR(bool aEnable);

  bool SupportsHardwareOverlays();
  bool SupportsHardwareOverlayRGB10A2();
  bool SupportsHardwareOverlayRGBA16F();
  DXGI_FORMAT GetOverlayFormatForSDR();

  bool SupportsSwapChainTearing();
  bool UseDCLayerDCompositionTexture();

  void SetUsedOverlayTypeInFrame(DCompOverlayTypes aTypes);

  int GetFrameId() { return mCurrentFrame; }

  void SetPendingCommit() { mPendingCommit = true; }

 protected:
  bool Initialize(HWND aHwnd, nsACString& aError);
  bool InitializeVideoOverlaySupport();
  bool MaybeUpdateDebugCounter();
  bool MaybeUpdateDebugVisualRedrawRegions();
  void DestroyEGLSurface();
  GLuint CreateEGLSurfaceForCompositionSurface(
      wr::DeviceIntRect aDirtyRect, wr::DeviceIntPoint* aOffset,
      RefPtr<IDCompositionSurface> aCompositionSurface,
      wr::DeviceIntPoint aSurfaceOffset);
  void ReleaseNativeCompositorResources();
  layers::OverlayInfo GetOverlayInfo();

  bool mUseLayerCompositor = false;
  bool mEnableAsyncScreenshot = false;
  bool mEnableAsyncScreenshotInNextFrame = false;
  int mAsyncScreenshotLastFrameUsed = 0;

  RefPtr<gl::GLContext> mGL;
  EGLConfig mEGLConfig;

  RefPtr<ID3D11Device> mDevice;
  RefPtr<ID3D11DeviceContext> mCtx;
  HWND mHwnd;

  RefPtr<IDCompositionDevice2> mCompositionDevice;
  RefPtr<IDCompositionTarget> mCompositionTarget;
  RefPtr<IDCompositionVisual2> mRootVisual;
  RefPtr<IDCompositionVisual2> mDefaultSwapChainVisual;

  RefPtr<ID3D11VideoDevice> mVideoDevice;
  RefPtr<ID3D11VideoContext> mVideoContext;
  RefPtr<ID3D11VideoProcessor> mVideoProcessor;
  RefPtr<ID3D11VideoProcessorEnumerator> mVideoProcessorEnumerator;
  bool mVideoProcessorAutoHDREnabled = false;
  gfx::IntSize mVideoInputSize;
  gfx::IntSize mVideoOutputSize;

  bool mDebugCounter;
  bool mDebugVisualRedrawRegions;

  // The EGL image that is bound to the D3D texture provided by
  // DirectComposition.
  EGLImage mEGLImage;

  // The GL render buffer ID that maps the EGLImage to an RBO for attaching to
  // an FBO.
  GLuint mColorRBO;

  struct SurfaceIdHashFn {
    std::size_t operator()(const wr::NativeSurfaceId& aId) const {
      return HashGeneric(wr::AsUint64(aId));
    }
  };

  std::unordered_map<wr::NativeSurfaceId, UniquePtr<DCSurface>, SurfaceIdHashFn>
      mDCSurfaces;

  // A list of layer IDs as they are added to the visual tree this frame.
  std::vector<wr::NativeSurfaceId> mCurrentLayers;

  // The previous frame's list of layer IDs in visual order.
  std::vector<wr::NativeSurfaceId> mPrevLayers;

  // Information about a cached FBO that is retained between frames.
  struct CachedFrameBuffer {
    int width;
    int height;
    GLuint fboId;
    GLuint depthRboId;
    int lastFrameUsed;
  };

  // A cache of FBOs, containing a depth buffer allocated to a specific size.
  // TODO(gw): Might be faster as a hashmap? The length is typically much less
  // than 10.
  nsTArray<CachedFrameBuffer> mFrameBuffers;
  int mCurrentFrame = 0;

  bool mPendingCommit;

  mutable Maybe<color::ColorProfileDesc> mOutputColorProfile;

  DCompOverlayTypes mUsedOverlayTypesInFrame = DCompOverlayTypes::NO_OVERLAY;

 public:
  const color::ColorProfileDesc& OutputColorProfile() const {
    if (!mOutputColorProfile) {
      mOutputColorProfile = Some(gfx::QueryOutputColorProfile());
    }
    return *mOutputColorProfile;
  }

 protected:
  static StaticAutoPtr<GpuOverlayInfo> sGpuOverlayInfo;
};

/**
 Represents a single picture cache slice.
 */
class DCSurface {
 public:
  explicit DCSurface(bool aIsOpaque, DCLayerTree* aDCLayerTree);
  virtual ~DCSurface();

  virtual bool Initialize();
  void SetClip(wr::DeviceIntRect aClipRect, wr::ClipRadius aClipRadius);

  IDCompositionVisual2* GetContentVisual() const { return mContentVisual; }
  IDCompositionVisual2* GetRootVisual() const { return mRootVisual; }

  // Implement these if the inherited surface supports attaching external image.
  virtual void AttachExternalImage(wr::ExternalImageId aExternalImage) {
    MOZ_RELEASE_ASSERT(true, "Not support attaching external image");
  }
  virtual void PresentExternalSurface(gfx::Matrix& aTransform) {
    MOZ_RELEASE_ASSERT(true, "Not support presenting external surface");
  }

  virtual DCSurfaceVideo* AsDCSurfaceVideo() { return nullptr; }
  virtual DCSurfaceHandle* AsDCSurfaceHandle() { return nullptr; }
  virtual DCLayerSurface* AsDCLayerSurface() { return nullptr; }
  virtual DCSwapChain* AsDCSwapChain() { return nullptr; }
  virtual DCLayerDCompositionTexture* AsDCLayerDCompositionTexture() {
    return nullptr;
  }
  virtual DCSurfaceDCompositionTextureOverlay*
  AsDCSurfaceDCompositionTextureOverlay() {
    return nullptr;
  }

  bool IsUpdated(const wr::CompositorSurfaceTransform& aTransform,
                 const wr::DeviceIntRect& aClipRect,
                 const wr::ImageRendering aImageRendering,
                 const wr::DeviceIntRect& aRoundedClipRect,
                 const wr::ClipRadius& aClipRadius);

 protected:
  DCLayerTree* mDCLayerTree;

  struct DCSurfaceData {
    DCSurfaceData(const wr::CompositorSurfaceTransform& aTransform,
                  const wr::DeviceIntRect& aClipRect,
                  const wr::ImageRendering aImageRendering,
                  const wr::DeviceIntRect& aRoundedClipRect,
                  const wr::ClipRadius& aClipRadius)
        : mTransform(aTransform),
          mClipRect(aClipRect),
          mImageRendering(aImageRendering),
          mRoundedClipRect(aRoundedClipRect),
          mClipRadius(aClipRadius) {}

    wr::CompositorSurfaceTransform mTransform;
    wr::DeviceIntRect mClipRect;
    wr::ImageRendering mImageRendering;
    wr::DeviceIntRect mRoundedClipRect;
    wr::ClipRadius mClipRadius;
  };

  // Each surface creates two visuals. The root is where it gets attached
  // to parent visuals, the content is where surface (or child visuals)
  // get attached. Most of the time, the root visual does nothing, but
  // in the case of a complex clip, we attach the clip here. This allows
  // us to implement the simple rectangle clip on the content, and apply
  // the complex clip, if present, in a way that it's not affected by
  // the transform of the content visual.

  RefPtr<IDCompositionVisual2> mRootVisual;
  RefPtr<IDCompositionVisual2> mContentVisual;
  RefPtr<IDCompositionRectangleClip> mClip;

  bool mIsOpaque;
  Maybe<DCSurfaceData> mDCSurfaceData;
};

class DCLayerSurface : public DCSurface {
 public:
  DCLayerSurface(bool aIsOpaque, DCLayerTree* aDCLayerTree)
      : DCSurface(aIsOpaque, aDCLayerTree) {}
  virtual ~DCLayerSurface() = default;

  virtual void Bind(const wr::DeviceIntRect* aDirtyRects,
                    size_t aNumDirtyRects) = 0;
  virtual bool Resize(wr::DeviceIntSize aSize) = 0;
  virtual void Present(const wr::DeviceIntRect* aDirtyRects,
                       size_t aNumDirtyRects) = 0;

  DCLayerSurface* AsDCLayerSurface() override { return this; }
};

class DCLayerDCompositionTexture : public DCLayerSurface {
 public:
  DCLayerDCompositionTexture(wr::DeviceIntSize aSize, bool aIsOpaque,
                             DCLayerTree* aDCLayerTree);
  virtual ~DCLayerDCompositionTexture();

  bool Initialize() override;

  void Bind(const wr::DeviceIntRect* aDirtyRects,
            size_t aNumDirtyRects) override;
  bool Resize(wr::DeviceIntSize aSize) override;
  void Present(const wr::DeviceIntRect* aDirtyRects,
               size_t aNumDirtyRects) override;

  DCLayerDCompositionTexture* AsDCLayerDCompositionTexture() override {
    return this;
  }

  const size_t mSwapChainBufferCount;

 private:
  struct TextureHolder {
    TextureHolder(ID3D11Texture2D* aTexture,
                  IDCompositionTexture* aDCompositionTexture,
                  EGLSurface aEGLSurface);
    TextureHolder() = default;

    RefPtr<ID3D11Texture2D> mTexture;
    RefPtr<IDCompositionTexture> mDCompositionTexture;
    EGLSurface mEGLSurface;
    bool mHasBeenPresented = false;
  };

  bool AllocateTextures();
  UniquePtr<TextureHolder> AllocateTexture();
  void DestroyTexture(UniquePtr<TextureHolder> aHolder);
  void DestroyTextures();
  UniquePtr<TextureHolder> GetNextTexture();
  void UpdateCurrentTexture();

  wr::DeviceIntSize mSize;
  std::deque<UniquePtr<TextureHolder>> mAvailableTextureHolders;

  UniquePtr<TextureHolder> mCurrentTextureHolder;
  UniquePtr<TextureHolder> mPresentingTextureHolder;

  // Kept across buffer rotations and resizes. The previous texture stays
  // displayed until the next successful present replaces it.
  RefPtr<IDCompositionDynamicTexture> mDCompositionDynamicTexture;
};

class DCSwapChain : public DCLayerSurface {
 public:
  DCSwapChain(wr::DeviceIntSize aSize, bool aIsOpaque,
              DCLayerTree* aDCLayerTree);
  virtual ~DCSwapChain();

  bool Initialize() override;

  void Bind(const wr::DeviceIntRect* aDirtyRects,
            size_t aNumDirtyRects) override;
  bool Resize(wr::DeviceIntSize aSize) override;
  void Present(const wr::DeviceIntRect* aDirtyRects,
               size_t aNumDirtyRects) override;

  DCSwapChain* AsDCSwapChain() override { return this; }

  const int mSwapChainBufferCount;

 private:
  wr::DeviceIntSize mSize;
  RefPtr<IDXGISwapChain1> mSwapChain;
  EGLSurface mEGLSurface;
  bool mFirstPresent = true;
};

class DCLayerCompositionSurface : public DCLayerSurface {
 public:
  DCLayerCompositionSurface(wr::DeviceIntSize aSize, bool aIsOpaque,
                            DCLayerTree* aDCLayerTree);
  virtual ~DCLayerCompositionSurface();

  bool Initialize() override;

  void Bind(const wr::DeviceIntRect* aDirtyRects,
            size_t aNumDirtyRects) override;
  bool Resize(wr::DeviceIntSize aSize) override;
  void Present(const wr::DeviceIntRect* aDirtyRects,
               size_t aNumDirtyRects) override;

 private:
  wr::DeviceIntSize mSize;
  EGLSurface mEGLSurface = EGL_NO_SURFACE;
  RefPtr<IDCompositionSurface> mCompositionSurface;
  bool mFirstDraw = true;
};

/**
 * A wrapper surface which can contain either a DCVideo or a DCSurfaceHandle.
 */
class DCExternalSurfaceWrapper : public DCSurface {
 public:
  DCExternalSurfaceWrapper(bool aIsOpaque, DCLayerTree* aDCLayerTree)
      : DCSurface(false /* opaque */, aDCLayerTree), mIsOpaque(aIsOpaque) {}
  virtual ~DCExternalSurfaceWrapper() = default;

  void AttachExternalImage(wr::ExternalImageId aExternalImage) override;

  void PresentExternalSurface(gfx::Matrix& aTransform) override;

  DCSurfaceVideo* AsDCSurfaceVideo() override {
    return mSurface ? mSurface->AsDCSurfaceVideo() : nullptr;
  }

  DCSurfaceHandle* AsDCSurfaceHandle() override {
    return mSurface ? mSurface->AsDCSurfaceHandle() : nullptr;
  }

  DCSurfaceDCompositionTextureOverlay* AsDCSurfaceDCompositionTextureOverlay()
      override {
    return mSurface ? mSurface->AsDCSurfaceDCompositionTextureOverlay()
                    : nullptr;
  }

 private:
  DCSurface* EnsureSurfaceForExternalImage(wr::ExternalImageId aExternalImage);

  UniquePtr<DCSurface> mSurface;
  const bool mIsOpaque;
  Maybe<ColorManagementChain> mCManageChain;
};

class DCSurfaceDCompositionTextureOverlay : public DCSurface {
 public:
  DCSurfaceDCompositionTextureOverlay(bool aIsOpaque,
                                      DCLayerTree* aDCLayerTree);

  void AttachExternalImage(wr::ExternalImageId aExternalImage) override;
  void Present();

  DCSurfaceDCompositionTextureOverlay* AsDCSurfaceDCompositionTextureOverlay()
      override {
    return this;
  }

 protected:
  virtual ~DCSurfaceDCompositionTextureOverlay();

  RefPtr<RenderTextureHost> mRenderTextureHost;
  RefPtr<RenderTextureHost> mPrevRenderTextureHost;
  int mSlowGetCount = 0;
};

class DCSurfaceVideo : public DCSurface {
 public:
  DCSurfaceVideo(bool aIsOpaque, DCLayerTree* aDCLayerTree);

  void AttachExternalImage(wr::ExternalImageId aExternalImage) override;
  bool CalculateSwapChainSize(gfx::Matrix& aTransform);
  void PresentVideo();
  void OnCompositorEndFrame(int aFrameId, uint32_t aDurationMs);

  DCSurfaceVideo* AsDCSurfaceVideo() override { return this; }

 protected:
  virtual ~DCSurfaceVideo();

  DXGI_FORMAT GetSwapChainFormat(bool aUseVpAutoHDR, bool aUseRGB10A2,
                                 bool aUseRGBA16F);
  bool CreateVideoSwapChain(DXGI_FORMAT aFormat);
  bool CallVideoProcessorBlt();
  bool ShaderBltSetup();
  bool ShaderBlt(const RECT& sourceRect, DXGI_COLOR_SPACE_TYPE outputColorSpace,
                 const RECT& destRect);
  void ReleaseDecodeSwapChainResources();

  RefPtr<ID3D11VideoProcessorOutputView> mOutputView;
  RefPtr<IDXGIResource> mDecodeResource;
  RefPtr<IDXGISwapChain1> mVideoSwapChain;
  RefPtr<IDXGIDecodeSwapChain> mDecodeSwapChain;
  HANDLE mSwapChainSurfaceHandle = 0;
  gfx::IntSize mVideoSize;
  gfx::IntSize mSwapChainSize;
  DXGI_FORMAT mSwapChainFormat = DXGI_FORMAT_B8G8R8A8_UNORM;
  bool mIsDRM = false;
  bool mFailedYuvSwapChain = false;
  // Indicates if VideoProcessorBlt is unable to convert YUV HLG to RGB PQ.
  bool mFailedVideoProcessorBltYUVHLGToRGBPQ = false;
  // Indicates if VideoProcessorBlt is unable to convert YUV PQ to RGB PQ.
  bool mFailedVideoProcessorBltYUVPQtoRGBPQ = false;
  RefPtr<RenderTextureHost> mRenderTextureHost;
  RefPtr<RenderTextureHost> mPrevTexture;
  RefPtr<RenderTextureHostUsageInfo> mRenderTextureHostUsageInfo;
  bool mFirstPresent = true;
  const UINT mSwapChainBufferCount;
  bool mUseVpAutoHDR = false;
  bool mVpAutoHDRFailed = false;
  bool mVpSuperResolutionFailed = false;
  bool mContentIsHDR = false;
  bool mUseHDR = false;
  RefPtr<ID3D11VertexShader> mShaderBltVertexShader;
  RefPtr<ID3DBlob> mShaderBltVSBlob;
  RefPtr<ID3D11PixelShader> mShaderBltPixelShader;
  RefPtr<ID3DBlob> mShaderBltPSBlob;
  RefPtr<ID3D11Buffer> mShaderBltIndexBuffer;
  RefPtr<ID3D11Buffer> mShaderBltVertexBuffer;
  RefPtr<ID3D11InputLayout> mShaderBltInputLayout;
  RefPtr<ID3D11RasterizerState> mShaderBltRasterizerState;
  RefPtr<ID3D11BlendState> mShaderBltBlendState;
  RefPtr<ID3D11Buffer> mShaderBltConstantBuffer;
  RefPtr<ID3D11SamplerState> mShaderBltSamplerState;
};

/**
 * A DC surface contains a IDCompositionSurface that is directly constructed by
 * a handle. This is used by the Media Foundataion media engine, which would
 * store the decoded video content in the surface.
 */
class DCSurfaceHandle : public DCSurface {
 public:
  DCSurfaceHandle(bool aIsOpaque, DCLayerTree* aDCLayerTree);
  virtual ~DCSurfaceHandle() = default;

  void AttachExternalImage(wr::ExternalImageId aExternalImage) override;
  void PresentSurfaceHandle();

  DCSurfaceHandle* AsDCSurfaceHandle() override { return this; }

 protected:
  HANDLE GetSurfaceHandle() const;
  IDCompositionSurface* EnsureSurface();

  RefPtr<RenderDcompSurfaceTextureHost> mDcompTextureHost;
};

}  // namespace wr
}  // namespace mozilla

#endif
