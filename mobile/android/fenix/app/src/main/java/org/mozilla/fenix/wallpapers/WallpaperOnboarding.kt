/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.wallpapers

import android.graphics.Bitmap
import android.util.Size
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.shape.CornerSize
import androidx.compose.material3.Icon
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Surface
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.Composable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.res.painterResource
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.tooling.preview.PreviewLightDark
import androidx.compose.ui.unit.dp
import mozilla.components.ui.icons.R as iconsR
import org.mozilla.fenix.R
import org.mozilla.fenix.settings.wallpaper.WallpaperThumbnails
import org.mozilla.fenix.theme.FirefoxTheme

/**
 * A view that shows content of a WallpaperOnboarding dialog.
 *
 * @param wallpapers Wallpapers to add to grid.
 * @param currentWallpaper The currently selected wallpaper.
 * @param loadWallpaperResource Callback to handle loading a wallpaper bitmap. Only optional in the default case.
 * @param onCloseClicked Callback for when the close button is clicked.
 * @param onExploreMoreButtonClicked Callback for when the bottom text button is clicked.
 * @param onSelectWallpaper Callback for when a new wallpaper is selected.
 */
@Composable
fun WallpaperOnboarding(
    wallpapers: List<Wallpaper>,
    currentWallpaper: Wallpaper,
    loadWallpaperResource: suspend (Wallpaper, Size) -> Bitmap?,
    onCloseClicked: () -> Unit,
    onExploreMoreButtonClicked: () -> Unit,
    onSelectWallpaper: (Wallpaper) -> Unit,
) {
    Surface(
        shape =
            MaterialTheme.shapes.large.copy(
                bottomStart = CornerSize(0.dp),
                bottomEnd = CornerSize(0.dp),
            )
    ) {
        Column(
            modifier = Modifier.padding(horizontal = 32.dp, vertical = 16.dp),
            horizontalAlignment = Alignment.CenterHorizontally,
        ) {
            Icon(
                painter = painterResource(id = iconsR.drawable.mozac_ic_cross_24),
                contentDescription = stringResource(id = R.string.close_tab),
                modifier = Modifier.clickable { onCloseClicked() }.size(24.dp).align(Alignment.End),
            )

            Spacer(modifier = Modifier.height(8.dp))

            Text(
                text = stringResource(R.string.wallpapers_onboarding_dialog_title_text),
                overflow = TextOverflow.Ellipsis,
                maxLines = 1,
                style = FirefoxTheme.typography.headline7,
            )

            Spacer(modifier = Modifier.height(4.dp))

            Text(
                text = stringResource(R.string.wallpapers_onboarding_dialog_body_text),
                color = MaterialTheme.colorScheme.onSurfaceVariant,
                overflow = TextOverflow.Ellipsis,
                maxLines = 1,
                style = FirefoxTheme.typography.caption,
            )

            Spacer(modifier = Modifier.height(16.dp))

            WallpaperThumbnails(
                wallpapers = wallpapers,
                selectedWallpaper = currentWallpaper,
                loadWallpaperResource = { wallpaper, size -> loadWallpaperResource(wallpaper, size) },
                onSelectWallpaper = { onSelectWallpaper(it) },
            )

            Spacer(modifier = Modifier.height(16.dp))

            TextButton(
                modifier = Modifier.align(Alignment.CenterHorizontally).fillMaxWidth(),
                onClick = { onExploreMoreButtonClicked() },
            ) {
                Text(
                    text = stringResource(R.string.wallpapers_onboarding_dialog_explore_more_button_text),
                    color = MaterialTheme.colorScheme.tertiary,
                    overflow = TextOverflow.Ellipsis,
                    maxLines = 1,
                    style = FirefoxTheme.typography.button,
                )
            }
        }
    }
}

@PreviewLightDark
@Composable
private fun WallpaperSnackbarPreview() {
    FirefoxTheme {
        WallpaperOnboarding(
            wallpapers = listOf(Wallpaper.Solid),
            currentWallpaper = Wallpaper.Solid,
            onCloseClicked = {},
            onExploreMoreButtonClicked = {},
            loadWallpaperResource = { _, _ -> null },
            onSelectWallpaper = {},
        )
    }
}
