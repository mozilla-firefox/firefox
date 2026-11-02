/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "PolicyContainer.h"

#include "mozilla/dom/ConnectionAllowlists.h"
#include "mozilla/dom/IntegrityPolicy.h"
#include "mozilla/dom/IntegrityPolicyWAICT.h"
#include "mozilla/dom/nsCSPContext.h"
#include "mozilla/ipc/PBackgroundSharedTypes.h"
#include "nsCSPUtils.h"
#include "nsIClassInfoImpl.h"
#include "nsIObjectInputStream.h"
#include "nsIObjectOutputStream.h"

using namespace mozilla;
using namespace mozilla::dom;

PolicyContainer::PolicyContainer() = default;
PolicyContainer::~PolicyContainer() = default;

constexpr static uint32_t kPolicyContainerSerializationVersion = 2;

NS_IMETHODIMP
PolicyContainer::Read(nsIObjectInputStream* aStream) {
  uint32_t version = 0;
  MOZ_TRY(aStream->Read32(&version));

  if (version < 1 || version > kPolicyContainerSerializationVersion) {
    return NS_ERROR_FAILURE;
  }

  auto ReadOptionalCSPObject = [aStream](nsISupports** aOutCSP) -> nsresult {
    bool nonnull = false;
    MOZ_TRY(aStream->ReadBoolean(&nonnull));

    if (nonnull) {
      nsCID cid;
      MOZ_TRY(aStream->ReadID(&cid));
      MOZ_ASSERT(cid.Equals(nsCSPContext::GetCID()),
                 "Expected nsCSPContext CID");

      nsIID iid;
      MOZ_TRY(aStream->ReadID(&iid));
      MOZ_ASSERT(iid.Equals(NS_GET_IID(nsIContentSecurityPolicy)),
                 "Expected nsIContentSecurityPolicy IID");

      RefPtr<nsCSPContext> csp = new nsCSPContext();
      MOZ_TRY(csp->PolicyContainerRead(aStream));
      csp.forget(aOutCSP);
    }

    return NS_OK;
  };

  nsCOMPtr<nsISupports> csp;
  MOZ_TRY(ReadOptionalCSPObject(getter_AddRefs(csp)));
  mCSP = do_QueryInterface(csp);

  nsCOMPtr<nsISupports> integrityPolicy;
  MOZ_TRY(
      NS_ReadOptionalObject(aStream, true, getter_AddRefs(integrityPolicy)));
  mIntegrityPolicy = do_QueryInterface(integrityPolicy);

  if (version >= 2) {
    uint16_t ipAS = 0;
    MOZ_TRY(aStream->Read16(&ipAS));
    mIPAddressSpace = static_cast<nsILoadInfo::IPAddressSpace>(ipAS);
  }

  return NS_OK;
}

NS_IMETHODIMP
PolicyContainer::Write(nsIObjectOutputStream* aStream) {
  MOZ_TRY(aStream->Write32(kPolicyContainerSerializationVersion));

  MOZ_TRY(NS_WriteOptionalCompoundObject(
      aStream, mCSP, NS_GET_IID(nsIContentSecurityPolicy), true));

  MOZ_TRY(NS_WriteOptionalCompoundObject(aStream, mIntegrityPolicy,
                                         NS_GET_IID(nsIIntegrityPolicy), true));

  // TODO(Bug 2017654): (De)Serialize the WAICT state as part of the
  // Policy-Container

  // TODO(Bug 2073404): Support ConnectionAllowlists

  MOZ_TRY(aStream->Write16(static_cast<uint16_t>(mIPAddressSpace)));

  return NS_OK;
}

// static
Result<already_AddRefed<PolicyContainer>, nsresult>
PolicyContainer::CreateForNonDocument(nsIPrincipal* aPrincipal,
                                      nsIURI* aSelfURI,
                                      const Headers& aHeaders) {
  RefPtr<PolicyContainer> policyContainer = new PolicyContainer();

  if (!aHeaders.mContentSecurityPolicy.IsEmpty() ||
      !aHeaders.mContentSecurityPolicyReportOnly.IsEmpty()) {
    RefPtr<nsCSPContext> csp = new nsCSPContext();
    MOZ_TRY(csp->SetRequestContextWithPrincipal(aPrincipal, aSelfURI,
                                                /* aReferrer */ ""_ns,
                                                /* aInnerWindowId */ 0));
    if (!aHeaders.mContentSecurityPolicy.IsEmpty()) {
      MOZ_TRY(CSP_AppendCSPFromHeader(
          csp, NS_ConvertUTF8toUTF16(aHeaders.mContentSecurityPolicy),
          false /* aReportOnly */));
    }
    if (!aHeaders.mContentSecurityPolicyReportOnly.IsEmpty()) {
      MOZ_TRY(CSP_AppendCSPFromHeader(
          csp, NS_ConvertUTF8toUTF16(aHeaders.mContentSecurityPolicyReportOnly),
          true /* aReportOnly */));
    }
    policyContainer->SetCSP(csp);
  }

  if (!aHeaders.mConnectionAllowlist.IsEmpty() ||
      !aHeaders.mConnectionAllowlistReportOnly.IsEmpty()) {
    RefPtr<ConnectionAllowlists> allowlists;
    MOZ_TRY(ConnectionAllowlists::ParseHeaders(
        aHeaders.mConnectionAllowlist, aHeaders.mConnectionAllowlistReportOnly,
        getter_AddRefs(allowlists)));
    if (allowlists) {
      allowlists->SetResponseURI(aSelfURI);
    }
    policyContainer->SetConnectionAllowlists(allowlists);
  }

  return policyContainer.forget();
}

void PolicyContainer::ToArgs(const PolicyContainer* aPolicy,
                             mozilla::ipc::PolicyContainerArgs& aArgs) {
  if (!aPolicy) {
    return;
  }

  if (aPolicy->mCSP) {
    mozilla::ipc::CSPInfo cspInfo;
    mozilla::ipc::CSPToCSPInfo(aPolicy->mCSP, &cspInfo);
    aArgs.csp() = Some(cspInfo);
  }

  if (aPolicy->mIntegrityPolicy) {
    mozilla::ipc::IntegrityPolicyArgs integrityPolicyArgs;
    IntegrityPolicy::ToArgs(IntegrityPolicy::Cast(aPolicy->mIntegrityPolicy),
                            integrityPolicyArgs);
    aArgs.integrityPolicy() = Some(integrityPolicyArgs);
  }

  if (aPolicy->mConnectionAllowlists) {
    mozilla::ipc::ConnectionAllowlistsArgs connectionAllowlistsArgs;
    aPolicy->mConnectionAllowlists->ToArgs(connectionAllowlistsArgs);
    aArgs.connectionAllowlists() = Some(std::move(connectionAllowlistsArgs));
  }

  aArgs.ipAddressSpace() = aPolicy->mIPAddressSpace;
}

void PolicyContainer::FromArgs(const mozilla::ipc::PolicyContainerArgs& aArgs,
                               mozilla::dom::Document* aRequestingDocument,
                               PolicyContainer** aPolicy) {
  RefPtr<PolicyContainer> policy = new PolicyContainer();

  if (aArgs.csp().isSome()) {
    nsCOMPtr<nsIContentSecurityPolicy> csp =
        mozilla::ipc::CSPInfoToCSP(*aArgs.csp(), aRequestingDocument);
    policy->SetCSP(nsCSPContext::Cast(csp));
  }

  if (aArgs.integrityPolicy().isSome()) {
    RefPtr<dom::IntegrityPolicy> integrityPolicy = new dom::IntegrityPolicy();
    IntegrityPolicy::FromArgs(*aArgs.integrityPolicy(),
                              getter_AddRefs(integrityPolicy));
    policy->SetIntegrityPolicy(integrityPolicy);
  }

  if (aArgs.connectionAllowlists().isSome()) {
    RefPtr<ConnectionAllowlists> connectionAllowlists =
        ConnectionAllowlists::FromArgs(*aArgs.connectionAllowlists());
    policy->SetConnectionAllowlists(connectionAllowlists);
  }

  policy->SetIPAddressSpace(aArgs.ipAddressSpace());

  policy.forget(aPolicy);
}

void PolicyContainer::InitFromOther(PolicyContainer* aOther) {
  if (!aOther) {
    return;
  }

  if (aOther->mCSP) {
    RefPtr csp = MakeRefPtr<nsCSPContext>();
    csp->InitFromOther(nsCSPContext::Cast(aOther->mCSP));
    mCSP = csp;
  }

  if (aOther->mIntegrityPolicy) {
    RefPtr integrityPolicy = MakeRefPtr<IntegrityPolicy>();
    integrityPolicy->InitFromOther(
        IntegrityPolicy::Cast(aOther->mIntegrityPolicy));
    mIntegrityPolicy = integrityPolicy;
  }

  if (aOther->mConnectionAllowlists) {
    // Unlike CSP, connection allowlists are not mutable, so they don't need to
    // be cloned.
    mConnectionAllowlists = aOther->mConnectionAllowlists;
    mConnectionAllowlists->Freeze();  // Avoid accidental modifications.
  }

  mIPAddressSpace = aOther->mIPAddressSpace;
}

NS_IMETHODIMP PolicyContainer::InitFromCSP(nsIContentSecurityPolicy* aCSP) {
  SetCSP(aCSP);

  return NS_OK;
}

bool PolicyContainer::Equals(const PolicyContainer* aContainer,
                             const PolicyContainer* aOtherContainer) {
  // Do a quick pointer check first, also checks if both are null.
  if (aContainer == aOtherContainer) {
    return true;
  }

  // We checked if they were null above, so make sure one of them is not null.
  if (!aContainer || !aOtherContainer) {
    return false;
  }

  if (!nsCSPContext::Equals(aContainer->mCSP, aOtherContainer->mCSP)) {
    return false;
  }

  if (!IntegrityPolicy::Equals(
          IntegrityPolicy::Cast(aContainer->mIntegrityPolicy),
          IntegrityPolicy::Cast(aOtherContainer->mIntegrityPolicy))) {
    return false;
  }

  // TODO(Bug 2073404): Support ConnectionAllowlists

  if (aContainer->mIPAddressSpace != aOtherContainer->mIPAddressSpace) {
    return false;
  }

  // TODO(Bug 2017654): Handle equality for WAICT.

  return true;
}

// == CSP ==
void PolicyContainer::SetCSP(nsIContentSecurityPolicy* aPolicy) {
  mCSP = aPolicy;
}

nsIContentSecurityPolicy* PolicyContainer::GetCSP() const { return mCSP; }

nsIContentSecurityPolicy* PolicyContainer::GetCSP(
    const nsIPolicyContainer* aPolicyContainer) {
  if (!aPolicyContainer) {
    return nullptr;
  }
  return PolicyContainer::Cast(aPolicyContainer)->GetCSP();
}

// == Integrity Policy ==
void PolicyContainer::SetIntegrityPolicy(nsIIntegrityPolicy* aPolicy) {
  mIntegrityPolicy = aPolicy;
}

nsIIntegrityPolicy* PolicyContainer::GetIntegrityPolicy() const {
  return mIntegrityPolicy;
}

nsIIntegrityPolicy* PolicyContainer::GetIntegrityPolicy(
    const nsIPolicyContainer* aPolicyContainer) {
  if (!aPolicyContainer) {
    return nullptr;
  }
  return PolicyContainer::Cast(aPolicyContainer)->GetIntegrityPolicy();
}

// == WAICT Integrity Policy ==
void PolicyContainer::SetIntegrityPolicyWAICT(IntegrityPolicyWAICT* aPolicy) {
  mIntegrityPolicyWAICT = aPolicy;
}

IntegrityPolicyWAICT* PolicyContainer::GetIntegrityPolicyWAICT() const {
  return mIntegrityPolicyWAICT;
}

IntegrityPolicyWAICT* PolicyContainer::GetIntegrityPolicyWAICT(
    const nsIPolicyContainer* aPolicyContainer) {
  if (!aPolicyContainer) {
    return nullptr;
  }
  return PolicyContainer::Cast(aPolicyContainer)->GetIntegrityPolicyWAICT();
}

// == Connection Allowlists ==
void PolicyContainer::SetConnectionAllowlists(
    ConnectionAllowlists* aAllowlists) {
  mConnectionAllowlists = aAllowlists;
}

ConnectionAllowlists* PolicyContainer::GetConnectionAllowlists() const {
  return mConnectionAllowlists;
}

ConnectionAllowlists* PolicyContainer::GetConnectionAllowlists(
    const nsIPolicyContainer* aPolicyContainer) {
  if (!aPolicyContainer) {
    return nullptr;
  }
  return PolicyContainer::Cast(aPolicyContainer)->GetConnectionAllowlists();
}

// == IP Address Space ==
nsILoadInfo::IPAddressSpace PolicyContainer::GetIPAddressSpace() const {
  return mIPAddressSpace;
}

void PolicyContainer::SetIPAddressSpace(
    nsILoadInfo::IPAddressSpace aIPAddressSpace) {
  mIPAddressSpace = aIPAddressSpace;
}

NS_IMETHODIMP PolicyContainer::GetCsp(nsIContentSecurityPolicy** aCsp) {
  nsCOMPtr<nsIContentSecurityPolicy> csp = mCSP;
  csp.forget(aCsp);
  return NS_OK;
}

NS_IMPL_CLASSINFO(PolicyContainer, nullptr, 0, NS_IPOLICYCONTAINER_IID)
NS_IMPL_ISUPPORTS_CI(PolicyContainer, nsIPolicyContainer, nsISerializable)
