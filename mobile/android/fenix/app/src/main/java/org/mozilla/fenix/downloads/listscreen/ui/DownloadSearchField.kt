/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.downloads.listscreen.ui

import androidx.compose.foundation.background
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.text.BasicTextField
import androidx.compose.foundation.text.input.TextFieldLineLimits
import androidx.compose.foundation.text.input.delete
import androidx.compose.foundation.text.input.rememberTextFieldState
import androidx.compose.material3.Icon
import androidx.compose.material3.LocalContentColor
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Surface
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.remember
import androidx.compose.runtime.snapshotFlow
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.focus.FocusRequester
import androidx.compose.ui.focus.focusRequester
import androidx.compose.ui.graphics.SolidColor
import androidx.compose.ui.res.painterResource
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.semantics.testTag
import androidx.compose.ui.semantics.testTagsAsResourceId
import androidx.compose.ui.tooling.preview.Preview
import androidx.compose.ui.tooling.preview.PreviewParameter
import androidx.compose.ui.unit.dp
import mozilla.components.compose.base.button.IconButton
import mozilla.components.compose.base.theme.PreviewThemeProvider
import mozilla.components.compose.base.theme.Theme
import mozilla.components.ui.icons.R as iconsR
import org.mozilla.fenix.R
import org.mozilla.fenix.downloads.listscreen.DownloadsListTestTag
import org.mozilla.fenix.theme.FirefoxTheme

/**
 * A search text field for the download list screen.
 *
 * @param initialText The initial text to display in the text field.
 * @param onValueChange Invoked when the text field value changes.
 * @param onSearchDismissRequest Invoked when the search is dismissed.
 * @param modifier The [Modifier] to be applied to this composable.
 */
@Composable
fun DownloadSearchField(
    initialText: String,
    onValueChange: (String) -> Unit,
    onSearchDismissRequest: () -> Unit,
    modifier: Modifier = Modifier,
) {
    val focusRequester = remember { FocusRequester() }
    val state = rememberTextFieldState(initialText)

    Surface {
        Row(
            verticalAlignment = Alignment.CenterVertically,
            modifier =
                Modifier.padding(horizontal = 8.dp, vertical = 8.dp)
                    .background(MaterialTheme.colorScheme.surfaceContainerHigh, MaterialTheme.shapes.small)
                    .fillMaxWidth(),
        ) {
            IconButton(
                onClick = onSearchDismissRequest,
                contentDescription = stringResource(R.string.download_close_search_description),
            ) {
                Icon(
                    painter = painterResource(iconsR.drawable.mozac_ic_back_24),
                    contentDescription = null,
                    modifier = Modifier.size(20.dp),
                )
            }

            Box(
                contentAlignment = Alignment.CenterStart,
                modifier = Modifier.weight(1f, fill = true),
            ) {
                if (state.text.isEmpty()) {
                    PlaceholderText()
                }

                BasicTextField(
                    state = state,
                    textStyle = FirefoxTheme.typography.body2.copy(color = LocalContentColor.current),
                    lineLimits = TextFieldLineLimits.SingleLine,
                    cursorBrush = SolidColor(LocalContentColor.current),
                    modifier =
                        modifier.fillMaxWidth().focusRequester(focusRequester).semantics {
                            testTagsAsResourceId = true
                            testTag = DownloadsListTestTag.SEARCH_FIELD
                        },
                )
            }

            if (state.text.isNotEmpty()) {
                IconButton(
                    onClick = {
                        state.edit {
                            delete(0, state.text.length)
                        }
                    },
                    contentDescription = stringResource(R.string.download_clear_search_description),
                ) {
                    Icon(
                        painter = painterResource(iconsR.drawable.mozac_ic_cross_circle_fill_20),
                        contentDescription = null,
                    )
                }
            }
        }
    }

    LaunchedEffect(Unit) {
        focusRequester.requestFocus()

        snapshotFlow { state.text }
            .collect { text ->
                onValueChange(text.toString())
            }
    }
}

@Composable
private fun PlaceholderText() {
    Text(
        text = stringResource(R.string.download_search_placeholder),
        color = MaterialTheme.colorScheme.onSurfaceVariant,
        style = FirefoxTheme.typography.body2,
    )
}

@Preview
@Composable
private fun DownloadSearchFieldPreview(@PreviewParameter(PreviewThemeProvider::class) theme: Theme) {
    FirefoxTheme(theme) {
        Surface {
            DownloadSearchField(
                initialText = "",
                onValueChange = {},
                onSearchDismissRequest = {},
                modifier = Modifier.height(56.dp).fillMaxWidth(),
            )
        }
    }
}
