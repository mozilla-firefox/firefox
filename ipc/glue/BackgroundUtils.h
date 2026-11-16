/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef mozilla_ipc_backgroundutils_h_
#define mozilla_ipc_backgroundutils_h_

#include "ipc/IPCMessageUtils.h"
#include "mozilla/OriginAttributes.h"
#include "mozilla/Result.h"
#include "nsCOMPtr.h"
#include "nscore.h"

class nsIContentSecurityPolicy;
class nsILoadInfo;
class nsINode;
class nsIPrincipal;
class nsIRedirectHistoryEntry;

namespace IPC {

namespace detail {
template <class ParamType>
struct OriginAttributesParamTraits {
  typedef ParamType paramType;

  static void Write(MessageWriter* aWriter, const paramType& aParam) {
    nsAutoCString suffix;
    aParam.CreateSuffix(suffix);
    WriteParam(aWriter, suffix);
  }

  static bool Read(MessageReader* aReader, paramType* aResult) {
    nsAutoCString suffix;
    return ReadParam(aReader, &suffix) && aResult->PopulateFromSuffix(suffix);
  }
};
}  // namespace detail

template <>
struct ParamTraits<mozilla::OriginAttributes>
    : public detail::OriginAttributesParamTraits<mozilla::OriginAttributes> {};

}  // namespace IPC

namespace mozilla {

namespace dom {
class Document;
}

namespace net {
class ChildLoadInfoForwarderArgs;
class LoadInfoArgs;
class LoadInfo;
class ParentLoadInfoForwarderArgs;
class RedirectHistoryEntryInfo;
}  // namespace net

namespace ipc {

class ContentSecurityPolicy;
class CSPInfo;
class PrincipalInfo;

/**
 * Convert a PrincipalInfo to an nsIPrincipal.
 *
 * MUST be called on the main thread.
 */
Result<nsCOMPtr<nsIPrincipal>, nsresult> PrincipalInfoToPrincipal(
    const PrincipalInfo& aPrincipalInfo);

/**
 * Convert an nsIPrincipal to a PrincipalInfo.
 *
 * MUST be called on the main thread only.
 */
nsresult PrincipalToPrincipalInfo(nsIPrincipal* aPrincipal,
                                  PrincipalInfo* aPrincipalInfo,
                                  bool aSkipBaseDomain = false);

/**
 * Compare storage keys for equivalence.
 *
 * Only use with storage keys retrieved from nsIGlobalObject::GetStorageKey!
 * Bug 1776271 tracks enhancing this into a proper type.
 */
bool StorageKeysEqual(const PrincipalInfo& aLeft, const PrincipalInfo& aRight);

/**
 * Convert a CSPInfo to an nsIContentSecurityPolicy.
 *
 * MUST be called on the main thread only.
 *
 * If possible, provide a requesting doc, so policy violation events can
 * be dispatched correctly. If aRequestingDoc is null, then the CSPInfo holds
 * the necessary fallback information, like a serialized requestPrincipal,
 * to generate a valid nsIContentSecurityPolicy.
 */
already_AddRefed<nsIContentSecurityPolicy> CSPInfoToCSP(
    const CSPInfo& aCSPInfo, mozilla::dom::Document* aRequestingDoc,
    nsresult* aOptionalResult = nullptr);

/**
 * Convert an nsIContentSecurityPolicy to a CSPInfo.
 *
 * MUST be called on the main thread only.
 */
nsresult CSPToCSPInfo(nsIContentSecurityPolicy* aCSP, CSPInfo* aCSPInfo);

/**
 * Return true if this PrincipalInfo is a content principal and it has
 * a privateBrowsing id in its OriginAttributes
 */
bool IsPrincipalInfoPrivate(const PrincipalInfo& aPrincipalInfo);

/**
 * Convert an RedirectHistoryEntryInfo to a nsIRedirectHistoryEntry.
 */

already_AddRefed<nsIRedirectHistoryEntry> RHEntryInfoToRHEntry(
    const mozilla::net::RedirectHistoryEntryInfo& aRHEntryInfo);

/**
 * Convert an nsIRedirectHistoryEntry to a RedirectHistoryEntryInfo.
 */

Result<mozilla::net::RedirectHistoryEntryInfo, nsresult> RHEntryToRHEntryInfo(
    nsIRedirectHistoryEntry* aRHEntry);

/**
 * Convert a LoadInfo to LoadInfoArgs struct.
 */
nsresult LoadInfoToLoadInfoArgs(nsILoadInfo* aLoadInfo,
                                mozilla::net::LoadInfoArgs* outLoadInfoArgs);

/**
 * Convert LoadInfoArgs to a LoadInfo.
 */
nsresult LoadInfoArgsToLoadInfo(
    const mozilla::net::LoadInfoArgs& aLoadInfoArgs,
    const mozilla::dom::RemoteType& aOriginRemoteType,
    nsILoadInfo** outLoadInfo);
nsresult LoadInfoArgsToLoadInfo(
    const mozilla::net::LoadInfoArgs& aLoadInfoArgs,
    const mozilla::dom::RemoteType& aOriginRemoteType,
    nsINode* aCspToInheritLoadingContext, nsILoadInfo** outLoadInfo);
nsresult LoadInfoArgsToLoadInfo(
    const net::LoadInfoArgs& aLoadInfoArgs,
    const mozilla::dom::RemoteType& aOriginRemoteType,
    mozilla::net::LoadInfo** outLoadInfo);
nsresult LoadInfoArgsToLoadInfo(
    const net::LoadInfoArgs& aLoadInfoArgs,
    const mozilla::dom::RemoteType& aOriginRemoteType,
    nsINode* aCspToInheritLoadingContext, mozilla::net::LoadInfo** outLoadInfo);

/**
 * Fills ParentLoadInfoForwarderArgs with properties we want to carry to child
 * processes.
 */
void LoadInfoToParentLoadInfoForwarder(
    nsILoadInfo* aLoadInfo,
    mozilla::net::ParentLoadInfoForwarderArgs* aForwarderArgsOut);

/**
 * Merges (replaces) properties of an existing LoadInfo on a child process
 * with properties carried down through ParentLoadInfoForwarderArgs.
 */
nsresult MergeParentLoadInfoForwarder(
    mozilla::net::ParentLoadInfoForwarderArgs const& aForwarderArgs,
    nsILoadInfo* aLoadInfo);

/**
 * Fills ChildLoadInfoForwarderArgs with properties we want to carry to the
 * parent process after the initial channel creation.
 */
void LoadInfoToChildLoadInfoForwarder(
    nsILoadInfo* aLoadInfo,
    mozilla::net::ChildLoadInfoForwarderArgs* aForwarderArgsOut);

/**
 * Merges (replaces) properties of an existing LoadInfo on the parent process
 * with properties contained in a ChildLoadInfoForwarderArgs.
 */
nsresult MergeChildLoadInfoForwarder(
    const mozilla::net::ChildLoadInfoForwarderArgs& aForwardArgs,
    nsILoadInfo* aLoadInfo);

}  // namespace ipc
}  // namespace mozilla

#endif  // mozilla_ipc_backgroundutils_h_
