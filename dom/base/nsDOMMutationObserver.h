/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef nsDOMMutationObserver_h
#define nsDOMMutationObserver_h

#include <utility>

#include "mozilla/Attributes.h"
#include "mozilla/DoublyLinkedList.h"
#include "mozilla/dom/Document.h"
#include "mozilla/dom/MutationObserverBinding.h"
#include "mozilla/dom/NodeOwnedObjectList.h"
#include "mozilla/dom/Nullable.h"
#include "nsCOMArray.h"
#include "nsClassHashtable.h"
#include "nsCycleCollectionParticipant.h"
#include "nsGlobalWindowInner.h"
#include "nsIMutationObserver.h"
#include "nsPIDOMWindow.h"
#include "nsStubAnimationObserver.h"
#include "nsTArray.h"
#include "nsTHashMap.h"
#include "nsWrapperCache.h"

class nsDOMMutationObserver;
using mozilla::dom::MutationObservingInfo;

namespace mozilla::dom {
class Animation;
class Element;
class NodeList;
class SimpleContentList;
}  // namespace mozilla::dom

class nsDOMMutationRecord final : public nsISupports, public nsWrapperCache {
  ~nsDOMMutationRecord();

 public:
  using AnimationArray = nsTArray<RefPtr<mozilla::dom::Animation>>;
  using NodeList = mozilla::dom::NodeList;

  nsDOMMutationRecord(nsAtom* aType, nsISupports* aOwner);

  nsISupports* GetParentObject() const { return mOwner; }

  JSObject* WrapObject(JSContext* aCx,
                       JS::Handle<JSObject*> aGivenProto) override {
    return mozilla::dom::MutationRecord_Binding::Wrap(aCx, this, aGivenProto);
  }

  NS_DECL_CYCLE_COLLECTING_ISUPPORTS_FINAL
  NS_DECL_CYCLE_COLLECTION_WRAPPERCACHE_CLASS(nsDOMMutationRecord)

  void GetType(mozilla::dom::DOMString& aRetVal) const {
    aRetVal.SetKnownLiveAtom(mType, mozilla::dom::DOMString::eNullNotExpected);
  }

  nsINode* GetTarget() const { return mTarget; }

  NodeList* AddedNodes();

  NodeList* RemovedNodes();

  nsINode* GetPreviousSibling() const { return mPreviousSibling; }

  nsINode* GetNextSibling() const { return mNextSibling; }

  void GetAttributeName(mozilla::dom::DOMString& aRetVal) const {
    aRetVal.SetKnownLiveAtom(mAttrName,
                             mozilla::dom::DOMString::eTreatNullAsNull);
  }

  void GetAttributeNamespace(mozilla::dom::DOMString& aRetVal) const {
    aRetVal.SetKnownLiveString(mAttrNamespace);
  }

  void GetOldValue(mozilla::dom::DOMString& aRetVal) const {
    aRetVal.SetKnownLiveString(mPrevValue);
  }

  void GetAddedAnimations(AnimationArray&) const;
  void GetRemovedAnimations(AnimationArray&) const;
  void GetChangedAnimations(AnimationArray&) const;

  nsCOMPtr<nsINode> mTarget;
  RefPtr<nsAtom> mType;
  RefPtr<nsAtom> mAttrName;
  nsString mAttrNamespace;
  nsString mPrevValue;
  RefPtr<mozilla::dom::SimpleContentList> mAddedNodes;
  RefPtr<mozilla::dom::SimpleContentList> mRemovedNodes;
  nsCOMPtr<nsINode> mPreviousSibling;
  nsCOMPtr<nsINode> mNextSibling;
  AnimationArray mAddedAnimations;
  AnimationArray mRemovedAnimations;
  AnimationArray mChangedAnimations;

  RefPtr<nsDOMMutationRecord> mNext;
  nsCOMPtr<nsISupports> mOwner;
};

// Base class just prevents direct access to
// members to make sure we go through getters/setters.
class nsMutationReceiverBase : public nsStubAnimationObserver {
 public:
  virtual ~nsMutationReceiverBase() = default;

  nsDOMMutationObserver* Observer();
  nsINode* Target() { return mParent ? mParent->Target() : mTarget; }
  nsINode* RegisterTarget() { return mRegisterTarget; }

  bool Subtree() { return mParent ? mParent->Subtree() : mSubtree; }
  void SetSubtree(bool aSubtree) {
    NS_ASSERTION(!mParent, "Shouldn't have parent");
    mSubtree = aSubtree;
  }

  bool ChildList() { return mParent ? mParent->ChildList() : mChildList; }
  void SetChildList(bool aChildList) {
    NS_ASSERTION(!mParent, "Shouldn't have parent");
    mChildList = aChildList;
  }

  bool CharacterData() {
    return mParent ? mParent->CharacterData() : mCharacterData;
  }
  void SetCharacterData(bool aCharacterData) {
    NS_ASSERTION(!mParent, "Shouldn't have parent");
    mCharacterData = aCharacterData;
  }

  bool CharacterDataOldValue() const {
    return mParent ? mParent->CharacterDataOldValue() : mCharacterDataOldValue;
  }
  void SetCharacterDataOldValue(bool aOldValue) {
    NS_ASSERTION(!mParent, "Shouldn't have parent");
    mCharacterDataOldValue = aOldValue;
  }

  bool Attributes() const {
    return mParent ? mParent->Attributes() : mAttributes;
  }
  void SetAttributes(bool aAttributes) {
    NS_ASSERTION(!mParent, "Shouldn't have parent");
    mAttributes = aAttributes;
  }

  bool AllAttributes() const {
    return mParent ? mParent->AllAttributes() : mAllAttributes;
  }
  void SetAllAttributes(bool aAll) {
    NS_ASSERTION(!mParent, "Shouldn't have parent");
    mAllAttributes = aAll;
  }

  bool Animations() const {
    return mParent ? mParent->Animations() : mAnimations;
  }
  void SetAnimations(bool aAnimations) {
    NS_ASSERTION(!mParent, "Shouldn't have parent");
    mAnimations = aAnimations;
  }

  bool AttributeOldValue() const {
    return mParent ? mParent->AttributeOldValue() : mAttributeOldValue;
  }
  void SetAttributeOldValue(bool aOldValue) {
    NS_ASSERTION(!mParent, "Shouldn't have parent");
    mAttributeOldValue = aOldValue;
  }

  bool ChromeOnlyNodes() const {
    return mParent ? mParent->ChromeOnlyNodes() : mChromeOnlyNodes;
  }

  void SetChromeOnlyNodes(bool aChromeOnlyNodes) {
    NS_ASSERTION(!mParent, "Shouldn't have parent");
    mChromeOnlyNodes = aChromeOnlyNodes;
  }

  nsTArray<RefPtr<nsAtom>>& AttributeFilter() { return mAttributeFilter; }
  void SetAttributeFilter(nsTArray<RefPtr<nsAtom>>&& aFilter) {
    NS_ASSERTION(!mParent, "Shouldn't have parent");
    mAttributeFilter.Clear();
    mAttributeFilter = std::move(aFilter);
  }

  void AddClone(nsMutationReceiverBase* aClone) {
    mTransientReceivers.AppendObject(aClone);
  }

  void RemoveClone(nsMutationReceiverBase* aClone) {
    mTransientReceivers.RemoveObject(aClone);
  }

 protected:
  nsMutationReceiverBase(nsINode* aTarget, nsDOMMutationObserver* aObserver)
      : mTarget(aTarget),
        mObserver(aObserver),
        mRegisterTarget(aTarget),
        mSubtree(false),
        mChildList(false),
        mCharacterData(false),
        mCharacterDataOldValue(false),
        mAttributes(false),
        mAllAttributes(false),
        mAttributeOldValue(false),
        mAnimations(false) {}

  nsMutationReceiverBase(nsINode* aRegisterTarget,
                         nsMutationReceiverBase* aParent)
      : mTarget(nullptr),
        mObserver(nullptr),
        mParent(aParent),
        mRegisterTarget(aRegisterTarget),
        mKungFuDeathGrip(aParent->Target()),
        mSubtree(false),
        mChildList(false),
        mCharacterData(false),
        mCharacterDataOldValue(false),
        mAttributes(false),
        mAllAttributes(false),
        mAttributeOldValue(false),
        mAnimations(false),
        mChromeOnlyNodes(false) {
    NS_ASSERTION(mParent->Subtree(), "Should clone a non-subtree observer!");
  }

  virtual void AddMutationObserver() = 0;

  void AddObserver() {
    AddMutationObserver();
    mRegisterTarget->SetMayHaveDOMMutationObserver();
    mRegisterTarget->OwnerDoc()->SetMayHaveDOMMutationObservers();
  }

  bool IsObservable(nsIContent* aContent);

  bool ObservesAttr(nsINode* aRegisterTarget, mozilla::dom::Element* aElement,
                    int32_t aNameSpaceID, nsAtom* aAttr);

  // The target for the MutationObserver.observe() method.
  nsINode* mTarget;
  nsDOMMutationObserver* mObserver;
  RefPtr<nsMutationReceiverBase> mParent;  // Cleared after microtask.
  // The node to which Gecko-internal nsIMutationObserver was registered to.
  // This is different than mTarget when dealing with transient observers.
  nsINode* mRegisterTarget;
  nsCOMArray<nsMutationReceiverBase> mTransientReceivers;
  // While we have transient receivers, keep the original mutation receiver
  // alive so it doesn't go away and disconnect all its transient receivers.
  nsCOMPtr<nsINode> mKungFuDeathGrip;

 private:
  AutoTArray<RefPtr<nsAtom>, 2> mAttributeFilter;
  bool mSubtree : 1;
  bool mChildList : 1;
  bool mCharacterData : 1;
  bool mCharacterDataOldValue : 1;
  bool mAttributes : 1;
  bool mAllAttributes : 1;
  bool mAttributeOldValue : 1;
  bool mAnimations : 1;
  bool mChromeOnlyNodes : 1;
};

class nsMutationReceiver : public nsMutationReceiverBase {
 protected:
  ~nsMutationReceiver() { Disconnect(false); }

 public:
  static nsMutationReceiver* Create(nsINode* aTarget,
                                    nsDOMMutationObserver* aObserver) {
    nsMutationReceiver* r = new nsMutationReceiver(aTarget, aObserver);
    r->AddObserver();
    return r;
  }

  static nsMutationReceiver* Create(nsINode* aRegisterTarget,
                                    nsMutationReceiverBase* aParent) {
    nsMutationReceiver* r = new nsMutationReceiver(aRegisterTarget, aParent);
    aParent->AddClone(r);
    r->AddObserver();
    return r;
  }

  nsMutationReceiver* GetParent() {
    return static_cast<nsMutationReceiver*>(mParent.get());
  }

  void RemoveClones() {
    for (int32_t i = 0; i < mTransientReceivers.Count(); ++i) {
      nsMutationReceiver* r =
          static_cast<nsMutationReceiver*>(mTransientReceivers[i]);
      r->DisconnectTransientReceiver();
    }
    mTransientReceivers.Clear();
  }

  void DisconnectTransientReceiver() {
    if (mRegisterTarget) {
      mRegisterTarget->RemoveMutationObserver(this);
      mRegisterTarget = nullptr;
    }

    mParent = nullptr;
    NS_ASSERTION(!mTarget, "Should not have mTarget");
    NS_ASSERTION(!mObserver, "Should not have mObserver");
  }

  void Disconnect(bool aRemoveFromObserver);

  NS_DECL_ISUPPORTS

  NS_DECL_NSIMUTATIONOBSERVER_ATTRIBUTEWILLCHANGE
  NS_DECL_NSIMUTATIONOBSERVER_CHARACTERDATAWILLCHANGE
  NS_DECL_NSIMUTATIONOBSERVER_CONTENTAPPENDED
  NS_DECL_NSIMUTATIONOBSERVER_CONTENTINSERTED
  NS_DECL_NSIMUTATIONOBSERVER_CONTENTREMOVED
  NS_DECL_NSIMUTATIONOBSERVER_NODEWILLBEDESTROYED

  virtual void AttributeSetToCurrentValue(mozilla::dom::Element* aElement,
                                          int32_t aNameSpaceID,
                                          nsAtom* aAttribute) override {
    // We can reuse AttributeWillChange implementation.
    AttributeWillChange(aElement, aNameSpaceID, aAttribute,
                        AttrModType::Modification);
  }

 protected:
  nsMutationReceiver(nsINode* aTarget, nsDOMMutationObserver* aObserver);

  nsMutationReceiver(nsINode* aRegisterTarget, nsMutationReceiverBase* aParent)
      : nsMutationReceiverBase(aRegisterTarget, aParent) {
    NS_ASSERTION(!static_cast<nsMutationReceiver*>(aParent)->GetParent(),
                 "Shouldn't create deep observer hierarchies!");
  }

  virtual void AddMutationObserver() override {
    mRegisterTarget->AddMutationObserver(this);
  }
};

class nsAnimationReceiver : public nsMutationReceiver {
 public:
  static nsAnimationReceiver* Create(nsINode* aTarget,
                                     nsDOMMutationObserver* aObserver) {
    nsAnimationReceiver* r = new nsAnimationReceiver(aTarget, aObserver);
    r->AddObserver();
    return r;
  }

  static nsAnimationReceiver* Create(nsINode* aRegisterTarget,
                                     nsMutationReceiverBase* aParent) {
    nsAnimationReceiver* r = new nsAnimationReceiver(aRegisterTarget, aParent);
    aParent->AddClone(r);
    r->AddObserver();
    return r;
  }

  NS_DECL_ISUPPORTS_INHERITED

  NS_DECL_NSIANIMATIONOBSERVER_ANIMATIONADDED
  NS_DECL_NSIANIMATIONOBSERVER_ANIMATIONCHANGED
  NS_DECL_NSIANIMATIONOBSERVER_ANIMATIONREMOVED

 protected:
  virtual ~nsAnimationReceiver() = default;

  nsAnimationReceiver(nsINode* aTarget, nsDOMMutationObserver* aObserver)
      : nsMutationReceiver(aTarget, aObserver) {}

  nsAnimationReceiver(nsINode* aRegisterTarget, nsMutationReceiverBase* aParent)
      : nsMutationReceiver(aRegisterTarget, aParent) {}

  virtual void AddMutationObserver() override {
    mRegisterTarget->AddAnimationObserver(this);
  }

 private:
  enum AnimationMutation {
    eAnimationMutation_Added,
    eAnimationMutation_Changed,
    eAnimationMutation_Removed
  };

  void RecordAnimationMutation(mozilla::dom::Animation* aAnimation,
                               AnimationMutation aMutationType);
};

#define NS_DOM_MUTATION_OBSERVER_IID \
  {0x0c3b91f8, 0xcc3b, 0x4b08, {0x9e, 0xab, 0x07, 0x47, 0xa9, 0xe4, 0x65, 0xb4}}

class nsDOMMutationObserver final
    : public nsISupports,
      public nsWrapperCache,
      public mozilla::DoublyLinkedListElement<nsDOMMutationObserver> {
 public:
  nsDOMMutationObserver(nsCOMPtr<nsPIDOMWindowInner>&& aOwner,
                        mozilla::dom::MutationCallback& aCb)
      : mOwner(std::move(aOwner)),
        mLastPendingMutation(nullptr),
        mPendingMutationCount(0),
        mCallback(&aCb),
        mWaitingForRun(false),
        mMergeAttributeRecords(false),
        mId(++sCount) {
    mozilla::dom::NodeOwnedObjectList<nsDOMMutationObserver>::Add(this);
  }
  NS_DECL_CYCLE_COLLECTING_ISUPPORTS_FINAL
  NS_DECL_CYCLE_COLLECTION_SCRIPT_HOLDER_CLASS(nsDOMMutationObserver)
  NS_INLINE_DECL_STATIC_IID(NS_DOM_MUTATION_OBSERVER_IID)

  static already_AddRefed<nsDOMMutationObserver> Constructor(
      const mozilla::dom::GlobalObject&, mozilla::dom::MutationCallback&,
      mozilla::ErrorResult&);

  JSObject* WrapObject(JSContext* aCx,
                       JS::Handle<JSObject*> aGivenProto) override {
    return mozilla::dom::MutationObserver_Binding::Wrap(aCx, this, aGivenProto);
  }

  nsISupports* GetParentObject() const { return mOwner; }

  void Observe(nsINode& aTarget,
               const mozilla::dom::MutationObserverInit& aOptions,
               mozilla::ErrorResult& aRv);

  void Disconnect();

  void TakeRecords(nsTArray<RefPtr<nsDOMMutationRecord>>& aRetVal);

  MOZ_CAN_RUN_SCRIPT void HandleMutation();

  void GetObservingInfo(
      nsTArray<mozilla::dom::Nullable<MutationObservingInfo>>& aResult,
      mozilla::ErrorResult& aRv);

  mozilla::dom::MutationCallback* MutationCallback() { return mCallback; }

  bool HasOwnerInCCGenerationDocument() const;
  void MarkForCC();

  bool MergeAttributeRecords() { return mMergeAttributeRecords; }

  void SetMergeAttributeRecords(bool aVal) { mMergeAttributeRecords = aVal; }

  // If both records are for 'attributes' type and for the same target and
  // attribute name and namespace are the same, we can skip the newer record.
  // aOldRecord->mPrevValue holds the original value, if observed.
  bool MergeableAttributeRecord(nsDOMMutationRecord* aOldRecord,
                                nsDOMMutationRecord* aRecord);

  void AppendMutationRecord(already_AddRefed<nsDOMMutationRecord> aRecord) {
    RefPtr<nsDOMMutationRecord> record = aRecord;
    MOZ_ASSERT(record);
    if (!mLastPendingMutation) {
      MOZ_ASSERT(!mFirstPendingMutation);
      mFirstPendingMutation = std::move(record);
      mLastPendingMutation = mFirstPendingMutation;
    } else {
      MOZ_ASSERT(mFirstPendingMutation);
      mLastPendingMutation->mNext = std::move(record);
      mLastPendingMutation = mLastPendingMutation->mNext;
    }
    ++mPendingMutationCount;
  }

  void ClearPendingRecords() {
    // Break down the pending mutation record list so that cycle collector
    // can delete the objects sooner.
    RefPtr<nsDOMMutationRecord> current = std::move(mFirstPendingMutation);
    mLastPendingMutation = nullptr;
    mPendingMutationCount = 0;
    while (current) {
      current = std::move(current->mNext);
    }
  }

  // static methods
  static void QueueMutationObserverMicroTask();

  MOZ_CAN_RUN_SCRIPT
  static void HandleMutations(mozilla::AutoSlowOperation& aAso);

  static bool AllScheduledMutationObserversAreSuppressed() {
    if (sScheduledMutationObservers) {
      uint32_t len = sScheduledMutationObservers->Length();
      if (len > 0) {
        for (uint32_t i = 0; i < len; ++i) {
          if (!(*sScheduledMutationObservers)[i]->Suppressed()) {
            return false;
          }
        }
        return true;
      }
    }
    return false;
  }

  static void EnterMutationHandling();
  static void LeaveMutationHandling();

  static void Shutdown();

 protected:
  virtual ~nsDOMMutationObserver();

  friend class nsMutationReceiver;
  friend class nsAnimationReceiver;
  friend class nsAutoMutationBatch;
  friend class nsAutoAnimationMutationBatch;
  nsMutationReceiver* GetReceiverFor(nsINode* aNode, bool aMayCreate,
                                     bool aWantsAnimations);
  void RemoveReceiver(nsINode* aTarget, nsMutationReceiver* aReceiver);

  void GetAllSubtreeObserversFor(nsINode* aNode,
                                 nsTArray<nsMutationReceiver*>& aObservers);
  void ScheduleForRun();
  void RescheduleForRun();

  nsDOMMutationRecord* CurrentRecord(nsAtom* aType);
  bool HasCurrentRecord(const nsAString& aType);

  bool Suppressed() {
    return mOwner && nsGlobalWindowInner::Cast(mOwner)->IsInSyncOperation();
  }

  MOZ_CAN_RUN_SCRIPT
  static void HandleMutationsInternal(mozilla::AutoSlowOperation& aAso);

  static void AddCurrentlyHandlingObserver(nsDOMMutationObserver* aObserver,
                                           uint32_t aMutationLevel);

  nsCOMPtr<nsPIDOMWindowInner> mOwner;

  nsCOMArray<nsMutationReceiver> mReceivers;
  // Fast lookup index for mReceivers. Values are weak pointers; mReceivers
  // retains ownership.
  nsTHashMap<nsINode*, nsMutationReceiver*> mReceiverMap;
  nsClassHashtable<nsISupportsHashKey, nsCOMArray<nsMutationReceiver>>
      mTransientReceivers;
  // MutationRecords which are being constructed.
  AutoTArray<nsDOMMutationRecord*, 4> mCurrentMutations;
  // MutationRecords which will be handed to the callback at the end of
  // the microtask.
  RefPtr<nsDOMMutationRecord> mFirstPendingMutation;
  nsDOMMutationRecord* mLastPendingMutation;
  uint32_t mPendingMutationCount;

  RefPtr<mozilla::dom::MutationCallback> mCallback;

  bool mWaitingForRun;
  bool mMergeAttributeRecords;

  uint64_t mId;

  static uint64_t sCount;
  static AutoTArray<RefPtr<nsDOMMutationObserver>, 4>*
      sScheduledMutationObservers;

  static uint32_t sMutationLevel;
  static AutoTArray<AutoTArray<RefPtr<nsDOMMutationObserver>, 4>, 4>*
      sCurrentlyHandlingObservers;
};

class nsAutoMutationBatch {
 public:
  nsAutoMutationBatch()
      : mPreviousBatch(nullptr),
        mBatchTarget(nullptr),
        mRemovalDone(false),
        mFromFirstToLast(false),
        mAllowNestedBatches(false) {}

  nsAutoMutationBatch(nsINode* aTarget, bool aFromFirstToLast,
                      bool aAllowNestedBatches)
      : mPreviousBatch(nullptr),
        mBatchTarget(nullptr),
        mRemovalDone(false),
        mFromFirstToLast(false),
        mAllowNestedBatches(false) {
    Init(aTarget, aFromFirstToLast, aAllowNestedBatches);
  }

  void Init(nsINode* aTarget, bool aFromFirstToLast, bool aAllowNestedBatches) {
    if (aTarget && aTarget->OwnerDoc()->MayHaveDOMMutationObservers()) {
      if (sCurrentBatch && !sCurrentBatch->mAllowNestedBatches) {
        return;
      }
      mBatchTarget = aTarget;
      mFromFirstToLast = aFromFirstToLast;
      mAllowNestedBatches = aAllowNestedBatches;
      mPreviousBatch = sCurrentBatch;
      sCurrentBatch = this;
      nsDOMMutationObserver::EnterMutationHandling();
    }
  }

  void RemovalDone() { mRemovalDone = true; }
  static bool IsRemovalDone() { return sCurrentBatch->mRemovalDone; }

  void SetPrevSibling(nsINode* aNode) { mPrevSibling = aNode; }
  void SetNextSibling(nsINode* aNode) { mNextSibling = aNode; }

  void Done();

  ~nsAutoMutationBatch() { NodesAdded(); }

  static bool IsBatching() { return !!sCurrentBatch; }

  static nsAutoMutationBatch* GetCurrentBatch() { return sCurrentBatch; }

  static void UpdateObserver(nsDOMMutationObserver* aObserver,
                             bool aWantsChildList) {
    uint32_t l = sCurrentBatch->mObservers.Length();
    for (uint32_t i = 0; i < l; ++i) {
      if (sCurrentBatch->mObservers[i].mObserver == aObserver) {
        if (aWantsChildList) {
          sCurrentBatch->mObservers[i].mWantsChildList = aWantsChildList;
        }
        return;
      }
    }
    BatchObserver* bo = sCurrentBatch->mObservers.AppendElement();
    bo->mObserver = aObserver;
    bo->mWantsChildList = aWantsChildList;
  }

  static nsINode* GetBatchTarget() { return sCurrentBatch->mBatchTarget; }

  // Mutation receivers notify the batch about removed child nodes.
  static void NodeRemoved(nsIContent* aChild) {
    if (IsBatching() && !sCurrentBatch->mRemovalDone) {
      uint32_t len = sCurrentBatch->mRemovedNodes.Length();
      if (!len || sCurrentBatch->mRemovedNodes[len - 1] != aChild) {
        sCurrentBatch->mRemovedNodes.AppendElement(aChild);
      }
    }
  }

  // Called after new child nodes have been added to the batch target.
  void NodesAdded() {
    if (sCurrentBatch != this) {
      return;
    }

    nsIContent* c = mPrevSibling ? mPrevSibling->GetNextSibling()
                                 : mBatchTarget->GetFirstChild();
    for (; c != mNextSibling; c = c->GetNextSibling()) {
      mAddedNodes.AppendElement(c);
    }
    Done();
  }

 private:
  struct BatchObserver {
    nsDOMMutationObserver* mObserver;
    bool mWantsChildList;
  };

  static nsAutoMutationBatch* sCurrentBatch;
  nsAutoMutationBatch* mPreviousBatch;
  AutoTArray<BatchObserver, 2> mObservers;
  nsTArray<nsCOMPtr<nsIContent>> mRemovedNodes;
  nsTArray<nsCOMPtr<nsIContent>> mAddedNodes;
  nsINode* mBatchTarget;
  bool mRemovalDone;
  bool mFromFirstToLast;
  bool mAllowNestedBatches;
  nsCOMPtr<nsINode> mPrevSibling;
  nsCOMPtr<nsINode> mNextSibling;
};

class nsAutoAnimationMutationBatch {
  struct Entry;

 public:
  explicit nsAutoAnimationMutationBatch(mozilla::dom::Document* aDocument);

  void Init(mozilla::dom::Document* aDocument);

  ~nsAutoAnimationMutationBatch();

  void Done();

  static bool IsBatching() { return !!sCurrentBatch; }

  static nsAutoAnimationMutationBatch* GetCurrentBatch() {
    return sCurrentBatch;
  }

  static void AddObserver(nsDOMMutationObserver* aObserver) {
    if (sCurrentBatch->mObservers.Contains(aObserver)) {
      return;
    }
    sCurrentBatch->mObservers.AppendElement(aObserver);
  }

  static void AnimationAdded(mozilla::dom::Animation* aAnimation,
                             nsINode* aTarget);
  static void AnimationChanged(mozilla::dom::Animation* aAnimation,
                               nsINode* aTarget);
  static void AnimationRemoved(mozilla::dom::Animation* aAnimation,
                               nsINode* aTarget);

 private:
  Entry* FindEntry(mozilla::dom::Animation* aAnimation, nsINode* aTarget);
  Entry* AddEntry(mozilla::dom::Animation* aAnimation, nsINode* aTarget);

  enum State {
    eState_RemainedPresent,
    eState_RemainedAbsent,
    eState_Added,
    eState_Removed
  };

  struct Entry {
    RefPtr<mozilla::dom::Animation> mAnimation;
    State mState;
    bool mChanged;
  };

  static nsAutoAnimationMutationBatch* sCurrentBatch;
  AutoTArray<RefPtr<nsDOMMutationObserver>, 2> mObservers;
  using EntryArray = nsTArray<Entry>;
  nsClassHashtable<nsPtrHashKey<nsINode>, EntryArray> mEntryTable;
  // List of nodes referred to by mEntryTable so we can sort them
  // For a specific pseudo element, we use its parent element as the
  // batch target, so they will be put in the same EntryArray.
  // Note: the batch object is used in lots of Animation APIs, and the script
  // may be in the scope of the batch via ready/finished promises, so we use the
  // strong refs here.
  nsTArray<nsCOMPtr<nsINode>> mBatchTargets;
};

inline nsDOMMutationObserver* nsMutationReceiverBase::Observer() {
  return mParent ? mParent->Observer()
                 : static_cast<nsDOMMutationObserver*>(mObserver);
}

class MOZ_RAII nsDOMMutationEnterLeave {
 public:
  explicit nsDOMMutationEnterLeave(mozilla::dom::Document* aDoc)
      : mNeeded(aDoc->MayHaveDOMMutationObservers()) {
    if (mNeeded) {
      nsDOMMutationObserver::EnterMutationHandling();
    }
  }
  ~nsDOMMutationEnterLeave() {
    if (mNeeded) {
      nsDOMMutationObserver::LeaveMutationHandling();
    }
  }

 private:
  const bool mNeeded;
};

#endif
