/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef mozilla_URLQueryStringStripper_h
#define mozilla_URLQueryStringStripper_h

#include "nsIURLQueryStringStripper.h"
#include "nsIURLQueryStrippingListService.h"
#include "nsIObserver.h"
#include "mozilla/dom/StripOnShareRuleBinding.h"
#include "nsStringFwd.h"
#include "nsTHashSet.h"
#include "nsTHashMap.h"

class nsIURI;

namespace mozilla {

class URLQueryStringStripper final : public nsIObserver,
                                     public nsIURLQueryStringStripper,
                                     public nsIURLQueryStrippingListObserver {
  NS_DECL_ISUPPORTS
  NS_DECL_NSIOBSERVER
  NS_DECL_NSIURLQUERYSTRIPPINGLISTOBSERVER

  NS_DECL_NSIURLQUERYSTRINGSTRIPPER

 public:
  static already_AddRefed<URLQueryStringStripper> GetSingleton();

 private:
  URLQueryStringStripper();
  ~URLQueryStringStripper() = default;

  static void OnPrefChange(const char* aPref, void* aData);
  nsresult ManageObservers();

  [[nodiscard]] nsresult Init();
  [[nodiscard]] nsresult Shutdown();

  [[nodiscard]] nsresult StripQueryString(nsIURI* aURI, nsIURI** aOutput,
                                          uint32_t* aStripCount);

  bool CheckAllowList(nsIURI* aURI);

  void PopulateStripList(const nsACString& aList);
  void PopulateAllowList(const nsACString& aList);

  // Returns whether there is a rule matching the host that tells us to strip
  // this query parameter.
  bool ShouldStripParam(const nsACString& aHost,
                        const nsACString& aSchemelessSite,
                        const nsACString& aName);
  // Tries parse query parameter value as url and recurse into
  // `StripForCopyOrShareInternal` to strip query parameters for it. Returns how
  // many params were stripped, but at most 1 in dry mode. Modifies aValue to
  // the stripped value in non-dry mode.
  int TryStripValue(const nsACString& aHost, nsACString& aValue, bool aDry);

  // Recursive helper function that helps strip URIs of tracking parameters
  // and enables the stripping of tracking paramerters that are in a URI which
  // is nested in a query parameter. Dry mode won't return a strippedURI and
  // will stop after the first strippable parameter was found making aStripCount
  // either 0 or 1. Used to determine whether stripping is possible.
  nsresult StripForCopyOrShareInternal(nsIURI* aURI, nsIURI** aStrippedURI,
                                       int& aStripCount, bool aDry,
                                       bool aStripNestedURIs);

  nsTHashSet<nsCString> mList;
  nsTHashSet<nsCString> mAllowList;
  nsCOMPtr<nsIURLQueryStrippingListService> mListService;
  nsTHashMap<nsCString, dom::StripRule> mStripOnShareOriginMap;
  nsTHashMap<nsCString, dom::StripRule> mStripOnShareSchemelessSiteMap;
  Maybe<dom::StripRule> mStripOnShareGlobal;
  bool mIsInitialized;
  // Indicates whether or not we currently have registered an observer
  // for the QPS/strip-on-share list updates
  bool mObservingQPS = false;
  bool mObservingStripOnShare = false;
};

}  // namespace mozilla

#endif  // mozilla_URLQueryStringStripper_h
