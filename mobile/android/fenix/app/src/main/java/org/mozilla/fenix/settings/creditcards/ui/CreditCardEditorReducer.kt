/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.settings.creditcards.ui

import mozilla.components.support.utils.CreditCardNetworkType
import mozilla.components.support.utils.creditCardIIN
import mozilla.components.support.utils.validateCreditCardNumber

private const val SECURITY_CODE_LENGTH = 3
internal const val AMEX_SECURITY_CODE_LENGTH = 4

/**
 * Reducer for the [CreditCardEditorStore].
 *
 * @param state The current state of the store.
 * @param action The action to be handled.
 * @return The new state of the store.
 */
internal fun creditCardEditorReducer(
    state: CreditCardEditorState,
    action: CreditCardEditorAction,
): CreditCardEditorState {
    return when (action) {
        is CreditCardEditorAction.Initialization -> state.handleInitialization(action)
        is CreditCardEditorAction.FieldChanged -> state.handleFieldChange(action)
        is CreditCardEditorAction.DeleteClicked -> state.copy(showDeleteDialog = true)
        is CreditCardEditorAction.DeleteDialogAction -> state.copy(showDeleteDialog = false)
        is CreditCardEditorAction.Save -> state.validateInput()
        else -> state
    }
}

private fun CreditCardEditorState.validateInput(): CreditCardEditorState {
    val validCardNumber = cardNumber.validateCreditCardNumber()
    val validNameOnCard = nameOnCard.isNotBlank()
    val securityCodeError =
        showSecurityCodeField &&
            // An unchanged security code is kept as stored, so it is not validated.
            isSecurityCodeChanged &&
            securityCode.isNotEmpty() &&
            securityCode.length != requiredSecurityCodeLength()

    return copy(
        showNameOnCardError = !validNameOnCard,
        showCardNumberError = !validCardNumber,
        showSecurityCodeError = securityCodeError,
    )
}

/** Whether the entered card number is an American Express card number. */
internal fun CreditCardEditorState.isAmexCard() =
    cardNumber.creditCardIIN()?.creditCardIssuerNetwork?.name == CreditCardNetworkType.AMEX.cardName

private fun CreditCardEditorState.requiredSecurityCodeLength() =
    if (isAmexCard()) AMEX_SECURITY_CODE_LENGTH else SECURITY_CODE_LENGTH

private fun CreditCardEditorState.handleInitialization(
    action: CreditCardEditorAction.Initialization
): CreditCardEditorState {
    return when (action) {
        is CreditCardEditorAction.Initialization.InitCompleted ->
            copy(
                guid = action.state.guid,
                cardNumber = action.state.cardNumber,
                showCardNumberError = action.state.showCardNumberError,
                nameOnCard = action.state.nameOnCard,
                showNameOnCardError = action.state.showNameOnCardError,
                expiryMonths = action.state.expiryMonths,
                selectedExpiryMonthIndex = action.state.selectedExpiryMonthIndex,
                expiryYears = action.state.expiryYears,
                selectedExpiryYearIndex = action.state.selectedExpiryYearIndex,
                inEditMode = action.state.inEditMode,
                showDeleteDialog = action.state.showDeleteDialog,
                showSecurityCodeField = action.state.showSecurityCodeField,
                securityCode = action.state.securityCode,
                isSecurityCodeChanged = action.state.isSecurityCodeChanged,
                showSecurityCodeError = action.state.showSecurityCodeError,
            )

        else -> this
    }
}

private fun CreditCardEditorState.handleFieldChange(
    action: CreditCardEditorAction.FieldChanged
): CreditCardEditorState {
    return when (action) {
        is CreditCardEditorAction.FieldChanged.CardNumberChanged ->
            copy(
                cardNumber = action.cardNumber,
                showCardNumberError = false,
            )

        is CreditCardEditorAction.FieldChanged.MonthSelected -> copy(selectedExpiryMonthIndex = action.index)
        is CreditCardEditorAction.FieldChanged.NameOnCardChanged ->
            copy(
                nameOnCard = action.nameOnCard,
                showNameOnCardError = false,
            )

        is CreditCardEditorAction.FieldChanged.YearSelected -> copy(selectedExpiryYearIndex = action.index)
        is CreditCardEditorAction.FieldChanged.SecurityCodeChanged ->
            copy(
                securityCode = action.securityCode.filter { it.isDigit() }.take(AMEX_SECURITY_CODE_LENGTH),
                isSecurityCodeChanged = true,
                showSecurityCodeError = false,
            )

        is CreditCardEditorAction.FieldChanged.SecurityCodeCleared ->
            copy(
                securityCode = "",
                isSecurityCodeChanged = true,
                showSecurityCodeError = false,
            )
    }
}
