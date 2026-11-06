/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/. */

#ifndef mozilla_dom_SpeculationRules_h
#define mozilla_dom_SpeculationRules_h

#include "mozilla/UniquePtr.h"
#include "mozilla/dom/SpeculationRuleSet.h"
#include "mozilla/dom/speculationrules_ffi_generated.h"
#include "nsCOMPtr.h"
#include "nsClassHashtable.h"
#include "nsCycleCollectionParticipant.h"
#include "nsHashKeys.h"
#include "nsStringFwd.h"
#include "nsTArrayForwardDeclare.h"
#include "nsTHashSet.h"

class nsIContent;
class nsIScriptElement;
class nsITimer;
class nsIURI;

namespace mozilla {
class ManagedPostRefreshObserver;
}

namespace mozilla::dom {

class Document;
class Element;
class SpeculationRuleSet;

class SpeculationRules final {
 public:
  NS_INLINE_DECL_CYCLE_COLLECTING_NATIVE_REFCOUNTING(SpeculationRules)
  NS_DECL_CYCLE_COLLECTION_NATIVE_CLASS(SpeculationRules)

  explicit SpeculationRules(Document* aDocument);

  void RegisterFromHeader(UniquePtr<SpeculationRuleSet> aRuleSet);
  void RegisterFromScript(nsIScriptElement* aScriptElement,
                          UniquePtr<SpeculationRuleSet> aRuleSet);
  void Unregister(nsIScriptElement* aScriptElement);

  void ConsiderLoads();
  void InnerConsiderLoads();

  void AddLink(Element* aElement);
  void RemoveLink(Element* aElement);

  void FindMatchingLinks(nsTArray<const Element*>& aLinks);

  void HoverContentChanged(nsIContent* aContent);
  void PointerDown(Element* aContent);

  void ProcessSpeculationRulesHeader(const nsACString& aHeader);

 private:
  virtual ~SpeculationRules();

  // The innermost inclusive flat tree ancestor of aContent that is a link this
  // document is tracking, or nullptr if there is none.
  Element* FindInterestedLink(nsIContent* aContent) const;

  // Document rules only match links that are being rendered, so if frame
  // construction is pending, rather than forcing a flush this postpones
  // InnerConsiderLoads until after the first refresh at which no frame
  // construction is pending. Returns true if it was postponed.
  bool WaitForPendingFrames();

  bool IsFrameConstructionPending() const;

  // https://html.spec.whatwg.org/#inner-consider-speculative-loads-steps
  // Step 7, for those candidate groups that the user's behaviour has shown to
  // be eager enough. A group is enacted if it is at least as eager as
  // aTriggerLevel and, when aURL is non-null, is for that URL. Only the least
  // eager qualifying group per URL is enacted, as it is the one whose tags were
  // collected from every candidate the user's behaviour justifies.
  void EnactCandidates(nsIURI* aURL, Eagerness aTriggerLevel);

  // Arms the hover timer to enact candidates at least as eager as aLevel for
  // mHoverLink once aDelayMs has passed.
  void ArmHoverTimer(uint32_t aDelayMs, Eagerness aLevel);
  void CancelHoverTimer();
  static void HoverTimerFired(nsITimer* aTimer, void* aClosure);

  bool HasRuleSets() const;

  RefPtr<Document> mDocument;

  // https://html.spec.whatwg.org/#document-sr-sets
  nsTArray<UniquePtr<SpeculationRuleSet>> mRuleSetsFromHeader;
  nsClassHashtable<nsRefPtrHashKey<nsIScriptElement>, SpeculationRuleSet>
      mRuleSetsFromScript;

  // https://html.spec.whatwg.org/#consider-speculative-loads-microtask-queued
  bool mConsiderSpeculativeLoadsMicrotaskQueued{false};

  // Set while InnerConsiderLoads is postponed until frame construction is no
  // longer pending.
  RefPtr<ManagedPostRefreshObserver> mPendingFramesObserver;

  // The set of HTML <a> and <area> elements with an href attribute that are
  // connected to this document. This is tracked so FindMatchingLinks doesn't
  // have to walk the document tree every time speculative loads are considered.
  // These are non-owning pointers; the elements should remove themselves when
  // they are unbound from the document or lose their href attribute.
  nsTHashSet<Element*> mLinks;

  // https://html.spec.whatwg.org/#speculative-load-candidate-group
  // The representative candidate of each group produced by the last run of
  // InnerConsiderLoads. Immediate groups have already been enacted; the rest
  // are kept so a later signal of user interest can enact them.
  nsTArray<PrefetchCandidate> mCandidateGroups;

  // The link the hover timer is waiting on. Owning, as nothing else keeps the
  // element alive for the duration of the timer.
  RefPtr<Element> mHoverLink;
  nsCOMPtr<nsITimer> mHoverTimer;
  // The eagerness level the pending hover timer will enact. When the eager
  // delay is the shorter one, the timer first fires for "eager" candidates and
  // then re-arms for "moderate" ones.
  Eagerness mHoverTimerLevel{Eagerness::Eager};
};

}  // namespace mozilla::dom

#endif  // mozilla_dom_SpeculationRules_h
