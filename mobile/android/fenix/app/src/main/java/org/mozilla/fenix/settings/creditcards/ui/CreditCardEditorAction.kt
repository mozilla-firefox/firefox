/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.settings.creditcards.ui

import mozilla.components.concept.storage.CreditCard
import mozilla.components.lib.state.Action

/**
 * Represents the various actions that can be triggered from the credit card editor screen, such as saving a credit card
 * or handling back navigation.
 */
sealed interface CreditCardEditorAction : Action {

    /** A group of actions that are dispatched when the credit card editor is initialized. */
    sealed interface Initialization : CreditCardEditorAction {
        /**
         * Initialize the credit card editor with the provided [CreditCard].
         *
         * @property creditCard Optional [CreditCard] to be initialized in the editor. When this property is null, a new
         *   credit card will be created.
         */
        data class InitStarted(val creditCard: CreditCard?) : Initialization

        /**
         * Signal that the initialization process has completed.
         *
         * @property state The final state of the credit card editor after initialization.
         */
        data class InitCompleted(val state: CreditCardEditorState) : Initialization
    }

    /** An action that signals the intention to navigate back from the current screen. */
    data object NavigateBack : CreditCardEditorAction

    /** An action that represents the user's request to save the entered credit card details. */
    data object Save : CreditCardEditorAction

    /** An action triggered when the user decides to discard the changes and exit the editor. */
    data object Cancel : CreditCardEditorAction

    /** An action triggered when the user wants to delete the current credit card. */
    data object DeleteClicked : CreditCardEditorAction

    /** A group of actions for handling the delete confirmation dialog. */
    sealed interface DeleteDialogAction : CreditCardEditorAction {

        /** An action that signals the user's intention to cancel the delete operation. */
        data object Cancel : DeleteDialogAction

        /** An action that signals the user's confirmation to delete the credit card. */
        data object Confirm : DeleteDialogAction
    }

    /** A group of actions that are dispatched whenever a field in the credit card form is modified. */
    sealed interface FieldChanged : CreditCardEditorAction {

        /**
         * Dispatched when the credit card number is changed by the user.
         *
         * @property cardNumber The updated credit card number string.
         */
        data class CardNumberChanged(val cardNumber: String) : FieldChanged

        /**
         * Dispatched when the name on the credit card is changed.
         *
         * @property nameOnCard The updated name on the card.
         */
        data class NameOnCardChanged(val nameOnCard: String) : FieldChanged

        /**
         * Dispatched when the expiration month of the credit card is selected.
         *
         * @property index The index of the selected month.
         */
        data class MonthSelected(val index: Int) : FieldChanged

        /**
         * Dispatched when the expiration year of the credit card is selected.
         *
         * @property index The index of the selected year.
         */
        data class YearSelected(val index: Int) : FieldChanged

        /**
         * Dispatched when the security code of the credit card is changed.
         *
         * @property securityCode The updated security code.
         */
        data class SecurityCodeChanged(val securityCode: String) : FieldChanged

        /** Dispatched when the user clears the security code with the clear button of the field. */
        data object SecurityCodeCleared : FieldChanged
    }
}
