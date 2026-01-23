/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.concept.storage

import androidx.work.Constraints
import androidx.work.PeriodicWorkRequest
import androidx.work.PeriodicWorkRequestBuilder
import java.util.concurrent.TimeUnit

/**
 * Builds and returns a [PeriodicWorkRequest] based on [PeriodicWorkRequestBuilder]
 * with a given [StorageMaintenanceWorker].
 *
 * @param repeatInterval Repeat interval of the periodic work request.
 * @param repeatIntervalTimeUnit Time unit for the repeat interval of the work request.
 * @param tag Work request's tag.
 * @param constraints A block that returns the [Constraints] for the work.
 * @return [PeriodicWorkRequest].
 * */
inline fun <reified T : StorageMaintenanceWorker> Storage.periodicStorageWorkRequest(
    repeatInterval: Long = StorageMaintenanceWorker.WORKER_PERIOD_IN_HOURS,
    repeatIntervalTimeUnit: TimeUnit = TimeUnit.HOURS,
    tag: String?,
    constraints: Storage.() -> Constraints,
): PeriodicWorkRequest {
    return PeriodicWorkRequestBuilder<T>(
        repeatInterval,
        repeatIntervalTimeUnit,
    ).apply {
        setConstraints(constraints())
        tag?.let { addTag(tag) }
    }.build()
}

/**
 * Builds and returns a [Constraints] based on [Constraints.Builder] provided by [block].
 * @return [Constraints].
 * */
fun constraints(block: Constraints.Builder.() -> Unit): Constraints {
    return Constraints.Builder().apply { block() }.build()
}
