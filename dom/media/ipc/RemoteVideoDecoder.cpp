/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */
#include "RemoteVideoDecoder.h"

#include "AOMDecoder.h"
#include "DAV1DDecoder.h"
#include "mozilla/layers/ImageDataSerializer.h"
#ifdef XP_WIN
#  include "WMFDecoderModule.h"
#endif
#include "GPUVideoImage.h"
#include "ImageContainer.h"  // for PlanarYCbCrData and BufferRecycleBin
#include "MediaDataDecoderProxy.h"
#include "MediaInfo.h"
#include "PDMFactory.h"
#include "RemoteCDMParent.h"
#include "RemoteDecodeUtils.h"
#include "RemoteImageHolder.h"
#include "RemoteMediaManagerParent.h"
#include "mozilla/PRemoteCDMChild.h"
#include "mozilla/StaticPrefs_media.h"
#include "mozilla/gfx/Logging.h"
#include "mozilla/layers/ImageClient.h"
#include "mozilla/layers/TextureClient.h"
#include "mozilla/layers/VideoBridgeChild.h"
#include "nsPrintfCString.h"
#ifdef MOZ_WIDGET_ANDROID
#  include "mozilla/layers/VideoBridgeParent.h"
#endif

namespace mozilla {

using namespace layers;  // for PlanarYCbCrData and BufferRecycleBin
using namespace ipc;
using namespace gfx;

RefPtr<layers::TextureForwarder> KnowsCompositorVideo::GetTextureForwarder() {
  auto vbc = VideoBridgeChild::GetSingleton();
  return (vbc && vbc->CanSend()) ? vbc : nullptr;
}
layers::LayersIPCActor* KnowsCompositorVideo::GetLayersIPCActor() {
  return GetTextureForwarder().get();
}

/* static */ already_AddRefed<KnowsCompositorVideo>
KnowsCompositorVideo::TryCreateForIdentifier(
    const layers::TextureFactoryIdentifier& aIdentifier) {
  auto child = VideoBridgeChild::GetSingleton();
  if (!child) {
    return nullptr;
  }

  RefPtr<KnowsCompositorVideo> knowsCompositor = new KnowsCompositorVideo();
  knowsCompositor->IdentifyTextureHost(aIdentifier);
  return knowsCompositor.forget();
}

RemoteVideoDecoderChild::RemoteVideoDecoderChild(RemoteMediaIn aLocation)
    : RemoteDecoderChild(aLocation), mBufferRecycleBin(new BufferRecycleBin) {}

MediaResult RemoteVideoDecoderChild::ProcessOutput(
    DecodedOutputIPDL&& aDecodedData) {
  AssertOnManagerThread();
  MOZ_ASSERT(aDecodedData.type() == DecodedOutputIPDL::TArrayOfRemoteVideoData);

  nsTArray<RemoteVideoData>& arrayData =
      aDecodedData.get_ArrayOfRemoteVideoData()->Array();

  for (auto&& data : arrayData) {
    if (data.image().IsEmpty()) {
      // This is a NullData object.
      mDecodedData.AppendElement(MakeRefPtr<NullData>(
          data.base().offset(), data.base().time(), data.base().duration()));
      continue;
    }
    MOZ_LOG_FMT(
        gRemoteDecodeLog, LogLevel::Verbose, "Remote video ts={} received: {}",
        data.base().time().ToMicroseconds(), data.image().ToString().get());
    RefPtr<Image> image = data.image().TransferToImage(mBufferRecycleBin);

    RefPtr<VideoData> video = VideoData::CreateFromImage(
        data.display(), data.base().offset(), data.base().time(),
        data.base().duration(), image, data.base().keyframe(),
        data.base().timecode());

    if (!video) {
      // OOM
      return MediaResult(NS_ERROR_OUT_OF_MEMORY, __func__);
    }
    mDecodedData.AppendElement(std::move(video));
  }
  return NS_OK;
}

MediaResult RemoteVideoDecoderChild::InitIPDL(
    const VideoInfo& aVideoInfo, float aFramerate,
    const CreateDecoderParams::OptionSet& aOptions,
    const Maybe<uint64_t>& aMediaEngineId, const Maybe<TrackingId>& aTrackingId,
    PRemoteCDMActor* aCDM) {
  RefPtr<RemoteMediaManagerChild> manager =
      RemoteMediaManagerChild::GetSingleton(mLocation);

  // The manager isn't available because RemoteMediaManagerChild has been
  // initialized with null end points and we don't want to decode video on RDD
  // process anymore. Return false here so that we can fallback to other PDMs.
  if (!manager) {
    return MediaResult(NS_ERROR_DOM_MEDIA_FATAL_ERR,
                       RESULT_DETAIL("RemoteMediaManager is not available."));
  }

  if (!manager->CanSend()) {
    if (mLocation == RemoteMediaIn::GpuProcess) {
      // The manager doesn't support sending messages because the GPU process
      // crashed after RemoteMediaManagerChild::EnsureGPUProcessReady resolved.
      // Constructing the decoder will then fail, and the caller will be
      // notified once the GPU process has been recreated, or is no longer
      // available.
      return NS_OK;
    }

    return MediaResult(NS_ERROR_DOM_MEDIA_FATAL_ERR,
                       RESULT_DETAIL("RemoteMediaManager unable to send."));
  }

  // If we are given a remote CDM, we need to make sure that it has been remoted
  // into the same process as the decoder.
  PRemoteCDMChild* cdm = nullptr;
  if (aCDM) {
    if (aCDM->GetLocation() != mLocation) {
      return MediaResult(
          NS_ERROR_DOM_MEDIA_FATAL_ERR,
          RESULT_DETAIL("PRemoteCDMActor is not in same process."));
    }

    cdm = aCDM->AsPRemoteCDMChild();
    if (!cdm) {
      return MediaResult(
          NS_ERROR_DOM_MEDIA_FATAL_ERR,
          RESULT_DETAIL("PRemoteCDMActor is not PRemoteCDMChild."));
    }

    // If the CDM's process crashed, then the CDM must be recreated by the
    // caller, and we cannot send it.
    if (!cdm->CanSend() || cdm->Manager() != manager) {
      return MediaResult(NS_ERROR_DOM_MEDIA_FATAL_ERR,
                         RESULT_DETAIL("PRemoteCDMChild is in stale process."));
    }
  }

  VideoDecoderInfoIPDL decoderInfo(aVideoInfo, aFramerate);
  if (!manager->SendPRemoteDecoderConstructor(
          this, decoderInfo, aOptions, aMediaEngineId, aTrackingId, cdm)) {
    return MediaResult(
        NS_ERROR_DOM_MEDIA_FATAL_ERR,
        RESULT_DETAIL("RemoteMediaManager unable to construct."));
  }

  return NS_OK;
}

RemoteVideoDecoderParent::RemoteVideoDecoderParent(
    RemoteMediaManagerParent* aParent, const VideoInfo& aVideoInfo,
    float aFramerate, const CreateDecoderParams::OptionSet& aOptions,
    nsISerialEventTarget* aManagerThread, TaskQueue* aDecodeTaskQueue,
    const Maybe<uint64_t>& aMediaEngineId, Maybe<TrackingId> aTrackingId,
    RemoteCDMParent* aCDM)
    : RemoteDecoderParent(aParent, aOptions, aManagerThread, aDecodeTaskQueue,
                          aMediaEngineId, std::move(aTrackingId), aCDM),
      mVideoInfo(aVideoInfo),
      mFramerate(aFramerate) {}

void RemoteVideoDecoderParent::ActorDestroy(ActorDestroyReason aWhy) {
  MOZ_ASSERT(OnManagerThread());
  mVideoBridgeRequest.DisconnectIfExists();
  mIdentifierRequest.DisconnectIfExists();
  RemoteDecoderParent::ActorDestroy(aWhy);
}

IPCResult RemoteVideoDecoderParent::RecvConstruct(
    ConstructResolver&& aResolver) {
  if (mState != State::Unconstructed) {
    aResolver(MediaResult(NS_ERROR_ALREADY_INITIALIZED, __func__));
    return IPC_OK();
  }
  mState = State::Constructing;

  if (!IsMediaEngineIdAllowed()) {
    aResolver(MediaResult(NS_ERROR_DOM_MEDIA_FATAL_ERR, __func__));
    return IPC_OK();
  }

  // If the compositor is being recreated, wait for the new video bridge so that
  // we can share the decoded frames with it.
  layers::VideoBridgeChild::WaitUntilReady()
      ->Then(GetCurrentSerialEventTarget(), __func__,
             [self = RefPtr{this}, resolver = std::move(aResolver)](
                 const GenericNonExclusivePromise::ResolveOrRejectValue&
                     aValue) mutable {
               self->mVideoBridgeRequest.Complete();
               if (aValue.IsReject()) {
                 resolver(MediaResult(NS_ERROR_DOM_MEDIA_CANCELED, __func__));
                 return;
               }
               self->RequestTextureFactoryIdentifier(std::move(resolver));
             })
      ->Track(mVideoBridgeRequest);
  return IPC_OK();
}

void RemoteVideoDecoderParent::RequestTextureFactoryIdentifier(
    ConstructResolver&& aResolver) {
  MOZ_ASSERT(OnManagerThread());
  RefPtr<layers::VideoBridgeChild> videoBridge =
      layers::VideoBridgeChild::GetSingleton();
  if (!videoBridge || !videoBridge->CanSend()) {
    CreateDecoder(Nothing(), std::move(aResolver));
    return;
  }

  // Ask the compositor for its identifier, since the content process may not
  // have the current one, e.g. if the GPU process is being recreated.
  videoBridge->SendGetTextureFactoryIdentifier()
      ->Then(GetCurrentSerialEventTarget(), __func__,
             [self = RefPtr{this}, resolver = std::move(aResolver)](
                 layers::VideoBridgeChild::GetTextureFactoryIdentifierPromise::
                     ResolveOrRejectValue&& aValue) mutable {
               self->mIdentifierRequest.Complete();
               self->CreateDecoder(
                   aValue.IsResolve() ? Some(aValue.ResolveValue()) : Nothing(),
                   std::move(resolver));
             })
      ->Track(mIdentifierRequest);
}

void RemoteVideoDecoderParent::CreateDecoder(
    const Maybe<layers::TextureFactoryIdentifier>& aIdentifier,
    ConstructResolver&& aResolver) {
  MOZ_ASSERT(OnManagerThread());
  if (mState != State::Constructing) {
    aResolver(MediaResult(NS_ERROR_ALREADY_INITIALIZED, __func__));
    return;
  }

  if (!aIdentifier) {
    MOZ_LOG_FMT(gRemoteDecodeLog, LogLevel::Warning,
                "No compositor for content process {} when creating decoder",
                uint64_t(mParent->GetContentId()));
    gfxCriticalNote << "Decoder missing TFI, content "
                    << uint64_t(mParent->GetContentId());
  }

  // Decoders in the GPU process must share their frames with the compositor,
  // so let the content process use another process instead.
  if (!aIdentifier && XRE_IsGPUProcess()) {
    aResolver(MediaResult(NS_ERROR_DOM_MEDIA_FATAL_ERR,
                          "No compositor for the content process"));
    return;
  }

  if (aIdentifier) {
    // Check to see if we have a direct PVideoBridge connection to the
    // destination process specified in aIdentifier, and create a
    // KnowsCompositor representing that connection if so. If this fails, then
    // we fall back to returning the decoded frames directly via Output().
    mKnowsCompositor =
        KnowsCompositorVideo::TryCreateForIdentifier(*aIdentifier);
  }

  auto imageContainer = MakeRefPtr<layers::ImageContainer>(
      layers::ImageUsageType::RemoteVideoDecoder,
      layers::ImageContainer::SYNCHRONOUS);
  if (mKnowsCompositor && XRE_IsRDDProcess()) {
    // Ensure to allocate recycle allocator
    imageContainer->EnsureRecycleAllocatorForRDD(mKnowsCompositor);
  }
  auto params = CreateDecoderParams{
      mVideoInfo,
      mKnowsCompositor,
      imageContainer,
      static_cast<PRemoteCDMActor*>(mCDM.get()),
      CreateDecoderParams::VideoFrameRate(mFramerate),
      mOptions,
      CreateDecoderParams::WrapperSet({/* No wrapper */}),
      mMediaEngineId,
      mTrackingId,
      mCDM,
  };

  mParent->EnsurePDMFactory().CreateDecoder(params)->Then(
      GetCurrentSerialEventTarget(), __func__,
      [resolver = std::move(aResolver), self = RefPtr{this}](
          PlatformDecoderModule::CreateDecoderPromise::ResolveOrRejectValue&&
              aValue) {
        if (aValue.IsReject()) {
          resolver(aValue.RejectValue());
          return;
        }
        MOZ_ASSERT(aValue.ResolveValue());
        if (self->mState != State::Constructing) {
          aValue.ResolveValue()->Shutdown();
          resolver(MediaResult(NS_ERROR_ALREADY_INITIALIZED, __func__));
          return;
        }
        self->mDecoder =
            new MediaDataDecoderProxy(aValue.ResolveValue().forget(),
                                      do_AddRef(self->mDecodeTaskQueue.get()));
        self->mState = State::Constructed;
        resolver(NS_OK);
      });
}

MediaResult RemoteVideoDecoderParent::AdjustDecodeError(
    const MediaResult& aError) {
  MOZ_ASSERT(OnManagerThread());

  // The decoder allocates its images from the video bridge. If the bridge has
  // shut down (e.g. the GPU process crashed), allocations fail and the error is
  // not a real decoding error. Report it as a remote crash so that the child
  // recreates the decoder instead of treating it as fatal.
  if (mKnowsCompositor && !mKnowsCompositor->GetTextureForwarder()) {
    return MediaResult(NS_ERROR_DOM_MEDIA_REMOTE_CRASHED_RDD_OR_GPU_ERR,
                       nsPrintfCString("Video bridge shut down: %s",
                                       aError.Description().get()));
  }
  return aError;
}

MediaResult RemoteVideoDecoderParent::ProcessDecodedData(
    MediaDataDecoder::DecodedData&& aData, DecodedOutputIPDL& aDecodedData) {
  MOZ_ASSERT(OnManagerThread());

  // If the video decoder bridge has shut down, stop.
  if (mKnowsCompositor && !mKnowsCompositor->GetTextureForwarder()) {
    aDecodedData = MakeRefPtr<ArrayOfRemoteVideoData>();
    return NS_OK;
  }

  nsTArray<RemoteVideoData> array;

  for (const auto& data : aData) {
    MOZ_ASSERT(data->mType == MediaData::Type::VIDEO_DATA ||
                   data->mType == MediaData::Type::NULL_DATA,
               "Can only decode videos using RemoteDecoderParent!");
    if (data->mType == MediaData::Type::NULL_DATA) {
      RemoteVideoData output(
          MediaDataIPDL(data->mOffset, data->mTime, data->mTimecode,
                        data->mDuration, data->mKeyframe),
          IntSize(), RemoteImageHolder(), -1);

      array.AppendElement(std::move(output));
      continue;
    }
    VideoData* video = static_cast<VideoData*>(data.get());

    MOZ_ASSERT(video->mImage,
               "Decoded video must output a layer::Image to "
               "be used with RemoteDecoderParent");

    RefPtr<TextureClient> texture;
    SurfaceDescriptor sd;
    IntSize size;
    bool needStorage = false;

    YUVColorSpace yuvColorSpace = gfx::YUVColorSpace::Default;
    ColorSpace2 colorPrimaries = gfx::ColorSpace2::UNKNOWN;
    TransferFunction transferFunction = gfx::TransferFunction::BT709;
    ColorRange colorRange = gfx::ColorRange::LIMITED;

    PlanarYCbCrImage* image = video->mImage->AsPlanarYCbCrImage();
    const PlanarYCbCrData* imageData = image ? image->GetData() : nullptr;

    if (mKnowsCompositor) {
      texture = video->mImage->GetTextureClient(mKnowsCompositor);

      if (!texture) {
        texture = ImageClient::CreateTextureClientForImage(video->mImage,
                                                           mKnowsCompositor);
      }

      if (texture) {
        if (!texture->IsAddedToCompositableClient()) {
          texture->InitIPDLActor(mKnowsCompositor, mParent->GetContentId());
          texture->SetAddedToCompositableClient();
        }
        needStorage = true;
        SurfaceDescriptorRemoteDecoder remoteSD;
        texture->GetSurfaceDescriptorRemoteDecoder(&remoteSD);
        sd = remoteSD;
        size = texture->GetSize();
      }
    }

    // If failed to create a GPU accelerated surface descriptor, fall back to
    // copying frames via shmem.
    if (!IsSurfaceDescriptorValid(sd)) {
      needStorage = false;
      if (!image) {
        return MediaResult(NS_ERROR_UNEXPECTED,
                           "Expected Planar YCbCr image in "
                           "RemoteVideoDecoderParent::ProcessDecodedData");
      }

      SurfaceDescriptorBuffer sdBuffer;
      nsresult rv = image->BuildSurfaceDescriptorBuffer(
          sdBuffer, Image::BuildSdbFlags::Default, [&](uint32_t aBufferSize) {
            ShmemBuffer buffer = AllocateBuffer(aBufferSize);
            if (buffer.Valid()) {
              return MemoryOrShmem(std::move(buffer.Get()));
            }
            return MemoryOrShmem();
          });

      if (NS_WARN_IF(NS_FAILED(rv))) {
        if (sdBuffer.data().type() == MemoryOrShmem::TShmem) {
          DeallocShmem(sdBuffer.data().get_Shmem());
        }
        return rv;
      }

      sd = sdBuffer;
      size = image->GetSize();
    }

    if ((!needStorage ||
         StaticPrefs::media_decoder_frame_color_metadata_enabled()) &&
        imageData) {
      yuvColorSpace = imageData->mYUVColorSpace;
      colorPrimaries = imageData->mColorPrimaries;
      transferFunction = imageData->mTransferFunction;
      colorRange = imageData->mColorRange;
    }

    if (needStorage) {
      MOZ_ASSERT(sd.type() != SurfaceDescriptor::TSurfaceDescriptorBuffer);
      mParent->StoreImage(static_cast<const SurfaceDescriptorGPUVideo&>(sd),
                          video->mImage, texture);
    }

    RemoteImageHolder imageHolder(
        mParent,
        XRE_IsGPUProcess()
            ? VideoBridgeSource::GpuProcess
            : (XRE_IsRDDProcess() ? VideoBridgeSource::RddProcess
                                  : VideoBridgeSource::MFMediaEngineCDMProcess),
        size, video->mImage->GetColorDepth(), sd, yuvColorSpace, colorPrimaries,
        transferFunction, colorRange);
    MOZ_LOG_FMT(
        gRemoteDecodeLog, LogLevel::Verbose,
        "Remote video ts={} send via {}: {}", video->mTime.ToMicroseconds(),
        needStorage ? "texture" : "shmem", imageHolder.ToString().get());

    RemoteVideoData output(
        MediaDataIPDL(data->mOffset, data->mTime, data->mTimecode,
                      data->mDuration, data->mKeyframe),
        video->mDisplay, std::move(imageHolder), video->mFrameID);

    array.AppendElement(std::move(output));
  }

  aDecodedData = MakeRefPtr<ArrayOfRemoteVideoData>(std::move(array));

  return NS_OK;
}

}  // namespace mozilla
