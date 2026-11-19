/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.settings.creditcards.ui

import mozilla.components.lib.state.State

/**
 * State defining the "Edit Credit Card" screen
 *
 * @property guid The unique identifier for the edited credit card.
 * @property cardNumber The credit card number of the card being edited.
 * @property showCardNumberError Indicates whether or not to show an error on the "card number" field.
 * @property nameOnCard The credit card name.
 * @property showNameOnCardError Indicates whether or not to show an error on the "name on card" field.
 * @property expiryMonths The non-empty list of expiry month options for display.
 * @property selectedExpiryMonthIndex The index of the selected expiry month.
 * @property expiryYears The non-empty list of expiry year options for display.
 * @property selectedExpiryYearIndex The index of the selected expiry year.
 * @property inEditMode Indicates whether or not the state is in edit more, or create mode.
 * @property showDeleteDialog Indicates whether or not to show the delete dialog.
 * @property showSecurityCodeField Indicates whether to show the "CVV" field.
 * @property securityCode The credit card security code.
 * @property isSecurityCodeChanged Indicates whether the user changed the security code. An unchanged security code
 *   keeps the stored one when the card is updated.
 * @property showSecurityCodeError Indicates whether to show an error on the "CVV" field.
 */
data class CreditCardEditorState(
    val guid: String,
    val cardNumber: String,
    val showCardNumberError: Boolean = false,
    val nameOnCard: String,
    val showNameOnCardError: Boolean = false,
    val expiryMonths: List<String>,
    val selectedExpiryMonthIndex: Int,
    val expiryYears: List<String>,
    val selectedExpiryYearIndex: Int,
    val inEditMode: Boolean,
    val showDeleteDialog: Boolean = false,
    val showSecurityCodeField: Boolean = false,
    val securityCode: String = "",
    val isSecurityCodeChanged: Boolean = false,
    val showSecurityCodeError: Boolean = false,
) : State {

    companion object {
        val Default =
            CreditCardEditorState(
                guid = "",
                cardNumber = "",
                nameOnCard = "",
                inEditMode = false,
                expiryMonths = listOf(),
                selectedExpiryMonthIndex = 0,
                expiryYears = listOf(),
                selectedExpiryYearIndex = 0,
            )
    }
}
