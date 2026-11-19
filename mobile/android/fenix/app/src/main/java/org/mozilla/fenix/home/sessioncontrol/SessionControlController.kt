/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.home.sessioncontrol

import android.app.Activity
import androidx.annotation.VisibleForTesting
import androidx.navigation.NavController
import androidx.navigation.NavDirections
import java.lang.ref.WeakReference
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.launch
import mozilla.components.browser.state.store.BrowserStore
import mozilla.components.concept.engine.Engine
import mozilla.components.concept.engine.prompt.ShareData
import mozilla.components.feature.session.SessionUseCases
import mozilla.components.feature.tab.collections.Tab as ComponentTab
import mozilla.components.feature.tab.collections.TabCollection
import mozilla.components.feature.tab.collections.ext.invoke
import mozilla.components.feature.tabs.TabsUseCases
import mozilla.components.service.nimbus.messaging.Message
import mozilla.telemetry.glean.private.NoExtras
import org.mozilla.fenix.GleanMetrics.Collections
import org.mozilla.fenix.GleanMetrics.HomeBookmarks
import org.mozilla.fenix.GleanMetrics.RecentTabs
import org.mozilla.fenix.R
import org.mozilla.fenix.components.AppStore
import org.mozilla.fenix.components.TabCollectionStorage
import org.mozilla.fenix.components.accounts.FenixFxAEntryPoint
import org.mozilla.fenix.components.appstate.AppAction
import org.mozilla.fenix.components.appstate.AppState
import org.mozilla.fenix.components.appstate.setup.checklist.ChecklistItem
import org.mozilla.fenix.components.share.ShareSource
import org.mozilla.fenix.components.usecases.FenixBrowserUseCases
import org.mozilla.fenix.components.usecases.ShareUseCases
import org.mozilla.fenix.ext.nav
import org.mozilla.fenix.home.HomeFragment
import org.mozilla.fenix.home.HomeFragmentDirections
import org.mozilla.fenix.messaging.MessageController
import org.mozilla.fenix.onboarding.WallpaperOnboardingDialogFragment.Companion.THUMBNAILS_SELECTION_COUNT
import org.mozilla.fenix.utils.Settings
import org.mozilla.fenix.wallpapers.Wallpaper
import org.mozilla.fenix.wallpapers.WallpaperState

/**
 * [HomeFragment] controller. An interface that handles the view manipulation of the Tabs triggered by the Interactor.
 */
@Suppress("TooManyFunctions")
interface SessionControlController {
    /** @see [CollectionInteractor.onCollectionOpenTabClicked] */
    fun handleCollectionOpenTabClicked(tab: ComponentTab)

    /** @see [CollectionInteractor.onCollectionOpenTabsTapped] */
    fun handleCollectionOpenTabsTapped(collection: TabCollection)

    /** @see [CollectionInteractor.onCollectionRemoveTab] */
    fun handleCollectionRemoveTab(collection: TabCollection, tab: ComponentTab)

    /** @see [CollectionInteractor.onCollectionShareTabsClicked] */
    fun handleCollectionShareTabsClicked(collection: TabCollection)

    /** @see [CollectionInteractor.onDeleteCollectionTapped] */
    fun handleDeleteCollectionTapped(collection: TabCollection)

    /** @see [CollectionInteractor.onToggleCollectionExpanded] */
    fun handleToggleCollectionExpanded(collection: TabCollection, expand: Boolean)

    /** @see [MessageCardInteractor.onMessageClicked] */
    fun handleMessageClicked(message: Message)

    /** @see [MessageCardInteractor.onMessageClosedClicked] */
    fun handleMessageClosed(message: Message)

    /** @see [WallpaperInteractor.showWallpapersOnboardingDialog] */
    fun handleShowWallpapersOnboardingDialog(state: WallpaperState): Boolean

    /** @see [SessionControlInteractor.reportSessionMetrics] */
    fun handleReportSessionMetrics(state: AppState)

    /** @see [SetupChecklistInteractor.onChecklistItemClicked] */
    fun onChecklistItemClicked(item: ChecklistItem)

    /** @see [SetupChecklistInteractor.onRemoveChecklistButtonClicked] */
    fun onRemoveChecklistButtonClicked()

    /** Registers a [SessionControlControllerCallback] to handle callbacks that are implemented in the UI layer. */
    fun registerCallback(callback: SessionControlControllerCallback)

    /** Unregisters the callback is typically called as part of cleaning up. */
    fun unregisterCallback()
}

/** Interface for [SessionControlController] callbacks that are implemented in the UI. */
interface SessionControlControllerCallback {

    /** Callback to remove collection. */
    fun removeCollection(tabCollection: TabCollection)

    /** Callback to show tab tray. */
    fun showTabTray()
}

@Suppress("TooManyFunctions", "LargeClass", "LongParameterList")
class DefaultSessionControlController(
    private val activityRef: WeakReference<Activity>,
    private val settings: Settings,
    private val engine: Engine,
    private val messageController: MessageController,
    private val store: BrowserStore,
    private val tabCollectionStorage: TabCollectionStorage,
    private val addTabUseCase: TabsUseCases.AddNewTabUseCase,
    private val restoreUseCase: TabsUseCases.RestoreUseCase,
    private val selectTabUseCase: TabsUseCases.SelectTabUseCase,
    private val reloadUrlUseCase: SessionUseCases.ReloadUrlUseCase,
    private val fenixBrowserUseCases: FenixBrowserUseCases,
    private val appStore: AppStore,
    private val navControllerRef: WeakReference<NavController>,
    private val viewLifecycleScope: CoroutineScope,
    private val shareUseCases: ShareUseCases,
    private val showAddSearchWidgetPrompt: () -> Unit,
    private val requestSetDefaultBrowserPrompt: () -> Unit,
    private val applicationScope: CoroutineScope = viewLifecycleScope,
) : SessionControlController {

    private var callback: SessionControlControllerCallback? = null
    private val activity: Activity
        get() = requireNotNull(activityRef.get())

    private val navController: NavController
        get() = requireNotNull(navControllerRef.get())

    override fun registerCallback(callback: SessionControlControllerCallback) {
        this.callback = callback
    }

    override fun unregisterCallback() {
        this.callback = null
    }

    override fun handleCollectionOpenTabClicked(tab: ComponentTab) {
        restoreUseCase.invoke(
            activity.filesDir,
            engine,
            tab,
            onTabRestored = {
                navController.navigate(R.id.browserFragment)
                selectTabUseCase.invoke(it)
                reloadUrlUseCase.invoke(it)
            },
            onFailure = {
                navController.navigate(R.id.browserFragment)
                fenixBrowserUseCases.loadUrlOrSearch(
                    searchTermOrURL = tab.url,
                    newTab = !settings.enableHomepageAsNewTab,
                    private = appStore.state.mode.isPrivate,
                )
            },
        )

        Collections.tabRestored.record(NoExtras())
    }

    override fun handleCollectionOpenTabsTapped(collection: TabCollection) {
        restoreUseCase.invoke(
            activity.filesDir,
            engine,
            collection,
            onFailure = { url ->
                addTabUseCase(url)
            },
        )

        callback?.showTabTray()
        Collections.allTabsRestored.record(NoExtras())
    }

    override fun handleCollectionRemoveTab(
        collection: TabCollection,
        tab: ComponentTab,
    ) {
        Collections.tabRemoved.record(NoExtras())

        // collection tabs hold a reference to the initial collection that could have changed since
        val updatedCollection =
            tabCollectionStorage.cachedTabCollections.firstOrNull {
                it.id == collection.id
            }

        if (updatedCollection?.tabs?.size == 1) {
            callback?.removeCollection(collection)
        } else {
            applicationScope.launch {
                tabCollectionStorage.removeTabFromCollection(collection, tab)
            }
        }
    }

    override fun handleCollectionShareTabsClicked(collection: TabCollection) {
        showShareFragment(
            collection.title,
            collection.tabs.map { ShareData(url = it.url, title = it.title, private = false) },
        )
        Collections.shared.record(NoExtras())
    }

    override fun handleDeleteCollectionTapped(collection: TabCollection) {
        callback?.removeCollection(collection)
        Collections.removed.record(NoExtras())
    }

    override fun handleShowWallpapersOnboardingDialog(state: WallpaperState): Boolean {
        return if (appStore.state.mode.isPrivate) {
            false
        } else {
            state.availableWallpapers
                .filter { wallpaper ->
                    wallpaper.thumbnailFileState == Wallpaper.ImageFileState.Downloaded
                }
                .size
                .let { downloadedCount ->
                    // We only display the dialog if enough thumbnails have been downloaded for it.
                    downloadedCount >= THUMBNAILS_SELECTION_COUNT
                }
                .also { showOnboarding ->
                    if (showOnboarding) {
                        navController.nav(
                            R.id.homeFragment,
                            HomeFragmentDirections.actionGlobalWallpaperOnboardingDialog(),
                        )
                    }
                }
        }
    }

    override fun handleToggleCollectionExpanded(collection: TabCollection, expand: Boolean) {
        appStore.dispatch(AppAction.CollectionExpanded(collection, expand))
    }

    private fun showShareFragment(shareSubject: String, data: List<ShareData>) {
        shareUseCases.shareItems(
            items = data,
            source = ShareSource.HOME,
            subject = shareSubject,
            navigateToShareFragment = {
                val directions =
                    HomeFragmentDirections.actionGlobalShareFragment(
                        sessionId = store.state.selectedTabId,
                        shareSubject = shareSubject,
                        data = data.toTypedArray(),
                    )
                navController.nav(R.id.homeFragment, directions)
            },
        )
    }

    override fun handleMessageClicked(message: Message) {
        messageController.onMessagePressed(message)
    }

    override fun handleMessageClosed(message: Message) {
        messageController.onMessageDismissed(message)
    }

    override fun handleReportSessionMetrics(state: AppState) {
        if (state.recentTabs.isEmpty()) {
            RecentTabs.sectionVisible.set(false)
        } else {
            RecentTabs.sectionVisible.set(true)
        }

        HomeBookmarks.bookmarksCount.set(state.bookmarks.size.toLong())
    }

    override fun onChecklistItemClicked(item: ChecklistItem) {
        if (item is ChecklistItem.Task) {
            // The navigation actions are required to be called on the main thread.
            navigationActionFor(item)
        }

        appStore.dispatch(AppAction.SetupChecklistAction.ChecklistItemClicked(item))
    }

    @VisibleForTesting
    internal fun navigationActionFor(item: ChecklistItem.Task) =
        when (item.type) {
            ChecklistItem.Task.Type.SET_AS_DEFAULT -> requestSetDefaultBrowserPrompt()

            ChecklistItem.Task.Type.SIGN_IN ->
                navigateTo(HomeFragmentDirections.actionGlobalTurnOnSync(FenixFxAEntryPoint.NewUserOnboarding))

            ChecklistItem.Task.Type.SELECT_THEME ->
                navigateTo(HomeFragmentDirections.actionGlobalCustomizationFragment())

            ChecklistItem.Task.Type.CHANGE_TOOLBAR_PLACEMENT ->
                navigateTo(HomeFragmentDirections.actionGlobalCustomizationFragment())

            ChecklistItem.Task.Type.INSTALL_SEARCH_WIDGET -> showAddSearchWidgetPrompt()

            ChecklistItem.Task.Type.EXPLORE_EXTENSION ->
                navigateTo(HomeFragmentDirections.actionGlobalAddonsManagementFragment())
        }

    private fun navigateTo(directions: NavDirections) = navController.nav(R.id.homeFragment, directions)

    override fun onRemoveChecklistButtonClicked() {
        appStore.dispatch(AppAction.SetupChecklistAction.Closed)
    }
}
