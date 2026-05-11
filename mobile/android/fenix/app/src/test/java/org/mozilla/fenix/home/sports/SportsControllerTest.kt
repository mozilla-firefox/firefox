/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.home.sports

import androidx.navigation.NavController
import io.mockk.mockk
import io.mockk.verify
import org.junit.Test
import org.mozilla.fenix.components.AppStore
import org.mozilla.fenix.components.appstate.AppAction
import org.mozilla.fenix.components.usecases.FenixBrowserUseCases
import org.mozilla.fenix.utils.Settings

class SportsControllerTest {

    private val appStore: AppStore = mockk(relaxed = true)
    private val settings: Settings = mockk(relaxed = true)
    private val navController: NavController = mockk(relaxed = true)
    private val fenixBrowserUseCases: FenixBrowserUseCases = mockk(relaxed = true)
    private val controller: SportsController = DefaultSportsController(
        appStore = appStore,
        settings = settings,
        navController = navController,
        fenixBrowserUseCases = fenixBrowserUseCases,
    )

    @Test
    fun `GIVEN a set of country codes WHEN countries are selected THEN the selection is persisted and the action is dispatched`() {
        val countryCodes = setOf("USA", "JPN", "BRA")

        controller.handleCountriesSelected(countryCodes)

        verify {
            settings.sportsSelectedCountries = countryCodes
            appStore.dispatch(
                AppAction.SportsWidgetAction.CountriesSelected(countryCodes = countryCodes),
            )
        }
    }

    @Test
    fun `GIVEN an empty set WHEN countries are selected THEN the selection is cleared and the action is dispatched`() {
        val countryCodes = emptySet<String>()

        controller.handleCountriesSelected(countryCodes)

        verify {
            settings.sportsSelectedCountries = countryCodes
            appStore.dispatch(
                AppAction.SportsWidgetAction.CountriesSelected(countryCodes = countryCodes),
            )
        }
    }

    @Test
    fun `GIVEN a single country WHEN countries are selected THEN the selection is persisted and the action is dispatched`() {
        val countryCodes = setOf("USA")

        controller.handleCountriesSelected(countryCodes)

        verify {
            settings.sportsSelectedCountries = countryCodes
            appStore.dispatch(
                AppAction.SportsWidgetAction.CountriesSelected(countryCodes = countryCodes),
            )
        }
    }

    @Test
    fun `WHEN the follow team flow is skipped THEN the preference is persisted and the action is dispatched`() {
        controller.handleSkippedFollowTeam()

        verify {
            settings.hasSkippedSportsFollowTeam = true
            appStore.dispatch(AppAction.SportsWidgetAction.FollowTeamSkipped)
        }
    }

    @Test
    fun `WHEN the sports widget is dismissed THEN the visibility preference is set to false and the action is dispatched`() {
        controller.handleSportsWidgetDismissed()

        verify {
            settings.showHomepageSportsWidget = false
            appStore.dispatch(AppAction.SportsWidgetAction.VisibilityChanged(isVisible = false))
        }
    }

    @Test
    fun `WHEN the countdown widget is dismissed THEN the visibility preference is set to false and the action is dispatched`() {
        controller.handleCountdownWidgetDismissed()

        verify {
            settings.showHomepageCountdownWidget = false
            appStore.dispatch(AppAction.SportsWidgetAction.CountdownVisibilityChanged(isCountdownVisible = false))
        }
    }
}
