/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/*
 * Implementation of the "@mozilla.org/layout/content-policy;1" contract.
 */

#include "nsContentPolicy.h"

#include "mozilla/Components.h"
#include "mozilla/Logging.h"
#include "mozilla/Try.h"
#include "mozilla/dom/ConnectionAllowlistsService.h"
#include "mozilla/dom/PolicyContainer.h"
#include "mozilla/dom/nsCSPService.h"
#include "mozilla/dom/nsMixedContentBlocker.h"
#include "mozilla/net/DocumentChannel.h"
#include "mozilla/net/DocumentLoadListener.h"
#include "nsAsyncRedirectVerifyHelper.h"
#include "nsCOMArray.h"
#include "nsContentPolicyUtils.h"
#include "nsContentUtils.h"
#include "nsIAsyncVerifyRedirectCallback.h"
#include "nsIBrowserChild.h"
#include "nsIChannel.h"
#include "nsIContent.h"
#include "nsIContentSecurityPolicy.h"
#include "nsIImageLoadingContent.h"
#include "nsIParentChannel.h"
#include "nsISupports.h"
#include "nsIURI.h"
#include "nsNetUtil.h"
#include "nsQueryObject.h"
#include "nsXPCOM.h"

class nsIDOMWindow;

using mozilla::LogLevel;

NS_IMPL_ISUPPORTS(nsContentPolicy, nsIContentPolicy, nsIChannelEventSink)

static mozilla::LazyLogModule gConPolLog("nsContentPolicy");

nsContentPolicy::nsContentPolicy() : mPolicies(NS_CONTENTPOLICY_CATEGORY) {}

nsresult nsContentPolicy::Init() {
  nsresult rv;
  mCSPService = mozilla::components::CSPService::Service(&rv);
  MOZ_TRY(rv);
  mMixedContentBlocker = mozilla::components::MixedContentBlocker::Service(&rv);
  return rv;
}

nsContentPolicy::~nsContentPolicy() = default;

#ifdef DEBUG
#  define WARN_IF_URI_UNINITIALIZED(uri, name)            \
    PR_BEGIN_MACRO                                        \
    if ((uri)) {                                          \
      nsAutoCString spec;                                 \
      (uri)->GetAsciiSpec(spec);                          \
      if (spec.IsEmpty()) {                               \
        NS_WARNING(name " is uninitialized, fix caller"); \
      }                                                   \
    }                                                     \
    PR_END_MACRO

#else  // ! defined(DEBUG)

#  define WARN_IF_URI_UNINITIALIZED(uri, name)

#endif  // defined(DEBUG)

inline nsresult nsContentPolicy::CheckPolicy(CPMethod policyMethod,
                                             nsIURI* contentLocation,
                                             nsILoadInfo* loadInfo,
                                             int16_t* decision) {
  nsCOMPtr<nsISupports> requestingContext = loadInfo->GetLoadingContext();
  // sanity-check passed-through parameters
  MOZ_ASSERT(decision, "Null out pointer");
  WARN_IF_URI_UNINITIALIZED(contentLocation, "Request URI");

#ifdef DEBUG
  {
    nsCOMPtr<nsINode> node(do_QueryInterface(requestingContext));
    nsCOMPtr<nsIDOMWindow> window(do_QueryInterface(requestingContext));
    nsCOMPtr<nsIBrowserChild> browserChild(
        do_QueryInterface(requestingContext));
    NS_ASSERTION(!requestingContext || node || window || browserChild,
                 "Context should be a DOM node, DOM window or a browserChild!");
  }
#endif

  nsCOMPtr<mozilla::dom::Document> doc;
  if (nsCOMPtr<nsIContent> node = do_QueryInterface(requestingContext)) {
    doc = node->OwnerDoc();
  } else {
    doc = do_QueryInterface(requestingContext);
  }

  if (doc) {
    if (nsCOMPtr<nsIContentSecurityPolicy> csp =
            PolicyContainer::GetCSP(doc->GetPolicyContainer())) {
      csp->EnsureEventTarget(mozilla::GetMainThreadSerialEventTarget());
    }
  }

  /*
   * mPolicies is enumerated in unspecified (hashtable) order and the first
   * rejection stops the enumeration, which would suppress the reporting side
   * effects of CSP and the mixed content blocker. Consult those two up front,
   * in a defined order.
   */
  auto rejects = [&](nsIContentPolicy* policy) {
    // Check the appropriate policy.
    nsresult rv = (policy->*policyMethod)(contentLocation, loadInfo, decision);
    return NS_SUCCEEDED(rv) && NS_CP_REJECTED(*decision);
  };

  if (rejects(mCSPService) || rejects(mMixedContentBlocker)) {
    return NS_OK;
  }

  const nsCOMArray<nsIContentPolicy>& entries = mPolicies.GetCachedEntries();
  for (int32_t i = 0; i < entries.Count(); ++i) {
    if (rejects(entries[i])) {
      return NS_OK;
    }
  }

  // Everyone returned failure, or no policies: sanitize result
  *decision = nsIContentPolicy::ACCEPT;
  return NS_OK;
}

// uses the parameters from ShouldXYZ to produce and log a message
// logType must be a literal string constant
#define LOG_CHECK(logType)                                                     \
  PR_BEGIN_MACRO                                                               \
  /* skip all this nonsense if the call failed or logging is disabled */       \
  if (NS_SUCCEEDED(rv) && MOZ_LOG_TEST(gConPolLog, LogLevel::Debug)) {         \
    const char* resultName;                                                    \
    if (decision) {                                                            \
      resultName = NS_CP_ResponseName(*decision);                              \
    } else {                                                                   \
      resultName = "(null ptr)";                                               \
    }                                                                          \
    MOZ_LOG(                                                                   \
        gConPolLog, LogLevel::Debug,                                           \
        ("Content Policy: " logType ": <%s> result=%s",                        \
         contentLocation ? contentLocation->GetSpecOrDefault().get() : "None", \
         resultName));                                                         \
  }                                                                            \
  PR_END_MACRO

NS_IMETHODIMP
nsContentPolicy::ShouldLoad(nsIURI* contentLocation, nsILoadInfo* loadInfo,
                            int16_t* decision) {
  // ShouldProcess does not need a content location, but we do
  MOZ_ASSERT(contentLocation, "Must provide request location");
  nsresult rv = CheckPolicy(&nsIContentPolicy::ShouldLoad, contentLocation,
                            loadInfo, decision);
  LOG_CHECK("ShouldLoad");

  return rv;
}

NS_IMETHODIMP
nsContentPolicy::ShouldProcess(nsIURI* contentLocation, nsILoadInfo* loadInfo,
                               int16_t* decision) {
  nsresult rv = CheckPolicy(&nsIContentPolicy::ShouldProcess, contentLocation,
                            loadInfo, decision);
  LOG_CHECK("ShouldProcess");

  return rv;
}

/* nsIChannelEventSink implementation */
NS_IMETHODIMP
nsContentPolicy::AsyncOnChannelRedirect(
    nsIChannel* aOldChannel, nsIChannel* aNewChannel, uint32_t aFlags,
    nsIAsyncVerifyRedirectCallback* aCallback) {
  mozilla::net::nsAsyncRedirectAutoCallback autoCallback(aCallback);

  if (XRE_IsE10sParentProcess()) {
    nsCOMPtr<nsIParentChannel> parentChannel;
    NS_QueryNotificationCallbacks(aOldChannel, parentChannel);
    RefPtr<mozilla::net::DocumentLoadListener> docListener =
        do_QueryObject(parentChannel);
    // Since this is an IPC'd channel we do not have access to the request
    // context. In turn, we do not have an event target for policy violations.
    // Enforce the CSP check in the content process where we have that info.
    // We allow redirect checks to run for document loads via
    // DocumentLoadListener, since these are fully supported and we don't
    // expose the redirects to the content process. We can't do this for all
    // request types yet because we don't serialize nsICSPEventListener.
    if (parentChannel && !docListener) {
      return NS_OK;
    }
  }

  // Don't do these checks if we're switching from DocumentChannel
  // to a real channel. In that case, we should already have done
  // the checks in the parent process. AsyncOnChannelRedirect
  // isn't called in the content process if we switch process,
  // so checking here would just hide bugs in the process switch
  // cases.
  if (RefPtr<mozilla::net::DocumentChannel> docChannel =
          do_QueryObject(aOldChannel)) {
    return NS_OK;
  }

  nsCOMPtr<nsIURI> newUri;
  MOZ_TRY(aNewChannel->GetURI(getter_AddRefs(newUri)));

  nsCOMPtr<nsILoadInfo> loadInfo = aOldChannel->LoadInfo();

  /* Since redirecting channels don't call into nsIContentPolicy, we call our
   * Content Policy implementation directly when redirects occur using the
   * information set in the LoadInfo when channels are created.
   *
   * We check if the CSP permits this host for this type of load, and if the
   * Connection Allowlists permit the redirect, if not, we cancel the load now.
   */
  nsCOMPtr<nsIURI> originalUri;
  nsresult rv = aOldChannel->GetOriginalURI(getter_AddRefs(originalUri));
  if (NS_FAILED(rv)) {
    autoCallback.DontCallback();
    aOldChannel->Cancel(NS_ERROR_DOM_BAD_URI);
    return rv;
  }

  mozilla::Maybe<nsresult> cancelCode;
  rv = CSPService::ConsultCSPForRedirect(originalUri, newUri, loadInfo, aFlags,
                                         cancelCode);
  if (NS_SUCCEEDED(rv)) {
    rv = mozilla::dom::ConnectionAllowlistsService::ConsultForRedirect(
        originalUri, newUri, loadInfo, aFlags, cancelCode);
  }
  if (cancelCode) {
    aOldChannel->Cancel(*cancelCode);
  }
  if (NS_FAILED(rv)) {
    autoCallback.DontCallback();
  }

  return rv;
}
