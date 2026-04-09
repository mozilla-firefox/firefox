/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.home.intent

import android.content.Intent
import androidx.navigation.NavController
import androidx.navigation.navOptions
import io.mockk.Called
import io.mockk.every
import io.mockk.mockk
import io.mockk.verify
import org.junit.Assert.assertFalse
import org.junit.Test
import org.junit.runner.RunWith
import org.mozilla.fenix.NavGraphDirections
import org.mozilla.fenix.R
import org.mozilla.fenix.components.metrics.MetricsUtils
import org.mozilla.fenix.utils.Settings
import org.robolectric.RobolectricTestRunner

@RunWith(RobolectricTestRunner::class)
class AssistIntentProcessorTest {
    private val navController: NavController = mockk(relaxed = true)
    private val out: Intent = mockk(relaxed = true)
    private val settings: Settings = mockk {
        every { shouldUseComposableToolbar } returns false
    }

    @Test
    fun `GIVEN an intent with wrong action WHEN it is processed THEN nothing should happen`() {
        val intent = Intent().apply {
            action = TEST_WRONG_ACTION
        }
        val result =
            StartSearchIntentProcessor { true }.process(intent, navController, out, settings)

        verify { navController wasNot Called }
        verify { out wasNot Called }
        assertFalse(result)
    }

    @Test
    fun `GIVEN an intent with ACTION_ASSIST action WHEN it is processed THEN navigate to the search dialog`() {
        val intent = Intent().apply {
            action = Intent.ACTION_ASSIST
        }

        AssistIntentProcessor().process(intent, navController, out, settings)
        val options = navOptions {
            popUpTo(R.id.homeFragment)
        }

        verify {
            navController.navigate(
                NavGraphDirections.actionGlobalSearchDialog(
                    sessionId = null,
                    searchAccessPoint = MetricsUtils.Source.DIGITAL_ASSISTANT,
                ),
                options,
            )
        }

        verify { out wasNot Called }
    }

    @Test
    fun `GIVEN an intent with ACTION_ASSIST action WHEN it is processed THEN navigate to the new search UX`() {
        every { settings.shouldUseComposableToolbar } returns true
        every { settings.shouldShowVoiceSearch } returns true

        val intent = Intent().apply {
            action = Intent.ACTION_ASSIST
        }

        AssistIntentProcessor().process(intent, navController, out, settings)

        verify {
            navController.navigate(
                NavGraphDirections.actionGlobalHome(
                    sessionToDelete = null,
                    sessionToStartSearchFor = null,
                    focusOnAddressBar = true,
                    startVoiceSearch = true,
                    searchAccessPoint = MetricsUtils.Source.DIGITAL_ASSISTANT,
                ),
                null,
            )
        }

        verify { out wasNot Called }
    }

    @Test
    fun `GIVEN an intent with ACTION_ASSIST action and voice search is disabled WHEN it is processed THEN startVoiceSearch should be false`() {
        every { settings.shouldUseComposableToolbar } returns true
        every { settings.shouldShowVoiceSearch } returns false
        val intent = Intent().apply {
            action = Intent.ACTION_ASSIST
        }

        AssistIntentProcessor().process(intent, navController, out, settings)

        verify {
            navController.navigate(
                NavGraphDirections.actionGlobalHome(
                    sessionToDelete = null,
                    sessionToStartSearchFor = null,
                    focusOnAddressBar = true,
                    startVoiceSearch = false,
                    searchAccessPoint = MetricsUtils.Source.DIGITAL_ASSISTANT,
                ),
                null,
            )
        }

        verify { out wasNot Called }
    }

    companion object {
        const val TEST_WRONG_ACTION = "test-action"
    }
}
