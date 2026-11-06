/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/* implements DOM interface for querying and observing media queries */

#ifndef mozilla_dom_MediaQueryList_h
#define mozilla_dom_MediaQueryList_h

#include "mozilla/DOMEventTargetHelper.h"
#include "mozilla/LinkedList.h"
#include "mozilla/dom/MediaQueryListBinding.h"
#include "nsCOMPtr.h"
#include "nsCycleCollectionParticipant.h"
#include "nsISupports.h"
#include "nsTArray.h"
#include "nsWrapperCache.h"

namespace mozilla::dom {

class MediaList;

class MediaQueryList final : public DOMEventTargetHelper,
                             public LinkedListElement<MediaQueryList> {
 public:
  // The caller who constructs is responsible for calling Evaluate
  // before calling any other methods.
  MediaQueryList(Document* aDocument, const nsACString& aMediaQueryList,
                 CallerType);

 private:
  ~MediaQueryList();

 public:
  NS_DECL_ISUPPORTS_INHERITED
  NS_DECL_CYCLE_COLLECTION_CLASS_INHERITED(MediaQueryList, DOMEventTargetHelper)

  nsISupports* GetParentObject() const;

  void MediaFeatureValuesChanged();

  // Returns whether we need to notify of the change by dispatching a change
  // event.
  [[nodiscard]] bool EvaluateOnRenderingUpdate();
  void FireChangeEvent();

  JSObject* WrapObject(JSContext* aCx,
                       JS::Handle<JSObject*> aGivenProto) override;

  // WebIDL methods
  void GetMedia(nsACString& aMedia) const;
  bool Matches();
  void AddListener(EventListener* aListener);
  void RemoveListener(EventListener* aListener);

  IMPL_EVENT_HANDLER(change)

  bool HasListeners() const;

  void Disconnect();

  size_t SizeOfExcludingThis(MallocSizeOf aMallocSizeOf) const;

 private:
  void LastRelease() final {
    auto* listElement = static_cast<LinkedListElement<MediaQueryList>*>(this);
    if (listElement->isInList()) {
      listElement->remove();
    }
  }

  // We only need a pointer to the document to support lazy
  // reevaluation following dynamic changes.  However, this lazy
  // reevaluation is perhaps somewhat important, since some usage
  // patterns may involve the creation of large numbers of
  // MediaQueryList objects which almost immediately become garbage
  // (after a single call to the .matches getter).
  //
  // This pointer does make us a little more dependent on cycle
  // collection.
  //
  // We have a non-null mDocument for our entire lifetime except
  // after cycle collection unlinking.  Having a non-null mDocument
  // is equivalent to being in that document's mDOMMediaQueryLists
  // linked list.
  RefPtr<Document> mDocument;
  const RefPtr<const MediaList> mMediaList;
  // Whether our MediaList depends on our viewport size. Our medialist is
  // immutable, so we can just compute this once and carry on with our lives.
  const bool mViewportDependent : 1;
  // The matches state.
  // https://drafts.csswg.org/cssom-view/#mediaquerylist-matches-state
  bool mMatches : 1;
  // The value of the matches state on creation, or on the last rendering
  // update, in order to implement:
  // https://drafts.csswg.org/cssom-view/#evaluate-media-queries-and-report-changes
  bool mMatchesOnRenderingUpdate : 1;
};

}  // namespace mozilla::dom

#endif /* !defined(mozilla_dom_MediaQueryList_h) */
