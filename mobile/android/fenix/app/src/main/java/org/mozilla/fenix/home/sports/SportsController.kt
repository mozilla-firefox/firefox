/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.home.sports

import androidx.navigation.NavController
import org.mozilla.fenix.GleanMetrics.WorldCup
import org.mozilla.fenix.components.AppStore
import org.mozilla.fenix.components.appstate.AppAction
import org.mozilla.fenix.components.usecases.FenixBrowserUseCases
import org.mozilla.fenix.ext.openToBrowser
import org.mozilla.fenix.utils.Settings

/**
 * Controller for handling sports widget interactions on the homepage.
 */
interface SportsController {

    /**
     * Handles the user selecting countries in the sports widget country selector.
     *
     * @param countryCodes Set of country codes for the selected countries.
     */
    fun handleCountriesSelected(countryCodes: Set<String>)

    /**
     * Handles the user skipping the "Follow your team" card.
     */
    fun handleSkippedFollowTeam()

    /**
     * Handles the user dismissing the sports widget from the homepage.
     */
    fun handleSportsWidgetDismissed()

    /**
     * Handles the user dismissing the countdown widget from the homepage.
     */
    fun handleCountdownWidgetDismissed()

    /**
     * Handles the user clicking the "View Schedule" button.
     */
    fun handleViewScheduleClicked()
}

/**
 * Default implementation of [SportsController] that dispatches actions to the [AppStore].
 *
 * @param appStore The [AppStore] to dispatch actions to.
 * @param settings [Settings] used to persist sports widget preferences.
 * @param navController [NavController] used to navigate to a new browser fragment.
 * @param fenixBrowserUseCases [FenixBrowserUseCases] used to load the sports schedule.
 */
class DefaultSportsController(
    private val appStore: AppStore,
    private val settings: Settings,
    private val navController: NavController,
    private val fenixBrowserUseCases: FenixBrowserUseCases,
) : SportsController {

    override fun handleCountriesSelected(countryCodes: Set<String>) {
        settings.sportsSelectedCountries = countryCodes
        appStore.dispatch(AppAction.SportsWidgetAction.CountriesSelected(countryCodes = countryCodes))
    }

    override fun handleSkippedFollowTeam() {
        settings.hasSkippedSportsFollowTeam = true
        appStore.dispatch(AppAction.SportsWidgetAction.FollowTeamSkipped)
    }

    override fun handleSportsWidgetDismissed() {
        settings.showHomepageSportsWidget = false
        appStore.dispatch(AppAction.SportsWidgetAction.VisibilityChanged(isVisible = false))
    }

    override fun handleCountdownWidgetDismissed() {
        settings.showHomepageCountdownWidget = false
        appStore.dispatch(AppAction.SportsWidgetAction.CountdownVisibilityChanged(isCountdownVisible = false))
        WorldCup.countdownCrossActionClicked.record()
    }

    override fun handleViewScheduleClicked() {
        navController.openToBrowser()

        fenixBrowserUseCases.loadUrlOrSearch(
            searchTermOrURL = SPORT_SCHEDULE_URL,
            newTab = true,
        )
        WorldCup.viewScheduleOnCountdownClicked.record()
    }

    companion object {
        const val SPORT_SCHEDULE_URL =
            "https://www.fifa.com/tournaments/mens/worldcup/canadamexicousa2026/scores-fixtures"
    }
}
