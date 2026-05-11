/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef FOGIPC_h_
#define FOGIPC_h_

#include <functional>

#include "mozilla/MozPromise.h"
#include "nsTArrayForwardDeclare.h"

namespace mozilla {
namespace ipc {
class ByteBuf;
}  // namespace ipc
}  // namespace mozilla

// This module provides the interface for FOG to communicate between processes.

namespace mozilla {
namespace glean {

/**
 * The parent process is asking you to flush your data ASAP.
 *
 * @param aResolver - The function you need to call with the bincoded,
 *                    serialized payload that the Rust impl hands you.
 */
void FlushFOGData(std::function<void(mozilla::ipc::ByteBuf&&)>&& aResolver);

/**
 * Called by FOG on the parent process when it wants to flush all its
 * children's data.
 * @param aResolver - The function that'll be called with the results.
 */
void FlushAllChildData(
    std::function<void(nsTArray<mozilla::ipc::ByteBuf>&&)>&& aResolver);

/**
 * A child process has sent you this buf as a treat.
 * @param buf - a bincoded serialized payload that the Rust impl understands.
 */
void FOGData(mozilla::ipc::ByteBuf&& buf);

/**
 * Called by FOG on a child process when it wants to send a buf to the parent.
 * @param buf - a bincoded serialized payload that the Rust impl understands.
 */
void SendFOGData(mozilla::ipc::ByteBuf&& buf);

/**
 * Called on the parent process to ask all child processes for data,
 * sending it all down into Rust to be used.
 *
 * @returns a Promise that resolves when the data has made it to the parent.
 */
RefPtr<GenericPromise> FlushAndUseFOGData();

/**
 * ** Test-only Method **
 *
 * Trigger GMP, GPU, RDD or Socket process test instrumentation.
 *
 * @param processType - one of the PROCESS_TYPE_* constants from nsIXULRuntime.
 * @param promise - a promise that will be resolved when the data has made it to
 *                  the target process.
 */
void TestTriggerMetrics(uint32_t processType,
                        const RefPtr<mozilla::dom::Promise>& promise);

#ifdef NIGHTLY_BUILD
/**
 * This function records the CPU activity (CPU time used and wakeup count)
 * of a specific thread. It is called only by profiler code, either multiple
 * times in a row when RecordPowerMetrics asks the profiler to record
 * the wakeup counts of all threads, or once when a thread is unregistered.
 *
 * @param aThreadName The name of the thread for which the CPU data is being
 *                    recorded.
 *                    The name will be converted to lower case, and characters
 *                    that are not valid for glean labels will be replaced with
 *                    '_'. The resulting name should be part of the
 *                    per_thread_labels static list of labels defined in
 *                    toolkit/components/processtools/metrics.yaml.
 * @param aCpuTimeMs CPU time in miliseconds since the last time CPU use data
 *                   was recorded for this thread.
 * @param aWakeCount How many times the thread woke up since the previous time
 *                   CPU use data was recorded for this thread.
 */
void RecordThreadCpuUse(const nsACString& aThreadName, uint64_t aCpuTimeMs,
                        uint64_t aWakeCount);
#endif

void RecordPowerMetrics();

}  // namespace glean
}  // namespace mozilla

#endif  // FOGIPC_h_
