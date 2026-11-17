/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.home.intent

import android.content.Intent
import androidx.navigation.NavController
import org.mozilla.fenix.HomeActivity
import org.mozilla.fenix.NavGraphDirections
import org.mozilla.fenix.R
import org.mozilla.fenix.browser.browsingmode.BrowsingModeManager
import org.mozilla.fenix.ext.alreadyOnDestination
import org.mozilla.fenix.tabstray.redux.state.Page
import org.mozilla.fenix.utils.Settings

/**
 * Responds to [HomeActivity.OPEN_TO_TABS_TRAY] flag by opening the tabs tray on the page matching the browsing mode,
 * which is set by [HomeActivity.PRIVATE_BROWSING_MODE] if present, otherwise the last known mode. Does not navigate
 * again if the tabs tray is already displayed.
 *
 * @param browsingModeManager [BrowsingModeManager] used to get and set the browsing mode.
 */
class OpenTabsTrayIntentProcessor(private val browsingModeManager: BrowsingModeManager) : HomeIntentProcessor {
    override fun process(intent: Intent, navController: NavController, out: Intent, settings: Settings): Boolean {
        return if (intent.extras?.getBoolean(HomeActivity.OPEN_TO_TABS_TRAY) == true) {
            out.removeExtra(HomeActivity.OPEN_TO_TABS_TRAY)

            browsingModeManager.updateMode(intent)
            if (!navController.alreadyOnDestination(R.id.tabManagementFragment)) {
                navController.navigate(
                    NavGraphDirections.actionGlobalTabManagementFragment(
                        page =
                            if (browsingModeManager.mode.isPrivate) {
                                Page.PrivateTabs
                            } else {
                                Page.NormalTabs
                            }
                    )
                )
            }
            true
        } else {
            false
        }
    }
}
