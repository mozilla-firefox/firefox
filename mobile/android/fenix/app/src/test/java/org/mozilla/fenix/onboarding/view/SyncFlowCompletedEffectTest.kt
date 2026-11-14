/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.onboarding.view

import androidx.compose.foundation.pager.PagerState
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.setValue
import androidx.compose.ui.test.junit4.v2.createComposeRule
import androidx.test.ext.junit.runners.AndroidJUnit4
import org.junit.Assert.assertEquals
import org.junit.Rule
import org.junit.Test
import org.junit.runner.RunWith

@RunWith(AndroidJUnit4::class)
class SyncFlowCompletedEffectTest {

    @get:Rule val composeTestRule = createComposeRule()

    private val pages = listOf(termsOfServicePageUiData, syncPairingPageUiData)
    var isSyncFlowCompleted by mutableStateOf(false)
    private var scrollToNextPageOrDismissCount = 0
    private var syncFlowCompletedHandledCount = 0

    @Test
    fun `GIVEN the sync flow is not completed WHEN the effect is composed THEN it does not scroll nor consume`() {
        setContent(currentPage = 1)

        composeTestRule.runOnIdle {
            assertEquals(0, scrollToNextPageOrDismissCount)
            assertEquals(0, syncFlowCompletedHandledCount)
        }
    }

    @Test
    fun `GIVEN the current page is a sync page WHEN the sync flow completes THEN it scrolls and consumes the result`() {
        isSyncFlowCompleted = true
        setContent(currentPage = 1)

        composeTestRule.runOnIdle {
            assertEquals(1, scrollToNextPageOrDismissCount)
            assertEquals(1, syncFlowCompletedHandledCount)
        }
    }

    @Test
    fun `GIVEN the current page is not a sync page WHEN the sync flow completes THEN it consumes the result without scrolling`() {
        isSyncFlowCompleted = true
        setContent(currentPage = 0)

        composeTestRule.runOnIdle {
            assertEquals(0, scrollToNextPageOrDismissCount)
            assertEquals(1, syncFlowCompletedHandledCount)
        }
    }

    private fun setContent(currentPage: Int) {
        val pagerState = PagerState(currentPage = currentPage) { pages.size }
        composeTestRule.setContent {
            SyncFlowCompletedEffect(
                isSyncFlowCompleted = isSyncFlowCompleted,
                pagerState = pagerState,
                pagesToDisplay = pages,
                scrollToNextPageOrDismiss = { scrollToNextPageOrDismissCount++ },
                onSyncFlowCompletedHandled = {
                    syncFlowCompletedHandledCount++
                    isSyncFlowCompleted = false
                },
            )
        }
    }
}
