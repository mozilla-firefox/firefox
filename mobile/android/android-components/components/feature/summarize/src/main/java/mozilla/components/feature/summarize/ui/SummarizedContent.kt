/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.feature.summarize.ui

import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.text.selection.SelectionContainer
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.MaterialTheme
import androidx.compose.runtime.Composable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.res.painterResource
import androidx.compose.ui.res.stringResource
import mozilla.components.feature.summarize.R
import mozilla.components.ui.richtext.RichText
import mozilla.components.ui.icons.R as iconsR

@Composable
internal fun SummarizedContent(
    text: String,
    modifier: Modifier = Modifier,
    onSettingsClicked: () -> Unit,
) {
    Column(modifier = modifier.fillMaxWidth()) {
        Box(modifier = Modifier.fillMaxWidth()) {
            IconButton(
                onClick = onSettingsClicked,
                modifier = Modifier.align(Alignment.TopEnd),
            ) {
                Icon(
                    painter = painterResource(id = iconsR.drawable.mozac_ic_settings_24),
                    contentDescription = stringResource(
                        id = R.string.mozac_summarize_settings_button_content_description,
                    ),
                    tint = MaterialTheme.colorScheme.onSurfaceVariant,
                )
            }
        }

        SelectionContainer(modifier = Modifier.fillMaxWidth()) {
            RichText(text = text)
        }
    }
}
