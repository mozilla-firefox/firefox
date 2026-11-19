/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.settings.creditcards.ui

/** Test tags for the credit card editor screen. */
object CreditCardEditorTestTags {

    private const val PREFIX = "credit_card.edit"

    /** Test tag for the card number input field. */
    const val CARD_NUMBER_FIELD = "$PREFIX.field.card_number"

    /** Test tag for the name on card input field. */
    const val NAME_ON_CARD_FIELD = "$PREFIX.field.name_on_card"

    /** Test tag for the expiration month input field. */
    const val EXPIRATION_MONTH_FIELD = "$PREFIX.field.expiration_month"

    /** Test tag for the expiration year input field. */
    const val EXPIRATION_YEAR_FIELD = "$PREFIX.field.expiration_year"

    /** Test tag for the save button. */
    const val SAVE_BUTTON = "$PREFIX.button.save"

    /** Test tag for the cancel button. */
    const val CANCEL_BUTTON = "$PREFIX.button.cancel"

    /** Test tag for the delete button. */
    const val DELETE_BUTTON = "$PREFIX.button.delete"

    /** Test tag for the cancel button in the delete confirmation dialog. */
    const val DELETE_DIALOG_CANCEL_BUTTON = "$PREFIX.dialog.button.cancel"

    /** Test tag for the delete button in the delete confirmation dialog. */
    const val DELETE_DIALOG_DELETE_BUTTON = "$PREFIX.dialog.button.delete"

    /** Test tag for the delete button in the top bar. */
    const val TOPBAR_DELETE_BUTTON = "$PREFIX.topbar.button.delete"

    /** Test tag for the save button in the top bar. */
    const val TOPBAR_SAVE_BUTTON = "$PREFIX.topbar.button.save"

    /** Test tag for the CVV input field. */
    const val SECURITY_CODE_FIELD = "$PREFIX.field.security_code"
}
