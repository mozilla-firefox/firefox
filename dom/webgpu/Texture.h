/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef GPU_Texture_H_
#define GPU_Texture_H_

#include <cstdint>

#include "ObjectModel.h"
#include "mozilla/WeakPtr.h"
#include "mozilla/webgpu/WebGPUTypes.h"
#include "mozilla/webgpu/ffi/wgpu.h"
#include "nsWrapperCache.h"

namespace mozilla {
class ErrorResult;
namespace dom {
struct GPUTextureDescriptor;
struct GPUTextureViewDescriptor;
enum class GPUTextureDimension : uint8_t;
enum class GPUTextureFormat : uint8_t;
enum class GPUTextureUsageFlags : uint32_t;
}  // namespace dom

namespace webgpu {

class CanvasContext;
class Device;
class TextureView;

class Texture final : public nsWrapperCache,
                      public ObjectBase,
                      public ChildOf<Device> {
 public:
  GPU_DECL_CYCLE_COLLECTION(Texture)
  GPU_DECL_JS_WRAP(Texture)

  Texture(Device* const aParent, RawId aId,
          const dom::GPUTextureDescriptor& aDesc);

  const dom::GPUTextureFormat mFormat;
  const Maybe<uint8_t> mBytesPerBlock;

  WeakPtr<CanvasContext> mTargetContext;

 private:
  virtual ~Texture();

  const ffi::WGPUExtent3d mSize;
  const uint32_t mMipLevelCount;
  const uint32_t mSampleCount;
  const dom::GPUTextureDimension mDimension;
  const uint32_t mUsage;

 public:
  already_AddRefed<TextureView> CreateView(
      const dom::GPUTextureViewDescriptor& aDesc, ErrorResult& aRv);
  void Destroy();

  uint32_t Width() const { return mSize.width; }
  uint32_t Height() const { return mSize.height; }
  uint32_t DepthOrArrayLayers() const { return mSize.depth_or_array_layers; }
  uint32_t MipLevelCount() const { return mMipLevelCount; }
  uint32_t SampleCount() const { return mSampleCount; }
  dom::GPUTextureDimension Dimension() const { return mDimension; }
  dom::GPUTextureFormat Format() const { return mFormat; }
  uint32_t Usage() const { return mUsage; }
};

}  // namespace webgpu
}  // namespace mozilla

#endif  // GPU_Texture_H_
