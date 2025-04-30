/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.components.toolbar

import android.content.Context
import androidx.annotation.VisibleForTesting
import androidx.lifecycle.Lifecycle.State.RESUMED
import androidx.lifecycle.LifecycleOwner
import androidx.lifecycle.ViewModel
import androidx.lifecycle.ViewModelProvider
import androidx.lifecycle.lifecycleScope
import androidx.lifecycle.repeatOnLifecycle
import androidx.navigation.NavController
import kotlinx.coroutines.flow.distinctUntilChangedBy
import kotlinx.coroutines.launch
import mozilla.components.browser.state.selector.getNormalOrPrivateTabs
import mozilla.components.browser.state.selector.normalTabs
import mozilla.components.browser.state.selector.privateTabs
import mozilla.components.browser.state.selector.selectedTab
import mozilla.components.browser.state.store.BrowserStore
import mozilla.components.browser.thumbnails.BrowserThumbnails
import mozilla.components.compose.browser.toolbar.concept.Action
import mozilla.components.compose.browser.toolbar.concept.Action.ActionButton
import mozilla.components.compose.browser.toolbar.concept.Action.TabCounterAction
import mozilla.components.compose.browser.toolbar.store.BrowserDisplayToolbarAction.BrowserActionsEndUpdated
import mozilla.components.compose.browser.toolbar.store.BrowserDisplayToolbarAction.UpdateProgressBarConfig
import mozilla.components.compose.browser.toolbar.store.BrowserToolbarAction
import mozilla.components.compose.browser.toolbar.store.BrowserToolbarInteraction.BrowserToolbarEvent
import mozilla.components.compose.browser.toolbar.store.BrowserToolbarInteraction.BrowserToolbarMenu
import mozilla.components.compose.browser.toolbar.store.BrowserToolbarMenuItem.BrowserToolbarMenuButton
import mozilla.components.compose.browser.toolbar.store.BrowserToolbarMenuItem.BrowserToolbarMenuDivider
import mozilla.components.compose.browser.toolbar.store.BrowserToolbarState
import mozilla.components.compose.browser.toolbar.store.BrowserToolbarStore
import mozilla.components.compose.browser.toolbar.store.ProgressBarConfig
import mozilla.components.compose.browser.toolbar.store.ProgressBarGravity
import mozilla.components.feature.tabs.TabsUseCases
import mozilla.components.lib.state.Middleware
import mozilla.components.lib.state.MiddlewareContext
import mozilla.components.lib.state.ext.flow
import org.mozilla.fenix.R
import org.mozilla.fenix.browser.BrowserFragmentDirections
import org.mozilla.fenix.browser.browsingmode.BrowsingMode
import org.mozilla.fenix.browser.browsingmode.BrowsingMode.Normal
import org.mozilla.fenix.browser.browsingmode.BrowsingMode.Private
import org.mozilla.fenix.browser.browsingmode.BrowsingModeManager
import org.mozilla.fenix.browser.store.BrowserScreenAction
import org.mozilla.fenix.browser.store.BrowserScreenStore
import org.mozilla.fenix.components.AppStore
import org.mozilla.fenix.components.appstate.AppAction.CurrentTabClosed
import org.mozilla.fenix.components.menu.MenuAccessPoint
import org.mozilla.fenix.components.toolbar.DisplayActions.MenuClicked
import org.mozilla.fenix.components.toolbar.TabCounterInteractions.AddNewPrivateTab
import org.mozilla.fenix.components.toolbar.TabCounterInteractions.AddNewTab
import org.mozilla.fenix.components.toolbar.TabCounterInteractions.CloseCurrentTab
import org.mozilla.fenix.components.toolbar.TabCounterInteractions.TabCounterClicked
import org.mozilla.fenix.components.toolbar.navbar.shouldAddNavigationBar
import org.mozilla.fenix.ext.nav
import org.mozilla.fenix.tabstray.Page
import org.mozilla.fenix.tabstray.ext.isActiveDownload
import org.mozilla.fenix.utils.Settings
import mozilla.components.ui.icons.R as iconsR

@VisibleForTesting
internal sealed class DisplayActions : BrowserToolbarEvent {
    data object MenuClicked : DisplayActions()
}

@VisibleForTesting
internal sealed class TabCounterInteractions : BrowserToolbarEvent {
    data object TabCounterClicked : TabCounterInteractions()
    data object AddNewTab : TabCounterInteractions()
    data object AddNewPrivateTab : TabCounterInteractions()
    data object CloseCurrentTab : TabCounterInteractions()
}

/**
 * [Middleware] responsible for configuring and handling interactions with the composable toolbar.
 *
 * This is also a [ViewModel] allowing to be easily persisted between activity restarts.
 */
class BrowserToolbarMiddleware(
    private val appStore: AppStore,
    private val browserScreenStore: BrowserScreenStore,
    private val browserStore: BrowserStore,
    private val tabsUseCases: TabsUseCases,
    private val settings: Settings,
) : Middleware<BrowserToolbarState, BrowserToolbarAction>, ViewModel() {
    private lateinit var dependencies: LifecycleDependencies
    private var store: BrowserToolbarStore? = null

    /**
     * Updates the [LifecycleDependencies] of this middleware.
     *
     * @param dependencies The new [LifecycleDependencies].
     */
    fun updateLifecycleDependencies(dependencies: LifecycleDependencies) {
        this.dependencies = dependencies

        updateProgressBar()
        updateToolbarActionsBasedOnOrientation()
        updateTabsCount()
        observeAcceptingCancellingPrivateDownloads()
    }

    override fun invoke(
        context: MiddlewareContext<BrowserToolbarState, BrowserToolbarAction>,
        next: (BrowserToolbarAction) -> Unit,
        action: BrowserToolbarAction,
    ) {
        when (action) {
            is BrowserToolbarAction.Init -> {
                store = context.store as BrowserToolbarStore

                updateEndBrowserActions()
            }
            is MenuClicked -> {
                dependencies.navController.nav(
                    R.id.browserFragment,
                    BrowserFragmentDirections.actionGlobalMenuDialogFragment(
                        accesspoint = MenuAccessPoint.Browser,
                    ),
                )
            }

            is TabCounterClicked -> {
                dependencies.thumbnailsFeature?.requestScreenshot()

                dependencies.navController.nav(
                    R.id.browserFragment,
                    BrowserFragmentDirections.actionGlobalTabsTrayFragment(
                        page = when (dependencies.browsingModeManager.mode) {
                            Normal -> Page.NormalTabs
                            Private -> Page.PrivateTabs
                        },
                    ),
                )
            }
            is AddNewTab -> {
                openNewTab(Normal)
            }
            is AddNewPrivateTab -> {
                openNewTab(Private)
            }
            is CloseCurrentTab -> {
                browserStore.state.selectedTab?.let { selectedTab ->
                    val isLastTab = browserStore.state.getNormalOrPrivateTabs(selectedTab.content.private).size == 1

                    if (!isLastTab) {
                        tabsUseCases.removeTab(selectedTab.id, selectParentIfExists = true)
                        appStore.dispatch(CurrentTabClosed(selectedTab.content.private))
                        return@let
                    }

                    if (!selectedTab.content.private) {
                        dependencies.navController.navigate(
                            BrowserFragmentDirections.actionGlobalHome(
                                sessionToDelete = selectedTab.id,
                            ),
                        )
                        return@let
                    }

                    val privateDownloads = browserStore.state.downloads.filter {
                        it.value.private && it.value.isActiveDownload()
                    }
                    if (privateDownloads.isNotEmpty() && !browserScreenStore.state.cancelPrivateDownloadsAccepted) {
                        browserScreenStore.dispatch(
                            BrowserScreenAction.ClosingLastPrivateTab(
                                tabId = selectedTab.id,
                                inProgressPrivateDownloads = privateDownloads.size,
                            ),
                        )
                    } else {
                        dependencies.navController.navigate(
                            BrowserFragmentDirections.actionGlobalHome(
                                sessionToDelete = selectedTab.id,
                            ),
                        )
                    }
                }
            }

            else -> next(action)
        }
    }

    private fun getCurrentNumberOfOpenedTabs() = when (dependencies.browsingModeManager.mode) {
        Normal -> browserStore.state.normalTabs.size
        Private -> browserStore.state.privateTabs.size
    }

    private fun updateEndBrowserActions() = store?.dispatch(
        BrowserActionsEndUpdated(
            buildEndBrowserActions(getCurrentNumberOfOpenedTabs()),
        ),
    )

    private fun buildEndBrowserActions(tabsCount: Int): List<Action> =
        when (!dependencies.context.shouldAddNavigationBar()) {
            true -> listOf(
                TabCounterAction(
                    count = tabsCount,
                    contentDescription = dependencies.context.getString(
                        R.string.mozac_tab_counter_open_tab_tray,
                        tabsCount.toString(),
                    ),
                    showPrivacyMask = dependencies.browsingModeManager.mode == Private,
                    onClick = TabCounterClicked,
                    onLongClick = buildTabCounterMenu(),
                ),
                ActionButton(
                    icon = R.drawable.mozac_ic_ellipsis_vertical_24,
                    contentDescription = R.string.content_description_menu,
                    tint = R.attr.actionPrimary,
                    onClick = MenuClicked,
                ),
            )

            false -> emptyList()
        }

    private fun buildTabCounterMenu() = BrowserToolbarMenu {
        listOf(
            BrowserToolbarMenuButton(
                iconResource = iconsR.drawable.mozac_ic_plus_24,
                text = R.string.mozac_browser_menu_new_tab,
                contentDescription = R.string.mozac_browser_menu_new_tab,
                onClick = AddNewTab,
            ),

            BrowserToolbarMenuButton(
                iconResource = iconsR.drawable.mozac_ic_private_mode_24,
                text = R.string.mozac_browser_menu_new_private_tab,
                contentDescription = R.string.mozac_browser_menu_new_private_tab,
                onClick = AddNewPrivateTab,
            ),

            BrowserToolbarMenuDivider,

            BrowserToolbarMenuButton(
                iconResource = iconsR.drawable.mozac_ic_cross_24,
                text = R.string.mozac_close_tab,
                contentDescription = R.string.mozac_close_tab,
                onClick = CloseCurrentTab,
            ),
        )
    }

    private fun buildProgressBar(progress: Int = 0) = ProgressBarConfig(
        progress = progress,
        gravity = when (settings.shouldUseBottomToolbar) {
            true -> ProgressBarGravity.Top
            false -> ProgressBarGravity.Bottom
        },
    )

    private fun openNewTab(browsingMode: BrowsingMode) {
        dependencies.browsingModeManager.mode = browsingMode
        dependencies.navController.navigate(
            BrowserFragmentDirections.actionGlobalHome(focusOnAddressBar = true),
        )
    }

    private fun updateProgressBar() {
        with(dependencies.lifecycleOwner) {
            lifecycleScope.launch {
                repeatOnLifecycle(RESUMED) {
                    browserStore.flow()
                        .distinctUntilChangedBy { it.selectedTab?.content?.progress }
                        .collect {
                            store?.dispatch(
                                UpdateProgressBarConfig(
                                    buildProgressBar(it.selectedTab?.content?.progress ?: 0),
                                ),
                            )
                        }
                }
            }
        }
    }

    private fun updateToolbarActionsBasedOnOrientation() {
        with(dependencies.lifecycleOwner) {
            lifecycleScope.launch {
                repeatOnLifecycle(RESUMED) {
                    appStore.flow()
                        .distinctUntilChangedBy { it.orientation }
                        .collect {
                            updateEndBrowserActions()
                        }
                }
            }
        }
    }

    private fun updateTabsCount() {
        with(dependencies.lifecycleOwner) {
            this.lifecycleScope.launch {
                repeatOnLifecycle(RESUMED) {
                    browserStore.flow()
                        .distinctUntilChangedBy { it.tabs }
                        .collect {
                            updateEndBrowserActions()
                        }
                }
            }
        }
    }

    private fun observeAcceptingCancellingPrivateDownloads() {
        with(dependencies.lifecycleOwner) {
            lifecycleScope.launch {
                repeatOnLifecycle(RESUMED) {
                    browserScreenStore.flow()
                        .distinctUntilChangedBy { it.cancelPrivateDownloadsAccepted }
                        .collect {
                            if (it.cancelPrivateDownloadsAccepted) {
                                store?.dispatch(CloseCurrentTab)
                            }
                        }
                }
            }
        }
    }

    /**
     * Lifecycle dependencies for the [BrowserToolbarMiddleware].
     *
     * @property context [Context] used for various system interactions.
     * @property lifecycleOwner [LifecycleOwner] depending on which lifecycle related operations will be scheduled.
     * @property navController [NavController] to use for navigating to other in-app destinations.
     * @property browsingModeManager [BrowsingModeManager] for querying the current browsing mode.
     * @property thumbnailsFeature [BrowserThumbnails] for requesting screenshots of the current tab.
     */
    data class LifecycleDependencies(
        val context: Context,
        val lifecycleOwner: LifecycleOwner,
        val navController: NavController,
        val browsingModeManager: BrowsingModeManager,
        val thumbnailsFeature: BrowserThumbnails?,
    )

    /**
     * Static functionalities of the [BrowserToolbarMiddleware].
     */
    companion object {
        /**
         * [ViewModelProvider.Factory] for creating a [BrowserToolbarMiddleware].
         *
         * @param appStore [AppStore] to sync from.
         * @param browserScreenStore [BrowserScreenStore] used for integration with other
         * browser screen functionalities.
         * @param browserStore [BrowserStore] to sync from.
         * @param tabsUseCases [TabsUseCases] for managing tabs.
         * @param settings [Settings] for accessing user preferences.
         */
        fun viewModelFactory(
            appStore: AppStore,
            browserScreenStore: BrowserScreenStore,
            browserStore: BrowserStore,
            tabsUseCases: TabsUseCases,
            settings: Settings,
        ): ViewModelProvider.Factory = object : ViewModelProvider.Factory {
            @Suppress("UNCHECKED_CAST")
            override fun <T : ViewModel> create(modelClass: Class<T>): T {
                if (modelClass.isAssignableFrom(BrowserToolbarMiddleware::class.java)) {
                    return BrowserToolbarMiddleware(
                        appStore = appStore,
                        browserScreenStore = browserScreenStore,
                        browserStore = browserStore,
                        tabsUseCases = tabsUseCases,
                        settings = settings,
                    ) as T
                }
                throw IllegalArgumentException("Unknown ViewModel class")
            }
        }
    }
}
