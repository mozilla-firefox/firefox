/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.home.toolbar

import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.WindowInsets
import androidx.compose.foundation.layout.displayCutout
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.systemBars
import androidx.compose.foundation.layout.union
import androidx.compose.runtime.Composable
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.drawWithCache
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.geometry.Size
import androidx.compose.ui.graphics.Brush
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.toArgb
import androidx.compose.ui.platform.LocalDensity
import mozilla.components.support.utils.ColorUtils
import org.mozilla.fenix.wallpapers.WallpaperTheme

private const val SCRIM_MAX_ALPHA = 0.5f
private const val SCRIM_MID_ALPHA = 0.35f
private const val SCRIM_MID_STOP = 0.5f

/**
 * Draws gradient scrims at the top and bottom edges of the screen so that toolbars laid over an edge-to-edge wallpaper
 * stay legible. Must be laid out full screen, behind the system bars. Each gradient starts at the screen edge, inside
 * the status bar or system navigation bar, and fades out at the inner edge of the toolbar content. The color is chosen
 * to contrast with [WallpaperTheme.onWallpaper].
 *
 * @param topContentHeight Lambda that returns the height in pixels of the content laid out below the status bar. It is
 *   only read when drawing, so that a changing height, e.g. while the toolbar scrolls away, doesn't cause
 *   recompositions.
 * @param bottomContentHeight Lambda that returns the height in pixels of the content laid out above the system
 *   navigation bar.
 */
@Composable
internal fun WallpaperToolbarScrim(
    topContentHeight: () -> Int,
    bottomContentHeight: () -> Int,
) {
    val scrimColor = if (ColorUtils.isDark(WallpaperTheme.onWallpaper.toArgb())) Color.White else Color.Black
    val insets = WindowInsets.systemBars.union(WindowInsets.displayCutout)
    val density = LocalDensity.current
    val topInset = insets.getTop(density)
    val bottomInset = insets.getBottom(density)

    Box(
        modifier =
            Modifier.fillMaxSize().drawWithCache {
                val topScrimHeight = (topInset + topContentHeight()).toFloat()
                val bottomScrimHeight = (bottomInset + bottomContentHeight()).toFloat()

                fun scrimBrush(startY: Float, endY: Float) =
                    Brush.verticalGradient(
                        0f to scrimColor.copy(alpha = SCRIM_MAX_ALPHA),
                        SCRIM_MID_STOP to scrimColor.copy(alpha = SCRIM_MID_ALPHA),
                        1f to Color.Transparent,
                        startY = startY,
                        endY = endY,
                    )
                val bottomScrimTop = size.height - bottomScrimHeight
                val topBrush = scrimBrush(startY = 0f, endY = topScrimHeight)
                val bottomBrush = scrimBrush(startY = size.height, endY = bottomScrimTop)

                onDrawBehind {
                    drawRect(brush = topBrush, size = Size(size.width, topScrimHeight))
                    drawRect(
                        brush = bottomBrush,
                        topLeft = Offset(0f, bottomScrimTop),
                        size = Size(size.width, bottomScrimHeight),
                    )
                }
            }
    )
}
