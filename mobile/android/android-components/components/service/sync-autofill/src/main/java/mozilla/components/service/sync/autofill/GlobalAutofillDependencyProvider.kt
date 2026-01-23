/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.service.sync.autofill

import androidx.annotation.VisibleForTesting
import mozilla.components.concept.storage.CreditCardsAddressesStorage
import mozilla.components.concept.storage.LoginsStorage

/**
 * Provides global access to the dependencies needed for autofill storage operations.
 * */
object GlobalAutofillDependencyProvider {

    @VisibleForTesting
    internal var autofillStorage: Lazy<CreditCardsAddressesStorage>? = null

    /**
     * Initializes logins storage for running the maintenance task via [AutofillStorageWorker].
     * This method should be called in client application's onCreate method and before
     * [AutofillCreditCardsAddressesStorage.registerStorageMaintenanceWorker] in order to run the worker while
     * the app is not running.
     * */
    fun initialize(autofillStorage: Lazy<CreditCardsAddressesStorage>) {
        this.autofillStorage = autofillStorage
    }

    /**
     * Provides [LoginsStorage] globally when needed for [AutofillStorageWorker]
     * to run maintenance on the storage.
     * */
    internal fun requireAutofillStorage(): CreditCardsAddressesStorage {
        return requireNotNull(autofillStorage?.value) {
            "GlobalAutofillDependencyProvider.initialize must be called before accessing the Autofill storage"
        }
    }
}
