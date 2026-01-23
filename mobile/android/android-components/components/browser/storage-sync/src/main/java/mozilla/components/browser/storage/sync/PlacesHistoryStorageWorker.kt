/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.browser.storage.sync

import android.content.Context
import androidx.work.WorkerParameters
import mozilla.components.concept.storage.StorageMaintenanceWorker
import mozilla.components.support.base.log.logger.Logger

/**
 * A WorkManager Worker that executes [PlacesStorage.runMaintenance].
 *
 * If there is a failure or the worker constraints are no longer met during execution,
 * active write operations on [PlacesStorage] are cancelled.
 *
 * See also [mozilla.components.concept.storage.StorageMaintenanceWorker].
 */
internal class PlacesHistoryStorageWorker(context: Context, params: WorkerParameters) :
    StorageMaintenanceWorker(context, params) {

    val logger = Logger(PLACES_HISTORY_STORAGE_WORKER_TAG)

    override suspend fun operate() {
        GlobalPlacesDependencyProvider.requirePlacesStorage()
            .runMaintenance(DB_SIZE_LIMIT_IN_BYTES.toUInt())
    }

    override fun onError(exception: Exception) {
        GlobalPlacesDependencyProvider.requirePlacesStorage().cancelWrites()
        logger.error("An exception occurred while running the maintenance task: ${exception.message}")
    }

    companion object {
        private const val IDENTIFIER_PREFIX = "mozilla.components.browser.storage.sync"
        private const val PLACES_HISTORY_STORAGE_WORKER_TAG = "$IDENTIFIER_PREFIX.PlacesHistoryStorageWorker"

        internal const val DB_SIZE_LIMIT_IN_BYTES = 75 * 1024 * 1024 // corresponds to 75MiB (in bytes)
        internal const val UNIQUE_NAME = "$IDENTIFIER_PREFIX.PlacesHistoryStorageWorker"
    }
}
