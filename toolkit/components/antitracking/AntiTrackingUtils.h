/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef mozilla_antitrackingutils_h
#define mozilla_antitrackingutils_h

#include "mozilla/AlreadyAddRefed.h"
#include "mozilla/Maybe.h"
#include "nsStringFwd.h"
#include "ContentBlockingNotifier.h"

#include "nsIClassifiedChannel.h"
#include "nsILoadInfo.h"

class nsPIDOMWindowInner;
class nsPIDOMWindowOuter;
class nsIChannel;
class nsIPermission;
class nsIPrincipal;
class nsIURI;

namespace mozilla {
namespace dom {
class BrowsingContext;
class CanonicalBrowsingContext;
class Document;
class WindowGlobalParent;
}  // namespace dom

class AntiTrackingUtils final {
 public:
  static already_AddRefed<nsPIDOMWindowInner> GetInnerWindow(
      dom::BrowsingContext* aBrowsingContext);

  static already_AddRefed<nsPIDOMWindowOuter> GetTopWindow(
      nsPIDOMWindowInner* aWindow);

  // Get the current document URI from a document channel as it is being loaded.
  static already_AddRefed<nsIURI> MaybeGetDocumentURIBeingLoaded(
      nsIChannel* aChannel);

  static void CreateStoragePermissionKey(const nsACString& aTrackingOrigin,
                                         nsACString& aPermissionKey);

  // Given a principal, returns the storage permission key that will be used for
  // the principal.  Returns true on success.
  static bool CreateStoragePermissionKey(nsIPrincipal* aPrincipal,
                                         nsACString& aKey);

  static void CreateStorageFramePermissionKey(const nsACString& aTrackingSite,
                                              nsACString& aPermissionKey);

  // Given a principal, returns the per-frame storage permission key that will
  // be used for the principal.  Returns true on success.
  static bool CreateStorageFramePermissionKey(nsIPrincipal* aPrincipal,
                                              nsACString& aKey);

  // Returns true if the permission passed in is a storage access permission
  // for the passed in principal argument.
  static bool IsStorageAccessPermission(nsIPermission* aPermission,
                                        nsIPrincipal* aPrincipal);

  // Returns true if the storage permission is granted for the given principal
  // and the storage permission key.
  static bool CheckStoragePermission(nsIPrincipal* aPrincipal,
                                     const nsAutoCString& aType,
                                     bool aIsInPrivateBrowsing);

  // Returns the number of sites that give this principal's origin storage
  // access.
  static Maybe<size_t> CountSitesAllowStorageAccess(nsIPrincipal* aPrincipal);

  // Test whether or not there is a storage access permission in aTopPrincipal
  // with secondary key for embedee aPrincipal.
  static nsresult TestStoragePermissionInParent(nsIPrincipal* aTopPrincipal,
                                                nsIPrincipal* aPrincipal,
                                                uint32_t* aResult);

  // Returns the storage permission state for the given channel. And this is
  // meant to be called in the parent process. This only reflects the fact that
  // whether the channel has the storage permission. It doesn't take the window
  // hierarchy into account. i.e. this will return
  // nsILoadInfo::HasStoragePermission even for a nested iframe that has storage
  // permission.
  static nsILoadInfo::StoragePermissionState GetStoragePermissionStateInParent(
      nsIChannel* aChannel);

  // Upgrades the storage permission state from inactive to active. Used in
  // Storage-Access-Headers
  static nsresult ActivateStoragePermissionStateInParent(nsIChannel* aChannel);

  // Parse Activate-Storage-Access response header, activate storage-access in
  // frame if necessary returns whether the channel needs to reload after with
  // access to unpartitioned cookies.
  static bool ProcessStorageAccessHeadersShouldRetry(nsIChannel* aChannel);

 private:
  static nsresult ProcessStorageAccessHeaders(nsIChannel* aChannel,
                                              bool* aOutReload);

 public:
  // Returns the toplevel inner window id, returns 0 if this is a toplevel
  // window.
  static uint64_t GetTopLevelAntiTrackingWindowId(
      dom::BrowsingContext* aBrowsingContext);

  // Returns the parent inner window id, returns 0 if this or the parent are not
  // a toplevel window. This is mainly used to determine the anti-tracking
  // storage area.
  static uint64_t GetTopLevelStorageAreaWindowId(
      dom::BrowsingContext* aBrowsingContext);

  // Returns the principal of the given browsing context.
  // This API should only be used either in child processes with an in-process
  // browsing context or in the parent process.
  static already_AddRefed<nsIPrincipal> GetPrincipal(
      dom::BrowsingContext* aBrowsingContext);

  // Returns the principal of the given browsing context and tracking origin.
  // This API should only be used either in child processes with an in-process
  // browsing context or in the parent process.
  static bool GetPrincipalAndTrackingOrigin(
      dom::BrowsingContext* aBrowsingContext, nsIPrincipal** aPrincipal,
      nsACString& aTrackingOrigin);

  // Retruns the cookie behavior of the given browsingContext,
  // return BEHAVIOR_REJECT when fail.
  static uint32_t GetCookieBehavior(dom::BrowsingContext* aBrowsingContext);

  // Returns the top-level global window parent. But we would stop at the
  // content window which is loaded by addons and consider this window as a top.
  //
  // Note that this is the parent-process implementation of
  // nsGlobalWindowOuter::GetTopExcludingExtensionAccessibleContentFrames
  static already_AddRefed<dom::WindowGlobalParent>
  GetTopWindowExcludingExtensionAccessibleContentFrames(
      dom::CanonicalBrowsingContext* aBrowsingContext, nsIURI* aURIBeingLoaded);

  // Given a channel, compute and set the IsThirdPartyContextToTopWindow for
  // this channel. This function is supposed to be called in the parent process.
  static void ComputeIsThirdPartyToTopWindow(nsIChannel* aChannel);

  // Given a channel, this function determines if this channel is a third party.
  // Note that this function also considers the top-level window. The channel
  // will be considered as a third party only when it's a third party to both
  // its parent and the top-level window.
  static bool IsThirdPartyChannel(nsIChannel* aChannel);

  // Returns true if the channel is annotated as a third-party tracker. This
  // takes the channel annotation preferences into account. Returns false for
  // null or non-classified channels.
  static bool IsThirdPartyTrackingChannel(nsIChannel* aChannel);

  // Returns true if the channel is annotated as a first-party tracker. This
  // takes the channel annotation preferences into account. Returns false for
  // null or non-classified channels.
  static bool IsFirstPartyTrackingChannel(nsIChannel* aChannel);

  // Returns the first- and third-party classification flags of the channel, or
  // zero flags for null or non-classified channels.
  static net::ClassificationFlags GetClassificationFlags(nsIChannel* aChannel);

  // Returns true if the channel is annotated as a third-party social tracker.
  // Returns false for null or non-classified channels.
  static bool IsThirdPartySocialTrackingChannel(nsIChannel* aChannel);

  // Returns true if the tracking protection preferences enable blocking of
  // tracking resources, either globally or for private browsing only.
  static bool IsTrackingProtectionEnabled(bool aIsPrivate);

  // Given a window and a URI, this function first determines if the window is
  // third-party with respect to the URI. The function returns if it's true.
  // Otherwise, it will continue to check if the window is third-party.
  static bool IsThirdPartyWindow(nsPIDOMWindowInner* aWindow, nsIURI* aURI);

  // Given a Document, this function determines if this document
  // is considered as a third party with respect to the top level context.
  // This prefers to use the document's Channel's LoadInfo, but falls back to
  // the BrowsingContext.
  static bool IsThirdPartyDocument(dom::Document* aDocument);

  // Given a browsing context, this function determines if this browsing context
  // is considered as a third party in respect to the top-level context.
  static bool IsThirdPartyContext(dom::BrowsingContext* aBrowsingContext);

  static nsCString GrantedReasonToString(
      ContentBlockingNotifier::StorageAccessPermissionGrantedReason aReason);

  /**
   * This function updates all the fields used by anti-tracking when a channel
   * is opened. We have to do this in the parent to access cross-origin info
   * that is not exposed to child processes.
   */
  static void UpdateAntiTrackingInfoForChannel(nsIChannel* aChannel);

 private:
  // Helper function for ComputeIsThirdPartyToTopWindow to peer into the
  // partition
  // key in the case that we don't have any other way to determine
  // third-partiness to the top level, e.g. third party SharedWorker script
  // loads.
  static nsresult IsThirdPartyToPartitionKeySite(nsIChannel* aChannel,
                                                 const nsCOMPtr<nsIURI>& aURI,
                                                 bool* aIsThirdParty);
};

}  // namespace mozilla

#endif  // mozilla_antitrackingutils_h
