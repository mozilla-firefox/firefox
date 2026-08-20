/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.home

import android.content.Context
import androidx.annotation.VisibleForTesting
import androidx.compose.material3.SnackbarHostState
import androidx.navigation.NavController
import kotlinx.coroutines.CoroutineScope
import mozilla.components.browser.state.selector.findTab
import mozilla.components.browser.state.selector.normalTabs
import mozilla.components.browser.state.selector.privateTabs
import mozilla.components.browser.state.selector.selectedTab
import mozilla.components.browser.state.store.BrowserStore
import mozilla.components.concept.engine.utils.ABOUT_HOME_URL
import mozilla.components.feature.tabs.TabsUseCases
import mozilla.components.support.base.feature.LifecycleAwareFeature
import org.mozilla.fenix.R
import org.mozilla.fenix.browser.browsingmode.BrowsingModeManager
import org.mozilla.fenix.components.usecases.FenixBrowserUseCases
import org.mozilla.fenix.ext.actualInactiveTabs
import org.mozilla.fenix.ext.components
import org.mozilla.fenix.ext.tabClosedUndoMessage
import org.mozilla.fenix.ext.tabsClosedUndoMessage
import org.mozilla.fenix.home.HomeScreenViewModel.Companion.ALL_ACTIVE_NORMAL_TABS
import org.mozilla.fenix.home.HomeScreenViewModel.Companion.ALL_NORMAL_TABS
import org.mozilla.fenix.home.HomeScreenViewModel.Companion.ALL_PRIVATE_TABS
import org.mozilla.fenix.utils.Settings
import org.mozilla.fenix.utils.allowUndo
import org.mozilla.fenix.utils.getUndoDelay

/**
 * Delegate to handle tab removal and undo actions in the homepage.
 *
 * @param context An Android [Context].
 * @param viewModel [HomeScreenViewModel] containing the data on the sessions to delete.
 * @param browserStore The [BrowserStore] that holds the currently open tabs.
 * @param browsingModeManager [BrowsingModeManager] used for fetching the current browsing mode.
 * @param navController [NavController] used for navigation.
 * @param tabsUseCases The [TabsUseCases] instance to perform tab actions.
 * @param fenixBrowserUseCases [FenixBrowserUseCases] used for adding new homepage tabs.
 * @param settings [Settings] used to check the application shared preferences.
 * @param snackbarHostState The [SnackbarHostState] used to display snackbars.
 * @param viewLifecycleScope The [CoroutineScope] to use for launching coroutines.
 */
@Suppress("LongParameterList")
class TabsCleanupFeature(
    private val context: Context,
    private val viewModel: HomeScreenViewModel,
    private val browserStore: BrowserStore,
    private val browsingModeManager: BrowsingModeManager,
    private val navController: NavController,
    private val tabsUseCases: TabsUseCases,
    private val fenixBrowserUseCases: FenixBrowserUseCases,
    private val settings: Settings,
    private val snackbarHostState: SnackbarHostState,
    private val viewLifecycleScope: CoroutineScope,
) : LifecycleAwareFeature {

    /** Removes the sessions that have been queued for deletion when the home screen is started. */
    override fun start() {
        viewModel.sessionToDelete?.also {
            if (it == ALL_NORMAL_TABS || it == ALL_PRIVATE_TABS || it == ALL_ACTIVE_NORMAL_TABS) {
                removeAllTabsAndShowSnackbar(it)
            } else {
                removeTabAndShowSnackbar(it)
            }
        }

        viewModel.sessionToDelete = null
    }

    override fun stop() = Unit

    /**
     * Shows an undo snackbar after removing one or more tabs.
     *
     * @param message The message to display in the snackbar.
     * @param onCancel The action to perform when the user clicks the "Undo" button.
     */
    @VisibleForTesting
    internal fun showUndoSnackbar(message: String, onCancel: () -> Unit) {
        viewLifecycleScope.allowUndo(
            snackbarHostState = snackbarHostState,
            message = message,
            undoActionTitle = context.getString(R.string.snackbar_deleted_undo),
            onCancel = onCancel,
            operation = {},
            undoDelay = context.components.settings.getUndoDelay(),
        )
    }

    private fun removeAllTabsAndShowSnackbar(sessionCode: String) {
        // The undo action restores the selected tab. Check if the selected tab is a
        // homepage tab before the tabs are removed.
        val isRestoringHomepageTab = browserStore.state.selectedTab?.content?.url == ABOUT_HOME_URL

        val tabsCount =
            when (sessionCode) {
                ALL_PRIVATE_TABS -> removeAllPrivateTabs()
                ALL_NORMAL_TABS -> removeAllNormalTabs()
                ALL_ACTIVE_NORMAL_TABS -> removeAllActiveNormalTabs()
                else -> return
            }

        var tabId: String? = null
        if (settings.enableHomepageAsNewTab) {
            // Add a new tab after all the tabs are removed to ensure there's always 1 tab.
            // Hold onto the new tab ID so that the new tab can be removed if the tabs are restored
            // by the undo action.
            tabId = fenixBrowserUseCases.addNewHomepageTab(private = browsingModeManager.mode.isPrivate)
        }

        showUndoSnackbar(
            message = context.tabsClosedUndoMessage(count = tabsCount),
            onCancel = {
                onUndoTabsRemoved(
                    tabId = tabId,
                    isRestoringHomepageTab = isRestoringHomepageTab,
                )
            },
        )
    }

    private fun removeAllPrivateTabs(): Int =
        browserStore.state.privateTabs.size.also { tabsUseCases.removePrivateTabs() }

    private fun removeAllNormalTabs(): Int = browserStore.state.normalTabs.size.also { tabsUseCases.removeNormalTabs() }

    private fun removeAllActiveNormalTabs(): Int {
        val inactiveTabIds = browserStore.state.actualInactiveTabs(settings = settings).map { it.id }.toSet()
        val normalTabIds = browserStore.state.normalTabs.map { it.id }
        val tabsToRemove = normalTabIds.filter { it !in inactiveTabIds }

        tabsUseCases.removeTabs(ids = tabsToRemove, excludedTabIds = inactiveTabIds)

        return tabsToRemove.size
    }

    private fun removeTabAndShowSnackbar(sessionId: String) {
        val tab = browserStore.state.findTab(sessionId) ?: return
        val isPrivate = browsingModeManager.mode.isPrivate

        // Check if this is the last tab being removed.
        val hasTabsRemaining =
            if (isPrivate) {
                browserStore.state.privateTabs.size > 1
            } else {
                browserStore.state.normalTabs.size > 1
            }

        val inactiveTabs = browserStore.state.actualInactiveTabs(settings = settings)

        tabsUseCases.removeTab(tabId = sessionId, excludedTabIds = inactiveTabs.map { it.id }.toSet())

        var tabId = ""
        if (settings.enableHomepageAsNewTab && !hasTabsRemaining) {
            // Add a new tab if the last tab is being removed to ensure there's always 1 tab.
            // Hold onto the new tab ID so that the new tab can be removed if the tabs are restored
            // by the undo action.
            tabId = fenixBrowserUseCases.addNewHomepageTab(private = isPrivate)
        }

        showUndoSnackbar(
            message = context.tabClosedUndoMessage(tab.content.private),
            onCancel = {
                onUndoTabsRemoved(
                    tabId = tabId,
                    isRestoringHomepageTab = tab.content.url == ABOUT_HOME_URL,
                )
            },
        )
    }

    /**
     * Callback invoked when the remove tabs action is cancelled.
     *
     * @param tabId Optional ID of the tab that should be removed after the tab removal is undone.
     * @param isRestoringHomepageTab Whether the restored selected tab is a homepage tab.
     */
    @VisibleForTesting
    internal fun onUndoTabsRemoved(
        tabId: String?,
        isRestoringHomepageTab: Boolean = false,
    ) {
        tabsUseCases.undo.invoke()

        if (tabId?.isNotBlank() == true) {
            tabsUseCases.removeTab.invoke(tabId)
        }

        // The homepage is already shown when the removed tabs are undone. Only navigate to the
        // browser when restoring a non-homepage tab.
        if (!isRestoringHomepageTab) {
            navController.navigate(HomeFragmentDirections.actionGlobalBrowser(null))
        }
    }
}
