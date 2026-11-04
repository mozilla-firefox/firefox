/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.search

import android.os.Build
import androidx.annotation.VisibleForTesting
import androidx.fragment.app.Fragment
import androidx.lifecycle.DefaultLifecycleObserver
import androidx.lifecycle.LifecycleOwner
import androidx.lifecycle.ProcessLifecycleOwner
import androidx.lifecycle.coroutineScope
import androidx.lifecycle.lifecycleScope
import androidx.navigation.NavController
import androidx.navigation.NavOptions
import kotlinx.coroutines.Job
import kotlinx.coroutines.flow.Flow
import kotlinx.coroutines.flow.distinctUntilChangedBy
import kotlinx.coroutines.launch
import mozilla.components.browser.state.action.AwesomeBarAction
import mozilla.components.browser.state.ext.getUrl
import mozilla.components.browser.state.search.DefaultSearchEngineProvider
import mozilla.components.browser.state.search.SearchAccessPoint
import mozilla.components.browser.state.search.SearchEngine
import mozilla.components.browser.state.selector.findTab
import mozilla.components.browser.state.store.BrowserStore
import mozilla.components.compose.browser.toolbar.store.BrowserEditToolbarAction
import mozilla.components.compose.browser.toolbar.store.BrowserEditToolbarAction.SearchQueryUpdated
import mozilla.components.compose.browser.toolbar.store.BrowserToolbarStore
import mozilla.components.compose.browser.toolbar.ui.BrowserToolbarQuery
import mozilla.components.concept.awesomebar.AwesomeBar
import mozilla.components.concept.engine.Engine
import mozilla.components.concept.engine.EngineSession.LoadUrlFlags
import mozilla.components.concept.engine.prompt.ShareData
import mozilla.components.feature.search.SearchUseCases.SearchUseCase
import mozilla.components.feature.session.SessionUseCases.LoadUrlUseCase
import mozilla.components.feature.tabs.TabsUseCases.SelectTabUseCase
import mozilla.components.lib.state.Action as MVIAction
import mozilla.components.lib.state.Middleware
import mozilla.components.lib.state.State
import mozilla.components.lib.state.Store
import mozilla.components.lib.state.ext.flow
import mozilla.components.support.ktx.util.URLStringUtils
import mozilla.components.support.utils.ClipboardHandler
import mozilla.telemetry.glean.private.NoExtras
import org.mozilla.fenix.GleanMetrics.BookmarksManagement
import org.mozilla.fenix.GleanMetrics.Events
import org.mozilla.fenix.GleanMetrics.History
import org.mozilla.fenix.GleanMetrics.Toolbar
import org.mozilla.fenix.NavGraphDirections
import org.mozilla.fenix.R
import org.mozilla.fenix.browser.browsingmode.BrowsingMode
import org.mozilla.fenix.browser.browsingmode.BrowsingModeManager
import org.mozilla.fenix.components.AppStore
import org.mozilla.fenix.components.NimbusComponents
import org.mozilla.fenix.components.UseCases
import org.mozilla.fenix.components.appstate.AppAction.SearchAction.SearchEngineSelected
import org.mozilla.fenix.components.appstate.AppAction.URLCopiedToClipboard
import org.mozilla.fenix.components.metrics.MetricsUtils
import org.mozilla.fenix.components.metrics.toSearchAccessPoint
import org.mozilla.fenix.components.search.BOOKMARKS_SEARCH_ENGINE_ID
import org.mozilla.fenix.components.search.HISTORY_SEARCH_ENGINE_ID
import org.mozilla.fenix.components.search.TABS_SEARCH_ENGINE_ID
import org.mozilla.fenix.components.share.ShareSource
import org.mozilla.fenix.components.usecases.ShareUseCases
import org.mozilla.fenix.ext.components
import org.mozilla.fenix.ext.nav
import org.mozilla.fenix.ext.telemetryName
import org.mozilla.fenix.search.SearchFragmentAction.CopyCurrentWebsiteDetailsClicked
import org.mozilla.fenix.search.SearchFragmentAction.EditCurrentWebsiteDetailsClicked
import org.mozilla.fenix.search.SearchFragmentAction.Init
import org.mozilla.fenix.search.SearchFragmentAction.PrivateSuggestionsCardAccepted
import org.mozilla.fenix.search.SearchFragmentAction.ReloadCurrentWebsiteClicked
import org.mozilla.fenix.search.SearchFragmentAction.SearchEnginesSelectedActions
import org.mozilla.fenix.search.SearchFragmentAction.SearchProvidersUpdated
import org.mozilla.fenix.search.SearchFragmentAction.SearchStarted
import org.mozilla.fenix.search.SearchFragmentAction.SearchSuggestionsVisibilityUpdated
import org.mozilla.fenix.search.SearchFragmentAction.ShareCurrentWebsiteDetailsClicked
import org.mozilla.fenix.search.SearchFragmentAction.SuggestionClicked
import org.mozilla.fenix.search.SearchFragmentAction.SuggestionSelected
import org.mozilla.fenix.search.SearchFragmentAction.UpdateQuery
import org.mozilla.fenix.search.awesomebar.DefaultSuggestionIconProvider
import org.mozilla.fenix.search.awesomebar.DefaultSuggestionsStringsProvider
import org.mozilla.fenix.search.awesomebar.SearchSuggestionsProvidersBuilder
import org.mozilla.fenix.search.awesomebar.toSearchProviderState
import org.mozilla.fenix.telemetry.ACTION_SEARCH_ENGINE_SELECTED
import org.mozilla.fenix.telemetry.SOURCE_ADDRESS_BAR
import org.mozilla.fenix.telemetry.SURFACE_BROWSER
import org.mozilla.fenix.telemetry.SURFACE_HOME
import org.mozilla.fenix.utils.Settings

/**
 * [SearchFragmentStore] [Middleware] that will handle the setup of the search UX and related user interactions.
 *
 * @param fragment [Fragment] in which this middleware is used. Will observe it's lifecycle for cleanup.
 * @param engine [Engine] used for speculative connections to search suggestions URLs.
 * @param useCases [UseCases] helping this integrate with other features of the applications.
 * @param nimbusComponents [NimbusComponents] used for accessing Nimbus events to use in telemetry.
 * @param settings [Settings] application settings.
 * @param appStore [AppStore] to sync search related data with.
 * @param browserStore [BrowserStore] to sync search related data with.
 * @param toolbarStore [BrowserToolbarStore] used for querying and updating the toolbar state.
 * @param navController [NavController] to use for navigating to other in-app destinations.
 * @param browsingModeManager [BrowsingModeManager] used for querying and updating the browsing mode.
 * @param shareUseCases [ShareUseCases] used for sharing applications content to other installed applications.
 * @param clipboardHandler [ClipboardHandler] used for copying text to the system clipboard.
 */
@Suppress("LongParameterList")
class FenixSearchMiddleware(
    private val fragment: Fragment,
    private val engine: Engine,
    private val useCases: UseCases,
    private val nimbusComponents: NimbusComponents,
    private val settings: Settings,
    private val appStore: AppStore,
    private val browserStore: BrowserStore,
    private val toolbarStore: BrowserToolbarStore,
    private val navController: NavController,
    private val browsingModeManager: BrowsingModeManager,
    private val shareUseCases: ShareUseCases,
    private val clipboardHandler: ClipboardHandler,
) : Middleware<SearchFragmentState, SearchFragmentAction> {
    private var observeSearchEnginesChangeJob: Job? = null

    @VisibleForTesting internal var suggestionsProvidersBuilder: SearchSuggestionsProvidersBuilder? = null

    @Suppress("LongMethod")
    override fun invoke(
        store: Store<SearchFragmentState, SearchFragmentAction>,
        next: (SearchFragmentAction) -> Unit,
        action: SearchFragmentAction,
    ) {
        when (action) {
            is Init -> {
                next(action)

                suggestionsProvidersBuilder = buildSearchSuggestionsProvider(store)
                updateSearchProviders(store)

                setupSuggestionsProvidersCleanup(store)

                store.dispatch(
                    SearchFragmentAction.UpdateSearchState(
                        browserStore.state.search,
                        isPrivate = browsingModeManager.mode.isPrivate,
                    )
                )
            }

            is SearchStarted -> {
                next(action)

                engine.speculativeCreateSession(action.inPrivateMode)
                suggestionsProvidersBuilder = buildSearchSuggestionsProvider(store)
                setSearchEngine(store, action.selectedSearchEngine, action.isUserSelected)
                observeSearchEngineSelection(store)
            }

            is UpdateQuery -> {
                next(action)

                maybeShowSearchSuggestions(store, action.query)
            }

            is SearchFragmentAction.SearchSourceTabUpdated -> {
                next(action)

                maybeShowSearchSuggestions(store, store.state.query)
            }

            is SearchEnginesSelectedActions -> {
                next(action)

                updateSearchProviders(store)
                maybeShowSearchSuggestions(store, store.state.query)
            }

            is SearchProvidersUpdated -> {
                next(action)

                if (action.providers.isNotEmpty()) {
                    maybeShowSearchSuggestions(store, store.state.query)
                }
            }

            is SuggestionClicked -> {
                val suggestion = action.suggestion
                when {
                    suggestion.flags.contains(AwesomeBar.Suggestion.Flag.HISTORY) -> {
                        History.searchResultTapped.record(NoExtras())
                    }
                    suggestion.flags.contains(AwesomeBar.Suggestion.Flag.BOOKMARK) -> {
                        BookmarksManagement.searchResultTapped.record(NoExtras())
                    }
                }
                browserStore.dispatch(AwesomeBarAction.SuggestionClicked(suggestion))
                toolbarStore.dispatch(BrowserEditToolbarAction.SearchQueryUpdated(BrowserToolbarQuery("")))
                suggestion.onSuggestionClicked?.invoke()
            }

            is SuggestionSelected -> {
                action.suggestion.editSuggestion?.let {
                    toolbarStore.dispatch(
                        BrowserEditToolbarAction.SearchQueryUpdated(
                            query =
                                BrowserToolbarQuery(
                                    if (action.suggestion.flags.any { flag -> flag in URL_SUGGESTION_FLAGS }) {
                                        URLStringUtils.toLosslessEditableUrl(it)
                                    } else {
                                        it
                                    }
                                ),
                            isQueryPrefilled = true,
                        )
                    )
                }
            }

            is PrivateSuggestionsCardAccepted -> {
                updateSearchProviders(store)
            }

            is ReloadCurrentWebsiteClicked -> {
                handleReloadingCurrentWebsite()
            }

            is ShareCurrentWebsiteDetailsClicked -> {
                handleSharingCurrentWebsiteDetails()
            }

            is CopyCurrentWebsiteDetailsClicked -> {
                handleCopyingCurrentWebsiteDetails()
            }

            is EditCurrentWebsiteDetailsClicked -> {
                handleEditingCurrentWebsiteURL()
            }

            else -> next(action)
        }
    }

    /**
     * Observe when the user changes the search engine to use for the current in-progress search and update the
     * suggestions providers used and shown suggestions accordingly.
     */
    private fun observeSearchEngineSelection(store: Store<SearchFragmentState, SearchFragmentAction>) {
        observeSearchEnginesChangeJob?.cancel()
        observeSearchEnginesChangeJob = appStore.observeWhileActive {
            distinctUntilChangedBy { it.searchState.selectedSearchEngine?.searchEngine }
                .collect {
                    it.searchState.selectedSearchEngine?.let {
                        when (it.isUserSelected) {
                            true -> handleSearchShortcutEngineSelectedByUser(store, it.searchEngine)
                            false -> handleSearchShortcutEngineSelected(store, it.searchEngine)
                        }
                    }
                }
        }
    }

    /**
     * Update the search engine to the one selected by the user or fallback to the default search engine.
     *
     * @param store The current [Store] allowing to read and update the search state.
     * @param searchEngine The new [SearchEngine] to be used for new searches or `null` to fallback to fallback to the
     *   default search engine.
     * @param isSelectedByUser isUserSelected Whether or not the search engine was selected by the user.
     */
    private fun setSearchEngine(
        store: Store<SearchFragmentState, SearchFragmentAction>,
        searchEngine: SearchEngine?,
        isSelectedByUser: Boolean,
    ) {
        searchEngine?.let {
            when (isSelectedByUser) {
                true -> handleSearchShortcutEngineSelectedByUser(store, it)
                false -> handleSearchShortcutEngineSelected(store, it)
            }
        } ?: store.state.defaultEngine?.let { handleSearchShortcutEngineSelected(store, it) }
    }

    /** Check if new search suggestions should be shown based on the current search query. */
    private fun maybeShowSearchSuggestions(
        store: Store<SearchFragmentState, SearchFragmentAction>,
        query: String,
    ) {
        val shouldShowTrendingSearches =
            store.state.run {
                (showTrendingSearches || showRecentSearches) &&
                    (searchStartedForCurrentUrl || settings.enableHomepageTrendingRecentSearch)
            }
        val shouldShowSearchSuggestions =
            with(store.state) {
                !isCurrentUrl(query, currentTabData?.url) && query.isNotBlank()
            }
        val shouldShowCurrentWebsiteDetails =
            settings.showAddressBarInFocusMode &&
                query.isBlank() &&
                appStore.state.searchState.sourceTabId?.let { browserStore.state.findTab(it) } != null
        val shouldShowSuggestions =
            shouldShowTrendingSearches || shouldShowSearchSuggestions || shouldShowCurrentWebsiteDetails

        store.dispatch(SearchSuggestionsVisibilityUpdated(shouldShowSuggestions))

        val showPrivatePrompt =
            with(store.state) {
                !settings.showSearchSuggestionsInPrivateOnboardingFinished &&
                    browsingModeManager.mode.isPrivate &&
                    !isSearchSuggestionsFeatureEnabled() &&
                    query.isNotBlank() &&
                    !isCurrentUrl(query, currentTabData?.url)
            }

        store.dispatch(SearchFragmentAction.AllowSearchSuggestionsInPrivateModePrompt(showPrivatePrompt))
    }

    /** Update the search providers used and shown suggestions based on the current search state. */
    private fun updateSearchProviders(store: Store<SearchFragmentState, SearchFragmentAction>) {
        val suggestionsProvidersBuilder = suggestionsProvidersBuilder ?: return
        store.dispatch(
            SearchProvidersUpdated(
                suggestionsProvidersBuilder.getProvidersToAdd(store.state.toSearchProviderState()).toList()
            )
        )
    }

    @VisibleForTesting
    internal fun buildSearchSuggestionsProvider(
        store: Store<SearchFragmentState, SearchFragmentAction>
    ): SearchSuggestionsProvidersBuilder? {
        val uiContext = fragment.context ?: return null

        return SearchSuggestionsProvidersBuilder(
            components = uiContext.components,
            scope = ProcessLifecycleOwner.get().lifecycleScope,
            browsingModeManager = browsingModeManager,
            includeSelectedTab = store.state.tabId == null,
            loadUrlUseCase = loadUrlUseCase(store),
            searchUseCase = searchUseCase(store),
            selectTabUseCase = selectTabUseCase(),
            suggestionsStringsProvider =
                DefaultSuggestionsStringsProvider(
                    uiContext,
                    DefaultSearchEngineProvider(uiContext.components.core.store),
                ),
            suggestionIconProvider = DefaultSuggestionIconProvider(uiContext),
            onSearchEngineSuggestionSelected = ::handleSearchEngineSuggestionClicked,
        )
    }

    @VisibleForTesting
    internal fun loadUrlUseCase(store: Store<SearchFragmentState, SearchFragmentAction>) =
        object : LoadUrlUseCase {
            override fun invoke(
                url: String,
                flags: LoadUrlFlags,
                additionalHeaders: Map<String, String>?,
                originalInput: String?,
            ) {
                openToBrowserAndLoad(
                    url = url,
                    createNewTab =
                        if (settings.enableHomepageAsNewTab) {
                            false
                        } else {
                            store.state.tabId == null
                        },
                    usePrivateMode = browsingModeManager.mode.isPrivate,
                    flags = flags,
                )

                Events.enteredUrl.record(Events.EnteredUrlExtra(autocomplete = false))

                browserStore.dispatch(AwesomeBarAction.EngagementFinished(abandoned = false))
            }
        }

    @VisibleForTesting
    internal fun searchUseCase(store: Store<SearchFragmentState, SearchFragmentAction>) =
        object : SearchUseCase {
            override fun invoke(
                searchTerms: String,
                searchEngine: SearchEngine?,
                parentSessionId: String?,
            ) {
                val searchEngine = store.state.searchEngineSource.searchEngine

                val searchAccessPoint =
                    when (store.state.searchAccessPoint) {
                        MetricsUtils.Source.NONE -> MetricsUtils.Source.SUGGESTION
                        else -> store.state.searchAccessPoint
                    }

                openToBrowserAndLoad(
                    url = searchTerms,
                    createNewTab =
                        if (settings.enableHomepageAsNewTab) {
                            false
                        } else {
                            store.state.tabId == null
                        },
                    usePrivateMode = browsingModeManager.mode.isPrivate,
                    forceSearch = true,
                    searchEngine = searchEngine,
                    accessPoint = searchAccessPoint.toSearchAccessPoint(),
                )

                if (searchEngine != null) {
                    MetricsUtils.recordSearchMetrics(
                        searchEngine,
                        searchEngine == store.state.defaultEngine,
                        searchAccessPoint,
                        nimbusComponents.events,
                    )
                }

                browserStore.dispatch(AwesomeBarAction.EngagementFinished(abandoned = false))
            }
        }

    @VisibleForTesting
    internal fun selectTabUseCase() =
        object : SelectTabUseCase {
            override fun invoke(tabId: String) {
                useCases.tabsUseCases.selectTab(tabId)

                navController.navigate(R.id.browserFragment)

                browserStore.dispatch(AwesomeBarAction.EngagementFinished(abandoned = false))
            }
        }

    private fun openToBrowserAndLoad(
        url: String,
        createNewTab: Boolean,
        usePrivateMode: Boolean,
        forceSearch: Boolean = false,
        searchEngine: SearchEngine? = null,
        flags: LoadUrlFlags = LoadUrlFlags.none(),
        accessPoint: String = SearchAccessPoint.DEFAULT,
    ) {
        navController.navigate(R.id.browserFragment)
        useCases.fenixBrowserUseCases.loadUrlOrSearch(
            searchTermOrURL = url,
            newTab = createNewTab,
            private = usePrivateMode,
            forceSearch = forceSearch,
            searchEngine = searchEngine,
            flags = flags,
            accessPoint = accessPoint,
        )
    }

    /**
     * Handle a search shortcut engine being selected by the user. This will result in using a different set of
     * suggestions providers and showing different search suggestions. The difference between this and
     * [handleSearchShortcutEngineSelected] is that this also records the appropriate telemetry for the user
     * interaction.
     *
     * @param store The store which will provide the state and environment dependencies needed.
     * @param searchEngine The [SearchEngine] to be used for the current in-progress search.
     */
    @VisibleForTesting
    internal fun handleSearchShortcutEngineSelectedByUser(
        store: Store<SearchFragmentState, SearchFragmentAction>,
        searchEngine: SearchEngine,
    ) {
        handleSearchShortcutEngineSelected(store, searchEngine)

        Toolbar.buttonTapped.record(
            Toolbar.ButtonTappedExtra(
                source = SOURCE_ADDRESS_BAR,
                item = ACTION_SEARCH_ENGINE_SELECTED,
                extra = searchEngine.telemetryName(),
                surface = if (store.state.tabId == null) SURFACE_HOME else SURFACE_BROWSER,
            )
        )
    }

    /**
     * Update what search engine to use for the current in-progress search. This will result in using a different set of
     * suggestions providers and showing different search suggestions.
     *
     * @param store The current [Store] allowing to read and update the search state.
     * @param searchEngine The [SearchEngine] to be used for the current in-progress search.
     */
    private fun handleSearchShortcutEngineSelected(
        store: Store<SearchFragmentState, SearchFragmentAction>,
        searchEngine: SearchEngine,
    ) {
        when {
            searchEngine.type == SearchEngine.Type.APPLICATION && searchEngine.id == HISTORY_SEARCH_ENGINE_ID -> {
                store.dispatch(SearchFragmentAction.SearchHistoryEngineSelected(searchEngine))
            }
            searchEngine.type == SearchEngine.Type.APPLICATION && searchEngine.id == BOOKMARKS_SEARCH_ENGINE_ID -> {
                store.dispatch(SearchFragmentAction.SearchBookmarksEngineSelected(searchEngine))
            }
            searchEngine.type == SearchEngine.Type.APPLICATION && searchEngine.id == TABS_SEARCH_ENGINE_ID -> {
                store.dispatch(SearchFragmentAction.SearchTabsEngineSelected(searchEngine))
            }
            searchEngine == store.state.defaultEngine -> {
                store.dispatch(
                    SearchFragmentAction.SearchDefaultEngineSelected(
                        engine = searchEngine,
                        browsingMode = browsingModeManager.mode,
                        settings = settings,
                    )
                )
            }
            else -> {
                store.dispatch(
                    SearchFragmentAction.SearchShortcutEngineSelected(
                        engine = searchEngine,
                        browsingMode = browsingModeManager.mode,
                        settings = settings,
                    )
                )
            }
        }
    }

    private fun handleSearchEngineSuggestionClicked(searchEngine: SearchEngine) {
        appStore.dispatch(SearchEngineSelected(searchEngine, true))
    }

    private inline fun <S : State, A : MVIAction> Store<S, A>.observeWhileActive(
        crossinline observe: suspend (Flow<S>.() -> Unit)
    ): Job = fragment.viewLifecycleOwner.lifecycle.coroutineScope.launch { flow().observe() }

    private fun setupSuggestionsProvidersCleanup(store: Store<SearchFragmentState, SearchFragmentAction>) {
        fragment.viewLifecycleOwner.lifecycle.addObserver(
            object : DefaultLifecycleObserver {
                override fun onDestroy(owner: LifecycleOwner) {
                    // Search providers may keep hard references to lifecycle dependent objects
                    // so we need to reset them when the environment is cleared.
                    store.dispatch(SearchProvidersUpdated(emptyList()))
                }
            }
        )
    }

    /**
     * Check whether search suggestions should be shown in the AwesomeBar.
     *
     * @return `true` if search suggestions should be shown `false` otherwise.
     */
    @VisibleForTesting
    internal fun isSearchSuggestionsFeatureEnabled(): Boolean {
        return when (browsingModeManager.mode) {
            BrowsingMode.Normal -> settings.shouldShowSearchSuggestions
            BrowsingMode.Private ->
                settings.shouldShowSearchSuggestions && settings.shouldShowSearchSuggestionsInPrivate
        }
    }

    private fun handleReloadingCurrentWebsite() {
        appStore.state.searchState.sourceTabId?.let {
            useCases.tabsUseCases.selectTab(it)
            navController.nav(navController.currentDestination?.id, NavGraphDirections.actionGlobalBrowser())
            useCases.sessionUseCases.reload(it)
            browserStore.dispatch(AwesomeBarAction.EngagementFinished(abandoned = true))
        }
    }

    private fun handleSharingCurrentWebsiteDetails() {
        val currentDestination = navController.currentDestination ?: return
        val session = sessionSearchWasStartedFor ?: return
        val url = session.getUrl()

        shareUseCases.shareUrl(
            id = session.id,
            url = url,
            title = session.content.title,
            source = ShareSource.BROWSER_TOOLBAR,
            isPrivate = session.content.private,
            isCustomTab = false,
            navigateToShareFragment = {
                val shareData =
                    arrayOf(ShareData(title = session.content.title, url = url, private = session.content.private))

                navController.nav(
                    id = currentDestination.id,
                    directions =
                        NavGraphDirections.actionGlobalShareFragment(
                            sessionId = session.id,
                            data = shareData,
                            showPage = true,
                        ),
                    navOptions = NavOptions.Builder().setPopUpTo(currentDestination.id, false).build(),
                )
            },
        )
    }

    private fun handleCopyingCurrentWebsiteDetails() {
        val session = sessionSearchWasStartedFor ?: return
        val url = session.getUrl()
        val isPrivate = session.content.private

        if (isPrivate) {
            clipboardHandler.sensitiveText = url
        } else {
            clipboardHandler.text = url
        }

        // Android 13+ shows by default a popup for copied text.
        // Avoid overlapping popups informing the user when the URL is copied to the clipboard.
        // and only show our snackbar when Android will not show an indication by default.
        // See https://developer.android.com/develop/ui/views/touch-and-input/copy-paste#duplicate-notifications).
        if (Build.VERSION.SDK_INT <= Build.VERSION_CODES.S_V2) {
            appStore.dispatch(URLCopiedToClipboard)
        }
    }

    private fun handleEditingCurrentWebsiteURL() {
        val session = sessionSearchWasStartedFor ?: return
        val searchTerms = session.content.searchTerms
        val currentInput =
            when (searchTerms.isEmpty()) {
                true -> session.getUrl()?.let(URLStringUtils::toLosslessEditableUrl)
                else -> searchTerms
            }

        currentInput?.let {
            toolbarStore.dispatch(SearchQueryUpdated(BrowserToolbarQuery(it), true))
        }
    }

    private val sessionSearchWasStartedFor
        get() = browserStore.state.findTab(appStore.state.searchState.sourceTabId ?: "")

    private companion object {
        // Suggestions whose edit text is a browser-owned URL.
        val URL_SUGGESTION_FLAGS =
            setOf(
                AwesomeBar.Suggestion.Flag.BOOKMARK,
                AwesomeBar.Suggestion.Flag.HISTORY,
                AwesomeBar.Suggestion.Flag.OPEN_TAB,
                AwesomeBar.Suggestion.Flag.SYNC_TAB,
            )
    }
}
