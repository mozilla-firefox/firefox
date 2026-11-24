/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "nsNavHistory.h"
#include "nsNavBookmarks.h"
#include "nsFaviconService.h"
#include "nsDebug.h"
#include "nsNetUtil.h"
#include "nsString.h"
#include "nsReadableUtils.h"
#include "prtime.h"
#include "mozIStorageRow.h"
#include "mozIStorageResultSet.h"
#include "nsQueryObject.h"
#include "nsCharSeparatedTokenizer.h"
#include "mozilla/dom/PlacesObservers.h"
#include "mozilla/dom/PlacesVisit.h"
#include "mozilla/dom/PlacesVisitRemoved.h"
#include "mozilla/dom/PlacesVisitTitle.h"
#include "mozilla/dom/PlacesBookmarkAddition.h"
#include "mozilla/dom/PlacesBookmarkRemoved.h"
#include "mozilla/dom/PlacesBookmarkMoved.h"
#include "mozilla/dom/PlacesBookmarkKeyword.h"
#include "mozilla/dom/PlacesBookmarkTags.h"
#include "mozilla/dom/PlacesBookmarkTime.h"
#include "mozilla/dom/PlacesBookmarkTitle.h"
#include "mozilla/dom/PlacesBookmarkUrl.h"
#include "mozilla/dom/PlacesFavicon.h"
#include "mozilla/intl/AppCollator.h"

#include "nsCycleCollectionParticipant.h"

// Thanks, Windows.h :(
#undef CompareString

#define TO_ICONTAINER(_node) \
  static_cast<nsINavHistoryContainerResultNode*>(_node)

#define TO_CONTAINER(_node) static_cast<nsNavHistoryContainerResultNode*>(_node)

#define NOTIFY_RESULT_OBSERVERS_RET(_result, _method, _ret)               \
  PR_BEGIN_MACRO                                                          \
  NS_ENSURE_TRUE(_result, _ret);                                          \
  if (!_result->mSuppressNotifications) {                                 \
    ENUMERATE_WEAKARRAY(_result->mObservers, nsINavHistoryResultObserver, \
                        _method)                                          \
  }                                                                       \
  PR_END_MACRO

#define NOTIFY_RESULT_OBSERVERS(_result, _method) \
  NOTIFY_RESULT_OBSERVERS_RET(_result, _method, NS_ERROR_UNEXPECTED)

// What we want is: NS_INTERFACE_MAP_ENTRY(self) for static IID accessors,
// but some of our classes (like nsNavHistoryResult) have an ambiguous base
// class of nsISupports which prevents this from working (the default macro
// converts it to nsISupports, then addrefs it, then returns it). Therefore, we
// expand the macro here and change it so that it works. Yuck.
#define NS_INTERFACE_MAP_STATIC_AMBIGUOUS(_class) \
  if (aIID.Equals(NS_GET_IID(_class))) {          \
    NS_ADDREF(this);                              \
    *aInstancePtr = this;                         \
    return NS_OK;                                 \
  } else

// Number of changes to handle separately in a batch.  If more changes are
// requested the node will switch to full refresh mode.
#define MAX_BATCH_CHANGES_BEFORE_REFRESH 5

// Number of Page_removed events to handle by simply refreshing all containers,
// because doing so is much faster than incremental updates.
#define MAX_PAGE_REMOVES_BEFORE_REFRESH 10

using namespace mozilla;
using namespace mozilla::dom;
using namespace mozilla::places;

namespace {

/**
 * Returns conditions for query update.
 *  QUERYUPDATE_TIME:
 *    This query is only limited by an inclusive time range on the first
 *    query object. The caller can quickly evaluate the time itself if it
 *    chooses. This is even simpler than "simple" below.
 *  QUERYUPDATE_SIMPLE:
 *    This query is evaluatable using evaluateQueryForNode to do live
 *    updating.
 *  QUERYUPDATE_COMPLEX:
 *    This query is not evaluatable using evaluateQueryForNode. When something
 *    happens that this query updates, you will need to re-run the query.
 *  QUERYUPDATE_COMPLEX_WITH_BOOKMARKS:
 *    A complex query that additionally has dependence on bookmarks. All
 *    bookmark-dependent queries fall under this category.
 *  QUERYUPDATE_MOBILEPREF:
 *    A complex query but only updates when the mobile preference changes.
 *  QUERYUPDATE_NONE:
 *    A query that never updates, e.g. the left-pane root query.
 *
 *  aHasSearchTerms will be set to true if the query has any dependence on
 *  keywords. When there is no dependence on keywords, we can handle title
 *  change operations as simple instead of complex.
 */
uint32_t getUpdateRequirements(const RefPtr<nsNavHistoryQuery>& aQuery,
                               const RefPtr<nsNavHistoryQueryOptions>& aOptions,
                               bool* aHasSearchTerms) {
  // first check if there are search terms
  bool hasSearchTerms = *aHasSearchTerms = !aQuery->SearchTerms().IsEmpty();

  bool nonTimeBasedItems = false;
  bool domainBasedItems = false;

  if (aQuery->Parents().Length() > 0 || aQuery->Tags().Length() > 0 ||
      (aOptions->QueryType() ==
           nsINavHistoryQueryOptions::QUERY_TYPE_BOOKMARKS &&
       hasSearchTerms)) {
    return QUERYUPDATE_COMPLEX_WITH_BOOKMARKS;
  }

  // Note: we don't currently have any complex non-bookmarked items, but these
  // are expected to be added. Put detection of these items here.
  if (hasSearchTerms || !aQuery->Domain().IsVoid() ||
      aQuery->Uri() != nullptr) {
    nonTimeBasedItems = true;
  }

  if (!aQuery->Domain().IsVoid()) {
    domainBasedItems = true;
  }

  if (aOptions->ResultType() ==
      nsINavHistoryQueryOptions::RESULTS_AS_TAGS_ROOT) {
    return QUERYUPDATE_COMPLEX_WITH_BOOKMARKS;
  }

  if (aOptions->ResultType() ==
      nsINavHistoryQueryOptions::RESULTS_AS_ROOTS_QUERY) {
    return QUERYUPDATE_MOBILEPREF;
  }

  if (aOptions->ResultType() ==
      nsINavHistoryQueryOptions::RESULTS_AS_LEFT_PANE_QUERY) {
    return QUERYUPDATE_NONE;
  }

  // Whenever there is a maximum number of results,
  // and we are not a bookmark query we must requery. This
  // is because we can't generally know if any given addition/change causes
  // the item to be in the top N items in the database.
  uint16_t sortingMode = aOptions->SortingMode();
  if (aOptions->MaxResults() > 0 &&
      sortingMode != nsINavHistoryQueryOptions::SORT_BY_DATE_ASCENDING &&
      sortingMode != nsINavHistoryQueryOptions::SORT_BY_DATE_DESCENDING) {
    return QUERYUPDATE_COMPLEX;
  }

  if (domainBasedItems) return QUERYUPDATE_HOST;
  if (!nonTimeBasedItems) return QUERYUPDATE_TIME;

  return QUERYUPDATE_SIMPLE;
}

/**
 * We might have interesting encodings and different case in the host name.
 * This will convert that host name into an ASCII host name by sending it
 * through the URI canonicalization. The result can be used for comparison
 * with other ASCII host name strings.
 */
nsresult asciiHostNameFromHostString(const nsACString& aHostName,
                                     nsACString& aAscii) {
  aAscii.Truncate();
  if (aHostName.IsEmpty()) {
    return NS_OK;
  }
  // To properly generate a uri we must provide a protocol.
  nsAutoCString fakeURL("http://");
  fakeURL.Append(aHostName);
  nsCOMPtr<nsIURI> uri;
  nsresult rv = NS_NewURI(getter_AddRefs(uri), fakeURL);
  NS_ENSURE_SUCCESS(rv, rv);
  rv = uri->GetAsciiHost(aAscii);
  NS_ENSURE_SUCCESS(rv, rv);
  return NS_OK;
}

bool isQueryMatchingVisitDetails(
    const RefPtr<nsNavHistoryQuery>& query,
    const RefPtr<nsNavHistoryQueryOptions>& options, bool hidden,
    PRTime visitTime, uint32_t transition, nsIURI* uri) {
  if (hidden && !options->IncludeHidden()) {
    return false;
  }

  bool hasIt;
  if (NS_SUCCEEDED(query->GetHasBeginTime(&hasIt)) && hasIt) {
    PRTime beginTime = nsNavHistory::NormalizeTime(query->BeginTimeReference(),
                                                   query->BeginTime());
    if (visitTime < beginTime) {
      return false;
    }
  }
  if (NS_SUCCEEDED(query->GetHasEndTime(&hasIt)) && hasIt) {
    PRTime endTime = nsNavHistory::NormalizeTime(query->EndTimeReference(),
                                                 query->EndTime());
    if (visitTime > endTime) {
      return false;
    }
  }

  const nsTArray<uint32_t>& transitions = query->Transitions();
  if (transition > 0 && transitions.Length() &&
      !transitions.Contains(transition)) {
    return false;
  }

  if (!query->Domain().IsVoid()) {
    nsAutoCString asciiRequest;
    if (NS_FAILED(asciiHostNameFromHostString(query->Domain(), asciiRequest))) {
      return false;
    }
    if (query->DomainIsHost()) {
      // Exact domain match.
      nsAutoCString host;
      if (NS_FAILED(uri->GetAsciiHost(host)) || !asciiRequest.Equals(host)) {
        return false;
      }
    } else {
      // Wildcard domain match, subdomains are included.
      nsNavHistory* history = nsNavHistory::GetHistoryService();
      if (history) {
        nsAutoCString domain;
        history->DomainNameFromURI(uri, domain);
        if (!asciiRequest.Equals(domain)) {
          return false;
        }
      }
    }
  }

  if (query->Uri()) {
    bool equals;
    if (NS_FAILED(query->Uri()->Equals(uri, &equals)) || !equals) {
      return false;
    }
  }

  return true;
}

inline bool isTimeFilteredQuery(const RefPtr<nsNavHistoryQuery>& query) {
  bool hasIt;
  return (NS_SUCCEEDED(query->GetHasBeginTime(&hasIt)) && hasIt) ||
         (NS_SUCCEEDED(query->GetHasEndTime(&hasIt)) && hasIt);
}

inline bool caseInsensitiveFind(const nsACString& aSearchTerms,
                                const nsACString& aTarget) {
  nsACString::const_iterator start, end;
  aTarget.BeginReading(start);
  aTarget.EndReading(end);
  return CaseInsensitiveFindInReadable(aSearchTerms, start, end);
}

bool isQuerySearchTermsMatching(const RefPtr<nsNavHistoryQuery>& aQuery,
                                const nsACString& aURI,
                                const nsACString& aTitle,
                                const nsAString& aTags) {
  nsAutoCString searchTerms = NS_ConvertUTF16toUTF8(aQuery->SearchTerms());
  if ((!aTitle.IsEmpty() && caseInsensitiveFind(searchTerms, aTitle)) ||
      (!aURI.IsEmpty() && caseInsensitiveFind(searchTerms, aURI))) {
    return true;
  }

  if (aTags.IsEmpty()) {
    return false;
  }
  for (const nsAString& tag : aTags.Split(',')) {
    if (caseInsensitiveFind(searchTerms, NS_ConvertUTF16toUTF8(tag))) {
      return true;
    }
  }
  return false;
}

bool isQuerySearchTermsMatching(const RefPtr<nsNavHistoryQuery>& aQuery,
                                const RefPtr<nsNavHistoryResultNode>& aNode) {
  return isQuerySearchTermsMatching(aQuery, aNode->mURI, aNode->mTitle,
                                    aNode->mTags);
}

// Emulate string comparison (used for sorting) for PRTime and int.
inline int32_t ComparePRTime(PRTime a, PRTime b) {
  if (a == b) {
    return 0;
  }
  return a < b ? -1 : 1;
}
inline int32_t CompareIntegers(uint32_t a, uint32_t b) {
  // These are unlikely to overflow, so just cast for now.
  return static_cast<int32_t>(a) - static_cast<int32_t>(b);
}

}  // anonymous namespace

NS_IMPL_CYCLE_COLLECTION(nsNavHistoryResultNode, mParent)

NS_INTERFACE_MAP_BEGIN_CYCLE_COLLECTION(nsNavHistoryResultNode)
  NS_INTERFACE_MAP_ENTRY_AMBIGUOUS(nsISupports, nsINavHistoryResultNode)
  NS_INTERFACE_MAP_ENTRY(nsINavHistoryResultNode)
NS_INTERFACE_MAP_END

NS_IMPL_CYCLE_COLLECTING_ADDREF(nsNavHistoryResultNode)
NS_IMPL_CYCLE_COLLECTING_RELEASE(nsNavHistoryResultNode)

nsNavHistoryResultNode::nsNavHistoryResultNode(const nsACString& aURI,
                                               const nsACString& aTitle,
                                               uint32_t aAccessCount,
                                               PRTime aTime)
    : mParent(nullptr),
      mURI(aURI),
      mTitle(aTitle),
      mAccessCount(aAccessCount),
      mTime(aTime),
      mBookmarkIndex(-1),
      mItemId(-1),
      mVisitId(-1),
      mDateAdded(0),
      mLastModified(0),
      mIndentLevel(-1),
      mFrecency(0),
      mHidden(false),
      mTransitionType(0) {
  mTags.SetIsVoid(true);
}

NS_IMETHODIMP
nsNavHistoryResultNode::GetIcon(nsACString& aIcon) {
  if (this->IsContainer() || mURI.IsEmpty()) {
    return NS_OK;
  }

  aIcon.AppendLiteral("page-icon:");
  aIcon.Append(mURI);

  return NS_OK;
}

NS_IMETHODIMP
nsNavHistoryResultNode::GetParent(nsINavHistoryContainerResultNode** aParent) {
  NS_IF_ADDREF(*aParent = mParent);
  return NS_OK;
}

NS_IMETHODIMP
nsNavHistoryResultNode::GetParentResult(nsINavHistoryResult** aResult) {
  *aResult = nullptr;
  if (IsContainer()) {
    NS_IF_ADDREF(*aResult = GetAsContainer()->mResult);
  } else if (mParent) {
    NS_IF_ADDREF(*aResult = mParent->mResult);
  }

  NS_ENSURE_STATE(*aResult);
  return NS_OK;
}

void nsNavHistoryResultNode::SetTags(const nsAString& aTags) {
  if (aTags.IsVoid()) {
    mTags.SetIsVoid(true);
    return;
  }

  mTags.Assign(aTags);
}

NS_IMETHODIMP
nsNavHistoryResultNode::GetTags(nsAString& aTags) {
  if (mTags.IsVoid()) {
    aTags.SetIsVoid(true);
    return NS_OK;
  }

  aTags.Assign(mTags);
  return NS_OK;
}

NS_IMETHODIMP
nsNavHistoryResultNode::GetPageGuid(nsACString& aPageGuid) {
  aPageGuid = mPageGuid;
  return NS_OK;
}

NS_IMETHODIMP
nsNavHistoryResultNode::GetBookmarkGuid(nsACString& aBookmarkGuid) {
  aBookmarkGuid = mBookmarkGuid;
  return NS_OK;
}

NS_IMETHODIMP
nsNavHistoryResultNode::GetVisitId(int64_t* aVisitId) {
  *aVisitId = mVisitId;
  return NS_OK;
}

NS_IMETHODIMP
nsNavHistoryResultNode::GetVisitType(uint32_t* aVisitType) {
  *aVisitType = mTransitionType;
  return NS_OK;
}

void nsNavHistoryResultNode::OnRemoving() { mParent = nullptr; }

/**
 * This will find the result for this node.  We can ask the nearest container
 * for this value (either ourselves or our parents should be a container,
 * and all containers have result pointers).
 *
 * @note The result may be null, if the container is detached from the result
 *       who owns it.
 */
nsNavHistoryResult* nsNavHistoryResultNode::GetResult() {
  nsNavHistoryResultNode* node = this;
  do {
    if (node->IsContainer()) {
      nsNavHistoryContainerResultNode* container = TO_CONTAINER(node);
      return container->mResult;
    }
    node = node->mParent;
  } while (node);
  MOZ_ASSERT(false, "No container node found in hierarchy!");
  return nullptr;
}

NS_IMPL_CYCLE_COLLECTION_INHERITED(nsNavHistoryContainerResultNode,
                                   nsNavHistoryResultNode, mResult, mChildren)

NS_IMPL_ADDREF_INHERITED(nsNavHistoryContainerResultNode,
                         nsNavHistoryResultNode)
NS_IMPL_RELEASE_INHERITED(nsNavHistoryContainerResultNode,
                          nsNavHistoryResultNode)

NS_INTERFACE_MAP_BEGIN_CYCLE_COLLECTION(nsNavHistoryContainerResultNode)
  NS_INTERFACE_MAP_STATIC_AMBIGUOUS(nsNavHistoryContainerResultNode)
  NS_INTERFACE_MAP_ENTRY(nsINavHistoryContainerResultNode)
NS_INTERFACE_MAP_END_INHERITING(nsNavHistoryResultNode)

nsNavHistoryContainerResultNode::nsNavHistoryContainerResultNode(
    const nsACString& aURI, const nsACString& aTitle, PRTime aTime,
    uint32_t aContainerType, nsNavHistoryQueryOptions* aOptions)
    : nsNavHistoryResultNode(aURI, aTitle, 0, aTime),
      mResult(nullptr),
      mContainerType(aContainerType),
      mExpanded(false),
      mOptions(aOptions),
      mAsyncCanceledState(NOT_CANCELED) {
  MOZ_ASSERT(mOptions);
  MOZ_ALWAYS_SUCCEEDS(mOptions->Clone(getter_AddRefs(mOriginalOptions)));
}

nsNavHistoryContainerResultNode::~nsNavHistoryContainerResultNode() {
  // Explicitly clean up array of children of this container.  We must ensure
  // all references are gone and all of their destructors are called.
  mChildren.Clear();
}

/**
 * Containers should notify their children that they are being removed when the
 * container is being removed.
 */
void nsNavHistoryContainerResultNode::OnRemoving() {
  nsNavHistoryResultNode::OnRemoving();
  for (nsNavHistoryResultNode* child : mChildren) {
    child->OnRemoving();
  }
  mChildren.Clear();
  mResult = nullptr;
}

bool nsNavHistoryContainerResultNode::AreChildrenVisible() {
  nsNavHistoryResult* result = GetResult();
  if (!result) {
    MOZ_ASSERT_UNREACHABLE("Invalid result");
    return false;
  }

  if (!mExpanded) return false;

  // Now check if any ancestor is closed.
  nsNavHistoryContainerResultNode* ancestor = mParent;
  while (ancestor) {
    if (!ancestor->mExpanded) return false;

    ancestor = ancestor->mParent;
  }

  return true;
}

nsresult nsNavHistoryContainerResultNode::OnVisitsRemoved(nsIURI* aURI) {
  if (!AreChildrenVisible()) {
    return NS_OK;
  }

  nsNavHistoryResult* result = GetResult();
  NS_ENSURE_STATE(result);
  if (result->CanSkipHistoryDetailsNotifications()) {
    return NS_OK;
  }

  nsAutoCString spec;
  nsresult rv = aURI->GetSpec(spec);
  NS_ENSURE_SUCCESS(rv, rv);

  nsCOMArray<nsNavHistoryResultNode> nodes;
  FindChildrenByURI(spec, &nodes);
  for (int32_t i = 0; i < nodes.Count(); i++) {
    nodes[i]->OnVisitsRemoved();
  }

  return NS_OK;
}

NS_IMETHODIMP
nsNavHistoryContainerResultNode::GetContainerOpen(bool* aContainerOpen) {
  *aContainerOpen = mExpanded;
  return NS_OK;
}

NS_IMETHODIMP
nsNavHistoryContainerResultNode::SetContainerOpen(bool aContainerOpen) {
  if (aContainerOpen) {
    if (!mExpanded) {
      if (mOptions->AsyncEnabled()) {
        OpenContainerAsync();
      } else {
        OpenContainer();
      }
    }
  } else {
    if (mExpanded) {
      CloseContainer();
    } else if (mAsyncPendingStmt) {
      CancelAsyncOpen(false);
    }
  }

  return NS_OK;
}

/**
 * Notifies the result's observers of a change in the container's state.  The
 * notification includes both the old and new states:  The old is aOldState, and
 * the new is the container's current state.
 *
 * @param aOldState
 *        The state being transitioned out of.
 */
nsresult nsNavHistoryContainerResultNode::NotifyOnStateChange(
    uint16_t aOldState) {
  nsNavHistoryResult* result = GetResult();
  NS_ENSURE_STATE(result);

  nsresult rv;
  uint16_t currState;
  rv = GetState(&currState);
  NS_ENSURE_SUCCESS(rv, rv);

  // Notify via the new ContainerStateChanged observer method.
  NOTIFY_RESULT_OBSERVERS(result,
                          ContainerStateChanged(this, aOldState, currState));
  return NS_OK;
}

NS_IMETHODIMP
nsNavHistoryContainerResultNode::GetState(uint16_t* _state) {
  NS_ENSURE_ARG_POINTER(_state);

  *_state = mExpanded           ? (uint16_t)STATE_OPENED
            : mAsyncPendingStmt ? (uint16_t)STATE_LOADING
                                : (uint16_t)STATE_CLOSED;

  return NS_OK;
}

/**
 * This handles the generic container case.  Other container types should
 * override this to do their own handling.
 */
nsresult nsNavHistoryContainerResultNode::OpenContainer() {
  NS_ASSERTION(!mExpanded, "Container must not be expanded to open it");
  mExpanded = true;

  nsresult rv = NotifyOnStateChange(STATE_CLOSED);
  NS_ENSURE_SUCCESS(rv, rv);

  return NS_OK;
}

/**
 * Unset aSuppressNotifications to notify observers on this change.  That is
 * the normal operation.  This is set to false for the recursive calls since the
 * root container that is being closed will handle recomputation of the visible
 * elements for its entire subtree.
 */
nsresult nsNavHistoryContainerResultNode::CloseContainer(
    bool aSuppressNotifications) {
  NS_ASSERTION(
      (mExpanded && !mAsyncPendingStmt) || (!mExpanded && mAsyncPendingStmt),
      "Container must be expanded or loading to close it");

  nsresult rv;
  uint16_t oldState;
  rv = GetState(&oldState);
  NS_ENSURE_SUCCESS(rv, rv);

  if (mExpanded) {
    // Recursively close all child containers.
    for (int32_t i = 0; i < mChildren.Count(); ++i) {
      if (mChildren[i]->IsContainer() &&
          mChildren[i]->GetAsContainer()->mExpanded) {
        mChildren[i]->GetAsContainer()->CloseContainer(true);
      }
    }

    mExpanded = false;
  }

  // Be sure to set this to null before notifying observers.  It signifies that
  // the container is no longer loading (if it was in the first place).
  mAsyncPendingStmt = nullptr;

  if (!aSuppressNotifications) {
    rv = NotifyOnStateChange(oldState);
    NS_ENSURE_SUCCESS(rv, rv);
  }

  // If this is the root container of a result, we can tell the result to stop
  // observing changes, otherwise the result will stay in memory and updates
  // itself till it is cycle collected.
  nsNavHistoryResult* result = GetResult();
  NS_ENSURE_STATE(result);
  if (result->mRootNode == this) {
    result->StopObserving();
    // When reopening this node its result will be out of sync.
    // We must clear our children to ensure we will call FillChildren
    // again in such a case.
    if (this->IsQuery()) {
      this->GetAsQuery()->ClearChildren(true);
    } else if (this->IsFolderOrShortcut()) {
      this->GetAsFolder()->ClearChildren(true);
    }
  }

  return NS_OK;
}

/**
 * The async version of OpenContainer.
 */
nsresult nsNavHistoryContainerResultNode::OpenContainerAsync() {
  return NS_ERROR_NOT_IMPLEMENTED;
}

/**
 * Cancels the pending asynchronous Storage execution triggered by
 * FillChildrenAsync, if it exists.  This method doesn't do much, because after
 * cancelation Storage will call this node's HandleCompletion callback, where
 * the real work is done.
 *
 * @param aRestart
 *        If true, async execution will be restarted by HandleCompletion.
 */
void nsNavHistoryContainerResultNode::CancelAsyncOpen(bool aRestart) {
  NS_ASSERTION(mAsyncPendingStmt, "Async execution canceled but not pending");

  mAsyncCanceledState = aRestart ? CANCELED_RESTART_NEEDED : CANCELED;

  // Cancel will fail if the pending statement has already been canceled.
  // That's OK since this method may be called multiple times, and multiple
  // cancels don't harm anything.
  (void)mAsyncPendingStmt->Cancel();
}

/**
 * This builds up tree statistics from the bottom up.  Call with a container
 * and the indent level of that container.  To init the full tree, call with
 * the root container.  The default indent level is -1, which is appropriate
 * for the root level.
 *
 * CALL THIS AFTER FILLING ANY CONTAINER to update the parent and result node
 * pointers, even if you don't care about visit counts and last visit dates.
 */
void nsNavHistoryContainerResultNode::FillStats() {
  uint32_t accessCount = 0;
  PRTime newTime = 0;

  for (nsNavHistoryResultNode* node : mChildren) {
    SetAsParentOfNode(node);
    accessCount += node->mAccessCount;
    // this is how container nodes get sorted by date
    // The container gets the most recent time of the child nodes.
    if (node->mTime > newTime) {
      newTime = node->mTime;
    }
  }

  if (mExpanded) {
    mAccessCount = accessCount;
    if (!IsQuery() || newTime > mTime) {
      mTime = newTime;
    }
  }
}

void nsNavHistoryContainerResultNode::SetAsParentOfNode(
    nsNavHistoryResultNode* aNode) {
  aNode->mParent = this;
  aNode->mIndentLevel = mIndentLevel + 1;
  if (aNode->IsContainer()) {
    nsNavHistoryContainerResultNode* container = aNode->GetAsContainer();
    // Propagate some of the parent's options to this container.
    if (mOptions->ExcludeItems()) {
      container->mOptions->SetExcludeItems(true);
    }
    if (mOptions->ExcludeQueries()) {
      container->mOptions->SetExcludeQueries(true);
    }
    if (aNode->IsFolderOrShortcut() && mOptions->AsyncEnabled()) {
      container->mOptions->SetAsyncEnabled(true);
    }
    if (!mOptions->ExpandQueries()) {
      container->mOptions->SetExpandQueries(false);
    }
    container->mResult = mResult;
    container->FillStats();
  }
}

/**
 * This walks up the tree until we find a query result node or the root to get
 * the sorting type.
 */
uint16_t nsNavHistoryContainerResultNode::GetSortType() {
  if (mParent) return mParent->GetSortType();
  if (mResult) return mResult->mSortingMode;

  // This is a detached container, just use natural order.
  return nsINavHistoryQueryOptions::SORT_BY_NONE;
}

nsresult nsNavHistoryContainerResultNode::Refresh() {
  NS_WARNING(
      "Refresh() is supported by queries or folders, not generic containers.");
  return NS_OK;
}

/**
 * @return the sorting comparator function for the give sort type, or null if
 * there is no comparator.
 */
nsNavHistoryContainerResultNode::SortComparator
nsNavHistoryContainerResultNode::GetSortingComparator(uint16_t aSortType) {
  switch (aSortType) {
    case nsINavHistoryQueryOptions::SORT_BY_NONE:
      return &SortComparison_Bookmark;
    case nsINavHistoryQueryOptions::SORT_BY_TITLE_ASCENDING:
      return &SortComparison_TitleLess;
    case nsINavHistoryQueryOptions::SORT_BY_TITLE_DESCENDING:
      return &SortComparison_TitleGreater;
    case nsINavHistoryQueryOptions::SORT_BY_DATE_ASCENDING:
      return &SortComparison_DateLess;
    case nsINavHistoryQueryOptions::SORT_BY_DATE_DESCENDING:
      return &SortComparison_DateGreater;
    case nsINavHistoryQueryOptions::SORT_BY_URI_ASCENDING:
      return &SortComparison_URILess;
    case nsINavHistoryQueryOptions::SORT_BY_URI_DESCENDING:
      return &SortComparison_URIGreater;
    case nsINavHistoryQueryOptions::SORT_BY_VISITCOUNT_ASCENDING:
      return &SortComparison_VisitCountLess;
    case nsINavHistoryQueryOptions::SORT_BY_VISITCOUNT_DESCENDING:
      return &SortComparison_VisitCountGreater;
    case nsINavHistoryQueryOptions::SORT_BY_DATEADDED_ASCENDING:
      return &SortComparison_DateAddedLess;
    case nsINavHistoryQueryOptions::SORT_BY_DATEADDED_DESCENDING:
      return &SortComparison_DateAddedGreater;
    case nsINavHistoryQueryOptions::SORT_BY_LASTMODIFIED_ASCENDING:
      return &SortComparison_LastModifiedLess;
    case nsINavHistoryQueryOptions::SORT_BY_LASTMODIFIED_DESCENDING:
      return &SortComparison_LastModifiedGreater;
    case nsINavHistoryQueryOptions::SORT_BY_TAGS_ASCENDING:
      return &SortComparison_TagsLess;
    case nsINavHistoryQueryOptions::SORT_BY_TAGS_DESCENDING:
      return &SortComparison_TagsGreater;
    case nsINavHistoryQueryOptions::SORT_BY_FRECENCY_ASCENDING:
      return &SortComparison_FrecencyLess;
    case nsINavHistoryQueryOptions::SORT_BY_FRECENCY_DESCENDING:
      return &SortComparison_FrecencyGreater;
    default:
      MOZ_ASSERT_UNREACHABLE("Bad sorting type");
      return nullptr;
  }
}

/**
 * This is used by Result::SetSortingMode and QueryResultNode::FillChildren to
 * sort the child list.
 *
 * This does NOT update any visibility or tree information.  The caller will
 * have to completely rebuild the visible list after this.
 */
void nsNavHistoryContainerResultNode::RecursiveSort(
    SortComparator aComparator) {
  mChildren.Sort(aComparator);
  for (nsNavHistoryResultNode* child : mChildren) {
    if (child->IsContainer()) {
      child->GetAsContainer()->RecursiveSort(aComparator);
    }
  }
}

/**
 * @return the index that the given item would fall on if it were to be
 * inserted using the given sorting.
 */
int32_t nsNavHistoryContainerResultNode::FindInsertionPoint(
    nsNavHistoryResultNode* aNode, SortComparator aComparator,
    bool* aItemExists) {
  if (aItemExists) {
    (*aItemExists) = false;
  }

  if (mChildren.Count() == 0) return 0;

  // The common case is the beginning or the end because this is used to insert
  // new items that are added to history, which is usually sorted by date.
  int32_t res;
  res = aComparator(aNode, mChildren[0]);
  if (res <= 0) {
    if (aItemExists && res == 0) {
      (*aItemExists) = true;
    }
    return 0;
  }
  res = aComparator(aNode, mChildren[mChildren.Count() - 1]);
  if (res >= 0) {
    if (aItemExists && res == 0) {
      (*aItemExists) = true;
    }
    return mChildren.Count();
  }

  int32_t beginRange = 0;                // inclusive
  int32_t endRange = mChildren.Count();  // exclusive
  while (beginRange < endRange) {
    int32_t center = beginRange + (endRange - beginRange) / 2;
    int32_t res = aComparator(aNode, mChildren[center]);
    if (res <= 0) {
      endRange = center;  // left side
      if (aItemExists && res == 0) {
        (*aItemExists) = true;
      }
    } else {
      beginRange = center + 1;  // right site
    }
  }
  return endRange;
}

/**
 * This checks the child node at the given index to see if its sorting is
 * correct.  This is called when nodes are updated and we need to see whether
 * we need to move it.
 *
 * @returns true if not and it should be resorted.
 */
bool nsNavHistoryContainerResultNode::DoesChildNeedResorting(
    int32_t aIndex, SortComparator aComparator) {
  MOZ_ASSERT(aIndex < mChildren.Count(), "Input index out of range");
  MOZ_ASSERT(aIndex >= 0, "Input index out of range");
  if (aIndex < 0 || aIndex >= mChildren.Count() || mChildren.Count() == 1) {
    return false;
  }

  if (aIndex > 0) {
    // compare to previous item
    if (aComparator(mChildren[aIndex - 1], mChildren[aIndex]) > 0) {
      return true;
    }
  }
  if (aIndex < mChildren.Count() - 1) {
    // compare to next item
    if (aComparator(mChildren[aIndex], mChildren[aIndex + 1]) > 0) {
      return true;
    }
  }
  return false;
}

/**
 * When there are bookmark indices, we should never have ties, so we don't
 * need to worry about tiebreaking.  When there are no bookmark indices,
 * everything will be -1 and we don't worry about sorting.
 */
int32_t nsNavHistoryContainerResultNode::SortComparison_Bookmark(
    nsNavHistoryResultNode* a, nsNavHistoryResultNode* b) {
  return a->mBookmarkIndex - b->mBookmarkIndex;
}

/**
 * These are a little more complicated because they do a localization
 * conversion.  If this is too slow, we can compute the sort keys once in
 * advance, sort that array, and then reorder the real array accordingly.
 * This would save some key generations.
 *
 * The collation object must be allocated before sorting on title!
 */
int32_t nsNavHistoryContainerResultNode::SortComparison_TitleLess(
    nsNavHistoryResultNode* a, nsNavHistoryResultNode* b) {
  uint32_t aType;
  a->GetType(&aType);

  int32_t value = mozilla::intl::AppCollator::CompareBase(a->mTitle, b->mTitle);
  if (value == 0) {
    // resolve by URI
    if (a->IsURI()) {
      value = Compare(a->mURI, b->mURI);
    }
    if (value == 0) {
      // resolve by date
      value = ComparePRTime(a->mTime, b->mTime);
      if (value == 0) {
        value = nsNavHistoryContainerResultNode::SortComparison_Bookmark(a, b);
      }
    }
  }
  return value;
}
int32_t nsNavHistoryContainerResultNode::SortComparison_TitleGreater(
    nsNavHistoryResultNode* a, nsNavHistoryResultNode* b) {
  return -SortComparison_TitleLess(a, b);
}

/**
 * Equal times will be very unusual, but it is important that there is some
 * deterministic ordering of the results so they don't move around.
 */
int32_t nsNavHistoryContainerResultNode::SortComparison_DateLess(
    nsNavHistoryResultNode* a, nsNavHistoryResultNode* b) {
  int32_t value = ComparePRTime(a->mTime, b->mTime);
  if (value == 0) {
    value = mozilla::intl::AppCollator::CompareBase(a->mTitle, b->mTitle);
    if (value == 0) {
      value = nsNavHistoryContainerResultNode::SortComparison_Bookmark(a, b);
    }
  }
  return value;
}
int32_t nsNavHistoryContainerResultNode::SortComparison_DateGreater(
    nsNavHistoryResultNode* a, nsNavHistoryResultNode* b) {
  return -nsNavHistoryContainerResultNode::SortComparison_DateLess(a, b);
}

int32_t nsNavHistoryContainerResultNode::SortComparison_DateAddedLess(
    nsNavHistoryResultNode* a, nsNavHistoryResultNode* b) {
  int32_t value = ComparePRTime(a->mDateAdded, b->mDateAdded);
  if (value == 0) {
    value = mozilla::intl::AppCollator::CompareBase(a->mTitle, b->mTitle);
    if (value == 0) {
      value = nsNavHistoryContainerResultNode::SortComparison_Bookmark(a, b);
    }
  }
  return value;
}
int32_t nsNavHistoryContainerResultNode::SortComparison_DateAddedGreater(
    nsNavHistoryResultNode* a, nsNavHistoryResultNode* b) {
  return -nsNavHistoryContainerResultNode::SortComparison_DateAddedLess(a, b);
}

int32_t nsNavHistoryContainerResultNode::SortComparison_LastModifiedLess(
    nsNavHistoryResultNode* a, nsNavHistoryResultNode* b) {
  int32_t value = ComparePRTime(a->mLastModified, b->mLastModified);
  if (value == 0) {
    value = mozilla::intl::AppCollator::CompareBase(a->mTitle, b->mTitle);
    if (value == 0) {
      value = nsNavHistoryContainerResultNode::SortComparison_Bookmark(a, b);
    }
  }
  return value;
}
int32_t nsNavHistoryContainerResultNode::SortComparison_LastModifiedGreater(
    nsNavHistoryResultNode* a, nsNavHistoryResultNode* b) {
  return -nsNavHistoryContainerResultNode::SortComparison_LastModifiedLess(a,
                                                                           b);
}

/**
 * Certain types of parent nodes are treated specially because URIs are not
 * valid (like days or hosts).
 */
int32_t nsNavHistoryContainerResultNode::SortComparison_URILess(
    nsNavHistoryResultNode* a, nsNavHistoryResultNode* b) {
  int32_t value;
  if (a->IsURI() && b->IsURI()) {
    // normal URI or visit
    value = Compare(a->mURI, b->mURI);
  } else if (a->IsContainer() && !b->IsContainer()) {
    // Containers appear before entries with a uri.
    return -1;
  } else if (b->IsContainer() && !a->IsContainer()) {
    return 1;
  } else {
    // For everything else, use title sorting.
    value = mozilla::intl::AppCollator::CompareBase(a->mTitle, b->mTitle);
  }

  if (value == 0) {
    value = ComparePRTime(a->mTime, b->mTime);
    if (value == 0) {
      value = nsNavHistoryContainerResultNode::SortComparison_Bookmark(a, b);
    }
  }
  return value;
}
int32_t nsNavHistoryContainerResultNode::SortComparison_URIGreater(
    nsNavHistoryResultNode* a, nsNavHistoryResultNode* b) {
  return -SortComparison_URILess(a, b);
}

/**
 * Fall back on dates for conflict resolution
 */
int32_t nsNavHistoryContainerResultNode::SortComparison_VisitCountLess(
    nsNavHistoryResultNode* a, nsNavHistoryResultNode* b) {
  int32_t value = CompareIntegers(a->mAccessCount, b->mAccessCount);
  if (value == 0) {
    value = ComparePRTime(a->mTime, b->mTime);
    if (value == 0) {
      value = nsNavHistoryContainerResultNode::SortComparison_Bookmark(a, b);
    }
  }
  return value;
}
int32_t nsNavHistoryContainerResultNode::SortComparison_VisitCountGreater(
    nsNavHistoryResultNode* a, nsNavHistoryResultNode* b) {
  return -nsNavHistoryContainerResultNode::SortComparison_VisitCountLess(a, b);
}

int32_t nsNavHistoryContainerResultNode::SortComparison_TagsLess(
    nsNavHistoryResultNode* a, nsNavHistoryResultNode* b) {
  int32_t value = 0;
  nsAutoString aTags, bTags;

  nsresult rv = a->GetTags(aTags);
  NS_ENSURE_SUCCESS(rv, 0);

  rv = b->GetTags(bTags);
  NS_ENSURE_SUCCESS(rv, 0);

  value = mozilla::intl::AppCollator::CompareBase(aTags, bTags);

  // fall back to title sorting
  if (value == 0) {
    value = SortComparison_TitleLess(a, b);
  }

  return value;
}

int32_t nsNavHistoryContainerResultNode::SortComparison_TagsGreater(
    nsNavHistoryResultNode* a, nsNavHistoryResultNode* b) {
  return -SortComparison_TagsLess(a, b);
}

/**
 * Fall back on date and bookmarked status, for conflict resolution.
 */
int32_t nsNavHistoryContainerResultNode::SortComparison_FrecencyLess(
    nsNavHistoryResultNode* a, nsNavHistoryResultNode* b) {
  int32_t value = CompareIntegers(a->mFrecency, b->mFrecency);
  if (value == 0) {
    value = ComparePRTime(a->mTime, b->mTime);
    if (value == 0) {
      value = nsNavHistoryContainerResultNode::SortComparison_Bookmark(a, b);
    }
  }
  return value;
}
int32_t nsNavHistoryContainerResultNode::SortComparison_FrecencyGreater(
    nsNavHistoryResultNode* a, nsNavHistoryResultNode* b) {
  return -nsNavHistoryContainerResultNode::SortComparison_FrecencyLess(a, b);
}

/**
 * Searches this folder for a node with the given URI.  Returns null if not
 * found.
 *
 * @note Does not addref the node!
 */
nsNavHistoryResultNode* nsNavHistoryContainerResultNode::FindChildByURI(
    const nsACString& aSpec, uint32_t* aNodeIndex) {
  for (int32_t i = 0; i < mChildren.Count(); ++i) {
    if (mChildren[i]->IsURI()) {
      if (aSpec.Equals(mChildren[i]->mURI)) {
        *aNodeIndex = i;
        return mChildren[i];
      }
    }
  }
  return nullptr;
}

/**
 * Searches for matches for the given URI.
 */
void nsNavHistoryContainerResultNode::FindChildrenByURI(
    const nsCString& aSpec, nsCOMArray<nsNavHistoryResultNode>* aMatches) {
  for (int32_t i = 0; i < mChildren.Count(); ++i) {
    if (mChildren[i]->IsURI()) {
      if (aSpec.Equals(mChildren[i]->mURI)) {
        aMatches->AppendObject(mChildren[i]);
      }
    }
  }
}

/**
 * Searches this folder for a node with the given guid/target-folder-guid.
 *
 * @return the node if found, null otherwise.
 * @note Does not addref the node!
 */
nsNavHistoryResultNode* nsNavHistoryContainerResultNode::FindChildByGuid(
    const nsACString& guid, int32_t* nodeIndex) {
  *nodeIndex = -1;
  for (int32_t i = 0; i < mChildren.Count(); ++i) {
    if (mChildren[i]->mBookmarkGuid == guid ||
        mChildren[i]->mPageGuid == guid ||
        (mChildren[i]->IsFolderOrShortcut() &&
         mChildren[i]->GetAsFolder()->mTargetFolderGuid == guid)) {
      *nodeIndex = i;
      return mChildren[i];
    }
  }
  return nullptr;
}

/**
 * Searches this folder for a node with the given id/target-folder-id.
 *
 * @return the node if found, null otherwise.
 * @note Does not addref the node!
 */
nsNavHistoryResultNode* nsNavHistoryContainerResultNode::FindChildById(
    int64_t aItemId, int32_t* aNodeIndex) {
  for (int32_t i = 0; i < mChildren.Count(); ++i) {
    if (mChildren[i]->mItemId == aItemId ||
        (mChildren[i]->IsFolderOrShortcut() &&
         mChildren[i]->GetAsFolder()->mTargetFolderItemId == aItemId)) {
      *aNodeIndex = i;
      return mChildren[i];
    }
  }
  *aNodeIndex = -1;
  return nullptr;
}

/**
 * This does the work of adding a child to the container.  The child can be
 * either a container or or a single item that may even be collapsed with the
 * adjacent ones.
 */
nsresult nsNavHistoryContainerResultNode::InsertChildAt(
    nsNavHistoryResultNode* aNode, int32_t aIndex) {
  nsNavHistoryResult* result = GetResult();
  NS_ENSURE_STATE(result);

  SetAsParentOfNode(aNode);

  if (!mChildren.InsertObjectAt(aNode, aIndex)) return NS_ERROR_OUT_OF_MEMORY;

  // Update our stats and notify the result's observers.
  uint32_t oldAccessCount = mAccessCount;
  PRTime oldTime = mTime;

  mAccessCount += aNode->mAccessCount;
  if (mTime < aNode->mTime) {
    mTime = aNode->mTime;
  }
  if ((!mParent || mParent->AreChildrenVisible()) &&
      !result->CanSkipHistoryDetailsNotifications()) {
    NOTIFY_RESULT_OBSERVERS(
        result, NodeHistoryDetailsChanged(TO_ICONTAINER(this), oldTime,
                                          oldAccessCount));
  }

  // Update tree if we are visible.  Note that we could be here and not
  // expanded, like when there is a bookmark folder being updated because its
  // parent is visible.
  if (AreChildrenVisible()) {
    NOTIFY_RESULT_OBSERVERS(result, NodeInserted(this, aNode, aIndex));
  }

  return NS_OK;
}

/**
 * This locates the proper place for insertion according to the current sort
 * and calls InsertChildAt
 */
nsresult nsNavHistoryContainerResultNode::InsertSortedChild(
    nsNavHistoryResultNode* aNode, bool aIgnoreDuplicates) {
  if (mChildren.Count() == 0) return InsertChildAt(aNode, 0);

  SortComparator comparator = GetSortingComparator(GetSortType());
  if (comparator) {
    // When inserting a new node, it must have proper statistics because we use
    // them to find the correct insertion point.  The insert function will then
    // recompute these statistics and fill in the proper parents and hierarchy
    // level.  Doing this twice shouldn't be a large performance penalty because
    // when we are inserting new containers, they typically contain only one
    // item (because we've browsed a new page).
    if (aNode->IsContainer()) {
      // need to update all the new item's children
      nsNavHistoryContainerResultNode* container = aNode->GetAsContainer();
      container->mResult = mResult;
      container->FillStats();
    }

    bool itemExists;
    int32_t position = FindInsertionPoint(aNode, comparator, &itemExists);
    if (aIgnoreDuplicates && itemExists) {
      return NS_OK;
    }

    return InsertChildAt(aNode, position);
  }
  return InsertChildAt(aNode, mChildren.Count());
}

/**
 * This checks if the item at aIndex is located correctly given the sorting
 * move.  If it's not, the item is moved, and the result's observers are
 * notified.
 *
 * @return true if the item position has been changed, false otherwise.
 */
bool nsNavHistoryContainerResultNode::EnsureItemPosition(int32_t aIndex) {
  NS_ASSERTION(aIndex < mChildren.Count(), "Invalid index");
  if (aIndex >= mChildren.Count()) {
    return false;
  }

  SortComparator comparator = GetSortingComparator(GetSortType());
  if (!comparator) {
    return false;
  }

  if (!DoesChildNeedResorting(aIndex, comparator)) {
    return false;
  }

  RefPtr<nsNavHistoryResultNode> node(mChildren[aIndex]);
  mChildren.RemoveObjectAt(aIndex);

  int32_t newIndex = FindInsertionPoint(node, comparator, nullptr);
  mChildren.InsertObjectAt(node.get(), newIndex);

  if (AreChildrenVisible()) {
    nsNavHistoryResult* result = GetResult();
    NOTIFY_RESULT_OBSERVERS_RET(
        result, NodeMoved(node, this, aIndex, this, newIndex), false);
  }

  return true;
}

/**
 * This does all the work of removing a child from this container, including
 * updating the tree if necessary.  Note that we do not need to be open for
 * this to work.
 */
nsresult nsNavHistoryContainerResultNode::RemoveChildAt(int32_t aIndex) {
  NS_ASSERTION(aIndex >= 0 && aIndex < mChildren.Count(), "Invalid index");

  // Hold an owning reference to keep from expiring while we work with it.
  RefPtr<nsNavHistoryResultNode> oldNode = mChildren[aIndex];

  // Update stats.
  // XXX This assertion does not reliably pass -- investigate!! (bug 1049797)
  // MOZ_ASSERT(mAccessCount >= mChildren[aIndex]->mAccessCount,
  //            "Invalid access count while updating!");
  mAccessCount -= mChildren[aIndex]->mAccessCount;

  // Remove it from our list and notify the result's observers.
  mChildren.RemoveObjectAt(aIndex);
  if (AreChildrenVisible()) {
    nsNavHistoryResult* result = GetResult();
    NOTIFY_RESULT_OBSERVERS(result, NodeRemoved(this, oldNode, aIndex));
  }

  oldNode->OnRemoving();
  return NS_OK;
}

/**
 * Searches for matches for the given URI.  If aOnlyOne is set, it will
 * terminate as soon as it finds a single match.  This would be used when there
 * are URI results so there will only ever be one copy of any URI.
 *
 * When aOnlyOne is false, it will check all elements.  This is for non-history
 * or visit style results that may have multiple copies of any given URI.
 */
void nsNavHistoryContainerResultNode::RecursiveFindURIs(
    bool aOnlyOne, nsNavHistoryContainerResultNode* aContainer,
    const nsCString& aSpec, nsCOMArray<nsNavHistoryResultNode>* aMatches) {
  for (int32_t i = 0; i < aContainer->mChildren.Count(); ++i) {
    auto* node = aContainer->mChildren[i];
    if (node->IsURI()) {
      if (node->mURI.Equals(aSpec)) {
        aMatches->AppendObject(node);
        if (aOnlyOne) {
          return;
        }
      }
    } else if (node->IsContainer() && node->GetAsContainer()->mExpanded) {
      RecursiveFindURIs(aOnlyOne, node->GetAsContainer(), aSpec, aMatches);
    }
  }
}

/**
 * If aUpdateSort is true, we will also update the sorting of this item.
 * Normally you want this to be true, but it can be false if the thing you are
 * changing can not affect sorting (like favicons).
 *
 * You should NOT change any child lists as part of the callback function.
 */
bool nsNavHistoryContainerResultNode::UpdateURIs(
    bool aRecursive, bool aOnlyOne, bool aUpdateSort, const nsCString& aSpec,
    nsresult (*aCallback)(nsNavHistoryResultNode*, const void*,
                          const nsNavHistoryResult*),
    const void* aClosure) {
  const nsNavHistoryResult* result = GetResult();
  if (!result) {
    MOZ_ASSERT(false, "Should have a result");
    return false;
  }

  // this needs to be owning since sometimes we remove and re-insert nodes
  // in their parents and we don't want them to go away.
  nsCOMArray<nsNavHistoryResultNode> matches;

  if (aRecursive) {
    RecursiveFindURIs(aOnlyOne, this, aSpec, &matches);
  } else if (aOnlyOne) {
    uint32_t nodeIndex;
    nsNavHistoryResultNode* node = FindChildByURI(aSpec, &nodeIndex);
    if (node) {
      matches.AppendObject(node);
    }
  } else {
    MOZ_ASSERT(
        false,
        "UpdateURIs does not handle nonrecursive updates of multiple items.");
    // this case easy to add if you need it, just find all the matching URIs
    // at this level.  However, this isn't currently used. History uses
    // recursive, Bookmarks uses one level and knows that the match is unique.
    return false;
  }

  if (matches.Count() == 0) return false;

  // PERFORMANCE: This updates each container for each child in it that
  // changes.  In some cases, many elements have changed inside the same
  // container.  It would be better to compose a list of containers, and
  // update each one only once for all the items that have changed in it.
  for (int32_t i = 0; i < matches.Count(); ++i) {
    nsNavHistoryResultNode* node = matches[i];
    nsNavHistoryContainerResultNode* parent = node->mParent;
    if (!parent) {
      MOZ_ASSERT(false, "All URI nodes being updated must have parents");
      continue;
    }

    aCallback(node, aClosure, result);

    if (aUpdateSort) {
      int32_t childIndex = parent->FindChild(node);
      MOZ_ASSERT(childIndex >= 0,
                 "Could not find child we just got a reference to");
      if (childIndex >= 0) {
        parent->EnsureItemPosition(childIndex);
      }
    }
  }

  return true;
}

/**
 * This is used to update the titles in the tree.  This is called from both
 * query and bookmark folder containers to update the tree.  Bookmark folders
 * should be sure to set recursive to false, since child folders will have
 * their own callbacks registered.
 */
static nsresult setTitleCallback(nsNavHistoryResultNode* aNode,
                                 const void* aClosure,
                                 const nsNavHistoryResult* aResult) {
  const nsACString* newTitle = static_cast<const nsACString*>(aClosure);
  aNode->mTitle = *newTitle;

  if (aResult && (!aNode->mParent || aNode->mParent->AreChildrenVisible())) {
    NOTIFY_RESULT_OBSERVERS(aResult, NodeTitleChanged(aNode, *newTitle));
  }

  return NS_OK;
}
nsresult nsNavHistoryContainerResultNode::ChangeTitles(
    nsIURI* aURI, const nsACString& aNewTitle, bool aRecursive, bool aOnlyOne) {
  // uri string
  nsAutoCString uriString;
  nsresult rv = aURI->GetSpec(uriString);
  NS_ENSURE_SUCCESS(rv, rv);

  // The recursive function will update the result's tree nodes, but only if we
  // give it a non-null pointer.  So if there isn't a tree, just pass nullptr
  // so it doesn't bother trying to call the result.
  nsNavHistoryResult* result = GetResult();
  NS_ENSURE_STATE(result);

  uint16_t sortType = GetSortType();
  bool updateSorting =
      (sortType == nsINavHistoryQueryOptions::SORT_BY_TITLE_ASCENDING ||
       sortType == nsINavHistoryQueryOptions::SORT_BY_TITLE_DESCENDING);

  UpdateURIs(aRecursive, aOnlyOne, updateSorting, uriString, setTitleCallback,
             static_cast<const void*>(&aNewTitle));

  return NS_OK;
}

/**
 * Complex containers (folders and queries) will override this.  Here, we
 * handle the case of simple containers (like host groups) where the children
 * are always stored.
 */
NS_IMETHODIMP
nsNavHistoryContainerResultNode::GetHasChildren(bool* aHasChildren) {
  *aHasChildren = (mChildren.Count() > 0);
  return NS_OK;
}

/**
 * @throws if this node is closed.
 */
NS_IMETHODIMP
nsNavHistoryContainerResultNode::GetChildCount(uint32_t* aChildCount) {
  if (!mExpanded) return NS_ERROR_NOT_AVAILABLE;
  *aChildCount = mChildren.Count();
  return NS_OK;
}

NS_IMETHODIMP
nsNavHistoryContainerResultNode::GetChild(uint32_t aIndex,
                                          nsINavHistoryResultNode** _child) {
  if (!mExpanded) return NS_ERROR_NOT_AVAILABLE;
  if (aIndex >= mChildren.Length()) return NS_ERROR_INVALID_ARG;
  nsCOMPtr<nsINavHistoryResultNode> child = mChildren.ElementAt(aIndex);
  child.forget(_child);
  return NS_OK;
}

NS_IMETHODIMP
nsNavHistoryContainerResultNode::GetChildIndex(nsINavHistoryResultNode* aNode,
                                               uint32_t* _retval) {
  if (!mExpanded) return NS_ERROR_NOT_AVAILABLE;

  int32_t nodeIndex = FindChild(static_cast<nsNavHistoryResultNode*>(aNode));
  if (nodeIndex == -1) return NS_ERROR_INVALID_ARG;

  *_retval = nodeIndex;
  return NS_OK;
}

/**
 * HOW QUERY UPDATING WORKS
 *
 * Queries are different than bookmark folders in that we can not always do
 * dynamic updates (easily) and updates are more expensive.  Therefore, we do
 * NOT query if we are not open and want to see if we have any children (for
 * drawing a twisty) and always assume we will.
 *
 * When the container is opened, we execute the query and register the
 * listeners.  Like bookmark folders, we stay registered even when closed, and
 * clear ourselves as soon as a message comes in.  This lets us respond quickly
 * if the user closes and reopens the container.
 *
 * We try to handle the most common notifications for the most common query
 * types dynamically, that is, figuring out what should happen in response to
 * a message without doing a requery.  For complex changes or complex queries,
 * we give up and requery.
 */
NS_IMPL_ISUPPORTS_INHERITED(nsNavHistoryQueryResultNode,
                            nsNavHistoryContainerResultNode,
                            nsINavHistoryQueryResultNode)

nsNavHistoryQueryResultNode::nsNavHistoryQueryResultNode(
    const nsACString& aTitle, PRTime aTime, const nsACString& aQueryURI,
    const RefPtr<nsNavHistoryQuery>& aQuery,
    const RefPtr<nsNavHistoryQueryOptions>& aOptions)
    : nsNavHistoryContainerResultNode(aQueryURI, aTitle, aTime,
                                      nsNavHistoryResultNode::RESULT_TYPE_QUERY,
                                      aOptions),
      mQuery(aQuery),
      mHasSearchTerms(false),
      mLiveUpdate(getUpdateRequirements(aQuery, aOptions, &mHasSearchTerms)),
      mContentsValid(false),
      mBatchChanges(0),
      mTransitions(aQuery->Transitions().Clone()) {}

nsNavHistoryQueryResultNode::~nsNavHistoryQueryResultNode() {
  // Remove this node from result's observers.  We don't need to be notified
  // anymore.
  if (mResult && mResult->mAllBookmarksObservers.Contains(this)) {
    mResult->RemoveAllBookmarksObserver(this);
  }
  if (mResult && mResult->mHistoryObservers.Contains(this)) {
    mResult->RemoveHistoryObserver(this);
  }
  if (mResult && mResult->mMobilePrefObservers.Contains(this)) {
    mResult->RemoveMobilePrefsObserver(this);
  }
}

/**
 * Whoever made us may want non-expanding queries. However, we always expand
 * when we are the root node, or else asking for non-expanding queries would be
 * useless.  A query node is not expandable if excludeItems is set or if
 * expandQueries is unset.
 */
bool nsNavHistoryQueryResultNode::CanExpand() {
  // The root node and containersQueries can always expand;
  if ((mResult && mResult->mRootNode == this) || IsContainersQuery()) {
    return true;
  }

  if (mOptions->ExcludeItems()) {
    return false;
  }
  if (mOptions->ExpandQueries()) {
    return true;
  }

  return false;
}

/**
 * Some query with a particular result type can contain other queries.  They
 * must be always expandable
 */
bool nsNavHistoryQueryResultNode::IsContainersQuery() {
  uint16_t resultType = Options()->ResultType();
  return resultType == nsINavHistoryQueryOptions::RESULTS_AS_DATE_QUERY ||
         resultType == nsINavHistoryQueryOptions::RESULTS_AS_DATE_SITE_QUERY ||
         resultType == nsINavHistoryQueryOptions::RESULTS_AS_TAGS_ROOT ||
         resultType == nsINavHistoryQueryOptions::RESULTS_AS_SITE_QUERY ||
         resultType == nsINavHistoryQueryOptions::RESULTS_AS_ROOTS_QUERY ||
         resultType == nsINavHistoryQueryOptions::RESULTS_AS_LEFT_PANE_QUERY;
}

/**
 * Here we do not want to call ContainerResultNode::OnRemoving since our own
 * ClearChildren will do the same thing and more (unregister the observers).
 * The base ResultNode::OnRemoving will clear some regular node stats, so it is
 * OK.
 */
void nsNavHistoryQueryResultNode::OnRemoving() {
  nsNavHistoryResultNode::OnRemoving();
  ClearChildren(true);
  mResult = nullptr;
}

/**
 * Marks the container as open, rebuilding results if they are invalid.  We
 * may still have valid results if the container was previously open and
 * nothing happened since closing it.
 *
 * We do not handle CloseContainer specially.  The default one just marks the
 * container as closed, but doesn't actually mark the results as invalid.
 * The results will be invalidated by the next history or bookmark
 * notification that comes in.  This means if you open and close the item
 * without anything happening in between, it will be fast (this actually
 * happens when results are used as menus).
 */
nsresult nsNavHistoryQueryResultNode::OpenContainer() {
  NS_ASSERTION(!mExpanded, "Container must be closed to open it");
  mExpanded = true;

  nsresult rv;

  if (!CanExpand()) return NS_OK;
  if (!mContentsValid) {
    rv = FillChildren();
    NS_ENSURE_SUCCESS(rv, rv);
  }

  rv = NotifyOnStateChange(STATE_CLOSED);
  NS_ENSURE_SUCCESS(rv, rv);

  return NS_OK;
}

/**
 * When we have valid results we can always give an exact answer.  When we
 * don't we just assume we'll have results, since actually doing the query
 * might be hard.  This is used to draw twisties on the tree, so precise results
 * don't matter.
 */
NS_IMETHODIMP
nsNavHistoryQueryResultNode::GetHasChildren(bool* aHasChildren) {
  *aHasChildren = false;

  if (!CanExpand()) {
    return NS_OK;
  }

  uint16_t resultType = mOptions->ResultType();

  // Tags are always populated, otherwise they are removed.
  if (mQuery->Tags().Length() == 1 && mParent &&
      mParent->mOptions->ResultType() ==
          nsINavHistoryQueryOptions::RESULTS_AS_TAGS_ROOT) {
    *aHasChildren = true;
    return NS_OK;
  }

  // AllBookmarks and the left pane folder also always have children.
  if (resultType == nsINavHistoryQueryOptions::RESULTS_AS_ROOTS_QUERY ||
      resultType == nsINavHistoryQueryOptions::RESULTS_AS_LEFT_PANE_QUERY) {
    *aHasChildren = true;
    return NS_OK;
  }

  // For history containers query we must check if we have any history
  if (resultType == nsINavHistoryQueryOptions::RESULTS_AS_DATE_QUERY ||
      resultType == nsINavHistoryQueryOptions::RESULTS_AS_DATE_SITE_QUERY ||
      resultType == nsINavHistoryQueryOptions::RESULTS_AS_SITE_QUERY) {
    nsNavHistory* history = nsNavHistory::GetHistoryService();
    NS_ENSURE_TRUE(history, NS_ERROR_OUT_OF_MEMORY);
    *aHasChildren = history->hasHistoryEntries();
    return NS_OK;
  }

  // TODO (Bug 1477934): We don't have a good synchronous way to fetch whether
  // we have tags or not, to properly reply to the hasChildren request on the
  // tags root. Potentially we could pass this information when we create the
  // container.

  // If the container is open and populated, this is trivial.
  if (mContentsValid) {
    *aHasChildren = (mChildren.Count() > 0);
    return NS_OK;
  }

  // Fallback to assume we have children.
  *aHasChildren = true;
  return NS_OK;
}

/**
 * This doesn't just return mURI because in the case of queries that may
 * be lazily constructed from the query objects.
 */
NS_IMETHODIMP
nsNavHistoryQueryResultNode::GetUri(nsACString& aURI) {
  aURI = mURI;
  return NS_OK;
}

NS_IMETHODIMP
nsNavHistoryQueryResultNode::GetFolderItemId(int64_t* aItemId) {
  *aItemId = -1;
  return NS_OK;
}

NS_IMETHODIMP
nsNavHistoryQueryResultNode::GetTargetFolderGuid(nsACString& aGuid) {
  aGuid.Truncate();
  return NS_OK;
}

NS_IMETHODIMP
nsNavHistoryQueryResultNode::GetQuery(nsINavHistoryQuery** _query) {
  RefPtr<nsNavHistoryQuery> query = mQuery;
  query.forget(_query);
  return NS_OK;
}

NS_IMETHODIMP
nsNavHistoryQueryResultNode::GetQueryOptions(
    nsINavHistoryQueryOptions** _options) {
  MOZ_ASSERT(mOptions, "Options should be valid");
  RefPtr<nsNavHistoryQueryOptions> options = mOptions;
  options.forget(_options);
  return NS_OK;
}

/**
 * Safe options getter, ensures query is parsed first.
 */
nsNavHistoryQueryOptions* nsNavHistoryQueryResultNode::Options() {
  MOZ_ASSERT(mOptions, "Options invalid, cannot generate from URI");
  return mOptions;
}

nsresult nsNavHistoryQueryResultNode::FillChildren() {
  MOZ_ASSERT(!mContentsValid,
             "Don't call FillChildren when contents are valid");
  MOZ_ASSERT(mChildren.Count() == 0,
             "We are trying to fill children when there already are some");
  NS_ENSURE_STATE(mQuery && mOptions);

  // get the results from the history service
  nsNavHistory* history = nsNavHistory::GetHistoryService();
  NS_ENSURE_TRUE(history, NS_ERROR_OUT_OF_MEMORY);
  nsresult rv = history->GetQueryResults(this, mQuery, mOptions, &mChildren);
  NS_ENSURE_SUCCESS(rv, rv);

  // it is important to call FillStats to fill in the parents on all
  // nodes and the result node pointers on the containers
  FillStats();

  uint16_t sortType = GetSortType();

  if (mResult && mResult->mNeedsToApplySortingMode) {
    // We should repopulate container and then apply sortingMode.  To avoid
    // sorting 2 times we simply do that here.
    mResult->SetSortingMode(mResult->mSortingMode);
  } else if (mOptions->QueryType() !=
                 nsINavHistoryQueryOptions::QUERY_TYPE_HISTORY ||
             sortType != nsINavHistoryQueryOptions::SORT_BY_NONE) {
    // The default SORT_BY_NONE sorts by the bookmark index (position),
    // which we do not have for history queries.
    // Once we've computed all tree stats, we can sort, because containers will
    // then have proper visit counts and dates.
    SortComparator comparator = GetSortingComparator(GetSortType());
    if (comparator) {
      // Usually containers queries results comes already sorted from the
      // database, but some locales could have special rules to sort by title.
      // RecursiveSort won't apply these rules to containers in containers
      // queries because when setting sortingMode on the result we want to sort
      // contained items (bug 473157).
      // Base container RecursiveSort will sort both our children and all
      // descendants, and is used in this case because we have to do manual
      // title sorting.
      // Query RecursiveSort will instead only sort descendants if we are a
      // constinaersQuery, e.g. a grouped query that will return other queries.
      // For other type of queries it will act as the base one.
      if (IsContainersQuery() && sortType == mOptions->SortingMode() &&
          (sortType == nsINavHistoryQueryOptions::SORT_BY_TITLE_ASCENDING ||
           sortType == nsINavHistoryQueryOptions::SORT_BY_TITLE_DESCENDING)) {
        nsNavHistoryContainerResultNode::RecursiveSort(comparator);
      } else {
        RecursiveSort(comparator);
      }
    }
  }

  // if we are limiting our results remove items from the end of the
  // mChildren array after sorting. This is done for root node only.
  // note, if count < max results, we won't do anything.
  if (!mParent && mOptions->MaxResults()) {
    while (mChildren.Length() > mOptions->MaxResults()) {
      mChildren.RemoveObjectAt(mChildren.Count() - 1);
    }
  }

  // If we're not updating the query, we don't need to add listeners, so bail
  // out early.
  if (mLiveUpdate == QUERYUPDATE_NONE) {
    mContentsValid = true;
    return NS_OK;
  }

  nsNavHistoryResult* result = GetResult();
  NS_ENSURE_STATE(result);

  // Ensure to add history observer before bookmarks observer, because the
  // latter wants to know if an history observer was added.

  if (mOptions->QueryType() == nsINavHistoryQueryOptions::QUERY_TYPE_HISTORY) {
    // Date containers that contain site containers have no reason to observe
    // history, if the inside site container is expanded it will update,
    // otherwise we are going to refresh the parent query.
    if (!mParent || mParent->mOptions->ResultType() !=
                        nsINavHistoryQueryOptions::RESULTS_AS_DATE_SITE_QUERY) {
      // register with the result for history updates
      result->AddHistoryObserver(this);
    }
  }

  if (mOptions->QueryType() ==
          nsINavHistoryQueryOptions::QUERY_TYPE_BOOKMARKS ||
      mLiveUpdate == QUERYUPDATE_COMPLEX_WITH_BOOKMARKS || mHasSearchTerms) {
    // register with the result for bookmark updates
    result->AddAllBookmarksObserver(this);
  }

  if (mLiveUpdate == QUERYUPDATE_MOBILEPREF) {
    result->AddMobilePrefsObserver(this);
  }

  mContentsValid = true;
  return NS_OK;
}

/**
 * Call with unregister = false when we are going to update the children (for
 * example, when the container is open).  This will clear the list and notify
 * all the children that they are going away.
 *
 * When the results are becoming invalid and we are not going to refresh them,
 * set unregister = true, which will unregister the listener from the
 * result if any.  We use unregister = false when we are refreshing the list
 * immediately so want to stay a notifier.
 */
void nsNavHistoryQueryResultNode::ClearChildren(bool aUnregister) {
  for (int32_t i = 0; i < mChildren.Count(); ++i) mChildren[i]->OnRemoving();
  mChildren.Clear();

  if (aUnregister && mContentsValid) {
    nsNavHistoryResult* result = GetResult();
    if (result) {
      result->RemoveHistoryObserver(this);
      result->RemoveAllBookmarksObserver(this);
      result->RemoveMobilePrefsObserver(this);
    }
  }
  mContentsValid = false;
}

/**
 * This is called to update the result when something has changed that we
 * can not incrementally update.
 */
nsresult nsNavHistoryQueryResultNode::Refresh() {
  nsNavHistoryResult* result = GetResult();
  NS_ENSURE_STATE(result);
  if (result->IsBatching()) {
    result->requestRefresh(this);
    return NS_OK;
  }

  // This is not a root node but it does not have a parent - this means that
  // the node has already been cleared and it is now called, because it was
  // left in a local copy of the observers array.
  if (mIndentLevel > -1 && !mParent) return NS_OK;

  // Do not refresh if we are not expanded or if we are child of a query
  // containing other queries.  In this case calling Refresh for each child
  // query could cause a major slowdown.  We should not refresh nested
  // queries, since we will already refresh the parent one.
  // The only exception to this, is if the parent query is of QUERYUPDATE_NONE,
  // this can be the case for the RESULTS_AS_TAGS_ROOT
  // under RESULTS_AS_LEFT_PANE_QUERY.
  if (!mExpanded) {
    ClearChildren(true);
    return NS_OK;
  }

  if (mParent && mParent->IsQuery()) {
    nsNavHistoryQueryResultNode* parent = mParent->GetAsQuery();
    if (parent->IsContainersQuery() &&
        parent->mLiveUpdate != QUERYUPDATE_NONE) {
      // Don't update, just invalidate and unhook
      ClearChildren(true);
      return NS_OK;  // no updates in tree state
    }
  }

  if (mLiveUpdate == QUERYUPDATE_COMPLEX_WITH_BOOKMARKS) {
    ClearChildren(true);
  } else {
    ClearChildren(false);
  }

  // Ignore errors from FillChildren, since we will still want to refresh
  // the tree (there just might not be anything in it on error).
  (void)FillChildren();

  NOTIFY_RESULT_OBSERVERS(result, InvalidateContainer(TO_CONTAINER(this)));
  return NS_OK;
}

/**
 * Here, we override GetSortType to return the current sorting for this
 * query.  GetSortType is used when dynamically inserting query results so we
 * can see which comparator we should use to find the proper insertion point
 * (it shouldn't be called from folder containers which maintain their own
 * sorting).
 *
 * Normally, the container just forwards it up the chain.  This is what we want
 * for host groups, for example.  For queries, we often want to use the query's
 * sorting mode.
 *
 * However, we only use this query node's sorting when it is not the root.
 * When it is the root, we use the result's sorting mode.  This is because
 * there are two cases:
 *   - You are looking at a bookmark hierarchy that contains an embedded
 *     result.  We should always use the query's sort ordering since the result
 *     node's headers have nothing to do with us (and are disabled).
 *   - You are looking at a query in the tree.  In this case, we want the
 *     result sorting to override ours (it should be initialized to the same
 *     sorting mode).
 */
uint16_t nsNavHistoryQueryResultNode::GetSortType() {
  if (mParent) return mOptions->SortingMode();
  if (mResult) return mResult->mSortingMode;

  // This is a detached container, just use natural order.
  return nsINavHistoryQueryOptions::SORT_BY_NONE;
}

void nsNavHistoryQueryResultNode::RecursiveSort(SortComparator aComparator) {
  if (!IsContainersQuery()) {
    mChildren.Sort(aComparator);
  }

  for (int32_t i = 0; i < mChildren.Count(); ++i) {
    if (mChildren[i]->IsContainer()) {
      mChildren[i]->GetAsContainer()->RecursiveSort(aComparator);
    }
  }
}

nsresult nsNavHistoryQueryResultNode::OnBeginUpdateBatch() { return NS_OK; }

nsresult nsNavHistoryQueryResultNode::OnEndUpdateBatch() {
  // If the query has no children it's possible it's not yet listening to
  // bookmarks changes, in such a case it's safer to force a refresh to gather
  // eventual new nodes matching query options.
  if (mChildren.Count() == 0) {
    nsresult rv = Refresh();
    NS_ENSURE_SUCCESS(rv, rv);
  }

  mBatchChanges = 0;
  return NS_OK;
}

static nsresult setHistoryDetailsCallback(nsNavHistoryResultNode* aNode,
                                          const void* aClosure,
                                          const nsNavHistoryResult* aResult) {
  const nsNavHistoryResultNode* updatedNode =
      static_cast<const nsNavHistoryResultNode*>(aClosure);

  aNode->mAccessCount = updatedNode->mAccessCount;
  if (aNode->mTime < updatedNode->mTime) {
    aNode->mTime = updatedNode->mTime;
  }
  aNode->mFrecency = updatedNode->mFrecency;
  aNode->mHidden = updatedNode->mHidden;

  return NS_OK;
}

/**
 * Here we need to update all copies of the URI we have with the new visit
 * count, and potentially add a new entry in our query.  This is the most
 * common update operation and it is important that it be as efficient as
 * possible.
 */
nsresult nsNavHistoryQueryResultNode::OnVisit(
    nsIURI* aURI, int64_t aVisitId, PRTime aTime, uint32_t aTransitionType,
    const nsACString& aGUID, bool aHidden, uint32_t aVisitCount,
    const nsAString& aLastKnownTitle, int64_t aFrecency, uint32_t* aAdded) {
  // Skip the notification if the visit details are filtered out by the query.
  if (!isQueryMatchingVisitDetails(mQuery, mOptions, aHidden, aTime,
                                   aTransitionType, aURI)) {
    return NS_OK;
  }
  nsNavHistoryResult* result = GetResult();
  NS_ENSURE_STATE(result);
  if (result->IsBatching() &&
      ++mBatchChanges > MAX_BATCH_CHANGES_BEFORE_REFRESH) {
    nsresult rv = Refresh();
    NS_ENSURE_SUCCESS(rv, rv);
    return NS_OK;
  }

  nsNavHistory* history = nsNavHistory::GetHistoryService();
  NS_ENSURE_TRUE(history, NS_ERROR_OUT_OF_MEMORY);

  switch (mLiveUpdate) {
    case QUERYUPDATE_MOBILEPREF: {
      return NS_OK;
    }

    case QUERYUPDATE_HOST: {
      // For these simple yet common cases we can check the host ourselves
      // before doing the overhead of creating a new result node.
      if (mQuery->Domain().IsVoid()) return NS_OK;

      nsAutoCString host;
      if (NS_FAILED(aURI->GetAsciiHost(host))) return NS_OK;

      if (!mQuery->Domain().Equals(host)) return NS_OK;

      // Fall through to check the time, if the time is not present it will
      // still match.
      [[fallthrough]];
    }

    case QUERYUPDATE_TIME: {
      // For these simple yet common cases we can check the time ourselves
      // before doing the overhead of creating a new result node.
      bool hasIt;
      mQuery->GetHasBeginTime(&hasIt);
      if (hasIt) {
        PRTime beginTime = nsNavHistory::NormalizeTime(
            mQuery->BeginTimeReference(), mQuery->BeginTime());
        if (aTime < beginTime) return NS_OK;  // before our time range
      }
      mQuery->GetHasEndTime(&hasIt);
      if (hasIt) {
        PRTime endTime = nsNavHistory::NormalizeTime(mQuery->EndTimeReference(),
                                                     mQuery->EndTime());
        if (aTime > endTime) return NS_OK;  // after our time range
      }
      // Now we know that our visit satisfies the time range, fall through to
      // the QUERYUPDATE_SIMPLE case below.
      [[fallthrough]];
    }

    case QUERYUPDATE_SIMPLE: {
      if (mOptions->ResultType() !=
              nsNavHistoryQueryOptions::RESULTS_AS_VISIT &&
          mOptions->ResultType() != nsNavHistoryQueryOptions::RESULTS_AS_URI) {
        // Certain result types manage the nodes by themselves.
        return NS_OK;
      }

      nsAutoCString spec;
      nsresult rv = aURI->GetSpec(spec);
      NS_ENSURE_SUCCESS(rv, rv);
      RefPtr<nsNavHistoryResultNode> addition = new nsNavHistoryResultNode(
          spec, NS_ConvertUTF16toUTF8(aLastKnownTitle), aVisitCount, aTime);
      addition->mPageGuid.Assign(aGUID);
      addition->mFrecency = aFrecency;
      addition->mHidden = aHidden;
      addition->mTransitionType = aTransitionType;

      if (mOptions->ResultType() ==
          nsNavHistoryQueryOptions::RESULTS_AS_VISIT) {
        addition->mVisitId = aVisitId;
      }

      // Optimization for a common case: if the query has maxResults and is
      // sorted by date, get the current boundaries and check if the added
      // visit would fit. Later, we may have to remove the last child to
      // respect maxResults.
      if (mOptions->MaxResults() &&
          mChildren.Length() >= mOptions->MaxResults()) {
        uint16_t sortType = GetSortType();
        if (sortType == nsINavHistoryQueryOptions::SORT_BY_DATE_ASCENDING &&
            aTime > std::max(mChildren[0]->mTime,
                             mChildren[mChildren.Count() - 1]->mTime)) {
          return NS_OK;
        }
        if (sortType == nsINavHistoryQueryOptions::SORT_BY_DATE_DESCENDING &&
            aTime < std::min(mChildren[0]->mTime,
                             mChildren[mChildren.Count() - 1]->mTime)) {
          return NS_OK;
        }
      }

      if (mOptions->ResultType() ==
          nsNavHistoryQueryOptions::RESULTS_AS_VISIT) {
        // If this is a visit type query, just insert the new visit.  We never
        // update visits, only add or remove them.
        // If the query has search terms, ensure the new visit is matching them.
        if (mHasSearchTerms && !isQuerySearchTermsMatching(mQuery, addition)) {
          return NS_OK;
        }
        rv = InsertSortedChild(addition);
        NS_ENSURE_SUCCESS(rv, rv);
      } else {
        uint16_t sortType = GetSortType();
        bool updateSorting =
            sortType ==
                nsINavHistoryQueryOptions::SORT_BY_VISITCOUNT_ASCENDING ||
            sortType ==
                nsINavHistoryQueryOptions::SORT_BY_VISITCOUNT_DESCENDING ||
            sortType == nsINavHistoryQueryOptions::SORT_BY_DATE_ASCENDING ||
            sortType == nsINavHistoryQueryOptions::SORT_BY_DATE_DESCENDING ||
            sortType == nsINavHistoryQueryOptions::SORT_BY_FRECENCY_ASCENDING ||
            sortType == nsINavHistoryQueryOptions::SORT_BY_FRECENCY_DESCENDING;
        if (!UpdateURIs(
                false, true, updateSorting, addition->mURI,
                setHistoryDetailsCallback,
                const_cast<void*>(static_cast<void*>(addition.get())))) {
          // Couldn't find a node to update, we may want to add one.
          // If the query has search terms, ensure the new visit is matching
          // them.
          if (mHasSearchTerms &&
              !isQuerySearchTermsMatching(mQuery, addition)) {
            return NS_OK;
          }
          rv = InsertSortedChild(addition);
          NS_ENSURE_SUCCESS(rv, rv);
        }
      }

      // Trim the children if necessary.
      if (mOptions->MaxResults() &&
          mChildren.Length() > mOptions->MaxResults()) {
        mChildren.RemoveObjectAt(mChildren.Count() - 1);
      }

      if (aAdded) {
        ++(*aAdded);
      }

      break;
    }

    case QUERYUPDATE_COMPLEX:
    case QUERYUPDATE_COMPLEX_WITH_BOOKMARKS:
      // need to requery in complex cases
      return Refresh();

    default:
      MOZ_ASSERT(false, "Invalid value for mLiveUpdate");
      return Refresh();
  }

  return NS_OK;
}

/**
 * Find every node that matches this URI and rename it.  We try to do
 * incremental updates here, even when we are closed, because changing titles
 * is easier than requerying if we are invalid.
 *
 * This actually gets called a lot.  Typically, we will get an AddURI message
 * when the user visits the page, and then the title will be set asynchronously
 * when the title element of the page is parsed.
 */
nsresult nsNavHistoryQueryResultNode::OnTitleChanged(
    nsIURI* aURI, const nsAString& aPageTitle, const nsACString& aGUID) {
  if (!mExpanded) {
    // When we are not expanded, we don't update, just invalidate and unhook.
    // It would still be pretty easy to traverse the results and update the
    // titles, but when a title changes, its unlikely that it will be the only
    // thing.  Therefore, we just give up.
    ClearChildren(true);
    return NS_OK;  // no updates in tree state
  }

  nsNavHistoryResult* result = GetResult();
  NS_ENSURE_STATE(result);
  if (result->IsBatching() &&
      ++mBatchChanges > MAX_BATCH_CHANGES_BEFORE_REFRESH) {
    nsresult rv = Refresh();
    NS_ENSURE_SUCCESS(rv, rv);
    return NS_OK;
  }

  // compute what the new title should be
  NS_ConvertUTF16toUTF8 newTitle(aPageTitle);

  bool onlyOneEntry =
      mOptions->ResultType() == nsINavHistoryQueryOptions::RESULTS_AS_URI;

  // See if our query has any search term matching.
  if (mHasSearchTerms) {
    // Find all matching URI nodes.
    nsCOMArray<nsNavHistoryResultNode> matches;
    nsAutoCString spec;
    nsresult rv = aURI->GetSpec(spec);
    NS_ENSURE_SUCCESS(rv, rv);
    RecursiveFindURIs(onlyOneEntry, this, spec, &matches);

    if (matches.Count() == 0) {
      // If we didn't find any matching URI, and the query is filters by search
      // terms, maybe the new title will match and then we want to Refresh()
      // contents. Otherwise this will continue being an empty query.
      return isQuerySearchTermsMatching(mQuery, mURI, newTitle, mTags)
                 ? Refresh()
                 : NS_OK;
    }
    for (int32_t i = 0; i < matches.Count(); ++i) {
      // For each matched node we check if it passes the query filter, if not
      // we remove the node from the result, otherwise we'll update the title
      // later.
      nsNavHistoryResultNode* node = matches[i];
      // We must check the node with the new title.
      node->mTitle = newTitle;

      nsNavHistory* history = nsNavHistory::GetHistoryService();
      NS_ENSURE_TRUE(history, NS_ERROR_OUT_OF_MEMORY);
      if (!isQuerySearchTermsMatching(mQuery, node)) {
        nsNavHistoryContainerResultNode* parent = node->mParent;
        // URI nodes should always have parents
        NS_ENSURE_TRUE(parent, NS_ERROR_UNEXPECTED);
        int32_t childIndex = parent->FindChild(node);
        NS_ASSERTION(childIndex >= 0, "Child not found in parent");
        parent->RemoveChildAt(childIndex);
      }
    }
  }

  return ChangeTitles(aURI, newTitle, true, onlyOneEntry);
}

/**
 * Here, we can always live update by just deleting all occurrences of
 * the given URI.
 */
nsresult nsNavHistoryQueryResultNode::OnPageRemovedFromStore(
    nsIURI* aURI, const nsACString& aGUID, uint16_t aReason) {
  nsNavHistoryResult* result = GetResult();
  NS_ENSURE_STATE(result);
  if (result->IsBatching() &&
      ++mBatchChanges > MAX_BATCH_CHANGES_BEFORE_REFRESH) {
    nsresult rv = Refresh();
    NS_ENSURE_SUCCESS(rv, rv);
    return NS_OK;
  }

  if (IsContainersQuery()) {
    // Incremental updates of query returning queries are pretty much
    // complicated.  In this case it's possible one of the child queries has
    // no more children and it should be removed.  Unfortunately there is no
    // way to know that without executing the child query and counting results.
    nsresult rv = Refresh();
    NS_ENSURE_SUCCESS(rv, rv);
    return NS_OK;
  }

  bool onlyOneEntry =
      mOptions->ResultType() == nsINavHistoryQueryOptions::RESULTS_AS_URI;

  nsAutoCString spec;
  nsresult rv = aURI->GetSpec(spec);
  NS_ENSURE_SUCCESS(rv, rv);

  nsCOMArray<nsNavHistoryResultNode> matches;
  RecursiveFindURIs(onlyOneEntry, this, spec, &matches);
  for (int32_t i = 0; i < matches.Count(); ++i) {
    nsNavHistoryResultNode* node = matches[i];
    nsNavHistoryContainerResultNode* parent = node->mParent;
    // URI nodes should always have parents
    NS_ENSURE_TRUE(parent, NS_ERROR_UNEXPECTED);

    int32_t childIndex = parent->FindChild(node);
    NS_ASSERTION(childIndex >= 0, "Child not found in parent");
    parent->RemoveChildAt(childIndex);
    if (parent->mChildren.Count() == 0 && parent->IsQuery() &&
        parent->mIndentLevel > -1) {
      // When query subcontainers (like hosts) get empty we should remove them
      // as well.  If the parent is not the root node, append it to our list
      // and it will get evaluated later in the loop.
      matches.AppendObject(parent);
    }
  }
  return NS_OK;
}

nsresult nsNavHistoryQueryResultNode::OnClearHistory() {
  nsresult rv = Refresh();
  NS_ENSURE_SUCCESS(rv, rv);
  return NS_OK;
}

static nsresult setFaviconCallback(nsNavHistoryResultNode* aNode,
                                   const void* aClosure,
                                   const nsNavHistoryResult* aResult) {
  if (aResult && (!aNode->mParent || aNode->mParent->AreChildrenVisible())) {
    NOTIFY_RESULT_OBSERVERS(aResult, NodeIconChanged(aNode));
  }

  return NS_OK;
}

nsresult nsNavHistoryQueryResultNode::OnPageRemovedVisits(
    nsIURI* aURI, bool aPartialRemoval, const nsACString& aGUID,
    uint16_t aReason, uint32_t aTransitionType) {
  MOZ_ASSERT(
      mOptions->QueryType() == nsINavHistoryQueryOptions::QUERY_TYPE_HISTORY,
      "Bookmarks queries should not get a OnDeleteVisits notification");

  if (!aPartialRemoval) {
    // All visits for this uri have been removed, but the uri won't be removed
    // from the databse, most likely because it's a bookmark.  For a history
    // query this is equivalent to a OnPageRemovedFromStore notification.
    nsresult rv = OnPageRemovedFromStore(aURI, aGUID, aReason);
    NS_ENSURE_SUCCESS(rv, rv);
  } else if (aReason == PlacesVisitRemoved_Binding::REASON_DELETED &&
             isTimeFilteredQuery(mQuery)) {
    // If the query has time filters we must Refresh because we don't know
    // which visits have been removed, it could be all the visits in the
    // filtered timeframe.
    // We skip this for expired visits, to avoid surprising the user with pages
    // disappearing from the UI.
    return Refresh();
  }
  if (aTransitionType > 0) {
    // All visits for aTransitionType have been removed, if the query is
    // filtering on such transition type, this is equivalent to an
    // OnPageRemovedFromStore notification.
    if (mTransitions.Length() > 0 && mTransitions.Contains(aTransitionType)) {
      nsresult rv = OnPageRemovedFromStore(aURI, aGUID, aReason);
      NS_ENSURE_SUCCESS(rv, rv);
    }
  }

  return NS_OK;
}

/**
 * These are the bookmark observer functions for query nodes.  They listen
 * for bookmark events and refresh the results if we have any dependence on
 * the bookmark system.
 */
nsresult nsNavHistoryQueryResultNode::OnItemAdded(
    int64_t aItemId, int64_t aParentId, int32_t aIndex, uint16_t aItemType,
    nsIURI* aURI, PRTime aDateAdded, const nsACString& aGUID,
    const nsACString& aParentGUID, uint16_t aSource) {
  if (aItemType == nsINavBookmarksService::TYPE_BOOKMARK &&
      mLiveUpdate != QUERYUPDATE_SIMPLE && mLiveUpdate != QUERYUPDATE_TIME &&
      mLiveUpdate != QUERYUPDATE_MOBILEPREF) {
    nsresult rv = Refresh();
    NS_ENSURE_SUCCESS(rv, rv);
  }
  return NS_OK;
}

nsresult nsNavHistoryQueryResultNode::OnItemRemoved(
    int64_t aItemId, int64_t aParentFolder, int32_t aIndex, uint16_t aItemType,
    nsIURI* aURI, const nsACString& aGUID, const nsACString& aParentGUID,
    uint16_t aSource) {
  if ((aItemType == nsINavBookmarksService::TYPE_BOOKMARK ||
       (aItemType == nsINavBookmarksService::TYPE_FOLDER &&
        mOptions->ResultType() ==
            nsINavHistoryQueryOptions::RESULTS_AS_TAGS_ROOT &&
        aParentGUID.EqualsLiteral(TAGS_ROOT_GUID))) &&
      mLiveUpdate != QUERYUPDATE_SIMPLE && mLiveUpdate != QUERYUPDATE_TIME &&
      mLiveUpdate != QUERYUPDATE_MOBILEPREF) {
    nsresult rv = Refresh();
    NS_ENSURE_SUCCESS(rv, rv);
  }
  return NS_OK;
}

nsresult nsNavHistoryQueryResultNode::OnItemMoved(
    int64_t aFolder, int32_t aOldIndex, int32_t aNewIndex, uint16_t aItemType,
    const nsACString& aGUID, const nsACString& aOldParentGUID,
    const nsACString& aNewParentGUID, uint16_t aSource,
    const nsACString& aURI) {
  // 1. The query cannot be affected by the item's position
  // 2. For the time being, we cannot optimize this not to update
  //    queries which are not restricted to some folders, due to way
  //    sub-queries are updated (see Refresh)
  if (mLiveUpdate == QUERYUPDATE_COMPLEX_WITH_BOOKMARKS &&
      aItemType != nsINavBookmarksService::TYPE_SEPARATOR &&
      !aNewParentGUID.Equals(aOldParentGUID)) {
    return Refresh();
  }
  return NS_OK;
}

nsresult nsNavHistoryQueryResultNode::OnItemTagsChanged(
    int64_t aItemId, const nsAString& aURL, const nsAString& aTags) {
  nsresult rv = nsNavHistoryResultNode::OnItemTagsChanged(aItemId, aURL, aTags);
  NS_ENSURE_SUCCESS(rv, rv);

  nsNavHistoryResult* result = GetResult();
  NS_ENSURE_STATE(result);

  // Check whether aURL is actually URI.
  nsCOMPtr<nsIURI> uri;
  rv = NS_NewURI(getter_AddRefs(uri), aURL);
  NS_ENSURE_SUCCESS(rv, rv);
  nsAutoCString spec;
  rv = uri->GetSpec(spec);
  NS_ENSURE_SUCCESS(rv, rv);

  bool onlyOneEntry =
      mOptions->ResultType() == nsINavHistoryQueryOptions::RESULTS_AS_URI;

  nsCOMArray<nsNavHistoryResultNode> matches;
  RecursiveFindURIs(onlyOneEntry, this, spec, &matches);

  if (matches.Count() == 0 && mHasSearchTerms) {
    return isQuerySearchTermsMatching(mQuery, this) ? Refresh() : NS_OK;
  }

  for (int32_t i = 0; i < matches.Count(); ++i) {
    nsNavHistoryResultNode* node = matches[i];
    node->SetTags(aTags);
    // It's possible now this node does not respect anymore the conditions.
    // In such a case it should be removed.
    if (mHasSearchTerms && !isQuerySearchTermsMatching(mQuery, node)) {
      nsNavHistoryContainerResultNode* parent = node->mParent;
      // URI nodes should always have parents
      NS_ENSURE_TRUE(parent, NS_ERROR_UNEXPECTED);
      int32_t childIndex = parent->FindChild(node);
      NS_ASSERTION(childIndex >= 0, "Child not found in parent");
      parent->RemoveChildAt(childIndex);
    } else {
      NOTIFY_RESULT_OBSERVERS(result, NodeTagsChanged(node));
    }
  }

  return NS_OK;
}

nsresult nsNavHistoryQueryResultNode::OnItemTimeChanged(int64_t aItemId,
                                                        const nsACString& aGUID,
                                                        PRTime aDateAdded,
                                                        PRTime aLastModified) {
  // Update ourselves first.
  nsresult rv = nsNavHistoryResultNode::OnItemTimeChanged(
      aItemId, aGUID, aDateAdded, aLastModified);
  NS_ENSURE_SUCCESS(rv, rv);

  if (mLiveUpdate == QUERYUPDATE_COMPLEX_WITH_BOOKMARKS) {
    return Refresh();
  }

  return NS_OK;
}

nsresult nsNavHistoryQueryResultNode::OnItemTitleChanged(
    int64_t aItemId, const nsACString& aGUID, const nsACString& aTitle,
    PRTime aLastModified) {
  // Update ourselves first.
  nsresult rv = nsNavHistoryResultNode::OnItemTitleChanged(
      aItemId, aGUID, aTitle, aLastModified);
  NS_ENSURE_SUCCESS(rv, rv);

  if (mLiveUpdate == QUERYUPDATE_COMPLEX_WITH_BOOKMARKS) {
    return Refresh();
  }

  return NS_OK;
}

nsresult nsNavHistoryQueryResultNode::OnItemUrlChanged(int64_t aItemId,
                                                       const nsACString& aGUID,
                                                       const nsACString& aURL,
                                                       PRTime aLastModified) {
  if (aItemId == mItemId) {
    nsresult rv = nsNavHistoryResultNode::OnItemUrlChanged(aItemId, aGUID, aURL,
                                                           aLastModified);
    NS_ENSURE_SUCCESS(rv, rv);

    nsNavHistory* history = nsNavHistory::GetHistoryService();
    NS_ENSURE_TRUE(history, NS_ERROR_OUT_OF_MEMORY);
    nsCOMPtr<nsINavHistoryQuery> query;
    nsCOMPtr<nsINavHistoryQueryOptions> options;
    rv = history->QueryStringToQuery(mURI, getter_AddRefs(query),
                                     getter_AddRefs(options));
    NS_ENSURE_SUCCESS(rv, rv);
    mQuery = do_QueryObject(query);
    NS_ENSURE_STATE(mQuery);
    mOptions = do_QueryObject(options);
    NS_ENSURE_STATE(mOptions);
    rv = mOptions->Clone(getter_AddRefs(mOriginalOptions));
    NS_ENSURE_SUCCESS(rv, rv);

    return Refresh();
  }

  if (mLiveUpdate == QUERYUPDATE_COMPLEX_WITH_BOOKMARKS) {
    return Refresh();
  }

  int32_t index;
  nsNavHistoryResultNode* node = FindChildById(aItemId, &index);
  if (node) {
    return node->OnItemUrlChanged(aItemId, aGUID, aURL, aLastModified);
  }

  return NS_OK;
}

/**
 * HOW DYNAMIC FOLDER UPDATING WORKS
 *
 * When you create a result, it will automatically keep itself in sync with
 * stuff that happens in the system.  For folder nodes, this means changes to
 * bookmarks.
 *
 * A folder will fill its children "when necessary." This means it is being
 * opened or whether we need to see if it is empty for twisty drawing.  It will
 * then register its ID with the main result object that owns it.  This result
 * object will listen for all bookmark notifications and pass those
 * notifications to folder nodes that have registered for that specific folder
 * ID.
 *
 * When a bookmark folder is closed, it will not clear its children.  Instead,
 * it will keep them and also stay registered as a listener.  This means that
 * you can more quickly re-open the same folder without doing any work.  This
 * happens a lot for menus, and bookmarks don't change very often.
 *
 * When a message comes in and the folder is open, we will do the correct
 * operations to keep ourselves in sync with the bookmark service.  If the
 * folder is closed, we just clear our list to mark it as invalid and
 * unregister as a listener.  This means we do not have to keep maintaining
 * an up-to-date list for the entire bookmark menu structure in every place
 * it is used.
 */
NS_IMPL_ISUPPORTS_INHERITED(nsNavHistoryFolderResultNode,
                            nsNavHistoryContainerResultNode,
                            nsINavHistoryQueryResultNode,
                            mozIStorageStatementCallback)

nsNavHistoryFolderResultNode::nsNavHistoryFolderResultNode(
    int64_t aItemId, const nsACString& aBookmarkGuid,
    int64_t aTargetFolderItemId, const nsACString& aTargetFolderGuid,
    const nsACString& aTitle, nsNavHistoryQueryOptions* aOptions)
    : nsNavHistoryContainerResultNode(
          ""_ns, aTitle, 0, nsNavHistoryResultNode::RESULT_TYPE_FOLDER,
          aOptions),
      mContentsValid(false),
      mTargetFolderItemId(aTargetFolderItemId),
      mTargetFolderGuid(aTargetFolderGuid),
      mIsRegisteredFolderObserver(false),
      mAsyncBookmarkIndex(-1) {
  mItemId = aItemId;
  mBookmarkGuid = aBookmarkGuid;
}

nsNavHistoryFolderResultNode::~nsNavHistoryFolderResultNode() {
  if (mIsRegisteredFolderObserver && mResult) {
    mResult->RemoveBookmarkFolderObserver(this, mTargetFolderGuid);
  }
}

/**
 * Here we do not want to call ContainerResultNode::OnRemoving since our own
 * ClearChildren will do the same thing and more (unregister the observers).
 * The base ResultNode::OnRemoving will clear some regular node stats, so it is
 * OK.
 */
void nsNavHistoryFolderResultNode::OnRemoving() {
  nsNavHistoryResultNode::OnRemoving();
  ClearChildren(true);
  mResult = nullptr;
}

nsresult nsNavHistoryFolderResultNode::OpenContainer() {
  NS_ASSERTION(!mExpanded, "Container must be expanded to close it");
  nsresult rv;

  if (!mContentsValid) {
    rv = FillChildren();
    NS_ENSURE_SUCCESS(rv, rv);
  }
  mExpanded = true;

  rv = NotifyOnStateChange(STATE_CLOSED);
  NS_ENSURE_SUCCESS(rv, rv);

  return NS_OK;
}

/**
 * The async version of OpenContainer.
 */
nsresult nsNavHistoryFolderResultNode::OpenContainerAsync() {
  NS_ASSERTION(!mExpanded, "Container already expanded when opening it");

  // If the children are valid, open the container synchronously.  This will be
  // the case when the container has already been opened and any other time
  // FillChildren or FillChildrenAsync has previously been called.
  if (mContentsValid) return OpenContainer();

  nsresult rv = FillChildrenAsync();
  NS_ENSURE_SUCCESS(rv, rv);

  rv = NotifyOnStateChange(STATE_CLOSED);
  NS_ENSURE_SUCCESS(rv, rv);

  return NS_OK;
}

/**
 * @see nsNavHistoryQueryResultNode::HasChildren.  The semantics here are a
 * little different.  Querying the contents of a bookmark folder is relatively
 * fast and it is common to have empty folders.  Therefore, we always want to
 * return the correct result so that twisties are drawn properly.
 */
NS_IMETHODIMP
nsNavHistoryFolderResultNode::GetHasChildren(bool* aHasChildren) {
  if (!mContentsValid) {
    nsresult rv = FillChildren();
    NS_ENSURE_SUCCESS(rv, rv);
  }
  *aHasChildren = (mChildren.Count() > 0);
  return NS_OK;
}

NS_IMETHODIMP
nsNavHistoryFolderResultNode::GetFolderItemId(int64_t* aItemId) {
  *aItemId = mTargetFolderItemId;
  return NS_OK;
}

NS_IMETHODIMP
nsNavHistoryFolderResultNode::GetTargetFolderGuid(nsACString& aGuid) {
  aGuid = mTargetFolderGuid;
  return NS_OK;
}

/**
 * Lazily computes the URI for this specific folder query with the current
 * options.
 */
NS_IMETHODIMP
nsNavHistoryFolderResultNode::GetUri(nsACString& aURI) {
  if (!mURI.IsEmpty()) {
    aURI = mURI;
    return NS_OK;
  }

  nsCOMPtr<nsINavHistoryQuery> query;
  nsresult rv = GetQuery(getter_AddRefs(query));
  NS_ENSURE_SUCCESS(rv, rv);

  nsNavHistory* history = nsNavHistory::GetHistoryService();
  NS_ENSURE_TRUE(history, NS_ERROR_OUT_OF_MEMORY);
  rv = history->QueryToQueryString(query, mOriginalOptions, mURI);
  NS_ENSURE_SUCCESS(rv, rv);
  aURI = mURI;
  return NS_OK;
}

/**
 * @return the queries that give you this bookmarks folder
 */
NS_IMETHODIMP
nsNavHistoryFolderResultNode::GetQuery(nsINavHistoryQuery** _query) {
  // get the query object
  RefPtr<nsNavHistoryQuery> query = new nsNavHistoryQuery();

  nsTArray<nsCString> parents;
  // query just has the folder ID set and nothing else
  // XXX(Bug 1631371) Check if this should use a fallible operation as it
  // pretended earlier, or change the return type to void.
  parents.AppendElement(mTargetFolderGuid);
  nsresult rv = query->SetParents(parents);
  NS_ENSURE_SUCCESS(rv, rv);

  query.forget(_query);
  return NS_OK;
}

/**
 * Options for the query that gives you this bookmarks folder.  This is just
 * the options for the folder with the current folder ID set.
 */
NS_IMETHODIMP
nsNavHistoryFolderResultNode::GetQueryOptions(
    nsINavHistoryQueryOptions** _options) {
  MOZ_ASSERT(mOptions, "Options should be valid");
  RefPtr<nsNavHistoryQueryOptions> options = mOptions;
  options.forget(_options);
  return NS_OK;
}

nsresult nsNavHistoryFolderResultNode::FillChildren() {
  nsresult rv = FillChildrenInternal();
  NS_ENSURE_SUCCESS(rv, rv);

  // PERFORMANCE: it may be better to also fill any child folders at this point
  // so that we can draw tree twisties without doing a separate query later.
  // If we don't end up drawing twisties a lot, it doesn't matter. If we do
  // this, we should wrap everything in a transaction here on the bookmark
  // service's connection.

  return OnChildrenFilled();
}

/**
 * Performs some tasks after all the children of the container have been added.
 * The container's contents are not valid until this method has been called.
 */
nsresult nsNavHistoryFolderResultNode::OnChildrenFilled() {
  // It is important to call FillStats to fill in the parents on all
  // nodes and the result node pointers on the containers.
  FillStats();

  if (mResult && mResult->mNeedsToApplySortingMode) {
    // We should repopulate container and then apply sortingMode.  To avoid
    // sorting 2 times we simply do that here.
    mResult->SetSortingMode(mResult->mSortingMode);
  } else {
    // Once we've computed all tree stats, we can sort, because containers will
    // then have proper visit counts and dates.
    SortComparator comparator = GetSortingComparator(GetSortType());
    if (comparator) {
      RecursiveSort(comparator);
    }
  }

  // If we are limiting our results remove items from the end of the
  // mChildren array after sorting.  This is done for root node only.
  // Note, if count < max results, we won't do anything.
  if (!mParent && mOptions->MaxResults()) {
    while (mChildren.Length() > mOptions->MaxResults()) {
      mChildren.RemoveObjectAt(mChildren.Count() - 1);
    }
  }

  // Register with the result for updates.
  EnsureRegisteredAsFolderObserver();

  mContentsValid = true;
  return NS_OK;
}

/**
 * Registers the node with its result as a folder observer if it is not already
 * registered.
 */
void nsNavHistoryFolderResultNode::EnsureRegisteredAsFolderObserver() {
  if (!mIsRegisteredFolderObserver && mResult) {
    mResult->AddBookmarkFolderObserver(this, mTargetFolderGuid);
    mIsRegisteredFolderObserver = true;
  }
}

/**
 * The async version of FillChildren. This begins asynchronous execution by
 * calling FillChildrenInternal.  During execution, this node's async Storage
 * callbacks, HandleResult and HandleCompletion, will be called.
 */
nsresult nsNavHistoryFolderResultNode::FillChildrenAsync() {
  // ProcessFolderNodeChild, called in HandleResult, increments this for every
  // result row it processes.  Initialize it here as we begin async execution.
  mAsyncBookmarkIndex = -1;

  nsresult rv = FillChildrenInternal(getter_AddRefs(mAsyncPendingStmt));
  NS_ENSURE_SUCCESS(rv, rv);

  // Register with the result for updates.  All updates during async execution
  // will cause it to be restarted.
  EnsureRegisteredAsFolderObserver();

  return NS_OK;
}

nsresult nsNavHistoryFolderResultNode::FillChildrenInternal(
    mozIStoragePendingStatement** aPendingStmt) {
  NS_ASSERTION(!mContentsValid,
               "Don't call FillChildrenInternal when contents are valid");
  NS_ASSERTION(mChildren.Count() == 0,
               "We are trying to fill children when there already are some");

  // Select all children of a given folder, sorted by position.
  // This is a LEFT JOIN because not all bookmarks types have a place.
  // We construct a result where the first columns exactly match those returned
  // by mDBGetURLPageInfo, and additionally contains columns for position,
  // item_child, and folder_child from moz_bookmarks.
  bool isAsync = !!aPendingStmt;
  // Tags are fetched per child, as pre-aggregating all the tags of the profile
  // would be expensive for the common small folders.
  bool fetchTags = !isAsync && !mOptions->ExcludeItems();
  nsCString sql =
      "SELECT "
      "  h.id, h.url, b.title, h.rev_host, h.visit_count, h.last_visit_date, "
      "  null, b.id, b.dateAdded, b.lastModified, b.parent, "_ns +
      nsCString(fetchTags ? " (SELECT group_concat(p.title ORDER BY p.title) "
                            "  FROM moz_bookmarks bt "
                            "  JOIN moz_bookmarks p ON p.id = bt.parent "
                            "  WHERE bt.fk = h.id AND p.parent = ( "
                            "    SELECT id FROM moz_bookmarks "
                            "    WHERE guid = " SQL_QUOTE(TAGS_ROOT_GUID) " "
                            "  ) "
                            " ) AS tags, "
                          : " null, ") +
      "  h.frecency, h.hidden, h.guid, null, null, null, b.guid, b.position, "
      "  b.type, b.fk, t.guid, t.id, t.title "
      "FROM moz_bookmarks b "
      "LEFT JOIN moz_places h ON b.fk = h.id "
      "LEFT JOIN moz_bookmarks t ON t.guid = target_folder_guid(h.url) "
      "WHERE b.parent = :parent "
      "  AND ("
      "    NOT :excludeItems OR "
      "    b.type = :folder OR "
      "    h.url_hash BETWEEN "
      "      hash('place', 'prefix_lo') AND hash('place', 'prefix_hi')"
      "  ) "
      "ORDER BY b.position ASC"_ns;

  RefPtr<Database> db = Database::GetDatabase();
  NS_ENSURE_STATE(db);
  nsCOMPtr<mozIStorageBaseStatement> stmt;
  if (isAsync) {
    stmt = db->GetAsyncStatement(sql);
    NS_ENSURE_STATE(stmt);
  } else {
    stmt = db->GetStatement(sql);
    NS_ENSURE_STATE(stmt);
  }

  nsresult rv = stmt->BindInt64ByName("parent"_ns, mTargetFolderItemId);
  NS_ENSURE_SUCCESS(rv, rv);
  rv = stmt->BindInt32ByName("folder"_ns, nsINavBookmarksService::TYPE_FOLDER);
  NS_ENSURE_SUCCESS(rv, rv);
  rv = stmt->BindInt32ByName("excludeItems"_ns, mOptions->ExcludeItems());
  NS_ENSURE_SUCCESS(rv, rv);

  if (isAsync) {
    nsCOMPtr<mozIStorageAsyncStatement> async = do_QueryInterface(stmt, &rv);
    NS_ENSURE_SUCCESS(rv, rv);

    nsCOMPtr<mozIStoragePendingStatement> pendingStmt;
    rv = async->ExecuteAsync(this, getter_AddRefs(pendingStmt));
    NS_ENSURE_SUCCESS(rv, rv);

    NS_IF_ADDREF(*aPendingStmt = pendingStmt);
  } else {
    nsCOMPtr<mozIStorageStatement> sync = do_QueryInterface(stmt, &rv);
    NS_ENSURE_SUCCESS(rv, rv);
    mozStorageStatementScoper scoper(sync);
    nsCOMPtr<mozIStorageValueArray> row = do_QueryInterface(sync, &rv);
    NS_ENSURE_SUCCESS(rv, rv);

    int32_t index = -1;
    bool hasResult;
    while (NS_SUCCEEDED(sync->ExecuteStep(&hasResult)) && hasResult) {
      rv = AppendRowAsChild(row, index);
      NS_ENSURE_SUCCESS(rv, rv);
    }
  }

  return NS_OK;
}

nsresult nsNavHistoryFolderResultNode::AppendRowAsChild(
    mozIStorageValueArray* aRow, int32_t& aCurrentIndex) {
  NS_ENSURE_ARG_POINTER(aRow);

  // The results will be in order of aCurrentIndex. Even if we don't add a node
  // because it was excluded, we need to count its index, so do that before
  // doing anything else.
  aCurrentIndex++;

  int32_t itemType;
  nsresult rv =
      aRow->GetInt32(nsNavBookmarks::kGetChildrenIndex_Type, &itemType);
  NS_ENSURE_SUCCESS(rv, rv);
  int64_t id;
  rv = aRow->GetInt64(nsNavHistory::kGetInfoIndex_ItemId, &id);
  NS_ENSURE_SUCCESS(rv, rv);

  RefPtr<nsNavHistoryResultNode> node;

  if (itemType == nsINavBookmarksService::TYPE_BOOKMARK) {
    nsNavHistory* history = nsNavHistory::GetHistoryService();
    NS_ENSURE_TRUE(history, NS_ERROR_OUT_OF_MEMORY);
    rv = history->RowToResult(aRow, mOptions, getter_AddRefs(node));
    NS_ENSURE_SUCCESS(rv, rv);
    uint32_t nodeType;
    node->GetType(&nodeType);
    if (nodeType == nsINavHistoryResultNode::RESULT_TYPE_QUERY &&
        mOptions->ExcludeQueries()) {
      return NS_OK;
    }
  } else if (itemType == nsINavBookmarksService::TYPE_FOLDER) {
    nsAutoCString title;
    bool isNull;
    rv = aRow->GetIsNull(nsNavHistory::kGetInfoIndex_Title, &isNull);
    NS_ENSURE_SUCCESS(rv, rv);
    if (!isNull) {
      rv = aRow->GetUTF8String(nsNavHistory::kGetInfoIndex_Title, title);
      NS_ENSURE_SUCCESS(rv, rv);
    }

    nsAutoCString guid;
    rv = aRow->GetUTF8String(nsNavBookmarks::kGetChildrenIndex_Guid, guid);
    NS_ENSURE_SUCCESS(rv, rv);

    // Don't use options from the parent to build the new folder node, it will
    // inherit those later when it's inserted in the result.
    node = new nsNavHistoryFolderResultNode(id, guid, id, guid, title,
                                            new nsNavHistoryQueryOptions());

    rv = aRow->GetInt64(nsNavHistory::kGetInfoIndex_ItemDateAdded,
                        reinterpret_cast<int64_t*>(&node->mDateAdded));
    NS_ENSURE_SUCCESS(rv, rv);
    rv = aRow->GetInt64(nsNavHistory::kGetInfoIndex_ItemLastModified,
                        reinterpret_cast<int64_t*>(&node->mLastModified));
    NS_ENSURE_SUCCESS(rv, rv);
  } else {
    // This is a separator.
    node = new nsNavHistorySeparatorResultNode();

    node->mItemId = id;
    rv = aRow->GetUTF8String(nsNavBookmarks::kGetChildrenIndex_Guid,
                             node->mBookmarkGuid);
    NS_ENSURE_SUCCESS(rv, rv);
    rv = aRow->GetInt64(nsNavHistory::kGetInfoIndex_ItemDateAdded,
                        reinterpret_cast<int64_t*>(&node->mDateAdded));
    NS_ENSURE_SUCCESS(rv, rv);
    rv = aRow->GetInt64(nsNavHistory::kGetInfoIndex_ItemLastModified,
                        reinterpret_cast<int64_t*>(&node->mLastModified));
    NS_ENSURE_SUCCESS(rv, rv);
  }

  // Store the index of the node within this container.  Note that this is not
  // moz_bookmarks.position.
  node->mBookmarkIndex = aCurrentIndex;

  NS_ENSURE_TRUE(mChildren.AppendObject(node), NS_ERROR_OUT_OF_MEMORY);
  return NS_OK;
}

/**
 * A mozIStorageStatementCallback method.  Called during the async execution
 * begun by FillChildrenAsync.
 *
 * @param aResultSet
 *        The result set containing the data from the database.
 */
NS_IMETHODIMP
nsNavHistoryFolderResultNode::HandleResult(mozIStorageResultSet* aResultSet) {
  NS_ENSURE_ARG_POINTER(aResultSet);

  // Consume all the currently available rows of the result set.
  nsCOMPtr<mozIStorageRow> row;
  while (NS_SUCCEEDED(aResultSet->GetNextRow(getter_AddRefs(row))) && row) {
    nsresult rv = AppendRowAsChild(row, mAsyncBookmarkIndex);
    if (NS_FAILED(rv)) {
      CancelAsyncOpen(false);
      return rv;
    }
  }

  return NS_OK;
}

/**
 * A mozIStorageStatementCallback method.  Called during the async execution
 * begun by FillChildrenAsync.
 *
 * @param aReason
 *        Indicates the final state of execution.
 */
NS_IMETHODIMP
nsNavHistoryFolderResultNode::HandleCompletion(uint16_t aReason) {
  if (aReason == mozIStorageStatementCallback::REASON_FINISHED &&
      mAsyncCanceledState == NOT_CANCELED) {
    // Async execution successfully completed.  The container is ready to open.

    nsresult rv = OnChildrenFilled();
    NS_ENSURE_SUCCESS(rv, rv);

    mExpanded = true;
    mAsyncPendingStmt = nullptr;

    // Notify observers only after mExpanded and mAsyncPendingStmt are set.
    rv = NotifyOnStateChange(STATE_LOADING);
    NS_ENSURE_SUCCESS(rv, rv);
  }

  else if (mAsyncCanceledState == CANCELED_RESTART_NEEDED) {
    // Async execution was canceled and needs to be restarted.
    mAsyncCanceledState = NOT_CANCELED;
    ClearChildren(false);
    FillChildrenAsync();
  }

  else {
    // Async execution failed or was canceled without restart.  Remove all
    // children and close the container, notifying observers.
    mAsyncCanceledState = NOT_CANCELED;
    ClearChildren(true);
    CloseContainer();
  }

  return NS_OK;
}

void nsNavHistoryFolderResultNode::ClearChildren(bool unregister) {
  for (int32_t i = 0; i < mChildren.Count(); ++i) mChildren[i]->OnRemoving();
  mChildren.Clear();

  bool needsUnregister = unregister && (mContentsValid || mAsyncPendingStmt);
  if (needsUnregister && mResult && mIsRegisteredFolderObserver) {
    mResult->RemoveBookmarkFolderObserver(this, mTargetFolderGuid);
    mIsRegisteredFolderObserver = false;
  }
  mContentsValid = false;
}

/**
 * This is called to update the result when something has changed that we
 * can not incrementally update.
 */
nsresult nsNavHistoryFolderResultNode::Refresh() {
  nsNavHistoryResult* result = GetResult();
  NS_ENSURE_STATE(result);
  if (result->IsBatching()) {
    result->requestRefresh(this);
    return NS_OK;
  }

  ClearChildren(true);

  if (!mExpanded) {
    // When we are not expanded, we don't update, just invalidate and unhook.
    return NS_OK;
  }

  // Ignore errors from FillChildren, since we will still want to refresh
  // the tree (there just might not be anything in it on error).  ClearChildren
  // has unregistered us as an observer since FillChildren will try to
  // re-register us.
  (void)FillChildren();

  NOTIFY_RESULT_OBSERVERS(result, InvalidateContainer(TO_CONTAINER(this)));
  return NS_OK;
}

/**
 * Implements the logic described above the constructor.  This sees if we
 * should do an incremental update and returns true if so.  If not, it
 * invalidates our children, unregisters us an observer, and returns false.
 */
bool nsNavHistoryFolderResultNode::StartIncrementalUpdate() {
  // if any items are excluded, we can not do incremental updates since the
  // indices from the bookmark service will not be valid

  if (!mOptions->ExcludeItems() && !mOptions->ExcludeQueries()) {
    // easy case: we are visible, always do incremental update
    if (mExpanded || AreChildrenVisible()) return true;

    nsNavHistoryResult* result = GetResult();
    NS_ENSURE_TRUE(result, false);

    // When any observers are attached also do incremental updates if our
    // parent is visible, so that twisties are drawn correctly.
    if (mParent) return result->mObservers.Length() > 0;
  }

  // otherwise, we don't do incremental updates, invalidate and unregister
  (void)Refresh();
  return false;
}

/**
 * This function adds aDelta to all bookmark indices between the two endpoints,
 * inclusive.  It is used when items are added or removed from the bookmark
 * folder.
 */
void nsNavHistoryFolderResultNode::ReindexRange(int32_t aStartIndex,
                                                int32_t aEndIndex,
                                                int32_t aDelta) {
  for (int32_t i = 0; i < mChildren.Count(); ++i) {
    nsNavHistoryResultNode* node = mChildren[i];
    if (node->mBookmarkIndex >= aStartIndex &&
        node->mBookmarkIndex <= aEndIndex) {
      node->mBookmarkIndex += aDelta;
    }
  }
}

// Used by nsNavHistoryFolderResultNode's methods below. If the container is
// notified of a bookmark event while asynchronous execution is pending, this
// restarts it and returns.
#define RESTART_AND_RETURN_IF_ASYNC_PENDING() \
  if (mAsyncPendingStmt) {                    \
    CancelAsyncOpen(true);                    \
    return NS_OK;                             \
  }

nsresult nsNavHistoryFolderResultNode::OnBeginUpdateBatch() { return NS_OK; }

nsresult nsNavHistoryFolderResultNode::OnEndUpdateBatch() { return NS_OK; }

nsresult nsNavHistoryFolderResultNode::OnItemAdded(
    int64_t aItemId, int64_t aParentFolder, int32_t aIndex, uint16_t aItemType,
    nsIURI* aURI, PRTime aDateAdded, const nsACString& aGUID,
    const nsACString& aParentGUID, uint16_t aSource, const nsACString& aTitle,
    const nsAString& aTags, int64_t aFrecency, bool aHidden,
    uint32_t aVisitCount, PRTime aLastVisitDate, int64_t aTargetFolderItemId,
    const nsACString& aTargetFolderGuid, const nsACString& aTargetFolderTitle) {
  MOZ_ASSERT(aParentFolder == mTargetFolderItemId, "Got wrong bookmark update");

  RESTART_AND_RETURN_IF_ASYNC_PENDING();

  {
    int32_t index;
    nsNavHistoryResultNode* node = FindChildById(aItemId, &index);
    // Bug 1097528.
    // It's possible our result registered due to a previous notification, for
    // example the Library left pane could have refreshed and replaced the
    // right pane as a consequence. In such a case our contents are already
    // up-to-date.  That's OK.
    if (node) return NS_OK;
  }

  bool excludeItems = mOptions->ExcludeItems();

  // here, try to do something reasonable if the bookmark service gives us
  // a bogus index.
  if (aIndex < 0) {
    MOZ_ASSERT_UNREACHABLE("Invalid index for item adding: <0");
    aIndex = 0;
  } else if (aIndex > mChildren.Count()) {
    if (!excludeItems) {
      // Something wrong happened while updating indexes.
      MOZ_ASSERT_UNREACHABLE(
          "Invalid index for item adding: greater than "
          "count");
    }
    aIndex = mChildren.Count();
  }

  nsresult rv;

  // Check for query URIs, which are bookmarks, but treated as containers
  // in results and views.
  bool isQuery = false;
  nsAutoCString itemURISpec;
  if (aItemType == nsINavBookmarksService::TYPE_BOOKMARK) {
    NS_ASSERTION(aURI, "Got a null URI when we are a bookmark?!");
    rv = aURI->GetSpec(itemURISpec);
    NS_ENSURE_SUCCESS(rv, rv);
    isQuery = IsQueryURI(itemURISpec);
  }

  if (aItemType != nsINavBookmarksService::TYPE_FOLDER && !isQuery &&
      excludeItems) {
    // don't update items when we aren't displaying them, but we still need
    // to adjust bookmark indices to account for the insertion
    ReindexRange(aIndex, INT32_MAX, 1);
    return NS_OK;
  }

  if (!StartIncrementalUpdate()) {
    return NS_OK;  // folder was completely refreshed for us
  }

  // adjust indices to account for insertion
  ReindexRange(aIndex, INT32_MAX, 1);

  RefPtr<nsNavHistoryResultNode> node;
  if (aItemType == nsINavBookmarksService::TYPE_BOOKMARK) {
    if (isQuery) {
      nsNavHistory* history = nsNavHistory::GetHistoryService();
      NS_ENSURE_TRUE(history, NS_ERROR_OUT_OF_MEMORY);
      rv = history->QueryUriToResult(itemURISpec, aItemId, aGUID, aTitle,
                                     aTargetFolderItemId, aTargetFolderGuid,
                                     aTargetFolderTitle, aVisitCount,
                                     aLastVisitDate, getter_AddRefs(node));
      NS_ENSURE_SUCCESS(rv, rv);
    } else {
      node = new nsNavHistoryResultNode(itemURISpec, aTitle, aVisitCount,
                                        aLastVisitDate);
      node->mItemId = aItemId;
      node->mBookmarkGuid = aGUID;
    }

    node->SetTags(aTags);
    node->mDateAdded = aDateAdded;
    node->mLastModified = aDateAdded;
    node->mFrecency = aFrecency;
    node->mHidden = aHidden;
  } else if (aItemType == nsINavBookmarksService::TYPE_FOLDER) {
    node = new nsNavHistoryFolderResultNode(
        aItemId, aGUID, aItemId, aGUID, aTitle, new nsNavHistoryQueryOptions());
    node->mDateAdded = aDateAdded;
    node->mLastModified = aDateAdded;
  } else if (aItemType == nsINavBookmarksService::TYPE_SEPARATOR) {
    node = new nsNavHistorySeparatorResultNode();
    node->mItemId = aItemId;
    node->mBookmarkGuid = aGUID;
    node->mDateAdded = aDateAdded;
    node->mLastModified = aDateAdded;
  }

  node->mBookmarkIndex = aIndex;

  if (aItemType == nsINavBookmarksService::TYPE_SEPARATOR ||
      GetSortType() == nsINavHistoryQueryOptions::SORT_BY_NONE) {
    // insert at natural bookmarks position
    return InsertChildAt(node, aIndex);
  }

  // insert at sorted position
  return InsertSortedChild(node);
}

nsresult nsNavHistoryQueryResultNode::OnMobilePrefChanged(bool newValue) {
  RESTART_AND_RETURN_IF_ASYNC_PENDING();

  if (newValue) {
    // If the folder is being added, simply refresh the query as that is
    // simpler than re-inserting just the mobile folder.
    return Refresh();
  }

  // We're removing the mobile folder, so find it.
  int32_t existingIndex;
  FindChildByGuid(nsLiteralCString(MOBILE_BOOKMARKS_VIRTUAL_GUID),
                  &existingIndex);

  if (existingIndex == -1) {
    return NS_OK;
  }

  return RemoveChildAt(existingIndex);
}

nsresult nsNavHistoryFolderResultNode::OnItemRemoved(
    int64_t aItemId, int64_t aParentFolder, int32_t aIndex, uint16_t aItemType,
    nsIURI* aURI, const nsACString& aGUID, const nsACString& aParentGUID,
    uint16_t aSource) {
  // Folder shortcuts should not be notified removal of the target folder.
  MOZ_ASSERT_IF(mItemId != mTargetFolderItemId, aItemId != mTargetFolderItemId);
  // Concrete folders should not be notified their own removal.
  // Note aItemId may equal mItemId for recursive folder shortcuts.
  MOZ_ASSERT_IF(mItemId == mTargetFolderItemId, aItemId != mItemId);

  // In any case though, here we only care about the children removal.
  if (mTargetFolderItemId == aItemId || mItemId == aItemId) return NS_OK;

  MOZ_ASSERT(aParentFolder == mTargetFolderItemId, "Got wrong bookmark update");

  RESTART_AND_RETURN_IF_ASYNC_PENDING();

  // don't trust the index from the bookmark service, find it ourselves.  The
  // sorting could be different, or the bookmark services indices and ours might
  // be out of sync somehow.
  int32_t index;
  nsNavHistoryResultNode* node = FindChildById(aItemId, &index);
  // Bug 1097528.
  // It's possible our result registered due to a previous notification, for
  // example the Library left pane could have refreshed and replaced the
  // right pane as a consequence. In such a case our contents are already
  // up-to-date.  That's OK.
  if (!node) {
    return NS_OK;
  }

  bool excludeItems = mOptions->ExcludeItems();

  if ((node->IsURI() || node->IsSeparator()) && excludeItems) {
    // don't update items when we aren't displaying them, but we do need to
    // adjust everybody's bookmark indices to account for the removal
    ReindexRange(aIndex, INT32_MAX, -1);
    return NS_OK;
  }

  if (!StartIncrementalUpdate()) return NS_OK;  // we are completely refreshed

  // shift all following indices down
  ReindexRange(aIndex + 1, INT32_MAX, -1);

  return RemoveChildAt(index);
}

nsresult nsNavHistoryResultNode::OnItemKeywordChanged(
    int64_t aItemId, const nsACString& aKeyword) {
  if (aItemId != mItemId) {
    return NS_OK;
  }

  bool shouldNotify = !mParent || mParent->AreChildrenVisible();
  if (shouldNotify) {
    nsNavHistoryResult* result = GetResult();
    NS_ENSURE_STATE(result);
    NOTIFY_RESULT_OBSERVERS(result, NodeKeywordChanged(this, aKeyword));
  }

  return NS_OK;
}

nsresult nsNavHistoryResultNode::OnItemTagsChanged(int64_t aItemId,
                                                   const nsAString& aURL,
                                                   const nsAString& aTags) {
  if (aItemId != mItemId) {
    return NS_OK;
  }

  SetTags(aTags);

  bool shouldNotify = !mParent || mParent->AreChildrenVisible();
  if (shouldNotify) {
    nsNavHistoryResult* result = GetResult();
    NS_ENSURE_STATE(result);
    NOTIFY_RESULT_OBSERVERS(result, NodeTagsChanged(this));
  }

  return NS_OK;
}

nsresult nsNavHistoryResultNode::OnItemTimeChanged(int64_t aItemId,
                                                   const nsACString& aGUID,
                                                   PRTime aDateAdded,
                                                   PRTime aLastModified) {
  if (aItemId != mItemId) {
    return NS_OK;
  }

  bool isDateAddedChanged = mDateAdded != aDateAdded;
  bool isLastModifiedChanged = mLastModified != aLastModified;

  if (!isDateAddedChanged && !isLastModifiedChanged) {
    return NS_OK;
  }

  mDateAdded = aDateAdded;
  mLastModified = aLastModified;

  bool shouldNotify = !mParent || mParent->AreChildrenVisible();
  if (shouldNotify) {
    nsNavHistoryResult* result = GetResult();
    NS_ENSURE_STATE(result);

    if (isDateAddedChanged) {
      NOTIFY_RESULT_OBSERVERS(result, NodeDateAddedChanged(this, aDateAdded));
    }
    if (isLastModifiedChanged) {
      NOTIFY_RESULT_OBSERVERS(result,
                              NodeLastModifiedChanged(this, aLastModified));
    }
  }

  return NS_OK;
}

nsresult nsNavHistoryResultNode::OnItemTitleChanged(int64_t aItemId,
                                                    const nsACString& aGUID,
                                                    const nsACString& aTitle,
                                                    PRTime aLastModified) {
  if (aItemId != mItemId) {
    return NS_OK;
  }

  mTitle = aTitle;
  mLastModified = aLastModified;

  bool shouldNotify = !mParent || mParent->AreChildrenVisible();
  if (shouldNotify) {
    nsNavHistoryResult* result = GetResult();
    NS_ENSURE_STATE(result);
    NOTIFY_RESULT_OBSERVERS(result, NodeTitleChanged(this, mTitle));
  }

  return NS_OK;
}

nsresult nsNavHistoryResultNode::OnItemUrlChanged(int64_t aItemId,
                                                  const nsACString& aGUID,
                                                  const nsACString& aURL,
                                                  PRTime aLastModified) {
  if (aItemId != mItemId) {
    return NS_OK;
  }

  // clear the tags string as well
  mTags.SetIsVoid(true);
  nsCString oldURI(mURI);
  mURI = aURL;
  mLastModified = aLastModified;

  bool shouldNotify = !mParent || mParent->AreChildrenVisible();
  if (shouldNotify) {
    nsNavHistoryResult* result = GetResult();
    NS_ENSURE_STATE(result);
    NOTIFY_RESULT_OBSERVERS(result, NodeURIChanged(this, oldURI));
  }

  if (!mParent) return NS_OK;

  // The sorting methods fall back to each other so we need to re-sort the
  // result even if it's not set to sort by the given property.
  int32_t ourIndex = mParent->FindChild(this);
  NS_ASSERTION(ourIndex >= 0, "Could not find self in parent");
  if (ourIndex >= 0) {
    mParent->EnsureItemPosition(ourIndex);
  }

  return NS_OK;
}

nsresult nsNavHistoryResultNode::OnVisitsRemoved() {
  PRTime oldTime = mTime;
  mTime = 0;

  nsNavHistoryResult* result = GetResult();
  NS_ENSURE_STATE(result);
  NOTIFY_RESULT_OBSERVERS(
      result, NodeHistoryDetailsChanged(this, oldTime, mAccessCount));

  return NS_OK;
}

/**
 * Updates visit count and last visit time and refreshes.
 */
nsresult nsNavHistoryFolderResultNode::OnItemVisited(nsIURI* aURI,
                                                     int64_t aVisitId,
                                                     PRTime aTime,
                                                     int64_t aFrecency) {
  if (mOptions->ExcludeItems()) {
    return NS_OK;  // don't update items when we aren't displaying them
  }

  RESTART_AND_RETURN_IF_ASYNC_PENDING();

  if (!StartIncrementalUpdate()) return NS_OK;

  nsAutoCString spec;
  nsresult rv = aURI->GetSpec(spec);
  NS_ENSURE_SUCCESS(rv, rv);
  nsCOMArray<nsNavHistoryResultNode> nodes;
  FindChildrenByURI(spec, &nodes);
  if (!nodes.Count()) {
    return NS_OK;
  }

  // Update us.
  ++mAccessCount;
  if (aTime > mTime) {
    mTime = aTime;
  }

  // Update nodes.
  for (int32_t i = 0; i < nodes.Count(); ++i) {
    nsNavHistoryResultNode* node = nodes[i];
    uint32_t nodeOldAccessCount = node->mAccessCount;
    PRTime nodeOldTime = node->mTime;
    if (node->mTime < aTime) {
      node->mTime = aTime;
    }
    ++node->mAccessCount;
    node->mFrecency = aFrecency;

    nsNavHistoryResult* result = GetResult();
    if (AreChildrenVisible() && !result->CanSkipHistoryDetailsNotifications()) {
      // Sorting has not changed, just redraw the row if it's visible.
      NOTIFY_RESULT_OBSERVERS(
          result,
          NodeHistoryDetailsChanged(node, nodeOldTime, nodeOldAccessCount));
    }

    // Update sorting if necessary.
    uint32_t sortType = GetSortType();
    if (sortType == nsINavHistoryQueryOptions::SORT_BY_VISITCOUNT_ASCENDING ||
        sortType == nsINavHistoryQueryOptions::SORT_BY_VISITCOUNT_DESCENDING ||
        sortType == nsINavHistoryQueryOptions::SORT_BY_DATE_ASCENDING ||
        sortType == nsINavHistoryQueryOptions::SORT_BY_DATE_DESCENDING ||
        sortType == nsINavHistoryQueryOptions::SORT_BY_FRECENCY_ASCENDING ||
        sortType == nsINavHistoryQueryOptions::SORT_BY_FRECENCY_DESCENDING) {
      int32_t childIndex = FindChild(node);
      NS_ASSERTION(childIndex >= 0,
                   "Could not find child we just got a reference to");
      if (childIndex >= 0) {
        EnsureItemPosition(childIndex);
      }
    }
  }

  return NS_OK;
}

nsresult nsNavHistoryFolderResultNode::OnItemMoved(
    int64_t aItemId, int32_t aOldIndex, int32_t aNewIndex, uint16_t aItemType,
    const nsACString& aGUID, const nsACString& aOldParentGUID,
    const nsACString& aNewParentGUID, uint16_t aSource, const nsACString& aURI,
    const nsACString& aTitle, const nsAString& aTags, int64_t aFrecency,
    bool aHidden, uint32_t aVisitCount, PRTime aLastVisitDate,
    PRTime aDateAdded) {
  MOZ_ASSERT(aOldParentGUID.Equals(mTargetFolderGuid) ||
                 aNewParentGUID.Equals(mTargetFolderGuid),
             "Got a bookmark message that doesn't belong to us");

  RESTART_AND_RETURN_IF_ASYNC_PENDING();

  bool excludeItems = mOptions->ExcludeItems();
  if (excludeItems && (aItemType == nsINavBookmarksService::TYPE_SEPARATOR ||
                       (aItemType == nsINavBookmarksService::TYPE_BOOKMARK &&
                        !StringBeginsWith(aURI, "place:"_ns)))) {
    // This is a bookmark or a separator, so we don't need to handle this if
    // we're excluding items.
    return NS_OK;
  }

  int32_t index;
  nsNavHistoryResultNode* node = FindChildById(aItemId, &index);
  // Bug 1097528.
  // It's possible our result registered due to a previous notification, for
  // example the Library left pane could have refreshed and replaced the
  // right pane as a consequence. In such a case our contents are already
  // up-to-date.  That's OK.
  if (node && aNewParentGUID.Equals(mTargetFolderGuid) && index == aNewIndex) {
    return NS_OK;
  }
  if (!node && aOldParentGUID.Equals(mTargetFolderGuid)) return NS_OK;

  if (!StartIncrementalUpdate()) {
    return NS_OK;  // entire container was refreshed for us
  }

  if (aNewParentGUID.Equals(aOldParentGUID)) {
    // getting moved within the same folder, we don't want to do a remove and
    // an add because that will lose your tree state.

    MOZ_ASSERT(node, "Can't find folder that is moving!");
    if (!node) {
      return NS_ERROR_FAILURE;
    }

    // adjust bookmark indices
    int32_t maxIndex = std::max(node->mBookmarkIndex, aNewIndex);
    // When moving multiple bookmarks, we are notified one bookmark at a time.
    // Since previously moved bookmarks may have shifted this bookmark's index,
    // we can't trust the passed-in oldIndex.
    ReindexRange(node->mBookmarkIndex + 1, maxIndex, -1);
    ReindexRange(aNewIndex, maxIndex, 1);

    MOZ_ASSERT(index < mChildren.Count(), "Invalid index!");
    node->mBookmarkIndex = aNewIndex;

    // adjust position
    EnsureItemPosition(index);
    return NS_OK;
  }

  // moving between two different folders, just do a remove and an add
  nsCOMPtr<nsIURI> itemURI;
  if (aItemType == nsINavBookmarksService::TYPE_BOOKMARK) {
    nsresult rv = NS_NewURI(getter_AddRefs(itemURI), aURI);
    NS_ENSURE_SUCCESS(rv, rv);
  }
  if (aOldParentGUID.Equals(mTargetFolderGuid)) {
    OnItemRemoved(aItemId, mTargetFolderItemId, aOldIndex, aItemType, itemURI,
                  aGUID, aOldParentGUID, aSource);
  }
  if (aNewParentGUID.Equals(mTargetFolderGuid)) {
    OnItemAdded(aItemId, mTargetFolderItemId, aNewIndex, aItemType, itemURI,
                aDateAdded, aGUID, aNewParentGUID, aSource, aTitle, aTags,
                aFrecency, aHidden, aVisitCount, aLastVisitDate,
                mTargetFolderItemId, mTargetFolderGuid, aTitle);
  }

  return NS_OK;
}

/**
 * Separator nodes do not hold any data.
 */
nsNavHistorySeparatorResultNode::nsNavHistorySeparatorResultNode()
    : nsNavHistoryResultNode(""_ns, ""_ns, 0, 0) {}

NS_IMPL_CYCLE_COLLECTION_CLASS(nsNavHistoryResult)

NS_IMPL_CYCLE_COLLECTION_UNLINK_BEGIN(nsNavHistoryResult)
  tmp->StopObservingOnUnlink();

  NS_IMPL_CYCLE_COLLECTION_UNLINK(mRootNode)
  NS_IMPL_CYCLE_COLLECTION_UNLINK(mObservers)
  NS_IMPL_CYCLE_COLLECTION_UNLINK(mMobilePrefObservers)
  NS_IMPL_CYCLE_COLLECTION_UNLINK(mAllBookmarksObservers)
  NS_IMPL_CYCLE_COLLECTION_UNLINK(mHistoryObservers)
  NS_IMPL_CYCLE_COLLECTION_UNLINK(mRefreshParticipants)
  NS_IMPL_CYCLE_COLLECTION_UNLINK_WEAK_REFERENCE
NS_IMPL_CYCLE_COLLECTION_UNLINK_END

NS_IMPL_CYCLE_COLLECTION_TRAVERSE_BEGIN(nsNavHistoryResult)
  NS_IMPL_CYCLE_COLLECTION_TRAVERSE(mRootNode)
  NS_IMPL_CYCLE_COLLECTION_TRAVERSE(mObservers)
  for (nsNavHistoryResult::FolderObserverList* list :
       tmp->mBookmarkFolderObservers.Values()) {
    for (uint32_t i = 0; i < list->Length(); ++i) {
      NS_CYCLE_COLLECTION_NOTE_EDGE_NAME(cb,
                                         "mBookmarkFolderObservers value[i]");
      nsNavHistoryResultNode* node = list->ElementAt(i);
      cb.NoteXPCOMChild(node);
    }
  }
  NS_IMPL_CYCLE_COLLECTION_TRAVERSE(mMobilePrefObservers)
  NS_IMPL_CYCLE_COLLECTION_TRAVERSE(mAllBookmarksObservers)
  NS_IMPL_CYCLE_COLLECTION_TRAVERSE(mHistoryObservers)
  NS_IMPL_CYCLE_COLLECTION_TRAVERSE(mRefreshParticipants)
NS_IMPL_CYCLE_COLLECTION_TRAVERSE_END

NS_IMPL_CYCLE_COLLECTING_ADDREF(nsNavHistoryResult)
NS_IMPL_CYCLE_COLLECTING_RELEASE(nsNavHistoryResult)

NS_INTERFACE_MAP_BEGIN_CYCLE_COLLECTION(nsNavHistoryResult)
  NS_INTERFACE_MAP_ENTRY_AMBIGUOUS(nsISupports, nsINavHistoryResult)
  NS_INTERFACE_MAP_STATIC_AMBIGUOUS(nsNavHistoryResult)
  NS_INTERFACE_MAP_ENTRY(nsINavHistoryResult)
  NS_INTERFACE_MAP_ENTRY(nsISupportsWeakReference)
NS_INTERFACE_MAP_END

nsNavHistoryResult::nsNavHistoryResult(
    nsNavHistoryContainerResultNode* aRoot,
    const RefPtr<nsNavHistoryQuery>& aQuery,
    const RefPtr<nsNavHistoryQueryOptions>& aOptions)
    : mRootNode(aRoot),
      mQuery(aQuery),
      mOptions(aOptions),
      mNeedsToApplySortingMode(false),
      mIsHistoryObserver(false),
      mIsBookmarksObserver(false),
      mIsMobilePrefObserver(false),
      mBookmarkFolderObservers(64),
      mSuppressNotifications(false),
      mIsHistoryDetailsObserver(false),
      mObserversWantHistoryDetails(true),
      mBatchInProgress(0) {
  mSortingMode = aOptions->SortingMode();

  mRootNode->mResult = this;
  MOZ_ASSERT(mRootNode->mIndentLevel == -1,
             "Root node's indent level initialized wrong");
  mRootNode->FillStats();

  AutoTArray<PlacesEventType, 1> events;
  events.AppendElement(PlacesEventType::Purge_caches);
  PlacesObservers::AddListener(events, this);
}

nsNavHistoryResult::~nsNavHistoryResult() {
  // Delete all heap-allocated bookmark folder observer arrays.
  for (auto it = mBookmarkFolderObservers.Iter(); !it.Done(); it.Next()) {
    delete it.Data();
    it.Remove();
  }
}

void nsNavHistoryResult::StopObserving() {
  AutoTArray<PlacesEventType, 12> events;
  events.AppendElement(PlacesEventType::Favicon_changed);
  if (mIsBookmarksObserver) {
    events.AppendElement(PlacesEventType::Bookmark_added);
    events.AppendElement(PlacesEventType::Bookmark_removed);
    events.AppendElement(PlacesEventType::Bookmark_moved);
    events.AppendElement(PlacesEventType::Bookmark_keyword_changed);
    events.AppendElement(PlacesEventType::Bookmark_tags_changed);
    events.AppendElement(PlacesEventType::Bookmark_time_changed);
    events.AppendElement(PlacesEventType::Bookmark_title_changed);
    events.AppendElement(PlacesEventType::Bookmark_url_changed);
    mIsBookmarksObserver = false;
  }
  if (mIsMobilePrefObserver) {
    Preferences::UnregisterCallback(OnMobilePrefChangedCallback,
                                    MOBILE_BOOKMARKS_PREF, this);
    mIsMobilePrefObserver = false;
  }
  if (mIsHistoryObserver) {
    mIsHistoryObserver = false;
    events.AppendElement(PlacesEventType::History_cleared);
    events.AppendElement(PlacesEventType::Page_removed);
  }
  if (mIsHistoryDetailsObserver) {
    events.AppendElement(PlacesEventType::Page_visited);
    events.AppendElement(PlacesEventType::Page_title_changed);
    mIsHistoryDetailsObserver = false;
  }

  PlacesObservers::RemoveListener(events, this);
}

void nsNavHistoryResult::StopObservingOnUnlink() {
  StopObserving();

  AutoTArray<PlacesEventType, 1> events;
  events.AppendElement(PlacesEventType::Purge_caches);
  PlacesObservers::RemoveListener(events, this);

  for (auto it = mBookmarkFolderObservers.Iter(); !it.Done(); it.Next()) {
    delete it.Data();
    it.Remove();
  }
}

bool nsNavHistoryResult::CanSkipHistoryDetailsNotifications() const {
  return !mObserversWantHistoryDetails &&
         mOptions->QueryType() ==
             nsINavHistoryQueryOptions::QUERY_TYPE_BOOKMARKS &&
         mSortingMode != nsINavHistoryQueryOptions::SORT_BY_DATE_ASCENDING &&
         mSortingMode != nsINavHistoryQueryOptions::SORT_BY_DATE_DESCENDING &&
         mSortingMode !=
             nsINavHistoryQueryOptions::SORT_BY_VISITCOUNT_ASCENDING &&
         mSortingMode !=
             nsINavHistoryQueryOptions::SORT_BY_VISITCOUNT_DESCENDING &&
         mSortingMode !=
             nsINavHistoryQueryOptions::SORT_BY_FRECENCY_ASCENDING &&
         mSortingMode != nsINavHistoryQueryOptions::SORT_BY_FRECENCY_DESCENDING;
}

void nsNavHistoryResult::AddHistoryObserver(
    nsNavHistoryQueryResultNode* aNode) {
  if (!mIsHistoryObserver) {
    mIsHistoryObserver = true;

    AutoTArray<PlacesEventType, 3> events;
    events.AppendElement(PlacesEventType::History_cleared);
    events.AppendElement(PlacesEventType::Page_removed);
    if (!mIsHistoryDetailsObserver) {
      events.AppendElement(PlacesEventType::Page_visited);
      events.AppendElement(PlacesEventType::Page_title_changed);
      mIsHistoryDetailsObserver = true;
    }
    PlacesObservers::AddListener(events, this);
  }
  // Don't add duplicate observers.  In some case we don't unregister when
  // children are cleared (see ClearChildren) and the next FillChildren call
  // will try to add the observer again.
  if (mHistoryObservers.IndexOf(aNode) == QueryObserverList::NoIndex) {
    mHistoryObservers.AppendElement(aNode);
  }
}

void nsNavHistoryResult::AddAllBookmarksObserver(
    nsNavHistoryQueryResultNode* aNode) {
  EnsureIsObservingBookmarks();
  // Don't add duplicate observers.  In some case we don't unregister when
  // children are cleared (see ClearChildren) and the next FillChildren call
  // will try to add the observer again.
  if (mAllBookmarksObservers.IndexOf(aNode) == QueryObserverList::NoIndex) {
    mAllBookmarksObservers.AppendElement(aNode);
  }
}

void nsNavHistoryResult::AddMobilePrefsObserver(
    nsNavHistoryQueryResultNode* aNode) {
  if (!mIsMobilePrefObserver) {
    Preferences::RegisterCallback(OnMobilePrefChangedCallback,
                                  MOBILE_BOOKMARKS_PREF, this);
    mIsMobilePrefObserver = true;
  }
  // Don't add duplicate observers.  In some case we don't unregister when
  // children are cleared (see ClearChildren) and the next FillChildren call
  // will try to add the observer again.
  if (mMobilePrefObservers.IndexOf(aNode) == QueryObserverList::NoIndex) {
    mMobilePrefObservers.AppendElement(aNode);
  }
}

void nsNavHistoryResult::AddBookmarkFolderObserver(
    nsNavHistoryFolderResultNode* aNode, const nsACString& aFolderGUID) {
  MOZ_ASSERT(!aFolderGUID.IsEmpty(), "aFolderGUID should not be empty");
  EnsureIsObservingBookmarks();
  // Don't add duplicate observers.  In some case we don't unregister when
  // children are cleared (see ClearChildren) and the next FillChildren call
  // will try to add the observer again.
  FolderObserverList* list = BookmarkFolderObserversForGUID(aFolderGUID, true);
  if (list->IndexOf(aNode) == FolderObserverList::NoIndex) {
    list->AppendElement(aNode);
  }
}

void nsNavHistoryResult::EnsureIsObservingBookmarks() {
  if (mIsBookmarksObserver) {
    return;
  }
  AutoTArray<PlacesEventType, 9> events;
  events.AppendElement(PlacesEventType::Bookmark_added);
  events.AppendElement(PlacesEventType::Bookmark_removed);
  events.AppendElement(PlacesEventType::Bookmark_moved);
  events.AppendElement(PlacesEventType::Bookmark_keyword_changed);
  events.AppendElement(PlacesEventType::Bookmark_tags_changed);
  events.AppendElement(PlacesEventType::Bookmark_time_changed);
  events.AppendElement(PlacesEventType::Bookmark_title_changed);
  events.AppendElement(PlacesEventType::Bookmark_url_changed);
  // If we're not observing visits yet, also add a page-visited observer to
  // serve onItemVisited.
  if (!mIsHistoryObserver && !mIsHistoryDetailsObserver) {
    events.AppendElement(PlacesEventType::Page_visited);
    mIsHistoryDetailsObserver = true;
  }
  PlacesObservers::AddListener(events, this);
  mIsBookmarksObserver = true;
}

void nsNavHistoryResult::RemoveHistoryObserver(
    nsNavHistoryQueryResultNode* aNode) {
  mHistoryObservers.RemoveElement(aNode);
}

void nsNavHistoryResult::RemoveAllBookmarksObserver(
    nsNavHistoryQueryResultNode* aNode) {
  mAllBookmarksObservers.RemoveElement(aNode);
}

void nsNavHistoryResult::RemoveMobilePrefsObserver(
    nsNavHistoryQueryResultNode* aNode) {
  mMobilePrefObservers.RemoveElement(aNode);
}

void nsNavHistoryResult::RemoveBookmarkFolderObserver(
    nsNavHistoryFolderResultNode* aNode, const nsACString& aFolderGUID) {
  MOZ_ASSERT(!aFolderGUID.IsEmpty(), "aFolderGUID should not be empty");
  FolderObserverList* list = BookmarkFolderObserversForGUID(aFolderGUID, false);
  if (!list) return;  // we don't even have an entry for that folder
  list->RemoveElement(aNode);
}

nsNavHistoryResult::FolderObserverList*
nsNavHistoryResult::BookmarkFolderObserversForGUID(
    const nsACString& aFolderGUID, bool aCreate) {
  FolderObserverList* list;
  if (mBookmarkFolderObservers.Get(aFolderGUID, &list)) return list;
  if (!aCreate) return nullptr;

  // need to create a new list
  list = new FolderObserverList;
  mBookmarkFolderObservers.InsertOrUpdate(aFolderGUID, list);
  return list;
}

NS_IMETHODIMP
nsNavHistoryResult::GetSortingMode(uint16_t* aSortingMode) {
  *aSortingMode = mSortingMode;
  return NS_OK;
}

NS_IMETHODIMP
nsNavHistoryResult::SetSortingMode(uint16_t aSortingMode) {
  NS_ENSURE_STATE(mRootNode);

  if (aSortingMode > nsINavHistoryQueryOptions::SORT_BY_FRECENCY_DESCENDING) {
    return NS_ERROR_INVALID_ARG;
  }

  // Keep everything in sync.
  NS_ASSERTION(mOptions, "Options should always be present for a root query");

  mSortingMode = aSortingMode;

  // If the sorting mode changed to one requiring history details, we must
  // ensure to start observing.
  bool addedListener = UpdateHistoryDetailsObservers();

  if (!mRootNode->mExpanded) {
    // Need to do this later when node will be expanded.
    mNeedsToApplySortingMode = true;
    return NS_OK;
  }

  if (addedListener) {
    // We must do a full refresh because the history details may be stale.
    if (mRootNode->IsQuery()) {
      return mRootNode->GetAsQuery()->Refresh();
    }
    if (mRootNode->IsFolderOrShortcut()) {
      return mRootNode->GetAsFolder()->Refresh();
    }
  }

  // Actually do sorting.
  nsNavHistoryContainerResultNode::SortComparator comparator =
      nsNavHistoryContainerResultNode::GetSortingComparator(aSortingMode);
  if (comparator) {
    nsNavHistory* history = nsNavHistory::GetHistoryService();
    NS_ENSURE_TRUE(history, NS_ERROR_OUT_OF_MEMORY);
    mRootNode->RecursiveSort(comparator);
  }

  NOTIFY_RESULT_OBSERVERS(this, SortingChanged(aSortingMode));
  NOTIFY_RESULT_OBSERVERS(this, InvalidateContainer(mRootNode));
  return NS_OK;
}

NS_IMETHODIMP
nsNavHistoryResult::AddObserver(nsINavHistoryResultObserver* aObserver,
                                bool aOwnsWeak) {
  NS_ENSURE_ARG(aObserver);
  nsresult rv = mObservers.AppendWeakElementUnlessExists(aObserver, aOwnsWeak);
  NS_ENSURE_SUCCESS(rv, rv);

  UpdateHistoryDetailsObservers();

  rv = aObserver->SetResult(this);
  NS_ENSURE_SUCCESS(rv, rv);

  // If we are batching, notify a fake batch start to the observers.
  // Not doing so would then notify a not coupled batch end.
  if (IsBatching()) {
    NOTIFY_RESULT_OBSERVERS(this, Batching(true));
  }

  if (!mRootNode->IsQuery() ||
      mRootNode->GetAsQuery()->mLiveUpdate != QUERYUPDATE_NONE) {
    // Pretty much all the views show favicons, thus observe changes to them.
    AutoTArray<PlacesEventType, 1> events;
    events.AppendElement(PlacesEventType::Favicon_changed);
    PlacesObservers::AddListener(events, this);
  }

  return NS_OK;
}

NS_IMETHODIMP
nsNavHistoryResult::RemoveObserver(nsINavHistoryResultObserver* aObserver) {
  NS_ENSURE_ARG(aObserver);
  nsresult rv = mObservers.RemoveWeakElement(aObserver);
  NS_ENSURE_SUCCESS(rv, rv);
  UpdateHistoryDetailsObservers();
  return NS_OK;
}

bool nsNavHistoryResult::UpdateHistoryDetailsObservers() {
  bool skipHistoryDetailsNotifications = false;
  // One observer set to true is enough to set mObserversWantHistoryDetails.
  for (uint32_t i = 0;
       i < mObservers.Length() && !skipHistoryDetailsNotifications; ++i) {
    const nsCOMPtr<nsINavHistoryResultObserver>& entry =
        mObservers.ElementAt(i).GetValue();
    if (entry) {
      entry->GetSkipHistoryDetailsNotifications(
          &skipHistoryDetailsNotifications);
    }
  }

  mObserversWantHistoryDetails = !skipHistoryDetailsNotifications;
  // If one observer wants history details we may have to add the listener.
  if (!CanSkipHistoryDetailsNotifications()) {
    if (!mIsHistoryDetailsObserver) {
      AutoTArray<PlacesEventType, 3> events;
      events.AppendElement(PlacesEventType::Page_visited);
      events.AppendElement(PlacesEventType::Page_title_changed);
      events.AppendElement(PlacesEventType::Page_removed);
      PlacesObservers::AddListener(events, this);
      mIsHistoryDetailsObserver = true;
      return true;
    }
  } else {
    AutoTArray<PlacesEventType, 3> events;
    events.AppendElement(PlacesEventType::Page_visited);
    events.AppendElement(PlacesEventType::Page_title_changed);
    events.AppendElement(PlacesEventType::Page_removed);
    PlacesObservers::RemoveListener(events, this);
    mIsHistoryDetailsObserver = false;
  }
  return false;
}

NS_IMETHODIMP
nsNavHistoryResult::GetSuppressNotifications(bool* _retval) {
  *_retval = mSuppressNotifications;
  return NS_OK;
}

NS_IMETHODIMP
nsNavHistoryResult::SetSuppressNotifications(bool aSuppressNotifications) {
  mSuppressNotifications = aSuppressNotifications;
  return NS_OK;
}

NS_IMETHODIMP
nsNavHistoryResult::GetRoot(nsINavHistoryContainerResultNode** aRoot) {
  if (!mRootNode) {
    MOZ_ASSERT_UNREACHABLE("Root is null");
    *aRoot = nullptr;
    return NS_ERROR_FAILURE;
  }
  RefPtr<nsNavHistoryContainerResultNode> node(mRootNode);
  node.forget(aRoot);
  return NS_OK;
}

void nsNavHistoryResult::requestRefresh(
    nsNavHistoryContainerResultNode* aContainer) {
  // Don't add twice the same container.
  if (mRefreshParticipants.IndexOf(aContainer) ==
      ContainerObserverList::NoIndex) {
    mRefreshParticipants.AppendElement(aContainer);
  }
}

// Here, it is important that we create a COPY of the observer array. Some
// observers will requery themselves, which may cause the observer array to
// be modified or added to.
#define ENUMERATE_BOOKMARK_FOLDER_OBSERVERS(_folderGUID, _functionCall) \
  PR_BEGIN_MACRO                                                        \
  FolderObserverList* _fol =                                            \
      BookmarkFolderObserversForGUID(_folderGUID, false);               \
  if (_fol) {                                                           \
    FolderObserverList _listCopy(_fol->Clone());                        \
    for (uint32_t _fol_i = 0; _fol_i < _listCopy.Length(); ++_fol_i) {  \
      if (_listCopy[_fol_i]) _listCopy[_fol_i]->_functionCall;          \
    }                                                                   \
  }                                                                     \
  PR_END_MACRO
#define ENUMERATE_LIST_OBSERVERS(_listType, _functionCall, _observersList, \
                                 _conditionCall)                           \
  PR_BEGIN_MACRO                                                           \
  _listType _listCopy((_observersList).Clone());                           \
  for (uint32_t _obs_i = 0; _obs_i < _listCopy.Length(); ++_obs_i) {       \
    if (_listCopy[_obs_i] && _listCopy[_obs_i]->_conditionCall)            \
      _listCopy[_obs_i]->_functionCall;                                    \
  }                                                                        \
  PR_END_MACRO
#define ENUMERATE_QUERY_OBSERVERS(_functionCall, _observersList,             \
                                  _conditionCall)                            \
  ENUMERATE_LIST_OBSERVERS(QueryObserverList, _functionCall, _observersList, \
                           _conditionCall)
#define ENUMERATE_ALL_BOOKMARKS_OBSERVERS(_functionCall) \
  ENUMERATE_QUERY_OBSERVERS(_functionCall, mAllBookmarksObservers, IsQuery())
#define ENUMERATE_HISTORY_OBSERVERS(_functionCall) \
  ENUMERATE_QUERY_OBSERVERS(_functionCall, mHistoryObservers, IsQuery())
#define ENUMERATE_MOBILE_PREF_OBSERVERS(_functionCall) \
  ENUMERATE_QUERY_OBSERVERS(_functionCall, mMobilePrefObservers, IsQuery())
#define ENUMERATE_BOOKMARK_CHANGED_OBSERVERS(_folderGUID, _targetId,        \
                                             _functionCall)                 \
  PR_BEGIN_MACRO                                                            \
  ENUMERATE_ALL_BOOKMARKS_OBSERVERS(_functionCall);                         \
  FolderObserverList* _fol =                                                \
      BookmarkFolderObserversForGUID(_folderGUID, false);                   \
                                                                            \
  /*                                                                        \
   * Note: folder-nodes set their own bookmark observer only once they're   \
   * opened, meaning we cannot optimize this code path for changes done to  \
   * folder-nodes.                                                          \
   */                                                                       \
                                                                            \
  for (uint32_t _fol_i = 0; _fol && _fol_i < _fol->Length(); ++_fol_i) {    \
    RefPtr<nsNavHistoryFolderResultNode> _folder = _fol->ElementAt(_fol_i); \
    if (_folder) {                                                          \
      int32_t _nodeIndex;                                                   \
      RefPtr<nsNavHistoryResultNode> _node =                                \
          _folder->FindChildById(_targetId, &_nodeIndex);                   \
      bool _excludeItems = _folder->mOptions->ExcludeItems();               \
      if (_node &&                                                          \
          (!_excludeItems || !(_node->IsURI() || _node->IsSeparator())) &&  \
          _folder->StartIncrementalUpdate()) {                              \
        _node->_functionCall;                                               \
      }                                                                     \
    }                                                                       \
  }                                                                         \
                                                                            \
  PR_END_MACRO

#define NOTIFY_REFRESH_PARTICIPANTS()                            \
  PR_BEGIN_MACRO                                                 \
  ENUMERATE_LIST_OBSERVERS(ContainerObserverList, Refresh(),     \
                           mRefreshParticipants, IsContainer()); \
  mRefreshParticipants.Clear();                                  \
  PR_END_MACRO

NS_IMETHODIMP
nsNavHistoryResult::OnBeginUpdateBatch() {
  // Since we could be observing both history and bookmarks, it's possible both
  // notify the batch.  We can safely ignore nested calls.
  if (++mBatchInProgress == 1) {
    ENUMERATE_HISTORY_OBSERVERS(OnBeginUpdateBatch());
    ENUMERATE_ALL_BOOKMARKS_OBSERVERS(OnBeginUpdateBatch());

    NOTIFY_RESULT_OBSERVERS(this, Batching(true));
  }

  return NS_OK;
}

NS_IMETHODIMP
nsNavHistoryResult::OnEndUpdateBatch() {
  // Since we could be observing both history and bookmarks, it's possible both
  // notify the batch.  We can safely ignore nested calls.
  // Notice it's possible we are notified OnEndUpdateBatch more times than
  // onBeginUpdateBatch, since the result could be created in the middle of
  // nested batches.
  if (--mBatchInProgress == 0) {
    ENUMERATE_HISTORY_OBSERVERS(OnEndUpdateBatch());
    ENUMERATE_ALL_BOOKMARKS_OBSERVERS(OnEndUpdateBatch());

    NOTIFY_REFRESH_PARTICIPANTS();
    NOTIFY_RESULT_OBSERVERS(this, Batching(false));
  }

  return NS_OK;
}

nsresult nsNavHistoryResult::OnVisit(nsIURI* aURI, int64_t aVisitId,
                                     PRTime aTime, uint32_t aTransitionType,
                                     const nsACString& aGUID, bool aHidden,
                                     uint32_t aVisitCount,
                                     const nsAString& aLastKnownTitle,
                                     int64_t aFrecency) {
  NS_ENSURE_ARG(aURI);

  // Embed visits are never shown in our views.
  if (aTransitionType == nsINavHistoryService::TRANSITION_EMBED) {
    return NS_OK;
  }

  uint32_t added = 0;

  ENUMERATE_HISTORY_OBSERVERS(OnVisit(aURI, aVisitId, aTime, aTransitionType,
                                      aGUID, aHidden, aVisitCount,
                                      aLastKnownTitle, aFrecency, &added));

  // When we add visits, we don't bother telling the world that the title
  // 'changed' from nothing to the first title we ever see for a history entry.
  // Our consumers here might still care, though, so we have to tell them, but
  // only for the first visit we add. Subsequent changes will get an usual
  // ontitlechanged notification.
  if (!aLastKnownTitle.IsVoid() && aVisitCount == 1) {
    ENUMERATE_HISTORY_OBSERVERS(OnTitleChanged(aURI, aLastKnownTitle, aGUID));
  }

  if (!mRootNode->mExpanded) return NS_OK;

  // If this visit is accepted by an overlapped container, and not all
  // overlapped containers are visible, we should still call Refresh if the
  // visit falls into any of them.
  bool todayIsMissing = false;
  uint32_t resultType = mRootNode->mOptions->ResultType();
  if (resultType == nsINavHistoryQueryOptions::RESULTS_AS_DATE_QUERY ||
      resultType == nsINavHistoryQueryOptions::RESULTS_AS_DATE_SITE_QUERY) {
    uint32_t childCount;
    nsresult rv = mRootNode->GetChildCount(&childCount);
    NS_ENSURE_SUCCESS(rv, rv);
    if (childCount) {
      nsCOMPtr<nsINavHistoryResultNode> firstChild;
      rv = mRootNode->GetChild(0, getter_AddRefs(firstChild));
      NS_ENSURE_SUCCESS(rv, rv);
      nsAutoCString title;
      rv = firstChild->GetTitle(title);
      NS_ENSURE_SUCCESS(rv, rv);
      nsNavHistory* history = nsNavHistory::GetHistoryService();
      NS_ENSURE_TRUE(history, NS_OK);
      nsAutoCString todayLabel;
      history->GetStringFromName("finduri-AgeInDays-is-0", todayLabel);
      todayIsMissing = !todayLabel.Equals(title);
    }
  }

  if (!added || todayIsMissing) {
    // None of registered query observers has accepted our URI.  This means,
    // that a matching query either was not expanded or it does not exist.
    if (resultType == nsINavHistoryQueryOptions::RESULTS_AS_DATE_QUERY ||
        resultType == nsINavHistoryQueryOptions::RESULTS_AS_DATE_SITE_QUERY) {
      // If the visit falls into the Today bucket and the bucket exists, it was
      // just not expanded, thus there's no reason to update.
      int64_t beginOfToday = nsNavHistory::NormalizeTime(
          nsINavHistoryQuery::TIME_RELATIVE_TODAY, 0);
      if (todayIsMissing || aTime < beginOfToday) {
        (void)mRootNode->GetAsQuery()->Refresh();
      }
      return NS_OK;
    }

    if (resultType == nsINavHistoryQueryOptions::RESULTS_AS_SITE_QUERY) {
      (void)mRootNode->GetAsQuery()->Refresh();
      return NS_OK;
    }

    // We are result of a folder node, then we should run through history
    // observers that are containers queries and refresh them.
    // We use a copy of the observers array since requerying could potentially
    // cause changes to the array.
    ENUMERATE_QUERY_OBSERVERS(Refresh(), mHistoryObservers,
                              IsContainersQuery());

    // Also notify onItemVisited to bookmark folder observers, that are not
    // observing history.
    if (!mIsHistoryObserver && mRootNode->IsFolderOrShortcut()) {
      nsAutoCString spec;
      nsresult rv = aURI->GetSpec(spec);
      NS_ENSURE_SUCCESS(rv, rv);
      // Find all the folders containing the visited URI, and notify them.
      nsCOMArray<nsNavHistoryResultNode> nodes;
      mRootNode->RecursiveFindURIs(true, mRootNode, spec, &nodes);
      for (int32_t i = 0; i < nodes.Count(); ++i) {
        nsNavHistoryResultNode* node = nodes[i];
        ENUMERATE_BOOKMARK_FOLDER_OBSERVERS(
            node->mParent->mBookmarkGuid,
            OnItemVisited(aURI, aVisitId, aTime, aFrecency));
      }
    }
  }

  return NS_OK;
}

void nsNavHistoryResult::OnIconChanged(nsIURI* aURI, nsIURI* aFaviconURI,
                                       const nsACString& aGUID) {
  if (!mRootNode->mExpanded) {
    return;
  }
  // Find all nodes for the given URI and update them.
  nsAutoCString spec;
  if (NS_SUCCEEDED(aURI->GetSpec(spec))) {
    bool onlyOneEntry =
        mOptions->QueryType() ==
            nsINavHistoryQueryOptions::QUERY_TYPE_HISTORY &&
        mOptions->ResultType() == nsINavHistoryQueryOptions::RESULTS_AS_URI;
    mRootNode->UpdateURIs(true, onlyOneEntry, false, spec, setFaviconCallback,
                          nullptr);
  }
}

bool nsNavHistoryResult::IsBulkPageRemovedEvent(
    const PlacesEventSequence& aEvents) {
  if (IsBatching() || aEvents.Length() <= MAX_PAGE_REMOVES_BEFORE_REFRESH) {
    return false;
  }
  for (const auto& event : aEvents) {
    if (event->Type() != PlacesEventType::Page_removed) return false;
  }
  return true;
}

void nsNavHistoryResult::HandlePlacesEvent(const PlacesEventSequence& aEvents) {
  if (IsBulkPageRemovedEvent(aEvents)) {
    ENUMERATE_HISTORY_OBSERVERS(Refresh());
    return;
  }

  for (const auto& event : aEvents) {
    switch (event->Type()) {
      case PlacesEventType::Favicon_changed: {
        const dom::PlacesFavicon* faviconEvent = event->AsPlacesFavicon();
        if (NS_WARN_IF(!faviconEvent)) {
          continue;
        }
        nsCOMPtr<nsIURI> uri, faviconUri;
        if (NS_WARN_IF(NS_FAILED(
                NS_NewURI(getter_AddRefs(uri), faviconEvent->mUrl)))) {
          continue;
        }
        if (NS_WARN_IF(NS_FAILED(NS_NewURI(getter_AddRefs(faviconUri),
                                           faviconEvent->mFaviconUrl)))) {
          continue;
        }
        OnIconChanged(uri, faviconUri, faviconEvent->mPageGuid);
        break;
      }
      case PlacesEventType::Page_visited: {
        const dom::PlacesVisit* visit = event->AsPlacesVisit();
        if (NS_WARN_IF(!visit)) {
          continue;
        }

        nsCOMPtr<nsIURI> uri;
        if (NS_WARN_IF(
                NS_FAILED(NS_NewURI(getter_AddRefs(uri), visit->mUrl)))) {
          continue;
        }
        OnVisit(uri, static_cast<int64_t>(visit->mVisitId),
                static_cast<PRTime>(visit->mVisitTime * 1000),
                visit->mTransitionType, visit->mPageGuid, visit->mHidden,
                visit->mVisitCount, visit->mLastKnownTitle, visit->mFrecency);
        break;
      }
      case PlacesEventType::Bookmark_added: {
        const dom::PlacesBookmarkAddition* item =
            event->AsPlacesBookmarkAddition();
        if (NS_WARN_IF(!item)) {
          continue;
        }

        nsCOMPtr<nsIURI> uri;
        if (item->mItemType == nsINavBookmarksService::TYPE_BOOKMARK) {
          if (NS_WARN_IF(
                  NS_FAILED(NS_NewURI(getter_AddRefs(uri), item->mUrl)))) {
            continue;
          }
        }

        ENUMERATE_BOOKMARK_FOLDER_OBSERVERS(
            item->mParentGuid,
            OnItemAdded(item->mId, item->mParentId, item->mIndex,
                        item->mItemType, uri, item->mDateAdded * 1000,
                        item->mGuid, item->mParentGuid, item->mSource,
                        NS_ConvertUTF16toUTF8(item->mTitle), item->mTags,
                        item->mFrecency, item->mHidden, item->mVisitCount,
                        item->mLastVisitDate.IsNull()
                            ? 0
                            : item->mLastVisitDate.Value() * 1000,
                        item->mTargetFolderItemId, item->mTargetFolderGuid,
                        NS_ConvertUTF16toUTF8(item->mTargetFolderTitle)));
        ENUMERATE_HISTORY_OBSERVERS(
            OnItemAdded(item->mId, item->mParentId, item->mIndex,
                        item->mItemType, uri, item->mDateAdded * 1000,
                        item->mGuid, item->mParentGuid, item->mSource));
        ENUMERATE_ALL_BOOKMARKS_OBSERVERS(
            OnItemAdded(item->mId, item->mParentId, item->mIndex,
                        item->mItemType, uri, item->mDateAdded * 1000,
                        item->mGuid, item->mParentGuid, item->mSource));
        break;
      }
      case PlacesEventType::Bookmark_removed: {
        const dom::PlacesBookmarkRemoved* item =
            event->AsPlacesBookmarkRemoved();
        if (NS_WARN_IF(!item)) {
          continue;
        }

        nsCOMPtr<nsIURI> uri;

        if (item->mIsDescendantRemoval) {
          continue;
        }
        ENUMERATE_BOOKMARK_FOLDER_OBSERVERS(
            item->mParentGuid,
            OnItemRemoved(item->mId, item->mParentId, item->mIndex,
                          item->mItemType, uri, item->mGuid, item->mParentGuid,
                          item->mSource));
        ENUMERATE_ALL_BOOKMARKS_OBSERVERS(OnItemRemoved(
            item->mId, item->mParentId, item->mIndex, item->mItemType, uri,
            item->mGuid, item->mParentGuid, item->mSource));
        ENUMERATE_HISTORY_OBSERVERS(OnItemRemoved(
            item->mId, item->mParentId, item->mIndex, item->mItemType, uri,
            item->mGuid, item->mParentGuid, item->mSource));
        break;
      }
      case PlacesEventType::Bookmark_moved: {
        const dom::PlacesBookmarkMoved* item = event->AsPlacesBookmarkMoved();
        if (NS_WARN_IF(!item)) {
          continue;
        }

        NS_ConvertUTF16toUTF8 url(item->mUrl);

        ENUMERATE_BOOKMARK_FOLDER_OBSERVERS(
            item->mOldParentGuid,
            OnItemMoved(item->mId, item->mOldIndex, item->mIndex,
                        item->mItemType, item->mGuid, item->mOldParentGuid,
                        item->mParentGuid, item->mSource, url,
                        NS_ConvertUTF16toUTF8(item->mTitle), item->mTags,
                        item->mFrecency, item->mHidden, item->mVisitCount,
                        item->mLastVisitDate.IsNull()
                            ? 0
                            : item->mLastVisitDate.Value() * 1000,
                        item->mDateAdded * 1000));
        if (!item->mParentGuid.Equals(item->mOldParentGuid)) {
          ENUMERATE_BOOKMARK_FOLDER_OBSERVERS(
              item->mParentGuid,
              OnItemMoved(item->mId, item->mOldIndex, item->mIndex,
                          item->mItemType, item->mGuid, item->mOldParentGuid,
                          item->mParentGuid, item->mSource, url,
                          NS_ConvertUTF16toUTF8(item->mTitle), item->mTags,
                          item->mFrecency, item->mHidden, item->mVisitCount,
                          item->mLastVisitDate.IsNull()
                              ? 0
                              : item->mLastVisitDate.Value() * 1000,
                          item->mDateAdded * 1000));
        }
        ENUMERATE_ALL_BOOKMARKS_OBSERVERS(
            OnItemMoved(item->mId, item->mOldIndex, item->mIndex,
                        item->mItemType, item->mGuid, item->mOldParentGuid,
                        item->mParentGuid, item->mSource, url));
        ENUMERATE_HISTORY_OBSERVERS(
            OnItemMoved(item->mId, item->mOldIndex, item->mIndex,
                        item->mItemType, item->mGuid, item->mOldParentGuid,
                        item->mParentGuid, item->mSource, url));
        break;
      }
      case PlacesEventType::Bookmark_keyword_changed: {
        const dom::PlacesBookmarkKeyword* keywordEvent =
            event->AsPlacesBookmarkKeyword();
        if (NS_WARN_IF(!keywordEvent)) {
          continue;
        }
        ENUMERATE_BOOKMARK_CHANGED_OBSERVERS(
            keywordEvent->mParentGuid, keywordEvent->mId,
            OnItemKeywordChanged(keywordEvent->mId, keywordEvent->mKeyword));
        break;
      }
      case PlacesEventType::Bookmark_tags_changed: {
        const dom::PlacesBookmarkTags* tagsEvent =
            event->AsPlacesBookmarkTags();
        if (NS_WARN_IF(!tagsEvent)) {
          continue;
        }

        nsString tags;
        tagsEvent->mTags.Length()
            ? tags.Assign(StringJoin(u","_ns, tagsEvent->mTags))
            : tags.SetIsVoid(true);

        ENUMERATE_BOOKMARK_CHANGED_OBSERVERS(
            tagsEvent->mParentGuid, tagsEvent->mId,
            OnItemTagsChanged(tagsEvent->mId, tagsEvent->mUrl, tags));
        break;
      }
      case PlacesEventType::Bookmark_time_changed: {
        const dom::PlacesBookmarkTime* timeEvent =
            event->AsPlacesBookmarkTime();
        if (NS_WARN_IF(!timeEvent)) {
          continue;
        }
        ENUMERATE_BOOKMARK_CHANGED_OBSERVERS(
            timeEvent->mParentGuid, timeEvent->mId,
            OnItemTimeChanged(timeEvent->mId, timeEvent->mGuid,
                              timeEvent->mDateAdded * 1000,
                              timeEvent->mLastModified * 1000));
        break;
      }
      case PlacesEventType::Bookmark_title_changed: {
        const dom::PlacesBookmarkTitle* titleEvent =
            event->AsPlacesBookmarkTitle();
        if (NS_WARN_IF(!titleEvent)) {
          continue;
        }

        NS_ConvertUTF16toUTF8 title(titleEvent->mTitle);
        ENUMERATE_BOOKMARK_CHANGED_OBSERVERS(
            titleEvent->mParentGuid, titleEvent->mId,
            OnItemTitleChanged(titleEvent->mId, titleEvent->mGuid, title,
                               titleEvent->mLastModified * 1000));
        break;
      }
      case PlacesEventType::Bookmark_url_changed: {
        const dom::PlacesBookmarkUrl* urlEvent = event->AsPlacesBookmarkUrl();
        if (NS_WARN_IF(!urlEvent)) {
          continue;
        }

        NS_ConvertUTF16toUTF8 url(urlEvent->mUrl);
        ENUMERATE_BOOKMARK_CHANGED_OBSERVERS(
            urlEvent->mParentGuid, urlEvent->mId,
            OnItemUrlChanged(urlEvent->mId, urlEvent->mGuid, url,
                             urlEvent->mLastModified * 1000));
        break;
      }
      case PlacesEventType::Page_title_changed: {
        const PlacesVisitTitle* titleEvent = event->AsPlacesVisitTitle();
        if (NS_WARN_IF(!titleEvent)) {
          continue;
        }

        nsCOMPtr<nsIURI> uri;
        if (NS_WARN_IF(
                NS_FAILED(NS_NewURI(getter_AddRefs(uri), titleEvent->mUrl)))) {
          continue;
        }

        ENUMERATE_HISTORY_OBSERVERS(
            OnTitleChanged(uri, titleEvent->mTitle, titleEvent->mPageGuid));
        break;
      }
      case PlacesEventType::History_cleared: {
        ENUMERATE_HISTORY_OBSERVERS(OnClearHistory());
        break;
      }
      case PlacesEventType::Page_removed: {
        const PlacesVisitRemoved* removeEvent = event->AsPlacesVisitRemoved();
        if (NS_WARN_IF(!removeEvent)) {
          continue;
        }

        nsCOMPtr<nsIURI> uri;
        if (NS_WARN_IF(
                NS_FAILED(NS_NewURI(getter_AddRefs(uri), removeEvent->mUrl)))) {
          continue;
        }

        if (removeEvent->mIsRemovedFromStore) {
          ENUMERATE_HISTORY_OBSERVERS(OnPageRemovedFromStore(
              uri, removeEvent->mPageGuid, removeEvent->mReason));
        } else {
          ENUMERATE_HISTORY_OBSERVERS(
              OnPageRemovedVisits(uri, removeEvent->mIsPartialVisistsRemoval,
                                  removeEvent->mPageGuid, removeEvent->mReason,
                                  removeEvent->mTransitionType));

          if (!removeEvent->mIsPartialVisistsRemoval && mRootNode) {
            mRootNode->OnVisitsRemoved(uri);
          }
        }

        break;
      }
      case PlacesEventType::Purge_caches: {
        mRootNode->Refresh();
        break;
      }
      default: {
        MOZ_ASSERT_UNREACHABLE(
            "Receive notification of a type not subscribed to.");
      }
    }
  }
}

void nsNavHistoryResult::OnMobilePrefChanged() {
  ENUMERATE_MOBILE_PREF_OBSERVERS(
      OnMobilePrefChanged(Preferences::GetBool(MOBILE_BOOKMARKS_PREF, false)));
}

void nsNavHistoryResult::OnMobilePrefChangedCallback(const char* prefName,
                                                     void* self) {
  MOZ_ASSERT(!strcmp(prefName, MOBILE_BOOKMARKS_PREF),
             "We only expected Mobile Bookmarks pref change.");

  static_cast<nsNavHistoryResult*>(self)->OnMobilePrefChanged();
}
