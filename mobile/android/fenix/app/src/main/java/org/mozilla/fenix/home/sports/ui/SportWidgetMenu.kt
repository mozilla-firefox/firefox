/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.home.sports.ui

import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.material3.Icon
import androidx.compose.material3.Surface
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Modifier
import androidx.compose.ui.res.painterResource
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.tooling.preview.PreviewLightDark
import mozilla.components.compose.base.button.IconButton
import mozilla.components.compose.base.menu.DropdownMenu
import mozilla.components.compose.base.menu.MenuItem
import mozilla.components.compose.base.text.Text
import mozilla.components.ui.icons.R as iconsR
import org.mozilla.fenix.R
import org.mozilla.fenix.theme.FirefoxTheme

/**
 * Overflow menu of the sports widget.
 *
 * @param expanded Whether the menu is displayed.
 * @param onDismissRequest Invoked when the menu should be dismissed.
 * @param onResults Invoked when "Results" menu item is selected.
 * @param onStandings Invoked when "Standings" menu item is selected.
 * @param onWallpapers Invoked when "Wallpapers" menu item is selected.
 * @param onShare Invoked when "Share" menu item is selected.
 * @param onRemove Invoked when "Remove Widget" menu item is selected.
 */
@Composable
internal fun SportsWidgetMenu(
    expanded: Boolean,
    onDismissRequest: () -> Unit,
    onResults: () -> Unit,
    onStandings: () -> Unit,
    onWallpapers: () -> Unit,
    onShare: () -> Unit,
    onRemove: () -> Unit,
) {
    DropdownMenu(
        menuItems =
            listOf(
                MenuItem.IconItem(
                    text = Text.Resource(R.string.wrex_widget_results),
                    drawableRes = iconsR.drawable.mozac_ic_clipboard_24,
                    onClick = {
                        onDismissRequest()
                        onResults()
                    },
                ),
                MenuItem.IconItem(
                    text = Text.Resource(R.string.wrex_widget_standings),
                    drawableRes = iconsR.drawable.mozac_ic_reader_view_24,
                    onClick = {
                        onDismissRequest()
                        onStandings()
                    },
                ),
                MenuItem.IconItem(
                    text = Text.Resource(R.string.wrex_widget_wallpapers),
                    drawableRes = iconsR.drawable.mozac_ic_wallpaper_24,
                    onClick = {
                        onDismissRequest()
                        onWallpapers()
                    },
                ),
                MenuItem.IconItem(
                    text = Text.Resource(R.string.share_header_2),
                    drawableRes = iconsR.drawable.mozac_ic_share_android_24,
                    onClick = {
                        onDismissRequest()
                        onShare()
                    },
                ),
                MenuItem.IconItem(
                    text = Text.Resource(R.string.wrex_widget_remove_widget),
                    drawableRes = iconsR.drawable.mozac_ic_delete_24,
                    level = MenuItem.FixedItem.Level.Critical,
                    onClick = {
                        onDismissRequest()
                        onRemove()
                    },
                ),
            ),
        expanded = expanded,
        onDismissRequest = onDismissRequest,
    )
}

@PreviewLightDark
@Composable
private fun SportsWidgetMenuPreview() {
    FirefoxTheme {
        Surface(modifier = Modifier.fillMaxSize()) {
            Box {
                var expanded by remember { mutableStateOf(true) }

                IconButton(
                    onClick = { expanded = true },
                    contentDescription = stringResource(R.string.wrex_widget_more_options_content_description),
                ) {
                    Icon(
                        painter = painterResource(iconsR.drawable.mozac_ic_ellipsis_vertical_24),
                        contentDescription = null,
                    )
                }

                SportsWidgetMenu(
                    expanded = expanded,
                    onDismissRequest = { expanded = false },
                    onResults = {},
                    onStandings = {},
                    onWallpapers = {},
                    onShare = {},
                    onRemove = {},
                )
            }
        }
    }
}
