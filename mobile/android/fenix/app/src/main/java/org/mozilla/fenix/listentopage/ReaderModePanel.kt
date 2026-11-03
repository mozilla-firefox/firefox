/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.listentopage

import androidx.compose.material3.Icon
import androidx.compose.material3.Surface
import androidx.compose.runtime.Composable
import androidx.compose.ui.Modifier
import androidx.compose.ui.res.painterResource
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.semantics.CollectionInfo
import androidx.compose.ui.semantics.collectionInfo
import androidx.compose.ui.semantics.contentDescription
import androidx.compose.ui.semantics.isTraversalGroup
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.tooling.preview.PreviewLightDark
import mozilla.components.compose.base.button.IconButton
import mozilla.components.ui.icons.R as iconsR
import org.mozilla.fenix.R
import org.mozilla.fenix.theme.FirefoxTheme

/**
 * The floating panel shown over an article in reader mode.
 *
 * @param modifier The [Modifier] to be applied to this panel.
 * @param isListeningEnabled Whether to show the button that reads the article out loud.
 * @param onListenClicked Invoked when the user asks to listen to the article.
 * @param onCustomizeReaderViewClicked Invoked when the user asks for the reader view appearance controls.
 */
@Composable
fun ReaderModePanel(
    modifier: Modifier = Modifier,
    isListeningEnabled: Boolean,
    onListenClicked: () -> Unit,
    onCustomizeReaderViewClicked: () -> Unit,
) {
    val panelDescription = stringResource(id = R.string.reader_mode_panel_content_description_2)
    val buttonCount = if (isListeningEnabled) 2 else 1

    FloatingPanel(
        modifier =
            modifier.semantics {
                isTraversalGroup = true
                contentDescription = panelDescription
                collectionInfo = CollectionInfo(rowCount = 1, columnCount = buttonCount)
            }
    ) {
        if (isListeningEnabled) {
            IconButton(
                onClick = onListenClicked,
                contentDescription = stringResource(id = R.string.reader_mode_panel_listen_content_description),
            ) {
                Icon(
                    painter = painterResource(iconsR.drawable.mozac_ic_listen_to_page_24),
                    contentDescription = null,
                )
            }
        }

        IconButton(
            onClick = onCustomizeReaderViewClicked,
            contentDescription = stringResource(id = R.string.browser_menu_customize_reader_view_2),
        ) {
            Icon(
                painter = painterResource(iconsR.drawable.mozac_ic_text_size_24),
                contentDescription = null,
            )
        }
    }
}

@PreviewLightDark
@Composable
private fun ReaderModePanelPreview() {
    FirefoxTheme {
        Surface {
            ReaderModePanel(isListeningEnabled = true, onListenClicked = {}, onCustomizeReaderViewClicked = {})
        }
    }
}
