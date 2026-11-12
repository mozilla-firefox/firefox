/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */
#ifndef include_dom_media_ipc_RemoteMediaDataEncoderParent_h
#define include_dom_media_ipc_RemoteMediaDataEncoderParent_h

#include <map>

#include "PlatformEncoderModule.h"
#include "mozilla/PRemoteEncoderParent.h"
#include "mozilla/ShmemRecycleAllocator.h"

namespace mozilla {
namespace layers {
class BufferRecycleBin;
}

class RemoteMediaManagerParent;
using mozilla::ipc::IPCResult;

class RemoteMediaDataEncoderParent final
    : public ShmemRecycleAllocator<RemoteMediaDataEncoderParent>,
      public PRemoteEncoderParent {
  friend class PRemoteEncoderParent;

 public:
  // We refcount this class since the task queue can have runnables
  // that reference us.
  NS_INLINE_DECL_THREADSAFE_REFCOUNTING(RemoteMediaDataEncoderParent, final)

  explicit RemoteMediaDataEncoderParent(const EncoderConfig& aConfig);

  // PRemoteEncoderParent
  IPCResult RecvConstruct(ConstructResolver&& aResolver);
  IPCResult RecvInit(InitResolver&& aResolver);
  IPCResult RecvEncode(const EncodedInputIPDL& aData,
                       EncodeResolver&& aResolver);
  IPCResult RecvReconfigure(
      NotNull<EncoderConfigurationChangeList*> aConfigurationChanges,
      ReconfigureResolver&& aResolver);
  IPCResult RecvDrain(DrainResolver&& aResolver);
  IPCResult RecvReleaseTicket(const uint32_t& aTicketId);
  IPCResult RecvShutdown(ShutdownResolver&& aResolver);
  IPCResult RecvSetBitrate(const uint32_t& aBitrate,
                           SetBitrateResolver&& aResolver);

  void ActorDestroy(ActorDestroyReason aWhy) override;

 protected:
  virtual ~RemoteMediaDataEncoderParent();

  RefPtr<MediaDataEncoder> mEncoder;
  RefPtr<layers::BufferRecycleBin> mBufferRecycleBin;
  const EncoderConfig mConfig;

  std::map<uint32_t, RefPtr<ShmemRecycleTicket>> mTickets;
  uint32_t mTicketCounter = 0;
  bool mConstructAttempted : 1 = false;
  bool mInitAttempted : 1 = false;
  bool mInitialized : 1 = false;
  bool mShutdown : 1 = false;
};

}  // namespace mozilla

#endif  // include_dom_media_ipc_RemoteMediaDataEncoderParent_h
