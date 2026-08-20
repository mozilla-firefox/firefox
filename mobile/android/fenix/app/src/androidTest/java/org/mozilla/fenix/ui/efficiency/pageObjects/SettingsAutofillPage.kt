/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.ui.efficiency.pageObjects

import android.os.SystemClock
import android.util.Log
import androidx.compose.ui.test.click
import androidx.compose.ui.test.hasTestTag
import androidx.compose.ui.test.hasText
import androidx.compose.ui.test.junit4.AndroidComposeTestRule
import androidx.compose.ui.test.onNodeWithTag
import androidx.compose.ui.test.performClick
import androidx.compose.ui.test.performScrollTo
import androidx.compose.ui.test.performTouchInput
import androidx.test.platform.app.InstrumentationRegistry
import org.mozilla.fenix.helpers.HomeActivityIntentTestRule
import org.mozilla.fenix.helpers.TestAssetHelper.waitingTimeLong
import org.mozilla.fenix.helpers.TestHelper.waitForAppWindowToBeUpdated
import org.mozilla.fenix.settings.address.ui.edit.EditAddressTestTag
import org.mozilla.fenix.settings.creditcards.ui.CreditCardEditorTestTags
import org.mozilla.fenix.ui.efficiency.data.AddressDetails
import org.mozilla.fenix.ui.efficiency.data.CreditCardDetails
import org.mozilla.fenix.ui.efficiency.helpers.BasePage
import org.mozilla.fenix.ui.efficiency.helpers.Selector
import org.mozilla.fenix.ui.efficiency.navigation.NavigationRegistry
import org.mozilla.fenix.ui.efficiency.navigation.NavigationStep
import org.mozilla.fenix.ui.efficiency.selectors.SettingsAutofillSelectors
import org.mozilla.fenix.ui.efficiency.selectors.SettingsSelectors

class SettingsAutofillPage(composeRule: AndroidComposeTestRule<HomeActivityIntentTestRule, *>) : BasePage(composeRule) {
    override val pageName = "SettingsAutofillPage"

    init {
        NavigationRegistry.register(
            from = pageName,
            to = "SettingsPage",
            steps = listOf(NavigationStep.Click(SettingsSelectors.GO_BACK_BUTTON)),
        )
    }

    override fun mozGetSelectorsByGroup(group: String): List<Selector> {
        return SettingsAutofillSelectors.all.filter { it.groups.contains(group) }
    }

    // Narrow the return type to this page so callers can fluently chain page-specific helpers
    // (e.g. fillAndSaveAddress) directly off navigateToPage().
    override fun navigateToPage(url: String, forceNavigation: Boolean): SettingsAutofillPage {
        super.navigateToPage(url = url, forceNavigation = forceNavigation)
        return this
    }

    /**
     * Open the "Add address" form, fill every field, and save — leaving the screen back on the Autofill list (asserted
     * via the "Manage addresses" button).
     *
     * Screen-specific flow (dropdown popups + soft-keyboard timing + scroll-to-save) is encapsulated here rather than
     * expressed as raw test steps, mirroring the legacy SettingsSubMenuAutofillRobot .fillAndSaveAddress. Text fields
     * go through mozEnterText; the Country/State dropdowns and the Save button need direct Compose handling
     * (touch-input toggle, last-matching-option, popup drain).
     */
    fun fillAndSaveAddress(address: AddressDetails): SettingsAutofillPage {
        // navigateToPage() returns as soon as the "Autofill" toolbar title renders, but the
        // preference list ("Add address" row) can lag a beat — poll for it before clicking so the
        // first attempt is deterministic (mozClick itself does not retry on a missing element).
        mozVerify(SettingsAutofillSelectors.ADD_ADDRESS_BUTTON)
        mozClick(SettingsAutofillSelectors.ADD_ADDRESS_BUTTON)
        composeRule.waitUntil(waitingTimeLong) {
            composeRule.onAllNodesWithTagCount(EditAddressTestTag.NAME_FIELD) > 0
        }

        mozEnterText(address.name, SettingsAutofillSelectors.ADDRESS_NAME_FIELD)
        mozEnterText(address.streetAddress, SettingsAutofillSelectors.ADDRESS_STREET_FIELD)
        mozEnterText(address.city, SettingsAutofillSelectors.ADDRESS_CITY_FIELD)

        // Country must be chosen before State, since the sub-region options depend on the country.
        selectDropdownOption(EditAddressTestTag.COUNTRY_FIELD, address.country)
        selectDropdownOption(EditAddressTestTag.ADDRESS_LEVEL1_FIELD, address.state)
        composeRule.waitForIdle()

        mozEnterText(address.zipCode, SettingsAutofillSelectors.ADDRESS_ZIP_FIELD)
        waitForKeyboardDismiss()
        mozEnterText(address.phoneNumber, SettingsAutofillSelectors.ADDRESS_PHONE_FIELD)
        waitForKeyboardDismiss()
        mozEnterText(address.emailAddress, SettingsAutofillSelectors.ADDRESS_EMAIL_FIELD)
        waitForKeyboardDismiss()

        val saveButton = composeRule.onNodeWithTag(EditAddressTestTag.SAVE_BUTTON)
        runCatching { saveButton.performScrollTo() }
        saveButton.performClick()

        mozVerify(SettingsAutofillSelectors.MANAGE_ADDRESSES_BUTTON)
        return this
    }

    /**
     * Fill the Add card form and save, leaving the Autofill list with one saved card (asserted via the "Manage cards"
     * button), mirroring the legacy SettingsSubMenuAutofillRobot.fillAndSaveCreditCard.
     *
     * The expiry month/year are dropdowns rather than text fields, so they reuse [selectDropdownOption] — the same
     * popup-drain handling the address Country/State fields need.
     */
    fun fillAndSaveCreditCard(card: CreditCardDetails): SettingsAutofillPage {
        mozVerify(SettingsAutofillSelectors.ADD_CREDIT_CARD_BUTTON)
        mozClick(SettingsAutofillSelectors.ADD_CREDIT_CARD_BUTTON)
        composeRule.waitUntil(waitingTimeLong) {
            composeRule.onAllNodesWithTagCount(CreditCardEditorTestTags.CARD_NUMBER_FIELD) > 0
        }

        mozEnterText(card.number, SettingsAutofillSelectors.CREDIT_CARD_NUMBER_FIELD)
        mozEnterText(card.nameOnCard, SettingsAutofillSelectors.CREDIT_CARD_NAME_FIELD)

        selectDropdownOption(CreditCardEditorTestTags.EXPIRATION_MONTH_FIELD, card.expiryMonth, substring = true)
        selectDropdownOption(CreditCardEditorTestTags.EXPIRATION_YEAR_FIELD, card.expiryYear, substring = true)
        waitForKeyboardDismiss()

        val saveButton = composeRule.onNodeWithTag(CreditCardEditorTestTags.SAVE_BUTTON)
        runCatching { saveButton.performScrollTo() }
        saveButton.performClick()

        mozVerify(SettingsAutofillSelectors.MANAGE_SAVED_CREDIT_CARDS_BUTTON)
        return this
    }

    // --- Screen-specific Compose helpers (ported from SettingsSubMenuAutofillRobot) ---

    private fun AndroidComposeTestRule<HomeActivityIntentTestRule, *>.onAllNodesWithTagCount(tag: String): Int =
        onAllNodes(hasTestTag(tag)).fetchSemanticsNodes().size

    /**
     * @param substring match the option by substring, case-insensitively. The card expiry dropdowns render their
     *   options with surrounding text (e.g. the month number alongside the name), so an exact match finds nothing
     *   there; the address dropdowns render the value on its own and match exactly.
     */
    private fun selectDropdownOption(dropdownTag: String, optionText: String, substring: Boolean = false) {
        waitForKeyboardDismiss()
        runCatching { waitForPopupToDismiss() }
        composeRule.waitForIdle()

        for (attempt in 1..3) {
            if (attempt > 1) {
                // A failed attempt can leave the popup open; clicking the trigger again would toggle
                // it shut instead of re-opening it. Best-effort dismiss before retrying.
                runCatching { waitForPopupToDismiss(2_000L) }
            }
            composeRule.onNodeWithTag(dropdownTag).performTouchInput { click() }

            val option = hasText(optionText, substring = substring, ignoreCase = substring)
            try {
                composeRule.waitUntil(5_000L) {
                    composeRule.onAllNodes(option).fetchSemanticsNodes().isNotEmpty()
                }
                val nodeCount = composeRule.onAllNodes(option).fetchSemanticsNodes().size
                runCatching {
                    composeRule.onAllNodes(option)[nodeCount - 1].performScrollTo()
                    composeRule.waitForIdle()
                }
                composeRule.onAllNodes(option)[nodeCount - 1].performClick()
                // Throwable, not Exception: Compose's waitUntil raises ComposeTimeoutException, which extends
                // Throwable directly, so catching Exception here let a timeout escape instead of retrying.
            } catch (e: Throwable) {
                Log.w(
                    "SettingsAutofillPage",
                    "selectDropdownOption: attempt $attempt for '$optionText' failed: ${e.message?.take(120)}",
                )
                continue
            }
            waitForPopupToDismiss()
            return
        }
        throw AssertionError("Dropdown option \"$optionText\" not found after 3 attempts")
    }

    private fun waitForKeyboardDismiss(timeoutMs: Long = 15_000L) {
        dismissSoftKeyboard()
        waitForAppWindowToBeUpdated()
        val startTime = SystemClock.elapsedRealtime()
        while (SystemClock.elapsedRealtime() - startTime < timeoutMs) {
            if (!mozIsKeyboardVisible()) return
            SystemClock.sleep(300)
        }
    }

    private fun waitForPopupToDismiss(timeoutMs: Long = 10_000L) {
        composeRule.waitForIdle()
        val startTime = SystemClock.elapsedRealtime()
        while (SystemClock.elapsedRealtime() - startTime < timeoutMs) {
            // Compose's DropdownMenu renders into a separate popup window titled "Pop-Up".
            val hasPopup =
                InstrumentationRegistry.getInstrumentation().uiAutomation.windows.any {
                    it.title?.contains("Pop-Up", ignoreCase = true) == true
                }
            if (!hasPopup) return
            composeRule.waitForIdle()
            SystemClock.sleep(100)
        }
    }
}
