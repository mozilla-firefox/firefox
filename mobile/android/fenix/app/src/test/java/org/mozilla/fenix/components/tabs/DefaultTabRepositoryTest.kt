/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.components.tabs

import android.graphics.Bitmap
import io.mockk.mockk
import java.util.concurrent.TimeUnit
import kotlin.test.assertEquals
import kotlin.test.assertFalse
import kotlin.test.assertNull
import kotlin.test.assertSame
import kotlin.test.assertTrue
import kotlinx.coroutines.ExperimentalCoroutinesApi
import kotlinx.coroutines.flow.toList
import kotlinx.coroutines.launch
import kotlinx.coroutines.test.TestScope
import kotlinx.coroutines.test.runCurrent
import kotlinx.coroutines.test.runTest
import mozilla.components.browser.state.action.ContentAction
import mozilla.components.browser.state.action.CustomTabListAction
import mozilla.components.browser.state.action.LastAccessAction
import mozilla.components.browser.state.action.ListenToPageAction
import mozilla.components.browser.state.action.MediaSessionAction
import mozilla.components.browser.state.action.TabListAction
import mozilla.components.browser.state.state.BrowserState
import mozilla.components.browser.state.state.MediaSessionState
import mozilla.components.browser.state.state.createCustomTab
import mozilla.components.browser.state.state.createTab
import mozilla.components.browser.state.store.BrowserStore
import mozilla.components.concept.engine.mediasession.MediaSession
import mozilla.components.feature.session.SessionUseCases
import mozilla.components.feature.tabdata.coordinator.data.Tab
import mozilla.components.feature.tabdata.coordinator.repository.TabRepository
import mozilla.components.feature.tabs.TabsUseCases
import org.junit.Test
import org.mozilla.fenix.components.AppStore
import org.mozilla.fenix.components.appstate.AppState
import org.mozilla.fenix.components.usecases.FenixBrowserUseCases
import org.mozilla.fenix.ext.maxActiveTime

private const val URL = "https://www.mozilla.org"
private val EXPECTED_TAB = Tab(id = "1", url = URL, title = URL, private = false, inactive = false)

@OptIn(ExperimentalCoroutinesApi::class)
class DefaultTabRepositoryTest {

    @Test
    fun `WHEN there are no open tabs THEN empty data is emitted`() = runTest {
        val emissions = observeTabFlow(BrowserState())

        assertEquals(expected = listOf(TabRepository.Data()), actual = emissions)
    }

    @Test
    fun `WHEN a tab is selected THEN the selected tab ID is emitted`() = runTest {
        val tab = createTab(url = URL, id = "1")
        val emissions = observeTabFlow(BrowserState(tabs = listOf(tab), selectedTabId = tab.id))

        assertEquals(expected = tab.id, actual = emissions.last().selectedTabId)
    }

    @Test
    fun `WHEN no tab is selected THEN the selected tab ID is null`() = runTest {
        val emissions = observeTabFlow(BrowserState(tabs = listOf(createTab(url = URL, id = "1"))))

        assertNull(emissions.last().selectedTabId)
    }

    @Test
    fun `WHEN a tab has a title THEN the title is used as the display title`() = runTest {
        val emissions = observeTabFlow(BrowserState(tabs = listOf(createTab(url = URL, id = "1", title = "Mozilla"))))

        assertEquals(expected = "Mozilla", actual = emissions.last().tabs.single().title)
    }

    @Test
    fun `WHEN a tab has no title THEN the URL is used as the display title`() = runTest {
        val emissions = observeTabFlow(BrowserState(tabs = listOf(createTab(url = URL, id = "1", title = ""))))

        assertEquals(expected = URL, actual = emissions.last().tabs.single().title)
    }

    @Test
    fun `WHEN a tab is private THEN it is mapped as private`() = runTest {
        val emissions = observeTabFlow(BrowserState(tabs = listOf(createTab(url = URL, id = "1", private = true))))

        assertTrue { emissions.last().tabs.single().private }
    }

    @Test
    fun `WHEN tabs are mapped THEN the order of the open tabs is preserved`() = runTest {
        val tabs = List(size = 5) { createTab(url = "$URL/$it", id = it.toString()) }
        val emissions = observeTabFlow(BrowserState(tabs = tabs))

        assertEquals(expected = tabs.map { it.id }, actual = emissions.last().tabs.map { it.id })
    }

    @Test
    fun `WHEN tabs are mapped THEN they are not assigned to a tab group`() = runTest {
        val emissions = observeTabFlow(BrowserState(tabs = listOf(createTab(url = URL, id = "1"))))

        assertNull(emissions.last().tabs.single().tabGroupId)
    }

    @Test
    fun `WHEN a tab is mapped THEN all of its displayable data is emitted`() = runTest {
        val tab = createTab(url = URL, id = "1", title = "Mozilla", private = true, lastAccess = 100L)
        val emissions = observeTabFlow(BrowserState(tabs = listOf(tab)))

        assertEquals(
            expected =
                Tab(
                    id = "1",
                    url = URL,
                    title = "Mozilla",
                    private = true,
                    inactive = false,
                    lastAccess = 100L,
                ),
            actual = emissions.last().tabs.single(),
        )
    }

    @Test
    fun `WHEN a tab's icon is loaded THEN the same icon instance is emitted`() = runTest {
        val icon = mockk<Bitmap>()
        val browserStore = BrowserStore(initialState = BrowserState(tabs = listOf(createTab(url = URL, id = "1"))))
        val emissions = observeTabFlow(browserStore)

        browserStore.dispatch(ContentAction.UpdateIconAction(sessionId = "1", pageUrl = URL, icon = icon))
        runCurrent()

        assertSame(expected = icon, actual = emissions.last().tabs.single().icon)
    }

    @Test
    fun `WHEN a tab is accessed THEN the updated last access time is emitted`() = runTest {
        val browserStore = BrowserStore(initialState = BrowserState(tabs = listOf(createTab(url = URL, id = "1"))))
        val emissions = observeTabFlow(browserStore)

        browserStore.dispatch(LastAccessAction.UpdateLastAccessAction(tabId = "1", lastAccess = 100L))
        runCurrent()

        assertEquals(expected = 100L, actual = emissions.last().tabs.single().lastAccess)
    }

    @Test
    fun `WHEN a tab is playing media THEN it is mapped as having active media`() = runTest {
        val tab =
            createTab(
                url = URL,
                id = "1",
                mediaSessionState = mediaSessionState(MediaSession.PlaybackState.PLAYING),
            )
        val emissions = observeTabFlow(BrowserState(tabs = listOf(tab)))

        assertTrue { emissions.last().tabs.single().isMediaActive }
    }

    @Test
    fun `WHEN a tab has paused media THEN it is not mapped as having active media`() = runTest {
        val tab =
            createTab(
                url = URL,
                id = "1",
                mediaSessionState = mediaSessionState(MediaSession.PlaybackState.PAUSED),
            )
        val emissions = observeTabFlow(BrowserState(tabs = listOf(tab)))

        assertFalse { emissions.last().tabs.single().isMediaActive }
    }

    @Test
    fun `WHEN a tab has no media THEN it is not mapped as having active media`() = runTest {
        val emissions = observeTabFlow(BrowserState(tabs = listOf(createTab(url = URL, id = "1"))))

        assertFalse { emissions.last().tabs.single().isMediaActive }
    }

    @Test
    fun `WHEN a tab starts playing media THEN the updated media state is emitted`() = runTest {
        val tab =
            createTab(
                url = URL,
                id = "1",
                mediaSessionState = mediaSessionState(MediaSession.PlaybackState.PAUSED),
            )
        val browserStore = BrowserStore(initialState = BrowserState(tabs = listOf(tab)))
        val emissions = observeTabFlow(browserStore)

        browserStore.dispatch(
            MediaSessionAction.UpdateMediaPlaybackStateAction(
                tabId = "1",
                playbackState = MediaSession.PlaybackState.PLAYING,
            )
        )
        runCurrent()

        assertEquals(expected = 2, actual = emissions.size)
        assertTrue { emissions.last().tabs.single().isMediaActive }
    }

    @Test
    fun `WHEN a tab is being read out loud THEN it is mapped as having active media`() = runTest {
        val tab = createTab(url = URL, id = "1", listenToPageActive = true)
        val emissions = observeTabFlow(BrowserState(tabs = listOf(tab)))

        assertTrue { emissions.last().tabs.single().isMediaActive }
    }

    @Test
    fun `GIVEN a tab has paused media WHEN it is being read out loud THEN it is mapped as having active media`() =
        runTest {
            val tab =
                createTab(
                    url = URL,
                    id = "1",
                    mediaSessionState = mediaSessionState(MediaSession.PlaybackState.PAUSED),
                    listenToPageActive = true,
                )
            val emissions = observeTabFlow(BrowserState(tabs = listOf(tab)))

            assertTrue { emissions.last().tabs.single().isMediaActive }
        }

    @Test
    fun `WHEN a tab starts being read out loud THEN the updated media state is emitted`() = runTest {
        val browserStore = BrowserStore(initialState = BrowserState(tabs = listOf(createTab(url = URL, id = "1"))))
        val emissions = observeTabFlow(browserStore)

        browserStore.dispatch(ListenToPageAction.ListenToPageActivated(tabId = "1"))
        runCurrent()

        assertEquals(expected = 2, actual = emissions.size)
        assertTrue { emissions.last().tabs.single().isMediaActive }
    }

    @Test
    fun `WHEN a tab stops being read out loud THEN the updated media state is emitted`() = runTest {
        val tab = createTab(url = URL, id = "1", listenToPageActive = true)
        val browserStore = BrowserStore(initialState = BrowserState(tabs = listOf(tab)))
        val emissions = observeTabFlow(browserStore)

        browserStore.dispatch(ListenToPageAction.ListenToPageDeactivated(tabId = "1"))
        runCurrent()

        assertEquals(expected = 2, actual = emissions.size)
        assertFalse { emissions.last().tabs.single().isMediaActive }
    }

    @Test
    fun `GIVEN a tab was created and last accessed before the max active time WHEN it is mapped THEN it is inactive`() =
        runTest {
            val tab = createTab(url = URL, id = "1", createdAt = expiredTime(), lastAccess = expiredTime())
            val emissions = observeTabFlow(BrowserState(tabs = listOf(tab)))

            assertTrue { emissions.last().tabs.single().inactive }
        }

    @Test
    fun `GIVEN a tab was created recently and never accessed WHEN it is mapped THEN it is active`() = runTest {
        val tab = createTab(url = URL, id = "1", createdAt = System.currentTimeMillis(), lastAccess = 0L)
        val emissions = observeTabFlow(BrowserState(tabs = listOf(tab)))

        assertFalse { emissions.last().tabs.single().inactive }
    }

    @Test
    fun `GIVEN a tab was created before the max active time but recently accessed WHEN it is mapped THEN it is active`() =
        runTest {
            val tab = createTab(url = URL, id = "1", createdAt = expiredTime(), lastAccess = System.currentTimeMillis())
            val emissions = observeTabFlow(BrowserState(tabs = listOf(tab)))

            assertFalse { emissions.last().tabs.single().inactive }
        }

    @Test
    fun `WHEN a tab is added THEN the updated tabs are emitted`() = runTest {
        val browserStore = BrowserStore(initialState = BrowserState(tabs = listOf(createTab(url = URL, id = "1"))))
        val emissions = observeTabFlow(browserStore)

        browserStore.dispatch(TabListAction.AddTabAction(tab = createTab(url = URL, id = "2")))
        runCurrent()

        assertEquals(expected = 2, actual = emissions.size)
        assertEquals(expected = listOf("1", "2"), actual = emissions.last().tabs.map { it.id })
    }

    @Test
    fun `WHEN the selected tab changes THEN the updated selection is emitted`() = runTest {
        val browserStore =
            BrowserStore(
                initialState =
                    BrowserState(
                        tabs = listOf(createTab(url = URL, id = "1"), createTab(url = URL, id = "2")),
                        selectedTabId = "1",
                    )
            )
        val emissions = observeTabFlow(browserStore)

        browserStore.dispatch(TabListAction.SelectTabAction(tabId = "2"))
        runCurrent()

        assertEquals(expected = 2, actual = emissions.size)
        assertEquals(expected = "2", actual = emissions.last().selectedTabId)
    }

    @Test
    fun `WHEN a custom tab is added THEN it is neither emitted nor does it trigger a new emission`() = runTest {
        val browserStore = BrowserStore(initialState = BrowserState(tabs = listOf(createTab(url = URL, id = "1"))))
        val emissions = observeTabFlow(browserStore)

        browserStore.dispatch(CustomTabListAction.AddCustomTabAction(tab = createCustomTab(url = URL, id = "2")))
        runCurrent()

        assertEquals(expected = listOf(TabRepository.Data(tabs = listOf(EXPECTED_TAB))), actual = emissions)
    }

    @Test
    fun `WHEN a tab changes in a way which is not displayed THEN no new emission occurs`() = runTest {
        val browserStore = BrowserStore(initialState = BrowserState(tabs = listOf(createTab(url = URL, id = "1"))))
        val emissions = observeTabFlow(browserStore)

        browserStore.dispatch(ContentAction.UpdateLoadingStateAction(sessionId = "1", loading = true))
        runCurrent()
        browserStore.dispatch(ContentAction.UpdateProgressAction(sessionId = "1", progress = 50))
        runCurrent()

        assertEquals(expected = listOf(TabRepository.Data(tabs = listOf(EXPECTED_TAB))), actual = emissions)
    }

    @Test
    fun `WHEN tabs are moved THEN the open tabs are reordered`() = runTest {
        val browserStore = createBrowserStore(tabIds = listOf("1", "2", "3", "4"))

        createRepository(browserStore).moveTabs(tabIds = listOf("1", "2"), targetTabId = "4", placeAfter = true)

        assertEquals(expected = listOf("3", "4", "1", "2"), actual = browserStore.state.tabs.map { it.id })
    }

    @Test
    fun `WHEN a tab is moved THEN the open tabs are reordered`() = runTest {
        val browserStore = createBrowserStore(tabIds = listOf("1", "2", "3"))

        createRepository(browserStore).moveTab(sourceTabId = "3", targetTabId = "1", placeAfter = false)

        assertEquals(expected = listOf("3", "1", "2"), actual = browserStore.state.tabs.map { it.id })
    }

    @Test
    fun `WHEN tabs are removed THEN they are closed and the excluded tabs are not selected`() = runTest {
        val browserStore = createBrowserStore(tabIds = listOf("1", "2", "3"), selectedTabId = "1")

        createRepository(browserStore).removeTabs(tabIds = listOf("1", "2"), excludedFallbackTabIds = setOf("3"))

        assertEquals(expected = listOf("3"), actual = browserStore.state.tabs.map { it.id })
        assertNull(browserStore.state.selectedTabId)
    }

    private fun expiredTime(): Long = System.currentTimeMillis() - maxActiveTime - TimeUnit.DAYS.toMillis(1)

    private fun mediaSessionState(playbackState: MediaSession.PlaybackState): MediaSessionState =
        MediaSessionState(controller = mockk(), playbackState = playbackState)

    private fun createBrowserStore(tabIds: List<String>, selectedTabId: String? = null): BrowserStore =
        BrowserStore(
            initialState =
                BrowserState(
                    tabs = tabIds.map { createTab(url = "$URL/$it", id = it) },
                    selectedTabId = selectedTabId,
                )
        )

    private fun createRepository(browserStore: BrowserStore): DefaultTabRepository {
        val tabsUseCases = TabsUseCases(store = browserStore)
        return DefaultTabRepository(
            browserStore = browserStore,
            tabsUseCases = tabsUseCases,
            fenixBrowserUseCases =
                FenixBrowserUseCases(
                    appStore = AppStore(AppState()),
                    tabsUseCases = tabsUseCases,
                    loadUrlUseCase = SessionUseCases(browserStore).loadUrl,
                    searchUseCases = mockk(relaxed = true),
                    homepageTitle = "",
                    profiler = null,
                ),
        )
    }

    private fun TestScope.observeTabFlow(initialState: BrowserState): List<TabRepository.Data> =
        observeTabFlow(BrowserStore(initialState = initialState))

    private fun TestScope.observeTabFlow(browserStore: BrowserStore): List<TabRepository.Data> {
        val emissions = mutableListOf<TabRepository.Data>()
        backgroundScope.launch { createRepository(browserStore).tabFlow.toList(emissions) }
        runCurrent()

        return emissions
    }
}
