/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.service.sync.logins

import androidx.annotation.VisibleForTesting
import mozilla.components.concept.storage.LoginsStorage

/**
 * Provides global access to the dependencies needed for logins storage operations.
 * */
object GlobalLoginsDependencyProvider {

    @VisibleForTesting
    internal var loginsStorage: Lazy<LoginsStorage>? = null

    /**
     * Initializes logins storage for running the maintenance task via [SyncableLoginsStorageWorker].
     * This method should be called in client application's onCreate method and before
     * [SyncableLoginsStorage.registerStorageMaintenanceWorker] in order to run the worker while
     * the app is not running.
     * */
    fun initialize(loginsStorage: Lazy<LoginsStorage>) {
        this.loginsStorage = loginsStorage
    }

    /**
     * Provides [LoginsStorage] globally when needed for [SyncableLoginsStorageWorker]
     * to run maintenance on the storage.
     * */
    internal fun requireLoginsStorage(): LoginsStorage {
        return requireNotNull(loginsStorage?.value) {
            "GlobalLoginsDependencyProvider.initialize must be called before accessing the Logins storage"
        }
    }
}
