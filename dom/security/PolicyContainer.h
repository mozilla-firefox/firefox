/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef PolicyContainer_h_
#define PolicyContainer_h_

#include "mozilla/AlreadyAddRefed.h"
#include "mozilla/Result.h"
#include "nsCOMPtr.h"
#include "nsIContentSecurityPolicy.h"
#include "nsIIntegrityPolicy.h"
#include "nsILoadInfo.h"
#include "nsIPolicyContainer.h"
#include "nsString.h"

namespace mozilla::ipc {
class PolicyContainerArgs;
}

namespace mozilla::dom {
class ConnectionAllowlists;
class Document;
class IntegrityPolicyWAICT;
}  // namespace mozilla::dom

#define NS_POLICYCONTAINER_CONTRACTID "@mozilla.org/policycontainer;1"

class nsIContentSecurityPolicy;
class nsIIntegrityPolicy;
class nsIPrincipal;
class nsIURI;

/**
  Implementation of
  https://html.spec.whatwg.org/multipage/browsers.html#policy-containers. Copied
  around the browser in the same way as CSP is copied, in fact, it replaces all
  the CSP inheritance code.
*/
class PolicyContainer : public nsIPolicyContainer {
 public:
  NS_DECL_ISUPPORTS
  NS_DECL_NSISERIALIZABLE
  NS_DECL_NSIPOLICYCONTAINER

  PolicyContainer();

  // Raw values of the response headers that populate a Policy-Container.
  // Empty values mean the header is absent.
  struct Headers {
    nsCString mContentSecurityPolicy;
    nsCString mContentSecurityPolicyReportOnly;
    nsCString mConnectionAllowlist;
    nsCString mConnectionAllowlistReportOnly;
  };

  // Creates a Policy-Container for non-documents by parsing the provided
  // security headers like Content-Security-Policy etc.
  static mozilla::Result<already_AddRefed<PolicyContainer>, nsresult>
  CreateForNonDocument(nsIPrincipal* aPrincipal, nsIURI* aSelfURI,
                       const Headers& aHeaders);

  static void ToArgs(const PolicyContainer* aPolicy,
                     mozilla::ipc::PolicyContainerArgs& aArgs);
  static void FromArgs(const mozilla::ipc::PolicyContainerArgs& aArgs,
                       mozilla::dom::Document* aRequestingDocument,
                       PolicyContainer** aPolicy);

  void InitFromOther(PolicyContainer* aOther);

  static const PolicyContainer* Cast(
      const nsIPolicyContainer* aPolicyContainer) {
    return static_cast<const PolicyContainer*>(aPolicyContainer);
  }

  static PolicyContainer* Cast(nsIPolicyContainer* aPolicyContainer) {
    return static_cast<PolicyContainer*>(aPolicyContainer);
  }

  static bool Equals(const PolicyContainer* aContainer,
                     const PolicyContainer* aOtherContainer);

  // == CSP ==
  nsIContentSecurityPolicy* GetCSP() const;
  void SetCSP(nsIContentSecurityPolicy* aPolicy);
  static nsIContentSecurityPolicy* GetCSP(
      const nsIPolicyContainer* aPolicyContainer);

  // == Integrity Policy ==
  nsIIntegrityPolicy* GetIntegrityPolicy() const;
  void SetIntegrityPolicy(nsIIntegrityPolicy* aPolicy);
  static nsIIntegrityPolicy* GetIntegrityPolicy(
      const nsIPolicyContainer* aPolicyContainer);

  // == WAICT Integrity Policy ===
  // TODO(Bug 2017658): Support WAICT in workers
  mozilla::dom::IntegrityPolicyWAICT* GetIntegrityPolicyWAICT() const;
  void SetIntegrityPolicyWAICT(mozilla::dom::IntegrityPolicyWAICT* aPolicy);
  static mozilla::dom::IntegrityPolicyWAICT* GetIntegrityPolicyWAICT(
      const nsIPolicyContainer* aPolicyContainer);

  // == Connection Allowlists ==
  // https://wicg.github.io/connection-allowlists/
  mozilla::dom::ConnectionAllowlists* GetConnectionAllowlists() const;
  void SetConnectionAllowlists(mozilla::dom::ConnectionAllowlists* aAllowlists);
  static mozilla::dom::ConnectionAllowlists* GetConnectionAllowlists(
      const nsIPolicyContainer* aPolicyContainer);

  // == IP Address Space ==
  // Stored per
  // https://wicg.github.io/local-network-access/#integration-with-html to allow
  // worker contexts (which have no browsing context) to perform Local Network
  // Access checks against their parent document's address space.
  nsILoadInfo::IPAddressSpace GetIPAddressSpace() const;
  void SetIPAddressSpace(nsILoadInfo::IPAddressSpace aIPAddressSpace);

 private:
  nsCOMPtr<nsIContentSecurityPolicy> mCSP;
  nsCOMPtr<nsIIntegrityPolicy> mIntegrityPolicy;
  RefPtr<mozilla::dom::IntegrityPolicyWAICT> mIntegrityPolicyWAICT;
  RefPtr<mozilla::dom::ConnectionAllowlists> mConnectionAllowlists;
  nsILoadInfo::IPAddressSpace mIPAddressSpace = nsILoadInfo::Unknown;

 protected:
  virtual ~PolicyContainer();
};

#endif /* PolicyContainer_h_ */
