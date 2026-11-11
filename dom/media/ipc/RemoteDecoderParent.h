/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */
#ifndef include_dom_media_ipc_RemoteDecoderParent_h
#define include_dom_media_ipc_RemoteDecoderParent_h

#include "mozilla/Maybe.h"
#include "mozilla/MozPromise.h"
#include "mozilla/PRemoteDecoderParent.h"
#include "mozilla/ShmemRecycleAllocator.h"

namespace mozilla {

class RemoteCDMParent;
class RemoteMediaManagerParent;
using mozilla::ipc::IPCResult;

class RemoteDecoderParent : public ShmemRecycleAllocator<RemoteDecoderParent>,
                            public PRemoteDecoderParent {
  friend class PRemoteDecoderParent;

 public:
  // We refcount this class since the task queue can have runnables
  // that reference us.
  NS_INLINE_DECL_THREADSAFE_REFCOUNTING(RemoteDecoderParent, final)

  RemoteDecoderParent(RemoteMediaManagerParent* aParent,
                      const CreateDecoderParams::OptionSet& aOptions,
                      nsISerialEventTarget* aManagerThread,
                      TaskQueue* aDecodeTaskQueue,
                      const Maybe<uint64_t>& aMediaEngineId,
                      Maybe<TrackingId> aTrackingId, RemoteCDMParent* aCDM);

  // PRemoteDecoderParent
  virtual IPCResult RecvConstruct(ConstructResolver&& aResolver) = 0;
  IPCResult RecvInit(InitResolver&& aResolver);
  IPCResult RecvDecode(ArrayOfRemoteMediaRawData* aData,
                       DecodeResolver&& aResolver);
  IPCResult RecvFlush(FlushResolver&& aResolver);
  IPCResult RecvDrain(DrainResolver&& aResolver);
  IPCResult RecvShutdown(ShutdownResolver&& aResolver);
  IPCResult RecvSetSeekThreshold(const media::TimeUnit& aTime);

  void ActorDestroy(ActorDestroyReason aWhy) override;

 protected:
  virtual ~RemoteDecoderParent();

  bool OnManagerThread();

  bool IsMediaEngineIdAllowed() const;

  // Rejects any in-flight IPC resolvers with NS_ERROR_DOM_MEDIA_CANCELED and
  // disconnects the matching MozPromiseRequestHolders so that their ThenValues
  // cannot trip AssertIsDead if the underlying decoder's promise chain is torn
  // down before the callback fires. Must be called on the manager thread.
  void AbortPendingRequests();

  virtual MediaResult ProcessDecodedData(MediaDataDecoder::DecodedData&& aData,
                                         DecodedOutputIPDL& aDecodedData) = 0;

  // Gives subclasses a chance to replace a decode or drain error before it is
  // returned to the child, e.g. to make it recoverable.
  virtual MediaResult AdjustDecodeError(const MediaResult& aError) {
    return aError;
  }

  const RefPtr<RemoteMediaManagerParent> mParent;
  const CreateDecoderParams::OptionSet mOptions;
  const RefPtr<TaskQueue> mDecodeTaskQueue;
  RefPtr<MediaDataDecoder> mDecoder;
  const RefPtr<RemoteCDMParent> mCDM;
  const Maybe<TrackingId> mTrackingId;

  // Only be used on Windows when the media engine playback is enabled.
  const Maybe<uint64_t> mMediaEngineId;

  // Construct is accepted only in Unconstructed, Init only in Constructed, and
  // Decode, Flush, Drain and SetSeekThreshold only in Initialized. Shutdown is
  // accepted in any state. A failed Construct or Init keeps the state, so only
  // Shutdown is accepted after it.
  enum class State : uint8_t {
    Unconstructed,
    Constructing,
    Constructed,
    Initializing,
    Initialized,
    ShutDown,
  };
  State mState = State::Unconstructed;

  // Pending IPC resolvers and matching request holders for the in-flight
  // decoder promise. Accessed only on the manager thread.
  Maybe<InitResolver> mPendingInitResolver;
  Maybe<DecodeResolver> mPendingDecodeResolver;
  Maybe<FlushResolver> mPendingFlushResolver;
  Maybe<DrainResolver> mPendingDrainResolver;

  MozPromiseRequestHolder<MediaDataDecoder::InitPromise> mInitRequest;
  MozPromiseRequestHolder<MediaDataDecoder::DecodePromise> mDecodeRequest;
  MozPromiseRequestHolder<MediaDataDecoder::FlushPromise> mFlushRequest;
  MozPromiseRequestHolder<MediaDataDecoder::DecodePromise> mDrainRequest;

 private:
  void DecodeNextSample(const RefPtr<ArrayOfRemoteMediaRawData>& aData,
                        size_t aIndex, MediaDataDecoder::DecodedData&& aOutput);
  const RefPtr<nsISerialEventTarget> mManagerThread;
};

}  // namespace mozilla

#endif  // include_dom_media_ipc_RemoteDecoderParent_h
