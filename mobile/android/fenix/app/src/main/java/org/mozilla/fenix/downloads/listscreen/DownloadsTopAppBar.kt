/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.downloads.listscreen

import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.PaddingValues
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.width
import androidx.compose.material.Icon
import androidx.compose.material.IconButton
import androidx.compose.material.Text
import androidx.compose.material.TopAppBar
import androidx.compose.runtime.Composable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.res.painterResource
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.unit.dp
import mozilla.components.compose.base.annotation.FlexibleWindowLightDarkPreview
import org.mozilla.fenix.R
import org.mozilla.fenix.theme.FirefoxTheme

/**
 * A TopAppBar for the Downloads screen. It has slots for a title, an optional navigation icon
 * and actions.
 *
 * @param backgroundColor - The background color for the TopAppBar.
 * @param modifier - The [Modifier] to be applied to this composable.
 * @param navigationIcon - The optional navigation icon displayed at the start of the TopAppBar.
 * @param title - The title to be displayed in the center of the TopAppBar.
 * @param actions - The actions displayed at the end of the TopAppBar.
 */
@Composable
internal fun DownloadsTopAppBar(
    backgroundColor: Color,
    modifier: Modifier = Modifier,
    navigationIcon: @Composable (() -> Unit)? = null,
    title: @Composable () -> Unit,
    actions: @Composable () -> Unit,
) {
    TopAppBar(
        backgroundColor = backgroundColor,
        contentPadding = PaddingValues(start = TopAppBarPaddingStart, end = TopAppBarPaddingEnd),
        content = {
            Row(
                modifier = modifier.fillMaxWidth(),
                verticalAlignment = Alignment.CenterVertically,
            ) {
                if (navigationIcon != null) {
                    Spacer(Modifier.width(4.dp))
                    navigationIcon()
                    Spacer(Modifier.width(4.dp))
                }
                Box(
                    modifier = Modifier.weight(1f),
                    contentAlignment = Alignment.CenterStart,
                ) {
                    title()
                }
                actions()
            }
        },
    )
}

/**
 * These padding values offset the start inset being applied by the material component on the
 * TopAppBar when there's no navigation icon, so the content can be centre aligned. See
 * constants at the bottom in [TopAppBar].
 */
private val TopAppBarPaddingEnd = 8.dp
private val TopAppBarPaddingStart = 0.dp

/**
 * @property title The title text to display in the Toolbar.
 * @property backgroundColor The background color of the Toolbar.
 * @property textColor The color of the text (title) in the Toolbar.
 * @property iconColor The color of the icons (e.g., navigation icon, overflow icon) in the Toolbar.
 */
data class ToolbarConfig(
    val title: String,
    val backgroundColor: Color,
    val textColor: Color,
    val iconColor: Color,
)

@Composable
@FlexibleWindowLightDarkPreview
private fun DownloadsTopAppBarPreview() {
    FirefoxTheme {
        DownloadsTopAppBar(
            backgroundColor = FirefoxTheme.colors.layerAccent,
            title = {
                Text(
                    color = FirefoxTheme.colors.textOnColorPrimary,
                    style = FirefoxTheme.typography.headline6,
                    text = stringResource(
                        R.string.download_multi_select_title,
                        1,
                    ),
                )
            },
            navigationIcon = {
                IconButton(onClick = {}) {
                    Icon(
                        painter = painterResource(R.drawable.mozac_ic_back_24),
                        contentDescription = stringResource(R.string.download_navigate_back_description),
                        tint = FirefoxTheme.colors.iconPrimary,
                    )
                }
            },
            actions = {
                IconButton(onClick = {}) {
                    Icon(
                        painter = painterResource(R.drawable.mozac_ic_ellipsis_vertical_24),
                        contentDescription = stringResource(
                            R.string.content_description_menu,
                        ),
                        tint = FirefoxTheme.colors.iconOnColor,
                    )
                }
            },
        )
    }
}
