/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.components.menu.middleware

import android.app.PendingIntent
import android.content.SharedPreferences
import kotlinx.coroutines.CoroutineDispatcher
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.SupervisorJob
import kotlinx.coroutines.launch
import mozilla.components.browser.state.state.SessionState
import mozilla.components.concept.engine.webextension.InstallationMethod
import mozilla.components.concept.storage.BookmarksStorage
import mozilla.components.feature.addons.Addon
import mozilla.components.feature.addons.AddonManager
import mozilla.components.feature.addons.AddonManagerException
import mozilla.components.feature.app.links.AppLinksUseCases
import mozilla.components.feature.session.SessionUseCases
import mozilla.components.feature.tabs.TabsUseCases
import mozilla.components.feature.top.sites.PinnedSiteStorage
import mozilla.components.feature.top.sites.TopSitesUseCases
import mozilla.components.lib.state.Middleware
import mozilla.components.lib.state.Store
import mozilla.components.support.base.log.logger.Logger
import org.mozilla.fenix.components.AppStore
import org.mozilla.fenix.components.appstate.AppAction
import org.mozilla.fenix.components.appstate.AppAction.BookmarkAction
import org.mozilla.fenix.components.appstate.AppAction.FindInPageAction
import org.mozilla.fenix.components.appstate.AppAction.ReaderViewAction
import org.mozilla.fenix.components.appstate.AppAction.ShortcutAction
import org.mozilla.fenix.components.bookmarks.BookmarksUseCase
import org.mozilla.fenix.components.menu.store.BookmarkState
import org.mozilla.fenix.components.menu.store.MenuAction
import org.mozilla.fenix.components.menu.store.MenuState
import org.mozilla.fenix.components.menu.store.MenuStore
import org.mozilla.fenix.components.menu.store.NavigationEvent
import org.mozilla.fenix.components.menu.store.SummarizationMenuState
import org.mozilla.fenix.components.metrics.MetricsUtils
import org.mozilla.fenix.home.topsites.AddShortcutEntryPoint
import org.mozilla.fenix.home.topsites.AddShortcutSource
import org.mozilla.fenix.summarization.eligibility.SummarizationEligibilityChecker
import org.mozilla.fenix.summarization.onboarding.SummarizationFeatureDiscoveryConfiguration
import org.mozilla.fenix.summarization.onboarding.SummarizeDiscoveryEvent
import org.mozilla.fenix.utils.Settings

/**
 * [Middleware] implementation for handling [MenuAction] and managing the [MenuState] for the menu dialog.
 *
 * @param appStore The [AppStore] used to dispatch actions to update the global state.
 * @param addonManager An instance of the [AddonManager] used to provide access to [Addon]s.
 * @param settings An instance of [Settings] to read and write to the [SharedPreferences] properties.
 * @param summarizeMenuSettings An instance of [SummarizationFeatureDiscoveryConfiguration] to manage the feature's
 *   settings in the menu.
 * @param summarizationEligibilityChecker Callback to check whether a page is eligibile for summarization.
 * @param bookmarksStorage An instance of the [BookmarksStorage] used to query matching bookmarks.
 * @param pinnedSiteStorage An instance of the [PinnedSiteStorage] used to query matching pinned shortcuts.
 * @param appLinksUseCases The [AppLinksUseCases] for opening a site in an external app.
 * @param addBookmarkUseCase The [BookmarksUseCase.AddBookmarksUseCase] for adding the selected tab as a bookmark.
 * @param addPinnedSiteUseCase The [TopSitesUseCases.AddPinnedSiteUseCase] for adding the selected tab as a pinned
 *   shortcut.
 * @param removePinnedSitesUseCase The [TopSitesUseCases.RemoveTopSiteUseCase] for removing the selected tab from pinned
 *   shortcuts.
 * @param requestDesktopSiteUseCase The [SessionUseCases.RequestDesktopSiteUseCase] for toggling desktop mode for the
 *   current session.
 * @param migratePrivateTabUseCase The [TabsUseCases.MigratePrivateTabUseCase] for moving a private tab to a normal tab.
 * @param mainDispatcher The [CoroutineDispatcher] for performing UI updates.
 */
@Suppress("LongParameterList", "CyclomaticComplexMethod")
class MenuDialogMiddleware(
    private val appStore: AppStore,
    private val addonManager: AddonManager,
    private val settings: Settings,
    private val summarizeMenuSettings: SummarizationFeatureDiscoveryConfiguration,
    private val summarizationEligibilityChecker: SummarizationEligibilityChecker,
    private val bookmarksStorage: BookmarksStorage,
    private val pinnedSiteStorage: PinnedSiteStorage,
    private val appLinksUseCases: AppLinksUseCases,
    private val addBookmarkUseCase: BookmarksUseCase.AddBookmarksUseCase,
    private val addPinnedSiteUseCase: TopSitesUseCases.AddPinnedSiteUseCase,
    private val removePinnedSitesUseCase: TopSitesUseCases.RemoveTopSiteUseCase,
    private val requestDesktopSiteUseCase: SessionUseCases.RequestDesktopSiteUseCase,
    private val migratePrivateTabUseCase: TabsUseCases.MigratePrivateTabUseCase,
    private val mainDispatcher: CoroutineDispatcher = Dispatchers.Main,
) : Middleware<MenuState, MenuAction> {

    private val logger = Logger("MenuDialogMiddleware")
    private val scope = CoroutineScope(mainDispatcher + SupervisorJob())

    override fun invoke(
        store: Store<MenuState, MenuAction>,
        next: (MenuAction) -> Unit,
        action: MenuAction,
    ) {
        val currentState = store.state

        when (action) {
            is MenuAction.InitAction -> initialize(store)
            is MenuAction.AddBookmark -> addBookmark(store)
            is MenuAction.AddShortcut -> addShortcut(store)
            is MenuAction.RemoveShortcut -> removeShortcut(store)
            is MenuAction.DeleteBrowsingDataAndQuit -> deleteBrowsingDataAndQuit(store)
            is MenuAction.FindInPage -> launchFindInPage(store)
            is MenuAction.DefaultBrowserMenuBannerDismissed -> dismissMenuBanner()
            is MenuAction.OpenInApp -> openInApp(store)
            is MenuAction.OpenInFirefox -> openInFirefox(store)
            is MenuAction.InstallAddon -> installAddon(store, action.addon)
            is MenuAction.InstallAddonSuccess -> installAddonSuccess(store)
            is MenuAction.CustomMenuItemAction -> customMenuItemAction(store, action.intent, action.url)
            is MenuAction.CustomizeReaderView -> customizeReaderView(store)
            is MenuAction.OnSummarizationMenuExposed -> cacheMenuExposure(store)
            is MenuAction.OnMoreMenuClicked -> cacheMoreMenuClick(store)
            is MenuAction.MoveToNonPrivateTab -> migratePrivateTab(store)
            is MenuAction.RequestDesktopSite,
            is MenuAction.RequestMobileSite ->
                requestSiteMode(
                    store = store,
                    tabId = currentState.browserMenuState?.selectedTab?.id,
                    shouldRequestDesktopMode = !currentState.isDesktopMode,
                )

            else -> Unit
        }

        next(action)
    }

    private fun initialize(store: Store<MenuState, MenuAction>) = scope.launch {
        setupBookmarkState(store)
        setupPinnedState(store)
        setupExtensionState(store)
        setupPageSummarizationState(store)
    }

    private suspend fun setupPageSummarizationState(store: Store<MenuState, MenuAction>) {
        val selectedTab = store.state.browserMenuState?.selectedTab
        val isNormalTab = selectedTab?.isNormalTab() ?: false
        val isSummarizationEligible = selectedTab.checkSummarizationEligibility()
        val showMenuItem = summarizeMenuSettings.showMenuItem

        val summarizationState =
            SummarizationMenuState.Default.copy(
                visible = showMenuItem,
                highlighted = summarizeMenuSettings.shouldHighlightMenuItem && isNormalTab,
                overflowMenuHighlighted = summarizeMenuSettings.shouldHighlightOverflowMenuItem && isNormalTab,
                showNewFeatureBadge = false,
                enabled = showMenuItem && isNormalTab && isSummarizationEligible,
            )
        store.dispatch(MenuAction.InitializeSummarizationMenuState(summarizationState))
        if (isNormalTab) {
            // the user must have interacted with the toolbar to open the menu
            // so we want to cache that interaction for normal tabs.
            summarizeMenuSettings.cacheDiscoveryEvent(SummarizeDiscoveryEvent.ToolbarOverflowInteraction)
        }
    }

    private suspend fun SessionState?.checkSummarizationEligibility(): Boolean =
        this@checkSummarizationEligibility?.engineState?.engineSession?.let { session ->
            summarizationEligibilityChecker.checkLanguage(session).getOrDefault(false)
        } ?: false

    private suspend fun setupBookmarkState(store: Store<MenuState, MenuAction>) {
        val url = store.state.browserMenuState?.selectedTab?.content?.url ?: return
        val bookmark =
            bookmarksStorage.getBookmarksWithUrl(url).getOrDefault(listOf()).firstOrNull { it.url == url } ?: return

        store.dispatch(
            MenuAction.UpdateBookmarkState(
                bookmarkState =
                    BookmarkState(
                        guid = bookmark.guid,
                        isBookmarked = true,
                    )
            )
        )
    }

    private suspend fun setupPinnedState(store: Store<MenuState, MenuAction>) {
        val selectedTab = store.state.browserMenuState?.selectedTab
        if (selectedTab.isCustomTab()) return
        val url = selectedTab?.content?.url ?: return
        pinnedSiteStorage.getPinnedSites().firstOrNull { it.url == url } ?: return

        store.dispatch(MenuAction.UpdatePinnedState(isPinned = true))
    }

    private fun setupExtensionState(store: Store<MenuState, MenuAction>) = scope.launch {
        try {
            val addons = addonManager.getAddons()

            store.dispatch(MenuAction.UpdateAvailableAddons(addons.filter { it.isInstalled() && it.isEnabled() }))

            if (addons.any { it.isInstalled() }) {
                return@launch
            }

            val recommendedAddons =
                addons.filter { !it.isInstalled() }.shuffled().take(NUMBER_OF_RECOMMENDED_ADDONS_TO_SHOW)

            if (recommendedAddons.isNotEmpty()) {
                store.dispatch(MenuAction.UpdateExtensionState(recommendedAddons = recommendedAddons))
            }
        } catch (e: AddonManagerException) {
            logger.error("Failed to query extensions", e)
        }
    }

    private fun addBookmark(store: Store<MenuState, MenuAction>) = scope.launch {
        val browserMenuState = store.state.browserMenuState ?: return@launch

        if (browserMenuState.bookmarkState.isBookmarked) {
            return@launch
        }

        val selectedTab = browserMenuState.selectedTab
        val url = selectedTab.getTabUrl() ?: return@launch

        val result =
            addBookmarkUseCase(
                url = url,
                title = selectedTab.content.title,
            )

        appStore.dispatch(
            BookmarkAction.BookmarkAdded(
                guidToEdit = result.guidToEdit,
                parentNode = result.parentNode,
                source = MetricsUtils.BookmarkAction.Source.MENU_DIALOG,
            )
        )

        store.emitEvent(NavigationEvent.Dismiss)
    }

    private fun SessionState.isNormalTab() = !content.private

    private fun addShortcut(store: Store<MenuState, MenuAction>) = scope.launch {
        val browserMenuState = store.state.browserMenuState ?: return@launch

        if (browserMenuState.isPinned) {
            return@launch
        }

        val selectedTab = browserMenuState.selectedTab
        val url = selectedTab.getTabUrl() ?: return@launch

        addPinnedSiteUseCase(
            title = selectedTab.content.title,
            url = url,
        )

        appStore.dispatch(
            ShortcutAction.ShortcutAdded(
                url = url,
                source = AddShortcutSource.MANUAL,
                entryPoint = AddShortcutEntryPoint.PAGE_MENU,
            )
        )

        store.emitEvent(NavigationEvent.Dismiss)
    }

    private fun removeShortcut(store: Store<MenuState, MenuAction>) = scope.launch {
        val browserMenuState = store.state.browserMenuState ?: return@launch

        if (!browserMenuState.isPinned) {
            return@launch
        }

        val selectedTab = browserMenuState.selectedTab
        val url = selectedTab.getTabUrl() ?: return@launch
        val topSite = pinnedSiteStorage.getPinnedSites().firstOrNull { it.url == url } ?: return@launch

        removePinnedSitesUseCase(topSite = topSite)
        store.emitEvent(NavigationEvent.Dismiss)
    }

    private fun deleteBrowsingDataAndQuit(store: Store<MenuState, MenuAction>) = scope.launch {
        store.emitEvent(NavigationEvent.DeleteBrowsingDataAndQuit)
        store.emitEvent(NavigationEvent.Dismiss)
    }

    private fun openInApp(store: Store<MenuState, MenuAction>) = scope.launch {
        val url = store.state.browserMenuState?.selectedTab?.content?.url ?: return@launch
        val redirect = appLinksUseCases.appLinkRedirect.invoke(url)

        if (!redirect.hasExternalApp()) {
            return@launch
        }

        settings.openInAppOpened = true

        appLinksUseCases.openAppLink.invoke(redirect.appIntent)
        store.emitEvent(NavigationEvent.Dismiss)
    }

    private fun openInFirefox(store: Store<MenuState, MenuAction>) = scope.launch {
        appStore.dispatch(AppAction.OpenInFirefoxStarted)
        store.emitEvent(NavigationEvent.Dismiss)
    }

    private fun installAddon(
        store: Store<MenuState, MenuAction>,
        addon: Addon,
    ) = scope.launch {
        if (addon.isInstalled()) {
            return@launch
        }

        store.dispatch(MenuAction.UpdateInstallAddonInProgress(addon = addon))

        addonManager.installAddon(
            url = addon.downloadUrl,
            installationMethod = InstallationMethod.MANAGER,
            onSuccess = {
                store.dispatch(MenuAction.InstallAddonSuccess(addon = addon))
            },
            onError = { e ->
                store.dispatch(MenuAction.InstallAddonFailed(addon = addon))
                logger.error("Failed to install addon", e)
            },
        )
    }

    private fun installAddonSuccess(store: Store<MenuState, MenuAction>) = scope.launch {
        store.emitEvent(NavigationEvent.Dismiss)
    }

    private fun customizeReaderView(store: Store<MenuState, MenuAction>) = scope.launch {
        appStore.dispatch(ReaderViewAction.ReaderViewControlsShown)
        store.emitEvent(NavigationEvent.Dismiss)
    }

    private fun launchFindInPage(store: Store<MenuState, MenuAction>) = scope.launch {
        appStore.dispatch(FindInPageAction.FindInPageStarted)
        store.emitEvent(NavigationEvent.Dismiss)
    }

    private fun dismissMenuBanner() = scope.launch {
        settings.shouldShowMenuBanner = false
    }

    private fun requestSiteMode(
        store: Store<MenuState, MenuAction>,
        tabId: String?,
        shouldRequestDesktopMode: Boolean,
    ) = scope.launch {
        if (tabId != null) {
            requestDesktopSiteUseCase(
                enable = shouldRequestDesktopMode,
                tabId = tabId,
            )
        }

        store.emitEvent(NavigationEvent.Dismiss)
    }

    private fun customMenuItemAction(
        store: Store<MenuState, MenuAction>,
        intent: PendingIntent,
        url: String?,
    ) = scope.launch {
        store.emitEvent(NavigationEvent.SendPendingIntentWithUrl(intent, url))
        store.emitEvent(NavigationEvent.Dismiss)
    }

    private fun cacheMenuExposure(store: Store<MenuState, MenuAction>) = scope.launch {
        if (store.state.summarizationMenuState.enabled) {
            summarizeMenuSettings.cacheDiscoveryEvent(SummarizeDiscoveryEvent.MenuItemExposure)
        }
    }

    private fun migratePrivateTab(store: Store<MenuState, MenuAction>) = scope.launch {
        val tabId = store.state.browserMenuState?.selectedTab?.id ?: return@launch
        migratePrivateTabUseCase(tabId)
        store.emitEvent(NavigationEvent.Dismiss)
    }

    private fun cacheMoreMenuClick(store: Store<MenuState, MenuAction>) = scope.launch {
        if (store.state.summarizationMenuState.overflowMenuHighlighted) {
            summarizeMenuSettings.cacheDiscoveryEvent(SummarizeDiscoveryEvent.MenuOverflowInteraction)
        }
    }

    private fun Store<MenuState, MenuAction>.emitEvent(effect: NavigationEvent) {
        (this as? MenuStore)?.emitEvent(effect)
    }

    companion object {
        private const val NUMBER_OF_RECOMMENDED_ADDONS_TO_SHOW = 3
    }
}
