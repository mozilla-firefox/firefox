/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef mozilla_net_UrlClassifierCommon_h
#define mozilla_net_UrlClassifierCommon_h

#include <vector>

#include "mozilla/net/ChannelClassifierLog.h"
#include "nsString.h"

class nsIChannel;
class nsIURI;

namespace mozilla {
namespace net {

class UrlClassifierCommon final {
 public:
  static const nsCString::size_type sMaxSpecLength;

  static bool AddonMayLoad(nsIChannel* aChannel, nsIURI* aURI);

  static bool ShouldEnableProtectionForChannel(nsIChannel* aChannel);

  // Use this function only when you are looking for a pairwise entitylist uri
  // with the format: http://toplevel.page/?resource=channel.uri.domain
  static nsresult CreatePairwiseEntityListURI(nsIChannel* aChannel,
                                              nsIURI** aURI);

  static nsresult SetTrackingInfo(nsIChannel* aChannel,
                                  const nsTArray<nsCString>& aLists,
                                  const nsTArray<nsCString>& aFullHashes);

  // Join the table names in 1 single string.
  static void TablesToString(const nsTArray<nsCString>& aList,
                             nsACString& aString);

  struct ClassificationData {
    nsCString mPrefix;
    uint32_t mFlag;
  };

  // Checks if the entries in aList are part of the ClassificationData vector
  // and it returns the corresponding flags. If none of them is found, the
  // default flag is returned.
  static uint32_t TablesToClassificationFlags(
      const nsTArray<nsCString>& aList,
      const std::vector<ClassificationData>& aData, uint32_t aDefaultFlag);

  static nsresult GetTopWindowURI(nsIChannel* aChannel, nsIURI** aURI);

  // Like GetTopWindowURI, but for channels without a top-level window, such as
  // worker requests, falls back to the URI of the SharedWorker or
  // ServiceWorker client, then to the URI of the loading principal. Sets
  // aURI to null when none is available.
  static void GetTopWindowURIWithFallback(nsIChannel* aChannel, nsIURI** aURI);

  static bool ShouldProcessWithProtectionFeature(nsIChannel* aChannel);

 private:
  static uint32_t TableToClassificationFlag(
      const nsACString& aTable, const std::vector<ClassificationData>& aData);
};

}  // namespace net
}  // namespace mozilla

#endif  // mozilla_net_UrlClassifierCommon_h
