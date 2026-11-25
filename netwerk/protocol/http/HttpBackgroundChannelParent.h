/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef mozilla_net_HttpBackgroundChannelParent_h
#define mozilla_net_HttpBackgroundChannelParent_h

#include "mozilla/Atomics.h"
#include "mozilla/Mutex.h"
#include "mozilla/dom/ipc/IdType.h"
#include "mozilla/net/PHttpBackgroundChannelParent.h"
#include "nsID.h"
#include "nsISupportsImpl.h"

class nsIInputStream;
class nsISerialEventTarget;

namespace mozilla {
namespace net {

class HttpChannelParent;

class HttpBackgroundChannelParent final : public PHttpBackgroundChannelParent {
 public:
  explicit HttpBackgroundChannelParent();

  NS_INLINE_DECL_THREADSAFE_REFCOUNTING(HttpBackgroundChannelParent, final)

  // Try to find associated HttpChannelParent with the same content process
  // and channel Id.
  nsresult Init(const dom::ContentParentId& aCpId, const uint64_t& aChannelId);

  // The content process that opened this background channel. Used by
  // BackgroundChannelRegistrar to ensure the channel is only linked to a
  // HttpChannelParent belonging to the same process.
  dom::ContentParentId GetContentParentId() const { return mContentParentId; }

  // Callbacks for BackgroundChannelRegistrar to notify
  // the associated HttpChannelParent is found.
  void LinkToChannel(HttpChannelParent* aChannelParent);

  // Callbacks for HttpChannelParent to close the background
  // IPC channel.
  void OnChannelClosed();

  // To send OnStartRequest message over background channel.
  bool OnStartRequest(nsHttpResponseHead&& aResponseHead,
                      const bool& aUseResponseHead,
                      const nsHttpHeaderArray& aRequestHeaders,
                      const HttpChannelOnStartRequestArgs& aArgs,
                      const nsCOMPtr<nsIInputStream>& aAltDataStream,
                      TimeStamp aOnStartRequestStart);

  // To send OnTransportAndData message over background channel.
  bool OnTransportAndData(const nsresult& aChannelStatus,
                          const nsresult& aTransportStatus,
                          const uint64_t& aOffset, const uint32_t& aCount,
                          const nsCString& aData,
                          TimeStamp aOnDataAvailableStart);

  // To send OnStopRequest message over background channel.
  bool OnStopRequest(const nsresult& aChannelStatus,
                     const ResourceTimingStructArgs& aTiming,
                     const nsHttpHeaderArray& aResponseTrailers,
                     const nsTArray<ConsoleReportCollected>& aConsoleReports,
                     TimeStamp aOnStopRequestStart);

  // To send OnAfterLastPart message over background channel.
  bool OnAfterLastPart(const nsresult aStatus);

  // To send OnProgress message over background channel.
  bool OnProgress(const int64_t aProgress, const int64_t aProgressMax);

  // To send OnStatus message over background channel.
  bool OnStatus(const nsresult aStatus);

  // To send FlushedForDiversion and DivertMessages messages
  // over background channel.
  bool OnDiversion();

  // To send NotifyClassificationFlags message over background channel.
  bool OnNotifyClassificationFlags(uint32_t aClassificationFlags,
                                   bool aIsThirdParty);

  // To send SetClassifierMatchedInfo message over background channel.
  bool OnSetClassifierMatchedInfo(const nsACString& aList,
                                  const nsACString& aProvider,
                                  const nsACString& aFullHash);

  // To send SetClassifierMatchedTrackingInfo message over background channel.
  bool OnSetClassifierMatchedTrackingInfo(const nsACString& aLists,
                                          const nsACString& aFullHashes);

  nsISerialEventTarget* GetBackgroundTarget();

  using ChildEndpointPromise =
      MozPromise<ipc::Endpoint<extensions::PStreamFilterChild>, bool, true>;
  [[nodiscard]] RefPtr<ChildEndpointPromise> AttachStreamFilter(
      Endpoint<extensions::PStreamFilterParent>&& aParentEndpoint,
      Endpoint<extensions::PStreamFilterChild>&& aChildEndpoint);

  [[nodiscard]] RefPtr<GenericPromise> DetachStreamFilters();

 protected:
  void ActorDestroy(ActorDestroyReason aWhy) override;

 private:
  virtual ~HttpBackgroundChannelParent();

  Atomic<bool> mIPCOpened;

  // Used to ensure atomicity of mBackgroundThread
  Mutex mBgThreadMutex;

  nsCOMPtr<nsISerialEventTarget> mBackgroundThread
      MOZ_GUARDED_BY(mBgThreadMutex);

  // The content process that opened this background channel, set once in Init
  // before the actor is registered.
  dom::ContentParentId mContentParentId;

  // associated HttpChannelParent for generating the channel events
  RefPtr<HttpChannelParent> mChannelParent;
};

}  // namespace net
}  // namespace mozilla

#endif  // mozilla_net_HttpBackgroundChannelParent_h
