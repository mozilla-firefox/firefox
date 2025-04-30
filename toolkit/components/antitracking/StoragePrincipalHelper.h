/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef mozilla_StoragePrincipalHelper_h
#define mozilla_StoragePrincipalHelper_h

#include <cstdint>
#include "ErrorList.h"
#include "nsStringFwd.h"

/**
 * StoragePrincipal
 * ~~~~~~~~~~~~~~~~

 * StoragePrincipal is the nsIPrincipal to be used to open the cookie jar of a
 * resource's origin. Normally, the StoragePrincipal corresponds to the
 * resource's origin, but, in some scenarios, it can be different: it has the
 * `partitionKey` attribute set to the top-level “site” (i.e., scheme plus
 * eTLD+1 of the origin of the top-level document).
 *
 * Each storage component should always use the StoragePrincipal instead of the
 * 'real' one in order to implement the partitioning correctly. See the list of
 * the components here: https://privacycg.github.io/storage-partitioning/
 *
 * On the web, each resource has its own origin (see
 * https://html.spec.whatwg.org/multipage/origin.html#concept-origin) and each
 * origin has its own cookie jar, containing cookies, storage data, cache and so
 * on.
 *
 * In gecko-world, the origin and its attributes are stored and managed by the
 * nsIPrincipal interface. Both a resource's Principal and a resource's
 * StoragePrincipal are nsIPrincipal interfaces and, normally, they are the same
 * object.
 *
 * Naming and usage
 * ~~~~~~~~~~~~~~~~
 *
 * StoragePrincipal exposes four types of principals for a resource:
 * - Regular Principal:
 *     A “first-party” principal derived from the origin of the resource. This
 *     does not have the `partitionKey` origin attribute set.
 * - Partitioned Principal:
 *     The regular principal plus the partitionKey origin attribute set to
 *     the site of the top-level document (i.e., scheme plus eTLD+1).
 * - Storage Access Principal:
 *     A dynamic principal that changes when a resource receives storage access.
 *     By default, when storage access is denied, this is equal to the
 *     Partitioned Principal. When storage access is granted, this is equal to
 *     the Regular Principal.
 * - Foreign Partitioned Principal
 *     A principal that would be decided according to the fact that if the
 *     resource is a third party or not. If the resource is in a third-party
 *     context, this will be the partitioned principal. Otherwise, a regular
 *     principal will be used. Also, this doesn't like Storage Access Principal
 *     which changes according to storage access of a resource. Note that this
 *     is dFPI only; this prinipcal will always return regular principal when
 *     dFPI is disabled.
 *
 * Consumers of StoragePrincipal can request the principal type that meets their
 * needs. For example, storage that should always be partitioned should choose
 * the Partitioned Principal, while storage that should change with storage
 * access grants should choose the Storage Access Principal. And the storage
 * should be always partiitoned in the third-party context should use the
 * Foreign Partitioned Principal.
 *
 * You can obtain these nsIPrincipal objects:
 *
 * From a Document:
 * - Regular Principal: nsINode::NodePrincipal
 * - Storage Access Principal: Document::EffectiveStoragePrincipal
 * - Partitioned Principal: Document::PartitionedPrincipal
 *
 * From a Global object:
 * - Regular Principal: nsIScriptObjectPrincipal::GetPrincipal
 * - Storage Access Principal:
 *     nsIScriptObjectPrincipal::GetEffectiveStoragePrincipal
 * - Partitioned Principal: nsIScriptObjectPrincipal::PartitionedPrincipal
 *
 * From a Worker:
 * - Regular Principal: WorkerPrivate::GetPrincipal (main-thread)
 * - Regular Principal: WorkerPrivate::GetPrincipalInfo (worker thread)
 * - Storage Access Principal: WorkerPrivate::GetEffectiveStoragePrincipalInfo
 *                  (worker-thread)
 *
 * For a nsIChannel, the final principals must be calculated and they can be
 * obtained by calling:
 * - Regular Principal: nsIScriptSecurityManager::getChannelResultPrincipal
 * - Storage Access Principal:
 *     nsIScriptSecurityManager::getChannelResultStoragePrincipal
 * - Partitioned and regular Principal:
 *     nsIScriptSecurityManager::getChannelResultPrincipals
 *
 * Each use of nsIPrincipal is unique and it should be reviewed by anti-tracking
 * peers. But we can group the use of nsIPrincipal in these categories:
 *
 * - Network loading: use the Regular Principal
 * - Cache, not directly visible by content (network cache, HSTS, image cache,
 *   etc): Use the Storage Access Principal (in the future we will use the
 *   Partitioned Principal, but this part is not done yet)
 * - Storage APIs or anything that is written on disk (or kept in memory in
 *   private-browsing): use the Storage Access Principal
 * - PostMessage: if in the agent-cluster, use the Regular Principal. Otherwise,
 *   use the Storage Access Principal
 *
 * Storage access permission
 * ~~~~~~~~~~~~~~~~~~~~~~~~~
 *
 * When the storage access permission is granted, any of the Storage Access
 * Principal getter methods will return the Regular Principal instead of the
 * Partitioned Principal, and each storage component should consider the new
 * principal only.
 *
 * The trackers and the 3rd parties (in dFPI) will have access to its
 first-party
 * cookie jar, escaping from its partitioning.
 *
 * Storage access permissions can be granted in several ways:
 * - The Storage Access API
 *     (https://developer.mozilla.org/en-US/docs/Web/API/Storage_Access_API)
 * - ETP’s heuristics
 *
 (https://developer.mozilla.org/en-US/docs/Mozilla/Firefox/Privacy/Storage_access_policy#Storage_access_grants)
 * - A dFPI-specific login heuristic
 *     (https://bugzilla.mozilla.org/show_bug.cgi?id=1616585#c12)
 *
 * There are several ways to receive storage-permission notifications. You can
 * use these notifications to re-initialize components, to nullify or enable
 them
 * to use the “new” effective StoragePrincipal. The list of the notifications
 is:
 *
 * - Add some code in nsGlobalWindowInner::StorageAccessPermissionGranted().
 * - WorkerScope::StorageAccessPermissionGranted for Workers.
 * - observe the permission changes (not recommended)
 *
 * Scope of Storage Access
 * ~~~~~~~~~~~~~~~~~~~~~~~
 *
 * Immediately after access is granted, the permission is propagated and
 notified
 * to any contexts (windows and workers) in the same agent-cluster
 * (BrowserContextGroup).
 *
 * This means that if A.com has 2 iframes with B.com, and one of the 2 Bs
 obtains
 * the storage access, the other B will be notified too. Other B.com, 3rd
 parties
 * in other agent clusters will not obtain the storage permission.
 *
 * When the page is reloaded or is loaded for the first time, if it contains
 * B.com, and B.com has received the storage permission for the same first-party
 * in a previous loading, B.com will have the storage access permission granted
 * immediately.
 *
 * Cookies, LocalStorage, indexedDB
 * ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
 *
 * When granting storage permission, several storage and channel API getters and
 * constructors will start exposing first-party cookie jar objects
 (localStorage,
 * BroadcastChannel, etc).
 *
 * There is a side effect of this change: If a tracker has a reference to these
 * objects pre-storage permission granting, it will be able to interact with the
 * partitioned and the non-partitioned cookie jar at the same time. Note that
 * similar synchronization can be done server-side too. Because of this, we
 don’t
 * think that privacy-wise, this is an issue.
 *
 * localStorage supports StoragePrincipal, and will be switched after storage
 * access is granted. Trackers listed in the pref
 * privacy.restrict3rdpartystorage.partitionedHosts will use another special
 * partitioned session-only storage called PartitionedLocalStorage.
 *
 * sessionStorage is not covered by StoragePrincipal, but is double-keyed using
 * the top-level site when dFPI is active
 * (https://bugzilla.mozilla.org/show_bug.cgi?id=1629707).
 *
 * SharedWorkers and BroadcastChannels
 * ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
 *
 * SharedWorker and BroadcastChannel instances latch the effective storage
 * principal at the moment of their creation. Existing bindings to the
 * partitioned storage principal will continue to exist and operate even as it
 * becomes possible to create bindings associated with the Regular Principal.
 * This makes it possible for such globals to bi-directionally bridge
 information
 * between partitioned and non-partitioned principals.
 *
 * This is true until the page is reloaded. After the reload, the partitioned
 * cookie jar will no longer be accessible.
 *
 * We are planning to clear the partitioned site-data as soon as the page is
 * reloaded or dismissed (not done yet - bug 1628313).
 *
 * {Dedicated,Shared,Service}Workers
 * ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
 *
 * The storage access permission propagation happens with a ControlRunnable.
 This
 * could impact the use of sync event-loops. Take a reference of the principal
 * you want to use because it can change!
 *
 * ServiceWorkers are currently disabled for partitioned contexts.
 *
 * Client API uses the regular nsIPrincipal always because there is not a direct
 * connection between this API and the cookie jar. If we want to support
 * ServiceWorkers in partitioned context, this part must be revisited.
 */

class nsIChannel;
class nsICookieJarSettings;
class nsIDocShell;
class nsILoadGroup;
class nsIPrincipal;
class nsIURI;
class nsPIDOMWindowInner;

namespace mozilla {

namespace dom {
class Document;
class WorkerPrivate;
}  // namespace dom

namespace ipc {
class PrincipalInfo;
}

class OriginAttributes;

class StoragePrincipalHelper final {
 public:
  static nsresult Create(nsIChannel* aChannel, nsIPrincipal* aPrincipal,
                         bool aForceIsolation,
                         nsIPrincipal** aStoragePrincipal);

  static nsresult CreatePartitionedPrincipalForServiceWorker(
      nsIPrincipal* aPrincipal, nsICookieJarSettings* aCookieJarSettings,
      nsIPrincipal** aPartitionedPrincipal);

  static nsresult PrepareEffectiveStoragePrincipalOriginAttributes(
      nsIChannel* aChannel, OriginAttributes& aOriginAttributes);

  // A helper function to verify storage principal info with the principal info.
  static bool VerifyValidStoragePrincipalInfoForPrincipalInfo(
      const mozilla::ipc::PrincipalInfo& aStoragePrincipalInfo,
      const mozilla::ipc::PrincipalInfo& aPrincipalInfo);

  // A helper function to verify client principal info with the principal info.
  //
  // Note that the client principal refers the principal of the client, which is
  // supposed to be the foreign partitioned principal.
  static bool VerifyValidClientPrincipalInfoForPrincipalInfo(
      const mozilla::ipc::PrincipalInfo& aClientPrincipalInfo,
      const mozilla::ipc::PrincipalInfo& aPrincipalInfo);

  enum PrincipalType {
    // This is the first-party principal.
    eRegularPrincipal,

    // This is a dynamic principal based on the current state of the origin. If
    // the origin has the storage permission granted, effective storagePrincipal
    // will be the regular principal, otherwise, the partitioned Principal
    // will be used.
    eStorageAccessPrincipal,

    // This is the first-party principal, plus, First-party isolation attribute
    // set.
    ePartitionedPrincipal,

    // This principal returns different results based on whether its associated
    // channel/window is in a third-party context. While in a third-party
    // context, it returns the partitioned principal; otherwise, it returns the
    // regular principal.
    //
    // Note that this principal is not a dynamic principal like
    // `eStorageAccessPrincipal`, which changes depending on whether the storage
    // access permission is granted. This principal doesn't take the storage
    // access permission into consideration. Also, this principle is used in
    // dFPI only, meaning that it always returns the regular principal when dFP
    // Is disabled.
    eForeignPartitionedPrincipal,
  };

  /**
   * Extract the principal from the channel/document according to the given
   * principal type.
   */
  static nsresult GetPrincipal(nsIChannel* aChannel,
                               PrincipalType aPrincipalType,
                               nsIPrincipal** aPrincipal);
  static nsresult GetPrincipal(nsPIDOMWindowInner* aWindow,
                               PrincipalType aPrincipalType,
                               nsIPrincipal** aPrincipal);

  // Check if we need to use the partitioned principal for the service worker of
  // the given docShell. Please do not use this API unless you cannot get the
  // foreign partitioned principal, e.g. creating the inital about:blank page.
  static bool ShouldUsePartitionPrincipalForServiceWorker(
      nsIDocShell* aDocShell);

  static bool ShouldUsePartitionPrincipalForServiceWorker(
      dom::WorkerPrivate* aWorkerPrivate);

  /**
   * Extract the right OriginAttributes from the channel's triggering
   * principal.
   */
  static bool GetOriginAttributes(nsIChannel* aChannel,
                                  OriginAttributes& aAttributes,
                                  PrincipalType aPrincipalType);

  static bool GetRegularPrincipalOriginAttributes(
      dom::Document* aDocument, OriginAttributes& aAttributes);

  static bool GetRegularPrincipalOriginAttributes(
      nsILoadGroup* aLoadGroup, OriginAttributes& aAttributes);

  // These methods return the correct originAttributes to be used for network
  // state components (HSTS, network cache, image-cache, and so on).
  static bool GetOriginAttributesForNetworkState(nsIChannel* aChannel,
                                                 OriginAttributes& aAttributes);
  static void GetOriginAttributesForNetworkState(dom::Document* aDocument,
                                                 OriginAttributes& aAttributes);
  static void UpdateOriginAttributesForNetworkState(
      nsIURI* aFirstPartyURI, OriginAttributes& aAttributes);

  // For HSTS we want to force 'HTTP' in the partition key.
  static bool GetOriginAttributesForHSTS(nsIChannel* aChannel,
                                         OriginAttributes& aAttributes);

  // Like the function above, this function forces `HTTPS` in the partition key.
  // The OA created by this function is mainly used in DNS cache. The spec
  // specifies that the presence of HTTPS RR for an origin also indicates that
  // all HTTP resources are available over HTTPS, so we use this function to
  // ensure that all HTTPS RRs in DNS cache are accessed by HTTPS requests only.
  static bool GetOriginAttributesForHTTPSRR(nsIChannel* aChannel,
                                            OriginAttributes& aAttributes);

  // Get the origin attributes from a PrincipalInfo
  static bool GetOriginAttributes(
      const mozilla::ipc::PrincipalInfo& aPrincipalInfo,
      OriginAttributes& aAttributes);

  static bool PartitionKeyHasBaseDomain(const nsAString& aPartitionKey,
                                        const nsACString& aBaseDomain);

  static bool PartitionKeyHasBaseDomain(const nsAString& aPartitionKey,
                                        const nsAString& aBaseDomain);

  // Partition keys can have the same-site bit added or removed from them.
  // "(https,foo.com)", false -> "(https,foo.com)"
  // "(https,foo.com,f)", false -> "(https,foo.com)"
  // "(https,foo.com,f)", true -> "(https,foo.com,f)"
  // "(https,foo.com)", true -> "(https,foo.com,f)"
  static void UpdatePartitionKeyWithForeignAncestorBit(
      nsAString& aKey, bool aForeignByAncestorContext);

  // Generate a partition-key for an expanded principal to be used for any
  // injected resources by add-on content scripts.
  static nsString PartitionKeyForExpandedPrincipal(
      nsIPrincipal* aExpandedPrincipal);
};

}  // namespace mozilla

#endif  // mozilla_StoragePrincipalHelper_h
