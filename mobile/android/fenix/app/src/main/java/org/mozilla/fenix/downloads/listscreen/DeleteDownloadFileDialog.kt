/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.downloads.listscreen

import androidx.compose.foundation.background
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material.AlertDialog
import androidx.compose.material.Text
import androidx.compose.runtime.Composable
import androidx.compose.ui.Modifier
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.unit.dp
import mozilla.components.compose.base.annotation.FlexibleWindowLightDarkPreview
import mozilla.components.compose.base.button.TextButton
import org.mozilla.fenix.R
import org.mozilla.fenix.theme.FirefoxTheme

/**
* This dialog is used to prompt the user to confirm if they want to delete
* selected downloads. It provides options to confirm or cancel the deletion.
*
* @param onConfirmDelete Callback invoked when the user confirms the deletion.
* @param onCancel Callback invoked when the user cancels the deletion.
*/
@Composable
fun DeleteDownloadFileDialog(
    onConfirmDelete: () -> Unit,
    onCancel: () -> Unit,
) {
    AlertDialog(
        onDismissRequest = {},
        modifier = Modifier.background(
            color = FirefoxTheme.colors.layer2,
            shape = RoundedCornerShape(8.dp),
        ),
        text = {
            Text(
                text = stringResource(
                    R.string.download_delete_multi_select_dialog_confirmation,
                ),
                color = FirefoxTheme.colors.textPrimary,
                style = FirefoxTheme.typography.body2,
            )
        },
        confirmButton = {
            TextButton(
                text = stringResource(id = R.string.download_delete_multi_select_dialog_confirm),
                onClick = { onConfirmDelete() },
            )
        },
        dismissButton = {
            TextButton(
                text = stringResource(id = R.string.download_delete_multi_select_dialog_cancel),
                onClick = { onCancel() },
            )
        },
        backgroundColor = FirefoxTheme.colors.layer2,
    )
}

@Composable
@FlexibleWindowLightDarkPreview
private fun DeleteDownloadFileDialogPreview() {
    FirefoxTheme {
        DeleteDownloadFileDialog(
            onConfirmDelete = {},
            onCancel = {},
        )
    }
}
