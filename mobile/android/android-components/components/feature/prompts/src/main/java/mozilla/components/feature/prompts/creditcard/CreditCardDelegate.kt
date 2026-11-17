/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.feature.prompts.creditcard

import mozilla.components.concept.storage.CreditCardEntry
import mozilla.components.feature.prompts.concept.AutocompletePrompt

/** Delegate for credit card picker and related callbacks */
interface CreditCardDelegate {
    /**
     * The [AutocompletePrompt] used for [CreditCardPicker] to display a a prompt with a list of credit cards available
     * for autocomplete.
     */
    val creditCardPickerView: AutocompletePrompt<CreditCardEntry>?
        get() = null

    /** Callback invoked when a user selects "Manage credit cards" from the select credit card prompt. */
    val onManageCreditCards: () -> Unit
        get() = {}

    /** Callback invoked when a user selects a credit card option from the select credit card prompt */
    val onSelectCreditCard: () -> Unit
        get() = {}

    /**
     * Whether the save credit card prompt shows the security code and the "Save security codes" toggle. When false, the
     * prompt does not save the security code.
     */
    val isSecurityCodeSaveEnabled: () -> Boolean
        get() = { false }

    /** The stored state of the "Save security codes" toggle. */
    val saveSecurityCode: () -> Boolean
        get() = { true }

    /**
     * Callback invoked with the state of the "Save security codes" toggle when the user saves a credit card from the
     * save credit card prompt. It is not invoked when the user dismisses the prompt.
     */
    val onSaveSecurityCodeChanged: (Boolean) -> Unit
        get() = {}
}
