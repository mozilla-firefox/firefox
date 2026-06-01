/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.tabstray.controller

import android.content.Context
import androidx.annotation.VisibleForTesting
import androidx.navigation.NavController
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext
import mozilla.components.browser.state.action.DebugAction
import mozilla.components.browser.state.action.LastAccessAction
import mozilla.components.browser.state.selector.selectedTab
import mozilla.components.browser.state.state.BrowserState
import mozilla.components.browser.state.store.BrowserStore
import mozilla.components.browser.storage.sync.Tab
import mozilla.components.concept.base.profiler.Profiler
import mozilla.components.concept.engine.prompt.ShareData
import mozilla.components.concept.engine.utils.ABOUT_HOME_URL
import mozilla.components.feature.accounts.push.CloseTabsUseCases
import mozilla.components.feature.downloads.ui.DownloadCancelDialogFragment
import mozilla.components.feature.tabs.TabsUseCases
import mozilla.components.lib.state.DelicateAction
import mozilla.components.service.fxa.manager.FxaAccountManager
import mozilla.components.support.base.log.logger.Logger
import mozilla.telemetry.glean.private.NoExtras
import org.mozilla.fenix.GleanMetrics.Collections
import org.mozilla.fenix.GleanMetrics.Events
import org.mozilla.fenix.GleanMetrics.TabsTray
import org.mozilla.fenix.R
import org.mozilla.fenix.browser.browsingmode.BrowsingMode
import org.mozilla.fenix.browser.browsingmode.BrowsingModeManager
import org.mozilla.fenix.collections.CollectionsDialog
import org.mozilla.fenix.collections.show
import org.mozilla.fenix.components.AppStore
import org.mozilla.fenix.components.TabCollectionStorage
import org.mozilla.fenix.components.accounts.FenixFxAEntryPoint
import org.mozilla.fenix.components.appstate.AppAction
import org.mozilla.fenix.components.bookmarks.BookmarksUseCase
import org.mozilla.fenix.components.share.ShareSource
import org.mozilla.fenix.components.usecases.FenixBrowserUseCases
import org.mozilla.fenix.components.usecases.ShareUseCases
import org.mozilla.fenix.ext.DEFAULT_ACTIVE_DAYS
import org.mozilla.fenix.ext.nav
import org.mozilla.fenix.ext.openToBrowser
import org.mozilla.fenix.ext.potentialInactiveTabs
import org.mozilla.fenix.home.HomeScreenViewModel.Companion.ALL_NORMAL_TABS
import org.mozilla.fenix.home.HomeScreenViewModel.Companion.ALL_PRIVATE_TABS
import org.mozilla.fenix.share.ShareFragment
import org.mozilla.fenix.tabstray.SyncedTabsController
import org.mozilla.fenix.tabstray.browser.InactiveTabsController
import org.mozilla.fenix.tabstray.browser.TabsTrayFabController
import org.mozilla.fenix.tabstray.data.TabsTrayItem
import org.mozilla.fenix.tabstray.data.toTabList
import org.mozilla.fenix.tabstray.ext.isActiveDownload
import org.mozilla.fenix.tabstray.ext.isSelect
import org.mozilla.fenix.tabstray.redux.action.TabsTrayAction
import org.mozilla.fenix.tabstray.redux.state.Page
import org.mozilla.fenix.tabstray.redux.state.TabsTrayState
import org.mozilla.fenix.tabstray.redux.store.TabsTrayStore
import org.mozilla.fenix.tabstray.ui.TabManagementFragmentDirections
import org.mozilla.fenix.utils.Settings
import java.util.concurrent.TimeUnit
import kotlin.coroutines.CoroutineContext

internal const val INACTIVE_TABS_FEATURE_NAME = "Inactive tabs"

/**
 * Controller for handling any actions in the tab manager.
 */
interface TabManagerController :
    SyncedTabsController,
    InactiveTabsController,
    TabsTrayFabController {

    /**
     * Set the current visible tab page to the provided [page].
     *
     * @param page The page on the tab manager to focus.
     */
    fun handleTabPageClicked(page: Page)

    /**
     * Navigate from the Tab Manager to Browser.
     */
    fun handleNavigateToBrowser()

    /**
     * Navigates from the tab manager to the homepage.
     */
    fun handleNavigateToHome()

    /**
     * Deletes the [TabsTrayItem] with the specified [tabId] or calls [DownloadCancelDialogFragment]
     * if user tries to close the last private tab while private downloads are active.
     * This method has no effect if the tab does not exist.
     *
     * @param tab The [TabsTrayItem.Tab] to be removed from the Tab Manager.
     * @param source app feature from which the tab with [tabId] was closed.
     */
    fun handleTabDeletion(tab: TabsTrayItem.Tab, source: String? = null)

    /**
     * Deletes the [TabsTrayItem] with the specified [tabId]
     *
     * @param tabId The id of the [TabsTrayItem] to be removed from the Tab Manager.
     * @param source app feature from which the tab with [tabId] was closed.
     */
    fun handleDeletePrivateTabWarningAccepted(tabId: String, source: String? = null)

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
     * Navigate from the Tab Manager to Recently Closed section in the History fragment.
     */
    fun handleNavigateToRecentlyClosed()

    /**
     * Marks all selected tabs' last access timestamp to be 15 days or [numDays];
     * enough time to have a tab considered as inactive.
     *
     * ⚠️ DO NOT USE THIS OUTSIDE OF DEBUGGING/TESTING.
     *
     * @param numDays The number of days to mark a tab's last access date.
     */
    fun handleForceSelectedTabsAsInactiveClicked(numDays: Long = DEFAULT_ACTIVE_DAYS + 1)

    /**
     * Adds the provided tab to the current selection of tabs.
     *
     * @param tab [TabsTrayItem.Tab] to be selected.
     * @param source App feature from which the tab was selected.
     */
    fun handleTabSelected(
        tab: TabsTrayItem.Tab,
        source: String?,
    )

    /**
     * Handle the completion of the TabTray transition animation.
     */
    fun handleNavigationRequested()

    /**
     * Exits multi select mode when the back button was pressed.
     *
     * @return true if the button press was consumed.
     */
    fun handleBackPressed(): Boolean

    /**
     * Navigates to the sign into Sync flow
     */
    fun handleSignInClicked()

    /**
     * Called when clicking the account settings button.
     */
    fun onAccountSettingsClicked()

    /**
     * Called when clicking the tab settings button.
     */
    fun onTabSettingsClicked()

    /**
     * Called when clicking the close all tabs button.
     */
    fun onCloseAllTabsClicked(private: Boolean)

    /**
     * Called when cancelling private downloads confirmed.
     */
    fun onCloseAllPrivateTabsWarningConfirmed(private: Boolean)

    /**
     * Called when opening the recently closed tabs menu button.
     */
    fun onOpenRecentlyClosedClicked()

    /**
     * Called when the trackers blocked pill is tapped.
     */
    fun onPrivacyReportTapped()
}

/**
 * Default implementation of [TabManagerController].
 *
 * @param accountManager [FxaAccountManager] used to determine signed in status.
 * @param context [Context] used for showing dialogs.
 * @param appStore [AppStore] used to dispatch any [AppAction].
 * @param tabsTrayStore [TabsTrayStore] used to read/update the [TabsTrayState].
 * @param browserStore [BrowserStore] used to read/update the current [BrowserState].
 * @param settings [Settings] used to update any user preferences.
 * @param browsingModeManager [BrowsingModeManager] used to read/update the current [BrowsingMode].
 * @param navController [NavController] used to navigate away from the tab manager.
 * @param navigateToHomeAndDeleteSession Lambda used to return to the Homescreen and delete the current session.
 * @param profiler [Profiler] used to add profiler markers.
 * @param tabsUseCases Use case wrapper for interacting with tabs.
 * @param fenixBrowserUseCases [FenixBrowserUseCases] used for adding new homepage tabs.
 * @param shareUseCases [ShareUseCases] for sharing content via the system share sheet or the in-app [ShareFragment].
 * @param closeSyncedTabsUseCases Use cases for closing synced tabs.
 * @param addBookmarkUseCase Use case for adding a new bookmark; resolves the parent folder via
 * the shared [LastSavedFolderCache] so the tab manager's bulk save lands in the same folder as
 * single-bookmark saves from the toolbar and menu.
 * @param ioDispatcher [CoroutineContext] used for storage operations.
 * @param mainDispatcher [CoroutineContext] used for UI operations.
 * @param collectionStorage Storage layer for interacting with collections.
 * @param showUndoSnackbarForTab Lambda used to display an undo snackbar when a normal or private tab is closed.
 * @param showUndoSnackbarForInactiveTab Lambda used to display an undo snackbar when an inactive tab is closed.
 * @param showUndoSnackbarForSyncedTab Lambda used to display an undo snackbar when a synced tab is closed.
 * @property showCancelledDownloadWarning Lambda used to display a cancelled download warning.
 * @param showBookmarkSnackbar Lambda used to display a snackbar upon saving tabs as bookmarks.
 * @param showCollectionSnackbar Lambda used to display a snackbar upon successfully saving tabs
 * to a collection.
 */
@Suppress("TooManyFunctions", "LongParameterList")
class DefaultTabManagerController(
    private val accountManager: FxaAccountManager,
    private val context: Context,
    private val appStore: AppStore,
    private val tabsTrayStore: TabsTrayStore,
    private val browserStore: BrowserStore,
    private val settings: Settings,
    private val browsingModeManager: BrowsingModeManager,
    private val navController: NavController,
    private val navigateToHomeAndDeleteSession: (String) -> Unit,
    private val profiler: Profiler?,
    private val tabsUseCases: TabsUseCases,
    private val fenixBrowserUseCases: FenixBrowserUseCases,
    private val shareUseCases: ShareUseCases,
    private val closeSyncedTabsUseCases: CloseTabsUseCases,
    private val addBookmarkUseCase: BookmarksUseCase.AddBookmarksUseCase,
    private val ioDispatcher: CoroutineContext = Dispatchers.IO,
    private val mainDispatcher: CoroutineContext = Dispatchers.Main,
    private val collectionStorage: TabCollectionStorage,
    private val showUndoSnackbarForTab: (Boolean) -> Unit,
    private val showUndoSnackbarForInactiveTab: (Int) -> Unit,
    private val showUndoSnackbarForSyncedTab: (CloseTabsUseCases.UndoableOperation) -> Unit,
    internal val showCancelledDownloadWarning: (downloadCount: Int, tabId: String?, source: String?) -> Unit,
    private val showBookmarkSnackbar: (tabSize: Int, parentFolderTitle: String?) -> Unit,
    private val showCollectionSnackbar: (
        tabSize: Int,
        isNewCollection: Boolean,
    ) -> Unit,
) : TabManagerController {

    override fun handleNormalTabsFabClick() {
        openNewTab(isPrivate = false)
    }

    override fun handlePrivateTabsFabClick() {
        openNewTab(isPrivate = true)
    }

    override fun handleSyncedTabsFabClick() {
        if (!tabsTrayStore.state.sync.isSyncing) {
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
        } else {
            navController.popBackStack()
            navController.navigate(
                TabManagementFragmentDirections.actionGlobalHome(focusOnAddressBar = true),
            )
        }

        TabsTray.closed.record(NoExtras())
        profiler?.addMarker(
            "DefaultTabManagerController.onNewTabTapped",
            startTime,
        )
        sendNewTabEvent(isPrivate)
    }

    override fun handleTabPageClicked(page: Page) {
        if (page != tabsTrayStore.state.selectedPage) {
            when (page) {
                Page.NormalTabs -> TabsTray.normalModeTapped.record(NoExtras())
                Page.PrivateTabs -> TabsTray.privateModeTapped.record(NoExtras())
                Page.TabGroups -> TabsTray.tabGroupModeTapped.record(NoExtras())
                Page.SyncedTabs -> TabsTray.syncedModeTapped.record(NoExtras())
            }
        }
        tabsTrayStore.dispatch(TabsTrayAction.PageSelected(page))
    }

    override fun handleNavigateToBrowser() {
        if (!navController.popBackStack(R.id.browserFragment, false)) {
            navController.navigate(R.id.browserFragment)
        }
    }

    override fun handleNavigateToHome() {
        if (!navController.popBackStack(R.id.homeFragment, false)) {
            navController.navigate(
                TabManagementFragmentDirections.actionGlobalHome(),
            )
        }
    }

    override fun handleTabDeletion(tab: TabsTrayItem.Tab, source: String?) {
        deleteTab(tab, source, isConfirmed = false)
    }

    override fun handleDeletePrivateTabWarningAccepted(tabId: String, source: String?) {
        val privateTab = tabsTrayStore.state.privateBrowsing.tabs.find { it.id == tabId } as? TabsTrayItem.Tab

        if (privateTab == null) {
            Logger.error(
                "handleDeletePrivateTabWarningAccepted: Failed to find private tab with ID $tabId",
            )
            return
        }
        deleteTab(privateTab, source, isConfirmed = true)
    }

    private fun deleteTab(tab: TabsTrayItem.Tab, source: String?, isConfirmed: Boolean) {
        val isPrivate = tab.private
        val isNormal = !isPrivate

        val tabsRemaining = willTabsRemainAfterDeletion(isPrivate = isPrivate, closingTabIds = setOf(tab.id))

        val isCurrentTab = tabsTrayStore.state.selectedTabId == tab.id

        if (tabsRemaining || !isCurrentTab) {
            // Using isNormal here makes it read beautifully
            val excludedTabIds = if (isNormal) getExcludedNormalTabIds() else emptySet()

            tabsUseCases.removeTab(excludedTabIds = excludedTabIds, tabId = tab.id)
            showUndoSnackbarForTab(isPrivate)
        } else {
            val privateDownloads = browserStore.state.downloads.filter { map ->
                map.value.private && map.value.isActiveDownload()
            }
            if (!isConfirmed && privateDownloads.isNotEmpty()) {
                showCancelledDownloadWarning(privateDownloads.size, tab.id, source)
                return
            } else {
                dismissTabManagerAndNavigateHome(tab.id)
            }
        }
        TabsTray.closedExistingTab.record(TabsTray.ClosedExistingTabExtra(source ?: "unknown"))
        tabsTrayStore.dispatch(TabsTrayAction.ExitSelectMode)
    }

    /**
     * Calculates the IDs of normal tabs that should be protected from engine deletion.
     * This includes all inactive tabs and tabs inside open (visible) tab groups.
     */
    private fun getExcludedNormalTabIds(): Set<String> {
        val state = tabsTrayStore.state

        val inactiveTabIds = state.inactiveTabs.tabs.map { it.id }

        val openGroupTabIds = state.tabGroupState.groups
            .filterNot { it.closed }
            .toTabList()
            .map { it.id }

        return (inactiveTabIds + openGroupTabIds).toSet()
    }

    /**
     * Determines if there will be any tabs left to display after a deletion.
     * Shared between single and multiple tab deletions to ensure routing logic stays in sync.
     * When closing all normal tabs and at least 1 tab group is open, this will always return true.
     *
     * @param isPrivate Indicates whether the tabs being deleted is private.
     * @param closingTabIds The set of tab IDs of tabs that are to be deleted.
     *
     */
    private fun willTabsRemainAfterDeletion(
        isPrivate: Boolean,
        closingTabIds: Set<String>,
    ): Boolean {
        val activeTabs = if (isPrivate) {
            tabsTrayStore.state.privateBrowsing.tabs
        } else {
            tabsTrayStore.state.normalTabsState.items.toTabList()
        }

        val closingAllActiveTabs = closingTabIds.containsAll(activeTabs.map { it.id })

        return !closingAllActiveTabs
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
    internal fun deleteMultipleTabs(tabs: Collection<TabsTrayItem.Tab>) {
        val isPrivate = tabs.any { it.private }
        val isNormal = !isPrivate

        val closingTabIds = tabs.map { it.id }.toSet()

        if (willTabsRemainAfterDeletion(isPrivate = isPrivate, closingTabIds = closingTabIds)) {
            val excludedTabIds = if (isNormal) getExcludedNormalTabIds() else emptySet()

            tabsUseCases.removeTabs(excludedTabIds = excludedTabIds, ids = tabs.map { it.id })
            showUndoSnackbarForTab(isPrivate)
        } else {
            dismissTabManagerAndNavigateHome(
                if (isPrivate) ALL_PRIVATE_TABS else ALL_NORMAL_TABS,
            )
        }
    }

    override fun handleNavigateToRecentlyClosed() {
        navController.navigate(R.id.recentlyClosedFragment)
    }

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

        tabsTrayStore.dispatch(TabsTrayAction.BookmarkSelectedTabs(tabCount = tabs.size))

        // We don't combine the context with lifecycleScope so that our jobs are not cancelled
        // if we leave the fragment, i.e. we still want the bookmarks to be added if the
        // tab manager closes before the job is done.
        CoroutineScope(ioDispatcher).launch {
            Result.runCatching {
                val results = tabs.map { tab ->
                    addBookmarkUseCase(url = tab.url, title = tab.title)
                }
                val parentNode = results.firstOrNull()?.parentNode
                withContext(mainDispatcher) {
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
    internal fun showCollectionsDialog(tabs: Collection<TabsTrayItem.Tab>) {
        val tabIds = tabs.map { it.id }.toSet()
        val transformedTabs = browserStore.state.tabs.filter { it.id in tabIds }
        CollectionsDialog(
            storage = collectionStorage,
            sessionList = transformedTabs,
            onPositiveButtonClick = { id, isNewCollection ->

                // If collection is null, a new one was created.
                if (isNewCollection) {
                    Collections.saved.record(
                        Collections.SavedExtra(
                            tabsTrayStore.state.normalTabsState.tabCount.toString(),
                            tabs.size.toString(),
                        ),
                    )
                } else {
                    Collections.tabsAdded.record(
                        Collections.TabsAddedExtra(
                            tabsTrayStore.state.normalTabsState.tabCount.toString(),
                            tabs.size.toString(),
                        ),
                    )
                }
                id?.apply {
                    showCollectionSnackbar(tabs.size, isNewCollection)
                }
            },
            onNegativeButtonClick = {},
        ).show(context)
    }

    override fun handleShareSelectedTabsClicked() {
        val tabs = tabsTrayStore.state.mode.selectedTabs

        TabsTray.shareSelectedTabs.record(TabsTray.ShareSelectedTabsExtra(tabCount = tabs.size))

        val data = tabs.map {
            ShareData(url = it.url, title = it.title)
        }

        shareUseCases.shareItems(
            items = data,
            source = ShareSource.TABS_TRAY,
            isPrivate = tabs.any { it.private },
            navigateToShareFragment = {
                navController.navigate(
                    TabManagementFragmentDirections.actionGlobalShareFragment(data = data.toTypedArray()),
                )
            },
        )
    }

    @VisibleForTesting
    internal fun sendNewTabEvent(isPrivateModeSelected: Boolean) {
        if (isPrivateModeSelected) {
            TabsTray.newPrivateTabTapped.record(NoExtras())
        } else {
            TabsTray.newTabTapped.record(NoExtras())
        }
    }

    /**
     * Navigate to home and delegate the session deletion to the Home Screen.
     * */
    @VisibleForTesting
    internal fun dismissTabManagerAndNavigateHome(sessionId: String) {
        navigateToHomeAndDeleteSession(sessionId)
    }

    override fun handleSyncedTabClicked(tab: Tab) {
        Events.syncedTabOpened.record(NoExtras())

        navController.openToBrowser()

        fenixBrowserUseCases.loadUrlOrSearch(
            searchTermOrURL = tab.active().url,
            newTab = true,
        )
    }

    override fun handleSyncedTabClosed(deviceId: String, tab: Tab) {
        CoroutineScope(ioDispatcher).launch {
            val operation = closeSyncedTabsUseCases.close(deviceId, tab.active().url)
            withContext(mainDispatcher) {
                showUndoSnackbarForSyncedTab(operation)
            }
        }
    }

    override fun handleTabSelected(tab: TabsTrayItem.Tab, source: String?) {
        val selected = tabsTrayStore.state.mode.selectedTabs
        when {
            selected.isEmpty() && tabsTrayStore.state.mode.isSelect().not() -> {
                TabsTray.openedExistingTab.record(TabsTray.OpenedExistingTabExtra(source ?: "unknown"))
                tabsUseCases.selectTab(tab.id)
                val mode = BrowsingMode.fromBoolean(tab.private)
                browsingModeManager.mode = mode

                handleNavigationRequested()
            }

            tab in selected -> {
                tabsTrayStore.dispatch(TabsTrayAction.RemoveSelectTab(tab))
            }

            source != INACTIVE_TABS_FEATURE_NAME -> {
                tabsTrayStore.dispatch(TabsTrayAction.AddSelectTab(tab))
            }
        }
    }

    private fun selectedTabisHome(): Boolean {
        return browserStore.state.selectedTab?.content?.url == ABOUT_HOME_URL
    }

    override fun handleNavigationRequested() {
        if (selectedTabisHome()) {
            handleNavigateToHome()
        } else {
            handleNavigateToBrowser()
        }
    }

    override fun handleBackPressed(): Boolean {
        if (tabsTrayStore.state.mode is TabsTrayState.Mode.Select) {
            tabsTrayStore.dispatch(TabsTrayAction.ExitSelectMode)
            return true
        }
        return false
    }

    override fun handleInactiveTabClicked(tab: TabsTrayItem.Tab) {
        TabsTray.openInactiveTab.add()
        handleTabSelected(tab, INACTIVE_TABS_FEATURE_NAME)
    }

    override fun handleCloseInactiveTabClicked(tab: TabsTrayItem.Tab) {
        TabsTray.closeInactiveTab.add()
        handleTabDeletion(tab, INACTIVE_TABS_FEATURE_NAME)
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
            tabsUseCases.removeTabs(it, excludedTabIds = emptySet())
            numTabs = it.size
        }
        showUndoSnackbarForInactiveTab(numTabs)
    }

    override fun handleSignInClicked() {
        navController.navigate(
            TabManagementFragmentDirections.actionGlobalTurnOnSync(
                entrypoint = FenixFxAEntryPoint.SyncedTabsMenu,
            ),
        )
    }

    override fun onAccountSettingsClicked() {
        val isSignedIn = accountManager.authenticatedAccount() != null

        val direction = if (isSignedIn) {
            TabManagementFragmentDirections.actionGlobalAccountSettingsFragment()
        } else {
            TabManagementFragmentDirections.actionGlobalTurnOnSync(
                entrypoint = FenixFxAEntryPoint.NavigationInteraction,
            )
        }
        navController.navigate(direction)
    }

    override fun onTabSettingsClicked() {
        navController.navigate(
            TabManagementFragmentDirections.actionGlobalTabSettingsFragment(),
        )
    }

    override fun onCloseAllTabsClicked(private: Boolean) {
        closeAllTabs(private = private, isConfirmed = false)
    }

    override fun onCloseAllPrivateTabsWarningConfirmed(private: Boolean) {
        closeAllTabs(private = private, isConfirmed = true)
    }

    override fun onOpenRecentlyClosedClicked() {
        navController.navigate(
            TabManagementFragmentDirections.actionGlobalRecentlyClosed(),
        )
        Events.recentlyClosedTabsOpened.record(NoExtras())
    }

    override fun onPrivacyReportTapped() {
        val currentSessionId = browserStore.state.selectedTabId
        navController.nav(
            R.id.tabManagementFragment,
            TabManagementFragmentDirections.actionTabManagementFragmentToGlobalProtectionsDashboard(currentSessionId),
        )
    }

    /**
     * Marks the inactive tabs auto close dialog as shown and to not be displayed again.
     */
    private fun markDialogAsShown() {
        settings.hasInactiveTabsAutoCloseDialogBeenDismissed = true
    }

    /**
     * Close all tabs.
     *
     * @param private Whether to close all of the Private tabs or all of the Normal tabs.
     * @param isConfirmed: whether the user has confirmed the warning message
     */
    private fun closeAllTabs(private: Boolean, isConfirmed: Boolean) {
        val sessionsToClose = if (private) {
            ALL_PRIVATE_TABS
        } else {
            ALL_NORMAL_TABS
        }

        if (private && !isConfirmed) {
            val privateDownloads = browserStore.state.downloads.filter {
                it.value.private && it.value.isActiveDownload()
            }
            if (privateDownloads.isNotEmpty()) {
                showCancelledDownloadWarning(privateDownloads.size, null, null)
                return
            }
        }
        dismissTabManagerAndNavigateHome(sessionsToClose)
    }
}
