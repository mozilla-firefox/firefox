/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.tabgroups.flow

import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.material3.AlertDialog
import androidx.compose.material3.Icon
import androidx.compose.material3.Surface
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.testTag
import androidx.compose.ui.res.painterResource
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.tooling.preview.PreviewParameter
import mozilla.components.compose.base.annotation.FlexibleWindowPreview
import mozilla.components.compose.base.button.TextButton
import mozilla.components.compose.base.theme.PreviewThemeProvider
import mozilla.components.compose.base.theme.Theme
import mozilla.components.ui.icons.R as iconsR
import org.mozilla.fenix.R
import org.mozilla.fenix.tabgroups.TabGroupsTestTag
import org.mozilla.fenix.theme.FirefoxTheme

/**
 * This dialog is used to prompt the user to confirm if they want to move the last remaining tab out of a group and
 * delete the associated tab group.
 *
 * @param destinationTabGroupTitle The title of the group the tab is moving to
 * @param sourceTabGroupTitle The title of the group the tab is moving from
 * @param onConfirmDelete Callback invoked when the user confirms moving the tab and deleting the group.
 * @param onCancel Callback invoked when the user cancels the action.
 */
@Composable
fun MoveLastTabAndDeleteTabGroupConfirmationDialog(
    destinationTabGroupTitle: String,
    sourceTabGroupTitle: String,
    onConfirmDelete: () -> Unit,
    onCancel: () -> Unit,
) {
    AlertDialog(
        icon = {
            Icon(
                painter = painterResource(iconsR.drawable.mozac_ic_delete_24),
                contentDescription = null,
            )
        },
        onDismissRequest = onCancel,
        title = {
            Text(
                text =
                    stringResource(
                        R.string.move_last_tab_and_delete_group_confirmation_dialog_title,
                        sourceTabGroupTitle,
                    ),
                style = FirefoxTheme.typography.headline5,
                modifier = Modifier.fillMaxWidth(),
                textAlign = TextAlign.Center,
            )
        },
        text = {
            Text(
                text =
                    stringResource(
                        R.string.move_last_tab_and_delete_group_confirmation_dialog_body,
                        destinationTabGroupTitle,
                        sourceTabGroupTitle,
                    ),
                style = FirefoxTheme.typography.body2,
            )
        },
        confirmButton = {
            TextButton(
                text = stringResource(R.string.move_last_tab_and_delete_group_confirmation_dialog_confirm),
                onClick = onConfirmDelete,
                modifier = Modifier.testTag(tag = TabGroupsTestTag.MOVE_LAST_TAB_AND_DELETE_DIALOG_CONFIRM_BUTTON),
            )
        },
        dismissButton = {
            TextButton(
                text = stringResource(R.string.move_last_tab_and_delete_group_confirmation_dialog_cancel),
                onClick = onCancel,
                modifier = Modifier.testTag(tag = TabGroupsTestTag.MOVE_LAST_TAB_AND_DELETE_DIALOG_CANCEL_BUTTON),
            )
        },
    )
}

@FlexibleWindowPreview
@Composable
private fun MoveTabAndDeleteGroupConfirmationDialogPreview(
    @PreviewParameter(PreviewThemeProvider::class) theme: Theme
) {
    FirefoxTheme(theme) {
        Surface {
            MoveLastTabAndDeleteTabGroupConfirmationDialog(
                sourceTabGroupTitle = "Old Tab Group",
                destinationTabGroupTitle = "New Tab Group",
                onConfirmDelete = {},
                onCancel = {},
            )
        }
    }
}
