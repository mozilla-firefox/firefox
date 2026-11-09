/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef mozilla_BounceTrackingState_h
#define mozilla_BounceTrackingState_h

#include "BounceTrackingRecord.h"
#include "mozilla/Maybe.h"
#include "mozilla/RefPtr.h"
#include "mozilla/WeakPtr.h"
#include "mozilla/OriginAttributes.h"
#include "nsIPrincipal.h"
#include "nsStringFwd.h"
#include "nsIWebProgressListener.h"
#include "nsWeakReference.h"
#include "fmt/format.h"

class nsIChannel;
class nsITimer;
class nsIPrincipal;

namespace mozilla {

class BounceTrackingProtection;

namespace dom {
class CanonicalBrowsingContext;
class BrowsingContext;
class BrowsingContextWebProgress;
}  // namespace dom

/**
 * This class manages the bounce tracking state for a given tab. It is attached
 * to top-level CanonicalBrowsingContexts.
 */
class BounceTrackingState : public nsIWebProgressListener,
                            public nsSupportsWeakReference,
                            public SupportsWeakPtr {
 public:
  NS_DECL_ISUPPORTS
  NS_DECL_NSIWEBPROGRESSLISTENER

  // Gets or creates an existing BrowsingContextState keyed by browserId. May
  // return nullptr if the given web progress / browsing context is not suitable
  // (see ShouldCreateBounceTrackingStateForWebProgress).
  static already_AddRefed<BounceTrackingState> GetOrCreate(
      dom::BrowsingContextWebProgress* aWebProgress, nsresult& aRv);

  // Reset state for all BounceTrackingState instances this includes resetting
  // BounceTrackingRecords and cancelling any running timers.
  static void ResetAll();

  // Resets and destroys all BounceTrackingState objects. This is used when the
  // feature gets disabled.
  static void DestroyAll();

  // Reset BounceTrackingState objects matching OriginAttributes.
  static void ResetAllForOriginAttributes(
      const OriginAttributes& aOriginAttributes);
  // Same as above but for a pattern.
  static void ResetAllForOriginAttributesPattern(
      const OriginAttributesPattern& aPattern);

  BounceTrackingRecord* GetBounceTrackingRecord();

  void ResetBounceTrackingRecord();

  // The top level BrowsingContext and its BrowsingContextWebProgress are
  // discarded (e.g. tab closed).
  void OnBrowsingContextDiscarded();

  // Callback for when we received a response from the server and are about to
  // create a document for the response. Calls into
  // BounceTrackingState::OnResponseReceived.
  [[nodiscard]] nsresult OnDocumentStartRequest(nsIChannel* aChannel);

  // At the start of a navigation, either initialize a new bounce tracking
  // record, or append a client-side redirect to the current bounce tracking
  // record.
  // Should only be called for top level content navigations.
  // aLoadId is the navigation's load identifier
  // (nsDocShellLoadState::GetLoadIdentifier). A single logical navigation can
  // reach this method more than once (speculative parent load, process switch,
  // or a re-open of the same load) all carrying the same load id; only the
  // first call for a given id advances the state machine.
  [[nodiscard]] nsresult OnStartNavigation(
      nsIPrincipal* aTriggeringPrincipal,
      const bool aHasValidUserGestureActivation, uint64_t aLoadId);

  // A top level history load was served from the bfcache and the restored
  // document is now current. Such a load has no DocumentLoadListener, so the
  // caller must have called OnStartNavigation for it before the swap. This
  // stands in for the response and the document load.
  [[nodiscard]] nsresult OnBFCacheRestored();

  // A top level load which started in aSource is about to commit in aTarget, a
  // new tab created for the container the load switched to. The extended
  // navigation moves along with the load, unless aSource has started another
  // navigation since. aLoadId is the retargeted load's load identifier.
  static void OnLoadRetargeted(dom::CanonicalBrowsingContext* aSource,
                               dom::CanonicalBrowsingContext* aTarget,
                               uint64_t aLoadId);

  // Check if there is a BounceTrackingState which current browsing context is
  // associated with aSite. The partition is taken into account for isolation
  // between normal browsing, private browsing and containers.
  // This is an approximation for checking if a given site is currently loaded
  // in the top level context, e.g. in a tab. See Bug 1842047 for adding a more
  // accurate check that calls into the browser implementations.
  [[nodiscard]] static nsresult HasBounceTrackingStateForSite(
      const BounceTrackingSite& aSite, bool& aResult);

  // Get the currently associated BrowsingContext. Returns nullptr if it has not
  // been attached yet.
  already_AddRefed<dom::BrowsingContext> CurrentBrowsingContext();

  uint64_t GetBrowserId() { return mBrowserId; }

  const OriginAttributes& OriginAttributesRef();

  // Record sites which have user activation in the current extended
  // navigation.
  [[nodiscard]] nsresult OnUserActivation(const BounceTrackingSite& aSite);

  // Drops the hops of a load retargeted into another container from the record
  // if the record is classified or continued here before OnLoadRetargeted could
  // move it. Otherwise the destination would remain a bounce, never become the
  // final host and be classified.
  void DropRetargetedLoadBounces();

 private:
  explicit BounceTrackingState();
  virtual ~BounceTrackingState();

  bool mIsInitialized{false};

  uint64_t mBrowserId{};

  // OriginAttributes associated with the browser this state is attached to.
  OriginAttributes mOriginAttributes;

  // Reference to the BounceTrackingProtection singleton.
  RefPtr<BounceTrackingProtection> mBounceTrackingProtection;

  // Record to keep track of extended navigation data. Reset on extended
  // navigation end.
  RefPtr<BounceTrackingRecord> mBounceTrackingRecord;

  // Load id of the most recent navigation OnStartNavigation ran the state
  // machine for. Used to ignore duplicate calls for the same logical
  // navigation, which happen because one navigation can span multiple
  // DocumentLoadListeners across process switches and speculative loads.
  Maybe<uint64_t> mLastStartedLoadId;

  // The storage partition the current load switched into, set when its
  // response is received and until OnLoadRetargeted moves the record into the
  // new tab. Hops in this partition belong to the load in the new tab: they are
  // dropped from the record if it ends here first, see
  // DropRetargetedLoadBounces.
  Maybe<OriginAttributes> mRetargetedOriginAttributes;

  // Timer to wait to wait for a client redirect after a navigation ends.
  RefPtr<nsITimer> mClientBounceDetectionTimeout;

  // Reset state for all BounceTrackingState instances this includes resetting
  // BounceTrackingRecords and cancelling any running timers.
  // Optionally filter by OriginAttributes or OriginAttributesPattern.
  static void Reset(const OriginAttributes* aOriginAttributes,
                    const OriginAttributesPattern* aPattern);

  // Whether the given web progress should hold a BounceTrackingState
  // instance to monitor bounce tracking navigations.
  static bool ShouldCreateBounceTrackingStateForWebProgress(
      dom::BrowsingContextWebProgress* aWebProgress);

  // Init to be called after creation, attaches nsIWebProgressListener.
  [[nodiscard]] nsresult Init(dom::BrowsingContextWebProgress* aWebProgress);

  // When the response is received at the end of a navigation, fill the
  // bounce set.
  [[nodiscard]] nsresult OnResponseReceived(
      const nsTArray<BounceTrackingSite>& aSiteList);

  [[nodiscard]] nsresult StartClientBounceDetectionTimeout();

  void CancelClientBounceDetectionTimeout();

  // When the document is loaded at the end of a navigation, update the
  // final host.
  [[nodiscard]] nsresult OnDocumentLoaded(nsIPrincipal* aDocumentPrincipal);

  friend struct fmt::formatter<BounceTrackingState>;
};

}  // namespace mozilla

template <>
struct fmt::formatter<mozilla::BounceTrackingState>
    : fmt::formatter<std::string_view> {
  auto format(const mozilla::BounceTrackingState& aState,
              fmt::format_context& aCtx) const {
    nsAutoCString oaSuffix;
    aState.mOriginAttributes.CreateSuffix(oaSuffix);

    auto out = aCtx.out();
    out = fmt::format_to(out, "{{ mBounceTrackingRecord: ");
    if (aState.mBounceTrackingRecord) {
      out = fmt::format_to(out, "{}", *aState.mBounceTrackingRecord);
    } else {
      out = fmt::format_to(out, "null");
    }
    return fmt::format_to(out, ", mOriginAttributes: {}, mBrowserId: {} }}",
                          oaSuffix, aState.mBrowserId);
  }
};

#endif
