/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.concept.storage

import mozilla.components.concept.storage.CreditCard.Companion.ELLIPSES_END
import mozilla.components.concept.storage.CreditCard.Companion.ELLIPSES_START
import mozilla.components.concept.storage.CreditCard.Companion.ELLIPSIS
import mozilla.components.support.ktx.kotlin.last4Digits
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Test

class CreditCardEntryTest {

    private val creditCard =
        CreditCardEntry(
            guid = "1",
            name = "Banana Apple",
            number = "4111111111111110",
            expiryMonth = "5",
            expiryYear = "2030",
            cardType = "amex",
            cardCvv = null,
        )

    @Test
    fun `WHEN obfuscatedCardNumber getter is called THEN the expected obfuscated card number is returned`() {
        assertEquals(
            ELLIPSES_START + ELLIPSIS + ELLIPSIS + ELLIPSIS + ELLIPSIS + creditCard.number.last4Digits() + ELLIPSES_END,
            creditCard.obfuscatedCardNumber,
        )
    }

    @Test
    fun `WHEN expiryDdate getter is called THEN the expected expiry date string is returned`() {
        assertEquals("0${creditCard.expiryMonth}/${creditCard.expiryYear}", creditCard.expiryDate)
    }

    @Test
    fun `GIVEN empty expiration date strings WHEN a credit card needs to display its full expiration date THEN the an empty string is returned`() {
        val creditCardWithoutYear =
            CreditCardEntry(
                guid = "1",
                name = "Banana Apple",
                number = "4111111111111110",
                expiryMonth = "5",
                expiryYear = "",
                cardType = "amex",
                cardCvv = null,
            )
        val creditCardWithoutMonth =
            CreditCardEntry(
                guid = "1",
                name = "Banana Apple",
                number = "4111111111111110",
                expiryMonth = "",
                expiryYear = "2030",
                cardType = "amex",
                cardCvv = null,
            )
        val creditCardWithoutFullDate =
            CreditCardEntry(
                guid = "1",
                name = "Banana Apple",
                number = "4111111111111110",
                expiryMonth = "",
                expiryYear = "",
                cardType = "amex",
                cardCvv = null,
            )

        assertEquals("", creditCardWithoutYear.expiryDate)
        assertEquals("", creditCardWithoutMonth.expiryDate)
        assertEquals("", creditCardWithoutFullDate.expiryDate)
    }

    @Test
    fun `GIVEN empty number THEN entry is considered invalid`() {
        val entry = creditCard.copy(number = "")

        assertFalse(entry.isValid)
    }

    @Test
    fun `GIVEN empty expiry month THEN entry is considered invalid`() {
        val entry = creditCard.copy(expiryMonth = "")

        assertFalse(entry.isValid)
    }

    @Test
    fun `GIVEN empty expiry year THEN entry is considered invalid`() {
        val entry = creditCard.copy(expiryYear = "")

        assertFalse(entry.isValid)
    }
}
