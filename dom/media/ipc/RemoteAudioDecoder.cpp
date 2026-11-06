/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */
#include "RemoteAudioDecoder.h"

#include "MediaDataDecoderProxy.h"
#include "PDMFactory.h"
#include "RemoteCDMParent.h"
#include "RemoteMediaManagerChild.h"
#include "RemoteMediaManagerParent.h"
#include "mozilla/PRemoteCDMChild.h"
#include "mozilla/StaticPrefs_media.h"

namespace mozilla {

RemoteAudioDecoderChild::RemoteAudioDecoderChild(RemoteMediaIn aLocation)
    : RemoteDecoderChild(aLocation) {}

MediaResult RemoteAudioDecoderChild::ProcessOutput(
    DecodedOutputIPDL&& aDecodedData) {
  AssertOnManagerThread();

  MOZ_ASSERT(aDecodedData.type() == DecodedOutputIPDL::TArrayOfRemoteAudioData);
  RefPtr<ArrayOfRemoteAudioData> arrayData =
      aDecodedData.get_ArrayOfRemoteAudioData();

  for (size_t i = 0; i < arrayData->Count(); i++) {
    RefPtr<AudioData> data = arrayData->ElementAt(i);
    if (!data) {
      // OOM
      return MediaResult(NS_ERROR_OUT_OF_MEMORY, __func__);
    }
    mDecodedData.AppendElement(data);
  }
  return NS_OK;
}

MediaResult RemoteAudioDecoderChild::InitIPDL(
    const AudioInfo& aAudioInfo, const CreateDecoderParams::OptionSet& aOptions,
    const Maybe<uint64_t>& aMediaEngineId, PRemoteCDMActor* aCDM) {
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

  if (!manager->SendPRemoteDecoderConstructor(this, aAudioInfo, aOptions,
                                              aMediaEngineId, Nothing(), cdm)) {
    return MediaResult(
        NS_ERROR_DOM_MEDIA_FATAL_ERR,
        RESULT_DETAIL("RemoteMediaManager unable to construct."));
  }

  return NS_OK;
}

RemoteAudioDecoderParent::RemoteAudioDecoderParent(
    RemoteMediaManagerParent* aParent, const AudioInfo& aAudioInfo,
    const CreateDecoderParams::OptionSet& aOptions,
    nsISerialEventTarget* aManagerThread, TaskQueue* aDecodeTaskQueue,
    Maybe<uint64_t> aMediaEngineId, RemoteCDMParent* aCDM)
    : RemoteDecoderParent(aParent, aOptions, aManagerThread, aDecodeTaskQueue,
                          aMediaEngineId, Nothing(), aCDM),
      mAudioInfo(aAudioInfo) {}

IPCResult RemoteAudioDecoderParent::RecvConstruct(
    ConstructResolver&& aResolver) {
  if (XRE_IsGPUProcess() && !mCDM) {
    return IPC_FAIL(this, "Audio decoding in GPU process requires a CDM");
  }

  if (mDecoder || mShutdown) {
    aResolver(MediaResult(NS_ERROR_ALREADY_INITIALIZED, __func__));
    return IPC_OK();
  }

  if (!IsMediaEngineIdAllowed()) {
    aResolver(MediaResult(NS_ERROR_DOM_MEDIA_FATAL_ERR, __func__));
    return IPC_OK();
  }

  auto params = CreateDecoderParams{
      mAudioInfo,     static_cast<PRemoteCDMActor*>(mCDM.get()),
      mOptions,       CreateDecoderParams::WrapperSet({/* No wrapper */}),
      mMediaEngineId, mTrackingId};

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
        if (self->mDecoder || self->mShutdown) {
          aValue.ResolveValue()->Shutdown();
          resolver(MediaResult(NS_ERROR_ALREADY_INITIALIZED, __func__));
          return;
        }
        self->mDecoder =
            new MediaDataDecoderProxy(aValue.ResolveValue().forget(),
                                      do_AddRef(self->mDecodeTaskQueue.get()));
        resolver(NS_OK);
      });

  return IPC_OK();
}

MediaResult RemoteAudioDecoderParent::ProcessDecodedData(
    MediaDataDecoder::DecodedData&& aData, DecodedOutputIPDL& aDecodedData) {
  MOZ_ASSERT(OnManagerThread());

  // Converted array to array of RefPtr<AudioData>
  nsTArray<RefPtr<AudioData>> data(aData.Length());
  for (auto&& element : aData) {
    MOZ_ASSERT(element->mType == MediaData::Type::AUDIO_DATA,
               "Can only decode audio using RemoteAudioDecoderParent!");
    AudioData* audio = static_cast<AudioData*>(element.get());
    data.AppendElement(audio);
  }
  auto array = MakeRefPtr<ArrayOfRemoteAudioData>();
  if (!array->Fill(std::move(data),
                   [&](size_t aSize) { return AllocateBuffer(aSize); })) {
    return MediaResult(
        NS_ERROR_OUT_OF_MEMORY,
        "Failed in RemoteAudioDecoderParent::ProcessDecodedData");
  }
  aDecodedData = std::move(array);
  return NS_OK;
}

}  // namespace mozilla
