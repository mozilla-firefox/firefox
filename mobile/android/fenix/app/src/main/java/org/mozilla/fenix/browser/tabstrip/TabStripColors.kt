/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.browser.tabstrip

import androidx.compose.material3.MaterialTheme
import androidx.compose.runtime.Composable
import androidx.compose.ui.graphics.Brush
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.SolidColor
import org.mozilla.fenix.wallpapers.WallpaperTheme

/**
 * Represents the colors for the tab strip.
 *
 * @property backgroundBrush The brush used to paint the tab strip background. Use [SolidColor] with a transparent color
 *   to let edge-to-edge wallpapers show through.
 * @property tabItemBackgroundColors The background colors of the tab strip items.
 * @property tabItemContentColors The colors of the tab strip item titles.
 * @property closeButtonColors The colors of the tab strip item close buttons.
 * @property addTabButtonColor The color of the add tab button.
 */
data class TabStripColors(
    val backgroundBrush: Brush,
    val tabItemBackgroundColors: TabColors,
    val tabItemContentColors: TabColors,
    val closeButtonColors: TabColors,
    val addTabButtonColor: Color,
) {

    /**
     * Represents the background colors of a tab item.
     *
     * @param activeColor The color to use when the tab is selected.
     * @param inactiveColor The color to use when the tab is not selected.
     */
    data class TabColors(
        private val activeColor: Color,
        private val inactiveColor: Color,
    ) {
        /**
         * Returns the appropriate background color based on the tab's active state.
         *
         * @param isActive Whether the tab is currently selected/active.
         * @return The active color if the tab is active, otherwise the inactive color.
         */
        fun get(isActive: Boolean) = if (isActive) activeColor else inactiveColor
    }

    companion object {

        /**
         * Builds a [TabStripColors] instance based on the provided parameters.
         *
         * @param isOverEdgeToEdgeWallpaper Whether the tab strip is drawn directly over a universal edge-to-edge
         *   wallpaper.
         */
        @Composable
        internal fun build(isOverEdgeToEdgeWallpaper: Boolean): TabStripColors =
            if (isOverEdgeToEdgeWallpaper) edgeToEdgeWallpaper() else default()

        /** Returns the default [TabStripColors] instance. */
        @Composable
        fun default() =
            TabStripColors(
                backgroundBrush = SolidColor(MaterialTheme.colorScheme.surface),
                tabItemBackgroundColors =
                    TabColors(
                        activeColor = MaterialTheme.colorScheme.surface,
                        inactiveColor = Color.Transparent,
                    ),
                tabItemContentColors = defaultTabItemContentColors(),
                closeButtonColors = defaultCloseButtonColors(),
                addTabButtonColor = MaterialTheme.colorScheme.onSurface,
            )

        /**
         * Returns the [TabStripColors] used when the tab strip is drawn directly over a universal edge-to-edge
         * wallpaper: no background of its own, and the content that sits directly on the wallpaper (and the toolbar
         * scrim, for wallpapers with an image) tinted with the wallpaper text color.
         */
        @Composable
        private fun edgeToEdgeWallpaper(): TabStripColors {
            val default = default()
            val onWallpaper = WallpaperTheme.onWallpaper
            return default.copy(
                backgroundBrush = SolidColor(Color.Transparent),
                tabItemContentColors =
                    TabColors(activeColor = MaterialTheme.colorScheme.onSurface, inactiveColor = onWallpaper),
                closeButtonColors =
                    TabColors(activeColor = MaterialTheme.colorScheme.onSurface, inactiveColor = onWallpaper),
                addTabButtonColor = onWallpaper,
            )
        }

        @Composable
        private fun defaultTabItemContentColors() =
            TabColors(
                activeColor = MaterialTheme.colorScheme.onSurface,
                inactiveColor = MaterialTheme.colorScheme.onSurface,
            )

        @Composable
        private fun defaultCloseButtonColors() =
            TabColors(
                activeColor = MaterialTheme.colorScheme.onSurface,
                inactiveColor = MaterialTheme.colorScheme.onSurfaceVariant,
            )
    }
}
