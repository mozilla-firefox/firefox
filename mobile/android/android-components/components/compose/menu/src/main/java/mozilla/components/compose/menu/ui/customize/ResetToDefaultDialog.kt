/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.compose.menu.ui.customize

import androidx.compose.material3.AlertDialog
import androidx.compose.material3.Surface
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.tooling.preview.PreviewLightDark
import mozilla.components.compose.base.button.TextButton
import mozilla.components.compose.base.theme.AcornTheme
import mozilla.components.compose.menu.R

/**
 * Dialog asking the user to confirm resetting the menu customization to default.
 *
 * @param onConfirm Called when the user confirms resetting the menu.
 * @param onDismiss Called when the user closes the dialog without resetting the menu.
 */
@Composable
internal fun ResetToDefaultDialog(
    onConfirm: () -> Unit,
    onDismiss: () -> Unit,
) {
    AlertDialog(
        onDismissRequest = onDismiss,
        title = {
            Text(
                text = stringResource(R.string.mozac_menu_customize_reset_dialog_title),
                style = AcornTheme.typography.headline5,
            )
        },
        text = {
            Text(
                text = stringResource(R.string.mozac_menu_customize_reset_dialog_message),
                style = AcornTheme.typography.body2,
            )
        },
        confirmButton = {
            TextButton(
                text = stringResource(R.string.mozac_menu_customize_reset_dialog_reset),
                onClick = onConfirm,
            )
        },
        dismissButton = {
            TextButton(
                text = stringResource(R.string.mozac_menu_customize_reset_dialog_cancel),
                onClick = onDismiss,
            )
        },
    )
}

@PreviewLightDark
@Composable
private fun ResetToDefaultDialogPreview() {
    AcornTheme {
        Surface {
            ResetToDefaultDialog(
                onConfirm = {},
                onDismiss = {},
            )
        }
    }
}
