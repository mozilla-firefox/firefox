/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef mozilla_BaseHistory_h
#define mozilla_BaseHistory_h

#include "IHistory.h"
#include "mozilla/dom/ContentParent.h"
#include "nsTHashSet.h"

/* A base class for history implementations that implement link coloring. */

namespace mozilla {

class BaseHistory : public IHistory {
 public:
  void RegisterVisitedCallback(nsIURI*, dom::Link*) final;
  void ScheduleVisitedQuery(nsIURI*, dom::ContentParent*) final;
  void UnregisterVisitedCallback(nsIURI*, dom::Link*) final;
  void NotifyVisited(nsIURI*, VisitedStatus,
                     const ContentParentSet* = nullptr) final;
  void NotifyHistoryCleared() final;

  // Some URIs like data-uris are never going to be stored in history, so we can
  // avoid doing IPC roundtrips for them or what not.
  static bool CanStore(nsIURI*);

 protected:
  void NotifyVisitedInThisProcess(nsIURI*, VisitedStatus);
  void NotifyVisitedFromParent(nsIURI*, VisitedStatus, const ContentParentSet*);
  static constexpr const size_t kTrackedUrisInitialSize = 64;

  BaseHistory() = default;
  ~BaseHistory() = default;

  using ObserverArray = nsTObserverArray<dom::Link*>;
  struct ObservingLinks {
    ObserverArray mLinks;
    VisitedStatus mStatus = VisitedStatus::Unknown;

    size_t SizeOfExcludingThis(MallocSizeOf aMallocSizeOf) const {
      return mLinks.ShallowSizeOfExcludingThis(aMallocSizeOf);
    }
  };

  using PendingVisitedQueries = nsTHashMap<nsURIHashKey, ContentParentSet>;
  struct PendingVisitedResult {
    dom::VisitedQueryResult mResult;
    ContentParentSet mProcessesToNotify;
  };
  using PendingVisitedResults = nsTArray<PendingVisitedResult>;

  // Starts all the queries in the pending queries list, potentially at the same
  // time.
  virtual void StartPendingVisitedQueries(PendingVisitedQueries&&) = 0;

 private:
  // Cancels a visited query, if it is at all possible, because we know we won't
  // use the results anymore.
  void CancelVisitedQueryIfPossible(nsIURI*);

  void SendPendingVisitedResultsToChildProcesses();

 protected:
  // A map from URI to links that depend on that URI, and whether that URI is
  // known-to-be-visited-or-unvisited already.
  nsTHashMap<nsURIHashKey, ObservingLinks> mTrackedURIs{
      kTrackedUrisInitialSize};

 private:
  // The set of pending URIs that we haven't queried yet but need to.
  PendingVisitedQueries mPendingQueries;
  // The set of pending query results that we still haven't dispatched to child
  // processes.
  PendingVisitedResults mPendingResults;
  // Whether we've successfully scheduled a runnable to call
  // StartPendingVisitedQueries already.
  bool mStartPendingVisitedQueriesScheduled = false;
  // Whether we've successfully scheduled a runnable to call
  // SendPendingVisitedResultsToChildProcesses already.
  bool mStartPendingResultsScheduled = false;
};

}  // namespace mozilla

#endif
