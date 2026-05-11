/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.settings.trustpanel.ui

import androidx.compose.material3.AlertDialog
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.tooling.preview.PreviewLightDark
import mozilla.components.compose.base.button.TextButton
import mozilla.components.compose.base.utils.parseHtml
import org.mozilla.fenix.R
import org.mozilla.fenix.theme.FirefoxTheme

/**
 * Clear Site Data Dialog.
 *
 * @param baseDomain The base domain of the current website.
 * @param onClearSiteDataClick Invoked when the user clicks on the "CLEAR" dialog button.
 * @param onCancelClick Invoked when the user clicks on the "CANCEL" dialog button.
 */
@Composable
fun ClearSiteDataDialog(
    baseDomain: String,
    onClearSiteDataClick: () -> Unit,
    onCancelClick: () -> Unit,
) {
    AlertDialog(
        onDismissRequest = {},
        title = {
            Text(
                text = stringResource(id = R.string.clear_site_data),
                style = FirefoxTheme.typography.headline5,
            )
        },
        text = {
            Text(
                text = parseHtml(stringResource(id = R.string.clear_site_data_dialog_description, baseDomain)),
                style = FirefoxTheme.typography.body2,
            )
        },
        confirmButton = {
            TextButton(
                text = stringResource(id = R.string.clear_site_data_dialog_positive_button_text),
                onClick = { onClearSiteDataClick() },
            )
        },
        dismissButton = {
            TextButton(
                text = stringResource(id = R.string.clear_site_data_dialog_negative_button_text),
                onClick = { onCancelClick() },
            )
        },
    )
}

@Composable
@PreviewLightDark
private fun ClearSiteDataDialogPreview() {
    FirefoxTheme {
        ClearSiteDataDialog(
            baseDomain = "mozilla.org",
            onClearSiteDataClick = {},
            onCancelClick = {},
        )
    }
}
