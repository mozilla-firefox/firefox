/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.home.ui

import android.graphics.Bitmap
import androidx.compose.foundation.Image
import androidx.compose.foundation.background
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Brush
import androidx.compose.ui.graphics.asImageBitmap
import androidx.compose.ui.layout.ContentScale
import androidx.compose.ui.platform.LocalConfiguration
import androidx.compose.ui.platform.testTag
import androidx.compose.ui.res.colorResource
import org.mozilla.fenix.R
import org.mozilla.fenix.wallpapers.Wallpaper

/**
 * Renders the wallpaper as a full-screen background behind the homepage content: a gradient for the edge-to-edge
 * wallpaper, or the wallpaper bitmap otherwise.
 *
 * @param wallpaper The [Wallpaper] to render.
 * @param loadBitmap Suspend function to load the wallpaper bitmap from disk.
 * @param onLoadFailed Called when [loadBitmap] returns null for a non-local wallpaper.
 */
@Composable
fun WallpaperBackground(
    wallpaper: Wallpaper,
    loadBitmap: suspend (Wallpaper, Int) -> Bitmap?,
    onLoadFailed: () -> Unit = {},
) {
    if (wallpaper.name == Wallpaper.EDGE_TO_EDGE) {
        EdgeToEdgeGradient()
        return
    }

    val orientation = LocalConfiguration.current.orientation
    var bitmap by remember { mutableStateOf<Bitmap?>(null) }

    LaunchedEffect(wallpaper.name, orientation) {
        bitmap =
            if (Wallpaper.isLocalWallpaper(wallpaper.name)) {
                null
            } else {
                loadBitmap(wallpaper, orientation).also { result ->
                    if (result == null) onLoadFailed()
                }
            }
    }

    bitmap?.let {
        Image(
            bitmap = it.asImageBitmap(),
            contentDescription = null,
            modifier = Modifier.fillMaxSize(),
            contentScale = ContentScale.Crop,
            alignment = Alignment.BottomStart,
        )
    }
}

@Composable
private fun EdgeToEdgeGradient() {
    Box(
        modifier =
            Modifier.fillMaxSize()
                .background(
                    brush =
                        Brush.verticalGradient(
                            colors =
                                listOf(
                                    colorResource(R.color.homepage_edge_to_edge_gradient_start),
                                    colorResource(R.color.homepage_edge_to_edge_gradient_end),
                                )
                        )
                )
                .testTag(HomepageTestTag.HOMEPAGE_EDGE_TO_EDGE_GRADIENT)
    )
}
