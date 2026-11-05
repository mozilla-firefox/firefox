/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.ui.efficiency.selectors

import org.mozilla.fenix.R
import org.mozilla.fenix.helpers.DataGenerationHelper.getStringResource
import org.mozilla.fenix.settings.address.ui.edit.EditAddressTestTag
import org.mozilla.fenix.settings.creditcards.ui.CreditCardEditorTestTags
import org.mozilla.fenix.ui.efficiency.helpers.PageReadinessProfiles
import org.mozilla.fenix.ui.efficiency.helpers.Selector
import org.mozilla.fenix.ui.efficiency.helpers.SelectorContainer
import org.mozilla.fenix.ui.efficiency.helpers.SelectorGroup
import org.mozilla.fenix.ui.efficiency.helpers.SelectorStrategy

object SettingsAutofillSelectors : SelectorContainer {
    enum class Group : SelectorGroup {
        AUTOFILL_SETTINGS,
        ADD_ADDRESS,
        MANAGE_ADDRESSES,
        ADDRESS_FORM,
        DELETE_ADDRESS,
        ADD_CREDIT_CARD,
        MANAGE_CREDIT_CARDS,
        EDIT_CREDIT_CARD,
        CREDIT_CARD_FORM,
    }

    val SETTINGS_AUTOFILL_TITLE =
        navigationToolbarTitle(
            title = getStringResource(R.string.preferences_autofill),
            description = "Autofill toolbar title",
        )

    // Content anchor for the page's NAVIGATION_READY/INTERACTIVE readiness. Kept on UiAutomator (like the
    // other content selectors here) rather than Espresso: an Espresso probe gates on the app reaching idle
    // before each poll, which right after the Settings->Autofill fragment transition can exhaust the 10s
    // readiness budget and time out; the a11y-tree query has no such coupling.
    val AUTOFILL_ADDRESSES_TOGGLE =
        Selector(
            strategy = SelectorStrategy.UIAUTOMATOR_WITH_TEXT_CONTAINS,
            value = getStringResource(R.string.preferences_addresses_save_and_autofill_addresses_2),
            description = "Save and fill addresses option",
            groups = setOf(Group.AUTOFILL_SETTINGS),
            readiness = PageReadinessProfiles.READY_CONTENT,
        )

    // "Add address" entry point on the Autofill settings screen (and inside Manage addresses).
    val ADD_ADDRESS_BUTTON =
        Selector(
            strategy = SelectorStrategy.UIAUTOMATOR_WITH_TEXT_CONTAINS,
            value = getStringResource(R.string.preferences_addresses_add_address),
            description = "The 'Add address' button",
            groups = setOf(Group.ADD_ADDRESS),
        )

    // Post-save anchor: after saving an address the screen returns to the Autofill list, which shows
    // the "Manage addresses" button. Used to confirm the save completed.
    val MANAGE_ADDRESSES_BUTTON =
        Selector(
            strategy = SelectorStrategy.UIAUTOMATOR_WITH_TEXT_CONTAINS,
            value = getStringResource(R.string.preferences_addresses_manage_addresses),
            description = "The 'Manage addresses' button",
            groups = setOf(Group.MANAGE_ADDRESSES),
        )

    // --- Add/Edit address form fields (Compose; keyed off EditAddressTestTag in main source) ---

    val ADDRESS_NAME_FIELD =
        Selector(
            strategy = SelectorStrategy.COMPOSE_BY_TAG,
            value = EditAddressTestTag.NAME_FIELD,
            description = "Address form: Name field",
            groups = setOf(Group.ADDRESS_FORM),
        )

    val ADDRESS_STREET_FIELD =
        Selector(
            strategy = SelectorStrategy.COMPOSE_BY_TAG,
            value = EditAddressTestTag.STREET_ADDRESS_FIELD,
            description = "Address form: Street address field",
            groups = setOf(Group.ADDRESS_FORM),
        )

    val ADDRESS_CITY_FIELD =
        Selector(
            strategy = SelectorStrategy.COMPOSE_BY_TAG,
            value = EditAddressTestTag.ADDRESS_LEVEL2_FIELD,
            description = "Address form: City field",
            groups = setOf(Group.ADDRESS_FORM),
        )

    val ADDRESS_STATE_DROPDOWN =
        Selector(
            strategy = SelectorStrategy.COMPOSE_BY_TAG,
            value = EditAddressTestTag.ADDRESS_LEVEL1_FIELD,
            description = "Address form: State/sub-region dropdown",
            groups = setOf(Group.ADDRESS_FORM),
        )

    val ADDRESS_ZIP_FIELD =
        Selector(
            strategy = SelectorStrategy.COMPOSE_BY_TAG,
            value = EditAddressTestTag.POSTAL_CODE_FIELD,
            description = "Address form: Zip code field",
            groups = setOf(Group.ADDRESS_FORM),
        )

    val ADDRESS_COUNTRY_DROPDOWN =
        Selector(
            strategy = SelectorStrategy.COMPOSE_BY_TAG,
            value = EditAddressTestTag.COUNTRY_FIELD,
            description = "Address form: Country dropdown",
            groups = setOf(Group.ADDRESS_FORM),
        )

    val ADDRESS_PHONE_FIELD =
        Selector(
            strategy = SelectorStrategy.COMPOSE_BY_TAG,
            value = EditAddressTestTag.TEL_FIELD,
            description = "Address form: Phone field",
            groups = setOf(Group.ADDRESS_FORM),
        )

    val ADDRESS_EMAIL_FIELD =
        Selector(
            strategy = SelectorStrategy.COMPOSE_BY_TAG,
            value = EditAddressTestTag.EMAIL_FIELD,
            description = "Address form: Email field",
            groups = setOf(Group.ADDRESS_FORM),
        )

    val ADDRESS_SAVE_BUTTON =
        Selector(
            strategy = SelectorStrategy.COMPOSE_BY_TAG,
            value = EditAddressTestTag.SAVE_BUTTON,
            description = "Address form: Save button",
            groups = setOf(Group.ADDRESS_FORM),
        )

    val DELETE_ADDRESS_TOOLBAR_BUTTON =
        Selector(
            strategy = SelectorStrategy.COMPOSE_BY_TAG,
            value = EditAddressTestTag.TOPBAR_DELETE_BUTTON,
            description = "Edit address screen: toolbar delete button",
            groups = setOf(Group.DELETE_ADDRESS),
        )

    val CANCEL_DELETE_ADDRESS_BUTTON =
        Selector(
            strategy = SelectorStrategy.COMPOSE_BY_TAG,
            value = EditAddressTestTag.DIALOG_CANCEL_BUTTON,
            description = "Delete address dialog: Cancel button",
            groups = setOf(Group.DELETE_ADDRESS),
        )

    val CONFIRM_DELETE_ADDRESS_BUTTON =
        Selector(
            strategy = SelectorStrategy.COMPOSE_BY_TAG,
            value = EditAddressTestTag.DIALOG_DELETE_BUTTON,
            description = "Delete address dialog: Delete (confirm) button",
            groups = setOf(Group.DELETE_ADDRESS),
        )

    @Suppress("FunctionName")
    fun SAVED_ADDRESS(name: String = "") =
        Selector(
            strategy = SelectorStrategy.COMPOSE_BY_TEXT,
            value = name,
            description = "Saved address row: $name",
            groups = setOf(Group.MANAGE_ADDRESSES),
        )

    @Suppress("FunctionName")
    fun SAVED_ADDRESS_DETAIL(savedAddressDetails: String = "") =
        Selector(
            strategy = SelectorStrategy.COMPOSE_BY_TEXT,
            value = savedAddressDetails,
            description = "Saved address detail: $savedAddressDetails",
            groups = setOf(Group.MANAGE_ADDRESSES),
        )

    // --- Credit cards ---

    // Credit-cards section of the Autofill screen (present before any card is saved).
    val CREDIT_CARDS_SECTION_TITLE =
        Selector(
            strategy = SelectorStrategy.UIAUTOMATOR_WITH_TEXT_CONTAINS,
            value = getStringResource(R.string.preferences_credit_cards_2),
            description = "The 'Credit cards' section title",
        )

    // Espresso-backed so its cousin Switch can be state-checked via mozVerifyOptionSwitchIsChecked.
    val SAVE_AND_AUTOFILL_CREDIT_CARDS_OPTION =
        Selector(
            strategy = SelectorStrategy.ESPRESSO_BY_TEXT,
            value = getStringResource(R.string.preferences_credit_cards_save_and_autofill_cards_2),
            description = "The 'Save and autofill cards' option",
        )

    val SAVE_AND_AUTOFILL_CREDIT_CARDS_SUMMARY =
        Selector(
            strategy = SelectorStrategy.UIAUTOMATOR_WITH_TEXT_CONTAINS,
            value = getStringResource(R.string.preferences_credit_cards_save_and_autofill_cards_summary_2),
            description = "The 'Save and autofill cards' summary",
        )

    val SYNC_CREDIT_CARDS_ACROSS_DEVICES_BUTTON =
        Selector(
            strategy = SelectorStrategy.UIAUTOMATOR_WITH_TEXT_CONTAINS,
            value = getStringResource(R.string.preferences_credit_cards_sync_cards_across_devices),
            description = "The 'Sync cards across devices' button",
        )

    val SAVED_CREDIT_CARDS_TOOLBAR_TITLE =
        Selector(
            strategy = SelectorStrategy.UIAUTOMATOR_WITH_TEXT_CONTAINS,
            value = getStringResource(R.string.credit_cards_saved_cards),
            description = "The 'Saved cards' toolbar title",
        )

    // A detail (masked last digits, expiry) on a saved-card row, matched by substring like the legacy
    // itemContainingText.
    @Suppress("FunctionName")
    fun SAVED_CREDIT_CARD_DETAIL(text: String = "") =
        Selector(
            strategy = SelectorStrategy.UIAUTOMATOR_WITH_TEXT_CONTAINS,
            value = text,
            description = "Saved card detail containing '$text'",
        )

    val ADD_CREDIT_CARD_BUTTON =
        Selector(
            strategy = SelectorStrategy.UIAUTOMATOR_WITH_TEXT_CONTAINS,
            value = getStringResource(R.string.preferences_credit_cards_add_credit_card_2),
            description = "The 'Add card' button",
            groups = setOf(Group.ADD_CREDIT_CARD),
        )

    // Post-save anchor, mirroring MANAGE_ADDRESSES_BUTTON: saving returns to the Autofill list, which
    // only offers "Manage cards" once a card exists.
    val MANAGE_SAVED_CREDIT_CARDS_BUTTON =
        Selector(
            strategy = SelectorStrategy.UIAUTOMATOR_WITH_TEXT_CONTAINS,
            value = getStringResource(R.string.preferences_credit_cards_manage_saved_cards_2),
            description = "The 'Manage cards' button",
            groups = setOf(Group.MANAGE_CREDIT_CARDS),
        )

    // "Later" on the system prompt offering to secure saved cards behind a device lock. It is a
    // platform AlertDialog, so the handle is the framework's negative-button id used verbatim — an
    // app-scoped strategy would look it up in the app's R class and fail to resolve it at all.
    val SECURED_CREDIT_CARDS_LATER_BUTTON =
        Selector(
            strategy = SelectorStrategy.UIAUTOMATOR_WITH_RAW_RES_ID,
            value = "android:id/button2",
            description = "The 'Later' button on the secure-your-cards prompt",
        )

    // A saved card row in Manage cards, matched on the card-type logo — the number itself is masked.
    val SAVED_CREDIT_CARD =
        Selector(
            strategy = SelectorStrategy.UIAUTOMATOR_WITH_RES_ID,
            value = "credit_card_logo",
            description = "A saved credit card row",
        )

    val EDIT_CREDIT_CARD_TOOLBAR_TITLE =
        Selector(
            strategy = SelectorStrategy.COMPOSE_BY_TEXT,
            value = getStringResource(R.string.credit_cards_edit_card),
            description = "The 'Edit card' toolbar title",
            groups = setOf(Group.EDIT_CREDIT_CARD),
        )

    // --- Add/Edit card form (Compose; keyed off CreditCardEditorTestTags in main source) ---

    val CREDIT_CARD_NUMBER_FIELD =
        Selector(
            strategy = SelectorStrategy.COMPOSE_BY_TAG,
            value = CreditCardEditorTestTags.CARD_NUMBER_FIELD,
            description = "Card form: Card number field",
            groups = setOf(Group.CREDIT_CARD_FORM),
        )

    val CREDIT_CARD_NAME_FIELD =
        Selector(
            strategy = SelectorStrategy.COMPOSE_BY_TAG,
            value = CreditCardEditorTestTags.NAME_ON_CARD_FIELD,
            description = "Card form: Name on card field",
            groups = setOf(Group.CREDIT_CARD_FORM),
        )

    val CREDIT_CARD_SAVE_BUTTON =
        Selector(
            strategy = SelectorStrategy.COMPOSE_BY_TAG,
            value = CreditCardEditorTestTags.SAVE_BUTTON,
            description = "Card form: Save button",
            groups = setOf(Group.CREDIT_CARD_FORM),
        )

    // Delete lives in the editor's body, distinct from the toolbar delete action.
    val DELETE_CREDIT_CARD_MENU_BUTTON =
        Selector(
            strategy = SelectorStrategy.COMPOSE_BY_TAG,
            value = CreditCardEditorTestTags.DELETE_BUTTON,
            description = "Card form: Delete card button",
            groups = setOf(Group.EDIT_CREDIT_CARD),
        )

    // The delete action in the editor's top app bar, distinct from the body DELETE_CREDIT_CARD_MENU_BUTTON.
    val DELETE_CREDIT_CARD_TOOLBAR_BUTTON =
        Selector(
            strategy = SelectorStrategy.COMPOSE_BY_TAG,
            value = CreditCardEditorTestTags.TOPBAR_DELETE_BUTTON,
            description = "Edit card screen: toolbar delete button",
            groups = setOf(Group.EDIT_CREDIT_CARD),
        )

    val DELETE_CREDIT_CARD_DIALOG_CANCEL_BUTTON =
        Selector(
            strategy = SelectorStrategy.COMPOSE_BY_TAG,
            value = CreditCardEditorTestTags.DELETE_DIALOG_CANCEL_BUTTON,
            description = "Delete card dialog: Cancel button",
        )

    val DELETE_CREDIT_CARD_DIALOG_DELETE_BUTTON =
        Selector(
            strategy = SelectorStrategy.COMPOSE_BY_TAG,
            value = CreditCardEditorTestTags.DELETE_DIALOG_DELETE_BUTTON,
            description = "Delete card dialog: Delete button",
        )
}
