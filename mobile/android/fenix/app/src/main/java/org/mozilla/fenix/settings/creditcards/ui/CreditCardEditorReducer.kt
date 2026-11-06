/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.settings.creditcards.ui

import mozilla.components.support.utils.validateCreditCardNumber

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

    return copy(showNameOnCardError = !validNameOnCard, showCardNumberError = !validCardNumber)
}

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
    }
}
