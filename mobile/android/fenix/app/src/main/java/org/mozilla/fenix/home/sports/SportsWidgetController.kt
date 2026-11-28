/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.home.sports

import androidx.navigation.NavController
import mozilla.components.concept.engine.prompt.ShareData
import org.mozilla.fenix.R
import org.mozilla.fenix.components.share.ShareSource
import org.mozilla.fenix.components.usecases.FenixBrowserUseCases
import org.mozilla.fenix.components.usecases.ShareUseCases
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
 * @param shareUseCases [ShareUseCases] used for sharing the club's matches page.
 * @param settings [Settings] used to check whether the homepage is shown as a new tab.
 * @param shareTitle Title used when sharing the club's matches page.
 */
class SportsWidgetController(
    private val navController: NavController,
    private val fenixBrowserUseCases: FenixBrowserUseCases,
    private val shareUseCases: ShareUseCases,
    private val settings: Settings,
    private val shareTitle: String,
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

    /**
     * Handles the user selecting the "Share" menu item by sharing the club's matches page.
     *
     * @param label Label of the visible match card, shared as the message, or null when no match card is shown.
     */
    fun handleSportsWidgetShareClicked(label: String?) {
        val shareData = ShareData(title = shareTitle, text = label, url = WREXHAM_MATCHES_URL, private = false)

        shareUseCases.shareItems(
            items = listOf(shareData),
            source = ShareSource.HOME,
            subject = shareTitle,
            text = label,
            navigateToShareFragment = {
                navController.nav(
                    R.id.homeFragment,
                    HomeFragmentDirections.actionGlobalShareFragment(
                        shareSubject = shareTitle,
                        data = arrayOf(shareData),
                    ),
                )
            },
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
