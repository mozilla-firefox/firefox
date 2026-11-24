/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.home.sports

import androidx.navigation.NavController
import org.mozilla.fenix.R
import org.mozilla.fenix.ext.nav
import org.mozilla.fenix.home.HomeFragmentDirections

/**
 * Controller for handling sports widget interactions on the homepage.
 *
 * @param navController [NavController] used for navigation.
 */
class SportsWidgetController(private val navController: NavController) {
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
