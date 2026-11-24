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

    @Before
    fun setup() {
        navController = mockk(relaxed = true)

        every { navController.currentDestination } returns
            mockk {
                every { id } returns R.id.homeFragment
            }
    }

    @Test
    fun `WHEN handleSportsWidgetRemoved is called THEN navigate to home settings`() {
        val controller = SportsWidgetController(navController = navController)

        controller.handleSportsWidgetRemoved()

        verify {
            navController.navigate(
                HomeFragmentDirections.actionGlobalHomeSettingsFragment(),
                null,
            )
        }
    }
}
