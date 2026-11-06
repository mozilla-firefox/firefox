/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.settings.creditcards.ui

import mozilla.components.concept.storage.Address
import mozilla.components.concept.storage.CreditCard
import mozilla.components.concept.storage.CreditCardCrypto
import mozilla.components.concept.storage.CreditCardsAddressesStorage
import mozilla.components.concept.storage.ManagedKey
import mozilla.components.concept.storage.NewCreditCardFields
import mozilla.components.concept.storage.UpdatableAddressFields
import mozilla.components.concept.storage.UpdatableCreditCardFields

/** Fake implementation of [CreditCardsAddressesStorage] that is used for testing credit cards feature */
class FakeCreditCardsStorage(
    var deletedCard: String? = null,
    var newAddedCard: NewCreditCardFields? = null,
    var updatedCard: Pair<String, UpdatableCreditCardFields>? = null,
) : CreditCardsAddressesStorage {

    override suspend fun addCreditCard(creditCardFields: NewCreditCardFields): CreditCard {
        newAddedCard = creditCardFields
        return CreditCard(
            guid = "new-card-id",
            billingName = creditCardFields.billingName,
            cardNumber = creditCardFields.cardNumber,
            cardNumberLast4 = creditCardFields.cardNumber.takeLast(4),
            expiryMonth = creditCardFields.expiryMonth,
            expiryYear = creditCardFields.expiryYear,
            cardType = creditCardFields.cardType,
            cardCvv = creditCardFields.cardCvv,
        )
    }

    override suspend fun updateCreditCard(
        guid: String,
        creditCardFields: UpdatableCreditCardFields,
    ) {
        updatedCard = Pair(guid, creditCardFields)
    }

    override suspend fun getCreditCard(guid: String): CreditCard? = null

    override suspend fun getAllCreditCards(): List<CreditCard> = emptyList()

    override suspend fun countAllCreditCards(): Long {
        return getAllCreditCards().count().toLong()
    }

    override suspend fun deleteCreditCard(guid: String): Boolean {
        deletedCard = guid
        return true
    }

    override suspend fun touchCreditCard(guid: String) = Unit

    override fun getCreditCardCrypto(): CreditCardCrypto {
        return object : CreditCardCrypto {
            override suspend fun getOrGenerateKey(): ManagedKey {
                return ManagedKey(key = "key")
            }
        }
    }

    override suspend fun scrubEncryptedData() {
        error("Not yet implemented")
    }

    override suspend fun addAddress(addressFields: UpdatableAddressFields): Address {
        throw NotImplementedError("Address features are not used in this test")
    }

    override suspend fun getAddress(guid: String): Address? = null

    override suspend fun getAllAddresses(): List<Address> = emptyList()

    override suspend fun countAllAddresses(): Long {
        return getAllAddresses().size.toLong()
    }

    override suspend fun updateAddress(guid: String, address: UpdatableAddressFields) = Unit

    override suspend fun deleteAddress(guid: String): Boolean = false

    override suspend fun touchAddress(guid: String) = Unit
}
