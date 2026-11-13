/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.summarization

import io.mockk.coEvery
import io.mockk.every
import io.mockk.mockk
import io.mockk.verify
import kotlin.test.assertEquals
import kotlin.test.assertFalse
import kotlin.test.assertTrue
import kotlinx.coroutines.ExperimentalCoroutinesApi
import kotlinx.coroutines.test.TestScope
import kotlinx.coroutines.test.runCurrent
import kotlinx.coroutines.test.runTest
import mozilla.components.browser.state.state.BrowserState
import mozilla.components.browser.state.state.ContentState
import mozilla.components.browser.state.state.EngineState
import mozilla.components.browser.state.state.TabSessionState
import mozilla.components.browser.state.state.createCustomTab
import mozilla.components.browser.state.store.BrowserStore
import mozilla.components.compose.base.text.Text
import mozilla.components.compose.menu.data.MenuItemTitle
import mozilla.components.compose.menu.data.StandardMenuItem
import mozilla.components.compose.menu.ui.MenuItemIconRes
import mozilla.components.compose.menu.ui.MenuItemState
import mozilla.components.concept.engine.EngineSession
import mozilla.components.ui.icons.R as iconsR
import org.junit.Test
import org.mozilla.fenix.R
import org.mozilla.fenix.components.menu.MenuFragmentDirections
import org.mozilla.fenix.components.menu.MenuTarget
import org.mozilla.fenix.components.menu.fake.FakeMenuHost
import org.mozilla.fenix.components.menu.fake.reachableEvents
import org.mozilla.fenix.components.menu.store.MenuAction
import org.mozilla.fenix.summarization.eligibility.SummarizationEligibilityChecker
import org.mozilla.fenix.summarization.onboarding.SummarizationFeatureDiscoveryConfiguration
import org.mozilla.fenix.summarization.onboarding.SummarizeDiscoveryEvent

@OptIn(ExperimentalCoroutinesApi::class)
class SummarizePageMenuItemProviderTest {
    @Test
    fun `GIVEN the feature is not available WHEN providing the item THEN don't show it`() = runTest {
        val provider = provider(settings = settings(showMenuItem = false))

        assertTrue(provider.itemsFlow.value.isEmpty())
    }

    @Test
    fun `GIVEN the page was not checked yet WHEN providing the item THEN show summarizing as not available`() =
        runTest {
            val provider = provider()

            assertEquals(summarizePageItem(isEnabled = false), provider.itemsFlow.value.single())
        }

    @Test
    fun `WHEN the page can be summarized THEN allow summarizing it`() = runTest {
        val provider = provider(eligibilityChecker = eligibilityChecker(isEligible = true))

        runCurrent()

        assertEquals(summarizePageItem(isEnabled = true), provider.itemsFlow.value.single())
    }

    @Test
    fun `GIVEN a custom tab WHEN the page can be summarized THEN allow summarizing it`() = runTest {
        val customTab =
            createCustomTab(url = "https://mozilla.org", id = "customTabId", engineSession = mockk(relaxed = true))
        val store = BrowserStore(BrowserState(customTabs = listOf(customTab)))
        val provider =
            provider(
                browserStore = store,
                eligibilityChecker = eligibilityChecker(isEligible = true),
                target = MenuTarget.CustomTab("customTabId"),
            )

        runCurrent()

        assertEquals(summarizePageItem(isEnabled = true), provider.itemsFlow.value.single())
    }

    @Test
    fun `GIVEN target is CustomizeMenu WHEN providing the item THEN show item without click events`() = runTest {
        val provider = provider(target = MenuTarget.CustomizeMenu)

        assertEquals(
            StandardMenuItem(
                title = MenuItemTitle(Text.Resource(R.string.browser_menu_summarize_page)),
                icon = MenuItemIconRes(iconsR.drawable.mozac_ic_lightning_24),
                onClickEvent = null,
                state = MenuItemState.DEFAULT,
            ),
            provider.itemsFlow.value.single(),
        )
    }

    @Test
    fun `WHEN the page cannot be summarized THEN keep summarizing as not available`() = runTest {
        val provider = provider(eligibilityChecker = eligibilityChecker(isEligible = false))

        runCurrent()

        assertEquals(summarizePageItem(isEnabled = false), provider.itemsFlow.value.single())
    }

    @Test
    fun `GIVEN a private page WHEN providing the item THEN don't allow summarizing it`() = runTest {
        val provider =
            provider(
                browserStore = browserStore(isPrivate = true),
                eligibilityChecker = eligibilityChecker(isEligible = true),
            )

        runCurrent()

        assertEquals(summarizePageItem(isEnabled = false, isHighlighted = false), provider.itemsFlow.value.single())
    }

    @Test
    fun `GIVEN the feature is to be highlighted WHEN providing the item THEN highlight its icon`() = runTest {
        val provider = provider(settings = settings(shouldHighlightMenuItem = true))

        assertEquals(summarizePageItem(isEnabled = false, isHighlighted = true), provider.itemsFlow.value.single())
    }

    @Test
    fun `WHEN clicking the item THEN show the summary of the current page in place of the menu`() = runTest {
        val menu = FakeMenuHost()

        provider().onEvent(MenuAction.Navigate.Summarizer, menu)

        assertEquals(
            MenuFragmentDirections.actionMenuFragmentToSummarizationFragment(sessionId = TAB_ID),
            menu.directions,
        )
    }

    @Test
    fun `WHEN tapping summarize with a custom tab THEN show the summary of the custom tab`() = runTest {
        val customTab = createCustomTab(url = "https://mozilla.org", id = CUSTOM_TAB_ID)
        val store = BrowserStore(BrowserState(customTabs = listOf(customTab)))
        val menu = FakeMenuHost()

        provider(browserStore = store, target = MenuTarget.CustomTab(CUSTOM_TAB_ID))
            .onEvent(MenuAction.Navigate.Summarizer, menu)

        assertEquals(
            MenuFragmentDirections.actionMenuFragmentToSummarizationFragment(sessionId = CUSTOM_TAB_ID),
            menu.directions,
        )
    }

    @Test
    fun `GIVEN the current page can be summarized WHEN the item is shown THEN count it as noticed`() = runTest {
        val settings = discoverySettings()
        val provider = provider(settings = settings, eligibilityChecker = eligibilityChecker(isEligible = true))

        provider.onEvent(MenuAction.OnSummarizationMenuExposed, FakeMenuHost())
        runCurrent()

        verify { settings.cacheDiscoveryEvent(SummarizeDiscoveryEvent.MenuItemExposure) }
    }

    @Test
    fun `GIVEN summarizing is not offered WHEN the item is shown THEN don't count it as noticed`() = runTest {
        val settings = discoverySettings(showMenuItem = false)
        val provider = provider(settings = settings, eligibilityChecker = eligibilityChecker(isEligible = true))

        provider.onEvent(MenuAction.OnSummarizationMenuExposed, FakeMenuHost())
        runCurrent()

        verify(exactly = 0) { settings.cacheDiscoveryEvent(any()) }
    }

    @Test
    fun `GIVEN a private page WHEN the item is shown THEN don't count it as noticed`() = runTest {
        val settings = discoverySettings()
        val provider =
            provider(
                browserStore = browserStore(isPrivate = true),
                settings = settings,
                eligibilityChecker = eligibilityChecker(isEligible = true),
            )

        provider.onEvent(MenuAction.OnSummarizationMenuExposed, FakeMenuHost())
        runCurrent()

        verify(exactly = 0) { settings.cacheDiscoveryEvent(any()) }
    }

    @Test
    fun `GIVEN the current page cannot be summarized WHEN the item is shown THEN don't count it as noticed`() =
        runTest {
            val settings = discoverySettings()
            val provider = provider(settings = settings, eligibilityChecker = eligibilityChecker(isEligible = false))

            provider.onEvent(MenuAction.OnSummarizationMenuExposed, FakeMenuHost())
            runCurrent()

            verify(exactly = 0) { settings.cacheDiscoveryEvent(any()) }
        }

    @Test
    fun `GIVEN no selected tab WHEN the item is shown THEN don't count it as noticed`() = runTest {
        val settings = discoverySettings()
        val provider =
            provider(
                browserStore = BrowserStore(),
                settings = settings,
                eligibilityChecker = eligibilityChecker(isEligible = true),
            )

        provider.onEvent(MenuAction.OnSummarizationMenuExposed, FakeMenuHost())
        runCurrent()

        verify(exactly = 0) { settings.cacheDiscoveryEvent(any()) }
    }

    @Test
    fun `WHEN the page can be summarized or not THEN handle all events the item can dispatch and no others`() =
        runTest {
            listOf(true, false).forEach { isEligible ->
                val provider = provider(eligibilityChecker = eligibilityChecker(isEligible = isEligible))
                runCurrent()

                val events = provider.itemsFlow.value.single().reachableEvents()

                assertTrue(events.all { provider.handles(it) }, "Not all of $events are handled")
                assertFalse(provider.handles(MenuAction.Navigate.Translate))
            }
        }

    private fun discoverySettings(showMenuItem: Boolean = true): SummarizationFeatureDiscoveryConfiguration =
        mockk(relaxed = true) { every { this@mockk.showMenuItem } returns showMenuItem }

    // The item is kept up to date, and the page checked, on a scope that runTest cancels at the end of each test.
    private fun TestScope.provider(
        browserStore: BrowserStore = browserStore(),
        settings: SummarizationFeatureDiscoveryConfiguration = settings(),
        eligibilityChecker: SummarizationEligibilityChecker = eligibilityChecker(isEligible = false),
        target: MenuTarget = MenuTarget.BrowserTab,
    ) =
        SummarizePageMenuItemProvider(
            browserStore = browserStore,
            target = target,
            summarizationSettings = settings,
            eligibilityChecker = eligibilityChecker,
            scope = backgroundScope,
        )

    private fun browserStore(isPrivate: Boolean = false) =
        BrowserStore(
            BrowserState(
                tabs =
                    listOf(
                        TabSessionState(
                            id = TAB_ID,
                            content = ContentState(url = "https://mozilla.org", private = isPrivate),
                            engineState = EngineState(engineSession = mockk<EngineSession>(relaxed = true)),
                        )
                    ),
                selectedTabId = TAB_ID,
            )
        )

    private fun settings(
        showMenuItem: Boolean = true,
        shouldHighlightMenuItem: Boolean = false,
    ): SummarizationFeatureDiscoveryConfiguration = mockk {
        every { this@mockk.showMenuItem } returns showMenuItem
        every { this@mockk.shouldHighlightMenuItem } returns shouldHighlightMenuItem
    }

    private fun eligibilityChecker(isEligible: Boolean): SummarizationEligibilityChecker = mockk {
        coEvery { checkLanguage(any()) } returns Result.success(isEligible)
    }

    private fun summarizePageItem(
        isEnabled: Boolean,
        isHighlighted: Boolean = false,
    ) =
        StandardMenuItem(
            title = MenuItemTitle(Text.Resource(R.string.browser_menu_summarize_page)),
            icon = MenuItemIconRes(iconsR.drawable.mozac_ic_lightning_24, isHighlighted = isHighlighted),
            onClickEvent = MenuAction.Navigate.Summarizer,
            onShownEvent = MenuAction.OnSummarizationMenuExposed,
            showNewIndicator = false,
            state =
                when (isEnabled) {
                    true -> MenuItemState.DEFAULT
                    else -> MenuItemState.DISABLED
                },
        )

    private companion object {
        const val TAB_ID = "tab1"
        const val CUSTOM_TAB_ID = "customTabId"
    }
}
