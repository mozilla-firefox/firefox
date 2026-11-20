/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.tabgroups.flow

import androidx.compose.ui.test.junit4.v2.createComposeRule
import androidx.compose.ui.test.onNodeWithTag
import androidx.compose.ui.test.onNodeWithText
import androidx.compose.ui.test.performClick
import androidx.test.ext.junit.runners.AndroidJUnit4
import kotlin.test.assertFalse
import kotlin.test.assertTrue
import mozilla.components.support.test.robolectric.testContext
import org.junit.Rule
import org.junit.Test
import org.junit.runner.RunWith
import org.mozilla.fenix.R
import org.mozilla.fenix.tabgroups.TabGroupsTestTag

@RunWith(AndroidJUnit4::class)
class MoveLastTabAndDeleteTabGroupConfirmationDialogTest {

    @get:Rule val composeTestRule = createComposeRule()

    @Test
    fun `WHEN the dialog is shown THEN the source and destination group titles are displayed`() {
        setContent()

        composeTestRule
            .onNodeWithText(
                testContext.getString(R.string.move_last_tab_and_delete_group_confirmation_dialog_title, SOURCE_TITLE)
            )
            .assertExists()
        composeTestRule
            .onNodeWithText(
                testContext.getString(
                    R.string.move_last_tab_and_delete_group_confirmation_dialog_body,
                    DESTINATION_TITLE,
                    SOURCE_TITLE,
                )
            )
            .assertExists()
    }

    @Test
    fun `WHEN confirm is clicked THEN onConfirmDelete is invoked`() {
        var onConfirmInvoked = false
        var onCancelInvoked = false
        setContent(onConfirmDelete = { onConfirmInvoked = true }, onCancel = { onCancelInvoked = true })

        composeTestRule.onNodeWithTag(TabGroupsTestTag.MOVE_LAST_TAB_AND_DELETE_DIALOG_CONFIRM_BUTTON).performClick()

        assertTrue(onConfirmInvoked)
        assertFalse(onCancelInvoked)
    }

    @Test
    fun `WHEN cancel is clicked THEN onCancel is invoked`() {
        var onConfirmInvoked = false
        var onCancelInvoked = false
        setContent(onConfirmDelete = { onConfirmInvoked = true }, onCancel = { onCancelInvoked = true })

        composeTestRule.onNodeWithTag(TabGroupsTestTag.MOVE_LAST_TAB_AND_DELETE_DIALOG_CANCEL_BUTTON).performClick()

        assertTrue(onCancelInvoked)
        assertFalse(onConfirmInvoked)
    }

    private fun setContent(onConfirmDelete: () -> Unit = {}, onCancel: () -> Unit = {}) {
        composeTestRule.setContent {
            MoveLastTabAndDeleteTabGroupConfirmationDialog(
                destinationTabGroupTitle = DESTINATION_TITLE,
                sourceTabGroupTitle = SOURCE_TITLE,
                onConfirmDelete = onConfirmDelete,
                onCancel = onCancel,
            )
        }
    }

    private companion object {
        const val SOURCE_TITLE = "Errands"
        const val DESTINATION_TITLE = "Work"
    }
}
