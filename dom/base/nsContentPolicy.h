/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef _nsContentPolicy_h_
#define _nsContentPolicy_h_

#include "nsCOMPtr.h"
#include "nsCategoryCache.h"
#include "nsIChannelEventSink.h"
#include "nsIContentPolicy.h"

/*
 * Implementation of the "@mozilla.org/layout/content-policy;1" contract.
 *
 * Redirects don't go through nsIContentPolicy, so this also checks them, as an
 * nsIChannelEventSink, against the policies that apply to redirects.
 */

class nsContentPolicy : public nsIContentPolicy, public nsIChannelEventSink {
 public:
  NS_DECL_ISUPPORTS
  NS_DECL_NSICONTENTPOLICY
  NS_DECL_NSICHANNELEVENTSINK

  nsContentPolicy();

  nsresult Init();

 protected:
  virtual ~nsContentPolicy();

 private:
  // Array of policies
  nsCategoryCache<nsIContentPolicy> mPolicies;

  // Policies that are consulted before everything in mPolicies, in the order
  // they are declared here.
  nsCOMPtr<nsIContentPolicy> mCSPService;
  nsCOMPtr<nsIContentPolicy> mMixedContentBlocker;

  // Helper type for CheckPolicy
  using CPMethod = decltype(&nsIContentPolicy::ShouldProcess);

  // Helper method that applies policyMethod across all policies in mPolicies
  // with the given parameters
  nsresult CheckPolicy(CPMethod policyMethod, nsIURI* aURI,
                       nsILoadInfo* aLoadInfo, int16_t* decision);
};

#endif /* _nsContentPolicy_h_ */
