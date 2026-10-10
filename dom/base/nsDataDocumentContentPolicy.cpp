/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/*
 * Content policy implementation that prevents all loads of images,
 * subframes, etc from documents loaded as data (eg documents loaded
 * via XMLHttpRequest).
 */

#include "nsDataDocumentContentPolicy.h"

#include "mozilla/ScopeExit.h"
#include "mozilla/dom/Document.h"
#include "nsContentPolicyUtils.h"
#include "nsContentUtils.h"
#include "nsINode.h"
#include "nsIProtocolHandler.h"
#include "nsIURI.h"
#include "nsNetUtil.h"
#include "nsScriptSecurityManager.h"

using namespace mozilla;

NS_IMPL_ISUPPORTS(nsDataDocumentContentPolicy, nsIContentPolicy)

// Helper method for ShouldLoad()
// Checks a URI for the given flags.  Returns true if the URI has the flags,
// and false if not (or if we weren't able to tell).
static bool HasFlags(nsIURI* aURI, uint32_t aURIFlags) {
  bool hasFlags;
  nsresult rv = NS_URIChainHasFlags(aURI, aURIFlags, &hasFlags);
  return NS_SUCCEEDED(rv) && hasFlags;
}

// If you change DataDocumentContentPolicy, make sure to check that
// CHECK_PRINCIPAL_AND_DATA in nsContentPolicyUtils is still valid.
// nsContentPolicyUtils may not pass all the parameters to ShouldLoad.
NS_IMETHODIMP
nsDataDocumentContentPolicy::ShouldLoad(nsIURI* aContentLocation,
                                        nsILoadInfo* aLoadInfo,
                                        int16_t* aDecision) {
  auto setBlockingReason = mozilla::MakeScopeExit([&]() {
    if (NS_CP_REJECTED(*aDecision)) {
      NS_SetRequestBlockingReason(
          aLoadInfo, nsILoadInfo::BLOCKING_REASON_CONTENT_POLICY_DATA_DOCUMENT);
    }
  });

  ExtContentPolicyType contentType = aLoadInfo->GetExternalContentPolicyType();
  nsCOMPtr<nsISupports> requestingContext = aLoadInfo->GetLoadingContext();

  *aDecision = nsIContentPolicy::ACCEPT;
  // Look for the document.  In most cases, requestingContext is a node.
  nsCOMPtr<mozilla::dom::Document> doc;
  nsCOMPtr<nsINode> node = do_QueryInterface(requestingContext);
  if (node) {
    doc = node->OwnerDoc();
  } else {
    if (nsCOMPtr<nsPIDOMWindowOuter> window =
            do_QueryInterface(requestingContext)) {
      doc = window->GetDoc();
    }
  }

  // DTDs are always OK to load
  if (!doc || contentType == ExtContentPolicy::TYPE_DTD) {
    return NS_OK;
  }

  if (doc->IsLoadedAsData()) {
    bool allowed = [&] {
      if (!doc->IsStaticDocument()) {
        // If not a print/print preview doc, then nothing else is allowed for
        // data documents.
        return false;
      }
      // Let static (print/print preview) documents to load fonts and
      // images.
      switch (contentType) {
        case ExtContentPolicy::TYPE_IMAGE:
        case ExtContentPolicy::TYPE_IMAGESET:
        case ExtContentPolicy::TYPE_FONT:
        case ExtContentPolicy::TYPE_UA_FONT:
        // This one is a bit sketchy, but nsObjectLoadingContent takes care of
        // only getting here if it is an image.
        case ExtContentPolicy::TYPE_OBJECT:
          return true;
        default:
          return false;
      }
    }();

    if (!allowed) {
      *aDecision = nsIContentPolicy::REJECT_TYPE;
      return NS_OK;
    }
  }

  if (doc->IsBeingUsedAsImage()) {
    // We only allow SVG images to load content from URIs that are local and
    // also satisfy one of the following conditions:
    //  - URI inherits security context, e.g. data URIs
    //   OR
    //  - URI loadable by subsumers, e.g. blob URIs
    // Any URI that doesn't meet these requirements will be rejected below.
    if (!(HasFlags(aContentLocation,
                   nsIProtocolHandler::URI_IS_LOCAL_RESOURCE) &&
          (HasFlags(aContentLocation,
                    nsIProtocolHandler::URI_INHERITS_SECURITY_CONTEXT) ||
           HasFlags(aContentLocation,
                    nsIProtocolHandler::URI_LOADABLE_BY_SUBSUMERS)))) {
      *aDecision = nsIContentPolicy::REJECT_TYPE;

      // Report error, if we can.
      if (node) {
        nsIPrincipal* requestingPrincipal = node->NodePrincipal();
        nsAutoCString sourceSpec;
        requestingPrincipal->GetAsciiSpec(sourceSpec);
        nsAutoCString targetSpec;
        aContentLocation->GetAsciiSpec(targetSpec);
        nsScriptSecurityManager::ReportError(
            "ExternalDataError", sourceSpec, targetSpec,
            requestingPrincipal->OriginAttributesRef().IsPrivateBrowsing());
      }
    } else if ((contentType == ExtContentPolicy::TYPE_IMAGE ||
                contentType == ExtContentPolicy::TYPE_IMAGESET) &&
               doc->GetDocumentURI()) {
      // Check for (& disallow) recursive image-loads
      bool isRecursiveLoad;
      nsresult rv = aContentLocation->EqualsExceptRef(doc->GetDocumentURI(),
                                                      &isRecursiveLoad);
      if (NS_FAILED(rv) || isRecursiveLoad) {
        NS_WARNING("Refusing to recursively load image");
        *aDecision = nsIContentPolicy::REJECT_TYPE;
      }
    }
    return NS_OK;
  }

  // Allow all loads for non-resource documents
  if (!doc->IsResourceDoc()) {
    return NS_OK;
  }

  // For resource documents, blacklist some load types
  if (contentType == ExtContentPolicy::TYPE_OBJECT ||
      contentType == ExtContentPolicy::TYPE_DOCUMENT ||
      contentType == ExtContentPolicy::TYPE_SUBDOCUMENT ||
      contentType == ExtContentPolicy::TYPE_SCRIPT ||
      contentType == ExtContentPolicy::TYPE_XSLT ||
      contentType == ExtContentPolicy::TYPE_FETCH ||
      contentType == ExtContentPolicy::TYPE_WEB_MANIFEST) {
    *aDecision = nsIContentPolicy::REJECT_TYPE;
  }

  // If you add more restrictions here, make sure to check that
  // CHECK_PRINCIPAL_AND_DATA in nsContentPolicyUtils is still valid.
  // nsContentPolicyUtils may not pass all the parameters to ShouldLoad

  return NS_OK;
}

NS_IMETHODIMP
nsDataDocumentContentPolicy::ShouldProcess(nsIURI* aContentLocation,
                                           nsILoadInfo* aLoadInfo,
                                           int16_t* aDecision) {
  return ShouldLoad(aContentLocation, aLoadInfo, aDecision);
}
