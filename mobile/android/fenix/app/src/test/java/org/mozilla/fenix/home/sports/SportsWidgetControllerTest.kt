/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.home.sports

import androidx.navigation.NavController
import io.mockk.every
import io.mockk.mockk
import io.mockk.verify
import kotlin.test.Test
import org.junit.Before
import org.mozilla.fenix.R
import org.mozilla.fenix.components.usecases.FenixBrowserUseCases
import org.mozilla.fenix.home.HomeFragmentDirections
import org.mozilla.fenix.utils.Settings

class SportsWidgetControllerTest {

    private lateinit var navController: NavController
    private lateinit var fenixBrowserUseCases: FenixBrowserUseCases
    private lateinit var settings: Settings
    private lateinit var controller: SportsWidgetController

    @Before
    fun setup() {
        navController = mockk(relaxed = true)
        fenixBrowserUseCases = mockk(relaxed = true)
        settings = mockk(relaxed = true)

        every { navController.currentDestination } returns
            mockk {
                every { id } returns R.id.homeFragment
            }

        controller =
            SportsWidgetController(
                navController = navController,
                fenixBrowserUseCases = fenixBrowserUseCases,
                settings = settings,
            )
    }

    @Test
    fun `GIVEN homepage as a new tab is disabled WHEN handleSportsWidgetMatchesAndResultsClicked is called THEN open the matches page in a new tab`() {
        every { settings.enableHomepageAsNewTab } returns false

        controller.handleSportsWidgetMatchesAndResultsClicked()

        verify {
            navController.navigate(R.id.browserFragment)
            fenixBrowserUseCases.loadUrlOrSearch(
                searchTermOrURL = WREXHAM_MATCHES_URL,
                newTab = true,
                private = false,
            )
        }
    }

    @Test
    fun `GIVEN homepage as a new tab is enabled WHEN handleSportsWidgetMatchesAndResultsClicked is called THEN open the matches page in the homepage tab`() {
        every { settings.enableHomepageAsNewTab } returns true

        controller.handleSportsWidgetMatchesAndResultsClicked()

        verify {
            navController.navigate(R.id.browserFragment)
            fenixBrowserUseCases.loadUrlOrSearch(
                searchTermOrURL = WREXHAM_MATCHES_URL,
                newTab = false,
                private = false,
            )
        }
    }

    @Test
    fun `GIVEN homepage as a new tab is disabled WHEN handleSportsWidgetMatchClicked is called THEN open the matches page in a new tab`() {
        every { settings.enableHomepageAsNewTab } returns false

        controller.handleSportsWidgetMatchClicked()

        verify {
            navController.navigate(R.id.browserFragment)
            fenixBrowserUseCases.loadUrlOrSearch(
                searchTermOrURL = WREXHAM_MATCHES_URL,
                newTab = true,
                private = false,
            )
        }
    }

    @Test
    fun `GIVEN homepage as a new tab is enabled WHEN handleSportsWidgetMatchClicked is called THEN open the matches page in the homepage tab`() {
        every { settings.enableHomepageAsNewTab } returns true

        controller.handleSportsWidgetMatchClicked()

        verify {
            navController.navigate(R.id.browserFragment)
            fenixBrowserUseCases.loadUrlOrSearch(
                searchTermOrURL = WREXHAM_MATCHES_URL,
                newTab = false,
                private = false,
            )
        }
    }

    @Test
    fun `WHEN handleSportsWidgetWallpapersClicked is called THEN navigate to the wallpaper settings`() {
        controller.handleSportsWidgetWallpapersClicked()

        verify {
            navController.navigate(
                HomeFragmentDirections.actionGlobalWallpaperSettingsFragment(),
                null,
            )
        }
    }

    @Test
    fun `WHEN handleSportsWidgetRemoved is called THEN navigate to home settings`() {
        controller.handleSportsWidgetRemoved()

        verify {
            navController.navigate(
                HomeFragmentDirections.actionGlobalHomeSettingsFragment(),
                null,
            )
        }
    }
}
