/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */
#include "RemoteDecoderParent.h"

#include "RemoteCDMParent.h"
#include "RemoteMediaManagerParent.h"
#include "mozilla/EnumeratedRange.h"

#ifdef MOZ_WMF_MEDIA_ENGINE
#  include "MFMediaEngineParent.h"
#endif

namespace mozilla {

namespace {
template <typename T>
void RejectResolver(Maybe<T>& aResolver, const char* aWhere) {
  if (aResolver) {
    auto resolver = std::move(*aResolver);
    aResolver.reset();
    resolver(MediaResult(NS_ERROR_DOM_MEDIA_CANCELED, aWhere));
  }
}
}  // namespace

RemoteDecoderParent::RemoteDecoderParent(
    RemoteMediaManagerParent* aParent,
    const CreateDecoderParams::OptionSet& aOptions,
    nsISerialEventTarget* aManagerThread, TaskQueue* aDecodeTaskQueue,
    const Maybe<uint64_t>& aMediaEngineId, Maybe<TrackingId> aTrackingId,
    RemoteCDMParent* aCDM)
    : ShmemRecycleAllocator(this),
      mParent(aParent),
      mOptions(aOptions),
      mDecodeTaskQueue(aDecodeTaskQueue),
      mCDM(aCDM),
      mTrackingId(aTrackingId),
      mMediaEngineId(aMediaEngineId),
      mManagerThread(aManagerThread) {
  MOZ_COUNT_CTOR(RemoteDecoderParent);
  MOZ_ASSERT(OnManagerThread());
}

RemoteDecoderParent::~RemoteDecoderParent() {
  MOZ_COUNT_DTOR(RemoteDecoderParent);
}

mozilla::ipc::IPCResult RemoteDecoderParent::RecvInit(
    InitResolver&& aResolver) {
  MOZ_ASSERT(OnManagerThread());
  if (!mDecoder) {
    aResolver(MediaResult(NS_ERROR_ABORT, __func__));
    return IPC_OK();
  }

  if (mState != State::Constructed) {
    aResolver(MediaResult(NS_ERROR_ALREADY_INITIALIZED, __func__));
    return IPC_OK();
  }
  mState = State::Initializing;

  mPendingInitResolver.emplace(std::move(aResolver));
  RefPtr<RemoteDecoderParent> self = this;
  mInitRequest.DisconnectIfExists();
  mDecoder->Init()
      ->Then(
          mManagerThread, __func__,
          [self](MediaDataDecoder::InitPromise::ResolveOrRejectValue&& aValue) {
            self->mInitRequest.Complete();
            if (!self->CanSend() || !self->mPendingInitResolver) {
              return;
            }
            auto resolver = std::move(*self->mPendingInitResolver);
            self->mPendingInitResolver.reset();
            if (aValue.IsReject()) {
              resolver(aValue.RejectValue());
              return;
            }
            self->mState = State::Initialized;
            auto track = aValue.ResolveValue();
            MOZ_ASSERT(track == TrackInfo::kAudioTrack ||
                       track == TrackInfo::kVideoTrack);
            if (self->mDecoder) {
              nsCString hardwareReason;
              bool hardwareAccelerated =
                  self->mDecoder->IsHardwareAccelerated(hardwareReason);
              nsTArray<DecodePropertyIPDL> properties;
              for (auto name : MakeInclusiveEnumeratedRange(
                       MediaDataDecoder::sHighestPropertyName)) {
                if (auto v = self->mDecoder->GetDecodeProperty(name)) {
                  properties.AppendElement(
                      DecodePropertyIPDL(name, std::move(v.ref())));
                }
              }
              resolver(InitCompletionIPDL{
                  track, self->mDecoder->GetDescriptionName(),
                  self->mDecoder->GetProcessName(),
                  self->mDecoder->GetCodecName(), hardwareAccelerated,
                  hardwareReason, self->mDecoder->NeedsConversion(),
                  self->mDecoder->ShouldDecoderAlwaysBeRecycled(), properties});
            }
          })
      ->Track(mInitRequest);
  return IPC_OK();
}

void RemoteDecoderParent::DecodeNextSample(
    const RefPtr<ArrayOfRemoteMediaRawData>& aData, size_t aIndex,
    MediaDataDecoder::DecodedData&& aOutput) {
  MOZ_ASSERT(OnManagerThread());

  if (!CanSend() || !mPendingDecodeResolver) {
    return;
  }

  if (!mDecoder) {
    auto resolver = std::move(*mPendingDecodeResolver);
    mPendingDecodeResolver.reset();
    resolver(MediaResult(NS_ERROR_ABORT, __func__));
    return;
  }

  if (aData->Count() == aIndex) {
    auto resolver = std::move(*mPendingDecodeResolver);
    mPendingDecodeResolver.reset();
    DecodedOutputIPDL result;
    MediaResult rv = ProcessDecodedData(std::move(aOutput), result);
    if (NS_FAILED(rv)) {
      resolver(AdjustDecodeError(rv));  // Out of Memory.
    } else {
      resolver(std::move(result));
    }
    return;
  }

  RefPtr<MediaRawData> rawData = aData->ElementAt(aIndex);
  if (!rawData) {
    auto resolver = std::move(*mPendingDecodeResolver);
    mPendingDecodeResolver.reset();
    resolver(MediaResult(NS_ERROR_OUT_OF_MEMORY, __func__));
    return;
  }

  mDecodeRequest.DisconnectIfExists();
  mDecoder->Decode(rawData)
      ->Then(mManagerThread, __func__,
             [self = RefPtr{this}, this, aData, aIndex,
              output = std::move(aOutput)](
                 MediaDataDecoder::DecodePromise::ResolveOrRejectValue&&
                     aValue) mutable {
               mDecodeRequest.Complete();
               if (aValue.IsReject()) {
                 if (mPendingDecodeResolver) {
                   auto resolver = std::move(*mPendingDecodeResolver);
                   mPendingDecodeResolver.reset();
                   resolver(AdjustDecodeError(aValue.RejectValue()));
                 }
                 return;
               }

               output.AppendElements(std::move(aValue.ResolveValue()));

               // Call again in case we have more data to decode.
               DecodeNextSample(aData, aIndex + 1, std::move(output));
             })
      ->Track(mDecodeRequest);
}

mozilla::ipc::IPCResult RemoteDecoderParent::RecvDecode(
    NotNull<ArrayOfRemoteMediaRawData*> aData, DecodeResolver&& aResolver) {
  MOZ_ASSERT(OnManagerThread());
  if (!mDecoder || mState != State::Initialized) {
    aResolver(MediaResult(NS_ERROR_ABORT, __func__));
    return IPC_OK();
  }

  // If we are here, we know all previously returned DecodedOutputIPDL got
  // used by the child. We can mark all previously sent ShmemBuffer as
  // available again.
  ReleaseAllBuffers();
  MOZ_DIAGNOSTIC_ASSERT(!mPendingDecodeResolver,
                        "overlapping Decode in RemoteDecoderParent");
  mPendingDecodeResolver.emplace(std::move(aResolver));
  MediaDataDecoder::DecodedData output;
  DecodeNextSample(aData.get(), 0, std::move(output));

  return IPC_OK();
}

mozilla::ipc::IPCResult RemoteDecoderParent::RecvFlush(
    FlushResolver&& aResolver) {
  MOZ_ASSERT(OnManagerThread());
  if (!mDecoder || mState != State::Initialized) {
    aResolver(MediaResult(NS_ERROR_ABORT, __func__));
    return IPC_OK();
  }

  MOZ_DIAGNOSTIC_ASSERT(!mPendingFlushResolver,
                        "overlapping Flush in RemoteDecoderParent");
  mPendingFlushResolver.emplace(std::move(aResolver));
  RefPtr<RemoteDecoderParent> self = this;
  mFlushRequest.DisconnectIfExists();
  mDecoder->Flush()
      ->Then(
          mManagerThread, __func__,
          [self](
              MediaDataDecoder::FlushPromise::ResolveOrRejectValue&& aValue) {
            self->mFlushRequest.Complete();
            if (!self->mPendingFlushResolver) {
              return;
            }
            auto resolver = std::move(*self->mPendingFlushResolver);
            self->mPendingFlushResolver.reset();
            self->ReleaseAllBuffers();
            if (aValue.IsReject()) {
              resolver(aValue.RejectValue());
            } else {
              resolver(MediaResult(NS_OK));
            }
          })
      ->Track(mFlushRequest);

  return IPC_OK();
}

mozilla::ipc::IPCResult RemoteDecoderParent::RecvDrain(
    DrainResolver&& aResolver) {
  MOZ_ASSERT(OnManagerThread());
  if (!mDecoder || mState != State::Initialized) {
    aResolver(MediaResult(NS_ERROR_ABORT, __func__));
    return IPC_OK();
  }

  MOZ_DIAGNOSTIC_ASSERT(!mPendingDrainResolver,
                        "overlapping Drain in RemoteDecoderParent");
  mPendingDrainResolver.emplace(std::move(aResolver));
  RefPtr<RemoteDecoderParent> self = this;
  mDrainRequest.DisconnectIfExists();
  mDecoder->Drain()
      ->Then(
          mManagerThread, __func__,
          [self, this](
              MediaDataDecoder::DecodePromise::ResolveOrRejectValue&& aValue) {
            mDrainRequest.Complete();
            if (!mPendingDrainResolver) {
              return;
            }
            auto resolver = std::move(*mPendingDrainResolver);
            mPendingDrainResolver.reset();
            ReleaseAllBuffers();
            if (!self->CanSend()) {
              resolver(MediaResult(NS_ERROR_DOM_MEDIA_CANCELED, __func__));
              return;
            }
            if (aValue.IsReject()) {
              resolver(AdjustDecodeError(aValue.RejectValue()));
              return;
            }
            DecodedOutputIPDL output;
            MediaResult rv =
                ProcessDecodedData(std::move(aValue.ResolveValue()), output);
            if (NS_FAILED(rv)) {
              resolver(AdjustDecodeError(rv));
            } else {
              resolver(std::move(output));
            }
          })
      ->Track(mDrainRequest);
  return IPC_OK();
}

mozilla::ipc::IPCResult RemoteDecoderParent::RecvShutdown(
    ShutdownResolver&& aResolver) {
  MOZ_ASSERT(OnManagerThread());
  AbortPendingRequests();
  if (mDecoder) {
    RefPtr<RemoteDecoderParent> self = this;
    mDecoder->Shutdown()->Then(
        mManagerThread, __func__,
        [self, resolver = std::move(aResolver)](
            const ShutdownPromise::ResolveOrRejectValue& aValue) {
          MOZ_ASSERT(aValue.IsResolve());
          self->ReleaseAllBuffers();
          resolver(true);
        });
  } else {
    aResolver(true);
  }
  mDecoder = nullptr;
  mState = State::ShutDown;
  return IPC_OK();
}

mozilla::ipc::IPCResult RemoteDecoderParent::RecvSetSeekThreshold(
    const TimeUnit& aTime) {
  MOZ_ASSERT(OnManagerThread());
  if (mDecoder && mState == State::Initialized) {
    mDecoder->SetSeekThreshold(aTime);
  }
  return IPC_OK();
}

void RemoteDecoderParent::ActorDestroy(ActorDestroyReason aWhy) {
  MOZ_ASSERT(OnManagerThread());
  // Ensure a decoder still being created is shut down when it is ready.
  mState = State::ShutDown;
  AbortPendingRequests();
  if (mDecoder) {
    mDecoder->Shutdown();
    mDecoder = nullptr;
  }
  CleanupShmemRecycleAllocator();
}

void RemoteDecoderParent::AbortPendingRequests() {
  MOZ_ASSERT(OnManagerThread());
  RejectResolver(mPendingInitResolver, __func__);
  RejectResolver(mPendingDecodeResolver, __func__);
  RejectResolver(mPendingFlushResolver, __func__);
  RejectResolver(mPendingDrainResolver, __func__);
  mInitRequest.DisconnectIfExists();
  mDecodeRequest.DisconnectIfExists();
  mFlushRequest.DisconnectIfExists();
  mDrainRequest.DisconnectIfExists();
}

bool RemoteDecoderParent::OnManagerThread() {
  return RemoteMediaManagerParent::OnManagerThread();
}

bool RemoteDecoderParent::IsMediaEngineIdAllowed() const {
#ifdef MOZ_WMF_MEDIA_ENGINE
  return !mMediaEngineId ||
         MFMediaEngineParent::IsMediaEngineIdAllowed(*mMediaEngineId, mParent);
#else
  return true;
#endif
}

}  // namespace mozilla
