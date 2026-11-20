/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.onboarding.view

import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.pager.HorizontalPager
import androidx.compose.foundation.pager.PagerState
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.rememberCoroutineScope
import androidx.compose.runtime.setValue
import androidx.compose.ui.Modifier
import androidx.compose.ui.test.junit4.v2.createComposeRule
import androidx.test.ext.junit.runners.AndroidJUnit4
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.launch
import org.junit.Assert.assertEquals
import org.junit.Rule
import org.junit.Test
import org.junit.runner.RunWith

@RunWith(AndroidJUnit4::class)
class OnboardingAutoAdvanceEffectsTest {

    @get:Rule val composeTestRule = createComposeRule()

    private var isSignedIn by mutableStateOf(false)
    private var isSetToDefault by mutableStateOf(false)
    private var scrollToNextPageOrDismissCount = 0
    private lateinit var pagerState: PagerState
    private lateinit var coroutineScope: CoroutineScope

    @Test
    fun `GIVEN the sync sign in page WHEN signing in THEN it advances`() {
        val pages = listOf(termsOfServicePageUiData, defaultBrowserPageUiData, syncPageUiData)
        setContent(pages = pages, initialPage = 2)

        isSignedIn = true

        composeTestRule.runOnIdle { assertEquals(1, scrollToNextPageOrDismissCount) }
    }

    @Test
    fun `GIVEN the sync pairing page WHEN signing in THEN it advances`() {
        val pages = listOf(termsOfServicePageUiData, defaultBrowserPageUiData, syncPairingPageUiData)
        setContent(pages = pages, initialPage = 2)

        isSignedIn = true

        composeTestRule.runOnIdle { assertEquals(1, scrollToNextPageOrDismissCount) }
    }

    @Test
    fun `GIVEN the default browser page WHEN set as default THEN it advances`() {
        val pages = listOf(termsOfServicePageUiData, defaultBrowserPageUiData, syncPageUiData)
        setContent(pages = pages, initialPage = 1)

        isSetToDefault = true

        composeTestRule.runOnIdle { assertEquals(1, scrollToNextPageOrDismissCount) }
    }

    @Test
    fun `GIVEN the current page is not a sync page WHEN signing in THEN it does not advance`() {
        val pages = listOf(termsOfServicePageUiData, defaultBrowserPageUiData, syncPageUiData)
        setContent(pages = pages, initialPage = 1)

        isSignedIn = true

        composeTestRule.runOnIdle { assertEquals(0, scrollToNextPageOrDismissCount) }
    }

    @Test
    fun `GIVEN the current page is not the default browser page WHEN set as default THEN it does not advance`() {
        val pages = listOf(termsOfServicePageUiData, defaultBrowserPageUiData, syncPageUiData)
        setContent(pages = pages, initialPage = 2)

        isSetToDefault = true

        composeTestRule.runOnIdle { assertEquals(0, scrollToNextPageOrDismissCount) }
    }

    @Test
    fun `GIVEN signed in on another page WHEN moving to the sync page THEN it does not advance`() {
        val pages = listOf(termsOfServicePageUiData, defaultBrowserPageUiData, syncPageUiData)
        setContent(pages = pages, initialPage = 1)
        isSignedIn = true

        composeTestRule.runOnIdle {
            coroutineScope.launch { pagerState.scrollToPage(2) }
        }

        composeTestRule.runOnIdle {
            assertEquals(2, pagerState.currentPage)
            assertEquals(0, scrollToNextPageOrDismissCount)
        }
    }

    @Test
    fun `GIVEN signed in and set as default WHEN composed on the default browser page THEN it advances once`() {
        isSetToDefault = true
        isSignedIn = true
        val pages = listOf(termsOfServicePageUiData, defaultBrowserPageUiData, syncPageUiData)
        setContent(pages = pages, initialPage = 1)

        composeTestRule.runOnIdle {
            assertEquals(1, scrollToNextPageOrDismissCount)
            assertEquals(2, pagerState.currentPage)
        }
    }

    @Test
    fun `GIVEN signed in and set as default WHEN composed on the sync page THEN it advances once`() {
        isSetToDefault = true
        isSignedIn = true
        val pages = listOf(termsOfServicePageUiData, defaultBrowserPageUiData, syncPageUiData)
        setContent(pages = pages, initialPage = 2)

        composeTestRule.runOnIdle {
            assertEquals(1, scrollToNextPageOrDismissCount)
        }
    }

    @Test
    fun `GIVEN signed in on the sync page WHEN set as default THEN it does not advance again`() {
        val pages = listOf(termsOfServicePageUiData, defaultBrowserPageUiData, syncPageUiData)
        setContent(pages = pages, initialPage = 2)

        isSignedIn = true
        composeTestRule.runOnIdle {
            assertEquals(1, scrollToNextPageOrDismissCount)
        }

        isSetToDefault = true
        composeTestRule.runOnIdle {
            assertEquals(1, scrollToNextPageOrDismissCount)
        }
    }

    @Test
    fun `GIVEN set as default on the default browser page WHEN signing in THEN it does not advance again`() {
        val pages = listOf(termsOfServicePageUiData, syncPageUiData, defaultBrowserPageUiData)
        setContent(pages = pages, initialPage = 2)

        isSetToDefault = true
        composeTestRule.runOnIdle {
            assertEquals(1, scrollToNextPageOrDismissCount)
        }

        isSignedIn = true
        composeTestRule.runOnIdle {
            assertEquals(1, scrollToNextPageOrDismissCount)
        }
    }

    @Test
    fun `GIVEN the sync page advanced WHEN set as default on the default browser page THEN it advances again`() {
        val pages = listOf(termsOfServicePageUiData, syncPageUiData, defaultBrowserPageUiData)
        setContent(pages = pages, initialPage = 1)
        isSignedIn = true

        composeTestRule.runOnIdle {
            assertEquals(1, scrollToNextPageOrDismissCount)
        }

        isSetToDefault = true
        composeTestRule.runOnIdle {
            assertEquals(2, scrollToNextPageOrDismissCount)
        }
    }

    @Test
    fun `GIVEN the default browser page advanced WHEN signing in on the sync page THEN it advances again`() {
        val pages = listOf(termsOfServicePageUiData, defaultBrowserPageUiData, syncPageUiData)
        setContent(pages = pages, initialPage = 1)
        isSetToDefault = true

        composeTestRule.runOnIdle {
            assertEquals(1, scrollToNextPageOrDismissCount)
            assertEquals(2, pagerState.currentPage)
        }

        isSignedIn = true
        composeTestRule.runOnIdle {
            assertEquals(2, scrollToNextPageOrDismissCount)
        }
    }

    private fun setContent(pages: List<OnboardingPageUiData>, initialPage: Int) {
        pagerState = PagerState(currentPage = initialPage) { pages.size }
        composeTestRule.setContent {
            coroutineScope = rememberCoroutineScope()
            HorizontalPager(state = pagerState) { Box(Modifier.fillMaxSize()) }
            OnboardingAutoAdvanceEffects(
                isSignedIn = isSignedIn,
                isSetToDefault = isSetToDefault,
                pagerState = pagerState,
                pagesToDisplay = pages,
                scrollToNextPageOrDismiss = {
                    scrollToNextPageOrDismissCount++
                    coroutineScope.launch {
                        pagerState.scrollToPage(pagerState.currentPage + 1)
                    }
                },
            )
        }
    }
}
