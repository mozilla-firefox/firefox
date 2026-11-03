/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.service.sync.autofill

import mozilla.components.concept.storage.Address
import mozilla.components.concept.storage.CreditCard
import mozilla.components.concept.storage.UpdatableAddressFields
import mozilla.components.concept.storage.UpdatableCreditCardFields

// We have type definitions at the concept level, and "external" types defined within Autofill.
// In practice these two types are largely the same, and this file is the conversion point.

/** Conversion from a generic [UpdatableAddressFields] into its richer comrade within the 'autofill' lib. */
internal fun UpdatableAddressFields.into(): mozilla.appservices.autofill.UpdatableAddressFields {
    return mozilla.appservices.autofill.UpdatableAddressFields(
        name = this.name,
        organization = this.organization,
        streetAddress = this.streetAddress,
        addressLevel3 = this.addressLevel3,
        addressLevel2 = this.addressLevel2,
        addressLevel1 = this.addressLevel1,
        postalCode = this.postalCode,
        country = this.country,
        tel = this.tel,
        email = this.email,
    )
}

/** Conversion from a generic [UpdatableCreditCardFields] into its comrade within the 'autofill' lib. */
internal fun UpdatableCreditCardFields.into(): mozilla.appservices.autofill.UpdatableCreditCardFields {
    return mozilla.appservices.autofill.UpdatableCreditCardFields(
        ccName = this.billingName,
        ccNumber = this.cardNumber,
        ccCvv = this.cardCvv,
        ccExpMonth = this.expiryMonth,
        ccExpYear = this.expiryYear,
        ccType = this.cardType,
    )
}

/** Conversion from a "native" autofill [Address] into its generic comrade. */
internal fun mozilla.appservices.autofill.Address.into(): Address {
    return Address(
        guid = this.guid,
        name = this.name,
        organization = this.organization,
        streetAddress = this.streetAddress,
        addressLevel3 = this.addressLevel3,
        addressLevel2 = this.addressLevel2,
        addressLevel1 = this.addressLevel1,
        postalCode = this.postalCode,
        country = this.country,
        tel = this.tel,
        email = this.email,
        timeCreated = this.timeCreated,
        timeLastUsed = this.timeLastUsed,
        timeLastModified = this.timeLastModified,
        timesUsed = this.timesUsed,
    )
}

/** Conversion from a "native" autofill [CreditCard] into its generic comrade. */
internal fun mozilla.appservices.autofill.CreditCard.into(): CreditCard {
    return CreditCard(
        guid = this.guid,
        billingName = this.ccName,
        cardNumber = this.ccNumber,
        cardNumberLast4 = this.ccNumberLast4,
        expiryMonth = this.ccExpMonth,
        expiryYear = this.ccExpYear,
        cardType = this.ccType,
        timeCreated = this.timeCreated,
        timeLastUsed = this.timeLastUsed,
        timeLastModified = this.timeLastModified,
        timesUsed = this.timesUsed,
        cardCvv = this.ccCvv,
    )
}
