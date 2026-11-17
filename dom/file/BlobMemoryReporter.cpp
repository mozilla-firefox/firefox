/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "BlobMemoryReporter.h"

#include "BlobImpl.h"
#include "mozilla/StaticMutex.h"
#include "mozilla/StaticPtr.h"
#include "mozilla/ThreadSafeWeakPtr.h"
#include "nsTHashSet.h"

namespace mozilla::dom {

static StaticMutex sMutex;
static StaticAutoPtr<nsTHashSet<ThreadSafeWeakPtr<BlobImpl>>> sEntries
    MOZ_GUARDED_BY(sMutex);

NS_IMPL_ISUPPORTS(BlobMemoryReporter, nsIMemoryReporter)

BlobMemoryReporter::~BlobMemoryReporter() {
  StaticMutexAutoLock lock(sMutex);
  sEntries = nullptr;
}

/* static */
void BlobMemoryReporter::Register(BlobImpl* aImpl) {
  bool needsRegistration = false;

  {
    StaticMutexAutoLock lock(sMutex);

    if (!sEntries) {
      sEntries = new nsTHashSet<ThreadSafeWeakPtr<BlobImpl>>();
      needsRegistration = true;
    }

    sEntries->Insert(aImpl);
  }

  // Outside the lock: dropping the reporter calls ~BlobMemoryReporter which
  // acquires sMutex.
  if (needsRegistration) {
    RegisterStrongMemoryReporter(MakeAndAddRef<BlobMemoryReporter>());
  }
}

/* static */
void BlobMemoryReporter::Unregister(BlobImpl* aImpl) {
  StaticMutexAutoLock lock(sMutex);
  if (sEntries) {
    sEntries->Remove(aImpl);
  }
}

NS_IMETHODIMP
BlobMemoryReporter::CollectReports(nsIHandleReportCallback* aHandleReport,
                                   nsISupports* aData, bool aAnonymize) {
  // Don't release the strong reference under the lock.
  nsTArray<RefPtr<BlobImpl>> alive;

  {
    StaticMutexAutoLock lock(sMutex);
    if (!sEntries) {
      return NS_OK;
    }

    // Memory reporting is best-effort: silently skip under OOM rather than
    // crashing in an infallible reallocation.
    if (!alive.SetCapacity(sEntries->Count(), fallible)) {
      return NS_OK;
    }

    for (const auto& key : *sEntries) {
      if (RefPtr<BlobImpl> ref(key); ref) {
        alive.AppendElement(std::move(ref));
      }
    }
  }

  for (const auto& impl : alive) {
    nsresult rv = impl->CollectReports(aHandleReport, aData, aAnonymize);
    NS_ENSURE_SUCCESS(rv, rv);
  }

  return NS_OK;
}

}  // namespace mozilla::dom
