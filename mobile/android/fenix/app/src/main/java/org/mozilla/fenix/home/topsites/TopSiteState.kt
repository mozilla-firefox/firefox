/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.home.topsites

import androidx.compose.runtime.Composable
import androidx.compose.runtime.ReadOnlyComposable
import mozilla.components.feature.top.sites.TopSite
import org.mozilla.fenix.components.appstate.AppState
import org.mozilla.fenix.utils.Settings

/**
 * State of the top sites section on the homepage.
 *
 * @property topSites List of [TopSite] to display.
 * @property colors The color set defined by [TopSiteColors] used to style a top site.
 */
internal data class TopSiteState(
    val topSites: List<TopSite>,
    val colors: TopSiteColors,
) {

    companion object {

        /**
         * Builds a new [TopSiteState] from the current [AppState] and [Settings], or null when the top sites section
         * should be hidden.
         *
         * @param appState State to build the [TopSiteState] from.
         * @param settings [Settings] corresponding to how the top sites should be displayed.
         */
        @Composable
        @ReadOnlyComposable
        internal fun build(
            appState: AppState,
            settings: Settings,
        ): TopSiteState? {
            return appState.topSites
                .takeIf { settings.showTopSitesFeature && it.isNotEmpty() }
                ?.let { topSites ->
                    TopSiteState(
                        topSites = topSites,
                        colors = TopSiteColors.colors(wallpaperState = appState.wallpaperState),
                    )
                }
        }
    }
}
