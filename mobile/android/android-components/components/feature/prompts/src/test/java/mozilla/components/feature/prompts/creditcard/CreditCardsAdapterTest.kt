/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.feature.prompts.creditcard

import mozilla.components.concept.storage.CreditCardEntry
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test

class CreditCardsAdapterTest {

    @Test
    fun testDiffCallback() {
        val creditCard1 =
            CreditCardEntry(
                guid = "1",
                name = "Banana Apple",
                number = "4111111111111110",
                expiryMonth = "5",
                expiryYear = "2030",
                cardType = "amex",
                cardCvv = null,
            )
        val creditCard2 = creditCard1.copy()

        assertTrue(CreditCardsAdapter.DiffCallback.areItemsTheSame(creditCard1, creditCard2))
        assertTrue(CreditCardsAdapter.DiffCallback.areContentsTheSame(creditCard1, creditCard2))

        val creditCard3 =
            CreditCardEntry(
                guid = "2",
                name = "Pineapple Orange",
                number = "4111111111115555",
                expiryMonth = "1",
                expiryYear = "2030",
                cardType = "amex",
                cardCvv = null,
            )

        assertFalse(CreditCardsAdapter.DiffCallback.areItemsTheSame(creditCard1, creditCard3))
        assertFalse(CreditCardsAdapter.DiffCallback.areContentsTheSame(creditCard1, creditCard3))
    }
}
