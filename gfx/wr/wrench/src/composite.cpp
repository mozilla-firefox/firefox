/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifdef _WIN32

#  include <d3d11.h>
#  include <wrl/client.h>
#  include <dcomp.h>
#  include <assert.h>
#  include <dxgi.h>

#  include <stdio.h>

using namespace Microsoft::WRL;

// A basic composition layer backed by a swap-chain for DirectComposition
class Layer {
 public:
  Layer(int width, int height, bool is_opaque,
        const ComPtr<IDXGIFactory2>& dxgiFactory,
        const ComPtr<ID3D11Device>& d3dDevice,
        const ComPtr<IDCompositionDesktopDevice>& dCompDevice) {
    HRESULT hr;

    DXGI_SWAP_CHAIN_DESC1 desc{};
    desc.Width = width;
    desc.Height = height;
    desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    desc.SampleDesc.Count = 1;
    desc.SampleDesc.Quality = 0;
    desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    desc.BufferCount = 2;
    // DXGI_SCALING_NONE caused swap chain creation failure.
    desc.Scaling = DXGI_SCALING_STRETCH;
    desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL;
    desc.AlphaMode =
        is_opaque ? DXGI_ALPHA_MODE_IGNORE : DXGI_ALPHA_MODE_PREMULTIPLIED;
    desc.Flags = 0;

    // Create a swap-chain, visual, attach them and get the backbuffer texture
    // to draw to

    hr = dxgiFactory->CreateSwapChainForComposition(
        d3dDevice.Get(), &desc, nullptr, mSwapChain.GetAddressOf());
    assert(SUCCEEDED(hr));

    hr = dCompDevice->CreateVisual(mVisual.GetAddressOf());
    assert(SUCCEEDED(hr));

    mVisual->SetContent(mSwapChain.Get());

    hr = mSwapChain->GetBuffer(0, __uuidof(ID3D11Texture2D),
                               (void**)mBackBuffer.GetAddressOf());
    assert(SUCCEEDED(hr));
  }

  ComPtr<IDXGISwapChain1> mSwapChain;
  ComPtr<IDCompositionVisual2> mVisual;
  ComPtr<ID3D11Texture2D> mBackBuffer;
};

// A basic DirectComposition compositor implementation
class Compositor {
 public:
  Compositor(ID3D11Device* aDevice, HWND hWnd) : pD3DDevice(aDevice) {
    HRESULT hr;

    // Get DXGI device from D3D (which was created by ANGLE)
    hr = pD3DDevice.As(&pDXGIDevice);
    assert(SUCCEEDED(hr));

    // Create DirectComposition
    hr = DCompositionCreateDevice2(pDXGIDevice.Get(),
                                   __uuidof(IDCompositionDesktopDevice),
                                   (void**)pDCompDevice.GetAddressOf());
    assert(SUCCEEDED(hr));

    // Bind DC to the hWnd that was created by winit
    hr = pDCompDevice->CreateTargetForHwnd(hWnd, TRUE,
                                           pCompositionTarget.GetAddressOf());
    assert(SUCCEEDED(hr));

    // Create and set root of the visual tree
    hr = pDCompDevice->CreateVisual(pRootVisual.GetAddressOf());
    assert(SUCCEEDED(hr));
    hr = pCompositionTarget->SetRoot(pRootVisual.Get());
    assert(SUCCEEDED(hr));
    pRootVisual->SetBitmapInterpolationMode(
        DCOMPOSITION_BITMAP_INTERPOLATION_MODE_NEAREST_NEIGHBOR);

    // Enable DC debug counter overlay (helpful for seeing if DC composition is
    // active during development)
    hr = pDCompDevice.As(&pDCompDebug);
    assert(SUCCEEDED(hr));
    pDCompDebug->EnableDebugCounters();

    // Get a DXGI factory interface for creating swap-chains
    hr = pDXGIDevice->GetAdapter(&pDXGIAdapter);
    assert(SUCCEEDED(hr));
    hr = pDXGIAdapter->GetParent(__uuidof(IDXGIFactory2),
                                 (void**)pIDXGIFactory.GetAddressOf());
    assert(SUCCEEDED(hr));
  }

  ~Compositor() {}

  // Construct a layer of given dimensions.
  Layer* create_layer(int width, int height, bool is_opaque) {
    Layer* layer = new Layer(width, height, is_opaque, pIDXGIFactory,
                             pD3DDevice, pDCompDevice);

    return layer;
  }

  void begin_frame() { pRootVisual->RemoveAllVisuals(); }

  void add_layer(Layer* layer) {
    // TODO(gwc): Don't add the visual during creation. Once we support multiple
    // swap-chain layers, we'll need to support rebuilding the visual tree for
    // DC as needed.
    HRESULT hr = pRootVisual->AddVisual(layer->mVisual.Get(), FALSE, nullptr);
    assert(SUCCEEDED(hr));
  }

  void end_frame() {
    // TODO(gwc): Only commit if the visual tree was rebuilt
    pDCompDevice->Commit();
  }

 private:
  ComPtr<ID3D11Device> pD3DDevice;
  ComPtr<IDXGIDevice> pDXGIDevice;
  ComPtr<IDCompositionDesktopDevice> pDCompDevice;
  ComPtr<IDCompositionTarget> pCompositionTarget;
  ComPtr<IDCompositionVisual2> pRootVisual;
  ComPtr<IDCompositionDeviceDebug> pDCompDebug;
  ComPtr<IDXGIAdapter> pDXGIAdapter;
  ComPtr<IDXGIFactory2> pIDXGIFactory;
};

// Bindings called by wrench rust impl of the swapchain part of the Compositor trait
extern "C" {
Compositor* wrc_new(void* d3d11_device, void* hwnd) {
  return new Compositor(static_cast<ID3D11Device*>(d3d11_device),
                        static_cast<HWND>(hwnd));
}

void wrc_delete(Compositor* compositor) { delete compositor; }

Layer* wrc_create_layer(Compositor* compositor, int width, int height,
                        bool is_opaque) {
  return compositor->create_layer(width, height, is_opaque);
}

void wrc_destroy_layer(Layer* layer) { delete layer; }

void* wrc_get_layer_backbuffer(Layer* layer) {
  void* p = layer->mBackBuffer.Get();
  return p;
}

void wrc_present_layer(Layer* layer) { layer->mSwapChain->Present(0, 0); }

void wrc_begin_frame(Compositor* compositor) { compositor->begin_frame(); }

void wrc_end_frame(Compositor* compositor) { compositor->end_frame(); }

void wrc_add_layer(Compositor* compositor, Layer* layer) {
  compositor->add_layer(layer);
}

void wrc_set_layer_position(Layer* layer, float x, float y) {
  layer->mVisual->SetOffsetX(x);
  layer->mVisual->SetOffsetY(y);
}
}

#endif
