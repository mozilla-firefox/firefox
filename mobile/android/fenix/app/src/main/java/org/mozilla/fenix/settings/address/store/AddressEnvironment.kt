/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.settings.address.store

import mozilla.components.concept.engine.autofill.AddressStructure
import mozilla.components.concept.storage.Address
import mozilla.components.concept.storage.UpdatableAddressFields

/**
 * Groups together all of the [AddressStore] dependencies.
 *
 * @property navigateBack used to navigate back.
 * @property createAddress used to create a new [Address].
 * @property updateAddress used to update an existing [Address].
 * @property deleteAddress used to delete an [Address].
 * @property getAddressStructure used to fetch an [AddressStructure].
 * @property submitCaughtException used to submit caught exceptions.
 */
data class AddressEnvironment(
    val navigateBack: () -> Unit,
    val createAddress: suspend (address: UpdatableAddressFields) -> Unit,
    val updateAddress: suspend (guid: String, address: UpdatableAddressFields) -> Unit,
    val deleteAddress: suspend (guid: String) -> Unit,
    val getAddressStructure: suspend (countryCode: String) -> AddressStructure,
    val submitCaughtException: (Throwable) -> Unit,
) {
    internal companion object {
        val empty: AddressEnvironment
            get() =
                AddressEnvironment(
                    navigateBack = {},
                    createAddress = {},
                    updateAddress = { _, _ -> },
                    deleteAddress = {},
                    getAddressStructure = { AddressStructure(listOf()) },
                    submitCaughtException = { _ -> },
                )
    }
}
