/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.tabstray

import androidx.annotation.VisibleForTesting
import androidx.navigation.NavController
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext
import mozilla.appservices.places.BookmarkRoot
import mozilla.components.browser.state.action.DebugAction
import mozilla.components.browser.state.action.LastAccessAction
import mozilla.components.browser.state.selector.findTab
import mozilla.components.browser.state.selector.getNormalOrPrivateTabs
import mozilla.components.browser.state.selector.normalTabs
import mozilla.components.browser.state.state.BrowserState
import mozilla.components.browser.state.state.SessionState
import mozilla.components.browser.state.state.TabSessionState
import mozilla.components.browser.state.store.BrowserStore
import mozilla.components.browser.storage.sync.Tab
import mozilla.components.concept.base.profiler.Profiler
import mozilla.components.concept.engine.mediasession.MediaSession.PlaybackState
import mozilla.components.concept.engine.prompt.ShareData
import mozilla.components.concept.storage.BookmarksStorage
import mozilla.components.feature.accounts.push.CloseTabsUseCases
import mozilla.components.feature.downloads.ui.DownloadCancelDialogFragment
import mozilla.components.feature.tabs.TabsUseCases
import mozilla.components.lib.state.DelicateAction
import mozilla.telemetry.glean.private.NoExtras
import org.mozilla.fenix.BrowserDirection
import org.mozilla.fenix.GleanMetrics.Collections
import org.mozilla.fenix.GleanMetrics.Events
import org.mozilla.fenix.GleanMetrics.TabsTray
import org.mozilla.fenix.HomeActivity
import org.mozilla.fenix.R
import org.mozilla.fenix.browser.browsingmode.BrowsingMode
import org.mozilla.fenix.browser.browsingmode.BrowsingModeManager
import org.mozilla.fenix.collections.CollectionsDialog
import org.mozilla.fenix.collections.show
import org.mozilla.fenix.components.AppStore
import org.mozilla.fenix.components.TabCollectionStorage
import org.mozilla.fenix.components.appstate.AppAction
import org.mozilla.fenix.components.usecases.FenixBrowserUseCases
import org.mozilla.fenix.ext.DEFAULT_ACTIVE_DAYS
import org.mozilla.fenix.ext.potentialInactiveTabs
import org.mozilla.fenix.home.HomeScreenViewModel.Companion.ALL_NORMAL_TABS
import org.mozilla.fenix.home.HomeScreenViewModel.Companion.ALL_PRIVATE_TABS
import org.mozilla.fenix.tabstray.browser.InactiveTabsController
import org.mozilla.fenix.tabstray.browser.TabsTrayFabController
import org.mozilla.fenix.tabstray.ext.getTabSessionState
import org.mozilla.fenix.tabstray.ext.isActiveDownload
import org.mozilla.fenix.tabstray.ext.isNormalTab
import org.mozilla.fenix.tabstray.ext.isSelect
import org.mozilla.fenix.utils.Settings
import java.util.concurrent.TimeUnit
import kotlin.coroutines.CoroutineContext
import org.mozilla.fenix.GleanMetrics.Tab as GleanTab

const val INACTIVE_TABS_FEATURE_NAME = "Inactive tabs"

/**
 * Controller for handling any actions in the tabs tray.
 */
interface TabsTrayController : SyncedTabsController, InactiveTabsController, TabsTrayFabController {

    /**
     * Set the current tray item to the clamped [position].
     *
     * @param position The position on the tray to focus.
     * @param smoothScroll If true, animate the scrolling from the current tab to [position].
     */
    fun handleTrayScrollingToPosition(position: Int, smoothScroll: Boolean)

    /**
     * Navigate from TabsTray to Browser.
     */
    fun handleNavigateToBrowser()

    /**
     * Deletes the [TabSessionState] with the specified [tabId] or calls [DownloadCancelDialogFragment]
     * if user tries to close the last private tab while private downloads are active.
     *
     * @param tabId The id of the [TabSessionState] to be removed from TabsTray.
     * @param source app feature from which the tab with [tabId] was closed.
     */
    fun handleTabDeletion(tabId: String, source: String? = null)

    /**
     * Deletes the [TabSessionState] with the specified [tabId]
     *
     * @param tabId The id of the [TabSessionState] to be removed from TabsTray.
     * @param source app feature from which the tab with [tabId] was closed.
     */
    fun handleDeleteTabWarningAccepted(tabId: String, source: String? = null)

    /**
     * Deletes the current state of selected tabs, offering an undo option.
     */
    fun handleDeleteSelectedTabsClicked()

    /**
     * Bookmarks the current set of selected tabs.
     */
    fun handleBookmarkSelectedTabsClicked()

    /**
     * Saves the current set of selected tabs to a collection.
     */
    fun handleAddSelectedTabsToCollectionClicked()

    /**
     * Shares the current set of selected tabs.
     */
    fun handleShareSelectedTabsClicked()

    /**
     * Moves [tabId] next to before/after [targetId]
     *
     * @param tabId The tab to be moved.
     * @param targetId The id of the tab that the moved tab will be placed next to.
     * @param placeAfter [Boolean] indicating whether to place the tab before or after the target.
     */
    fun handleTabsMove(tabId: String, targetId: String?, placeAfter: Boolean)

    /**
     * Navigate from TabsTray to Recently Closed section in the History fragment.
     */
    fun handleNavigateToRecentlyClosed()

    /**
     * Sets the current state of selected tabs into the inactive state.
     *
     * ⚠️ DO NOT USE THIS OUTSIDE OF DEBUGGING/TESTING.
     *
     * @param numDays The number of days to mark a tab's last access date.
     */
    fun handleForceSelectedTabsAsInactiveClicked(numDays: Long = DEFAULT_ACTIVE_DAYS + 1)

    /**
     * Handles when a tab item is click either to play/pause.
     */
    fun handleMediaClicked(tab: SessionState)

    /**
     * Adds the provided tab to the current selection of tabs.
     *
     * @param tab [TabSessionState] that was long clicked.
     */
    fun handleTabLongClick(tab: TabSessionState): Boolean

    /**
     * Adds the provided tab to the current selection of tabs.
     *
     * @param tab [TabSessionState] to be selected.
     * @param source App feature from which the tab was selected.
     */
    fun handleTabSelected(
        tab: TabSessionState,
        source: String?,
    )

    /**
     * Removes the provided tab from the current selection of tabs.
     *
     * @param tab [TabSessionState] to be unselected.
     */
    fun handleTabUnselected(tab: TabSessionState)

    /**
     * Exits multi select mode when the back button was pressed.
     *
     * @return true if the button press was consumed.
     */
    fun handleBackPressed(): Boolean
}

/**
 * Default implementation of [TabsTrayController].
 *
 * @param activity [HomeActivity] used to perform top-level app actions.
 * @param appStore [AppStore] used to dispatch any [AppAction].
 * @param tabsTrayStore [TabsTrayStore] used to read/update the [TabsTrayState].
 * @param browserStore [BrowserStore] used to read/update the current [BrowserState].
 * @param settings [Settings] used to update any user preferences.
 * @param browsingModeManager [BrowsingModeManager] used to read/update the current [BrowsingMode].
 * @param navController [NavController] used to navigate away from the tabs tray.
 * @param navigateToHomeAndDeleteSession Lambda used to return to the Homescreen and delete the current session.
 * @param profiler [Profiler] used to add profiler markers.
 * @param navigationInteractor [NavigationInteractor] used to perform navigation actions with side effects.
 * @param tabsUseCases Use case wrapper for interacting with tabs.
 * @param fenixBrowserUseCases [FenixBrowserUseCases] used for adding new homepage tabs.
 * @param bookmarksStorage Storage layer for retrieving and saving bookmarks.
 * @param closeSyncedTabsUseCases Use cases for closing synced tabs.
 * @param ioDispatcher [CoroutineContext] used for storage operations.
 * @param collectionStorage Storage layer for interacting with collections.
 * @param dismissTray Lambda used to dismiss/minimize the tabs tray.
 * @param showUndoSnackbarForTab Lambda used to display an undo snackbar when a normal or private tab is closed.
 * @param showUndoSnackbarForInactiveTab Lambda used to display an undo snackbar when an inactive tab is closed.
 * @param showUndoSnackbarForSyncedTab Lambda used to display an undo snackbar when a synced tab is closed.
 * @property showCancelledDownloadWarning Lambda used to display a cancelled download warning.
 * @param showBookmarkSnackbar Lambda used to display a snackbar upon saving tabs as bookmarks.
 * @param showCollectionSnackbar Lambda used to display a snackbar upon successfully saving tabs
 * to a collection.
 */
@Suppress("TooManyFunctions", "LongParameterList")
class DefaultTabsTrayController(
    private val activity: HomeActivity,
    private val appStore: AppStore,
    private val tabsTrayStore: TabsTrayStore,
    private val browserStore: BrowserStore,
    private val settings: Settings,
    private val browsingModeManager: BrowsingModeManager,
    private val navController: NavController,
    private val navigateToHomeAndDeleteSession: (String) -> Unit,
    private val profiler: Profiler?,
    private val navigationInteractor: NavigationInteractor,
    private val tabsUseCases: TabsUseCases,
    private val fenixBrowserUseCases: FenixBrowserUseCases,
    private val bookmarksStorage: BookmarksStorage,
    private val closeSyncedTabsUseCases: CloseTabsUseCases,
    private val ioDispatcher: CoroutineContext,
    private val collectionStorage: TabCollectionStorage,
    private val dismissTray: () -> Unit,
    private val showUndoSnackbarForTab: (Boolean) -> Unit,
    private val showUndoSnackbarForInactiveTab: (Int) -> Unit,
    private val showUndoSnackbarForSyncedTab: (CloseTabsUseCases.UndoableOperation) -> Unit,
    internal val showCancelledDownloadWarning: (downloadCount: Int, tabId: String?, source: String?) -> Unit,
    private val showBookmarkSnackbar: (tabSize: Int, parentFolderTitle: String?) -> Unit,
    private val showCollectionSnackbar: (
        tabSize: Int,
        isNewCollection: Boolean,
    ) -> Unit,
) : TabsTrayController {

    override fun handleNormalTabsFabClick() {
        openNewTab(isPrivate = false)
    }

    override fun handlePrivateTabsFabClick() {
        openNewTab(isPrivate = true)
    }

    override fun handleSyncedTabsFabClick() {
        if (!tabsTrayStore.state.syncing) {
            tabsTrayStore.dispatch(TabsTrayAction.SyncNow)
        }
    }

    /**
     * Opens a new tab.
     *
     * @param isPrivate [Boolean] indicating whether the new tab is private.
     */
    private fun openNewTab(isPrivate: Boolean) {
        val startTime = profiler?.getProfilerTime()
        browsingModeManager.mode = BrowsingMode.fromBoolean(isPrivate)

        if (settings.enableHomepageAsNewTab) {
            fenixBrowserUseCases.addNewHomepageTab(
                private = isPrivate,
            )
        }

        navController.navigate(
            TabsTrayFragmentDirections.actionGlobalHome(focusOnAddressBar = true),
        )
        navigationInteractor.onTabTrayDismissed()
        profiler?.addMarker(
            "DefaultTabTrayController.onNewTabTapped",
            startTime,
        )
        sendNewTabEvent(isPrivate)
    }

    override fun handleTrayScrollingToPosition(position: Int, smoothScroll: Boolean) {
        val page = Page.positionToPage(position)

        if (page != tabsTrayStore.state.selectedPage) {
            when (page) {
                Page.NormalTabs -> TabsTray.normalModeTapped.record(NoExtras())
                Page.PrivateTabs -> TabsTray.privateModeTapped.record(NoExtras())
                Page.SyncedTabs -> TabsTray.syncedModeTapped.record(NoExtras())
            }
        }
        tabsTrayStore.dispatch(TabsTrayAction.PageSelected(page))
    }

    /**
     * Dismisses the tabs tray and navigates to the browser.
     */
    override fun handleNavigateToBrowser() {
        dismissTray()

        if (navController.currentDestination?.id == R.id.browserFragment) {
            return
        } else if (!navController.popBackStack(R.id.browserFragment, false)) {
            navController.navigate(R.id.browserFragment)
        }
    }

    /**
     * Deletes the [TabSessionState] with the specified [tabId].
     *
     * @param tabId The id of the [TabSessionState] to be removed from TabsTray.
     * @param source app feature from which the tab with [tabId] was closed.
     * This method has no effect if the tab does not exist.
     */
    override fun handleTabDeletion(tabId: String, source: String?) {
        deleteTab(tabId, source, isConfirmed = false)
    }

    override fun handleDeleteTabWarningAccepted(tabId: String, source: String?) {
        deleteTab(tabId, source, isConfirmed = true)
    }

    private fun deleteTab(tabId: String, source: String?, isConfirmed: Boolean) {
        val tab = browserStore.state.findTab(tabId)

        tab?.let {
            val isLastTab = browserStore.state.getNormalOrPrivateTabs(it.content.private).size == 1
            val isCurrentTab = browserStore.state.selectedTabId.equals(tabId)
            if (!isLastTab || !isCurrentTab) {
                tabsUseCases.removeTab(tabId)
                showUndoSnackbarForTab(it.content.private)
            } else {
                val privateDownloads = browserStore.state.downloads.filter { map ->
                    map.value.private && map.value.isActiveDownload()
                }
                if (!isConfirmed && privateDownloads.isNotEmpty()) {
                    showCancelledDownloadWarning(privateDownloads.size, tabId, source)
                    return
                } else {
                    dismissTabsTrayAndNavigateHome(tabId)
                }
            }
            TabsTray.closedExistingTab.record(TabsTray.ClosedExistingTabExtra(source ?: "unknown"))
        }

        tabsTrayStore.dispatch(TabsTrayAction.ExitSelectMode)
    }

    override fun handleDeleteSelectedTabsClicked() {
        val tabs = tabsTrayStore.state.mode.selectedTabs

        TabsTray.closeSelectedTabs.record(TabsTray.CloseSelectedTabsExtra(tabCount = tabs.size))

        deleteMultipleTabs(tabs)

        tabsTrayStore.dispatch(TabsTrayAction.ExitSelectMode)
    }

    /**
     * Helper function to delete multiple tabs and offer an undo option.
     */
    @VisibleForTesting
    internal fun deleteMultipleTabs(tabs: Collection<TabSessionState>) {
        val isPrivate = tabs.any { it.content.private }

        // If user closes all the tabs from selected tabs page dismiss tray and navigate home.
        if (tabs.size == browserStore.state.getNormalOrPrivateTabs(isPrivate).size) {
            dismissTabsTrayAndNavigateHome(
                if (isPrivate) ALL_PRIVATE_TABS else ALL_NORMAL_TABS,
            )
        } else {
            tabs.map { it.id }.let {
                tabsUseCases.removeTabs(it)
            }
        }
        showUndoSnackbarForTab(isPrivate)
    }

    override fun handleTabsMove(
        tabId: String,
        targetId: String?,
        placeAfter: Boolean,
    ) {
        if (targetId != null && tabId != targetId) {
            tabsUseCases.moveTabs(listOf(tabId), targetId, placeAfter)
        }
    }

    /**
     * Dismisses the tabs tray and navigates to the Recently Closed section in the History fragment.
     */
    override fun handleNavigateToRecentlyClosed() {
        dismissTray()

        navController.navigate(R.id.recentlyClosedFragment)
    }

    /**
     * Marks all selected tabs with the [TabSessionState.lastAccess] to 15 days or [numDays]; enough time to
     * have a tab considered as inactive.
     *
     * ⚠️ DO NOT USE THIS OUTSIDE OF DEBUGGING/TESTING.
     *
     * @param numDays The number of days to mark a tab's last access date.
     */
    @OptIn(DelicateAction::class)
    override fun handleForceSelectedTabsAsInactiveClicked(numDays: Long) {
        val tabs = tabsTrayStore.state.mode.selectedTabs
        val currentTabId = browserStore.state.selectedTabId
        tabs
            .filterNot { it.id == currentTabId }
            .forEach { tab ->
                val daysSince = System.currentTimeMillis() - TimeUnit.DAYS.toMillis(numDays)
                browserStore.apply {
                    dispatch(LastAccessAction.UpdateLastAccessAction(tab.id, daysSince))
                    dispatch(DebugAction.UpdateCreatedAtAction(tab.id, daysSince))
                }
            }

        tabsTrayStore.dispatch(TabsTrayAction.ExitSelectMode)
    }

    override fun handleBookmarkSelectedTabsClicked() {
        val tabs = tabsTrayStore.state.mode.selectedTabs

        TabsTray.bookmarkSelectedTabs.record(TabsTray.BookmarkSelectedTabsExtra(tabCount = tabs.size))

        // We don't combine the context with lifecycleScope so that our jobs are not cancelled
        // if we leave the fragment, i.e. we still want the bookmarks to be added if the
        // tabs tray closes before the job is done.
        CoroutineScope(ioDispatcher).launch {
            Result.runCatching {
                val parentGuid = bookmarksStorage
                    .getRecentBookmarks(1)
                    .firstOrNull()
                    ?.parentGuid
                    ?: BookmarkRoot.Mobile.id

                val parentNode = bookmarksStorage.getBookmark(parentGuid)

                tabs.forEach { tab ->
                    bookmarksStorage.addItem(
                        parentGuid = parentNode!!.guid,
                        url = tab.content.url,
                        title = tab.content.title,
                        position = null,
                    )
                }
                withContext(Dispatchers.Main) {
                    showBookmarkSnackbar(tabs.size, parentNode?.title)
                }
            }.getOrElse {
                // silently fail
            }
        }

        tabsTrayStore.dispatch(TabsTrayAction.ExitSelectMode)
    }

    override fun handleAddSelectedTabsToCollectionClicked() {
        val tabs = tabsTrayStore.state.mode.selectedTabs

        TabsTray.selectedTabsToCollection.record(TabsTray.SelectedTabsToCollectionExtra(tabCount = tabs.size))
        TabsTray.saveToCollection.record(NoExtras())

        tabsTrayStore.dispatch(TabsTrayAction.ExitSelectMode)

        showCollectionsDialog(tabs)
    }

    @VisibleForTesting
    internal fun showCollectionsDialog(tabs: Collection<TabSessionState>) {
        CollectionsDialog(
            storage = collectionStorage,
            sessionList = browserStore.getTabSessionState(tabs),
            onPositiveButtonClick = { id, isNewCollection ->

                // If collection is null, a new one was created.
                if (isNewCollection) {
                    Collections.saved.record(
                        Collections.SavedExtra(
                            browserStore.state.normalTabs.size.toString(),
                            tabs.size.toString(),
                        ),
                    )
                } else {
                    Collections.tabsAdded.record(
                        Collections.TabsAddedExtra(
                            browserStore.state.normalTabs.size.toString(),
                            tabs.size.toString(),
                        ),
                    )
                }
                id?.apply {
                    showCollectionSnackbar(tabs.size, isNewCollection)
                }
            },
            onNegativeButtonClick = {},
        ).show(activity)
    }

    override fun handleShareSelectedTabsClicked() {
        val tabs = tabsTrayStore.state.mode.selectedTabs

        TabsTray.shareSelectedTabs.record(TabsTray.ShareSelectedTabsExtra(tabCount = tabs.size))

        val data = tabs.map {
            ShareData(url = it.content.url, title = it.content.title)
        }
        val directions = TabsTrayFragmentDirections.actionGlobalShareFragment(
            data = data.toTypedArray(),
        )
        navController.navigate(directions)
    }

    @VisibleForTesting
    internal fun sendNewTabEvent(isPrivateModeSelected: Boolean) {
        if (isPrivateModeSelected) {
            TabsTray.newPrivateTabTapped.record(NoExtras())
        } else {
            TabsTray.newTabTapped.record(NoExtras())
        }
    }

    @VisibleForTesting
    internal fun dismissTabsTrayAndNavigateHome(sessionId: String) {
        dismissTray()
        navigateToHomeAndDeleteSession(sessionId)
    }

    override fun handleMediaClicked(tab: SessionState) {
        when (tab.mediaSessionState?.playbackState) {
            PlaybackState.PLAYING -> {
                GleanTab.mediaPause.record(NoExtras())
                tab.mediaSessionState?.controller?.pause()
            }

            PlaybackState.PAUSED -> {
                GleanTab.mediaPlay.record(NoExtras())
                tab.mediaSessionState?.controller?.play()
            }
            else -> throw AssertionError(
                "Play/Pause button clicked without play/pause state.",
            )
        }
    }

    override fun handleSyncedTabClicked(tab: Tab) {
        Events.syncedTabOpened.record(NoExtras())

        dismissTray()
        activity.openToBrowserAndLoad(
            searchTermOrURL = tab.active().url,
            newTab = true,
            from = BrowserDirection.FromTabsTray,
        )
    }

    override fun handleSyncedTabClosed(deviceId: String, tab: Tab) {
        CoroutineScope(ioDispatcher).launch {
            val operation = closeSyncedTabsUseCases.close(deviceId, tab.active().url)
            withContext(Dispatchers.Main) {
                showUndoSnackbarForSyncedTab(operation)
            }
        }
    }

    override fun handleTabLongClick(tab: TabSessionState): Boolean {
        return if (tab.isNormalTab() && tabsTrayStore.state.mode.selectedTabs.isEmpty()) {
            Collections.longPress.record(NoExtras())
            tabsTrayStore.dispatch(TabsTrayAction.AddSelectTab(tab))
            true
        } else {
            false
        }
    }

    override fun handleTabSelected(tab: TabSessionState, source: String?) {
        val selected = tabsTrayStore.state.mode.selectedTabs
        when {
            selected.isEmpty() && tabsTrayStore.state.mode.isSelect().not() -> {
                TabsTray.openedExistingTab.record(TabsTray.OpenedExistingTabExtra(source ?: "unknown"))
                tabsUseCases.selectTab(tab.id)
                val mode = BrowsingMode.fromBoolean(tab.content.private)
                browsingModeManager.mode = mode
                appStore.dispatch(AppAction.ModeChange(mode))
                handleNavigateToBrowser()
            }
            tab.id in selected.map { it.id } -> handleTabUnselected(tab)
            source != INACTIVE_TABS_FEATURE_NAME -> {
                tabsTrayStore.dispatch(TabsTrayAction.AddSelectTab(tab))
            }
        }
    }

    override fun handleTabUnselected(tab: TabSessionState) {
        tabsTrayStore.dispatch(TabsTrayAction.RemoveSelectTab(tab))
    }

    override fun handleBackPressed(): Boolean {
        if (tabsTrayStore.state.mode is TabsTrayState.Mode.Select) {
            tabsTrayStore.dispatch(TabsTrayAction.ExitSelectMode)
            return true
        }
        return false
    }

    override fun handleInactiveTabClicked(tab: TabSessionState) {
        TabsTray.openInactiveTab.add()
        handleTabSelected(tab, INACTIVE_TABS_FEATURE_NAME)
    }

    override fun handleCloseInactiveTabClicked(tab: TabSessionState) {
        TabsTray.closeInactiveTab.add()
        handleTabDeletion(tab.id, INACTIVE_TABS_FEATURE_NAME)
    }

    override fun handleInactiveTabsHeaderClicked(expanded: Boolean) {
        appStore.dispatch(AppAction.UpdateInactiveExpanded(expanded))

        when (expanded) {
            true -> TabsTray.inactiveTabsExpanded.record(NoExtras())
            false -> TabsTray.inactiveTabsCollapsed.record(NoExtras())
        }
    }

    override fun handleInactiveTabsAutoCloseDialogDismiss() {
        markDialogAsShown()
        TabsTray.autoCloseDimissed.record(NoExtras())
    }

    override fun handleEnableInactiveTabsAutoCloseClicked() {
        markDialogAsShown()
        settings.closeTabsAfterOneMonth = true
        settings.closeTabsAfterOneWeek = false
        settings.closeTabsAfterOneDay = false
        settings.manuallyCloseTabs = false
        TabsTray.autoCloseTurnOnClicked.record(NoExtras())
    }

    override fun handleDeleteAllInactiveTabsClicked() {
        val numTabs: Int
        TabsTray.closeAllInactiveTabs.record(NoExtras())
        browserStore.state.potentialInactiveTabs.map { it.id }.let {
            tabsUseCases.removeTabs(it)
            numTabs = it.size
        }
        showUndoSnackbarForInactiveTab(numTabs)
    }

    /**
     * Marks the inactive tabs auto close dialog as shown and to not be displayed again.
     */
    private fun markDialogAsShown() {
        settings.hasInactiveTabsAutoCloseDialogBeenDismissed = true
    }
}
