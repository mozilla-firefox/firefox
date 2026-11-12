/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.settings.account

import android.content.Intent
import androidx.navigation.NavController
import io.mockk.Called
import io.mockk.Runs
import io.mockk.every
import io.mockk.just
import io.mockk.mockk
import io.mockk.spyk
import io.mockk.verify
import org.junit.Assert.assertEquals
import org.junit.Test
import org.junit.runner.RunWith
import org.mozilla.fenix.BrowserDirection
import org.mozilla.fenix.HomeActivity
import org.mozilla.fenix.ext.components
import org.mozilla.fenix.utils.Settings
import org.robolectric.RobolectricTestRunner

@RunWith(RobolectricTestRunner::class)
class AuthCustomTabActivityTest {

    @Test
    fun `navigateToBrowserOnColdStart does nothing for AuthCustomTabActivity`() {
        val activity = spyk(AuthCustomTabActivity())

        val settings: Settings = mockk()
        every { settings.shouldReturnToBrowser } returns true
        every { activity.components.settings.shouldReturnToBrowser } returns true
        every { activity.openToBrowser(any(), any()) } just Runs

        activity.navigateToBrowserOnColdStart()

        verify(exactly = 0) { activity.openToBrowser(BrowserDirection.FromGlobal) }
    }

    @Test
    fun `navigateToHome does nothing for AuthCustomTabActivity`() {
        val activity = spyk(AuthCustomTabActivity())
        val navHostController: NavController = mockk()

        activity.navigateToHome(navHostController)
        verify { navHostController wasNot Called }
    }

    @Test
    fun `handleNewIntent does nothing for AuthCustomTabActivity`() {
        val activity = spyk(AuthCustomTabActivity())
        val intent: Intent = mockk(relaxed = true)

        activity.handleNewIntent(intent)
        verify { intent wasNot Called }
    }

    @Test
    fun `GIVEN the open home extra WHEN authentication completes THEN HomeActivity is opened and the activity finishes`() {
        val activity = spyk(AuthCustomTabActivity())
        every { activity.intent } returns
            Intent().putExtra(AuthCustomTabActivity.EXTRA_OPEN_HOME_ON_AUTHENTICATED, true)
        every { activity.packageName } returns "org.mozilla.fenix"
        every { activity.startActivity(any<Intent>()) } just Runs
        every { activity.finish() } just Runs

        activity.onAuthenticationComplete()

        verify {
            activity.startActivity(
                withArg {
                    assertEquals(
                        HomeActivity::class.java.name,
                        it.component?.className,
                    )
                }
            )
            activity.finish()
        }
    }

    @Test
    fun `GIVEN no open home extra WHEN authentication completes THEN only the activity finishes`() {
        val activity = spyk(AuthCustomTabActivity())
        every { activity.intent } returns Intent()
        every { activity.finish() } just Runs

        activity.onAuthenticationComplete()

        verify(exactly = 0) { activity.startActivity(any<Intent>()) }
        verify(exactly = 1) { activity.finish() }
    }
}
