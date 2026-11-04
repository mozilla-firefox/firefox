/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "EMEDecoderModule.h"

#include <stdint.h>

#include "Adts.h"
#include "BlankDecoderModule.h"
#include "ChromiumCDMVideoDecoder.h"
#include "DecryptThroughputLimit.h"
#include "GMPDecoderModule.h"
#include "GMPService.h"
#include "GMPVideoDecoder.h"
#include "MP4Decoder.h"
#include "MediaInfo.h"
#include "PDMFactory.h"
#include "mozilla/CDMProxy.h"
#include "mozilla/EMEUtils.h"
#include "mozilla/RemoteCDMProxy.h"
#include "mozilla/StaticPrefs_media.h"
#include "mozilla/UniquePtr.h"
#include "nsClassHashtable.h"
#include "nsServiceManagerUtils.h"

namespace mozilla {

using DecryptPromiseRequestHolder = MozPromiseRequestHolder<DecryptPromise>;

DDLoggedTypeDeclNameAndBase(EMEDecryptor, MediaDataDecoder);

class ADTSSampleConverter {
 public:
  explicit ADTSSampleConverter(const AudioInfo& aInfo)
      : mNumChannels(aInfo.mChannels)
        // Note: we set profile to 2 if we encounter an extended profile (which
        // set mProfile to 0 and then set mExtendedProfile) such as HE-AACv2
        // (profile 5). These can then pass through conversion to ADTS and back.
        // This is done as ADTS only has 2 bits for profile, and the transform
        // subtracts one from the value. We check if the profile supplied is > 4
        // for safety. 2 is used as a fallback value, though it seems the CDM
        // doesn't care what is set.
        ,
        mProfile(aInfo.mProfile < 1 || aInfo.mProfile > 4 ? 2 : aInfo.mProfile),
        mFrequencyIndex(ADTS::GetFrequencyIndex(aInfo.mRate).unwrapOr(255)) {
    EME_LOG(
        "ADTSSampleConvertor(): aInfo.mProfile={} aInfo.mExtendedProfile={}",
        aInfo.mProfile, aInfo.mExtendedProfile);
    if (aInfo.mProfile < 1 || aInfo.mProfile > 4) {
      EME_LOG(
          "ADTSSampleConvertor(): Profile not in [1, 4]! Samples will "
          "their profile set to 2!");
    }
  }
  bool Convert(MediaRawData* aSample) const {
    return ADTS::ConvertSample(mNumChannels, mFrequencyIndex, mProfile,
                               aSample);
  }
  bool Revert(MediaRawData* aSample) const {
    return ADTS::RevertSample(aSample);
  }

 private:
  const uint32_t mNumChannels;
  const uint8_t mProfile;
  const uint8_t mFrequencyIndex{};
};

class EMEDecryptor final : public MediaDataDecoder,
                           public DecoderDoctorLifeLogger<EMEDecryptor> {
 public:
  NS_INLINE_DECL_THREADSAFE_REFCOUNTING(EMEDecryptor, final);

  EMEDecryptor(MediaDataDecoder* aDecoder, CDMProxy* aProxy,
               TrackInfo::TrackType aType,
               const std::function<MediaEventProducer<TrackInfo::TrackType>*()>&
                   aOnWaitingForKey,
               UniquePtr<ADTSSampleConverter> aConverter = nullptr)
      : mDecoder(aDecoder),
        mProxy(aProxy),
        mSamplesWaitingForKey(
            new SamplesWaitingForKey(mProxy, aType, aOnWaitingForKey)),
        mADTSSampleConverter(std::move(aConverter)),
        mIsShutdown(false) {
    DDLINKCHILD("decoder", mDecoder.get());
  }

  RefPtr<InitPromise> Init() override {
    MOZ_ASSERT(!mIsShutdown);
    mThread = GetCurrentSerialEventTarget();
    uint32_t maxThroughputMs = StaticPrefs::media_eme_max_throughput_ms();
    EME_LOG("EME max-throughput-ms={}", maxThroughputMs);
    mThroughputLimiter.emplace(mThread, maxThroughputMs);

    return mDecoder->Init();
  }

  RefPtr<DecodePromise> Decode(MediaRawData* aSample) override {
    MOZ_ASSERT(mThread->IsOnCurrentThread());
    MOZ_RELEASE_ASSERT(mDecrypts.Count() == 0,
                       "Can only process one sample at a time");
    RefPtr<DecodePromise> p = mDecodePromise.Ensure(__func__);

    RefPtr<EMEDecryptor> self = this;
    mSamplesWaitingForKey->WaitIfKeyNotUsable(aSample)
        ->Then(
            mThread, __func__,
            [self](const RefPtr<MediaRawData>& aSample) {
              self->mKeyRequest.Complete();
              self->ThrottleDecode(aSample);
            },
            [self]() { self->mKeyRequest.Complete(); })
        ->Track(mKeyRequest);
    return p;
  }

  void ThrottleDecode(MediaRawData* aSample) {
    MOZ_ASSERT(mThread->IsOnCurrentThread());

    RefPtr<EMEDecryptor> self = this;
    mThroughputLimiter->Throttle(aSample)
        ->Then(
            mThread, __func__,
            [self](const RefPtr<MediaRawData>& aSample) {
              self->mThrottleRequest.Complete();
              self->AttemptDecode(aSample);
            },
            [self]() { self->mThrottleRequest.Complete(); })
        ->Track(mThrottleRequest);
  }

  void AttemptDecode(MediaRawData* aSample) {
    MOZ_ASSERT(mThread->IsOnCurrentThread());
    if (mIsShutdown) {
      NS_WARNING("EME encrypted sample arrived after shutdown");
      mDecodePromise.RejectIfExists(NS_ERROR_DOM_MEDIA_CANCELED, __func__);
      return;
    }

    if (mADTSSampleConverter && !mADTSSampleConverter->Convert(aSample)) {
      mDecodePromise.RejectIfExists(
          MediaResult(
              NS_ERROR_DOM_MEDIA_FATAL_ERR,
              RESULT_DETAIL("Failed to convert encrypted AAC sample to ADTS")),
          __func__);
      return;
    }

    const auto& decrypt = mDecrypts.InsertOrUpdate(
        aSample, std::make_unique<DecryptPromiseRequestHolder>());
    mProxy->Decrypt(aSample)
        ->Then(mThread, __func__, this, &EMEDecryptor::Decrypted,
               &EMEDecryptor::Decrypted)
        ->Track(*decrypt);
  }

  void Decrypted(const DecryptResult& aDecrypted) {
    MOZ_ASSERT(mThread->IsOnCurrentThread());
    MOZ_ASSERT(aDecrypted.mSample);

    UniquePtr<DecryptPromiseRequestHolder> holder;
    mDecrypts.Remove(aDecrypted.mSample, &holder);
    if (holder) {
      holder->Complete();
    } else {
      // Decryption is not in the list of decrypt operations waiting
      // for a result. It must have been flushed or drained. Ignore result.
      return;
    }

    if (mADTSSampleConverter &&
        !mADTSSampleConverter->Revert(aDecrypted.mSample)) {
      mDecodePromise.RejectIfExists(
          MediaResult(
              NS_ERROR_DOM_MEDIA_FATAL_ERR,
              RESULT_DETAIL("Failed to revert decrypted ADTS sample to AAC")),
          __func__);
      return;
    }

    if (mIsShutdown) {
      NS_WARNING("EME decrypted sample arrived after shutdown");
      return;
    }

    if (aDecrypted.mStatus == eme::NoKeyErr) {
      // Key became unusable after we sent the sample to CDM to decrypt.
      // Call Decode() again, so that the sample is enqueued for decryption
      // if the key becomes usable again.
      AttemptDecode(aDecrypted.mSample);
    } else if (aDecrypted.mStatus != eme::Ok) {
      mDecodePromise.RejectIfExists(
          MediaResult(NS_ERROR_DOM_MEDIA_FATAL_ERR,
                      RESULT_DETAIL("decrypted.mStatus=%u",
                                    uint32_t(aDecrypted.mStatus))),
          __func__);
    } else {
      MOZ_ASSERT(!mIsShutdown);
      // The sample is no longer encrypted, so clear its crypto metadata.
      UniquePtr<MediaRawDataWriter> writer(aDecrypted.mSample->CreateWriter());
      writer->mCrypto = CryptoSample();
      RefPtr<EMEDecryptor> self = this;
      mDecoder->Decode(aDecrypted.mSample)
          ->Then(mThread, __func__,
                 [self](DecodePromise::ResolveOrRejectValue&& aValue) {
                   self->mDecodeRequest.Complete();
                   self->mDecodePromise.ResolveOrReject(std::move(aValue),
                                                        __func__);
                 })
          ->Track(mDecodeRequest);
    }
  }

  RefPtr<FlushPromise> Flush() override {
    MOZ_ASSERT(mThread->IsOnCurrentThread());
    MOZ_ASSERT(!mIsShutdown);
    mKeyRequest.DisconnectIfExists();
    mThrottleRequest.DisconnectIfExists();
    mDecodeRequest.DisconnectIfExists();
    mDecodePromise.RejectIfExists(NS_ERROR_DOM_MEDIA_CANCELED, __func__);
    mThroughputLimiter->Flush();
    for (auto iter = mDecrypts.Iter(); !iter.Done(); iter.Next()) {
      auto* holder = iter.UserData();
      holder->DisconnectIfExists();
      iter.Remove();
    }
    RefPtr<SamplesWaitingForKey> k = mSamplesWaitingForKey;
    return mDecoder->Flush()->Then(mThread, __func__, [k]() {
      k->Flush();
      return FlushPromise::CreateAndResolve(true, __func__);
    });
  }

  RefPtr<DecodePromise> Drain() override {
    MOZ_ASSERT(mThread->IsOnCurrentThread());
    MOZ_ASSERT(!mIsShutdown);
    MOZ_ASSERT(mDecodePromise.IsEmpty() && !mDecodeRequest.Exists(),
               "Must wait for decoding to complete");
    for (auto iter = mDecrypts.Iter(); !iter.Done(); iter.Next()) {
      auto* holder = iter.UserData();
      holder->DisconnectIfExists();
      iter.Remove();
    }
    return mDecoder->Drain();
  }

  RefPtr<ShutdownPromise> Shutdown() override {
    // mThread may not be set if Init hasn't been called first.
    MOZ_ASSERT(!mThread || mThread->IsOnCurrentThread());
    MOZ_ASSERT(!mIsShutdown);
    mIsShutdown = true;
    mSamplesWaitingForKey->BreakCycles();
    mSamplesWaitingForKey = nullptr;
    mProxy = nullptr;
    return mDecoder->Shutdown();
  }

  nsCString GetProcessName() const override {
    return mDecoder->GetProcessName();
  }

  nsCString GetDescriptionName() const override {
    return mDecoder->GetDescriptionName();
  }

  nsCString GetCodecName() const override { return mDecoder->GetCodecName(); }

  ConversionRequired NeedsConversion() const override {
    return mDecoder->NeedsConversion();
  }

 private:
  ~EMEDecryptor() = default;

  const RefPtr<MediaDataDecoder> mDecoder;
  nsCOMPtr<nsISerialEventTarget> mThread;
  RefPtr<CDMProxy> mProxy;
  nsClassHashtable<nsRefPtrHashKey<MediaRawData>, DecryptPromiseRequestHolder>
      mDecrypts;
  RefPtr<SamplesWaitingForKey> mSamplesWaitingForKey;
  MozPromiseRequestHolder<SamplesWaitingForKey::WaitForKeyPromise> mKeyRequest;
  Maybe<DecryptThroughputLimit> mThroughputLimiter;
  MozPromiseRequestHolder<DecryptThroughputLimit::ThrottlePromise>
      mThrottleRequest;
  MozPromiseHolder<DecodePromise> mDecodePromise;
  MozPromiseHolder<DecodePromise> mDrainPromise;
  MozPromiseHolder<FlushPromise> mFlushPromise;
  MozPromiseRequestHolder<DecodePromise> mDecodeRequest;
  UniquePtr<ADTSSampleConverter> mADTSSampleConverter;
  bool mIsShutdown;
};

EMEMediaDataDecoderProxy::EMEMediaDataDecoderProxy(
    const CreateDecoderParams& aParams,
    already_AddRefed<MediaDataDecoder> aProxyDecoder,
    already_AddRefed<nsISerialEventTarget> aProxyThread, CDMProxy* aProxy)
    : MediaDataDecoderProxy(std::move(aProxyDecoder), std::move(aProxyThread)),
      mThread(GetCurrentSerialEventTarget()),
      mSamplesWaitingForKey(new SamplesWaitingForKey(
          aProxy, aParams.mType, aParams.mOnWaitingForKeyEvent)),
      mProxy(aProxy) {}

EMEMediaDataDecoderProxy::EMEMediaDataDecoderProxy(
    const CreateDecoderParams& aParams,
    already_AddRefed<MediaDataDecoder> aProxyDecoder, CDMProxy* aProxy)
    : MediaDataDecoderProxy(std::move(aProxyDecoder),
                            do_AddRef(GetCurrentSerialEventTarget())),
      mThread(GetCurrentSerialEventTarget()),
      mSamplesWaitingForKey(new SamplesWaitingForKey(
          aProxy, aParams.mType, aParams.mOnWaitingForKeyEvent)),
      mProxy(aProxy) {}

RefPtr<MediaDataDecoder::DecodePromise> EMEMediaDataDecoderProxy::Decode(
    MediaRawData* aSample) {
  RefPtr<EMEMediaDataDecoderProxy> self = this;
  RefPtr<MediaRawData> sample = aSample;
  return InvokeAsync(mThread, __func__, [self, this, sample]() {
    RefPtr<DecodePromise> p = mDecodePromise.Ensure(__func__);
    mSamplesWaitingForKey->WaitIfKeyNotUsable(sample)
        ->Then(
            mThread, __func__,
            [self, this](const RefPtr<MediaRawData>& aSample) {
              mKeyRequest.Complete();

              MediaDataDecoderProxy::Decode(aSample)
                  ->Then(mThread, __func__,
                         [self,
                          this](DecodePromise::ResolveOrRejectValue&& aValue) {
                           mDecodeRequest.Complete();
                           mDecodePromise.ResolveOrReject(std::move(aValue),
                                                          __func__);
                         })
                  ->Track(mDecodeRequest);
            },
            [self]() {
              self->mKeyRequest.Complete();
              MOZ_CRASH("Should never get here");
            })
        ->Track(mKeyRequest);

    return p;
  });
}

RefPtr<MediaDataDecoder::FlushPromise> EMEMediaDataDecoderProxy::Flush() {
  RefPtr<EMEMediaDataDecoderProxy> self = this;
  return InvokeAsync(mThread, __func__, [self, this]() {
    mKeyRequest.DisconnectIfExists();
    mDecodeRequest.DisconnectIfExists();
    mDecodePromise.RejectIfExists(NS_ERROR_DOM_MEDIA_CANCELED, __func__);
    return MediaDataDecoderProxy::Flush();
  });
}

RefPtr<ShutdownPromise> EMEMediaDataDecoderProxy::Shutdown() {
  RefPtr<EMEMediaDataDecoderProxy> self = this;
  return InvokeAsync(mThread, __func__, [self, this]() {
    mSamplesWaitingForKey->BreakCycles();
    mSamplesWaitingForKey = nullptr;
    mProxy = nullptr;
    return MediaDataDecoderProxy::Shutdown();
  });
}

EMEDecoderModule::EMEDecoderModule(CDMProxy* aProxy, PDMFactory* aPDM)
    : mProxy(aProxy), mPDM(aPDM) {}

EMEDecoderModule::~EMEDecoderModule() = default;

static already_AddRefed<MediaDataDecoderProxy> CreateDecoderWrapper(
    CDMProxy* aProxy, const CreateDecoderParams& aParams) {
  RefPtr<gmp::GeckoMediaPluginService> s(
      gmp::GeckoMediaPluginService::GetGeckoMediaPluginService());
  if (!s) {
    return nullptr;
  }
  nsCOMPtr<nsISerialEventTarget> thread(s->GetGMPThread());
  if (!thread) {
    return nullptr;
  }
  RefPtr<MediaDataDecoderProxy> decoder(
      new EMEMediaDataDecoderProxy(aParams,
                                   do_AddRef(new ChromiumCDMVideoDecoder(
                                       GMPVideoDecoderParams(aParams), aProxy)),
                                   thread.forget(), aProxy));
  return decoder.forget();
}

RefPtr<EMEDecoderModule::CreateDecoderPromise>
EMEDecoderModule::AsyncCreateDecoder(const CreateDecoderParams& aParams) {
  MOZ_ASSERT(aParams.mConfig.mCrypto.IsEncrypted() ||
             aParams.mEncryptedCustomIdent ==
                 CreateDecoderParams::EncryptedCustomIdent::True);
  MOZ_ASSERT(mPDM);

  // If the CDMProxy is a RemoteCDMChild actor, then we know that the CDM
  // functionality will be exercised by the decoder in the remote process.
  if (auto* cdm = static_cast<PRemoteCDMActor*>(mProxy->AsRemoteCDMProxy())) {
    return mPDM->CreateDecoder(CreateDecoderParams{aParams, cdm});
  }

  if (aParams.mConfig.IsVideo()) {
    if (StaticPrefs::media_eme_video_blank()) {
      EME_LOG(
          "EMEDecoderModule::AsyncCreateDecoder() creating a blank video "
          "decoder.");
      RefPtr<PlatformDecoderModule> m(BlankDecoderModule::Create());
      return m->AsyncCreateDecoder(aParams);
    }

    const bool gmpSupported =
        !SupportsMimeType(aParams.mConfig.mMimeType, nullptr).isEmpty();

    // Pre-create the GMP decoder wrapper before aParams is moved into the
    // resolve lambda below. CreateDecoderWrapper does not call Init(), so
    // creating an unused wrapper is safe.
    RefPtr<MediaDataDecoder> gmpFallback =
        gmpSupported ? CreateDecoderWrapper(mProxy, aParams) : nullptr;

    if (gmpSupported &&
        !StaticPrefs::media_eme_video_prefer_platform_decoder()) {
      // GMP decodes. Assume that means it can decrypt too.
      EME_LOG("EMEDecoderModule::AsyncCreateDecoder() using GMP decoder.");
      return EMEDecoderModule::CreateDecoderPromise::CreateAndResolve(
          gmpFallback, __func__);
    }

    RefPtr<EMEDecoderModule::CreateDecoderPromise> p =
        mPDM->CreateDecoder(aParams)->Then(
            GetCurrentSerialEventTarget(), __func__,
            [self = RefPtr{this},
             params = CreateDecoderParamsForAsync(aParams)](
                RefPtr<MediaDataDecoder>&& aDecoder) {
              EME_LOG(
                  "EMEDecoderModule::AsyncCreateDecoder() using platform "
                  "decoder.");
              RefPtr<MediaDataDecoder> emeDecoder(
                  new EMEDecryptor(aDecoder, self->mProxy, params.mType,
                                   params.mOnWaitingForKeyEvent));
              return EMEDecoderModule::CreateDecoderPromise::CreateAndResolve(
                  emeDecoder, __func__);
            },
            [gmpFallback](const MediaResult& aError) {
              if (gmpFallback) {
                EME_LOG(
                    "EMEDecoderModule::AsyncCreateDecoder() platform decoder "
                    "failed, falling back to GMP decoder.");
                return EMEDecoderModule::CreateDecoderPromise::CreateAndResolve(
                    gmpFallback, __func__);
              }
              return EMEDecoderModule::CreateDecoderPromise::CreateAndReject(
                  aError, __func__);
            });
    return p;
  }

  MOZ_ASSERT(aParams.mConfig.IsAudio());

  // We don't support using the GMP to decode audio.
  MOZ_ASSERT(SupportsMimeType(aParams.mConfig.mMimeType, nullptr).isEmpty());
  MOZ_ASSERT(mPDM);

  if (StaticPrefs::media_eme_audio_blank()) {
    EME_LOG(
        "EMEDecoderModule::AsyncCreateDecoder() creating a blank audio "
        "decoder.");
    RefPtr<PlatformDecoderModule> m(BlankDecoderModule::Create());
    return m->AsyncCreateDecoder(aParams);
  }

  UniquePtr<ADTSSampleConverter> converter = nullptr;
  if (MP4Decoder::IsAAC(aParams.mConfig.mMimeType)) {
    // The CDM expects encrypted AAC to be in ADTS format.
    // See bug 1433344.
    converter = std::make_unique<ADTSSampleConverter>(aParams.AudioConfig());
  }

  RefPtr<EMEDecoderModule::CreateDecoderPromise> p =
      mPDM->CreateDecoder(aParams)->Then(
          GetCurrentSerialEventTarget(), __func__,
          [self = RefPtr{this}, params = CreateDecoderParamsForAsync(aParams),
           converter = std::move(converter)](
              RefPtr<MediaDataDecoder>&& aDecoder) mutable {
            RefPtr<MediaDataDecoder> emeDecoder(new EMEDecryptor(
                aDecoder, self->mProxy, params.mType,
                params.mOnWaitingForKeyEvent, std::move(converter)));
            return EMEDecoderModule::CreateDecoderPromise::CreateAndResolve(
                emeDecoder, __func__);
          },
          [](const MediaResult& aError) {
            return EMEDecoderModule::CreateDecoderPromise::CreateAndReject(
                aError, __func__);
          });
  return p;
}

media::DecodeSupportSet EMEDecoderModule::SupportsMimeType(
    const nsACString& aMimeType, DecoderDoctorDiagnostics* aDiagnostics) const {
  Maybe<nsCString> keySystem;
  keySystem.emplace(NS_ConvertUTF16toUTF8(mProxy->KeySystem()));
  return GMPDecoderModule::SupportsMimeType(
      aMimeType, nsLiteralCString(CHROMIUM_CDM_API), keySystem);
}

}  // namespace mozilla
