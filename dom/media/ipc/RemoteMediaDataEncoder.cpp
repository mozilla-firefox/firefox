/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "RemoteMediaDataEncoder.h"

#include "RemoteDecodeUtils.h"
#include "mozilla/dom/WebCodecsUtils.h"

namespace mozilla {

extern LazyLogModule sPEMLog;

#define AUTO_MARKER(var, postfix) \
  AutoWebCodecsMarker var("RemoteMediaDataEncoder", postfix);

#define LOGE(fmt, ...)                           \
  MOZ_LOG_FMT(sPEMLog, mozilla::LogLevel::Error, \
              "[RemoteMediaDataEncoder] {}: " fmt, __func__, __VA_ARGS__)
#define LOGW(fmt, ...)                             \
  MOZ_LOG_FMT(sPEMLog, mozilla::LogLevel::Warning, \
              "[RemoteMediaDataEncoder] {}: " fmt, __func__, __VA_ARGS__)
#define LOGD(fmt, ...)                           \
  MOZ_LOG_FMT(sPEMLog, mozilla::LogLevel::Debug, \
              "[RemoteMediaDataEncoder] {}: " fmt, __func__, __VA_ARGS__)
#define LOGV(fmt, ...)                             \
  MOZ_LOG_FMT(sPEMLog, mozilla::LogLevel::Verbose, \
              "[RemoteMediaDataEncoder] {}: " fmt, __func__, __VA_ARGS__)

RemoteMediaDataEncoder::RemoteMediaDataEncoder(
    nsCOMPtr<nsISerialEventTarget>&& aThread, RemoteMediaIn aLocation)
    : mChild(MakeRefPtr<RemoteMediaDataEncoderChild>()),
      mThread(std::move(aThread)),
      mLocation(aLocation) {
  LOGV("[{}] child {}", fmt::ptr(this), fmt::ptr(mChild.get()));
}

RemoteMediaDataEncoder::~RemoteMediaDataEncoder() {
  LOGV("[{}]", fmt::ptr(this));
  // If this is the last reference, and we still have an actor, then we know
  // that the last reference is solely due to the IPDL reference. Dispatch to
  // the owning thread to delete that so that we can clean up.
  MutexAutoLock lock(mMutex);
  if (mNeedsShutdown) {
    mNeedsShutdown = false;
    mThread->Dispatch(
        NS_NewRunnableFunction(__func__, [child = RefPtr{mChild}]() {
          if (child->CanSend()) {
            LOGD("[{}] destroying final self reference", fmt::ptr(child.get()));
            child->Send__delete__(child);
          }
        }));
  }
}

RefPtr<PlatformEncoderModule::CreateEncoderPromise>
RemoteMediaDataEncoder::Construct() {
  {
    MutexAutoLock lock(mMutex);
    mNeedsShutdown = mChild->CanSend();
  }

  LOGD("[{}] send", fmt::ptr(this));
  mChild->SendConstruct()->Then(
      mThread, __func__,
      [self = RefPtr{this}](MediaResult aResult) {
        LOGD("[{}] Construct resolved code={}", fmt::ptr(self.get()),
             aResult.Description());
        if (NS_FAILED(aResult.Code())) {
          self->mConstructPromise.RejectIfExists(aResult, __func__);
          self->mInitPromise.RejectIfExists(aResult, __func__);
          return;
        }
        self->mHasConstructed = true;
        self->mConstructPromise.Resolve(self, __func__);
        if (!self->mInitPromise.IsEmpty()) {
          self->DoSendInit();
        }
      },
      [self = RefPtr{this}](const mozilla::ipc::ResponseRejectReason& aReason) {
        LOGE("[{}] Construct ipc failed", fmt::ptr(self.get()));
        RemoteMediaManagerChild::HandleRejectionError(
            self->GetManager(), self->mLocation, aReason,
            [self](const MediaResult& aError) {
              self->mConstructPromise.RejectIfExists(aError, __func__);
              self->mInitPromise.RejectIfExists(aError, __func__);
            });
      });
  return mConstructPromise.Ensure(__func__);
}

void RemoteMediaDataEncoder::DoSendInit() {
  MOZ_ASSERT(mHasConstructed);

  LOGD("[{}] Init send", fmt::ptr(this));
  mChild->SendInit()->Then(
      mThread, __func__,
      [self = RefPtr{this}](EncodeInitResultIPDL&& aResponse) {
        if (aResponse.type() == EncodeInitResultIPDL::TMediaResult) {
          LOGE("[{}] Init resolved code={}", fmt::ptr(self.get()),
               aResponse.get_MediaResult().Description());
          self->mInitPromise.Reject(aResponse.get_MediaResult(), __func__);
          return;
        }

        const auto& initResponse = aResponse.get_EncodeInitCompletionIPDL();

        LOGD("[{}] Init resolved hwAccel={} desc=\"{}\"", fmt::ptr(self.get()),
             initResponse.hardware(), initResponse.description().get());
        MutexAutoLock lock(self->mMutex);
        self->mDescription = initResponse.description();
        self->mDescription.AppendFmt(
            " ({})", RemoteMediaInToStr(self->GetManager()->Location()));

        self->mIsHardwareAccelerated = initResponse.hardware();
        self->mHardwareAcceleratedReason = initResponse.hardwareReason();
        self->mInitPromise.ResolveIfExists(true, __func__);
      },
      [self = RefPtr{this}](const mozilla::ipc::ResponseRejectReason& aReason) {
        LOGE("[{}] Init ipc failed", fmt::ptr(self.get()));
        RemoteMediaManagerChild::HandleRejectionError(
            self->GetManager(), self->mLocation, aReason,
            [self](const MediaResult& aError) {
              self->mInitPromise.RejectIfExists(aError, __func__);
            });
      });
}

RefPtr<MediaDataEncoder::InitPromise> RemoteMediaDataEncoder::Init() {
  return InvokeAsync(
      mThread, __func__,
      [self = RefPtr{this}]() -> RefPtr<MediaDataEncoder::InitPromise> {
        // If the owner called Init before the Construct response, then just
        // create promise and wait for that first. This can happen if the owner
        // created the encoder via RemoteEncoderModule's CreateAudioEncoder or
        // CreateVideoEncoder instead of AsyncCreateEncoder.
        //
        // mConstructPromise might not have been created yet either because we
        // may have delayed dispatching related to the process launching.
        // mHasConstructed will be set when the construct IPDL call returns.
        if (self->mHasConstructed) {
          self->DoSendInit();
        } else {
          LOGD("[{}] Init deferred, still constructing", fmt::ptr(self.get()));
        }
        return self->mInitPromise.Ensure(__func__);
      });
}

RefPtr<PRemoteEncoderChild::EncodePromise> RemoteMediaDataEncoder::DoSendEncode(
    const nsTArray<RefPtr<MediaData>>& aSamples, ShmemRecycleTicket* aTicket) {
  if (mChild->HasRemoteCrashed()) {
    LOGE("[{}] remote crashed", fmt::ptr(this));
    nsresult err = NS_ERROR_DOM_MEDIA_REMOTE_CRASHED_UTILITY_ERR;
    if (mLocation == RemoteMediaIn::GpuProcess ||
        mLocation == RemoteMediaIn::RddProcess) {
      err = NS_ERROR_DOM_MEDIA_REMOTE_CRASHED_RDD_OR_GPU_ERR;
    } else if (mLocation == RemoteMediaIn::UtilityProcess_MFMediaEngineCDM) {
      err = NS_ERROR_DOM_MEDIA_REMOTE_CRASHED_MF_CDM_ERR;
    }
    return PRemoteEncoderChild::EncodePromise::CreateAndResolve(
        MediaResult(err, "Remote process crashed"), __func__);
  }

  if (aSamples.IsEmpty()) {
    LOGE("[{}] no samples to encode", fmt::ptr(this));
    return PRemoteEncoderChild::EncodePromise::CreateAndResolve(
        MediaResult(NS_ERROR_INVALID_ARG), __func__);
  }

  MediaData::Type type = aSamples[0]->mType;

  if (type == MediaData::Type::AUDIO_DATA) {
    nsTArray<RefPtr<AudioData>> audioSamples;
    for (auto& sample : aSamples) {
      audioSamples.AppendElement(sample->As<AudioData>());
    }

    auto samples = MakeRefPtr<ArrayOfRemoteAudioData>();
    if (!samples->Fill(audioSamples, [&](size_t aSize) {
          return mChild->AllocateBuffer(aSize, aTicket);
        })) {
      LOGE("[{}] buffer audio failed", fmt::ptr(this));
      return PRemoteEncoderChild::EncodePromise::CreateAndResolve(
          MediaResult(NS_ERROR_OUT_OF_MEMORY), __func__);
    }
    LOGD("[{}] send {} audio samples", fmt::ptr(this), audioSamples.Length());
    return mChild->SendEncode(std::move(samples));
  }

  if (type == MediaData::Type::VIDEO_DATA) {
    nsTArray<RefPtr<VideoData>> videoSamples;
    for (auto& sample : aSamples) {
      videoSamples.AppendElement(sample->As<VideoData>());
    }

    auto samples = MakeRefPtr<ArrayOfRemoteVideoData>();
    for (const auto& videoSample : videoSamples) {
      if (layers::Image* videoImage = videoSample->mImage) {
        // We don't need to supply a working deallocator because the ticket is
        // responsible for that cleanup.
        layers::SurfaceDescriptor sd;
        nsresult rv = videoImage->BuildSurfaceDescriptorGPUVideoOrBuffer(
            sd, layers::Image::BuildSdbFlags::Default,
            Some(GetVideoBridgeSourceFromRemoteMediaIn(mLocation)),
            [&](uint32_t aBufferSize) {
              ShmemBuffer buffer = mChild->AllocateBuffer(aBufferSize, aTicket);
              if (buffer.Valid()) {
                return layers::MemoryOrShmem(std::move(buffer.Get()));
              }
              return layers::MemoryOrShmem();
            },
            [&](layers::MemoryOrShmem&&) {});

        if (NS_WARN_IF(NS_FAILED(rv))) {
          LOGE("[{}] buffer video failed, code={}", fmt::ptr(this),
               fmt::enums::format_as(rv));
          return PRemoteEncoderChild::EncodePromise::CreateAndResolve(
              MediaResult(rv), __func__);
        }

        samples->Append(RemoteVideoData(
            MediaDataIPDL(videoSample->mOffset, videoSample->mTime,
                          videoSample->mTimecode, videoSample->mDuration,
                          videoSample->mKeyframe),
            videoSample->mDisplay, RemoteImageHolder(std::move(sd)),
            videoSample->mFrameID));
      }
    }
    LOGD("[{}] send {} video samples", fmt::ptr(this), videoSamples.Length());
    return mChild->SendEncode(std::move(samples));
  }

  return PRemoteEncoderChild::EncodePromise::CreateAndResolve(
      MediaResult(NS_ERROR_INVALID_ARG), __func__);
}

RefPtr<MediaDataEncoder::EncodePromise> RemoteMediaDataEncoder::Encode(
    const MediaData* aSample) {
  return Encode(nsTArray<RefPtr<MediaData>>{const_cast<MediaData*>(aSample)});
}

RefPtr<MediaDataEncoder::EncodePromise> RemoteMediaDataEncoder::Encode(
    nsTArray<RefPtr<MediaData>>&& aSamples) {
  return InvokeAsync(
      mThread, __func__,
      [self = RefPtr{this}, samples = std::move(aSamples)]()
          -> RefPtr<MediaDataEncoder::EncodePromise> {
        auto promise =
            MakeRefPtr<MediaDataEncoder::EncodePromise::Private>(__func__);
        auto ticket = MakeRefPtr<ShmemRecycleTicket>();
        self->DoSendEncode(samples, ticket)
            ->Then(
                self->mThread, __func__,
                [self, promise, ticket](EncodeResultIPDL&& aResponse) {
                  self->mChild->ReleaseTicket(ticket);

                  if (aResponse.type() == EncodeResultIPDL::TMediaResult) {
                    LOGD("[{}] Encode resolved, code={}", fmt::ptr(self.get()),
                         aResponse.get_MediaResult().Description());
                    promise->Reject(aResponse.get_MediaResult(), __func__);
                    return;
                  }

                  const auto& encodeResponse =
                      aResponse.get_EncodeCompletionIPDL();

                  nsTArray<RefPtr<MediaRawData>> samples;
                  if (auto remoteSamples = encodeResponse.samples()) {
                    size_t count = remoteSamples->Count();
                    samples.SetCapacity(count);
                    for (size_t i = 0; i < count; ++i) {
                      AUTO_MARKER(marker, ".Encode.ElementAt");
                      if (RefPtr<MediaRawData> sample =
                              remoteSamples->ElementAt(i)) {
                        marker.End();
                        samples.AppendElement(std::move(sample));
                      } else {
                        LOGE(
                            "[{}] Encode resolved, failed to buffer "
                            "samples",
                            fmt::ptr(self.get()));
                        promise->Reject(MediaResult(NS_ERROR_OUT_OF_MEMORY),
                                        __func__);
                        return;
                      }
                    }
                  }

                  LOGV("[{}] Encode resolved, {} samples", fmt::ptr(self.get()),
                       samples.Length());
                  promise->Resolve(std::move(samples), __func__);
                  self->mChild->SendReleaseTicket(encodeResponse.ticketId());
                },
                [self, promise,
                 ticket](const mozilla::ipc::ResponseRejectReason& aReason) {
                  LOGE("[{}] Encode ipc failed", fmt::ptr(self.get()));
                  self->mChild->ReleaseTicket(ticket);
                  RemoteMediaManagerChild::HandleRejectionError(
                      self->GetManager(), self->mLocation, aReason,
                      [promise](const MediaResult& aError) {
                        promise->Reject(aError, __func__);
                      });
                });
        return promise;
      });
}

RefPtr<MediaDataEncoder::EncodePromise> RemoteMediaDataEncoder::Drain() {
  return InvokeAsync(
      mThread, __func__,
      [self = RefPtr{this}]() -> RefPtr<MediaDataEncoder::EncodePromise> {
        LOGD("[{}] Drain send", fmt::ptr(self.get()));
        self->mChild->SendDrain()->Then(
            self->mThread, __func__,
            [self](EncodeResultIPDL&& aResponse) {
              if (aResponse.type() == EncodeResultIPDL::TMediaResult) {
                LOGE("[{}] Drain resolved, code={}", fmt::ptr(self.get()),
                     aResponse.get_MediaResult().Description());
                self->mDrainPromise.Reject(aResponse.get_MediaResult(),
                                           __func__);
                return;
              }

              const auto& encodeResponse = aResponse.get_EncodeCompletionIPDL();

              nsTArray<RefPtr<MediaRawData>> samples;
              if (auto remoteSamples = encodeResponse.samples()) {
                size_t count = remoteSamples->Count();
                samples.SetCapacity(count);
                for (size_t i = 0; i < count; ++i) {
                  if (RefPtr<MediaRawData> sample =
                          remoteSamples->ElementAt(i)) {
                    samples.AppendElement(std::move(sample));
                  } else {
                    LOGE("[{}] Drain resolved, failed to buffer samples",
                         fmt::ptr(self.get()));
                    self->mDrainPromise.Reject(
                        MediaResult(NS_ERROR_OUT_OF_MEMORY), __func__);
                    return;
                  }
                }
              }

              LOGD("[{}] Drain resolved, {} samples", fmt::ptr(self.get()),
                   samples.Length());
              self->mDrainPromise.Resolve(std::move(samples), __func__);
              self->mChild->SendReleaseTicket(encodeResponse.ticketId());
            },
            [self](const mozilla::ipc::ResponseRejectReason& aReason) {
              LOGE("[{}] Drain ipc failed", fmt::ptr(self.get()));
              RemoteMediaManagerChild::HandleRejectionError(
                  self->GetManager(), self->mLocation, aReason,
                  [self](const MediaResult& aError) {
                    self->mDrainPromise.RejectIfExists(aError, __func__);
                  });
            });
        return self->mDrainPromise.Ensure(__func__);
      });
}

RefPtr<MediaDataEncoder::ReconfigurationPromise>
RemoteMediaDataEncoder::Reconfigure(
    const RefPtr<const EncoderConfigurationChangeList>& aConfigurationChanges) {
  return InvokeAsync(
      mThread, __func__,
      [self = RefPtr{this}, changes = RefPtr{aConfigurationChanges}]()
          -> RefPtr<MediaDataEncoder::ReconfigurationPromise> {
        LOGD("[{}] Reconfigure send", fmt::ptr(self.get()));
        self->mChild
            ->SendReconfigure(
                const_cast<EncoderConfigurationChangeList*>(changes.get()))
            ->Then(
                self->mThread, __func__,
                [self](const MediaResult& aResult) {
                  if (NS_SUCCEEDED(aResult)) {
                    LOGD("[{}] Reconfigure resolved", fmt::ptr(self.get()));
                    self->mReconfigurePromise.ResolveIfExists(true, __func__);
                  } else {
                    LOGD("[{}] Reconfigure resolved, code={}",
                         fmt::ptr(self.get()), aResult.Description());
                    self->mReconfigurePromise.RejectIfExists(aResult, __func__);
                  }
                },
                [self](const mozilla::ipc::ResponseRejectReason& aReason) {
                  LOGE("[{}] Reconfigure ipc failed", fmt::ptr(self.get()));
                  RemoteMediaManagerChild::HandleRejectionError(
                      self->GetManager(), self->mLocation, aReason,
                      [self](const MediaResult& aError) {
                        self->mReconfigurePromise.RejectIfExists(aError,
                                                                 __func__);
                      });
                });
        return self->mReconfigurePromise.Ensure(__func__);
      });
}

RefPtr<mozilla::ShutdownPromise> RemoteMediaDataEncoder::Shutdown() {
  {
    MutexAutoLock lock(mMutex);
    mNeedsShutdown = false;
  }

  return InvokeAsync(
      mThread, __func__,
      [self = RefPtr{this}]() -> RefPtr<mozilla::ShutdownPromise> {
        LOGD("[{}] Shutdown send", fmt::ptr(self.get()));
        self->mChild->SendShutdown()->Then(
            self->mThread, __func__,
            [self](PRemoteEncoderChild::ShutdownPromise::ResolveOrRejectValue&&
                       aValue) {
              LOGD("[{}] Shutdown resolved", fmt::ptr(self.get()));
              if (self->mChild->CanSend()) {
                self->mChild->Send__delete__(self->mChild);
              }
              self->mShutdownPromise.Resolve(aValue.IsResolve(), __func__);
            });
        return self->mShutdownPromise.Ensure(__func__);
      });
}

RefPtr<GenericPromise> RemoteMediaDataEncoder::SetBitrate(
    uint32_t aBitsPerSec) {
  return InvokeAsync(
      mThread, __func__,
      [self = RefPtr{this}, aBitsPerSec]() -> RefPtr<GenericPromise> {
        auto promise = MakeRefPtr<GenericPromise::Private>(__func__);
        self->mChild->SendSetBitrate(aBitsPerSec)
            ->Then(
                self->mThread, __func__,
                [promise](const nsresult& aRv) {
                  if (NS_SUCCEEDED(aRv)) {
                    promise->Resolve(true, __func__);
                  } else {
                    promise->Reject(aRv, __func__);
                  }
                },
                [self,
                 promise](const mozilla::ipc::ResponseRejectReason& aReason) {
                  LOGE("[{}] SetBitrate ipc failed", fmt::ptr(self.get()));
                  RemoteMediaManagerChild::HandleRejectionError(
                      self->GetManager(), self->mLocation, aReason,
                      [promise](const MediaResult& aError) {
                        promise->Reject(aError.Code(), __func__);
                      });
                });
        return promise.forget();
      });
}

RemoteMediaManagerChild* RemoteMediaDataEncoder::GetManager() {
  if (!mChild->CanSend()) {
    return nullptr;
  }
  return mozilla::ipc::ActorCast<RemoteMediaManagerChild>(mChild->Manager());
}

bool RemoteMediaDataEncoder::IsHardwareAccelerated(
    nsACString& aFailureReason) const {
  MutexAutoLock lock(mMutex);
  aFailureReason = mHardwareAcceleratedReason;
  return mIsHardwareAccelerated;
}

nsCString RemoteMediaDataEncoder::GetDescriptionName() const {
  MutexAutoLock lock(mMutex);
  return mDescription;
}

#undef AUTO_MARKER

}  // namespace mozilla
