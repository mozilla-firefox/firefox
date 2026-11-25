/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.home.privatebrowsing.controller

import androidx.navigation.NavController
import mozilla.telemetry.glean.private.NoExtras
import org.mozilla.fenix.GleanMetrics.Homepage
import org.mozilla.fenix.R
import org.mozilla.fenix.browser.browsingmode.BrowsingMode
import org.mozilla.fenix.browser.browsingmode.BrowsingModeManager
import org.mozilla.fenix.components.usecases.FenixBrowserUseCases
import org.mozilla.fenix.home.privatebrowsing.interactor.PrivateBrowsingInteractor
import org.mozilla.fenix.settings.SupportUtils
import org.mozilla.fenix.utils.Settings

/** An interface that handles the view manipulation of the private browsing mode. */
interface PrivateBrowsingController {
    /** @see [PrivateBrowsingInteractor.onLearnMoreClicked] */
    fun handleLearnMoreClicked()

    /** @see [PrivateBrowsingInteractor.onPrivateModeButtonClicked] */
    fun handlePrivateModeButtonClicked(newMode: BrowsingMode)
}

/** The default implementation of [PrivateBrowsingController]. */
class DefaultPrivateBrowsingController(
    private val navController: NavController,
    private val browsingModeManager: BrowsingModeManager,
    private val fenixBrowserUseCases: FenixBrowserUseCases,
    private val settings: Settings,
) : PrivateBrowsingController {

    override fun handleLearnMoreClicked() {
        val learnMoreURL =
            SupportUtils.getGenericSumoURLForTopic(SupportUtils.SumoTopic.PRIVATE_BROWSING_MYTHS) +
                "?as=u&utm_source=inproduct"
        val newTab = settings.enableHomepageAsNewTab.not()

        navController.navigate(R.id.browserFragment)
        fenixBrowserUseCases.loadUrlOrSearch(
            searchTermOrURL = learnMoreURL,
            newTab = newTab,
            private = true,
        )
    }

    override fun handlePrivateModeButtonClicked(newMode: BrowsingMode) {
        Homepage.privateModeIconTapped.record(NoExtras())

        if (settings.enableHomepageAsNewTab) {
            fenixBrowserUseCases.selectOrAddHomepageTab(private = newMode.isPrivate)
        }

        browsingModeManager.mode = newMode

        if (newMode == BrowsingMode.Private) {
            settings.incrementNumTimesPrivateModeOpened()
        }
    }
}
