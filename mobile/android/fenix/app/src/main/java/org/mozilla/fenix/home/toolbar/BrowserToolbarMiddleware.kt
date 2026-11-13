/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.home.toolbar

import android.content.Context
import android.content.Intent
import android.speech.RecognizerIntent
import androidx.annotation.DrawableRes
import androidx.annotation.StringRes
import androidx.annotation.VisibleForTesting
import androidx.navigation.NavController
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Job
import kotlinx.coroutines.delay
import kotlinx.coroutines.flow.Flow
import kotlinx.coroutines.flow.distinctUntilChangedBy
import kotlinx.coroutines.flow.first
import kotlinx.coroutines.launch
import mozilla.components.browser.state.search.SearchEngine
import mozilla.components.browser.state.selector.getNormalOrPrivateTabs
import mozilla.components.browser.state.selector.selectedTab
import mozilla.components.browser.state.state.selectedOrDefaultSearchEngine
import mozilla.components.browser.state.store.BrowserStore
import mozilla.components.compose.browser.toolbar.concept.Action
import mozilla.components.compose.browser.toolbar.concept.Action.ActionButton
import mozilla.components.compose.browser.toolbar.concept.Action.ActionButtonRes
import mozilla.components.compose.browser.toolbar.concept.Action.TabCounterAction
import mozilla.components.compose.browser.toolbar.concept.PageOrigin
import mozilla.components.compose.browser.toolbar.concept.PageOrigin.Companion.ContextualMenuOption.LoadFromClipboard
import mozilla.components.compose.browser.toolbar.concept.PageOrigin.Companion.ContextualMenuOption.PasteFromClipboard
import mozilla.components.compose.browser.toolbar.concept.PageOrigin.Companion.PageOriginContextualMenuInteractions.LoadFromClipboardClicked
import mozilla.components.compose.browser.toolbar.concept.PageOrigin.Companion.PageOriginContextualMenuInteractions.PasteFromClipboardClicked
import mozilla.components.compose.browser.toolbar.store.BrowserDisplayToolbarAction
import mozilla.components.compose.browser.toolbar.store.BrowserDisplayToolbarAction.BrowserActionsEndUpdated
import mozilla.components.compose.browser.toolbar.store.BrowserDisplayToolbarAction.NavigationActionsUpdated
import mozilla.components.compose.browser.toolbar.store.BrowserDisplayToolbarAction.PageActionsEndUpdated
import mozilla.components.compose.browser.toolbar.store.BrowserDisplayToolbarAction.PageActionsStartUpdated
import mozilla.components.compose.browser.toolbar.store.BrowserDisplayToolbarAction.PageOriginUpdated
import mozilla.components.compose.browser.toolbar.store.BrowserEditToolbarAction.SearchQueryUpdated
import mozilla.components.compose.browser.toolbar.store.BrowserToolbarAction
import mozilla.components.compose.browser.toolbar.store.BrowserToolbarAction.EnterEditMode
import mozilla.components.compose.browser.toolbar.store.BrowserToolbarAction.ExitEditMode
import mozilla.components.compose.browser.toolbar.store.BrowserToolbarAction.Init
import mozilla.components.compose.browser.toolbar.store.BrowserToolbarInteraction.BrowserToolbarEvent
import mozilla.components.compose.browser.toolbar.store.BrowserToolbarInteraction.BrowserToolbarEvent.Source
import mozilla.components.compose.browser.toolbar.store.BrowserToolbarInteraction.CombinedEventAndMenu
import mozilla.components.compose.browser.toolbar.store.BrowserToolbarMenuItem.BrowserToolbarMenuButton
import mozilla.components.compose.browser.toolbar.store.BrowserToolbarMenuItem.BrowserToolbarMenuButton.ContentDescription.StringResContentDescription
import mozilla.components.compose.browser.toolbar.store.BrowserToolbarMenuItem.BrowserToolbarMenuButton.Icon.DrawableResIcon
import mozilla.components.compose.browser.toolbar.store.BrowserToolbarMenuItem.BrowserToolbarMenuButton.Text.StringResText
import mozilla.components.compose.browser.toolbar.store.BrowserToolbarState
import mozilla.components.compose.browser.toolbar.store.Mode
import mozilla.components.compose.browser.toolbar.ui.BrowserToolbarQuery
import mozilla.components.feature.summarize.R as summariesR
import mozilla.components.lib.state.Action as MVIAction
import mozilla.components.lib.state.Middleware
import mozilla.components.lib.state.State
import mozilla.components.lib.state.Store
import mozilla.components.lib.state.ext.flow
import mozilla.components.support.base.log.logger.Logger
import mozilla.components.support.utils.ClipboardHandler
import mozilla.components.ui.icons.R as iconsR
import mozilla.components.ui.tabcounter.R as tabcounterR
import org.mozilla.fenix.GleanMetrics.Events
import org.mozilla.fenix.NavGraphDirections
import org.mozilla.fenix.R
import org.mozilla.fenix.browser.browsingmode.BrowsingMode
import org.mozilla.fenix.browser.browsingmode.BrowsingMode.Normal
import org.mozilla.fenix.browser.browsingmode.BrowsingMode.Private
import org.mozilla.fenix.browser.browsingmode.BrowsingModeManager
import org.mozilla.fenix.components.AppStore
import org.mozilla.fenix.components.appstate.AppAction
import org.mozilla.fenix.components.appstate.AppAction.SearchAction.SearchStarted
import org.mozilla.fenix.components.appstate.SupportedMenuNotifications
import org.mozilla.fenix.components.appstate.VoiceSearchAction.VoiceInputRequested
import org.mozilla.fenix.components.menu.MenuAccessPoint
import org.mozilla.fenix.components.usecases.FenixBrowserUseCases
import org.mozilla.fenix.ext.nav
import org.mozilla.fenix.home.HomeFragmentDirections
import org.mozilla.fenix.home.goBackFromHomepage
import org.mozilla.fenix.home.goForwardFromHomepage
import org.mozilla.fenix.home.privatebrowsing.controller.PrivateBrowsingController
import org.mozilla.fenix.home.toolbar.DisplayActions.FakeClicked
import org.mozilla.fenix.home.toolbar.DisplayActions.MenuClicked
import org.mozilla.fenix.home.toolbar.DisplayActions.PrivateModeClicked
import org.mozilla.fenix.home.toolbar.DisplayActions.StoriesClicked
import org.mozilla.fenix.home.toolbar.DisplayActions.VoiceSearchClicked
import org.mozilla.fenix.home.toolbar.NavigationInteractions.NavigateBackClicked
import org.mozilla.fenix.home.toolbar.NavigationInteractions.NavigateBackLongClicked
import org.mozilla.fenix.home.toolbar.NavigationInteractions.NavigateForwardClicked
import org.mozilla.fenix.home.toolbar.NavigationInteractions.NavigateForwardLongClicked
import org.mozilla.fenix.home.toolbar.PageOriginInteractions.OriginClicked
import org.mozilla.fenix.home.toolbar.TabCounterInteractions.AddNewPrivateTab
import org.mozilla.fenix.home.toolbar.TabCounterInteractions.AddNewTab
import org.mozilla.fenix.home.toolbar.TabCounterInteractions.TabCounterClicked
import org.mozilla.fenix.home.toolbar.TabCounterInteractions.TabCounterLongClicked
import org.mozilla.fenix.nimbus.FxNimbus
import org.mozilla.fenix.search.BrowserToolbarSearchMiddleware
import org.mozilla.fenix.search.ext.searchEngineShortcuts
import org.mozilla.fenix.settings.ShortcutType
import org.mozilla.fenix.tabstray.redux.state.Page
import org.mozilla.fenix.translations.TranslationsEnabledSettings
import org.mozilla.fenix.utils.Settings

// Speculative delay for putting the toolbar in edit mode after an initial voice search request.
private const val DISPLAY_TOOLBAR_DELAY_AFTER_VOICE_REQUEST = 1_000L

@VisibleForTesting
internal sealed class DisplayActions : BrowserToolbarEvent {
    data class MenuClicked(override val source: Source) : DisplayActions()

    data object FakeClicked : DisplayActions()

    data object VoiceSearchClicked : DisplayActions()

    data class StoriesClicked(override val source: Source) : DisplayActions()

    data class PrivateModeClicked(override val source: Source) : DisplayActions()
}

internal sealed class TabCounterInteractions : BrowserToolbarEvent {
    data class TabCounterClicked(override val source: Source) : TabCounterInteractions()

    data class TabCounterLongClicked(override val source: Source) : TabCounterInteractions()

    data class AddNewTab(override val source: Source) : TabCounterInteractions()

    data class AddNewPrivateTab(override val source: Source) : TabCounterInteractions()
}

internal sealed class PageOriginInteractions : BrowserToolbarEvent {
    data object OriginClicked : PageOriginInteractions()
}

@VisibleForTesting
internal sealed class NavigationInteractions : BrowserToolbarEvent {
    data class NavigateBackClicked(override val source: Source) : NavigationInteractions()

    data class NavigateBackLongClicked(override val source: Source) : NavigationInteractions()

    data class NavigateForwardClicked(override val source: Source) : NavigationInteractions()

    data class NavigateForwardLongClicked(override val source: Source) : NavigationInteractions()
}

/**
 * [Middleware] responsible for configuring and handling interactions with the composable toolbar.
 *
 * @param uiContext [Context] used for various system interactions.
 * @param appStore [AppStore] to sync from.
 * @param browserStore [BrowserStore] to sync from.
 * @param clipboard [ClipboardHandler] to use for reading from device's clipboard.
 * @param fenixBrowserUseCases [FenixBrowserUseCases] for loading URLs and opening new tabs.
 * @param navController [NavController] to use for navigating to other in-app destinations.
 * @param browsingModeManager [BrowsingModeManager] for querying the current browsing mode.
 * @param privateBrowsingController [PrivateBrowsingController] for handling private browsing interactions.
 * @param settings [Settings] for accessing application settings.
 * @param translationsFeatureSettings Web content translations availability status.
 * @param isWideScreen Callback for checking if the screen is wide.
 * @param isTallScreen Callback for checking if the screen is tall.
 * @param scope [CoroutineScope] used for running long running operations in background.
 */
@Suppress("LongParameterList", "TooManyFunctions")
class BrowserToolbarMiddleware(
    private val uiContext: Context,
    private val appStore: AppStore,
    private val browserStore: BrowserStore,
    private val clipboard: ClipboardHandler,
    private val fenixBrowserUseCases: FenixBrowserUseCases,
    private val navController: NavController,
    private val browsingModeManager: BrowsingModeManager,
    private val privateBrowsingController: PrivateBrowsingController,
    private val settings: Settings,
    private val translationsFeatureSettings: TranslationsEnabledSettings,
    private val isWideScreen: () -> Boolean,
    private val isTallScreen: () -> Boolean,
    private val scope: CoroutineScope,
) : Middleware<BrowserToolbarState, BrowserToolbarAction> {
    private var syncCurrentSearchEngineJob: Job? = null
    private var observeBrowserSearchStateJob: Job? = null

    @Suppress("LongMethod", "CyclomaticComplexMethod", "CognitiveComplexMethod")
    override fun invoke(
        store: Store<BrowserToolbarState, BrowserToolbarAction>,
        next: (BrowserToolbarAction) -> Unit,
        action: BrowserToolbarAction,
    ) {
        when (action) {
            is Init -> {
                next(action)

                if (store.state.mode == Mode.DISPLAY) {
                    observeSearchStateUpdates(store)
                }

                updatePageOrigin(store)
                updateStartBrowserActions(store)
                updateEndPageActions(store)
                updateEndBrowserActions(store)
                scope.launch {
                    updateNavigationActions(store)
                }
                updateToolbarActionsBasedOnOrientation(store)
                updateToolbarActionsBasedOnBrowsingMode(store)
                updateTabsCount(store)
                updateMenuHighlight(store)
                observePageNavigationStatus(store)

                observeTranslationsFeatureAvailabilityUpdates(store)
            }

            is EnterEditMode -> {
                next(action)

                stopSearchStateUpdates()
            }

            is ExitEditMode -> {
                next(action)

                observeSearchStateUpdates(store)
            }

            is MenuClicked -> {
                navController.nav(
                    R.id.homeFragment,
                    when (settings.isMenuCustomizationEnabled) {
                        true ->
                            HomeFragmentDirections.actionHomeFragmentToMenuFragment(accessPoint = MenuAccessPoint.Home)
                        else ->
                            HomeFragmentDirections.actionGlobalMenuDialogFragment(accesspoint = MenuAccessPoint.Home)
                    },
                )
                removeMenuButtonHighlight()
                next(action)
            }

            is TabCounterClicked -> {
                navController.nav(
                    R.id.homeFragment,
                    NavGraphDirections.actionGlobalTabManagementFragment(
                        page =
                            when (browsingModeManager.mode) {
                                Normal -> Page.NormalTabs
                                Private -> Page.PrivateTabs
                            }
                    ),
                )
                next(action)
            }
            is AddNewTab -> {
                addNewTab(store, Normal)
                next(action)
            }
            is AddNewPrivateTab -> {
                addNewTab(store, Private)
                next(action)
            }

            is StoriesClicked -> {
                navController.nav(
                    R.id.homeFragment,
                    HomeFragmentDirections.actionHomeFragmentToStoriesFragment(),
                )
                next(action)
            }

            is PrivateModeClicked -> {
                privateBrowsingController.handlePrivateModeButtonClicked(
                    BrowsingMode.fromBoolean(!browsingModeManager.mode.isPrivate)
                )
                next(action)
            }

            is OriginClicked -> {
                Events.searchBarTapped.record(Events.SearchBarTappedExtra("HOME"))
                appStore.dispatch(SearchStarted())
            }
            is NavigateBackClicked -> {
                goBackFromHomepage(
                    browserStore = browserStore,
                    navController = navController,
                )
                next(action)
            }
            is NavigateBackLongClicked -> {
                showTabHistory()
                next(action)
            }
            is NavigateForwardClicked -> {
                goForwardFromHomepage(
                    browserStore = browserStore,
                    navController = navController,
                )
                next(action)
            }
            is NavigateForwardLongClicked -> {
                showTabHistory()
                next(action)
            }
            is VoiceSearchClicked -> {
                scope.launch {
                    appStore.dispatch(VoiceInputRequested)
                    delay(DISPLAY_TOOLBAR_DELAY_AFTER_VOICE_REQUEST)
                    appStore.dispatch(SearchStarted())
                }
                next(action)
            }
            is PasteFromClipboardClicked -> {
                openNewTab(store, searchTerms = clipboard.text)
            }
            is LoadFromClipboardClicked -> {
                clipboard.extractURL()?.let {
                    fenixBrowserUseCases.loadUrlOrSearch(
                        searchTermOrURL = it,
                        newTab = !settings.enableHomepageAsNewTab,
                        private = browsingModeManager.mode == Private,
                        searchEngine = reconcileSelectedEngine(),
                    )
                    navController.navigate(R.id.browserFragment)
                }
                    ?: run {
                        Logger("HomeOriginContextMenu").error("Clipboard contains URL but unable to read text")
                    }
            }

            else -> next(action)
        }
    }

    private fun showTabHistory() =
        navController.nav(
            R.id.homeFragment,
            NavGraphDirections.actionGlobalTabHistoryDialogFragment(activeSessionId = null),
        )

    private fun addNewTab(
        store: Store<BrowserToolbarState, BrowserToolbarAction>,
        browsingMode: BrowsingMode,
    ) {
        if (settings.enableHomepageAsNewTab) {
            fenixBrowserUseCases.addNewHomepageTab(private = browsingMode.isPrivate)
            browsingModeManager.mode = browsingMode
        } else {
            openNewTab(store, browsingMode)
        }
    }

    private fun openNewTab(
        store: Store<BrowserToolbarState, BrowserToolbarAction>,
        browsingMode: BrowsingMode? = null,
        searchTerms: String? = null,
    ) {
        browsingMode?.let { browsingModeManager.mode = it }
        store.dispatch(SearchQueryUpdated(BrowserToolbarQuery(searchTerms ?: ""), true))
        appStore.dispatch(SearchStarted())
    }

    private fun observeSearchStateUpdates(store: Store<BrowserToolbarState, BrowserToolbarAction>) {
        syncCurrentSearchEngineJob?.cancel()
        syncCurrentSearchEngineJob = appStore.observeWhileActive {
            distinctUntilChangedBy { it.searchState.selectedSearchEngine?.searchEngine }
                .collect {
                    it.searchState.selectedSearchEngine?.let {
                        updateStartPageActions(store, it.searchEngine)
                    }
                }
        }

        observeBrowserSearchStateJob?.cancel()
        observeBrowserSearchStateJob = browserStore.observeWhileActive {
            distinctUntilChangedBy { it.search.searchEngineShortcuts }
                .collect {
                    updateStartPageActions(
                        store = store,
                        selectedSearchEngine = reconcileSelectedEngine(),
                    )
                }
        }
    }

    private fun stopSearchStateUpdates() {
        syncCurrentSearchEngineJob?.cancel()
        observeBrowserSearchStateJob?.cancel()
    }

    private fun updateStartPageActions(
        store: Store<BrowserToolbarState, BrowserToolbarAction>,
        selectedSearchEngine: SearchEngine?,
    ) = store.dispatch(PageActionsStartUpdated(buildStartPageActions(selectedSearchEngine)))

    private fun updatePageOrigin(store: Store<BrowserToolbarState, BrowserToolbarAction>) =
        store.dispatch(
            PageOriginUpdated(
                PageOrigin(
                    hint =
                        if (isVoiceSearchEnabledForDisplayToolbar()) {
                            R.string.search_hint_short
                        } else {
                            R.string.search_hint
                        },
                    title = null,
                    url = null,
                    contextualMenuOptions = listOf(PasteFromClipboard, LoadFromClipboard),
                    onClick = OriginClicked,
                )
            )
        )

    private fun updateStartBrowserActions(store: Store<BrowserToolbarState, BrowserToolbarAction>) {
        store.dispatch(BrowserDisplayToolbarAction.BrowserActionsStartUpdated(buildStartBrowserActions()))
    }

    private fun updateEndBrowserActions(store: Store<BrowserToolbarState, BrowserToolbarAction>) {
        store.dispatch(BrowserActionsEndUpdated(buildEndBrowserActions()))
    }

    private fun updateEndPageActions(store: Store<BrowserToolbarState, BrowserToolbarAction>) =
        store.dispatch(PageActionsEndUpdated(buildEndPageActions()))

    private fun buildEndPageActions(): List<Action> = buildList {
        if (isVoiceSearchEnabledForDisplayToolbar() && isSpeechRecognitionAvailable()) {
            add(
                ActionButtonRes(
                    drawableResId = iconsR.drawable.mozac_ic_microphone_24,
                    contentDescription = R.string.voice_search_content_description,
                    onClick = VoiceSearchClicked,
                )
            )
        }
    }

    private fun isVoiceSearchEnabledForDisplayToolbar() =
        settings.shouldShowVoiceSearch && settings.showVoiceSearchInDisplayToolbar

    private fun isSpeechRecognitionAvailable() =
        Intent(RecognizerIntent.ACTION_RECOGNIZE_SPEECH).resolveActivity(uiContext.packageManager) != null

    private fun buildStartPageActions(selectedSearchEngine: SearchEngine?): List<Action> {
        return listOfNotNull(
            BrowserToolbarSearchMiddleware.buildSearchSelector(
                selectedSearchEngine = selectedSearchEngine,
                searchEngineShortcuts = browserStore.state.search.searchEngineShortcuts,
                resources = uiContext.resources,
            )
        )
    }

    private fun buildStartBrowserActions(): List<Action> =
        listOf(
                HomeToolbarActionConfig(HomeToolbarAction.Stories) {
                    settings.isHomepageMiddleSearchEnabled &&
                        settings.showPocketRecommendationsFeature &&
                        !browsingModeManager.mode.isPrivate
                },
                HomeToolbarActionConfig(HomeToolbarAction.Back) {
                    isWideScreen() && settings.enableHomepageAsNewTab
                },
                HomeToolbarActionConfig(HomeToolbarAction.Forward) {
                    isWideScreen() && settings.enableHomepageAsNewTab
                },
            )
            .filter { config -> config.isVisible() }
            .map { config -> buildHomeAction(config.action, Source.AddressBar.BrowserStart) }

    private fun buildEndBrowserActions(): List<Action> {
        val isWideWindow = isWideScreen()
        val isTallWindow = isTallScreen()
        val shouldUseExpandedToolbar = settings.shouldUseExpandedToolbar

        return listOf(
                HomeToolbarActionConfig(HomeToolbarAction.PrivateMode) {
                    settings.isHomepageMiddleSearchEnabled
                },
                HomeToolbarActionConfig(HomeToolbarAction.TabCounter) {
                    !shouldUseExpandedToolbar || !isTallWindow || isWideWindow
                },
                HomeToolbarActionConfig(HomeToolbarAction.Menu) {
                    !shouldUseExpandedToolbar || !isTallWindow || isWideWindow
                },
            )
            .filter { config ->
                config.isVisible()
            }
            .map { config ->
                buildHomeAction(config.action, Source.AddressBar.BrowserEnd)
            }
    }

    private suspend fun updateNavigationActions(store: Store<BrowserToolbarState, BrowserToolbarAction>) {
        store.dispatch(NavigationActionsUpdated(buildNavigationActions()))
    }

    /**
     * - Devices taller than 480dp:
     *     - The navigation bar is always shown (if the user enabled it).
     *
     * - Devices shorter than 480dp:
     *     - The navigation bar is hidden (even if the user enabled it).
     *     - The toolbar redesign customization option is also hidden.
     *
     *   Devices wider than 600dp:
     *     - The navigation bar is hidden. (even If user enabled it)
     *     - The toolbar redesign customization option is also hidden.
     */
    private suspend fun buildNavigationActions(): List<Action> {
        val isWideWindow = isWideScreen()
        val isTallWindow = isTallScreen()
        val shouldUseExpandedToolbar = settings.shouldUseExpandedToolbar
        val primarySlotAction =
            ShortcutType.fromValue(settings.toolbarExpandedShortcutKey)?.toHomeToolbarAction()
                ?: HomeToolbarAction.FakeBookmark

        return listOf(
                HomeToolbarActionConfig(primarySlotAction) {
                    shouldUseExpandedToolbar && isTallWindow && !isWideWindow
                },
                HomeToolbarActionConfig(HomeToolbarAction.FakeShare) {
                    shouldUseExpandedToolbar && isTallWindow && !isWideWindow
                },
                HomeToolbarActionConfig(HomeToolbarAction.NewTab) {
                    shouldUseExpandedToolbar && isTallWindow && !isWideWindow
                },
                HomeToolbarActionConfig(HomeToolbarAction.TabCounter) {
                    shouldUseExpandedToolbar && isTallWindow && !isWideWindow
                },
                HomeToolbarActionConfig(HomeToolbarAction.Menu) {
                    shouldUseExpandedToolbar && isTallWindow && !isWideWindow
                },
            )
            .filter { config ->
                config.isVisible()
            }
            .map { config ->
                buildHomeAction(config.action, Source.NavigationBar)
            }
    }

    private fun buildTabCounterMenu(source: Source): CombinedEventAndMenu? {
        val currentBrowsingMode = browsingModeManager.mode

        return CombinedEventAndMenu(TabCounterLongClicked(source)) {
            when (currentBrowsingMode) {
                Private ->
                    listOf(
                        BrowserToolbarMenuButton(
                            icon = DrawableResIcon(iconsR.drawable.mozac_ic_plus_24),
                            text = StringResText(tabcounterR.string.mozac_browser_menu_new_tab),
                            contentDescription =
                                StringResContentDescription(tabcounterR.string.mozac_browser_menu_new_tab),
                            onClick = AddNewTab(source),
                        )
                    )

                else ->
                    listOf(
                        BrowserToolbarMenuButton(
                            icon = DrawableResIcon(iconsR.drawable.mozac_ic_private_mode_fill_24),
                            text = StringResText(tabcounterR.string.mozac_browser_menu_new_private_tab),
                            contentDescription =
                                StringResContentDescription(tabcounterR.string.mozac_browser_menu_new_private_tab),
                            onClick = AddNewPrivateTab(source),
                        )
                    )
            }
        }
    }

    private fun updateToolbarActionsBasedOnBrowsingMode(store: Store<BrowserToolbarState, BrowserToolbarAction>) {
        appStore.observeWhileActive {
            distinctUntilChangedBy { it.mode }
                .collect {
                    updateStartBrowserActions(store)
                    updateEndBrowserActions(store)
                }
        }
    }

    private fun updateToolbarActionsBasedOnOrientation(store: Store<BrowserToolbarState, BrowserToolbarAction>) {
        appStore.observeWhileActive {
            distinctUntilChangedBy { it.orientation }
                .collect {
                    updateStartBrowserActions(store)
                    updateEndBrowserActions(store)
                    updateNavigationActions(store)
                }
        }
    }

    private fun updateTabsCount(store: Store<BrowserToolbarState, BrowserToolbarAction>) {
        browserStore.observeWhileActive {
            distinctUntilChangedBy { it.tabs.size }
                .collect {
                    updateEndBrowserActions(store)
                    updateNavigationActions(store)
                }
        }
    }

    private fun observePageNavigationStatus(store: Store<BrowserToolbarState, BrowserToolbarAction>) {
        browserStore.observeWhileActive {
            distinctUntilChangedBy {
                Pair(
                    it.selectedTab?.content?.canGoBack,
                    it.selectedTab?.content?.canGoForward,
                )
            }
                .collect {
                    updateStartBrowserActions(store)
                    if (ShortcutType.fromValue(settings.toolbarExpandedShortcutKey) == ShortcutType.BACK) {
                        updateNavigationActions(store)
                    }
                }
        }
    }

    private fun updateMenuHighlight(store: Store<BrowserToolbarState, BrowserToolbarAction>) {
        appStore.observeWhileActive {
            distinctUntilChangedBy { it.supportedMenuNotifications.shouldHighlightHomeMenuButton() }
                .collect {
                    updateEndBrowserActions(store)
                    updateNavigationActions(store)
                }
        }
    }

    private fun removeMenuButtonHighlight() {
        val notification = SupportedMenuNotifications.NotDefaultBrowser
        if (notification in appStore.state.supportedMenuNotifications) {
            appStore.dispatch(AppAction.MenuNotification.RemoveMenuNotification(notification))
        }
    }

    private fun observeTranslationsFeatureAvailabilityUpdates(store: Store<BrowserToolbarState, BrowserToolbarAction>) {
        scope.launch {
            translationsFeatureSettings.isEnabled.collect {
                updateEndBrowserActions(store)
            }
        }
    }

    private inline fun <S : State, A : MVIAction> Store<S, A>.observeWhileActive(
        crossinline observe: suspend (Flow<S>.() -> Unit)
    ): Job = scope.launch { flow().observe() }

    private fun reconcileSelectedEngine(): SearchEngine? =
        appStore.state.searchState.selectedSearchEngine?.searchEngine
            ?: browserStore.state.search.selectedOrDefaultSearchEngine(private = browsingModeManager.mode.isPrivate)

    @VisibleForTesting
    internal enum class HomeToolbarAction {
        TabCounter,
        Menu,
        Stories,
        PrivateMode,
        FakeBookmark,
        FakeShare,
        NewTab,
        FakeTranslate,
        FakeHomepage,
        Back,
        FakeBack,
        Forward,
        FakeSummarize,
    }

    private data class HomeToolbarActionConfig(
        val action: HomeToolbarAction,
        val isVisible: () -> Boolean = { true },
    )

    private fun createFakeActionButtonRes(
        @DrawableRes drawableResId: Int,
        @StringRes contentDescription: Int,
    ) =
        ActionButtonRes(
            drawableResId = drawableResId,
            contentDescription = contentDescription,
            state = ActionButton.State.DISABLED,
            onClick = FakeClicked,
        )

    private fun createBackActionButtonRes(source: Source) =
        ActionButtonRes(
            drawableResId = iconsR.drawable.mozac_ic_back_24,
            contentDescription = R.string.browser_menu_back,
            state =
                if (browserStore.state.selectedTab?.content?.canGoBack == true) {
                    ActionButton.State.DEFAULT
                } else {
                    ActionButton.State.DISABLED
                },
            onClick = NavigateBackClicked(source),
            onLongClick = NavigateBackLongClicked(source),
        )

    private fun createForwardActionButtonRes(source: Source) =
        ActionButtonRes(
            drawableResId = iconsR.drawable.mozac_ic_forward_24,
            contentDescription = R.string.browser_menu_forward,
            state =
                if (browserStore.state.selectedTab?.content?.canGoForward == true) {
                    ActionButton.State.DEFAULT
                } else {
                    ActionButton.State.DISABLED
                },
            onClick = NavigateForwardClicked(source),
            onLongClick = NavigateForwardLongClicked(source),
        )

    @VisibleForTesting
    internal fun buildHomeAction(
        action: HomeToolbarAction,
        source: Source = Source.Unknown,
    ): Action =
        when (action) {
            HomeToolbarAction.TabCounter -> {
                val isInPrivateMode = browsingModeManager.mode.isPrivate
                val tabsCount = browserStore.state.getNormalOrPrivateTabs(isInPrivateMode).size

                val tabCounterDescription =
                    if (isInPrivateMode) {
                        uiContext.getString(tabcounterR.string.mozac_tab_counter_private, tabsCount.toString())
                    } else {
                        uiContext.getString(tabcounterR.string.mozac_tab_counter_open_tab_tray, tabsCount.toString())
                    }

                TabCounterAction(
                    count = tabsCount,
                    contentDescription = tabCounterDescription,
                    showPrivacyMask = isInPrivateMode,
                    onClick = TabCounterClicked(source),
                    onLongClick = buildTabCounterMenu(source),
                )
            }

            HomeToolbarAction.Menu -> {
                val highlighted = appStore.state.supportedMenuNotifications.shouldHighlightHomeMenuButton()
                ActionButtonRes(
                    drawableResId = iconsR.drawable.mozac_ic_more_vertical_round_24,
                    contentDescription = R.string.content_description_menu,
                    highlighted = highlighted,
                    onClick = MenuClicked(source),
                )
            }

            HomeToolbarAction.FakeBookmark ->
                createFakeActionButtonRes(
                    drawableResId = iconsR.drawable.mozac_ic_bookmark_24,
                    contentDescription = R.string.browser_menu_bookmark_this_page_2,
                )

            HomeToolbarAction.FakeShare ->
                createFakeActionButtonRes(
                    drawableResId = iconsR.drawable.mozac_ic_share_android_24,
                    contentDescription = R.string.browser_menu_share,
                )

            HomeToolbarAction.Stories ->
                ActionButtonRes(
                    drawableResId = iconsR.drawable.mozac_ic_newsfeed_24,
                    contentDescription = R.string.homepage_all_stories,
                    onClick = StoriesClicked(source),
                )

            HomeToolbarAction.PrivateMode ->
                ActionButtonRes(
                    drawableResId = iconsR.drawable.mozac_ic_private_mode_24,
                    contentDescription = R.string.content_description_private_browsing,
                    state =
                        if (browsingModeManager.mode.isPrivate) {
                            ActionButton.State.ACTIVE
                        } else {
                            ActionButton.State.DEFAULT
                        },
                    onClick = PrivateModeClicked(source),
                )

            HomeToolbarAction.NewTab ->
                ActionButtonRes(
                    drawableResId = iconsR.drawable.mozac_ic_plus_24,
                    contentDescription =
                        if (browsingModeManager.mode == Private) {
                            R.string.home_screen_shortcut_open_new_private_tab_2
                        } else {
                            R.string.home_screen_shortcut_open_new_tab_2
                        },
                    onClick =
                        if (browsingModeManager.mode == Private) {
                            AddNewPrivateTab(source)
                        } else {
                            AddNewTab(source)
                        },
                )

            HomeToolbarAction.FakeTranslate ->
                createFakeActionButtonRes(
                    drawableResId = iconsR.drawable.mozac_ic_translate_24,
                    contentDescription = R.string.browser_toolbar_translate,
                )

            HomeToolbarAction.FakeHomepage ->
                createFakeActionButtonRes(
                    drawableResId = iconsR.drawable.mozac_ic_home_24,
                    contentDescription = R.string.browser_menu_homepage,
                )

            HomeToolbarAction.FakeBack ->
                createFakeActionButtonRes(
                    drawableResId = iconsR.drawable.mozac_ic_back_24,
                    contentDescription = R.string.browser_menu_back,
                )

            HomeToolbarAction.FakeSummarize ->
                createFakeActionButtonRes(
                    drawableResId = iconsR.drawable.mozac_ic_lightning_24,
                    contentDescription = summariesR.string.mozac_summarize_settings_summarize_pages,
                )

            HomeToolbarAction.Back -> createBackActionButtonRes(source)

            HomeToolbarAction.Forward -> createForwardActionButtonRes(source)
        }

    private suspend fun isTranslationsFeatureAvailable(): Boolean {
        val isTranslationEngineSupported = browserStore.state.translationEngine.isEngineSupported ?: false
        return isTranslationEngineSupported &&
            FxNimbus.features.translations.value().mainFlowToolbarEnabled &&
            translationsFeatureSettings.isEnabled.first()
    }

    @VisibleForTesting
    internal suspend fun ShortcutType.toHomeToolbarAction() =
        when (this) {
            ShortcutType.NEW_TAB -> HomeToolbarAction.NewTab
            ShortcutType.SHARE -> HomeToolbarAction.FakeShare
            ShortcutType.BOOKMARK -> HomeToolbarAction.FakeBookmark
            ShortcutType.TRANSLATE ->
                when (isTranslationsFeatureAvailable()) {
                    true -> HomeToolbarAction.FakeTranslate
                    false -> HomeToolbarAction.FakeBookmark // the first available option in settings.
                }
            ShortcutType.HOMEPAGE -> HomeToolbarAction.FakeHomepage
            ShortcutType.BACK ->
                when (settings.enableHomepageAsNewTab) {
                    true -> HomeToolbarAction.Back
                    false -> HomeToolbarAction.FakeBack
                }
            ShortcutType.SUMMARIZE -> HomeToolbarAction.FakeSummarize
            ShortcutType.NONE -> null
        }

    private fun Set<SupportedMenuNotifications>.shouldHighlightHomeMenuButton() = any {
        it !is SupportedMenuNotifications.Summarize &&
            it !is SupportedMenuNotifications.TabGroups &&
            it != SupportedMenuNotifications.OpenInApp
    }
}
