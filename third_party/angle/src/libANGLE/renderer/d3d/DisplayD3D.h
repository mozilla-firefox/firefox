//
// Copyright 2014 The ANGLE Project Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//

// DisplayD3D.h: D3D implementation of egl::Display

#ifndef LIBANGLE_RENDERER_D3D_DISPLAYD3D_H_
#define LIBANGLE_RENDERER_D3D_DISPLAYD3D_H_

#include "libANGLE/Device.h"
#include "libANGLE/renderer/DisplayImpl.h"
#include "libANGLE/renderer/ShareGroupImpl.h"

#include "libANGLE/renderer/d3d/RendererD3D.h"

namespace rx
{
class ShareGroupD3D : public ShareGroupImpl
{
  public:
    ShareGroupD3D(const egl::ShareGroupState &state) : ShareGroupImpl(state) {}
};

// testDeviceLost(), restoreLostDevice(), and makeCurrent() are implemented by DisplayD3D
class ThreadSafeDisplayD3D : public ThreadSafeDisplayImpl
{
  public:
    ThreadSafeDisplayD3D()           = default;
    ~ThreadSafeDisplayD3D() override = default;
};

class DisplayD3D : public DisplayImpl, public d3d::Context, public ThreadSafeDisplayD3D
{
  public:
    DisplayD3D(const egl::DisplayState &state);

    egl::Error initialize(egl::Display *display) override;
    void terminate() override;

    // Surface creation
    SurfaceImpl *createWindowSurface(const egl::SurfaceState &state,
                                     EGLNativeWindowType window,
                                     const egl::AttributeMap &attribs) override;
    SurfaceImpl *createPbufferSurface(const egl::SurfaceState &state,
                                      const egl::AttributeMap &attribs) override;
    SurfaceImpl *createPbufferFromClientBuffer(const egl::SurfaceState &state,
                                               EGLenum buftype,
                                               EGLClientBuffer clientBuffer,
                                               const egl::AttributeMap &attribs) override;
    SurfaceImpl *createPixmapSurface(const egl::SurfaceState &state,
                                     NativePixmapType nativePixmap,
                                     const egl::AttributeMap &attribs) override;

    ImageImpl *createImage(const egl::ImageState &state,
                           const gl::Context *context,
                           EGLenum target,
                           const egl::AttributeMap &attribs) override;

    ContextImpl *createContext(const gl::State &state,
                               gl::ErrorSet *errorSet,
                               const egl::Config *configuration,
                               const gl::Context *shareContext,
                               const egl::AttributeMap &attribs) override;

    StreamProducerImpl *createStreamProducerD3DTexture(egl::Stream::ConsumerType consumerType,
                                                       const egl::AttributeMap &attribs) override;

    ExternalImageSiblingImpl *createExternalImageSibling(const gl::Context *context,
                                                         EGLenum target,
                                                         EGLClientBuffer buffer,
                                                         const egl::AttributeMap &attribs) override;

    ShareGroupImpl *createShareGroup(const egl::ShareGroupState &state) override;

    egl::Error makeCurrent(egl::Surface *drawSurface,
                           egl::Surface *readSurface,
                           gl::Context *context) override;

    egl::ConfigSet generateConfigs() override;

    bool testDeviceLost() override;
    egl::Error restoreLostDevice(const egl::ThreadSafeDisplay *display) override;

    bool isValidNativeWindow(EGLNativeWindowType window) const override;
    egl::Error validateClientBuffer(const egl::Config *configuration,
                                    EGLenum buftype,
                                    EGLClientBuffer clientBuffer,
                                    const egl::AttributeMap &attribs) const override;
    egl::Error validateImageClientBuffer(const gl::Context *context,
                                         EGLenum target,
                                         EGLClientBuffer clientBuffer,
                                         const egl::AttributeMap &attribs) const override;

    DeviceImpl *createDevice() override;

    std::string getRendererDescription() override;
    std::string getVendorString() override;
    std::string getVersionString(bool includeFullVersion) override;

    egl::Error waitClient(const gl::Context *context) override;
    egl::Error waitNative(const gl::Context *context, EGLint engine) override;
    gl::Version getMaxSupportedESVersion() const override;
    gl::Version getMaxConformantESVersion() const override;

    void handleResult(HRESULT hr,
                      const char *message,
                      const char *file,
                      const char *function,
                      unsigned int line) override;

    const std::string &getStoredErrorString() const { return mStoredErrorString; }

    void initializeFrontendFeatures(angle::FrontendFeatures *features) const override;

    void populateFeatureList(angle::FeatureList *features) override;

    ThreadSafeDisplayImpl *getThreadSafeDisplayImpl() override { return this; }

  private:
    void generateExtensions(egl::DisplayExtensions *outExtensions) const override;
    void generateCaps(egl::Caps *outCaps) const override;

    egl::Display *mDisplay;

    rx::RendererD3D *mRenderer;
    std::string mStoredErrorString;
};

// Possible reasons RendererD3D initialize can fail
enum D3D11InitError
{
    // The renderer loaded successfully
    D3D11_INIT_SUCCESS = 0,
    // Failed to load the ANGLE & D3D compiler libraries
    D3D11_INIT_COMPILER_ERROR,
    // Failed to load a necessary DLL (non-compiler)
    D3D11_INIT_MISSING_DEP,
    // CreateDevice returned E_INVALIDARG
    D3D11_INIT_CREATEDEVICE_INVALIDARG,
    // CreateDevice failed with an error other than invalid arg
    D3D11_INIT_CREATEDEVICE_ERROR,
    // DXGI 1.2 required but not found
    D3D11_INIT_INCOMPATIBLE_DXGI,
    // Other initialization error
    D3D11_INIT_OTHER_ERROR,
    // CreateDevice returned E_FAIL
    D3D11_INIT_CREATEDEVICE_FAIL,
    // CreateDevice returned E_NOTIMPL
    D3D11_INIT_CREATEDEVICE_NOTIMPL,
    // CreateDevice returned E_OUTOFMEMORY
    D3D11_INIT_CREATEDEVICE_OUTOFMEMORY,
    // CreateDevice returned DXGI_ERROR_INVALID_CALL
    D3D11_INIT_CREATEDEVICE_INVALIDCALL,
    // CreateDevice returned DXGI_ERROR_SDK_COMPONENT_MISSING
    D3D11_INIT_CREATEDEVICE_COMPONENTMISSING,
    // CreateDevice returned DXGI_ERROR_WAS_STILL_DRAWING
    D3D11_INIT_CREATEDEVICE_WASSTILLDRAWING,
    // CreateDevice returned DXGI_ERROR_NOT_CURRENTLY_AVAILABLE
    D3D11_INIT_CREATEDEVICE_NOTAVAILABLE,
    // CreateDevice returned DXGI_ERROR_DEVICE_HUNG
    D3D11_INIT_CREATEDEVICE_DEVICEHUNG,
    // CreateDevice returned NULL
    D3D11_INIT_CREATEDEVICE_NULL,
    NUM_D3D11_INIT_ERRORS
};

}  // namespace rx

#endif  // LIBANGLE_RENDERER_D3D_DISPLAYD3D_H_
