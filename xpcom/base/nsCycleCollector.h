/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef nsCycleCollector_h_
#define nsCycleCollector_h_

class nsICycleCollectorListener;
class nsICycleCollectorLogSink;
class nsISupports;
template <class T>
struct already_AddRefed;

#include <cstdint>

#include "mozilla/Attributes.h"
#include "mozilla/TimeStamp.h"
#include "nsCycleCollectionParticipant.h"

namespace JS {
class SliceBudget;
}

namespace mozilla {
class CycleCollectedJSContext;
}  // namespace mozilla

bool nsCycleCollector_init();

void nsCycleCollector_startup();

typedef void (*CC_BeforeUnlinkCallback)(void);
void nsCycleCollector_setBeforeUnlinkCallback(CC_BeforeUnlinkCallback aCB);

typedef void (*CC_ForgetSkippableCallback)(void);
void nsCycleCollector_setForgetSkippableCallback(
    CC_ForgetSkippableCallback aCB);

void nsCycleCollector_forgetSkippable(mozilla::TimeStamp aStartTime,
                                      JS::SliceBudget& aBudget, bool aInIdle,
                                      bool aRemoveChildlessNodes = false,
                                      bool aAsyncSnowWhiteFreeing = false);

void nsCycleCollector_prepareForGarbageCollection();

// If an incremental cycle collection is in progress, finish it.
void nsCycleCollector_finishAnyCurrentCollection();

void nsCycleCollector_dispatchDeferredDeletion(bool aContinuation = false,
                                               bool aPurge = false);
bool nsCycleCollector_doDeferredDeletion();
bool nsCycleCollector_doDeferredDeletionWithBudget(JS::SliceBudget& aBudget);
bool nsCycleCollector_maybeDoDeferredDeletion();

already_AddRefed<nsICycleCollectorLogSink> nsCycleCollector_createLogSink(
    bool aLogGC);
already_AddRefed<nsICycleCollectorListener> nsCycleCollector_createLogger();

// Run a cycle collection and return whether anything was collected.
bool nsCycleCollector_collect(mozilla::CCReason aReason,
                              nsICycleCollectorListener* aManualListener);

void nsCycleCollector_collectSlice(JS::SliceBudget& budget,
                                   mozilla::CCReason aReason,
                                   bool aPreferShorterSlices = false);

uint32_t nsCycleCollector_suspectedCount();

// If aDoCollect is true, then run the GC and CC a few times before
// shutting down the CC completely.
MOZ_CAN_RUN_SCRIPT
void nsCycleCollector_shutdown(bool aDoCollect = true);

// Helpers for interacting with JS
void nsCycleCollector_registerJSContext(mozilla::CycleCollectedJSContext* aCx);
void nsCycleCollector_forgetJSContext();

#endif  // nsCycleCollector_h_
