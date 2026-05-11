/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.components.toolbar

import android.content.Context
import android.os.Build
import androidx.annotation.VisibleForTesting
import androidx.navigation.NavController
import kotlinx.coroutines.CoroutineDispatcher
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.Job
import kotlinx.coroutines.flow.Flow
import kotlinx.coroutines.flow.distinctUntilChangedBy
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext
import mozilla.appservices.places.BookmarkRoot
import mozilla.components.browser.state.action.ContentAction
import mozilla.components.browser.state.action.EngineAction
import mozilla.components.browser.state.search.SearchEngine
import mozilla.components.browser.state.selector.getNormalOrPrivateTabs
import mozilla.components.browser.state.selector.selectedTab
import mozilla.components.browser.state.state.SecurityInfo
import mozilla.components.browser.state.state.selectedOrDefaultSearchEngine
import mozilla.components.browser.state.store.BrowserStore
import mozilla.components.browser.thumbnails.BrowserThumbnails
import mozilla.components.compose.browser.toolbar.concept.Action
import mozilla.components.compose.browser.toolbar.concept.Action.ActionButton
import mozilla.components.compose.browser.toolbar.concept.Action.ActionButtonRes
import mozilla.components.compose.browser.toolbar.concept.Action.TabCounterAction
import mozilla.components.compose.browser.toolbar.concept.PageOrigin
import mozilla.components.compose.browser.toolbar.concept.PageOrigin.Companion.ContextualMenuOption
import mozilla.components.compose.browser.toolbar.concept.PageOrigin.Companion.PageOriginContextualMenuInteractions.CopyToClipboardClicked
import mozilla.components.compose.browser.toolbar.concept.PageOrigin.Companion.PageOriginContextualMenuInteractions.LoadFromClipboardClicked
import mozilla.components.compose.browser.toolbar.concept.PageOrigin.Companion.PageOriginContextualMenuInteractions.PasteFromClipboardClicked
import mozilla.components.compose.browser.toolbar.store.BrowserDisplayToolbarAction
import mozilla.components.compose.browser.toolbar.store.BrowserDisplayToolbarAction.BrowserActionsEndUpdated
import mozilla.components.compose.browser.toolbar.store.BrowserDisplayToolbarAction.BrowserActionsStartUpdated
import mozilla.components.compose.browser.toolbar.store.BrowserDisplayToolbarAction.NavigationActionsUpdated
import mozilla.components.compose.browser.toolbar.store.BrowserDisplayToolbarAction.PageActionsEndUpdated
import mozilla.components.compose.browser.toolbar.store.BrowserDisplayToolbarAction.UpdateProgressBarConfig
import mozilla.components.compose.browser.toolbar.store.BrowserEditToolbarAction.SearchQueryUpdated
import mozilla.components.compose.browser.toolbar.store.BrowserToolbarAction
import mozilla.components.compose.browser.toolbar.store.BrowserToolbarAction.Init
import mozilla.components.compose.browser.toolbar.store.BrowserToolbarInteraction
import mozilla.components.compose.browser.toolbar.store.BrowserToolbarInteraction.BrowserToolbarEvent
import mozilla.components.compose.browser.toolbar.store.BrowserToolbarInteraction.BrowserToolbarEvent.Source
import mozilla.components.compose.browser.toolbar.store.BrowserToolbarInteraction.CombinedEventAndMenu
import mozilla.components.compose.browser.toolbar.store.BrowserToolbarMenuItem.BrowserToolbarMenuButton
import mozilla.components.compose.browser.toolbar.store.BrowserToolbarMenuItem.BrowserToolbarMenuButton.ContentDescription.StringResContentDescription
import mozilla.components.compose.browser.toolbar.store.BrowserToolbarMenuItem.BrowserToolbarMenuButton.Icon.DrawableResIcon
import mozilla.components.compose.browser.toolbar.store.BrowserToolbarMenuItem.BrowserToolbarMenuButton.Text.StringResText
import mozilla.components.compose.browser.toolbar.store.BrowserToolbarMenuItem.BrowserToolbarMenuDivider
import mozilla.components.compose.browser.toolbar.store.BrowserToolbarState
import mozilla.components.compose.browser.toolbar.store.ProgressBarConfig
import mozilla.components.compose.browser.toolbar.ui.BrowserToolbarQuery
import mozilla.components.concept.engine.EngineSession.LoadUrlFlags
import mozilla.components.concept.engine.cookiehandling.CookieBannersStorage
import mozilla.components.concept.engine.permission.SitePermissions
import mozilla.components.concept.engine.permission.SitePermissionsStorage
import mozilla.components.concept.engine.prompt.ShareData
import mozilla.components.concept.engine.utils.ABOUT_HOME_URL
import mozilla.components.concept.storage.BookmarksStorage
import mozilla.components.feature.ipprotection.Authorized
import mozilla.components.feature.ipprotection.IPProtectionStore
import mozilla.components.feature.session.SessionUseCases
import mozilla.components.feature.session.TrackingProtectionUseCases
import mozilla.components.lib.publicsuffixlist.PublicSuffixList
import mozilla.components.lib.state.Middleware
import mozilla.components.lib.state.State
import mozilla.components.lib.state.Store
import mozilla.components.lib.state.ext.flow
import mozilla.components.support.base.log.logger.Logger
import mozilla.components.support.ktx.kotlin.applyRegistrableDomainSpan
import mozilla.components.support.ktx.kotlin.getOrigin
import mozilla.components.support.ktx.kotlin.isContentUrl
import mozilla.components.support.ktx.kotlin.isUrl
import mozilla.components.support.ktx.util.URLStringUtils
import mozilla.components.support.utils.ClipboardHandler
import mozilla.telemetry.glean.private.NoExtras
import org.mozilla.fenix.GleanMetrics.Events
import org.mozilla.fenix.GleanMetrics.ReaderMode
import org.mozilla.fenix.GleanMetrics.Translations
import org.mozilla.fenix.NavGraphDirections
import org.mozilla.fenix.R
import org.mozilla.fenix.browser.BrowserFragmentDirections
import org.mozilla.fenix.browser.browsingmode.BrowsingMode
import org.mozilla.fenix.browser.browsingmode.BrowsingMode.Normal
import org.mozilla.fenix.browser.browsingmode.BrowsingMode.Private
import org.mozilla.fenix.browser.browsingmode.BrowsingModeManager
import org.mozilla.fenix.browser.readermode.ReaderModeController
import org.mozilla.fenix.browser.store.BrowserScreenAction
import org.mozilla.fenix.browser.store.BrowserScreenStore
import org.mozilla.fenix.components.AppStore
import org.mozilla.fenix.components.NimbusComponents
import org.mozilla.fenix.components.UseCases
import org.mozilla.fenix.components.appstate.AppAction.BookmarkAction
import org.mozilla.fenix.components.appstate.AppAction.CurrentTabClosed
import org.mozilla.fenix.components.appstate.AppAction.SearchAction.SearchEnded
import org.mozilla.fenix.components.appstate.AppAction.SearchAction.SearchStarted
import org.mozilla.fenix.components.appstate.AppAction.SnackbarAction.SnackbarDismissed
import org.mozilla.fenix.components.appstate.AppAction.URLCopiedToClipboard
import org.mozilla.fenix.components.appstate.snackbar.SnackbarState
import org.mozilla.fenix.components.menu.MenuAccessPoint
import org.mozilla.fenix.components.metrics.MetricsUtils
import org.mozilla.fenix.components.share.createPdfShareAction
import org.mozilla.fenix.components.toolbar.DisplayActions.AddBookmarkClicked
import org.mozilla.fenix.components.toolbar.DisplayActions.EditBookmarkClicked
import org.mozilla.fenix.components.toolbar.DisplayActions.HomepageClicked
import org.mozilla.fenix.components.toolbar.DisplayActions.MenuClicked
import org.mozilla.fenix.components.toolbar.DisplayActions.NavigateBackClicked
import org.mozilla.fenix.components.toolbar.DisplayActions.NavigateBackLongClicked
import org.mozilla.fenix.components.toolbar.DisplayActions.NavigateForwardClicked
import org.mozilla.fenix.components.toolbar.DisplayActions.NavigateForwardLongClicked
import org.mozilla.fenix.components.toolbar.DisplayActions.RefreshClicked
import org.mozilla.fenix.components.toolbar.DisplayActions.ShareClicked
import org.mozilla.fenix.components.toolbar.DisplayActions.StopRefreshClicked
import org.mozilla.fenix.components.toolbar.DisplayActions.TranslateClicked
import org.mozilla.fenix.components.toolbar.PageEndActionsInteractions.ReaderModeClicked
import org.mozilla.fenix.components.toolbar.PageOriginInteractions.OriginClicked
import org.mozilla.fenix.components.toolbar.TabCounterInteractions.AddNewPrivateTab
import org.mozilla.fenix.components.toolbar.TabCounterInteractions.AddNewTab
import org.mozilla.fenix.components.toolbar.TabCounterInteractions.CloseCurrentTab
import org.mozilla.fenix.components.toolbar.TabCounterInteractions.TabCounterClicked
import org.mozilla.fenix.components.toolbar.TabCounterInteractions.TabCounterLongClicked
import org.mozilla.fenix.ext.canGoBackInHistoryOrToStories
import org.mozilla.fenix.ext.nav
import org.mozilla.fenix.ext.navigateSafe
import org.mozilla.fenix.nimbus.FxNimbus
import org.mozilla.fenix.settings.ShortcutType
import org.mozilla.fenix.settings.quicksettings.protections.cookiebanners.getCookieBannerUIMode
import org.mozilla.fenix.tabstray.ext.isActiveDownload
import org.mozilla.fenix.tabstray.redux.state.Page
import org.mozilla.fenix.utils.Settings
import org.mozilla.fenix.utils.Stories.hasUrlOfAHomeScreenStory
import org.mozilla.fenix.utils.Stories.hasUrlOfAStoriesScreenStory
import org.mozilla.fenix.utils.lastSavedFolderCache
import mozilla.components.browser.toolbar.R as toolbarR
import mozilla.components.lib.state.Action as MVIAction
import mozilla.components.ui.icons.R as iconsR
import mozilla.components.ui.tabcounter.R as tabcounterR

@VisibleForTesting
internal sealed class DisplayActions(override val source: Source) : BrowserToolbarEvent {
    data class MenuClicked(override val source: Source) : DisplayActions(source)
    data class NavigateBackClicked(override val source: Source) : DisplayActions(source)
    data class NavigateBackLongClicked(override val source: Source) : DisplayActions(source)
    data object NavigateForwardClicked : DisplayActions(Source.AddressBar.BrowserStart)
    data object NavigateForwardLongClicked : DisplayActions(Source.AddressBar.BrowserStart)
    data class RefreshClicked(val bypassCache: Boolean) : DisplayActions(Source.AddressBar.BrowserStart)
    data object StopRefreshClicked : DisplayActions(Source.AddressBar.BrowserStart)
    data class AddBookmarkClicked(override val source: Source) : DisplayActions(source)
    data class EditBookmarkClicked(override val source: Source) : DisplayActions(source)
    data class ShareClicked(override val source: Source) : DisplayActions(source)
    data class TranslateClicked(override val source: Source) : DisplayActions(source)
    data class HomepageClicked(override val source: Source) : DisplayActions(source)
}

@VisibleForTesting
internal sealed class StartPageActions(override val source: Source) : BrowserToolbarEvent {
    data object SiteInfoClicked : StartPageActions(Source.AddressBar.PageStart)
}

@VisibleForTesting
internal sealed class TabCounterInteractions : BrowserToolbarEvent {
    data class TabCounterClicked(override val source: Source) : TabCounterInteractions()
    data class TabCounterLongClicked(override val source: Source) : TabCounterInteractions()
    data class AddNewTab(override val source: Source) : TabCounterInteractions()
    data class AddNewPrivateTab(override val source: Source) : TabCounterInteractions()
    data object CloseCurrentTab : TabCounterInteractions()
}

@VisibleForTesting
internal sealed class PageOriginInteractions : BrowserToolbarEvent {
    data object OriginClicked : PageOriginInteractions()
}

@VisibleForTesting
internal sealed class PageEndActionsInteractions(override val source: Source) : BrowserToolbarEvent {
    data class ReaderModeClicked(
        val isActive: Boolean,
    ) : PageEndActionsInteractions(Source.AddressBar.PageEnd)
}

/**
 * [Middleware] responsible for configuring and handling interactions with the composable toolbar.
 *
 * @param uiContext [Context] used for various system interactions.
 * @param appStore [AppStore] allowing to integrate with other features of the applications.
 * @param browserScreenStore [BrowserScreenStore] used for integration with other browser screen functionalities.
 * @param browserStore [BrowserStore] to sync from.
 * @param ipProtectionStore [IPProtectionStore] to observe IP protection proxy status.
 * @param permissionsStorage [SitePermissionsStorage] to find currently selected tab site permissions.
 * @param cookieBannersStorage [CookieBannersStorage] to get the current status of cookie banner ui mode.
 * @param bookmarksStorage [BookmarksStorage] to read and write bookmark data related to the current site.
 * @param trackingProtectionUseCases [TrackingProtectionUseCases] allowing to query tracking protection data
 * of the current tab.
 * @param useCases [UseCases] helping this integrate with other features of the applications.
 * @param sessionUseCases [SessionUseCases] for interacting with the current session.
 * @param nimbusComponents [NimbusComponents] used for accessing Nimbus events to use in telemetry.
 * @param clipboard [ClipboardHandler] to use for reading from device's clipboard.
 * @param publicSuffixList [PublicSuffixList] used to obtain the base domain of the current site.
 * @param settings [Settings] for accessing user preferences.
 * @param navController [NavController] to use for navigating to other in-app destinations.
 * @param browsingModeManager [BrowsingModeManager] for querying the current browsing mode.
 * @param readerModeController [ReaderModeController] for showing or hiding the reader view UX.
 * @param thumbnailsFeature [BrowserThumbnails] for requesting screenshots of the current tab.
 * @param isWideScreen Callback for checking if the screen is wide.
 * @param isTallScreen Callback for checking if the screen is tall.
 * @param scope [CoroutineScope] used for running long running operations in background.
 * @param ioDispatcher [CoroutineDispatcher] to use for IO operations.
 */
@Suppress("LargeClass", "LongParameterList", "TooManyFunctions")
class BrowserToolbarMiddleware(
    private val uiContext: Context,
    private val appStore: AppStore,
    private val browserScreenStore: BrowserScreenStore,
    private val browserStore: BrowserStore,
    private val ipProtectionStore: IPProtectionStore,
    private val permissionsStorage: SitePermissionsStorage,
    private val cookieBannersStorage: CookieBannersStorage,
    private val bookmarksStorage: BookmarksStorage,
    private val trackingProtectionUseCases: TrackingProtectionUseCases,
    private val useCases: UseCases,
    private val sessionUseCases: SessionUseCases = SessionUseCases(browserStore),
    private val nimbusComponents: NimbusComponents,
    private val clipboard: ClipboardHandler,
    private val publicSuffixList: PublicSuffixList,
    private val settings: Settings,
    private val navController: NavController,
    private val browsingModeManager: BrowsingModeManager,
    private val readerModeController: ReaderModeController,
    private val thumbnailsFeature: () -> BrowserThumbnails?,
    private val isWideScreen: () -> Boolean,
    private val isTallScreen: () -> Boolean,
    private val scope: CoroutineScope,
    private val ioDispatcher: CoroutineDispatcher = Dispatchers.IO,
) : Middleware<BrowserToolbarState, BrowserToolbarAction> {
    @Suppress("LongMethod", "CyclomaticComplexMethod", "NestedBlockDepth", "ReturnCount", "CognitiveComplexMethod")
    override fun invoke(
        store: Store<BrowserToolbarState, BrowserToolbarAction>,
        next: (BrowserToolbarAction) -> Unit,
        action: BrowserToolbarAction,
    ) {
        when (action) {
            is Init -> {
                next(action)

                appStore.dispatch(SearchEnded)

                updateStartBrowserActions(store)
                updateStartPageActions(store)
                updateCurrentPageOrigin(store)
                updateEndPageActions(store)

                scope.launch {
                    updateEndBrowserActions(store)
                    updateNavigationActions(store)
                }

                observeProgressBarUpdates(store)
                observeOrientationChanges(store)
                observeTabsCountUpdates(store)
                observeMenuHighlightChanges(store)
                observeAcceptingCancellingPrivateDownloads(store)
                observePageNavigationStatus(store)
                observePageOriginUpdates(store)
                observeSelectedTabBookmarkedUpdates(store)
                observeReaderModeUpdates(store)
                observePageTranslationsUpdates(store)
                observePageRefreshUpdates(store)
                observePageTrackingProtectionUpdates(store)
                observePageSecurityUpdates(store)
                observePermissionHighlightsUpdates(store)
                observeIPProtectionUpdates(store)
            }

            is StartPageActions.SiteInfoClicked -> {
                onSiteInfoClicked()
                next(action)
            }

            is MenuClicked -> {
                navController.nav(
                    R.id.browserFragment,
                    BrowserFragmentDirections.actionGlobalMenuDialogFragment(
                        accesspoint = MenuAccessPoint.Browser,
                    ),
                )

                next(action)
            }

            is TabCounterClicked -> {
                thumbnailsFeature()?.requestScreenshot()

                navController.nav(
                    R.id.browserFragment,
                    BrowserFragmentDirections.actionGlobalTabManagementFragment(
                        page = when (browsingModeManager.mode) {
                            Normal -> Page.NormalTabs
                            Private -> Page.PrivateTabs
                        },
                    ),
                )

                next(action)
            }
            is AddNewTab -> {
                openNewTab(Normal)
                next(action)
            }
            is AddNewPrivateTab -> {
                openNewTab(Private)
                next(action)
            }
            is CloseCurrentTab -> {
                browserStore.state.selectedTab?.let { selectedTab ->
                    val isLastTab = browserStore.state.getNormalOrPrivateTabs(selectedTab.content.private).size == 1

                    if (!isLastTab) {
                        useCases.tabsUseCases.removeTab(selectedTab.id, selectParentIfExists = true)
                        appStore.dispatch(CurrentTabClosed(selectedTab.content.private))
                        return@let
                    }

                    if (!selectedTab.content.private) {
                        navController.navigate(
                            BrowserFragmentDirections.actionGlobalHome(
                                sessionToDelete = selectedTab.id,
                            ),
                        )
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
                        navController.navigate(
                            BrowserFragmentDirections.actionGlobalHome(
                                sessionToDelete = selectedTab.id,
                            ),
                        )
                    }
                }
            }

            is OriginClicked -> {
                Events.searchBarTapped.record(Events.SearchBarTappedExtra("BROWSER"))

                val selectedTab = browserStore.state.selectedTab ?: return
                val searchTerms = selectedTab.content.searchTerms
                if (searchTerms.isBlank()) {
                    navController.navigate(
                        BrowserFragmentDirections.actionGlobalHome(
                            focusOnAddressBar = true,
                            sessionToStartSearchFor = selectedTab.id,
                        ),
                    )
                } else {
                    store.dispatch(SearchQueryUpdated(BrowserToolbarQuery(searchTerms), true))
                    appStore.dispatch(SearchStarted(selectedTab.id))
                }
            }
            is CopyToClipboardClicked -> {
                Events.copyUrlTapped.record(NoExtras())

                val selectedTab = browserStore.state.selectedTab
                val url = selectedTab?.readerState?.activeUrl ?: selectedTab?.content?.url
                clipboard.text = url

                // Android 13+ shows by default a popup for copied text.
                // Avoid overlapping popups informing the user when the URL is copied to the clipboard.
                // and only show our snackbar when Android will not show an indication by default.
                // See https://developer.android.com/develop/ui/views/touch-and-input/copy-paste#duplicate-notifications).
                if (Build.VERSION.SDK_INT <= Build.VERSION_CODES.S_V2) {
                    appStore.dispatch(URLCopiedToClipboard)
                }
            }
            is PasteFromClipboardClicked -> {
                store.dispatch(SearchQueryUpdated(BrowserToolbarQuery(clipboard.text.orEmpty()), true))
                appStore.dispatch(SearchStarted(browserStore.state.selectedTabId))
            }
            is LoadFromClipboardClicked -> {
                clipboard.extractURL()?.let {
                    val searchEngine = reconcileSelectedEngine()
                    val selectedTabId = browserStore.state.selectedTabId ?: return
                    if (it.isUrl() || searchEngine == null) {
                        browserStore.dispatch(
                            ContentAction.UpdateSearchTermsAction(
                                selectedTabId,
                                "",
                            ),
                        )
                        Events.enteredUrl.record(Events.EnteredUrlExtra(autocomplete = false))
                    } else {
                        browserStore.dispatch(
                            ContentAction.UpdateSearchTermsAction(
                                selectedTabId,
                                it,
                            ),
                        )
                        val searchAccessPoint = MetricsUtils.Source.ACTION
                        MetricsUtils.recordSearchMetrics(
                            engine = searchEngine,
                            isDefault = true,
                            searchAccessPoint = searchAccessPoint,
                            nimbusEventStore = nimbusComponents.events,
                        )
                    }

                    useCases.fenixBrowserUseCases.loadUrlOrSearch(
                        searchTermOrURL = it,
                        newTab = false,
                        searchEngine = searchEngine,
                        private = browsingModeManager.mode == Private,
                    )
                } ?: run {
                    Logger("BrowserOriginContextMenu").error("Clipboard contains URL but unable to read text")
                }
            }
            is NavigateBackClicked -> {
                browserStore.state.selectedTab?.let {
                    when {
                        settings.enableHomepageAsNewTab -> browserStore.dispatch(EngineAction.GoBackAction(it.id))
                        it.hasUrlOfAHomeScreenStory() -> {
                            // First attempting to go back to the existing home fragment
                            // to preserve its scroll position.
                            val popToExistingHomeFragment =
                                navController.popBackStack(R.id.homeFragment, false)
                            if (!popToExistingHomeFragment) {
                                navController.nav(
                                    id = R.id.browserFragment,
                                    directions = NavGraphDirections.actionGlobalHome(),
                                )
                            }
                        }
                        it.hasUrlOfAStoriesScreenStory() -> {
                            // First attempting to go back to the existing stories fragment
                            // to preserve its scroll position.
                            val popToExistingStoriesFragment =
                                navController.popBackStack(R.id.storiesFragment, false)
                            if (!popToExistingStoriesFragment) {
                                navController.nav(
                                    id = R.id.browserFragment,
                                    directions = BrowserFragmentDirections.actionBrowserFragmentToStoriesFragment(),
                                )
                            }
                        }
                        else -> browserStore.dispatch(EngineAction.GoBackAction(it.id))
                    }
                }
                next(action)
            }
            is NavigateBackLongClicked -> {
                showTabHistory()
                next(action)
            }
            is NavigateForwardClicked -> {
                browserStore.state.selectedTab?.let {
                    browserStore.dispatch(EngineAction.GoForwardAction(it.id))
                }
                next(action)
            }
            is NavigateForwardLongClicked -> {
                showTabHistory()
                next(action)
            }

            is ReaderModeClicked -> {
                when (action.isActive) {
                    true -> {
                        ReaderMode.closed.record(NoExtras())
                        readerModeController.hideReaderView()
                    }
                    false -> {
                        ReaderMode.opened.record(NoExtras())
                        readerModeController.showReaderView()
                    }
                }

                next(action)
            }

            is TranslateClicked -> {
                Translations.action.record(Translations.ActionExtra("main_flow_toolbar"))

                appStore.dispatch(SnackbarDismissed)
                navController.navigateSafe(
                    resId = R.id.browserFragment,
                    directions = BrowserFragmentDirections.actionBrowserFragmentToTranslationsDialogFragment(),
                )
                next(action)
            }

            is RefreshClicked -> {
                val tabId = browserStore.state.selectedTabId
                if (action.bypassCache) {
                    sessionUseCases.reload.invoke(
                        tabId,
                        flags = LoadUrlFlags.select(
                            LoadUrlFlags.BYPASS_CACHE,
                        ),
                    )
                } else {
                    sessionUseCases.reload(tabId)
                }
                next(action)
            }
            is StopRefreshClicked -> {
                val tabId = browserStore.state.selectedTabId
                sessionUseCases.stopLoading(tabId)
                next(action)
            }

            is AddBookmarkClicked -> {
                val selectedTab = browserStore.state.selectedTab

                selectedTab?.let {
                    scope.launch(ioDispatcher) {
                        val targetParentFolderId =
                            settings.lastSavedFolderCache.getGuid() ?: BookmarkRoot.Mobile.id

                        val parentNode = bookmarksStorage.getBookmark(targetParentFolderId).getOrNull()
                            ?: bookmarksStorage.getBookmark(BookmarkRoot.Mobile.id).getOrNull()
                        val parentGuid = parentNode?.guid ?: BookmarkRoot.Mobile.id

                        if (targetParentFolderId != parentGuid) {
                            settings.lastSavedFolderCache.setGuid(null)
                        }

                        val guidToEdit = useCases.bookmarksUseCases.addBookmark(
                            url = selectedTab.content.url,
                            title = selectedTab.content.title,
                            parentGuid = parentGuid,
                        )

                        appStore.dispatch(
                            BookmarkAction.BookmarkAdded(
                                guidToEdit = guidToEdit,
                                parentNode = parentNode,
                                source = action.source.toMetricSource(),
                            ),
                        )
                    }
                }

                next(action)
            }

            is EditBookmarkClicked -> {
                val selectedTab = browserStore.state.selectedTab ?: return

                scope.launch(Dispatchers.Main) {
                    val guidToEdit: String? = withContext(ioDispatcher) {
                      bookmarksStorage
                          .getBookmarksWithUrl(selectedTab.content.url)
                          .getOrDefault(listOf())
                          .firstOrNull()
                          ?.guid
                    }

                    guidToEdit?.let { guid ->
                        navController.navigateSafe(
                            R.id.browserFragment,
                            BrowserFragmentDirections.actionGlobalBookmarkEditFragment(
                                guidToEdit = guid,
                                requiresSnackbarPaddingForToolbar = true,
                            ),
                        )
                    }
                }

                next(action)
            }

            is ShareClicked -> {
                val selectedTab = browserStore.state.selectedTab ?: return
                val shareAction = browserStore.createPdfShareAction(selectedTab.id, selectedTab.content.url)
                if (shareAction != null) {
                    browserStore.dispatch(shareAction)
                } else {
                    navController.nav(
                        R.id.browserFragment,
                        BrowserFragmentDirections.actionGlobalShareFragment(
                            sessionId = selectedTab.id,
                            data = arrayOf(
                                ShareData(
                                    url = selectedTab.content.url,
                                    title = selectedTab.content.title,
                                ),
                            ),
                            showPage = true,
                        ),
                    )
                }

                next(action)
            }

            is HomepageClicked -> {
                if (settings.enableHomepageAsNewTab) {
                    useCases.fenixBrowserUseCases.navigateToHomepage()
                } else {
                    val directions = BrowserFragmentDirections.actionGlobalHome()
                    navController.navigate(directions)
                }
                next(action)
            }

            else -> next(action)
        }
    }

    private fun showTabHistory() = navController.nav(
        R.id.browserFragment,
        BrowserFragmentDirections.actionGlobalTabHistoryDialogFragment(
            activeSessionId = null,
        ),
    )

    private fun onSiteInfoClicked() {
        val tab = browserStore.state.selectedTab ?: return
        scope.launch(ioDispatcher) {
            val sitePermissions: SitePermissions? = tab.content.url.getOrigin()?.let { origin ->
                permissionsStorage.findSitePermissionsBy(origin, private = tab.content.private)
            }

            scope.launch(Dispatchers.Main) {
                trackingProtectionUseCases.containsException(tab.id) { hasTrackingProtectionException ->
                    scope.launch {
                        val cookieBannerUIMode = cookieBannersStorage.getCookieBannerUIMode(
                            tab = tab,
                            isFeatureEnabledInPrivateMode = settings.shouldUseCookieBannerPrivateMode,
                            publicSuffixList = publicSuffixList,
                        )

                        val isTrackingProtectionEnabled =
                            tab.trackingProtection.enabled && !hasTrackingProtectionException
                        val directions = if (settings.enableUnifiedTrustPanel) {
                            BrowserFragmentDirections.actionBrowserFragmentToTrustPanelFragment(
                                sessionId = tab.id,
                                url = tab.content.url,
                                title = tab.content.title,
                                isLocalPdf = tab.content.url.isContentUrl(),
                                isSecured = tab.content.securityInfo.isSecure,
                                sitePermissions = sitePermissions,
                                certificate = tab.content.securityInfo.certificate,
                                permissionHighlights = tab.content.permissionHighlights,
                                isTrackingProtectionEnabled = isTrackingProtectionEnabled,
                                cookieBannerUIMode = cookieBannerUIMode,
                            )
                        } else {
                            BrowserFragmentDirections.actionBrowserFragmentToQuickSettingsSheetDialogFragment(
                                sessionId = tab.id,
                                url = tab.content.url,
                                title = tab.content.title,
                                isLocalPdf = tab.content.url.isContentUrl(),
                                isSecured = tab.content.securityInfo.isSecure,
                                sitePermissions = sitePermissions,
                                gravity = settings.toolbarPosition.androidGravity,
                                certificateName = tab.content.securityInfo.issuer,
                                permissionHighlights = tab.content.permissionHighlights,
                                isTrackingProtectionEnabled = isTrackingProtectionEnabled,
                                cookieBannerUIMode = cookieBannerUIMode,
                            )
                        }
                        navController.nav(
                            R.id.browserFragment,
                            directions,
                        )
                    }
                }
            }
        }
    }

    private fun updateStartBrowserActions(store: Store<BrowserToolbarState, BrowserToolbarAction>) =
        store.dispatch(
            BrowserActionsStartUpdated(
                buildStartBrowserActions(),
            ),
        )

    private fun updateStartPageActions(store: Store<BrowserToolbarState, BrowserToolbarAction>) =
        store.dispatch(
            BrowserDisplayToolbarAction.PageActionsStartUpdated(
                buildStartPageActions(),
            ),
    )

    private suspend fun updateEndBrowserActions(store: Store<BrowserToolbarState, BrowserToolbarAction>) {
        store.dispatch(
            BrowserActionsEndUpdated(
                buildEndBrowserActions(),
            ),
        )
    }

    private fun buildStartPageActions(): List<Action> {
        return listOf(
            ToolbarActionConfig(ToolbarAction.SiteInfo) {
                !browserScreenStore.state.readerModeStatus.isActive
            },
        ).filter { config ->
            config.isVisible()
        }.map { config ->
            buildAction(config.action, Source.AddressBar.PageStart)
        }
    }

    private fun updateEndPageActions(store: Store<BrowserToolbarState, BrowserToolbarAction>) =
        store.dispatch(
            PageActionsEndUpdated(
                buildEndPageActions(),
            ),
    )

    /**
     *  Devices wider than 600dp:
     *   - The navigation buttons (forward, back, and refresh) are always shown on the left side of the address bar.
     */
    private fun buildStartBrowserActions(): List<Action> {
        val isWideScreen = isWideScreen()

        return listOf(
            ToolbarActionConfig(ToolbarAction.Back) { isWideScreen },
            ToolbarActionConfig(ToolbarAction.Forward) { isWideScreen },
            ToolbarActionConfig(ToolbarAction.RefreshOrStop) { isWideScreen },
        ).filter { config ->
            config.isVisible()
        }.map { config ->
            buildAction(config.action, Source.AddressBar.BrowserStart)
        }
    }

    /**
     *  Devices wider than 600dp:
     *   - The page action buttons (Share and Translate), which were removed from smaller devices, are shown again.
     */
    private fun buildEndPageActions(): List<Action> {
        val isWideScreen = isWideScreen()
        val tabStripEnabled = settings.isTabStripEnabled
        val simpleShortcut = ShortcutType.fromValue(settings.toolbarSimpleShortcut)
        val translateShortcutEnabled = simpleShortcut == ShortcutType.TRANSLATE
        val shareShortcutEnabled = simpleShortcut == ShortcutType.SHARE

        return listOf(
            ToolbarActionConfig(ToolbarAction.ReaderMode) {
                browserScreenStore.state.readerModeStatus.isAvailable
            },
            ToolbarActionConfig(ToolbarAction.Translate) {
                browserScreenStore.state.pageTranslationStatus.isTranslationPossible &&
                    isWideScreen && FxNimbus.features.translations.value().mainFlowToolbarEnabled &&
                        !translateShortcutEnabled
            },
            ToolbarActionConfig(ToolbarAction.Share) {
                isWideScreen && !tabStripEnabled && !shareShortcutEnabled
            },
        ).filter { config ->
            config.isVisible()
        }.map { config ->
            buildAction(config.action, Source.AddressBar.PageEnd)
        }
    }

    private suspend fun buildEndBrowserActions(): List<Action> {
        val isWideWindow = isWideScreen()
        val isTallWindow = isTallScreen()
        val shouldUseExpandedToolbar = settings.shouldUseExpandedToolbar
        val primarySlotAction = ShortcutType.fromValue(settings.toolbarSimpleShortcut)?.toToolbarAction()

        val configs = listOfNotNull(
            primarySlotAction?.let {
                ToolbarActionConfig(it) {
                    !shouldUseExpandedToolbar || !isTallWindow || isWideWindow
                }
            },
            ToolbarActionConfig(ToolbarAction.TabCounter) {
                !shouldUseExpandedToolbar || !isTallWindow || isWideWindow
            },
            ToolbarActionConfig(ToolbarAction.Menu) {
                !shouldUseExpandedToolbar || !isTallWindow || isWideWindow
            },
        )

        return configs.mapNotNull { config ->
            config.takeIf { it.isVisible() }?.let { buildAction(it.action, Source.AddressBar.BrowserEnd) }
        }
    }

    /**
     * - Devices taller than 480dp:
     *   - The navigation bar is always shown (if the user enabled it).
     *
     * - Devices shorter than 480dp:
     *   - The navigation bar is hidden (even if the user enabled it).
     *   - The toolbar redesign customization option is also hidden.
     *
     *   Devices wider than 600dp:
     *   - The navigation bar is hidden. (even If user enabled it)
     *   - The toolbar redesign customization option is also hidden.
     */
    private suspend fun buildNavigationActions(): List<Action> {
        val isWideWindow = isWideScreen()
        val isTallWindow = isTallScreen()
        val shouldUseExpandedToolbar = settings.shouldUseExpandedToolbar
        val primarySlotAction = ShortcutType.fromValue(settings.toolbarExpandedShortcut)
            ?.toToolbarAction() ?: getBookmarkAction()

        return listOf(
            ToolbarActionConfig(primarySlotAction) { shouldUseExpandedToolbar && isTallWindow && !isWideWindow },
            ToolbarActionConfig(ToolbarAction.Share) { shouldUseExpandedToolbar && isTallWindow && !isWideWindow },
            ToolbarActionConfig(ToolbarAction.NewTab) { shouldUseExpandedToolbar && isTallWindow && !isWideWindow },
            ToolbarActionConfig(ToolbarAction.TabCounter) { shouldUseExpandedToolbar && isTallWindow && !isWideWindow },
            ToolbarActionConfig(ToolbarAction.Menu) { shouldUseExpandedToolbar && isTallWindow && !isWideWindow },
        ).filter { config ->
            config.isVisible()
        }.map { config ->
            buildAction(config.action, Source.NavigationBar)
        }
    }

    private suspend fun updateNavigationActions(store: Store<BrowserToolbarState, BrowserToolbarAction>) {
        store.dispatch(
            NavigationActionsUpdated(
                buildNavigationActions(),
            ),
        )
    }

    private fun buildTabCounterMenu(source: Source, toolbarPosition: ToolbarPosition) =
        CombinedEventAndMenu(TabCounterLongClicked(source)) {
            val list = listOf(
                BrowserToolbarMenuButton(
                    icon = DrawableResIcon(iconsR.drawable.mozac_ic_plus_24),
                    text = StringResText(tabcounterR.string.mozac_browser_menu_new_tab),
                    contentDescription = StringResContentDescription(tabcounterR.string.mozac_browser_menu_new_tab),
                    onClick = AddNewTab(source),
                ),
                BrowserToolbarMenuButton(
                    icon = DrawableResIcon(iconsR.drawable.mozac_ic_private_mode_fill_24),
                    text = StringResText(tabcounterR.string.mozac_browser_menu_new_private_tab),
                    contentDescription =
                        StringResContentDescription(tabcounterR.string.mozac_browser_menu_new_private_tab),
                    onClick = AddNewPrivateTab(source),
                ),
                BrowserToolbarMenuDivider,
                BrowserToolbarMenuButton(
                    icon = DrawableResIcon(iconsR.drawable.mozac_ic_cross_24),
                    text = StringResText(tabcounterR.string.mozac_close_tab),
                    contentDescription = StringResContentDescription(tabcounterR.string.mozac_close_tab),
                    onClick = CloseCurrentTab,
                ),
            )
            when (toolbarPosition) {
                ToolbarPosition.TOP -> {
                    list
                }
                ToolbarPosition.BOTTOM -> {
                    list.reversed()
                }
            }
    }

    private fun buildProgressBar(progress: Int = 0) = ProgressBarConfig(progress)

    private fun openNewTab(
        browsingMode: BrowsingMode,
    ) {
        if (settings.enableHomepageAsNewTab) {
            useCases.fenixBrowserUseCases.addNewHomepageTab(
                private = browsingMode.isPrivate,
            )
        } else {
            val focusOnAddressBar = !settings.enableHomepageSearchBar

            browsingModeManager.mode = browsingMode
            navController.navigate(
                BrowserFragmentDirections.actionGlobalHome(focusOnAddressBar = focusOnAddressBar),
            )
        }
    }

    private fun observeProgressBarUpdates(store: Store<BrowserToolbarState, BrowserToolbarAction>) {
        browserStore.observeWhileActive {
            distinctUntilChangedBy { it.selectedTab?.content?.progress }
            .collect {
                store.dispatch(
                    UpdateProgressBarConfig(
                        buildProgressBar(it.selectedTab?.content?.progress ?: 0),
                    ),
                )
            }
        }
    }

    private fun observeOrientationChanges(store: Store<BrowserToolbarState, BrowserToolbarAction>) {
        appStore.observeWhileActive {
            distinctUntilChangedBy { it.orientation }
            .collect {
                updateStartBrowserActions(store)
                updateEndBrowserActions(store)
                updateEndPageActions(store)
                updateNavigationActions(store)
            }
        }
    }

    private fun observeTabsCountUpdates(store: Store<BrowserToolbarState, BrowserToolbarAction>) {
        browserStore.observeWhileActive {
            distinctUntilChangedBy { it.tabs.size }
            .collect {
                updateEndBrowserActions(store)
                updateNavigationActions(store)
            }
        }
    }

    private fun observeMenuHighlightChanges(store: Store<BrowserToolbarState, BrowserToolbarAction>) {
        appStore.observeWhileActive {
            distinctUntilChangedBy { it.supportedMenuNotifications.isNotEmpty() }
            .collect {
                updateEndBrowserActions(store)
                updateNavigationActions(store)
            }
        }
    }

    private fun observePageOriginUpdates(store: Store<BrowserToolbarState, BrowserToolbarAction>) {
        browserStore.observeWhileActive {
            distinctUntilChangedBy { it.selectedTab?.content?.url }
            .collect {
                updateCurrentPageOrigin(store)
                updateEndBrowserActions(store)
                updateNavigationActions(store)
            }
        }
    }

    private fun observeIPProtectionUpdates(store: Store<BrowserToolbarState, BrowserToolbarAction>) {
        ipProtectionStore.observeWhileActive {
            distinctUntilChangedBy { it.proxyStatus }
                .collect {
                    updateStartPageActions(store)
                }
        }
    }

    private fun updateCurrentPageOrigin(
        store: Store<BrowserToolbarState, BrowserToolbarAction>,
    ) = scope.launch {
        val url = browserStore.state.selectedTab?.content?.url?.let {
            it.applyRegistrableDomainSpan(publicSuffixList)
        }
        val searchTerms = browserStore.state.selectedTab?.content?.searchTerms ?: ""

        val displayUrl = url?.let { originalUrl ->
            if (originalUrl.toString() == ABOUT_HOME_URL) {
                // Default to showing the toolbar hint when the URL is ABOUT_HOME.
                ""
            } else if (searchTerms.isNotBlank()) {
                searchTerms
            } else {
                URLStringUtils.toDisplayUrl(originalUrl)
            }
        }

        store.dispatch(
            BrowserDisplayToolbarAction.PageOriginUpdated(
                PageOrigin(
                    hint = R.string.search_hint,
                    title = null,
                    url = displayUrl,
                    contextualMenuOptions = ContextualMenuOption.entries,
                    onClick = OriginClicked,
                ),
            ),
        )
    }

    private fun observePageSecurityUpdates(store: Store<BrowserToolbarState, BrowserToolbarAction>) {
        browserStore.observeWhileActive {
            distinctUntilChangedBy { it.selectedTab?.content?.securityInfo }
                .collect {
                    updateStartPageActions(store)
                }
        }
    }

    private fun observeAcceptingCancellingPrivateDownloads(
        store: Store<BrowserToolbarState, BrowserToolbarAction>,
    ) {
        browserScreenStore.observeWhileActive {
            distinctUntilChangedBy { it.cancelPrivateDownloadsAccepted }
            .collect {
                if (it.cancelPrivateDownloadsAccepted) {
                    store.dispatch(CloseCurrentTab)
                }
            }
        }
    }

    private fun observeReaderModeUpdates(store: Store<BrowserToolbarState, BrowserToolbarAction>) {
        browserScreenStore.observeWhileActive {
            distinctUntilChangedBy { it.readerModeStatus }
                .collect {
                    updateStartPageActions(store)
                    updateEndPageActions(store)
                }
        }
    }

    private fun observePageTranslationsUpdates(store: Store<BrowserToolbarState, BrowserToolbarAction>) {
        browserScreenStore.observeWhileActive {
            distinctUntilChangedBy { it.pageTranslationStatus }
                .collect {
                    updateEndPageActions(store)
                    if (ShortcutType.fromValue(settings.toolbarSimpleShortcut) == ShortcutType.TRANSLATE) {
                        updateEndBrowserActions(store)
                    }
                    if (ShortcutType.fromValue(settings.toolbarExpandedShortcut) == ShortcutType.TRANSLATE) {
                        updateNavigationActions(store)
                    }
                }
        }
    }

    private fun observePageNavigationStatus(store: Store<BrowserToolbarState, BrowserToolbarAction>) {
        browserStore.observeWhileActive {
            distinctUntilChangedBy {
                arrayOf(
                    it.selectedTab?.content?.canGoBack,
                    it.selectedTab?.content?.canGoForward,
                )
            }.collect {
                updateStartBrowserActions(store)
                if (ShortcutType.fromValue(settings.toolbarSimpleShortcut) == ShortcutType.BACK) {
                    updateEndBrowserActions(store)
                }
                if (ShortcutType.fromValue(settings.toolbarExpandedShortcut) == ShortcutType.BACK) {
                    updateNavigationActions(store)
                }
            }
        }
    }

    private fun observePageRefreshUpdates(store: Store<BrowserToolbarState, BrowserToolbarAction>) {
        browserStore.observeWhileActive {
            distinctUntilChangedBy { it.selectedTab?.content?.loading == true }
                .collect { updateStartBrowserActions(store) }
        }
    }

    private fun observePageTrackingProtectionUpdates(
        store: Store<BrowserToolbarState, BrowserToolbarAction>,
    ) {
        browserStore.observeWhileActive {
            distinctUntilChangedBy { it.selectedTab?.trackingProtection }
                .collect { updateStartPageActions(store) }
        }
    }

    private fun observeSelectedTabBookmarkedUpdates(
        store: Store<BrowserToolbarState, BrowserToolbarAction>,
    ) {
        appStore.observeWhileActive {
            distinctUntilChangedBy {
                it.snackbarState is SnackbarState.BookmarkAdded ||
                        it.snackbarState is SnackbarState.BookmarkDeleted
            }.collect { isBookmarked ->
                if (ShortcutType.fromValue(settings.toolbarSimpleShortcut) == ShortcutType.BOOKMARK) {
                    updateEndBrowserActions(store)
                }
                if (ShortcutType.fromValue(settings.toolbarExpandedShortcut) == ShortcutType.BOOKMARK) {
                    updateNavigationActions(store)
                }
            }
        }
    }

    private fun observePermissionHighlightsUpdates(
        store: Store<BrowserToolbarState, BrowserToolbarAction>,
    ) {
        browserStore.observeWhileActive {
            distinctUntilChangedBy { it.selectedTab?.content?.permissionHighlights }
                .collect {
                    updateStartPageActions(store)
                }
        }
    }

    private inline fun <S : State, A : MVIAction> Store<S, A>.observeWhileActive(
        crossinline observe: suspend (Flow<S>.() -> Unit),
    ): Job = scope.launch { flow().observe() }

    @VisibleForTesting
    internal enum class ToolbarAction {
        NewTab,
        Back,
        Forward,
        RefreshOrStop,
        Menu,
        ReaderMode,
        Translate,
        TabCounter,
        SiteInfo,
        Bookmark,
        EditBookmark,
        Share,
        Homepage,
    }

    private data class ToolbarActionConfig(
        val action: ToolbarAction,
        val isVisible: () -> Boolean = { true },
    )

    private fun reconcileSelectedEngine(): SearchEngine? =
        appStore.state.searchState.selectedSearchEngine?.searchEngine
            ?: browserStore.state.search.selectedOrDefaultSearchEngine

    @Suppress("LongMethod", "CognitiveComplexMethod")
    @VisibleForTesting
    internal fun buildAction(
        toolbarAction: ToolbarAction,
        source: Source = Source.Unknown,
    ): Action = when (toolbarAction) {
        ToolbarAction.NewTab -> ActionButtonRes(
            drawableResId = iconsR.drawable.mozac_ic_plus_24,
            contentDescription = if (browsingModeManager.mode == Private) {
                R.string.home_screen_shortcut_open_new_private_tab_2
            } else {
                R.string.home_screen_shortcut_open_new_tab_2
            },
            onClick = if (browsingModeManager.mode == Private) {
                AddNewPrivateTab(source)
            } else {
                AddNewTab(source)
            },
        )

        ToolbarAction.Back -> ActionButtonRes(
            drawableResId = iconsR.drawable.mozac_ic_back_24,
            contentDescription = R.string.browser_menu_back,
            state = if (browserStore.state.canGoBackInHistoryOrToStories()) {
                ActionButton.State.DEFAULT
            } else {
                ActionButton.State.DISABLED
            },
            onClick = NavigateBackClicked(source),
            onLongClick = NavigateBackLongClicked(source),
        )

        ToolbarAction.Forward -> ActionButtonRes(
            drawableResId = iconsR.drawable.mozac_ic_forward_24,
            contentDescription = R.string.browser_menu_forward,
            state = if (browserStore.state.selectedTab?.content?.canGoForward == true) {
                ActionButton.State.DEFAULT
            } else {
                ActionButton.State.DISABLED
            },
            onClick = NavigateForwardClicked,
            onLongClick = NavigateForwardLongClicked,
        )

        ToolbarAction.RefreshOrStop -> {
            if (browserStore.state.selectedTab?.content?.loading != true) {
                ActionButtonRes(
                    drawableResId = iconsR.drawable.mozac_ic_arrow_clockwise_24,
                    contentDescription = R.string.browser_menu_refresh,
                    onClick = RefreshClicked(bypassCache = false),
                    onLongClick = RefreshClicked(bypassCache = true),
                )
            } else {
                ActionButtonRes(
                    drawableResId = iconsR.drawable.mozac_ic_cross_24,
                    contentDescription = R.string.browser_menu_stop,
                    onClick = StopRefreshClicked,
                )
            }
        }

        ToolbarAction.Menu -> ActionButtonRes(
            drawableResId = iconsR.drawable.mozac_ic_ellipsis_vertical_24,
            contentDescription = R.string.content_description_menu,
            highlighted = appStore.state.supportedMenuNotifications.isNotEmpty(),
            onClick = MenuClicked(source),
        )

        ToolbarAction.ReaderMode -> ActionButtonRes(
            drawableResId = if (browserScreenStore.state.readerModeStatus.isActive) {
                iconsR.drawable.mozac_ic_reader_view_fill_24
            } else {
                iconsR.drawable.mozac_ic_reader_view_24
            },
            contentDescription = if (browserScreenStore.state.readerModeStatus.isActive) {
                R.string.browser_menu_read_close
            } else {
                R.string.browser_menu_read
            },
            state = if (browserScreenStore.state.readerModeStatus.isActive) {
                ActionButton.State.ACTIVE
            } else {
                ActionButton.State.DEFAULT
            },
            onClick = ReaderModeClicked(browserScreenStore.state.readerModeStatus.isActive),
        )

        ToolbarAction.Translate -> ActionButtonRes(
            drawableResId = iconsR.drawable.mozac_ic_translate_24,
            contentDescription = R.string.browser_toolbar_translate,
            state = if (browserScreenStore.state.pageTranslationStatus.isTranslated) {
                ActionButton.State.ACTIVE
            } else {
                ActionButton.State.DEFAULT
            },
            onClick = TranslateClicked(source),
        )

        ToolbarAction.TabCounter -> {
            val isInPrivateMode = browsingModeManager.mode.isPrivate
            val tabsCount = browserStore.state.getNormalOrPrivateTabs(isInPrivateMode).size

            val tabCounterDescription = if (isInPrivateMode) {
                uiContext.getString(tabcounterR.string.mozac_tab_counter_private, tabsCount.toString())
            } else {
                uiContext.getString(tabcounterR.string.mozac_tab_counter_open_tab_tray, tabsCount.toString())
            }

            TabCounterAction(
                count = tabsCount,
                contentDescription = tabCounterDescription,
                showPrivacyMask = isInPrivateMode,
                onClick = TabCounterClicked(source),
                onLongClick = buildTabCounterMenu(source, settings.toolbarPosition),
            )
        }

        ToolbarAction.SiteInfo -> {
            val highlight = (
                    browserStore.state.selectedTab
                        ?.content
                        ?.permissionHighlights
                        ?.permissionsChanged == true
                    ) || (
                    browserStore.state.selectedTab
                        ?.trackingProtection
                        ?.ignoredOnTrackingProtection == true
                    )
            val selectedTab = browserStore.state.selectedTab
            if (selectedTab?.content?.url?.isContentUrl() == true) {
                ActionButtonRes(
                    drawableResId = iconsR.drawable.mozac_ic_page_portrait_24,
                    contentDescription = toolbarR.string.mozac_browser_toolbar_content_description_site_info,
                    highlighted = highlight,
                    onClick = StartPageActions.SiteInfoClicked,
                )
            } else if (selectedTab?.content?.securityInfo == null ||
                selectedTab.content.securityInfo == SecurityInfo.Unknown
            ) {
                buildSiteInfoAction(
                    drawableResId = iconsR.drawable.mozac_ic_globe_24,
                    contentDescription = toolbarR.string.mozac_browser_toolbar_content_description_site_info,
                    highlighted = highlight,
                    onClick = object : BrowserToolbarEvent {},
                )
            } else if (
                selectedTab.content.securityInfo.isSecure &&
                selectedTab.trackingProtection.enabled &&
                !selectedTab.trackingProtection.ignoredOnTrackingProtection
            ) {
                buildSiteInfoAction(
                    drawableResId = iconsR.drawable.mozac_ic_shield_checkmark_24,
                    contentDescription = toolbarR.string.mozac_browser_toolbar_content_description_site_info,
                    highlighted = highlight,
                    onClick = StartPageActions.SiteInfoClicked,
                )
            } else {
                buildSiteInfoAction(
                    drawableResId = iconsR.drawable.mozac_ic_shield_slash_24,
                    contentDescription = toolbarR.string.mozac_browser_toolbar_content_description_site_info,
                    highlighted = highlight,
                    onClick = StartPageActions.SiteInfoClicked,
                )
            }
        }

        ToolbarAction.Bookmark -> {
            ActionButtonRes(
                drawableResId = iconsR.drawable.mozac_ic_bookmark_24,
                contentDescription = R.string.browser_menu_bookmark_this_page_2,
                onClick = AddBookmarkClicked(source),
            )
        }

        ToolbarAction.EditBookmark -> {
            ActionButtonRes(
                drawableResId = iconsR.drawable.mozac_ic_bookmark_fill_24,
                contentDescription = R.string.browser_menu_edit_bookmark,
                onClick = EditBookmarkClicked(source),
                state = ActionButton.State.ACTIVE,
            )
        }

        ToolbarAction.Share -> ActionButtonRes(
            drawableResId = iconsR.drawable.mozac_ic_share_android_24,
            contentDescription = R.string.browser_menu_share,
            onClick = ShareClicked(source),
        )

        ToolbarAction.Homepage -> ActionButtonRes(
            drawableResId = iconsR.drawable.mozac_ic_home_24,
            contentDescription = R.string.browser_menu_homepage,
            onClick = HomepageClicked(source),
        )
    }

    private fun buildSiteInfoAction(
        drawableResId: Int,
        contentDescription: Int,
        highlighted: Boolean = false,
        onClick: BrowserToolbarInteraction,
    ): Action {
        return if (ipProtectionStore.state.proxyStatus == Authorized.Active) {
            Action.AnimatedPillActionRes(
                iconResId = drawableResId,
                overlayResId = iconsR.drawable.mozac_ic_globe_24,
                textResId = R.string.ip_protection_toolbar_pill_label,
                contentDescriptionResId = R.string.ip_protection_toolbar_pill_description,
                highlighted = highlighted,
                onClick = onClick,
            )
        } else {
            ActionButtonRes(
                drawableResId = drawableResId,
                contentDescription = contentDescription,
                highlighted = highlighted,
                onClick = onClick,
            )
        }
    }

    private fun Source.toMetricSource() = when (this) {
        is Source.AddressBar, Source.Unknown -> MetricsUtils.BookmarkAction.Source.BROWSER_TOOLBAR
        Source.NavigationBar -> MetricsUtils.BookmarkAction.Source.BROWSER_NAVBAR
    }

    private suspend fun getBookmarkAction(): ToolbarAction {
        val url = browserStore.state.selectedTab?.content?.url ?: return ToolbarAction.Bookmark
        val isBookmarked = withContext(ioDispatcher) {
            bookmarksStorage.getBookmarksWithUrl(url).getOrDefault(emptyList()).isNotEmpty()
        }
        return if (isBookmarked) ToolbarAction.EditBookmark else ToolbarAction.Bookmark
    }

    @VisibleForTesting
    internal suspend fun ShortcutType.toToolbarAction() = when (this) {
        ShortcutType.NEW_TAB -> ToolbarAction.NewTab
        ShortcutType.SHARE -> ToolbarAction.Share
        ShortcutType.BOOKMARK -> getBookmarkAction()
        ShortcutType.TRANSLATE -> ToolbarAction.Translate
        ShortcutType.HOMEPAGE -> ToolbarAction.Homepage
        ShortcutType.BACK -> ToolbarAction.Back
        ShortcutType.NONE -> null
    }
}
