/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef mozilla_dom_ViewTransition_h
#define mozilla_dom_ViewTransition_h

#include <functional>
#include <memory>

#include "mozilla/Attributes.h"
#include "mozilla/UniquePtr.h"
#include "mozilla/layers/IpcResourceUpdateQueue.h"
#include "nsAtomHashKeys.h"
#include "nsCOMPtr.h"
#include "nsClassHashtable.h"
#include "nsRect.h"
#include "nsRefPtrHashtable.h"
#include "nsWrapperCache.h"

class nsIGlobalObject;
class nsIPrincipal;
class nsITimer;

namespace mozilla {

class ErrorResult;
struct Keyframe;
struct PseudoStyleRequest;
struct StyleLockedDeclarationBlock;
class WindowRenderer;

namespace layers {
class RenderRootStateManager;
}  // namespace layers

namespace wr {
struct ImageKey;
class IpcResourceUpdateQueue;
}  // namespace wr

namespace dom {

class ViewTransitionTypeSet;
extern LazyLogModule gViewTransitionsLog;

#define VT_LOG(...)                                                    \
  MOZ_LOG(mozilla::dom::gViewTransitionsLog, mozilla::LogLevel::Debug, \
          (__VA_ARGS__))

#ifdef DEBUG
#  define VT_LOG_DEBUG(...) VT_LOG(__VA_ARGS__)
#else
#  define VT_LOG_DEBUG(...)
#endif

class Document;
class Element;
class Promise;
class ViewTransitionUpdateCallback;

enum class SkipTransitionReason : uint8_t {
  JS,
  DocumentHidden,
  RootRemoved,
  ClobberedActiveTransition,
  Timeout,
  UpdateCallbackRejected,
  DuplicateTransitionNameCapturingOldState,
  DuplicateTransitionNameCapturingNewState,
  PseudoUpdateFailure,
  Resize,
  PageSwap,
  // Can happen due to various recoverable internal errors such as GPU process
  // crashes or GPU device resets.
  ResetRendering,
  // The navigation an outbound cross-document transition was captured for was
  // stopped or superseded before the new document was revealed.
  NavigationAborted,
};

// https://drafts.csswg.org/css-view-transitions-1/#viewtransition-phase
enum class ViewTransitionPhase : uint8_t {
  PendingCapture = 0,
  UpdateCallbackCalled,
  Animating,
  PendingDone,
  Done,
};

struct ViewTransitionCapturedElement;
struct OldSnapshotData;

using ViewTransitionNamedElements =
    nsClassHashtable<nsAtomHashKey, ViewTransitionCapturedElement>;

// https://drafts.csswg.org/css-view-transitions-2/#view-transition-params
// See the same members on ViewTransition
struct ViewTransitionParams {
  ViewTransitionNamedElements namedElements;
  AutoTArray<RefPtr<nsAtom>, 8> names;
  nsSize initialSnapshotContainingBlockSize;
  // Principal of the document whose old state was captured. Reveal() checks
  // that the new document is same-origin with it.
  nsCOMPtr<nsIPrincipal> producerPrincipal;
  // Id of the BrowsingContext whose document captured the old state.
  uint64_t producerBrowsingContextId = 0;

  ~ViewTransitionParams();
};

class ViewTransition final : public nsISupports, public nsWrapperCache {
 public:
  using Phase = ViewTransitionPhase;
  using TypeList = nsTArray<RefPtr<nsAtom>>;

  NS_DECL_CYCLE_COLLECTING_ISUPPORTS_FINAL
  NS_DECL_CYCLE_COLLECTION_WRAPPERCACHE_CLASS(ViewTransition)

  ViewTransition(Document&, ViewTransitionUpdateCallback*, TypeList&&);

  static already_AddRefed<ViewTransition> CreateCrossDocument(
      Document&, UniquePtr<ViewTransitionParams>, TypeList&&);

  static already_AddRefed<ViewTransition> CreateOutboundCrossDocument(
      Document&, TypeList&&);

  bool IsOutboundCrossDocument() const { return mIsOutboundCrossDocument; }

  // Moves old-state snapshots from another WebRender bridge to the bridge of
  // aRenderer. Does nothing if aRenderer does not use WebRender.
  static void AdoptOldSnapshots(WindowRenderer* aRenderer,
                                ViewTransitionNamedElements& aNamedElements);

  // Skips the active view transitions of aDocument and its subdocuments.
  // Snapshot image keys are not valid anymore after the WebRender instance or
  // id namespace changes.
  static void SkipAllAfterRenderingReset(Document& aDocument);

  [[nodiscard]] MOZ_CAN_RUN_SCRIPT UniquePtr<ViewTransitionParams>
  CaptureOldStateForCrossDocument();

  // Captures the old state in the next rendering update, after the animation
  // frame callbacks. aProceed then gets the captured params, or null if the
  // transition was skipped.
  void BeginDeferredOutboundCapture(
      std::function<void(UniquePtr<ViewTransitionParams>&&)>&& aProceed);

  Promise* GetUpdateCallbackDone(ErrorResult&);
  Promise* GetReady(ErrorResult&);
  Promise* GetFinished(ErrorResult&);
  ViewTransitionTypeSet* Types();
  const TypeList& GetTypeList() const { return mTypeList; }
  TypeList& GetTypeList() { return mTypeList; }

  // Called when the active types change. Updates the styles that use
  // :active-view-transition-type().
  void NotifyTypesChanged();

  void SkipTransition(SkipTransitionReason = SkipTransitionReason::JS);
  MOZ_CAN_RUN_SCRIPT void PerformPendingOperations();

  void FinishDone();

  // Get the snapshot containing block, which is the top-layer for rendering the
  // view transition tree.
  Element* GetSnapshotContainingBlock() const {
    return mSnapshotContainingBlock;
  }
  // Get ::view-transition pseudo element, which is the view transition tree
  // root. We find the pseudo element of this tree from this node.
  Element* GetViewTransitionTreeRoot() const;
  // Get all currently captured element frames if accessible.
  void GetCapturedFrames(nsTArray<nsIFrame*>& aCapturedFrames) const;

  Maybe<nsRect> GetOldInkOverflowRect(nsAtom* aName) const;
  Maybe<nsRect> GetNewInkOverflowRect(nsAtom* aName) const;
  Maybe<nsSize> GetOldBorderBoxSize(nsAtom* aName) const;
  Maybe<nsSize> GetNewBorderBoxSize(nsAtom* aName) const;
  Maybe<nsRect> GetOldActiveRect(nsAtom* aName) const;
  Maybe<nsRect> GetNewActiveRect(nsAtom* aName) const;
  // Use this to generate the old state image key for use in a stacking context.
  // Do not use the returned image key in an image display item, use
  // ReadOldImageKey instead.
  const wr::ImageKey* GetOrCreateOldImageKey(nsAtom* aName,
                                             layers::RenderRootStateManager*,
                                             wr::IpcResourceUpdateQueue&) const;
  // Use this to get the already-created old state image key for use in an image
  // display item.
  // This marks the old state image key as used which influences the how eagerly
  // it can be deleted.
  const wr::ImageKey* ReadOldImageKey(nsAtom* aName,
                                      layers::RenderRootStateManager*,
                                      wr::IpcResourceUpdateQueue&) const;
  const wr::ImageKey* GetNewImageKey(nsAtom* aName) const;
  const wr::ImageKey* GetImageKeyForCapturedFrame(
      nsIFrame* aFrame, layers::RenderRootStateManager*,
      wr::IpcResourceUpdateQueue&) const;
  void UpdateActiveRectForCapturedFrame(
      nsIFrame* capturedFrame, const gfx::MatrixScales& aInheritedScale,
      nsRect& aOutCapturedRect);

  // For a transition in an in-process subdocument, the embedder paints a
  // snapshot of the whole subdocument while rendering is suppressed for the
  // transition, instead of the subdocument itself.
  bool IsCapturingSubdocumentSnapshot() const {
    return mCapturingSubdocumentSnapshot;
  }
  // Whether the embedder holds the subdocument's last paint. It then paints the
  // snapshot if there is one, and nothing otherwise, e.g. if the subdocument
  // wasn't visible when the snapshot was taken.
  bool IsHoldingSubdocument() const { return !!mSubdocumentSnapshot; }
  // The snapshot rect, relative to the embedder frame.
  nsRect SubdocumentSnapshotRect() const;
  // Restricts the snapshot rect to aRect, relative to the embedder frame, and
  // returns the result.
  nsRect ClipSubdocumentSnapshotRect(const nsRect& aRect);
  // Same as GetOrCreateOldImageKey and ReadOldImageKey, for the subdocument
  // snapshot.
  const wr::ImageKey* GetOrCreateSubdocumentSnapshotKey(
      layers::RenderRootStateManager*, wr::IpcResourceUpdateQueue&);
  const wr::ImageKey* ReadSubdocumentSnapshotKey();

  Element* FindPseudo(const PseudoStyleRequest&) const;

  const StyleLockedDeclarationBlock* GetDynamicRuleFor(const Element&) const;

  static constexpr nsLiteralString kGroupAnimPrefix =
      u"-ua-view-transition-group-anim-"_ns;

  [[nodiscard]] bool GetGroupKeyframes(nsAtom* aAnimationName,
                                       const StyleComputedTimingFunction&,
                                       nsTArray<Keyframe>&);

  bool MatchClassList(nsAtom*, const nsTArray<StyleAtom>&) const;

  nsIGlobalObject* GetParentObject() const;
  JSObject* WrapObject(JSContext*, JS::Handle<JSObject*> aGivenProto) override;

  static nsRect SnapshotContainingBlockRect(nsPresContext*);
  static nsRect CapturedInkOverflowRectForFrame(nsIFrame*, bool aIsRoot);
  MOZ_CAN_RUN_SCRIPT void CallUpdateCallback(ErrorResult&);

  void Activate();

 private:
  using CapturedElement = ViewTransitionCapturedElement;

  MOZ_CAN_RUN_SCRIPT void MaybeScheduleUpdateCallback();

  void ClearActiveTransition(bool aIsDocumentHidden);
  void Timeout();
  // Runs mOutboundCaptureProceed without params if it is set, so that the
  // navigation continues.
  void ReleaseDeferredOutboundCapture();
  MOZ_CAN_RUN_SCRIPT void Setup();
  [[nodiscard]] MOZ_CAN_RUN_SCRIPT Maybe<SkipTransitionReason>
  CaptureOldState();
  [[nodiscard]] Maybe<SkipTransitionReason> CaptureNewState();
  MOZ_CAN_RUN_SCRIPT void CaptureSubdocumentSnapshotIfNeeded();
  void ReleaseSubdocumentSnapshot();
  void SetupTransitionPseudoElements();
  [[nodiscard]] bool UpdatePseudoElementStyles(bool aNeedsInvalidation);
  void ClearNamedElements();
  void HandleFrame();
  bool CheckForActiveAnimations() const;
  void SkipTransition(SkipTransitionReason, JS::Handle<JS::Value>);
  void ClearTimeoutTimer();

  nsRect SnapshotContainingBlockRect() const;

  Maybe<uint64_t> GetElementIdentifier(Element* aElement) const;
  uint64_t EnsureElementIdentifier(Element* aElement);

  already_AddRefed<nsAtom> DocumentScopedTransitionNameFor(nsIFrame* aFrame);

  ~ViewTransition();

  // Stored for the whole lifetime of the object (until CC).
  RefPtr<Document> mDocument;
  RefPtr<ViewTransitionUpdateCallback> mUpdateCallback;

  // https://drafts.csswg.org/css-view-transitions/#viewtransition-named-elements
  ViewTransitionNamedElements mNamedElements;
  // mNamedElements is an unordered map, we need to keep the tree order. This
  // also keeps the strong reference to the view-transition-name which may be
  // auto-generated for this view transition.
  AutoTArray<RefPtr<nsAtom>, 8> mNames;

  using OldCaptureFramesArray =
      AutoTArray<std::pair<nsIFrame*, RefPtr<nsAtom>>, 32>;
  // Short lived array pointer used to mark old captures for DL building.
  OldCaptureFramesArray* mOldCaptureElements = nullptr;

  // See IsCapturingSubdocumentSnapshot.
  std::unique_ptr<OldSnapshotData> mSubdocumentSnapshot;
  bool mCapturingSubdocumentSnapshot = false;

  // The element identifier for the elements which need the auto-generated
  // view-transition-name. The lifetime of those element identifiers is
  // element’s node document’s active view transition.
  // Note: Use a non-owning element pointer because we never dereference it.
  // It is just a key to map an id. The size of this hashmap is fixed after we
  // capture the old state and new state,
  using ElementIdentifiers = nsTHashMap<Element*, uint64_t>;
  ElementIdentifiers mElementIdentifiers;

  // https://drafts.csswg.org/css-view-transitions/#viewtransition-initial-snapshot-containing-block-size
  nsSize mInitialSnapshotContainingBlockSize;

  // Allocated lazily, but same object once allocated (again until CC).
  RefPtr<Promise> mUpdateCallbackDonePromise;
  RefPtr<Promise> mReadyPromise;
  RefPtr<Promise> mFinishedPromise;

  TypeList mTypeList;
  RefPtr<ViewTransitionTypeSet> mTypes;

  static void TimeoutCallback(nsITimer*, void*);
  RefPtr<nsITimer> mTimeoutTimer;

  Phase mPhase = Phase::PendingCapture;
  // True for the outbound transition created on the old document during a
  // cross-document navigation.
  bool mIsOutboundCrossDocument = false;
  // The aProceed of BeginDeferredOutboundCapture. PerformPendingOperations
  // runs it after the capture.
  std::function<void(UniquePtr<ViewTransitionParams>&&)>
      mOutboundCaptureProceed;
  // The wrapper of the pseudo-elements tree, to make sure it is always
  // out-of-flow. This is the top-layer for rendering the view transition tree.
  // So in general, its child (and only one) is the transition root
  // pseudo-element.
  RefPtr<Element> mSnapshotContainingBlock;
};

}  // namespace dom
}  // namespace mozilla

#endif
