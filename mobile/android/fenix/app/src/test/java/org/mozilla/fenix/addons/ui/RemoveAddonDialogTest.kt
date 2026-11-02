/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.addons.ui

import androidx.compose.ui.test.assertIsDisplayed
import androidx.compose.ui.test.junit4.v2.createComposeRule
import androidx.compose.ui.test.onNodeWithTag
import androidx.compose.ui.test.onNodeWithText
import androidx.compose.ui.test.performClick
import androidx.test.ext.junit.runners.AndroidJUnit4
import kotlin.test.assertTrue
import mozilla.components.support.test.robolectric.testContext
import org.junit.Rule
import org.junit.Test
import org.junit.runner.RunWith
import org.mozilla.fenix.R
import org.mozilla.fenix.addons.AddonsTestTag

@RunWith(AndroidJUnit4::class)
class RemoveAddonDialogTest {

    @get:Rule val composeTestRule = createComposeRule()

    @Test
    fun `GIVEN the remove add-on dialog is displayed WHEN the remove button is clicked THEN the confirm callback is invoked`() {
        var onConfirmInvoked = false

        composeTestRule.setContent {
            RemoveAddonDialog(
                addonName = ADDON_NAME,
                onConfirm = { onConfirmInvoked = true },
                onDismiss = {},
            )
        }

        composeTestRule.onNodeWithTag(AddonsTestTag.REMOVE_ADDON_DIALOG_CONFIRM_BUTTON).performClick()

        assertTrue(onConfirmInvoked)
    }

    @Test
    fun `GIVEN the remove add-on dialog is displayed WHEN the cancel button is clicked THEN the cancel callback is invoked`() {
        var onCancelInvoked = false

        composeTestRule.setContent {
            RemoveAddonDialog(
                addonName = ADDON_NAME,
                onConfirm = {},
                onDismiss = { onCancelInvoked = true },
            )
        }

        composeTestRule.onNodeWithTag(AddonsTestTag.REMOVE_ADDON_DIALOG_CANCEL_BUTTON).performClick()

        assertTrue(onCancelInvoked)
    }

    @Test
    fun `GIVEN the remove add-on dialog is displayed THEN the title with the add-on name and the body are shown`() {
        composeTestRule.setContent {
            RemoveAddonDialog(
                addonName = ADDON_NAME,
                onConfirm = {},
                onDismiss = {},
            )
        }

        composeTestRule
            .onNodeWithText(testContext.getString(R.string.addon_remove_dialog_title, ADDON_NAME))
            .assertIsDisplayed()

        composeTestRule.onNodeWithText(testContext.getString(R.string.addon_remove_dialog_body)).assertIsDisplayed()
    }

    private companion object {
        const val ADDON_NAME = "Example Add-on"
    }
}
