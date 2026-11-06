/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.settings.creditcards

import mozilla.components.concept.storage.CreditCard
import mozilla.components.support.utils.CreditCardNetworkType
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test
import org.mozilla.fenix.settings.creditcards.view.CreditCardsAdapter

class CreditCardsAdapterTest {

    @Test
    fun testDiffCallback() {
        val creditCard1 =
            CreditCard(
                guid = "id",
                billingName = "Banana Apple",
                cardNumber = "4111111111111110",
                cardNumberLast4 = "1110",
                expiryMonth = 1,
                expiryYear = 2030,
                cardType = CreditCardNetworkType.AMEX.cardName,
                timeCreated = 1L,
                timeLastUsed = 1L,
                timeLastModified = 1L,
                timesUsed = 1L,
                cardCvv = null,
            )
        val creditCard2 =
            CreditCard(
                guid = "id",
                billingName = "Banana Apple",
                cardNumber = "4111111111111110",
                cardNumberLast4 = "1110",
                expiryMonth = 1,
                expiryYear = 2030,
                cardType = CreditCardNetworkType.AMEX.cardName,
                timeCreated = 1L,
                timeLastUsed = 1L,
                timeLastModified = 1L,
                timesUsed = 1L,
                cardCvv = null,
            )

        assertTrue(CreditCardsAdapter.DiffCallback.areItemsTheSame(creditCard1, creditCard2))
        assertTrue(CreditCardsAdapter.DiffCallback.areContentsTheSame(creditCard1, creditCard2))

        val creditCard3 =
            CreditCard(
                guid = "id3",
                billingName = "Banana Apple",
                cardNumber = "4111111111111110",
                cardNumberLast4 = "1110",
                expiryMonth = 1,
                expiryYear = 2030,
                cardType = CreditCardNetworkType.AMEX.cardName,
                timeCreated = 1L,
                timeLastUsed = 1L,
                timeLastModified = 1L,
                timesUsed = 1L,
                cardCvv = null,
            )

        assertFalse(CreditCardsAdapter.DiffCallback.areItemsTheSame(creditCard1, creditCard3))
        assertFalse(CreditCardsAdapter.DiffCallback.areContentsTheSame(creditCard1, creditCard3))
    }
}
