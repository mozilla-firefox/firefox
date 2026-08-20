/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */
#include "RemoteDecoderChild.h"

#include "RemoteMediaManagerChild.h"
#include "mozilla/RemoteDecodeUtils.h"

namespace mozilla {

RemoteDecoderChild::RemoteDecoderChild(RemoteMediaIn aLocation)
    : ShmemRecycleAllocator(this),
      mLocation(aLocation),
      mThread(GetCurrentSerialEventTarget()) {
#ifdef MOZ_DIAGNOSTIC_ASSERT_ENABLED
  auto managerThread = RemoteMediaManagerChild::GetManagerThread();
  MOZ_DIAGNOSTIC_ASSERT(managerThread);
  MOZ_DIAGNOSTIC_ASSERT(managerThread->IsOnCurrentThread());
#endif
}

RemoteDecoderChild::~RemoteDecoderChild() = default;

// ActorDestroy is called if the channel goes down while waiting for a response.
void RemoteDecoderChild::ActorDestroy(ActorDestroyReason aWhy) {
  mRemoteDecoderCrashed = (aWhy == AbnormalShutdown);
  mInitPromiseRequest.DisconnectIfExists();

  const nsresult err = mRemoteDecoderCrashed ? GetCrashedErrorCode()
                                             : NS_ERROR_DOM_MEDIA_CANCELED;

  mInitPromise.RejectIfExists(err, __func__);
  mDecodePromise.RejectIfExists(err, __func__);
  mDrainPromise.RejectIfExists(err, __func__);
  mFlushPromise.RejectIfExists(err, __func__);
  mShutdownPromise.ResolveIfExists(true, __func__);
  mDecodedData.Clear();
  CleanupShmemRecycleAllocator();
  RecordShutdownTelemetry(mRemoteDecoderCrashed);
}

void RemoteDecoderChild::DestroyIPDL() {
  AssertOnManagerThread();
  MOZ_DIAGNOSTIC_ASSERT(mInitPromise.IsEmpty() && mDecodePromise.IsEmpty() &&
                            mDrainPromise.IsEmpty() &&
                            mFlushPromise.IsEmpty() &&
                            mShutdownPromise.IsEmpty(),
                        "All promises should have been rejected");
  if (CanSend()) {
    PRemoteDecoderChild::Send__delete__(this);
  }
}

// MediaDataDecoder methods

RefPtr<MediaDataDecoder::InitPromise> RemoteDecoderChild::Init() {
  AssertOnManagerThread();

  mRemoteDecoderCrashed = false;

  RefPtr<RemoteDecoderChild> self = this;
  SendInit()
      ->Then(
          mThread, __func__,
          [self, this](InitResultIPDL&& aResponse) {
            mInitPromiseRequest.Complete();
            if (aResponse.type() == InitResultIPDL::TMediaResult) {
              mInitPromise.Reject(aResponse.get_MediaResult(), __func__);
              return;
            }
            const auto& initResponse = aResponse.get_InitCompletionIPDL();
            mDescription = initResponse.decoderDescription();
            mDescription.Append(" (");
            mDescription.Append(RemoteMediaInToStr(GetManager()->Location()));
            mDescription.Append(" remote)");

            mProcessName = initResponse.decoderProcessName();
            mCodecName = initResponse.decoderCodecName();

            mIsHardwareAccelerated = initResponse.hardware();
            mHardwareAcceleratedReason = initResponse.hardwareReason();
            mConversion = initResponse.conversion();
            mShouldDecoderAlwaysBeRecycled =
                initResponse.shouldDecoderAlwaysBeRecycled();
            for (auto p : initResponse.decodeProperties()) {
              mDecodeProperties[p.name()] = Some(p.value());
            }
            // Either the promise has not yet been resolved or the handler has
            // been disconnected and we can't get here.
            mInitPromise.Resolve(initResponse.type(), __func__);
          },
          [self](const mozilla::ipc::ResponseRejectReason& aReason) {
            self->mInitPromiseRequest.Complete();
            RemoteMediaManagerChild::HandleRejectionError(
                self->GetManager(), self->mLocation, aReason,
                [self](const MediaResult& aError) {
                  self->mInitPromise.RejectIfExists(aError, __func__);
                });
          })
      ->Track(mInitPromiseRequest);

  return mInitPromise.Ensure(__func__);
}

RefPtr<MediaDataDecoder::DecodePromise> RemoteDecoderChild::Decode(
    const nsTArray<RefPtr<MediaRawData>>& aSamples) {
  AssertOnManagerThread();

  if (mRemoteDecoderCrashed) {
    return MediaDataDecoder::DecodePromise::CreateAndReject(
        GetCrashedErrorCode(), __func__);
  }

  auto samples = MakeRefPtr<ArrayOfRemoteMediaRawData>();
  if (!samples->Fill(aSamples,
                     [&](size_t aSize) { return AllocateBuffer(aSize); })) {
    return MediaDataDecoder::DecodePromise::CreateAndReject(
        NS_ERROR_OUT_OF_MEMORY, __func__);
  }
  SendDecode(samples)->Then(
      mThread, __func__,
      [self = RefPtr{this}, this](
          PRemoteDecoderChild::DecodePromise::ResolveOrRejectValue&& aValue) {
        // We no longer need the samples as the data has been
        // processed by the parent.
        // If the parent died, the error being fatal will cause the
        // decoder to be torn down and all shmem in the pool will be
        // deallocated.
        ReleaseAllBuffers();

        if (aValue.IsReject()) {
          RemoteMediaManagerChild::HandleRejectionError(
              self->GetManager(), self->mLocation, aValue.RejectValue(),
              [self](const MediaResult& aError) {
                self->mDecodePromise.RejectIfExists(aError, __func__);
              });
          return;
        }
        MOZ_DIAGNOSTIC_ASSERT(CanSend(),
                              "The parent unexpectedly died, promise should "
                              "have been rejected first");
        if (mDecodePromise.IsEmpty()) {
          // We got flushed.
          return;
        }
        auto response = std::move(aValue.ResolveValue());
        if (response.type() == DecodeResultIPDL::TMediaResult &&
            NS_FAILED(response.get_MediaResult())) {
          mDecodePromise.Reject(response.get_MediaResult(), __func__);
          return;
        }
        if (response.type() == DecodeResultIPDL::TDecodedOutputIPDL) {
          ProcessOutput(std::move(response.get_DecodedOutputIPDL()));
        }
        mDecodePromise.Resolve(std::move(mDecodedData), __func__);
        mDecodedData = MediaDataDecoder::DecodedData();
      });

  return mDecodePromise.Ensure(__func__);
}

RefPtr<MediaDataDecoder::FlushPromise> RemoteDecoderChild::Flush() {
  AssertOnManagerThread();
  mDecodePromise.RejectIfExists(NS_ERROR_DOM_MEDIA_CANCELED, __func__);
  mDrainPromise.RejectIfExists(NS_ERROR_DOM_MEDIA_CANCELED, __func__);

  RefPtr<RemoteDecoderChild> self = this;
  SendFlush()->Then(
      mThread, __func__,
      [self](const MediaResult& aResult) {
        if (NS_SUCCEEDED(aResult)) {
          self->mFlushPromise.ResolveIfExists(true, __func__);
        } else {
          self->mFlushPromise.RejectIfExists(aResult, __func__);
        }
      },
      [self](const mozilla::ipc::ResponseRejectReason& aReason) {
        RemoteMediaManagerChild::HandleRejectionError(
            self->GetManager(), self->mLocation, aReason,
            [self](const MediaResult& aError) {
              self->mFlushPromise.RejectIfExists(aError, __func__);
            });
      });
  return mFlushPromise.Ensure(__func__);
}

RefPtr<MediaDataDecoder::DecodePromise> RemoteDecoderChild::Drain() {
  AssertOnManagerThread();

  RefPtr<RemoteDecoderChild> self = this;
  SendDrain()->Then(
      mThread, __func__,
      [self, this](DecodeResultIPDL&& aResponse) {
        if (mDrainPromise.IsEmpty()) {
          // We got flushed.
          return;
        }
        if (aResponse.type() == DecodeResultIPDL::TMediaResult &&
            NS_FAILED(aResponse.get_MediaResult())) {
          mDrainPromise.Reject(aResponse.get_MediaResult(), __func__);
          return;
        }
        MOZ_DIAGNOSTIC_ASSERT(CanSend(),
                              "The parent unexpectedly died, promise should "
                              "have been rejected first");
        if (aResponse.type() == DecodeResultIPDL::TDecodedOutputIPDL) {
          ProcessOutput(std::move(aResponse.get_DecodedOutputIPDL()));
        }
        mDrainPromise.Resolve(std::move(mDecodedData), __func__);
        mDecodedData = MediaDataDecoder::DecodedData();
      },
      [self](const mozilla::ipc::ResponseRejectReason& aReason) {
        RemoteMediaManagerChild::HandleRejectionError(
            self->GetManager(), self->mLocation, aReason,
            [self](const MediaResult& aError) {
              self->mDrainPromise.RejectIfExists(aError, __func__);
            });
      });
  return mDrainPromise.Ensure(__func__);
}

RefPtr<mozilla::ShutdownPromise> RemoteDecoderChild::Shutdown() {
  AssertOnManagerThread();
  // Shutdown() can be called while an InitPromise is pending.
  mInitPromiseRequest.DisconnectIfExists();
  mInitPromise.RejectIfExists(NS_ERROR_DOM_MEDIA_CANCELED, __func__);
  mDecodePromise.RejectIfExists(NS_ERROR_DOM_MEDIA_CANCELED, __func__);
  mDrainPromise.RejectIfExists(NS_ERROR_DOM_MEDIA_CANCELED, __func__);
  mFlushPromise.RejectIfExists(NS_ERROR_DOM_MEDIA_CANCELED, __func__);

  RefPtr<RemoteDecoderChild> self = this;
  SendShutdown()->Then(
      mThread, __func__,
      [self](
          PRemoteDecoderChild::ShutdownPromise::ResolveOrRejectValue&& aValue) {
        self->mShutdownPromise.ResolveIfExists(aValue.IsResolve(), __func__);
      });
  return mShutdownPromise.Ensure(__func__);
}

bool RemoteDecoderChild::IsHardwareAccelerated(
    nsACString& aFailureReason) const {
  AssertOnManagerThread();
  aFailureReason = mHardwareAcceleratedReason;
  return mIsHardwareAccelerated;
}

nsCString RemoteDecoderChild::GetDescriptionName() const {
  AssertOnManagerThread();
  return mDescription;
}

nsCString RemoteDecoderChild::GetProcessName() const {
  AssertOnManagerThread();
  return mProcessName;
}

nsCString RemoteDecoderChild::GetCodecName() const {
  AssertOnManagerThread();
  return mCodecName;
}

void RemoteDecoderChild::SetSeekThreshold(const media::TimeUnit& aTime) {
  AssertOnManagerThread();
  (void)SendSetSeekThreshold(aTime);
}

MediaDataDecoder::ConversionRequired RemoteDecoderChild::NeedsConversion()
    const {
  AssertOnManagerThread();
  return mConversion;
}

bool RemoteDecoderChild::ShouldDecoderAlwaysBeRecycled() const {
  AssertOnManagerThread();
  return mShouldDecoderAlwaysBeRecycled;
}

void RemoteDecoderChild::AssertOnManagerThread() const {
  MOZ_ASSERT(mThread->IsOnCurrentThread());
}

nsresult RemoteDecoderChild::GetCrashedErrorCode() const {
  if (mLocation == RemoteMediaIn::GpuProcess ||
      mLocation == RemoteMediaIn::RddProcess) {
    return NS_ERROR_DOM_MEDIA_REMOTE_CRASHED_RDD_OR_GPU_ERR;
  }
  if (mLocation == RemoteMediaIn::UtilityProcess_MFMediaEngineCDM) {
    return NS_ERROR_DOM_MEDIA_REMOTE_CRASHED_MF_CDM_ERR;
  }
  return NS_ERROR_DOM_MEDIA_REMOTE_CRASHED_UTILITY_ERR;
}

RemoteMediaManagerChild* RemoteDecoderChild::GetManager() {
  if (!CanSend()) {
    return nullptr;
  }
  return mozilla::ipc::ActorCast<RemoteMediaManagerChild>(Manager());
}

}  // namespace mozilla
