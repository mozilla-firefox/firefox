/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.browser.engine.gecko.ext

import androidx.test.ext.junit.runners.AndroidJUnit4
import mozilla.components.concept.storage.CreditCardEntry
import org.junit.Assert.assertEquals
import org.junit.Assert.assertNull
import org.junit.Test
import org.junit.runner.RunWith
import org.mozilla.geckoview.Autocomplete

@RunWith(AndroidJUnit4::class)
class CreditCardTest {

    private fun autocompleteCreditCard(securityCode: String?) =
        Autocomplete.CreditCard.Builder()
            .guid("1")
            .name("Jon Doe")
            .number("4111111111111111")
            .expirationMonth("12")
            .expirationYear("2028")
            .securityCode(securityCode)
            .build()

    @Test
    fun `GIVEN a GeckoView credit card with a security code WHEN it is converted THEN the entry has the security code`() {
        val entry = autocompleteCreditCard(securityCode = "123").toCreditCardEntry()

        assertEquals("123", entry.cardCvv)
    }

    @Test
    fun `GIVEN a GeckoView credit card without a security code WHEN it is converted THEN the entry has no security code`() {
        assertNull(autocompleteCreditCard(securityCode = "").toCreditCardEntry().cardCvv)
        assertNull(autocompleteCreditCard(securityCode = null).toCreditCardEntry().cardCvv)
    }

    @Test
    fun `GIVEN an entry with a security code WHEN it is converted THEN the GeckoView credit card has the security code`() {
        val entry =
            CreditCardEntry(
                guid = "1",
                name = "Jon Doe",
                number = "4111111111111111",
                expiryMonth = "12",
                expiryYear = "2028",
                cardType = "visa",
                cardCvv = "123",
            )

        assertEquals("123", entry.toAutocompleteCreditCard().securityCode)
    }

    @Test
    fun `GIVEN an entry without a security code WHEN it is converted THEN the GeckoView credit card has an empty security code`() {
        val entry =
            CreditCardEntry(
                guid = "1",
                name = "Jon Doe",
                number = "4111111111111111",
                expiryMonth = "12",
                expiryYear = "2028",
                cardType = "visa",
                cardCvv = null,
            )

        assertEquals("", entry.toAutocompleteCreditCard().securityCode)
    }
}
