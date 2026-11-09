/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */
#ifndef mozilla_ClearDataCallback_h_
#define mozilla_ClearDataCallback_h_

#include "BounceTrackingMapEntry.h"
#include "BounceTrackingRecord.h"
#include "mozilla/MozPromise.h"
#include "mozilla/glean/bindings/GleanMetric.h"
#include "nsIClearDataService.h"
#include "nsIURIClassifier.h"

namespace mozilla {

// Pending clear operations are stored as ClearDataMozPromise, one per host.
using ClearDataMozPromise =
    MozPromise<RefPtr<BounceTrackingPurgeEntry>, uint32_t, true>;

extern LazyLogModule gBounceTrackingProtectionLog;

// Helper for clear data callbacks used for purge bounce trackers.
// Wraps nsIClearDataCallback in MozPromise.
class ClearDataCallback final : public nsIClearDataCallback,
                                public nsIUrlClassifierFeatureCallback {
 public:
  NS_DECL_ISUPPORTS
  NS_DECL_NSICLEARDATACALLBACK
  NS_DECL_NSIURLCLASSIFIERFEATURECALLBACK

  explicit ClearDataCallback(ClearDataMozPromise::Private* aPromise,
                             const BounceTrackingSite& aSite,
                             PRTime aBounceTime,
                             BounceTrackingRecord* aChainRecord = nullptr);

 private:
  virtual ~ClearDataCallback();

  // Entry containing the site host which was cleared and the timestamp of when
  // the bounce occurred that led to the tracker being purged.
  RefPtr<BounceTrackingPurgeEntry> mEntry;

  // Promise which is resolved or rejected when the clear operation completes.
  RefPtr<ClearDataMozPromise::Private> mPromise;

  // Clear duration telemetry
  void RecordClearDurationTelemetry();
  glean::TimerId mClearDurationTimer;

  // Purge count telemetry
  void RecordPurgeCountTelemetry(bool aFailed);

  // URL Classifier telemetry
  void RecordURLClassifierTelemetry();

  // Event telemetry for purges.
  void RecordPurgeEventTelemetry(bool aSuccess);
};

}  // namespace mozilla

#endif
