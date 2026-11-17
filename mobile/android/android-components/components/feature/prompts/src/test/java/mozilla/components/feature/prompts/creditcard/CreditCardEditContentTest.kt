/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.feature.prompts.creditcard

import mozilla.components.concept.storage.CreditCardEntry
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test

class CreditCardEditContentTest {

    private val creditCard =
        CreditCardEntry(
            guid = "1",
            name = "Banana Apple",
            number = "5555555555554444",
            expiryMonth = "5",
            expiryYear = "2030",
            cardType = "mastercard",
            cardCvv = "123",
        )

    @Test
    fun `WHEN edits with a different card number are applied THEN the entry has the edited values and no guid`() {
        val edited =
            creditCard.withEdits(
                number = "4111 1111 1111 1111",
                name = "  Firstname Lastname ",
                expiryMonth = 8,
                expiryYear = 2031,
                securityCode = "4567",
            )

        assertEquals(
            CreditCardEntry(
                guid = null,
                name = "Firstname Lastname",
                number = "4111111111111111",
                expiryMonth = "8",
                expiryYear = "2031",
                cardType = "visa",
                cardCvv = "4567",
            ),
            edited,
        )
    }

    @Test
    fun `WHEN edits with the same card number are applied THEN the entry keeps its guid`() {
        val edited =
            creditCard.withEdits(
                number = "5555 5555 5555 4444",
                name = "Firstname Lastname",
                expiryMonth = 8,
                expiryYear = 2031,
                securityCode = "123",
            )

        assertEquals("1", edited.guid)
        assertEquals("5555555555554444", edited.number)
    }

    @Test
    fun `WHEN edits are applied with an empty security code THEN the entry has no security code`() {
        val edited =
            creditCard.withEdits(
                number = creditCard.number,
                name = creditCard.name,
                expiryMonth = 5,
                expiryYear = 2030,
                securityCode = "",
            )

        assertEquals(null, edited.cardCvv)
    }

    @Test
    fun `GIVEN valid edits WHEN checking whether they can be saved THEN they can`() {
        assertTrue(
            canSaveEdits(
                isNumberChanged = true,
                isNumberValid = true,
                isSecurityCodeValid = true,
                month = 8,
                year = 2031,
            )
        )
    }

    @Test
    fun `GIVEN an unchanged card number that is not valid WHEN checking whether the edits can be saved THEN they can`() {
        assertTrue(
            canSaveEdits(
                isNumberChanged = false,
                isNumberValid = false,
                isSecurityCodeValid = true,
                month = 8,
                year = 2031,
            )
        )
    }

    @Test
    fun `GIVEN a changed card number that is not valid WHEN checking whether the edits can be saved THEN they cannot`() {
        assertFalse(
            canSaveEdits(
                isNumberChanged = true,
                isNumberValid = false,
                isSecurityCodeValid = true,
                month = 8,
                year = 2031,
            )
        )
    }

    @Test
    fun `GIVEN a security code that is not valid WHEN checking whether the edits can be saved THEN they cannot`() {
        assertFalse(
            canSaveEdits(
                isNumberChanged = false,
                isNumberValid = true,
                isSecurityCodeValid = false,
                month = 8,
                year = 2031,
            )
        )
    }

    @Test
    fun `GIVEN no expiry month or year WHEN checking whether the edits can be saved THEN they cannot`() {
        assertFalse(
            canSaveEdits(
                isNumberChanged = false,
                isNumberValid = true,
                isSecurityCodeValid = true,
                month = null,
                year = 2031,
            )
        )
        assertFalse(
            canSaveEdits(
                isNumberChanged = false,
                isNumberValid = true,
                isSecurityCodeValid = true,
                month = 8,
                year = null,
            )
        )
    }
}
