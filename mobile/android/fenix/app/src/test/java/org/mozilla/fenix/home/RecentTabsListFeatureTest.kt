/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.home

import io.mockk.mockk
import kotlin.test.assertIs
import kotlin.test.assertNotNull
import kotlinx.coroutines.test.StandardTestDispatcher
import kotlinx.coroutines.test.runTest
import mozilla.components.browser.state.action.ContentAction.UpdateIconAction
import mozilla.components.browser.state.action.ContentAction.UpdateTitleAction
import mozilla.components.browser.state.action.MediaSessionAction
import mozilla.components.browser.state.action.TabListAction
import mozilla.components.browser.state.state.BrowserState
import mozilla.components.browser.state.state.LastMediaAccessState
import mozilla.components.browser.state.state.createTab
import mozilla.components.browser.state.store.BrowserStore
import mozilla.components.concept.engine.mediasession.MediaSession
import mozilla.components.feature.media.middleware.LastMediaAccessMiddleware
import mozilla.components.feature.tabdata.coordinator.data.TabGroup
import mozilla.components.feature.tabdata.coordinator.data.TabsSnapshot
import mozilla.components.feature.tabdata.coordinator.fakes.FakeTabDataCoordinator
import mozilla.components.support.test.middleware.CaptureActionsMiddleware
import org.junit.After
import org.junit.Assert.assertEquals
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Before
import org.junit.Ignore
import org.junit.Test
import org.mozilla.fenix.components.AppStore
import org.mozilla.fenix.components.appstate.AppAction
import org.mozilla.fenix.components.appstate.AppState
import org.mozilla.fenix.home.recenttabs.RecentTab
import org.mozilla.fenix.home.recenttabs.RecentTabsListFeature

class RecentTabsListFeatureTest {

    private val testDispatcher = StandardTestDispatcher()
    private lateinit var appStore: AppStore
    private lateinit var middleware: CaptureActionsMiddleware<AppState, AppAction>
    private val tabDataCoordinator = FakeTabDataCoordinator(initialTabsSnapshot = TabsSnapshot())

    @Before
    fun setup() {
        middleware = CaptureActionsMiddleware()
        appStore = AppStore(middlewares = listOf(middleware))
    }

    @After
    fun teardown() {
        middleware.reset()
    }

    @Test
    fun `GIVEN no selected, last active or in progress media tab WHEN the feature starts THEN dispatch an empty list`() =
        runTest(testDispatcher) {
            val browserStore = BrowserStore()
            val appStore = AppStore()
            val feature =
                RecentTabsListFeature(
                    browserStore = browserStore,
                    appStore = appStore,
                    tabDataCoordinator = tabDataCoordinator,
                    mainDispatcher = testDispatcher,
                )

            feature.start()
            testDispatcher.scheduler.advanceUntilIdle()

            assertEquals(0, appStore.state.recentTabs.size)
        }

    @Test
    fun `GIVEN no selected but last active tab available WHEN the feature starts THEN dispatch the last active tab as a recent tab list`() =
        runTest(testDispatcher) {
            val tab =
                createTab(
                    url = "https://www.mozilla.org",
                    id = "1",
                )
            val tabs = listOf(tab)
            val browserStore = BrowserStore(BrowserState(tabs = tabs))
            val feature =
                RecentTabsListFeature(
                    browserStore = browserStore,
                    appStore = appStore,
                    tabDataCoordinator = tabDataCoordinator,
                    mainDispatcher = testDispatcher,
                )

            feature.start()
            testDispatcher.scheduler.advanceUntilIdle()
            testDispatcher.scheduler.advanceUntilIdle()
            assertEquals(1, appStore.state.recentTabs.size)
        }

    @Test
    fun `GIVEN a selected tab WHEN the feature starts THEN dispatch the selected tab as a recent tab list`() =
        runTest(testDispatcher) {
            val tab =
                createTab(
                    url = "https://www.mozilla.org",
                    id = "1",
                )
            val tabs = listOf(tab)
            val browserStore =
                BrowserStore(
                    BrowserState(
                        tabs = tabs,
                        selectedTabId = "1",
                    )
                )
            val feature =
                RecentTabsListFeature(
                    browserStore = browserStore,
                    appStore = appStore,
                    tabDataCoordinator = tabDataCoordinator,
                    mainDispatcher = testDispatcher,
                )

            feature.start()
            testDispatcher.scheduler.advanceUntilIdle()
            assertEquals(1, appStore.state.recentTabs.size)
        }

    @Test
    fun `GIVEN the last active tab is in a closed tab group WHEN the feature starts THEN dispatch the last active tab outside of it`() =
        runTest(testDispatcher) {
            val olderTab = createTab(url = "https://www.example.com", id = "0", lastAccess = 0)
            val tab = createTab(url = "https://www.mozilla.org", id = "1", lastAccess = 1)
            val closedTabGroupTab = createTab(url = "https://www.firefox.com", id = "2", lastAccess = 2)
            val browserStore = BrowserStore(BrowserState(tabs = listOf(olderTab, tab, closedTabGroupTab)))
            tabDataCoordinator.tabsSnapshot.value =
                TabsSnapshot(tabGroups = listOf(tabGroup(closed = true, tabIds = listOf(closedTabGroupTab.id))))
            val feature =
                RecentTabsListFeature(
                    browserStore = browserStore,
                    appStore = appStore,
                    tabDataCoordinator = tabDataCoordinator,
                    mainDispatcher = testDispatcher,
                )

            feature.start()
            testDispatcher.scheduler.advanceUntilIdle()

            assertEquals(listOf(RecentTab.Tab(tab)), appStore.state.recentTabs)
        }

    @Test
    fun `GIVEN the last active tab is in a closed tab group WHEN the tab group is opened THEN dispatch that tab as a recent tab list`() =
        runTest(testDispatcher) {
            val olderTab = createTab(url = "https://www.example.com", id = "0", lastAccess = 0)
            val tab = createTab(url = "https://www.mozilla.org", id = "1", lastAccess = 1)
            val tabGroupTab = createTab(url = "https://www.firefox.com", id = "2", lastAccess = 2)
            val browserStore = BrowserStore(BrowserState(tabs = listOf(olderTab, tab, tabGroupTab)))
            tabDataCoordinator.tabsSnapshot.value =
                TabsSnapshot(tabGroups = listOf(tabGroup(closed = true, tabIds = listOf(tabGroupTab.id))))
            val feature =
                RecentTabsListFeature(
                    browserStore = browserStore,
                    appStore = appStore,
                    tabDataCoordinator = tabDataCoordinator,
                    mainDispatcher = testDispatcher,
                )

            feature.start()
            testDispatcher.scheduler.advanceUntilIdle()
            assertEquals(listOf(RecentTab.Tab(tab)), appStore.state.recentTabs)

            tabDataCoordinator.openTabGroup(tabGroupId = "group")
            testDispatcher.scheduler.advanceUntilIdle()

            assertEquals(listOf(RecentTab.Tab(tabGroupTab)), appStore.state.recentTabs)
        }

    @Test
    fun `GIVEN the tab data coordinator is not initialized WHEN the feature starts THEN no recent tabs are dispatched until it is`() =
        runTest(testDispatcher) {
            val tab = createTab(url = "https://www.mozilla.org", id = "1")
            val browserStore = BrowserStore(BrowserState(tabs = listOf(tab), selectedTabId = tab.id))
            val tabDataCoordinator = FakeTabDataCoordinator(initialTabsSnapshot = null)
            val feature =
                RecentTabsListFeature(
                    browserStore = browserStore,
                    appStore = appStore,
                    tabDataCoordinator = tabDataCoordinator,
                    mainDispatcher = testDispatcher,
                )

            feature.start()
            testDispatcher.scheduler.advanceUntilIdle()
            assertTrue(appStore.state.recentTabs.isEmpty())

            tabDataCoordinator.tabsSnapshot.value = TabsSnapshot()
            testDispatcher.scheduler.advanceUntilIdle()

            assertEquals(listOf(RecentTab.Tab(tab)), appStore.state.recentTabs)
        }

    @Ignore("Disabled until we want to enable this feature. See #21670.")
    @Test
    fun `GIVEN a valid inProgressMediaTabId and another selected tab exists WHEN the feature starts THEN dispatch both as as a recent tabs list`() =
        runTest(testDispatcher) {
            val mediaTab =
                createTab(
                    url = "https://mozilla.com",
                    id = "42",
                    lastMediaAccessState = LastMediaAccessState("https://mozilla.com", 123),
                )
            val selectedTab = createTab("https://mozilla.com", id = "43")
            val browserStore =
                BrowserStore(
                    BrowserState(
                        tabs = listOf(mediaTab, selectedTab),
                        selectedTabId = "43",
                    )
                )
            val feature =
                RecentTabsListFeature(
                    browserStore = browserStore,
                    appStore = appStore,
                    tabDataCoordinator = tabDataCoordinator,
                    mainDispatcher = testDispatcher,
                )

            feature.start()
            testDispatcher.scheduler.advanceUntilIdle()

            assertEquals(2, appStore.state.recentTabs.size)
            assertIs<RecentTab.Tab>(appStore.state.recentTabs[0])
            assertEquals(selectedTab, (appStore.state.recentTabs[0] as RecentTab.Tab).state)
            assertIs<RecentTab.Tab>(appStore.state.recentTabs[1])
            assertEquals(mediaTab, (appStore.state.recentTabs[1] as RecentTab.Tab).state)
        }

    @Ignore("Disabled until we want to enable this feature. See #21670.")
    @Test
    fun `GIVEN a valid inProgressMediaTabId exists and that is the selected tab WHEN the feature starts THEN dispatch just one tab as the recent tabs list`() =
        runTest(testDispatcher) {
            val selectedMediaTab =
                createTab(
                    "https://mozilla.com",
                    id = "42",
                    lastMediaAccessState = LastMediaAccessState("https://mozilla.com", 123),
                )
            val browserStore =
                BrowserStore(
                    BrowserState(
                        tabs = listOf(selectedMediaTab),
                        selectedTabId = "42",
                    )
                )
            val feature =
                RecentTabsListFeature(
                    browserStore = browserStore,
                    appStore = appStore,
                    tabDataCoordinator = tabDataCoordinator,
                    mainDispatcher = testDispatcher,
                )

            feature.start()
            testDispatcher.scheduler.advanceUntilIdle()

            assertEquals(1, appStore.state.recentTabs.size)
            assertIs<RecentTab.Tab>(appStore.state.recentTabs[0])
            assertEquals(selectedMediaTab, (appStore.state.recentTabs[0] as RecentTab.Tab).state)
        }

    @Test
    fun `WHEN the browser state has an updated select tab THEN dispatch the new recent tab list`() =
        runTest(testDispatcher) {
            val tab1 =
                createTab(
                    url = "https://www.mozilla.org",
                    id = "1",
                )
            val tab2 =
                createTab(
                    url = "https://www.firefox.com",
                    id = "2",
                )
            val tabs = listOf(tab1, tab2)
            val browserStore =
                BrowserStore(
                    BrowserState(
                        tabs = tabs,
                        selectedTabId = "1",
                    )
                )
            val feature =
                RecentTabsListFeature(
                    browserStore = browserStore,
                    appStore = appStore,
                    tabDataCoordinator = tabDataCoordinator,
                    mainDispatcher = testDispatcher,
                )

            feature.start()
            testDispatcher.scheduler.advanceUntilIdle()
            assertEquals(1, appStore.state.recentTabs.size)
            assertIs<RecentTab.Tab>(appStore.state.recentTabs[0])
            assertEquals(tab1, (appStore.state.recentTabs[0] as RecentTab.Tab).state)

            browserStore.dispatch(TabListAction.SelectTabAction(tab2.id))
            testDispatcher.scheduler.advanceUntilIdle()
            assertEquals(1, appStore.state.recentTabs.size)
            assertIs<RecentTab.Tab>(appStore.state.recentTabs[0])
            assertEquals(tab2, (appStore.state.recentTabs[0] as RecentTab.Tab).state)
        }

    @Ignore("Disabled until we want to enable this feature. See #21670.")
    @Test
    fun `WHEN the browser state has an in progress media tab THEN dispatch the new recent tab list`() =
        runTest(testDispatcher) {
            val initialMediaTab =
                createTab(
                    url = "https://mozilla.com",
                    id = "1",
                    lastMediaAccessState = LastMediaAccessState("https://mozilla.com", 123),
                )
            val newMediaTab =
                createTab(
                    url = "http://mozilla.org",
                    id = "2",
                    lastMediaAccessState = LastMediaAccessState("https://mozilla.com", 100),
                )
            val browserStore =
                BrowserStore(
                    initialState =
                        BrowserState(
                            tabs = listOf(initialMediaTab, newMediaTab),
                            selectedTabId = "1",
                        ),
                    middleware = listOf(LastMediaAccessMiddleware()),
                )
            val feature =
                RecentTabsListFeature(
                    browserStore = browserStore,
                    appStore = appStore,
                    tabDataCoordinator = tabDataCoordinator,
                    mainDispatcher = testDispatcher,
                )

            feature.start()
            testDispatcher.scheduler.advanceUntilIdle()
            assertEquals(2, appStore.state.recentTabs.size)
            assertIs<RecentTab.Tab>(appStore.state.recentTabs[0])
            assertEquals(initialMediaTab, (appStore.state.recentTabs[0] as RecentTab.Tab).state)

            browserStore.dispatch(
                MediaSessionAction.UpdateMediaPlaybackStateAction(
                    "2",
                    MediaSession.PlaybackState.PLAYING,
                )
            )
            testDispatcher.scheduler.advanceUntilIdle()
            assertEquals(2, appStore.state.recentTabs.size)
            assertIs<RecentTab.Tab>(appStore.state.recentTabs[0])
            assertEquals(initialMediaTab, (appStore.state.recentTabs[0] as RecentTab.Tab).state)
            // UpdateMediaPlaybackStateAction would set the current timestamp as the new value for lastMediaAccess
            val updatedLastMediaAccess =
                (appStore.state.recentTabs[1] as RecentTab.Tab).state.lastMediaAccessState.lastMediaAccess
            assertTrue(
                "expected lastMediaAccess ($updatedLastMediaAccess) > 100",
                updatedLastMediaAccess > 100,
            )
            assertEquals(
                "http://mozilla.org",
                (appStore.state.recentTabs[1] as RecentTab.Tab).state.lastMediaAccessState.lastMediaUrl,
            )
            // Check that the media tab is updated ignoring just the lastMediaAccess property.
            assertEquals(
                newMediaTab,
                (appStore.state.recentTabs[1] as RecentTab.Tab)
                    .state
                    .copy(lastMediaAccessState = LastMediaAccessState("https://mozilla.com", 100)),
            )
        }

    @Test
    fun `WHEN the browser state selects a private tab THEN dispatch an empty list`() =
        runTest(testDispatcher) {
            val selectedNormalTab =
                createTab(
                    url = "https://www.mozilla.org",
                    id = "1",
                    lastAccess = 0,
                )
            val lastAccessedNormalTab =
                createTab(
                    url = "https://www.mozilla.org",
                    id = "2",
                    lastAccess = 1,
                )
            val privateTab =
                createTab(
                    url = "https://www.firefox.com",
                    id = "3",
                    private = true,
                )
            val tabs = listOf(selectedNormalTab, lastAccessedNormalTab, privateTab)
            val browserStore =
                BrowserStore(
                    BrowserState(
                        tabs = tabs,
                        selectedTabId = "1",
                    )
                )
            val feature =
                RecentTabsListFeature(
                    browserStore = browserStore,
                    appStore = appStore,
                    tabDataCoordinator = tabDataCoordinator,
                    mainDispatcher = testDispatcher,
                )

            feature.start()
            testDispatcher.scheduler.advanceUntilIdle()
            assertEquals(1, appStore.state.recentTabs.size)
            assertIs<RecentTab.Tab>(appStore.state.recentTabs[0])
            assertEquals(selectedNormalTab, (appStore.state.recentTabs[0] as RecentTab.Tab).state)

            browserStore.dispatch(TabListAction.SelectTabAction(privateTab.id))
            testDispatcher.scheduler.advanceUntilIdle()

            // If the selected tab is a private tab the feature should show the last accessed normal tab.
            assertEquals(1, appStore.state.recentTabs.size)
            assertIs<RecentTab.Tab>(appStore.state.recentTabs[0])
            assertEquals(
                lastAccessedNormalTab,
                (appStore.state.recentTabs[0] as RecentTab.Tab).state,
            )
        }

    @Test
    fun `WHEN the selected tabs title or icon update THEN update the home store`() =
        runTest(testDispatcher) {
            val browserStore =
                BrowserStore(
                    BrowserState(
                        tabs =
                            listOf(
                                createTab(
                                    url = "https://www.mozilla.org",
                                    id = "1",
                                )
                            ),
                        selectedTabId = "1",
                    )
                )
            val feature =
                RecentTabsListFeature(
                    browserStore = browserStore,
                    appStore = appStore,
                    tabDataCoordinator = tabDataCoordinator,
                    mainDispatcher = testDispatcher,
                )

            feature.start()
            testDispatcher.scheduler.advanceUntilIdle()
            middleware.assertLastAction(AppAction.RecentTabsChange::class) {
                val tab = it.recentTabs.first() as RecentTab.Tab
                assertTrue(tab.state.content.title.isEmpty())
                assertNull(tab.state.content.icon)
            }

            browserStore.dispatch(UpdateTitleAction("1", "test"))
            testDispatcher.scheduler.advanceUntilIdle()
            middleware.assertLastAction(AppAction.RecentTabsChange::class) {
                val tab = it.recentTabs.first() as RecentTab.Tab
                assertEquals("test", tab.state.content.title)
                assertNull(tab.state.content.icon)
            }

            browserStore.dispatch(UpdateIconAction("1", "https://www.mozilla.org", mockk()))

            testDispatcher.scheduler.advanceUntilIdle()
            middleware.assertLastAction(AppAction.RecentTabsChange::class) {
                val tab = it.recentTabs.first() as RecentTab.Tab
                assertEquals("test", tab.state.content.title)
                assertNotNull(tab.state.content.icon)
            }
        }

    @Test
    fun `GIVEN inProgressMediaTab already set WHEN the media tab is closed THEN remove it from recent tabs`() =
        runTest(testDispatcher) {
            val initialMediaTab = createTab(url = "https://mozilla.com", id = "1")
            val selectedTab = createTab(url = "https://mozilla.com/firefox", id = "2")
            val browserStore =
                BrowserStore(
                    initialState =
                        BrowserState(
                            listOf(initialMediaTab, selectedTab),
                            selectedTabId = "2",
                        ),
                    middleware = listOf(LastMediaAccessMiddleware()),
                )
            val feature =
                RecentTabsListFeature(
                    browserStore = browserStore,
                    appStore = appStore,
                    tabDataCoordinator = tabDataCoordinator,
                    mainDispatcher = testDispatcher,
                )

            feature.start()
            testDispatcher.scheduler.advanceUntilIdle()
            browserStore.dispatch(TabListAction.RemoveTabsAction(listOf("1")))
            testDispatcher.scheduler.advanceUntilIdle()
            assertEquals(1, appStore.state.recentTabs.size)
            assertIs<RecentTab.Tab>(appStore.state.recentTabs[0])
            assertEquals(selectedTab, (appStore.state.recentTabs[0] as RecentTab.Tab).state)
        }

    private fun tabGroup(closed: Boolean, tabIds: List<String>) =
        TabGroup(id = "group", title = "Group", theme = "Yellow", closed = closed, lastModified = 0L, tabIds = tabIds)
}
