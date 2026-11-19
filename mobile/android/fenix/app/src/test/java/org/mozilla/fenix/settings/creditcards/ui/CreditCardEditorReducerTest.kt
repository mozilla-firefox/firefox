/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.settings.creditcards.ui

import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test
import org.mozilla.fenix.settings.creditcards.ui.CreditCardEditorAction.DeleteDialogAction
import org.mozilla.fenix.settings.creditcards.ui.CreditCardEditorAction.FieldChanged

class CreditCardEditorReducerTest {

    @Test
    fun `GIVEN a state, WHEN a CardNumberChanged action is received, then the card number is updated`() {
        val state = createState()

        val result =
            creditCardEditorReducer(
                state = state,
                action = FieldChanged.CardNumberChanged("55554444"),
            )

        assertEquals(
            "Expected card number to be updated",
            "55554444",
            result.cardNumber,
        )
    }

    @Test
    fun `GIVEN a state, WHEN a MonthSelected action is received, then the month is updated`() {
        val state =
            createState(
                expiryMonths = listOf("January", "February", "March"),
                selectedExpiryMonthIndex = 1,
            )

        val result =
            creditCardEditorReducer(
                state = state,
                action = FieldChanged.MonthSelected(index = 2),
            )

        assertEquals(
            "Expected month index is updated",
            2,
            result.selectedExpiryMonthIndex,
        )
    }

    @Test
    fun `GIVEN a state, WHEN a YearSelected action is received, then the year is updated`() {
        val state =
            createState(
                expiryYears = listOf("2025", "2026", "2027"),
                selectedExpiryYearIndex = 0,
            )

        val result =
            creditCardEditorReducer(
                state = state,
                action = FieldChanged.YearSelected(index = 1),
            )

        assertEquals(
            "Expected year index is updated",
            1,
            result.selectedExpiryYearIndex,
        )
    }

    @Test
    fun `GIVEN a state, WHEN a NameOnCardChanged action is received, then the name on card is updated`() {
        val state = createState(nameOnCard = "Jane Doe")

        val result =
            creditCardEditorReducer(
                state = state,
                action = FieldChanged.NameOnCardChanged("Janey Doe"),
            )

        assertEquals(
            "Expected name on card to be updated",
            "Janey Doe",
            result.nameOnCard,
        )
    }

    @Test
    fun `GIVEN a state with name error, WHEN the name changes, then an error is cleared on the name field`() {
        val state = createState(nameOnCard = "", showNameOnCardError = true)

        val result =
            creditCardEditorReducer(
                state = state,
                action = FieldChanged.NameOnCardChanged("Janey Doe"),
            )

        assertFalse(
            "Expected name on card error to be cleared",
            result.showNameOnCardError,
        )
    }

    @Test
    fun `GIVEN a state with card number error, WHEN the card number changes, then an error is cleared on the name field`() {
        val state = createState(cardNumber = "5555", showCardNumberError = true)

        val result =
            creditCardEditorReducer(
                state = state,
                action = FieldChanged.CardNumberChanged("55554"),
            )

        assertFalse(
            "Expected name on card error to be cleared",
            result.showCardNumberError,
        )
    }

    @Test
    fun `GIVEN a state, WHEN a delete dialog cancel action is received, then the dialog is hidden`() {
        val state = createState(showDeleteDialog = true)

        val result =
            creditCardEditorReducer(
                state = state,
                action = DeleteDialogAction.Cancel,
            )

        assertFalse(
            "Expected delete dialog to be hidden",
            result.showDeleteDialog,
        )
    }

    @Test
    fun `GIVEN a state, WHEN a delete dialog confirm action is received, then the dialog is hidden`() {
        val state = createState(showDeleteDialog = true)

        val result =
            creditCardEditorReducer(
                state = state,
                action = DeleteDialogAction.Confirm,
            )

        assertFalse(
            "Expected delete dialog to be hidden",
            result.showDeleteDialog,
        )
    }

    @Test
    fun `GIVEN a state, WHEN a DeleteClicked action is received, then the dialog is shown`() {
        val state = createState(showDeleteDialog = false)

        val result =
            creditCardEditorReducer(
                state = state,
                action = CreditCardEditorAction.DeleteClicked,
            )

        assertTrue(
            "Expected delete dialog to be shown",
            result.showDeleteDialog,
        )
    }

    @Test
    fun `GIVEN a state with invalid card number, WHEN save action is received, then the error is shown`() {
        val state = createState(cardNumber = "3333")

        val result =
            creditCardEditorReducer(
                state = state,
                action = CreditCardEditorAction.Save,
            )

        assertTrue(
            "Expected card number error to be shown",
            result.showCardNumberError,
        )
    }

    @Test
    fun `GIVEN state with card number error, WHEN card number field is changed, then the error is cleared`() {
        val state = createState(showCardNumberError = true)

        val result =
            creditCardEditorReducer(
                state = state,
                action = FieldChanged.CardNumberChanged(cardNumber = "5555"),
            )

        assertFalse(
            "Expected card number error to no longer be shown",
            result.showCardNumberError,
        )
    }

    @Test
    fun `GIVEN state with empty name on card, WHEN a save action is received, then the error is shown`() {
        val state = createState(nameOnCard = "")

        val result =
            creditCardEditorReducer(
                state = state,
                action = CreditCardEditorAction.Save,
            )

        assertTrue(
            "Expected name on card error to be shown",
            result.showNameOnCardError,
        )
    }

    @Test
    fun `GIVEN a state with name on card error, WHEN the name field changes, then the error is cleared`() {
        val state = createState(showNameOnCardError = true)

        val result =
            creditCardEditorReducer(
                state = state,
                action = FieldChanged.NameOnCardChanged(nameOnCard = "John"),
            )

        assertFalse(
            "Expected name on card error to no longer be shown",
            result.showNameOnCardError,
        )
    }

    @Test
    fun `GIVEN a state, WHEN initialization is completed, then the state is updated accordingly`() {
        val oldState = createState()

        val newState =
            createState(
                nameOnCard = "New Name",
                cardNumber = "1234556789900",
            )

        val result =
            creditCardEditorReducer(
                state = oldState,
                action = CreditCardEditorAction.Initialization.InitCompleted(state = newState),
            )

        assertEquals(
            "Expected the state to be what was received in the action",
            newState,
            result,
        )
    }

    @Test
    fun `WHEN a SecurityCodeChanged action is received THEN the security code is updated and the error is cleared`() {
        val state = createState(showSecurityCodeError = true)

        val result = creditCardEditorReducer(state, FieldChanged.SecurityCodeChanged("123"))

        assertEquals("123", result.securityCode)
        assertTrue(result.isSecurityCodeChanged)
        assertFalse(result.showSecurityCodeError)
    }

    @Test
    fun `WHEN a SecurityCodeChanged action has non-digits or more than 4 digits THEN only the first 4 digits are kept`() {
        val result = creditCardEditorReducer(createState(), FieldChanged.SecurityCodeChanged("1a2-3 456"))

        assertEquals("1234", result.securityCode)
    }

    @Test
    fun `WHEN a SecurityCodeCleared action is received THEN the security code is emptied and the error is cleared`() {
        val state = createState(securityCode = "12", showSecurityCodeError = true)

        val result = creditCardEditorReducer(state, FieldChanged.SecurityCodeCleared)

        assertEquals("", result.securityCode)
        assertTrue(result.isSecurityCodeChanged)
        assertFalse(result.showSecurityCodeError)
    }

    @Test
    fun `GIVEN a non American Express card WHEN saving THEN only an empty or 3 digit security code is valid`() {
        assertFalse(saveWithSecurityCode(MASTERCARD_NUMBER, "").showSecurityCodeError)
        assertFalse(saveWithSecurityCode(MASTERCARD_NUMBER, "123").showSecurityCodeError)
        assertTrue(saveWithSecurityCode(MASTERCARD_NUMBER, "12").showSecurityCodeError)
        assertTrue(saveWithSecurityCode(MASTERCARD_NUMBER, "1234").showSecurityCodeError)
    }

    @Test
    fun `GIVEN an American Express card WHEN saving THEN only an empty or 4 digit security code is valid`() {
        assertFalse(saveWithSecurityCode(AMEX_NUMBER, "").showSecurityCodeError)
        assertFalse(saveWithSecurityCode(AMEX_NUMBER, "1234").showSecurityCodeError)
        assertTrue(saveWithSecurityCode(AMEX_NUMBER, "123").showSecurityCodeError)
    }

    @Test
    fun `GIVEN the security code field is hidden WHEN saving THEN the security code is not validated`() {
        val state =
            createState(
                cardNumber = MASTERCARD_NUMBER,
                showSecurityCodeField = false,
                securityCode = "12",
                isSecurityCodeChanged = true,
            )

        val result = creditCardEditorReducer(state, CreditCardEditorAction.Save)

        assertFalse(result.showSecurityCodeError)
    }

    @Test
    fun `GIVEN an unchanged stored security code of the wrong length WHEN saving THEN the security code is not validated`() {
        val mastercardWith4Digits =
            createState(cardNumber = MASTERCARD_NUMBER, showSecurityCodeField = true, securityCode = "1234")
        val amexWith3Digits = createState(cardNumber = AMEX_NUMBER, showSecurityCodeField = true, securityCode = "123")

        assertFalse(creditCardEditorReducer(mastercardWith4Digits, CreditCardEditorAction.Save).showSecurityCodeError)
        assertFalse(creditCardEditorReducer(amexWith3Digits, CreditCardEditorAction.Save).showSecurityCodeError)
    }

    @Test
    fun `Only American Express card numbers are American Express cards`() {
        assertTrue(createState(cardNumber = AMEX_NUMBER).isAmexCard())
        assertFalse(createState(cardNumber = MASTERCARD_NUMBER).isAmexCard())
    }

    private fun saveWithSecurityCode(cardNumber: String, securityCode: String) =
        creditCardEditorReducer(
            createState(
                cardNumber = cardNumber,
                showSecurityCodeField = true,
                securityCode = securityCode,
                isSecurityCodeChanged = true,
            ),
            CreditCardEditorAction.Save,
        )

    private companion object {
        const val MASTERCARD_NUMBER = "5555444433331111"
        const val AMEX_NUMBER = "378282246310005"
    }
}
