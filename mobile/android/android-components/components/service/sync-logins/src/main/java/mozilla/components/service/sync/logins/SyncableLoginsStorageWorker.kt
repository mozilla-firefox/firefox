/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.service.sync.logins

import android.content.Context
import androidx.work.WorkerParameters
import mozilla.components.concept.storage.StorageMaintenanceWorker
import mozilla.components.support.base.log.logger.Logger

/**
 * A WorkManager Worker that executes [SyncableLoginsStorage.runMaintenance].
 *
 * If there is a failure or the worker constraints are no longer met during execution,
 * active write operations on [SyncableLoginsStorage] are cancelled.
 *
 * See also [mozilla.components.concept.storage.StorageMaintenanceWorker].
 */
internal class SyncableLoginsStorageWorker(context: Context, params: WorkerParameters) :
    StorageMaintenanceWorker(context, params) {

    val logger = Logger(PLACES_HISTORY_STORAGE_WORKER_TAG)

    override suspend fun operate() {
        GlobalLoginsDependencyProvider.requireLoginsStorage()
            .runMaintenance(DB_SIZE_LIMIT_IN_BYTES.toUInt())
    }

    override fun onError(exception: Exception) {
        GlobalLoginsDependencyProvider.requireLoginsStorage().cancelWrites()
        logger.error("An exception occurred while running the maintenance task: ${exception.message}")
    }

    companion object {
        private const val IDENTIFIER_PREFIX = "mozilla.components.service.sync.logins"
        private const val PLACES_HISTORY_STORAGE_WORKER_TAG = "$IDENTIFIER_PREFIX.SyncableLoginsStorageWorker"

        internal const val UNIQUE_NAME = "$IDENTIFIER_PREFIX.SyncableLoginsStorageWorker"

        // The implementation of `runMaintenance` on `DatabaseLoginsStorage` doesn't take any input
        // but `Storage` requires that we pass a value.
        internal const val DB_SIZE_LIMIT_IN_BYTES = Int.MAX_VALUE
    }
}
