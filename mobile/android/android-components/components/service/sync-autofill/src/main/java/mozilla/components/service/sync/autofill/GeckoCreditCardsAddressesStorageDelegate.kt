/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.service.sync.autofill

import mozilla.components.concept.storage.Address
import mozilla.components.concept.storage.CreditCard
import mozilla.components.concept.storage.CreditCardEntry
import mozilla.components.concept.storage.CreditCardValidationDelegate
import mozilla.components.concept.storage.CreditCardsAddressesStorage
import mozilla.components.concept.storage.CreditCardsAddressesStorageDelegate
import mozilla.components.concept.storage.NewCreditCardFields
import mozilla.components.concept.storage.UpdatableAddressFields
import mozilla.components.concept.storage.UpdatableCreditCardFields

/**
 * [CreditCardsAddressesStorageDelegate] implementation.
 *
 * @param storage The [CreditCardsAddressesStorage] used for looking up addresses and credit cards to autofill.
 * @param isCreditCardAutofillEnabled callback allowing to limit [storage] operations if autofill is disabled.
 * @param validationDelegate The [DefaultCreditCardValidationDelegate] used to check if a credit card can be saved in
 *   [storage] and returns information about why it can or cannot
 */
class GeckoCreditCardsAddressesStorageDelegate(
    private val storage: Lazy<CreditCardsAddressesStorage>,
    private val validationDelegate: DefaultCreditCardValidationDelegate = DefaultCreditCardValidationDelegate(storage),
    private val isCreditCardAutofillEnabled: () -> Boolean = { false },
    private val isAddressAutofillEnabled: () -> Boolean = { false },
) : CreditCardsAddressesStorageDelegate {

    override suspend fun onAddressesFetch(): List<Address> =
        if (!isAddressAutofillEnabled()) {
            emptyList()
        } else {
            storage.value.getAllAddresses()
        }

    override suspend fun onAddressSave(address: Address) {
        val fields =
            UpdatableAddressFields(
                name = address.name,
                organization = address.organization,
                streetAddress = address.streetAddress,
                addressLevel3 = address.addressLevel3,
                addressLevel2 = address.addressLevel2,
                addressLevel1 = address.addressLevel1,
                postalCode = address.postalCode,
                country = address.country,
                tel = address.tel,
                email = address.email,
            )

        if (address.guid.isBlank()) {
            storage.value.addAddress(fields)
        } else {
            storage.value.updateAddress(address.guid, fields)
        }
    }

    override suspend fun onCreditCardsFetch(): List<CreditCard> =
        if (!isCreditCardAutofillEnabled()) {
            emptyList()
        } else {
            storage.value.getAllCreditCards()
        }

    override suspend fun onCreditCardSave(creditCard: CreditCardEntry) {
        if (!creditCard.isValid) return
        when (val result = validationDelegate.shouldCreateOrUpdate(creditCard)) {
            is CreditCardValidationDelegate.Result.CanBeCreated -> {
                storage.value.addCreditCard(
                    NewCreditCardFields(
                        billingName = creditCard.name,
                        cardNumber = creditCard.number,
                        expiryMonth = creditCard.expiryMonth.toLong(),
                        expiryYear = creditCard.expiryYear.toLong(),
                        cardType = creditCard.cardType,
                        cardCvv = creditCard.cardCvv,
                    )
                )
            }
            is CreditCardValidationDelegate.Result.CanBeUpdated -> {
                storage.value.updateCreditCard(
                    guid = result.foundCreditCard.guid,
                    creditCardFields =
                        UpdatableCreditCardFields(
                            billingName = creditCard.name,
                            cardNumber = creditCard.number,
                            expiryMonth = creditCard.expiryMonth.toLong(),
                            expiryYear = creditCard.expiryYear.toLong(),
                            cardType = creditCard.cardType,
                            cardCvv = creditCard.cardCvv,
                        ),
                )
            }
        }
    }
}
