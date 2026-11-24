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
import org.mozilla.fenix.home.HomeFragmentDirections

class SportsWidgetControllerTest {

    private lateinit var navController: NavController
    private lateinit var controller: SportsWidgetController

    @Before
    fun setup() {
        navController = mockk(relaxed = true)

        every { navController.currentDestination } returns
            mockk {
                every { id } returns R.id.homeFragment
            }

        controller = SportsWidgetController(navController = navController)
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
