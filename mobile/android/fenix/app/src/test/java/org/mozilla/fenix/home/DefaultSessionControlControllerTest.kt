/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.home

import androidx.navigation.NavController
import io.mockk.every
import io.mockk.mockk
import io.mockk.verify
import java.io.File
import java.lang.ref.WeakReference
import kotlin.test.assertNotNull
import kotlinx.coroutines.test.TestScope
import mozilla.components.browser.state.action.TabListAction
import mozilla.components.browser.state.search.SearchEngine
import mozilla.components.browser.state.state.BrowserState
import mozilla.components.browser.state.state.ReaderState
import mozilla.components.browser.state.state.SearchState
import mozilla.components.browser.state.state.createTab
import mozilla.components.browser.state.state.recover.RecoverableTab
import mozilla.components.browser.state.state.recover.TabState
import mozilla.components.browser.state.store.BrowserStore
import mozilla.components.concept.engine.Engine
import mozilla.components.feature.session.SessionUseCases
import mozilla.components.feature.tab.collections.Tab as ComponentTab
import mozilla.components.feature.tab.collections.TabCollection
import mozilla.components.feature.tabs.TabsUseCases
import mozilla.components.service.nimbus.messaging.Message
import mozilla.components.support.test.robolectric.testContext
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Before
import org.junit.Rule
import org.junit.Test
import org.junit.rules.TemporaryFolder
import org.junit.runner.RunWith
import org.mozilla.fenix.GleanMetrics.Collections
import org.mozilla.fenix.GleanMetrics.HomeBookmarks
import org.mozilla.fenix.GleanMetrics.RecentTabs
import org.mozilla.fenix.HomeActivity
import org.mozilla.fenix.R
import org.mozilla.fenix.browser.browsingmode.BrowsingMode
import org.mozilla.fenix.components.AppStore
import org.mozilla.fenix.components.TabCollectionStorage
import org.mozilla.fenix.components.accounts.FenixFxAEntryPoint
import org.mozilla.fenix.components.appstate.AppAction
import org.mozilla.fenix.components.appstate.AppState
import org.mozilla.fenix.components.appstate.setup.checklist.ChecklistItem
import org.mozilla.fenix.components.share.ShareSource
import org.mozilla.fenix.components.usecases.FenixBrowserUseCases
import org.mozilla.fenix.components.usecases.ShareUseCases
import org.mozilla.fenix.ext.components
import org.mozilla.fenix.helpers.FenixGleanTestRule
import org.mozilla.fenix.home.bookmarks.Bookmark
import org.mozilla.fenix.home.recenttabs.RecentTab
import org.mozilla.fenix.home.sessioncontrol.DefaultSessionControlController
import org.mozilla.fenix.home.sessioncontrol.SessionControlControllerCallback
import org.mozilla.fenix.messaging.MessageController
import org.mozilla.fenix.onboarding.WallpaperOnboardingDialogFragment.Companion.THUMBNAILS_SELECTION_COUNT
import org.mozilla.fenix.utils.Settings
import org.mozilla.fenix.wallpapers.Wallpaper
import org.mozilla.fenix.wallpapers.WallpaperState
import org.robolectric.RobolectricTestRunner

@RunWith(RobolectricTestRunner::class) // For gleanTestRule
class DefaultSessionControlControllerTest {

    @get:Rule val temporaryFolder = TemporaryFolder()

    @get:Rule val gleanTestRule = FenixGleanTestRule(testContext)

    private val activity: HomeActivity = mockk(relaxed = true)

    private val filesDir: File by lazy { temporaryFolder.newFolder() }
    private val appStore: AppStore = mockk(relaxed = true)
    private val navController: NavController = mockk(relaxed = true)
    private val messageController: MessageController = mockk(relaxed = true)
    private val engine: Engine = mockk(relaxed = true)
    private val tabCollectionStorage: TabCollectionStorage = mockk(relaxed = true)
    private val tabsUseCases: TabsUseCases = mockk(relaxed = true)
    private val reloadUrlUseCase: SessionUseCases = mockk(relaxed = true)
    private val selectTabUseCase: TabsUseCases = mockk(relaxed = true)
    private val fenixBrowserUseCases: FenixBrowserUseCases = mockk(relaxed = true)
    private val settings: Settings = mockk(relaxed = true)
    private val shareUseCases: ShareUseCases = mockk(relaxed = true)
    private val searchEngine =
        SearchEngine(
            id = "test",
            name = "Test Engine",
            icon = mockk(relaxed = true),
            type = SearchEngine.Type.BUNDLED,
            resultUrls = listOf("https://example.org/?q={searchTerms}"),
        )

    private lateinit var store: BrowserStore
    private val appState: AppState = mockk(relaxed = true)
    private var showAddSearchWidgetPromptCalled = false
    private var requestSetDefaultBrowserPromptCalled = false

    @Before
    fun setup() {
        showAddSearchWidgetPromptCalled = false
        store = BrowserStore(BrowserState(search = SearchState(regionSearchEngines = listOf(searchEngine))))

        every { appStore.state } returns
            AppState(
                collections = emptyList(),
                expandedCollections = emptySet(),
                mode = BrowsingMode.Normal,
                topSites = emptyList(),
                recentTabs = emptyList(),
                bookmarks = emptyList(),
            )

        every { navController.currentDestination } returns
            mockk {
                every { id } returns R.id.homeFragment
            }
        every { activity.components.settings } returns settings
        every { activity.components.settings } returns settings
        every { activity.filesDir } returns filesDir
    }

    @Test
    fun `GIVEN browsing mode is private and collection tab cannot be restored WHEN a collection tab is opened THEN open collection in a new private tab`() {
        every { appStore.state.mode } returns BrowsingMode.Private

        val tab =
            mockk<ComponentTab> {
                every { url } returns "https://mozilla.org"
                every { restore(filesDir, engine, restoreSessionId = false) } returns null
            }
        createController().handleCollectionOpenTabClicked(tab)

        assertNotNull(Collections.tabRestored.testGetValue())
        val recordedEvents = Collections.tabRestored.testGetValue()!!
        assertEquals(1, recordedEvents.size)
        assertEquals(null, recordedEvents.single().extra)

        verify {
            navController.navigate(R.id.browserFragment)
            fenixBrowserUseCases.loadUrlOrSearch(
                searchTermOrURL = "https://mozilla.org",
                newTab = true,
                private = true,
            )
        }
    }

    @Test
    fun `GIVEN homepage as a new tab is enabled and collection tab cannot be restored WHEN a collection tab is opened THEN open collection tab in existing tab`() {
        every { settings.enableHomepageAsNewTab } returns true

        val tab =
            mockk<ComponentTab> {
                every { url } returns "https://mozilla.org"
                every { restore(filesDir, engine, restoreSessionId = false) } returns null
            }
        createController().handleCollectionOpenTabClicked(tab)

        assertNotNull(Collections.tabRestored.testGetValue())
        val recordedEvents = Collections.tabRestored.testGetValue()!!
        assertEquals(1, recordedEvents.size)
        assertEquals(null, recordedEvents.single().extra)

        verify {
            navController.navigate(R.id.browserFragment)
            fenixBrowserUseCases.loadUrlOrSearch(
                searchTermOrURL = "https://mozilla.org",
                newTab = false,
                private = false,
            )
        }
    }

    @Test
    fun `handleCollectionOpenTabClicked with existing selected tab`() {
        val recoverableTab =
            RecoverableTab(
                engineSessionState = null,
                state =
                    TabState(
                        id = "test",
                        parentId = null,
                        url = "https://www.mozilla.org",
                        title = "Mozilla",
                        contextId = null,
                        readerState = ReaderState(),
                        lastAccess = 0,
                        private = false,
                    ),
            )

        val tab =
            mockk<ComponentTab> {
                every { restore(filesDir, engine, restoreSessionId = false) } returns recoverableTab
            }

        val restoredTab = createTab(id = recoverableTab.state.id, url = recoverableTab.state.url)
        val otherTab = createTab(id = "otherTab", url = "https://mozilla.org")
        store.dispatch(TabListAction.AddTabAction(otherTab))
        store.dispatch(TabListAction.SelectTabAction(otherTab.id))
        store.dispatch(TabListAction.AddTabAction(restoredTab))

        createController().handleCollectionOpenTabClicked(tab)

        assertNotNull(Collections.tabRestored.testGetValue())
        val recordedEvents = Collections.tabRestored.testGetValue()!!
        assertEquals(1, recordedEvents.size)
        assertEquals(null, recordedEvents.single().extra)

        verify { navController.navigate(R.id.browserFragment) }
        verify { selectTabUseCase.selectTab.invoke(restoredTab.id) }
        verify { reloadUrlUseCase.reload.invoke(restoredTab.id) }
    }

    @Test
    fun `handleCollectionOpenTabClicked without existing selected tab`() {
        val recoverableTab =
            RecoverableTab(
                engineSessionState = null,
                state =
                    TabState(
                        id = "test",
                        parentId = null,
                        url = "https://www.mozilla.org",
                        title = "Mozilla",
                        contextId = null,
                        readerState = ReaderState(),
                        lastAccess = 0,
                        private = false,
                    ),
            )

        val tab =
            mockk<ComponentTab> {
                every { restore(filesDir, engine, restoreSessionId = false) } returns recoverableTab
            }

        val restoredTab = createTab(id = recoverableTab.state.id, url = recoverableTab.state.url)
        store.dispatch(TabListAction.AddTabAction(restoredTab))

        createController().handleCollectionOpenTabClicked(tab)

        assertNotNull(Collections.tabRestored.testGetValue())
        val recordedEvents = Collections.tabRestored.testGetValue()!!
        assertEquals(1, recordedEvents.size)
        assertEquals(null, recordedEvents.single().extra)

        verify { navController.navigate(R.id.browserFragment) }
        verify { selectTabUseCase.selectTab.invoke(restoredTab.id) }
        verify { reloadUrlUseCase.reload.invoke(restoredTab.id) }
    }

    @Test
    fun handleCollectionOpenTabsTapped() {
        val collection =
            mockk<TabCollection> {
                every { tabs } returns emptyList()
            }
        createController().handleCollectionOpenTabsTapped(collection)

        assertNotNull(Collections.allTabsRestored.testGetValue())
        val recordedEvents = Collections.allTabsRestored.testGetValue()!!
        assertEquals(1, recordedEvents.size)
        assertEquals(null, recordedEvents.single().extra)
    }

    @Test
    fun `handleCollectionRemoveTab one tab`() {
        val tab = mockk<ComponentTab>()

        val expectedCollection =
            mockk<TabCollection> {
                every { id } returns 123L
                every { tabs } returns listOf(tab)
            }

        var actualCollection: TabCollection? = null
        every { tabCollectionStorage.cachedTabCollections } returns listOf(expectedCollection)

        createController(
                removeCollectionWithUndo = { collection ->
                    actualCollection = collection
                }
            )
            .handleCollectionRemoveTab(expectedCollection, tab)

        assertNotNull(Collections.tabRemoved.testGetValue())
        val recordedEvents = Collections.tabRemoved.testGetValue()!!
        assertEquals(1, recordedEvents.size)
        assertEquals(null, recordedEvents.single().extra)

        assertEquals(expectedCollection, actualCollection)
    }

    @Test
    fun `handleCollectionRemoveTab multiple tabs`() {
        val collection: TabCollection = mockk(relaxed = true)
        val tab: ComponentTab = mockk(relaxed = true)
        createController().handleCollectionRemoveTab(collection, tab)

        assertNotNull(Collections.tabRemoved.testGetValue())
        val recordedEvents = Collections.tabRemoved.testGetValue()!!
        assertEquals(1, recordedEvents.size)
        assertEquals(null, recordedEvents.single().extra)
    }

    @Test
    fun `WHEN handleCollectionShareTabsClicked is called THEN share use case is invoked with the collection items and title as subject`() {
        val collectionTitle = "Reading list"
        val collection =
            mockk<TabCollection> {
                every { tabs } returns emptyList()
                every { title } returns collectionTitle
            }

        createController().handleCollectionShareTabsClicked(collection)

        assertNotNull(Collections.shared.testGetValue())
        val recordedEvents = Collections.shared.testGetValue()!!
        assertEquals(1, recordedEvents.size)
        assertEquals(null, recordedEvents.single().extra)

        verify {
            shareUseCases.shareItems(
                items = emptyList(),
                source = ShareSource.HOME,
                subject = collectionTitle,
                navigateToShareFragment = any(),
            )
        }
    }

    @Test
    fun handleDeleteCollectionTapped() {
        val expectedCollection =
            mockk<TabCollection> {
                every { title } returns "Collection"
            }

        var actualCollection: TabCollection? = null

        createController(
                removeCollectionWithUndo = { collection ->
                    actualCollection = collection
                }
            )
            .handleDeleteCollectionTapped(expectedCollection)

        assertEquals(expectedCollection, actualCollection)
        assertNotNull(Collections.removed.testGetValue())
        val recordedEvents = Collections.removed.testGetValue()!!
        assertEquals(1, recordedEvents.size)
        assertEquals(null, recordedEvents.single().extra)
    }

    @Test
    fun `GIVEN exactly the required amount of downloaded thumbnails with no errors WHEN handling wallpaper dialog THEN dialog is shown`() {
        val wallpaperState =
            WallpaperState.default.copy(
                availableWallpapers =
                    makeFakeRemoteWallpapers(
                        THUMBNAILS_SELECTION_COUNT,
                        false,
                    )
            )
        assertTrue(createController().handleShowWallpapersOnboardingDialog(wallpaperState))
    }

    @Test
    fun `GIVEN more than required amount of downloaded thumbnails with no errors WHEN handling wallpaper dialog THEN dialog is shown`() {
        val wallpaperState =
            WallpaperState.default.copy(
                availableWallpapers =
                    makeFakeRemoteWallpapers(
                        THUMBNAILS_SELECTION_COUNT,
                        false,
                    )
            )
        assertTrue(createController().handleShowWallpapersOnboardingDialog(wallpaperState))
    }

    @Test
    fun `GIVEN more than required amount of downloaded thumbnails with some errors WHEN handling wallpaper dialog THEN dialog is shown`() {
        val wallpaperState =
            WallpaperState.default.copy(
                availableWallpapers =
                    makeFakeRemoteWallpapers(
                        THUMBNAILS_SELECTION_COUNT + 2,
                        true,
                    )
            )
        assertTrue(createController().handleShowWallpapersOnboardingDialog(wallpaperState))
    }

    @Test
    fun `GIVEN fewer than the required amount of downloaded thumbnails WHEN handling wallpaper dialog THEN the dialog is not shown`() {
        val wallpaperState =
            WallpaperState.default.copy(
                availableWallpapers =
                    makeFakeRemoteWallpapers(
                        THUMBNAILS_SELECTION_COUNT - 1,
                        false,
                    )
            )
        assertFalse(createController().handleShowWallpapersOnboardingDialog(wallpaperState))
    }

    @Test
    fun `GIVEN exactly the required amount of downloaded thumbnails with errors WHEN handling wallpaper dialog THEN the dialog is not shown`() {
        val wallpaperState =
            WallpaperState.default.copy(
                availableWallpapers =
                    makeFakeRemoteWallpapers(
                        THUMBNAILS_SELECTION_COUNT,
                        true,
                    )
            )
        assertFalse(createController().handleShowWallpapersOnboardingDialog(wallpaperState))
    }

    @Test
    fun `GIVEN app is in private browsing mode WHEN handling wallpaper dialog THEN the dialog is not shown`() {
        every { appStore.state } returns AppState(mode = BrowsingMode.Private)

        val wallpaperState =
            WallpaperState.default.copy(
                availableWallpapers =
                    makeFakeRemoteWallpapers(
                        THUMBNAILS_SELECTION_COUNT,
                        false,
                    )
            )

        assertFalse(createController().handleShowWallpapersOnboardingDialog(wallpaperState))
    }

    @Test
    fun handleToggleCollectionExpanded() {
        val collection = mockk<TabCollection>()
        createController().handleToggleCollectionExpanded(collection, true)
        verify { appStore.dispatch(AppAction.CollectionExpanded(collection, true)) }
    }

    @Test
    fun `WHEN handleReportSessionMetrics is called AND there are zero recent tabs THEN report Event#RecentTabsSectionIsNotVisible`() {
        assertNull(RecentTabs.sectionVisible.testGetValue())

        every { appState.recentTabs } returns emptyList()
        createController().handleReportSessionMetrics(appState)
        assertNotNull(RecentTabs.sectionVisible.testGetValue())
        assertFalse(RecentTabs.sectionVisible.testGetValue()!!)
    }

    @Test
    fun `WHEN handleReportSessionMetrics is called AND there is at least one recent tab THEN report Event#RecentTabsSectionIsVisible`() {
        assertNull(RecentTabs.sectionVisible.testGetValue())

        val recentTab: RecentTab = mockk(relaxed = true)
        every { appState.recentTabs } returns listOf(recentTab)
        createController().handleReportSessionMetrics(appState)

        assertNotNull(RecentTabs.sectionVisible.testGetValue())
        assertTrue(RecentTabs.sectionVisible.testGetValue()!!)
    }

    @Test
    fun `WHEN handleReportSessionMetrics is called AND there are zero bookmarks THEN report Event#BookmarkCount(0)`() {
        every { appState.bookmarks } returns emptyList()
        every { appState.recentTabs } returns emptyList()
        assertNull(HomeBookmarks.bookmarksCount.testGetValue())

        createController().handleReportSessionMetrics(appState)

        assertNotNull(HomeBookmarks.bookmarksCount.testGetValue())
        assertEquals(0L, HomeBookmarks.bookmarksCount.testGetValue())
    }

    @Test
    fun `WHEN handleReportSessionMetrics is called AND there is at least one bookmark THEN report Event#BookmarkCount(1)`() {
        val bookmark: Bookmark = mockk(relaxed = true)
        every { appState.bookmarks } returns listOf(bookmark)
        every { appState.recentTabs } returns emptyList()
        assertNull(HomeBookmarks.bookmarksCount.testGetValue())

        createController().handleReportSessionMetrics(appState)

        assertNotNull(HomeBookmarks.bookmarksCount.testGetValue())
        assertEquals(1L, HomeBookmarks.bookmarksCount.testGetValue())
    }

    @Test
    fun `WHEN handleMessageClicked and handleMessageClosed are called THEN delegate to messageController`() {
        val controller = createController()
        val message = mockk<Message>()

        controller.handleMessageClicked(message)
        controller.handleMessageClosed(message)

        verify {
            messageController.onMessagePressed(message)
        }
        verify {
            messageController.onMessageDismissed(message)
        }
    }

    @Test
    fun `GIVEN item is a group WHEN onChecklistItemClicked is called THEN dispatch checklist performs the expected actions`() {
        val controller = createController()
        val group = mockk<ChecklistItem.Group>()

        controller.onChecklistItemClicked(group)

        verify { appStore.dispatch(AppAction.SetupChecklistAction.ChecklistItemClicked(group)) }
    }

    @Test
    fun `GIVEN item is a task WHEN onChecklistItemClicked is called THEN performs the expected actions`() {
        val controller = createController()
        val task = mockk<ChecklistItem.Task>()
        every { task.type } returns ChecklistItem.Task.Type.SET_AS_DEFAULT

        controller.onChecklistItemClicked(task)

        assertTrue("Should have called the new requestSetDefaultBrowserPrompt", requestSetDefaultBrowserPromptCalled)
        verify { appStore.dispatch(AppAction.SetupChecklistAction.ChecklistItemClicked(task)) }
    }

    @Test
    fun `WHEN set as default task THEN navigationActionFor calls the set to default prompt`() {
        val controller = createController()
        val task = mockk<ChecklistItem.Task>()
        every { task.type } returns ChecklistItem.Task.Type.SET_AS_DEFAULT

        controller.navigationActionFor(task)

        assertTrue("Should have called the new requestSetDefaultBrowserPrompt", requestSetDefaultBrowserPromptCalled)
    }

    @Test
    fun `WHEN sign in task THEN navigationActionFor navigates to the expected fragment`() {
        val controller = createController()
        val task = mockk<ChecklistItem.Task>()
        every { task.type } returns ChecklistItem.Task.Type.SIGN_IN
        every { navController.currentDestination } returns
            mockk {
                every { id } returns R.id.homeFragment
            }

        controller.navigationActionFor(task)

        verify {
            navController.navigate(
                HomeFragmentDirections.actionGlobalTurnOnSync(FenixFxAEntryPoint.NewUserOnboarding),
                null,
            )
        }
    }

    @Test
    fun `WHEN select theme task THEN navigationActionFor navigates to the expected fragment`() {
        val controller = createController()
        val task = mockk<ChecklistItem.Task>()
        every { task.type } returns ChecklistItem.Task.Type.SELECT_THEME
        every { navController.currentDestination } returns
            mockk {
                every { id } returns R.id.homeFragment
            }

        controller.navigationActionFor(task)

        verify {
            navController.navigate(
                HomeFragmentDirections.actionGlobalCustomizationFragment(),
                null,
            )
        }
    }

    @Test
    fun `WHEN toolbar placement task THEN navigationActionFor navigates to the expected fragment`() {
        val controller = createController()
        val task = mockk<ChecklistItem.Task>()
        every { task.type } returns ChecklistItem.Task.Type.CHANGE_TOOLBAR_PLACEMENT
        every { navController.currentDestination } returns
            mockk {
                every { id } returns R.id.homeFragment
            }

        controller.navigationActionFor(task)

        verify {
            navController.navigate(
                HomeFragmentDirections.actionGlobalCustomizationFragment(),
                null,
            )
        }
    }

    @Test
    fun `WHEN install search widget task THEN navigationActionFor calls the add search widget prompt`() {
        assertFalse("flag is false at test start", showAddSearchWidgetPromptCalled)
        val controller = createController()
        val task = mockk<ChecklistItem.Task>()
        every { task.type } returns ChecklistItem.Task.Type.INSTALL_SEARCH_WIDGET

        controller.navigationActionFor(task)

        assertTrue("showAddSearchWidgetPrompt should have been called", showAddSearchWidgetPromptCalled)
    }

    @Test
    fun `WHEN extensions task THEN navigationActionFor navigates to the expected fragment`() {
        val controller = createController()
        val task = mockk<ChecklistItem.Task>()
        every { task.type } returns ChecklistItem.Task.Type.EXPLORE_EXTENSION
        every { navController.currentDestination } returns
            mockk {
                every { id } returns R.id.homeFragment
            }

        controller.navigationActionFor(task)

        verify {
            navController.navigate(
                HomeFragmentDirections.actionGlobalAddonsManagementFragment(),
                null,
            )
        }
    }

    @Test
    fun `WHEN on remove checklist button clicked THEN onRemoveChecklistButtonClicked dispatches the expected action to the app store `() {
        val controller = createController()

        controller.onRemoveChecklistButtonClicked()

        verify { appStore.dispatch(AppAction.SetupChecklistAction.Closed) }
    }

    private fun createController(
        showTabTray: () -> Unit = {},
        removeCollectionWithUndo: (tabCollection: TabCollection) -> Unit = {},
    ): DefaultSessionControlController {
        return DefaultSessionControlController(
                activityRef = WeakReference(activity),
                settings = settings,
                engine = engine,
                store = store,
                messageController = messageController,
                tabCollectionStorage = tabCollectionStorage,
                addTabUseCase = tabsUseCases.addTab,
                restoreUseCase = mockk(relaxed = true),
                selectTabUseCase = selectTabUseCase.selectTab,
                reloadUrlUseCase = reloadUrlUseCase.reload,
                fenixBrowserUseCases = fenixBrowserUseCases,
                appStore = appStore,
                navControllerRef = WeakReference(navController),
                viewLifecycleScope = TestScope(),
                shareUseCases = shareUseCases,
                showAddSearchWidgetPrompt = { showAddSearchWidgetPromptCalled = true },
                requestSetDefaultBrowserPrompt = { requestSetDefaultBrowserPromptCalled = true },
            )
            .apply {
                registerCallback(
                    object : SessionControlControllerCallback {
                        override fun removeCollection(tabCollection: TabCollection) {
                            removeCollectionWithUndo(tabCollection)
                        }

                        override fun showTabTray() {
                            showTabTray()
                        }
                    }
                )
            }
    }

    private fun makeFakeRemoteWallpapers(size: Int, hasError: Boolean): List<Wallpaper> {
        val list = mutableListOf<Wallpaper>()
        for (i in 0 until size) {
            if (hasError && i == 0) {
                list.add(makeFakeRemoteWallpaper(Wallpaper.ImageFileState.Error))
            } else {
                list.add(makeFakeRemoteWallpaper(Wallpaper.ImageFileState.Downloaded))
            }
        }
        return list
    }

    private fun makeFakeRemoteWallpaper(
        thumbnailFileState: Wallpaper.ImageFileState = Wallpaper.ImageFileState.Unavailable
    ) =
        Wallpaper(
            name = "name",
            collection =
                Wallpaper.Collection(
                    name = Wallpaper.FIREFOX_COLLECTION,
                    heading = null,
                    description = null,
                    availableLocales = null,
                    startDate = null,
                    endDate = null,
                    learnMoreUrl = null,
                ),
            textColor = null,
            cardColorLight = null,
            cardColorDark = null,
            thumbnailFileState = thumbnailFileState,
            assetsFileState = Wallpaper.ImageFileState.Unavailable,
        )
}
