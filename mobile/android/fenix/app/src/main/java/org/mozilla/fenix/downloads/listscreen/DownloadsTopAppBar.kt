/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.downloads.listscreen

import androidx.compose.foundation.layout.WindowInsets
import androidx.compose.material3.Icon
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.material3.TopAppBar
import androidx.compose.material3.TopAppBarDefaults
import androidx.compose.runtime.Composable
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.res.painterResource
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.unit.dp
import mozilla.components.compose.base.annotation.FlexibleWindowLightDarkPreview
import mozilla.components.compose.base.button.IconButton
import mozilla.components.ui.icons.R as iconsR
import org.mozilla.fenix.R
import org.mozilla.fenix.theme.FirefoxTheme

/**
 * A TopAppBar for the Downloads screen. It has slots for a title, an optional navigation icon and actions.
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
    navigationIcon: @Composable () -> Unit,
    title: @Composable () -> Unit,
    actions: @Composable () -> Unit,
) {
    TopAppBar(
        title = {
            title()
        },
        modifier = modifier,
        navigationIcon = navigationIcon,
        actions = {
            actions()
        },
        windowInsets =
            WindowInsets(
                top = 0.dp,
                bottom = 0.dp,
            ),
        colors = TopAppBarDefaults.topAppBarColors(containerColor = backgroundColor),
    )
}

@Composable
@FlexibleWindowLightDarkPreview
private fun DownloadsTopAppBarPreview() {
    FirefoxTheme {
        DownloadsTopAppBar(
            backgroundColor = MaterialTheme.colorScheme.primary,
            title = {
                Text(
                    color = MaterialTheme.colorScheme.inverseOnSurface,
                    style = FirefoxTheme.typography.headline5,
                    text =
                        stringResource(
                            R.string.download_multi_select_title,
                            1,
                        ),
                )
            },
            navigationIcon = {
                IconButton(
                    onClick = {},
                    contentDescription = stringResource(R.string.download_navigate_back_description),
                ) {
                    Icon(
                        painter = painterResource(iconsR.drawable.mozac_ic_back_24),
                        contentDescription = null,
                        tint = MaterialTheme.colorScheme.inverseOnSurface,
                    )
                }
            },
            actions = {
                IconButton(
                    onClick = {},
                    contentDescription = stringResource(R.string.content_description_menu),
                ) {
                    Icon(
                        painter = painterResource(iconsR.drawable.mozac_ic_more_vertical_round_24),
                        contentDescription = null,
                        tint = MaterialTheme.colorScheme.inverseOnSurface,
                    )
                }
            },
        )
    }
}
