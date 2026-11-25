/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.home.sports

import androidx.navigation.NavController
import org.mozilla.fenix.R
import org.mozilla.fenix.components.usecases.FenixBrowserUseCases
import org.mozilla.fenix.ext.nav
import org.mozilla.fenix.ext.openToBrowser
import org.mozilla.fenix.home.HomeFragmentDirections
import org.mozilla.fenix.utils.Settings

internal const val WREXHAM_WEBSITE_DOMAIN = "wrexhamafc.co.uk"
internal const val WREXHAM_MATCHES_URL = "https://www.$WREXHAM_WEBSITE_DOMAIN/matches"

/**
 * Controller for handling sports widget interactions on the homepage.
 *
 * @param navController [NavController] used for navigation.
 * @param fenixBrowserUseCases [FenixBrowserUseCases] used for loading the club's website.
 * @param settings [Settings] used to check whether the homepage is shown as a new tab.
 */
class SportsWidgetController(
    private val navController: NavController,
    private val fenixBrowserUseCases: FenixBrowserUseCases,
    private val settings: Settings,
) {
    /** Handles the user selecting the "Matches and Results" menu item by opening the club's matches page. */
    fun handleSportsWidgetMatchesAndResultsClicked() {
        openMatchesPage()
    }

    /** Handles the user clicking a match by opening the club's matches page. */
    fun handleSportsWidgetMatchClicked() {
        openMatchesPage()
    }

    private fun openMatchesPage() {
        navController.openToBrowser()
        fenixBrowserUseCases.loadUrlOrSearch(
            searchTermOrURL = WREXHAM_MATCHES_URL,
            newTab = !settings.enableHomepageAsNewTab,
            private = false,
        )
    }

    /** Handles the user selecting the "Wallpapers" menu item by opening the wallpaper settings. */
    fun handleSportsWidgetWallpapersClicked() {
        navController.nav(
            R.id.homeFragment,
            HomeFragmentDirections.actionGlobalWallpaperSettingsFragment(),
        )
    }

    /** Handles the user removing the sports widget from the homepage. */
    fun handleSportsWidgetRemoved() {
        navController.nav(
            R.id.homeFragment,
            HomeFragmentDirections.actionGlobalHomeSettingsFragment(),
        )
    }
}
