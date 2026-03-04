/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.components.menu

import android.app.PendingIntent
import android.content.Intent
import android.widget.TextView
import androidx.appcompat.app.AlertDialog
import com.google.android.material.dialog.MaterialAlertDialogBuilder
import io.mockk.coEvery
import io.mockk.coVerify
import io.mockk.every
import io.mockk.mockk
import io.mockk.spyk
import io.mockk.verify
import kotlinx.coroutines.runBlocking
import kotlinx.coroutines.test.StandardTestDispatcher
import kotlinx.coroutines.test.runTest
import mozilla.appservices.places.BookmarkRoot
import mozilla.components.browser.state.state.createTab
import mozilla.components.concept.engine.webextension.InstallationMethod
import mozilla.components.feature.addons.Addon
import mozilla.components.feature.addons.AddonManager
import mozilla.components.feature.app.links.AppLinkRedirect
import mozilla.components.feature.app.links.AppLinksUseCases
import mozilla.components.feature.session.SessionUseCases
import mozilla.components.feature.top.sites.PinnedSiteStorage
import mozilla.components.feature.top.sites.TopSite
import mozilla.components.feature.top.sites.TopSitesUseCases
import mozilla.components.support.test.middleware.CaptureActionsMiddleware
import mozilla.components.support.test.robolectric.testContext
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNotNull
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
import org.mozilla.fenix.components.appstate.AppState
import org.mozilla.fenix.components.bookmarks.BookmarksUseCase.AddBookmarksUseCase
import org.mozilla.fenix.components.menu.fake.FakeBookmarksStorage
import org.mozilla.fenix.components.menu.middleware.MenuDialogMiddleware
import org.mozilla.fenix.components.menu.store.BrowserMenuState
import org.mozilla.fenix.components.menu.store.MenuAction
import org.mozilla.fenix.components.menu.store.MenuState
import org.mozilla.fenix.components.menu.store.MenuStore
import org.mozilla.fenix.settings.summarize.FakeSummarizationFeatureConfiguration
import org.mozilla.fenix.utils.LastSavedFolderCache
import org.mozilla.fenix.utils.Settings
import org.robolectric.RobolectricTestRunner

@RunWith(RobolectricTestRunner::class)
class MenuDialogMiddlewareTest {

    private val testDispatcher = StandardTestDispatcher()

    private val bookmarksStorage = FakeBookmarksStorage()
    private val addBookmarkUseCase: AddBookmarksUseCase =
        spyk(AddBookmarksUseCase(storage = bookmarksStorage))

    private val addonManager: AddonManager = mockk(relaxed = true)
    private val onDeleteAndQuit: () -> Unit = mockk()

    private lateinit var alertDialogBuilder: MaterialAlertDialogBuilder
    private lateinit var pinnedSiteStorage: PinnedSiteStorage
    private lateinit var addPinnedSiteUseCase: TopSitesUseCases.AddPinnedSiteUseCase
    private lateinit var removePinnedSiteUseCase: TopSitesUseCases.RemoveTopSiteUseCase
    private lateinit var appLinksUseCases: AppLinksUseCases
    private lateinit var requestDesktopSiteUseCase: SessionUseCases.RequestDesktopSiteUseCase
    private lateinit var settings: Settings

    private val summarizeFeatureSettings = FakeSummarizationFeatureConfiguration()
    private lateinit var lastSavedFolderCache: LastSavedFolderCache

    companion object {
        const val TOP_SITES_MAX_COUNT = 16
    }

    @Before
    fun setup() {
        alertDialogBuilder = mockk(relaxed = true)
        pinnedSiteStorage = mockk(relaxUnitFun = true)
        addPinnedSiteUseCase = mockk(relaxUnitFun = true)
        removePinnedSiteUseCase = mockk(relaxUnitFun = true)
        appLinksUseCases = mockk()
        requestDesktopSiteUseCase = mockk(relaxUnitFun = true)
        lastSavedFolderCache = mockk(relaxed = true)

        settings = Settings(testContext)

        runBlocking {
            coEvery { pinnedSiteStorage.getPinnedSites() } returns emptyList()
            coEvery { addonManager.getAddons() } returns emptyList()
        }
    }

    @Test
    fun `GIVEN no selected tab WHEN init action is dispatched THEN browser state is not updated`() = runTest(testDispatcher) {
        val store = createStore(
            menuState = MenuState(
                browserMenuState = null,
            ),
        )
        testScheduler.advanceUntilIdle()

        assertNull(store.state.browserMenuState)
    }

    @Test
    fun `GIVEN selected tab is bookmarked WHEN init action is dispatched THEN initial bookmark state is updated`() = runTest(testDispatcher) {
        val url = "https://www.mozilla.org"
        val title = "Mozilla"

        val guid = bookmarksStorage.addItem(
            parentGuid = BookmarkRoot.Mobile.id,
            url = url,
            title = title,
            position = 5u,
        )

        val browserMenuState = BrowserMenuState(
            selectedTab = createTab(
                url = url,
                title = title,
            ),
        )
        val store = createStore(
            menuState = MenuState(
                browserMenuState = browserMenuState,
            ),
        )
        testScheduler.advanceUntilIdle()

        assertEquals(guid.getOrNull()!!, store.state.browserMenuState!!.bookmarkState.guid)
        assertTrue(store.state.browserMenuState!!.bookmarkState.isBookmarked)
    }

    @Test
    fun `GIVEN selected tab is not bookmarked WHEN init action is dispatched THEN initial bookmark state is not updated`() = runTest(testDispatcher) {
        val url = "https://www.mozilla.org"
        val title = "Mozilla"
        val browserMenuState = BrowserMenuState(
            selectedTab = createTab(
                url = url,
                title = title,
            ),
        )
        val store = createStore(
            menuState = MenuState(
                browserMenuState = browserMenuState,
            ),
        )
        testScheduler.advanceUntilIdle()

        assertNull(store.state.browserMenuState!!.bookmarkState.guid)
        assertFalse(store.state.browserMenuState!!.bookmarkState.isBookmarked)
    }

    @Test
    fun `GIVEN recommended addons are available WHEN init action is dispatched THEN initial extension state is updated`() = runTest(testDispatcher) {
        val addon = Addon(id = "ext1")
        coEvery { addonManager.getAddons() } returns listOf(addon)

        val store = createStore()
        testScheduler.advanceUntilIdle()

        assertTrue(store.state.extensionMenuState.availableAddons.isEmpty())
        assertEquals(1, store.state.extensionMenuState.recommendedAddons.size)
        assertEquals(addon, store.state.extensionMenuState.recommendedAddons.first())
    }

    @Test
    fun `GIVEN recommended addons are available WHEN init action is dispatched THEN initial extension state is updated and shows maximum three recommended addons`() = runTest(testDispatcher) {
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
            val addonTwo = Addon(
                id = "ext2",
                installedState = Addon.InstalledState(
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
            val addon = Addon(
                id = "ext",
                installedState = Addon.InstalledState(
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
    fun `GIVEN last save folder cache is empty WHEN add bookmark action is dispatched for a selected tab THEN bookmark is added with Mobile root as the parent`() = runTest(testDispatcher) {
        val url = "https://www.mozilla.org"
        val title = "Mozilla"
        var dismissWasCalled = false

        val browserMenuState = BrowserMenuState(
            selectedTab = createTab(
                url = url,
                title = title,
            ),
        )
        val captureMiddleware = CaptureActionsMiddleware<AppState, AppAction>()
        val appStore = AppStore(middlewares = listOf(captureMiddleware))
        val store = createStore(
            appStore = appStore,
            menuState = MenuState(
                browserMenuState = browserMenuState,
            ),
            onDismiss = { dismissWasCalled = true },
        )
        testScheduler.advanceUntilIdle()

        coEvery { lastSavedFolderCache.getGuid() } returns null

        store.dispatch(MenuAction.AddBookmark)
        testScheduler.advanceUntilIdle()

        coVerify {
            addBookmarkUseCase.invoke(
                url = url,
                title = title,
                parentGuid = BookmarkRoot.Mobile.id,
            )
        }

        captureMiddleware.assertLastAction(BookmarkAction.BookmarkAdded::class) { action: BookmarkAction.BookmarkAdded ->
            assertNotNull(action.guidToEdit)
        }
        assertTrue(dismissWasCalled)
    }

    @Test
    fun `GIVEN last save folder cache has a value WHEN add bookmark action is dispatched for a selected tab THEN bookmark is added with the cached value as its parent`() = runTest(testDispatcher) {
        // given that the last saved folder actually exists
        val lastSavedFolderId = bookmarksStorage.addFolder(BookmarkRoot.Mobile.id, "last-folder")
            .getOrThrow()
        coEvery { lastSavedFolderCache.getGuid() } returns lastSavedFolderId
        val url = "https://www.mozilla.org"
        val title = "Mozilla"
        var dismissWasCalled = false

        val browserMenuState = BrowserMenuState(
            selectedTab = createTab(
                url = url,
                title = title,
            ),
        )
        val captureMiddleware = CaptureActionsMiddleware<AppState, AppAction>()
        val appStore = AppStore(middlewares = listOf(captureMiddleware))
        val store = createStore(
            appStore = appStore,
            menuState = MenuState(
                browserMenuState = browserMenuState,
            ),
            onDismiss = { dismissWasCalled = true },
        )
        testScheduler.advanceUntilIdle()

        store.dispatch(MenuAction.AddBookmark)
        testScheduler.advanceUntilIdle()

        coVerify { addBookmarkUseCase.invoke(url = url, title = title, parentGuid = lastSavedFolderId) }

        captureMiddleware.assertLastAction(BookmarkAction.BookmarkAdded::class) { action: BookmarkAction.BookmarkAdded ->
            assertNotNull(action.guidToEdit)
        }
        assertTrue(dismissWasCalled)
    }

    @Test
    fun `GIVEN last save folder cache has a value that is no longer available THEN a new bookmark is added to the mobile root`() =
        runTest(testDispatcher) {
        val url = "https://www.mozilla.org"
        val title = "Mozilla"

        val browserMenuState = BrowserMenuState(
            selectedTab = createTab(
                url = url,
                title = title,
            ),
        )
        val captureMiddleware = CaptureActionsMiddleware<AppState, AppAction>()
        val appStore = AppStore(middlewares = listOf(captureMiddleware))
        val store = createStore(
            appStore = appStore,
            menuState = MenuState(
                browserMenuState = browserMenuState,
            ),
            onDismiss = { },
        )
        testScheduler.advanceUntilIdle()

        coEvery { lastSavedFolderCache.getGuid() } returns "cached-value"

        store.dispatch(MenuAction.AddBookmark)

        testScheduler.advanceUntilIdle()

        // we fall back to the mobile root
        coVerify { addBookmarkUseCase.invoke(url = url, title = title, parentGuid = BookmarkRoot.Mobile.id) }
    }

    @Test
    fun `GIVEN the last added bookmark does not belong to a folder WHEN bookmark is added THEN bookmark is added to mobile root`() = runTest(testDispatcher) {
        val url = "https://www.mozilla.org"
        val title = "Mozilla"

        // Add a pre-existing item. This accounts for the null case, but that shouldn't actually be
        // possible because the mobile root is a subfolder of the synced root
        bookmarksStorage.addFolder(
            parentGuid = "",
            title = "title",
        )
        val browserMenuState = BrowserMenuState(
            selectedTab = createTab(
                url = url,
                title = title,
            ),
        )
        val appStore = AppStore()
        val store = createStore(
            appStore = appStore,
            menuState = MenuState(
                browserMenuState = browserMenuState,
            ),
            onDismiss = { },
        )
        testScheduler.advanceUntilIdle()

        store.dispatch(MenuAction.AddBookmark)
        testScheduler.advanceUntilIdle()

        coVerify { addBookmarkUseCase.invoke(url = url, title = title, parentGuid = BookmarkRoot.Mobile.id) }
    }

    @Test
    fun `GIVEN selected tab is bookmarked WHEN add bookmark action is dispatched THEN add bookmark use case is never called`() = runTest(testDispatcher) {
        val url = "https://www.mozilla.org"
        val title = "Mozilla"
        var dismissWasCalled = false

        val guid = bookmarksStorage.addItem(
            parentGuid = BookmarkRoot.Mobile.id,
            url = url,
            title = title,
            position = 5u,
        )

        val browserMenuState = BrowserMenuState(
            selectedTab = createTab(
                url = url,
                title = title,
            ),
        )
        val captureMiddleware = CaptureActionsMiddleware<AppState, AppAction>()
        val appStore = AppStore(middlewares = listOf(captureMiddleware))
        val store = createStore(
            appStore = appStore,
            menuState = MenuState(
                browserMenuState = browserMenuState,
            ),
            onDismiss = { dismissWasCalled = true },
        )
        testScheduler.advanceUntilIdle()

        assertEquals(guid.getOrNull()!!, store.state.browserMenuState!!.bookmarkState.guid)
        assertTrue(store.state.browserMenuState!!.bookmarkState.isBookmarked)

        store.dispatch(MenuAction.AddBookmark)
        testScheduler.advanceUntilIdle()

        coVerify(exactly = 0) { addBookmarkUseCase.invoke(url = url, title = title) }
        captureMiddleware.assertNotDispatched(BookmarkAction.BookmarkAdded::class)
        assertFalse(dismissWasCalled)
    }

    @Test
    fun `GIVEN selected tab is pinned WHEN init action is dispatched THEN initial pinned state is updated`() = runTest(testDispatcher) {
        val url = "https://www.mozilla.org"
        val title = "Mozilla"

        coEvery { pinnedSiteStorage.getPinnedSites() } returns listOf(TopSite.Pinned(id = 0, title = title, url = url, createdAt = 0))

        val browserMenuState = BrowserMenuState(
            selectedTab = createTab(
                url = url,
                title = title,
            ),
        )
        val store = createStore(
            menuState = MenuState(
                browserMenuState = browserMenuState,
            ),
        )
        testScheduler.advanceUntilIdle()

        assertTrue(store.state.browserMenuState!!.isPinned)
    }

    @Test
    fun `GIVEN selected tab is not pinned WHEN init action is dispatched THEN initial pinned state is not updated`() = runTest(testDispatcher) {
        val url = "https://www.mozilla.org"
        val title = "Mozilla"

        val browserMenuState = BrowserMenuState(
            selectedTab = createTab(
                url = url,
                title = title,
            ),
        )
        val store = createStore(
            menuState = MenuState(
                browserMenuState = browserMenuState,
            ),
        )
        testScheduler.advanceUntilIdle()

        assertFalse(store.state.browserMenuState!!.isPinned)
    }

    @Test
    fun `WHEN add to shortcuts action is dispatched for a selected tab THEN the site is pinned`() = runTest(testDispatcher) {
        val url = "https://www.mozilla.org"
        val title = "Mozilla"
        var dismissedWasCalled = false

        val browserMenuState = BrowserMenuState(
            selectedTab = createTab(
                url = url,
                title = title,
            ),
        )
        val appStore = spyk(AppStore())
        val store = createStore(
            appStore = appStore,
            menuState = MenuState(
                browserMenuState = browserMenuState,
            ),
            onDismiss = { dismissedWasCalled = true },
        )
        testScheduler.advanceUntilIdle()

        store.dispatch(MenuAction.AddShortcut)
        testScheduler.advanceUntilIdle()

        verify { addPinnedSiteUseCase.invoke(url = url, title = title) }
        verify {
            appStore.dispatch(
                AppAction.ShortcutAction.ShortcutAdded,
            )
        }
        assertTrue(dismissedWasCalled)
    }

    @Test
    fun `GIVEN selected tab is pinned WHEN add to shortcuts action is dispatched THEN add pinned site use case is never called`() = runTest(testDispatcher) {
        val url = "https://www.mozilla.org"
        val title = "Mozilla"
        var dismissedWasCalled = false

        coEvery { pinnedSiteStorage.getPinnedSites() } returns
                listOf(
                    TopSite.Pinned(
                        id = 0,
                        title = title,
                        url = url,
                        createdAt = 0,
                    ),
                )

        pinnedSiteStorage.addPinnedSite(
            url = url,
            title = title,
        )

        val browserMenuState = BrowserMenuState(
            selectedTab = createTab(
                url = url,
                title = title,
            ),
        )
        val appStore = spyk(AppStore())
        val store = createStore(
            appStore = appStore,
            menuState = MenuState(
                browserMenuState = browserMenuState,
            ),
            onDismiss = { dismissedWasCalled = true },
        )
        testScheduler.advanceUntilIdle()

        assertTrue(store.state.browserMenuState!!.isPinned)

        store.dispatch(MenuAction.AddShortcut)
        testScheduler.advanceUntilIdle()

        verify(exactly = 0) { addPinnedSiteUseCase.invoke(url = url, title = title) }
        verify(exactly = 0) {
            appStore.dispatch(
                AppAction.ShortcutAction.ShortcutAdded,
            )
        }
        assertFalse(dismissedWasCalled)
    }

    @Test
    fun `WHEN remove from shortcuts action is dispatched for a selected tab THEN remove pinned site use case is never called`() = runTest(testDispatcher) {
        val url = "https://www.mozilla.org"
        val title = "Mozilla"
        var dismissedWasCalled = false

        val topSite = TopSite.Pinned(
            id = 0,
            title = title,
            url = url,
            createdAt = 0,
        )
        val browserMenuState = BrowserMenuState(
            selectedTab = createTab(
                url = url,
                title = title,
            ),
        )
        val appStore = spyk(AppStore())
        val store = createStore(
            appStore = appStore,
            menuState = MenuState(
                browserMenuState = browserMenuState,
            ),
            onDismiss = { dismissedWasCalled = true },
        )
        testScheduler.advanceUntilIdle()

        assertFalse(store.state.browserMenuState!!.isPinned)

        store.dispatch(MenuAction.RemoveShortcut)
        testScheduler.advanceUntilIdle()

        verify(exactly = 0) { removePinnedSiteUseCase.invoke(topSite = topSite) }
        assertFalse(dismissedWasCalled)
    }

    @Test
    fun `GIVEN selected tab is pinned WHEN remove from shortcuts action is dispatched THEN pinned state is updated`() = runTest(testDispatcher) {
        val url = "https://www.mozilla.org"
        val title = "Mozilla"
        val topSite = TopSite.Pinned(
            id = 0,
            title = title,
            url = url,
            createdAt = 0,
        )
        var dismissedWasCalled = false

        coEvery { pinnedSiteStorage.getPinnedSites() } returns listOf(topSite)

        val browserMenuState = BrowserMenuState(
            selectedTab = createTab(
                url = url,
                title = title,
            ),
        )
        val appStore = spyk(AppStore())
        val store = createStore(
            appStore = appStore,
            menuState = MenuState(
                browserMenuState = browserMenuState,
            ),
            onDismiss = { dismissedWasCalled = true },
        )
        testScheduler.advanceUntilIdle()

        assertTrue(store.state.browserMenuState!!.isPinned)

        store.dispatch(MenuAction.RemoveShortcut)
        testScheduler.advanceUntilIdle()

        verify { removePinnedSiteUseCase.invoke(topSite = topSite) }
        assertTrue(dismissedWasCalled)
    }

    @Test
    fun `GIVEN maximum number of top sites is reached WHEN add to shortcuts action is dispatched THEN add pinned site use case is never called`() = runTest(testDispatcher) {
        val url = "https://www.mozilla.org"
        val title = "Mozilla"
        var dismissedWasCalled = false

        val pinnedSitesList = mutableListOf<TopSite>()

        repeat(TOP_SITES_MAX_COUNT) {
            pinnedSitesList.add(
                TopSite.Pinned(
                    id = 0,
                    title = title,
                    url = "$url/1",
                    createdAt = 0,
                ),
            )
        }

        coEvery { pinnedSiteStorage.getPinnedSites() } returns pinnedSitesList

        val newAlertDialog: AlertDialog = mockk(relaxed = true)
        val mockButton: TextView = mockk(relaxed = true)
        every { alertDialogBuilder.create() } returns newAlertDialog
        every { newAlertDialog.findViewById<TextView>(any()) } returns mockButton

        val browserMenuState = BrowserMenuState(
            selectedTab = createTab(
                url = url,
                title = title,
            ),
        )
        val appStore = spyk(AppStore())
        val store = createStore(
            appStore = appStore,
            menuState = MenuState(
                browserMenuState = browserMenuState,
            ),
            onDismiss = { dismissedWasCalled = true },
        )
        testScheduler.advanceUntilIdle()

        assertFalse(store.state.browserMenuState!!.isPinned)

        store.dispatch(MenuAction.AddShortcut)
        testScheduler.advanceUntilIdle()

        verify(exactly = 0) { addPinnedSiteUseCase.invoke(url = url, title = title) }
        verify(exactly = 0) {
            appStore.dispatch(
                AppAction.ShortcutAction.ShortcutAdded,
            )
        }
        assertTrue(dismissedWasCalled)
    }

    @Test
    fun `GIVEN selected tab has external app WHEN open in app action is dispatched THEN the site is opened in app`() = runTest(testDispatcher) {
        val url = "https://www.mozilla.org"
        val title = "Mozilla"
        var dismissWasCalled = false

        val browserMenuState = BrowserMenuState(
            selectedTab = createTab(
                url = url,
                title = title,
            ),
        )
        val store = createStore(
            menuState = MenuState(
                browserMenuState = browserMenuState,
            ),
            onDismiss = { dismissWasCalled = true },
        )
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
        assertTrue(dismissWasCalled)
    }

    @Test
    fun `GIVEN selected tab does not have external app WHEN open in app action is dispatched THEN the site is not opened in app`() = runTest(testDispatcher) {
        val url = "https://www.mozilla.org"
        val title = "Mozilla"
        var dismissWasCalled = false

        val browserMenuState = BrowserMenuState(
            selectedTab = createTab(
                url = url,
                title = title,
            ),
        )
        val store = createStore(
            menuState = MenuState(
                browserMenuState = browserMenuState,
            ),
            onDismiss = { dismissWasCalled = true },
        )
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
        assertFalse(dismissWasCalled)
    }

    @Test
    fun `WHEN install addon action is dispatched THEN addon is installed`() = runTest(testDispatcher) {
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
    fun `WHEN customize reader view action is dispatched THEN reader view action is dispatched`() = runTest(testDispatcher) {
        var dismissWasCalled = false

        val appStore = spyk(AppStore())
        val store = createStore(
            appStore = appStore,
            menuState = MenuState(),
            onDismiss = { dismissWasCalled = true },
        )
        testScheduler.advanceUntilIdle()

        store.dispatch(MenuAction.CustomizeReaderView)
        testScheduler.advanceUntilIdle()

        verify { appStore.dispatch(ReaderViewAction.ReaderViewControlsShown) }
        assertTrue(dismissWasCalled)
    }

    @Test
    fun `WHEN open in Firefox action is dispatched for a custom tab THEN the tab is opened in the browser`() = runTest(testDispatcher) {
        val url = "https://www.mozilla.org"
        val title = "Mozilla"
        var dismissedWasCalled = false

        val browserMenuState = BrowserMenuState(
            selectedTab = createTab(
                url = url,
                title = title,
            ),
        )
        val appStore = spyk(AppStore())
        val store = createStore(
            appStore = appStore,
            menuState = MenuState(
                browserMenuState = browserMenuState,
            ),
            onDismiss = { dismissedWasCalled = true },
        )
        testScheduler.advanceUntilIdle()

        store.dispatch(MenuAction.OpenInFirefox)
        testScheduler.advanceUntilIdle()

        verify { appStore.dispatch(AppAction.OpenInFirefoxStarted) }
        assertTrue(dismissedWasCalled)
    }

    @Test
    fun `WHEN find in page action is dispatched THEN find in page app action is dispatched`() = runTest(testDispatcher) {
        val url = "https://www.mozilla.org"
        val title = "Mozilla"
        var dismissWasCalled = false

        val browserMenuState = BrowserMenuState(
            selectedTab = createTab(
                url = url,
                title = title,
            ),
        )
        val appStore = spyk(AppStore())
        val store = spyk(
            createStore(
                appStore = appStore,
                menuState = MenuState(
                    browserMenuState = browserMenuState,
                ),
                onDismiss = { dismissWasCalled = true },
            ),
        )
        testScheduler.advanceUntilIdle()

        store.dispatch(MenuAction.FindInPage)
        testScheduler.advanceUntilIdle()

        verify { appStore.dispatch(FindInPageAction.FindInPageStarted) }
        assertTrue(dismissWasCalled)
    }

    @Test
    fun `WHEN custom menu item action is dispatched THEN pending intent is sent with url`() = runTest(testDispatcher) {
        val url = "https://www.mozilla.org"
        val mockIntent: PendingIntent = mockk()
        var dismissWasCalled = false
        var sentIntent: PendingIntent? = null
        var sentUrl: String? = null

        val store = spyk(
            createStore(
                onDismiss = { dismissWasCalled = true },
                onSendPendingIntentWithUrl = { _, _ ->
                    sentIntent = mockIntent
                    sentUrl = url
                },
            ),
        )
        testScheduler.advanceUntilIdle()

        assertNull(sentIntent)
        assertNull(sentUrl)

        store.dispatch(
            MenuAction.CustomMenuItemAction(
                intent = mockIntent,
                url = url,
            ),
        )
        testScheduler.advanceUntilIdle()

        assertEquals(sentIntent, mockIntent)
        assertEquals(sentUrl, url)
        assertTrue(dismissWasCalled)
    }

    @Test
    fun `GIVEN menu is accessed from the browser WHEN request desktop mode action is dispatched THEN request desktop site use case is invoked`() = runTest(testDispatcher) {
        val url = "https://www.mozilla.org"
        val title = "Mozilla"
        val selectedTab = createTab(
            url = url,
            title = title,
            desktopMode = false,
        )
        val browserMenuState = BrowserMenuState(
            selectedTab = selectedTab,
        )
        var dismissWasCalled = false
        val store = createStore(
            menuState = MenuState(
                browserMenuState = browserMenuState,
            ),
            onDismiss = { dismissWasCalled = true },
        )
        testScheduler.advanceUntilIdle()

        store.dispatch(MenuAction.RequestDesktopSite)
        testScheduler.advanceUntilIdle()

        verify {
            requestDesktopSiteUseCase.invoke(
                enable = eq(true),
                tabId = eq(selectedTab.id),
            )
        }
        assertTrue(dismissWasCalled)
    }

    @Test
    fun `GIVEN menu is accessed from the browser and desktop mode is enabled WHEN request mobile mode action is dispatched THEN request desktop site use case is invoked`() = runTest(testDispatcher) {
        val url = "https://www.mozilla.org"
        val title = "Mozilla"
        val isDesktopMode = true
        val selectedTab = createTab(
            url = url,
            title = title,
            desktopMode = isDesktopMode,
        )
        val browserMenuState = BrowserMenuState(
            selectedTab = selectedTab,
        )
        var dismissWasCalled = false
        val store = createStore(
            menuState = MenuState(
                browserMenuState = browserMenuState,
                isDesktopMode = isDesktopMode,
            ),
            onDismiss = { dismissWasCalled = true },
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
        assertTrue(dismissWasCalled)
    }

    @Test
    fun `WHEN CFR is shown THEN on CFR shown action is dispatched`() = runTest(testDispatcher) {
        var shownWasCalled = false

        val appStore = spyk(AppStore())
        val store = createStore(
            appStore = appStore,
            menuState = MenuState(
                browserMenuState = null,
            ),
            onDismiss = { shownWasCalled = true },
        )
        testScheduler.advanceUntilIdle()

        store.dispatch(MenuAction.OnCFRShown)
        testScheduler.advanceUntilIdle()

        assertFalse(settings.shouldShowMenuCFR)
        assertFalse(shownWasCalled)
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
    fun `GIVEN a page is loading, WHEN menu is initialized, THEN the the summarization menu item is disabled`() =
        runTest(testDispatcher) {
            summarizeFeatureSettings.showMenuItem = true

            val store = createStore(isTabLoading = true)
            store.dispatch(MenuAction.InitAction)

            testScheduler.advanceUntilIdle()

            assertFalse(
                "Expected the menu item to be disabled because the page is loading",
                store.state.summarizationMenuState.enabled,
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
            val store = createStore(
                menuState = MenuState(
                    browserMenuState = BrowserMenuState(
                        selectedTab = createTab(url = "https://mozilla.org", private = true),
                    ),
                ),
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
            val store = createStore(
                menuState = MenuState(
                    browserMenuState = null,
                ),
            )
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
            val store = createStore(
                menuState = MenuState(
                    browserMenuState = BrowserMenuState(
                        selectedTab = createTab(url = "https://mozilla.org"),
                    ),
                ),
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

    private fun createStore(
        appStore: AppStore = AppStore(),
        isTabLoading: Boolean = false,
        menuState: MenuState = MenuState(
            browserMenuState = BrowserMenuState(
                selectedTab = createTab(
                    url = "https://mozilla.org",
                ),
                isLoading = isTabLoading,
            ),
        ),
        onDismiss: suspend () -> Unit = {},
        onSendPendingIntentWithUrl: (intent: PendingIntent, url: String?) -> Unit = { _: PendingIntent, _: String? -> },
    ) = MenuStore(
        initialState = menuState,
        middleware = listOf(
            MenuDialogMiddleware(
                appStore = appStore,
                addonManager = addonManager,
                settings = settings,
                summarizeMenuSettings = summarizeFeatureSettings,
                bookmarksStorage = bookmarksStorage,
                pinnedSiteStorage = pinnedSiteStorage,
                appLinksUseCases = appLinksUseCases,
                addBookmarkUseCase = addBookmarkUseCase,
                addPinnedSiteUseCase = addPinnedSiteUseCase,
                removePinnedSitesUseCase = removePinnedSiteUseCase,
                requestDesktopSiteUseCase = requestDesktopSiteUseCase,
                materialAlertDialogBuilder = alertDialogBuilder,
                topSitesMaxLimit = TOP_SITES_MAX_COUNT,
                onDeleteAndQuit = onDeleteAndQuit,
                onDismiss = onDismiss,
                onSendPendingIntentWithUrl = onSendPendingIntentWithUrl,
                mainDispatcher = testDispatcher,
                lastSavedFolderCache = lastSavedFolderCache,
            ),
        ),
    )
}
