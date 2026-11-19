/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.settings.creditcards.ui

/** Creates a [CreditCardEditorState] with the given parameters for use in tests. */
internal fun createState(
    guid: String = "",
    cardNumber: String = "5555444433331111",
    showCardNumberError: Boolean = false,
    nameOnCard: String = "Jane Doe",
    showNameOnCardError: Boolean = false,
    expiryMonths: List<String> = listOf("January", "February", "March"),
    selectedExpiryMonthIndex: Int = 0,
    expiryYears: List<String> = listOf("2025", "2026", "2027"),
    selectedExpiryYearIndex: Int = 1,
    inEditMode: Boolean = false,
    showDeleteDialog: Boolean = false,
    showSecurityCodeField: Boolean = false,
    securityCode: String = "",
    isSecurityCodeChanged: Boolean = false,
    showSecurityCodeError: Boolean = false,
): CreditCardEditorState {
    return CreditCardEditorState(
        guid = guid,
        cardNumber = cardNumber,
        showCardNumberError = showCardNumberError,
        nameOnCard = nameOnCard,
        showNameOnCardError = showNameOnCardError,
        expiryMonths = expiryMonths,
        selectedExpiryMonthIndex = selectedExpiryMonthIndex,
        expiryYears = expiryYears,
        selectedExpiryYearIndex = selectedExpiryYearIndex,
        inEditMode = inEditMode,
        showDeleteDialog = showDeleteDialog,
        showSecurityCodeField = showSecurityCodeField,
        securityCode = securityCode,
        isSecurityCodeChanged = isSecurityCodeChanged,
        showSecurityCodeError = showSecurityCodeError,
    )
}
