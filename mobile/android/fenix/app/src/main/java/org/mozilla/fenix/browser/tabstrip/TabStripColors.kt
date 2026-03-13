/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.browser.tabstrip

import androidx.compose.runtime.Composable
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.res.colorResource
import mozilla.components.compose.base.theme.AcornTheme
import mozilla.components.compose.browser.toolbar.store.BrowserToolbarState
import org.mozilla.fenix.R
import org.mozilla.fenix.browser.browsingmode.BrowsingModeManager
import org.mozilla.fenix.theme.FirefoxTheme
import org.mozilla.fenix.utils.Settings
import org.mozilla.fenix.wallpapers.Wallpaper

/**
 * Represents the colors for the tab strip.
 *
 * @property backgroundColor The background color of the tab strip.
 * @property tabItemBackgroundColors The background colors of the tab strip items.
 */
data class TabStripColors(
    val backgroundColor: Color,
    val tabItemBackgroundColors: TabColors,
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
         * @param toolbarState The current state of the browser toolbar.
         * @param browsingModeManager The browsing mode manager.
         * @param settings The application settings.
         * @return
         */
        @Composable
        internal fun build(
            toolbarState: BrowserToolbarState?,
            browsingModeManager: BrowsingModeManager,
            settings: Settings,
        ): TabStripColors {
            val isPrivate = browsingModeManager.mode.isPrivate
            val isEdgeToEdgeBackgroundEnabled = settings.currentWallpaperName == Wallpaper.EDGE_TO_EDGE
            val isSearching = toolbarState?.let {
                it.isEditMode() && it.editState.query.current.isNotEmpty()
            }
            val shouldUseEdgeToEdgeColors =
                isEdgeToEdgeBackgroundEnabled && !isPrivate && isSearching == false

            return if (false) {
                TabStripColors(
                    backgroundColor = colorResource(R.color.homepage_tab_edge_to_edge_toolbar_background),
                    tabItemBackgroundColors = TabColors(
                        activeColor = colorResource(
                            R.color.homepage_tab_edge_to_edge_tab_strip_item_background_active,
                        ),
                        inactiveColor = colorResource(
                            R.color.homepage_tab_edge_to_edge_tab_strip_item_background_inactive,
                        ),
                    ),
                )
            } else {
                TabStripColors(
                    backgroundColor = AcornTheme.colors.layer3,
                    tabItemBackgroundColors = TabColors(
                        activeColor = AcornTheme.colors.tabActive,
                        inactiveColor = AcornTheme.colors.tabInactive,
                    ),
                )
            }
        }

        /**
         * Returns the default [TabStripColors] instance.
         */
        @Composable
        fun default() = TabStripColors(
            backgroundColor = FirefoxTheme.colors.layer3,
            tabItemBackgroundColors = TabColors(
                activeColor = FirefoxTheme.colors.tabActive,
                inactiveColor = FirefoxTheme.colors.tabInactive,
            ),
        )
    }
}
