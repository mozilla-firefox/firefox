/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.settings.trustpanel

import androidx.activity.BackEventCompat
import androidx.activity.OnBackPressedDispatcher
import androidx.activity.compose.LocalOnBackPressedDispatcherOwner
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.test.junit4.v2.createComposeRule
import androidx.test.ext.junit.runners.AndroidJUnit4
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Rule
import org.junit.Test
import org.junit.runner.RunWith

@RunWith(AndroidJUnit4::class)
class TrustPanelBackHandlerTest {
    @get:Rule val composeTestRule = createComposeRule()

    private lateinit var dispatcher: OnBackPressedDispatcher
    private var currentRoute = Route.ProtectionPanel
    private var dismissed = false

    @Test
    fun `GIVEN the certificate details panel WHEN system back is pressed THEN the protection panel is shown`() {
        setBackHandler(Route.CertificateDetailsPanel)

        composeTestRule.runOnIdle { dispatcher.onBackPressed() }

        composeTestRule.runOnIdle {
            assertEquals(Route.ProtectionPanel, currentRoute)
            assertFalse(dismissed)
        }
    }

    @Test
    fun `GIVEN the certificate details panel WHEN the back gesture is completed THEN the protection panel is shown`() {
        setBackHandler(Route.CertificateDetailsPanel)

        composeTestRule.runOnIdle {
            dispatcher.dispatchOnBackStarted(backEvent(progress = 0f))
            dispatcher.dispatchOnBackProgressed(backEvent(progress = 0.5f))
            dispatcher.onBackPressed()
        }

        composeTestRule.runOnIdle {
            assertEquals(Route.ProtectionPanel, currentRoute)
            assertFalse(dismissed)
        }
    }

    @Test
    fun `GIVEN the certificate details panel WHEN the back gesture is cancelled THEN the certificate details panel stays shown`() {
        setBackHandler(Route.CertificateDetailsPanel)

        composeTestRule.runOnIdle {
            dispatcher.dispatchOnBackStarted(backEvent(progress = 0f))
            dispatcher.dispatchOnBackProgressed(backEvent(progress = 0.5f))
            dispatcher.dispatchOnBackCancelled()
        }

        composeTestRule.runOnIdle {
            assertEquals(Route.CertificateDetailsPanel, currentRoute)
            assertFalse(dismissed)
        }
    }

    @Test
    fun `GIVEN the certificate details panel WHEN system back is pressed twice THEN the trust panel is dismissed`() {
        setBackHandler(Route.CertificateDetailsPanel)

        composeTestRule.runOnIdle { dispatcher.onBackPressed() }
        composeTestRule.runOnIdle { dispatcher.onBackPressed() }

        composeTestRule.runOnIdle { assertTrue(dismissed) }
    }

    private fun backEvent(progress: Float) =
        BackEventCompat(touchX = 0f, touchY = 0f, progress = progress, swipeEdge = BackEventCompat.EDGE_LEFT)

    private fun setBackHandler(initialRoute: Route) {
        currentRoute = initialRoute
        composeTestRule.setContent {
            dispatcher = requireNotNull(LocalOnBackPressedDispatcherOwner.current).onBackPressedDispatcher
            var route by remember { mutableStateOf(initialRoute) }

            TrustPanelBackHandler(
                route = route,
                onNavigateBack = {
                    route = it
                    currentRoute = it
                },
                onDismiss = { dismissed = true },
            )
        }
    }
}
