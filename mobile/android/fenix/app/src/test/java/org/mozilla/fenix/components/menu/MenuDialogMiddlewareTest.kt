/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.components.menu

import android.app.PendingIntent
import android.content.Intent
import io.mockk.coEvery
import io.mockk.coVerify
import io.mockk.every
import io.mockk.mockk
import io.mockk.spyk
import io.mockk.verify
import kotlin.test.assertNotNull
import kotlinx.coroutines.ExperimentalCoroutinesApi
import kotlinx.coroutines.launch
import kotlinx.coroutines.runBlocking
import kotlinx.coroutines.test.StandardTestDispatcher
import kotlinx.coroutines.test.TestScope
import kotlinx.coroutines.test.UnconfinedTestDispatcher
import kotlinx.coroutines.test.runTest
import mozilla.appservices.places.BookmarkRoot
import mozilla.components.browser.state.state.createCustomTab
import mozilla.components.browser.state.state.createTab
import mozilla.components.concept.engine.EngineSession
import mozilla.components.concept.engine.webextension.InstallationMethod
import mozilla.components.feature.addons.Addon
import mozilla.components.feature.addons.AddonManager
import mozilla.components.feature.app.links.AppLinkRedirect
import mozilla.components.feature.app.links.AppLinksUseCases
import mozilla.components.feature.session.SessionUseCases
import mozilla.components.feature.tabs.TabsUseCases
import mozilla.components.feature.top.sites.PinnedSiteStorage
import mozilla.components.feature.top.sites.TopSite
import mozilla.components.feature.top.sites.TopSitesUseCases
import mozilla.components.support.test.fakes.engine.TestEngineSession
import mozilla.components.support.test.middleware.CaptureActionsMiddleware
import mozilla.components.support.test.robolectric.testContext
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Before
import org.junit.Test
import org.junit.runner.RunWith
import org.mozilla.fenix.components.AppStore
import org.mozilla.fenix.components.appstate.AppAction
import org.mozilla.fenix.components.appstate.AppAction.BookmarkAction
import org.mozilla.fenix.components.appstate.AppAction.FindInPageAction
import org.mozilla.fenix.components.appstate.AppAction.ReaderViewAction
import org.mozilla.fenix.components.appstate.AppAction.ShortcutAction
import org.mozilla.fenix.components.appstate.AppState
import org.mozilla.fenix.components.bookmarks.BookmarksUseCase.AddBookmarksUseCase
import org.mozilla.fenix.components.bookmarks.LastSavedFolderCache
import org.mozilla.fenix.components.menu.fake.FakeBookmarksStorage
import org.mozilla.fenix.components.menu.middleware.MenuDialogMiddleware
import org.mozilla.fenix.components.menu.store.BrowserMenuState
import org.mozilla.fenix.components.menu.store.MenuAction
import org.mozilla.fenix.components.menu.store.MenuState
import org.mozilla.fenix.components.menu.store.MenuStore
import org.mozilla.fenix.components.menu.store.NavigationEvent
import org.mozilla.fenix.home.topsites.AddShortcutEntryPoint
import org.mozilla.fenix.home.topsites.AddShortcutSource
import org.mozilla.fenix.home.topsites.TopSitesConfigConstants.TOP_SITES_MAX_COUNT
import org.mozilla.fenix.settings.summarize.FakeSummarizationFeatureConfiguration
import org.mozilla.fenix.summarization.eligibility.SummarizationEligibilityChecker
import org.mozilla.fenix.utils.Settings
import org.robolectric.RobolectricTestRunner

@RunWith(RobolectricTestRunner::class)
class MenuDialogMiddlewareTest {

    private val testDispatcher = StandardTestDispatcher()

    private val bookmarksStorage = FakeBookmarksStorage()
    private lateinit var addBookmarkUseCase: AddBookmarksUseCase

    private val addonManager: AddonManager = mockk(relaxed = true)
    private val navigationEvents = mutableListOf<NavigationEvent>()

    private lateinit var pinnedSiteStorage: PinnedSiteStorage
    private lateinit var addPinnedSiteUseCase: TopSitesUseCases.AddPinnedSiteUseCase
    private lateinit var removePinnedSiteUseCase: TopSitesUseCases.RemoveTopSiteUseCase
    private lateinit var appLinksUseCases: AppLinksUseCases
    private lateinit var requestDesktopSiteUseCase: SessionUseCases.RequestDesktopSiteUseCase
    private lateinit var migratePrivateTabUseCase: TabsUseCases.MigratePrivateTabUseCase
    private lateinit var settings: Settings

    private val summarizeFeatureSettings = FakeSummarizationFeatureConfiguration()
    private lateinit var lastSavedFolderCache: LastSavedFolderCache

    @Before
    fun setup() {
        pinnedSiteStorage = mockk(relaxUnitFun = true)
        addPinnedSiteUseCase = mockk(relaxUnitFun = true)
        removePinnedSiteUseCase = mockk(relaxUnitFun = true)
        appLinksUseCases = mockk()
        requestDesktopSiteUseCase = mockk(relaxUnitFun = true)
        migratePrivateTabUseCase = mockk(relaxed = true)
        lastSavedFolderCache = mockk(relaxed = true)
        addBookmarkUseCase =
            spyk(
                AddBookmarksUseCase(
                    storage = bookmarksStorage,
                    lastSavedFolderCache = lastSavedFolderCache,
                )
            )

        settings = Settings(testContext)

        runBlocking {
            coEvery { pinnedSiteStorage.getPinnedSites() } returns emptyList()
            coEvery { addonManager.getAddons() } returns emptyList()
        }
    }

    @Test
    fun `GIVEN no selected tab WHEN init action is dispatched THEN browser state is not updated`() =
        runTest(testDispatcher) {
            val store = createStore(menuState = MenuState(browserMenuState = null))
            testScheduler.advanceUntilIdle()

            assertNull(store.state.browserMenuState)
        }

    @Test
    fun `GIVEN selected tab is bookmarked WHEN init action is dispatched THEN initial bookmark state is updated`() =
        runTest(testDispatcher) {
            val url = "https://www.mozilla.org"
            val title = "Mozilla"

            val guid =
                bookmarksStorage.addItem(
                    parentGuid = BookmarkRoot.Mobile.id,
                    url = url,
                    title = title,
                    position = 5u,
                )

            val browserMenuState =
                BrowserMenuState(
                    selectedTab =
                        createTab(
                            url = url,
                            title = title,
                        )
                )
            val store = createStore(menuState = MenuState(browserMenuState = browserMenuState))
            testScheduler.advanceUntilIdle()

            assertEquals(guid.getOrNull()!!, store.state.browserMenuState!!.bookmarkState.guid)
            assertTrue(store.state.browserMenuState!!.bookmarkState.isBookmarked)
        }

    @Test
    fun `GIVEN selected tab is not bookmarked WHEN init action is dispatched THEN initial bookmark state is not updated`() =
        runTest(testDispatcher) {
            val url = "https://www.mozilla.org"
            val title = "Mozilla"
            val browserMenuState =
                BrowserMenuState(
                    selectedTab =
                        createTab(
                            url = url,
                            title = title,
                        )
                )
            val store = createStore(menuState = MenuState(browserMenuState = browserMenuState))
            testScheduler.advanceUntilIdle()

            assertNull(store.state.browserMenuState!!.bookmarkState.guid)
            assertFalse(store.state.browserMenuState!!.bookmarkState.isBookmarked)
        }

    @Test
    fun `GIVEN recommended addons are available WHEN init action is dispatched THEN initial extension state is updated`() =
        runTest(testDispatcher) {
            val addon = Addon(id = "ext1")
            coEvery { addonManager.getAddons() } returns listOf(addon)

            val store = createStore()
            testScheduler.advanceUntilIdle()

            assertTrue(store.state.extensionMenuState.availableAddons.isEmpty())
            assertEquals(1, store.state.extensionMenuState.recommendedAddons.size)
            assertEquals(addon, store.state.extensionMenuState.recommendedAddons.first())
        }

    @Test
    fun `GIVEN recommended addons are available WHEN init action is dispatched THEN initial extension state is updated and shows maximum three recommended addons`() =
        runTest(testDispatcher) {
            val addon = Addon(id = "ext1")
            val addonTwo = Addon(id = "ext2")
            val addonThree = Addon(id = "ext3")
            val addonFour = Addon(id = "ext4")
            val addonFive = Addon(id = "ext5")
            coEvery { addonManager.getAddons() } returns listOf(addon, addonTwo, addonThree, addonFour, addonFive)

            val store = createStore()
            testScheduler.advanceUntilIdle()

            assertTrue(store.state.extensionMenuState.availableAddons.isEmpty())
            assertEquals(3, store.state.extensionMenuState.recommendedAddons.size)
        }

    @Test
    fun `GIVEN at least one addon is installed WHEN init action is dispatched THEN initial extension state is updated`() =
        runTest(testDispatcher) {
            val addon = Addon(id = "ext1")
            val addonTwo =
                Addon(
                    id = "ext2",
                    installedState =
                        Addon.InstalledState(
                            id = "id",
                            version = "1.0",
                            enabled = true,
                            optionsPageUrl = "",
                        ),
                )
            val addonThree = Addon(id = "ext3")
            coEvery { addonManager.getAddons() } returns listOf(addon, addonTwo, addonThree)

            val store = createStore()
            testScheduler.advanceUntilIdle()

            assertEquals(1, store.state.extensionMenuState.availableAddons.size)
            assertTrue(store.state.extensionMenuState.recommendedAddons.isEmpty())
        }

    @Test
    fun `GIVEN at least one addon is installed and not enabled WHEN init action is dispatched THEN initial extension state is updated`() =
        runTest(testDispatcher) {
            val addon =
                Addon(
                    id = "ext",
                    installedState =
                        Addon.InstalledState(
                            id = "id",
                            version = "1.0",
                            enabled = false,
                            optionsPageUrl = "",
                        ),
                )

            coEvery { addonManager.getAddons() } returns listOf(addon)

            val store = createStore()
            testScheduler.advanceUntilIdle()

            assertTrue(store.state.extensionMenuState.availableAddons.isEmpty())
            assertTrue(store.state.extensionMenuState.recommendedAddons.isEmpty())
        }

    @Test
    fun `WHEN add bookmark action is dispatched for a selected tab THEN use case is invoked without an explicit parent and BookmarkAdded is dispatched`() =
        runTest(testDispatcher) {
            val url = "https://www.mozilla.org"
            val title = "Mozilla"

            val browserMenuState =
                BrowserMenuState(
                    selectedTab =
                        createTab(
                            url = url,
                            title = title,
                        )
                )
            val captureMiddleware = CaptureActionsMiddleware<AppState, AppAction>()
            val appStore = AppStore(middlewares = listOf(captureMiddleware))
            val store =
                createStore(
                    appStore = appStore,
                    menuState = MenuState(browserMenuState = browserMenuState),
                )
            testScheduler.advanceUntilIdle()

            coEvery { lastSavedFolderCache.getGuid() } returns null

            store.dispatch(MenuAction.AddBookmark)
            testScheduler.advanceUntilIdle()

            coVerify { addBookmarkUseCase.invoke(url = url, title = title) }
            captureMiddleware.assertLastAction(BookmarkAction.BookmarkAdded::class) {
                action: BookmarkAction.BookmarkAdded ->
                assertNotNull(action.guidToEdit)
            }
            assertTrue(navigationEvents.contains(NavigationEvent.Dismiss))
        }

    @Test
    fun `GIVEN selected tab is bookmarked WHEN add bookmark action is dispatched THEN add bookmark use case is never called`() =
        runTest(testDispatcher) {
            val url = "https://www.mozilla.org"
            val title = "Mozilla"

            val guid =
                bookmarksStorage.addItem(
                    parentGuid = BookmarkRoot.Mobile.id,
                    url = url,
                    title = title,
                    position = 5u,
                )

            val browserMenuState =
                BrowserMenuState(
                    selectedTab =
                        createTab(
                            url = url,
                            title = title,
                        )
                )
            val captureMiddleware = CaptureActionsMiddleware<AppState, AppAction>()
            val appStore = AppStore(middlewares = listOf(captureMiddleware))
            val store =
                createStore(
                    appStore = appStore,
                    menuState = MenuState(browserMenuState = browserMenuState),
                )
            testScheduler.advanceUntilIdle()

            assertEquals(guid.getOrNull()!!, store.state.browserMenuState!!.bookmarkState.guid)
            assertTrue(store.state.browserMenuState!!.bookmarkState.isBookmarked)

            store.dispatch(MenuAction.AddBookmark)
            testScheduler.advanceUntilIdle()

            coVerify(exactly = 0) { addBookmarkUseCase.invoke(url = url, title = title) }
            captureMiddleware.assertNotDispatched(BookmarkAction.BookmarkAdded::class)
            assertFalse(navigationEvents.contains(NavigationEvent.Dismiss))
        }

    @Test
    fun `GIVEN selected tab is pinned WHEN init action is dispatched THEN initial pinned state is updated`() =
        runTest(testDispatcher) {
            val url = "https://www.mozilla.org"
            val title = "Mozilla"

            coEvery { pinnedSiteStorage.getPinnedSites() } returns
                listOf(TopSite.Pinned(id = 0, title = title, url = url, createdAt = 0))

            val browserMenuState =
                BrowserMenuState(
                    selectedTab =
                        createTab(
                            url = url,
                            title = title,
                        )
                )
            val store = createStore(menuState = MenuState(browserMenuState = browserMenuState))
            testScheduler.advanceUntilIdle()

            assertTrue(store.state.browserMenuState!!.isPinned)
        }

    @Test
    fun `GIVEN selected tab is not pinned WHEN init action is dispatched THEN initial pinned state is not updated`() =
        runTest(testDispatcher) {
            val url = "https://www.mozilla.org"
            val title = "Mozilla"

            val browserMenuState =
                BrowserMenuState(
                    selectedTab =
                        createTab(
                            url = url,
                            title = title,
                        )
                )
            val store = createStore(menuState = MenuState(browserMenuState = browserMenuState))
            testScheduler.advanceUntilIdle()

            assertFalse(store.state.browserMenuState!!.isPinned)
        }

    @Test
    fun `GIVEN selected tab is a custom tab WHEN init action is dispatched THEN initial pinned state is not updated`() =
        runTest(testDispatcher) {
            val url = "https://www.mozilla.org"
            val title = "Mozilla"

            coEvery { pinnedSiteStorage.getPinnedSites() } returns
                listOf(TopSite.Pinned(id = 0, title = title, url = url, createdAt = 0))

            val browserMenuState =
                BrowserMenuState(
                    selectedTab =
                        createCustomTab(
                            url = url,
                            title = title,
                        )
                )
            val store = createStore(menuState = MenuState(browserMenuState = browserMenuState))
            testScheduler.advanceUntilIdle()

            assertFalse(store.state.browserMenuState!!.isPinned)
        }

    @Test
    fun `WHEN add to shortcuts action is dispatched for a selected tab THEN the site is pinned`() =
        runTest(testDispatcher) {
            val url = "https://www.mozilla.org"
            val title = "Mozilla"

            val browserMenuState =
                BrowserMenuState(
                    selectedTab =
                        createTab(
                            url = url,
                            title = title,
                        )
                )
            val appStore = spyk(AppStore())
            val store =
                createStore(
                    appStore = appStore,
                    menuState = MenuState(browserMenuState = browserMenuState),
                )
            testScheduler.advanceUntilIdle()

            store.dispatch(MenuAction.AddShortcut)
            testScheduler.advanceUntilIdle()

            coVerify { addPinnedSiteUseCase.invoke(url = url, title = title) }
            verify {
                appStore.dispatch(
                    ShortcutAction.ShortcutAdded(
                        url = url,
                        source = AddShortcutSource.MANUAL,
                        entryPoint = AddShortcutEntryPoint.PAGE_MENU,
                    )
                )
            }
            assertTrue(navigationEvents.contains(NavigationEvent.Dismiss))
        }

    @Test
    fun `GIVEN there are TOP_SITES_MAX_COUNT number of pinned sites WHEN add to shortcuts action is dispatched THEN the site is still pinned`() =
        runTest(testDispatcher) {
            val url = "https://www.mozilla.org"
            val title = "Mozilla"

            coEvery { pinnedSiteStorage.getPinnedSites() } returns
                List(TOP_SITES_MAX_COUNT) { index ->
                    TopSite.Pinned(id = index.toLong(), title = title, url = "$url/$index", createdAt = 0)
                }

            val browserMenuState =
                BrowserMenuState(
                    selectedTab =
                        createTab(
                            url = url,
                            title = title,
                        )
                )
            val appStore = spyk(AppStore())
            val store =
                createStore(
                    appStore = appStore,
                    menuState = MenuState(browserMenuState = browserMenuState),
                )
            testScheduler.advanceUntilIdle()

            assertFalse(store.state.browserMenuState!!.isPinned)

            store.dispatch(MenuAction.AddShortcut)
            testScheduler.advanceUntilIdle()

            coVerify { addPinnedSiteUseCase.invoke(url = url, title = title) }
            verify {
                appStore.dispatch(
                    ShortcutAction.ShortcutAdded(
                        url = url,
                        source = AddShortcutSource.MANUAL,
                        entryPoint = AddShortcutEntryPoint.PAGE_MENU,
                    )
                )
            }
            assertTrue(navigationEvents.contains(NavigationEvent.Dismiss))
        }

    @Test
    fun `GIVEN selected tab is pinned WHEN add to shortcuts action is dispatched THEN add pinned site use case is never called`() =
        runTest(testDispatcher) {
            val url = "https://www.mozilla.org"
            val title = "Mozilla"

            coEvery { pinnedSiteStorage.getPinnedSites() } returns
                listOf(
                    TopSite.Pinned(
                        id = 0,
                        title = title,
                        url = url,
                        createdAt = 0,
                    )
                )

            pinnedSiteStorage.addPinnedSite(
                url = url,
                title = title,
            )

            val browserMenuState =
                BrowserMenuState(
                    selectedTab =
                        createTab(
                            url = url,
                            title = title,
                        )
                )
            val appStore = spyk(AppStore())
            val store =
                createStore(
                    appStore = appStore,
                    menuState = MenuState(browserMenuState = browserMenuState),
                )
            testScheduler.advanceUntilIdle()

            assertTrue(store.state.browserMenuState!!.isPinned)

            store.dispatch(MenuAction.AddShortcut)
            testScheduler.advanceUntilIdle()

            coVerify(exactly = 0) { addPinnedSiteUseCase.invoke(url = url, title = title) }
            verify(exactly = 0) {
                appStore.dispatch(
                    ShortcutAction.ShortcutAdded(
                        url = url,
                        source = AddShortcutSource.MANUAL,
                        entryPoint = AddShortcutEntryPoint.PAGE_MENU,
                    )
                )
            }
            assertFalse(navigationEvents.contains(NavigationEvent.Dismiss))
        }

    @Test
    fun `WHEN remove from shortcuts action is dispatched for a selected tab THEN remove pinned site use case is never called`() =
        runTest(testDispatcher) {
            val url = "https://www.mozilla.org"
            val title = "Mozilla"

            val topSite =
                TopSite.Pinned(
                    id = 0,
                    title = title,
                    url = url,
                    createdAt = 0,
                )
            val browserMenuState =
                BrowserMenuState(
                    selectedTab =
                        createTab(
                            url = url,
                            title = title,
                        )
                )
            val appStore = spyk(AppStore())
            val store =
                createStore(
                    appStore = appStore,
                    menuState = MenuState(browserMenuState = browserMenuState),
                )
            testScheduler.advanceUntilIdle()

            assertFalse(store.state.browserMenuState!!.isPinned)

            store.dispatch(MenuAction.RemoveShortcut)
            testScheduler.advanceUntilIdle()

            coVerify(exactly = 0) { removePinnedSiteUseCase.invoke(topSite = topSite) }
            assertFalse(navigationEvents.contains(NavigationEvent.Dismiss))
        }

    @Test
    fun `GIVEN selected tab is pinned WHEN remove from shortcuts action is dispatched THEN pinned state is updated`() =
        runTest(testDispatcher) {
            val url = "https://www.mozilla.org"
            val title = "Mozilla"
            val topSite =
                TopSite.Pinned(
                    id = 0,
                    title = title,
                    url = url,
                    createdAt = 0,
                )

            coEvery { pinnedSiteStorage.getPinnedSites() } returns listOf(topSite)

            val browserMenuState =
                BrowserMenuState(
                    selectedTab =
                        createTab(
                            url = url,
                            title = title,
                        )
                )
            val appStore = spyk(AppStore())
            val store =
                createStore(
                    appStore = appStore,
                    menuState = MenuState(browserMenuState = browserMenuState),
                )
            testScheduler.advanceUntilIdle()

            assertTrue(store.state.browserMenuState!!.isPinned)

            store.dispatch(MenuAction.RemoveShortcut)
            testScheduler.advanceUntilIdle()

            coVerify { removePinnedSiteUseCase.invoke(topSite = topSite) }
            assertTrue(navigationEvents.contains(NavigationEvent.Dismiss))
        }

    @Test
    fun `GIVEN selected tab has external app WHEN open in app action is dispatched THEN the site is opened in app`() =
        runTest(testDispatcher) {
            val url = "https://www.mozilla.org"
            val title = "Mozilla"

            val browserMenuState =
                BrowserMenuState(
                    selectedTab =
                        createTab(
                            url = url,
                            title = title,
                        )
                )
            val store = createStore(menuState = MenuState(browserMenuState = browserMenuState))
            testScheduler.advanceUntilIdle()

            val getRedirect: AppLinksUseCases.GetAppLinkRedirect = mockk()
            every { appLinksUseCases.appLinkRedirect } returns getRedirect

            val redirect: AppLinkRedirect = mockk()
            every { getRedirect.invoke(url) } returns redirect
            every { redirect.hasExternalApp() } returns true

            val intent: Intent = mockk(relaxed = true)
            every { redirect.appIntent } returns intent

            val openAppLinkRedirect: AppLinksUseCases.OpenAppLinkRedirect = mockk(relaxUnitFun = true)
            every { appLinksUseCases.openAppLink } returns openAppLinkRedirect

            store.dispatch(MenuAction.OpenInApp)
            testScheduler.advanceUntilIdle()

            verify { openAppLinkRedirect.invoke(appIntent = intent) }
            assertTrue(navigationEvents.contains(NavigationEvent.Dismiss))
        }

    @Test
    fun `GIVEN selected tab does not have external app WHEN open in app action is dispatched THEN the site is not opened in app`() =
        runTest(testDispatcher) {
            val url = "https://www.mozilla.org"
            val title = "Mozilla"

            val browserMenuState =
                BrowserMenuState(
                    selectedTab =
                        createTab(
                            url = url,
                            title = title,
                        )
                )
            val store = createStore(menuState = MenuState(browserMenuState = browserMenuState))
            testScheduler.advanceUntilIdle()

            val getRedirect: AppLinksUseCases.GetAppLinkRedirect = mockk()
            every { appLinksUseCases.appLinkRedirect } returns getRedirect

            val redirect: AppLinkRedirect = mockk()
            every { getRedirect.invoke(url) } returns redirect
            every { redirect.hasExternalApp() } returns false

            val intent: Intent = mockk()
            val openAppLinkRedirect: AppLinksUseCases.OpenAppLinkRedirect = mockk()

            store.dispatch(MenuAction.OpenInApp)
            testScheduler.advanceUntilIdle()

            verify(exactly = 0) { openAppLinkRedirect.invoke(appIntent = intent) }
            assertFalse(navigationEvents.contains(NavigationEvent.Dismiss))
        }

    @Test
    fun `WHEN install addon action is dispatched THEN addon is installed`() =
        runTest(testDispatcher) {
            val addon = Addon(id = "ext1", downloadUrl = "downloadUrl")
            val store = createStore()
            testScheduler.advanceUntilIdle()

            store.dispatch(MenuAction.InstallAddon(addon))
            testScheduler.advanceUntilIdle()

            verify {
                addonManager.installAddon(
                    url = eq(addon.downloadUrl),
                    installationMethod = eq(InstallationMethod.MANAGER),
                    onSuccess = any(),
                    onError = any(),
                )
            }

            assertEquals(store.state.extensionMenuState.addonInstallationInProgress, addon)
        }

    @Test
    fun `WHEN customize reader view action is dispatched THEN reader view action is dispatched`() =
        runTest(testDispatcher) {
            val appStore = spyk(AppStore())
            val store =
                createStore(
                    appStore = appStore,
                    menuState = MenuState(),
                )
            testScheduler.advanceUntilIdle()

            store.dispatch(MenuAction.CustomizeReaderView)
            testScheduler.advanceUntilIdle()

            verify { appStore.dispatch(ReaderViewAction.ReaderViewControlsShown) }
            assertTrue(navigationEvents.contains(NavigationEvent.Dismiss))
        }

    @Test
    fun `WHEN open in Firefox action is dispatched for a custom tab THEN the tab is opened in the browser`() =
        runTest(testDispatcher) {
            val url = "https://www.mozilla.org"
            val title = "Mozilla"

            val browserMenuState =
                BrowserMenuState(
                    selectedTab =
                        createTab(
                            url = url,
                            title = title,
                        )
                )
            val appStore = spyk(AppStore())
            val store =
                createStore(
                    appStore = appStore,
                    menuState = MenuState(browserMenuState = browserMenuState),
                )
            testScheduler.advanceUntilIdle()

            store.dispatch(MenuAction.OpenInFirefox)
            testScheduler.advanceUntilIdle()

            verify { appStore.dispatch(AppAction.OpenInFirefoxStarted) }
            assertTrue(navigationEvents.contains(NavigationEvent.Dismiss))
        }

    @Test
    fun `WHEN find in page action is dispatched THEN find in page app action is dispatched`() =
        runTest(testDispatcher) {
            val url = "https://www.mozilla.org"
            val title = "Mozilla"

            val browserMenuState =
                BrowserMenuState(
                    selectedTab =
                        createTab(
                            url = url,
                            title = title,
                        )
                )
            val appStore = spyk(AppStore())
            val store =
                spyk(
                    createStore(
                        appStore = appStore,
                        menuState = MenuState(browserMenuState = browserMenuState),
                    )
                )
            testScheduler.advanceUntilIdle()

            store.dispatch(MenuAction.FindInPage)
            testScheduler.advanceUntilIdle()

            verify { appStore.dispatch(FindInPageAction.FindInPageStarted) }
            assertTrue(navigationEvents.contains(NavigationEvent.Dismiss))
        }

    @Test
    fun `WHEN move to non-private tab action is dispatched THEN the private tab is migrated and menu is dismissed`() =
        runTest(testDispatcher) {
            val tabId = "test-tab-id"

            val browserMenuState =
                BrowserMenuState(
                    selectedTab =
                        createTab(
                            id = tabId,
                            url = "https://www.mozilla.org",
                            private = true,
                        )
                )
            val store = createStore(menuState = MenuState(browserMenuState = browserMenuState))
            testScheduler.advanceUntilIdle()

            store.dispatch(MenuAction.MoveToNonPrivateTab)
            testScheduler.advanceUntilIdle()

            coVerify { migratePrivateTabUseCase(tabId) }
            assertTrue(navigationEvents.contains(NavigationEvent.Dismiss))
        }

    @Test
    fun `GIVEN no selected tab WHEN move to non-private tab action is dispatched THEN the use case is not invoked`() =
        runTest(testDispatcher) {
            val store = createStore(menuState = MenuState(browserMenuState = null))
            testScheduler.advanceUntilIdle()

            store.dispatch(MenuAction.MoveToNonPrivateTab)
            testScheduler.advanceUntilIdle()

            coVerify(exactly = 0) { migratePrivateTabUseCase(any()) }
            assertFalse(navigationEvents.contains(NavigationEvent.Dismiss))
        }

    @Test
    fun `WHEN custom menu item action is dispatched THEN pending intent is sent with url`() =
        runTest(testDispatcher) {
            val url = "https://www.mozilla.org"
            val mockIntent: PendingIntent = mockk()

            val store = spyk(createStore())
            testScheduler.advanceUntilIdle()

            assertTrue(navigationEvents.isEmpty())

            store.dispatch(
                MenuAction.CustomMenuItemAction(
                    intent = mockIntent,
                    url = url,
                )
            )
            testScheduler.advanceUntilIdle()

            assertEquals(
                listOf(
                    NavigationEvent.SendPendingIntentWithUrl(intent = mockIntent, url = url),
                    NavigationEvent.Dismiss,
                ),
                navigationEvents,
            )
        }

    @Test
    fun `GIVEN menu is accessed from the browser WHEN request desktop mode action is dispatched THEN request desktop site use case is invoked`() =
        runTest(testDispatcher) {
            val url = "https://www.mozilla.org"
            val title = "Mozilla"
            val selectedTab =
                createTab(
                    url = url,
                    title = title,
                    desktopMode = false,
                )
            val browserMenuState = BrowserMenuState(selectedTab = selectedTab)
            val store = createStore(menuState = MenuState(browserMenuState = browserMenuState))
            testScheduler.advanceUntilIdle()

            store.dispatch(MenuAction.RequestDesktopSite)
            testScheduler.advanceUntilIdle()

            verify {
                requestDesktopSiteUseCase.invoke(
                    enable = eq(true),
                    tabId = eq(selectedTab.id),
                )
            }
            assertTrue(navigationEvents.contains(NavigationEvent.Dismiss))
        }

    @Test
    fun `GIVEN menu is accessed from the browser and desktop mode is enabled WHEN request mobile mode action is dispatched THEN request desktop site use case is invoked`() =
        runTest(testDispatcher) {
            val url = "https://www.mozilla.org"
            val title = "Mozilla"
            val isDesktopMode = true
            val selectedTab =
                createTab(
                    url = url,
                    title = title,
                    desktopMode = isDesktopMode,
                )
            val browserMenuState = BrowserMenuState(selectedTab = selectedTab)
            val store =
                createStore(
                    menuState =
                        MenuState(
                            browserMenuState = browserMenuState,
                            isDesktopMode = isDesktopMode,
                        )
                )
            testScheduler.advanceUntilIdle()

            store.dispatch(MenuAction.RequestMobileSite)
            testScheduler.advanceUntilIdle()

            verify {
                requestDesktopSiteUseCase.invoke(
                    enable = eq(false),
                    tabId = eq(selectedTab.id),
                )
            }
            assertTrue(navigationEvents.contains(NavigationEvent.Dismiss))
        }

    @Test
    fun `GIVEN summarization feature setting indicates the menu item is not visible, WHEN menu is initialized, THEN the menu item is not visible`() =
        runTest(testDispatcher) {
            summarizeFeatureSettings.showMenuItem = false

            val store = createStore()
            store.dispatch(MenuAction.InitAction)

            testScheduler.advanceUntilIdle()

            assertFalse(
                "Expected the menu item is not visible because the feature settings indicate that it should not be visible",
                store.state.summarizationMenuState.visible,
            )
        }

    @Test
    fun `GIVEN the selected tab is not eligible for summarization by language, WHEN menu is initialized, THEN the menu item is not enabled`() =
        runTest(testDispatcher) {
            summarizeFeatureSettings.showMenuItem = true

            val store =
                createStore(
                    summarizationEligibilityChecker = TestSummarizationEligibilityChecker(isEligibleByLanguage = false)
                )
            store.dispatch(MenuAction.InitAction)

            testScheduler.advanceUntilIdle()

            assertFalse(
                "Expected the menu item is not enabled because the page is not eligible for summarization",
                store.state.summarizationMenuState.enabled,
            )
        }

    @Test
    fun `GIVEN an eligible custom tab WHEN the menu is initialized THEN the summarize page menu item is enabled`() =
        runTest(testDispatcher) {
            summarizeFeatureSettings.showMenuItem = true
            val customTab =
                createCustomTab(
                    url = "https://mozilla.org",
                    engineSession = TestEngineSession(),
                )
            val store = createStore(menuState = MenuState(browserMenuState = BrowserMenuState(selectedTab = customTab)))

            testScheduler.advanceUntilIdle()

            assertTrue(store.state.summarizationMenuState.enabled)
        }

    @Test
    fun `GIVEN summarization feature setting indicates the menu item should be visible, WHEN menu is initialized, THEN the menu item is visible`() =
        runTest(testDispatcher) {
            summarizeFeatureSettings.showMenuItem = true

            val store = createStore()
            store.dispatch(MenuAction.InitAction)

            testScheduler.advanceUntilIdle()

            assertTrue(
                "Expected the menu item to be visible because the feature settings indicate that it should be visible",
                store.state.summarizationMenuState.visible,
            )
        }

    @Test
    fun `GIVEN summarization feature setting indicates that menu item is not highlighted, WHEN menu is initialized, THEN the menu item is not highlighted`() =
        runTest(testDispatcher) {
            summarizeFeatureSettings.shouldHighlightMenuItem = false

            val store = createStore()
            store.dispatch(MenuAction.InitAction)

            testScheduler.advanceUntilIdle()

            assertFalse(
                "Expected the menu item to be highlighted because the feature settings indicate that it should not be highlighted",
                store.state.summarizationMenuState.highlighted,
            )
        }

    @Test
    fun `GIVEN summarization feature setting indicates that menu item should be highlighted, WHEN menu is initialized, THEN the menu item is highlighted`() =
        runTest(testDispatcher) {
            summarizeFeatureSettings.shouldHighlightMenuItem = true

            val store = createStore()
            store.dispatch(MenuAction.InitAction)

            testScheduler.advanceUntilIdle()

            assertTrue(
                "Expected the menu item to be highlighted because the feature settings indicate that it should be highlighted",
                store.state.summarizationMenuState.highlighted,
            )
        }

    @Test
    fun `WHEN summarization menu is exposed to the user, THEN we cache that exposure in the settings`() =
        runTest(testDispatcher) {
            val store = createStore()
            store.dispatch(MenuAction.InitAction)

            testScheduler.advanceUntilIdle()
            store.dispatch(MenuAction.OnSummarizationMenuExposed)
            testScheduler.advanceUntilIdle()

            assertEquals(
                "Expected the feature settings now indicates that the user has been exposed",
                1,
                summarizeFeatureSettings.menuItemExposureCount,
            )
        }

    @Test
    fun `GIVEN selected tab is private, WHEN init action is dispatched, THEN the summarize page menu item is not enabled`() =
        runTest(testDispatcher) {
            val store =
                createStore(
                    menuState =
                        MenuState(
                            browserMenuState =
                                BrowserMenuState(selectedTab = createTab(url = "https://mozilla.org", private = true))
                        )
                )
            store.dispatch(MenuAction.InitAction)

            testScheduler.advanceUntilIdle()
            assertFalse(
                "Expected the summarize page menu item to be disabled",
                store.state.summarizationMenuState.enabled,
            )
        }

    @Test
    fun `GIVEN no selected tab, WHEN init action is dispatched, THEN the summarize page menu item is not enabled`() =
        runTest(testDispatcher) {
            val store = createStore(menuState = MenuState(browserMenuState = null))
            store.dispatch(MenuAction.InitAction)

            testScheduler.advanceUntilIdle()
            assertFalse(
                "Expected the summarize page menu item to be disabled",
                store.state.summarizationMenuState.enabled,
            )
        }

    @Test
    fun `GIVEN selected tab is normal, WHEN init action is dispatched, THEN a toolbar highlight interaction is cached`() =
        runTest(testDispatcher) {
            val store =
                createStore(
                    menuState =
                        MenuState(
                            browserMenuState = BrowserMenuState(selectedTab = createTab(url = "https://mozilla.org"))
                        )
                )
            store.dispatch(MenuAction.InitAction)

            testScheduler.advanceUntilIdle()
            assertTrue(
                "Expected the toolbar highlight interaction to be cached",
                summarizeFeatureSettings.toolbarOverflowMenuInteractionCount > 0,
            )
        }

    @Test
    fun `WHEN more menu is clicked, THEN we cache that interaction in the summarization menu settings`() =
        runTest(testDispatcher) {
            val store = createStore()
            store.dispatch(MenuAction.InitAction)
            testScheduler.advanceUntilIdle()
            store.dispatch(MenuAction.OnMoreMenuClicked)
            testScheduler.advanceUntilIdle()

            assertEquals(
                "Expected the more menu clicked to be cached in the summarization menu settings",
                1,
                summarizeFeatureSettings.menuOverflowInteractionCount,
            )
        }

    @Test
    fun `GIVEN more menu is not highlighted by summarize feature, WHEN more menu is clicked, THEN the interaction is not cached`() =
        runTest(testDispatcher) {
            summarizeFeatureSettings.shouldHighlightOverflowMenuItem = false

            val store = createStore()
            store.dispatch(MenuAction.InitAction)
            store.dispatch(MenuAction.OnMoreMenuClicked)
            testScheduler.advanceUntilIdle()

            assertEquals(
                "Expected the more menu click is not cached in the summarization menu settings",
                0,
                summarizeFeatureSettings.menuOverflowInteractionCount,
            )
        }

    @OptIn(ExperimentalCoroutinesApi::class) // UnconfinedTestDispatcher
    private fun TestScope.createStore(
        appStore: AppStore = AppStore(),
        summarizationEligibilityChecker: SummarizationEligibilityChecker = TestSummarizationEligibilityChecker(),
        menuState: MenuState =
            MenuState(
                browserMenuState =
                    BrowserMenuState(
                        selectedTab =
                            createTab(
                                url = "https://mozilla.org",
                                engineSession = TestEngineSession(),
                            )
                    )
            ),
    ) =
        MenuStore(
                initialState = menuState,
                middleware =
                    listOf(
                        MenuDialogMiddleware(
                            appStore = appStore,
                            addonManager = addonManager,
                            settings = settings,
                            summarizeMenuSettings = summarizeFeatureSettings,
                            summarizationEligibilityChecker = summarizationEligibilityChecker,
                            bookmarksStorage = bookmarksStorage,
                            pinnedSiteStorage = pinnedSiteStorage,
                            appLinksUseCases = appLinksUseCases,
                            addBookmarkUseCase = addBookmarkUseCase,
                            addPinnedSiteUseCase = addPinnedSiteUseCase,
                            removePinnedSitesUseCase = removePinnedSiteUseCase,
                            requestDesktopSiteUseCase = requestDesktopSiteUseCase,
                            migratePrivateTabUseCase = migratePrivateTabUseCase,
                            mainDispatcher = testDispatcher,
                        )
                    ),
            )
            .also { store ->
                backgroundScope.launch(UnconfinedTestDispatcher(testScheduler)) {
                    store.navigationEvents.collect { navigationEvents.add(it) }
                }
            }

    private class TestSummarizationEligibilityChecker(
        private val isEligible: Boolean = false,
        private val isEligibleByLanguage: Boolean = true,
    ) : SummarizationEligibilityChecker {
        override suspend fun check(session: EngineSession): Result<Boolean> = Result.success(isEligible)

        override suspend fun checkLanguage(session: EngineSession): Result<Boolean> =
            Result.success(isEligibleByLanguage)
    }
}
