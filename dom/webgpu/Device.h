/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef GPU_DEVICE_H_
#define GPU_DEVICE_H_

#include "ExternalTexture.h"
#include "ObjectModel.h"
#include "mozilla/DOMEventTargetHelper.h"
#include "mozilla/MozPromise.h"
#include "mozilla/RefPtr.h"
#include "mozilla/WeakPtr.h"
#include "mozilla/webgpu/WebGPUTypes.h"
#include "mozilla/webrender/WebRenderAPI.h"
#include "nsTHashSet.h"

namespace mozilla {
namespace dom {
struct GPUExtensions;
struct GPUFeatures;
struct GPULimits;
struct GPUExtent3DDict;

struct GPUBufferDescriptor;
struct GPUTextureDescriptor;
struct GPUExternalTextureDescriptor;
struct GPUSamplerDescriptor;
struct GPUBindGroupLayoutDescriptor;
struct GPUPipelineLayoutDescriptor;
struct GPUBindGroupDescriptor;
struct GPUBlendStateDescriptor;
struct GPUDepthStencilStateDescriptor;
struct GPUInputStateDescriptor;
struct GPUShaderModuleDescriptor;
struct GPUAttachmentStateDescriptor;
struct GPUComputePipelineDescriptor;
struct GPURenderBundleEncoderDescriptor;
struct GPURenderPipelineDescriptor;
struct GPUCommandEncoderDescriptor;
struct GPUCanvasConfiguration;
struct GPUQuerySetDescriptor;

class EventHandlerNonNull;
class Promise;
template <typename T>
class Sequence;
class GPUBufferOrGPUTexture;
enum class GPUDeviceLostReason : uint8_t;
enum class GPUErrorFilter : uint8_t;
enum class GPUFeatureName : uint8_t;
class GPULogCallback;
}  // namespace dom
namespace ipc {
enum class ResponseRejectReason;
}  // namespace ipc

namespace webgpu {
namespace ffi {
struct WGPULimits;
}
class Adapter;
class AdapterInfo;
class BindGroup;
class BindGroupLayout;
class Buffer;
class CommandEncoder;
class ComputePipeline;
class Fence;
class InputState;
class PipelineLayout;
class QuerySet;
class Queue;
class RenderBundleEncoder;
class RenderPipeline;
class Sampler;
class ShaderModule;
class SupportedFeatures;
class SupportedLimits;
class Texture;
class WebGPUChild;

class Device final : public DOMEventTargetHelper,
                     public SupportsWeakPtr,
                     public ObjectBase {
 public:
  NS_DECL_ISUPPORTS_INHERITED
  NS_DECL_CYCLE_COLLECTION_CLASS_INHERITED(Device, DOMEventTargetHelper)
  GPU_DECL_JS_WRAP(Device)

  RefPtr<SupportedFeatures> mFeatures;
  RefPtr<SupportedLimits> mLimits;
  RefPtr<AdapterInfo> mAdapterInfo;
  const bool mSupportSharedTextureInSwapChain;

  static CheckedInt<uint32_t> BufferStrideWithMask(
      const gfx::IntSize& aSize, const gfx::SurfaceFormat& aFormat);

  explicit Device(Adapter* const aParent, RawId aDeviceId, RawId aQueueId,
                  RefPtr<SupportedFeatures> aFeatures,
                  RefPtr<SupportedLimits> aLimits,
                  RefPtr<AdapterInfo> aAdapterInfo,
                  RefPtr<dom::Promise> aLostPromise);

  already_AddRefed<Texture> InitSwapChain(
      const dom::GPUCanvasConfiguration* const aConfig,
      const layers::RemoteTextureOwnerId aOwnerId,
      bool aUseSharedTextureInSwapChain, gfx::SurfaceFormat aFormat,
      gfx::IntSize aCanvasSize);
  bool CheckNewWarning(const nsACString& aMessage);

  void TrackBuffer(Buffer* aBuffer);
  void UntrackBuffer(Buffer* aBuffer);

 private:
  virtual ~Device();
  // Expires external textures in mExternalTexturesToExpire. Scheduled to run
  // as a stable state task when an external texture is imported from an
  // HTMLVideoElement.
  void ExpireExternalTextures();

  // Used to guard losing the device multiple times.
  bool mLost;
  RefPtr<dom::Promise> mLostPromise;
  RefPtr<Queue> mQueue;
  nsTHashSet<nsCString> mKnownWarnings;
  nsTHashSet<Buffer*> mTrackedBuffers;
  ExternalTextureCache mExternalTextureCache;
  // List of external textures due to be expired in the next automatic expiry
  // task.
  nsTArray<WeakPtr<ExternalTexture>> mExternalTexturesToExpire;

 public:
  dom::Promise* GetLost(ErrorResult& aRv);
  void ResolveLost(dom::GPUDeviceLostReason aReason, const nsAString& aMessage);

  const RefPtr<SupportedFeatures>& Features() const { return mFeatures; }
  const RefPtr<SupportedLimits>& Limits() const { return mLimits; }
  const RefPtr<webgpu::AdapterInfo>& GetAdapterInfo() const {
    return mAdapterInfo;
  }
  const RefPtr<Queue>& GetQueue() const { return mQueue; }

  already_AddRefed<Buffer> CreateBuffer(const dom::GPUBufferDescriptor& aDesc,
                                        ErrorResult& aRv);

  static dom::GPUTextureDescriptor SwapChainTextureDescriptor(
      const dom::GPUCanvasConfiguration& aConfig,
      const gfx::IntSize& aCanvasSize);
  already_AddRefed<Texture> CreateTextureForSwapChain(
      const dom::GPUCanvasConfiguration* const aConfig,
      const gfx::IntSize& aCanvasSize,
      const layers::RemoteTextureOwnerId aOwnerId);
  already_AddRefed<Texture> CreateTexture(
      const dom::GPUTextureDescriptor& aDesc);
  already_AddRefed<Texture> CreateTexture(
      const dom::GPUTextureDescriptor& aDesc,
      Maybe<layers::RemoteTextureOwnerId> aOwnerId);
  already_AddRefed<ExternalTexture> ImportExternalTexture(
      const dom::GPUExternalTextureDescriptor& aDesc, ErrorResult& aRv);
  already_AddRefed<Sampler> CreateSampler(
      const dom::GPUSamplerDescriptor& aDesc);

  already_AddRefed<CommandEncoder> CreateCommandEncoder(
      const dom::GPUCommandEncoderDescriptor& aDesc);
  already_AddRefed<RenderBundleEncoder> CreateRenderBundleEncoder(
      const dom::GPURenderBundleEncoderDescriptor& aDesc);

  already_AddRefed<QuerySet> CreateQuerySet(
      const dom::GPUQuerySetDescriptor& aDesc, ErrorResult& aRv);

  already_AddRefed<BindGroupLayout> CreateBindGroupLayout(
      const dom::GPUBindGroupLayoutDescriptor& aDesc);
  already_AddRefed<PipelineLayout> CreatePipelineLayout(
      const dom::GPUPipelineLayoutDescriptor& aDesc);
  already_AddRefed<BindGroup> CreateBindGroup(
      const dom::GPUBindGroupDescriptor& aDesc);

  already_AddRefed<ShaderModule> CreateShaderModule(
      const dom::GPUShaderModuleDescriptor& aDesc, ErrorResult& aRv);
  already_AddRefed<ComputePipeline> CreateComputePipeline(
      const dom::GPUComputePipelineDescriptor& aDesc);
  already_AddRefed<RenderPipeline> CreateRenderPipeline(
      const dom::GPURenderPipelineDescriptor& aDesc);
  already_AddRefed<dom::Promise> CreateComputePipelineAsync(
      const dom::GPUComputePipelineDescriptor& aDesc, ErrorResult& aRv);
  already_AddRefed<dom::Promise> CreateRenderPipelineAsync(
      const dom::GPURenderPipelineDescriptor& aDesc, ErrorResult& aRv);

  void PushErrorScope(const dom::GPUErrorFilter& aFilter);
  already_AddRefed<dom::Promise> PopErrorScope(ErrorResult& aRv);

  void Destroy();

  IMPL_EVENT_HANDLER(uncapturederror)
};

// We can't use MOZ_CAN_RUN_SCRIPT since it's called by a function that has its
// declaration generated by cbindgen.
MOZ_CAN_RUN_SCRIPT_BOUNDARY
void reportCompilationMessagesToConsole(
    const RefPtr<ShaderModule>& aShaderModule,
    const nsTArray<WebGPUCompilationMessage>& aMessages);

}  // namespace webgpu
}  // namespace mozilla

#endif  // GPU_DEVICE_H_
