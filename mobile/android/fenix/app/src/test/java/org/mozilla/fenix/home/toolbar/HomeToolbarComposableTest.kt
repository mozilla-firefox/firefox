/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.home.toolbar

import android.content.Intent
import android.content.pm.ActivityInfo
import android.content.pm.ResolveInfo
import android.speech.RecognizerIntent
import androidx.compose.material3.MaterialTheme
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.test.junit4.v2.createComposeRule
import androidx.test.ext.junit.runners.AndroidJUnit4
import io.mockk.every
import io.mockk.mockk
import kotlin.test.assertNotNull
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.ExperimentalCoroutinesApi
import kotlinx.coroutines.test.StandardTestDispatcher
import kotlinx.coroutines.test.runTest
import mozilla.components.browser.state.state.BrowserState
import mozilla.components.browser.state.state.createTab
import mozilla.components.browser.state.store.BrowserStore
import mozilla.components.compose.base.theme.Theme
import mozilla.components.compose.browser.toolbar.store.BrowserToolbarStore
import mozilla.components.support.test.robolectric.testContext
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Rule
import org.junit.Test
import org.junit.runner.RunWith
import org.mozilla.fenix.browser.browsingmode.BrowsingMode
import org.mozilla.fenix.browser.browsingmode.BrowsingModeManager
import org.mozilla.fenix.components.AppStore
import org.mozilla.fenix.components.appstate.AppAction.SearchAction
import org.mozilla.fenix.components.metrics.MetricsUtils
import org.mozilla.fenix.home.toolbar.HomeToolbarComposable.Companion.DirectToSearchConfig
import org.mozilla.fenix.theme.FirefoxTheme
import org.mozilla.fenix.utils.Settings
import org.robolectric.shadows.ShadowPackageManager

@RunWith(AndroidJUnit4::class)
@OptIn(ExperimentalCoroutinesApi::class) // for advanceTimeBy
class HomeToolbarComposableTest {
    val appStore = AppStore()
    val browserStore = BrowserStore()
    val toolbarStore = BrowserToolbarStore()
    val dispatcher = StandardTestDispatcher()

    @get:Rule val composeTestRule = createComposeRule()

    @Test
    fun `GIVEN speech recognition is available WHEN should start to a voice search THEN start voice recognition and then search mode`() =
        runTest(dispatcher) {
            val htc =
                buildHomeToolbarComposable(
                    directToSearchConfig =
                        DirectToSearchConfig(
                            startVoiceSearch = true,
                            startSearch = true,
                            source = MetricsUtils.Source.DIGITAL_ASSISTANT,
                        ),
                    coroutineScope = this,
                )
            stubSpeechRecognition()

            htc.build(false)

            assertTrue(appStore.state.voiceSearchState.isRequestingVoiceInput)
            assertFalse(appStore.state.searchState.isSearchActive)

            testScheduler.advanceTimeBy(EDIT_TOOLBAR_DELAY_AFTER_VOICE_REQUEST + 1)
            assertTrue(appStore.state.searchState.isSearchActive)
            assertNull(appStore.state.searchState.sourceTabId)
            assertEquals(MetricsUtils.Source.DIGITAL_ASSISTANT, appStore.state.searchState.searchAccessPoint)
        }

    @Test
    fun `GIVEN speech recognition is not available WHEN should start to a voice search THEN enter search mode`() =
        runTest(dispatcher) {
            val htc =
                buildHomeToolbarComposable(
                    directToSearchConfig =
                        DirectToSearchConfig(
                            startVoiceSearch = true,
                            startSearch = true,
                            source = MetricsUtils.Source.DIGITAL_ASSISTANT,
                        )
                )

            htc.build(false)

            assertFalse(appStore.state.voiceSearchState.isRequestingVoiceInput)
            assertTrue(appStore.state.searchState.isSearchActive)
            assertNull(appStore.state.searchState.sourceTabId)
            assertEquals(MetricsUtils.Source.DIGITAL_ASSISTANT, appStore.state.searchState.searchAccessPoint)
        }

    @Test
    fun `GIVEN a specific tab WHEN should start a typed search from it THEN enter search mode with tab's URL prefilled`() {
        val settings: Settings =
            mockk(relaxed = true) {
                every { showAddressBarInFocusMode } returns false
            }
        val tab = createTab("https://test.com")
        val browserStore = BrowserStore(BrowserState(tabs = listOf(tab)))
        val htc =
            buildHomeToolbarComposable(
                directToSearchConfig =
                    DirectToSearchConfig(
                        startVoiceSearch = false,
                        startSearch = true,
                        sessionId = tab.id,
                        source = MetricsUtils.Source.ACTION,
                    ),
                browserStore = browserStore,
                settings = settings,
            )

        htc.build(false)

        assertFalse(appStore.state.voiceSearchState.isRequestingVoiceInput)
        assertTrue(appStore.state.searchState.isSearchActive)
        assertEquals(tab.id, appStore.state.searchState.sourceTabId)
        assertEquals(MetricsUtils.Source.ACTION, appStore.state.searchState.searchAccessPoint)
        assertEquals(tab.content.url, toolbarStore.state.editState.query.current)
        assertTrue(toolbarStore.state.editState.isQueryPrefilled)
    }

    @Test
    fun `GIVEN addressbar focus mode is enabled WHEN should start a typed search from a tab THEN don't prefill its URL`() {
        val settings: Settings =
            mockk(relaxed = true) {
                every { showAddressBarInFocusMode } returns true
            }
        val tab = createTab("https://test.com")
        val browserStore = BrowserStore(BrowserState(tabs = listOf(tab)))
        val htc =
            buildHomeToolbarComposable(
                directToSearchConfig =
                    DirectToSearchConfig(
                        startVoiceSearch = false,
                        startSearch = true,
                        sessionId = tab.id,
                        source = MetricsUtils.Source.ACTION,
                    ),
                browserStore = browserStore,
                settings = settings,
            )

        htc.build(false)

        assertTrue(appStore.state.searchState.isSearchActive)
        assertEquals(tab.id, appStore.state.searchState.sourceTabId)
        assertEquals("", toolbarStore.state.editState.query.current)
        assertFalse(toolbarStore.state.editState.isQueryPrefilled)
    }

    @Test
    fun `WHEN should start a new typed search THEN enter search mode`() {
        val htc =
            buildHomeToolbarComposable(
                directToSearchConfig =
                    DirectToSearchConfig(
                        startVoiceSearch = false,
                        startSearch = true,
                        source = MetricsUtils.Source.WIDGET,
                    )
            )

        htc.build(false)

        assertFalse(appStore.state.voiceSearchState.isRequestingVoiceInput)
        assertTrue(appStore.state.searchState.isSearchActive)
        assertNull(appStore.state.searchState.sourceTabId)
        assertEquals(MetricsUtils.Source.WIDGET, appStore.state.searchState.searchAccessPoint)
        assertEquals("", toolbarStore.state.editState.query.current)
        assertFalse(toolbarStore.state.editState.isQueryPrefilled)
    }

    @Test
    fun `WHEN search starts and ends THEN the search background color is only provided while search is active`() {
        val htc = buildHomeToolbarComposable(directToSearchConfig = DirectToSearchConfig())
        var searchBackgroundColor: Color? = null
        composeTestRule.setContent {
            FirefoxTheme(theme = Theme.Light) {
                searchBackgroundColor = htc.searchBackgroundColor()
            }
        }

        composeTestRule.waitForIdle()
        assertNull(searchBackgroundColor)

        appStore.dispatch(SearchAction.SearchStarted())
        composeTestRule.waitForIdle()
        assertNotNull(searchBackgroundColor)

        appStore.dispatch(SearchAction.SearchEnded)
        composeTestRule.waitForIdle()
        assertNull(searchBackgroundColor)
    }

    @Test
    fun `GIVEN search is active WHEN getting the search background color THEN it is the theme surface color`() {
        val settings: Settings =
            mockk(relaxed = true) {
                every { enableHomepageTrendingRecentSearch } returns true
            }
        val htc = buildHomeToolbarComposable(directToSearchConfig = DirectToSearchConfig(), settings = settings)
        appStore.dispatch(SearchAction.SearchStarted())
        var surfaceColor: Color? = null
        var searchBackgroundColor: Color? = null
        composeTestRule.setContent {
            FirefoxTheme(theme = Theme.Light) {
                surfaceColor = MaterialTheme.colorScheme.surface
                searchBackgroundColor = htc.searchBackgroundColor()
            }
        }

        composeTestRule.waitForIdle()
        assertEquals(surfaceColor, searchBackgroundColor)
    }

    @Test
    fun `GIVEN an empty search WHEN getting the search background color THEN it is transparent`() {
        val settings: Settings =
            mockk(relaxed = true) {
                every { enableHomepageTrendingRecentSearch } returns false
            }
        val htc = buildHomeToolbarComposable(directToSearchConfig = DirectToSearchConfig(), settings = settings)
        appStore.dispatch(SearchAction.SearchStarted())
        var searchBackgroundColor: Color? = null
        composeTestRule.setContent {
            FirefoxTheme(theme = Theme.Light) {
                searchBackgroundColor = htc.searchBackgroundColor()
            }
        }

        composeTestRule.waitForIdle()
        assertEquals(0f, searchBackgroundColor?.alpha)
    }

    private fun buildHomeToolbarComposable(
        directToSearchConfig: DirectToSearchConfig,
        browserStore: BrowserStore = this.browserStore,
        settings: Settings = mockk(relaxed = true),
        coroutineScope: CoroutineScope = CoroutineScope(Dispatchers.Main),
        browsingModeManager: BrowsingModeManager = mockk { every { mode } returns BrowsingMode.Normal },
    ) =
        HomeToolbarComposable(
            context = testContext,
            navController = mockk(),
            toolbarStore = toolbarStore,
            appStore = appStore,
            browserStore = browserStore,
            browsingModeManager = browsingModeManager,
            settings = settings,
            directToSearchConfig = directToSearchConfig,
            coroutineScope = coroutineScope,
            tabStripContent = {},
            searchSuggestionsContent = {},
            navigationBarContent = {},
        )

    private fun stubSpeechRecognition() {
        val speechRecognitionIntent = Intent(RecognizerIntent.ACTION_RECOGNIZE_SPEECH)

        val info =
            ResolveInfo().apply {
                activityInfo =
                    ActivityInfo().apply {
                        packageName = "fake.voice.recognizer"
                    }
            }

        @Suppress("Deprecation") ShadowPackageManager().addResolveInfoForIntent(speechRecognitionIntent, info)
    }
}
