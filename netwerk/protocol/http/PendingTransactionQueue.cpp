/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

// HttpLog.h should generally be included first
#include "HttpLog.h"

// Log on level :5, instead of default :4.
#undef LOG
#define LOG(args) LOG5(args)
#undef LOG_ENABLED
#define LOG_ENABLED() LOG5_ENABLED()

#include "PendingTransactionQueue.h"
#include "mozilla/ChaosMode.h"
#include "mozilla/StaticPrefs_network.h"
#include "nsHttpHandler.h"

namespace mozilla {
namespace net {

static uint64_t TabIdForQueuing(nsAHttpTransaction* transaction) {
  return StaticPrefs::network_http_active_tab_priority()
             ? transaction->BrowserId()
             : 0;
}

// This function decides the transaction's order in the pending queue.
// Given two transactions t1 and t2, returning true means that t2 is
// more important than t1 and thus should be dispatched first.
static bool TransactionComparator(nsHttpTransaction* t1,
                                  nsHttpTransaction* t2) {
  bool t1Blocking =
      t1->Caps() & (NS_HTTP_LOAD_AS_BLOCKING | NS_HTTP_LOAD_UNBLOCKED);
  bool t2Blocking =
      t2->Caps() & (NS_HTTP_LOAD_AS_BLOCKING | NS_HTTP_LOAD_UNBLOCKED);

  if (t1Blocking > t2Blocking) {
    return false;
  }

  if (t2Blocking > t1Blocking) {
    return true;
  }

  return t1->Priority() >= t2->Priority();
}

void PendingTransactionQueue::InsertTransactionNormal(
    PendingTransactionInfo* info,
    bool aInsertAsFirstForTheSamePriority /*= false*/) {
  LOG(
      ("PendingTransactionQueue::InsertTransactionNormal"
       " trans=%p, bid=%" PRIu64 "\n",
       info->Transaction(), info->Transaction()->BrowserId()));

  uint64_t windowId = TabIdForQueuing(info->Transaction());
  nsTArray<RefPtr<PendingTransactionInfo>>* const infoArray =
      mPendingTransactionTable.GetOrInsertNew(windowId);

  // XXX At least if a new array was empty before, this isn't efficient, as it
  // does an insert-sort. It would be better to just append all elements and
  // then sort.
  InsertTransactionSorted(*infoArray, info, aInsertAsFirstForTheSamePriority);
  ++mPendingQueueLength;
}

void PendingTransactionQueue::InsertTransactionSorted(
    nsTArray<RefPtr<PendingTransactionInfo>>& pendingQ,
    PendingTransactionInfo* pendingTransInfo,
    bool aInsertAsFirstForTheSamePriority /*= false*/) {
  // insert the transaction into the front of the queue based on following
  // rules:
  // 1. The transaction has NS_HTTP_LOAD_AS_BLOCKING or NS_HTTP_LOAD_UNBLOCKED.
  // 2. The transaction's priority is higher.
  //
  // search in reverse order under the assumption that many of the
  // existing transactions will have the same priority (usually 0).

  nsHttpTransaction* trans = pendingTransInfo->Transaction();

  for (int32_t i = pendingQ.Length() - 1; i >= 0; --i) {
    nsHttpTransaction* t = pendingQ[i]->Transaction();
    if (TransactionComparator(trans, t)) {
      if (ChaosMode::isActive(ChaosFeature::NetworkScheduling) ||
          aInsertAsFirstForTheSamePriority) {
        int32_t samePriorityCount;
        for (samePriorityCount = 0; i - samePriorityCount >= 0;
             ++samePriorityCount) {
          if (pendingQ[i - samePriorityCount]->Transaction()->Priority() !=
              trans->Priority()) {
            break;
          }
        }
        if (aInsertAsFirstForTheSamePriority) {
          i -= samePriorityCount;
        } else {
          // skip over 0...all of the elements with the same priority.
          i -= ChaosMode::randomUint32LessThan(samePriorityCount + 1);
        }
      }
      pendingQ.InsertElementAt(i + 1, pendingTransInfo);
      return;
    }
  }
  pendingQ.InsertElementAt(0, pendingTransInfo);
}

void PendingTransactionQueue::InsertTransaction(
    PendingTransactionInfo* pendingTransInfo,
    bool aInsertAsFirstForTheSamePriority /* = false */) {
  if (pendingTransInfo->Transaction()->Caps() & NS_HTTP_URGENT_START) {
    LOG(
        ("  adding transaction to pending queue "
         "[trans=%p urgent-start-count=%zu]\n",
         pendingTransInfo->Transaction(), mUrgentStartQ.Length() + 1));
    // put this transaction on the urgent-start queue...
    InsertTransactionSorted(mUrgentStartQ, pendingTransInfo);
  } else {
    LOG(
        ("  adding transaction to pending queue "
         "[trans=%p pending-count=%zu]\n",
         pendingTransInfo->Transaction(), PendingQueueLength() + 1));
    // put this transaction on the pending queue...
    InsertTransactionNormal(pendingTransInfo);
  }
}

nsTArray<RefPtr<PendingTransactionInfo>>*
PendingTransactionQueue::GetTransactionPendingQHelper(
    nsAHttpTransaction* trans) {
  nsTArray<RefPtr<PendingTransactionInfo>>* pendingQ = nullptr;
  int32_t caps = trans->Caps();
  if (caps & NS_HTTP_URGENT_START) {
    pendingQ = &(mUrgentStartQ);
  } else {
    pendingQ = mPendingTransactionTable.Get(TabIdForQueuing(trans));
  }
  return pendingQ;
}

void PendingTransactionQueue::AppendPendingUrgentStartQ(
    nsTArray<RefPtr<PendingTransactionInfo>>& result) {
  result.InsertElementsAt(0, mUrgentStartQ.Elements(), mUrgentStartQ.Length());
  mUrgentStartQ.Clear();
}

void PendingTransactionQueue::AppendPendingQForFocusedWindow(
    uint64_t windowId, nsTArray<RefPtr<PendingTransactionInfo>>& result,
    uint32_t maxCount) {
  nsTArray<RefPtr<PendingTransactionInfo>>* infoArray = nullptr;
  if (!mPendingTransactionTable.Get(windowId, &infoArray)) {
    result.Clear();
    return;
  }

  uint32_t countToAppend = maxCount;
  countToAppend = countToAppend > infoArray->Length() || countToAppend == 0
                      ? infoArray->Length()
                      : countToAppend;

  result.InsertElementsAt(result.Length(), infoArray->Elements(),
                          countToAppend);
  infoArray->RemoveElementsAt(0, countToAppend);
  MOZ_ASSERT(mPendingQueueLength >= countToAppend);
  mPendingQueueLength -= countToAppend;

  LOG(
      ("PendingTransactionQueue::AppendPendingQForFocusedWindow, "
       "pendingQ count=%zu window.count=%zu for focused window (id=%" PRIu64
       ")\n",
       result.Length(), infoArray->Length(), windowId));
}

void PendingTransactionQueue::AppendPendingQForNonFocusedWindows(
    uint64_t windowId, nsTArray<RefPtr<PendingTransactionInfo>>& result,
    uint32_t maxCount) {
  // XXX Adjust the order of transactions in a smarter manner.
  uint32_t totalCount = 0;
  for (const auto& entry : mPendingTransactionTable) {
    if (windowId && entry.GetKey() == windowId) {
      continue;
    }

    uint32_t count = 0;
    for (; count < entry.GetRawPtr()->Length(); ++count) {
      if (maxCount && totalCount == maxCount) {
        break;
      }

      // Because elements in |result| could come from multiple penndingQ,
      // call |InsertTransactionSorted| to make sure the order is correct.
      InsertTransactionSorted(result, entry.GetRawPtr()->ElementAt(count));
      ++totalCount;
    }
    entry.GetRawPtr()->RemoveElementsAt(0, count);
    MOZ_ASSERT(mPendingQueueLength >= count);
    mPendingQueueLength -= count;

    if (maxCount && totalCount == maxCount) {
      if (entry.GetRawPtr()->Length()) {
        // There are still some pending transactions for background
        // tabs but we limit their dispatch.  This is considered as
        // an active tab optimization.
        nsHttp::NotifyActiveTabLoadOptimization();
      }
      break;
    }
  }
}

void PendingTransactionQueue::ReschedTransaction(nsHttpTransaction* aTrans) {
  nsTArray<RefPtr<PendingTransactionInfo>>* pendingQ =
      GetTransactionPendingQHelper(aTrans);

  int32_t index =
      pendingQ ? pendingQ->IndexOf(aTrans, 0, PendingComparator()) : -1;
  if (index >= 0) {
    RefPtr<PendingTransactionInfo> pendingTransInfo = (*pendingQ)[index];
    pendingQ->RemoveElementAt(index);
    InsertTransactionSorted(*pendingQ, pendingTransInfo);
  }
}

void PendingTransactionQueue::RemoveEmptyPendingQ() {
  for (auto it = mPendingTransactionTable.Iter(); !it.Done(); it.Next()) {
    if (it.UserData()->IsEmpty()) {
      it.Remove();
    }
  }
}

#ifdef DEBUG
size_t PendingTransactionQueue::ComputePendingQueueLength() const {
  size_t length = 0;
  for (const auto& data : mPendingTransactionTable.Values()) {
    length += data->Length();
  }
  return length;
}
#endif

void PendingTransactionQueue::OnPendingTransactionRemovedFromTable() {
  MOZ_ASSERT(mPendingQueueLength > 0);
  --mPendingQueueLength;
}

size_t PendingTransactionQueue::PendingQueueLengthForWindow(
    uint64_t windowId) const {
  auto* pendingQ = mPendingTransactionTable.Get(windowId);
  return (pendingQ) ? pendingQ->Length() : 0;
}

void PendingTransactionQueue::PrintPendingQ() {
  LOG(("urgent queue ["));
  for (const auto& info : mUrgentStartQ) {
    LOG(("  %p", info->Transaction()));
  }
  for (const auto& entry : mPendingTransactionTable) {
    LOG(("] window id = %" PRIx64 " queue [", entry.GetKey()));
    for (const auto& info : *entry.GetRawPtr()) {
      LOG(("  %p", info->Transaction()));
    }
  }
  LOG(("]"));
}

void PendingTransactionQueue::Compact() {
  mUrgentStartQ.Compact();
  for (const auto& data : mPendingTransactionTable.Values()) {
    data->Compact();
  }
}

void PendingTransactionQueue::CancelAllTransactions(nsresult reason) {
  AutoTArray<RefPtr<nsHttpTransaction>, 64> toClose;
  for (const auto& info : mUrgentStartQ) {
    toClose.AppendElement(info->Transaction());
  }
  mUrgentStartQ.Clear();

  // Drain all table entries into toClose, then clear them.
  for (const auto& data : mPendingTransactionTable.Values()) {
    for (const auto& info : *data) {
      toClose.AppendElement(info->Transaction());
    }
    data->Clear();
  }
  mPendingTransactionTable.Clear();
  mPendingQueueLength = 0;

  for (const auto& trans : toClose) {
    LOG(("PendingTransactionQueue::CancelAllTransactions %p\n", trans.get()));
    trans->Close(reason);
  }
}

}  // namespace net
}  // namespace mozilla
